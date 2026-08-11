#pragma once
// Copyright (c) 2019, PG & Francesco149 & Khangaroo & Givikap120, 2026, WH, All rights reserved.

#if __has_include("config.h")
#include "config.h"
#endif

#include "noinclude.h"
#include "types.h"
#include "Vectors.h"

#ifndef BUILD_TOOLS_ONLY
#include "SyncStoptoken.h"
#else

#include <stop_token>
namespace Sync {
using std::stop_token;
}
#endif

#include "StrainComputeState.h"
#include "StaticPImpl.h"

#include <vector>
#include <array>
#include <memory>
#include <algorithm>
#include <cmath>
#include <limits>

enum class ModFlags : u64;

class ConVar;
class AbstractBeatmapInterface;
class DatabaseBeatmap;

struct FinishedScore;

namespace neomod {
enum class SLIDERCURVETYPE : char;
class SliderCurve;
namespace DatabaseBeatmapTypes {
struct SLIDER_SCORING_TIME;
}
namespace DiffCalc {
using DatabaseBeatmapTypes::SLIDER_SCORING_TIME;

// for forward declaration
extern const u32 PP_ALGORITHM_VERSION;

// a parsed hitobject plus the per-object difficulty data computed from it by the star calc.
// the parsed part is filled by DatabaseBeatmap::loadDifficultyHitObjects, the computed part is
// roughly equivalent to lazer's OsuDifficultyHitObject (+ a per-object slice of the skill state).
// NOTE: unlike lazer, the first hitobject is included in the objects array (lazer's difficulty
// hit objects start at the second one), so lazer's Index == index here.
class DifficultyHitObject {
   public:
    enum class TYPE : u8 {
        CIRCLE,
        SPINNER,
        SLIDER,
    };

    [[nodiscard]] inline bool isCircle() const { return type == TYPE::CIRCLE; }
    [[nodiscard]] inline bool isSpinner() const { return type == TYPE::SPINNER; }
    [[nodiscard]] inline bool isSlider() const { return type == TYPE::SLIDER; }
    [[nodiscard]] inline i32 getStack() const { return stack; }
    [[nodiscard]] inline i32 getClickTime() const { return time; }
    [[nodiscard]] inline i32 getEndTime() const { return time + getDuration(); }
    inline void setStack(i32 newStack) { stack = newStack; }

   public:
    DifficultyHitObject() = delete;

    DifficultyHitObject(TYPE type, vec2 pos, i32 time);               // circle
    DifficultyHitObject(TYPE type, vec2 pos, i32 time, i32 endTime);  // spinner
    DifficultyHitObject(TYPE type, vec2 pos, i32 time, i32 endTime, f32 spanDuration,
                        SLIDERCURVETYPE osuSliderCurveType, const std::vector<vec2> &controlPoints, f32 pixelLength,
                        std::vector<SLIDER_SCORING_TIME> scoringTimes, i32 repeats,
                        bool calculateSliderCurveInConstructor);  // slider
    ~DifficultyHitObject();

    DifficultyHitObject(const DifficultyHitObject &) = delete;
    DifficultyHitObject(DifficultyHitObject &&dobj) noexcept;

    DifficultyHitObject &operator=(const DifficultyHitObject &dobj) = delete;
    DifficultyHitObject &operator=(DifficultyHitObject &&dobj) noexcept;

    void updateStackPosition(f32 stackOffset, bool hardRock);

    // returns stacked curve position (applies stack offset derived from pos vs originalPos)
    [[nodiscard]] vec2 curvePointAt(f32 t) const;

    // for stacking calculations, always returns the unstacked original position at that point in time
    [[nodiscard]] vec2 getOriginalRawPosAt(i32 pos) const;
    [[nodiscard]] f32 getT(i32 pos, bool raw) const;

    [[nodiscard]] inline i32 getDuration() const {
        // Sanity clamp because of that one Aspire map
        // (MSVC std::clamp doesn't like when MAX < MIN)
        return std::max(0, endTime - time);
    }

   public:
    // circles (base)
    vec2 pos;
    i32 time;
    i32 baseTime;  // not adjusted by clockrate

    // spinners + sliders
    i32 endTime;
    i32 baseEndTime;  // not adjusted by clockrate

    // sliders
    std::vector<SLIDER_SCORING_TIME> scoringTimes;
    std::vector<vec2> scheduledCurveAllocControlPoints;
    std::unique_ptr<SliderCurve> curve;

    f32 spanDuration;  // i.e. sliderTimeWithoutRepeats
    f32 pixelLength;
    i32 repeats;

    // custom
    i32 stack;
    vec2 originalPos;

    TYPE type;
    SLIDERCURVETYPE osuSliderCurveType;
    bool scheduledCurveAlloc;

    // ============================================================================================================== //
    // computed by the star calc (calculateStarDiffForHitObjects), never set by the loader/ctor.
    struct Computed;
    StaticPImpl<Computed, 216> c;
};

// This struct is the core data computed by difficulty calculation and used in performance calculation
// TODO: this should match osu-lazer difficulty attributes:
// 1) Add StarRating here, and use it globally instead of separate variable
// 2) Add MaxCombo, HitCircle and Spinner count here, and use them globally instead of separate variable (together with SliderCount)
// 3) Remove ApproachRate and OverallDifficulty
struct DifficultyAttributes {
    f64 AimDifficulty{0.};
    f64 AimDifficultSliderCount{0.};

    f64 SpeedDifficulty{0.};
    f64 SpeedNoteCount{0.};

    f64 ReadingDifficulty{0.};
    f64 ReadingDifficultNoteCount{0.};

    f64 FlashlightDifficulty{0.};

    f64 SliderFactor{0.};

    f64 AimTopWeightedSliderFactor{0.};
    f64 SpeedTopWeightedSliderFactor{0.};

    f64 AimDifficultStrainCount{0.};
    f64 SpeedDifficultStrainCount{0.};

    f64 NestedScorePerObject{0.};
    f64 LegacyScoreBaseMultiplier{0.};

    i32 SliderCount{0};
    u32 MaximumLegacyComboScore{0};

    // Those 3 attributes are performance calculator only (for now)
    // TODO: use SliderCount globally like the attributes above and remove AR and OD
    f64 ApproachRate{0.};
    f64 OverallDifficulty{0.};
};

// This structs has all the beatmap data necessary for difficulty calculation
// Its purpose is to remove dependency of diffcalc on the specific beatmap class/object
struct BeatmapDiffcalcData {
    // Hitobjects
    std::vector<DifficultyHitObject> &sortedHitObjects;

    // Basic attributes, they're NOT adjusted by rate
    f32 CS{5.f}, HP{5.f}, AR{5.f}, OD{5.f};

    // raw file values: the scorev1 base multiplier reads the original beatmap in lazer, so it
    // ignores HR/EZ/override-adjusted stats
    f32 fileCS{5.f}, fileHP{5.f}, fileOD{5.f};

    // Relevant mods
    bool hidden{false}, relax{false}, autopilot{false}, touchDevice{false}, flashlight{false};
    f32 speedMultiplier{1.f};

    u32 breakDuration{0};
};

// raw difficulty values before the final rating transform. the hidden-dependent skills carry
// both variants so recomputeStarRating stays pure math for HD pairs (no strain recompute).
// the flashlight values are only filled when the flashlight flag was set for the calculation.
struct RawDifficultyValues {
    f64 aimNoSliders{0.};
    f64 aim{0.};
    f64 speed{0.};
    f64 readingNoHidden{0.};
    f64 readingHidden{0.};
    f64 flashlightNoHidden{0.};
    f64 flashlightHidden{0.};
};

struct StarCalcParams {
    DifficultyAttributes &outAttributes;
    const BeatmapDiffcalcData &beatmapData;

    std::vector<f32> *outAimStrains;
    std::vector<f32> *outSpeedStrains;
    i32 upToObjectIndex{-1};

    // cancellation
    Sync::stop_token cancelCheck{};

    // if non-null, raw difficulty values are written here before the rating transform
    RawDifficultyValues *outRawDifficulty{nullptr};

    // if non-null, tracks which parameters the computed fields of beatmapData.sortedHitObjects were
    // last fully computed with: on match the whole preprocessing+strain pass is skipped, otherwise
    // it runs and the state is updated (so repeat calls over the same vector are cheap, e.g. live pp
    // with an increasing upToObjectIndex). pass the state owned by the vector's LOAD_DIFFOBJ_RESULT,
    // or null to always recompute.
    StrainComputeState *strainState{nullptr};
};

// stars, standalone
f64 calculateStarDiffForHitObjects(StarCalcParams &params);

// recompute final star rating from pre-calculated raw difficulty values with
// different mod flags (e.g. hidden). skips all strain/difficulty calculation.
f64 recomputeStarRating(const RawDifficultyValues &raw, const BeatmapDiffcalcData &beatmapData);

struct PPv2CalcParams {
    DifficultyAttributes attributes;

    ModFlags modFlags;
    f64 timescale;
    f64 ar;
    f64 od;

    i32 numHitObjects;
    i32 numCircles;
    i32 numSliders;

    i32 numSpinners;
    i32 maxPossibleCombo;
    i32 combo;
    i32 misses;
    i32 c300;
    i32 c100;
    i32 c50;

    u32 legacyTotalScore;
    bool isMcOsuImported;  // mcosu scores use a different scorev1 algorithm
};

// pp, standalone
f64 calculatePPv2(PPv2CalcParams &cparams);

// misc public utils
[[nodiscard]] f64 getScoreV1ScoreMultiplier(ModFlags flags, f64 speedOverride, bool mcosu = false);
std::string PPv2CalcParamsToString(const PPv2CalcParams &pars);

}  // namespace DiffCalc
}  // namespace neomod
