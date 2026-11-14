// Copyright (c) 2026, WH, All rights reserved.
#include "BeatSnap.h"

#include "ContainerRanges.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace {
// a file can say any beat length: only a finite positive one has beats
bool hasBeats(f64 beatLength) { return beatLength > 0.0 && std::isfinite(beatLength); }

// a time in whole milliseconds, truncated (within what they can be)
i32 truncated(f64 time) {
    return (i32)std::clamp(time, (f64)std::numeric_limits<i32>::min(), (f64)std::numeric_limits<i32>::max());
}
}  // namespace

BeatSnap::BeatSnap(std::span<const neomod::BeatmapFile::TimingPoint> timingPoints) {
    for(const auto &point : timingPoints) {
        if(point.uninherited) this->sections.push_back({.time = point.time, .beatLength = point.beatLength});
    }
    if(!this->sections.empty()) {
        // back to before 0 a beat at a time (where ticks fall to the millisecond depends on this rounding, which
        // existing maps were snapped with; a beat too short to get there in reasonable time stops short of it)
        this->firstStart = this->sections.front().time;
        if(const f64 length = this->sections.front().beatLength; hasBeats(length)) {
            for(i32 beats = 0; this->firstStart > 0.0 && beats < 100000; beats++) this->firstStart -= length;
        }
    }
}

i32 BeatSnap::clampDivisor(i32 divisor) {
    if(divisor < 1 || divisor > 16) return 1;
    if(divisor == 5) return 4;
    if(divisor == 7) return 6;
    if(divisor >= 9 && divisor <= 11) return 12;
    if(divisor > 12 && divisor < 16) return 16;
    return divisor;
}

BeatSnap::Section BeatSnap::sectionAt(f64 time) const {
    const auto it = Mc::ranges::upper_bound(this->sections, time, {}, &Section::time);
    if(it == this->sections.begin() || it - 1 == this->sections.begin()) {
        return {.time = this->firstStart, .beatLength = this->sections.front().beatLength};
    }
    return *(it - 1);
}

i32 BeatSnap::snap(f64 time, i32 divisor, f64 from) const {
    if(this->sections.empty() || divisor <= 0) return truncated(time);
    const Section section = this->sectionAt(from);
    if(!hasBeats(section.beatLength)) return truncated(time);

    const f64 increment = section.beatLength / divisor;
    const f64 sinceStart = time - section.time;
    const f64 flooredCount = std::trunc(sinceStart / increment) - (sinceStart < 0.0 ? 1.0 : 0.0);
    const i32 left = truncated(flooredCount * increment + section.time);
    const i32 right = truncated((flooredCount + 1.0) * increment + section.time);
    if(from != time) {
        if(left == from) return right;
        if(right == from) return left;
    }
    return time - left < right - time ? left : right;
}

i32 BeatSnap::step(i32 time, i32 ticks, i32 divisor, bool playing) const {
    const i32 direction = ticks < 0 ? -1 : 1;
    i32 count = std::abs(ticks);
    if(this->sections.empty() || divisor <= 0) {
        // a beat of 1000 ms, 60 BPM
        const i32 unit = 1000 / std::max(divisor, 1);
        const i32 target = time + direction * unit * count;
        return direction < 0 ? std::max(unit * (target / unit), 0) : unit * (target / unit);
    }

    const f64 beatLength = this->beatLengthAt(time);
    if(playing) count *= (i32)std::floor(250.0 / std::max(std::trunc(beatLength), 1.0)) + 1;
    const f64 skip = std::trunc(beatLength / (playing ? 1 : divisor)) * count;
    return this->snap(time + direction * skip, divisor, time);
}

f64 BeatSnap::beatLengthAt(f64 time) const {
    const f64 length = this->sections.empty() ? 0.0 : this->sectionAt(time).beatLength;
    return hasBeats(length) ? length : 1000.0;
}

f64 BeatSnap::offGrid(f64 time) const {
    if(this->sections.empty()) return 0.0;
    const Section section = this->sectionAt(time);
    if(!hasBeats(section.beatLength)) return 0.0;
    f64 off = std::numeric_limits<f64>::max();
    // the ticks for every divisor are among the ticks for these two
    for(const f64 divisor : {12.0, 16.0}) {
        const f64 ticks = (time - section.time) / (section.beatLength / divisor);
        off = std::min(off, std::abs(ticks - std::round(ticks)) * (section.beatLength / divisor));
    }
    if(const auto next = Mc::ranges::upper_bound(this->sections, time, {}, &Section::time);
       next != this->sections.end()) {
        off = std::min(off, next->time - time);
    }
    return std::isfinite(off) ? off : 0.0;
}
