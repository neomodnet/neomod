#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "BeatmapFile/BeatmapFile.h"
#include "types.h"

#include <optional>
#include <span>

class Clock;
class Document;

// the beats from the uninherited timing points in a map, counted in bars from each point, and their ticks while the
// music plays
namespace Metronome {
struct Beat {
    i32 inBar;  // from 0, the first in a bar
    i32 meter;  // how many a bar has
    f64 phase;  // how far the time is into the beat, from 0 to 1
};
// the beat `time` is in, by the uninherited point in force (the first one before them all); none without one
[[nodiscard]] std::optional<Beat> beatAt(std::span<const neomod::BeatmapFile::TimingPoint> points, f64 time);

enum class Tick : u8 { NONE, BEAT, BAR };
// what ticks when the time goes from `from` to `to`: a beat was passed (after `from`, up to `to`), or the first in a
// bar
[[nodiscard]] Tick crossed(std::span<const neomod::BeatmapFile::TimingPoint> points, i32 from, i32 to);

// once a frame, after the clock update: plays the tick for a beat the music passed
void play(const Clock &clock, const Document &document);
}  // namespace Metronome
