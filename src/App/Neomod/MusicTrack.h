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
    // the clock; run once per frame, before anything reads the clock. whether a load finished (the music may have
    // started with it)
    bool update();

    enum class Loaded : u8 {
        NO_AUDIO,   // the map has no audio file, nothing changed
        SAME_FILE,  // its file is the one already loaded, which goes on as it was
        NEW_FILE,   // its file is loading, or loaded (a sync load)
    };
    // makes `map`'s audio the track's: loaded unless it's the file already loaded (or `reload`), and counted as loading
    // until the map's loudness is in when normalization wants it. `map` is kept for the volume and the restart point
    // until the next load or releaseMap(). a play() asked for before (still waiting for a load) is dropped
    Loaded load(DatabaseBeatmap *map, bool async, bool reload = false);
    // forgets the map, before the maps it could point into go away (database loads), and a play() still waiting for its
    // loudness: the selection after the load asks again
    void releaseMap();
    // stops and frees the stream, e.g. before deleting the file it reads
    void unload();

    // while a hold lives, nothing changes the selected map and its music on its own (an install's auto-select, a removed
    // set's reselection, the main menu's next song); its holder still selects as usual
    [[nodiscard]] Mc::Registration hold();
    [[nodiscard]] bool isHeld() const { return this->holds > 0; }

    // a load or its loudness wait hasn't finished
    [[nodiscard]] bool isLoading() const;
    // the stream is loaded and can play
    [[nodiscard]] bool isReady() const;
    // nothing was loaded (or it was unloaded)
    [[nodiscard]] bool isEmpty() const { return this->stream == nullptr; }

    // for plays: the time also runs before the song and past its end at the track's rate while it plays (the song
    // paused there and started when the time comes into it), and setPosition() can go there. off: the time keeps to the
    // song, which stops at its end
    void setVirtualTime(bool on);

    // plays now, or once the load and its loudness wait are done (from the start again once it has played to its end)
    void play();
    void pause();
    // for the buttons and keys that pause and resume the music
    void togglePause();
    // plays from the restart point: the map's preview time, or 40% into the song without one
    void restart();
    // also after the music has played to its end; the clock reads the new time at once. outside the song only in virtual
    // time (otherwise before it is its start)
    void setPosition(i32 ms);
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
    // what a finished load still waits for, then the stream is set up and plays if asked to; whether it finished now
    bool finishLoad();
    // a stream that played to its end has no voice left (SoLoud) to seek: a new one, paused, with the track's rate
    void makeVoice();
    // the stream from `ms`, playing if the track does
    void enterSong(i32 ms);
    void applyRate();
    [[nodiscard]] u32 getRestartPoint() const;
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
    f64 lastPosition{0.0};  // the stream's, at the last sample
    bool seeked{false};     // restarts the smoothing at the next sample
    f64 lastUpdate{0.0};    // real time

    bool virtualTime{false};
    bool playing{false};      // asked to play, which the time follows in virtual time
    bool outsideSong{false};  // in virtual time: before the song or past its end
    f64 virtualMS{0.0};       // the time there
    // asked for while the stream was still loading
    bool seekOnLoad{false};
    bool restartOnLoad{false};
    bool playOnLoad{false};
    std::unique_ptr<GameplayInterpolator> smoothing;

    bool loadFinished{true};
    u32 holds{0};

    // across a device change
    bool deviceChanging{false};
    bool resumeAfterDeviceChange{false};
};
