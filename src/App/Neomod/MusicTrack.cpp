// Copyright (c) 2026, WH, All rights reserved.
#include "MusicTrack.h"

#include "DatabaseBeatmap.h"
#include "Logging.h"
#include "MakeDelegateWrapper.h"
#include "OsuConVars.h"
#include "PlaybackInterpolator.h"
#include "ResourceManager.h"
#include "Sound.h"
#include "SoundEngine.h"
#include "SongBrowser/VolNormalization.h"
#include "Timing.h"

#include <algorithm>
#include <cmath>
#include <utility>

MusicTrack::MusicTrack() {
    this->deviceChangeListener =
        soundEngine->addDeviceChangeListener(SA::MakeDelegate<&MusicTrack::onDeviceChangeBefore>(this),
                                             SA::MakeDelegate<&MusicTrack::onDeviceChangeAfter>(this));
}

MusicTrack::~MusicTrack() {
    if(this->stream) resourceManager->destroyResource(this->stream, ResourceDestroyFlags::RDF_FORCE_BLOCKING);
}

bool MusicTrack::update() {
    const bool finished = this->finishLoad();

    if(this->resumeScheduled && soundEngine->isReady()) {
        if(this->stream && !this->stream->isPlaying()) soundEngine->play(this->stream);
        this->resumeScheduled = false;
    }

    // the clock: the stream's position once a frame, smoothed (interpolate_music_pos: lazer's unless McOsu's or none),
    // which every reader gets. a stream without a voice (before its first one, after its end) has no position to read,
    // so the time stays
    if(!this->isReady() || this->deviceChanging || this->stream->isFinished()) return finished;

    const f64 now = Timing::getTimeReal<f64>();
    const f64 position = (f64)this->stream->getPositionUS() / 1000.0;
    // the smoothing starts over after a seek and wherever the position went back (a loop, a new voice), it never
    // runs from its old time into the new one
    const bool wentBack = position < std::exchange(this->lastPosition, position);
    bool restart = std::exchange(this->seeked, false) || wentBack;

    const i32 interpolation = cv::interpolate_music_pos.getInt();
    if(interpolation == 2 && (!this->smoothing || this->smoothing->getType() != 2)) {
        this->smoothing = std::make_unique<McOsuInterpolator>();
        restart = true;
    } else if(interpolation != 0 && interpolation != 2 && (!this->smoothing || this->smoothing->getType() != 3)) {
        this->smoothing = std::make_unique<TachyonInterpolator>();
        restart = true;
    }

    if(interpolation == 0) {
        this->time = (i32)std::round(position);
    } else {
        if(restart) this->smoothing->reset(position, now);
        this->time = (i32)this->smoothing->update(position, now, this->speed, false, this->getLengthMS(),
                                                  this->stream->isPlaying());
    }

    logIf(cv::debug_snd.getInt() > 1, "music clock: real time {:.6f} position {:.3f} time {}", now, position,
          this->time);
    return finished;
}

MusicTrack::Loaded MusicTrack::load(DatabaseBeatmap *map, bool async, bool reload) {
    std::string path = map->getFullSoundFilePath();
    if(path.empty()) {
        debugLog("no music file for {}!", map->getFilePath());
        return Loaded::NO_AUDIO;
    }
    this->map = map;

    // (a load in flight only shows its path in getFilePath() once it's done)
    const bool loaded = this->stream && this->stream->isReady();
    const bool skip = !reload && loaded && path == this->stream->getFilePath();

    logIf(cv::debug_osu.getBool() || cv::debug_snd.getBool(),
          "reload: {} async: {} existing music: {} existing music loaded successfully: {} skipping: {}", reload, async,
          !!this->stream, loaded, skip);

    // finished by finishLoad() even if the file doesn't need loading: the map can still be missing its loudness (e.g.
    // the db's copy of a preloaded main menu map)
    this->loadFinished = false;
    this->playOnLoad = false;

    // if normalization is enabled and we don't yet have loudness for this map, kick off a priority calc in parallel
    // with the audio decode, finishLoad() holds the music back until it lands (avoiding an audible volume snap)
    if(cv::normalize_loudness.getBool() && map->loudness.load(std::memory_order_acquire) == 0.f) {
        VolNormalization::request_priority(map);
    }

    if(!skip) {
        if(this->stream) {
            this->stream->rebuild(path, async);
        } else {
            if(async) resourceManager->requestNextLoadAsync();
            this->stream = resourceManager->loadSoundAbs(path, "BEATMAP_MUSIC", true /* stream */, false, false);
        }
        this->time = 0;
        this->seeked = true;
        this->seekOnLoad = false;
        this->restartOnLoad = false;
    }
    this->path = std::move(path);

    // a sync load (or none) is done now, an async one is finished by update()
    if(!async || skip) this->finishLoad();
    return skip ? Loaded::SAME_FILE : Loaded::NEW_FILE;
}

void MusicTrack::releaseMap() {
    this->map = nullptr;
    // (without the map there is no loudness to wait for)
    this->playOnLoad = false;
}

void MusicTrack::unload() {
    if(this->stream) {
        resourceManager->destroyResource(this->stream);
        this->stream = nullptr;
    }
    this->path.clear();
    this->loadFinished = true;
    this->time = 0;
    this->seekOnLoad = false;
    this->restartOnLoad = false;
    this->playOnLoad = false;
}

bool MusicTrack::finishLoad() {
    if(this->loadFinished || !this->stream) return false;
    if(resourceManager->isLoadingResource(this->stream)) return false;

    // hold off until loudness has landed if normalization is currently enabled, so the song doesn't briefly play at
    // unnormalized volume. fallback_loudness is non-zero, so this never hangs: the priority worker always writes a
    // non-zero value (real or fallback). re-checked each frame: toggling normalization off while waiting lets playback
    // proceed
    if(this->map && cv::normalize_loudness.getBool() && this->map->loudness.load(std::memory_order_acquire) == 0.f) {
        return false;
    }

    this->loadFinished = true;

    // (a file that didn't need loading can still be playing, and BASS refuses to enqueue a playing stream)
    if(!this->stream->isReady() || (!this->stream->isPlaying() && !soundEngine->enqueue(this->stream))) {
        logIf(cv::debug_osu.getBool() || cv::debug_snd.getBool(), "failed to enqueue music at {}",
              this->stream->getFilePath());
        return true;
    }

    // ready and enqueued (or still playing)
    this->stream->setBaseVolume(this->getVolume());
    this->baseFrequency = this->stream->getFrequency();
    this->stream->setLoop(this->loop);
    this->applyRate();
    if(std::exchange(this->restartOnLoad, false)) {
        this->time = (i32)this->getRestartPoint();
        this->seekOnLoad = true;
    }
    if(std::exchange(this->seekOnLoad, false)) {
        this->stream->setPositionMS((u32)this->time);
        this->seeked = true;
    }
    if(std::exchange(this->playOnLoad, false)) this->play();
    return true;
}

void MusicTrack::makeVoice() {
    if(this->stream->isFinished() && soundEngine->enqueue(this->stream)) this->applyRate();
}

void MusicTrack::applyRate() {
    this->stream->setSpeed(this->speed, this->preservePitch);
    this->stream->setPitch(this->pitch);
}

bool MusicTrack::isLoading() const { return this->stream && !this->loadFinished; }

bool MusicTrack::isReady() const { return this->stream && this->stream->isReady(); }

void MusicTrack::play() {
    if(!this->isReady() || this->isLoading()) {
        this->playOnLoad = true;
        return;
    }

    // (a new voice starts with the file's rate)
    const bool newVoice = this->stream->isFinished();
    if(soundEngine->play(this->stream) && newVoice) this->applyRate();
}

void MusicTrack::pause() {
    this->playOnLoad = false;
    if(this->stream) soundEngine->pause(this->stream);
}

void MusicTrack::togglePause() {
    if(this->isPlaying() || this->playOnLoad) {
        this->pause();
    } else {
        this->play();
    }
}

void MusicTrack::restart() {
    if(this->isReady()) {
        this->setPosition(this->getRestartPoint());
    } else {
        // (the restart point can need the song's length)
        this->restartOnLoad = true;
        this->seekOnLoad = false;
    }
    this->play();
}

u32 MusicTrack::getRestartPoint() const {
    const i32 preview = this->map ? this->map->getPreviewTime() : -1;
    return preview >= 0 ? (u32)preview : (u32)(this->getLengthMS() * 0.4f);
}

void MusicTrack::setPosition(u32 ms) {
    this->time = (i32)ms;
    this->seeked = true;
    if(!this->isReady()) {
        this->seekOnLoad = true;
        this->restartOnLoad = false;
        return;
    }

    this->makeVoice();
    this->stream->setPositionMS(ms);
}

void MusicTrack::setLoop(bool loop) {
    this->loop = loop;
    if(this->isReady()) this->stream->setLoop(loop);
}

void MusicTrack::setRate(f32 speed, f32 pitch, bool preservePitch) {
    this->speed = speed;
    this->pitch = pitch;
    this->preservePitch = preservePitch;
    if(this->stream) this->applyRate();
}

void MusicTrack::setSlowdown(f32 factor) {
    this->slowdown = factor;
    if(this->stream) this->stream->setFrequency(std::max(this->baseFrequency * factor, 100.f));
}

void MusicTrack::endSlowdown() {
    this->slowdown = 1.f;
    // (0 is the file's own frequency)
    if(this->stream) this->stream->setFrequency(0.f);
}

void MusicTrack::updateVolume() {
    if(this->stream) this->stream->setBaseVolume(this->getVolume());
}

f32 MusicTrack::getVolume() const {
    const f32 volume = cv::volume_music.getFloat();
    if(!cv::normalize_loudness.getBool() || !this->map) return volume;

    const f32 loudness = this->map->loudness.load(std::memory_order_acquire);
    return loudness != 0.f ? volume * std::pow(10.f, (cv::loudness_target.getFloat() - loudness) / 20.f) : volume;
}

bool MusicTrack::isPlaying() const { return this->stream && this->stream->isPlaying(); }

bool MusicTrack::isFinished() const { return this->stream && this->stream->isFinished(); }

i32 MusicTrack::getOffset(const DatabaseBeatmap *map) const {
    i32 offset =
        (i32)((cv::universal_offset.getFloat() + cv::universal_offset_hardcoded_blamepeppy.getFloat()) * this->speed) +
        cv::universal_offset_norate.getInt();
    if(this->speed < 1.f && cv::compensate_music_speed.getBool() && this->preservePitch) {
        offset += (i32)(((1.f - this->speed) / 0.75f) * 5);  // osu (new)
    }
    if(map) {
        offset -= map->getLocalOffset() + map->getOnlineOffset();
        if(map->getVersion() < 5) offset -= cv::old_beatmap_offset.getInt();
    }
    return offset;
}

u32 MusicTrack::getLengthMS() const { return this->stream ? this->stream->getLengthMS() : 0; }

f64 MusicTrack::getPositionPct() const {
    const u32 length = this->getLengthMS();
    return length > 0 ? std::clamp((f64)this->time / length, 0.0, 1.0) : 0.0;
}

void MusicTrack::onDeviceChangeBefore() {
    // (when a device fails to open, SoLoud reports the change again for the previous one, with the stream stopped)
    if(std::exchange(this->deviceChanging, true)) return;
    this->resumeAfterDeviceChange = this->isPlaying();
}

void MusicTrack::onDeviceChangeAfter() {
    this->deviceChanging = false;
    if(!this->stream) return;

    // the stream again from its file (BASS frees every stream along with its device), where it was
    // TODO(spec): is this even right? why do we only unload music after already destroying/restarting soundengine
    resourceManager->destroyResource(this->stream);
    this->stream = resourceManager->loadSoundAbs(this->path, "BEATMAP_MUSIC", true /* stream */, false, false);
    this->loadFinished = false;
    this->seekOnLoad = true;
    this->finishLoad();

    if(this->resumeAfterDeviceChange) this->resumeScheduled = true;
}
