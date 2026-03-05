// Copyright (c) 2025, WH, All rights reserved.

#include "config.h"

#ifdef MCENGINE_FEATURE_SOLOUD
#include "SoLoudSound.h"
#include "SoLoudThread.h"

#include "SoLoudFX.h"
#include "SoLoudSoundEngine.h"

#include "ConVar.h"
#include "Engine.h"
#include "File.h"
#include "ResourceManager.h"
#include "Logging.h"

#include "soloud_file.h"
#include "soloud_wav.h"

SoLoudSound::SoLoudSound(std::string filepath, bool stream, bool overlayable, bool loop)
    : Sound(std::move(filepath), stream, overlayable, loop) {}
SoLoudSound::~SoLoudSound() { this->destroy(); }

void SoLoudSound::init() {
    if(this->bIgnored || this->sFilePath.length() < 2 || !(this->isAsyncReady())) return;

    if(!this->audioSource)
        debugLog("Couldn't load sound \"{}\", stream = {}, file = {}", this->sFilePath, this->bStream, this->sFilePath);
    else
        this->setReady(true);
}

void SoLoudSound::initAsync() {
    Sound::initAsync();
    if(this->bIgnored) return;

    // clean up any previous instance
    this->audioSource.reset();

    // create the appropriate audio source based on streaming flag
    SoLoud::result result = SoLoud::SO_NO_ERROR;
    if(this->bStream) {
        // use SLFXStream for streaming audio (music, etc.) includes rate/pitch processing like BASS_FX_TempoCreate
        auto *stream = new SoLoud::SLFXStream(cv::snd_soloud_prefer_ffmpeg.getInt() > 0);
        result = stream->loadToMem(this->sFilePath.c_str());

        if(result == SoLoud::SO_NO_ERROR) {
            this->audioSource.reset(stream);
            this->fFrequency = stream->mBaseSamplerate;

            this->audioSource->setInaudibleBehavior(
                true, false);  // keep ticking the sound if it goes to 0 volume, and don't kill it

            logIfCV(debug_snd,
                    "SoLoudSound: Created SLFXStream for {:s} with speed={:f}, pitch={:f}, looping={:s}, "
                    "decoder={:s}",
                    this->sFilePath, this->fSpeed, this->fPitch, this->bIsLooped ? "true" : "false",
                    stream->getDecoder());
        } else {
            delete stream;
            debugLog("Sound Error: SLFXStream::load() error {} on file {:s}", result, this->sFilePath);
            return;
        }
    } else {
        SoLoud::DiskFile df(File::fopen_c(this->sFilePath.c_str(), "rb"));

        if(!df.getFilePtr()) {  // fopen failed
            debugLog("Sound Error: SoLoud::Wav::load() error {} on file {:s}", result, this->sFilePath);
            return;
        }

        // use Wav for non-streaming audio (hit sounds, effects, etc.)
        auto *wav = new SoLoud::Wav(cv::snd_soloud_prefer_ffmpeg.getInt() > 1);
        // the file's contents are immediately read into an internal buffer, so we don't have to leave it open
        // this is untrue for streams, but the SLFXStream wrapper handles wide path conversion internally
        result = wav->loadFile(&df);

        if(result == SoLoud::SO_NO_ERROR) {
            this->audioSource.reset(wav);
            this->fFrequency = wav->mBaseSamplerate;

            this->audioSource->setInaudibleBehavior(
                true, true);  // keep ticking the sound if it goes to 0 volume, but do kill it if necessary
        } else {
            delete wav;
            debugLog("Sound Error: SoLoud::Wav::load() error {} on file {:s}", result, this->sFilePath);
            return;
        }
    }

    // only play one music track at a time
    this->audioSource->setSingleInstance(this->bStream || !this->bIsOverlayable);
    this->audioSource->setLooping(this->bIsLooped);

    this->setAsyncReady(true);
}

SOUNDHANDLE SoLoudSound::getHandle() { return this->handle; }

void SoLoudSound::destroy() {
    if(!this->isAsyncReady()) {
        this->interruptLoad();
    }

    this->setAsyncReady(false);
    this->setReady(false);

    // stop the sound if it's playing
    if(this->handle != 0) {
        if(soloud) soloud->stop(this->handle);
        this->handle = 0;
    }

    // clean up audio source
    this->audioSource.reset();

    // need to reset this because the soloud handle has been destroyed
    this->fFrequency = 44100.0f;
    this->fPitch = 1.0f;
    this->fSpeed = 1.0f;
    this->fPan = 0.0f;
    this->activeHandleCache.clear();
    this->fLastPlayTime = 0.0f;
    this->bIgnored = false;

    // reset position cache state
    this->cached_stream_position = 0.0;
    this->soloud_stream_position_cache_time = -1.0;
    this->soloud_paused_handle_cache_time = 0.0;
    this->soloud_valid_handle_cache_time = 0.0;
    this->force_sync_position_next = true;
}

void SoLoudSound::setPositionUS(u64 us) {
    if(!this->isReady() || !this->audioSource || !this->handle) return;

    const auto lengthUS = this->getLengthUS();
    if(us > lengthUS) return;

    const f64 positionInSeconds = static_cast<f64>(us) / (1000. * 1000.);

    logIfCV(debug_snd, "seeking to {:.4f}s (length: {:.4f}s)", positionInSeconds,
            static_cast<f64>(lengthUS) / (1000. * 1000.));

    // seek
    soloud->seek(this->handle, positionInSeconds);

    // force next position query to be synchronous to get accurate post-seek position
    this->force_sync_position_next = true;

    // reset position interp vars with the new position
    this->interpolator.reset(positionInSeconds, Timing::getTimeReal(), getSpeed());
}

void SoLoudSound::setSpeed(float speed) {
    if(!this->isReady() || !this->audioSource || !this->handle) return;

    // sample speed could be supported, but there is nothing using it right now so i will only bother when the time
    // comes
    if(!this->bStream) {
        debugLog("Programmer Error: tried to setSpeed on a sample!");
        return;
    }

    speed = std::clamp<float>(speed, 0.05f, 50.0f);

    auto *filteredStream = static_cast<SoLoud::SLFXStream *>(this->audioSource.get());

    const float filteredSpeed = filteredStream->getSpeedFactor();
    const float previousSpeed = this->fSpeed;
    this->fSpeed = speed;

    if(cv::snd_speed_compensate_pitch.getBool()) {
        if(speed != filteredSpeed) {
            // update the SLFXStream parameters
            filteredStream->setSpeedFactor(speed);
            logIfCV(debug_snd, "SoLoudSound: Speed change (compensated pitch) {:s}: {:f}->{:f}", this->sFilePath,
                    previousSpeed, speed);
        }
    } else {
        const float soloudSpeed = soloud->getRelativePlaySpeed(this->handle);
        if(filteredSpeed != 1.f) {
            // make sure the filter speed/pitch is reset, set the relative play speed directly for uncompensated playback
            filteredStream->setSpeedFactor(1.f);
            this->setPitch(1.f);
        }

        if(speed != soloudSpeed) {
            soloud->setRelativePlaySpeed(this->handle, speed);

            logIfCV(debug_snd, "SoLoudSound: Speed change (un-compensated pitch) {:s}: {:f}->{:f}", this->sFilePath,
                    previousSpeed, speed);
        }
    }
}

void SoLoudSound::setPitch(float pitch) {
    if(!this->isReady() || !this->audioSource || !this->handle) return;

    // sample pitch could be supported, but there is nothing using it right now so i will only bother when the time
    // comes
    if(!this->bStream) {
        debugLog("Programmer Error: tried to this->setPitch on a sample!");
        return;
    }

    pitch = std::clamp<float>(pitch, 0.0f, 2.0f);

    auto *filteredStream = static_cast<SoLoud::SLFXStream *>(this->audioSource.get());

    // this should technically be == fPitch, but get it/update it from the source directly just in case
    const float previousPitch = (this->fPitch = filteredStream->getPitchFactor());
    if(previousPitch != pitch) {
        this->fPitch = pitch;

        // update the SLFXStream parameters
        filteredStream->setPitchFactor(this->fPitch);

        logIfCV(debug_snd, "SoLoudSound: Pitch change {:s}: {:f}->{:f}", this->sFilePath, previousPitch, this->fPitch);
    }
}

void SoLoudSound::setFrequency(float frequency) {
    if(!this->isReady() || !this->audioSource) return;

    const float previousFreq = this->fFrequency;
    // 0 means reset to default
    this->fFrequency =
        (frequency > 99.0f ? std::clamp<float>(frequency, 100.0f, 100000.0f) : this->audioSource->mBaseSamplerate);

    logIfCV(debug_snd, "SoLoudSound: Freq change {:s}: {:f}->{:f} (base: {} speed: {} effective: {})", this->sFilePath,
            previousFreq, this->fFrequency, this->audioSource->mBaseSamplerate, this->fSpeed,
            this->fFrequency / this->fSpeed);

    // need to account for speed
    soloud->setSamplerate(this->handle, this->fFrequency / this->fSpeed);
}

void SoLoudSound::setPan(float pan) {
    if(!this->isReady() || !this->handle) return;

    pan = std::clamp<float>(pan, -1.0f, 1.0f);

    this->fPan = pan;

    // apply to the active voice
    soloud->setPan(this->handle, pan);
}

void SoLoudSound::setLoop(bool loop) {
    if(!this->isReady() || !this->audioSource) return;

    this->bIsLooped = loop;

    logIfCV(debug_snd, "setLoop {}", loop);

    // apply to the source
    this->audioSource->setLooping(loop);

    // apply to the active voice
    if(this->handle != 0) {
        soloud->setLooping(this->handle, loop);
    }
}

f64 SoLoudSound::getPositionPct() const {
    if(!this->isReady() || !this->audioSource || !this->handle) return 0.0f;

    const f64 streamLengthInSeconds = getSourceLengthInSeconds();
    if(streamLengthInSeconds <= 0.0) return 0.0f;

    const f64 streamPositionInSeconds = getStreamPositionInSeconds();

    // update interped state while we're at it
    this->interpolator.update(streamPositionInSeconds, Timing::getTimeReal(), getSpeed(), isLooped(),
                              static_cast<u32>(std::round(streamLengthInSeconds * 1000.0)), isPlaying());

    return std::clamp<f64>(streamPositionInSeconds / streamLengthInSeconds, 0.0f, 1.0f);
}

i32 SoLoudSound::getRateBasedStreamDelayMS() const {
    if(!this->isReady() || !this->bStream || !this->audioSource || !this->handle) return 0;

    const auto *strm = static_cast<SoLoud::SLFXStream *>(this->audioSource.get());
    return static_cast<i32>(std::round(strm->getInternalLatency() * 1000.0));
}

u64 SoLoudSound::getPositionUS() const {
    if(!this->isReady() || !this->audioSource || !this->handle) return 0;

    return this->interpolator.update(getStreamPositionInSeconds(), Timing::getTimeReal(), getSpeed(), isLooped(),
                                     getLengthMS(), isPlaying());
}

u64 SoLoudSound::getLengthUS() const {
    if(!this->isReady() || !this->audioSource) return 0;

    const u64 lengthUS = static_cast<u64>(std::round(getSourceLengthInSeconds() * 1000.0 * 1000.0));
    // if (cv::debug_snd.getBool())
    // 	debugLog("lengthUS for {:s}: {:g}", this->sFilePath, lengthUS);
    return lengthUS;
}

float SoLoudSound::getSpeed() const {
    if(!this->isReady()) return 1.0f;

    return this->fSpeed;
}

float SoLoudSound::getPitch() const {
    if(!this->isReady()) return 1.0f;

    return this->fPitch;
}

bool SoLoudSound::isPlaying() const {
    if(!this->isReady()) return false;

    // a sound is playing if our handle is valid and the sound isn't paused
    return this->is_playing_cached();
}

bool SoLoudSound::isFinished() const {
    if(!this->isReady()) return false;

    // a sound is finished if our handle is no longer valid
    const bool finished = !this->valid_handle_cached();

    return finished;
}

bool SoLoudSound::isHandleValid(SOUNDHANDLE queryHandle) const {
    return queryHandle != 0 && this->isReady() && soloud && soloud->isValidVoiceHandle(queryHandle);
}

void SoLoudSound::setHandleVolume(SOUNDHANDLE handle, float volume) {
    if(handle != 0 && this->isReady() && soloud) {
        // soloud does not support amplified (>1.0f) volume
        soloud->setVolume(handle, std::clamp<float>(volume, 0.f, 1.f));
    }
}

// soloud-specific accessors

double SoLoudSound::getStreamPositionInSeconds() const {
    if(!this->audioSource || !this->handle) return this->interpolator.getLastInterpolatedPositionS();

    const auto now = Timing::getTimeReal();

    // check if we need to force synchronous access (e.g. init, or after seek)
    if(this->force_sync_position_next) {
        this->force_sync_position_next = false;
        this->cached_stream_position.store(soloud->getStreamPosition(this->handle), std::memory_order_release);
        this->soloud_stream_position_cache_time.store(now, std::memory_order_release);
        return this->cached_stream_position;
    }

    // use cached value if recent enough (updated within last 10ms)
    if(now >= this->soloud_stream_position_cache_time + 0.01) {
        // cache is stale, trigger async update
        this->soloud_stream_position_cache_time.store(now, std::memory_order_relaxed);  // prevent multiple async calls
        soloud->updateCachedPosition(this->handle, this->soloud_stream_position_cache_time,
                                     this->cached_stream_position);
    }

    return this->cached_stream_position.load(std::memory_order_acquire);
}

double SoLoudSound::getSourceLengthInSeconds() const {
    if(!this->audioSource) return 0.0;
    if(this->bStream)
        return static_cast<SoLoud::SLFXStream *>(this->audioSource.get())->getLength();
    else
        return static_cast<SoLoud::Wav *>(this->audioSource.get())->getLength();
}

bool SoLoudSound::valid_handle_cached() const {
    if(this->handle == 0) return false;

    const auto now = engine->getTime();
    if(now >= this->soloud_valid_handle_cache_time + 0.01) {  // 10ms intervals should be fast enough
        this->soloud_valid_handle_cache_time = now;
        if(!soloud->isValidVoiceHandle(this->handle)) {
            const_cast<SoLoudSound *>(this)->handle = 0;
        }
    }

    return this->handle != 0;
}

bool SoLoudSound::is_playing_cached() const {
    if(!this->valid_handle_cached()) return false;

    const auto now = engine->getTime();
    if(now >= this->soloud_paused_handle_cache_time + 0.01) {
        this->soloud_paused_handle_cache_time = now;
        this->cached_pause_state = soloud->getPause(this->handle);
    }

    return this->cached_pause_state != true;
}

#endif  // MCENGINE_FEATURE_SOLOUD
