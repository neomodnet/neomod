// Copyright (c) 2025, WH, All rights reserved.
#include "config.h"

#ifdef MCENGINE_FEATURE_SOLOUD

#include "SoLoudSoundEngine.h"

#include "MakeDelegateWrapper.h"
#include "SString.h"
#include "SoLoudSound.h"
#include "SoLoudThread.h"

#include "ConVar.h"
#include "Engine.h"
#include "Logging.h"

#include "Environment.h"
#include "ResourceManager.h"

#include <utility>
#include <cstdlib>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_audio.h>

std::unique_ptr<SoLoudThreadWrapper> soloud{nullptr};

// factory
Sound *SoLoudSoundEngine::createSound(std::string filepath, bool stream, bool overlayable, bool loop) {
    return new SoLoudSound(std::move(filepath), stream, overlayable, loop);
}

SoundEngine::OutputDriver SoLoudSoundEngine::getMAorSDLCV() {
    OutputDriver out{OutputDriver::SOLOUD_MA};

    const auto &cvBackend = cv::snd_soloud_backend.getString();

    if(SString::contains_ncase(cvBackend, "sdl")) {
        out = OutputDriver::SOLOUD_SDL;
    } else {
        out = OutputDriver::SOLOUD_MA;
    }

    return out;
}

unsigned int SoLoudSoundEngine::getResamplerFromCV() {
    unsigned int resampler{SoLoud::Soloud::RESAMPLER_LINEAR};

    const auto &cvResampler = cv::snd_soloud_resampler.getString();

    if(SString::contains_ncase(cvResampler, "catmull")) {
        resampler = SoLoud::Soloud::RESAMPLER_CATMULLROM;
    } else if(SString::contains_ncase(cvResampler, "point")) {
        resampler = SoLoud::Soloud::RESAMPLER_POINT;
    } else {
        cv::snd_soloud_resampler.setValue("linear", false);
    }

    return resampler;
}

SoLoudSoundEngine::SoLoudSoundEngine() : SoundEngine() {
    // in WASM headless, use no-op audio backends (because Node.js doesn't have audio)
#ifdef MCENGINE_PLATFORM_WASM
    if(env->isHeadless()) {
        cv::snd_soloud_backend.setValue("SDL3", false);
        setenv("SOLOUD_MINIAUDIO_DRIVER", "null", 1);
    }
#endif

#if SOLOUD_VERSION >= 202512
    {
        static SoLoud::logFunctionType SoLoudLogCB = +[](const char *message, void * /*userdata*/) -> void {
            if(!Environment::getEnvVariable("SOLOUD_DEBUG").empty() ||
               cv::debug_snd.getBool()) {  // otherwise just throw the message away
                // avoid stray newlines
                size_t end_pos = message ? strlen(message) : 0;
                while(end_pos > 0 && (message[end_pos - 1] == '\r' || message[end_pos - 1] == '\n')) {
                    --end_pos;
                }

                logRaw(std::string_view(message, end_pos));
            }
        };

        // both the same for now
        SoLoud::setStdoutLogFunction(SoLoudLogCB, nullptr);
        SoLoud::setStderrLogFunction(SoLoudLogCB, nullptr);
    }
#endif
    if(!soloud) {
        bool threaded = false;
        auto args = env->getLaunchArgs();
        auto threadedString = args["-sound"].value_or("soloud");
        SString::trim_inplace(threadedString);
        SString::lower_inplace(threadedString);
        if(threadedString == "soloud-threaded") {
            threaded = true;
        }
        soloud = std::make_unique<SoLoudThreadWrapper>(threaded);
    }

    cv::snd_freq.setValue(SoLoud::Soloud::AUTO);  // let it be auto-negotiated (the snd_freq callback will adjust if
                                                  // needed, if this is manually set in a config)
    cv::snd_freq.setDefaultDouble(SoLoud::Soloud::AUTO);

    this->iMaxActiveVoices =
        std::clamp<int>(cv::snd_sanity_simultaneous_limit.getInt(), 64,
                        255);  // TODO: lower this minimum (it will crash if more than this many sounds play at once...)

    OUTPUT_DEVICE defaultOutputDevice{.isDefault = true, .driver = getMAorSDLCV()};
    cv::snd_output_device.setValue(defaultOutputDevice.name);
    this->outputDevices.push_back(defaultOutputDevice);
    this->currentOutputDevice = defaultOutputDevice;

    this->mSoloudDevices = {};
    this->bInitSuccess = true;
}

void SoLoudSoundEngine::restart() {
    if(this->bWasBackendEverReady) {
        this->setOutputDeviceInt(this->getWantedDevice(), true);
    } else {
        // if switching backends, we need to reinit from scratch (device enumeration needs refresh)
        UString initName = cv::snd_output_device.getString();
        int initID = -1;
        // if the output device isn't default, use some non-default id, we don't know what id it will end up at
        if(initName != cv::snd_output_device.getDefaultString()) {
            initID = -2;
        }
        this->setOutputDeviceInt(OUTPUT_DEVICE{.id = initID, .name = initName, .driver = getMAorSDLCV()}, true);
        // if constexpr(Env::cfg(OS::WASM)) {
        //     // TODO: this makes no sense, but audio is really messed up unless you restart again after the first init
        //     // weirdly, this happens with either miniaudio or SDL
        //     if(this->bWasBackendEverReady) {
        //         this->restart();
        //     }
        // }
    }
}

bool SoLoudSoundEngine::play(Sound *snd, f32 pan, f32 pitch, f32 playVolume, bool startPaused) {
    if(!this->isReady() || snd == nullptr || !snd->isReady()) return false;

    // @spec: adding 1 here because kiwec changed the calling code in some way that i dont understand yet
    pitch += 1.0f;

    pan = std::clamp<float>(pan, -1.0f, 1.0f);
    pitch = std::clamp<float>(pitch, 0.01f, 2.0f);

    auto *soloudSound = snd->as<SoLoudSound>();
    if(!soloudSound) return false;

    auto existingHandle = soloudSound->getHandle();

    // check if we have a non-stale voice handle for the most recently played instance
    if(existingHandle != 0 && !soloud->isValidVoiceHandle(existingHandle)) {
        existingHandle = 0;
        soloudSound->handle = 0;
    }

    if(existingHandle != 0 && !soloudSound->isOverlayable()) {
        // if we do and it's not overlayable, update this last instance
        return this->updateExistingSound(soloudSound, existingHandle, pan, pitch, playVolume, startPaused);
    } else {
        // otherwise try playing a new instance
        return this->playSound(soloudSound, pan, pitch, playVolume, startPaused);
    }
}

bool SoLoudSoundEngine::updateExistingSound(SoLoudSound *soloudSound, SOUNDHANDLE handle, f32 pan, f32 pitch,
                                            f32 playVolume, bool startPaused) {
    assert(soloudSound);

    // TODO(spec): don't do pitch += 1.0f; in play(), and do soundEngine->play(music, 0, music->getPitch())
    // for both bass/soloud
    // workaround for now
    if(!soloudSound->isStream()) {
        if(soloudSound->getPitch() != pitch) {
            soloudSound->setPitch(pitch);
        }

        if(soloudSound->getPan() != pan) {
            soloudSound->setPan(pan);
        }
    }

    soloudSound->setHandleVolume(handle, soloudSound->getBaseVolume() * playVolume);

    // update existing handle in cache with new params
    PlaybackParams newParams{.pan = pan, .pitch = pitch, .volume = playVolume};
    soloudSound->activeHandleCache[handle] = newParams;

    // make sure it's not paused
    if(!startPaused) {
        soloud->setPause(handle, false);
        soloudSound->setLastPlayTime(engine->getTime());
    }

    logIfCV(debug_snd, "handle was already valid, for non-overlayable sound {}", soloudSound->getName());
    return true;
}

bool SoLoudSoundEngine::playSound(SoLoudSound *soloudSound, f32 pan, f32 pitch, f32 playVolume, bool startPaused) {
    assert(soloudSound);

    // check if we should allow playing this frame
    const bool allowPlayFrame =
        startPaused || (!soloudSound->isOverlayable() || !cv::snd_restrict_play_frame.getBool() ||
                        engine->getTime() > soloudSound->getLastPlayTime());
    if(!allowPlayFrame) return false;

    logIfCV(debug_snd,
            "SoLoudSoundEngine: Attempting to play {:s} (stream={:d}) with speed={:f}, pitch={:f}, playVolume={:f} "
            "(effective volume={:f})",
            soloudSound->sFilePath, soloudSound->bStream ? 1 : 0, soloudSound->fSpeed, pitch, playVolume,
            soloudSound->fBaseVolume * playVolume);

    // play the sound with appropriate method
    SOUNDHANDLE handle = 0;

    if(soloudSound->bStream) {
        // reset these, because they're "sticky" properties
        soloudSound->setPitch(pitch);
        soloudSound->setPan(pan);

        // streaming audio (music) - play SLFXStream directly (it handles SoundTouch internally)
        // start it at 0 volume and fade it in when we play it (to avoid clicks/pops)
        handle = soloud->play(*soloudSound->audioSource, 0, pan, true /* paused */);
        if(handle)
            // protect the music channel (don't let it get interrupted when many sounds play back at once)
            // NOTE: this doesn't seem to work 100% properly, not sure why... need to setMaxActiveVoiceCount
            // higher than the default 16 as a workaround, otherwise rapidly overlapping samples like from
            // buzzsliders can cause glitches in music playback
            soloud->setProtectVoice(handle, true);
    } else {
        // non-streams don't go through the SoLoudFX wrapper
        handle = soloud->play(*soloudSound->audioSource, soloudSound->fBaseVolume * playVolume, pan, true /* paused */);
    }

    // finalize playback
    if(handle == 0) {
        logIfCV(debug_snd, "SoLoudSoundEngine: Failed to play sound {:s}", soloudSound->sFilePath);
        return false;
    }

    // store the handle and mark playback time
    soloudSound->handle = handle;

    PlaybackParams newInstance{.pan = pan, .pitch = pitch, .volume = playVolume};
    soloudSound->addActiveInstance(handle, newInstance);

    const bool debug = cv::debug_snd.getBool();

    if(!soloudSound->bStream) {
        // calculate final pitch by combining all pitch modifiers
        float playbackPitch = pitch * soloudSound->getPitch() * soloudSound->getSpeed();

        // set relative play speed (affects both pitch and speed)
        soloud->setRelativePlaySpeed(handle, playbackPitch);

        logIf(debug,
              "SoLoudSoundEngine: {} non-streaming audio with playbackPitch={:f} (pitch={:f} * "
              "soundPitch={:f}, soundSpeed={:f})",
              startPaused ? "enqueuing" : "playing", playbackPitch, pitch, soloudSound->getPitch(),
              soloudSound->getSpeed());
    }

    logIf(debug && soloudSound->bStream,
          "SoLoudSoundEngine: {} streaming audio through SLFXStream with speed={:f}, pitch={:f}",
          startPaused ? "enqueuing" : "playing", soloudSound->getSpeed(), soloudSound->getPitch());

    // exit early if we don't want to play yet
    if(startPaused) return true;

    // fade it in if it's a stream (since we started it paused with 0 volume)
    if(soloudSound->bStream) {
        this->setVolumeGradual(handle, soloudSound->fBaseVolume * playVolume);
    }

    // now unpause it
    soloud->setPause(handle, false);

    soloudSound->setLastPlayTime(engine->getTime());

    return true;
}

void SoLoudSoundEngine::pause(Sound *snd) {
    if(!this->isReady() || snd == nullptr || !snd->isReady()) return;

    auto *soloudSound = snd->as<SoLoudSound>();
    if(!soloudSound || soloudSound->handle == 0) return;

    soloud->setPause(soloudSound->handle, true);
    soloudSound->setLastPlayTime(0.0);
}

void SoLoudSoundEngine::stop(Sound *snd) {
    if(!this->isReady() || snd == nullptr || !snd->isReady()) return;

    auto *soloudSound = snd->as<SoLoudSound>();
    if(!soloudSound || soloudSound->handle == 0) return;

    soloudSound->setPositionMS(0);
    soloudSound->setLastPlayTime(0.0);
    soloudSound->setFrequency(0.0);
    soloud->stop(soloudSound->handle);
    soloudSound->handle = 0;
}

void SoLoudSoundEngine::setOutputDeviceByName(std::string_view desiredDeviceName) {
    for(const auto &device : this->outputDevices) {
        if(device.name.toUtf8() == desiredDeviceName) {
            this->setOutputDeviceInt(device);
            return;
        }
    }

    debugLog("couldn't find output device \"{:s}\"!", desiredDeviceName);
    this->initializeOutputDevice(this->getDefaultDevice());  // initialize default
}

void SoLoudSoundEngine::setOutputDevice(const SoundEngine::OUTPUT_DEVICE &device) {
    this->setOutputDeviceInt(device, false);
}

void SoLoudSoundEngine::updateLastDevice() {
    if(this->currentOutputDevice.driver == OutputDriver::SOLOUD_MA) {
        this->lastMADevice = this->currentOutputDevice;
    } else if(this->currentOutputDevice.driver == OutputDriver::SOLOUD_SDL) {
        this->lastSDLDevice = this->currentOutputDevice;
    }
}

// this is a stupid amount of code to do something very simple (change from a device with (Exclusive)<->(Shared) suffix)
bool SoLoudSoundEngine::switchShareModes(const std::optional<OUTPUT_DEVICE> &toKnownDevice) {
    if constexpr(!Env::cfg(OS::WINDOWS)) return false;
    if(this->currentOutputDevice.driver != OutputDriver::SOLOUD_MA) return false;
    if(this->outputDevices.size() < 2 || this->mSoloudDevices.size() < 2) return false;

    int currentSharedPos = -1, currentExclusivePos = -1;
    if((currentSharedPos = this->currentOutputDevice.name.find("(Shared)")) == -1 &&
       (currentExclusivePos = this->currentOutputDevice.name.find("(Exclusive)")) == -1)
        return false;

    bool toShared = currentExclusivePos != -1;
    bool toExclusive = currentSharedPos != -1;

    SoLoud::DeviceInfo desiredSLDevice;
    OUTPUT_DEVICE desiredDevice;
    if(toKnownDevice.has_value()) {
        desiredDevice = toKnownDevice.value();
        if(desiredDevice.driver == OutputDriver::SOLOUD_MA && this->mSoloudDevices.contains(desiredDevice.id)) {
            desiredSLDevice = this->mSoloudDevices[desiredDevice.id];
        } else {
            // exit early, soloud device map doesn't contain our desired id or we're going to SDL
            return false;
        }
    } else {
        bool foundPair = false;

        std::string_view fromPfx = this->currentOutputDevice.name.utf8View();
        fromPfx = fromPfx.substr(0, fromPfx.find(toShared ? "(Exclusive)" : "(Shared)"));
        if(fromPfx.empty()) {
            return false;  // wtf? impossible
        }

        // from current device
        for(const auto &dev : this->outputDevices) {
            if(dev.id == this->currentOutputDevice.id) continue;  // skip same device
            if(dev.id == -1) continue;                            // skip default device

            const auto &slDevIt = this->mSoloudDevices.find(dev.id);
            if(slDevIt == this->mSoloudDevices.end()) continue;  // not in soloud devices map, somehow

            const auto &[slID, slDev] = *slDevIt;
            if(slDev.isExclusive && toShared) continue;  // skip exclusive->exclusive and shared->shared possibilities
            if(!slDev.isExclusive && toExclusive) continue;

            std::string_view toDevName{slDev.name.data(), strlen(slDev.name.data())};

            std::string_view toPfx = toDevName.substr(0, toDevName.find(toShared ? "(Shared)" : "(Exclusive)"));

            if(toPfx.empty()) {
                continue;
                // keep looking
            }

            if(fromPfx == toPfx) {
                desiredDevice = dev;
                desiredSLDevice = slDevIt->second;
                foundPair = true;
                break;
            }
        }
        if(!foundPair) return false;
    }

    if(soloud->setDevice(&desiredSLDevice.identifier[0]) == SoLoud::SO_NO_ERROR) {
        this->currentOutputDevice = desiredDevice;
        logIfCV(debug_snd, "switched share modes to {}", desiredDevice.id);
        return true;
    } else {
        debugLog("SoundEngine: Tried to switch to {} mode, but couldn't.", toShared ? "shared" : "exclusive");
    }

    return false;
}

void SoLoudSoundEngine::onFocusGained() {
    if(cv::snd_disable_exclusive_unfocused.getBool() && cv::snd_soloud_prefer_exclusive.getBool() &&
       this->currentOutputDevice.name.find("(Shared)") != -1) {
        this->switchShareModes();
    }
}

void SoLoudSoundEngine::onFocusLost() {
    if(cv::snd_disable_exclusive_unfocused.getBool() && cv::snd_soloud_prefer_exclusive.getBool() &&
       this->currentOutputDevice.name.find("(Exclusive)") != -1) {
        this->switchShareModes();
    }
}

bool SoLoudSoundEngine::setOutputDeviceInt(const SoundEngine::OUTPUT_DEVICE &desiredDevice, bool force) {
    auto dumpOutputDevices = [&]() {
        const auto &curDev = this->currentOutputDevice;
        debugLog("CURRENT id: {} drv: {} enbl: {} def: {} init: {} name: {}", curDev.id, (u8)curDev.driver,
                 curDev.enabled, curDev.isDefault, curDev.isInit, curDev.name);
        for(const auto &dev : this->outputDevices) {
            debugLog("OUR id: {} name: {} drv: {} enbl: {} def: {} init: {}", dev.id, dev.name, (u8)dev.driver,
                     dev.enabled, dev.isDefault, dev.isInit, dev.name);
        }
        for(const auto &[id, dev] : this->mSoloudDevices) {
            debugLog("SOLOUD id: {} name: {} def: {} excl: {} identifier: {}", id,
                     std::string_view{dev.name.data(), strlen(dev.name.data())}, dev.isDefault, dev.isExclusive,
                     std::string_view{dev.identifier.data(), strlen(dev.identifier.data())});
        }
    };

    auto onOut = [&](bool ret) -> bool {
        cv::snd_output_device.setValue(this->currentOutputDevice.name, false);
        this->updateLastDevice();
        if(soloud) soloud->setGlobalVolume(this->fMasterVolume);
        if(cv::debug_snd.getBool()) dumpOutputDevices();
        return ret;
    };

    if(force || !this->bReady || !this->bWasBackendEverReady) {
        // TODO: This is blocking main thread, can freeze for a long time on some sound cards
        auto previous = this->currentOutputDevice;
        if(!this->initializeOutputDevice(desiredDevice)) {
            if((desiredDevice.id == previous.id && desiredDevice.driver == previous.driver) ||
               !this->initializeOutputDevice(previous)) {
                // We failed to reinitialize the device, don't start an infinite loop, just give up
                this->currentOutputDevice = {};
                return onOut(false);
            }
        }
        return onOut(true);
    }

    // non-forced device change, post-init
    // first, check if we're only changing the share mode (miniaudio+windows only)
    if(this->switchShareModes(desiredDevice)) {
        if(const auto &it = this->mSoloudDevices.find(desiredDevice.id); it != this->mSoloudDevices.end()) {
            // since this was a manual change, update the preference convar to reflect the choice
            cv::snd_soloud_prefer_exclusive.setValue(it->second.isExclusive);
        }
        return onOut(true);
    }

    // otherwise, full reinit
    for(const auto &device : this->outputDevices) {
        if(device.name == desiredDevice.name) {
            if(device.id != this->currentOutputDevice.id &&
               !(device.isDefault && this->currentOutputDevice.isDefault)) {
                auto previous = this->currentOutputDevice;
                logIfCV(debug_snd, "switching devices, current id {} default {}, new id {} default {}", previous.id,
                        previous.isDefault, device.id, device.isDefault);
                if(!this->initializeOutputDevice(desiredDevice)) this->initializeOutputDevice(previous);
            } else {
                // multiple ids can map to the same device (e.g. default device), just update the name
                this->currentOutputDevice.name = device.name;

                debugLog("\"{:s}\" already is the current device.", desiredDevice.name);
                return onOut(false);
            }

            return onOut(true);
        }
    }

    debugLog("couldn't find output device \"{:s}\"!", desiredDevice.name);
    return onOut(false);
}

// delay setting these until after everything is fully init, so we don't restart multiple times while reading config
void SoLoudSoundEngine::allowInternalCallbacks() {
    // convar callbacks
    cv::snd_freq.setCallback(SA::MakeDelegate<&SoLoudSoundEngine::restart>(this));
    cv::cmd::snd_restart.setCallback(SA::MakeDelegate<&SoLoudSoundEngine::restart>(this));

    static auto backendSwitchCB = [](std::string_view arg) -> void {
        if(!soundEngine || soundEngine->getTypeId() != SndEngineType::SOLOUD) return;

        auto *enginePtr = static_cast<SoLoudSoundEngine *>(soundEngine.get());
        const auto curDriver = enginePtr->getOutputDriverType();

        const bool nowSDL = SString::contains_ncase(arg, "sdl"sv);
        // don't do anything if we're already ready with the same output driver
        if(enginePtr->bWasBackendEverReady &&
           ((nowSDL && curDriver == OutputDriver::SOLOUD_SDL) || (!nowSDL && curDriver == OutputDriver::SOLOUD_MA)))
            return;

        // needed due to different device enumeration between backends
        enginePtr->bWasBackendEverReady = false;
        enginePtr->restart();
    };

    cv::snd_soloud_backend.setCallback(backendSwitchCB);
    cv::snd_sanity_simultaneous_limit.setCallback(SA::MakeDelegate<&SoLoudSoundEngine::onMaxActiveChange>(this));
    cv::snd_output_device.setCallback(SA::MakeDelegate<&SoLoudSoundEngine::setOutputDeviceByName>(this));
    cv::snd_soloud_resampler.setCallback(SA::MakeDelegate<&SoLoudSoundEngine::restart>(this));

    bool doRestart = !this->bWasBackendEverReady ||          //
                     !cv::snd_freq.isDefault() ||            //
                     !cv::snd_soloud_backend.isDefault() ||  //
                     !cv::snd_soloud_resampler.isDefault();

    if(doRestart) {
        this->restart();
    }

    bool doMaxActive =
        cv::snd_sanity_simultaneous_limit.getDefaultFloat() != cv::snd_sanity_simultaneous_limit.getFloat();
    if(doMaxActive) {
        this->onMaxActiveChange(cv::snd_sanity_simultaneous_limit.getFloat());
    }

    // if we restarted already, then we already set the output device to the desired one
    bool doChangeOutput = !doRestart && !cv::snd_output_device.isDefault();
    if(doChangeOutput) {
        this->setOutputDeviceByName(cv::snd_output_device.getString());
    }
}

SoLoudSoundEngine::~SoLoudSoundEngine() {
    if(!!this->restartCBs[0]) {
        this->restartCBs[0]();
    }
    if(soloud && this->isReady()) {
        soloud->deinit();
    }
    soloud.reset();
    cv::snd_freq.reset();
    cv::cmd::snd_restart.reset();
    cv::snd_soloud_backend.reset();
    cv::snd_sanity_simultaneous_limit.reset();
    cv::snd_output_device.reset();
    cv::snd_soloud_resampler.reset();
}

void SoLoudSoundEngine::setMasterVolume(float volume) {
    if(!this->isReady()) return;

    this->fMasterVolume = std::clamp<float>(volume, 0.0f, 1.0f);

    // if (cv::debug_snd.getBool())
    // 	debugLog("setting global volume to {:f}", fVolume);
    soloud->setGlobalVolume(this->fMasterVolume);
}

void SoLoudSoundEngine::setVolumeGradual(SOUNDHANDLE handle, float targetVol, float fadeTimeMs) {
    if(!this->isReady() || handle == 0 || !soloud->isValidVoiceHandle(handle)) return;

    soloud->setVolume(handle, 0.0f);

    logIfCV(debug_snd, "fading in to {:.2f}", targetVol);

    soloud->fadeVolume(handle, targetVol, fadeTimeMs / 1000.0f);
}

void SoLoudSoundEngine::updateOutputDevices(bool printInfo) {
    if(!this->isReady())  // soloud needs to be initialized first
        return;

    using namespace SoLoud;

    const auto currentDriver = getMAorSDLCV();

    // reset these, because if the backend changed, it might enumerate devices differently
    this->mSoloudDevices.clear();
    this->outputDevices.clear();
    this->outputDevices.push_back(
        OUTPUT_DEVICE{.isDefault = true, .driver = currentDriver});  // re-add dummy default device

    debugLog("SoundEngine: Using SoLoud backend: {:s}", cv::snd_soloud_backend.getString());
    DeviceInfo currentDevice{}, fallbackDevice{};

    result res = soloud->getCurrentDevice(&currentDevice);
    if(res == SO_NO_ERROR) {
        // in case we can't go through devices to find the real default, use the current one as the default
        fallbackDevice = currentDevice;
        debugLog("SoundEngine: Current device: {} (Default: {:s})", &currentDevice.name[0],
                 currentDevice.isDefault ? "Yes" : "No");
    } else {
        fallbackDevice = {.name = {"Unavailable"},
                          .identifier = {""},
                          .isDefault = true,
                          .isExclusive = false,
                          .nativeDeviceInfo = nullptr};
        this->mSoloudDevices[-1] = fallbackDevice;
    }

    DeviceInfo *devicearray{};
    unsigned int deviceCount = 0;
    res = soloud->enumerateDevices(&devicearray, &deviceCount);

    if(res == SO_NO_ERROR) {
        // sort to keep them in the same order for each query
        std::vector<DeviceInfo> devices{devicearray, devicearray + deviceCount};
        std::ranges::stable_sort(
            devices, [](const char *a, const char *b) -> bool { return strcasecmp(a, b) < 0; },
            [](const DeviceInfo &di) -> const char * { return &di.name[0]; });

        for(int d = 0; d < devices.size(); d++) {
            if(printInfo) {
                debugLog("SoundEngine: Device {}: {} (Default: {:s})", d, &devices[d].name[0],
                         devices[d].isDefault ? "Yes" : "No");
            }

            UString originalDeviceName{&devices[d].name[0]};
            OUTPUT_DEVICE soundDevice;
            soundDevice.id = d;
            soundDevice.name = originalDeviceName;
            soundDevice.enabled = true;
            soundDevice.isDefault = devices[d].isDefault;
            soundDevice.driver = currentDriver;

            // avoid duplicate names
            int duplicateNameCounter = 2;
            while(true) {
                bool foundDuplicateName = false;
                for(const auto &existingDevice : this->outputDevices) {
                    if(existingDevice.name == soundDevice.name) {
                        foundDuplicateName = true;
                        soundDevice.name = originalDeviceName;
                        soundDevice.name.append(fmt::format(" ({})", duplicateNameCounter));
                        duplicateNameCounter++;
                        break;
                    }
                }

                if(!foundDuplicateName) break;
            }
            logIfCV(debug_snd, "added device id {} name {} iteration (d) {}", soundDevice.id, soundDevice.name, d);

            // SDL3 backend has a special "default device", replace the engine default with that one and don't add it
            if(soundDevice.isDefault && soundDevice.name.find("Default Playback Device") != -1) {
                soundDevice.id = -1;
                this->outputDevices[0] = soundDevice;
                this->mSoloudDevices[-1] = {devices[d]};
            } else {
                // otherwise add it as a new device with a real id
                this->outputDevices.push_back(soundDevice);
                if(soundDevice.isDefault) {
                    this->mSoloudDevices[-1] = {devices[d]};
                }
                this->mSoloudDevices[d] = {devices[d]};
            }
        }
    } else {
        // failed enumeration, use fallback
        this->mSoloudDevices[-1] = fallbackDevice;
    }
}

bool SoLoudSoundEngine::initializeOutputDevice(const OUTPUT_DEVICE &device) {
    // run callbacks pt. 1
    if(this->restartCBs[0] != nullptr) this->restartCBs[0]();
    debugLog("id {} name {}", device.id, device.name);

    // cleanup potential previous device
    if(this->isReady()) {
        // this isn't technically required, but might it be, if audio device initialization hangs and we have to detach the soloud thread
        // should be fixable in soloud itself, probably
        if(soloud->isThreaded()) {
            for(auto *soundRes : resourceManager->getSounds()) {
                soundRes->release();
            }
        }
        soloud->deinit();
        this->bReady = false;
    }

    auto backend = (getMAorSDLCV() == OutputDriver::SOLOUD_MA) ? SoLoud::Soloud::MINIAUDIO : SoLoud::Soloud::SDL3;

    // roundoff clipping alters/"damages" the waveform, but it sounds weird without it
    unsigned int flags = SoLoud::Soloud::CLIP_ROUNDOFF; /* | SoLoud::Soloud::NO_FPU_REGISTER_CHANGE; */
    if((backend == SoLoud::Soloud::MINIAUDIO) &&
       ((device.name.find("(Exclusive)") != -1) || cv::snd_soloud_prefer_exclusive.getBool())) {
        flags |= SoLoud::Soloud::INIT_EXCLUSIVE;
    }

    unsigned int sampleRate =
        (cv::snd_freq.getVal<unsigned int>() == static_cast<unsigned int>(cv::snd_freq.getDefaultFloat())
             ? (unsigned int)SoLoud::Soloud::AUTO
             : cv::snd_freq.getVal<unsigned int>());
    if(sampleRate < 22500 || sampleRate > 192000) sampleRate = SoLoud::Soloud::AUTO;

    // WASM: browser complains if buffer size isn't explicitly a power of 2, so just set it to a power of 2 here
    // (512 is quite low but seems okay on my hardware)
    // TODO: maybe it could be lower? users can adjust with ingame convar if they want, for now
    unsigned int bufferSize = Env::cfg(OS::WASM)
                                  ? 512
                                  : (cv::snd_soloud_buffer.getVal<unsigned int>() ==
                                             static_cast<unsigned int>(cv::snd_soloud_buffer.getDefaultFloat())
                                         ? (unsigned int)SoLoud::Soloud::AUTO
                                         : cv::snd_soloud_buffer.getVal<unsigned int>());
    if(bufferSize > 2048) bufferSize = SoLoud::Soloud::AUTO;

    // use stereo output
    constexpr unsigned int channels = 2;

    // setup some SDL hints in case the SDL backend is used
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DEVICE_STREAM_NAME, PACKAGE_NAME, SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DEVICE_STREAM_ROLE, "game", SDL_HINT_OVERRIDE);

    // initialize a new soloud instance
    SoLoud::result result = SoLoud::UNKNOWN_ERROR;
    // try both backends, first with the one we chose, then the other one if that timed out/failed
    for(const auto tryBackend :
        std::array{backend, backend == SoLoud::Soloud::SDL3 ? SoLoud::Soloud::MINIAUDIO : SoLoud::Soloud::SDL3}) {
        result = soloud->init(flags, tryBackend, sampleRate, bufferSize, channels);
        backend = tryBackend;
        if(result == SoLoud::SO_NO_ERROR) {
            break;
        } else {
            debugLog("SoundEngine: The {} output backend failed to initialize, trying another one...",
                     cv::snd_soloud_backend.getString());
        }
    }

    if(result != SoLoud::SO_NO_ERROR) {
        this->bReady = false;
        engine->showMessageError("Sound Error", fmt::format("SoLoud::Soloud::init() failed ({})!", result));
        return false;
    }

    this->bReady = true;

    {
        // populate devices array and set the desired output
        this->updateOutputDevices(true);

        OUTPUT_DEVICE desiredDev = device;
        if(desiredDev.id == -2 || !this->bWasBackendEverReady) {
            // try looking up the desired device in the output device list for fresh init backends
            desiredDev.id = -1;  // set it to default -1 first, in case we don't find it

            if(backend == SoLoud::Soloud::MINIAUDIO && this->lastMADevice.has_value()) {
                desiredDev = this->lastMADevice.value();
            } else if(backend == SoLoud::Soloud::SDL3 && this->lastSDLDevice.has_value()) {
                desiredDev = this->lastSDLDevice.value();
            }

            for(const auto &[id, enumeratedDev] : this->mSoloudDevices) {
                if(strncasecmp(desiredDev.name.toUtf8(), enumeratedDev.name.data(),
                               std::min<size_t>(static_cast<size_t>(desiredDev.name.lengthUtf8()),
                                                enumeratedDev.name.size())) == 0) {
                    desiredDev.id = id;
                    break;
                }
            }
        }

        if(desiredDev.id != this->currentOutputDevice.id && this->outputDevices.size() > 1 &&
           this->mSoloudDevices[desiredDev.id].identifier !=
               this->mSoloudDevices[this->currentOutputDevice.id].identifier) {
            // set the actual desired device, after we enumerated things
            if(soloud->setDevice(&this->mSoloudDevices[desiredDev.id].identifier[0]) != SoLoud::SO_NO_ERROR) {
                // reset to default
                debugLog("resetting to default, setting to {} failed", desiredDev.name);
                this->currentOutputDevice = desiredDev = this->outputDevices[0];
                soloud->setDevice(&this->mSoloudDevices[desiredDev.id].identifier[0]);
            }
        }

        if(Env::cfg(OS::WINDOWS) && backend == SoLoud::Soloud::MINIAUDIO) {
            // remember this setting, for switching between SDL/non-WASAPI output backends (which don't support exclusive mode)
            SoLoud::DeviceInfo currentDevice{};
            SoLoud::result res = soloud->getCurrentDevice(&currentDevice);

            if(res == SoLoud::SO_NO_ERROR) {
                const bool nowExclusive = currentDevice.isExclusive;
                cv::snd_soloud_prefer_exclusive.setValue(nowExclusive);

                if(nowExclusive && desiredDev.name.find("(Exclusive)") == -1) {
                    // don't use "Default" device if we switched from SDL to MiniAudio and got an exclusive device, find the full device name mapped to default
                    // (sigh...)

                    // get actual name/device in our devices vector
                    const std::string_view devstring{currentDevice.name.data(), strlen(currentDevice.name.data())};
                    const auto &devVectorIt =
                        std::ranges::find(this->outputDevices, devstring,
                                          [](const auto &devstruct) { return devstruct.name.utf8View(); });
                    if(devVectorIt != this->outputDevices.end()) {
                        desiredDev = *devVectorIt;
                    }
                }
            }
        }

        // update actual current device now, after all that BS
        this->currentOutputDevice = desiredDev;
        if(UString{&this->mSoloudDevices[desiredDev.id].name[0]}.find("Default Playback Device") != -1) {
            // replace engine default device name (e.g. Default Playback Device, for SDL)
            this->currentOutputDevice.name = {&this->mSoloudDevices[desiredDev.id].name[0]};
        }

        if(this->currentOutputDevice.isDefault && this->currentOutputDevice.id == -1) {
            // update "fake" default convar string (avoid saving to configs)
            cv::snd_output_device.setDefaultString(this->currentOutputDevice.name.toUtf8());
        }

        cv::snd_output_device.setValue(this->currentOutputDevice.name, false);
    }

    this->updateLastDevice();
    this->bWasBackendEverReady = true;

    // it's 0.95 by default, for some reason
    soloud->setPostClipScaler(0.99f);

    // it's LINEAR by default
    soloud->setMainResampler(getResamplerFromCV());

    cv::snd_freq.setValue(soloud->getBackendSamplerate(),
                          false);  // set the cvar to match the actual output sample rate (without running callbacks)
    cv::snd_soloud_backend.setValue(soloud->getBackendString(), false);  // ditto
    if(cv::snd_soloud_buffer.getFloat() != cv::snd_soloud_buffer.getDefaultFloat())
        cv::snd_soloud_buffer.setValue(soloud->getBackendBufferSize(),
                                       false);  // ditto (but only if explicitly non-default was requested already)

    this->onMaxActiveChange(cv::snd_sanity_simultaneous_limit.getFloat());

    debugLog(
        "SoundEngine: Initialized SoLoud ({}) with output device = \"{:s}\" flags: 0x{:x}, backend: {:s}, sampleRate: "
        "{}, "
        "bufferSize: {}, channels: {}, resampler: {}",
        soloud->isThreaded() ? "multi-threaded" : "single-thread", this->currentOutputDevice.name.toUtf8(),
        static_cast<unsigned int>(flags), soloud->getBackendString(), soloud->getBackendSamplerate(),
        soloud->getBackendBufferSize(), soloud->getBackendChannels(), cv::snd_soloud_resampler.getString());

    {
        SoLoud::DeviceInfo inf{};
        soloud->getCurrentDevice(&inf);
        // sanity...
        logIfCV(debug_snd, "ACTUAL current soloud device: {}",
                std::string_view{inf.name.data(), strlen(inf.name.data())});
    }

    // init global volume
    this->setMasterVolume(this->fMasterVolume);

    // run callbacks pt. 2
    if(this->restartCBs[1] != nullptr) {
        this->restartCBs[1]();
    }
    return true;
}

void SoLoudSoundEngine::onMaxActiveChange(float newMax) {
    if(!soloud || !this->isReady()) return;
    const auto desired = std::clamp<unsigned int>(static_cast<unsigned int>(newMax), 64, 255);
    if(std::cmp_not_equal(soloud->getMaxActiveVoiceCount(), desired)) {
        SoLoud::result res = soloud->setMaxActiveVoiceCount(desired);
        if(res != SoLoud::SO_NO_ERROR) debugLog("SoundEngine WARNING: failed to setMaxActiveVoiceCount ({})", res);
    }
    this->iMaxActiveVoices = static_cast<int>(soloud->getMaxActiveVoiceCount());
    cv::snd_sanity_simultaneous_limit.setValue(this->iMaxActiveVoices, false);  // no infinite callback loop
}

#endif  // MCENGINE_FEATURE_SOLOUD
