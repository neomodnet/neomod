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

#include <vector>
#include <array>
#include <memory>
#include <algorithm>

enum class ModFlags : u64;

class SliderCurve;
class ConVar;
class AbstractBeatmapInterface;
class DatabaseBeatmap;

struct FinishedScore;

struct SLIDER_SCORING_TIME {
    enum class TYPE : u8 {
        TICK,
        REPEAT,
        END,
    };

    TYPE type;
    f32 time;
};

class DifficultyHitObject {
   public:
    enum class TYPE : u8 {
        INVALID = 0,
        CIRCLE,
        SPINNER,
        SLIDER,
    };

   public:
    DifficultyHitObject() = delete;

    DifficultyHitObject(TYPE type, vec2 pos, i32 time);               // circle
    DifficultyHitObject(TYPE type, vec2 pos, i32 time, i32 endTime);  // spinner
    DifficultyHitObject(TYPE type, vec2 pos, i32 time, i32 endTime, f32 spanDuration, i8 osuSliderCurveType,
                        const std::vector<vec2> &controlPoints, f32 pixelLength,
                        std::vector<SLIDER_SCORING_TIME> scoringTimes, i32 repeats,
                        bool calculateSliderCurveInConstructor);  // slider
    ~DifficultyHitObject();

    DifficultyHitObject(const DifficultyHitObject &) = delete;
    DifficultyHitObject(DifficultyHitObject &&dobj) noexcept;

    DifficultyHitObject &operator=(const DifficultyHitObject &dobj) = delete;
    DifficultyHitObject &operator=(DifficultyHitObject &&dobj) noexcept;

    void updateStackPosition(f32 stackOffset);
    void updateCurveStackPosition(f32 stackOffset);

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
    f32 scheduledCurveAllocStackOffset;
    i32 stack;
    vec2 originalPos;

    TYPE type;
    i8 osuSliderCurveType;
    bool scheduledCurveAlloc;
};

namespace DiffCalc {
// for forward declaration
extern const u32 PP_ALGORITHM_VERSION;
}  // namespace DiffCalc

class DifficultyCalculator {
   public:
    struct Skills {
        // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
        enum Skill : u8 { SPEED, AIM_SLIDERS, AIM_NO_SLIDERS, NUM_SKILLS };
    };

    static constexpr const f64 performance_base_multiplier = 1.14;  // keep final pp normalized across changes

    // see https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Speed.cs
    // see https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Aim.cs

    // how much strains decay per interval (if the previous interval's peak strains after applying decay are still higher than the current one's, they will be used as the peak strains).
    static constexpr const f64 decay_base[Skills::NUM_SKILLS] = {0.3, 0.15, 0.15};

    static constexpr const f64 DIFFCALC_EPSILON = 1e-32;

    // This struct is the stripped out version of difficulty attributes needed for incremental (per-object) calculation
    struct IncrementalState {
        f64 interval_end;
        f64 max_strain;
        f64 max_object_strain;
        f64 max_slider_strain;

        // for difficult strain count calculation
        f64 consistent_top_strain;
        f64 difficult_strains;
        f64 top_weighted_sliders;

        // difficulty attributes
        f64 aim_difficult_slider_count;
        f64 speed_note_count;

        std::vector<f64> highest_strains;
        std::vector<f64> slider_strains;
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

    struct DiffObject {
       public:
        // move-only
        DiffObject(const DiffObject &) = delete;
        DiffObject &operator=(const DiffObject &) = delete;
        DiffObject(DiffObject &&) = default;
        DiffObject &operator=(DiffObject &&) = delete;

       public:
        DiffObject() = delete;
        ~DiffObject() = default;

        DiffObject(DifficultyHitObject *base_object, f32 radius_scaling_factor,
                   const std::vector<DiffObject> *diff_objects, i32 prevObjectIdx)
            : ho(base_object),
              norm_start(ho->pos * radius_scaling_factor),
              lazyEndPos(ho->pos),
              prevObjectIndex(prevObjectIdx),
              objects(diff_objects) {}

        std::array<f64, Skills::NUM_SKILLS> strains{};

        DifficultyHitObject *ho;

        // https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Speed.cs
        // needed because raw speed strain and rhythm strain are combined in different ways
        f64 raw_speed_strain{0.};
        f64 rhythm{0.};

        vec2 norm_start;  // start position normalized on radius

        f64 angle{std::numeric_limits<f64>::quiet_NaN()};  // precalc

        f64 jumpDistance{0.};     // precalc
        f64 minJumpDistance{0.};  // precalc
        f64 minJumpTime{0.};      // precalc
        f64 travelDistance{0.};   // precalc

        f64 delta_time{0.};           // strain temp
        f64 adjusted_delta_time{0.};  // strain temp

        vec2 lazyEndPos;         // precalc temp
        f64 lazyTravelDist{0.};  // precalc temp
        f64 lazyTravelTime{0.};  // precalc temp
        f64 travelTime{0.};      // precalc temp
        f64 smallCircleBonus{1.};

        i32 prevObjectIndex;  // WARNING: this will be -1 for the first object (as the name implies), see note above

        bool lazyCalcFinished{false};  // precalc temp

        // NOTE: McOsu stores the first object in this array while lazer doesn't. newer lazer algorithms require referencing objects "randomly", so we just keep the entire vector around.
        const std::vector<DiffObject> *objects;

        [[nodiscard]] inline const DiffObject *get_previous(i32 backwardsIdx) const {
            return (objects->size() > 0 && prevObjectIndex - backwardsIdx < (i32)objects->size()
                        ? &(*objects)[std::max(0, prevObjectIndex - backwardsIdx)]
                        : nullptr);
        }
        [[nodiscard]] inline const DiffObject *get_next(i32 forwardIdx) const {
            return (objects->size() > 0 && prevObjectIndex + forwardIdx < (i32)objects->size()
                        ? &(*objects)[std::max(0, prevObjectIndex + forwardIdx)]
                        : nullptr);
        }

        [[nodiscard]] inline f64 get_strain(Skills::Skill type) const {
            return strains[type] * (type == Skills::SPEED ? rhythm : 1.0);
        }
        [[nodiscard]] inline f64 get_slider_strain(Skills::Skill type) const {
            return ho->type == DifficultyHitObject::TYPE::SLIDER
                       ? strains[type] * (type == Skills::SPEED ? rhythm : 1.0)
                       : -1;
        }

        inline static f64 applyDiminishingExp(f64 val) { return std::pow(val, 0.99); }
        inline static f64 strainDecay(Skills::Skill type, f64 ms) { return std::pow(decay_base[type], ms / 1000.0); }

        void calculate_strains(const DiffObject &prev, const DiffObject *next, f64 hitWindow300, bool autopilotNerf);
        void calculate_strain(const DiffObject &prev, const DiffObject *next, f64 hitWindow300, bool autopilotNerf,
                              const Skills::Skill dtype);
        static f64 calculate_difficulty(const Skills::Skill type, const DiffObject *dobjects, uSz dobjectCount,
                                        IncrementalState *incremental, std::vector<f64> *outStrains = nullptr,
                                        DifficultyAttributes *outAttributes = nullptr);

        f64 spacing_weight2(const Skills::Skill diff_type, const DiffObject &prev, const DiffObject *next,
                            f64 hitWindow300, bool autopilotNerf);
        f64 get_doubletapness(const DiffObject *next, f64 hitWindow300) const;
    };

   public:
    // raw difficulty values before the final rating transform (computeAimRating/computeSpeedRating).
    // identical between hidden and non-hidden for the same strains, so can be reused
    // to avoid redundant calculate_difficulty calls for HD pairs.
    struct RawDifficultyValues {
        f64 aimNoSliders{0.};
        f64 aim{0.};
        f64 speed{0.};
    };

    struct StarCalcParams {
        std::unique_ptr<std::vector<DiffObject>> cachedDiffObjects;
        DifficultyAttributes &outAttributes;
        const BeatmapDiffcalcData &beatmapData;

        std::vector<f64> *outAimStrains;
        std::vector<f64> *outSpeedStrains;
        IncrementalState *incremental;
        i32 upToObjectIndex{-1};

        // cancellation
        Sync::stop_token cancelCheck{};

        // if non-null, raw difficulty values are written here before the rating transform
        RawDifficultyValues *outRawDifficulty{nullptr};

        // "pseudo-incremental":
        // ignore "upToObjectIndex" to expect future calls with a larger object index
        // by pre-calculating and filling cachedDiffObjects, if it's empty
        bool forceFillDiffobjCache{false};
    };

    // stars, fully static
    static f64 calculateStarDiffForHitObjects(StarCalcParams &params);

    // recompute final star rating from pre-calculated raw difficulty values with
    // different mod flags (e.g. hidden). skips all strain/difficulty calculation.
    static f64 recomputeStarRating(const RawDifficultyValues &raw, const BeatmapDiffcalcData &beatmapData);

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

    // pp, fully static
    static f64 calculatePPv2(PPv2CalcParams &cparams);

    // misc public utils
    [[nodiscard]] static f64 getScoreV1ScoreMultiplier(ModFlags flags, f64 speedOverride, bool mcosu = false);
    static std::string PPv2CalcParamsToString(const PPv2CalcParams &pars);

   private:
    // helper functions
    [[nodiscard]] static f64 calculateTotalStarsFromSkills(f64 aim, f64 speed);
    static void calculateScoreV1Attributes(DifficultyAttributes &attributes, const BeatmapDiffcalcData &beatmapData,
                                           i32 upToObjectIndex);
    [[nodiscard]] static f64 calculateScoreV1SpinnerScore(f64 spinnerDuration);

   private:
    struct ScoreData {
        ModFlags modFlags;
        f64 accuracy;
        i32 countGreat;
        i32 countOk;
        i32 countMeh;
        i32 countMiss;
        i32 totalHits;
        i32 totalSuccessfulHits;
        i32 beatmapMaxCombo;
        i32 scoreMaxCombo;
        i32 amountHitObjectsWithAccuracy;
        u32 legacyTotalScore;
    };

    struct RhythmIsland {
        // NOTE: lazer stores "deltaDifferenceEpsilon" (hitWindow300 * 0.3) in this struct, but OD is constant here
        i32 delta;
        i32 deltaCount;

        inline bool equals(RhythmIsland &other, f64 deltaDifferenceEpsilon) const {
            return std::abs(delta - other.delta) < deltaDifferenceEpsilon && deltaCount == other.deltaCount;
        }
    };
    static thread_local std::vector<std::pair<RhythmIsland, int>> islandCounts;

   private:
    // Skill values calculation
    static f64 computeAimValue(const ScoreData &score, const DifficultyAttributes &attributes, f64 effectiveMissCount);
    static f64 computeSpeedValue(const ScoreData &score, const DifficultyAttributes &attributes, f64 effectiveMissCount,
                                 f64 speedDeviation);
    static f64 computeAccuracyValue(const ScoreData &score, const DifficultyAttributes &attributes);

    // High deviation nerf
    static f64 calculateSpeedDeviation(const ScoreData &score, const DifficultyAttributes &attributes, f64 timescale);
    static f64 calculateDeviation(const DifficultyAttributes &attributes, f64 timescale, f64 relevantCountGreat,
                                  f64 relevantCountOk, f64 relevantCountMeh);
    static f64 calculateSpeedHighDeviationNerf(const DifficultyAttributes &attributes, f64 speedDeviation);

    static f64 calculateEstimatedSliderBreaks(const ScoreData &score, f64 topWeightedSliderFactor,
                                              f64 effectiveMissCount);

    // ScoreV1 misscount estimation
    static f64 calculateScoreBasedMisscount(const DifficultyAttributes &attributes, const ScoreData &score,
                                            f64 timescale, bool isMcOsuImported);
    static f64 calculateScoreAtCombo(const DifficultyAttributes &attributes, const ScoreData &score, f64 combo,
                                     f64 relevantComboPerObject, f64 scoreV1Multiplier);
    static f64 calculateRelevantScoreComboPerObject(const DifficultyAttributes &attributes, const ScoreData &score);
    static f64 calculateMaximumComboBasedMissCount(const DifficultyAttributes &attributes, const ScoreData &score);

    static f64 calculateDifficultyRating(f64 difficultyValue);
    static f64 calculateAimVisibilityFactor(f64 approachRate, f64 mechanicalDifficultyRating);
    static f64 calculateSpeedVisibilityFactor(f64 approachRate, f64 mechanicalDifficultyRating);
    static f64 calculateVisibilityBonus(f64 approachRate, f64 visibilityFactor = 1.0, f64 sliderFactor = 1.0);
    static f64 computeAimRating(f64 aimDifficultyValue, u32 totalHits, f64 approachRate, f64 overallDifficulty,
                                f64 mechanicalDifficultyRating, f64 sliderFactor,
                                const BeatmapDiffcalcData &beatmapData);
    static f64 computeSpeedRating(f64 speedDifficultyValue, u32 totalHits, f64 approachRate, f64 overallDifficulty,
                                  f64 mechanicalDifficultyRating, const BeatmapDiffcalcData &beatmapData);
    static f64 calculateStarRating(f64 basePerformance);
    static f64 calculateMechanicalDifficultyRating(f64 aimDifficultyValue, f64 speedDifficultyValue);

    // helper functions
    static f64 erf(f64 x);
    static f64 erfInv(f64 x);
    static forceinline INLINE_BODY f64 reverseLerp(f64 x, f64 start, f64 end) {
        return std::clamp<f64>((x - start) / (end - start), 0.0, 1.0);
    };
    static forceinline INLINE_BODY f64 smoothstep(f64 x, f64 start, f64 end) {
        x = reverseLerp(x, start, end);
        return x * x * (3.0 - 2.0 * x);
    };
    static forceinline INLINE_BODY f64 smootherStep(f64 x, f64 start, f64 end) {
        x = reverseLerp(x, start, end);
        return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
    };
    static forceinline INLINE_BODY f64 smoothstepBellCurve(f64 x, f64 mean = 0.5, f64 width = 0.5) {
        x -= mean;
        x = x > 0 ? (width - x) : (width + x);
        return smoothstep(x, 0, width);
    };
    static forceinline INLINE_BODY f64 logistic(f64 x, f64 midpointOffset, f64 multiplier, f64 maxValue = 1.0) {
        return maxValue / (1 + std::exp(multiplier * (midpointOffset - x)));
    }
    static forceinline INLINE_BODY f64 strainDifficultyToPerformance(f64 difficulty) {
        return std::pow(5.0 * std::max(1.0, difficulty / 0.0675) - 4.0, 3.0) / 100000.0;
    }

    // Adjust hitwindow to match lazer
    static forceinline INLINE_BODY f64 adjustHitWindow(f64 hitwindow) { return std::floor(hitwindow) - 0.5; }

    // Lazer formula for adjusting OD by clock rate
    static f64 adjustOverallDifficultyByClockRate(f64 OD, f64 clockRate);
};
