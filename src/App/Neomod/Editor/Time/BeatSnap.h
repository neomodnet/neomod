#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "BeatmapFile/BeatmapFile.h"
#include "types.h"

#include <array>
#include <span>
#include <vector>

// the beat grid from the uninherited timing points in a map, to snap to and step through
class BeatSnap {
   public:
    // what a beat can be divided into
    static constexpr std::array<i32, 8> DIVISORS{1, 2, 3, 4, 6, 8, 12, 16};
    // one of them for any number
    [[nodiscard]] static i32 clampDivisor(i32 divisor);

    explicit BeatSnap(std::span<const neomod::BeatmapFile::TimingPoint> timingPoints);

    // the tick of `divisor` nearest to `time` in the section `from` is in (ties go to the later one). ticks fall on
    // whole milliseconds, truncated; when `from` isn't `time`, a tick at `from` doesn't count, so a step always moves
    [[nodiscard]] i32 snap(f64 time, i32 divisor, f64 from) const;
    // where `ticks` steps (negative: back) from `time` go: ticks of `divisor`, or while playing whole beats, enough of
    // them to clear 250 ms. without timing points a beat is 1000 ms
    [[nodiscard]] i32 step(i32 time, i32 ticks, i32 divisor, bool playing) const;
    // the beat length in the section `time` is in (1000 ms without one)
    [[nodiscard]] f64 beatLengthAt(f64 time) const;
    // how far `time` is from the nearest tick at any divisor or from the next section start, in ms (from where ticks
    // fall exactly, not truncated; 0 without beats)
    [[nodiscard]] f64 offGrid(f64 time) const;

   private:
    struct Section {
        f64 time;
        f64 beatLength;
    };
    // the last section starting at or before `time`, the first one before them all; the first section start pulled back
    // before 0 by whole beats
    [[nodiscard]] Section sectionAt(f64 time) const;

    std::vector<Section> sections;
    f64 firstStart{0.0};  // the first section start, pulled back
};
