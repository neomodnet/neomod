#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "BeatmapFile/BeatmapFile.h"
#include "Document.h"
#include "types.h"

#include <optional>
#include <span>

// changes to the timing points in a map, and what follows from them for its objects
namespace Retiming {
using TimingPoint = neomod::BeatmapFile::TimingPoint;

// the beat length for a tempo, and the one an inherited point has for a slider velocity, to the fifteen significant
// digits such a line is written with
[[nodiscard]] f64 beatLengthOf(f64 bpm);
[[nodiscard]] f64 velocityBeatLength(f64 velocity);

// the tempo of taps along the music (their times, in order), as a whole number of beats a minute: a line through
// them, where a gap of about twice the usual counts as two beats (a tap left out). none before the fourth tap
[[nodiscard]] std::optional<f64> tappedBpm(std::span<const i32> taps);

// a timing point line and what it becomes
struct Edit {
    Document::Id id;
    TimingPoint point;
};
// makes these timing point lines these points. with `carry`, an uninherited point whose time or beat length changes
// takes its section along: the objects (spinner ends too) and the inherited points from it to the next uninherited
// point (for the first one, those before it too) keep their place in beats from it.
// - what was on a grid tick goes to the tick at that divisor nearest to its new place
// - another object goes to the nearest millisecond
// - another inherited point goes to the millisecond at or before its place, so that it still comes before what it
//   came before, and stays in its section (a millisecond before the next uninherited point at the latest)
// it reads the document lines as they are. within a Document::drag() it so carries from where things were before the
// gesture: times are whole milliseconds, and a tempo changed in many small steps must not round at each
void apply(Document::Change &c, const Document &document, std::span<const Edit> edits, bool carry);

// puts every object start on the nearest tick at `divisor` in its section, spinner ends too (a tick after the start
// at least). inherited points go with them as in apply(): still before what they came before, and in their sections
void resnap(Document::Change &c, const Document &document, i32 divisor);
}  // namespace Retiming
