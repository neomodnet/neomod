// Copyright (c) 2026, WH, All rights reserved.
#include "Verify.h"

#include "BeatSnap.h"
#include "BeatmapFile/BeatmapFile.h"
#include "BeatmapFile/BeatmapPrimitives.h"
#include "DatabaseBeatmapTypes.h"
#include "Environment.h"
#include "GameRules.h"
#include "HitObjects.h"
#include "HitSounds.h"
#include "MapObjects.h"
#include "Vectors.h"

#include "fmt/format.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <variant>

using namespace neomod;
using namespace neomod::DatabaseBeatmapTypes;

namespace Verify {
namespace {
// the rules and their numbers come from osu!lazer (osu.Game/Rulesets/Edit/Checks, osu.Game.Rulesets.Osu/Edit/Checks)
constexpr f64 UNSNAP_PROBLEM_MS{2.0};
constexpr f64 UNSNAP_MINOR_MS{1.0};
constexpr i32 ALMOST_CONCURRENT_MS{10};
// what a 4:3 screen shows around the playfield, in osu!px
constexpr f32 SCREEN_LEFT{-67.f};
constexpr f32 SCREEN_TOP{-60.f};
constexpr f32 SCREEN_RIGHT{579.f};
constexpr f32 SCREEN_BOTTOM{428.f};
constexpr f32 PATH_STEP{5.f};        // osu!px between the places a slider is looked at...
constexpr i32 MAX_PATH_STEPS{2000};  // ...of which a slider of any length has this many at most
constexpr i32 MUTED_VOLUME{5};
constexpr i32 QUIET_VOLUME{20};
constexpr f64 MIN_DRAIN_MS{30000.0};

bool isOffscreen(vec2 pos, f32 radius) {
    return pos.x - radius < SCREEN_LEFT || pos.x + radius > SCREEN_RIGHT || pos.y - radius < SCREEN_TOP ||
           pos.y + radius > SCREEN_BOTTOM;
}
}  // namespace

std::vector<Issue> check(const Document &document, const MapObjects &objects, std::string_view folder) {
    const Primitives::PRIMITIVE_CONTAINER &c = document.getPrimitives();
    const BeatSnap &snap = document.getBeatSnap();
    const auto &made = objects.objects;
    std::vector<Issue> issues;

    // the whole map
    // a file is there under its name in any case, as the game finds it
    const auto missing = [&](std::string_view file) {
        std::string path = fmt::format("{}/{}", folder, file);
        return !Environment::fileExists(path);
    };
    if(document.getPreviewTime() < 0) issues.push_back({.kind = Kind::NO_PREVIEW, .severity = Severity::PROBLEM});
    if(document.getBackground().empty()) {
        issues.push_back({.kind = Kind::NO_BACKGROUND, .severity = Severity::PROBLEM});
    } else if(!folder.empty() && missing(document.getBackground())) {
        issues.push_back({.kind = Kind::MISSING_BACKGROUND, .severity = Severity::PROBLEM});
    }
    if(const auto audio = document.getFile().getValue(BeatmapFile::SectionKind::GENERAL, "AudioFilename");
       !folder.empty() && (!audio || audio->empty() || missing(*audio))) {
        issues.push_back({.kind = Kind::MISSING_AUDIO, .severity = Severity::PROBLEM});
    }
    for(const auto &[setting, value] : {std::pair{Setting::HP, c.HP}, std::pair{Setting::CS, c.CS},
                                        std::pair{Setting::OD, c.OD}, std::pair{Setting::AR, c.AR}}) {
        if(std::round(value * 10.f) / 10.f != value) {
            issues.push_back(
                {.kind = Kind::SETTING_DECIMALS, .severity = Severity::PROBLEM, .value = value, .setting = setting});
        }
        if(value < 0.f || value > 10.f) {
            issues.push_back(
                {.kind = Kind::SETTING_RANGE, .severity = Severity::WARNING, .value = value, .setting = setting});
        }
    }
    if(!made.empty()) {
        i32 end = made.front()->getEndTime();
        for(const auto &object : made) end = std::max(end, object->getEndTime());
        const f64 drain = (f64)end - made.front()->getClickTime() - c.totalBreakDuration;
        if(drain < MIN_DRAIN_MS) {
            issues.push_back({.kind = Kind::SHORT_DRAIN, .severity = Severity::PROBLEM, .value = drain / 1000.0});
        }
    }

    const uSz timed = issues.size();
    // an issue of `kind` if `time` is off the grid; whether it is
    const auto unsnapped = [&](Kind kind, f64 time, Document::Id line) {
        const f64 off = snap.offGrid(time);
        if(off < UNSNAP_MINOR_MS) return false;
        issues.push_back({.kind = kind,
                          .severity = off >= UNSNAP_PROBLEM_MS ? Severity::PROBLEM : Severity::MINOR,
                          .time = (i32)time,
                          .lines = {line},
                          .value = off});
        return true;
    };
    // the hit volume, else the volume of its timing point
    const auto quiet = [&](HITSAMPLE_BITS samples, i32 time, Document::Id line) {
        const i32 volume = samples.volume ? samples.volume : HitSoundUtils::samplesAt(c.timingpoints, time).volume;
        if(volume > QUIET_VOLUME) return;
        issues.push_back({.kind = Kind::QUIET,
                          .severity = volume <= MUTED_VOLUME ? Severity::PROBLEM : Severity::WARNING,
                          .time = time,
                          .lines = {line},
                          .value = (f64)volume});
    };
    const f32 radius = GameRules::getRawHitCircleDiameter(c.CS) / 2.f;
    // under and over these a spinner is too short for any player, or for some
    const f64 shortSpinner = 450.0 + (c.OD < 5.f ? (5.0 - c.OD) * -17.0 : (c.OD - 5.0) * 17.0);
    const f64 briefSpinner = 500.0 + (c.OD < 5.f ? (5.0 - c.OD) * -21.8 : (c.OD - 5.0) * 20.0);

    for(uSz i = 0; i < made.size(); i++) {
        const HitObject &object = *made[i];
        const Document::Id line = objects.ids[i];
        const i32 start = object.getClickTime();
        const i32 end = object.getEndTime();

        unsnapped(Kind::UNSNAPPED, start, line);
        if(const auto *circle = std::get_if<HITCIRCLE>(&c.objects[i])) {
            quiet(circle->samples, start, line);
            if(isOffscreen(object.getRawPosAt(start), radius)) {
                issues.push_back(
                    {.kind = Kind::OFFSCREEN, .severity = Severity::PROBLEM, .time = start, .lines = {line}});
            }
        } else if(const auto *slider = std::get_if<SLIDER>(&c.objects[i])) {
            quiet(slider->edgeSamples.empty() ? slider->hoverSamples : slider->edgeSamples.front(), start, line);
            // whole milliseconds on the object itself: a file can say any length
            const f64 span = (f64)object.getDuration() / std::max(slider->repeat, 1);
            // the first one that is
            for(i32 repeat = 1; repeat < slider->repeat; repeat++) {
                if(unsnapped(Kind::UNSNAPPED_REPEAT, start + span * repeat, line)) break;
            }
            unsnapped(Kind::UNSNAPPED_END, end, line);
            // one pass along it, to where it first leaves the screen
            const f32 length = slider->pixelLength;
            const i32 steps =
                length > 0.f ? (i32)std::clamp(std::ceil(length / PATH_STEP), 1.f, (f32)MAX_PATH_STEPS) : 1;
            for(i32 step = 0; step <= steps; step++) {
                const i32 time = start + (i32)(span * step / steps);
                if(isOffscreen(object.getRawPosAt(time), radius)) {
                    issues.push_back(
                        {.kind = Kind::OFFSCREEN, .severity = Severity::PROBLEM, .time = time, .lines = {line}});
                    break;
                }
            }
        } else {
            const auto &spinner = std::get<SPINNER>(c.objects[i]);
            unsnapped(Kind::UNSNAPPED_END, end, line);
            if(const f64 length = end - start; length < briefSpinner) {
                issues.push_back({.kind = Kind::SHORT_SPINNER,
                                  .severity = length < shortSpinner ? Severity::PROBLEM : Severity::WARNING,
                                  .time = start,
                                  .lines = {line},
                                  .value = length});
            }
            if(!spinner.newCombo) {
                issues.push_back({.kind = Kind::SPINNER_WITHOUT_NEW_COMBO,
                                  .severity = Severity::WARNING,
                                  .time = start,
                                  .lines = {line}});
            }
        }

        // with the next object only: in maps with objects on top of each other, every pair would be an issue
        if(i + 1 < made.size()) {
            const i32 gap = made[i + 1]->getClickTime() - end;
            const bool concurrent = gap <= (i32)UNSNAP_PROBLEM_MS;
            if(concurrent || std::abs(gap) < ALMOST_CONCURRENT_MS) {
                issues.push_back({.kind = concurrent ? Kind::CONCURRENT : Kind::ALMOST_CONCURRENT,
                                  .severity = Severity::PROBLEM,
                                  .time = start,
                                  .lines = {line, objects.ids[i + 1]}});
            }
        }
    }

    const auto points = document.getTimingPoints();
    const auto pointIds = document.getTimingPointIds();
    for(uSz i = 1; i < points.size(); i++) {
        // two of a kind at one time are next to each other, see Primitives::timingOrder()
        if(points[i].time == points[i - 1].time && points[i].uninherited == points[i - 1].uninherited) {
            issues.push_back({.kind = Kind::TIMING_OVERLAP,
                              .severity = Severity::PROBLEM,
                              .time = (i32)points[i].time,
                              .lines = {pointIds[i - 1], pointIds[i]}});
        }
    }

    std::stable_sort(issues.begin() + (std::ptrdiff_t)timed, issues.end(),
                     [](const Issue &a, const Issue &b) { return *a.time < *b.time; });
    return issues;
}
}  // namespace Verify
