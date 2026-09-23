// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "noinclude.h"
#include "StaticPImpl.h"
#include "types.h"

// plays the short audio previews of online beatmapsets (b.<endpoint>/preview/<set id>.mp3) one at a time, from a
// size-bounded cache directory per server. the selected beatmap's music is paused while a preview plays, and resumed
// once none is playing or loading anymore
class PreviewTrackManager final {
    NOCOPY_NOMOVE(PreviewTrackManager)
   public:
    enum class State : u8 {
        NONE,         // not the current preview
        LOADING,      // being downloaded or loaded
        PLAYING,      //
        UNAVAILABLE,  // the server has none (or sent something that doesn't play), not asked for again this session
    };

    PreviewTrackManager();
    ~PreviewTrackManager();

    // this is run during Osu::update()
    void update();

    // switches to the preview of set_id, from the start unless it's already loading or playing
    void play(i32 set_id);
    // stops the current preview (if any), resuming the music it paused
    void stop();

    [[nodiscard]] State get_state(i32 set_id) const;
    // how far the current preview has played, from 0 to 1
    [[nodiscard]] f32 get_progress() const;

    // call this when cv::volume_music changed
    void apply_music_volume();

   private:
    struct Impl;
    StaticPImpl<Impl, 288> m_impl;
};
