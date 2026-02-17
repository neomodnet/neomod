// Copyright (c) 2019, PG & Francesco149 & Khangaroo & Givikap120, All rights reserved.
#include "DifficultyCalculator.h"

#include "SliderCurves.h"
#include "GameRules.h"
#include "ModFlags.h"

#include <numbers>
#include <utility>
#include <cstring>

#ifndef BUILD_TOOLS_ONLY
#include "OsuConVars.h"
#define STARS_SLIDER_CURVE_POINTS_SEPARATION cv::stars_slider_curve_points_separation.getFloat()
#define IGNORE_CLAMPED_SLIDERS cv::stars_ignore_clamped_sliders.getBool()
#define SLIDER_CURVE_MAX_LENGTH cv::slider_curve_max_length.getFloat()
#define SLIDER_END_INSIDE_CHECK_OFFSET (f64) cv::slider_end_inside_check_offset.getInt()

#define FORMAT_STRING_ fmt::format

#define WANT_SPREADSORT
#include "Sorting.h"

#define SPREADSORT_RANGE srt::spreadsort

#else

#define STARS_SLIDER_CURVE_POINTS_SEPARATION 20.f
#define IGNORE_CLAMPED_SLIDERS true
#define SLIDER_CURVE_MAX_LENGTH 32768.f
#define SLIDER_END_INSIDE_CHECK_OFFSET 36.

#include <print>
#include <algorithm>
#define FORMAT_STRING_ std::format

#define SPREADSORT_RANGE std::ranges::sort

#endif

namespace DiffCalc {
// NOTE: bumped version from 20251007 because of a bug in the first implementation with mcosu-imported scores
const u32 PP_ALGORITHM_VERSION{20251008};
}  // namespace DiffCalc

DifficultyHitObject::DifficultyHitObject(TYPE type, vec2 pos, i32 time) : DifficultyHitObject(type, pos, time, time) {}

DifficultyHitObject::DifficultyHitObject(TYPE type, vec2 pos, i32 time, i32 endTime)
    : DifficultyHitObject(type, pos, time, endTime, 0.0f, '\0', std::vector<vec2>(), 0.0f,
                          std::vector<SLIDER_SCORING_TIME>(), 0, true) {}

DifficultyHitObject::DifficultyHitObject(TYPE type, vec2 pos, i32 time, i32 endTime, f32 spanDuration,
                                         i8 osuSliderCurveType, const std::vector<vec2> &controlPoints, f32 pixelLength,
                                         std::vector<SLIDER_SCORING_TIME> scoringTimes, i32 repeats,
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
      scheduledCurveAllocStackOffset(0.f),
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

            this->curve = SliderCurve::createCurve(this->osuSliderCurveType, controlPoints, this->pixelLength,
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
    this->scheduledCurveAllocStackOffset = dobj.scheduledCurveAllocStackOffset;
    this->repeats = dobj.repeats;

    this->stack = dobj.stack;

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
    this->scheduledCurveAllocStackOffset = dobj.scheduledCurveAllocStackOffset;

    this->scoringTimes = std::move(dobj.scoringTimes);
    this->scheduledCurveAllocControlPoints = std::move(dobj.scheduledCurveAllocControlPoints);
    this->curve = std::move(dobj.curve);

    return *this;
}

void DifficultyHitObject::updateStackPosition(f32 stackOffset) {
    scheduledCurveAllocStackOffset = stackOffset;

    pos = originalPos - vec2(stack * stackOffset, stack * stackOffset);

    updateCurveStackPosition(stackOffset);
}

void DifficultyHitObject::updateCurveStackPosition(f32 stackOffset) {
    if(curve != nullptr) curve->updateStackPosition(stack * stackOffset, false);
}

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
            return curve->originalPointAt(0.0f);
        else if(pos >= endTime) {
            if(repeats % 2 == 0)
                return curve->originalPointAt(0.0f);
            else
                return curve->originalPointAt(1.0f);
        } else
            return curve->originalPointAt(getT(pos, false));
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

f64 DifficultyCalculator::calculateStarDiffForHitObjects(StarCalcParams &params) {
    // NOTE: upToObjectIndex is applied way below, during the construction of the 'dobjects'

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
    f64 smallCircleBonus = std::max(1.0, 1.0 + (30.0 - circleRadiusInOsuPixels) / 40.0);

    // ****************************************************************************************************************************************** //

    // see https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Preprocessing/OsuDifficultyHitObject.cs

    class DistanceCalc {
       public:
        static void computeSliderCursorPosition(DiffObject &slider, f32 circleRadius) {
            if(slider.lazyCalcFinished || slider.ho->curve == nullptr) return;

            // NOTE: lazer won't load sliders above a certain length, but mcosu will
            // this isn't entirely accurate to how lazer does it (as that skips loading the object entirely),
            // but this is a good middle ground for maps that aren't completely aspire and still have relatively normal star counts on lazer
            // see: DJ Noriken - Stargazer feat. YUC'e (PSYQUI Remix) (Hishiro Chizuru) [Starg-Azer isn't so great? Are you kidding me?]
            if(IGNORE_CLAMPED_SLIDERS) {
                if(slider.ho->curve->getPixelLength() >= SLIDER_CURVE_MAX_LENGTH) return;
            }

            // NOTE: although this looks like a duplicate of the end tick time, this really does have a noticeable impact on some maps due to precision issues
            // see: Ocelot - KAEDE (Hollow Wings) [EX EX]
            const f64 tailLeniency = SLIDER_END_INSIDE_CHECK_OFFSET;
            const f64 totalDuration = (f64)slider.ho->spanDuration * slider.ho->repeats;
            f64 trackingEndTime = (f64)slider.ho->time + std::max(totalDuration - tailLeniency, totalDuration / 2.0);

            // NOTE: lazer has logic to reorder the last slider tick if it happens after trackingEndTime here, which already happens in mcosu

            slider.lazyTravelTime = trackingEndTime - (f64)slider.ho->time;

            f64 endTimeMin = slider.lazyTravelTime / slider.ho->spanDuration;
            if(std::fmod(endTimeMin, 2.0) >= 1.0)
                endTimeMin = 1.0 - std::fmod(endTimeMin, 1.0);
            else
                endTimeMin = std::fmod(endTimeMin, 1.0);

            slider.lazyEndPos = slider.ho->curve->pointAt(endTimeMin);

            vec2 cursor_pos = slider.ho->pos;
            f64 scaling_factor = 50.0 / circleRadius;

            for(uSz i = 0; i < slider.ho->scoringTimes.size(); i++) {
                vec2 diff;

                if(slider.ho->scoringTimes[i].type == SLIDER_SCORING_TIME::TYPE::END) {
                    // NOTE: In lazer, the position of the slider end is at the visual end, but the time is at the scoring end
                    diff = slider.ho->curve->pointAt(slider.ho->repeats % 2 ? 1.0 : 0.0) - cursor_pos;
                } else {
                    f64 progress = (std::clamp<f32>(slider.ho->scoringTimes[i].time - (f32)slider.ho->time, 0.0f,
                                                    slider.ho->getDuration())) /
                                   slider.ho->spanDuration;
                    if(std::fmod(progress, 2.0) >= 1.0)
                        progress = 1.0 - std::fmod(progress, 1.0);
                    else
                        progress = std::fmod(progress, 1.0);

                    diff = slider.ho->curve->pointAt(progress) - cursor_pos;
                }

                f64 diff_len = scaling_factor * vec::length(diff);

                f64 req_diff = 90.0;

                if(i == slider.ho->scoringTimes.size() - 1) {
                    // Slider end
                    vec2 lazy_diff = slider.lazyEndPos - cursor_pos;
                    if(vec::length(lazy_diff) < vec::length(diff)) diff = lazy_diff;
                    diff_len = scaling_factor * vec::length(diff);
                } else if(slider.ho->scoringTimes[i].type == SLIDER_SCORING_TIME::TYPE::REPEAT) {
                    // Slider repeat
                    req_diff = 50.0;
                }

                if(diff_len > req_diff) {
                    cursor_pos += (diff * (f32)((diff_len - req_diff) / diff_len));
                    diff_len *= (diff_len - req_diff) / diff_len;
                    slider.lazyTravelDist += diff_len;
                }

                if(i == slider.ho->scoringTimes.size() - 1) slider.lazyEndPos = cursor_pos;
            }

            slider.lazyCalcFinished = true;
        }

        static vec2 getEndCursorPosition(DiffObject &hitObject, f32 circleRadius) {
            if(hitObject.ho->type == DifficultyHitObject::TYPE::SLIDER) {
                computeSliderCursorPosition(hitObject, circleRadius);
                return hitObject
                    .lazyEndPos;  // (slider.lazyEndPos is already initialized to ho->pos in DiffObject constructor)
            }

            return hitObject.ho->pos;
        }
    };

    // ****************************************************************************************************************************************** //

    // initialize if not given
    if(!params.cachedDiffObjects) {
        params.cachedDiffObjects = std::make_unique<std::vector<DifficultyCalculator::DiffObject>>();
    }

    auto &cachedDiffObjsRef = *params.cachedDiffObjects;

    // initialize dobjects
    const uSz numDiffObjects =
        (params.upToObjectIndex < 0) ? params.beatmapData.sortedHitObjects.size() : params.upToObjectIndex + 1;

    // only for first load
    const uSz cacheSize = params.forceFillDiffobjCache ? params.beatmapData.sortedHitObjects.size() : numDiffObjects;

    const bool isUsingCachedDiffObjects = (cachedDiffObjsRef.size() >= cacheSize);

    DiffObject *diffObjects;
    if(!isUsingCachedDiffObjects) {
        // not cached (full rebuild computation)
        cachedDiffObjsRef.reserve(cacheSize);
        for(uSz i = 0; i < cacheSize; i++) {
            if(params.cancelCheck.stop_requested()) return 0.0;

            DiffObject newDiffObject{&params.beatmapData.sortedHitObjects[i], radius_scaling_factor,
                                     params.cachedDiffObjects.get(),
                                     (i32)i - 1};  // this already initializes the angle to NaN
            newDiffObject.smallCircleBonus = smallCircleBonus;
            cachedDiffObjsRef.push_back(std::move(newDiffObject));
        }
    }
    diffObjects = cachedDiffObjsRef.data();

    // calculate angles and travel/jump distances (before calculating strains)
    if(!isUsingCachedDiffObjects) {
        const f32 starsSliderCurvePointsSeparation = STARS_SLIDER_CURVE_POINTS_SEPARATION;
        for(uSz i = 0; i < cacheSize; i++) {
            if(params.cancelCheck.stop_requested()) return 0.0;

            // see setDistances() @ https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Preprocessing/OsuDifficultyHitObject.cs

            if(i > 0) {
                // calculate travel/jump distances
                DiffObject &cur = diffObjects[i];
                DiffObject &prev1 = diffObjects[i - 1];

                // MCKAY:
                {
                    // delay curve creation to when it's needed (1)
                    if(prev1.ho->scheduledCurveAlloc && prev1.ho->curve == nullptr) {
                        prev1.ho->curve = SliderCurve::createCurve(
                            prev1.ho->osuSliderCurveType, prev1.ho->scheduledCurveAllocControlPoints,
                            prev1.ho->pixelLength, starsSliderCurvePointsSeparation);
                        prev1.ho->updateCurveStackPosition(
                            prev1.ho->scheduledCurveAllocStackOffset);  // NOTE: respect stacking
                    }
                }

                if(cur.ho->type == DifficultyHitObject::TYPE::SLIDER) {
                    DistanceCalc::computeSliderCursorPosition(cur, circleRadiusInOsuPixels);
                    cur.travelDistance = cur.lazyTravelDist * std::pow(1.0 + (cur.ho->repeats - 1) / 2.5, 1.0 / 2.5);
                    cur.travelTime = std::max(cur.lazyTravelTime, 25.0);
                }

                // don't need to jump to reach spinners
                if(cur.ho->type == DifficultyHitObject::TYPE::SPINNER ||
                   prev1.ho->type == DifficultyHitObject::TYPE::SPINNER)
                    continue;

                const vec2 lastCursorPosition = DistanceCalc::getEndCursorPosition(prev1, circleRadiusInOsuPixels);

                f64 cur_strain_time =
                    (f64)std::max(cur.ho->time - prev1.ho->time, 25);  // adjusted_delta_time isn't initialized here
                cur.jumpDistance = vec::length(cur.norm_start - lastCursorPosition * radius_scaling_factor);
                cur.minJumpDistance = cur.jumpDistance;
                cur.minJumpTime = cur_strain_time;

                if(prev1.ho->type == DifficultyHitObject::TYPE::SLIDER) {
                    f64 last_travel = std::max(prev1.lazyTravelTime, 25.0);
                    cur.minJumpTime = std::max(cur_strain_time - last_travel, 25.0);

                    // NOTE: "curve shouldn't be null here, but Yin [test7] causes that to happen"
                    // NOTE: the curve can be null if controlPoints.size() < 1 because the OsuDifficultyHitObject() constructor will then not set scheduledCurveAlloc to true (which is perfectly fine and correct)
                    f32 tail_jump_dist =
                        vec::distance(prev1.ho->curve ? prev1.ho->curve->pointAt(prev1.ho->repeats % 2 ? 1.0 : 0.0)
                                                      : prev1.ho->pos,
                                      cur.ho->pos) *
                        radius_scaling_factor;
                    cur.minJumpDistance = std::max(
                        0.0f, std::min((f32)cur.minJumpDistance - (maximum_slider_radius - assumed_slider_radius),
                                       tail_jump_dist - maximum_slider_radius));
                }

                // calculate angles
                if(i > 1) {
                    DiffObject &prev2 = diffObjects[i - 2];
                    if(prev2.ho->type == DifficultyHitObject::TYPE::SPINNER) continue;

                    const vec2 lastLastCursorPosition =
                        DistanceCalc::getEndCursorPosition(prev2, circleRadiusInOsuPixels);

                    // MCKAY:
                    {
                        // and also immediately delete afterwards (2)
                        // NOTE: this trivial sliding window implementation will keep the last 2 curves alive at the end, but they get auto deleted later anyway so w/e
                        if(i > 2) {
                            DiffObject &prev3 = diffObjects[i - 3];

                            if(prev3.ho->scheduledCurveAlloc) prev3.ho->curve.reset();
                        }
                    }

                    const vec2 v1 = lastLastCursorPosition - prev1.ho->pos;
                    const vec2 v2 = cur.ho->pos - lastCursorPosition;

                    const f64 dot = vec::dot(v1, v2);
                    const f64 det = (v1.x * v2.y) - (v1.y * v2.x);

                    cur.angle = std::fabs(std::atan2(det, dot));
                }
            }
        }
    }

    // calculate strains/skills
    if(!isUsingCachedDiffObjects)  // NOTE: yes, this loses some extremely minor accuracy (~0.001 stars territory) for live star/pp for some rare individual upToObjectIndex due to not being recomputed for the cut set of cached diffObjects every time, but the performance gain is so insane I don't care
    {
        for(uSz i = 1; i < cacheSize; i++)  // NOTE: start at 1
        {
            diffObjects[i].calculate_strains(diffObjects[i - 1], (i == cacheSize - 1) ? nullptr : &diffObjects[i + 1],
                                             hitWindow300, params.beatmapData.autopilot);
        }
    }

    // calculate final difficulty (weigh strains)
    f64 aimNoSliders = DiffObject::calculate_difficulty(
        Skills::AIM_NO_SLIDERS, diffObjects, numDiffObjects,
        params.incremental ? &params.incremental[Skills::AIM_NO_SLIDERS] : nullptr, nullptr, &params.outAttributes);

    f64 speed = DiffObject::calculate_difficulty(Skills::SPEED, diffObjects, numDiffObjects,
                                                 params.incremental ? &params.incremental[Skills::SPEED] : nullptr,
                                                 params.outSpeedStrains, &params.outAttributes);

    // Very important hack (because otherwise I have to rewrite how to `DiffObject::calculate_difficulty` works):
    // At this point params.outAttributes `AimDifficultStrains` and `AimTopWeightedSlidersFactor` are calculated on aimNoSliders, what is exactly what we need here
    // Later it would be overriden by normal Aim that includes sliders
    // Technically TopWeightedSliderFactor attribute here is TopWeightedSliderCount, we're temporary using it for calculations
    f64 aimTopWeightedSliderFactor =
        params.outAttributes.AimTopWeightedSliderFactor /
        std::max(1.0, params.outAttributes.AimDifficultStrainCount - params.outAttributes.AimTopWeightedSliderFactor);
    f64 speedTopWeightedSliderFactor = params.outAttributes.SpeedTopWeightedSliderFactor /
                                       std::max(1.0, params.outAttributes.SpeedDifficultStrainCount -
                                                         params.outAttributes.SpeedTopWeightedSliderFactor);

    // Don't move this aim above, it's intended, read previous comments
    f64 aim = DiffObject::calculate_difficulty(Skills::AIM_SLIDERS, diffObjects, numDiffObjects,
                                               params.incremental ? &params.incremental[Skills::AIM_SLIDERS] : nullptr,
                                               params.outAimStrains, &params.outAttributes);

    params.outAttributes.SliderFactor =
        aim > 0.0 ? calculateDifficultyRating(aimNoSliders) / calculateDifficultyRating(aim) : 1.0;

    f64 mechanicalDifficultyRating = calculateMechanicalDifficultyRating(aim, speed);

    // Don't forget to scale AR and OD by rate here before using it in rating calculation
    const f64 adjAR = GameRules::arWithSpeed(params.beatmapData.AR, params.beatmapData.speedMultiplier);
    const f64 adjOD = adjustOverallDifficultyByClockRate(params.beatmapData.OD, params.beatmapData.speedMultiplier);

    if(params.outRawDifficulty) {
        *params.outRawDifficulty = {aimNoSliders, aim, speed};
    }

    aimNoSliders = computeAimRating(aimNoSliders, numDiffObjects, adjAR, adjOD, mechanicalDifficultyRating,
                                    params.outAttributes.SliderFactor, params.beatmapData);
    aim = computeAimRating(aim, numDiffObjects, adjAR, adjOD, mechanicalDifficultyRating,
                           params.outAttributes.SliderFactor, params.beatmapData);
    speed = computeSpeedRating(speed, numDiffObjects, adjAR, adjOD, mechanicalDifficultyRating, params.beatmapData);

    // Scorev1
    calculateScoreV1Attributes(params.outAttributes, params.beatmapData, params.upToObjectIndex);

    params.outAttributes.AimDifficulty = aim;
    params.outAttributes.SpeedDifficulty = speed;

    params.outAttributes.AimTopWeightedSliderFactor = aimTopWeightedSliderFactor;
    params.outAttributes.SpeedTopWeightedSliderFactor = speedTopWeightedSliderFactor;

    return calculateTotalStarsFromSkills(aim, speed);
}

f64 DifficultyCalculator::recomputeStarRating(const RawDifficultyValues &raw, const BeatmapDiffcalcData &beatmapData) {
    const u32 numDiffObjects = beatmapData.sortedHitObjects.size();
    const f64 sliderFactor =
        raw.aim > 0.0 ? calculateDifficultyRating(raw.aimNoSliders) / calculateDifficultyRating(raw.aim) : 1.0;
    const f64 mechanicalDifficultyRating = calculateMechanicalDifficultyRating(raw.aim, raw.speed);

    const f64 adjAR = GameRules::arWithSpeed(beatmapData.AR, beatmapData.speedMultiplier);
    const f64 adjOD = adjustOverallDifficultyByClockRate(beatmapData.OD, beatmapData.speedMultiplier);

    const f64 aim = computeAimRating(raw.aim, numDiffObjects, adjAR, adjOD, mechanicalDifficultyRating, sliderFactor,
                                     beatmapData);
    const f64 speed =
        computeSpeedRating(raw.speed, numDiffObjects, adjAR, adjOD, mechanicalDifficultyRating, beatmapData);

    return calculateTotalStarsFromSkills(aim, speed);
}

void DifficultyCalculator::calculateScoreV1Attributes(DifficultyAttributes &attributes, const BeatmapDiffcalcData &b,
                                                      i32 upToObjectIndex) {
    // Nested score per object
    const f64 big_tick_score = 30;
    const f64 small_tick_score = 10;

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

            sliderScore += amountOfBigTicks * big_tick_score + amountOfSmallTicks * small_tick_score;
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
    i32 combo = 0;
    attributes.MaximumLegacyComboScore = 0;

    for(i32 i = 0; i < upToObjectIndex; i++) {
        const auto &hitObject = b.sortedHitObjects[i];

        if(hitObject.type == DifficultyHitObject::TYPE::SLIDER) {
            // Increase combo for each nested object
            combo += hitObject.scoringTimes.size();

            // For sliders we need to increase combo BEFORE giving score
            combo++;
        }

        attributes.MaximumLegacyComboScore +=
            (i32)(std::max(0, combo - 1) * (score_increase / 25. * attributes.LegacyScoreBaseMultiplier));

        // We have already increased combo for slider
        if(hitObject.type != DifficultyHitObject::TYPE::SLIDER) combo++;
    }
}

f64 DifficultyCalculator::calculateScoreV1SpinnerScore(f64 spinnerDuration) {
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

f64 DifficultyCalculator::calculatePPv2(PPv2CalcParams &cpar) {
    const bool isMcOsuImported = cpar.isMcOsuImported;

    // NOTE: depends on active mods + OD + AR

    if(cpar.c300 < 0) cpar.c300 = cpar.numHitObjects - cpar.c100 - cpar.c50 - cpar.misses;

    if(cpar.combo < 0) cpar.combo = cpar.maxPossibleCombo;

    if(cpar.combo < 1) return 0.0;

    const i32 totalHits = cpar.c300 + cpar.c100 + cpar.c50 + cpar.misses;

    ScoreData score{
        .modFlags = cpar.modFlags,
        .accuracy =
            (totalHits > 0 ? (f64)(cpar.c300 * 300 + cpar.c100 * 100 + cpar.c50 * 50) / (f64)(totalHits * 300) : 0.0),
        .countGreat = cpar.c300,
        .countOk = cpar.c100,
        .countMeh = cpar.c50,
        .countMiss = cpar.misses,
        .totalHits = totalHits,
        .totalSuccessfulHits = cpar.c300 + cpar.c100 + cpar.c50,
        .beatmapMaxCombo = cpar.maxPossibleCombo,
        .scoreMaxCombo = cpar.combo,
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
    const f64 adjAR = GameRules::arWithSpeed(cpar.ar, cpar.timescale);
    const f64 adjOD = adjustOverallDifficultyByClockRate(cpar.od, cpar.timescale);

    // calculateEffectiveMissCount @ https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/OsuPerformanceCalculator.cs
    // required because slider breaks aren't exposed to pp calculation
    f64 comboBasedMissCount = 0.0;
    if(cpar.numSliders > 0) {
        f64 fullComboThreshold = cpar.maxPossibleCombo - (0.1 * cpar.numSliders);
        if((f64)cpar.combo < fullComboThreshold)
            comboBasedMissCount = fullComboThreshold / std::max(1.0, (f64)cpar.combo);
    }
    f64 effectiveMissCount =
        std::clamp<f64>(comboBasedMissCount, (f64)cpar.misses, (f64)(cpar.c50 + cpar.c100 + cpar.misses));

    if(score.legacyTotalScore > 0) {
        f64 scoreBasedMisscount = calculateScoreBasedMisscount(cpar.attributes, score, cpar.timescale, isMcOsuImported);
        effectiveMissCount =
            std::clamp<f64>(scoreBasedMisscount, (f64)cpar.misses, (f64)(cpar.c50 + cpar.c100 + cpar.misses));
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

    const f64 aimValue = computeAimValue(score, cpar.attributes, effectiveMissCount);
    const f64 speedValue = computeSpeedValue(score, cpar.attributes, effectiveMissCount, speedDeviation);
    const f64 accuracyValue = computeAccuracyValue(score, cpar.attributes);

    const f64 totalValue =
        std::pow(std::pow(aimValue, 1.1) + std::pow(speedValue, 1.1) + std::pow(accuracyValue, 1.1), 1.0 / 1.1) *
        multiplier;

    return totalValue;
}

f64 DifficultyCalculator::calculateTotalStarsFromSkills(f64 aim, f64 speed) {
    f64 baseAimPerformance = strainDifficultyToPerformance(aim);
    f64 baseSpeedPerformance = strainDifficultyToPerformance(speed);
    f64 basePerformance = std::pow(std::pow(baseAimPerformance, 1.1) + std::pow(baseSpeedPerformance, 1.1), 1.0 / 1.1);
    return calculateStarRating(basePerformance);
}

// https://github.com/ppy/osu-performance/blob/master/src/performance/osu/OsuScore.cpp
// https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/OsuPerformanceCalculator.cs

f64 DifficultyCalculator::computeAimValue(const ScoreData &score,
                                          const DifficultyCalculator::DifficultyAttributes &attributes,
                                          f64 effectiveMissCount) {
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

    f64 aimValue = strainDifficultyToPerformance(aimDifficulty);

    // length bonus
    f64 lengthBonus = 0.95 + 0.4 * std::min(1.0, ((f64)score.totalHits / 2000.0)) +
                      (score.totalHits > 2000 ? std::log10(((f64)score.totalHits / 2000.0)) * 0.5 : 0.0);
    aimValue *= lengthBonus;

    // miss penalty
    // see https://github.com/ppy/osu/pull/16280/
    if(effectiveMissCount > 0 && score.totalHits > 0) {
        f64 aimEstimatedSliderBreaks =
            calculateEstimatedSliderBreaks(score, attributes.AimTopWeightedSliderFactor, effectiveMissCount);
        f64 relevantMissCount = std::min(effectiveMissCount + aimEstimatedSliderBreaks,
                                         (f64)(score.countOk + score.countMeh + score.countMiss));
        aimValue *=
            0.96 / ((relevantMissCount / (4.0 * std::pow(std::log(attributes.AimDifficultStrainCount), 0.94))) + 1.0);
    }

    // scale aim with acc
    aimValue *= score.accuracy;

    return aimValue;
}

f64 DifficultyCalculator::computeSpeedValue(const ScoreData &score, const DifficultyAttributes &attributes,
                                            f64 effectiveMissCount, f64 speedDeviation) {
    if((flags::has<ModFlags::Relax>(score.modFlags)) || std::isnan(speedDeviation)) return 0.0;

    f64 speedValue = strainDifficultyToPerformance(attributes.SpeedDifficulty);

    // length bonus
    f64 lengthBonus = 0.95 + 0.4 * std::min(1.0, ((f64)score.totalHits / 2000.0)) +
                      (score.totalHits > 2000 ? std::log10(((f64)score.totalHits / 2000.0)) * 0.5 : 0.0);
    speedValue *= lengthBonus;

    // miss penalty
    // see https://github.com/ppy/osu/pull/16280/
    if(effectiveMissCount > 0) {
        f64 speedEstimatedSliderBreaks =
            calculateEstimatedSliderBreaks(score, attributes.SpeedTopWeightedSliderFactor, effectiveMissCount);
        f64 relevantMissCount = std::min(effectiveMissCount + speedEstimatedSliderBreaks,
                                         (f64)(score.countOk + score.countMeh + score.countMiss));
        speedValue *=
            0.96 / ((relevantMissCount / (4.0 * std::pow(std::log(attributes.SpeedDifficultStrainCount), 0.94))) + 1.0);
    }

    f64 speedHighDeviationMultiplier = calculateSpeedHighDeviationNerf(attributes, speedDeviation);
    speedValue *= speedHighDeviationMultiplier;

    // "Calculate accuracy assuming the worst case scenario"
    f64 relevantTotalDiff = std::max(0.0, score.totalHits - attributes.SpeedNoteCount);
    f64 relevantCountGreat = std::max(0.0, score.countGreat - relevantTotalDiff);
    f64 relevantCountOk = std::max(0.0, score.countOk - std::max(0.0, relevantTotalDiff - score.countGreat));
    f64 relevantCountMeh =
        std::max(0.0, score.countMeh - std::max(0.0, relevantTotalDiff - score.countGreat - score.countOk));
    f64 relevantAccuracy =
        attributes.SpeedNoteCount == 0
            ? 0
            : (relevantCountGreat * 6.0 + relevantCountOk * 2.0 + relevantCountMeh) / (attributes.SpeedNoteCount * 6.0);

    // see https://github.com/ppy/osu-performance/pull/128/
    // Scale the speed value with accuracy and OD
    speedValue *= std::pow((score.accuracy + relevantAccuracy) / 2.0, (14.5 - attributes.OverallDifficulty) / 2);

    // singletap buff (XXX: might be too weak)
    if(flags::has<ModFlags::Singletap>(score.modFlags)) {
        speedValue *= 1.25;
    }

    // no keylock nerf (XXX: might be too harsh)
    if(flags::has<ModFlags::NoKeylock>(score.modFlags)) {
        speedValue *= 0.5;
    }

    return speedValue;
}

f64 DifficultyCalculator::computeAccuracyValue(const ScoreData &score, const DifficultyAttributes &attributes) {
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

    // length bonus
    accuracyValue *= std::min(1.15, std::pow(score.amountHitObjectsWithAccuracy / 1000.0, 0.3));

    // hidden bonus
    if(flags::has<ModFlags::Hidden>(score.modFlags)) {
        // Decrease bonus for AR > 10
        accuracyValue *= 1.0 + 0.08 * reverseLerp(attributes.ApproachRate, 11.5, 10.0);
    }

    // flashlight bonus
    if(flags::has<ModFlags::Flashlight>(score.modFlags)) accuracyValue *= 1.02;

    return accuracyValue;
}

f64 DifficultyCalculator::calculateEstimatedSliderBreaks(const ScoreData &score, f64 topWeightedSliderFactor,
                                                         f64 effectiveMissCount) {
    if(score.countOk == 0 || score.beatmapMaxCombo < 1) return 0.0;

    f64 missedComboPercent = 1.0 - (f64)score.scoreMaxCombo / score.beatmapMaxCombo;
    f64 estimatedSliderBreaks = std::min((f64)score.countOk, effectiveMissCount * topWeightedSliderFactor);

    // Scores with more Oks are more likely to have slider breaks.
    f64 okAdjustment = ((score.countOk - estimatedSliderBreaks) + 0.5) / (f64)score.countOk;

    // There is a low probability of extra slider breaks on effective miss counts close to 1, as score based calculations are good at indicating if only a single break occurred.
    estimatedSliderBreaks *= smoothstep(effectiveMissCount, 1.0, 2.0);

    return estimatedSliderBreaks * okAdjustment * logistic(missedComboPercent, 0.33, 15.0);
}

f64 DifficultyCalculator::calculateSpeedDeviation(const ScoreData &score, const DifficultyAttributes &attributes,
                                                  f64 timescale) {
    if(score.countGreat + score.countOk + score.countMeh == 0) return std::numeric_limits<f64>::quiet_NaN();

    f64 speedNoteCount = attributes.SpeedNoteCount;
    speedNoteCount += (score.totalHits - attributes.SpeedNoteCount) * 0.1;

    f64 relevantCountMiss = std::min((f64)score.countMiss, speedNoteCount);
    f64 relevantCountMeh = std::min((f64)score.countMeh, speedNoteCount - relevantCountMiss);
    f64 relevantCountOk = std::min((f64)score.countOk, speedNoteCount - relevantCountMiss - relevantCountMeh);
    f64 relevantCountGreat = std::max(0.0, speedNoteCount - relevantCountMiss - relevantCountMeh - relevantCountOk);

    return calculateDeviation(attributes, timescale, relevantCountGreat, relevantCountOk, relevantCountMeh);
}

f64 DifficultyCalculator::calculateDeviation(const DifficultyAttributes &attributes, f64 timescale,
                                             f64 relevantCountGreat, f64 relevantCountOk, f64 relevantCountMeh) {
    if(relevantCountGreat + relevantCountOk + relevantCountMeh <= 0.0) return std::numeric_limits<f64>::quiet_NaN();

    const f64 greatHitWindow = adjustHitWindow(GameRules::odTo300HitWindowMS(attributes.OverallDifficulty)) / timescale;
    const f64 okHitWindow = adjustHitWindow(GameRules::odTo100HitWindowMS(attributes.OverallDifficulty)) / timescale;
    const f64 mehHitWindow = adjustHitWindow(GameRules::odTo50HitWindowMS(attributes.OverallDifficulty)) / timescale;

    static constexpr const f64 z = 2.32634787404;
    static constexpr const f64 sqrt2 = std::numbers::sqrt2;
    static constexpr const f64 sqrt3 = std::numbers::sqrt3;
    static constexpr const f64 sqrt2OverPi = 0.7978845608028654;

    f64 n = std::max(1.0, relevantCountGreat + relevantCountOk);
    f64 p = relevantCountGreat / n;
    f64 pLowerBound =
        std::min(p, (n * p + z * z / 2.0) / (n + z * z) - z / (n + z * z) * sqrt(n * p * (1.0 - p) + z * z / 4.0));

    f64 deviation;

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

f64 DifficultyCalculator::calculateSpeedHighDeviationNerf(const DifficultyAttributes &attributes, f64 speedDeviation) {
    if(std::isnan(speedDeviation)) return 0.0;

    f64 speedValue = std::pow(5.0 * std::max(1.0, attributes.SpeedDifficulty / 0.0675) - 4.0, 3.0) / 100000.0;
    f64 excessSpeedDifficultyCutoff = 100.0 + 220.0 * std::pow(22.0 / speedDeviation, 6.5);
    if(speedValue <= excessSpeedDifficultyCutoff) return 1.0;

    const f64 scale = 50.0;
    f64 adjustedSpeedValue = scale * (std::log((speedValue - excessSpeedDifficultyCutoff) / scale + 1.0) +
                                      excessSpeedDifficultyCutoff / scale);
    f64 lerpVal = 1.0 - std::clamp<f64>((speedDeviation - 22.0) / (27.0 - 22.0), 0.0, 1.0);
    adjustedSpeedValue = std::lerp(adjustedSpeedValue, speedValue, lerpVal);

    return adjustedSpeedValue / speedValue;
}

f64 DifficultyCalculator::calculateScoreBasedMisscount(const DifficultyAttributes &attributes, const ScoreData &score,
                                                       f64 timescale, bool isMcOsuImported) {
    if(score.beatmapMaxCombo == 0) return 0;

    const bool scoreV2 = flags::has<ModFlags::ScoreV2>(score.modFlags);

    f64 modMultiplier = getScoreV1ScoreMultiplier(score.modFlags, timescale, isMcOsuImported);
    f64 scoreV1Multiplier = attributes.LegacyScoreBaseMultiplier * modMultiplier;
    f64 relevantComboPerObject = calculateRelevantScoreComboPerObject(attributes, score);

    f64 maximumMissCount = calculateMaximumComboBasedMissCount(attributes, score);

    f64 scoreObtainedDuringMaxCombo =
        calculateScoreAtCombo(attributes, score, score.scoreMaxCombo, relevantComboPerObject, scoreV1Multiplier);
    if(scoreV2) scoreObtainedDuringMaxCombo *= 700000. / attributes.MaximumLegacyComboScore;

    f64 scoreLegacyTotalScore =
        score.legacyTotalScore - (scoreV2 ? 300000. * std::pow(score.accuracy, 10) * modMultiplier : 0);
    f64 remainingScore = scoreLegacyTotalScore - scoreObtainedDuringMaxCombo;

    if(remainingScore <= 0) return maximumMissCount;

    f64 remainingCombo = score.beatmapMaxCombo - score.scoreMaxCombo;
    f64 expectedRemainingScore =
        calculateScoreAtCombo(attributes, score, remainingCombo, relevantComboPerObject, scoreV1Multiplier);
    if(scoreV2) expectedRemainingScore *= 700000. / attributes.MaximumLegacyComboScore;

    f64 scoreBasedMissCount = expectedRemainingScore / remainingScore;

    // If there's less then one miss detected - let combo-based miss count decide if this is FC or not
    scoreBasedMissCount = std::max(scoreBasedMissCount, 1.0);

    // Cap result by very harsh version of combo-based miss count
    return std::min(scoreBasedMissCount, maximumMissCount);
}

f64 DifficultyCalculator::calculateScoreAtCombo(const DifficultyAttributes &attributes, const ScoreData &score,
                                                f64 combo, f64 relevantComboPerObject, f64 scoreV1Multiplier) {
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
    comboScore *= 300.0 / 25.0 * scoreV1Multiplier;

    // For scoreV2 we need only combo score not scaled by accuracy.
    // This is technically incorrect because scoreV2 is using different formula,
    // but we have to sacrifice estimation precision, since it's not as important here.
    const bool scoreV2 = flags::has<ModFlags::ScoreV2>(score.modFlags);
    if(scoreV2) return comboScore;

    f64 objectsHit = (totalHits - countMiss) * combo / score.beatmapMaxCombo;

    // Score also has a non-combo portion we need to create the final score value.
    f64 nonComboScore = (300.0 + attributes.NestedScorePerObject) * objectsHit;

    return (comboScore + nonComboScore) * score.accuracy;
}

f64 DifficultyCalculator::calculateRelevantScoreComboPerObject(const DifficultyAttributes &attributes,
                                                               const ScoreData &score) {
    f64 comboScore = attributes.MaximumLegacyComboScore;

    // We then reverse apply the ScoreV1 multipliers to get the raw value.
    comboScore /= 300.0 / 25.0 * attributes.LegacyScoreBaseMultiplier;

    // Reverse the arithmetic progression to work out the amount of combo per object based on the score.
    f64 result = (f64)((score.beatmapMaxCombo - 2) * score.beatmapMaxCombo);
    result /= std::max((f64)score.beatmapMaxCombo + 2.0 * (comboScore - 1.0), 1.0);

    return result;
}

f64 DifficultyCalculator::calculateMaximumComboBasedMissCount(const DifficultyAttributes &attributes,
                                                              const ScoreData &score) {
    i32 scoreMissCount = score.countMiss;

    if(attributes.SliderCount <= 0) return scoreMissCount;

    i32 countOk = score.countOk;
    i32 countMeh = score.countMeh;

    i32 totalImperfectHits = countOk + countMeh + scoreMissCount;

    f64 missCount = 0;

    // Consider that full combo is maximum combo minus dropped slider tails since they don't contribute to combo but also don't break it
    // In classic scores we can't know the amount of dropped sliders so we estimate to 10% of all sliders on the map
    f64 fullComboThreshold = score.beatmapMaxCombo - 0.1 * attributes.SliderCount;

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

f64 DifficultyCalculator::getScoreV1ScoreMultiplier(ModFlags flags, f64 speedOverride, bool mcosu) {
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

// https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/OsuRatingCalculator.cs

f64 DifficultyCalculator::calculateDifficultyRating(f64 difficultyValue) {
    const f64 difficulty_multiplier = 0.0675;
    return std::sqrt(difficultyValue) * difficulty_multiplier;
}

f64 DifficultyCalculator::calculateAimVisibilityFactor(f64 approachRate, f64 mechanicalDifficultyRating) {
    const f64 ar_factor_end_point = 11.5;

    f64 mechanicalDifficultyFactor = reverseLerp(mechanicalDifficultyRating, 5.0, 10.0);
    f64 arFactorStartingPoint = std::lerp(9., 10.33, mechanicalDifficultyFactor);

    return reverseLerp(approachRate, ar_factor_end_point, arFactorStartingPoint);
}

f64 DifficultyCalculator::calculateSpeedVisibilityFactor(f64 approachRate, f64 mechanicalDifficultyRating) {
    const f64 ar_factor_end_point = 11.5;

    f64 mechanicalDifficultyFactor = reverseLerp(mechanicalDifficultyRating, 5.0, 10.0);
    f64 arFactorStartingPoint = std::lerp(10., 10.33, mechanicalDifficultyFactor);

    return reverseLerp(approachRate, ar_factor_end_point, arFactorStartingPoint);
}

f64 DifficultyCalculator::calculateVisibilityBonus(f64 approachRate, f64 visibilityFactor, f64 sliderFactor) {
    // WARNING: this value is equal to true for Traceable (Lazer mod) and the lazer-specific HD setting.
    // So in this case we're always setting it to false
    bool isAlwaysPartiallyVisible = false;

    f64 readingBonus =
        (isAlwaysPartiallyVisible ? 0.025 : 0.04) *
        (12.0 - std::max(std::min(12.0, approachRate), 7.0));  // NOTE: clamped because McOsu allows AR > 12

    readingBonus *= visibilityFactor;

    // We want to reward slideraim on low AR less
    f64 sliderVisibilityFactor = std::pow(sliderFactor, 3.0);

    // For AR up to 0 - reduce reward for very low ARs when object is visible
    if(approachRate < 7.0)
        readingBonus +=
            (isAlwaysPartiallyVisible ? 0.02 : 0.045) * (7.0 - std::max(approachRate, 0.0)) * sliderVisibilityFactor;

    // Starting from AR0 - cap values so they won't grow to infinity
    if(approachRate < 0.0)
        readingBonus +=
            (isAlwaysPartiallyVisible ? 0.01 : 0.1) * (1.0 - std::max(1.5, approachRate)) * sliderVisibilityFactor;

    return readingBonus;
}

f64 DifficultyCalculator::computeAimRating(f64 aimDifficultyValue, u32 totalHits, f64 approachRate,
                                           f64 overallDifficulty, f64 mechanicalDifficultyRating, f64 sliderFactor,
                                           const BeatmapDiffcalcData &beatmapData) {
    if(beatmapData.autopilot) return 0;

    f64 aimRating = calculateDifficultyRating(aimDifficultyValue);

    if(beatmapData.touchDevice) aimRating = std::pow(aimRating, 0.8);

    if(beatmapData.relax) aimRating *= 0.9;

    f64 ratingMultiplier = 1.0;

    f64 approachRateLengthBonus = 0.95 + 0.4 * std::min(1.0, totalHits / 2000.0) +
                                  (totalHits > 2000 ? std::log10(totalHits / 2000.0) * 0.5 : 0.0);

    f64 approachRateFactor = 0.0;
    if(approachRate > 10.33)
        approachRateFactor = 0.3 * (approachRate - 10.33);
    else if(approachRate < 8.0)
        approachRateFactor = 0.05 * (8.0 - approachRate);

    if(beatmapData.relax) approachRateFactor = 0.0;

    ratingMultiplier += approachRateFactor * approachRateLengthBonus;  // Buff for longer maps with high AR.

    if(beatmapData.hidden) {
        f64 visibilityFactor = calculateAimVisibilityFactor(approachRate, mechanicalDifficultyRating);
        ratingMultiplier += calculateVisibilityBonus(approachRate, visibilityFactor, sliderFactor);
    }

    // It is important to consider accuracy difficulty when scaling with accuracy.
    ratingMultiplier *= 0.98 + std::pow(std::max(0.0, overallDifficulty), 2.0) / 2500.0;

    return aimRating * std::cbrt(ratingMultiplier);
}

f64 DifficultyCalculator::computeSpeedRating(f64 speedDifficultyValue, u32 totalHits, f64 approachRate,
                                             f64 overallDifficulty, f64 mechanicalDifficultyRating,
                                             const BeatmapDiffcalcData &beatmapData) {
    if(beatmapData.relax) return 0.0;

    f64 speedRating = calculateDifficultyRating(speedDifficultyValue);

    if(beatmapData.autopilot) speedRating *= 0.5;

    f64 ratingMultiplier = 1.0;

    f64 approachRateLengthBonus = 0.95 + 0.4 * std::min(1.0, totalHits / 2000.0) +
                                  (totalHits > 2000 ? std::log10(totalHits / 2000.0) * 0.5 : 0.0);

    f64 approachRateFactor = 0.0;
    if(approachRate > 10.33) approachRateFactor = 0.3 * (approachRate - 10.33);

    if(beatmapData.autopilot) approachRateFactor = 0.0;

    ratingMultiplier += approachRateFactor * approachRateLengthBonus;  // Buff for longer maps with high AR.

    if(beatmapData.hidden) {
        f64 visibilityFactor = calculateSpeedVisibilityFactor(approachRate, mechanicalDifficultyRating);
        ratingMultiplier += calculateVisibilityBonus(approachRate, visibilityFactor);
    }

    ratingMultiplier *= 0.95 + std::pow(std::max(0.0, overallDifficulty), 2.0) / 750.0;

    return speedRating * std::cbrt(ratingMultiplier);
}

// https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/OsuDifficultyCalculator.cs#L148-L164
f64 DifficultyCalculator::calculateStarRating(f64 basePerformance) {
    const f64 star_rating_multiplier = 0.0265;

    if(basePerformance <= 0.00001) return 0.0;

    return std::cbrt(performance_base_multiplier) * star_rating_multiplier *
           (std::cbrt(100000.0 / std::pow(2.0, 1.0 / 1.1) * basePerformance) + 4.0);
}

f64 DifficultyCalculator::calculateMechanicalDifficultyRating(f64 aimDifficultyValue, f64 speedDifficultyValue) {
    f64 aimValue = strainDifficultyToPerformance(calculateDifficultyRating(aimDifficultyValue));
    f64 speedValue = strainDifficultyToPerformance(calculateDifficultyRating(speedDifficultyValue));

    f64 totalValue = std::pow(std::pow(aimValue, 1.1) + std::pow(speedValue, 1.1), 1.0 / 1.1);

    return calculateStarRating(totalValue);
}

f64 DifficultyCalculator::adjustOverallDifficultyByClockRate(f64 OD, f64 clockRate) {
    return (79.5 - (adjustHitWindow(GameRules::odTo300HitWindowMS(OD)) / clockRate)) / 6.0;
}

f64 DifficultyCalculator::erf(f64 x) {
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

f64 DifficultyCalculator::erfInv(f64 x) {
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

void DifficultyCalculator::DiffObject::calculate_strains(const DiffObject &prev, const DiffObject *next,
                                                         f64 hitWindow300, bool autopilotNerf) {
    calculate_strain(prev, next, hitWindow300, autopilotNerf, Skills::SPEED);
    calculate_strain(prev, next, hitWindow300, autopilotNerf, Skills::AIM_SLIDERS);
    calculate_strain(prev, next, hitWindow300, autopilotNerf, Skills::AIM_NO_SLIDERS);
}

void DifficultyCalculator::DiffObject::calculate_strain(const DiffObject &prev, const DiffObject *next,
                                                        f64 hitWindow300, bool autopilotNerf,
                                                        const Skills::Skill dtype) {
    const f64 AimMultiplier = 26;
    const f64 SpeedMultiplier = 1.47;

    f64 currentStrainOfDiffObject = 0;

    const i32 time_elapsed = ho->time - prev.ho->time;

    // update our delta time
    delta_time = (f64)time_elapsed;
    adjusted_delta_time = (f64)std::max(time_elapsed, 25);

    switch(ho->type) {
        case DifficultyHitObject::TYPE::SLIDER:
        case DifficultyHitObject::TYPE::CIRCLE:
            currentStrainOfDiffObject = spacing_weight2(dtype, prev, next, hitWindow300, autopilotNerf);
            break;

        case DifficultyHitObject::TYPE::SPINNER:
            break;

        case DifficultyHitObject::TYPE::INVALID:
            // NOTE: silently ignore
            return;
    }

    // see Process() @ https://github.com/ppy/osu/blob/master/osu.Game/Rulesets/Difficulty/Skills/Skill.cs
    f64 currentStrain = prev.strains[dtype];
    {
        currentStrain *= strainDecay(dtype, dtype == Skills::SPEED ? adjusted_delta_time : delta_time);
        currentStrain += currentStrainOfDiffObject * (dtype == Skills::SPEED ? SpeedMultiplier : AimMultiplier);
    }
    strains[dtype] = currentStrain;
}

f64 DifficultyCalculator::DiffObject::calculate_difficulty(const Skills::Skill type, const DiffObject *dobjects,
                                                           uSz dobjectCount, IncrementalState *incremental,
                                                           std::vector<f64> *outStrains,
                                                           DifficultyAttributes *outAttributes) {
    // (old) see https://github.com/ppy/osu/blob/master/osu.Game/Rulesets/Difficulty/Skills/Skill.cs
    // (new) see https://github.com/ppy/osu/blob/master/osu.Game/Rulesets/Difficulty/Skills/StrainSkill.cs

    static constexpr f64 strain_step = 400.0;  // the length of each strain section
    static constexpr f64 decay_weight =
        0.9;  // max strains are weighted from highest to lowest, and this is how much the weight decays.

    if(dobjectCount < 1) return 0.0;

    f64 interval_end =
        incremental ? incremental->interval_end : (std::ceil((f64)dobjects[0].ho->time / strain_step) * strain_step);
    f64 max_strain = incremental ? incremental->max_strain : 0.0;

    std::vector<f64> highestStrains;
    std::vector<f64> *highestStrainsRef = incremental ? &incremental->highest_strains : &highestStrains;
    std::vector<f64> sliderStrains;
    std::vector<f64> *sliderStrainsRef = incremental ? &incremental->slider_strains : &sliderStrains;
    for(uSz i = (incremental ? dobjectCount - 1 : 0); i < dobjectCount; i++) {
        const DiffObject &cur = dobjects[i];
        const DiffObject &prev = dobjects[i > 0 ? i - 1 : i];

        // make previous peak strain decay until the current object
        while(cur.ho->time > interval_end) {
            if(incremental)
                highestStrainsRef->insert(std::ranges::upper_bound(*highestStrainsRef, max_strain), max_strain);
            else
                highestStrainsRef->push_back(max_strain);

            // skip calculating strain decay for very long breaks (e.g. beatmap upload size limit hack diffs)
            // strainDecay with a base of 0.3 at 60 seconds is 4.23911583e-32, well below any meaningful difference even after being multiplied by object strain
            f64 strainDelta = interval_end - (f64)prev.ho->time;
            if(i < 1 || strainDelta > 600000.0)  // !prev
                max_strain = 0.0;
            else
                max_strain = prev.get_strain(type) * strainDecay(type, strainDelta);

            interval_end += strain_step;
        }

        // calculate max strain for this interval
        f64 cur_strain = cur.get_strain(type);
        max_strain = std::max(max_strain, cur_strain);

        // NOTE: this is done in StrainValueAt in lazer's code, but doing it here is more convenient for the incremental case
        if(type == Skills::AIM_SLIDERS && cur.ho->type == DifficultyHitObject::TYPE::SLIDER)
            sliderStrainsRef->push_back(cur_strain);
    }

    // the peak strain will not be saved for the last section in the above loop
    if(incremental) {
        incremental->interval_end = interval_end;
        incremental->max_strain = max_strain;
        highestStrains.reserve(incremental->highest_strains.size() + 1);  // required so insert call doesn't reallocate
        highestStrains = incremental->highest_strains;
        highestStrains.insert(std::ranges::upper_bound(highestStrains, max_strain), max_strain);
    } else
        highestStrains.push_back(max_strain);

    if(outStrains != nullptr) (*outStrains) = highestStrains;  // save a copy

    if(outAttributes) {
        if(type == Skills::SPEED) {
            // calculate relevant speed note count
            // RelevantNoteCount @ https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Speed.cs
            static constexpr auto compareDiffObjects = +[](const DiffObject &x, const DiffObject &y) -> bool {
                return (x.get_strain(Skills::SPEED) < y.get_strain(Skills::SPEED));
            };

            f64 maxObjectStrain;
            {
                if(incremental)
                    maxObjectStrain =
                        std::max(incremental->max_object_strain, dobjects[dobjectCount - 1].get_strain(type));
                else
                    maxObjectStrain =
                        (*std::max_element(dobjects, dobjects + dobjectCount, compareDiffObjects)).get_strain(type);
            }

            if(maxObjectStrain == 0.0)
                outAttributes->SpeedNoteCount = 0.0;
            else {
                f64 tempSum = 0.0;
                if(incremental && std::abs(incremental->max_object_strain - maxObjectStrain) < DIFFCALC_EPSILON) {
                    incremental->speed_note_count +=
                        1.0 / (1.0 + std::exp(-((dobjects[dobjectCount - 1].get_strain(type) / maxObjectStrain * 12.0) -
                                                6.0)));
                    tempSum = incremental->speed_note_count;
                } else {
                    for(uSz i = 0; i < dobjectCount; i++) {
                        tempSum +=
                            1.0 / (1.0 + std::exp(-((dobjects[i].get_strain(type) / maxObjectStrain * 12.0) - 6.0)));
                    }

                    if(incremental) {
                        incremental->max_object_strain = maxObjectStrain;
                        incremental->speed_note_count = tempSum;
                    }
                }
                outAttributes->SpeedNoteCount = tempSum;
            }
        } else if(type == Skills::AIM_SLIDERS) {
            // calculate difficult sliders
            // GetDifficultSliders @ https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Aim.cs
            static constexpr auto compareSliderObjects = +[](const DiffObject &x, const DiffObject &y) -> bool {
                return (x.get_slider_strain(Skills::AIM_SLIDERS) < y.get_slider_strain(Skills::AIM_SLIDERS));
            };

            if(incremental && dobjects[dobjectCount - 1].ho->type != DifficultyHitObject::TYPE::SLIDER)
                outAttributes->AimDifficultSliderCount = incremental->aim_difficult_slider_count;
            else {
                f64 maxSliderStrain;
                f64 curSliderStrain = incremental ? dobjects[dobjectCount - 1].strains[Skills::AIM_SLIDERS] : 0.0;
                {
                    if(incremental) {
                        incremental->slider_strains.push_back(curSliderStrain);
                        maxSliderStrain = std::max(incremental->max_slider_strain, curSliderStrain);
                    } else
                        maxSliderStrain = (*std::max_element(dobjects, dobjects + dobjectCount, compareSliderObjects))
                                              .get_slider_strain(Skills::AIM_SLIDERS);
                }

                if(maxSliderStrain <= 0.0)
                    outAttributes->AimDifficultSliderCount = 0.0;
                else {
                    f64 tempSum = 0.0;
                    if(incremental && std::abs(incremental->max_slider_strain - maxSliderStrain) < DIFFCALC_EPSILON) {
                        incremental->aim_difficult_slider_count +=
                            1.0 / (1.0 + std::exp(-((curSliderStrain / maxSliderStrain * 12.0) - 6.0)));
                        tempSum = incremental->aim_difficult_slider_count;
                    } else {
                        if(incremental) {
                            for(f64 slider_strain : incremental->slider_strains) {
                                tempSum += 1.0 / (1.0 + std::exp(-((slider_strain / maxSliderStrain * 12.0) - 6.0)));
                            }
                            incremental->max_slider_strain = maxSliderStrain;
                            incremental->aim_difficult_slider_count = tempSum;
                        } else {
                            for(uSz i = 0; i < dobjectCount; i++) {
                                f64 sliderStrain = dobjects[i].get_slider_strain(Skills::AIM_SLIDERS);
                                if(sliderStrain >= 0.0)
                                    tempSum += 1.0 / (1.0 + std::exp(-((sliderStrain / maxSliderStrain * 12.0) - 6.0)));
                            }
                        }
                    }
                    outAttributes->AimDifficultSliderCount = tempSum;
                }
            }
        }
    }

    // (old) see DifficultyValue() @ https://github.com/ppy/osu/blob/master/osu.Game/Rulesets/Difficulty/Skills/Skill.cs
    // (new) see DifficultyValue() @ https://github.com/ppy/osu/blob/master/osu.Game/Rulesets/Difficulty/Skills/StrainSkill.cs
    // (new) see DifficultyValue() @ https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/OsuStrainSkill.cs

    static constexpr uSz reducedSectionCount = 10;
    static constexpr f64 reducedStrainBaseline = 0.75;

    f64 difficulty = 0.0;
    f64 weight = 1.0;

    // sort strains
    {
        // new implementation
        // NOTE: lazer does this from highest to lowest, but sorting it in reverse lets the reduced top section loop below have a better average insertion time
        if(!incremental) SPREADSORT_RANGE(highestStrains);
    }

    // new implementation (https://github.com/ppy/osu/pull/13483/)
    {
        uSz skillSpecificReducedSectionCount = reducedSectionCount;
        {
            switch(type) {
                case Skills::NUM_SKILLS:
                    std::unreachable();
                    break;
                case Skills::SPEED:
                    skillSpecificReducedSectionCount = 5;
                    break;
                case Skills::AIM_SLIDERS:
                case Skills::AIM_NO_SLIDERS:
                    break;
            }
        }

        // "We are reducing the highest strains first to account for extreme difficulty spikes"
        uSz actualReducedSectionCount = std::min(highestStrains.size(), skillSpecificReducedSectionCount);
        for(uSz i = 0; i < actualReducedSectionCount; i++) {
            const f64 scale = std::log10(
                std::lerp(1.0, 10.0, std::clamp<f64>((f64)i / (f64)skillSpecificReducedSectionCount, 0.0, 1.0)));
            highestStrains[highestStrains.size() - i - 1] *= std::lerp(reducedStrainBaseline, 1.0, scale);
        }

        // re-sort
        f64 reducedSections
            [reducedSectionCount];  // actualReducedSectionCount <= skillSpecificReducedSectionCount <= reducedSectionCount
        memcpy(reducedSections, &highestStrains[highestStrains.size() - actualReducedSectionCount],
               actualReducedSectionCount * sizeof(f64));
        highestStrains.erase(highestStrains.end() - actualReducedSectionCount, highestStrains.end());
        for(uSz i = 0; i < actualReducedSectionCount; i++) {
            highestStrains.insert(std::ranges::upper_bound(highestStrains, reducedSections[i]), reducedSections[i]);
        }

        // weigh the top strains
        for(uSz i = 0; i < highestStrains.size(); i++) {
            f64 last = difficulty;
            difficulty += highestStrains[highestStrains.size() - i - 1] * weight;
            weight *= decay_weight;
            if(std::abs(difficulty - last) < DIFFCALC_EPSILON) break;
        }
    }

    // see CountDifficultStrains @ https://github.com/ppy/osu/pull/16280/files#diff-07543a9ffe2a8d7f02cadf8ef7f81e3d7ec795ec376b2fff8bba7b10fb574e19R78
    if(outAttributes) {
        f64 &difficultStrainCount =
            (type == Skills::SPEED ? outAttributes->SpeedDifficultStrainCount : outAttributes->AimDifficultStrainCount);
        f64 &topWeightedSlidersCount = (type == Skills::SPEED ? outAttributes->SpeedTopWeightedSliderFactor
                                                              : outAttributes->AimTopWeightedSliderFactor);

        if(difficulty == 0.0) {
            difficultStrainCount = difficulty;
            topWeightedSlidersCount = 0;
        } else {
            f64 tempTotalSum = 0.0;
            f64 tempSliderSum = 0.0;
            f64 consistentTopStrain = difficulty / 10.0;

            if(incremental && std::abs(incremental->consistent_top_strain - consistentTopStrain) < DIFFCALC_EPSILON) {
                incremental->difficult_strains +=
                    1.1 / (1.0 + std::exp(-10.0 *
                                          (dobjects[dobjectCount - 1].get_strain(type) / consistentTopStrain - 0.88)));
                tempTotalSum = incremental->difficult_strains;

                f64 sliderStrain = dobjects[dobjectCount - 1].get_slider_strain(type);
                if(sliderStrain >= 0) {
                    incremental->top_weighted_sliders +=
                        1.1 / (1.0 + std::exp(-10.0 * (sliderStrain / consistentTopStrain - 0.88)));
                }
                tempSliderSum = incremental->top_weighted_sliders;
            } else {
                for(uSz i = 0; i < dobjectCount; i++) {
                    tempTotalSum +=
                        1.1 / (1.0 + std::exp(-10.0 * (dobjects[i].get_strain(type) / consistentTopStrain - 0.88)));

                    f64 sliderStrain = dobjects[i].get_slider_strain(type);
                    if(sliderStrain >= 0)
                        tempSliderSum += 1.1 / (1.0 + std::exp(-10.0 * (sliderStrain / consistentTopStrain - 0.88)));
                }

                if(incremental) {
                    incremental->consistent_top_strain = consistentTopStrain;
                    incremental->difficult_strains = tempTotalSum;
                    incremental->top_weighted_sliders = tempSliderSum;
                }
            }

            difficultStrainCount = tempTotalSum;
            topWeightedSlidersCount = tempSliderSum;
        }
    }

    return difficulty;
}

thread_local std::vector<std::pair<DifficultyCalculator::RhythmIsland, int>> DifficultyCalculator::islandCounts;
// new implementation, Xexxar, (ppv2.1), see https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/
f64 DifficultyCalculator::DiffObject::spacing_weight2(const Skills::Skill diff_type, const DiffObject &prev,
                                                      const DiffObject *next, f64 hitWindow300, bool autopilotNerf) {
    static constexpr f64 single_spacing_threshold = 125.0;

    static constexpr f64 min_speed_bonus = 75.0; /* ~200BPM 1/4 streams */
    static constexpr f64 speed_balancing_factor = 40.0;
    static constexpr f64 distance_multiplier = 0.8;

    static constexpr i32 history_time_max = 5000;
    static constexpr i32 history_objects_max = 32;
    static constexpr f64 rhythm_overall_multiplier = 1.0;
    static constexpr f64 rhythm_ratio_multiplier = 15.0;

    //f64 angle_bonus = 1.0; // (apparently unused now in lazer?)

    switch(diff_type) {
        case Skills::NUM_SKILLS:
            std::unreachable();
            break;
        case Skills::SPEED: {
            // see https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Skills/Speed.cs
            if(ho->type == DifficultyHitObject::TYPE::SPINNER) {
                raw_speed_strain = 0.0;
                rhythm = 0.0;

                return 0.0;
            }

            // https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Evaluators/SpeedEvaluator.cs
            const f64 distance = std::min(single_spacing_threshold, prev.travelDistance + minJumpDistance);

            f64 adjusted_delta_time = this->adjusted_delta_time;
            adjusted_delta_time /= std::clamp<f64>((adjusted_delta_time / hitWindow300) / 0.93, 0.92, 1.0);

            f64 doubletapness = 1.0 - get_doubletapness(next, hitWindow300);

            f64 speed_bonus = 0.0;
            if(adjusted_delta_time < min_speed_bonus)
                speed_bonus = 0.75 * std::pow((min_speed_bonus - adjusted_delta_time) / speed_balancing_factor, 2.0);

            f64 distance_bonus =
                autopilotNerf ? 0.0 : std::pow(distance / single_spacing_threshold, 3.95) * distance_multiplier;

            // Apply reduced small circle bonus because flow aim difficulty on small circles doesn't scale as hard as jumps
            distance_bonus *= std::sqrt(smallCircleBonus);

            raw_speed_strain = (1.0 + speed_bonus + distance_bonus) * 1000.0 * doubletapness / adjusted_delta_time;

            // https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Evaluators/RhythmEvaluator.cs
            f64 rhythmComplexitySum = 0;

            const f64 deltaDifferenceEpsilon = hitWindow300 * 0.3;

            RhythmIsland island{INT_MAX, 0};
            RhythmIsland previousIsland{INT_MAX, 0};

            DifficultyCalculator::islandCounts.clear();

            f64 startRatio = 0.0;  // store the ratio of the current start of an island to buff for tighter rhythms

            bool firstDeltaSwitch = false;

            i32 historicalNoteCount = std::min(prevObjectIndex, history_objects_max);

            i32 rhythmStart = 0;

            while(rhythmStart < historicalNoteCount - 2 &&
                  ho->time - get_previous(rhythmStart)->ho->time < history_time_max) {
                rhythmStart++;
            }

            const DiffObject *prevObj = get_previous(rhythmStart);
            const DiffObject *lastObj = get_previous(rhythmStart + 1);

            for(i32 i = rhythmStart; i > 0; i--) {
                const DiffObject *currObj = get_previous(i - 1);

                // scales note 0 to 1 from history to now
                f64 timeDecay = (history_time_max - (ho->time - currObj->ho->time)) / (f64)history_time_max;
                f64 noteDecay = (f64)(historicalNoteCount - i) / historicalNoteCount;

                f64 currHistoricalDecay =
                    std::min(noteDecay, timeDecay);  // either we're limited by time or limited by object count.

                f64 currDelta = std::max(currObj->delta_time, 1e-7);
                f64 prevDelta = std::max(prevObj->delta_time, 1e-7);
                f64 lastDelta = std::max(lastObj->delta_time, 1e-7);

                // calculate how much current delta difference deserves a rhythm bonus
                // this function is meant to reduce rhythm bonus for deltas that are multiples of each other (i.e 100 and 200)
                f64 deltaDifference = std::max(prevDelta, currDelta) / std::min(prevDelta, currDelta);

                // Take only the fractional part of the value since we're only interested in punishing multiples
                f64 deltaDifferenceFraction = deltaDifference - std::trunc(deltaDifference);

                f64 currRatio =
                    1.0 + rhythm_ratio_multiplier * std::min(0.5, smoothstepBellCurve(deltaDifferenceFraction));

                // reduce ratio bonus if delta difference is too big
                f64 differenceMultiplier = std::clamp<f64>(2.0 - deltaDifference / 8.0, 0.0, 1.0);

                f64 windowPenalty =
                    std::min(1.0, std::max(0.0, std::abs(prevDelta - currDelta) - deltaDifferenceEpsilon) /
                                      deltaDifferenceEpsilon);

                f64 effectiveRatio = windowPenalty * currRatio * differenceMultiplier;

                if(firstDeltaSwitch) {
                    if(std::abs(prevDelta - currDelta) < deltaDifferenceEpsilon) {
                        // island is still progressing
                        if(island.delta == INT_MAX) {
                            island.delta = std::max((i32)currDelta, 25);
                        }
                        island.deltaCount++;
                    } else {
                        if(currObj->ho->type ==
                           DifficultyHitObject::TYPE::SLIDER)  // bpm change is into slider, this is easy acc window
                            effectiveRatio *= 0.125;

                        if(prevObj->ho->type ==
                           DifficultyHitObject::TYPE::
                               SLIDER)  // bpm change was from a slider, this is easier typically than circle -> circle
                            effectiveRatio *= 0.3;

                        if(island.deltaCount % 2 ==
                           previousIsland.deltaCount % 2)  // repeated island polarity (2 -> 4, 3 -> 5)
                            effectiveRatio *= 0.5;

                        if(lastDelta > prevDelta + deltaDifferenceEpsilon &&
                           prevDelta >
                               currDelta +
                                   deltaDifferenceEpsilon)  // previous increase happened a note ago, 1/1->1/2-1/4, dont want to buff this.
                            effectiveRatio *= 0.125;

                        if(previousIsland.deltaCount ==
                           island.deltaCount)  // repeated island size (ex: triplet -> triplet)
                            effectiveRatio *= 0.5;

                        std::pair<RhythmIsland, int> *islandCount = nullptr;
                        for(auto &i : DifficultyCalculator::islandCounts) {
                            if(i.first.equals(island, deltaDifferenceEpsilon)) {
                                islandCount = &i;
                                break;
                            }
                        }

                        if(islandCount != nullptr) {
                            // only add island to island counts if they're going one after another
                            if(previousIsland.equals(island, deltaDifferenceEpsilon)) islandCount->second++;

                            // repeated island (ex: triplet -> triplet)
                            static constexpr f64 E = 2.7182818284590451;
                            f64 power = 2.75 / (1.0 + std::pow(E, 14.0 - (0.24 * island.delta)));
                            effectiveRatio *=
                                std::min(3.0 / islandCount->second, std::pow(1.0 / islandCount->second, power));
                        } else {
                            DifficultyCalculator::islandCounts.emplace_back(island, 1);
                        }

                        // scale down the difficulty if the object is doubletappable
                        f64 doubletapness = prevObj->get_doubletapness(currObj, hitWindow300);
                        effectiveRatio *= 1.0 - doubletapness * 0.75;

                        rhythmComplexitySum += std::sqrt(effectiveRatio * startRatio) * currHistoricalDecay;

                        startRatio = effectiveRatio;

                        previousIsland = island;

                        if(prevDelta + deltaDifferenceEpsilon < currDelta)  // we're slowing down, stop counting
                            firstDeltaSwitch =
                                false;  // if we're speeding up, this stays true and  we keep counting island size.

                        island = RhythmIsland{std::max((i32)currDelta, 25), 1};
                    }
                } else if(prevDelta > currDelta + deltaDifferenceEpsilon)  // we want to be speeding up.
                {
                    // Begin counting island until we change speed again.
                    firstDeltaSwitch = true;

                    if(currObj->ho->type ==
                       DifficultyHitObject::TYPE::SLIDER)  // bpm change is into slider, this is easy acc window
                        effectiveRatio *= 0.6;

                    if(prevObj->ho->type ==
                       DifficultyHitObject::TYPE::
                           SLIDER)  // bpm change was from a slider, this is easier typically than circle -> circle
                        effectiveRatio *= 0.6;

                    startRatio = effectiveRatio;

                    island = RhythmIsland{std::max((i32)currDelta, 25), 1};
                }

                lastObj = prevObj;
                prevObj = currObj;
            }

            // produces multiplier that can be applied to strain. range [1, infinity) (not really though)
            rhythm = (std::sqrt(4.0 + rhythmComplexitySum * rhythm_overall_multiplier) / 2.0) *
                     (1.0 - get_doubletapness(get_next(0), hitWindow300));

            DifficultyCalculator::islandCounts.clear();
            return raw_speed_strain;
        } break;

        case Skills::AIM_SLIDERS:
        case Skills::AIM_NO_SLIDERS: {
            // https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Evaluators/AimEvaluator.cs
            static constexpr f64 wide_angle_multiplier = 1.5;
            static constexpr f64 acute_angle_multiplier = 2.55;
            static constexpr f64 slider_multiplier = 1.35;
            static constexpr f64 velocity_change_multiplier = 0.75;
            static constexpr f64 wiggle_multiplier = 1.02;

            const bool withSliders = (diff_type == Skills::AIM_SLIDERS);

            if(ho->type == DifficultyHitObject::TYPE::SPINNER || prevObjectIndex <= 1 ||
               prev.ho->type == DifficultyHitObject::TYPE::SPINNER)
                return 0.0;

            static constexpr auto calcWideAngleBonus =
                +[](f64 angle) { return smoothstep(angle, 40.0 * (PI / 180.0), 140.0 * (PI / 180.0)); };
            static constexpr auto calcAcuteAngleBonus =
                +[](f64 angle) { return smoothstep(angle, 140.0 * (PI / 180.0), 40.0 * (PI / 180.0)); };

            const DiffObject *prevPrev = get_previous(1);
            const DiffObject *prev2 = get_previous(2);

            f64 currVelocity = jumpDistance / adjusted_delta_time;

            if(prev.ho->type == DifficultyHitObject::TYPE::SLIDER && withSliders) {
                f64 travelVelocity = prev.travelDistance / prev.travelTime;
                f64 movementVelocity = minJumpDistance / minJumpTime;
                currVelocity = std::max(currVelocity, movementVelocity + travelVelocity);
            }
            f64 aimStrain = currVelocity;

            f64 prevVelocity = prev.jumpDistance / prev.adjusted_delta_time;
            if(prevPrev->ho->type == DifficultyHitObject::TYPE::SLIDER && withSliders) {
                f64 travelVelocity = prevPrev->travelDistance / prevPrev->travelTime;
                f64 movementVelocity = prev.minJumpDistance / prev.minJumpTime;
                prevVelocity = std::max(prevVelocity, movementVelocity + travelVelocity);
            }

            f64 wideAngleBonus = 0;
            f64 acuteAngleBonus = 0;
            f64 sliderBonus = 0;
            f64 velocityChangeBonus = 0;
            f64 wiggleBonus = 0;

            if(!std::isnan(angle) && !std::isnan(prev.angle)) {
                f64 angleBonus = std::min(currVelocity, prevVelocity);

                if(std::max(adjusted_delta_time, prev.adjusted_delta_time) <
                   1.25 * std::min(adjusted_delta_time, prev.adjusted_delta_time)) {
                    acuteAngleBonus = calcAcuteAngleBonus(angle);
                    acuteAngleBonus *=
                        0.08 + 0.92 * (1.0 - std::min(acuteAngleBonus, std::pow(calcAcuteAngleBonus(prev.angle), 3.0)));
                    acuteAngleBonus *= angleBonus * smootherStep(60000.0 / (adjusted_delta_time * 2.0), 300.0, 400.0) *
                                       smootherStep(jumpDistance, 100.0, 200.0);
                }

                wideAngleBonus = calcWideAngleBonus(angle);
                wideAngleBonus *= 1.0 - std::min(wideAngleBonus, pow(calcWideAngleBonus(prev.angle), 3.0));
                wideAngleBonus *= angleBonus * smootherStep(jumpDistance, 0.0, 100.0);

                wiggleBonus = angleBonus * smootherStep(jumpDistance, 50.0, 100.0) *
                              pow(reverseLerp(jumpDistance, 300.0, 100.0), 1.8) *
                              smootherStep(angle, 110.0 * (PI / 180.0), 60.0 * (PI / 180.0)) *
                              smootherStep(prev.jumpDistance, 50.0, 100.0) *
                              pow(reverseLerp(prev.jumpDistance, 300.0, 100.0), 1.8) *
                              smootherStep(prev.angle, 110.0 * (PI / 180.0), 60.0 * (PI / 180.0));

                if(prev2 != nullptr) {
                    f32 distance = vec::length(prev.ho->pos - prev2->ho->pos);
                    if(distance < 1.0) wideAngleBonus *= 1.0 - 0.35 * (1.0 - distance);
                }
            }

            if(std::max(prevVelocity, currVelocity) != 0.0) {
                prevVelocity = (prev.jumpDistance + prevPrev->travelDistance) / prev.adjusted_delta_time;
                currVelocity = (jumpDistance + prev.travelDistance) / adjusted_delta_time;

                f64 distRatio =
                    smoothstep(std::abs(prevVelocity - currVelocity) / std::max(prevVelocity, currVelocity), 0, 1);
                f64 overlapVelocityBuff = std::min(125.0 / std::min(adjusted_delta_time, prev.adjusted_delta_time),
                                                   std::abs(prevVelocity - currVelocity));
                velocityChangeBonus = overlapVelocityBuff * distRatio *
                                      std::pow(std::min(adjusted_delta_time, prev.adjusted_delta_time) /
                                                   std::max(adjusted_delta_time, prev.adjusted_delta_time),
                                               2.0);
            }

            if(prev.ho->type == DifficultyHitObject::TYPE::SLIDER) sliderBonus = prev.travelDistance / prev.travelTime;

            aimStrain += wiggleBonus * wiggle_multiplier;
            aimStrain += velocityChangeBonus * velocity_change_multiplier;
            aimStrain += std::max(acuteAngleBonus * acute_angle_multiplier, wideAngleBonus * wide_angle_multiplier);

            // Apply high circle size bonus
            aimStrain *= smallCircleBonus;

            if(withSliders) aimStrain += sliderBonus * slider_multiplier;

            return aimStrain;
        } break;
    }

    return 0.0;
}

f64 DifficultyCalculator::DiffObject::get_doubletapness(const DifficultyCalculator::DiffObject *next,
                                                        f64 hitWindow300) const {
    if(next != nullptr) {
        f64 cur_delta = std::max(1.0, delta_time);
        f64 next_delta = std::max(1, next->ho->time - ho->time);  // NOTE: next delta time isn't initialized yet
        f64 delta_diff = std::abs(next_delta - cur_delta);
        f64 speedRatio = cur_delta / std::max(cur_delta, delta_diff);
        f64 windowRatio = std::pow(std::min(1.0, cur_delta / hitWindow300), 2.0);

        return 1.0 - std::pow(speedRatio, 1.0 - windowRatio);
    }
    return 0.0;
}

std::string DifficultyCalculator::PPv2CalcParamsToString(const PPv2CalcParams &pars) {
    const auto &attrs = pars.attributes;
    return FORMAT_STRING_(R"(pars.attrs.AimDifficulty: {}
attrs.AimDifficultSliderCount: {}
attrs.SpeedDifficulty: {}
attrs.SpeedNoteCount: {}
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
                          attrs.AimDifficulty, attrs.AimDifficultSliderCount, attrs.SpeedDifficulty,
                          attrs.SpeedNoteCount, attrs.SliderFactor, attrs.AimTopWeightedSliderFactor,
                          attrs.SpeedTopWeightedSliderFactor, attrs.AimDifficultStrainCount,
                          attrs.SpeedDifficultStrainCount, attrs.NestedScorePerObject, attrs.LegacyScoreBaseMultiplier,
                          attrs.SliderCount, attrs.MaximumLegacyComboScore, attrs.ApproachRate, attrs.OverallDifficulty,
                          (u64)pars.modFlags, pars.timescale, pars.ar, pars.od, pars.numHitObjects, pars.numCircles,
                          pars.numSliders, pars.numSpinners, pars.maxPossibleCombo, pars.combo, pars.misses, pars.c300,
                          pars.c100, pars.c50, pars.legacyTotalScore);
}