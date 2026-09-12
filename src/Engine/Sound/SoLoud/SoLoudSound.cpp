// Copyright (c) 2025, WH, All rights reserved.

#include "config.h"

#ifdef MCENGINE_FEATURE_SOLOUD
#include "SoLoudSound.h"

#include "SoLoudSoundEngine.h"

#include "ConVar.h"
#include "Engine.h"
#include "File.h"
#include "ResourceManager.h"
#include "Logging.h"
#include "Timing.h"

#include "soloud.h"
#include "soloud_file.h"
#include "soloud_wav.h"
#include "soloud_wavstream.h"

#include <algorithm>
#include <cmath>
#include <utility>

// streams need the voice's built-in time-stretch stage (setTempo/setPitchShift)
static_assert(SOLOUD_VERSION >= 202609, "neomod needs a neoloud with the built-in time-stretch stage");

namespace {
// the stage's priming buffer and per-seek decoding grow with the tempo, so past this the resampler carries the rest of
// the speed (and the pitch stops being compensated)
constexpr float MAX_TEMPO = 4.f;

// WavStream::mFiletype as a name, for the debug log
const char *decoderName(SoLoud::WAVSTREAM_FILETYPE filetype) {
    switch(filetype) {
        case SoLoud::WAVSTREAM_WAV:
            return "dr_wav";
        case SoLoud::WAVSTREAM_OGG:
            return "dr_ogg";
        case SoLoud::WAVSTREAM_FLAC:
            return "dr_flac";
        case SoLoud::WAVSTREAM_MPG123:
            return "libmpg123";
        case SoLoud::WAVSTREAM_DRMP3:
            return "dr_mp3";
        case SoLoud::WAVSTREAM_FFMPEG:
            return "ffmpeg";
    }
    return "unknown";
}
}  // namespace

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

    // both source types read the whole file into memory up front (streams are decoded from there), so it doesn't have
    // to stay open, and the wide path conversion happens once in fopen_c
    SoLoud::DiskFile df(File::fopen_c(this->sFilePath.c_str(), "rb"));
    if(!df.getFilePtr()) {
        debugLog("Sound Error: couldn't open file {:s}", this->sFilePath);
        return;
    }

    // create the appropriate audio source based on streaming flag
    SoLoud::result result = SoLoud::SO_NO_ERROR;
    if(this->bStream) {
        // use WavStream for streaming audio (music, etc.), the voice's time-stretch stage handles speed/pitch changes
        auto *stream = new SoLoud::WavStream(cv::snd_soloud_prefer_ffmpeg.getInt() > 0);
        result = stream->loadFileToMem(&df);

        if(result == SoLoud::SO_NO_ERROR) {
            this->audioSource.reset(stream);
            this->fFrequency = stream->mBaseSamplerate;

            this->audioSource->setInaudibleBehavior(
                true, false);  // keep ticking the sound if it goes to 0 volume, and don't kill it

            logIfCV(debug_snd,
                    "SoLoudSound: Created WavStream for {:s} with speed={:f}, pitch={:f}, looping={:s}, decoder={:s}",
                    this->sFilePath, this->fSpeed, this->fPitch, this->bIsLooped ? "true" : "false",
                    decoderName(stream->mFiletype));
        } else {
            delete stream;
            debugLog("Sound Error: SoLoud::WavStream::loadFileToMem() error {} on file {:s}", result, this->sFilePath);
            return;
        }
    } else {
        // use Wav for non-streaming audio (hit sounds, effects, etc.)
        auto *wav = new SoLoud::Wav(cv::snd_soloud_prefer_ffmpeg.getInt() > 1);
        result = wav->loadFile(&df);

        if(result == SoLoud::SO_NO_ERROR) {
            this->audioSource.reset(wav);
            this->fFrequency = wav->mBaseSamplerate;

            this->audioSource->setInaudibleBehavior(
                true, true);  // keep ticking the sound if it goes to 0 volume, but do kill it if necessary
        } else {
            delete wav;
            debugLog("Sound Error: SoLoud::Wav::loadFile() error {} on file {:s}", result, this->sFilePath);
            return;
        }
    }

    // only play one music track at a time
    this->audioSource->setSingleInstance(this->bStream || !this->bIsOverlayable);
    this->audioSource->setLooping(this->bIsLooped);

    this->setAsyncReady(true);
}

SOUNDHANDLE SoLoudSound::getHandle() { return this->handle; }

// a voice plays its source through the resampler (relative play speed: speed and pitch together) and, once engaged,
// through the time-stretch stage (tempo with the pitch kept, plus an independent pitch shift). with pitch compensation
// the stage carries the speed and the pitch and the resampler only applies the frequency override, without it the
// resampler carries everything. the engine's position accounting covers both, so getStreamPosition() is exact at any
// speed, there is no latency to model and nothing to re-seek when the speed changes
void SoLoudSound::applyVoiceRate() {
    if(!this->bStream || !this->audioSource || !this->handle) return;

    const bool compensatePitch = cv::snd_speed_compensate_pitch.getBool();
    const float tempo = compensatePitch ? std::min(this->fSpeed, MAX_TEMPO) : 1.f;
    // Sound's pitch is on BASS_FX's scale (1.0 = unchanged, 1/60 per semitone), the stage takes a frequency factor
    const float pitchShift = compensatePitch ? std::exp2((this->fPitch - 1.f) * 60.f / 12.f) : 1.f;

    soloud->setTempo(this->handle, tempo);
    soloud->setPitchShift(this->handle, pitchShift);
    soloud->setRelativePlaySpeed(this->handle,
                                 (this->fSpeed / tempo) * this->fFrequency / this->audioSource->mBaseSamplerate);
}

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

    const float previousSpeed = std::exchange(this->fSpeed, speed);

    // always pushed, since the speed can stay the same while snd_speed_compensate_pitch changed (unchanged values are
    // free to re-apply, only toggling the compensation costs a seek to drop/re-engage the stage). the voice's position
    // stays continuous through a tempo change, so nothing else needs resetting
    this->applyVoiceRate();

    logIfCV(debug_snd, "SoLoudSound: Speed change ({:s}compensated pitch) {:s}: {:f}->{:f}",
            cv::snd_speed_compensate_pitch.getBool() ? "" : "un-", this->sFilePath, previousSpeed, speed);
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

    const float previousPitch = std::exchange(this->fPitch, pitch);
    this->applyVoiceRate();

    if(previousPitch != pitch)
        logIfCV(debug_snd, "SoLoudSound: Pitch change {:s}: {:f}->{:f}", this->sFilePath, previousPitch, pitch);
}

void SoLoudSound::setFrequency(float frequency) {
    if(!this->isReady() || !this->audioSource) return;

    const float previousFreq = this->fFrequency;
    // 0 means reset to default
    this->fFrequency =
        (frequency > 99.0f ? std::clamp<float>(frequency, 100.0f, 100000.0f) : this->audioSource->mBaseSamplerate);

    logIfCV(debug_snd, "SoLoudSound: Freq change {:s}: {:f}->{:f} (base: {} speed: {})", this->sFilePath, previousFreq,
            this->fFrequency, this->audioSource->mBaseSamplerate, this->fSpeed);

    if(this->bStream)
        this->applyVoiceRate();
    else
        soloud->setSamplerate(this->handle, this->fFrequency);
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

void SoLoudSound::setHandleVolume(SOUNDHANDLE handle, f32 volume) {
    if(handle != 0 && this->isReady() && soloud) {
        // soloud does not support amplified (>1.0f) volume
        const f32 clamped = std::clamp<f32>(volume, 0.f, 1.f);
        if(this->isStream()) {
            // ramp in streams
            // (hardcoded 10ms since it's annoying to adjust fadein on BASS, and there are too many backend-specific convars already)
            soloud->fadeVolume(handle, clamped, 10.f / 1000.f);
        } else {
            soloud->setVolume(handle, clamped);
        }
    }
}

// soloud-specific accessors

double SoLoudSound::getStreamPositionInSeconds() const {
    if(!this->audioSource || !this->handle) return this->interpolator.getLastInterpolatedPositionS();

    const auto now = Timing::getTimeReal();

    if(this->force_sync_position_next || (now >= this->soloud_stream_position_cache_time + 0.01)) {
        this->force_sync_position_next = false;
        this->cached_stream_position = soloud->getStreamPosition(this->handle);
        this->soloud_stream_position_cache_time = now;
    }

    return this->cached_stream_position;
}

double SoLoudSound::getSourceLengthInSeconds() const {
    if(!this->audioSource) return 0.0;
    if(this->bStream)
        return static_cast<SoLoud::WavStream *>(this->audioSource.get())->getLength();
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
