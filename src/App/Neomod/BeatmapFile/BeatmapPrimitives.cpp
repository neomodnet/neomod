// Copyright (c) 2020, PG & 2026, WH, All rights reserved.
#include "BeatmapPrimitives.h"
#include "BeatmapFile.h"

#include "Parsing.h"
#include "SString.h"
#include "SliderCurves.h"
#include "Vectors.h"

#define WANT_PDQSORT
#include "Sorting.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>

namespace neomod::Primitives {

using namespace DatabaseBeatmapTypes;

namespace {

TIMINGPOINT toTimingPoint(const BeatmapFile::TimingPoint &tp) {
    return {.offset = tp.time,
            .msPerBeat = tp.beatLength,
            .sampleSet = tp.sampleSet,
            .sampleIndex = tp.sampleIndex,
            .volume = std::clamp(tp.volume, 0, 100),
            .uninherited = tp.uninherited,
            .kiai = (tp.effects & BeatmapFile::TimingPoint::EFFECT_KIAI) != 0};
}

// parse a sample set value with lenient handling, matching lazer behavior:
// values outside 0-3 default to Normal (1)
// see: https://github.com/ppy/osu/blob/56ef5eae1409622518fbc19872d5e3477abe90a2/osu.Game/Rulesets/Objects/Legacy/ConvertHitObjectParser.cs#L203
forceinline u8 sampleSetValue(i32 val) {
    return (val >= 0 && val <= 3) ? static_cast<u8>(val) : static_cast<u8>(SampleSetType::NORMAL);
}

// the hit sample's sets and volume
// TODO: the index of custom beatmap skin samples and their filename (which overrides everything else) are unused atm
void applyHitSample(const BeatmapFile::HitSample &sample, HITSAMPLE_BITS &samples) {
    samples.normalSet = sampleSetValue(sample.normalSet);
    samples.additionSet = sampleSetValue(sample.additionSet);
    samples.volume = static_cast<u8>(std::clamp(sample.volume, 0, 100));  // for some reason this can be negative
}

bool sliderScoringTimeComparator(const SLIDER_SCORING_TIME &a, const SLIDER_SCORING_TIME &b) {
    if(a.time != b.time) return a.time < b.time;
    if(a.type != b.type) return static_cast<i32>(a.type) < static_cast<i32>(b.type);
    return false;  // equivalent
};

bool timingPointSortComparator(const TIMINGPOINT &a, const TIMINGPOINT &b) {
    if(a.offset != b.offset) return a.offset < b.offset;

    // uninherited timingpoints go before inherited timingpoints
    const bool a_uninherited = a.msPerBeat >= 0;
    const bool b_uninherited = b.msPerBeat >= 0;
    if(a_uninherited != b_uninherited) return a_uninherited;

    if(a.sampleSet != b.sampleSet) return a.sampleSet < b.sampleSet;
    if(a.sampleIndex != b.sampleIndex) return a.sampleIndex < b.sampleIndex;
    if(a.kiai != b.kiai) return a.kiai;

    return false;  // equivalent
}

}  // namespace

TimingPoints::TimingPoints(std::vector<TIMINGPOINT> points) : entries(points.size()) {
    // sort timingpoints by time
    if(points.size() > 1) srt::pdqsort(points, timingPointSortComparator);

    u32 lastUninherited = 0;
    u32 lastInherited = 0;
    for(u32 i = 0; i < points.size(); i++) {
        (points[i].uninherited ? lastUninherited : lastInherited) = i;
        this->entries[i] = {.point = points[i], .lastUninherited = lastUninherited, .lastInherited = lastInherited};
    }
}

TIMING_INFO TimingPoints::getTimingInfo(i32 positionMS) const {
    if(this->entries.empty()) {
        return {.offset = 0,
                .beatLengthBase = 1,
                .beatLength = 1,
                .sampleSet = 0,
                .sampleIndex = 0,
                .volume = 100,
                .isNaN = false};
    }

    // peppy's algorithm (correctly handles aspire & NaNs): the last point at or before the time gives the samples,
    // the last uninherited one the beat length, and the last inherited one a multiplier if it comes after that
    const uSz audioPoint = this->entryAt(positionMS);
    const uSz point = this->entries[audioPoint].lastUninherited;
    const uSz samplePoint = this->entries[audioPoint].lastInherited;

    const TIMINGPOINT &timing = this->entries[point].point;
    const TIMINGPOINT &sample = this->entries[samplePoint].point;
    const TIMINGPOINT &audio = this->entries[audioPoint].point;

    const f32 mult = (samplePoint > point && sample.msPerBeat < 0)
                         ? std::clamp<f32>((f32)-sample.msPerBeat, 10.0f, 1000.0f) / 100.0f
                         : 1.f;

    TIMING_INFO ti;
    ti.beatLengthBase = (f32)timing.msPerBeat;
    ti.offset = (i32)timing.offset;
    ti.isNaN = std::isnan(sample.msPerBeat) || std::isnan(timing.msPerBeat);
    ti.beatLength = ti.beatLengthBase * mult;
    ti.volume = audio.volume;
    ti.sampleSet = audio.sampleSet;
    ti.sampleIndex = audio.sampleIndex;
    return ti;
}

f64 TimingPoints::getBeat(i32 positionMS) const {
    if(this->entries.empty()) return 0.0;
    const TIMINGPOINT &timing = this->entries[this->entries[this->entryAt(positionMS)].lastUninherited].point;
    if(!(timing.msPerBeat > 0.0)) return 0.0;  // (NaN too)
    return (positionMS - timing.offset) / timing.msPerBeat;
}

uSz TimingPoints::entryAt(i32 positionMS) const {
    const Entry *const first = this->entries.data();
    const Entry *const after = std::upper_bound(first, first + this->entries.size(), positionMS,
                                                [](i32 time, const Entry &e) { return time < e.point.offset; });
    return after == first ? 0 : (after - first) - 1;
}

TimingPoints readTimingPoints(const BeatmapFile &file) {
    std::vector<TIMINGPOINT> timingpoints;
    BeatmapFile::TimingPoint timingPoint;
    for(const auto line : file.getEntries(BeatmapFile::SectionKind::TIMING_POINTS)) {
        // a time that isn't a number or doesn't fit in 32 bits drops the point, as in osu!lazer
        if(BeatmapFile::parse(line.text, timingPoint) &&
           std::abs(timingPoint.time) <= std::numeric_limits<i32>::max()) {
            timingpoints.push_back(toTimingPoint(timingPoint));
        }
    }
    return TimingPoints{std::move(timingpoints)};
}

PRIMITIVE_CONTAINER loadPrimitiveObjectsFromData(std::span<const u8> fileBuffer, const Limits &limits,
                                                 const Sync::stop_token &dead) {
    using Kind = BeatmapFile::SectionKind;
    using HO = BeatmapFile::HitObject;

    PRIMITIVE_CONTAINER c{};
    c.limits = limits;

    if(dead.stop_requested()) {
        c.error.errc = LoadError::LOAD_INTERRUPTED;
        return c;
    }
    if(fileBuffer.empty()) {
        c.error.errc = LoadError::FILE_LOAD;
        return c;
    }

    const BeatmapFile file{fileBuffer};

    const float sliderSanityRange = limits.sliderCurveMaxLength;  // infinity sanity check, same as before
    const int sliderMaxRepeatRange =
        limits.sliderMaxRepeats;  // NOTE: osu! will refuse to play any beatmap which has sliders with more than
                                  // 9000 repeats, here we just clamp it instead

    // (e.g. "osu file format v12")
    if(const auto version = file.getVersion()) c.version = *version;

    BeatmapFile::KeyValue kv;

    u8 gamemode{(u8)-1};  // ignore non-standard gamemodes for now
    for(const auto line : file.getEntries(Kind::GENERAL)) {
        if(!BeatmapFile::parse(line.text, kv)) continue;
        if(kv.key == "Mode") {
            if(gamemode == (u8)-1 && Parsing::parse(kv.value, &gamemode) && gamemode != 0) {
                c.error.errc = LoadError::NON_STD_GAMEMODE;
                return c;
            }
        } else if(kv.key == "SampleSet") {
            const std::string sampleSet = SString::to_lower(kv.value);
            if(sampleSet == "normal") {
                c.defaultSampleSet = SampleSetType::NORMAL;
            } else if(sampleSet == "soft") {
                c.defaultSampleSet = SampleSetType::SOFT;
            } else if(sampleSet == "drum") {
                c.defaultSampleSet = SampleSetType::DRUM;
            }
        } else if(kv.key == "StackLeniency") {
            Parsing::parse(kv.value, &c.stackLeniency);
        }
    }

    bool foundAR = false;
    for(const auto line : file.getEntries(Kind::DIFFICULTY)) {
        if(!BeatmapFile::parse(line.text, kv)) continue;
        if(kv.key == "CircleSize") {
            Parsing::parse(kv.value, &c.CS);
        } else if(kv.key == "ApproachRate") {
            foundAR |= Parsing::parse(kv.value, &c.AR);
        } else if(kv.key == "HPDrainRate") {
            Parsing::parse(kv.value, &c.HP);
        } else if(kv.key == "OverallDifficulty") {
            Parsing::parse(kv.value, &c.OD);
        } else if(kv.key == "SliderMultiplier") {
            Parsing::parse(kv.value, &c.sliderMultiplier);
        } else if(kv.key == "SliderTickRate") {
            Parsing::parse(kv.value, &c.sliderTickRate);
        }
    }

    BeatmapFile::Event event;
    for(const auto line : file.getEntries(Kind::EVENTS)) {
        if(BeatmapFile::parse(line.text, event) && event.kind == BeatmapFile::Event::Kind::BREAK) {
            c.breaks.push_back(BREAK{.startTime = event.start, .endTime = event.end});
            // also update total break duration as we go along here
            c.totalBreakDuration += (u32)(event.end - event.start);
        }
    }

    std::array<std::optional<Color>, 8> tempColors;
    BeatmapFile::Colour colour;
    for(const auto line : file.getEntries(Kind::COLOURS)) {
        u8 comboNum;
        if(BeatmapFile::parse(line.text, colour) && Parsing::parse(colour.name, "Combo", &comboNum) && comboNum >= 1 &&
           comboNum <= 8) {  // bare minimum validation effort
            tempColors[comboNum - 1] = rgb(colour.r, colour.g, colour.b);
        }
    }

    // each object's time and type, to put them in time order and number their combos once all are read (below)
    struct ComboEntry {
        i32 time;
        u8 type;
        ObjectRef object;
    };
    std::vector<ComboEntry> comboEntries;

    // circles:
    // x,y,time,type,hitSounds,hitSamples
    // sliders:
    // x,y,time,type,hitSounds,sliderType|curveX:curveY|...,repeat,pixelLength,edgeHitsound,edgeSets,hitSamples
    // spinners:
    // x,y,time,type,hitSounds,endTime,hitSamples
    HO ho;
    for(const auto line : file.getEntries(Kind::HIT_OBJECTS)) {
        if(dead.stop_requested()) {
            c.error.errc = LoadError::LOAD_INTERRUPTED;
            return c;
        }

        if(!BeatmapFile::parse(line.text, ho)) {
            c.skippedLines.push_back(line.number);
            continue;
        }

        switch(ho.kind) {
            case HO::Kind::NONE:
                break;

            case HO::Kind::CIRCLE: {
                HITCIRCLE h{};
                h.x = (f32)(i32)ho.x;  // NOTE: lazer beatmaps do not truncate here
                h.y = (f32)(i32)ho.y;
                h.time = ho.time;
                // h.clicked = false; // unknown what this field was supposed to be for
                h.samples.hitSounds = (ho.hitSounds & HitSoundType::VALID_HITSOUNDS);
                applyHitSample(ho.sample, h.samples);

                comboEntries.push_back(
                    {.time = ho.time, .type = ho.type, .object = {ObjectRef::Kind::CIRCLE, (u32)c.hitcircles.size()}});
                c.hitcircles.push_back(h);
                break;
            }

            case HO::Kind::SLIDER: {
                SLIDER slider{};
                slider.time = ho.time;
                slider.hoverSamples.hitSounds = (ho.hitSounds & HitSoundType::VALID_SLIDER_HITSOUNDS);

                slider.type = SLIDERCURVETYPE{ho.curveType};
                slider.points.reserve(ho.curvePoints.size() + 1);
                for(const vec2 &point : ho.curvePoints) {
                    slider.points.emplace_back(std::clamp(point.x, -sliderSanityRange, sliderSanityRange),
                                               std::clamp(point.y, -sliderSanityRange, sliderSanityRange));
                }

                // special case: osu! logic for handling the hitobject point vs the controlpoints (since
                // sliders have both, and older beatmaps store the start point inside the control
                // points)
                vec2 xy = vec2(std::clamp(ho.x, -sliderSanityRange, sliderSanityRange),
                               std::clamp(ho.y, -sliderSanityRange, sliderSanityRange));
                if(slider.points.size() > 0) {
                    if(slider.points[0] != xy) slider.points.insert(slider.points.begin(), xy);
                } else {
                    slider.points.push_back(xy);
                }

                // partially allow bullshit sliders (add second point to make valid)
                // e.g. https://osu.ppy.sh/beatmapsets/791900#osu/1676490
                if(slider.points.size() == 1) slider.points.push_back(xy);

                // an edge the lists leave out plays the object's hitsounds and sample sets (as osu!stable's sliders
                // without edge fields do, and as lazer reads them)
                const HITSAMPLE_BITS objectEdge{.hitSounds = (u8)(ho.hitSounds & HitSoundType::VALID_HITSOUNDS),
                                                .normalSet = sampleSetValue(ho.sample.normalSet),
                                                .additionSet = sampleSetValue(ho.sample.additionSet),
                                                .volume = 0};
                for(uSz i = 0; i < std::max(ho.edgeSounds.size(), ho.edgeSets.size()); i++) {
                    HITSAMPLE_BITS samples = objectEdge;
                    if(i < ho.edgeSounds.size()) samples.hitSounds = ho.edgeSounds[i] & HitSoundType::VALID_HITSOUNDS;
                    if(i < ho.edgeSets.size()) {
                        samples.normalSet = sampleSetValue(ho.edgeSets[i].normalSet);
                        samples.additionSet = sampleSetValue(ho.edgeSets[i].additionSet);
                    }
                    slider.edgeSamples.push_back(samples);
                }

                // No start sample specified, use the object's
                if(slider.edgeSamples.empty()) slider.edgeSamples.push_back(objectEdge);

                // No end sample specified, use the same as start
                if(slider.edgeSamples.size() == 1) slider.edgeSamples.push_back(slider.edgeSamples.front());

                applyHitSample(ho.sample, slider.hoverSamples);

                const auto pixelLength = static_cast<f32>(ho.length);
                slider.x = (f32)(i32)ho.x;  // NOTE: lazer beatmaps do not truncate here
                slider.y = (f32)(i32)ho.y;
                slider.repeat = std::clamp(ho.slides, 0, sliderMaxRepeatRange);
                slider.pixelLength =
                    std::isnan(pixelLength) ? 0.f : std::clamp(pixelLength, -sliderSanityRange, sliderSanityRange);
                if(slider.pixelLength == 0.f) {
                    // without a length: as long as its curve
                    slider.pixelLength =
                        std::min(SliderCurve{slider.type, slider.points, 0.f}.getPixelLength(), sliderSanityRange);
                }
                comboEntries.push_back(
                    {.time = ho.time, .type = ho.type, .object = {ObjectRef::Kind::SLIDER, (u32)c.sliders.size()}});
                c.sliders.push_back(std::move(slider));
                break;
            }

            case HO::Kind::SPINNER: {
                SPINNER s{.x = (f32)(i32)ho.x,  // NOTE: lazer beatmaps do not truncate here
                          .y = (f32)(i32)ho.y,
                          .time = ho.time,
                          .endTime = ho.endTime,
                          .samples = {},
                          .isEndOfCombo = false};
                s.samples.hitSounds = (u8)(ho.hitSounds & HitSoundType::VALID_HITSOUNDS);
                applyHitSample(ho.sample, s.samples);

                comboEntries.push_back(
                    {.time = ho.time, .type = ho.type, .object = {ObjectRef::Kind::SPINNER, (u32)c.spinners.size()}});
                c.spinners.push_back(s);
                break;
            }
        }
    }

    // the objects as they come in time, which a file's lines don't have to follow (osu!stable and lazer sort them too;
    // equal times keep the order of their lines), and their combos in that order
    std::ranges::stable_sort(comboEntries, {}, &ComboEntry::time);
    int hitobjectsWithoutSpinnerCounter = 0;
    int colorCounter = 1;
    int colorOffset = 0;
    int comboNumber = 1;
    std::vector<i32> breakEnds;
    breakEnds.reserve(c.breaks.size());
    for(const BREAK &b : c.breaks) breakEnds.push_back(b.endTime);
    std::ranges::sort(breakEnds);
    auto nextBreakEnd = breakEnds.cbegin();
    bool forceNewCombo = false;
    bool *previousEndsCombo = nullptr;
    c.objectsByTime.reserve(comboEntries.size());
    for(const ComboEntry &entry : comboEntries) {
        c.objectsByTime.push_back(entry.object);
        const bool isSpinner = entry.object.kind == ObjectRef::Kind::SPINNER;
        if(!isSpinner) hitobjectsWithoutSpinnerCounter++;
        for(; nextBreakEnd != breakEnds.cend() && *nextBreakEnd < entry.time; ++nextBreakEnd) forceNewCombo = true;

        // a combo ends before any object that starts one, a spinner too (as in osu!stable, where a v8 file's spinners
        // always do)
        if(previousEndsCombo && ((entry.type & HO::TYPE_NEW_COMBO) || forceNewCombo || (isSpinner && c.version <= 8))) {
            *previousEndsCombo = true;
        }

        // the first object after spinners or a break starts a new combo whether its line has one or not (as in
        // osu!stable and lazer)
        if((entry.type & HO::TYPE_NEW_COMBO) || (forceNewCombo && !isSpinner)) {
            comboNumber = 1;

            // special case 1: if the current object is a spinner, then the raw color counter is not
            // increased (but the offset still is!)
            // special case 2: the first (non-spinner) hitobject in a beatmap is always a new combo,
            // therefore the raw color counter is not increased for it (but the offset still is!)
            if(!isSpinner && hitobjectsWithoutSpinnerCounter > 1) colorCounter++;

            // special case 3: "Bits 4-6 (16, 32, 64) form a 3-bit number (0-7) that chooses how many combo colours to skip."
            // (only an object's own new combo skips any)
            if(entry.type & HO::TYPE_NEW_COMBO) colorOffset += (entry.type >> HO::TYPE_COLOUR_SKIP_SHIFT) & 0b111;
        }
        forceNewCombo = isSpinner;

        if(entry.object.kind == ObjectRef::Kind::CIRCLE) {
            HITCIRCLE &h = c.hitcircles[entry.object.index];
            h.number = comboNumber++;
            h.colorCounter = colorCounter;
            h.colorOffset = colorOffset;
            previousEndsCombo = &h.isEndOfCombo;
        } else if(entry.object.kind == ObjectRef::Kind::SLIDER) {
            SLIDER &slider = c.sliders[entry.object.index];
            slider.number = comboNumber++;
            slider.colorCounter = colorCounter;
            slider.colorOffset = colorOffset;
            previousEndsCombo = &slider.isEndOfCombo;
        } else {
            previousEndsCombo = &c.spinners[entry.object.index].isEndOfCombo;
        }
    }
    if(previousEndsCombo) *previousEndsCombo = true;

    // special case: old beatmaps have AR = OD, there is no ApproachRate stored
    if(!foundAR) c.AR = c.OD;

    // late bail if too many hitobjects would run out of memory and crash
    if(c.getNumObjects() > limits.maxHitObjects) {
        c.error.errc = LoadError::TOOMANY_HITOBJECTS;
        return c;
    }

    for(const auto &tempCol : tempColors) {
        if(tempCol.has_value()) {
            c.combocolors.push_back(tempCol.value());
        }
    }

    c.timingpoints = readTimingPoints(file);

    return c;
}

LoadError calculateSliderTimesClicksTicks(int beatmapVersion, std::vector<SLIDER> &sliders,
                                          const TimingPoints &timingpoints, float sliderMultiplier,
                                          float sliderTickRate, const Limits &limits, const Sync::stop_token &dead) {
    LoadError r;

    if(timingpoints.size() < 1) {
        r.errc = LoadError::NO_TIMINGPOINTS;
        return r;
    }

    struct SliderHelper {
        static float getSliderTickDistance(float sliderMultiplier, float sliderTickRate) {
            return ((100.0f * sliderMultiplier) / sliderTickRate);
        }

        static float getSliderTimeForSlider(const SLIDER &slider, const TIMING_INFO &timingInfo,
                                            float sliderMultiplier) {
            const float duration = timingInfo.beatLength * (slider.pixelLength / sliderMultiplier) / 100.0f;
            return (duration >= 1.0f && std::isfinite(duration) && !std::isnan(duration)) ? duration
                                                                                          : 1.0f;  // sanity check
        }

        static float getSliderVelocity(const TIMING_INFO &timingInfo, float sliderMultiplier, float sliderTickRate) {
            const float beatLength = timingInfo.beatLength;
            if(beatLength > 0.0f)
                return (getSliderTickDistance(sliderMultiplier, sliderTickRate) * sliderTickRate *
                        (1000.0f / beatLength));
            else
                return getSliderTickDistance(sliderMultiplier, sliderTickRate) * sliderTickRate;
        }

        static float getTimingPointMultiplierForSlider(const TIMING_INFO &timingInfo)  // needed for slider ticks
        {
            float beatLengthBase = timingInfo.beatLengthBase;
            if(beatLengthBase == 0.0f)  // sanity check
                beatLengthBase = 1.0f;

            return timingInfo.beatLength / beatLengthBase;
        }
    };

    for(auto &s : sliders) {
        if(dead.stop_requested()) {
            r.errc = LoadError::LOAD_INTERRUPTED;
            return r;
        }

        // sanity reset
        s.ticks.clear();
        s.scoringTimesForStarCalc.clear();

        // calculate duration
        const TIMING_INFO timingInfo = timingpoints.getTimingInfo(s.time);
        s.sliderTimeWithoutRepeats = SliderHelper::getSliderTimeForSlider(s, timingInfo, sliderMultiplier);
        s.sliderTime = s.sliderTimeWithoutRepeats * s.repeat;

        // calculate ticks
        int brk = 0;
        // don't generate ticks for NaN timingpoints and infinite values
        while(!brk++ && !timingInfo.isNaN && !std::isnan(s.pixelLength) && std::isfinite(s.pixelLength)) {
            const float minTickPixelDistanceFromEnd =
                0.01f * SliderHelper::getSliderVelocity(timingInfo, sliderMultiplier, sliderTickRate);
            const float tickPixelLength =
                (beatmapVersion < 8 ? SliderHelper::getSliderTickDistance(sliderMultiplier, sliderTickRate)
                                    : SliderHelper::getSliderTickDistance(sliderMultiplier, sliderTickRate) /
                                          SliderHelper::getTimingPointMultiplierForSlider(timingInfo));

            if(std::isnan(tickPixelLength) || !std::isfinite(tickPixelLength)) break;

            const float tickDurationPercentOfSliderLength =
                tickPixelLength / (s.pixelLength == 0.0f ? 1.0f : s.pixelLength);
            const int max_ticks = limits.sliderMaxTicks;
            const int tickCount = std::min((int)std::ceil(s.pixelLength / tickPixelLength) - 1,
                                           max_ticks);  // NOTE: hard sanity limit number of ticks per slider

            if(tickCount > 0) {
                const float tickTOffset = tickDurationPercentOfSliderLength;
                float pixelDistanceToEnd = s.pixelLength;
                float t = tickTOffset;
                for(int i = 0; i < tickCount; i++, t += tickTOffset) {
                    // skip ticks which are too close to the end of the slider
                    pixelDistanceToEnd -= tickPixelLength;
                    if(pixelDistanceToEnd <= minTickPixelDistanceFromEnd) break;

                    s.ticks.push_back(t);
                }
            }
        }

        // bail if too many predicted heuristic scoringTimes would run out of memory and crash
        if((size_t)std::abs(s.repeat) * s.ticks.size() > (size_t)limits.maxSliderScoringTimes) {
            r.errc = LoadError::TOOMANY_HITOBJECTS;
            return r;
        }

        // calculate s.scoringTimesForStarCalc, which should include every point in time where the cursor must be within
        // the followcircle radius and at least one key must be pressed: see
        // https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Preprocessing/OsuDifficultyHitObject.cs
        const i32 osuSliderEndInsideCheckOffset = limits.sliderEndInsideCheckOffset;

        // 1) "skip the head circle"

        // 2) add repeat times (either at slider begin or end)
        for(int i = 0; i < (s.repeat - 1); i++) {
            const f32 time = s.time + (s.sliderTimeWithoutRepeats * (i + 1));  // see Slider.cpp
            s.scoringTimesForStarCalc.push_back(SLIDER_SCORING_TIME{
                .time = time,
                .type = SLIDER_SCORING_TIME::TYPE::REPEAT,
            });
        }

        // 3) add tick times (somewhere within slider, repeated for every repeat)
        for(int i = 0; i < s.repeat; i++) {
            for(int t = 0; t < s.ticks.size(); t++) {
                const float tickPercentRelativeToRepeatFromStartAbs =
                    (((i + 1) % 2) != 0 ? s.ticks[t] : 1.0f - s.ticks[t]);  // see Slider.cpp
                const f32 time =
                    s.time + (s.sliderTimeWithoutRepeats * i) +
                    (tickPercentRelativeToRepeatFromStartAbs * s.sliderTimeWithoutRepeats);  // see Slider.cpp
                s.scoringTimesForStarCalc.push_back(SLIDER_SCORING_TIME{
                    .time = time,
                    .type = SLIDER_SCORING_TIME::TYPE::TICK,
                });
            }
        }

        // 4) add slider end (potentially before last tick for bullshit sliders, but sorting takes care of that)
        // see https://github.com/ppy/osu/pull/4193#issuecomment-460127543
        const f32 time =
            std::max(static_cast<f32>(s.time) + s.sliderTime / 2.0f,
                     (static_cast<f32>(s.time) + s.sliderTime) - static_cast<f32>(osuSliderEndInsideCheckOffset));
        s.scoringTimesForStarCalc.push_back(SLIDER_SCORING_TIME{
            .time = time,
            .type = SLIDER_SCORING_TIME::TYPE::END,
        });

        if(dead.stop_requested()) {
            r.errc = LoadError::LOAD_INTERRUPTED;
            return r;
        }

        // 5) sort scoringTimes from earliest to latest
        if(s.scoringTimesForStarCalc.size() > 1) {
            srt::pdqsort(s.scoringTimesForStarCalc, sliderScoringTimeComparator);
        }
    }

    return r;
}

}  // namespace neomod::Primitives
