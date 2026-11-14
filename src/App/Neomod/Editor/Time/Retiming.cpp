// Copyright (c) 2026, WH, All rights reserved.
#include "Retiming.h"

#include "BeatSnap.h"
#include "BeatmapFile/BeatmapPrimitives.h"
#include "ContainerRanges.h"
#include "Hashing.h"
#include "Parsing.h"

#include "fmt/format.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

using neomod::BeatmapFile;
using Kind = BeatmapFile::SectionKind;

namespace Retiming {
namespace {
f64 toFifteenDigits(f64 value) {
    const std::string text = fmt::format("{:.15g}", value);
    f64 rounded{value};
    Parsing::parse(text, &rounded);
    return rounded;
}

// the object lines, each with what it reads as (`edit` says whether it changed the record, which is written then)
template <typename F>
void editObjects(Document::Change &c, const Document &document, F &&edit) {
    BeatmapFile::HitObject object;
    for(const auto line : document.getFile().getEntries(Kind::HIT_OBJECTS)) {
        if(BeatmapFile::parse(line.text, object) && edit(object)) c.setObject(document.getIdAt(line.number), object);
    }
}
}  // namespace

f64 beatLengthOf(f64 bpm) { return toFifteenDigits(60000.0 / bpm); }
f64 velocityBeatLength(f64 velocity) { return toFifteenDigits(-100.0 / velocity); }

std::optional<f64> tappedBpm(std::span<const i32> taps) {
    if(taps.size() < 4) return std::nullopt;
    std::vector<f64> gaps;
    for(uSz i = 1; i < taps.size(); i++) gaps.push_back(taps[i] - taps[i - 1]);
    std::vector<f64> sorted = gaps;
    Mc::ranges::sort(sorted);
    const f64 usual = sorted[sorted.size() / 2];
    if(usual <= 0.0) return std::nullopt;

    // the beat for each tap, then the slope of the times over the beats (least squares)
    std::vector<f64> beats{0.0};
    for(const f64 gap : gaps) beats.push_back(beats.back() + std::max(1.0, std::round(gap / usual)));
    f64 meanBeat{0.0};
    f64 meanTime{0.0};
    for(uSz i = 0; i < taps.size(); i++) {
        meanBeat += beats[i] / (f64)taps.size();
        meanTime += taps[i] / (f64)taps.size();
    }
    f64 covariance{0.0};
    f64 variance{0.0};
    for(uSz i = 0; i < taps.size(); i++) {
        covariance += (beats[i] - meanBeat) * (taps[i] - meanTime);
        variance += (beats[i] - meanBeat) * (beats[i] - meanBeat);
    }
    const f64 beatLength = covariance / variance;
    if(!(beatLength > 0.0)) return std::nullopt;
    return std::clamp(std::round(60000.0 / beatLength), 1.0, 10000.0);
}

void apply(Document::Change &c, const Document &document, std::span<const Edit> edits, bool carry) {
    Hash::flat::map<Document::Id, const TimingPoint *> edited;
    for(const Edit &edit : edits) edited[edit.id] = &edit.point;
    if(!carry) {
        for(const Edit &edit : edits) c.setTimingPoint(edit.id, edit.point);
        return;
    }

    // the points the lines have now, in the order the game reads them
    std::vector<Edit> now;
    TimingPoint read;
    for(const auto line : document.getFile().getEntries(Kind::TIMING_POINTS)) {
        if(BeatmapFile::parse(line.text, read)) now.push_back({.id = document.getIdAt(line.number), .point = read});
    }
    Mc::ranges::stable_sort(
        now, {}, [](const Edit &e) { return neomod::Primitives::timingOrder(e.point.time, e.point.uninherited); });
    for(const Edit &edit : edits) c.setTimingPoint(edit.id, edit.point);

    // the sections as they are and as they'll be: the time and beat length for each uninherited point
    struct Section {
        f64 time;
        f64 beatLength;
        f64 newTime;
        f64 newBeatLength;
        [[nodiscard]] bool changes() const { return newTime != time || newBeatLength != beatLength; }
    };
    std::vector<Section> sections;
    std::vector<TimingPoint> before;
    std::vector<TimingPoint> after;
    for(const Edit &point : now) {
        const auto it = edited.find(point.id);
        const TimingPoint &then = it != edited.end() ? *it->second : point.point;
        if(point.point.uninherited && then.uninherited) {
            sections.push_back({.time = point.point.time,
                                .beatLength = point.point.beatLength,
                                .newTime = then.time,
                                .newBeatLength = then.beatLength});
        }
        before.push_back(point.point);
        if(then.uninherited) after.push_back(then);
    }
    if(Mc::ranges::none_of(sections, &Section::changes)) return;
    Mc::ranges::stable_sort(after, {}, &TimingPoint::time);
    const BeatSnap grid{before};
    const BeatSnap newGrid{after};

    // the section `time` is in (the first one before them all)
    const auto sectionAt = [&](f64 time) -> const Section & {
        const auto it = Mc::ranges::upper_bound(sections, time, {}, &Section::time);
        return it == sections.begin() ? sections.front() : *(it - 1);
    };
    // where a time in a section that changes goes: the place its tick goes if it's on one (none otherwise), and its own
    struct Carried {
        std::optional<i32> tick;
        f64 place;
    };
    const auto carried = [&](const Section &section, f64 time) -> Carried {
        const f64 scale =
            section.beatLength > 0.0 && section.newBeatLength > 0.0 ? section.newBeatLength / section.beatLength : 1.0;
        const f64 to = section.newTime + (time - section.time) * scale;
        for(const i32 divisor : BeatSnap::DIVISORS) {
            if(grid.snap(time, divisor, time) == time) return {.tick = newGrid.snap(to, divisor, to), .place = to};
        }
        return {.tick = std::nullopt, .place = to};
    };

    for(const Edit &inherited : now) {
        if(inherited.point.uninherited || edited.contains(inherited.id)) continue;
        const Section &section = sectionAt(inherited.point.time);
        if(!section.changes()) continue;
        const Carried to = carried(section, inherited.point.time);
        TimingPoint point = inherited.point;
        point.time = to.tick ? (f64)*to.tick : std::floor(to.place);
        // not to the next uninherited point, which an inherited point at its time would follow, nor to before the point
        // it was at or after
        if(const auto next = Mc::ranges::upper_bound(sections, inherited.point.time, {}, &Section::time);
           next != sections.end()) {
            point.time = std::min(point.time, std::ceil(next->newTime) - 1.0);
        }
        if(inherited.point.time >= section.time) point.time = std::max(point.time, section.newTime);
        if(point.time != inherited.point.time) c.setTimingPoint(inherited.id, point);
    }
    const auto objectTime = [&](const Section &section, i32 time) {
        const Carried to = carried(section, time);
        return to.tick ? *to.tick : (i32)std::round(to.place);
    };
    editObjects(c, document, [&](BeatmapFile::HitObject &object) {
        const Section &section = sectionAt(object.time);
        if(!section.changes()) return false;
        const i32 start = objectTime(section, object.time);
        i32 end = object.endTime;
        if(object.kind == BeatmapFile::HitObject::Kind::SPINNER) {
            end = std::max(objectTime(section, object.endTime), start);
        }
        if(start == object.time && end == object.endTime) return false;
        object.time = start;
        object.endTime = end;
        return true;
    });
}

void resnap(Document::Change &c, const Document &document, i32 divisor) {
    const auto points = document.getTimingPoints();
    const auto ids = document.getTimingPointIds();
    const BeatSnap grid{points};

    f64 section{0.0};  // the time the uninherited point in force has
    bool inSection{false};
    uSz next{0};  // the next uninherited point (points.size(): none)
    for(uSz i = 0; i < points.size(); i++) {
        if(points[i].uninherited) {
            section = points[i].time;
            inSection = true;
            continue;
        }
        for(next = std::max(next, i); next < points.size() && !points[next].uninherited;) next++;
        TimingPoint point = points[i];
        point.time = grid.snap(point.time, divisor, point.time);
        // an inherited point at the time of the next one would follow it
        if(next < points.size()) point.time = std::min(point.time, std::ceil(points[next].time) - 1.0);
        if(inSection) point.time = std::max(point.time, section);
        if(point.time != points[i].time) c.setTimingPoint(ids[i], point);
    }
    editObjects(c, document, [&](BeatmapFile::HitObject &object) {
        const i32 start = grid.snap(object.time, divisor, object.time);
        i32 end = object.endTime;
        if(object.kind == BeatmapFile::HitObject::Kind::SPINNER) {
            end = grid.snap(end, divisor, end);
            if(end <= start) end = grid.step(start, 1, divisor, false);
        }
        if(start == object.time && end == object.endTime) return false;
        object.time = start;
        object.endTime = end;
        return true;
    });
}
}  // namespace Retiming
