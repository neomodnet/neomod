#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "HitObjects.h"
#include "noinclude.h"
#include "types.h"

#include <span>

class Clock;
class Document;
class MapObjects;

// the hitsounds a perfect play makes while the editor music plays: the cues the time passes (none for what a seek jumps
// over), and slider slides while the time is inside them
class Playback {
    NOCOPY_NOMOVE(Playback)
   public:
    Playback() = default;
    ~Playback();

    // once a frame, after the clock update
    void update(const Clock &clock, const MapObjects &objects, const Document &document);
    // stops a slide that plays
    void stop();

    // the cues (in time order) that play when the time goes from `from` to `to`: after `from`, up to `to`
    [[nodiscard]] static std::span<const neomod::HitObject::SoundCue> crossed(
        std::span<const neomod::HitObject::SoundCue> cues, i32 from, i32 to);

   private:
    bool sliding{false};
};
