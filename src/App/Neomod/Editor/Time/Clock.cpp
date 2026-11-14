// Copyright (c) 2026, WH, All rights reserved.
#include "Clock.h"

#include <algorithm>
#include <cassert>
#include <utility>

void Clock::attach(DatabaseBeatmap &map, i32 version, i32 timeMS) {
    assert(!this->track && "a map shown twice");
    this->track.emplace();
    this->map = &map;
    this->track->load(&map, true);
    this->version = version;
    this->setRate(1.f);
    this->seek(timeMS);
}

void Clock::detach() {
    this->track.reset();
    this->map = nullptr;
}

void Clock::reselect(DatabaseBeatmap &map) {
    if(!this->track) return;
    this->map = &map;
    // the same audio file goes on where it is; one loaded anew, e.g. after it didn't load before, starts at the time
    if(this->track->load(&map, true) == MusicTrack::Loaded::NEW_FILE) this->seek(this->time);
}

void Clock::update() {
    this->previousTime = this->time;
    if(this->track) {
        this->track->update();
        this->time = this->track->getTime() + this->getOffset();
    }
    this->seekFrame = std::exchange(this->seeked, false);
}

void Clock::play() {
    if(this->track) this->track->play();
}

void Clock::pause() {
    if(this->track) this->track->pause();
}

void Clock::togglePause() {
    if(this->track) this->track->togglePause();
}

void Clock::seek(i32 timeMS) {
    // the length is 0 while the music still loads
    const u32 length = this->getLength();
    this->time = length > 0 ? std::clamp(timeMS, 0, (i32)length) : std::max(timeMS, 0);
    this->seeked = true;
    if(this->track) this->track->setPosition(this->time - this->getOffset());
}

void Clock::setRate(f32 rate) {
    this->rate = std::clamp(rate, 0.25f, 1.5f);
    if(this->track) this->track->setRate(this->rate, true);
}

i32 Clock::getOffset() const {
    // the offsets the map has belong to the player, not the map
    return this->track->getOffset(nullptr, this->version);
}
