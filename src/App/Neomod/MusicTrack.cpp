// Copyright (c) 2026, WH, All rights reserved.
#include "MusicTrack.h"

#include "DatabaseBeatmap.h"
#include "Logging.h"
#include "MakeDelegateWrapper.h"
#include "OsuConVars.h"
#include "ResourceManager.h"
#include "Sound.h"
#include "SoundEngine.h"
#include "SongBrowser/VolNormalization.h"

#include <algorithm>
#include <cmath>

MusicTrack::MusicTrack() {
    this->deviceChangeListener =
        soundEngine->addDeviceChangeListener(SA::MakeDelegate<&MusicTrack::onDeviceChangeBefore>(this),
                                             SA::MakeDelegate<&MusicTrack::onDeviceChangeAfter>(this));
}

MusicTrack::~MusicTrack() {
    if(this->stream) resourceManager->destroyResource(this->stream, ResourceDestroyFlags::RDF_FORCE_BLOCKING);
}

void MusicTrack::update() {
    this->finishLoad();

    if(this->resumeScheduled && soundEngine->isReady()) {
        if(this->stream && !this->stream->isPlaying()) soundEngine->play(this->stream);
        this->resumeScheduled = false;
    }
}

bool MusicTrack::load(DatabaseBeatmap *map, bool async, bool reload) {
    std::string path = map->getFullSoundFilePath();
    if(path.empty()) {
        debugLog("no music file for {}!", map->getFilePath());
        return false;
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
    }
    this->path = std::move(path);

    // a sync load (or none) is done now, an async one is finished by update()
    if(!async || skip) this->finishLoad();
    return true;
}

void MusicTrack::releaseMap() { this->map = nullptr; }

void MusicTrack::unload() {
    if(this->stream) {
        resourceManager->destroyResource(this->stream);
        this->stream = nullptr;
    }
    this->path.clear();
    this->loadFinished = true;
}

void MusicTrack::finishLoad() {
    if(this->loadFinished || !this->stream) return;
    if(resourceManager->isLoadingResource(this->stream)) return;

    // hold off until loudness has landed if normalization is currently enabled, so the song doesn't briefly play at
    // unnormalized volume. fallback_loudness is non-zero, so this never hangs: the priority worker always writes a
    // non-zero value (real or fallback). re-checked each frame: toggling normalization off while waiting lets playback
    // proceed
    if(this->map && cv::normalize_loudness.getBool() && this->map->loudness.load(std::memory_order_acquire) == 0.f) {
        return;
    }

    this->loadFinished = true;

    // (a file that didn't need loading can still be playing, and BASS refuses to enqueue a playing stream)
    if(!this->stream->isReady() || (!this->stream->isPlaying() && !soundEngine->enqueue(this->stream))) {
        logIf(cv::debug_osu.getBool() || cv::debug_snd.getBool(), "failed to enqueue music at {}",
              this->stream->getFilePath());
        return;
    }

    // ready and enqueued (or still playing)
    this->stream->setBaseVolume(this->getVolume());
    this->baseFrequency = this->stream->getFrequency();
    this->stream->setSpeed(this->speed, this->preservePitch);
    this->stream->setPitch(this->pitch);
}

bool MusicTrack::isLoading() const { return this->stream && !this->loadFinished; }

bool MusicTrack::isReady() const { return this->stream && this->stream->isReady(); }

bool MusicTrack::play() { return this->stream && soundEngine->play(this->stream); }

void MusicTrack::pause() {
    if(this->stream) soundEngine->pause(this->stream);
}

void MusicTrack::togglePause() {
    if(this->isPlaying()) {
        this->pause();
    } else {
        this->play();
    }
}

void MusicTrack::setPosition(u32 ms) {
    if(this->stream) this->stream->setPositionMS(ms);
}

void MusicTrack::setLoop(bool loop) {
    if(this->stream) this->stream->setLoop(loop);
}

void MusicTrack::setRate(f32 speed, f32 pitch, bool preservePitch) {
    this->speed = speed;
    this->pitch = pitch;
    this->preservePitch = preservePitch;
    if(this->stream) {
        this->stream->setSpeed(speed, preservePitch);
        this->stream->setPitch(pitch);
    }
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

bool MusicTrack::isLooped() const { return this->stream && this->stream->isLooped(); }

u32 MusicTrack::getPositionMS() const { return this->stream ? this->stream->getPositionMS() : 0; }

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

f64 MusicTrack::getPositionPct() const { return this->stream ? this->stream->getPositionPct() : 0.0; }

f32 MusicTrack::getSpeed() const { return this->stream ? this->stream->getSpeed() : 1.f; }

void MusicTrack::onDeviceChangeBefore() {
    // (when a device fails to open, SoLoud reports the change again for the previous one, with the stream stopped)
    if(std::exchange(this->deviceChanging, true)) return;
    this->resumeAfterDeviceChange = this->isPlaying();
    this->positionBeforeDeviceChange = this->getPositionMS();
}

void MusicTrack::onDeviceChangeAfter() {
    this->deviceChanging = false;
    if(!this->stream) return;

    // the stream again from its file (BASS frees every stream along with its device), where it was
    // TODO(spec): is this even right? why do we only unload music after already destroying/restarting soundengine
    const bool loop = this->stream->isLooped();
    resourceManager->destroyResource(this->stream);
    this->stream = resourceManager->loadSoundAbs(this->path, "BEATMAP_MUSIC", true /* stream */, false, false);
    this->loadFinished = false;
    this->finishLoad();

    this->stream->setLoop(loop);
    this->stream->setPositionMS(this->positionBeforeDeviceChange);
    if(this->resumeAfterDeviceChange) this->resumeScheduled = true;
}
