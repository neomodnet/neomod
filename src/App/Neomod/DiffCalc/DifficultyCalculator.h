#pragma once
// Copyright (c) 2019, PG & Francesco149 & Khangaroo & Givikap120, All rights reserved.

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

struct Skills {
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
    enum Skill : u8 { SPEED, AIM_SLIDERS, AIM_NO_SLIDERS, NUM_SKILLS };
};

inline constexpr const f64 performance_base_multiplier = 1.14;  // keep final pp normalized across changes

// see https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Speed.cs
// see https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Aim.cs

// how much strains decay per interval (if the previous interval's peak strains after applying decay are still higher than the current one's, they will be used as the peak strains).
inline constexpr const f64 decay_base[Skills::NUM_SKILLS] = {0.3, 0.15, 0.15};

inline constexpr const f64 DIFFCALC_EPSILON = 1e-32;

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

    void updateStackPosition(f32 stackOffset);

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

    // star calc methods, these operate on the computed fields below
    [[nodiscard]] inline const DifficultyHitObject *get_previous(i32 backwardsIdx) const {
        // NOTE: never null for a non-empty array, clamps to the first object instead (unlike lazer's Previous())
        return (numObjects > 0 && index - backwardsIdx < numObjects ? &objects[std::max(0, index - backwardsIdx)]
                                                                    : nullptr);
    }
    [[nodiscard]] inline const DifficultyHitObject *get_next(i32 forwardIdx) const {
        // NOTE: this actually returns the *previous* object for forwardIdx == 0 (indices are relative to
        // index, like get_previous), unlike lazer's Next(0) which is the real next object.
        // its only caller (rhythm doubletapness) relies on the resulting values, so keep it as-is.
        return (numObjects > 0 && index + forwardIdx < numObjects ? &objects[std::max(0, index + forwardIdx)]
                                                                  : nullptr);
    }

    [[nodiscard]] inline f64 get_strain(Skills::Skill dtype) const {
        return strains[dtype] * (dtype == Skills::SPEED ? rhythm : 1.0);
    }
    [[nodiscard]] inline f64 get_slider_strain(Skills::Skill dtype) const {
        return type == TYPE::SLIDER ? strains[dtype] * (dtype == Skills::SPEED ? rhythm : 1.0) : -1;
    }

    inline static f64 strainDecay(Skills::Skill dtype, f64 ms) { return std::pow(decay_base[dtype], ms / 1000.0); }

    void calculate_strains(const DifficultyHitObject &prev, const DifficultyHitObject *next, f64 hitWindow300,
                           bool autopilotNerf, f64 smallCircleBonus);
    void calculate_strain(const DifficultyHitObject &prev, const DifficultyHitObject *next, f64 hitWindow300,
                          bool autopilotNerf, f64 smallCircleBonus, const Skills::Skill dtype);
    f64 spacing_weight2(const Skills::Skill diff_type, const DifficultyHitObject &prev, const DifficultyHitObject *next,
                        f64 hitWindow300, bool autopilotNerf, f64 smallCircleBonus);
    [[nodiscard]] f64 get_doubletapness(const DifficultyHitObject *next, f64 hitWindow300) const;

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
    // computed by the star calc (calculateStarDiffForHitObjects), never set by the loader.
    // IMPORTANT: resetComputedFields() below must reset every field in this section, keep the two in sync!

    std::array<f64, Skills::NUM_SKILLS> strains{};

    // https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Speed.cs
    // needed because raw speed strain and rhythm strain are combined in different ways
    f64 raw_speed_strain{0.};
    f64 rhythm{0.};

    vec2 norm_start{};  // start position normalized on radius

    f64 angle{std::numeric_limits<f64>::quiet_NaN()};  // precalc

    f64 lazyJumpDistance{0.};     // precalc
    f64 minimumJumpDistance{0.};  // precalc
    f64 minimumJumpTime{0.};      // precalc
    f64 travelDistance{0.};       // precalc

    f64 deltaTime{0.};   // strain temp
    f64 strainTime{0.};  // strain temp

    vec2 lazyEndPos{};       // precalc temp
    f64 lazyTravelDist{0.};  // precalc temp
    f64 lazyTravelTime{0.};  // precalc temp
    f64 travelTime{0.};      // precalc temp

    // first element (data()) and size of the containing array, so lazer-style previous()/next()
    // lookups work anywhere (lazer stores the equivalent list reference per object).
    // points at the element buffer, which is stable across vector/LOAD_DIFFOBJ_RESULT moves.
    const DifficultyHitObject *objects{nullptr};
    i32 numObjects{0};

    // position in lazer's difficulty hit object list (== lazer's Index): lazer excludes the first
    // hitobject, so this is the array position minus 1. WARNING: -1 for the first object!
    i32 index{-1};

    bool lazyCalcFinished{false};  // precalc temp

    // brings every computed field into the same state a freshly constructed object would have,
    // done for the whole vector before every full computation
    inline void resetComputedFields(const DifficultyHitObject *allObjects, i32 numObjs, i32 arrayIndex,
                                    f32 radiusScalingFactor) {
        strains = {};
        raw_speed_strain = 0.;
        rhythm = 0.;
        norm_start = pos * radiusScalingFactor;
        angle = std::numeric_limits<f64>::quiet_NaN();
        lazyJumpDistance = 0.;
        minimumJumpDistance = 0.;
        minimumJumpTime = 0.;
        travelDistance = 0.;
        deltaTime = 0.;
        strainTime = 0.;
        lazyEndPos = pos;
        lazyTravelDist = 0.;
        lazyTravelTime = 0.;
        travelTime = 0.;
        objects = allObjects;
        numObjects = numObjs;
        index = arrayIndex - 1;
        lazyCalcFinished = false;
    }

    // for the move ctor/assignment (all computed fields are trivially copyable)
    inline void copyComputedFields(const DifficultyHitObject &dobj) {
        strains = dobj.strains;
        raw_speed_strain = dobj.raw_speed_strain;
        rhythm = dobj.rhythm;
        norm_start = dobj.norm_start;
        angle = dobj.angle;
        lazyJumpDistance = dobj.lazyJumpDistance;
        minimumJumpDistance = dobj.minimumJumpDistance;
        minimumJumpTime = dobj.minimumJumpTime;
        travelDistance = dobj.travelDistance;
        deltaTime = dobj.deltaTime;
        strainTime = dobj.strainTime;
        lazyEndPos = dobj.lazyEndPos;
        lazyTravelDist = dobj.lazyTravelDist;
        lazyTravelTime = dobj.lazyTravelTime;
        travelTime = dobj.travelTime;
        objects = dobj.objects;
        numObjects = dobj.numObjects;
        index = dobj.index;
        lazyCalcFinished = dobj.lazyCalcFinished;
    }
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

    // Relevant mods
    bool hidden{false}, relax{false}, autopilot{false}, touchDevice{false};
    f32 speedMultiplier{1.f};

    u32 breakDuration{0};
    u32 playableLength{0};
};

// raw difficulty values before the final rating transform (computeAimRating/computeSpeedRating).
// identical between hidden and non-hidden for the same strains, so can be reused
// to avoid redundant calculate_difficulty calls for HD pairs.
struct RawDifficultyValues {
    f64 aimNoSliders{0.};
    f64 aim{0.};
    f64 speed{0.};
};

struct StarCalcParams {
    DifficultyAttributes &outAttributes;
    const BeatmapDiffcalcData &beatmapData;

    std::vector<f64> *outAimStrains;
    std::vector<f64> *outSpeedStrains;
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
