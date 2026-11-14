#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "MusicTrack.h"
#include "noinclude.h"
#include "types.h"

#include <optional>

class DatabaseBeatmap;

// the editor time: from a music track of its own while the editor shows a map. plays, pauses, seeks and sets the rate
// there; the time in map terms is read once a frame
class Clock {
    NOCOPY_NOMOVE(Clock)
   public:
    Clock() = default;
    ~Clock() = default;

    // loads the music for `map`, paused at timeMS (version: the file format, which its offset follows)
    void attach(DatabaseBeatmap &map, i32 version, i32 timeMS);
    // frees the music
    void detach();
    // the map record is another object now (its file was saved): the music goes on for that one
    void reselect(DatabaseBeatmap &map);
    [[nodiscard]] bool isAttached() const { return this->track.has_value(); }
    // the record for the map it shows (also without music: the track only takes maps with an audio file)
    [[nodiscard]] DatabaseBeatmap *getMap() const { return this->map; }

    // reads the time for this frame; once a frame, before anything reads it
    void update();
    [[nodiscard]] i32 getTime() const { return this->time; }
    [[nodiscard]] i32 getPreviousTime() const { return this->previousTime; }
    // the time jumped since the last frame (a seek, not playback)
    [[nodiscard]] bool isSeekFrame() const { return this->seekFrame; }
    [[nodiscard]] bool isPlaying() const { return this->track && this->track->isPlaying(); }
    [[nodiscard]] u32 getLength() const { return this->track ? this->track->getLengthMS() : 0; }

    void play();
    void pause();
    void togglePause();
    // to timeMS, kept within the song
    void seek(i32 timeMS);
    // from 0.25 to 1, keeping the pitch
    void setRate(f32 rate);
    [[nodiscard]] f32 getRate() const { return this->rate; }

   private:
    // how far the track time is off from the map time
    [[nodiscard]] i32 getOffset() const;

    std::optional<MusicTrack> track;
    DatabaseBeatmap *map{nullptr};
    i32 version{14};
    i32 time{0};
    i32 previousTime{0};
    bool seeked{false};
    bool seekFrame{false};
    f32 rate{1.f};
};
