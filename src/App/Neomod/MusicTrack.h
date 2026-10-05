// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "noinclude.h"
#include "types.h"
#include "Registration.h"

#include <memory>
#include <string>

class DatabaseBeatmap;
class GameplayInterpolator;
class Sound;

// the selected beatmap's music: one stream at a time, the wait for its map's loudness before it starts, its volume
// (volume_music and loudness normalization), its recovery after output device changes, and the clock that everything
// following the music reads. what it plays and when is up to its users (the screens that select maps, gameplay)
class MusicTrack final {
    NOCOPY_NOMOVE(MusicTrack)
   public:
    MusicTrack();
    ~MusicTrack();

    // finishes a load once the file and the map's loudness are in, resumes the music after a device change and samples
    // the clock; run once per frame, before anything reads the clock
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

    // whether it plays now (from the start again once it has played to its end)
    bool play();
    void pause();
    // for the buttons and keys that pause and resume the music
    void togglePause();
    // also after the music has played to its end; the clock reads the new time at once
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
    [[nodiscard]] bool isLooped() const { return this->loop; }
    [[nodiscard]] bool isSlowedDown() const { return this->slowdown < 1.f; }
    // the clock: this frame's time in ms, without offsets (the stream's position, smoothed by interpolate_music_pos)
    [[nodiscard]] i32 getTime() const { return this->time; }
    // what to add to the time for a map's time: the universal offsets and the slow-rate compensation at the track's
    // rate, and with a map its local, online and old-version offsets
    [[nodiscard]] i32 getOffset(const DatabaseBeatmap *map) const;
    [[nodiscard]] u32 getLengthMS() const;
    [[nodiscard]] f64 getPositionPct() const;
    [[nodiscard]] f32 getSpeed() const { return this->speed; }

   private:
    // what a finished load still waits for, then the stream is set up to play (paused)
    void finishLoad();
    // a stream that played to its end has no voice left (SoLoud) to seek: a new one, paused, with the track's rate
    void makeVoice();
    void applyRate();
    void onDeviceChangeBefore();
    void onDeviceChangeAfter();
    [[nodiscard]] f32 getVolume() const;

    Sound *stream{nullptr};
    std::string path;  // what the stream plays, or is loading
    DatabaseBeatmap *map{nullptr};
    Mc::Registration deviceChangeListener;

    // the transport, kept for the stream a load or a device change makes and for its new voices
    bool loop{false};
    f32 speed{1.f};
    f32 pitch{1.f};
    bool preservePitch{true};
    f32 baseFrequency{0.f};
    f32 slowdown{1.f};

    i32 time{0};
    bool seeked{false};      // restarts the smoothing at the next sample
    bool seekOnLoad{false};  // the stream wasn't loaded yet when the time was set
    std::unique_ptr<GameplayInterpolator> smoothing;

    bool loadFinished{true};

    // across a device change
    bool deviceChanging{false};
    bool resumeAfterDeviceChange{false};
    bool resumeScheduled{false};
};
