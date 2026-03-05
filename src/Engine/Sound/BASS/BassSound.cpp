// Copyright (c) 2014, PG, All rights reserved.
#include "BassSound.h"

#ifdef MCENGINE_FEATURE_BASS

#include <algorithm>

#include "BassManager.h"
#include "ConVar.h"
#include "Engine.h"
#include "File.h"
#include "ResourceManager.h"
#include "Timing.h"
#include "Logging.h"
#include "SString.h"
#include "SyncOnce.h"

namespace {  // static
int currentTransposerAlgorithm{BASS_FX_TEMPO_ALGO_CUBIC};

int getTransposerValForString(std::string str) {
    int ret = currentTransposerAlgorithm;

    if(!str.empty()) {
        SString::lower_inplace(str);

        if(str.contains("linear"))
            ret = BASS_FX_TEMPO_ALGO_LINEAR;
        else if(str.contains("cubic"))
            ret = BASS_FX_TEMPO_ALGO_CUBIC;  // default
        else if(str.contains("shannon"))
            ret = BASS_FX_TEMPO_ALGO_SHANNON;
    }

    return ret;
}

Sync::once_flag transposerCallbackSet;
}  // namespace

void BassSound::init() {
    if(this->bIgnored || this->sFilePath.length() < 2 || !(this->isAsyncReady())) return;

    this->setReady(this->isAsyncReady());
}

void BassSound::initAsync() {
    Sound::initAsync();
    if(this->bIgnored) return;

    UString file_path{this->sFilePath};

    if(this->bStream) {
        Sync::call_once(transposerCallbackSet, []() -> void {
            // set initial value
            currentTransposerAlgorithm = getTransposerValForString(cv::snd_rate_transpose_algorithm.getString());

            // SoLoudFX.cpp uses a change callback, so these dont conflict
            cv::snd_rate_transpose_algorithm.setCallback([](std::string_view newv) {
                currentTransposerAlgorithm = getTransposerValForString(std::string{newv});
            });
        });

        u32 flags = BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_STREAM_PRESCAN;
        if(cv::snd_async_buffer.getInt() > 0) flags |= BASS_ASYNCFILE;
        if constexpr(Env::cfg(OS::WINDOWS)) flags |= BASS_UNICODE;

        if(this->isInterrupted()) return;
        this->srchandle = BASS_StreamCreateFile(BASS_FILE_NAME, file_path.plat_str(), 0, 0, flags);
        if(!this->srchandle) {
            debugLog("BASS_StreamCreateFile() error on file {}: {}", this->sFilePath.c_str(),
                     BassManager::getErrorString());
            return;
        }

        if(this->isInterrupted()) return;
        this->srchandle =
            BASS_FX_TempoCreate(this->srchandle, currentTransposerAlgorithm | BASS_FX_FREESOURCE | BASS_STREAM_DECODE);
        if(!this->srchandle) {
            debugLog("BASS_FX_TempoCreate() error on file {}: {}", this->sFilePath.c_str(),
                     BassManager::getErrorString());
            return;
        }

        // copied from SoLoudFX.cpp
        BASS_ChannelSetAttribute(this->srchandle, BASS_ATTRIB_TEMPO_OPTION_USE_AA_FILTER, 1.f);
        BASS_ChannelSetAttribute(this->srchandle, BASS_ATTRIB_TEMPO_OPTION_AA_FILTER_LENGTH, 64.f);
        BASS_ChannelSetAttribute(this->srchandle, BASS_ATTRIB_TEMPO_OPTION_USE_QUICKALGO, 0.f);
        BASS_ChannelSetAttribute(this->srchandle, BASS_ATTRIB_TEMPO_OPTION_SEQUENCE_MS, 15.f);
        BASS_ChannelSetAttribute(this->srchandle, BASS_ATTRIB_TEMPO_OPTION_SEEKWINDOW_MS, 30.f);
        BASS_ChannelSetAttribute(this->srchandle, BASS_ATTRIB_TEMPO_OPTION_OVERLAP_MS, 6.f);

        BASS_ChannelSetAttribute(this->srchandle, BASS_ATTRIB_TEMPO_OPTION_OLDPOS, 1);  // use old position calculation

        // Only compute the length once
        if(this->isInterrupted()) return;
    } else {
        u32 flags = BASS_SAMPLE_FLOAT;
        if constexpr(Env::cfg(OS::WINDOWS)) flags |= BASS_UNICODE;

        if(this->isInterrupted()) return;
        this->srchandle = BASS_SampleLoad(false, file_path.plat_str(), 0, 0, 1, flags);
        if(!this->srchandle) {
            auto code = BASS_ErrorGetCode();
            if(code == BASS_ERROR_EMPTY) {
                debugLog("BassSound: Ignoring empty file {}", this->sFilePath.c_str());
                return;
            } else {
                debugLog("BASS_SampleLoad() error on file {}: {}", this->sFilePath.c_str(),
                         BassManager::getErrorString(code));
                return;
            }
        }

        if(this->isInterrupted()) return;
    }

    // Only compute the length once
    i64 length = BASS_ChannelGetLength(this->srchandle, BASS_POS_BYTE);
    f64 lengthInSeconds = BASS_ChannelBytes2Seconds(this->srchandle, length);
    logIfCV(debug_snd, "got length bytes: {} seconds: {}", length, lengthInSeconds);
    this->lengthUS = static_cast<u64>(std::round(lengthInSeconds * 1000. * 1000.));

    this->fSpeed = 1.0f;
    this->fPitch = 1.0f;
    this->setAsyncReady(true);
}

void BassSound::destroy() {
    if(!this->isAsyncReady()) {
        this->interruptLoad();
    }

    if(this->srchandle) {
        if(this->bStream) {
            BASS_Mixer_ChannelRemove(this->srchandle);
            BASS_ChannelStop(this->srchandle);
            BASS_StreamFree(this->srchandle);
        } else {
            BASS_SampleStop(this->srchandle);
            BASS_SampleFree(this->srchandle);
        }
        this->srchandle = 0;
    }

    for(const auto& [handle, _] : this->activeHandleCache) {
        BASS_Mixer_ChannelRemove(handle);
        BASS_ChannelStop(handle);
        BASS_ChannelFree(handle);
    }

    this->activeHandleCache.clear();

    this->bStarted = false;
    this->setReady(false);
    this->setAsyncReady(false);
    this->bPaused = false;
    this->paused_position_us = 0;
    this->bIgnored = false;
    this->fLastPlayTime = 0.f;
}

void BassSound::setPositionUS(u64 us) {
    if(!this->isReady() || us > this->getLengthUS()) {
        logIfCV(debug_snd, "can't set position to {}us: {}", us,
                !this->isReady() ? "not ready" : fmt::format("{} > {}", us, this->getLengthUS()));
        return;
    }
    assert(this->bStream);  // can't call setPositionUS() on a sample

    const f64 tgtSecs = static_cast<f64>(us) / (1000. * 1000.);
    const i64 tgtByte = BASS_ChannelSeconds2Bytes(this->srchandle, tgtSecs);
    if(tgtByte < 0) {
        debugLog("BASS_ChannelSeconds2Bytes( stream , {} ) error on file {}: {}", tgtSecs, this->sFilePath.c_str(),
                 BassManager::getErrorString());
        return;
    }

    if(!BASS_Mixer_ChannelSetPosition(this->srchandle, tgtByte, BASS_POS_BYTE | BASS_POS_MIXER_RESET)) {
        logIfCV(debug_snd, "BASS_Mixer_ChannelSetPosition( stream , {} ) error on file {}: {}", us,
                this->sFilePath.c_str(), BassManager::getErrorString());
        return;
    }

    // when paused, position change is immediate
    if(this->bPaused) {
        this->paused_position_us = us;
        logIfCV(debug_snd, "set paused position to {}us", us);
        this->interpolator.reset(tgtSecs, Timing::getTimeReal(), this->getSpeed());
        return;
    }

    // when playing, poll until position updates (with timeout)
    // this is necessary because BASS_Mixer_ChannelGetPosition takes time to reflect the change
    const u64 start = Timing::getTicksMS();
    constexpr u64 timeoutMS = 100;
    constexpr i64 fwdTolUS = 50 * 1000LL;

    f64 actualSecs = 0.;
    i64 actualUS = 0;
    while(true) {
        i64 posBytes = BASS_Mixer_ChannelGetPosition(this->srchandle, BASS_POS_BYTE);
        if(posBytes >= 0) {
            actualSecs = BASS_ChannelBytes2Seconds(this->srchandle, posBytes);
            actualUS = (i64)(actualSecs * 1000. * 1000.);

            // check if we're within tolerance of target
            if(actualUS >= ((i64)us - 1) && actualUS <= ((i64)us + fwdTolUS)) {
                break;
            }
        }

        // check timeout
        if(Timing::getTicksMS() - start > timeoutMS) {
            debugLog("timeout waiting for position update on {} (wanted {:.4f}s, got {:.4f}s)", this->sFilePath.c_str(),
                     tgtSecs, actualSecs);
            break;
        }

        Timing::sleepNS(100ULL * 1000);
    }

    logIfCV(debug_snd, "set position to actual: {:.4f}s desired: {:.4f}s", actualSecs, tgtSecs);
    this->interpolator.reset(actualSecs, Timing::getTimeReal(), this->getSpeed());
}

void BassSound::setSpeed(f32 speed) {
    if(!this->isReady()) return;
    assert(this->bStream);  // can't call setSpeed() on a sample

    speed = std::clamp<float>(speed, 0.05f, 50.0f);

    float freq = cv::snd_freq.getFloat();
    BASS_ChannelGetAttribute(this->srchandle, BASS_ATTRIB_FREQ, &freq);

    BASS_ChannelSetAttribute(this->srchandle, BASS_ATTRIB_TEMPO, 1.0f);
    BASS_ChannelSetAttribute(this->srchandle, BASS_ATTRIB_TEMPO_FREQ, freq);

    if(cv::snd_speed_compensate_pitch.getBool()) {
        BASS_ChannelSetAttribute(this->srchandle, BASS_ATTRIB_TEMPO, (speed - 1.0f) * 100.0f);
    } else {
        BASS_ChannelSetAttribute(this->srchandle, BASS_ATTRIB_TEMPO_FREQ, speed * freq);
    }

    this->fSpeed = speed;
}

void BassSound::setPitch(f32 pitch) {
    if(!this->isReady()) return;
    assert(this->bStream);  // can't call setPitch() on a sample

    pitch = std::clamp<float>(pitch, 0.0f, 2.0f);
    BASS_ChannelSetAttribute(this->srchandle, BASS_ATTRIB_TEMPO_PITCH, (pitch - 1.0f) * 60.0f);

    this->fPitch = pitch;
}

void BassSound::setFrequency(float frequency) {
    if(!this->isReady()) return;

    frequency = (frequency > 99.0f ? std::clamp<float>(frequency, 100.0f, 100000.0f) : 0.0f);

    for(const auto& [handle, _] : this->getActiveHandles()) {
        BASS_ChannelSetAttribute(handle, BASS_ATTRIB_FREQ, frequency);
    }
}

void BassSound::setPan(float pan) {
    if(!this->isReady()) return;

    this->fPan = std::clamp<float>(pan, -1.0f, 1.0f);

    for(const auto& [handle, _] : this->getActiveHandles()) {
        BASS_ChannelSetAttribute(handle, BASS_ATTRIB_PAN, this->fPan);
    }
}

void BassSound::setLoop(bool loop) {
    if(!this->isReady()) return;
    assert(this->bStream);  // can't call setLoop() on a sample

    this->bIsLooped = loop;
    BASS_ChannelFlags(this->srchandle, this->bIsLooped ? BASS_SAMPLE_LOOP : 0, BASS_SAMPLE_LOOP);
}

f64 BassSound::getPositionPct() const {
    const u64 length = this->getLengthUS();
    if(length == 0) return 0.;

    return (f64)this->getPositionUS() / (f64)length;
}

u64 BassSound::getPositionUS() const {
    if(!this->isReady()) return 0;
    assert(this->bStream);  // can't call getPositionMS() on a sample

    if(this->bPaused) {
        this->interpolator.reset((f64)this->paused_position_us / (1000. * 1000.), Timing::getTimeReal(),
                                 this->getSpeed());
        logIf(cv::debug_snd.getInt() > 1, "paused pos {:.4f}s", (f64)this->paused_position_us / (1000. * 1000.));
        return this->paused_position_us;
    }

    if(!this->isPlaying()) {
        // We 'pause' even when stopping the sound, so it is safe to assume the sound hasn't started yet.
        return 0;
    }

    const i64 positionBytes = BASS_Mixer_ChannelGetPosition(this->srchandle, BASS_POS_BYTE);
    if(positionBytes < 0) {
        assert(false);  // invalid handle
        return 0;
    }

    const f64 positionInSeconds = BASS_ChannelBytes2Seconds(this->srchandle, positionBytes);
    const u64 ret = this->interpolator.update(positionInSeconds, Timing::getTimeReal(), this->getSpeed(),
                                              this->isLooped(), static_cast<u64>(this->lengthUS), this->isPlaying());

    logIf(cv::debug_snd.getInt() > 1, "pos {:.4f}s", (f64)ret / (1000. * 1000.));
    return ret;
}

u64 BassSound::getLengthUS() const {
    if(!this->isReady()) return 0;
    return this->lengthUS;
}

float BassSound::getFrequency() const {
    auto default_freq = cv::snd_freq.getFloat();
    if(!this->isReady()) return default_freq;
    assert(this->bStream);  // can't call getFrequency() on a sample

    float frequency = default_freq;
    BASS_ChannelGetAttribute(this->srchandle, BASS_ATTRIB_FREQ, &frequency);
    return frequency;
}

bool BassSound::isPlaying() const {
    return this->isReady() && this->bStarted && !this->bPaused &&
           !const_cast<BassSound*>(this)->getActiveHandles().empty();
}

bool BassSound::isFinished() const { return this->getPositionMS() >= this->getLengthMS(); }

bool BassSound::isHandleValid(SOUNDHANDLE queryHandle) const { return BASS_Mixer_ChannelGetMixer(queryHandle) != 0; }

void BassSound::setHandleVolume(SOUNDHANDLE handle, float volume) {
    BASS_ChannelSetAttribute(handle, BASS_ATTRIB_VOL, volume);
}

// Kind of bad naming, this gets an existing handle for streams, and creates a new one for samples
// Will be stored in active instances if playback succeeds
SOUNDHANDLE BassSound::getNewHandle() {
    if(this->bStream) {
        return this->srchandle;
    } else {
        auto chan = BASS_SampleGetChannel(this->srchandle, BASS_SAMCHAN_STREAM | BASS_STREAM_DECODE);
        return chan;
    }
}

#endif
