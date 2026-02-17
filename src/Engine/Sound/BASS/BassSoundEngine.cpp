// Copyright (c) 2014, PG, All rights reserved.
#include "config.h"

#ifdef MCENGINE_FEATURE_BASS

#include "BassManager.h"
#include "BassSoundEngine.h"
#include "BassSound.h"
#include "ConVar.h"
#include "Engine.h"
#include "ResourceManager.h"
#include "Sound.h"
#include "Timing.h"
#include "Logging.h"
#include "App.h"

#include <cmath>
#include <utility>

// factory
Sound *BassSoundEngine::createSound(std::string filepath, bool stream, bool overlayable, bool loop) {
    return new BassSound(std::move(filepath), stream, overlayable, loop);
}

#ifdef MCENGINE_PLATFORM_WINDOWS

uint32_t BassSoundEngine::ASIO_clamp(const BASS_ASIO_INFO &info, uint32_t buflen) {
    if(buflen == (uint32_t)-1) return info.bufpref;
    if(buflen < info.bufmin) return info.bufmin;
    if(buflen > info.bufmax) return info.bufmax;
    if(info.bufgran == 0) return buflen;

    if(info.bufgran == -1) {
        // Buffer lengths are only allowed in powers of 2
        for(int oksize = info.bufmin; oksize <= info.bufmax; oksize *= 2) {
            if(oksize == buflen) {
                return buflen;
            } else if(oksize > buflen) {
                oksize /= 2;
                return oksize;
            }
        }

        // Unreachable
        return info.bufpref;
    } else {
        // Buffer lengths are only allowed in multiples of info.bufgran
        buflen -= info.bufmin;
        buflen = (buflen / info.bufgran) * info.bufgran;
        buflen += info.bufmin;
        return buflen;
    }
}
#endif

BassSoundEngine::BassSoundEngine() : SoundEngine() {
    if(!BassManager::init())  // this checks the library versions as well
    {
        engine->showMessageErrorFatal("Fatal Sound Error",
                                      fmt::format("Failed to load BASS feature: {:s} !", BassManager::getFailedLoad()));
        return;
    }

    BASS_SetConfig(BASS_CONFIG_BUFFER, 100);

    // all beatmaps timed to non-iTunesSMPB + 529 sample deletion offsets on old dlls pre 2015
    BASS_SetConfig(BASS_CONFIG_MP3_OLDGAPS, 1);

    // avoids lag/jitter in BASS_Mixer_ChannelGetPosition() shortly after a BASS_ChannelPlay() after loading/silence
    BASS_SetConfig(BASS_CONFIG_DEV_NONSTOP, 1);

    // if set to 1, increases sample playback latency by 10 ms
    BASS_SetConfig(BASS_CONFIG_VISTA_TRUEPOS, 0);
    this->currentOutputDevice = {
        .id = 0,
        .enabled = true,
        .isDefault = true,
        .name = "No sound",
        .driver = OutputDriver::NONE,
    };

    this->bInitSuccess = true;
}

BassSoundEngine::~BassSoundEngine() {
    if(this->bInitSuccess) {
        this->shutdown();
    }
    BassManager::cleanup();
}

void BassSoundEngine::updateOutputDevices(bool /*printInfo*/) {
    this->outputDevices.clear();

    BASS_DEVICEINFO deviceInfo;
    for(int d = 0; (BASS_GetDeviceInfo(d, &deviceInfo) == TRUE); d++) {
        const bool isEnabled = (deviceInfo.flags & BASS_DEVICE_ENABLED);
        const bool isDefault = (deviceInfo.flags & BASS_DEVICE_DEFAULT);

        SoundEngine::OUTPUT_DEVICE soundDevice;
        soundDevice.id = d;
        soundDevice.name = deviceInfo.name;
        soundDevice.enabled = isEnabled;
        soundDevice.isDefault = isDefault;

        // avoid duplicate names
        int duplicateNameCounter = 2;
        while(true) {
            bool foundDuplicateName = false;
            for(auto &outputDevice : this->outputDevices) {
                if(outputDevice.name == soundDevice.name) {
                    foundDuplicateName = true;

                    soundDevice.name = deviceInfo.name;
                    soundDevice.name.append(fmt::format(" ({})", duplicateNameCounter));

                    duplicateNameCounter++;

                    break;
                }
            }

            if(!foundDuplicateName) break;
        }

        soundDevice.driver = OutputDriver::BASS;
        this->outputDevices.push_back(soundDevice);

        debugLog("DSOUND: Device {:d} = \"{:s}\", enabled = {:d}, default = {:d}", d, deviceInfo.name, (int)isEnabled,
                 (int)isDefault);
    }

#ifdef _WIN32
    BASS_ASIO_DEVICEINFO asioDeviceInfo;
    for(int d = 0; (BASS_ASIO_GetDeviceInfo(d, &asioDeviceInfo) == TRUE); d++) {
        SoundEngine::OUTPUT_DEVICE soundDevice;
        soundDevice.id = d;
        soundDevice.name = asioDeviceInfo.name;
        soundDevice.enabled = true;
        soundDevice.isDefault = false;

        // avoid duplicate names
        int duplicateNameCounter = 2;
        while(true) {
            bool foundDuplicateName = false;
            for(size_t i = 0; i < this->outputDevices.size(); i++) {
                if(this->outputDevices[i].name == soundDevice.name) {
                    foundDuplicateName = true;

                    soundDevice.name = deviceInfo.name;
                    soundDevice.name.append(fmt::format(" ({})", duplicateNameCounter));

                    duplicateNameCounter++;

                    break;
                }
            }

            if(!foundDuplicateName) break;
        }

        soundDevice.driver = OutputDriver::BASS_ASIO;
        soundDevice.name.append(" [ASIO]");
        this->outputDevices.push_back(soundDevice);

        debugLog("ASIO: Device {:d} = \"{:s}\"", d, asioDeviceInfo.name);
    }

    BASS_WASAPI_DEVICEINFO wasapiDeviceInfo;
    for(int d = 0; (BASS_WASAPI_GetDeviceInfo(d, &wasapiDeviceInfo) == TRUE); d++) {
        const bool isEnabled = (wasapiDeviceInfo.flags & BASS_DEVICE_ENABLED);
        const bool isDefault = (wasapiDeviceInfo.flags & BASS_DEVICE_DEFAULT);
        const bool isInput = (wasapiDeviceInfo.flags & BASS_DEVICE_INPUT);
        if(isInput) continue;

        SoundEngine::OUTPUT_DEVICE soundDevice;
        soundDevice.id = d;
        soundDevice.name = wasapiDeviceInfo.name;
        soundDevice.enabled = isEnabled;
        soundDevice.isDefault = isDefault;

        // avoid duplicate names
        int duplicateNameCounter = 2;
        while(true) {
            bool foundDuplicateName = false;
            for(size_t i = 0; i < this->outputDevices.size(); i++) {
                if(this->outputDevices[i].name == soundDevice.name) {
                    foundDuplicateName = true;

                    soundDevice.name = wasapiDeviceInfo.name;
                    soundDevice.name.append(fmt::format(" ({})", duplicateNameCounter));

                    duplicateNameCounter++;

                    break;
                }
            }

            if(!foundDuplicateName) break;
        }

        soundDevice.driver = OutputDriver::BASS_WASAPI;
        soundDevice.name.append(" [WASAPI]");
        this->outputDevices.push_back(soundDevice);

        debugLog("WASAPI: Device {:d} = \"{:s}\", enabled = {:d}, default = {:d}", d, wasapiDeviceInfo.name,
                 (int)isEnabled, (int)isDefault);
    }
#endif
}

// The BASS mixer is used for every sound driver, but it's useful to be able to
// initialize it later on some drivers where we know the best available frequency.
bool BassSoundEngine::init_bass_mixer(const SoundEngine::OUTPUT_DEVICE &device) {
    auto bass_flags = BASS_DEVICE_STEREO | BASS_DEVICE_FREQ | BASS_DEVICE_NOSPEAKER;
    auto freq = cv::snd_freq.getInt();

    // We initialize a "No sound" device for measuring loudness and mixing sounds,
    // regardless of the device we'll use for actual output.
    if(!BASS_Init(0, freq, bass_flags | BASS_DEVICE_SOFTWARE, nullptr, nullptr)) {
        auto code = BASS_ErrorGetCode();
        if(code != BASS_ERROR_ALREADY) {
            this->ready_since = -1.0;
            debugLog("BASS_Init(0) failed.");
            app->showNotification({BassManager::getErrorString(), NotificationPreset::ERROR});
            return false;
        }
    }

    if(device.driver == OutputDriver::BASS) {
        if(!BASS_Init(device.id, freq, bass_flags | BASS_DEVICE_SOFTWARE, nullptr, nullptr)) {
            this->ready_since = -1.0;
            debugLog("BASS_Init({:d}) errored out.", device.id);
            app->showNotification({BassManager::getErrorString(), NotificationPreset::ERROR});
            return false;
        }
    }

    auto mixer_flags = BASS_SAMPLE_FLOAT | BASS_MIXER_NONSTOP | BASS_MIXER_RESUME;
    if(device.driver != OutputDriver::BASS) mixer_flags |= BASS_STREAM_DECODE | BASS_MIXER_POSEX;
    this->g_bassOutputMixer = BASS_Mixer_StreamCreate(freq, 2, mixer_flags);
    if(this->g_bassOutputMixer == 0) {
        this->ready_since = -1.0;
        debugLog("BASS_Mixer_StreamCreate() failed.");
        app->showNotification({BassManager::getErrorString(), NotificationPreset::ERROR});
        return false;
    }

    // Disable buffering to lower latency on regular BASS output
    // This has no effect on ASIO/WASAPI since for those the mixer is a decode stream
    BASS_ChannelSetAttribute(this->g_bassOutputMixer, BASS_ATTRIB_BUFFER, 0.f);

    // Switch to "No sound" device for all future sound processing
    // Only this->g_bassOutputMixer will be output to the actual device!
    BASS_SetDevice(0);
    return true;
}

bool BassSoundEngine::initializeOutputDevice(const SoundEngine::OUTPUT_DEVICE &device) {
    debugLog("BassSoundEngine: initializeOutputDevice( {:s} ) ...", device.name.toUtf8());

    this->shutdown();

    if(device.driver == OutputDriver::NONE || (device.driver == OutputDriver::BASS && device.id == 0)) {
        this->ready_since = -1.0;
        this->currentOutputDevice = device;
        cv::snd_output_device.setValue(this->currentOutputDevice.name);
        debugLog("BassSoundEngine: Output Device = \"{:s}\"", this->currentOutputDevice.name.toUtf8());

        return true;
    }

    if(device.driver == OutputDriver::BASS) {
        // Normal output: render playback buffer every 5ms (buffer is 30ms large)
        BASS_SetConfig(BASS_CONFIG_UPDATEPERIOD, 5);
        BASS_SetConfig(BASS_CONFIG_UPDATETHREADS, 1);
    } else if(device.driver == OutputDriver::BASS_ASIO || device.driver == OutputDriver::BASS_WASAPI) {
        // ASIO/WASAPI: let driver decide when to render playback buffer
        BASS_SetConfig(BASS_CONFIG_UPDATEPERIOD, 0);
        BASS_SetConfig(BASS_CONFIG_UPDATETHREADS, 0);
    }

    // allow users to override some defaults (but which may cause beatmap desyncs)
    // we only want to set these if their values have been explicitly modified (to avoid sideeffects in the default
    // case, and for my sanity)
    {
        if(cv::snd_updateperiod.getFloat() != cv::snd_updateperiod.getDefaultFloat())
            BASS_SetConfig(BASS_CONFIG_UPDATEPERIOD, cv::snd_updateperiod.getInt());

        if(cv::snd_dev_buffer.getFloat() != cv::snd_dev_buffer.getDefaultFloat())
            BASS_SetConfig(BASS_CONFIG_DEV_BUFFER, cv::snd_dev_buffer.getInt());

        if(cv::snd_dev_period.getFloat() != cv::snd_dev_period.getDefaultFloat())
            BASS_SetConfig(BASS_CONFIG_DEV_PERIOD, cv::snd_dev_period.getInt());
    }

    // When the driver is BASS, we can init the mixer immediately
    // On other drivers, we'd rather get the sound card's frequency first
    if(device.driver == OutputDriver::BASS) {
        if(!this->init_bass_mixer(device)) {
            return false;
        }
    }

#ifdef _WIN32
    if(device.driver == OutputDriver::BASS_ASIO) {
        if(!BASS_ASIO_Init(device.id, 0)) {
            this->ready_since = -1.0;
            debugLog("BASS_ASIO_Init() failed.");
            app->showNotification({BassManager::getErrorString(), NotificationPreset::ERROR});
            return false;
        }

        double sample_rate = cv::asio_freq.getFloat();
        if(sample_rate > 0.0) {
            if(!BASS_ASIO_SetRate(sample_rate)) {
                debugLog("ASIO: Device refused to use {}Hz sample rate!", sample_rate);
            }
        }

        sample_rate = BASS_ASIO_GetRate();
        debugLog("ASIO: Using {}Hz sample rate.", sample_rate);
        cv::snd_freq.setValue(sample_rate, false);

        if(!init_bass_mixer(device)) {
            return false;
        }

        BASS_ASIO_INFO info{};
        BASS_ASIO_GetInfo(&info);
        auto bufsize = cv::asio_buffer_size.getVal<u32>();
        bufsize = ASIO_clamp(info, bufsize);

        // TODO: this is weird and ugly and barely necessary, but lazy to figure out a proper solution for now
        if(this->asio_buffer_change_cb) this->asio_buffer_change_cb(info);

        if(!BASS_ASIO_ChannelEnableBASS(false, 0, this->g_bassOutputMixer, true)) {
            this->ready_since = -1.0;
            debugLog("BASS_ASIO_ChannelEnableBASS() failed.");
            app->showNotification({BassManager::getErrorString(), NotificationPreset::ERROR});
            return false;
        }

        if(!BASS_ASIO_Start(bufsize, 0)) {
            this->ready_since = -1.0;
            debugLog("BASS_ASIO_Start() failed.");
            app->showNotification({BassManager::getErrorString(), NotificationPreset::ERROR});
            return false;
        }

        double wanted_latency = 1000.0 * cv::asio_buffer_size.getFloat() / sample_rate;
        double actual_latency = 1000.0 * (double)BASS_ASIO_GetLatency(false) / sample_rate;
        BASS_ChannelSetAttribute(this->g_bassOutputMixer, BASS_ATTRIB_MIXER_LATENCY, actual_latency / 1000.0);
        debugLog("ASIO: wanted {:f} ms, got {:f} ms latency. Sample rate: {:f} Hz", wanted_latency, actual_latency,
                 sample_rate);
    }

    if(device.driver == OutputDriver::BASS_WASAPI) {
        const bool evcallback = cv::win_snd_wasapi_event_callbacks.getBool();
        const float bufferSize =
            evcallback ? 0.f : std::round(cv::win_snd_wasapi_buffer_size.getFloat() * 1000.0f) / 1000.0f;  // in seconds
        const float updatePeriod =
            evcallback ? 0.00000001f : /* see: https://github.com/ppy/osu-framework/pull/6651 */
                std::round(cv::win_snd_wasapi_period_size.getFloat() * 1000.0f) / 1000.0f;  // in seconds

        BASS_WASAPI_DEVICEINFO info;
        if(!BASS_WASAPI_GetDeviceInfo(device.id, &info)) {
            debugLog("WASAPI: Failed to get device info");
            return false;
        }
        cv::snd_freq.setValue(info.mixfreq);
        if(!init_bass_mixer(device)) {
            return false;
        }

        // BASS_MIXER_NONSTOP prevents some sound cards from going to sleep when there is no output
        auto flags = BASS_MIXER_NONSTOP |      //
                     BASS_WASAPI_AUTOFORMAT |  // attempt to initialize with the best driver-compatible sample format
                     BASS_WASAPI_RAW |  // ignores sound "enhancements" that some sound cards offer (adds latency)
                     (evcallback ? BASS_WASAPI_EVENT : 0);  // use event-driven (pull) mode instead of timer (push) mode

        if(cv::win_snd_wasapi_exclusive.getBool()) {
            // BASS_WASAPI_EXCLUSIVE makes us have exclusive output to the sound card
            flags |= BASS_WASAPI_EXCLUSIVE;
        }
        if(!BASS_WASAPI_Init(device.id, 0, 0, flags, bufferSize, updatePeriod, WASAPIPROC_BASS,
                             reinterpret_cast<void *>(static_cast<uintptr_t>(g_bassOutputMixer)))) {
            this->ready_since = -1.0;
            debugLog("BASS_WASAPI_Init() failed.");
            app->showNotification({BassManager::getErrorString(), NotificationPreset::ERROR});
            return false;
        }

        if(!BASS_WASAPI_Start()) {
            this->ready_since = -1.0;
            debugLog("BASS_WASAPI_Start() failed.");
            app->showNotification({BassManager::getErrorString(), NotificationPreset::ERROR});
            return false;
        }

        BASS_ChannelSetAttribute(this->g_bassOutputMixer, BASS_ATTRIB_MIXER_LATENCY,
                                 cv::win_snd_wasapi_buffer_size.getFloat());
    }
#endif

    this->ready_since = engine->getTime();
    this->currentOutputDevice = device;
    cv::snd_output_device.setValue(this->currentOutputDevice.name);
    debugLog("BassSoundEngine: Output Device = \"{:s}\"", this->currentOutputDevice.name.toUtf8());

    return true;
}

void BassSoundEngine::restart() { this->setOutputDevice(this->currentOutputDevice); }

void BassSoundEngine::shutdown() {
    // run pre-restart callback here
    if(this->restartCBs[0] != nullptr) this->restartCBs[0]();

    if(this->currentOutputDevice.driver == OutputDriver::BASS) {
        BASS_SetDevice(this->currentOutputDevice.id);
        BASS_Free();
        BASS_SetDevice(0);
    }

#ifdef _WIN32
    if(this->currentOutputDevice.driver == OutputDriver::BASS_ASIO) {
        BASS_ASIO_Free();
    } else if(this->currentOutputDevice.driver == OutputDriver::BASS_WASAPI) {
        BASS_WASAPI_Free();
    }
#endif

    this->ready_since = -1.0;
    this->g_bassOutputMixer = 0;
    BASS_Free();  // free "No sound" device
}

bool BassSoundEngine::play(Sound *snd, f32 pan, f32 pitch, f32 playVolume, bool startPaused) {
    if(!this->isReady() || !snd || !snd->isReady()) return false;

    auto bassSound = snd->as<BassSound>();
    if(!bassSound->isStream()) {
        // starting paused is only implemented for streams right now.
        startPaused = false;
    }

    if(!bassSound->isStream() && !bassSound->isOverlayable() && !bassSound->getActiveHandles().empty()) {
        logIfCV(debug_snd, "Attempted to play non-overlayable {} while it's still playing!", snd->getName());
        return false;
    }

    auto handle = bassSound->getNewHandle();
    if(!handle) {
        debugLog("BassSoundEngine::play() failed to get channel, errorcode {:d}", BASS_ErrorGetCode());
        return false;
    }

    if(!startPaused && bassSound->getLastPlayTime() == engine->getTime()) {
        logIfCV(debug_snd, "Played {} twice in the same update loop!", snd->getName());
        if(cv::snd_restrict_play_frame.getBool()) {
            return false;
        }
    }

    // NOTE: BASS documentation says setting FREQ/PAN/VOL won't work on DECODE streams, but it works just fine...
    pan = std::clamp<float>(pan, -1.0f, 1.0f);
    BASS_ChannelSetAttribute(handle, BASS_ATTRIB_VOL, bassSound->getBaseVolume() * playVolume);
    BASS_ChannelSetAttribute(handle, BASS_ATTRIB_PAN, pan);
    BASS_ChannelSetAttribute(handle, BASS_ATTRIB_NORAMP, bassSound->isStream() ? 0 : 1);
    if(pitch != 0.0f) {
        f32 freq = cv::snd_freq.getFloat();
        BASS_ChannelGetAttribute(handle, BASS_ATTRIB_FREQ, &freq);
        BASS_ChannelSetAttribute(handle, BASS_ATTRIB_FREQ, std::pow(2.0f, pitch) * freq);
    }

    BASS_ChannelFlags(handle, bassSound->isLooped() ? BASS_SAMPLE_LOOP : 0, BASS_SAMPLE_LOOP);

    if(bassSound->bPaused) {
        assert(bassSound->bStream);

        // update handle params for existing handle
        PlaybackParams newParams{.pan = pan, .pitch = pitch, .volume = playVolume};
        bassSound->activeHandleCache[handle] = newParams;

        bool success = true;
        if(!startPaused) {
            success = this->actuallyPlay(bassSound, handle, bassSound->paused_position_us);
        }

        return success;
    }

    if(BASS_Mixer_ChannelGetMixer(handle) != 0) {
        logIfCV(debug_snd, "Attempted to play {}, but channel is already playing!", snd->getName());
        return false;
    }

    auto flags = BASS_MIXER_DOWNMIX | BASS_MIXER_NORAMPIN | (startPaused ? BASS_MIXER_CHAN_PAUSE : 0);
    if(!snd->isStream()) flags |= BASS_STREAM_AUTOFREE;

    if(!BASS_Mixer_StreamAddChannel(this->g_bassOutputMixer, handle, flags)) {
        debugLog("BASS_Mixer_StreamAddChannel() failed ({:d})!", BASS_ErrorGetCode());

        // We called getNewHandle(), and it created a new channel. But since it wasn't
        // added to any mixer, it will never get AUTOFREEd - so we need to free it here.
        if(!snd->isStream()) {
            BASS_ChannelFree(handle);
        }

        return false;
    }

    bool succeeded = true;
    if(!startPaused) {
        succeeded = this->actuallyPlay(bassSound, handle, 0);
    } else {
        bassSound->bPaused = true;
    }

    if(succeeded) {
        PlaybackParams newParams{.pan = pan, .pitch = pitch, .volume = playVolume};
        bassSound->addActiveInstance(handle, newParams);
    }

    return succeeded;
}

bool BassSoundEngine::actuallyPlay(BassSound *bassSound, SOUNDHANDLE playHandle, u64 positionUS) {
    assert(bassSound);
    // Make sure the mixer is playing! Duh.
    if(bassSound->bPaused && this->currentOutputDevice.driver == OutputDriver::BASS) {
        if(BASS_ChannelIsActive(this->g_bassOutputMixer) != BASS_ACTIVE_PLAYING) {
            if(!BASS_ChannelPlay(this->g_bassOutputMixer, true)) {
                debugLog("BassSoundEngine::play() couldn't BASS_ChannelPlay(), errorcode {:d}", BASS_ErrorGetCode());
                return false;
            }
        }
    }

    if(bassSound->isStream()) {
        // set position, and unpause the channel if paused
        bassSound->setPositionUS(positionUS);
        if(bassSound->bPaused) {
            BASS_Mixer_ChannelFlags(playHandle, 0, BASS_MIXER_CHAN_PAUSE);
        }
    }

    bassSound->bPaused = false;
    bassSound->bStarted = true;

    bassSound->setLastPlayTime(engine->getTime());

    logIfCV(debug_snd, "Playing {:s}", bassSound->getFilePath().c_str());

    return true;
}

void BassSoundEngine::pause(Sound *snd) {
    if(!this->isReady() || !snd || !snd->isReady()) return;
    assert(snd->isStream());  // can't call pause() on a sample

    auto bassSound = snd->as<BassSound>();
    if(!bassSound->isPlaying()) return;

    const u64 posUS = bassSound->getPositionUS();
    BASS_Mixer_ChannelFlags(bassSound->srchandle, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
    bassSound->bPaused = true;
    bassSound->paused_position_us = posUS;
    bassSound->interpolator.reset((f64)posUS / (1000. * 1000.), Timing::getTimeReal(), bassSound->getSpeed());
}

void BassSoundEngine::stop(Sound *snd) {
    if(!this->isReady() || snd == nullptr || !snd->isReady()) return;

    auto bassSound = snd->as<BassSound>();
    if(!bassSound->isPlaying()) return;

    if(snd->isStream()) {
        BASS_Mixer_ChannelFlags(bassSound->srchandle, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
        bassSound->bPaused = true;
        bassSound->setPositionUS(0);
    } else {
        for(auto &[handle, _] : bassSound->getActiveHandles()) {
            BASS_Mixer_ChannelRemove(handle);
            // sample channels get AUTOFREEd
        }
    }
}

bool BassSoundEngine::isReady() {
    if(this->ready_since == -1.0) return false;
    return this->ready_since + (double)cv::snd_ready_delay.getFloat() < engine->getTime();
}

bool BassSoundEngine::hasExclusiveOutput() {
    return this->isASIO() || (this->isWASAPI() && cv::win_snd_wasapi_exclusive.getBool());
}

void BassSoundEngine::setOutputDevice(const SoundEngine::OUTPUT_DEVICE &device) {
    // run callbacks pt. 1
    // NOTE: moved to shutdown() (called from within initializeOutputDevice), because that's called on engine shutdown as well
    // if(this->restartCBs[0] != nullptr) this->restartCBs[0]();

    // TODO: This is blocking main thread, can freeze for a long time on some sound cards
    auto previous = this->currentOutputDevice;
    if(!this->initializeOutputDevice(device)) {
        if((device.id == previous.id && device.driver == previous.driver) || !this->initializeOutputDevice(previous)) {
            // We failed to reinitialize the device, don't start an infinite loop, just give up
            this->currentOutputDevice = {
                .id = 0,
                .enabled = true,
                .isDefault = true,
                .name = "No sound",
                .driver = OutputDriver::NONE,
            };
        }
    }

    // run callbacks pt. 2
    if(this->restartCBs[1] != nullptr) this->restartCBs[1]();
}

void BassSoundEngine::setMasterVolume(float volume) {
    if(!this->isReady()) return;

    this->fMasterVolume = std::clamp<float>(volume, 0.0f, 1.0f);
    if(this->currentOutputDevice.driver == OutputDriver::BASS_ASIO) {
#ifdef _WIN32
        BASS_ASIO_ChannelSetVolume(false, 0, this->fMasterVolume);
        BASS_ASIO_ChannelSetVolume(false, 1, this->fMasterVolume);
#endif
    } else if(this->currentOutputDevice.driver == OutputDriver::BASS_WASAPI) {
#ifdef _WIN32
        if(hasExclusiveOutput()) {
            // Device volume doesn't seem to work, so we'll use DSP instead
            BASS_ChannelSetAttribute(this->g_bassOutputMixer, BASS_ATTRIB_VOLDSP, this->fMasterVolume);
        } else {
            BASS_WASAPI_SetVolume(BASS_WASAPI_CURVE_WINDOWS | BASS_WASAPI_VOL_SESSION, this->fMasterVolume);
        }
#endif
    } else {
        // 0 (silent) - 10000 (full).
        BASS_SetConfig(BASS_CONFIG_GVOL_SAMPLE, (DWORD)(this->fMasterVolume * 10000));
        BASS_SetConfig(BASS_CONFIG_GVOL_STREAM, (DWORD)(this->fMasterVolume * 10000));
        BASS_SetConfig(BASS_CONFIG_GVOL_MUSIC, (DWORD)(this->fMasterVolume * 10000));
    }
}

void BassSoundEngine::onFreqChanged(float oldFreq, float newFreq) {
    if(!this->isReady() || oldFreq == newFreq) return;
    this->restart();
}

void BassSoundEngine::onParamChanged(float oldValue, float newValue) {
    const int oldValueMS = static_cast<int>(std::round(oldValue * 1000.0f));
    const int newValueMS = static_cast<int>(std::round(newValue * 1000.0f));

    if(oldValueMS != newValueMS) this->restart();
}

#endif
