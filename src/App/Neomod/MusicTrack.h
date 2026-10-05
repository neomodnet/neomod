// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "noinclude.h"
#include "types.h"
#include "Registration.h"

#include <string>

class DatabaseBeatmap;
class Sound;

// the selected beatmap's music: one stream at a time, the wait for its map's loudness before it starts, its volume
// (volume_music and loudness normalization) and its recovery after output device changes. what it plays and when is up
// to its users (the screens that select maps, gameplay)
class MusicTrack final {
    NOCOPY_NOMOVE(MusicTrack)
   public:
    MusicTrack();
    ~MusicTrack();

    // finishes a load once the file and the map's loudness are in, and resumes the music after a device change; run
    // once per frame
    void update();

    // makes `map`'s audio the track's: loaded unless it's the file already loaded (or `reload`), and counted as loading
    // until the map's loudness is in when normalization wants it. `map` is kept for the volume until the next load or
    // releaseMap(). false (and nothing changes) for a map without an audio file
    bool load(DatabaseBeatmap *map, bool async, bool reload = false);
    // forgets the map, before the maps it could point into go away (database loads)
    void releaseMap();
    // stops and frees the stream, e.g. before deleting the file it reads
    void unload();

    // a load or its loudness wait hasn't finished
    [[nodiscard]] bool isLoading() const;
    // the stream is loaded and can play
    [[nodiscard]] bool isReady() const;
    // nothing was loaded (or it was unloaded)
    [[nodiscard]] bool isEmpty() const { return this->stream == nullptr; }

    // whether it plays now
    bool play();
    void pause();
    // for the buttons and keys that pause and resume the music
    void togglePause();
    void setPosition(u32 ms);
    void setLoop(bool loop);
    void setRate(f32 speed, f32 pitch, bool preservePitch);
    // slows the music down by lowering its frequency to `factor` of its own (the fail animation), until endSlowdown()
    void setSlowdown(f32 factor);
    void endSlowdown();
    // after a change of volume_music or of the loudness normalization
    void updateVolume();

    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool isFinished() const;
    [[nodiscard]] bool isLooped() const;
    [[nodiscard]] bool isSlowedDown() const { return this->slowdown < 1.f; }
    [[nodiscard]] u32 getPositionMS() const;
    [[nodiscard]] u32 getLengthMS() const;
    [[nodiscard]] f64 getPositionPct() const;
    [[nodiscard]] f32 getSpeed() const;

   private:
    // what a finished load still waits for, then the stream is set up to play (paused)
    void finishLoad();
    void onDeviceChangeBefore();
    void onDeviceChangeAfter();
    [[nodiscard]] f32 getVolume() const;

    Sound *stream{nullptr};
    std::string path;  // what the stream plays, or is loading
    DatabaseBeatmap *map{nullptr};
    Mc::Registration deviceChangeListener;

    // the rate, kept for the voices a reload or a device change makes
    f32 speed{1.f};
    f32 pitch{1.f};
    bool preservePitch{true};

    f32 baseFrequency{0.f};
    f32 slowdown{1.f};
    bool loadFinished{true};

    // across a device change
    bool deviceChanging{false};
    bool resumeAfterDeviceChange{false};
    bool resumeScheduled{false};
    u32 positionBeforeDeviceChange{0};
};
