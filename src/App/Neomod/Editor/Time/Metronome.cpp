// Copyright (c) 2026, WH, All rights reserved.
#include "Metronome.h"

#include "Clock.h"
#include "ContainerRanges.h"
#include "Document.h"
#include "Osu.h"
#include "Skin.h"
#include "SoundEngine.h"

#include <algorithm>
#include <cmath>
#include <iterator>

using TimingPoint = neomod::BeatmapFile::TimingPoint;

namespace Metronome {
namespace {
// a frame longer than this (in music time) is a hitch, whose beats stay silent
constexpr i32 MAX_FRAME_MS{300};

// the uninherited point in force at `time` (the first one before them all), if it has a tempo
const TimingPoint *sectionAt(std::span<const TimingPoint> points, f64 time) {
    auto section = Mc::ranges::upper_bound(points, time, {}, &TimingPoint::time);
    while(section != points.begin() && !std::prev(section)->uninherited) --section;
    section = section != points.begin() ? std::prev(section) : Mc::ranges::find_if(points, &TimingPoint::uninherited);
    return section != points.end() && section->beatLength > 0.0 ? &*section : nullptr;
}
}  // namespace

std::optional<Beat> beatAt(std::span<const TimingPoint> points, f64 time) {
    const TimingPoint *section = sectionAt(points, time);
    if(!section) return std::nullopt;
    const f64 beats = (time - section->time) / section->beatLength;
    const i32 meter = std::max(section->meter, 1);
    const i64 whole = (i64)std::floor(beats);
    return Beat{.inBar = (i32)(((whole % meter) + meter) % meter), .meter = meter, .phase = beats - (f64)whole};
}

Tick crossed(std::span<const TimingPoint> points, i32 from, i32 to) {
    const TimingPoint *section = sectionAt(points, to);
    if(!section || to <= from) return Tick::NONE;
    // a time before the section point is before its first beat
    const f64 before = std::floor((from - section->time) / section->beatLength);
    const f64 now = std::floor((to - section->time) / section->beatLength);
    if(now <= before) return Tick::NONE;
    const i64 meter = std::max(section->meter, 1);
    return (((i64)now % meter) + meter) % meter == 0 ? Tick::BAR : Tick::BEAT;
}

void play(const Clock &clock, const Document &document) {
    const Skin *skin = osu->getSkin();
    const i32 from = clock.getPreviousTime();
    const i32 to = clock.getTime();
    if(!skin || !clock.isPlaying() || clock.isSeekFrame() || to - from > MAX_FRAME_MS) return;
    switch(crossed(document.getTimingPoints(), from, to)) {
        case Tick::NONE:
            break;
        case Tick::BEAT:
            soundEngine->play(skin->s_metronome_low);
            break;
        case Tick::BAR:
            soundEngine->play(skin->s_metronome_high);
            break;
    }
}
}  // namespace Metronome
