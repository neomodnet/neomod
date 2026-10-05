// Copyright (c) 2020, PG & 2026, WH, All rights reserved.
#pragma once
// DatabaseBeatmap data types

#if __has_include("config.h")
#include "config.h"
#endif

#include "types.h"
#include "Vectors_fwd.h"

#include <vector>

namespace neomod {

enum class SLIDERCURVETYPE : char;  // see SliderCurves.h

namespace DatabaseBeatmapTypes {

// raw structs (not editable, we're following db format directly)
struct TIMINGPOINT final {
    f64 offset;
    f64 msPerBeat;

    i32 sampleSet;
    i32 sampleIndex;
    i32 volume;

    bool uninherited;  // <=> timingChange
    bool kiai;

    bool operator==(const TIMINGPOINT &) const = default;
};

struct BREAK final {
    i64 startTime;
    i64 endTime;
};

struct TIMING_INFO final {
    i32 offset{0};

    f32 beatLengthBase{0.f};
    f32 beatLength{0.f};

    i32 sampleSet{0};
    i32 sampleIndex{0};
    i32 volume{0};

    bool isNaN{false};

    bool operator==(const TIMING_INFO &) const = default;
};

namespace NSSampleSetType {
enum t : u8 {
    NORMAL = 1,
    SOFT = 2,
    DRUM = 3,
};
}

using SampleSetType = NSSampleSetType::t;

namespace NSHitSoundType {
enum t : u8 {
    NORMAL = (1 << 0),
    WHISTLE = (1 << 1),
    FINISH = (1 << 2),
    CLAP = (1 << 3),

    VALID_HITSOUNDS = NORMAL | WHISTLE | FINISH | CLAP,
    VALID_SLIDER_HITSOUNDS = NORMAL | WHISTLE,
};
}

using HitSoundType = NSHitSoundType::t;

struct HITSAMPLE_BITS final {
    u8 hitSounds;    // bitfield of HitSoundTypes to play
    u8 normalSet;    // SampleSetType of the normal sound
    u8 additionSet;  // SampleSetType of the whistle, finish and clap sounds
    u8 volume;       // volume of the sample, 1-100. if 0, use timing point volume instead
};

namespace NSPpyHitObjectType {
enum t : u8 {
    CIRCLE = (1 << 0),
    SLIDER = (1 << 1),
    NEW_COMBO = (1 << 2),
    SPINNER = (1 << 3),
    // 4, 5, 6: 3-bit integer specifying how many combo colors to skip (if NEW_COMBO is set)
    MANIA_HOLD_NOTE = (1 << 7),
};
}

using PpyHitObjectType = NSPpyHitObjectType::t;

// primitive objects

struct HITCIRCLE final {
    f32 x, y;
    i32 time;
    i32 number;
    i32 colorCounter;
    i32 colorOffset;
    // bool clicked; // not sure what this was supposed to be used for
    HITSAMPLE_BITS samples;
};

struct SLIDER_SCORING_TIME final {  // for star calc
    enum class TYPE : u8 {
        TICK,
        REPEAT,
        END,
    };

    f32 time;
    TYPE type;
};

struct SLIDER final {
    f32 x, y;
    i32 repeat;
    f32 pixelLength;
    i32 time;
    i32 number;
    i32 colorCounter;
    i32 colorOffset;

    f32 sliderTime;
    f32 sliderTimeWithoutRepeats;

    std::vector<SLIDER_SCORING_TIME> scoringTimesForStarCalc;

    std::vector<vec2> points;
    std::vector<f32> ticks;

    std::vector<HITSAMPLE_BITS> edgeSamples;
    HITSAMPLE_BITS hoverSamples;

    SLIDERCURVETYPE type;
};

struct SPINNER final {
    f32 x, y;
    i32 time;
    i32 endTime;
    HITSAMPLE_BITS samples;
};
}  // namespace DatabaseBeatmapTypes
}  // namespace neomod

namespace DBType = neomod::DatabaseBeatmapTypes;
