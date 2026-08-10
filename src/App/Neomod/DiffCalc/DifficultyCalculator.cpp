// Copyright (c) 2019, PG & Francesco149 & Khangaroo & Givikap120, 2026, WH, All rights reserved.
#include "DifficultyCalculator.h"

#include "DatabaseBeatmapTypes.h"
#include "SliderCurves.h"
#include "GameRules.h"
#include "ModFlags.h"
#include "InlineVec.h"

#include <numbers>
#include <utility>
#include <cstring>
#include <type_traits>
#include <span>

#ifndef BUILD_TOOLS_ONLY
#include "OsuConVars.h"
#define STARS_SLIDER_CURVE_POINTS_SEPARATION cv::stars_slider_curve_points_separation.getFloat()
#define IGNORE_CLAMPED_SLIDERS cv::stars_ignore_clamped_sliders.getBool()
#define SLIDER_CURVE_MAX_LENGTH cv::slider_curve_max_length.getFloat()
#define SLIDER_END_INSIDE_CHECK_OFFSET (f64) cv::slider_end_inside_check_offset.getInt()

#define FORMAT_STRING_ fmt::format

#else

#include "OsuConVars/DiffCalcDefaults.h"

#define STARS_SLIDER_CURVE_POINTS_SEPARATION cv::defaults::stars_slider_curve_points_separation
#define IGNORE_CLAMPED_SLIDERS cv::defaults::stars_ignore_clamped_sliders
#define SLIDER_CURVE_MAX_LENGTH cv::defaults::slider_curve_max_length
#define SLIDER_END_INSIDE_CHECK_OFFSET (f64) cv::defaults::slider_end_inside_check_offset

#include <print>
#include <algorithm>
#define FORMAT_STRING_ std::format

#endif

namespace neomod::DiffCalc {
// see https://github.com/ppy/osu/pull/37850
const u32 PP_ALGORITHM_VERSION{20260706};

namespace {
// internal helper utils (forward decls)

struct Skills {
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
    enum Skill : u8 { SPEED, AIM_SLIDERS, AIM_NO_SLIDERS, NUM_SKILLS };
};

inline constexpr const f64 performance_base_multiplier = 1.12;  // keep final pp normalized across changes

// see https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Speed.cs
// see https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Aim.cs

// normalized EMA decay bases per skill (strain = strain * decay + noteDiff * (1 - decay))
inline constexpr const f64 decay_base[Skills::NUM_SKILLS] = {0.3, 0.2, 0.2};

static void calculateScoreV1Attributes(DifficultyAttributes &attributes, const BeatmapDiffcalcData &beatmapData,
                                       i32 upToObjectIndex);
static f64 calculateScoreV1SpinnerScore(f64 spinnerDuration);

// per-map constants shared by every strain evaluator (mod effects are per-note now)
struct StrainEvalContext {
    f64 hitWindow300{};             // rate-adjusted, == lazer HitWindowGreat
    f64 circleRadius{};             // real (unnormalized) circle radius in osu!pixels
    f64 smallCircleBonus{1.};       // max(1, 1 + (30 - radius) / 70)
    f64 smallCircleBonusSqrt{1.};   // sqrt(smallCircleBonus) (flow aim)
    f64 smallCircleBonusPow15{1.};  // smallCircleBonus^1.5 (agility)
    f64 odScalingAimFl{1.};         // 0.985 + OD^2 / 4000 (per-note OD scaling for aim + flashlight)
    f64 odScalingReading{1.};       // 0.825 + OD^2.2 / 1125
    f64 preemptRaw{};               // beatmap-time preempt from AR (OpacityAt domain)
    f64 preemptAdjusted{};          // rate-adjusted preempt (lazer Preempt)
    f64 fadeInRaw{};                // 400 * min(1, preemptRaw / 450), beatmap-time

    bool relax{false};
    bool autopilot{false};
    bool touchDevice{false};
    bool flashlight{false};
};

static void calculate_strains(DifficultyHitObject &cur, const DifficultyHitObject &prev, const StrainEvalContext &ctx);

// per-note strain evaluators (lazer Evaluators/*), definitions below the strain pass
static f64 evaluate_aim_difficulty_of(const DifficultyHitObject &cur, bool withSliders, f64 agilityDifficultyRaw,
                                      const StrainEvalContext &ctx);
static f64 evaluate_agility_of(const DifficultyHitObject &cur, const StrainEvalContext &ctx);
static f64 evaluate_speed_difficulty_of(const DifficultyHitObject &cur, const StrainEvalContext &ctx);
static f64 evaluate_rhythm_of(const DifficultyHitObject &cur, const StrainEvalContext &ctx);
static f64 calculate_double_tap_feasibility(const DifficultyHitObject &cur, const DifficultyHitObject *next,
                                            f64 hitWindow300);
// both hidden variants of the hidden-dependent skills are evaluated in one pass
struct ReadingDifficultyPair {
    f64 noHidden;
    f64 hidden;
};
struct FlashlightDifficultyPair {
    f64 noHidden;
    f64 hidden;
};
static ReadingDifficultyPair evaluate_reading_difficulty_of(const DifficultyHitObject &cur,
                                                            const StrainEvalContext &ctx);
static FlashlightDifficultyPair evaluate_flashlight_difficulty_of(const DifficultyHitObject &cur,
                                                                  const StrainEvalContext &ctx);

// final difficulty calculation over the first dobjectCount objects (array position 0 is not a
// lazer difficulty object; all of these iterate lazer objects only, i.e. array positions 1+)
static f64 calculate_aim_difficulty(Skills::Skill type, const DifficultyHitObject *dobjects, uSz dobjectCount);
static f64 calculate_speed_difficulty(const DifficultyHitObject *dobjects, uSz dobjectCount, f64 &outObjectWeightSum);
static f64 calculate_reading_difficulty(const DifficultyHitObject *dobjects, uSz dobjectCount, bool hidden,
                                        f64 &outDifficultNoteCount);
static f64 calculate_flashlight_difficulty(const DifficultyHitObject *dobjects, uSz dobjectCount, bool hidden);
static std::vector<f64> build_fixed_strain_sections(const DifficultyHitObject *dobjects, uSz dobjectCount,
                                                    f64 decayBase, f64 (*strainOf)(const DifficultyHitObject &));

// per-object weighted counts (lazer Count*/Relevant*/GetDifficultSliders)
static f64 count_top_weighted_strains_geometric(const DifficultyHitObject *dobjects, uSz dobjectCount,
                                                Skills::Skill type, f64 difficultyValue);
static f64 count_top_weighted_sliders_geometric(const DifficultyHitObject *dobjects, uSz dobjectCount,
                                                Skills::Skill type, f64 difficultyValue);
static f64 calculate_aim_difficult_slider_count(const DifficultyHitObject *dobjects, uSz dobjectCount,
                                                Skills::Skill type);
static f64 calculate_speed_relevant_object_count(const DifficultyHitObject *dobjects, uSz dobjectCount);
static f64 count_top_weighted_speed_strains(const DifficultyHitObject *dobjects, uSz dobjectCount, f64 difficultyValue,
                                            f64 objectWeightSum);
static f64 count_top_weighted_speed_sliders(const DifficultyHitObject *dobjects, uSz dobjectCount, f64 difficultyValue,
                                            f64 objectWeightSum);

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

// Skill values calculation
static f64 computeAimValue(const ScoreData &score, const DifficultyAttributes &attributes, f64 effectiveMissCount,
                           f64 aimEstimatedSliderBreaks);
static f64 computeSpeedValue(const ScoreData &score, const DifficultyAttributes &attributes, f64 effectiveMissCount,
                             f64 speedEstimatedSliderBreaks, f64 speedDeviation);
static f64 computeAccuracyValue(const ScoreData &score, const DifficultyAttributes &attributes);
static f64 computeReadingValue(const ScoreData &score, const DifficultyAttributes &attributes, f64 effectiveMissCount,
                               f64 aimEstimatedSliderBreaks);
static f64 computeFlashlightValue(const ScoreData &score, const DifficultyAttributes &attributes,
                                  f64 effectiveMissCount);

// miss penalty assumes that a player will miss on the hardest parts of a map, so the amount of
// relatively difficult sections adjusts it to be more punishing on maps with fewer hard sections
static forceinline INLINE_BODY f64 calculateMissPenalty(f64 missCount, f64 difficultStrainCount) {
    return 0.93 / (missCount / (4.0 * std::log(std::max(1.0, difficultStrainCount))) + 1.0);
}

// High deviation nerf
static f64 calculateSpeedDeviation(const ScoreData &score, const DifficultyAttributes &attributes, f64 timescale);
static f64 calculateDeviation(const DifficultyAttributes &attributes, f64 timescale, f64 relevantCountGreat,
                              f64 relevantCountOk, f64 relevantCountMeh);
static f64 calculateSpeedHighDeviationNerf(const DifficultyAttributes &attributes, f64 speedDeviation);

static f64 calculateEstimatedSliderBreaks(const ScoreData &score, f64 topWeightedSliderFactor, f64 effectiveMissCount);

// ScoreV1 misscount estimation
static f64 calculateScoreBasedMisscount(const DifficultyAttributes &attributes, const ScoreData &score, f64 timescale,
                                        bool isMcOsuImported);
static f64 calculateScoreAtCombo(const DifficultyAttributes &attributes, const ScoreData &score, f64 combo,
                                 f64 relevantComboPerObject, f64 scoreV1Multiplier);
static f64 calculateRelevantScoreComboPerObject(const DifficultyAttributes &attributes, const ScoreData &score);
static f64 calculateMaximumComboBasedMissCount(const DifficultyAttributes &attributes, const ScoreData &score);

// rating transforms + star rating
// https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/OsuDifficultyCalculator.cs
static f64 calculateAimDifficultyRating(f64 difficultyValue);
static f64 calculateDifficultyRating(f64 difficultyValue);
static f64 sumCognitionDifficulty(f64 reading, f64 flashlight);
static f64 calculateStarRatingFromRatings(f64 aimRating, f64 speedRating, f64 readingRating, f64 flashlightRating);

// helper functions
static f64 erf(f64 x);
static f64 erfInv(f64 x);
static forceinline INLINE_BODY f64 reverseLerp(f64 x, f64 start, f64 end) {
    return std::clamp<f64>((x - start) / (end - start), 0.0, 1.0);
}
static forceinline INLINE_BODY f64 smoothstep(f64 x, f64 start, f64 end) {
    x = reverseLerp(x, start, end);
    return x * x * (3.0 - 2.0 * x);
}
static forceinline INLINE_BODY f64 smootherStep(f64 x, f64 start, f64 end) {
    x = reverseLerp(x, start, end);
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}
// 1 at x = 0.5, smoothly falling to 0 at x = 0 and x = 1 (lazer single-arg SmoothstepBellCurve)
static forceinline INLINE_BODY f64 smoothstepBellCurve(f64 x) {
    x = 0.5 - std::abs(x - 0.5);
    x = std::clamp(x * 2.0, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}
static forceinline INLINE_BODY f64 logistic(f64 x, f64 midpointOffset, f64 multiplier, f64 maxValue = 1.0) {
    return maxValue / (1 + std::exp(multiplier * (midpointOffset - x)));
}
// single-exponent overload (lazer DiffUtils.Logistic(exponent, maxValue))
static forceinline INLINE_BODY f64 logisticExp(f64 exponent, f64 maxValue = 1.0) {
    return maxValue / (1 + std::exp(exponent));
}
// p-norm (lazer DiffUtils.Norm)
static forceinline INLINE_BODY f64 norm(f64 p, std::initializer_list<f64> values) {
    f64 sum = 0.0;
    for(const f64 x : values) sum += std::pow(x, p);
    return std::pow(sum, 1.0 / p);
}
// small integer exponents are repeated multiplication upstream (lazer DiffUtils.Pow(double, int)),
// replicate exactly for determinism
static forceinline INLINE_BODY f64 powi(f64 x, i32 exponent) {
    switch(exponent) {
        case 0:
            return 1.0;
        case 1:
            return x;
        case 2:
            return x * x;
        case 3:
            return x * x * x;
        case 4:
            return x * x * x * x;
        case 5:
            return x * x * x * x * x;
        default:
            return std::pow(x, (f64)exponent);
    }
}
// 4 * d^3, shared by aim (on the rating), speed and reading (lazer HarmonicSkill/OsuPerformanceCalculator.DifficultyToPerformance)
static forceinline INLINE_BODY f64 difficultyToPerformance(f64 difficulty) { return 4.0 * powi(difficulty, 3); }
// 25 * d^2 (lazer Flashlight.DifficultyToPerformance)
static forceinline INLINE_BODY f64 flashlightDifficultyToPerformance(f64 difficulty) {
    return 25.0 * powi(difficulty, 2);
}

static forceinline INLINE_BODY f64 strainDecay(Skills::Skill dtype, f64 ms) {
    return std::pow(decay_base[dtype], ms / 1000.0);
}

// Adjust hitwindow to match lazer
static forceinline INLINE_BODY f64 adjustHitWindow(f64 hitwindow) { return std::floor(hitwindow) - 0.5; }

// Lazer formula for adjusting OD by clock rate
static f64 adjustOverallDifficultyByClockRate(f64 OD, f64 clockRate);
}  // namespace

// computed by the star calc (calculateStarDiffForHitObjects), never set by the loader/DifficultHitObject ctor.
// IMPORTANT: resetFields() below must reset every field in this section, keep the two in sync!
struct DifficultyHitObject::Computed {
    // star calc methods, these operate on the computed fields below.
    // lazer Previous(skip)/Next(skip) null semantics: array position 0 is not a difficulty
    // object, so anything reaching it (or out of bounds) is null. several evaluator loops rely
    // on null termination, do NOT clamp here.
    [[nodiscard]] inline const DifficultyHitObject *get_previous(i32 backwardsIdx) const {
        const i32 arrayIdx = index - backwardsIdx;  // == lazer (Index - (skip + 1)) + 1
        return (arrayIdx >= 1 ? &objects[arrayIdx] : nullptr);
    }
    [[nodiscard]] inline const DifficultyHitObject *get_next(i32 forwardIdx) const {
        const i32 arrayIdx = index + forwardIdx + 2;  // == lazer (Index + skip + 1) + 1
        return (arrayIdx < numObjects ? &objects[arrayIdx] : nullptr);
    }

    // per-object recorded difficulty (lazer Skill.ObjectDifficulties): for speed the rhythm
    // multiplier is part of the recorded value
    [[nodiscard]] inline f64 get_strain(Skills::Skill dtype) const {
        return strains[dtype] * (dtype == Skills::SPEED ? rhythm : 1.0);
    }

    std::array<f64, Skills::NUM_SKILLS> strains{};

    // https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Speed.cs
    // the speed EMA and the rhythm multiplier are stored separately: the EMA chains through
    // strains[SPEED], the recorded per-object value is their product (get_strain)
    f64 rhythm{0.};

    // reading/flashlight recorded per-object difficulties, both hidden variants tracked in one
    // pass so the hidden flag stays out of the strain state key (flashlight only filled when
    // the flashlight flag is set, zero otherwise)
    f64 readingStrainNoHidden{0.};
    f64 readingStrainHidden{0.};
    f64 flashlightStrainNoHidden{0.};
    f64 flashlightStrainHidden{0.};

    vec2 norm_start{};  // start position normalized on radius

    f64 angle{std::numeric_limits<f64>::quiet_NaN()};  // precalc

    // angle of the current-1 -> current vector, normalized so symmetrical vectors in any axis
    // compare equal (lazer NormalisedVectorAngle); NaN when lazer would have null
    f64 normalisedVectorAngle{std::numeric_limits<f64>::quiet_NaN()};  // precalc

    f64 jumpDistance{0.};         // precalc (start->start distance, lazer JumpDistance)
    f64 lazyJumpDistance{0.};     // precalc
    f64 minimumJumpDistance{0.};  // precalc
    f64 minimumJumpTime{0.};      // precalc
    f64 travelDistance{0.};       // precalc

    // time between the previous object's end and this object's start, min 25ms
    // (lazer LastObjectEndDeltaTime; falls back to the start->start delta for the first pair)
    f64 lastObjectEndDeltaTime{0.};  // precalc

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
    inline void resetFields(DifficultyHitObject *parent, const DifficultyHitObject *allObjects, i32 numObjs,
                            i32 arrayIndex, f32 radiusScalingFactor) {
        strains = {};
        rhythm = 0.;
        readingStrainNoHidden = 0.;
        readingStrainHidden = 0.;
        flashlightStrainNoHidden = 0.;
        flashlightStrainHidden = 0.;
        norm_start = parent->pos * radiusScalingFactor;
        angle = std::numeric_limits<f64>::quiet_NaN();
        normalisedVectorAngle = std::numeric_limits<f64>::quiet_NaN();
        jumpDistance = 0.;
        lazyJumpDistance = 0.;
        minimumJumpDistance = 0.;
        minimumJumpTime = 0.;
        travelDistance = 0.;
        lastObjectEndDeltaTime = 0.;
        deltaTime = 0.;
        strainTime = 0.;
        lazyEndPos = parent->pos;
        lazyTravelDist = 0.;
        lazyTravelTime = 0.;
        travelTime = 0.;
        objects = allObjects;
        numObjects = numObjs;
        index = arrayIndex - 1;
        lazyCalcFinished = false;
    }
};

DifficultyHitObject::DifficultyHitObject(TYPE type, vec2 pos, i32 time) : DifficultyHitObject(type, pos, time, time) {}

DifficultyHitObject::DifficultyHitObject(TYPE type, vec2 pos, i32 time, i32 endTime)
    : DifficultyHitObject(type, pos, time, endTime, 0.0f, SLIDERCURVETYPE{}, std::vector<vec2>(), 0.0f,
                          std::vector<SLIDER_SCORING_TIME>(), 0, true) {}

DifficultyHitObject::DifficultyHitObject(TYPE type, vec2 pos, i32 time, i32 endTime, f32 spanDuration,
                                         SLIDERCURVETYPE osuSliderCurveType, const std::vector<vec2> &controlPoints,
                                         f32 pixelLength, std::vector<SLIDER_SCORING_TIME> scoringTimes, i32 repeats,
                                         bool calculateSliderCurveInConstructor)
    : pos(pos),
      time(time),
      baseTime(time),
      endTime(endTime),
      baseEndTime(endTime),
      scoringTimes(std::move(scoringTimes)),
      curve(nullptr),
      spanDuration(spanDuration),
      pixelLength(pixelLength),
      repeats(repeats),
      stack(0),
      originalPos(pos),
      type(type),
      osuSliderCurveType(osuSliderCurveType),
      scheduledCurveAlloc(false) {
    // build slider curve, if this is a (valid) slider
    if(this->type == TYPE::SLIDER && controlPoints.size() > 1) {
        if(calculateSliderCurveInConstructor) {
            // old: too much kept memory allocations for over 14000 sliders in https://osu.ppy.sh/beatmapsets/592138#osu/1277649

            // NOTE: this is still used for beatmaps with less than 5000 sliders (because precomputing all curves is way faster, especially for star cache loader)
            // NOTE: the 5000 slider threshold was chosen by looking at the longest non-aspire marathon maps
            // NOTE: 15000 slider curves use ~1.6 GB of RAM, for 32-bit executables with 2GB cap limiting to 5000 sliders gives ~530 MB which should be survivable without crashing (even though the heap gets fragmented to fuck)

            // 6000 sliders @ The Weather Channel - 139 Degrees (Tiggz Mumbson) [Special Weather Statement].osu
            // 3674 sliders @ Various Artists - Alternator Compilation (Monstrata) [Marathon].osu
            // 4599 sliders @ Renard - Because Maybe! (Mismagius) [- Nogard Marathon -].osu
            // 4921 sliders @ Renard - Because Maybe! (Mismagius) [- Nogard Marathon v2 -].osu
            // 14960 sliders @ pishifat - H E L L O  T H E R E (Kondou-Shinichi) [Sliders in the 69th centries].osu
            // 5208 sliders @ MillhioreF - haitai but every hai adds another haitai in the background (Chewy-san) [Weriko Rank the dream (nerf) but loli].osu

            this->curve = std::make_unique<SliderCurve>(this->osuSliderCurveType, controlPoints, this->pixelLength,
                                                        STARS_SLIDER_CURVE_POINTS_SEPARATION);
        } else {
            // new: delay curve creation to when it's needed, and also immediately delete afterwards (at the cost of having to store a copy of the control points)
            this->scheduledCurveAlloc = true;
            this->scheduledCurveAllocControlPoints = controlPoints;
        }
    }
}

DifficultyHitObject::~DifficultyHitObject() = default;

DifficultyHitObject::DifficultyHitObject(DifficultyHitObject &&dobj) noexcept
    : pos(dobj.pos), originalPos(dobj.originalPos) {
    // move
    this->type = dobj.type;
    this->time = dobj.time;
    this->baseTime = dobj.baseTime;
    this->endTime = dobj.endTime;
    this->baseEndTime = dobj.baseEndTime;
    this->spanDuration = dobj.spanDuration;
    this->osuSliderCurveType = dobj.osuSliderCurveType;
    this->pixelLength = dobj.pixelLength;
    this->scoringTimes = std::move(dobj.scoringTimes);

    this->curve = std::move(dobj.curve);
    this->scheduledCurveAlloc = dobj.scheduledCurveAlloc;
    this->scheduledCurveAllocControlPoints = std::move(dobj.scheduledCurveAllocControlPoints);
    this->repeats = dobj.repeats;

    this->stack = dobj.stack;

    this->c = dobj.c;

    // reset source
    dobj.curve = nullptr;
    dobj.scheduledCurveAlloc = false;
}

DifficultyHitObject &DifficultyHitObject::operator=(DifficultyHitObject &&dobj) noexcept {
    // self-assignment check
    if(this == &dobj) return *this;

    // move everything
    this->type = dobj.type;
    this->pos = dobj.pos;
    this->time = dobj.time;
    this->baseTime = dobj.baseTime;
    this->endTime = dobj.endTime;
    this->baseEndTime = dobj.baseEndTime;
    this->spanDuration = dobj.spanDuration;
    this->osuSliderCurveType = dobj.osuSliderCurveType;
    this->pixelLength = dobj.pixelLength;
    this->repeats = dobj.repeats;
    this->stack = dobj.stack;
    this->originalPos = dobj.originalPos;
    this->scheduledCurveAlloc = dobj.scheduledCurveAlloc;

    this->scoringTimes = std::move(dobj.scoringTimes);
    this->scheduledCurveAllocControlPoints = std::move(dobj.scheduledCurveAllocControlPoints);
    this->curve = std::move(dobj.curve);

    this->c = std::move(dobj.c);

    return *this;
}

void DifficultyHitObject::updateStackPosition(f32 stackOffset) {
    pos = originalPos - vec2((f32)stack * stackOffset, (f32)stack * stackOffset);
}

vec2 DifficultyHitObject::curvePointAt(f32 t) const { return curve->pointAt(t) - (originalPos - pos); }

vec2 DifficultyHitObject::getOriginalRawPosAt(i32 pos) const {
    // NOTE: the delayed curve creation has been deliberately disabled here for stacking purposes for beatmaps with insane slider counts for performance reasons
    // NOTE: this means that these aspire maps will have incorrect stars due to incorrect slider stacking, but the delta is below 0.02 even for the most insane maps which currently exist
    // NOTE: if this were to ever get implemented properly, then a sliding window for curve allocation must be added to the stack algorithm itself (as doing it in here is O(n!) madness)
    // NOTE: to validate the delta, use Acid Rain [Aspire] - Karoo13 (6.76* with slider stacks -> 6.75* without slider stacks)

    if(type != TYPE::SLIDER || (curve == nullptr /* && !scheduledCurveAlloc*/))
        return originalPos;
    else {
        // new (correct)
        if(pos <= time)
            return curve->pointAt(0.0f);
        else if(pos >= endTime) {
            if(repeats % 2 == 0)
                return curve->pointAt(0.0f);
            else
                return curve->pointAt(1.0f);
        } else
            return curve->pointAt(getT(pos, false));
    }
}

f32 DifficultyHitObject::getT(i32 pos, bool raw) const {
    f32 t = (f32)((i32)pos - (i32)time) / spanDuration;
    if(raw)
        return t;
    else {
        f32 floorVal = (f32)std::floor(t);
        return ((i32)floorVal % 2 == 0) ? t - floorVal : floorVal + 1 - t;
    }
}

namespace {
// see https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Preprocessing/OsuDifficultyHitObject.cs

namespace DistanceCalc {
static void computeSliderCursorPosition(DifficultyHitObject &slider, f32 circleRadius) {
    if(slider.c->lazyCalcFinished || slider.curve == nullptr) return;

    // NOTE: lazer won't load sliders above a certain length, but mcosu will
    // this isn't entirely accurate to how lazer does it (as that skips loading the object entirely),
    // but this is a good middle ground for maps that aren't completely aspire and still have relatively normal star counts on lazer
    // see: DJ Noriken - Stargazer feat. YUC'e (PSYQUI Remix) (Hishiro Chizuru) [Starg-Azer isn't so great? Are you kidding me?]
    if(IGNORE_CLAMPED_SLIDERS) {
        if(slider.curve->getPixelLength() >= SLIDER_CURVE_MAX_LENGTH) return;
    }

    // NOTE: although this looks like a duplicate of the end tick time, this really does have a noticeable impact on some maps due to precision issues
    // see: Ocelot - KAEDE (Hollow Wings) [EX EX]
    const f64 tailLeniency = SLIDER_END_INSIDE_CHECK_OFFSET;
    const f64 totalDuration = (f64)slider.spanDuration * slider.repeats;
    f64 trackingEndTime = (f64)slider.time + std::max(totalDuration - tailLeniency, totalDuration / 2.0);

    // NOTE: lazer has logic to reorder the last slider tick if it happens after trackingEndTime here, which already happens in mcosu

    slider.c->lazyTravelTime = trackingEndTime - (f64)slider.time;

    f64 endTimeMin = slider.c->lazyTravelTime / slider.spanDuration;
    if(std::fmod(endTimeMin, 2.0) >= 1.0)
        endTimeMin = 1.0 - std::fmod(endTimeMin, 1.0);
    else
        endTimeMin = std::fmod(endTimeMin, 1.0);

    slider.c->lazyEndPos = slider.curvePointAt((f32)endTimeMin);

    vec2 cursor_pos = slider.pos;
    f64 scaling_factor = 50.0 / circleRadius;

    for(uSz i = 0; i < slider.scoringTimes.size(); i++) {
        vec2 diff;

        if(slider.scoringTimes[i].type == SLIDER_SCORING_TIME::TYPE::END) {
            // NOTE: In lazer, the position of the slider end is at the visual end, but the time is at the scoring end
            diff = slider.curvePointAt(slider.repeats % 2 ? 1.0 : 0.0) - cursor_pos;
        } else {
            f64 progress =
                (std::clamp<f32>(slider.scoringTimes[i].time - (f32)slider.time, 0.0f, (f32)slider.getDuration())) /
                slider.spanDuration;
            if(std::fmod(progress, 2.0) >= 1.0)
                progress = 1.0 - std::fmod(progress, 1.0);
            else
                progress = std::fmod(progress, 1.0);

            diff = slider.curvePointAt((f32)progress) - cursor_pos;
        }

        f64 diff_len = scaling_factor * vec::length(diff);

        f64 req_diff = 90.0;

        if(i == slider.scoringTimes.size() - 1) {
            // Slider end
            vec2 lazy_diff = slider.c->lazyEndPos - cursor_pos;
            if(vec::length(lazy_diff) < vec::length(diff)) diff = lazy_diff;
            diff_len = scaling_factor * vec::length(diff);
        } else if(slider.scoringTimes[i].type == SLIDER_SCORING_TIME::TYPE::REPEAT) {
            // Slider repeat
            req_diff = 50.0;
        }

        if(diff_len > req_diff) {
            cursor_pos += (diff * (f32)((diff_len - req_diff) / diff_len));
            diff_len *= (diff_len - req_diff) / diff_len;
            slider.c->lazyTravelDist += diff_len;
        }

        if(i == slider.scoringTimes.size() - 1) slider.c->lazyEndPos = cursor_pos;
    }

    slider.c->lazyCalcFinished = true;
}

static vec2 getEndCursorPosition(DifficultyHitObject &hitObject, f32 circleRadius) {
    if(hitObject.type == DifficultyHitObject::TYPE::SLIDER) {
        computeSliderCursorPosition(hitObject, circleRadius);
        return hitObject.c->lazyEndPos;  // (lazyEndPos is reset to pos before every full computation)
    }

    return hitObject.pos;
}

// lazer NestedHitObjects[^2] (second-to-last nested object, head included); our
// scoringTimes exclude the head, so this is scoringTimes[n-2] or the head itself
static vec2 getSecondToLastNestedPosition(const DifficultyHitObject &slider) {
    if(slider.scoringTimes.size() < 2 || slider.curve == nullptr) return slider.pos;

    const auto &scoringTime = slider.scoringTimes[slider.scoringTimes.size() - 2];
    f64 progress =
        (std::clamp<f32>(scoringTime.time - (f32)slider.time, 0.0f, (f32)slider.getDuration())) / slider.spanDuration;
    if(std::fmod(progress, 2.0) >= 1.0)
        progress = 1.0 - std::fmod(progress, 1.0);
    else
        progress = std::fmod(progress, 1.0);

    return slider.curvePointAt((f32)progress);
}

static f64 calcAngleBetween(vec2 currentPosition, vec2 lastPosition, vec2 lastLastPosition) {
    const vec2 v1 = lastLastPosition - lastPosition;
    const vec2 v2 = currentPosition - lastPosition;

    const f64 dot = vec::dot(v1, v2);
    const f64 det = (v1.x * v2.y) - (v1.y * v2.x);

    return std::fabs(std::atan2(det, dot));
}
}  // namespace DistanceCalc
}  // namespace

f64 calculateStarDiffForHitObjects(StarCalcParams &params) {
    // NOTE: osu always returns 0 stars for beatmaps with only 1 object, except if that object is a slider
    if(params.beatmapData.sortedHitObjects.size() < 2) {
        if(params.beatmapData.sortedHitObjects.size() < 1) return 0.0;
        if(params.beatmapData.sortedHitObjects[0].type != DifficultyHitObject::TYPE::SLIDER) return 0.0;
    }

    // global independent variables/constants
    // NOTE: clamped CS because McOsu allows CS > ~12.1429 (at which point the diameter becomes negative)
    f32 circleRadiusInOsuPixels =
        64.0f * GameRules::getRawHitCircleScale(std::clamp<f32>(params.beatmapData.CS, 0.0f, 12.142f));
    const f64 hitWindow300 = 2.0 * adjustHitWindow(GameRules::odTo300HitWindowMS(params.beatmapData.OD)) /
                             params.beatmapData.speedMultiplier;

    // ****************************************************************************************************************************************** //

    // see setDistances() @ https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Preprocessing/OsuDifficultyHitObject.cs

    static constexpr f32 normalized_radius = 50.0f;  // normalization factor
    static constexpr f32 maximum_slider_radius = normalized_radius * 2.4f;
    static constexpr f32 assumed_slider_radius = normalized_radius * 1.8f;

    // multiplier to normalize positions so that we can calc as if everything was the same circlesize.

    f32 radius_scaling_factor = normalized_radius / circleRadiusInOsuPixels;

    // handle high CS bonus
    f64 smallCircleBonus = std::max(1.0, 1.0 + (30.0 - circleRadiusInOsuPixels) / 70.0);

    // ****************************************************************************************************************************************** //

    // NOTE: the computed fields are always (re)computed for the whole vector, upToObjectIndex only
    // cuts off the final difficulty calculation below. this way repeat calls with an increasing
    // upToObjectIndex (live pp) reuse the computed fields via params.strainState.
    // (for some rare individual upToObjectIndex this loses some extremely minor accuracy (~0.001 stars
    // territory) vs computing the cut set exactly, because the last object of the cut set sees its real
    // next object instead of none, but the performance gain is so insane I don't care)
    DifficultyHitObject *diffObjects = params.beatmapData.sortedHitObjects.data();
    const uSz numObjects = params.beatmapData.sortedHitObjects.size();
    const uSz numDiffObjects = (params.upToObjectIndex < 0) ? numObjects : params.upToObjectIndex + 1;

    const bool reuseComputedFields =
        params.strainState &&
        params.strainState->matches(params.beatmapData.CS, params.beatmapData.OD, params.beatmapData.AR,
                                    params.beatmapData.speedMultiplier, params.beatmapData.relax,
                                    params.beatmapData.autopilot, params.beatmapData.touchDevice,
                                    params.beatmapData.flashlight);

    if(!reuseComputedFields) {
        // full (re)computation, start from a clean slate
        for(uSz i = 0; i < numObjects; i++) {
            diffObjects[i].c->resetFields(&diffObjects[i], diffObjects, (i32)numObjects, (i32)i, radius_scaling_factor);
        }
    }

    // calculate angles and travel/jump distances (before calculating strains)
    if(!reuseComputedFields) {
        const f32 starsSliderCurvePointsSeparation = STARS_SLIDER_CURVE_POINTS_SEPARATION;
        for(uSz i = 0; i < numObjects; i++) {
            if(!(i % 64) && params.cancelCheck.stop_requested()) return 0.0;

            // see setDistances() @ https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Preprocessing/OsuDifficultyHitObject.cs

            if(i > 0) {
                // calculate travel/jump distances
                DifficultyHitObject &cur = diffObjects[i];
                DifficultyHitObject &prev1 = diffObjects[i - 1];

                // (the first pair has no previous difficulty object in lazer, so it falls back to
                // the min-clamped start->start delta there)
                cur.c->lastObjectEndDeltaTime = (i == 1) ? std::max((f64)(cur.time - prev1.time), 25.0)
                                                         : std::max((f64)(cur.time - prev1.endTime), 25.0);

                // MCKAY:
                {
                    // delay curve creation to when it's needed (1)
                    if(prev1.scheduledCurveAlloc && prev1.curve == nullptr) {
                        prev1.curve = std::make_unique<SliderCurve>(
                            prev1.osuSliderCurveType, prev1.scheduledCurveAllocControlPoints, prev1.pixelLength,
                            starsSliderCurvePointsSeparation);
                    }
                }

                if(cur.type == DifficultyHitObject::TYPE::SLIDER) {
                    DistanceCalc::computeSliderCursorPosition(cur, circleRadiusInOsuPixels);
                    // repeat slider bonus until a better per nested object strain system can be achieved
                    cur.c->travelDistance =
                        cur.c->lazyTravelDist * std::max(1.0, std::pow((f64)(cur.repeats - 1), 0.3));
                    cur.c->travelTime = std::max(cur.c->lazyTravelTime, 25.0);
                }

                f64 cur_strain_time = (f64)std::max(cur.time - prev1.time, 25);  // strainTime isn't initialized here

                // (lazer sets this for every object, including spinners)
                cur.c->minimumJumpTime = cur_strain_time;

                // don't need to jump to reach spinners
                if(cur.type == DifficultyHitObject::TYPE::SPINNER || prev1.type == DifficultyHitObject::TYPE::SPINNER)
                    continue;

                // lazer's first difficulty object has no previous difficulty object, so its jump
                // starts from the previous object's start position (never a slider lazy end)
                const vec2 lastCursorPosition =
                    (i > 1) ? DistanceCalc::getEndCursorPosition(prev1, circleRadiusInOsuPixels) : prev1.pos;

                cur.c->jumpDistance = vec::length(prev1.pos - cur.pos) * radius_scaling_factor;
                cur.c->lazyJumpDistance = vec::length(cur.c->norm_start - lastCursorPosition * radius_scaling_factor);
                cur.c->minimumJumpDistance = cur.c->lazyJumpDistance;

                if(i > 1 && prev1.type == DifficultyHitObject::TYPE::SLIDER) {
                    f64 last_travel = std::max(prev1.c->lazyTravelTime, 25.0);
                    cur.c->minimumJumpTime = std::max(cur_strain_time - last_travel, 25.0);

                    // NOTE: "curve shouldn't be null here, but Yin [test7] causes that to happen"
                    // NOTE: the curve can be null if controlPoints.size() < 1 because the DifficultyHitObject() constructor will then not set scheduledCurveAlloc to true (which is perfectly fine and correct)
                    f32 tail_jump_dist =
                        vec::distance(prev1.curve ? prev1.curvePointAt(prev1.repeats % 2 ? 1.0 : 0.0) : prev1.pos,
                                      cur.pos) *
                        radius_scaling_factor;
                    cur.c->minimumJumpDistance = std::max(
                        0.0f,
                        std::min((f32)cur.c->minimumJumpDistance - (maximum_slider_radius - assumed_slider_radius),
                                 tail_jump_dist - maximum_slider_radius));
                }

                // calculate angles: lazer needs a lastlast difficulty object, which only exists
                // from the fourth hitobject on (i > 2)
                if(i > 2) {
                    DifficultyHitObject &prev2 = diffObjects[i - 2];
                    if(prev2.type == DifficultyHitObject::TYPE::SPINNER) continue;

                    const vec2 lastLastCursorPosition =
                        DistanceCalc::getEndCursorPosition(prev2, circleRadiusInOsuPixels);

                    // MCKAY:
                    {
                        // and also immediately delete afterwards (2)
                        // NOTE: this trivial sliding window implementation will keep the last 2 curves alive at the end, but they get auto deleted later anyway so w/e
                        DifficultyHitObject &prev3 = diffObjects[i - 3];
                        if(prev3.scheduledCurveAlloc) prev3.curve.reset();
                    }

                    const bool prevSliderWithTravel =
                        (prev1.type == DifficultyHitObject::TYPE::SLIDER && prev1.c->travelDistance > 0.0);

                    // plain angle: the vertex moves to the previous slider's head when it has travel
                    const vec2 angleLastCursor = prevSliderWithTravel ? prev1.pos : lastCursorPosition;
                    // slider angle: the vertex stays at the lazy end, but the lastlast position
                    // moves to the previous slider's second-to-last nested object
                    const vec2 sliderAngleLastLast = prevSliderWithTravel
                                                         ? DistanceCalc::getSecondToLastNestedPosition(prev1)
                                                         : lastLastCursorPosition;

                    const f64 angle = DistanceCalc::calcAngleBetween(cur.pos, angleLastCursor, lastLastCursorPosition);
                    const f64 sliderAngle =
                        DistanceCalc::calcAngleBetween(cur.pos, lastCursorPosition, sliderAngleLastLast);

                    cur.c->angle = std::min(angle, sliderAngle);

                    const vec2 v = cur.pos - angleLastCursor;
                    cur.c->normalisedVectorAngle = std::atan2(std::fabs(v.y), std::fabs(v.x));
                }
            }
        }
    }

    // calculate strains/skills
    if(!reuseComputedFields) {
        // per-map constants for the evaluators; the per-note OD scalings use the per-object
        // OverallDifficulty, which is the same for every object here (uniform hit windows)
        const f64 perObjectOD =
            adjustOverallDifficultyByClockRate(params.beatmapData.OD, params.beatmapData.speedMultiplier);
        const f64 preemptRaw = (f64)GameRules::arToMilliseconds(params.beatmapData.AR);

        const StrainEvalContext ctx{
            .hitWindow300 = hitWindow300,
            .circleRadius = (f64)circleRadiusInOsuPixels,
            .smallCircleBonus = smallCircleBonus,
            .smallCircleBonusSqrt = std::sqrt(smallCircleBonus),
            .smallCircleBonusPow15 = std::pow(smallCircleBonus, 1.5),
            .odScalingAimFl = 0.985 + powi(std::max(0.0, perObjectOD), 2) / 4000.0,
            .odScalingReading = 0.825 + std::pow(std::max(0.0, perObjectOD), 2.2) / 1125.0,
            .preemptRaw = preemptRaw,
            .preemptAdjusted = preemptRaw / params.beatmapData.speedMultiplier,
            .fadeInRaw = 400.0 * std::min(1.0, preemptRaw / 450.0),
            .relax = params.beatmapData.relax,
            .autopilot = params.beatmapData.autopilot,
            .touchDevice = params.beatmapData.touchDevice,
            .flashlight = params.beatmapData.flashlight,
        };

        for(uSz i = 1; i < numObjects; i++) {  // NOTE: start at 1
            if(!(i % 64) && params.cancelCheck.stop_requested()) return 0.0;
            calculate_strains(diffObjects[i], diffObjects[i - 1], ctx);
        }

        // everything was computed without cancellation, remember what for
        if(params.strainState) {
            *params.strainState = {.CS = params.beatmapData.CS,
                                   .OD = params.beatmapData.OD,
                                   .AR = params.beatmapData.AR,
                                   .speedMultiplier = params.beatmapData.speedMultiplier,
                                   .relax = params.beatmapData.relax,
                                   .autopilot = params.beatmapData.autopilot,
                                   .touchDevice = params.beatmapData.touchDevice,
                                   .flashlight = params.beatmapData.flashlight,
                                   .valid = true};
        }
    }

    // calculate final difficulty values per skill
    const f64 aimDifficultyValue = calculate_aim_difficulty(Skills::AIM_SLIDERS, diffObjects, numDiffObjects);
    const f64 aimNoSlidersDifficultyValue =
        calculate_aim_difficulty(Skills::AIM_NO_SLIDERS, diffObjects, numDiffObjects);

    f64 speedObjectWeightSum = 0.0;
    const f64 speedDifficultyValue = calculate_speed_difficulty(diffObjects, numDiffObjects, speedObjectWeightSum);

    // per-object weighted counts
    params.outAttributes.AimDifficultStrainCount =
        count_top_weighted_strains_geometric(diffObjects, numDiffObjects, Skills::AIM_SLIDERS, aimDifficultyValue);
    params.outAttributes.AimDifficultSliderCount =
        calculate_aim_difficult_slider_count(diffObjects, numDiffObjects, Skills::AIM_SLIDERS);

    // the aim top-weighted slider factor is intentionally computed on the no-sliders skill
    const f64 aimNSTopWeightedSliderCount = count_top_weighted_sliders_geometric(
        diffObjects, numDiffObjects, Skills::AIM_NO_SLIDERS, aimNoSlidersDifficultyValue);
    const f64 aimNSDifficultStrainCount = count_top_weighted_strains_geometric(
        diffObjects, numDiffObjects, Skills::AIM_NO_SLIDERS, aimNoSlidersDifficultyValue);
    params.outAttributes.AimTopWeightedSliderFactor =
        aimNSTopWeightedSliderCount / std::max(1.0, aimNSDifficultStrainCount - aimNSTopWeightedSliderCount);

    params.outAttributes.SpeedDifficultStrainCount =
        count_top_weighted_speed_strains(diffObjects, numDiffObjects, speedDifficultyValue, speedObjectWeightSum);
    const f64 speedTopWeightedSliderCount =
        count_top_weighted_speed_sliders(diffObjects, numDiffObjects, speedDifficultyValue, speedObjectWeightSum);
    params.outAttributes.SpeedTopWeightedSliderFactor =
        speedTopWeightedSliderCount /
        std::max(1.0, params.outAttributes.SpeedDifficultStrainCount - speedTopWeightedSliderCount);

    params.outAttributes.SpeedNoteCount = calculate_speed_relevant_object_count(diffObjects, numDiffObjects);

    // reading + flashlight, both hidden variants (the strain series carry both, so the raw
    // values can drive hidden recomputes without another strain pass)
    f64 readingDifficultNoteCountNoHidden = 0.0;
    f64 readingDifficultNoteCountHidden = 0.0;
    const f64 readingNoHiddenDifficultyValue =
        calculate_reading_difficulty(diffObjects, numDiffObjects, false, readingDifficultNoteCountNoHidden);
    const f64 readingHiddenDifficultyValue =
        calculate_reading_difficulty(diffObjects, numDiffObjects, true, readingDifficultNoteCountHidden);

    const f64 readingDifficultyValue =
        params.beatmapData.hidden ? readingHiddenDifficultyValue : readingNoHiddenDifficultyValue;
    params.outAttributes.ReadingDifficultNoteCount =
        params.beatmapData.hidden ? readingDifficultNoteCountHidden : readingDifficultNoteCountNoHidden;

    f64 flashlightNoHiddenDifficultyValue = 0.0;
    f64 flashlightHiddenDifficultyValue = 0.0;
    if(params.beatmapData.flashlight) {
        flashlightNoHiddenDifficultyValue = calculate_flashlight_difficulty(diffObjects, numDiffObjects, false);
        flashlightHiddenDifficultyValue = calculate_flashlight_difficulty(diffObjects, numDiffObjects, true);
    }
    const f64 flashlightDifficultyValue =
        params.beatmapData.hidden ? flashlightHiddenDifficultyValue : flashlightNoHiddenDifficultyValue;

    // strain graph export (display only): fixed 400ms bucketed per-object strain peaks, both
    // series share the same section grid so they always have equal sizes
    if(params.outAimStrains != nullptr) {
        *params.outAimStrains =
            build_fixed_strain_sections(diffObjects, numDiffObjects, decay_base[Skills::AIM_SLIDERS],
                                        [](const DifficultyHitObject &o) { return o.c->strains[Skills::AIM_SLIDERS]; });
    }
    if(params.outSpeedStrains != nullptr) {
        *params.outSpeedStrains =
            build_fixed_strain_sections(diffObjects, numDiffObjects, decay_base[Skills::SPEED],
                                        [](const DifficultyHitObject &o) { return o.c->get_strain(Skills::SPEED); });
    }

    if(params.outRawDifficulty) {
        *params.outRawDifficulty = {aimNoSlidersDifficultyValue,    aimDifficultyValue,
                                    speedDifficultyValue,           readingNoHiddenDifficultyValue,
                                    readingHiddenDifficultyValue,   flashlightNoHiddenDifficultyValue,
                                    flashlightHiddenDifficultyValue};
    }

    // rating transforms (all mod adjustments happen per-note inside the skills now)
    const f64 aimRating = calculateAimDifficultyRating(aimDifficultyValue);
    const f64 aimNoSlidersRating = calculateAimDifficultyRating(aimNoSlidersDifficultyValue);
    const f64 speedRating = calculateDifficultyRating(speedDifficultyValue);
    const f64 readingRating = calculateDifficultyRating(readingDifficultyValue);
    const f64 flashlightRating = calculateDifficultyRating(flashlightDifficultyValue);

    params.outAttributes.SliderFactor = aimDifficultyValue > 0.0 ? aimNoSlidersRating / aimRating : 1.0;

    // Scorev1
    calculateScoreV1Attributes(params.outAttributes, params.beatmapData, params.upToObjectIndex);

    params.outAttributes.AimDifficulty = aimRating;
    params.outAttributes.SpeedDifficulty = speedRating;
    params.outAttributes.ReadingDifficulty = readingRating;
    params.outAttributes.FlashlightDifficulty = flashlightRating;

    return calculateStarRatingFromRatings(aimRating, speedRating, readingRating, flashlightRating);
}

f64 recomputeStarRating(const RawDifficultyValues &raw, const BeatmapDiffcalcData &beatmapData) {
    // all mod adjustments happen per-note inside the skills now, and both hidden variants of
    // the hidden-dependent skills are carried in the raw values, so this is pure math
    const f64 reading = beatmapData.hidden ? raw.readingHidden : raw.readingNoHidden;
    const f64 flashlight = beatmapData.hidden ? raw.flashlightHidden : raw.flashlightNoHidden;

    return calculateStarRatingFromRatings(calculateAimDifficultyRating(raw.aim), calculateDifficultyRating(raw.speed),
                                          calculateDifficultyRating(reading), calculateDifficultyRating(flashlight));
}

f64 calculatePPv2(PPv2CalcParams &cpar) {
    // NOTE: depends on active mods + OD + AR

    if(cpar.c300 < 0) cpar.c300 = cpar.numHitObjects - cpar.c100 - cpar.c50 - cpar.misses;

    if(cpar.combo < 0) cpar.combo = cpar.maxPossibleCombo;

    if(cpar.combo < 1) return 0.0;

    const i32 totalHits = cpar.c300 + cpar.c100 + cpar.c50 + cpar.misses;

    ScoreData score{
        .modFlags = cpar.modFlags,
        .accuracy = std::clamp(
            (totalHits > 0 ? (f64)(cpar.c300 * 300 + cpar.c100 * 100 + cpar.c50 * 50) / (f64)(totalHits * 300) : 0.0),
            0.0, 1.0),
        .countGreat = cpar.c300,
        .countOk = cpar.c100,
        .countMeh = cpar.c50,
        .countMiss = cpar.misses,
        .totalHits = totalHits,
        .totalSuccessfulHits = cpar.c300 + cpar.c100 + cpar.c50,
        .beatmapMaxCombo = cpar.maxPossibleCombo,
        .scoreMaxCombo = std::clamp(cpar.combo, 0, cpar.maxPossibleCombo),
        .amountHitObjectsWithAccuracy =
            (flags::has<ModFlags::ScoreV2>(cpar.modFlags) ? cpar.numCircles + cpar.numSliders : cpar.numCircles),
        .legacyTotalScore = cpar.legacyTotalScore};

    // We apply original (not adjusted) OD here
    cpar.attributes.OverallDifficulty = cpar.od;
    cpar.attributes.SliderCount = cpar.numSliders;

    // apply "timescale" aka speed multiplier to ar/od
    // (the original incoming ar/od values are guaranteed to not yet have any speed multiplier applied to them, but they do have non-time-related mods already applied, like HR or any custom overrides)
    // (yes, this does work correctly when the override slider "locking" feature is used. in this case, the stored ar/od is already compensated such that it will have the locked value AFTER applying the speed multiplier here)
    // (all UI elements which display ar/od from stored scores, like the ranking screen or score buttons, also do this calculation before displaying the values to the user. of course the mod selection screen does too.)
    const f64 adjAR = GameRules::arWithSpeed((f32)cpar.ar, (f32)cpar.timescale);
    const f64 adjOD = adjustOverallDifficultyByClockRate(cpar.od, cpar.timescale);

    const i32 totalImperfectHits = cpar.c100 + cpar.c50 + cpar.misses;

    // calculateComboBasedEstimatedMissCount @ https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/OsuPerformanceCalculator.cs
    // required because slider breaks aren't exposed to pp calculation (classic slider accuracy path)
    f64 comboBasedMissCount = (f64)cpar.misses;
    if(cpar.numSliders > 0) {
        // if sliders in the map are hard it's likely for the player to drop sliderends, if they
        // are easy it's more likely to be a sliderbreak
        const f64 likelyMissedSliderendPortion =
            0.04 + 0.06 * powi(std::min(cpar.attributes.AimTopWeightedSliderFactor, 1.0), 2);

        // full combo is maximum combo minus dropped slider tails since they don't contribute to
        // combo but also don't break it; in classic scores the amount is estimated
        const f64 fullComboThreshold =
            cpar.maxPossibleCombo -
            std::min(4.0 + likelyMissedSliderendPortion * cpar.numSliders, (f64)cpar.numSliders);

        if((f64)score.scoreMaxCombo < fullComboThreshold)
            comboBasedMissCount = fullComboThreshold / std::max(1.0, (f64)score.scoreMaxCombo);

        // in classic scores there can't be more misses than a sum of all non-perfect judgements
        comboBasedMissCount = std::min(comboBasedMissCount, (f64)totalImperfectHits);

        // every slider has *at least* 2 combo attributed in classic mechanics, so a score that
        // loses 1 combo can't possibly have been a slider break (must have been a slider end)
        const i32 maxPossibleSliderBreaks =
            std::min(cpar.numSliders, (cpar.maxPossibleCombo - score.scoreMaxCombo) / 2);

        const f64 sliderBreaks = comboBasedMissCount - cpar.misses;
        if(sliderBreaks > (f64)maxPossibleSliderBreaks) comboBasedMissCount = cpar.misses + maxPossibleSliderBreaks;
    }

    f64 effectiveMissCount = comboBasedMissCount;

    // scorev2 never stores a meaningful scorev1 total, so the score-based misscount only
    // applies to classic scores (#35962)
    if(score.legacyTotalScore > 0 && !flags::has<ModFlags::ScoreV2>(cpar.modFlags)) {
        effectiveMissCount = calculateScoreBasedMisscount(cpar.attributes, score, cpar.timescale, cpar.isMcOsuImported);
    }

    effectiveMissCount = std::max((f64)cpar.misses, effectiveMissCount);
    effectiveMissCount = std::min((f64)totalHits, effectiveMissCount);
    effectiveMissCount = std::max(0.0, effectiveMissCount);

    // slider break estimates are computed once upfront (reading reuses aim's)
    f64 aimEstimatedSliderBreaks = 0.0;
    f64 speedEstimatedSliderBreaks = 0.0;
    if(effectiveMissCount > 0.0) {
        aimEstimatedSliderBreaks =
            calculateEstimatedSliderBreaks(score, cpar.attributes.AimTopWeightedSliderFactor, effectiveMissCount);
        speedEstimatedSliderBreaks =
            calculateEstimatedSliderBreaks(score, cpar.attributes.SpeedTopWeightedSliderFactor, effectiveMissCount);
    }

    // custom multipliers for nofail and spunout
    f64 multiplier = performance_base_multiplier;  // keep final pp normalized across changes
    {
        if(flags::has<ModFlags::NoFail>(cpar.modFlags))
            multiplier *= std::max(
                0.9, 1.0 - 0.02 * effectiveMissCount);  // see https://github.com/ppy/osu-performance/pull/127/files

        if((flags::has<ModFlags::SpunOut>(cpar.modFlags)) && score.totalHits > 0)
            multiplier *= 1.0 - std::pow((f64)cpar.numSpinners / (f64)score.totalHits,
                                         0.85);  // see https://github.com/ppy/osu-performance/pull/110/

        if(flags::has<ModFlags::Relax>(cpar.modFlags)) {
            f64 okMultiplier = 0.75 * std::max(0.0, adjOD > 0.0 ? 1 - adjOD / 13.33 : 1.0);             // 100
            f64 mehMultiplier = std::max(0.0, adjOD > 0.0 ? 1.0 - std::pow(adjOD / 13.33, 5.0) : 1.0);  // 50
            effectiveMissCount = std::min(effectiveMissCount + cpar.c100 * okMultiplier + cpar.c50 * mehMultiplier,
                                          (f64)score.totalHits);
        }
    }

    const f64 speedDeviation = calculateSpeedDeviation(score, cpar.attributes, cpar.timescale);

    // Apply adjusted AR and OD when deviation is calculated
    cpar.attributes.ApproachRate = adjAR;
    cpar.attributes.OverallDifficulty = adjOD;

    const f64 aimValue = computeAimValue(score, cpar.attributes, effectiveMissCount, aimEstimatedSliderBreaks);
    const f64 speedValue =
        computeSpeedValue(score, cpar.attributes, effectiveMissCount, speedEstimatedSliderBreaks, speedDeviation);
    const f64 accuracyValue = computeAccuracyValue(score, cpar.attributes);

    const f64 readingValue = computeReadingValue(score, cpar.attributes, effectiveMissCount, aimEstimatedSliderBreaks);
    const f64 flashlightValue = computeFlashlightValue(score, cpar.attributes, effectiveMissCount);
    const f64 cognitionValue = sumCognitionDifficulty(readingValue, flashlightValue);

    const f64 totalValue = norm(1.1, {aimValue, speedValue, accuracyValue, cognitionValue}) * multiplier;

    return totalValue;
}

f64 getScoreV1ScoreMultiplier(ModFlags flags, f64 speedOverride, bool mcosu) {
    // this has to match how the score's scorev1 was actually calculated
    // neomod uses a custom curve for custom speeds, mcosu uses a flat 1.12/0.30 for "DT" (> 1.0) or "HT" (<1.0) speeds
    f64 multiplier = 1.0;
    if(!mcosu) {
        const bool ez = flags::has<ModFlags::Easy>(flags);
        const bool nf = flags::has<ModFlags::NoFail>(flags);
        const bool sv2 = flags::has<ModFlags::ScoreV2>(flags);
        const bool hr = flags::has<ModFlags::HardRock>(flags);
        const bool fl = flags::has<ModFlags::Flashlight>(flags);
        const bool hd = flags::has<ModFlags::Hidden>(flags);
        const bool so = flags::has<ModFlags::SpunOut>(flags);
        const bool rx = flags::has<ModFlags::Relax>(flags);
        const bool ap = flags::has<ModFlags::Autopilot>(flags);

        // Dumb formula, but the values for HT/DT were dumb to begin with
        if(speedOverride > 1.) {
            multiplier *= (0.24 * speedOverride) + 0.76;
        } else if(speedOverride < 1.) {
            multiplier *= 0.008 * std::exp(4.81588 * speedOverride);
        }

        if(ez || (nf && !sv2)) multiplier *= 0.5;
        if(hr) multiplier *= sv2 ? 1.1f : 1.06;
        if(fl) multiplier *= 1.12;
        if(hd) multiplier *= 1.06;
        if(so) multiplier *= 0.90;
        if(rx || ap) multiplier *= 0.;
    } else {
        const bool sv2 = flags::has<ModFlags::ScoreV2>(flags);
        const bool dt = speedOverride > 1.;
        const bool ht = speedOverride < 1.;

        if(flags::has<ModFlags::NoFail>(flags)) multiplier *= sv2 ? 1.0f : 0.50f;
        if(flags::has<ModFlags::Easy>(flags)) multiplier *= 0.50f;
        if(ht) multiplier *= 0.30f;
        if(flags::has<ModFlags::HardRock>(flags)) multiplier *= sv2 ? 1.1f : 1.06f;
        if(dt) multiplier *= sv2 ? 1.2f : 1.12f;
        if(flags::has<ModFlags::Hidden>(flags)) multiplier *= 1.06f;
        if(flags::has<ModFlags::SpunOut>(flags)) multiplier *= 0.90f;
    }

    return multiplier;
}

std::string PPv2CalcParamsToString(const PPv2CalcParams &pars) {
    const auto &attrs = pars.attributes;
    return FORMAT_STRING_(
        R"(pars.attrs.AimDifficulty: {}
attrs.AimDifficultSliderCount: {}
attrs.SpeedDifficulty: {}
attrs.SpeedNoteCount: {}
attrs.ReadingDifficulty: {}
attrs.ReadingDifficultNoteCount: {}
attrs.FlashlightDifficulty: {}
attrs.SliderFactor: {}
attrs.AimTopWeightedSliderFactor: {}
attrs.SpeedTopWeightedSliderFactor: {}
attrs.AimDifficultStrainCount: {}
attrs.SpeedDifficultStrainCount: {}
attrs.NestedScorePerObject: {}
attrs.LegacyScoreBaseMultiplier: {}
attrs.SliderCount: {}
attrs.MaximumLegacyComboScore: {}
attrs.ApproachRate: {}
attrs.OverallDifficulty: {}
modFlags: {:016x}
timescale: {}
ar: {}
od: {}
numHitObjects: {}
numCircles: {}
numSliders: {}
numSpinners: {}
maxPossibleCombo: {}
combo: {}
misses: {}
c300: {}
c100: {}
c50: {}
legacyTotalScore: {})",
        attrs.AimDifficulty, attrs.AimDifficultSliderCount, attrs.SpeedDifficulty, attrs.SpeedNoteCount,
        attrs.ReadingDifficulty, attrs.ReadingDifficultNoteCount, attrs.FlashlightDifficulty, attrs.SliderFactor,
        attrs.AimTopWeightedSliderFactor, attrs.SpeedTopWeightedSliderFactor, attrs.AimDifficultStrainCount,
        attrs.SpeedDifficultStrainCount, attrs.NestedScorePerObject, attrs.LegacyScoreBaseMultiplier, attrs.SliderCount,
        attrs.MaximumLegacyComboScore, attrs.ApproachRate, attrs.OverallDifficulty, (u64)pars.modFlags, pars.timescale,
        pars.ar, pars.od, pars.numHitObjects, pars.numCircles, pars.numSliders, pars.numSpinners, pars.maxPossibleCombo,
        pars.combo, pars.misses, pars.c300, pars.c100, pars.c50, pars.legacyTotalScore);
}

// Implementation details below
namespace {

// one skill pass per object, in list order (lazer DifficultyCalculator processes every skill
// per object before moving on; the recorded per-object values here are lazer's
// Skill.ObjectDifficulties, aggregated later by calculate_*_difficulty)
static void calculate_strains(DifficultyHitObject &cur, const DifficultyHitObject &prev, const StrainEvalContext &ctx) {
    const i32 time_elapsed = cur.time - prev.time;

    // update our delta time
    cur.c->deltaTime = (f64)time_elapsed;
    cur.c->strainTime = (f64)std::max(time_elapsed, 25);  // == lazer AdjustedDeltaTime

    // aim, both variants (lazer Aim.StrainValueAt): normalized EMA on AdjustedDeltaTime.
    // with autopilot lazer returns 0 before ever touching the running strain
    if(ctx.autopilot) {
        cur.c->strains[Skills::AIM_SLIDERS] = 0.0;
        cur.c->strains[Skills::AIM_NO_SLIDERS] = 0.0;
    } else {
        // agility doesn't depend on the slider variant, evaluate it once for both
        const f64 agilityDifficultyRaw = evaluate_agility_of(cur, ctx);

        for(const Skills::Skill dtype : {Skills::AIM_SLIDERS, Skills::AIM_NO_SLIDERS}) {
            const f64 decay = strainDecay(dtype, cur.c->strainTime);
            cur.c->strains[dtype] =
                prev.c->strains[dtype] * decay +
                evaluate_aim_difficulty_of(cur, dtype == Skills::AIM_SLIDERS, agilityDifficultyRaw, ctx) *
                    (1.0 - decay);
        }
    }

    // speed (lazer Speed.ObjectDifficultyOf): with relax lazer returns 0 before ever touching
    // the running strain. the recorded per-object value is strain * rhythm (get_strain)
    if(ctx.relax) {
        cur.c->strains[Skills::SPEED] = 0.0;
        cur.c->rhythm = 0.0;
    } else {
        constexpr f64 skill_multiplier = 1.16;

        f64 speedDifficulty = evaluate_speed_difficulty_of(cur, ctx);
        if(ctx.autopilot) speedDifficulty *= 0.5;

        const f64 decay = strainDecay(Skills::SPEED, cur.c->strainTime);
        cur.c->strains[Skills::SPEED] =
            prev.c->strains[Skills::SPEED] * decay + speedDifficulty * (1.0 - decay) * skill_multiplier;

        cur.c->rhythm = evaluate_rhythm_of(cur, ctx);
    }

    // reading (lazer Reading.ObjectDifficultyOf): normalized EMA on the RAW delta time (decay
    // can be 1.0 for simultaneous objects, lazer allows this), both hidden variants
    {
        constexpr f64 skill_multiplier = 2.5;

        const auto readingDifficulty = evaluate_reading_difficulty_of(cur, ctx);

        // per-note mod adjustments + OD scaling (lazer Reading.calculateAdjustedDifficulty)
        const auto adjust = [&ctx](f64 difficulty) {
            if(ctx.touchDevice) difficulty = std::pow(difficulty, 0.89);
            if(ctx.relax) difficulty *= 0.4;
            if(ctx.autopilot) difficulty *= 0.1;
            return difficulty * ctx.odScalingReading;
        };

        const f64 decay = std::pow(0.8, cur.c->deltaTime / 1000.0);
        cur.c->readingStrainNoHidden = prev.c->readingStrainNoHidden * decay +
                                       adjust(readingDifficulty.noHidden) * (1.0 - decay) * skill_multiplier;
        cur.c->readingStrainHidden =
            prev.c->readingStrainHidden * decay + adjust(readingDifficulty.hidden) * (1.0 - decay) * skill_multiplier;
    }

    // flashlight (lazer Flashlight.StrainValueAt): NON-normalized accumulator on the RAW delta
    // time, only computed when the flashlight flag is set (lazer only instantiates the skill
    // with the mod active)
    if(ctx.flashlight) {
        const f64 skill_multiplier = 0.058;

        const auto flashlightDifficulty = evaluate_flashlight_difficulty_of(cur, ctx);

        // per-note mod adjustments + OD scaling (lazer Flashlight.calculateAdjustedDifficulty)
        const auto adjust = [&ctx](f64 difficulty) {
            if(ctx.touchDevice) difficulty = std::pow(difficulty, 0.9);
            if(ctx.relax) difficulty *= 0.7;
            if(ctx.autopilot) difficulty *= 0.4;
            return difficulty * ctx.odScalingAimFl;
        };

        const f64 decay = std::pow(0.15, cur.c->deltaTime / 1000.0);
        cur.c->flashlightStrainNoHidden =
            prev.c->flashlightStrainNoHidden * decay + adjust(flashlightDifficulty.noHidden) * skill_multiplier;
        cur.c->flashlightStrainHidden =
            prev.c->flashlightStrainHidden * decay + adjust(flashlightDifficulty.hidden) * skill_multiplier;
    }
}

// see DifficultyValue() @ https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Aim.cs
// see https://github.com/ppy/osu/blob/master/osu.Game/Rulesets/Difficulty/Skills/VariableLengthStrainSkill.cs
// rebuilds the variable-length strain sections from the recorded per-object strains in one
// forward pass (equivalent to lazer's incremental ProcessInternal + provisional final peak),
// then reduces the top strains and takes the continuous geometric integral
f64 calculate_aim_difficulty(const Skills::Skill type, const DifficultyHitObject *dobjects, uSz dobjectCount) {
    if(dobjectCount < 2) return 0.0;

    static constexpr f64 max_section_length = 400.0;
    static constexpr f64 decay_weight = 0.9;
    // number of sections preserving at least 99.999% of the difficulty value; anything past
    // this is trimmed (the trim is part of the value, don't "improve" it)
    static constexpr f64 max_stored_length = 11.0 / (1.0 - decay_weight);

    // one strain section of variable length (lazer VariableLengthStrainSkill.StrainPeak)
    struct StrainPeak {
        f64 value;
        f64 sectionLength;
    };

    static constexpr auto byValueDesc = [](const StrainPeak &a, const StrainPeak &b) -> bool {
        return a.value > b.value;
    };

    std::vector<StrainPeak> peaks;  // kept sorted by value descending (lazer AddInPlace)
    {
        // TODO: very slow
        const auto insertSorted = [&peaks](StrainPeak peak) {
            peaks.insert(std::ranges::upper_bound(peaks, peak, byValueDesc), peak);
        };
        // stores previous strains so a high-strain object followed by lower ones still fills the
        // gap sections with its decaying influence instead of a harsh drop-off
        struct QueuedStrain {
            f64 strain;
            f64 startTime;
        };
        // consumed FIFO from queuedHead (instead of popping the front) to keep consumption O(1)
        std::vector<QueuedStrain> queuedStrains;
        uSz queuedHead = 0;

        f64 curPeak = 0.0;
        f64 curBegin = 0.0;
        f64 curEnd = 0.0;

        f64 totalLength = 0.0;

        const auto saveCurrentPeak = [&](f64 sectionLength) {
            insertSorted(StrainPeak{.value = curPeak, .sectionLength = std::round(sectionLength)});
            totalLength += sectionLength;

            // remove sections from the back (smallest values) once they can't contribute anymore
            while(totalLength > max_stored_length * max_section_length) {
                totalLength -= peaks.back().sectionLength;
                peaks.pop_back();
            }
        };

        for(uSz i = 1; i < dobjectCount; i++) {
            const f64 time = (f64)dobjects[i].time;
            const f64 strain = dobjects[i].c->strains[type];

            if(i == 1) {
                // first lazer object: set up the first section to end max_section_length after it
                curBegin = time;
                curEnd = curBegin + max_section_length;
                curPeak = strain;
                continue;
            }

            const f64 prevTime = (f64)dobjects[i - 1].time;
            const f64 prevStrain = dobjects[i - 1].c->strains[type];
            // CalculateInitialStrain: the running strain decayed from the previous object to the
            // given section start
            const auto initialStrain = [&](f64 t) { return prevStrain * strainDecay(type, t - prevTime); };

            // backfillPeaks: fill the space between the current section end and this object
            while(time > curEnd) {
                saveCurrentPeak(curEnd - curBegin);
                curBegin = curEnd;

                if(queuedHead < queuedStrains.size()) {
                    const auto [queuedStrain, queuedStart] = queuedStrains[queuedHead++];

                    // the section ends max_section_length after the queued strain, so a queued
                    // strain gets its own section if the gap to the current object is large enough
                    curEnd = queuedStart + max_section_length;
                    curPeak = std::max(initialStrain(curBegin), queuedStrain);
                } else {
                    curEnd = curBegin + max_section_length;
                    curPeak = initialStrain(curBegin);
                }
            }

            if(strain > curPeak) {
                // begin a new peak: nothing in the queue can contribute anymore
                queuedStrains.clear();
                queuedHead = 0;
                saveCurrentPeak(time - curBegin);

                curBegin = time;
                curEnd = curBegin + max_section_length;
                curPeak = strain;
            } else {
                while(queuedStrains.size() > queuedHead && queuedStrains.back().strain < strain)
                    queuedStrains.pop_back();
                queuedStrains.emplace_back(strain, time);
            }
        }

        // the provisional final peak (GetCurrentStrainPeaks); plain insert, no length accounting
        insertSorted(StrainPeak{.value = curPeak, .sectionLength = std::round(curEnd - curBegin)});
    }

    // getReducedStrainPeaks: chop the top strains within the first 4000ms of cumulative section
    // time into 20ms chunks with a log10-lerp reduction, then re-sort
    static constexpr f64 reduced_section_time = 4000.0;
    static constexpr f64 reduced_strain_baseline = 0.727;
    static constexpr f64 chunk_size = 20.0;

    std::vector<StrainPeak> strains;
    strains.reserve(peaks.size());
    for(const auto &peak : peaks) {
        if(peak.value > 0.0) strains.push_back(peak);
    }

    f64 time = 0.0;
    uSz skipCount = 0;

    while(strains.size() > skipCount && time < reduced_section_time) {
        const StrainPeak strain = strains[skipCount];  // copy, the vector is appended to below

        for(i32 chunkIdx = 0;; chunkIdx++) {
            const f64 addedTime = chunk_size * (f64)chunkIdx;
            if(addedTime >= strain.sectionLength) break;

            const f64 scale =
                std::log10(std::lerp(1.0, 10.0, std::clamp<f64>((time + addedTime) / reduced_section_time, 0.0, 1.0)));
            strains.push_back(
                StrainPeak{.value = strain.value * std::lerp(reduced_strain_baseline, 1.0, scale),
                           .sectionLength = std::round(std::min(chunk_size, strain.sectionLength - addedTime))});
        }

        time += strain.sectionLength;
        skipCount++;
    }

    // the first skipCount entries were replaced by their chunks; sort the rest descending
    // (stable, matching LINQ OrderByDescending tie behavior)
    std::stable_sort(strains.begin() + (sSz)skipCount, strains.end(), byValueDesc);

    // continuous weighted sum: weight = integral of decay_weight^x over the section, with time
    // in units of max_section_length
    f64 difficulty = 0.0;
    f64 t = 0.0;

    for(uSz i = skipCount; i < strains.size(); i++) {
        const f64 t1 = t + strains[i].sectionLength / max_section_length;
        difficulty += strains[i].value * (std::pow(decay_weight, t) - std::pow(decay_weight, t1));
        t = t1;
    }

    return difficulty / (1.0 - decay_weight);
}

// see https://github.com/ppy/osu/blob/master/osu.Game/Rulesets/Difficulty/Skills/HarmonicSkill.cs
// harmonic weighted sum over the sorted per-object difficulties; every note contributes.
// objectWeightSum is needed by the top-weighted counts and must come from this same sum.
f64 harmonic_difficulty_sum(std::span<f64> difficulties, f64 harmonicScale, f64 decayExponent,
                            f64 &outObjectWeightSum) {
    outObjectWeightSum = 0.0;

    std::ranges::sort(difficulties, std::ranges::greater{});

    f64 difficulty = 0.0;
    i32 index = 0;

    for(const f64 obj : difficulties) {
        if(obj <= 0.0) break;  // sorted descending, nothing after this contributes

        const f64 harmonic = harmonicScale / (1 + index);
        const f64 weight = (1 + harmonic) / (std::pow((f64)index, decayExponent) + 1 + harmonic);

        outObjectWeightSum += weight;
        difficulty += obj * weight;
        index += 1;
    }

    return difficulty;
}

// see DifficultyValue() @ https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Speed.cs
f64 calculate_speed_difficulty(const DifficultyHitObject *dobjects, uSz dobjectCount, f64 &outObjectWeightSum) {
    outObjectWeightSum = 0.0;
    if(dobjectCount < 2) return 0.0;

    std::vector<f64> difficulties;
    difficulties.reserve(dobjectCount - 1);
    for(uSz i = 1; i < dobjectCount; i++) {
        difficulties.push_back(dobjects[i].c->get_strain(Skills::SPEED));
    }

    return harmonic_difficulty_sum(difficulties, 20.0, 0.9, outObjectWeightSum);
}

// see DifficultyValue() @ https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Reading.cs
// harmonic sum (HS=1, DE=0.9) over the recorded per-object reading difficulties with the
// first-minute notes scaled down ("assume the first seconds are completely memorised"), plus
// the top-weighted note count (order dependency: it needs this sum's ObjectWeightSum)
f64 calculate_reading_difficulty(const DifficultyHitObject *dobjects, uSz dobjectCount, bool hidden,
                                 f64 &outDifficultNoteCount) {
    outDifficultNoteCount = 0.0;
    if(dobjectCount < 2) return 0.0;

    const auto strainOf = [hidden](const DifficultyHitObject &o) {
        return hidden ? o.c->readingStrainHidden : o.c->readingStrainNoHidden;
    };

    // lazer counts this during processing; identical to counting the prefix objects within the
    // first 60 (rate-adjusted) seconds after the first difficulty object
    const f64 reducedDuration = (f64)dobjects[1].time + 60.0 * 1000.0;
    f64 reducedNoteCount = 0.0;
    for(uSz i = 1; i < dobjectCount; i++) {
        if((f64)dobjects[i].time <= reducedDuration) reducedNoteCount += 1.0;
    }

    // GetTransformedDifficulties: filter out zeros (in map order), then scale the first minute
    // down with a log10-lerp from 0
    std::vector<f64> difficulties;
    difficulties.reserve(dobjectCount - 1);
    for(uSz i = 1; i < dobjectCount; i++) {
        const f64 v = strainOf(dobjects[i]);
        if(v > 0.0) difficulties.push_back(v);
    }

    for(uSz i = 0; (f64)i < std::min((f64)difficulties.size(), reducedNoteCount); i++) {
        const f64 scale = std::log10(std::lerp(1.0, 10.0, std::clamp((f64)i / reducedNoteCount, 0.0, 1.0)));
        difficulties[i] *= std::lerp(0.0, 1.0, scale);
    }

    f64 objectWeightSum = 0.0;
    const f64 difficulty = harmonic_difficulty_sum(difficulties, 1.0, 0.9, objectWeightSum);

    // CountTopWeightedObjectDifficulties (reading override), over the untransformed recorded values
    if(objectWeightSum != 0.0) {
        const f64 consistentTopNote = difficulty / objectWeightSum;
        if(consistentTopNote != 0.0) {
            f64 sum = 0.0;
            for(uSz i = 1; i < dobjectCount; i++) {
                sum += logistic(strainOf(dobjects[i]) / consistentTopNote, 1.15, 5.0, 1.1);
            }
            outDifficultNoteCount = sum;
        }
    }

    return difficulty;
}

// fixed 400ms section peaks over the recorded per-object strains (lazer StrainSkill); used by
// the flashlight skill and the strain-graph export
std::vector<f64> build_fixed_strain_sections(const DifficultyHitObject *dobjects, uSz dobjectCount, f64 decayBase,
                                             f64 (*strainOf)(const DifficultyHitObject &)) {
    static constexpr f64 strain_step = 400.0;

    std::vector<f64> highestStrains;
    if(dobjectCount < 2) return highestStrains;

    // the first lazer object begins with an incremented section end
    f64 interval_end = (std::ceil((f64)dobjects[1].time / strain_step) * strain_step);
    f64 max_strain = 0.0;

    for(uSz i = 1; i < dobjectCount; i++) {
        const DifficultyHitObject &cur = dobjects[i];

        // make previous peak strain decay until the current object
        while(cur.time > interval_end) {
            highestStrains.push_back(max_strain);

            // skip calculating strain decay for very long breaks (e.g. beatmap upload size limit hack diffs)
            const f64 strainDelta = interval_end - (f64)dobjects[i - 1].time;
            if(strainDelta > 600000.0)
                max_strain = 0.0;
            else
                max_strain = strainOf(dobjects[i - 1]) * std::pow(decayBase, strainDelta / 1000.0);

            interval_end += strain_step;
        }

        max_strain = std::max(max_strain, strainOf(cur));
    }

    // the peak strain will not be saved for the last section in the above loop
    highestStrains.push_back(max_strain);

    return highestStrains;
}

// see DifficultyValue() @ https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Flashlight.cs
// plain sum of the fixed 400ms section peaks, scaled by a length factor (shorter maps have a
// higher ratio of 0 combo/100 combo flashlight radius)
f64 calculate_flashlight_difficulty(const DifficultyHitObject *dobjects, uSz dobjectCount, bool hidden) {
    const std::vector<f64> peaks = build_fixed_strain_sections(
        dobjects, dobjectCount, 0.15,
        hidden ? +[](const DifficultyHitObject &o) { return o.c->flashlightStrainHidden; }
               : +[](const DifficultyHitObject &o) { return o.c->flashlightStrainNoHidden; });

    f64 sum = 0.0;
    for(const f64 peak : peaks) sum += peak;

    const f64 totalObjects = (f64)dobjectCount;
    sum *= 0.7 + 0.1 * std::min(1.0, totalObjects / 200.0) +
           (totalObjects > 200.0 ? 0.2 * std::min(1.0, (totalObjects - 200.0) / 200.0) : 0.0);

    return sum;
}

// CountTopWeightedStrains @ https://github.com/ppy/osu/blob/master/osu.Game/Rulesets/Difficulty/Skills/VariableLengthStrainSkill.cs
f64 count_top_weighted_strains_geometric(const DifficultyHitObject *dobjects, uSz dobjectCount,
                                         const Skills::Skill type, f64 difficultyValue) {
    if(dobjectCount < 2) return 0.0;

    // what would the top strain be if all strain values were identical
    const f64 consistentTopStrain = difficultyValue * (1.0 - 0.9);
    if(consistentTopStrain == 0.0) return (f64)(dobjectCount - 1);  // lazer returns the object count here

    f64 sum = 0.0;
    for(uSz i = 1; i < dobjectCount; i++) {
        sum += logistic(dobjects[i].c->strains[type] / consistentTopStrain, 0.88, 10.0, 1.1);
    }
    return sum;
}

// CountTopWeightedSliders @ https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Aim.cs
f64 count_top_weighted_sliders_geometric(const DifficultyHitObject *dobjects, uSz dobjectCount,
                                         const Skills::Skill type, f64 difficultyValue) {
    if(dobjectCount < 2) return 0.0;

    const f64 consistentTopStrain = difficultyValue * (1.0 - 0.9);
    if(consistentTopStrain == 0.0) return 0.0;

    f64 sum = 0.0;
    for(uSz i = 1; i < dobjectCount; i++) {
        if(dobjects[i].type != DifficultyHitObject::TYPE::SLIDER) continue;
        sum += logistic(dobjects[i].c->strains[type] / consistentTopStrain, 0.88, 10.0, 1.1);
    }
    return sum;
}

// GetDifficultSliders @ https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Aim.cs
f64 calculate_aim_difficult_slider_count(const DifficultyHitObject *dobjects, uSz dobjectCount,
                                         const Skills::Skill type) {
    f64 maxSliderStrain = 0.0;
    for(uSz i = 1; i < dobjectCount; i++) {
        if(dobjects[i].type == DifficultyHitObject::TYPE::SLIDER)
            maxSliderStrain = std::max(maxSliderStrain, dobjects[i].c->strains[type]);
    }
    if(maxSliderStrain <= 0.0) return 0.0;

    f64 sum = 0.0;
    for(uSz i = 1; i < dobjectCount; i++) {
        if(dobjects[i].type != DifficultyHitObject::TYPE::SLIDER) continue;
        sum += logistic(dobjects[i].c->strains[type] / maxSliderStrain, 0.5, 12.0);
    }
    return sum;
}

// RelevantObjectCount @ https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Speed.cs
f64 calculate_speed_relevant_object_count(const DifficultyHitObject *dobjects, uSz dobjectCount) {
    f64 maxStrain = 0.0;
    for(uSz i = 1; i < dobjectCount; i++) {
        maxStrain = std::max(maxStrain, dobjects[i].c->get_strain(Skills::SPEED));
    }
    if(maxStrain == 0.0) return 0.0;

    f64 sum = 0.0;
    for(uSz i = 1; i < dobjectCount; i++) {
        sum += logistic(dobjects[i].c->get_strain(Skills::SPEED) / maxStrain, 0.5, 12.0);
    }
    return sum;
}

// CountTopWeightedObjectDifficulties @ https://github.com/ppy/osu/blob/master/osu.Game/Rulesets/Difficulty/Skills/HarmonicSkill.cs
f64 count_top_weighted_speed_strains(const DifficultyHitObject *dobjects, uSz dobjectCount, f64 difficultyValue,
                                     f64 objectWeightSum) {
    if(dobjectCount < 2 || objectWeightSum == 0.0) return 0.0;

    // what would the top difficulty be if all object difficulties were identical
    const f64 consistentTopObject = difficultyValue / objectWeightSum;
    if(consistentTopObject == 0.0) return 0.0;

    f64 sum = 0.0;
    for(uSz i = 1; i < dobjectCount; i++) {
        sum += logistic(dobjects[i].c->get_strain(Skills::SPEED) / consistentTopObject, 0.88, 10.0, 1.1);
    }
    return sum;
}

// CountTopWeightedSliders @ https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Speed.cs
f64 count_top_weighted_speed_sliders(const DifficultyHitObject *dobjects, uSz dobjectCount, f64 difficultyValue,
                                     f64 objectWeightSum) {
    if(dobjectCount < 2 || objectWeightSum == 0.0) return 0.0;

    const f64 consistentTopObject = difficultyValue / objectWeightSum;
    if(consistentTopObject == 0.0) return 0.0;

    f64 sum = 0.0;
    for(uSz i = 1; i < dobjectCount; i++) {
        if(dobjects[i].type != DifficultyHitObject::TYPE::SLIDER) continue;
        sum += logistic(dobjects[i].c->get_strain(Skills::SPEED) / consistentTopObject, 0.88, 10.0, 1.1);
    }
    return sum;
}

// an island is a group of consecutive objects with the same delta time
// (lazer RhythmEvaluator.Island; reference semantics there, value copies here)
struct RhythmIsland {
    static constexpr i32 NOT_INITIALIZED = std::numeric_limits<i32>::max();

    i32 delta{NOT_INITIALIZED};
    i32 deltaCount{1};
    i32 occurrences{1};

    RhythmIsland() = default;
    explicit RhythmIsland(i32 d) : delta(d == NOT_INITIALIZED ? NOT_INITIALIZED : std::max(d, 25)) {}

    inline void addDelta(i32 d) {
        if(delta == NOT_INITIALIZED) delta = std::max(d, 25);
        deltaCount++;
    }

    [[nodiscard]] inline bool isSimilarPolarity(const RhythmIsland &other, f64 epsilon) const {
        // single delta islands shouldn't be compared
        if(deltaCount <= 1 || other.deltaCount <= 1) return false;

        return std::abs(delta - other.delta) < epsilon && deltaCount % 2 == other.deltaCount % 2;
    }

    [[nodiscard]] inline bool almostEquals(const RhythmIsland &other, f64 epsilon) const {
        return std::abs(delta - other.delta) < epsilon && deltaCount == other.deltaCount;
    }
};

// https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Preprocessing/OsuDifficultyHitObject.cs
// how possible it is to doubletap cur together with next and still get a perfect judgement.
// next's own deltaTime may not be computed yet during the strain pass, so it is derived from
// the times directly (identical value)
f64 calculate_double_tap_feasibility(const DifficultyHitObject &cur, const DifficultyHitObject *next,
                                     f64 hitWindow300) {
    if(next == nullptr) return 0.0;

    const f64 currDeltaTime = std::max(1.0, cur.c->deltaTime);
    const f64 nextDeltaTime = std::max(1.0, (f64)(next->time - cur.time));

    const f64 deltaDifference = std::abs(nextDeltaTime - currDeltaTime);

    const f64 speedRatio = currDeltaTime / std::max(currDeltaTime, deltaDifference);
    const f64 windowRatio = powi(std::min(1.0, currDeltaTime / hitWindow300), 5);

    // can't doubletap if circles don't intersect
    const f64 distanceFactor = powi(reverseLerp(cur.c->lazyJumpDistance, 100.0, 50.0), 2);

    return 1.0 - std::pow(speedRatio, distanceFactor * (1.0 - windowRatio));
}

// https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Evaluators/Speed/SpeedEvaluator.cs
f64 evaluate_speed_difficulty_of(const DifficultyHitObject &cur, const StrainEvalContext &ctx) {
    if(cur.type == DifficultyHitObject::TYPE::SPINNER) return 0.0;

    static constexpr f64 min_speed_bonus_ms = 75.0;  // 200 BPM 1/4th
    static constexpr f64 speed_balancing_factor = 40.0;

    f64 strainTime = cur.c->strainTime;
    const f64 doubleTapFeasibility = 1.0 - calculate_double_tap_feasibility(cur, cur.c->get_next(0), ctx.hitWindow300);

    // cap deltatime to the OD 300 hitwindow
    // 0.93 is derived from making sure 260bpm OD8 streams aren't nerfed harshly, whilst 0.92 limits the effect of the cap
    strainTime /= std::clamp((strainTime / ctx.hitWindow300) / 0.93, 0.92, 1.0);

    // additional scaling bonus for streams/bursts higher than 200bpm
    f64 speedBonus = 0.0;
    if(strainTime < min_speed_bonus_ms)
        speedBonus = 0.75 * powi((min_speed_bonus_ms - strainTime) / speed_balancing_factor, 2);

    f64 speedDifficulty = (1.0 + speedBonus) * 1000.0 / strainTime;

    // having less time to strain-recover is harder (normalized EMA has no time scaling anymore)
    speedDifficulty *= 1.0 / (1.0 - std::pow(0.3, cur.c->strainTime / 1000.0));

    // apply penalty if there's doubletappable doubles
    return speedDifficulty * doubleTapFeasibility;
}

// https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Evaluators/Speed/RhythmEvaluator.cs
forceinline f64 rhythm_effective_difficulty(f64 deltaDifferenceRatio) {
    static constexpr f64 rhythm_ratio_difficulty_multiplier = 26.0;

    // take only the fractional part of the value since we're only interested in punishing multiples
    const f64 deltaDifferenceFraction = deltaDifferenceRatio - std::trunc(deltaDifferenceRatio);

    return 1.0 + rhythm_ratio_difficulty_multiplier * std::min(0.5, smoothstepBellCurve(deltaDifferenceFraction));
}

// rhythm multiplier for the tap difficulty from the historic rhythm data of the current object
f64 evaluate_rhythm_of(const DifficultyHitObject &cur, const StrainEvalContext &ctx) {
    if(cur.type == DifficultyHitObject::TYPE::SPINNER) return 0.0;

    static constexpr i32 history_time_max = 5 * 1000;  // 5 seconds
    static constexpr i32 history_objects_max = 32;
    static constexpr f64 rhythm_overall_multiplier = 0.95;

    // delta times can be zero for simultaneous objects, floor them to a tiny value
    static constexpr f64 delta_min_value = 1e-7;

    f64 rhythmComplexitySum = 0.0;

    const f64 deltaDifferenceEpsilon = ctx.hitWindow300 * 0.3;

    RhythmIsland island{RhythmIsland::NOT_INITIALIZED};
    RhythmIsland previousIsland{RhythmIsland::NOT_INITIALIZED};

    Mc::InlineVec<RhythmIsland, 16> islands;

    f64 startDifficulty = 0.0;  // store the difficulty of the current start of an island to buff for tighter rhythms

    bool firstDeltaSwitch = false;

    const i32 historicalNoteCount = std::min(cur.c->index, history_objects_max);

    i32 rhythmStart = 0;

    while(rhythmStart < historicalNoteCount - 2 &&
          cur.time - cur.c->get_previous(rhythmStart)->time < history_time_max) {
        rhythmStart++;
    }

    const DifficultyHitObject *prevObj = cur.c->get_previous(rhythmStart);
    const DifficultyHitObject *prevPrevObj = cur.c->get_previous(rhythmStart + 1);

    // we go from the furthest object back to the current one
    for(i32 i = rhythmStart; i > 0; i--) {
        const DifficultyHitObject *currObj = cur.c->get_previous(i - 1);

        if(currObj->type == DifficultyHitObject::TYPE::SPINNER) continue;

        // scales note 0 to 1 from history to now
        const f64 timeDecay = (f64)(history_time_max - (cur.time - currObj->time)) / history_time_max;
        const f64 noteDecay = (f64)(historicalNoteCount - i) / historicalNoteCount;

        const f64 currHistoricalDecay =
            std::min(noteDecay, timeDecay);  // either we're limited by time or limited by object count

        const f64 currDelta = std::max(currObj->c->deltaTime, delta_min_value);
        const f64 prevDelta = std::max(prevObj->c->deltaTime, delta_min_value);

        const f64 deltaDifference = std::abs(prevDelta - currDelta);

        // make sure to always have the current island initialised
        if(island.delta == RhythmIsland::NOT_INITIALIZED) island = RhythmIsland{(i32)currDelta};

        // calculate how much current delta difference deserves a rhythm bonus
        // this function is meant to reduce rhythm bonus for deltas that are multiples of each other (i.e 100 and 200)
        const f64 deltaDifferenceRatio = std::max(prevDelta, currDelta) / std::min(prevDelta, currDelta);

        // reduce ratio bonus if delta difference is too big
        const f64 differenceMultiplier = std::clamp(2.0 - deltaDifferenceRatio / 8.0, 0.0, 1.0);

        const f64 windowPenalty =
            std::clamp((deltaDifference - deltaDifferenceEpsilon) / deltaDifferenceEpsilon, 0.0, 1.0);

        f64 effectiveDifficulty =
            rhythm_effective_difficulty(deltaDifferenceRatio) * windowPenalty * differenceMultiplier;

        // if the previous object is a slider the "unpress->tap" motion might be simple even if
        // the full deltatime is a weird ratio (slider-circle-circle should be evaluated as a
        // regular triple), so take the least difficult interpretation of the lazy/real end deltas
        if(prevObj->type == DifficultyHitObject::TYPE::SLIDER) {
            const f64 sliderLazyEndDelta = currObj->c->minimumJumpTime;
            const f64 sliderLazyDeltaDifferenceRatio =
                std::max(sliderLazyEndDelta, currDelta) / std::min(sliderLazyEndDelta, currDelta);

            const f64 sliderRealEndDelta = currObj->c->lastObjectEndDeltaTime;
            const f64 sliderRealDeltaDifferenceRatio =
                std::max(sliderRealEndDelta, currDelta) / std::min(sliderRealEndDelta, currDelta);

            const f64 sliderEffectiveDifficulty = std::min(rhythm_effective_difficulty(sliderLazyDeltaDifferenceRatio),
                                                           rhythm_effective_difficulty(sliderRealDeltaDifferenceRatio));
            effectiveDifficulty = std::min(sliderEffectiveDifficulty, effectiveDifficulty);
        }

        if(deltaDifference < deltaDifferenceEpsilon) {
            // island is still progressing
            island.addDelta((i32)currDelta);
        }

        if(firstDeltaSwitch) {
            if(deltaDifference > deltaDifferenceEpsilon) {
                // bpm change is into slider, this is easy acc window
                if(currObj->type == DifficultyHitObject::TYPE::SLIDER) effectiveDifficulty *= 0.5;

                // repeated island polarity (2 -> 4, 3 -> 5)
                if(island.isSimilarPolarity(previousIsland, deltaDifferenceEpsilon)) effectiveDifficulty *= 0.5;

                // previous increase happened a note ago, 1/1->1/2-1/4, dont want to buff this.
                // (prevPrevObj is never null here: firstDeltaSwitch implies at least one prior
                // iteration shifted it to a real object)
                if(std::max(prevPrevObj->c->deltaTime, delta_min_value) > prevDelta + deltaDifferenceEpsilon &&
                   prevDelta > currDelta + deltaDifferenceEpsilon)
                    effectiveDifficulty *= 0.125;

                // repeated island size (ex: triplet -> triplet)
                // TODO: remove this nerf since its staying here only for balancing purposes because of the flawed ratio calculation
                if(previousIsland.deltaCount == island.deltaCount) effectiveDifficulty *= 0.5;

                const bool isSpeedingUp = prevDelta > currDelta + deltaDifferenceEpsilon;
                if(isSpeedingUp) effectiveDifficulty *= 0.65;

                bool found = false;
                for(auto &existingIsland : islands) {
                    if(existingIsland.almostEquals(island, deltaDifferenceEpsilon)) {
                        // only increase island occurrences if they're going one after another
                        if(previousIsland.almostEquals(island, deltaDifferenceEpsilon)) existingIsland.occurrences++;

                        // repeated island (ex: triplet -> triplet)
                        const f64 power = logistic(island.delta, 58.33, 0.24, 2.75);
                        effectiveDifficulty *= std::min(3.0 / existingIsland.occurrences,
                                                        std::pow(1.0 / existingIsland.occurrences, power));

                        found = true;
                        break;
                    }
                }

                if(!found && island.deltaCount > 0) islands.emplace_back(island);

                // scale down the difficulty if the object is double-tappable
                effectiveDifficulty *=
                    1.0 - calculate_double_tap_feasibility(*prevObj, currObj, ctx.hitWindow300) * 0.75;

                if(island.deltaCount > 1) {
                    rhythmComplexitySum += std::sqrt(effectiveDifficulty * startDifficulty) * currHistoricalDecay;
                } else {
                    // constant difficulty for single-note islands
                    rhythmComplexitySum += 0.7 * currHistoricalDecay;
                }

                startDifficulty = effectiveDifficulty;

                if(prevDelta + deltaDifferenceEpsilon < currDelta)  // we're slowing down, stop counting
                    firstDeltaSwitch = false;  // if we're speeding up, this stays true and we keep counting island size

                previousIsland = island;
                island = RhythmIsland{(i32)currDelta};
            }
        } else if(prevDelta > currDelta + deltaDifferenceEpsilon)  // we're speeding up
        {
            // begin counting island until we change speed again
            firstDeltaSwitch = true;

            // bpm change is into slider, this is easy acc window
            if(currObj->type == DifficultyHitObject::TYPE::SLIDER) effectiveDifficulty *= 0.6;

            // bpm change was from a slider, this is easier typically than circle -> circle
            // unintentional side effect is that bursts with kicksliders at the ends might have lower difficulty than bursts without sliders
            if(prevObj->type == DifficultyHitObject::TYPE::SLIDER) effectiveDifficulty *= 0.6;

            startDifficulty = effectiveDifficulty;

            island = RhythmIsland{(i32)currDelta};
        }

        prevPrevObj = prevObj;
        prevObj = currObj;
    }

    // if the current island is long we don't want the sum to have as big of an effect
    rhythmComplexitySum *= reverseLerp(island.deltaCount, 22.0, 3.0);

    // produces multiplier that can be applied to strain. range [1, infinity) (not really though)
    return std::sqrt(4.0 + rhythmComplexitySum * rhythm_overall_multiplier) / 2.0;
}

// https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Evaluators/Aim/SnapAimEvaluator.cs
forceinline f64 calc_angle_wideness(f64 angle) { return smoothstep(angle, 40.0 * PIOVER180, 140.0 * PIOVER180); }
forceinline f64 calc_angle_acuteness(f64 angle) { return smoothstep(angle, 140.0 * PIOVER180, 40.0 * PIOVER180); }

f64 snap_vector_angle_repetition(const DifficultyHitObject &cur, const DifficultyHitObject &previous) {
    if(std::isnan(cur.c->angle) || std::isnan(previous.c->angle)) return 1.0;

    static constexpr i32 note_limit = 6;
    static constexpr f64 maximum_repetition_nerf = 0.15;
    static constexpr f64 maximum_vector_influence = 0.5;

    f64 constantAngleCount = 0.0;

    for(i32 index = 0; index < note_limit; index++) {
        const DifficultyHitObject *prevObj = cur.c->get_previous(index);

        if(prevObj == nullptr) break;

        // only consider vectors in the same jump section, stopping to change rhythm ruins momentum
        if(std::max(cur.c->strainTime, prevObj->c->strainTime) >
           1.1 * std::min(cur.c->strainTime, prevObj->c->strainTime))
            break;

        if(!std::isnan(prevObj->c->normalisedVectorAngle) && !std::isnan(cur.c->normalisedVectorAngle)) {
            const f64 angleDifference = std::abs(cur.c->normalisedVectorAngle - prevObj->c->normalisedVectorAngle);
            // constants need to be precise so that values stay within the range of 0 and 1,
            // https://www.desmos.com/calculator/a8jesv5sv2
            constantAngleCount += std::cos(8.0 * std::min(11.25 * PIOVER180, angleDifference));
        }
    }

    const f64 vectorRepetition = powi(std::min(0.5 / constantAngleCount, 1.0), 2);

    const f64 stackFactor = smootherStep(cur.c->lazyJumpDistance, 0.0, 100.0);

    const f64 angleDifferenceAdjusted =
        std::cos(2.0 * std::min(45.0 * PIOVER180, std::abs(cur.c->angle - previous.c->angle) * stackFactor));

    const f64 baseNerf =
        1.0 - maximum_repetition_nerf * calc_angle_acuteness(previous.c->angle) * angleDifferenceAdjusted;

    return powi(baseNerf + (1.0 - baseNerf) * vectorRepetition * maximum_vector_influence * stackFactor, 2);
}

f64 evaluate_snap_aim_of(const DifficultyHitObject &cur, bool withSliderTravelDistance, const StrainEvalContext &ctx) {
    if(cur.type == DifficultyHitObject::TYPE::SPINNER || cur.c->index <= 1) return 0.0;

    const DifficultyHitObject &last = *cur.c->get_previous(0);
    if(last.type == DifficultyHitObject::TYPE::SPINNER) return 0.0;

    static constexpr f64 wide_angle_multiplier = 9.67;
    static constexpr f64 acute_angle_multiplier = 2.41;
    static constexpr f64 slider_multiplier = 1.5;
    static constexpr f64 velocity_change_multiplier = 0.9;

    // WARNING: increasing this multiplier beyond 1.02 reduces difficulty as distance increases
    static constexpr f64 wiggle_multiplier = 1.02;

    const DifficultyHitObject *last2 = cur.c->get_previous(2);

    static constexpr f64 radius = 50.0;     // NORMALISED_RADIUS
    static constexpr f64 diameter = 100.0;  // NORMALISED_DIAMETER

    // velocity to the current object, assuming the last object is a hitcircle
    const f64 currDistance = withSliderTravelDistance ? cur.c->lazyJumpDistance : cur.c->jumpDistance;
    f64 currVelocity = currDistance / cur.c->strainTime;

    // but if the last object is a slider, then we extend the travel velocity through the slider into the current object
    if(last.type == DifficultyHitObject::TYPE::SLIDER && withSliderTravelDistance) {
        const f64 sliderDistance = last.c->lazyTravelDist + cur.c->lazyJumpDistance;
        currVelocity = std::max(currVelocity, sliderDistance / cur.c->strainTime);
    }

    const f64 prevDistance = withSliderTravelDistance ? last.c->lazyJumpDistance : last.c->jumpDistance;
    f64 prevVelocity = prevDistance / last.c->strainTime;

    f64 snapDifficulty = currVelocity;  // start difficulty with regular velocity

    // penalize angle repetition
    snapDifficulty *= snap_vector_angle_repetition(cur, last);

    if(!std::isnan(cur.c->angle) && !std::isnan(last.c->angle)) {
        const f64 currAngle = cur.c->angle;
        const f64 lastAngle = last.c->angle;

        // rewarding angles, take the smaller velocity as base
        const f64 velocityInfluence = std::min(currVelocity, prevVelocity);

        f64 acuteAngleBonus = 0.0;

        if(std::max(cur.c->strainTime, last.c->strainTime) < 1.25 * std::min(cur.c->strainTime, last.c->strainTime)) {
            // rhythms are the same
            acuteAngleBonus = calc_angle_acuteness(currAngle);

            // penalize angle repetition, important to do it _before_ multiplying by anything (raw acuteness compared)
            acuteAngleBonus *=
                0.08 + 0.92 * (1.0 - std::min(acuteAngleBonus, powi(calc_angle_acuteness(lastAngle), 3)));

            // apply acute angle bonus for BPM above 300 1/2 and distance more than one diameter
            acuteAngleBonus *= velocityInfluence * smootherStep(30000.0 / cur.c->strainTime, 300.0, 400.0) *
                               smootherStep(currDistance, 0.0, diameter * 2.0);
        }

        f64 wideAngleBonus = calc_angle_wideness(currAngle);

        // penalize angle repetition, important to do it _before_ multiplying by velocity (raw wideness compared)
        wideAngleBonus *= 0.25 + 0.75 * (1.0 - std::min(wideAngleBonus, powi(calc_angle_wideness(lastAngle), 3)));

        // rescaling velocity for the wide angle bonus
        static constexpr f64 wide_angle_time_scale = 1.45;
        f64 wideAngleCurrVelocity = currDistance / std::pow(cur.c->strainTime, wide_angle_time_scale);
        const f64 wideAnglePrevVelocity = prevDistance / std::pow(last.c->strainTime, wide_angle_time_scale);

        if(last.type == DifficultyHitObject::TYPE::SLIDER && withSliderTravelDistance) {
            const f64 sliderDistance = last.c->lazyTravelDist + cur.c->lazyJumpDistance;
            wideAngleCurrVelocity =
                std::max(wideAngleCurrVelocity, sliderDistance / std::pow(cur.c->strainTime, wide_angle_time_scale));
        }

        wideAngleBonus *= std::min(wideAngleCurrVelocity, wideAnglePrevVelocity);

        if(last2 != nullptr) {
            // if objects just go back and forth through a middle point - don't give as much wide bonus
            // (any object's angle's center point is always the previous object)
            const f32 distance = vec::length(last2->pos - last.pos);

            if(distance < 1.0f) wideAngleBonus *= 1.0 - 0.55 * (1.0 - distance);
        }

        // add in acute angle bonus or wide angle bonus, whichever is larger
        snapDifficulty += std::max(acuteAngleBonus * acute_angle_multiplier, wideAngleBonus * wide_angle_multiplier);

        // apply wiggle bonus for jumps that are [radius, 3*diameter] in distance, with < 110 angle
        // https://www.desmos.com/calculator/dp0v0nvowc
        const f64 wiggleBonus = velocityInfluence * smootherStep(currDistance, radius, diameter) *
                                std::pow(reverseLerp(currDistance, diameter * 3.0, diameter), 1.8) *
                                smootherStep(currAngle, 110.0 * PIOVER180, 60.0 * PIOVER180) *
                                smootherStep(prevDistance, radius, diameter) *
                                std::pow(reverseLerp(prevDistance, diameter * 3.0, diameter), 1.8) *
                                smootherStep(lastAngle, 110.0 * PIOVER180, 60.0 * PIOVER180);

        snapDifficulty += wiggleBonus * wiggle_multiplier;
    }

    if(std::max(prevVelocity, currVelocity) != 0.0) {
        if(withSliderTravelDistance) {
            // we want to use just the object jump without slider velocity when awarding differences
            currVelocity = currDistance / cur.c->strainTime;
        }

        // scale with ratio of difference compared to 0.5 * max dist
        const f64 distRatio =
            smoothstep(std::abs(prevVelocity - currVelocity) / std::max(prevVelocity, currVelocity), 0.0, 1.0);

        // reward for % distance up to 125 / strainTime for overlaps where velocity is still changing
        const f64 overlapVelocityBuff = std::min(diameter * 1.25 / std::min(cur.c->strainTime, last.c->strainTime),
                                                 std::abs(prevVelocity - currVelocity));

        f64 velocityChangeBonus = overlapVelocityBuff * distRatio;

        // penalize for rhythm changes
        velocityChangeBonus *=
            powi(std::min(cur.c->strainTime, last.c->strainTime) / std::max(cur.c->strainTime, last.c->strainTime), 2);

        snapDifficulty += velocityChangeBonus * velocity_change_multiplier;
    }

    // reward sliders based on velocity
    if(cur.type == DifficultyHitObject::TYPE::SLIDER && withSliderTravelDistance) {
        const f64 sliderBonus = cur.c->travelDistance / cur.c->travelTime;
        snapDifficulty += (sliderBonus < 1.0 ? sliderBonus : std::pow(sliderBonus, 0.75)) * slider_multiplier;
    }

    // apply high circle size bonus
    snapDifficulty *= ctx.smallCircleBonus;

    // having less time to strain-recover is harder (normalized EMA has no time scaling anymore)
    snapDifficulty *= 1.0 / (1.0 - std::pow(0.03, std::pow(cur.c->strainTime / 1000.0, 0.65)));

    return snapDifficulty;
}

// https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Evaluators/Aim/FlowAimEvaluator.cs
f64 flow_overlap_factor(const DifficultyHitObject &first, const DifficultyHitObject &second, f64 objectRadius) {
    const f64 distance = vec::distance(first.pos, second.pos);
    return std::clamp(1.0 - powi(std::max(distance - objectRadius, 0.0) / objectRadius, 2), 0.0, 1.0);
}

// "flow aim": the player doesn't stop their cursor on every object and instead flows through them
f64 evaluate_flow_aim_of(const DifficultyHitObject &cur, bool withSliderTravelDistance, const StrainEvalContext &ctx) {
    if(cur.type == DifficultyHitObject::TYPE::SPINNER || cur.c->index <= 1) return 0.0;

    const DifficultyHitObject &last = *cur.c->get_previous(0);
    if(last.type == DifficultyHitObject::TYPE::SPINNER) return 0.0;

    static constexpr f64 velocity_change_multiplier = 0.52;

    const DifficultyHitObject &lastLast = *cur.c->get_previous(1);

    const f64 currDistance = withSliderTravelDistance ? cur.c->lazyJumpDistance : cur.c->jumpDistance;
    const f64 prevDistance = withSliderTravelDistance ? last.c->lazyJumpDistance : last.c->jumpDistance;

    f64 currVelocity = currDistance / cur.c->strainTime;

    if(last.type == DifficultyHitObject::TYPE::SLIDER && withSliderTravelDistance) {
        // if the last object is a slider, then we extend the travel velocity through the slider into the current object
        const f64 sliderDistance = last.c->lazyTravelDist + cur.c->lazyJumpDistance;
        currVelocity = std::max(currVelocity, sliderDistance / cur.c->strainTime);
    }

    const f64 prevVelocity = prevDistance / last.c->strainTime;

    f64 flowDifficulty = currVelocity;

    // apply high circle size bonus to the base velocity, reduced because the bonus was made for
    // an evaluator with a different d/t scaling
    flowDifficulty *= ctx.smallCircleBonusSqrt;

    // rhythm changes are harder to flow
    flowDifficulty *= 1.0 + std::min(0.25, powi((std::max(cur.c->strainTime, last.c->strainTime) -
                                                 std::min(cur.c->strainTime, last.c->strainTime)) /
                                                    50.0,
                                                4));

    if(!std::isnan(cur.c->angle) && !std::isnan(last.c->angle)) {
        const f64 angleDifference = std::abs(cur.c->angle - last.c->angle);
        const f64 angleDifferenceAdjusted = std::sin(angleDifference / 2.0) * 180.0;
        const f64 angularVelocity = angleDifferenceAdjusted / (cur.c->strainTime * 0.1);

        // low angular velocity flow (angles are consistent) is easier to follow than erratic flow
        flowDifficulty *= 0.8 + std::sqrt(angularVelocity / 270.0);
    }

    // if all three notes are overlapping - don't reward bonuses as you don't have to do additional movement
    f64 overlappedNotesWeight = 1.0;

    if(cur.c->index > 2) {
        const f64 o1 = flow_overlap_factor(cur, last, ctx.circleRadius);
        const f64 o2 = flow_overlap_factor(cur, lastLast, ctx.circleRadius);
        const f64 o3 = flow_overlap_factor(last, lastLast, ctx.circleRadius);

        overlappedNotesWeight = 1.0 - o1 * o2 * o3;
    }

    if(!std::isnan(cur.c->angle)) {
        // acute angles are also hard to flow
        flowDifficulty += currVelocity * calc_angle_acuteness(cur.c->angle) * overlappedNotesWeight;
    }

    if(std::max(prevVelocity, currVelocity) != 0.0) {
        if(withSliderTravelDistance) {
            currVelocity = currDistance / cur.c->strainTime;
        }

        // scale with ratio of difference compared to 0.5 * max dist
        const f64 distRatio =
            smoothstep(std::abs(prevVelocity - currVelocity) / std::max(prevVelocity, currVelocity), 0.0, 1.0);

        // reward for % distance up to 125 / strainTime for overlaps where velocity is still changing
        const f64 overlapVelocityBuff =
            std::min(125.0 / std::min(cur.c->strainTime, last.c->strainTime), std::abs(prevVelocity - currVelocity));

        flowDifficulty += overlapVelocityBuff * distRatio * overlappedNotesWeight * velocity_change_multiplier;
    }

    if(cur.type == DifficultyHitObject::TYPE::SLIDER && withSliderTravelDistance) {
        // include slider velocity to make velocity more consistent with snap
        flowDifficulty += cur.c->travelDistance / cur.c->travelTime;
    }

    // raised to a power because flow difficulty scales harder with both high distance and time
    flowDifficulty = std::pow(flowDifficulty, 1.45);

    // reduce difficulty for low spacing since spacing below radius is always to be flowed
    return flowDifficulty * smootherStep(currDistance, 0.0, 50.0);
}

// https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Evaluators/Aim/AgilityEvaluator.cs
f64 evaluate_agility_of(const DifficultyHitObject &cur, const StrainEvalContext &ctx) {
    if(cur.type == DifficultyHitObject::TYPE::SPINNER) return 0.0;

    static constexpr f64 distance_cap = 100.0 * 1.2;  // 1.2 circles distance between centers

    const DifficultyHitObject *prevObj = cur.c->get_previous(0);

    const f64 travelDistance = prevObj != nullptr ? prevObj->c->lazyTravelDist : 0.0;
    const f64 distance = travelDistance + cur.c->lazyJumpDistance;

    const f64 distanceScaled = std::min(distance, distance_cap) / distance_cap;

    f64 agilityDifficulty = distanceScaled * 1000.0 / cur.c->strainTime;

    agilityDifficulty *= ctx.smallCircleBonusPow15;

    // having less time to strain-recover is harder (normalized EMA has no time scaling anymore)
    agilityDifficulty *= 1.0 / (1.0 - std::pow(0.2, cur.c->strainTime / 1000.0));

    return agilityDifficulty;
}

// P(snap) as a function of the flow : snap difficulty ratio; P(snap) + P(flow) = 1
f64 snap_flow_probability(f64 ratio) {
    static constexpr f64 k = 7.27;

    if(ratio == 0.0) return 0.0;
    if(std::isnan(ratio)) return 1.0;

    return logisticExp(-k * std::log(ratio));
}

// https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Aim.cs
// per-note aim difficulty: snap/agility/flow blended by the probability of the player
// snapping vs flowing the pattern, with per-note mod adjustments. the agility difficulty is
// passed in because it doesn't depend on withSliders (the caller evaluates both variants)
f64 evaluate_aim_difficulty_of(const DifficultyHitObject &cur, bool withSliders, f64 agilityDifficultyRaw,
                               const StrainEvalContext &ctx) {
    static constexpr f64 skill_multiplier_snap = 70.9;
    static constexpr f64 skill_multiplier_agility = 2.35;
    static constexpr f64 skill_multiplier_flow = 242.0;
    static constexpr f64 skill_multiplier_total = 1.12;
    static constexpr f64 combined_snap_norm_exponent = 1.2;

    f64 snapDifficulty = evaluate_snap_aim_of(cur, withSliders, ctx) * skill_multiplier_snap;
    const f64 agilityDifficulty = agilityDifficultyRaw * skill_multiplier_agility;
    f64 flowDifficulty = evaluate_flow_aim_of(cur, withSliders, ctx) * skill_multiplier_flow;

    // snap by itself doesn't have enough difficulty to be above flow on streams; agility
    // measures the rate of cursor velocity changes while snapping, so snapping every circle on
    // a stream requires an enormous amount of agility at which point it's easier to flow
    f64 combinedSnapDifficulty = norm(combined_snap_norm_exponent, {snapDifficulty, agilityDifficulty});

    const f64 pSnap = snap_flow_probability(flowDifficulty / combinedSnapDifficulty);
    const f64 pFlow = 1.0 - pSnap;

    if(ctx.touchDevice) {
        // agility is not adjusted since it represents TD difficulty in a decent enough way
        snapDifficulty = std::pow(snapDifficulty, 0.89);
        combinedSnapDifficulty = norm(combined_snap_norm_exponent, {snapDifficulty, agilityDifficulty});
    }

    if(ctx.relax) {
        combinedSnapDifficulty *= 0.75;
        flowDifficulty *= 0.6;
    }

    f64 totalDifficulty = (combinedSnapDifficulty * pSnap + flowDifficulty * pFlow) * skill_multiplier_total;

    // per-note OD scaling
    totalDifficulty *= ctx.odScalingAimFl;

    return totalDifficulty;
}

// https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Preprocessing/OsuDifficultyHitObject.cs
// visual opacity of obj at a point in RAW beatmap time (lazer OpacityAt; the reading windows
// use rate-adjusted times, but opacities are evaluated in the raw domain)
f64 opacity_at(const DifficultyHitObject &obj, f64 rawTime, bool hidden, const StrainEvalContext &ctx) {
    if(rawTime > (f64)obj.baseTime) {
        // consider a hitobject as being invisible when its start time is passed (approximation)
        return 0.0;
    }

    const f64 fadeInStartTime = (f64)obj.baseTime - ctx.preemptRaw;

    if(hidden) {
        // taken from OsuModHidden: the fade out starts after the fade in, over 30% of the preempt.
        // the HD beatmap transform shortens TimeFadeIn to 0.4 * preempt for everything except
        // sliders (which keep the default duration to match stable), and lazer's OpacityAt sees
        // that adjusted value here while the fade in itself still uses the unadjusted duration
        const f64 hiddenFadeIn = obj.isSlider() ? ctx.fadeInRaw : 0.4 * ctx.preemptRaw;
        const f64 fadeOutStartTime = (f64)obj.baseTime - ctx.preemptRaw + hiddenFadeIn;
        const f64 fadeOutDuration = ctx.preemptRaw * 0.3;

        return std::min(std::clamp((rawTime - fadeInStartTime) / ctx.fadeInRaw, 0.0, 1.0),
                        1.0 - std::clamp((rawTime - fadeOutStartTime) / fadeOutDuration, 0.0, 1.0));
    }

    return std::clamp((rawTime - fadeInStartTime) / ctx.fadeInRaw, 0.0, 1.0);
}

// https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Evaluators/ReadingEvaluator.cs
static constexpr f64 reading_window_size = 3000.0;                  // 3 seconds
static constexpr f64 reading_distance_influence_threshold = 150.0;  // 1.5 circles distance between centers

// nerf for objects that are very distant in time, affecting reading less
forceinline f64 reading_time_nerf(f64 deltaTime) {
    return std::clamp(2.0 - deltaTime / (reading_window_size / 2.0), 0.0, 1.0);
}

// density of objects visible at the point in time the current object needs to be clicked
f64 reading_current_visible_density(const DifficultyHitObject &cur, const StrainEvalContext &ctx) {
    f64 visibleObjectCount = 0.0;

    const DifficultyHitObject *hitObject = cur.c->get_next(0);

    while(hitObject != nullptr) {
        // object not visible at the time the current object needs to be clicked
        if((f64)(hitObject->time - cur.time) > reading_window_size ||
           (f64)cur.time < (f64)hitObject->time - ctx.preemptAdjusted)
            break;

        const f64 timeNerfFactor = reading_time_nerf((f64)(hitObject->time - cur.time));

        visibleObjectCount += opacity_at(*hitObject, (f64)cur.baseTime, false, ctx) * timeNerfFactor;

        hitObject = hitObject->c->get_next(0);
    }

    return visibleObjectCount;
}

// influence of objects that are visible on screen at the point in time the current object
// becomes visible
f64 reading_past_object_influence(const DifficultyHitObject &cur, const StrainEvalContext &ctx) {
    f64 pastObjectDifficultyInfluence = 0.0;

    for(i32 i = 0;; i++) {
        const DifficultyHitObject *loopObj = cur.c->get_previous(i);

        if(loopObj == nullptr || (f64)(cur.time - loopObj->time) > reading_window_size ||
           (f64)loopObj->time < (f64)cur.time - ctx.preemptAdjusted)  // current object not visible then
            break;

        f64 loopDifficulty = opacity_at(cur, (f64)loopObj->baseTime, false, ctx);

        // small distances mean previous objects may be cheesed, so it doesn't matter whether
        // they were arranged confusingly
        loopDifficulty *= smootherStep(loopObj->c->lazyJumpDistance, 15.0, reading_distance_influence_threshold);

        loopDifficulty *= reading_time_nerf((f64)(cur.time - loopObj->time));

        pastObjectDifficultyInfluence += loopDifficulty;
    }

    return pastObjectDifficultyInfluence;
}

// how often the current object's angle has been repeated within the last 2 seconds, with
// alternating-angle patterns weighted separately. https://www.desmos.com/calculator/eb057a4822
// NOTE: the loop terminates via the real-null previous() (a clamping accessor would never let
// the time gap grow past the first object and hang forever)
f64 reading_constant_angle_nerf(const DifficultyHitObject &cur) {
    static constexpr f64 minimum_angle_relevancy_time = 2000.0;  // 2 seconds
    static constexpr f64 maximum_angle_relevancy_time = 200.0;

    f64 constantAngleCount = 0.0;
    i32 index = 0;
    f64 currentTimeGap = 0.0;

    const DifficultyHitObject *loopObjPrev0 = &cur;
    const DifficultyHitObject *loopObjPrev1 = nullptr;
    const DifficultyHitObject *loopObjPrev2 = nullptr;

    while(currentTimeGap < minimum_angle_relevancy_time) {
        const DifficultyHitObject *loopObj = cur.c->get_previous(index);

        if(loopObj == nullptr) break;

        // account less for objects that are close to the time limit
        const f64 longIntervalFactor =
            1.0 - reverseLerp(loopObj->c->strainTime, maximum_angle_relevancy_time, minimum_angle_relevancy_time);

        if(!std::isnan(loopObj->c->angle) && !std::isnan(cur.c->angle)) {
            const f64 angleDifference = std::abs(cur.c->angle - loopObj->c->angle);
            f64 angleDifferenceAlternating = PI;

            if(!std::isnan(loopObjPrev0->c->angle) && loopObjPrev1 != nullptr && !std::isnan(loopObjPrev1->c->angle) &&
               loopObjPrev2 != nullptr && !std::isnan(loopObjPrev2->c->angle)) {
                angleDifferenceAlternating = std::abs(loopObjPrev1->c->angle - loopObj->c->angle);
                angleDifferenceAlternating += std::abs(loopObjPrev2->c->angle - loopObjPrev0->c->angle);

                f64 weight = 1.0;

                // be sure that one of the angles is very sharp, when the other is wide
                weight *= reverseLerp(std::min(loopObj->c->angle, loopObjPrev0->c->angle) * 180.0 / PI, 20.0, 5.0);
                weight *= reverseLerp(std::max(loopObj->c->angle, loopObjPrev0->c->angle) * 180.0 / PI, 60.0, 120.0);

                // lerp between max angle difference and rescaled alternating difference, with
                // more harsh scaling compared to normal difference
                angleDifferenceAlternating = std::lerp(PI, 0.1 * angleDifferenceAlternating, weight);
            }

            const f64 stackFactor = smootherStep(loopObj->c->lazyJumpDistance, 0.0, 50.0);

            constantAngleCount +=
                std::cos(3.0 * std::min(30.0 * PIOVER180,
                                        std::min(angleDifference, angleDifferenceAlternating) * stackFactor)) *
                longIntervalFactor;
        }

        currentTimeGap = (f64)(cur.time - loopObj->time);
        index++;

        loopObjPrev2 = loopObjPrev1;
        loopObjPrev1 = loopObjPrev0;
        loopObjPrev0 = loopObj;
    }

    return std::clamp(2.0 / constantAngleCount, 0.2, 1.0);
}

// per-note reading difficulty, both hidden variants in one pass (the expensive density/angle
// windows are shared, hidden only adds one extra term)
ReadingDifficultyPair evaluate_reading_difficulty_of(const DifficultyHitObject &cur, const StrainEvalContext &ctx) {
    if(cur.type == DifficultyHitObject::TYPE::SPINNER || cur.c->index == 0) return {.noHidden = 0.0, .hidden = 0.0};

    static constexpr f64 density_multiplier = 2.4;
    static constexpr f64 density_difficulty_base = 2.5;
    static constexpr f64 preempt_balancing_factor = 140000.0;
    static constexpr f64 preempt_starting_point = 500.0;  // AR 9.66 in milliseconds
    static constexpr f64 hidden_multiplier = 0.28;

    const f64 velocity = std::max(1.0, cur.c->lazyJumpDistance / cur.c->strainTime);  // only allow velocity to buff

    const f64 currentVisibleObjectDensity = reading_current_visible_density(cur, ctx);
    const f64 pastObjectDifficultyInfluence = reading_past_object_influence(cur, ctx);

    const f64 constantAngleNerfFactor = reading_constant_angle_nerf(cur);

    // note density: consider future densities too because it can make the path the cursor
    // takes less clear
    f64 noteDensityDifficulty = 0.0;
    {
        f64 futureObjectDifficultyInfluence = std::sqrt(currentVisibleObjectDensity);

        const DifficultyHitObject *nextObj = cur.c->get_next(0);
        if(nextObj != nullptr) {
            // reduce difficulty if movement to next object is small
            futureObjectDifficultyInfluence *=
                smootherStep(nextObj->c->lazyJumpDistance, 15.0, reading_distance_influence_threshold);
        }

        // value higher note densities exponentially
        noteDensityDifficulty = std::pow(pastObjectDifficultyInfluence + futureObjectDifficultyInfluence, 1.7) * 0.4 *
                                constantAngleNerfFactor * velocity;

        // award only denser than average maps
        noteDensityDifficulty = std::max(0.0, noteDensityDifficulty - density_difficulty_base);

        // apply a soft cap to general density reading to account for partial memorization
        noteDensityDifficulty = std::pow(noteDensityDifficulty, 0.45) * density_multiplier;
    }

    // preempt: arbitrary curve for the base value preempt difficulty should have as approach
    // rate increases, https://www.desmos.com/calculator/c175335a71
    const f64 preemptDifficulty = std::pow((preempt_starting_point - ctx.preemptAdjusted +
                                            std::abs(ctx.preemptAdjusted - preempt_starting_point)) /
                                               2.0,
                                           2.5) /
                                  preempt_balancing_factor * constantAngleNerfFactor * velocity;

    // hidden: time spent invisible + densities, with a perfect-stack bonus
    f64 hiddenDifficulty = 0.0;
    {
        // higher preempt means that time spent invisible is higher too, we want to reward that
        const f64 preemptFactor = std::pow(ctx.preemptAdjusted, 2.2) * 0.01;

        // account for both past and current densities
        const f64 densityFactor = std::pow(currentVisibleObjectDensity + pastObjectDifficultyInfluence, 3.3) * 3.0;

        hiddenDifficulty = (preemptFactor + densityFactor) * constantAngleNerfFactor * velocity * 0.01;

        // apply a soft cap to general HD reading to account for partial memorization
        hiddenDifficulty = std::pow(hiddenDifficulty, 0.4) * hidden_multiplier;

        const DifficultyHitObject *previousObj = cur.c->get_previous(0);  // non-null (index > 0)

        // buff perfect stacks only if the current note is completely invisible at the time you
        // click the previous note (perfect stacks are harder the less time between notes)
        if(cur.c->lazyJumpDistance == 0.0 && opacity_at(cur, (f64)previousObj->baseTime, true, ctx) == 0.0 &&
           (f64)previousObj->time > (f64)cur.time - ctx.preemptAdjusted)
            hiddenDifficulty += hidden_multiplier * 2500.0 / std::pow(cur.c->strainTime, 1.5);
    }

    // having less time to process information is harder
    const f64 highBpmBonus = 1.0 / (1.0 - std::pow(0.8, cur.c->strainTime / 1000.0));

    return {.noHidden = norm(1.5, {preemptDifficulty, 0.0, noteDensityDifficulty}) * highBpmBonus,
            .hidden = norm(1.5, {preemptDifficulty, hiddenDifficulty, noteDensityDifficulty}) * highBpmBonus};
}

// https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Evaluators/FlashlightEvaluator.cs
// stacked end position in real osu!pixels (lazer StackedEndPosition; for deferred-alloc maps a
// far-back slider's curve may already be freed, fall back to its head like the other curve users)
vec2 flashlight_end_position(const DifficultyHitObject &obj) {
    if(obj.type != DifficultyHitObject::TYPE::SLIDER || obj.curve == nullptr) return obj.pos;
    return obj.curvePointAt(obj.repeats % 2 ? 1.0f : 0.0f);
}

// per-note flashlight difficulty, both hidden variants in one pass (only the opacity bonus and
// the flat hidden multiplier differ)
FlashlightDifficultyPair evaluate_flashlight_difficulty_of(const DifficultyHitObject &cur,
                                                           const StrainEvalContext &ctx) {
    if(cur.type == DifficultyHitObject::TYPE::SPINNER) return {.noHidden = 0.0, .hidden = 0.0};

    static constexpr f64 max_opacity_bonus = 0.4;
    static constexpr f64 hidden_bonus = 0.2;
    static constexpr f64 min_velocity = 0.5;
    static constexpr f64 slider_multiplier = 1.3;
    static constexpr f64 min_angle_multiplier = 0.2;

    const f64 scalingFactor = 52.0 / ctx.circleRadius;

    f64 smallDistNerf = 1.0;
    f64 cumulativeStrainTime = 0.0;

    f64 difficultyNoHidden = 0.0;
    f64 difficultyHidden = 0.0;

    const DifficultyHitObject *lastObj = &cur;

    f64 angleRepeatCount = 0.0;

    // iterating backwards in time from the current object
    const i32 lookback = std::min(cur.c->index, 10);
    for(i32 i = 0; i < lookback; i++) {
        const DifficultyHitObject *currentObj = cur.c->get_previous(i);

        cumulativeStrainTime += lastObj->c->strainTime;

        if(currentObj->type != DifficultyHitObject::TYPE::SPINNER) {
            const f64 jumpDistance = vec::distance(cur.pos, flashlight_end_position(*currentObj));

            // objects that can be easily seen within the flashlight circle radius are nerfed
            if(i == 0) smallDistNerf = std::min(1.0, jumpDistance / 75.0);

            // nerf stacks so that only the first object of the stack is accounted for
            const f64 stackNerf = std::min(1.0, (currentObj->c->lazyJumpDistance / scalingFactor) / 25.0);

            const f64 basePart = stackNerf * scalingFactor * jumpDistance / cumulativeStrainTime;

            // bonus based on how visible the object is
            difficultyNoHidden +=
                (1.0 + max_opacity_bonus * (1.0 - opacity_at(cur, (f64)currentObj->baseTime, false, ctx))) * basePart;
            difficultyHidden +=
                (1.0 + max_opacity_bonus * (1.0 - opacity_at(cur, (f64)currentObj->baseTime, true, ctx))) * basePart;

            if(!std::isnan(currentObj->c->angle) && !std::isnan(cur.c->angle)) {
                // objects further back in time should count less for the nerf
                if(std::abs(currentObj->c->angle - cur.c->angle) < 0.02)
                    angleRepeatCount += std::max(1.0 - 0.1 * i, 0.0);
            }
        }

        lastObj = currentObj;
    }

    difficultyNoHidden = powi(smallDistNerf * difficultyNoHidden, 2);
    difficultyHidden = powi(smallDistNerf * difficultyHidden, 2);

    // additional bonus for hidden due to there being no approach circles
    difficultyHidden *= 1.0 + hidden_bonus;

    // nerf patterns with repeated angles
    const f64 angleNerf = min_angle_multiplier + (1.0 - min_angle_multiplier) / (angleRepeatCount + 1.0);
    difficultyNoHidden *= angleNerf;
    difficultyHidden *= angleNerf;

    f64 sliderBonus = 0.0;

    if(cur.type == DifficultyHitObject::TYPE::SLIDER) {
        // invert the scaling factor to determine the true travel distance independent of circle size
        const f64 pixelTravelDistance = cur.c->lazyTravelDist / scalingFactor;

        // reward sliders based on velocity
        sliderBonus = std::pow(std::max(0.0, pixelTravelDistance / cur.c->travelTime - min_velocity), 0.5);

        // longer sliders require more memorisation
        sliderBonus *= pixelTravelDistance;

        // nerf sliders with repeats, as less memorisation is required
        if(cur.repeats - 1 > 0) sliderBonus /= (f64)cur.repeats;  // == lazer RepeatCount + 1
    }

    difficultyNoHidden += sliderBonus * slider_multiplier;
    difficultyHidden += sliderBonus * slider_multiplier;

    return {.noHidden = difficultyNoHidden, .hidden = difficultyHidden};
}

}  // namespace

namespace {

void calculateScoreV1Attributes(DifficultyAttributes &attributes, const BeatmapDiffcalcData &b, i32 upToObjectIndex) {
    // Nested score per object
    constexpr f64 bigTickScore = 30;
    constexpr f64 smallTickScore = 10;

    f64 sliderScore = 0;
    f64 spinnerScore = 0;

    if(upToObjectIndex < 1) upToObjectIndex = (i32)b.sortedHitObjects.size();

    for(i32 i = 0; i < upToObjectIndex; i++) {
        const auto &hitObject = b.sortedHitObjects[i];

        if(hitObject.type == DifficultyHitObject::TYPE::SLIDER) {
            // 1 for head, 1 for tail
            i32 amountOfBigTicks = 2;

            // Add slider repeats
            amountOfBigTicks += hitObject.repeats - 1;

            i32 amountOfSmallTicks = 0;

            // Slider ticks
            for(const auto &nestedHitObject : hitObject.scoringTimes) {
                if(nestedHitObject.type == SLIDER_SCORING_TIME::TYPE::TICK) amountOfSmallTicks++;
            }

            sliderScore += amountOfBigTicks * bigTickScore + amountOfSmallTicks * smallTickScore;
        } else if(hitObject.type == DifficultyHitObject::TYPE::SPINNER)
            spinnerScore += calculateScoreV1SpinnerScore(hitObject.baseEndTime - hitObject.baseTime);
    }

    attributes.NestedScorePerObject = (sliderScore + spinnerScore) / (f64)std::max(upToObjectIndex, 1);

    // Legacy score base multiplier
    const u32 breakTimeMS = b.breakDuration;
    const u32 drainLength = std::max(b.playableLength - std::min(breakTimeMS, b.playableLength), (u32)1000) / 1000;
    attributes.LegacyScoreBaseMultiplier = (i32)std::round(
        (b.CS + b.HP + b.OD + std::clamp<f32>((f32)b.sortedHitObjects.size() / (f32)drainLength * 8.0f, 0.0f, 16.0f)) /
        38.0f * 5.0f);

    // Maximum combo score
    const f64 score_increase = 300.;
    u32 combo = 0;
    attributes.MaximumLegacyComboScore = 0;

    for(i32 i = 0; i < upToObjectIndex; i++) {
        const auto &hitObject = b.sortedHitObjects[i];

        if(hitObject.type == DifficultyHitObject::TYPE::SLIDER) {
            // Increase combo for each nested object
            combo += hitObject.scoringTimes.size();

            // For sliders we need to increase combo BEFORE giving score
            combo++;
        }

        if(combo > 0) {
            attributes.MaximumLegacyComboScore +=
                (u32)(combo * (score_increase / 25. * attributes.LegacyScoreBaseMultiplier));
        }

        // We have already increased combo for slider
        if(hitObject.type != DifficultyHitObject::TYPE::SLIDER) combo++;
    }
}

f64 calculateScoreV1SpinnerScore(f64 spinnerDuration) {
    const i32 spin_score = 100;
    const i32 bonus_spin_score = 1000;

    // The spinner object applies a lenience because gameplay mechanics differ from osu-stable.
    // We'll redo the calculations to match osu-stable here...
    const f64 maximum_rotations_per_second = 477.0 / 60;

    // Normally, this value depends on the final overall difficulty. For simplicity, we'll only consider the worst case that maximises bonus score.
    // As we're primarily concerned with computing the maximum theoretical final score,
    // this will have the final effect of slightly underestimating bonus score achieved on stable when converting from score V1.
    const f64 minimum_rotations_per_second = 3.0;

    f64 secondsDuration = spinnerDuration / 1000.0;

    // The total amount of half spins possible for the entire spinner.
    i32 totalHalfSpinsPossible = (i32)(secondsDuration * maximum_rotations_per_second * 2);
    // The amount of half spins that are required to successfully complete the spinner (i.e. get a 300).
    i32 halfSpinsRequiredForCompletion = (i32)(secondsDuration * minimum_rotations_per_second);
    // To be able to receive bonus poi32s, the spinner must be rotated another 1.5 times.
    i32 halfSpinsRequiredBeforeBonus = halfSpinsRequiredForCompletion + 3;

    i32 score = 0;

    i32 fullSpins = (totalHalfSpinsPossible / 2);

    // Normal spin score
    score += spin_score * fullSpins;

    i32 bonusSpins = (totalHalfSpinsPossible - halfSpinsRequiredBeforeBonus) / 2;

    // Reduce amount of bonus spins because we want to represent the more average case, rather than the best one.
    bonusSpins = std::max(0, bonusSpins - fullSpins / 2);
    score += bonus_spin_score * bonusSpins;

    return score;
}

// https://github.com/ppy/osu-performance/blob/master/src/performance/osu/OsuScore.cpp
// https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/OsuPerformanceCalculator.cs

f64 computeAimValue(const ScoreData &score, const DifficultyAttributes &attributes, f64 effectiveMissCount,
                    f64 aimEstimatedSliderBreaks) {
    if(flags::has<ModFlags::Autopilot>(score.modFlags)) return 0.0;

    f64 aimDifficulty = attributes.AimDifficulty;

    // McOsu doesn't track dropped slider ends, so the ScoreV2/lazer case can't be handled here
    if(attributes.SliderCount > 0 && attributes.AimDifficultSliderCount > 0) {
        i32 maximumPossibleDroppedSliders = score.countOk + score.countMeh + score.countMiss;
        f64 estimateImproperlyFollowedDifficultSliders =
            std::clamp<f64>((f64)std::min(maximumPossibleDroppedSliders, score.beatmapMaxCombo - score.scoreMaxCombo),
                            0.0, attributes.AimDifficultSliderCount);
        f64 sliderNerfFactor =
            (1.0 - attributes.SliderFactor) *
                std::pow(1.0 - estimateImproperlyFollowedDifficultSliders / attributes.AimDifficultSliderCount, 3.0) +
            attributes.SliderFactor;
        aimDifficulty *= sliderNerfFactor;
    }

    f64 aimValue = difficultyToPerformance(aimDifficulty);

    // length bonus
    f64 lengthBonus = 0.95 + 0.35 * std::min(1.0, ((f64)score.totalHits / 2000.0)) +
                      (score.totalHits > 2000 ? std::log10(((f64)score.totalHits / 2000.0)) * 0.5 : 0.0);
    aimValue *= lengthBonus;

    // miss penalty
    if(effectiveMissCount > 0) {
        f64 relevantMissCount = std::min(effectiveMissCount + aimEstimatedSliderBreaks,
                                         (f64)(score.countOk + score.countMeh + score.countMiss));
        aimValue *= calculateMissPenalty(relevantMissCount, attributes.AimDifficultStrainCount);
    }

    // scale aim with acc
    aimValue *= score.accuracy;

    return aimValue;
}

f64 computeSpeedValue(const ScoreData &score, const DifficultyAttributes &attributes, f64 effectiveMissCount,
                      f64 speedEstimatedSliderBreaks, f64 speedDeviation) {
    if((flags::has<ModFlags::Relax>(score.modFlags)) || std::isnan(speedDeviation)) return 0.0;

    f64 speedValue = difficultyToPerformance(attributes.SpeedDifficulty);

    // miss penalty
    if(effectiveMissCount > 0) {
        f64 relevantMissCount = std::min(effectiveMissCount + speedEstimatedSliderBreaks,
                                         (f64)(score.countOk + score.countMeh + score.countMiss));
        speedValue *= calculateMissPenalty(relevantMissCount, attributes.SpeedDifficultStrainCount);
    }

    f64 speedHighDeviationMultiplier = calculateSpeedHighDeviationNerf(attributes, speedDeviation);
    speedValue *= speedHighDeviationMultiplier;

    // an effective hit window is created based on the speed SR: the higher the speed
    // difficulty, the shorter the hit window (e.g. speed SR 4.0 -> 20ms, which is OD 10)
    f64 effectiveHitWindow = 20.0 * std::pow(4.0 / attributes.SpeedDifficulty, 0.35);

    // the proportion of 300s on speed notes assuming the hit window was the effective one
    f64 effectiveAccuracy = erf(effectiveHitWindow / speedDeviation);

    // scale speed value by normalized accuracy
    speedValue *= powi(effectiveAccuracy, 2);

    // singletap buff (XXX: might be too weak)
    if(flags::has<ModFlags::Singletap>(score.modFlags)) {
        speedValue *= 1.25;
    }

    // no keylock nerf (XXX: might be too harsh)
    if(flags::has<ModFlags::NoKeylock>(score.modFlags)) {
        speedValue *= 0.5;
    }

    // DKS nerf (XXX: might be too harsh/weak)
    if(flags::has<ModFlags::DKS>(score.modFlags)) {
        speedValue *= 0.5;
    }

    return speedValue;
}

f64 computeAccuracyValue(const ScoreData &score, const DifficultyAttributes &attributes) {
    if(flags::has<ModFlags::Relax>(score.modFlags)) return 0.0;

    f64 betterAccuracyPercentage =
        score.amountHitObjectsWithAccuracy > 0
            ? ((f64)(score.countGreat - std::max(score.totalHits - score.amountHitObjectsWithAccuracy, 0)) * 6.0 +
               (score.countOk * 2.0) + score.countMeh) /
                  (f64)(score.amountHitObjectsWithAccuracy * 6.0)
            : 0.0;

    // it's possible to reach negative accuracy, cap at zero
    if(betterAccuracyPercentage < 0.0) betterAccuracyPercentage = 0.0;

    // arbitrary values tom crafted out of trial and error
    f64 accuracyValue =
        std::pow(1.52163, attributes.OverallDifficulty) * std::pow(betterAccuracyPercentage, 24.0) * 2.83;

    // bonus for many hitcircles: it's harder to keep good accuracy up for longer
    // (the HD and FL accuracy bonuses moved into the reading/cognition values)
    accuracyValue *= score.amountHitObjectsWithAccuracy < 1000
                         ? std::pow(score.amountHitObjectsWithAccuracy / 1000.0, 0.3)
                         : std::pow(score.amountHitObjectsWithAccuracy / 1000.0, 0.1);

    return accuracyValue;
}

f64 computeReadingValue(const ScoreData &score, const DifficultyAttributes &attributes, f64 effectiveMissCount,
                        f64 aimEstimatedSliderBreaks) {
    f64 readingValue = difficultyToPerformance(attributes.ReadingDifficulty);

    if(effectiveMissCount > 0)
        readingValue *=
            calculateMissPenalty(effectiveMissCount + aimEstimatedSliderBreaks, attributes.ReadingDifficultNoteCount);

    // scale the reading value with accuracy _harshly_
    readingValue *= powi(score.accuracy, 3);

    return readingValue;
}

f64 computeFlashlightValue(const ScoreData &score, const DifficultyAttributes &attributes, f64 effectiveMissCount) {
    if(!flags::has<ModFlags::Flashlight>(score.modFlags)) return 0.0;

    f64 flashlightValue = flashlightDifficultyToPerformance(attributes.FlashlightDifficulty);

    // penalize misses by assessing # of misses relative to the total # of objects, with a
    // default 3% reduction for any # of misses
    if(effectiveMissCount > 0)
        flashlightValue *= 0.97 * std::pow(1.0 - std::pow(effectiveMissCount / (f64)score.totalHits, 0.775),
                                           std::pow(effectiveMissCount, 0.875));

    // combo scaling
    if(score.beatmapMaxCombo > 0)
        flashlightValue *=
            std::min(std::pow((f64)score.scoreMaxCombo, 0.8) / std::pow((f64)score.beatmapMaxCombo, 0.8), 1.0);

    // scale the flashlight value with accuracy _slightly_
    flashlightValue *= 0.5 + score.accuracy / 2.0;

    return flashlightValue;
}

f64 calculateEstimatedSliderBreaks(const ScoreData &score, f64 topWeightedSliderFactor, f64 effectiveMissCount) {
    const i32 nonMissMistakes = score.countOk + score.countMeh;

    if(nonMissMistakes == 0 || score.beatmapMaxCombo < 1) return 0.0;

    f64 missedComboPercent = 1.0 - (f64)score.scoreMaxCombo / score.beatmapMaxCombo;
    f64 estimatedSliderBreaks = std::min((f64)nonMissMistakes, effectiveMissCount * topWeightedSliderFactor);

    // Scores with more Oks and Mehs are more likely to have slider breaks.
    // An arbitrary value is added to both sides of the division to make it more stable on extreme ends.
    f64 nonMissMistakeAdjustment = (nonMissMistakes - estimatedSliderBreaks + 4.5) / (nonMissMistakes + 4.0);

    // There is a low probability of extra slider breaks on effective miss counts close to 1, as score based calculations are good at indicating if only a single break occurred.
    estimatedSliderBreaks *= smoothstep(effectiveMissCount, 1.0, 2.0);

    return estimatedSliderBreaks * nonMissMistakeAdjustment * logistic(missedComboPercent, 0.33, 15.0);
}

f64 calculateSpeedDeviation(const ScoreData &score, const DifficultyAttributes &attributes, f64 timescale) {
    if(score.countGreat + score.countOk + score.countMeh == 0) return std::numeric_limits<f64>::quiet_NaN();

    f64 speedNoteCount = attributes.SpeedNoteCount;
    speedNoteCount += (score.totalHits - attributes.SpeedNoteCount) * 0.1;

    f64 relevantCountMiss = std::min((f64)score.countMiss, speedNoteCount);
    f64 relevantCountMeh = std::min((f64)score.countMeh, speedNoteCount - relevantCountMiss);
    f64 relevantCountOk = std::min((f64)score.countOk, speedNoteCount - relevantCountMiss - relevantCountMeh);
    f64 relevantCountGreat = std::max(0.0, speedNoteCount - relevantCountMiss - relevantCountMeh - relevantCountOk);

    return calculateDeviation(attributes, timescale, relevantCountGreat, relevantCountOk, relevantCountMeh);
}

f64 calculateDeviation(const DifficultyAttributes &attributes, f64 timescale, f64 relevantCountGreat,
                       f64 relevantCountOk, f64 relevantCountMeh) {
    if(relevantCountGreat + relevantCountOk + relevantCountMeh <= 0.0) return std::numeric_limits<f64>::quiet_NaN();

    const f64 greatHitWindow =
        adjustHitWindow(GameRules::odTo300HitWindowMS((f32)attributes.OverallDifficulty)) / timescale;
    const f64 okHitWindow =
        adjustHitWindow(GameRules::odTo100HitWindowMS((f32)attributes.OverallDifficulty)) / timescale;
    const f64 mehHitWindow =
        adjustHitWindow(GameRules::odTo50HitWindowMS((f32)attributes.OverallDifficulty)) / timescale;

    static constexpr const f64 z = 2.32634787404;
    static constexpr const f64 sqrt2 = std::numbers::sqrt2;
    static constexpr const f64 sqrt3 = std::numbers::sqrt3;
    static constexpr const f64 sqrt2OverPi = 0.7978845608028654;

    f64 n = std::max(1.0, relevantCountGreat + relevantCountOk);
    f64 p = relevantCountGreat / n;
    f64 pLowerBound =
        std::min(p, (n * p + z * z / 2.0) / (n + z * z) - z / (n + z * z) * sqrt(n * p * (1.0 - p) + z * z / 4.0));

    f64 deviation;  // NOLINT(cppcoreguidelines-init-variables)

    if(pLowerBound > 0.01) {
        deviation = greatHitWindow / (sqrt2 * erfInv(pLowerBound));
        f64 okHitWindowTailAmount = sqrt2OverPi * okHitWindow *
                                    std::exp(-0.5 * std::pow(okHitWindow / deviation, 2.0)) /
                                    (deviation * erf(okHitWindow / (sqrt2 * deviation)));
        deviation *= std::sqrt(1.0 - okHitWindowTailAmount);
    } else
        deviation = okHitWindow / sqrt3;

    f64 mehVariance = (mehHitWindow * mehHitWindow + okHitWindow * mehHitWindow + okHitWindow * okHitWindow) / 3.0;
    return std::sqrt(
        ((relevantCountGreat + relevantCountOk) * std::pow(deviation, 2.0) + relevantCountMeh * mehVariance) /
        (relevantCountGreat + relevantCountOk + relevantCountMeh));
}

f64 calculateSpeedHighDeviationNerf(const DifficultyAttributes &attributes, f64 speedDeviation) {
    if(std::isnan(speedDeviation)) return 0.0;

    f64 speedValue = difficultyToPerformance(attributes.SpeedDifficulty);
    f64 excessSpeedDifficultyCutoff = 100.0 + 220.0 * std::pow(22.0 / speedDeviation, 6.5);
    if(speedValue <= excessSpeedDifficultyCutoff) return 1.0;

    const f64 scale = 50.0;
    f64 adjustedSpeedValue = scale * (std::log((speedValue - excessSpeedDifficultyCutoff) / scale + 1.0) +
                                      excessSpeedDifficultyCutoff / scale);
    f64 lerpVal = 1.0 - std::clamp<f64>((speedDeviation - 22.0) / (27.0 - 22.0), 0.0, 1.0);
    adjustedSpeedValue = std::lerp(adjustedSpeedValue, speedValue, lerpVal);

    return adjustedSpeedValue / speedValue;
}

f64 calculateScoreBasedMisscount(const DifficultyAttributes &attributes, const ScoreData &score, f64 timescale,
                                 bool isMcOsuImported) {
    // (scorev2 never reaches this path, the caller gates on it)
    if(score.beatmapMaxCombo == 0) return 0;

    f64 modMultiplier = getScoreV1ScoreMultiplier(score.modFlags, timescale, isMcOsuImported);
    f64 scoreV1Multiplier = attributes.LegacyScoreBaseMultiplier * modMultiplier;
    f64 relevantComboPerObject = calculateRelevantScoreComboPerObject(attributes, score);

    f64 maximumMissCount = calculateMaximumComboBasedMissCount(attributes, score);

    f64 scoreObtainedDuringMaxCombo =
        calculateScoreAtCombo(attributes, score, score.scoreMaxCombo, relevantComboPerObject, scoreV1Multiplier);

    f64 remainingScore = (f64)score.legacyTotalScore - scoreObtainedDuringMaxCombo;

    if(remainingScore <= 0) return maximumMissCount;

    f64 remainingCombo = score.beatmapMaxCombo - score.scoreMaxCombo;
    f64 expectedRemainingScore =
        calculateScoreAtCombo(attributes, score, remainingCombo, relevantComboPerObject, scoreV1Multiplier);

    f64 scoreBasedMissCount = expectedRemainingScore / remainingScore;

    // If there's less then one miss detected - let combo-based miss count decide if this is FC or not
    scoreBasedMissCount = std::max(scoreBasedMissCount, 1.0);

    // Cap result by very harsh version of combo-based miss count
    return std::min(scoreBasedMissCount, maximumMissCount);
}

f64 calculateScoreAtCombo(const DifficultyAttributes &attributes, const ScoreData &score, f64 combo,
                          f64 relevantComboPerObject, f64 scoreV1Multiplier) {
    i32 countGreat = score.countGreat;
    i32 countOk = score.countOk;
    i32 countMeh = score.countMeh;
    i32 countMiss = score.countMiss;

    i32 totalHits = countGreat + countOk + countMeh + countMiss;

    f64 estimatedObjects = (combo / relevantComboPerObject) - 1.0;

    // The combo portion of ScoreV1 follows arithmetic progression
    // Therefore, we calculate the combo portion of score using the combo per object and our current combo.
    f64 comboScore = relevantComboPerObject > 0.0
                         ? (2.0 * (relevantComboPerObject - 1.0) + (estimatedObjects - 1.0) * relevantComboPerObject) *
                               estimatedObjects / 2.0
                         : 0.0;

    // We then apply the accuracy and ScoreV1 multipliers to the resulting score.
    comboScore *= score.accuracy * 300.0 / 25.0 * scoreV1Multiplier;

    f64 objectsHit = (totalHits - countMiss) * combo / score.beatmapMaxCombo;

    // Score also has a non-combo portion we need to create the final score value.
    f64 nonComboScore = (300.0 + attributes.NestedScorePerObject) * score.accuracy * objectsHit;

    return comboScore + nonComboScore;
}

f64 calculateRelevantScoreComboPerObject(const DifficultyAttributes &attributes, const ScoreData &score) {
    f64 comboScore = attributes.MaximumLegacyComboScore;

    // We then reverse apply the ScoreV1 multipliers to get the raw value.
    comboScore /= 300.0 / 25.0 * attributes.LegacyScoreBaseMultiplier;

    // Reverse the arithmetic progression to work out the amount of combo per object based on the score.
    f64 result = (f64)((score.beatmapMaxCombo - 2) * score.beatmapMaxCombo);
    result /= std::max((f64)score.beatmapMaxCombo + 2.0 * (comboScore - 1.0), 1.0);

    return result;
}

f64 calculateMaximumComboBasedMissCount(const DifficultyAttributes &attributes, const ScoreData &score) {
    i32 scoreMissCount = score.countMiss;

    if(attributes.SliderCount <= 0) return scoreMissCount;

    i32 countOk = score.countOk;
    i32 countMeh = score.countMeh;

    i32 totalImperfectHits = countOk + countMeh + scoreMissCount;

    f64 missCount = 0;

    // If sliders in the map are hard - it's likely for player to drop sliderends
    // If map has easy sliders - it's more likely for player to sliderbreak
    f64 likelyMissedSliderendPortion = 0.04 + 0.06 * powi(std::min(attributes.AimTopWeightedSliderFactor, 1.0), 2);

    // Consider that full combo is maximum combo minus dropped slider tails since they don't contribute to combo but also don't break it
    // In classic scores we can't know the amount of dropped sliders so we estimate it
    f64 fullComboThreshold =
        score.beatmapMaxCombo -
        std::min(4.0 + likelyMissedSliderendPortion * attributes.SliderCount, (f64)attributes.SliderCount);

    if(score.scoreMaxCombo < fullComboThreshold)
        missCount = std::pow(fullComboThreshold / std::max(1, score.scoreMaxCombo), 2.5);

    // In classic scores there can't be more misses than a sum of all non-perfect judgements
    missCount = std::min(missCount, (f64)totalImperfectHits);

    // Every slider has *at least* 2 combo attributed in classic mechanics.
    // If they broke on a slider with a tick, then this still works since they would have lost at least 2 combo (the tick and the end)
    // Using this as a max means a score that loses 1 combo on a map can't possibly have been a slider break.
    // It must have been a slider end.
    i32 maxPossibleSliderBreaks = std::min(attributes.SliderCount, (score.beatmapMaxCombo - score.scoreMaxCombo) / 2);

    f64 sliderBreaks = missCount - scoreMissCount;

    if(sliderBreaks > maxPossibleSliderBreaks) missCount = scoreMissCount + maxPossibleSliderBreaks;

    return missCount;
}

// https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/OsuDifficultyCalculator.cs

f64 calculateAimDifficultyRating(f64 difficultyValue) { return std::pow(difficultyValue, 0.63) * 0.02275; }

f64 calculateDifficultyRating(f64 difficultyValue) {
    const f64 difficulty_multiplier = 0.0675;
    return std::sqrt(difficultyValue) * difficulty_multiplier;
}

f64 sumCognitionDifficulty(f64 reading, f64 flashlight) {
    if(reading <= 0.0) return flashlight;
    if(flashlight <= 0.0) return reading;

    // Nerf flashlight value in cognition sum when reading is greater than flashlight
    return norm(1.1, {reading, flashlight * std::clamp(flashlight / reading, 0.25, 1.0)});
}

f64 calculateStarRatingFromRatings(f64 aimRating, f64 speedRating, f64 readingRating, f64 flashlightRating) {
    const f64 baseAimPerformance = difficultyToPerformance(aimRating);
    const f64 baseSpeedPerformance = difficultyToPerformance(speedRating);
    const f64 baseReadingPerformance = difficultyToPerformance(readingRating);
    const f64 baseFlashlightPerformance = flashlightDifficultyToPerformance(flashlightRating);
    const f64 baseCognitionPerformance = sumCognitionDifficulty(baseReadingPerformance, baseFlashlightPerformance);

    const f64 basePerformance = norm(1.1, {baseAimPerformance, baseSpeedPerformance, baseCognitionPerformance});

    return std::cbrt(basePerformance * performance_base_multiplier);
}

f64 adjustOverallDifficultyByClockRate(f64 OD, f64 clockRate) {
    return (79.5 - (adjustHitWindow(GameRules::odTo300HitWindowMS((f32)OD)) / clockRate)) / 6.0;
}

f64 erf(f64 x) {
    switch(std::fpclassify(x)) {
        case FP_INFINITE:
            return (x > 0) ? 1.0 : -1.0;
        case FP_NAN:
            return std::numeric_limits<f64>::quiet_NaN();
        case FP_ZERO:
            return 0.0;
        default:
            break;
    }

    // Constants for approximation (Abramowitz and Stegun formula 7.1.26)
    f64 t = 1.0 / (1.0 + 0.3275911 * std::abs(x));
    f64 tau = t * (0.254829592 + t * (-0.284496736 + t * (1.421413741 + t * (-1.453152027 + t * 1.061405429))));

    f64 erf = 1.0 - tau * std::exp(-x * x);

    return x >= 0 ? erf : -erf;
}

f64 erfInv(f64 x) {
    if(x == 0.0)
        return 0.0;
    else if(x >= 1.0)
        return std::numeric_limits<f64>::infinity();
    else if(x <= -1.0)
        return -std::numeric_limits<f64>::infinity();

    static constexpr const f64 a = 0.147;
    f64 sgn = (x > 0.0) ? 1.0 : (x < 0.0 ? -1.0 : 0.0);
    x = std::fabs(x);

    f64 ln = std::log(1.0 - x * x);
    f64 t1 = 2.0 / (PI * a) + ln / 2.0;
    f64 t2 = ln / a;
    f64 baseApprox = std::sqrt(t1 * t1 - t2) - t1;

    // Correction reduces max error from -0.005 to -0.00045.
    f64 c = (x >= 0.85) ? std::pow((x - 0.85) / 0.293, 8.0) : 0.0;

    f64 erfInv = sgn * (std::sqrt(baseApprox) + c);
    return erfInv;
}
}  // namespace
}  // namespace neomod::DiffCalc
