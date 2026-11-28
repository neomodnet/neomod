#include "HitObjects.h"
#include "Algorithms.h"

#include <cmath>
#include <utility>
#include <type_traits>
#include <variant>

#include "GameplayJudge.h"
#include "AnimationHandler.h"
#include "BeatmapPrimitives.h"
#include "BeatmapStacking.h"
#include "ContainerRanges.h"
#include "Graphics.h"
#include "Bancho.h"
#include "OsuConVars.h"
#include "Engine.h"
#include "GameRules.h"
#include "HUD.h"
#include "InlineVec.h"
#include "ModFPoSu.h"
#include "Font.h"
#include "VertexArrayObject.h"
#include "DatabaseBeatmap.h"
#include "PlayfieldView.h"
#include "Replay.h"
#include "RenderTarget.h"
#include "ResourceManager.h"
#include "Skin.h"
#include "SkinImage.h"
#include "SliderRenderer.h"
#include "score.h"
#include "Logging.h"
#include "HitSounds.h"
#include "ReplayFile.h"
#include "crypto.h"

namespace neomod {
using namespace flags::operators;

namespace {
// what a hit animation started at its hit (from 0.001, so it shows from the hit's frame on) is at elapsedMS of durationMS
f32 hitAnimationAt(i32 elapsedMS, i32 durationMS) {
    if(elapsedMS >= durationMS) return 1.0f;
    const f32 eased = anim::ease(anim::QuadOut, (f32)elapsedMS / (f32)durationMS);
    return 0.001f * (1.0f - eased) + eased;
}

// a trail (see HitObjectDrawing::pose()) elapsedMS after the hit: the circle alpha, from 1 to 0 over durationMS
f32 trailAlphaAt(i32 elapsedMS, i32 durationMS) {
    return 1.0f - Mc::clamp((f32)elapsedMS / (f32)durationMS, 0.0f, 1.0f);
}

// and its approach circle's scale, a tenth larger over the first 100 ms
f32 trailApproachScaleAt(i32 elapsedMS) { return 1.0f + 0.1f * Mc::clamp((f32)elapsedMS / 100.0f, 0.0f, 1.0f); }

f32 lerp3f(f32 a, f32 b, f32 c, f32 percent) {
    if(percent <= 0.5f)
        return std::lerp(a, b, percent * 2.0f);
    else
        return std::lerp(b, c, (percent - 0.5f) * 2.0f);
}

// where a slider is along its curve at posMS (0 to 1, back and forth over its repeats)
f32 sliderT(const SliderState &s, i32 clickTimeMS, i32 posMS) {
    const f32 t = (f32)((i32)posMS - clickTimeMS) / s.sliderTimeMSWithoutRepeats;
    const auto floorVal = (f32)std::floor(t);
    return ((i32)floorVal % 2 == 0) ? t - floorVal : floorVal + 1 - t;
}
}  // namespace

void HitObjectDrawing::drawHitResult(const PlayfieldView &view, vec2 pos, LiveHitResult result, f32 animPercentInv,
                                     f32 hitDeltaRangePercent) {
    if(animPercentInv <= 0.0f) return;

    const Skin *skin = view.getSkin();
    const f32 hitcircleDiameter = view.getHitcircleDiameter();
    const f32 rawHitcircleDiameter = view.getRawHitcircleDiameter();
    const f32 animPercent = 1.0f - animPercentInv;

    const f32 fadeInEndPercent = cv::hitresult_fadein_duration.getFloat() / cv::hitresult_duration.getFloat();

    // determine color/transparency
    {
        if(!cv::hitresult_delta_colorize.getBool() || result == LiveHitResult::HIT_MISS)
            g->setColor(0xffffffff);
        else {
            // NOTE: hitDeltaRangePercent is within -1.0f to 1.0f
            // -1.0f means early miss
            // 1.0f means late miss
            // -0.999999999f means early 50
            // 0.999999999f means late 50
            // percentage scale is linear with respect to the entire hittable 50s range in both directions (contrary to
            // OD brackets which are nonlinear of course)
            if(hitDeltaRangePercent != 0.0f) {
                hitDeltaRangePercent = Mc::clamp<f32>(
                    hitDeltaRangePercent * cv::hitresult_delta_colorize_multiplier.getFloat(), -1.0f, 1.0f);

                const f32 rf = lerp3f(cv::hitresult_delta_colorize_early_r.getFloat() / 255.0f, 1.0f,
                                      cv::hitresult_delta_colorize_late_r.getFloat() / 255.0f,
                                      cv::hitresult_delta_colorize_interpolate.getBool()
                                          ? hitDeltaRangePercent / 2.0f + 0.5f
                                          : (hitDeltaRangePercent < 0.0f ? -1.0f : 1.0f));
                const f32 gf = lerp3f(cv::hitresult_delta_colorize_early_g.getFloat() / 255.0f, 1.0f,
                                      cv::hitresult_delta_colorize_late_g.getFloat() / 255.0f,
                                      cv::hitresult_delta_colorize_interpolate.getBool()
                                          ? hitDeltaRangePercent / 2.0f + 0.5f
                                          : (hitDeltaRangePercent < 0.0f ? -1.0f : 1.0f));
                const f32 bf = lerp3f(cv::hitresult_delta_colorize_early_b.getFloat() / 255.0f, 1.0f,
                                      cv::hitresult_delta_colorize_late_b.getFloat() / 255.0f,
                                      cv::hitresult_delta_colorize_interpolate.getBool()
                                          ? hitDeltaRangePercent / 2.0f + 0.5f
                                          : (hitDeltaRangePercent < 0.0f ? -1.0f : 1.0f));

                g->setColor(argb(1.0f, rf, gf, bf));
            }
        }

        const f32 fadeOutStartPercent = cv::hitresult_fadeout_start_time.getFloat() / cv::hitresult_duration.getFloat();
        const f32 fadeOutDurationPercent =
            cv::hitresult_fadeout_duration.getFloat() / cv::hitresult_duration.getFloat();

        g->setAlpha(Mc::clamp<f32>(animPercent < fadeInEndPercent
                                       ? animPercent / fadeInEndPercent
                                       : 1.0f - ((animPercent - fadeOutStartPercent) / fadeOutDurationPercent),
                                   0.0f, 1.0f));
    }

    g->pushTransform();
    {
        const f32 osuCoordScaleMultiplier = hitcircleDiameter / rawHitcircleDiameter;

        bool doScaleOrRotateAnim = true;
        bool hasParticle = true;
        f32 hitImageScale = 1.0f;

        switch(result) {
            using enum LiveHitResult;
            case HIT_MISS:
                doScaleOrRotateAnim = skin->i_hit0.getNumImages() == 1;
                hitImageScale = (rawHitcircleDiameter / skin->i_hit0.getSizeBaseRaw().x) * osuCoordScaleMultiplier;
                break;

            case HIT_50:
                doScaleOrRotateAnim = skin->i_hit50.getNumImages() == 1;
                hasParticle = skin->i_particle50 != MISSING_TEXTURE;
                hitImageScale = (rawHitcircleDiameter / skin->i_hit50.getSizeBaseRaw().x) * osuCoordScaleMultiplier;
                break;

            case HIT_100:
                doScaleOrRotateAnim = skin->i_hit100.getNumImages() == 1;
                hasParticle = skin->i_particle100 != MISSING_TEXTURE;
                hitImageScale = (rawHitcircleDiameter / skin->i_hit100.getSizeBaseRaw().x) * osuCoordScaleMultiplier;
                break;

            case HIT_300:
                doScaleOrRotateAnim = skin->i_hit300.getNumImages() == 1;
                hasParticle = skin->i_particle300 != MISSING_TEXTURE;
                hitImageScale = (rawHitcircleDiameter / skin->i_hit300.getSizeBaseRaw().x) * osuCoordScaleMultiplier;
                break;

            case HIT_100K:
                doScaleOrRotateAnim = skin->i_hit100k.getNumImages() == 1;
                hasParticle = skin->i_particle100 != MISSING_TEXTURE;
                hitImageScale = (rawHitcircleDiameter / skin->i_hit100k.getSizeBaseRaw().x) * osuCoordScaleMultiplier;
                break;

            case HIT_300K:
                doScaleOrRotateAnim = skin->i_hit300k.getNumImages() == 1;
                hasParticle = skin->i_particle300 != MISSING_TEXTURE;
                hitImageScale = (rawHitcircleDiameter / skin->i_hit300k.getSizeBaseRaw().x) * osuCoordScaleMultiplier;
                break;

            case HIT_300G:
                doScaleOrRotateAnim = skin->i_hit300g.getNumImages() == 1;
                hasParticle = skin->i_particle300 != MISSING_TEXTURE;
                hitImageScale = (rawHitcircleDiameter / skin->i_hit300g.getSizeBaseRaw().x) * osuCoordScaleMultiplier;
                break;

            default:
                break;
        }

        // non-misses have a special scale animation (the type of which depends on hasParticle)
        f32 scale = 1.0f;
        if(doScaleOrRotateAnim && cv::hitresult_animated.getBool()) {
            if(!hasParticle) {
                if(animPercent < fadeInEndPercent * 0.8f)
                    scale = std::lerp(0.6f, 1.1f, Mc::clamp<f32>(animPercent / (fadeInEndPercent * 0.8f), 0.0f, 1.0f));
                else if(animPercent < fadeInEndPercent * 1.2f)
                    scale = std::lerp(1.1f, 0.9f,
                                      Mc::clamp<f32>((animPercent - fadeInEndPercent * 0.8f) /
                                                         (fadeInEndPercent * 1.2f - fadeInEndPercent * 0.8f),
                                                     0.0f, 1.0f));
                else if(animPercent < fadeInEndPercent * 1.4f)
                    scale = std::lerp(0.9f, 1.0f,
                                      Mc::clamp<f32>((animPercent - fadeInEndPercent * 1.2f) /
                                                         (fadeInEndPercent * 1.4f - fadeInEndPercent * 1.2f),
                                                     0.0f, 1.0f));
            } else
                scale = std::lerp(0.9f, 1.05f, Mc::clamp<f32>(animPercent, 0.0f, 1.0f));

            // TODO: osu draws an additive copy of the hitresult on top (?) with 0.5 alpha anim and negative timing, if
            // the skin hasParticle. in this case only the copy does the wobble anim, while the main result just scales
        }

        switch(result) {
            using enum LiveHitResult;

            case HIT_MISS: {
                // special case: animated misses don't move down, and skins with version <= 1 also don't move down
                vec2 downAnim{0.f};
                if(skin->i_hit0.getNumImages() < 2 && skin->version > 1.0f)
                    downAnim.y =
                        std::lerp(-5.0f, 40.0f, Mc::clamp<f32>(animPercent * animPercent * animPercent, 0.0f, 1.0f)) *
                        osuCoordScaleMultiplier;

                f32 missScale = 1.0f + Mc::clamp<f32>((1.0f - (animPercent / fadeInEndPercent)), 0.0f, 1.0f) *
                                           (cv::hitresult_miss_fadein_scale.getFloat() - 1.0f);
                if(!cv::hitresult_animated.getBool()) missScale = 1.0f;

                // TODO: rotation anim (only for all non-animated skins), rot = rng(-0.15f, 0.15f), anim1 = 120 ms to
                // rot, anim2 = rest to rot*2, all ease in

                skin->i_hit0.drawRaw(pos + downAnim, (doScaleOrRotateAnim ? missScale : 1.0f) * hitImageScale *
                                                         cv::hitresult_scale.getFloat());
            } break;

            case HIT_50:
                skin->i_hit50.drawRaw(
                    pos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                break;

            case HIT_100:
                skin->i_hit100.drawRaw(
                    pos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                break;

            case HIT_300:
                if(cv::hitresult_draw_300s.getBool()) {
                    skin->i_hit300.drawRaw(
                        pos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                }
                break;

            case HIT_100K:
                skin->i_hit100k.drawRaw(
                    pos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                break;

            case HIT_300K:
                if(cv::hitresult_draw_300s.getBool()) {
                    skin->i_hit300k.drawRaw(
                        pos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                }
                break;

            case HIT_300G:
                if(cv::hitresult_draw_300s.getBool()) {
                    skin->i_hit300g.drawRaw(
                        pos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                }
                break;

            default:
                break;
        }
    }
    g->popTransform();
}

namespace {
void drawHitResultAnim(const PlayfieldView &view, const HitObjects &objects, uSz index,
                       const HitResultAnim &hitresultanim) {
    if((hitresultanim.timeSecs - cv::hitresult_duration.getFloat()) <
           engine->getTime()  // NOTE: this is written like that on purpose, don't change it ("future" results can be
                              // scheduled with it, e.g. for slider end)
       && (hitresultanim.timeSecs + cv::hitresult_duration_max.getFloat() * (1.0f / view.getBaseAnimationSpeed())) >
              engine->getTime()) {
        const Skin *skin = view.getSkin();
        const f32 skinSpeedMultiplier = skin->anim_speed;

        const i32 skinAnimationTimeStartOffset = objects.clickTimes[index] +
                                                 (hitresultanim.addObjectDurationToSkinAnimationTimeStartOffset
                                                      ? (objects.endTimes[index] - objects.clickTimes[index])
                                                      : 0) +
                                                 hitresultanim.deltaMS;

        for(SkinImage *image : {&skin->i_hit0, &skin->i_hit50, &skin->i_hit100, &skin->i_hit100k, &skin->i_hit300,
                                &skin->i_hit300g, &skin->i_hit300k}) {
            image->setAnimationTimeOffset(skinSpeedMultiplier, skinAnimationTimeStartOffset);
            image->setAnimationFrameClampUp();
        }

        const f32 animPercentInv =
            1.0f - (((engine->getTime() - hitresultanim.timeSecs) * view.getBaseAnimationSpeed()) /
                    cv::hitresult_duration.getFloat());

        HitObjectDrawing::drawHitResult(view, view.osuCoords2Pixels(hitresultanim.rawPos), hitresultanim.result,
                                        animPercentInv, hitresultanim.deltaRangePercent);
    }
}
}  // namespace

namespace {
void drawHitResults(const PlayfieldView &view, const HitObjects &objects, uSz index) {
    const HitObjectLook &l = objects.looks[index];
    drawHitResultAnim(view, objects, index, l.result1);
    drawHitResultAnim(view, objects, index, l.result2);
}
}  // namespace

namespace {
bool isVisibleWith(const HitObjects &objects, uSz index, f64 curPosMS, ModFlags mods, i32 approachTimeMS) {
    const i32 visibleTms =
        (flags::has<ModFlags::FreezeFrame>(mods) ? objects.comboStartTimes[index] : objects.clickTimes[index]);
    // 1 ms fudge by using >=, shouldn't really be a problem
    return curPosMS >= (visibleTms - approachTimeMS) && curPosMS < objects.endTimes[index];
}

i32 lookApproachTime(const HitObjectLook &l, i32 fadeInTimeMS, f32 approachTimeMS) {
    return l.useFadeInTimeAsApproachTime ? fadeInTimeMS : (i32)approachTimeMS;
}

i32 fadeInTimeFor(f32 speedAdjustedAnimationSpeed) {
    const f64 animationSpeedMultiplier = speedAdjustedAnimationSpeed;
    return GameRules::getFadeInTime() * animationSpeedMultiplier;
}

// the fades, approach, Hidden, the hittable dim and visibility at curPosMS
void updateLook(HitObjects &objects, uSz index, f64 curPosMS, ModFlags mods, f32 approachTimeMS, i32 fadeInTimeMS) {
    HitObjectLook &l = objects.looks[index];
    l.alphaForApproachCircle = 0.0f;
    l.hittableDimRGBColorMultiplierPct = 1.0f;

    const i32 visibleTms =
        (flags::has<ModFlags::FreezeFrame>(mods) ? objects.comboStartTimes[index] : objects.clickTimes[index]);
    l.fadeInTimeMS = fadeInTimeMS;
    l.approachTimeMS = lookApproachTime(l, l.fadeInTimeMS, approachTimeMS);
    l.deltaMS = objects.clickTimes[index] - curPosMS;

    if(isVisibleWith(objects, index, curPosMS, mods, l.approachTimeMS)) {
        // approach circle scale
        // freeze frame keeps the usual shrink rate, so objects later in the combo start out larger
        const f32 scale = Mc::max(0.0f, (f32)(objects.clickTimes[index] - curPosMS) / (f32)l.approachTimeMS);
        l.approachScale = 1 + (scale * cv::approach_scale_multiplier.getFloat());
        if(flags::has<ModFlags::ApproachDifferent>(mods)) {
            constexpr f32 back_const = 1.70158;

            f32 time = 1.0f - Mc::min(scale, 1.0f);
            {
                switch(cv::mod_approach_different_style.getInt()) {
                    default:  // "Linear"
                        break;
                    case 1:  // "Gravity" / InBack
                        time = time * time * ((back_const + 1.0f) * time - back_const);
                        break;
                    case 2:  // "InOut1" / InOutCubic
                        if(time < 0.5f)
                            time = time * time * time * 4.0f;
                        else {
                            --time;
                            time = time * time * time * 4.0f + 1.0f;
                        }
                        break;
                    case 3:  // "InOut2" / InOutQuint
                        if(time < 0.5f)
                            time = time * time * time * time * time * 16.0f;
                        else {
                            --time;
                            time = time * time * time * time * time * 16.0f + 1.0f;
                        }
                        break;
                    case 4:  // "Accelerate1" / In
                        time = time * time;
                        break;
                    case 5:  // "Accelerate2" / InCubic
                        time = time * time * time;
                        break;
                    case 6:  // "Accelerate3" / InQuint
                        time = time * time * time * time * time;
                        break;
                    case 7:  // "Decelerate1" / Out
                        time = time * (2.0f - time);
                        break;
                    case 8:  // "Decelerate2" / OutCubic
                        --time;
                        time = time * time * time + 1.0f;
                        break;
                    case 9:  // "Decelerate3" / OutQuint
                        --time;
                        time = time * time * time * time * time + 1.0f;
                        break;
                }
                // NOTE: some of the easing functions will overflow/underflow, don't clamp and instead allow it on
                // purpose
            }
            l.approachScale = 1 + std::lerp(cv::mod_approach_different_initial_size.getFloat() - 1.0f, 0.0f, time);
        }

        // hitobject body fadein
        const i32 fadeInStart = visibleTms - l.approachTimeMS;
        // Mc::min() ensures that the fade always finishes at click_time
        // (even if the fadeintime is longer than the approachtime)
        const i32 fadeInEnd = Mc::min(visibleTms, visibleTms - l.approachTimeMS + l.fadeInTimeMS);
        l.alpha = Mc::clamp<f32>(1.0f - ((f32)(fadeInEnd - curPosMS) / (f32)(fadeInEnd - fadeInStart)), 0.0f, 1.0f);
        l.alphaWithoutHidden = l.alpha;

        if(flags::has<ModFlags::FreezeFrame>(mods)) {
            // HACK: set l.alphaWithoutHidden as "alpha without freeze time or hidden"
            //       this makes slider bodies & spinners draw correctly
            const i32 fadeInStart = objects.clickTimes[index] - l.approachTimeMS;
            const i32 fadeInEnd =
                Mc::min(objects.clickTimes[index], objects.clickTimes[index] - l.approachTimeMS + l.fadeInTimeMS);
            l.alphaWithoutHidden =
                Mc::clamp<f32>(1.0f - ((f32)(fadeInEnd - curPosMS) / (f32)(fadeInEnd - fadeInStart)), 0.0f, 1.0f);
        }

        if(flags::has<ModFlags::Hidden>(mods)) {
            // hidden hitobject body fadein
            const f32 fin_start_percent = cv::mod_hd_circle_fadein_start_percent.getFloat();
            const f32 fin_end_percent = cv::mod_hd_circle_fadein_end_percent.getFloat();
            const i32 hiddenFadeInStartMS = visibleTms - (i32)(l.approachTimeMS * fin_start_percent);
            const i32 hiddenFadeInEndMS = visibleTms - (i32)(l.approachTimeMS * fin_end_percent);
            l.alpha = Mc::clamp<f32>(
                1.0f - ((f32)(hiddenFadeInEndMS - curPosMS) / (f32)(hiddenFadeInEndMS - hiddenFadeInStartMS)), 0.0f,
                1.0f);

            // hidden hitobject body fadeout
            const f32 fout_start_percent = cv::mod_hd_circle_fadeout_start_percent.getFloat();
            const f32 fout_end_percent = cv::mod_hd_circle_fadeout_end_percent.getFloat();
            const i32 hiddenFadeOutStart = visibleTms - (i32)(l.approachTimeMS * fout_start_percent);
            const i32 hiddenFadeOutEnd = visibleTms - (i32)(l.approachTimeMS * fout_end_percent);
            if(curPosMS >= hiddenFadeOutStart)
                l.alpha = Mc::clamp<f32>(
                    ((f32)(hiddenFadeOutEnd - curPosMS) / (f32)(hiddenFadeOutEnd - hiddenFadeOutStart)), 0.0f, 1.0f);
        }

        // approach circle fadein (doubled fadeintime)
        const i32 approachCircleFadeStart = visibleTms - l.approachTimeMS;
        const i32 approachCircleFadeEnd =
            Mc::min(visibleTms,
                    visibleTms - l.approachTimeMS +
                        2 * l.fadeInTimeMS);  // Mc::min() ensures that the fade always finishes at click_time
                                              // (even if the fadeintime is longer than the approachtime)
        l.alphaForApproachCircle = Mc::clamp<f32>(
            1.0f - ((f32)(approachCircleFadeEnd - curPosMS) / (f32)(approachCircleFadeEnd - approachCircleFadeStart)),
            0.0f, 1.0f);

        // hittable dim, see https://github.com/ppy/osu/pull/20572
        if(cv::hitobject_hittable_dim.getBool() &&
           (!flags::has<ModFlags::Mafham>(mods) || !cv::mod_mafham_ignore_hittable_dim.getBool())) {
            const i32 hittableDimFadeStart = objects.clickTimes[index] - (i32)GameRules::HITWINDOW_MISS;

            // yes, this means the un-dim animation cuts into the already clickable range
            const i32 hittableDimFadeEnd = hittableDimFadeStart + (i32)cv::hitobject_hittable_dim_duration.getInt();

            l.hittableDimRGBColorMultiplierPct =
                std::lerp(cv::hitobject_hittable_dim_start_percent.getFloat(), 1.0f,
                          Mc::clamp<f32>(1.0f - (f32)(hittableDimFadeEnd - curPosMS) /
                                                    (f32)(hittableDimFadeEnd - hittableDimFadeStart),
                                         0.0f, 1.0f));
        }

        objects.visible[index] = true;
    } else {
        l.approachScale = 1.0f;
        objects.visible[index] = false;
    }
}

void updateSlideLook(HitObjects &objects, uSz index, f64 curPosMS, ModFlags mods) {
    const HitObjectLook &l = objects.looks[index];
    SliderState &s = objects.sliderAt(index);
    const i32 visibleTms =
        flags::has<ModFlags::FreezeFrame>(mods) ? objects.comboStartTimes[index] : objects.clickTimes[index];
    const f32 sliderSnakeDuration = (1.0f / 3.0f) * l.approachTimeMS * cv::slider_snake_duration_multiplier.getFloat();
    s.sliderSnakePercent = Mc::min(1.0f, (f32)(curPosMS - (visibleTms - l.approachTimeMS)) / sliderSnakeDuration);

    const i32 reverseArrowFadeInStart =
        objects.clickTimes[index] -
        (cv::snaking_sliders.getBool() ? (l.approachTimeMS - sliderSnakeDuration) : l.approachTimeMS);
    const i32 reverseArrowFadeInEnd = reverseArrowFadeInStart + cv::slider_reverse_arrow_fadein_duration.getInt();
    s.reverseArrowAlpha = 1.0f - Mc::clamp<f32>(((f32)(reverseArrowFadeInEnd - curPosMS) /
                                                 (f32)(reverseArrowFadeInEnd - reverseArrowFadeInStart)),
                                                0.0f, 1.0f);
    s.reverseArrowAlpha *= cv::slider_reverse_arrow_alpha_multiplier.getFloat();

    s.bodyAlpha = l.alpha;
    if(flags::has<ModFlags::Hidden>(mods)) {  // hidden modifies the body alpha
        s.bodyAlpha = l.alphaWithoutHidden;   // fade in as usual

        // fade out over the duration of the slider, starting exactly when the default fadein finishes
        // Mc::min() ensures that the fade always starts at click_time
        // (even if the fadeintime is longer than the approachtime)
        const i32 hiddenSliderBodyFadeOutStart = Mc::min(visibleTms, visibleTms - l.approachTimeMS + l.fadeInTimeMS);
        const f32 fade_percent = cv::mod_hd_slider_fade_percent.getFloat();
        const i32 hiddenSliderBodyFadeOutEnd = objects.clickTimes[index] + (i32)(fade_percent * s.sliderTimeMS);
        if(curPosMS >= hiddenSliderBodyFadeOutStart) {
            s.bodyAlpha = Mc::clamp<f32>(((f32)(hiddenSliderBodyFadeOutEnd - curPosMS) /
                                          (f32)(hiddenSliderBodyFadeOutEnd - hiddenSliderBodyFadeOutStart)),
                                         0.0f, 1.0f);
            s.bodyAlpha *= s.bodyAlpha;  // quad in body fadeout
        }
    }

    updateSlide(s, objects.clickTimes[index], curPosMS, objects.visible[index]);
}

// the follow circle fades at curPosMS
void updateAnimations(HitObjects &objects, uSz index, f64 curPosMS, f32 speedAdjustedAnimationSpeed) {
    SliderState &s = objects.sliderAt(index);
    f32 animation_multiplier = speedAdjustedAnimationSpeed;

    f32 fadein_fade_time = cv::slider_followcircle_fadein_fade_time.getFloat() * animation_multiplier;
    f32 fadeout_fade_time = cv::slider_followcircle_fadeout_fade_time.getFloat() * animation_multiplier;
    f32 fadein_scale_time = cv::slider_followcircle_fadein_scale_time.getFloat() * animation_multiplier;
    f32 fadeout_scale_time = cv::slider_followcircle_fadeout_scale_time.getFloat() * animation_multiplier;

    // handle followcircle animations
    s.followCircleAnimationAlpha = Mc::clamp<f32>(
        (f32)((curPosMS - objects.clickTimes[index])) / 1000.0f /
            Mc::clamp<f32>(fadein_fade_time, 0.0f, (objects.endTimes[index] - objects.clickTimes[index]) / 1000.0f),
        0.0f, 1.0f);
    if(objects.finished[index]) {
        s.followCircleAnimationAlpha =
            1.0f -
            Mc::clamp<f32>((f32)((curPosMS - (objects.endTimes[index]))) / 1000.0f / fadeout_fade_time, 0.0f, 1.0f);
        s.followCircleAnimationAlpha *= s.followCircleAnimationAlpha;  // quad in
    }

    s.followCircleAnimationScale = Mc::clamp<f32>(
        (f32)((curPosMS - objects.clickTimes[index])) / 1000.0f /
            Mc::clamp<f32>(fadein_scale_time, 0.0f, (objects.endTimes[index] - objects.clickTimes[index]) / 1000.0f),
        0.0f, 1.0f);
    if(objects.finished[index]) {
        s.followCircleAnimationScale =
            Mc::clamp<f32>((f32)((curPosMS - (objects.endTimes[index]))) / 1000.0f / fadeout_scale_time, 0.0f, 1.0f);
    }
    s.followCircleAnimationScale = -s.followCircleAnimationScale * (s.followCircleAnimationScale - 2.0f);  // quad out

    if(!objects.finished[index])
        s.followCircleAnimationScale =
            cv::slider_followcircle_fadein_scale.getFloat() +
            (1.0f - cv::slider_followcircle_fadein_scale.getFloat()) * s.followCircleAnimationScale;
    else
        s.followCircleAnimationScale =
            1.0f - (1.0f - cv::slider_followcircle_fadeout_scale.getFloat()) * s.followCircleAnimationScale;
}

}  // namespace

void updateSlide(SliderState &s, i32 clickTimeMS, f64 curPosMS, bool visible) {
    // slider slide percent
    s.slidePct = 0.0f;
    if(curPosMS > clickTimeMS)
        s.slidePct = Mc::clamp<f32>(
            (f32)Mc::clamp<f64>(curPosMS - clickTimeMS, 0.0, (i32)s.sliderTimeMS) / s.sliderTimeMS, 0.0f, 1.0f);

    // if this slider is active, recalculate sliding/curve position and general state
    if(s.slidePct > 0.0f || visible) {
        // handle reverse sliders
        s.inReverse = false;
        s.hideNumberAfterFirstRepeatHit = false;
        if(s.repeat > 1) {
            if(s.slidePct > 0.0f && s.startFinished) s.hideNumberAfterFirstRepeatHit = true;

            f32 part = 1.0f / (f32)s.repeat;
            s.curRepeat = (i32)(s.slidePct * s.repeat);
            f32 baseSlidePercent = part * s.curRepeat;
            f32 partSlidePercent = (s.slidePct - baseSlidePercent) / part;
            if(s.curRepeat % 2 == 0) {
                s.slidePct = partSlidePercent;
                s.reverseArrowPos = 2;
            } else {
                s.slidePct = 1.0f - partSlidePercent;
                s.reverseArrowPos = 1;
                s.inReverse = true;
            }

            // no reverse arrow on the last repeat
            if(s.curRepeat == s.repeat - 1) s.reverseArrowPos = 0;

            // osu style: immediately show all coming reverse arrows (even on the circle we just started from)
            if(s.curRepeat < s.repeat - 2 && s.slidePct > 0.0f && s.repeat > 2) s.reverseArrowPos = 3;
        }

        s.curPointRaw = s.pointAt(s.slidePct);
    } else {
        s.curPointRaw = s.pointAt(0.0f);
    }
}

namespace HitObjectDrawing {

LookTiming lookTiming(const GameplayJudge &judge, const PlayfieldView *view) {
    const f32 animationSpeed = view ? view->getSpeedAdjustedAnimationSpeed() : judge.getSpeedMultiplier();
    // the view mods include the experimental ones (objects without a view aren't drawn)
    return {.mods = view ? view->getModFlags() : judge.getMods().flags,
            .playMods = judge.getMods().flags,
            .approachTimeMS = judge.getCachedApproachTimeForUpdate(),
            .animationSpeed = animationSpeed,
            .fadeInTimeMS = fadeInTimeFor(animationSpeed),
            .drawn = view != nullptr};
}

void animate(HitObjects &objects, uSz index, f64 curPosMS, const LookTiming &timing) {
    updateLook(objects, index, curPosMS, timing.mods, timing.approachTimeMS, timing.fadeInTimeMS);
    if(objects.types[index] != HitObjectType::SLIDER) return;

    // animations must be updated even if we are finished
    if(timing.drawn) updateAnimations(objects, index, curPosMS, timing.animationSpeed);

    if(!objects.finished[index]) updateSlideLook(objects, index, curPosMS, timing.playMods);
}

bool isVisibleAt(const HitObjects &objects, uSz index, f64 curPosMS, const LookTiming &timing) {
    return isVisibleWith(objects, index, curPosMS, timing.mods,
                         lookApproachTime(objects.looks[index], timing.fadeInTimeMS, timing.approachTimeMS));
}

void pose(HitObjects &objects, uSz index, const PlayfieldView &view, i32 timeMS, i32 fadeOutMS, i32 trailMS) {
    HitObjectLook &l = objects.looks[index];
    switch(objects.types[index]) {
        case HitObjectType::CIRCLE: {
            CircleState &s = objects.circleAt(index);
            updateLook(objects, index, timeMS, view.getModFlags(), view.getApproachTime(),
                       fadeInTimeFor(view.getSpeedAdjustedAnimationSpeed()));

            s.waiting = false;
            objects.finished[index] = timeMS >= objects.clickTimes[index];
            const i32 sinceHitMS = timeMS - objects.clickTimes[index];
            const bool trail = objects.finished[index] && trailMS > 0;
            s.hitAnimation = objects.finished[index] && !trail ? hitAnimationAt(sinceHitMS, fadeOutMS) : 0.0f;
            l.trailAlpha = trail ? trailAlphaAt(sinceHitMS, trailMS) : 0.0f;
            l.trailApproachScale = trail ? trailApproachScaleAt(sinceHitMS) : 1.0f;
            s.shakeAnimation = 0.0f;
        } break;
        case HitObjectType::SLIDER: {
            SliderState &s = objects.sliderAt(index);
            const ModFlags mods = view.getModFlags();
            const f32 animationSpeed = view.getSpeedAdjustedAnimationSpeed();
            updateLook(objects, index, timeMS, mods, view.getApproachTime(), fadeInTimeFor(animationSpeed));

            const i32 endTimeMS = objects.endTimes[index];
            s.startFinished = timeMS >= objects.clickTimes[index];
            objects.finished[index] = timeMS >= endTimeMS;
            s.endFinished = objects.finished[index];
            s.heldTillEnd = true;
            s.endResult = objects.finished[index] ? LiveHitResult::HIT_300 : LiveHitResult::HIT_NULL;
            for(auto &click : s.clicks) {
                click.finished = click.successful = timeMS >= click.timeMS;
            }
            for(auto &tick : s.ticks) {
                tick.finished = true;
            }
            for(const auto &click : s.clicks) {
                if(click.type == 1 && !click.finished) s.ticks[click.tickIndex].finished = false;
            }

            // the slide stops where the end was hit, as in play
            updateSlideLook(objects, index, Mc::min(timeMS, endTimeMS), mods);
            s.tracking = s.startFinished;
            updateAnimations(objects, index, timeMS, animationSpeed);

            // the head at the start, the repeats at theirs and the tail at the end, as onHit() and onRepeatHit() add them
            s.clickAnimations.clear();
            const auto addHitAnimAt = [&](i32 hitTimeMS, u8 typeFlags) {
                if(timeMS < hitTimeMS || timeMS - hitTimeMS >= fadeOutMS || s.clickAnimations.size() >= 128) return;
                s.clickAnimations.push_back(
                    SliderHitAnim{.percent{hitAnimationAt(timeMS - hitTimeMS, fadeOutMS)}, .type{typeFlags}});
            };
            const bool trail = s.startFinished && trailMS > 0;
            if(trailMS <= 0) {
                addHitAnimAt(objects.clickTimes[index], SliderHitAnim::HEAD);
                for(const auto &click : s.clicks) {
                    if(click.type == 0)
                        addHitAnimAt(click.timeMS, click.sliderend ? SliderHitAnim::TAIL : SliderHitAnim::HEAD);
                }
                addHitAnimAt(endTimeMS, s.repeat % 2 != 0 ? SliderHitAnim::TAIL : SliderHitAnim::HEAD);
            }
            l.trailAlpha = trail ? trailAlphaAt(timeMS - objects.clickTimes[index], trailMS) : 0.0f;
            l.trailApproachScale = trail ? trailApproachScaleAt(timeMS - objects.clickTimes[index]) : 1.0f;

            s.endSliderBodyFadeAnimation =
                objects.finished[index]
                    ? hitAnimationAt(timeMS - endTimeMS,
                                     (i32)(fadeOutMS * cv::slider_body_fade_out_time_multiplier.getFloat()))
                    : 0.0f;

            // the follow circle pulses on every tick and repeat
            i32 lastPulseMS = -1;
            for(const auto &click : s.clicks) {
                if(click.finished) lastPulseMS = Mc::max(lastPulseMS, click.timeMS);
            }
            const f32 pulseMS = cv::slider_followcircle_tick_pulse_time.getFloat() * animationSpeed * 1000.0f;
            s.followCircleTickAnimationScale =
                lastPulseMS < 0 ? 0.0f : Mc::clamp<f32>((f32)(timeMS - lastPulseMS) / pulseMS, 0.0f, 1.0f);
        } break;
        case HitObjectType::SPINNER: {
            SpinnerState &s = objects.spinnerAt(index);
            updateLook(objects, index, timeMS, view.getModFlags(), view.getApproachTime(),
                       fadeInTimeFor(view.getSpeedAdjustedAnimationSpeed()));

            objects.finished[index] = timeMS >= objects.endTimes[index];

            // at rest, without a result
            s.drawRot = 0.0f;
            s.rotations = 0.0f;
            s.ratio = 0.0f;
            s.RPM = 0.0f;
            s.completedTimeMS = -1;
            s.firstSpinTimeMS = -1;
            s.bonusTimeMS = -1;
            s.bonusSpins = 0;
            s.hitSuccess = false;
        } break;
    }
}

}  // namespace HitObjectDrawing

namespace Circle {
namespace {
// necessary due to the static draw functions
i32 rainbowNumber = 0;
i32 rainbowColorCounter = 0;
}  // namespace
}  // namespace Circle

void Circle::drawApproachCircle(const PlayfieldView &view, vec2 rawPos, i32 number, i32 colorCounter, i32 colorOffset,
                                f32 colorRGBMultiplier, f32 approachScale, f32 alpha, bool overrideHDApproachCircle) {
    rainbowNumber = number;
    rainbowColorCounter = colorCounter;

    if(flags::has<ModFlags::Mafham>(view.getModFlags())) return;

    Color comboColor = Colors::scale(view.getComboColor(colorCounter, colorOffset),
                                     colorRGBMultiplier * cv::circle_color_saturation.getFloat());

    drawApproachCircle(view.getSkin(), view.osuCoords2Pixels(rawPos), comboColor, view.getHitcircleDiameter(),
                       approachScale, alpha, flags::has<ModFlags::Hidden>(view.getModFlags()),
                       overrideHDApproachCircle);
}

void Circle::drawCircle(const PlayfieldView &view, vec2 rawPos, i32 number, i32 colorCounter, i32 colorOffset,
                        f32 colorRGBMultiplier, f32 /*approachScale*/, f32 alpha, f32 numberAlpha, bool drawNumber,
                        bool /*overrideHDApproachCircle*/) {
    // approach circle
    /// drawApproachCircle(skin, pos, comboColor, hitcircleDiameter, approachScale, alpha, modHD,
    /// overrideHDApproachCircle); // they are now drawn separately in draw2()

    drawCircleIn(view, rawPos, number, colorCounter,
                 Colors::scale(view.getComboColor(colorCounter, colorOffset),
                               colorRGBMultiplier * cv::circle_color_saturation.getFloat()),
                 colorRGBMultiplier, alpha, numberAlpha, drawNumber, false);
}

void Circle::drawCircleIn(const PlayfieldView &view, vec2 rawPos, i32 number, i32 colorCounter, Color color,
                          f32 colorRGBMultiplier, f32 alpha, f32 numberAlpha, bool drawNumber, bool sliderHead) {
    if(alpha <= 0.0f || !cv::draw_circles.getBool()) return;

    rainbowNumber = number;
    rainbowColorCounter = colorCounter;

    const Skin *skin = view.getSkin();
    const vec2 pos = view.osuCoords2Pixels(rawPos);
    const f32 hitcircleDiameter = view.getHitcircleDiameter();

    // if no sliderstartcircle image is preset, fallback to default circle
    const bool startCircle = sliderHead && skin->i_slider_start_circle != MISSING_TEXTURE;
    const BasicSkinImage &circleImage = startCircle ? skin->i_slider_start_circle : skin->i_hitcircle;
    const SkinImage &overlayImage = startCircle ? skin->i_slider_start_circle_overlay2 : skin->i_hitcircleoverlay;
    const bool drawOverlay = !startCircle || skin->i_slider_start_circle_overlay != MISSING_TEXTURE;

    // circle
    const f32 circleImageScale = hitcircleDiameter / (128.0f * (circleImage.scale()));
    drawHitCircle(circleImage, pos, color, circleImageScale, alpha);

    // overlay
    const f32 circleOverlayImageScale = hitcircleDiameter / overlayImage.getSizeBaseRaw().x;
    if(drawOverlay && !skin->o_hitcircle_overlay_above_number)
        drawHitCircleOverlay(overlayImage, pos, circleOverlayImageScale, alpha, colorRGBMultiplier);

    // number
    if(drawNumber)
        drawHitCircleNumber(skin, view.getNumberScale(), view.getHitcircleOverlapScale(), pos, number, numberAlpha,
                            colorRGBMultiplier);

    // overlay
    if(drawOverlay && skin->o_hitcircle_overlay_above_number)
        drawHitCircleOverlay(overlayImage, pos, circleOverlayImageScale, alpha, colorRGBMultiplier);
}

void Circle::drawCircle(const Skin *skin, vec2 pos, f32 hitcircleDiameter, Color color, f32 alpha) {
    // circle
    const f32 circleImageScale = hitcircleDiameter / (128.0f * (skin->i_hitcircle.scale()));
    drawHitCircle(skin->i_hitcircle, pos, color, circleImageScale, alpha);

    // overlay
    const f32 circleOverlayImageScale = hitcircleDiameter / skin->i_hitcircleoverlay.getSizeBaseRaw().x;
    drawHitCircleOverlay(skin->i_hitcircleoverlay, pos, circleOverlayImageScale, alpha, 1.0f);
}

void Circle::drawSliderStartCircle(const PlayfieldView &view, vec2 rawPos, i32 number, i32 colorCounter,
                                   i32 colorOffset, f32 colorRGBMultiplier, f32 /*approachScale*/, f32 alpha,
                                   f32 numberAlpha, bool drawNumber, bool /*overrideHDApproachCircle*/) {
    drawCircleIn(view, rawPos, number, colorCounter,
                 Colors::scale(view.getComboColor(colorCounter, colorOffset),
                               colorRGBMultiplier * cv::circle_color_saturation.getFloat()),
                 colorRGBMultiplier, alpha, numberAlpha, drawNumber, true);
}

void Circle::drawSliderEndCircle(const PlayfieldView &view, vec2 rawPos, i32 number, i32 colorCounter, i32 colorOffset,
                                 f32 colorRGBMultiplier, f32 approachScale, f32 alpha, f32 numberAlpha, bool drawNumber,
                                 bool overrideHDApproachCircle) {
    if(alpha <= 0.0f || !cv::slider_draw_endcircle.getBool() || !cv::draw_circles.getBool()) return;

    const Skin *skin = view.getSkin();

    // if no sliderendcircle image is preset, fallback to default circle
    if(skin->i_slider_end_circle == MISSING_TEXTURE) {
        drawCircle(view, rawPos, number, colorCounter, colorOffset, colorRGBMultiplier, approachScale, alpha,
                   numberAlpha, drawNumber, overrideHDApproachCircle);
        return;
    }

    rainbowNumber = number;
    rainbowColorCounter = colorCounter;

    const vec2 pos = view.osuCoords2Pixels(rawPos);
    const f32 hitcircleDiameter = view.getHitcircleDiameter();

    Color comboColor = Colors::scale(view.getComboColor(colorCounter, colorOffset),
                                     colorRGBMultiplier * cv::circle_color_saturation.getFloat());

    // circle
    const f32 circleImageScale = hitcircleDiameter / (128.0f * (skin->i_slider_end_circle.scale()));
    drawHitCircle(skin->i_slider_end_circle, pos, comboColor, circleImageScale, alpha);

    // overlay
    if(skin->i_slider_end_circle_overlay != MISSING_TEXTURE) {
        const f32 circleOverlayImageScale = hitcircleDiameter / skin->i_slider_end_circle_overlay2.getSizeBaseRaw().x;
        drawHitCircleOverlay(skin->i_slider_end_circle_overlay2, pos, circleOverlayImageScale, alpha,
                             colorRGBMultiplier);
    }
}

void Circle::drawApproachCircle(const Skin *skin, vec2 pos, Color comboColor, f32 hitcircleDiameter, f32 approachScale,
                                f32 alpha, bool modHD, bool overrideHDApproachCircle) {
    if((!modHD || overrideHDApproachCircle) && cv::draw_approach_circles.getBool()) {
        if(approachScale > 1.0f) {
            const f32 approachCircleImageScale = hitcircleDiameter / (128.0f * (skin->i_approachcircle.scale()));

            g->setColor(comboColor);

            if(cv::circle_rainbow.getBool()) {
                const f64 frequency = 0.3;
                const f64 time = engine->getTime() * 20.0;
                const f32 offset = (f32)std::fmod(frequency * time + rainbowNumber * rainbowColorCounter, 2.0 * PI_F);

                f32 red1 = 0.5f + (std::sin(offset + 0) * 0.5f);
                f32 green1 = 0.5f + (std::sin(offset + 2) * 0.5f);
                f32 blue1 = 0.5f + (std::sin(offset + 4) * 0.5f);

                g->setColor(rgb(red1, green1, blue1));
            }

            g->setAlpha(alpha * cv::approach_circle_alpha_multiplier.getFloat());

            g->pushTransform();
            {
                g->scale(approachCircleImageScale * approachScale, approachCircleImageScale * approachScale);
                g->translate(pos.x, pos.y);
                g->drawImage(skin->i_approachcircle);
            }
            g->popTransform();
        }
    }
}

void Circle::drawHitCircleOverlay(const SkinImage &hitCircleOverlayImage, vec2 pos, f32 circleOverlayImageScale,
                                  f32 alpha, f32 colorRGBMultiplier) {
    g->setColor(argb(alpha, colorRGBMultiplier, colorRGBMultiplier, colorRGBMultiplier));
    hitCircleOverlayImage.drawRaw(pos, circleOverlayImageScale);
}

void Circle::drawHitCircle(Image *hitCircleImage, vec2 pos, Color comboColor, f32 circleImageScale, f32 alpha) {
    g->setColor(comboColor);

    if(cv::circle_rainbow.getBool()) {
        const f64 frequency = 0.3;
        const f64 time = engine->getTime() * 20.0;
        const f32 offset =
            (f32)std::fmod(frequency * time + rainbowNumber * rainbowNumber * rainbowColorCounter, 2.0 * PI_F);

        f32 red1 = 0.5f + (std::sin(offset + 0) * 0.5f);
        f32 green1 = 0.5f + (std::sin(offset + 2) * 0.5f);
        f32 blue1 = 0.5f + (std::sin(offset + 4) * 0.5f);

        g->setColor(rgb(red1, green1, blue1));
    }

    g->setAlpha(alpha);

    g->pushTransform();
    {
        g->scale(circleImageScale, circleImageScale);
        g->translate(pos.x, pos.y);
        g->drawImage(hitCircleImage);
    }
    g->popTransform();
}

void Circle::drawHitCircleNumber(const Skin *skin, f32 numberScale, f32 overlapScale, vec2 pos, i32 number,
                                 f32 numberAlpha, f32 /*colorRGBMultiplier*/) {
    if(!cv::draw_numbers.getBool()) return;

    // extract digits
    i32 digits[10];
    i32 digitCount = 0;

    do {
        digits[digitCount++] = number % 10;
        number /= 10;
    } while(number > 0);

    // set color
    // g->setColor(argb(1.0f, colorRGBMultiplier, colorRGBMultiplier, colorRGBMultiplier)); // see
    // https://github.com/ppy/osu/issues/24506
    g->setColor(0xffffffff);
    if(cv::circle_number_rainbow.getBool()) {
        const f64 frequency = 0.3;
        const f64 time = engine->getTime() * 20.0;
        const f32 offset = (f32)std::fmod(
            frequency * time + rainbowNumber * rainbowNumber * rainbowNumber * rainbowColorCounter, 2.0 * PI_F);

        f32 red1 = 0.5f + (std::sin(offset + 0) * 0.5f);
        f32 green1 = 0.5f + (std::sin(offset + 2) * 0.5f);
        f32 blue1 = 0.5f + (std::sin(offset + 4) * 0.5f);

        g->setColor(rgb(red1, green1, blue1));
    }
    g->setAlpha(numberAlpha);

    const auto &defaultImgs = skin->i_defaults;

    // get total width for centering
    f32 digitWidthCombined = 0.0f;
    for(i32 i = 0; i < digitCount; i++) {
        digitWidthCombined += defaultImgs[digits[i]]->getWidth();
    }

    // draw digits, start at correct offset
    g->pushTransform();
    {
        g->scale(numberScale, numberScale);
        g->translate(pos.x, pos.y);

        const i32 digitOverlapCount = digitCount - 1;
        const f32 firstDigitWidth = defaultImgs[digits[digitCount - 1]]->getWidth();
        g->translate(
            -(digitWidthCombined * numberScale - skin->hitcircle_overlap_amt * digitOverlapCount * overlapScale) *
                    0.5f +
                firstDigitWidth * numberScale * 0.5f,
            0);

        // draw from most significant to least significant
        for(i32 i = digitCount - 1; i >= 0; i--) {
            g->drawImage(defaultImgs[digits[i]]);

            f32 offset = defaultImgs[digits[i]]->getWidth() * numberScale;
            if(i > 0) {
                offset += defaultImgs[digits[i - 1]]->getWidth() * numberScale;
            }

            g->translate(offset * 0.5f - skin->hitcircle_overlap_amt * overlapScale, 0);
        }
    }
    g->popTransform();
}

namespace {
void drawCircleObject(const PlayfieldView &view, HitObjects &objects, uSz index) {
    const HitObjectLook &l = objects.looks[index];
    const CircleState &s = objects.circleAt(index);
    const Skin *skin = view.getSkin();

    const ModFlags curGameplayFlags = view.getModFlags();

    if(flags::has<ModFlags::Traceable>(curGameplayFlags)) {
        // draw nothing for traceable (approach circles are drawn in draw2())
        return;
    }

    const bool hd = flags::has<ModFlags::Hidden>(curGameplayFlags);

    const i32 animTimeOffset =
        !view.isInMafhamRenderChunk() ? objects.clickTimes[index] - l.approachTimeMS : view.getJudgeMusicPos();

    // draw hit animation (if not hidden)
    const bool instafade = cv::instafade.getBool() && cv::sv_allow_instafade.getBool();
    if(!hd && !instafade && s.hitAnimation > 0.f && s.hitAnimation != 1.f) {
        f32 alpha = 1.0f - s.hitAnimation;

        f32 scale = s.hitAnimation;
        scale = -scale * (scale - 2.0f);  // quad out scale

        const bool drawNumber = skin->version <= 1.f || !cv::sv_allow_instafade.getBool();
        const f32 foscale = cv::circle_fade_out_scale.getFloat();

        g->pushTransform();
        {
            g->scale((1.0f + scale * foscale), (1.0f + scale * foscale));

            skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
            Circle::drawCircle(view, objects.positions[index], objects.comboNumbers[index],
                               objects.colorCounters[index], objects.colorOffsets[index], 1.0f, 1.0f, alpha, alpha,
                               drawNumber);
        }
        g->popTransform();
    }

    // posed with a trail: white, fading out
    if(l.trailAlpha > 0.0f) {
        skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
        Circle::drawCircleIn(view, objects.positions[index], objects.comboNumbers[index], objects.colorCounters[index],
                             0xffffffff, 1.0f, l.trailAlpha, l.trailAlpha, true, false);
        return;
    }

    if(objects.finished[index] ||
       (!objects.visible[index] &&
        !s.waiting))  // special case needed for when we are past this objects time, but still
                      // within not-miss range, because we still need to draw the object
        return;

    // draw circle
    vec2 shakeCorrectedPos = objects.positions[index];
    if(engine->getTime() < s.shakeAnimation && !view.isInMafhamRenderChunk())  // handle note blocking shaking
    {
        f32 smooth =
            1.0f - ((s.shakeAnimation - engine->getTime()) / cv::circle_shake_duration.getFloat());  // goes from 0 to 1
        if(smooth < 0.5f)
            smooth = smooth / 0.5f;
        else
            smooth = (1.0f - smooth) / 0.5f;
        // (now smooth goes from 0 to 1 to 0 linearly)
        smooth = -smooth * (smooth - 2);  // quad out
        smooth = -smooth * (smooth - 2);  // quad out twice
        shakeCorrectedPos.x += std::sin(engine->getTime() * 120) * smooth * cv::circle_shake_strength.getFloat();
    }
    skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);

    {
        const f32 approachScale = s.waiting && !hd ? 1.0f : l.approachScale;
        const f32 alpha = s.waiting && !hd ? 1.0f : l.alpha;
        const f32 numberAlpha = s.waiting && !hd ? 1.0f : l.alpha;

        Circle::drawCircle(view, shakeCorrectedPos, objects.comboNumbers[index], objects.colorCounters[index],
                           objects.colorOffsets[index], l.hittableDimRGBColorMultiplierPct, approachScale, alpha,
                           numberAlpha, true, l.overrideHDApproachCircle);
    }
}
}  // namespace

namespace {
void drawCircleObject2(const PlayfieldView &view, HitObjects &objects, uSz index) {
    const HitObjectLook &l = objects.looks[index];
    const CircleState &s = objects.circleAt(index);
    drawHitResults(view, objects, index);
    if(l.trailAlpha > 0.0f) {
        Circle::drawApproachCircle(view, objects.positions[index], objects.comboNumbers[index],
                                   objects.colorCounters[index], objects.colorOffsets[index], 1.0f,
                                   l.trailApproachScale, l.trailAlpha);
        return;
    }
    if(objects.finished[index] || (!objects.visible[index] && !s.waiting))
        return;  // special case needed for when we are past this objects time, but still within not-miss range, because
                 // we still need to draw the object

    // draw approach circle
    const bool hd = flags::has<ModFlags::Hidden>(view.getModFlags());

    // HACKHACK: don't fucking change this piece of code here, it fixes a heisenbug
    // (https://github.com/McKay42/McOsu/issues/165)
    if(cv::bug_flicker_log.getBool()) {
        const f32 approachCircleImageScale =
            view.getHitcircleDiameter() / (128.0f * (view.getSkin()->i_approachcircle.scale()));
        debugLog("click_time = {:d}, aScale = {:f}, iScale = {:f}", objects.clickTimes[index], l.approachScale,
                 approachCircleImageScale);
    }

    Circle::drawApproachCircle(view, objects.positions[index], objects.comboNumbers[index],
                               objects.colorCounters[index], objects.colorOffsets[index],
                               l.hittableDimRGBColorMultiplierPct, s.waiting && !hd ? 1.0f : l.approachScale,
                               s.waiting && !hd ? 1.0f : l.alphaForApproachCircle, l.overrideHDApproachCircle);
}
}  // namespace

namespace {
// the drawn part of the curve while snaking in or shrinking
std::pair<f32, f32> snakeRange(const HitObjects &objects, uSz index) {
    const SliderState &s = objects.sliderAt(index);
    f32 from = 0.0f;
    f32 to = cv::snaking_sliders.getBool() ? s.sliderSnakePercent : 1.0f;

    // shrinking sliders
    if(cv::slider_shrink.getBool() && s.reverseArrowPos == 0) {
        from = s.inReverse ? 0.0f : s.slidePct;
        if(s.inReverse) to = s.slidePct;
    }
    return {from, to};
}
}  // namespace

namespace {
void addSliderBody(const PlayfieldView &view, const HitObjects &objects, uSz index, f32 alpha, f32 from, f32 to,
                   SliderRenderer::Batch &batch) {
    const HitObjectLook &l = objects.looks[index];
    const SliderState &s = objects.sliderAt(index);
    Mc::InlineVec<vec2, 3> alwaysPoints;
    // smooth begin/end while snaking/shrinking
    if(cv::slider_body_smoothsnake.getBool()) {
        if(cv::slider_shrink.getBool() && s.sliderSnakePercent > 0.999f) {
            alwaysPoints.push_back(view.osuCoords2Pixels(s.pointAt(s.slidePct)));  // curpoint
            alwaysPoints.push_back(view.osuCoords2Pixels(objects.rawPosAt(
                index, objects.endTimes[index] + 1)));  // endpoint (because setDrawPercent() causes the last
                                                        // circle mesh to become invisible too quickly)
        }
        if(cv::snaking_sliders.getBool() && s.sliderSnakePercent < 1.0f)
            alwaysPoints.push_back(
                view.osuCoords2Pixels(s.pointAt(s.sliderSnakePercent)));  // snakeoutpoint (only while snaking out)
    }

    SliderRenderer::Body body{
        .alwaysPoints = alwaysPoints,
        .hitcircleDiameter = view.getHitcircleDiameter(),
        .from = from,
        .to = to,
        .skinSettings = {view.getSkin()},
        .undimmedColor = view.getComboColor(objects.colorCounters[index], objects.colorOffsets[index]),
        .borderColor = l.tint,
        .colorRGBMultiplier = l.hittableDimRGBColorMultiplierPct,
        .alpha = alpha,
        .sliderTimeForRainbow = objects.clickTimes[index]};

    Mc::InlineVec<vec2, 256> screenPoints;
    if(view.slidersRenderDynamically()) {
        // peppy sliders: the shape changes every frame
        Mc::ranges::assign(screenPoints, s.curve.getPoints());
        for(auto &screenPoint : screenPoints) {
            screenPoint = view.osuCoords2Pixels(screenPoint - s.stackOffset);
        }
        body.points = screenPoints;
    } else {
        // vertex buffered sliders
        // as the base mesh is centered at (0, 0, 0) and in raw osu coordinates, we have to scale and translate it to
        // make it fit the actual desktop playfield
        body.mesh = &s.mesh;
        body.scale = view.getPlayfieldScaleFactor();
        body.translation = view.getPlayfieldCenter();

        if(view.hasFailed())
            body.translation =
                view.osuCoords2Pixels(vec2(GameRules::OSU_COORD_WIDTH / 2, GameRules::OSU_COORD_HEIGHT / 2));

        if(flags::has<ModFlags::FPS>(view.getModFlags())) body.translation += view.getFirstPersonCursorDelta();
    }
    batch.add((u32)index, body);
}
}  // namespace

namespace {
// adds the live body to batch, or the one fading out after the end
void addSliderBody(const PlayfieldView &view, const HitObjects &objects, uSz index, SliderRenderer::Batch &batch) {
    const HitObjectLook &l = objects.looks[index];
    const SliderState &s = objects.sliderAt(index);
    if(s.numCtrlPoints <= 0) return;

    const bool isCompletelyFinished = s.startFinished && s.endFinished && objects.finished[index];
    if((objects.visible[index] || (s.startFinished && !objects.finished[index])) && !isCompletelyFinished) {
        const f32 alpha = (cv::mod_hd_slider_fast_fade.getBool() ? l.alpha : s.bodyAlpha);
        if(alpha <= 0.0f || !cv::slider_draw_body.getBool()) return;

        const auto [from, to] = snakeRange(objects, index);
        addSliderBody(view, objects, index, alpha, from, to, batch);
        return;
    }

    // fading out after the end hit (which finishes the slider, so never together with the live body above)
    const bool slider_fading_out = s.endSliderBodyFadeAnimation > 0.0f && s.endSliderBodyFadeAnimation != 1.0f;
    const bool instafade = cv::instafade_sliders.getBool() && cv::sv_allow_instafade.getBool();
    if(flags::has<ModFlags::Hidden>(view.getModFlags()) || instafade || !slider_fading_out) return;

    if(!cv::slider_shrink.getBool()) {
        addSliderBody(view, objects, index, 1.0f - s.endSliderBodyFadeAnimation, 0, 1, batch);
        return;
    }
    if(!cv::slider_body_lazer_fadeout_style.getBool()) return;

    const vec2 curPoint = view.osuCoords2Pixels(s.pointAt(s.slidePct));
    batch.add((u32)index, SliderRenderer::Body{.alwaysPoints = {&curPoint, 1},
                                               .hitcircleDiameter = view.getHitcircleDiameter(),
                                               .from = 0.0f,
                                               .to = 0.0f,
                                               .skinSettings = {view.getSkin()},
                                               .undimmedColor = view.getComboColor(objects.colorCounters[index],
                                                                                   objects.colorOffsets[index]),
                                               .alpha = 1.0f - s.endSliderBodyFadeAnimation,
                                               .sliderTimeForRainbow = objects.clickTimes[index]});
}
}  // namespace

namespace {
void drawSliderHead(const PlayfieldView &view, const HitObjects &objects, uSz index, f32 alpha) {
    const HitObjectLook &l = objects.looks[index];
    const SliderState &s = objects.sliderAt(index);
    const Skin *skin = view.getSkin();

    if(s.startFinished) {
        const i32 animTimeOffset = !view.isInMafhamRenderChunk() ? objects.clickTimes[index] : view.getJudgeMusicPos();

        skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
        skin->i_slider_end_circle_overlay2.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);

        Circle::drawSliderEndCircle(view, s.pointAt(0.0f), objects.comboNumbers[index], objects.colorCounters[index],
                                    objects.colorOffsets[index], l.hittableDimRGBColorMultiplierPct, 1.0f, alpha, 0.0f,
                                    false, false);
    } else {
        const i32 visibleTms = flags::has<ModFlags::FreezeFrame>(view.getModFlags()) ? objects.comboStartTimes[index]
                                                                                     : objects.clickTimes[index];
        const i32 animTimeOffset =
            !view.isInMafhamRenderChunk() ? visibleTms - l.approachTimeMS : view.getJudgeMusicPos();

        skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
        skin->i_slider_start_circle_overlay2.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);

        Circle::drawSliderStartCircle(view, s.pointAt(0.0f), objects.comboNumbers[index], objects.colorCounters[index],
                                      objects.colorOffsets[index], l.hittableDimRGBColorMultiplierPct, l.approachScale,
                                      alpha, alpha, !s.hideNumberAfterFirstRepeatHit, l.overrideHDApproachCircle);
    }
}
}  // namespace

namespace {
void drawSliderTail(const PlayfieldView &view, const HitObjects &objects, uSz index, f32 alpha, f32 sliderSnake) {
    const HitObjectLook &l = objects.looks[index];
    const SliderState &s = objects.sliderAt(index);
    const Skin *skin = view.getSkin();
    const i32 animTimeOffset =
        !view.isInMafhamRenderChunk() ? objects.clickTimes[index] - l.fadeInTimeMS : view.getJudgeMusicPos();

    skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
    skin->i_slider_end_circle_overlay2.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);

    Circle::drawSliderEndCircle(view, s.pointAt(sliderSnake), objects.comboNumbers[index], objects.colorCounters[index],
                                objects.colorOffsets[index], l.hittableDimRGBColorMultiplierPct, 1.0f, alpha, 0.0f,
                                false, false);
}
}  // namespace

namespace {
void drawSliderObject(const PlayfieldView &view, HitObjects &objects, uSz index, SliderRenderer::Batch &batch) {
    const HitObjectLook &l = objects.looks[index];
    SliderState &s = objects.sliderAt(index);
    if(s.numCtrlPoints <= 0) return;

    const f32 foscale = cv::circle_fade_out_scale.getFloat();
    const Skin *skin = view.getSkin();

    const ModFlags curGameplayFlags = view.getModFlags();

    const bool hd = flags::has<ModFlags::Hidden>(curGameplayFlags);
    const bool tc = flags::has<ModFlags::Traceable>(curGameplayFlags);

    batch.draw((u32)index);

    const bool isCompletelyFinished = s.startFinished && s.endFinished && objects.finished[index];
    // extra possibility to avoid flicker between the visible delay and the fadeout animation below
    const bool drawParts =
        (objects.visible[index] || (s.startFinished && !objects.finished[index])) && !isCompletelyFinished;
    const bool has_points = s.numCtrlPoints > 1;
    if(drawParts) {
        const f32 alpha = (cv::mod_hd_slider_fast_fade.getBool() ? l.alpha : s.bodyAlpha);
        const f32 sliderSnake = snakeRange(objects, index).second;

        // draw slider ticks
        Color tickColor = 0xffffffff;
        tickColor = Colors::scale(tickColor, l.hittableDimRGBColorMultiplierPct);
        const f32 tickImageScale =
            (view.getHitcircleDiameter() / (16.0f * (skin->i_slider_score_point.scale()))) * 0.125f;
        for(const auto &tick : s.ticks) {
            if(tick.finished || tick.percent > sliderSnake) continue;

            vec2 pos = view.osuCoords2Pixels(s.pointAt(tick.percent));

            g->setColor(Color(tickColor).setA(alpha));

            g->pushTransform();
            {
                g->scale(tickImageScale, tickImageScale);
                g->translate(pos.x, pos.y);
                g->drawImage(skin->i_slider_score_point);
            }
            g->popTransform();
        }

        // draw start & end circle (if not traceable)
        if(has_points && !tc) {
            // HACKHACK: very dirty code
            bool sliderRepeatStartCircleFinished = (s.repeat < 2);
            bool sliderRepeatEndCircleFinished = false;
            bool endCircleIsAtActualSliderEnd = true;
            for(const auto &click : s.clicks) {
                // repeats
                if(click.type == 0) {
                    endCircleIsAtActualSliderEnd = click.sliderend;

                    if(endCircleIsAtActualSliderEnd)
                        sliderRepeatEndCircleFinished = click.finished;
                    else
                        sliderRepeatStartCircleFinished = click.finished;
                }
            }

            const bool ifStrictTrackingModShouldDrawEndCircle =
                (!flags::has<ModFlags::StrictTracking>(curGameplayFlags) || s.endResult != LiveHitResult::HIT_MISS);

            const bool draw_end =
                ((!s.endFinished && s.repeat % 2 != 0 && ifStrictTrackingModShouldDrawEndCircle) ||
                 (!sliderRepeatEndCircleFinished &&
                  (ifStrictTrackingModShouldDrawEndCircle || (s.repeat > 1 && endCircleIsAtActualSliderEnd) ||
                   (s.repeat > 1 && std::abs(s.repeat - s.curRepeat) > 2))));

            const bool draw_start =
                (!s.startFinished ||
                 (!sliderRepeatStartCircleFinished &&
                  (ifStrictTrackingModShouldDrawEndCircle || (s.repeat > 1 && !endCircleIsAtActualSliderEnd) ||
                   (s.repeat > 1 && std::abs(s.repeat - s.curRepeat) > 2))) ||
                 (!s.endFinished && s.repeat % 2 == 0 && ifStrictTrackingModShouldDrawEndCircle));

            const f32 circle_alpha = l.alpha;

            // end circle
            if(draw_end) drawSliderTail(view, objects, index, circle_alpha, sliderSnake);

            // start circle
            if(draw_start) drawSliderHead(view, objects, index, circle_alpha);
        }
    }

    // posed with a trail: the head stays, white, fading out (under the reverse arrows, as in osu!stable)
    if(l.trailAlpha > 0.0f && !tc) {
        const i32 animTimeOffset = objects.clickTimes[index] - l.approachTimeMS;
        skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
        skin->i_slider_start_circle_overlay2.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
        Circle::drawCircleIn(view, s.pointAt(0.0f), objects.comboNumbers[index], objects.colorCounters[index],
                             0xffffffff, 1.0f, l.trailAlpha, l.trailAlpha, true, true);
    }

    if(drawParts) {
        // draw reverse arrows
        const bool reversePossible = has_points && s.reverseArrowAlpha > 0.0f;
        const bool reverseEnd = reversePossible && (s.reverseArrowPos == 2 || s.reverseArrowPos == 3);
        const bool reverseStart = reversePossible && (s.reverseArrowPos == 1 || s.reverseArrowPos == 3);
        if(reverseEnd || reverseStart) {
            // if the combo color is nearly white, blacken the reverse arrow
            Color comboColor = view.getComboColor(objects.colorCounters[index], objects.colorOffsets[index]);
            Color reverseArrowColor = 0xffffffff;
            if((comboColor.Rf() + comboColor.Gf() + comboColor.Bf()) / 3.0f >
               cv::slider_reverse_arrow_black_threshold.getFloat())
                reverseArrowColor = 0xff000000;

            reverseArrowColor = Colors::scale(reverseArrowColor, l.hittableDimRGBColorMultiplierPct);

            f32 div = 0.30f;
            f32 pulse = (div - std::fmod(std::abs(view.getCurMusicPos()) / 1000.0f, div)) / div;
            pulse *= pulse;  // quad in

            if(!cv::slider_reverse_arrow_animated.getBool() || view.isInMafhamRenderChunk()) {
                pulse = 0.0f;
            }

            const auto &raImage = skin->i_reversearrow;
            const f32 osuCoordScaleMultiplier = view.getHitcircleDiameter() / view.getRawHitcircleDiameter();
            const f32 reverseArrowImageScale =
                ((view.getRawHitcircleDiameter() / (128.0f * raImage.scale())) * osuCoordScaleMultiplier) *
                (1.0f + pulse * 0.30f);

            // end and/or start
            for(i32 rev = 0; rev < 2; ++rev) {
                const bool isEnd = rev == 0;
                if(isEnd && !reverseEnd) continue;
                if(!isEnd && !reverseStart) continue;
                vec2 pos = view.osuCoords2Pixels(s.pointAt(isEnd ? 1.f : 0.f));
                const f32 rotation = view.osuAngle2PixelAngle(isEnd ? s.curve.getEndAngle() : s.curve.getStartAngle());

                g->setColor(Color(reverseArrowColor).setA(s.reverseArrowAlpha));

                g->pushTransform();
                {
                    g->rotate(rotation);
                    g->scale(reverseArrowImageScale, reverseArrowImageScale);
                    g->translate(pos.x, pos.y);
                    g->drawImage(raImage);
                }
                g->popTransform();
            }
        }
    }

    // draw start/end circle hit animation
    const bool instafade_slider_head = cv::instafade.getBool() && cv::sv_allow_instafade.getBool();

    const bool do_endhit_animations = !tc && !hd && !instafade_slider_head;  // no animations with traceable/hidden here
    const bool do_starthit_animations = do_endhit_animations && cv::slider_sliderhead_fadeout.getBool();
    for(uSz i = 0; i < s.clickAnimations.size();) {
        if(!s.clickAnimations[i].isAnimating()) {
            s.clickAnimations[i] = std::move(s.clickAnimations.back());
            s.clickAnimations.pop_back();
            continue;
        }
        auto &anim = s.clickAnimations[i];
        ++i;

        if(do_starthit_animations && (anim.type & SliderHitAnim::HEAD)) {
            const f32 alpha = 1.0f - anim.percent;
            const f32 number_alpha = alpha;

            f32 scale = anim.percent;
            scale = -scale * (scale - 2.0f);  // quad out scale

            const bool drawNumber = (skin->version <= 1.f || !cv::sv_allow_instafade.getBool()) && s.curRepeat < 1;

            g->pushTransform();
            {
                g->scale((1.0f + scale * foscale), (1.0f + scale * foscale));
                if(s.curRepeat < 1) {
                    const i32 animTimeOffset = !view.isInMafhamRenderChunk()
                                                   ? objects.clickTimes[index] - l.approachTimeMS
                                                   : view.getJudgeMusicPos();

                    skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
                    skin->i_slider_start_circle_overlay2.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);

                    Circle::drawSliderStartCircle(view, s.pointAt(0.0f), objects.comboNumbers[index],
                                                  objects.colorCounters[index], objects.colorOffsets[index], 1.0f, 1.0f,
                                                  alpha, number_alpha, drawNumber);
                } else {
                    const i32 animTimeOffset =
                        !view.isInMafhamRenderChunk() ? objects.clickTimes[index] : view.getJudgeMusicPos();

                    skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
                    skin->i_slider_end_circle_overlay2.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);

                    Circle::drawSliderEndCircle(view, s.pointAt(0.0f), objects.comboNumbers[index],
                                                objects.colorCounters[index], objects.colorOffsets[index], 1.0f, 1.0f,
                                                alpha, alpha, drawNumber);
                }
            }
            g->popTransform();
        }
        if(do_endhit_animations && (anim.type & SliderHitAnim::TAIL)) {
            const f32 alpha = 1.0f - anim.percent;

            f32 scale = anim.percent;
            scale = -scale * (scale - 2.0f);  // quad out scale

            g->pushTransform();
            {
                g->scale((1.0f + scale * foscale), (1.0f + scale * foscale));

                const i32 animTimeOffset = !view.isInMafhamRenderChunk() ? objects.clickTimes[index] - l.fadeInTimeMS
                                                                         : view.getJudgeMusicPos();

                skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
                skin->i_slider_end_circle_overlay2.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);

                Circle::drawSliderEndCircle(view, s.pointAt(1.0f), objects.comboNumbers[index],
                                            objects.colorCounters[index], objects.colorOffsets[index], 1.0f, 1.0f,
                                            alpha, 0.0f, false);
            }
            g->popTransform();
        }
    }
}
}  // namespace

namespace {
void drawSliderObject2(const PlayfieldView &view, const HitObjects &objects, uSz index, bool drawApproachCircle,
                       bool drawOnlyApproachCircle) {
    const HitObjectLook &l = objects.looks[index];
    const SliderState &s = objects.sliderAt(index);
    drawHitResults(view, objects, index);

    const Skin *skin = view.getSkin();

    // HACKHACK: so much code duplication aaaaaaah
    if((objects.visible[index] || (s.startFinished && !objects.finished[index])) &&
       drawApproachCircle)  // extra possibility to avoid flicker between the visible delay and the fadeout
                            // animation below this if block
    {
        if(s.numCtrlPoints > 1) {
            // HACKHACK: very dirty code
            bool sliderRepeatStartCircleFinished = s.repeat < 2;
            for(auto &click : s.clicks) {
                if(click.type == 0) {
                    if(!click.sliderend) sliderRepeatStartCircleFinished = click.finished;
                }
            }

            // start circle
            if(!s.startFinished || !sliderRepeatStartCircleFinished || (!s.endFinished && s.repeat % 2 == 0)) {
                Circle::drawApproachCircle(view, s.pointAt(0.0f), objects.comboNumbers[index],
                                           objects.colorCounters[index], objects.colorOffsets[index],
                                           l.hittableDimRGBColorMultiplierPct, l.approachScale,
                                           l.alphaForApproachCircle, l.overrideHDApproachCircle);
            }
        }
    }

    if(drawApproachCircle && l.trailAlpha > 0.0f) {
        Circle::drawApproachCircle(view, s.pointAt(0.0f), objects.comboNumbers[index], objects.colorCounters[index],
                                   objects.colorOffsets[index], 1.0f, l.trailApproachScale, l.trailAlpha);
    }

    if(drawApproachCircle && drawOnlyApproachCircle) return;

    // draw followcircle
    // HACKHACK: this is not entirely correct (due to s.bHeldTillEnd, if held within 300 range but then released, will
    // flash followcircle at the end)
    bool should_draw_followcircle = (objects.visible[index] && s.tracking);
    should_draw_followcircle |= (objects.finished[index] && s.followCircleAnimationAlpha > 0.0f && s.heldTillEnd);

    if(should_draw_followcircle) {
        vec2 point = view.osuCoords2Pixels(s.curPointRaw);

        // HACKHACK: this is shit
        f32 tickAnimation =
            (s.followCircleTickAnimationScale < 0.1f ? s.followCircleTickAnimationScale / 0.1f
                                                     : (1.0f - s.followCircleTickAnimationScale) / 0.9f);
        if(s.followCircleTickAnimationScale < 0.1f) {
            tickAnimation = -tickAnimation * (tickAnimation - 2.0f);
            tickAnimation = Mc::clamp<f32>(tickAnimation / 0.02f, 0.0f, 1.0f);
        }
        f32 tickAnimationScale = 1.0f + tickAnimation * cv::slider_followcircle_tick_pulse_scale.getFloat();

        g->setColor(Color(0xffffffff).setA(s.followCircleAnimationAlpha));

        skin->i_slider_follow_circle.setAnimationTimeOffset(skin->anim_speed, objects.clickTimes[index]);
        skin->i_slider_follow_circle.drawRaw(
            point,
            (view.getSliderFollowCircleDiameter() / skin->i_slider_follow_circle.getSizeBaseRaw().x) *
                tickAnimationScale * s.followCircleAnimationScale *
                0.85f);  // this is a bit strange, but seems to work perfectly with 0.85
    }

    const bool isCompletelyFinished = s.startFinished && s.endFinished && objects.finished[index];

    // draw sliderb on top of everything
    if((objects.visible[index] || (s.startFinished && !objects.finished[index])) &&
       !isCompletelyFinished)  // extra possibility in the if-block to avoid flicker between the visible
                               // delay and the fadeout animation below this if-block
    {
        if(s.slidePct > 0.0f) {
            // draw sliderb
            vec2 point = view.osuCoords2Pixels(s.curPointRaw);
            vec2 c1 = view.osuCoords2Pixels(s.pointAt(s.slidePct + 0.01f <= 1.0f ? s.slidePct : s.slidePct - 0.01f));
            vec2 c2 = view.osuCoords2Pixels(s.pointAt(s.slidePct + 0.01f <= 1.0f ? s.slidePct + 0.01f : s.slidePct));
            f32 ballAngle = vec::degrees(std::atan2(c2.y - c1.y, c2.x - c1.x));
            if(skin->o_sliderball_flip) ballAngle += (s.curRepeat % 2 == 0) ? 0 : 180;

            g->setColor(skin->o_allow_sliderball_tint
                            ? (cv::slider_ball_tint_combo_color.getBool()
                                   ? view.getComboColor(objects.colorCounters[index], objects.colorOffsets[index])
                                   : skin->c_slider_ball)
                            : rgb(255, 255, 255));
            g->pushTransform();
            {
                g->rotate(ballAngle);
                skin->i_sliderb.setAnimationTimeOffset(skin->anim_speed, objects.clickTimes[index]);
                skin->i_sliderb.drawRaw(point, view.getHitcircleDiameter() / skin->i_sliderb.getSizeBaseRaw().x);
            }
            g->popTransform();
        }
    }
}
}  // namespace

namespace {
void rebuildSliderMesh(const PlayfieldView &view, HitObjects &objects, uSz index) {
    SliderState &s = objects.sliderAt(index);
    // base mesh (background) (raw unscaled, size in raw osu coordinates centered at (0, 0, 0))
    // this mesh needs to be scaled and translated appropriately since we are not 1:1 with the playfield
    const auto rawPoints = s.curve.getPoints();
    std::vector<vec2> osuCoordPoints{rawPoints.begin(), rawPoints.end()};
    for(auto &p : osuCoordPoints) p = view.osuCoords2LegacyPixels(p - s.stackOffset);
    s.mesh = SliderRenderer::generateMesh(view.getScreenSize(), osuCoordPoints, view.getRawHitcircleDiameter(),
                                          /*skipOOBPoints=*/true);
}
}  // namespace

static CONSTINIT VertexArrayObject spinnerMetreVAO{DrawPrimitive::QUADS};

namespace {
void drawSpinnerObject(const PlayfieldView &view, HitObjects &objects, uSz index) {
    const HitObjectLook &l = objects.looks[index];
    SpinnerState &s = objects.spinnerAt(index);
    const f32 fadeOutMultiplier = cv::spinner_fade_out_time_multiplier.getFloat();
    const i32 fadeOutTimeMS =
        (i32)(GameRules::getFadeOutTime(view.getBaseAnimationSpeed()) * 1000.0f * fadeOutMultiplier);
    const f64 deltaEnd = l.deltaMS + (objects.endTimes[index] - objects.clickTimes[index]);

    const Skin *skin = view.getSkin();
    const vec2 center = view.osuCoords2Pixels(objects.positions[index]);

    // osu!stable lays the spinner out in its 640x480 window space (with the spinner centered at (320, 248), which is
    // the playfield center), and draws 1x sprites at 0.625x of that space, see
    // https://osu.ppy.sh/wiki/en/Skinning/osu%21#spinner
    const f32 windowScale = view.getPlayfieldSize().y / (f32)GameRules::OSU_COORD_HEIGHT;
    const f32 spinnerScale = 0.625f * windowScale;
    const auto windowPos = [&](f32 dx, f32 dy) { return center + vec2{dx, dy} * windowScale; };
    const auto drawSprite = [](const BasicSkinImage &img, vec2 pos, f32 scale, f32 rotationDeg = 0.f,
                               AnchorPoint anchor = AnchorPoint::CENTER) {
        g->pushTransform();
        {
            g->rotate(rotationDeg);
            g->scale(scale, scale);
            g->translate(pos.x, pos.y);
            g->drawImage(img, anchor);
        }
        g->popTransform();
    };

    // version 1.0 skins get a "spinner-osu" hit burst (behind everything else) after a successful hit
    if(s.hitSuccess && deltaEnd <= 0 && skin->version < 2.0f && skin->i_spinner_osu != MISSING_TEXTURE) {
        const f32 t = (f32)-deltaEnd / 1000.f;
        const f32 osuAlpha = Mc::clamp<f32>(t / cv::hitresult_fadein_duration.getFloat(), 0.f, 1.f) *
                             (1.f - Mc::clamp<f32>((t - cv::hitresult_fadeout_start_time.getFloat()) /
                                                       cv::hitresult_fadeout_duration.getFloat(),
                                                   0.f, 1.f));
        if(osuAlpha > 0.f) {
            g->setColor(Color(0xffffffff).setA(osuAlpha));
            drawSprite(skin->i_spinner_osu, windowPos(0.f, -68.f), spinnerScale / skin->i_spinner_osu.scale());
        }
    }

    if((objects.finished[index] || !objects.visible[index]) &&
       (std::round(deltaEnd) > 0 || (deltaEnd < -fadeOutTimeMS)))
        return;

    const f64 curPosMS = objects.clickTimes[index] - l.deltaMS;
    const i32 endTimeMS = objects.endTimes[index];
    const f32 percent =
        1.f - Mc::clamp<f32>((f32)-l.deltaMS / (f32)Mc::max(endTimeMS - objects.clickTimes[index], 1), 0.f, 1.f);

    // avoid blinking when effectively paused
    const bool madeProgress = curPosMS != s.lastDrawMusicPosMS;
    s.lastDrawMusicPosMS = curPosMS;

    // only used for fade out anim atm
    const f32 alphaMultiplier =
        Mc::clamp<f32>((deltaEnd < 0 ? 1.f - ((f32)std::abs(deltaEnd) / (f32)fadeOutTimeMS) : 1.f), 0.f, 1.f);
    const f32 alpha = l.alphaWithoutHidden * alphaMultiplier;
    const auto easeOut = [](f32 t) { return 1.f - (1.f - t) * (1.f - t); };

    // the spinner grows until reaching 100% during spinning, depending on how many spins are left
    const f32 clampedRatio = Mc::clamp<f32>(s.ratio, 0.0f, 1.0f);
    const f32 finishScale = 0.80f + easeOut(clampedRatio) * 0.20f;

    // spun out / autopilot spinners are drawn dimmed
    const Color tint = l.tint                                                                    ? *l.tint
                       : flags::any<ModFlags::SpunOut | ModFlags::Autopilot>(view.getModFlags()) ? Color(0xff808080)
                                                                                                 : Color(0xffffffff);

    // "SPIN!" starts fading out on the first (half) spin, but not before 500 ms in, and "CLEAR!" can't show up before
    // that either
    const i32 spinStartMS = s.firstSpinTimeMS < 0 ? -1 : Mc::max(s.firstSpinTimeMS, objects.clickTimes[index] + 500);
    const i32 clearTimeMS = (s.completedTimeMS < 0 || spinStartMS < 0) ? -1 : Mc::max(s.completedTimeMS, spinStartMS);

    if(skin->o_spinner_fade_playfield) {
        // black bars above and below the spinner-background, covering the rest of stable's 4:3 window
        g->setColor(Color(0xff000000).setA(alpha));
        g->fillRect(windowPos(-320.f, -248.f), vec2{640.f, 29.f} * windowScale);
        g->fillRect(windowPos(-320.f, 213.f), vec2{640.f, 19.f} * windowScale);
    }

    // the approach circle is only shown when the disc is actually skinned (peppy removed it from the default skin:
    // https://osu.ppy.sh/community/forums/topics/100765)
    const auto drawSpinnerApproachCircle = [&](const BasicSkinImage &disc) {
        if(flags::has<ModFlags::Hidden>(view.getModFlags()) || disc.isFromDefault() ||
           skin->i_spinner_approach_circle == MISSING_TEXTURE)
            return;

        // shrinks from 1.86x to 0.1x over the duration
        g->setColor(Color(tint).setA(alpha));
        drawSprite(skin->i_spinner_approach_circle, center,
                   spinnerScale / skin->i_spinner_approach_circle.scale() * std::lerp(0.1f, 1.86f, percent));
    };

    const bool oldStyle = skin->i_spinner_bg != MISSING_TEXTURE || skin->version < 2.0f;
    if(oldStyle) {
        if(skin->i_spinner_bg != MISSING_TEXTURE) {
            g->setColor(Color(skin->c_spinner_bg).setA(alpha));
            drawSprite(skin->i_spinner_bg, center, spinnerScale / skin->i_spinner_bg.scale());
        }

        if(skin->i_spinner_circle != MISSING_TEXTURE) {
            g->setColor(Color(tint).setA(alpha));
            drawSprite(skin->i_spinner_circle, center, spinnerScale / skin->i_spinner_circle.scale(), s.drawRot);
        }

        // the metre fills up from the bottom in 10 whole bars spread over its top 692 (1x) pixels, the rest below
        // is always visible. the next bar blinks in randomly (more often the closer it is to filling up), and the top
        // bar keeps blinking at 100%, unless SpinnerNoBlink is set.
        if(skin->i_spinner_metre != MISSING_TEXTURE) {
            const bool blink =
                madeProgress && !objects.finished[index] && !skin->o_spinner_no_blink && !cv::avoid_flashes.getBool();
            i32 progress = (i32)(clampedRatio * 100.f);
            if(blink) progress = Mc::min(progress, 99);
            i32 bars = progress / 10;
            if(blink && (prand() % 10) < (progress % 10)) bars++;

            const f32 width = (f32)skin->i_spinner_metre.getWidth();
            const f32 height = (f32)skin->i_spinner_metre.getHeight();
            const f32 hidden =
                Mc::min(height, 692.f * skin->i_spinner_metre.scale() * (1.f - (f32)bars / 10.f)) / height;
            if(hidden < 1.f) {
                const f32 metreScale = spinnerScale / skin->i_spinner_metre.scale();
                // anchored to the top left corner of stable's 4:3 window, not to the spinner itself
                const vec2 topLeft = windowPos(-320.f, -219.f);

                g->setColor(Color(tint).setA(alpha));
                g->pushTransform();
                {
                    g->scale(metreScale, metreScale);
                    g->translate(topLeft.x, topLeft.y);

                    spinnerMetreVAO.clear();
                    spinnerMetreVAO.addVertex(0.f, height * hidden);
                    spinnerMetreVAO.addTexcoord(0.f, hidden);
                    spinnerMetreVAO.addVertex(0.f, height);
                    spinnerMetreVAO.addTexcoord(0.f, 1.f);
                    spinnerMetreVAO.addVertex(width, height);
                    spinnerMetreVAO.addTexcoord(1.f, 1.f);
                    spinnerMetreVAO.addVertex(width, height * hidden);
                    spinnerMetreVAO.addTexcoord(1.f, hidden);

                    skin->i_spinner_metre.bind();
                    g->drawVAO(&spinnerMetreVAO);
                    skin->i_spinner_metre.unbind();
                }
                g->popTransform();
            }
        }

        drawSpinnerApproachCircle(skin->i_spinner_circle);
    } else {  // new style
        const f32 topRotation = s.drawRot * (skin->i_spinner_middle2 != MISSING_TEXTURE ? 0.5f : 1.f);

        if(skin->i_spinner_glow != MISSING_TEXTURE) {
            // additive cyan glow which fills in with progress, and flashes white on bonus spins
            const f32 flash =
                s.bonusTimeMS < 0 ? 1.f : Mc::clamp<f32>((f32)(curPosMS - s.bonusTimeMS) / 200.f, 0.f, 1.f);
            g->setBlendMode(DrawBlendMode::ADDITIVE);
            g->setColor(argb(alpha * clampedRatio, std::lerp(1.f, 3.f / 255.f, flash),
                             std::lerp(1.f, 151.f / 255.f, flash), 1.f));
            drawSprite(skin->i_spinner_glow, center, spinnerScale / skin->i_spinner_glow.scale() * finishScale);
            g->setBlendMode(DrawBlendMode::ALPHA);
        }

        if(skin->i_spinner_bottom != MISSING_TEXTURE) {
            g->setColor(Color(tint).setA(alpha));
            drawSprite(skin->i_spinner_bottom, center, spinnerScale / skin->i_spinner_bottom.scale() * finishScale,
                       topRotation / 3.0f);
        }

        if(skin->i_spinner_top != MISSING_TEXTURE) {
            g->setColor(Color(tint).setA(alpha));
            drawSprite(skin->i_spinner_top, center, spinnerScale / skin->i_spinner_top.scale() * finishScale,
                       topRotation);
        }

        if(skin->i_spinner_middle2 != MISSING_TEXTURE) {
            g->setColor(Color(0xffffffff).setA(alpha));
            drawSprite(skin->i_spinner_middle2, center, spinnerScale / skin->i_spinner_middle2.scale() * finishScale,
                       s.drawRot);
        }

        drawSpinnerApproachCircle(skin->i_spinner_top);

        if(skin->i_spinner_middle != MISSING_TEXTURE) {
            // does not rotate, tints red as the time runs out
            g->setColor(argb(alpha, 1.f, percent, percent));
            drawSprite(skin->i_spinner_middle, center, spinnerScale / skin->i_spinner_middle.scale() * finishScale);
        }
    }

    // "CLEAR!"
    if(clearTimeMS >= 0 && curPosMS >= clearTimeMS && skin->i_spinner_clear != MISSING_TEXTURE) {
        // fades in over 400 ms with a bounce, and out over the last 50 ms. when cleared less than 400 ms before the
        // end, the animation starts 400 ms before the end, so it shows up already partway through.
        const f32 t = (f32)(curPosMS - Mc::min(clearTimeMS, endTimeMS - 400));
        const f32 fadeIn = easeOut(Mc::clamp<f32>(t / 400.f, 0.f, 1.f));
        const f32 fadeOut = 1.f - Mc::clamp<f32>((f32)(curPosMS - (endTimeMS - 50)) / 50.f, 0.f, 1.f);
        const f32 bounce = t < 240.f ? std::lerp(2.f, 0.8f, easeOut(t / 240.f))
                                     : std::lerp(0.8f, 1.f, Mc::clamp<f32>((t - 240.f) / 160.f, 0.f, 1.f));

        g->setColor(Color(0xffffffff).setA(fadeIn * fadeOut));
        drawSprite(skin->i_spinner_clear, windowPos(0.f, -104.f),
                   spinnerScale / skin->i_spinner_clear.scale() * bounce);
    }

    // "SPIN!"
    if(skin->i_spinner_spin != MISSING_TEXTURE) {
        // fades in over the second half of the fadein and out over the last 400 ms, unless the first spin fades it
        // out for good (300 ms, restarted as a 100 ms fade if the spinner gets cleared before that finishes)
        const f32 halfFadeInMS = Mc::max(1.f, (f32)l.fadeInTimeMS / 2.f);
        const i32 endFadeMS = Mc::clamp((objects.endTimes[index] - objects.clickTimes[index]), 1, 400);
        const auto idleAlpha = [&](f64 t) {
            if(t >= endTimeMS - endFadeMS)
                return 1.f - Mc::clamp<f32>((f32)(t - (endTimeMS - endFadeMS)) / (f32)endFadeMS, 0.f, 1.f);
            return Mc::clamp<f32>((f32)(t - objects.clickTimes[index]) / halfFadeInMS + 1.f, 0.f, 1.f);
        };

        f32 spinAlpha = idleAlpha(curPosMS);
        if(spinStartMS >= 0 && curPosMS >= spinStartMS) {
            i32 fadeStartMS = spinStartMS;
            f32 fadeMS = 300.f;
            f32 fadeFrom = idleAlpha(spinStartMS);
            if(clearTimeMS >= 0 && curPosMS >= clearTimeMS && clearTimeMS < spinStartMS + 300) {
                fadeFrom *= 1.f - (f32)(clearTimeMS - spinStartMS) / 300.f;
                fadeStartMS = clearTimeMS;
                fadeMS = 100.f;
            }
            spinAlpha = fadeFrom * (1.f - Mc::clamp<f32>((f32)(curPosMS - fadeStartMS) / fadeMS, 0.f, 1.f));
        }

        if(spinAlpha > 0.f) {
            g->setColor(Color(0xffffffff).setA(spinAlpha));
            drawSprite(skin->i_spinner_spin, windowPos(0.f, 116.f), spinnerScale / skin->i_spinner_spin.scale());
        }
    }

    // bonus score counter
    if(s.bonusSpins > 0 && curPosMS >= s.bonusTimeMS) {
        const f32 t = Mc::clamp<f32>((f32)(curPosMS - s.bonusTimeMS) / 800.f, 0.f, 1.f);
        if(t < 1.f) {
            const f32 digitScale = spinnerScale * std::lerp(2.f, 1.28f, easeOut(t)) / skin->i_scores[0].scale();
            const vec2 pos = windowPos(0.f, 80.f);

            g->setColor(Color(tint).setA((1.f - easeOut(t)) * alphaMultiplier));
            g->pushTransform();
            {
                g->scale(digitScale, digitScale);
                g->translate(pos.x, pos.y);
                HUD::drawNumberWithSkinDigits({.number = (u64)s.bonusSpins * 1000,
                                               .scale = digitScale,
                                               .combo = false,
                                               .anchor = AnchorPoint::CENTER,
                                               .skin = skin});
            }
            g->popTransform();
        }
    }

    // RPM
    if(skin->i_spinner_rpm != MISSING_TEXTURE) {
        // slides up into place during the fadein
        const f32 slide = easeOut(Mc::clamp<f32>(
            (f32)(curPosMS - (objects.clickTimes[index] - l.fadeInTimeMS)) / (f32)Mc::max(1, l.fadeInTimeMS), 0.f,
            1.f));
        const f32 hideOffset = 50.f * (1.f - slide);

        g->setColor(Color(tint).setA(alpha));
        drawSprite(skin->i_spinner_rpm, windowPos(-87.f, 197.f + hideOffset),
                   spinnerScale / skin->i_spinner_rpm.scale(), 0.f, AnchorPoint::TOP_LEFT);

        const f32 digitScale = spinnerScale * 0.9f / skin->i_scores[0].scale();
        const vec2 pos = windowPos(80.f, 200.f + hideOffset);
        g->pushTransform();
        {
            g->scale(digitScale, digitScale);
            g->translate(pos.x, pos.y + (f32)skin->i_scores[0]->getHeight() * digitScale / 2.f);
            HUD::drawNumberWithSkinDigits({.number = (u64)s.RPM,
                                           .scale = digitScale,
                                           .combo = false,
                                           .anchor = AnchorPoint::RIGHT,
                                           .skin = skin});
        }
        g->popTransform();
    } else if(l.deltaMS < 0 && cv::skin_always_draw_spinner_rpm.getBool()) {
        McFont *rpmFont = engine->getDefaultFont();
        const f32 stringWidth = rpmFont->getStringWidth("RPM: 477");
        g->setColor(Color(0xffffffff)
                        .setA(l.alphaWithoutHidden * l.alphaWithoutHidden * l.alphaWithoutHidden * alphaMultiplier));

        g->pushTransform();
        {
            const vec2 screen = view.getScreenSize();
            g->translate((i32)((i32)screen.x / 2 - stringWidth / 2),
                         (i32)((i32)screen.y - 5 + (5 + rpmFont->getHeight()) * (1.0f - l.alphaWithoutHidden)));
            g->drawString(rpmFont, fmt::format("RPM: {}", (i32)(s.RPM + 0.4f)));
        }
        g->popTransform();
    }
}
}  // namespace

SliderState::SliderState(SLIDERCURVETYPE stype, std::span<const vec2> points, f32 pixelLength)
    : curve(stype, points, pixelLength), numCtrlPoints(static_cast<u32>(points.size())) {}

const DatabaseBeatmapTypes::HITSAMPLE_BITS &SliderState::repeatSamples(i32 repeat) const {
    // Try to play a repeat sample based on what the mapper gave us
    // (the map loader gives every slider at least a start and an end sample)
    const uSz nb_edge_samples = this->edgeSamples.size();
    assert(nb_edge_samples >= 2);
    if(std::cmp_less(repeat + 1, nb_edge_samples)) return this->edgeSamples[repeat];
    // We have more repeats than edge samples!
    // Just play whatever we can (either the last repeat sample, or the start sample)
    return this->edgeSamples[nb_edge_samples - 2];
}

HitObjects::HitObjects() = default;
HitObjects::~HitObjects() = default;

void HitObjects::clear() {
    this->resize(0);
    this->circles.clear();
    this->sliders.clear();
    this->spinners.clear();
}

void HitObjects::resize(uSz size) {
    this->types.resize(size);
    this->typeIndices.resize(size);
    this->clickTimes.resize(size);
    this->endTimes.resize(size);
    this->comboStartTimes.resize(size);
    this->comboNumbers.resize(size);
    this->colorCounters.resize(size);
    this->colorOffsets.resize(size);
    this->endsCombo.resize(size);
    this->lines.resize(size);
    this->stacks.resize(size);
    this->samples.resize(size);
    this->positions.resize(size);
    this->originalPositions.resize(size);
    this->finished.resize(size);
    this->blocked.resize(size);
    this->misAimed.resize(size);
    this->visible.resize(size);
    this->autopilotDeltas.resize(size);
    this->looks.resize(size);
}

void HitObjects::build(const Primitives::PRIMITIVE_CONTAINER &primitives) {
    this->clear();
    this->resize(primitives.objects.size());
    for(uSz i = 0; i < primitives.objects.size(); i++) this->make(primitives, i, false);
    this->startCombos(0, this->size());
}

void HitObjects::remake(const Primitives::PRIMITIVE_CONTAINER &primitives, uSz index) {
    this->make(primitives, index, true);
}

void HitObjects::make(const Primitives::PRIMITIVE_CONTAINER &primitives, uSz index, bool remade) {
    const auto &object = primitives.objects[index];
    // made again as the type it was, it takes its old place among them (one that changes type leaves it empty)
    const auto place = [&](auto &states, HitObjectType type, auto &&made) -> auto & {
        const bool reused = remade && this->types[index] == type;
        this->types[index] = type;
        if(reused) return states[this->typeIndices[index]] = std::forward<decltype(made)>(made);
        this->typeIndices[index] = (u32)states.size();
        return states.emplace_back(std::forward<decltype(made)>(made));
    };
    this->comboStartTimes[index] = 0;
    this->stacks[index] = 0;
    this->finished[index] = false;
    this->blocked[index] = false;
    this->misAimed[index] = false;
    this->visible[index] = false;
    this->autopilotDeltas[index] = 0;
    this->looks[index] = {};
    if(const auto *h = std::get_if<DatabaseBeatmapTypes::HITCIRCLE>(&object)) {
        this->clickTimes[index] = h->time;
        this->endTimes[index] = h->time;
        this->comboNumbers[index] = h->number;
        this->colorCounters[index] = h->colorCounter;
        this->colorOffsets[index] = h->colorOffset;
        this->endsCombo[index] = h->isEndOfCombo;
        this->lines[index] = h->line;
        this->samples[index] = h->samples;
        this->positions[index] = this->originalPositions[index] = vec2{h->x, h->y};
        place(this->circles, HitObjectType::CIRCLE, CircleState{});
    } else if(const auto *s = std::get_if<DatabaseBeatmapTypes::SLIDER>(&object)) {
        const i32 duration = (i32)s->sliderTime;
        this->clickTimes[index] = s->time;
        this->endTimes[index] = s->time + (duration >= 0 ? duration : 1);  // force clamp to positive range
        this->comboNumbers[index] = s->number;
        this->colorCounters[index] = s->colorCounter;
        this->colorOffsets[index] = s->colorOffset;
        this->endsCombo[index] = s->isEndOfCombo;
        this->lines[index] = s->line;
        this->samples[index] = s->hoverSamples;

        // build curve
        SliderState &slider =
            place(this->sliders, HitObjectType::SLIDER, SliderState{s->type, s->points, std::abs(s->pixelLength)});
        slider.edgeSamples = s->edgeSamples;
        slider.sliderTimeMS = s->sliderTime;
        slider.sliderTimeMSWithoutRepeats = s->sliderTimeWithoutRepeats;
        slider.repeat = s->repeat;
        const std::span<const f32> ticks = s->ticks;
        // build raw ticks
        slider.ticks.reserve(ticks.size());
        for(f32 tick : ticks) {
            slider.ticks.emplace_back(SliderTick{.percent = tick, .finished = false});
        }
        slider.clicks.reserve((uSz)Mc::max(slider.repeat - 1, 0) +
                              (uSz)Mc::max(slider.repeat, 0) * slider.ticks.size());

        // build repeats
        for(i32 i = 0; i < (slider.repeat - 1); i++) {
            slider.clicks.emplace_back(SliderClick{
                .timeMS = s->time + (i32)(slider.sliderTimeMSWithoutRepeats * (i + 1)),
                .type = 0,
                .tickIndex = 0,
                .finished = false,
                .successful = false,
                .sliderend = ((i % 2) == 0),  // for hit animation on repeat hit
            });
        }

        // build ticks
        for(i32 i = 0; i < slider.repeat; i++) {
            for(i32 t = 0; t < slider.ticks.size(); t++) {
                // NOTE: repeat ticks are not necessarily symmetric.
                //
                // e.g. this slider: [1]=======*==[2]
                //
                // the '*' is where the tick is, let's say percent = 0.75
                // on repeat 0, the tick is at: click_time + 0.75*slider.fSliderTimeWithoutRepeats
                // but on repeat 1, the tick is at: click_time + 1*slider.fSliderTimeWithoutRepeats + (1.0 -
                // 0.75)*slider.fSliderTimeWithoutRepeats this gives better readability at the cost of invalid rhythms: ticks are
                // guaranteed to always be at the same position, even in repeats so, depending on which repeat we are in
                // (even or odd), we either do (percent) or (1.0 - percent)

                const f32 tickPercentRelativeToRepeatFromStartAbs =
                    (((i + 1) % 2) != 0 ? slider.ticks[t].percent : 1.0f - slider.ticks[t].percent);

                slider.clicks.emplace_back(SliderClick{
                    .timeMS = s->time + (i32)(slider.sliderTimeMSWithoutRepeats * i) +
                              (i32)(tickPercentRelativeToRepeatFromStartAbs * slider.sliderTimeMSWithoutRepeats),
                    .type = 1,
                    .tickIndex = t,
                    .finished = false,
                    .successful = false,
                    .sliderend = false});
            }
        }

        // in time order, which is the order they are judged in (see update())
        Mc::ranges::stable_sort(slider.clicks, {}, &SliderClick::timeMS);

        this->positions[index] = this->originalPositions[index] = slider.curve.pointAt(0.0f);
    } else {
        const auto &spinner = std::get<DatabaseBeatmapTypes::SPINNER>(object);
        this->clickTimes[index] = spinner.time;
        this->endTimes[index] = spinner.endTime;
        this->comboNumbers[index] = -1;
        this->colorCounters[index] = -1;
        this->colorOffsets[index] = -1;
        this->endsCombo[index] = spinner.isEndOfCombo;
        this->lines[index] = spinner.line;
        this->samples[index] = spinner.samples;
        this->positions[index] = this->originalPositions[index] = vec2{spinner.x, spinner.y};
        place(this->spinners, HitObjectType::SPINNER, SpinnerState{});
        // spinners don't need misaims
        this->misAimed[index] = true;
        // spinners don't use AR-dependent fadein, instead they always fade in with hardcoded 400 ms (see
        // GameRules::getFadeInTime())
        this->looks[index].useFadeInTimeAsApproachTime = !cv::spinner_use_ar_fadein.getBool();
    }
}

void HitObjects::startCombos(uSz from, uSz to) {
    i32 comboStartTime = 0;
    bool startsCombo = true;
    for(uSz i = from; i < to; i++) {
        if(startsCombo) comboStartTime = this->clickTimes[i];
        this->comboStartTimes[i] = comboStartTime;
        startsCombo = this->endsCombo[i];
    }
}

namespace {
// one of them as stacking reads and moves it (Objects: const when it only reads)
template <typename Objects>
struct StackedObject {
    Objects *objects;
    uSz index;

    [[nodiscard]] bool isCircle() const { return this->objects->types[this->index] == HitObjectType::CIRCLE; }
    [[nodiscard]] bool isSlider() const { return this->objects->types[this->index] == HitObjectType::SLIDER; }
    [[nodiscard]] bool isSpinner() const { return this->objects->types[this->index] == HitObjectType::SPINNER; }
    [[nodiscard]] i32 getClickTime() const { return this->objects->clickTimes[this->index]; }
    [[nodiscard]] i32 getEndTime() const { return this->objects->endTimes[this->index]; }
    [[nodiscard]] i32 getDuration() const { return this->getEndTime() - this->getClickTime(); }
    [[nodiscard]] vec2 getOriginalRawPosAt(i32 posMS) const {
        return this->objects->originalRawPosAt(this->index, posMS);
    }
    [[nodiscard]] i32 getStack() const { return this->objects->stacks[this->index]; }
    void setStack(i32 stack) const
        requires(!std::is_const_v<Objects>)
    {
        this->objects->stacks[this->index] = stack;
    }
};
}  // namespace

void HitObjects::stack(uSz from, uSz to, f32 AR, i32 beatmapVersion, f32 stackLeniency, f32 rawHitcircleDiameter,
                       bool hardRock) {
    // reset
    Mc::fill(this->stacks.begin() + (sSz)from, this->stacks.begin() + (sSz)to, 0);

    std::vector<StackedObject<HitObjects>> run;
    run.reserve(to - from);
    for(uSz i = from; i < to; i++) run.push_back({this, i});
    Primitives::calculateStacks([&run](uSz idx) { return &run[idx]; }, run.size(), AR, beatmapVersion, stackLeniency);

    // update hitobject positions (also of ones stacked before that aren't now)
    const f32 STACK_OFFSET = 0.05f;
    const f32 stackOffset = rawHitcircleDiameter * STACK_OFFSET;
    for(uSz i = from; i < to; i++) {
        if(this->types[i] == HitObjectType::SPINNER) continue;
        const vec2 offset{this->stacks[i] * stackOffset, this->stacks[i] * stackOffset * (hardRock ? -1.0f : 1.0f)};
        if(this->types[i] == HitObjectType::SLIDER) {
            SliderState &slider = this->sliderAt(i);
            slider.stackOffset = offset;
            this->positions[i] = slider.pointAt(0.0f);
        } else {
            this->positions[i] = this->originalPositions[i] - offset;
        }
    }
}

uSz HitObjects::nextStackingRun(uSz start, f32 AR, f32 stackLeniency) const {
    // Primitives::nextStackingRun() reads one object at a time, so a single view moves along
    StackedObject<const HitObjects> object{this, 0};
    return Primitives::nextStackingRun(
        [&object](uSz i) {
            object.index = i;
            return &object;
        },
        this->size(), start, AR, stackLeniency);
}

vec2 HitObjects::rawPosAt(uSz index, i32 posMS) const {
    if(this->types[index] != HitObjectType::SLIDER) return this->positions[index];
    const SliderState &s = this->sliderAt(index);
    if(posMS <= this->clickTimes[index])
        return s.pointAt(0.0f);
    else if(posMS >= this->clickTimes[index] + s.sliderTimeMS) {
        if(s.repeat % 2 == 0)
            return s.pointAt(0.0f);
        else
            return s.pointAt(1.0f);
    } else
        return s.pointAt(sliderT(s, this->clickTimes[index], posMS));
}

vec2 HitObjects::originalRawPosAt(uSz index, i32 posMS) const {
    if(this->types[index] != HitObjectType::SLIDER) return this->originalPositions[index];
    const SliderState &s = this->sliderAt(index);
    if(posMS <= this->clickTimes[index])
        return s.curve.pointAt(0.0f);
    else if(posMS >= this->clickTimes[index] + s.sliderTimeMS) {
        if(s.repeat % 2 == 0)
            return s.curve.pointAt(0.0f);
        else
            return s.curve.pointAt(1.0f);
    } else
        return s.curve.pointAt(sliderT(s, this->clickTimes[index], posMS));
}

void HitObjects::addSoundCues(uSz index, std::vector<SoundCue> &out) const {
    switch(this->types[index]) {
        case HitObjectType::CIRCLE:
            out.push_back({.timeMS = this->clickTimes[index],
                           .endTimeMS = this->clickTimes[index],
                           .kind = SoundCue::Kind::HIT,
                           .samples = this->samples[index],
                           .rawPos = this->positions[index]});
            break;
        case HitObjectType::SLIDER: {
            const SliderState &s = this->sliderAt(index);
            if(s.numCtrlPoints == 0) break;

            const auto cue = [&](i32 timeMS, SoundCue::Kind kind, const DatabaseBeatmapTypes::HITSAMPLE_BITS &samples,
                                 i32 endTimeMS) {
                out.push_back({.timeMS = timeMS,
                               .endTimeMS = endTimeMS,
                               .kind = kind,
                               .samples = samples,
                               .rawPos = this->rawPosAt(index, timeMS)});
            };
            const i32 endTimeMS = this->endTimes[index];
            cue(this->clickTimes[index], SoundCue::Kind::HIT, s.edgeSamples.front(), this->clickTimes[index]);
            cue(this->clickTimes[index], SoundCue::Kind::SLIDE, this->samples[index], endTimeMS);
            i32 repeat = 0;
            for(const auto &click : s.clicks) {
                if(click.type == 0) {
                    cue(click.timeMS, SoundCue::Kind::HIT, s.repeatSamples(++repeat), click.timeMS);
                } else {
                    cue(click.timeMS, SoundCue::Kind::TICK, this->samples[index], click.timeMS);
                }
            }
            cue(endTimeMS, SoundCue::Kind::HIT, s.edgeSamples.back(), endTimeMS);
        } break;
        case HitObjectType::SPINNER:
            out.push_back({.timeMS = this->endTimes[index],
                           .endTimeMS = this->endTimes[index],
                           .kind = SoundCue::Kind::HIT,
                           .samples = this->samples[index],
                           .rawPos = this->positions[index]});
            break;
    }
}

namespace HitObjectDrawing {

void drawFollowPoints(const PlayfieldView &view, const HitObjects &objects, uSz firstIndex) {
    const Skin *skin = view.getSkin();

    const f64 curPos = view.getCurMusicPosWithOffsets();

    // followpoints always fade in with a fixed 800 ms approach time (like stable),
    // which can be abused for reading high AR.
    // followpoints_clamp caps it at the current approach time instead.

    // 0.7x means animation lasts only 0.7 of it's time
    const f64 animationMultiplier = view.getSpeedAdjustedAnimationSpeed();
    const i32 followPointApproachTime =
        animationMultiplier * (cv::followpoints_clamp.getBool()
                                   ? Mc::min((i32)view.getApproachTime(), (i32)cv::followpoints_approachtime.getFloat())
                                   : (i32)cv::followpoints_approachtime.getFloat());
    const bool followPointsConnectCombos = cv::followpoints_connect_combos.getBool();
    const bool followPointsConnectSpinners = cv::followpoints_connect_spinners.getBool();
    const f32 followPointSeparationMultiplier = Mc::max(cv::followpoints_separation_multiplier.getFloat(), 0.1f);
    const f32 followPointFadeTime = animationMultiplier * cv::followpoints_prevfadetime.getFloat();
    const f32 followPointScaleMultiplier = cv::followpoints_scale_multiplier.getFloat();
    const int screenWidth = (int)view.getScreenSize().x;
    const int screenHeight = (int)view.getScreenSize().y;

    // include previous object in followpoints
    int lastObjectIndex = -1;

    for(int index = (int)firstIndex; index < objects.size(); index++) {
        lastObjectIndex = index - 1;

        // ignore future spinners
        if(objects.types[index] == HitObjectType::SPINNER && !followPointsConnectSpinners)  // if this is a spinner
        {
            lastObjectIndex = -1;
            continue;
        }

        const bool isCurrentHitObjectNewCombo =
            (lastObjectIndex >= 0 ? (bool)objects.endsCombo[lastObjectIndex] : false);
        const bool isCurrentHitObjectSpinner = (lastObjectIndex >= 0 && followPointsConnectSpinners
                                                    ? objects.types[lastObjectIndex] == HitObjectType::SPINNER
                                                    : false);
        if(lastObjectIndex >= 0 && (!isCurrentHitObjectNewCombo || followPointsConnectCombos ||
                                    (isCurrentHitObjectSpinner && followPointsConnectSpinners))) {
            // ignore previous spinners
            if(objects.types[lastObjectIndex] == HitObjectType::SPINNER &&
               !followPointsConnectSpinners)  // if this is a spinner
            {
                lastObjectIndex = -1;
                continue;
            }

            // get time & pos of the last and current object
            const i32 lastObjectEndTime = objects.endTimes[lastObjectIndex];
            const i32 objectStartTime = objects.clickTimes[index];
            const i32 timeDiff = objectStartTime - lastObjectEndTime;

            const vec2 startPointRaw = objects[lastObjectIndex].getRawPosAt(lastObjectEndTime);
            const vec2 endPointRaw = objects[index].getRawPosAt(objectStartTime);
            const vec2 startPoint = view.osuCoords2Pixels(startPointRaw);
            const vec2 endPoint = view.osuCoords2Pixels(endPointRaw);

            const f32 xDiff = endPoint.x - startPoint.x;
            const f32 yDiff = endPoint.y - startPoint.y;
            const vec2 diff = endPoint - startPoint;

            // NOTE: dist and separation are in osu!pixels, so that followpoint placement is independent of how the
            // playfield is scaled to the screen (only the final positions are mapped to screen space)
            const i32 dist = (i32)vec::length(endPointRaw - startPointRaw);

            // draw all points between the two objects
            const int followPointSeparation = 32.0f * followPointSeparationMultiplier;
            for(int j = (int)(followPointSeparation * 1.5f); j < (dist - followPointSeparation);
                j += followPointSeparation) {
                const f32 animRatio = ((f32)j / (f32)dist);

                const vec2 animPosStart = startPoint + (animRatio - 0.1f) * diff;
                const vec2 finalPos = startPoint + animRatio * diff;

                const i32 fadeInTime = (i32)(lastObjectEndTime + animRatio * timeDiff) - followPointApproachTime;
                const i32 fadeOutTime = (i32)(lastObjectEndTime + animRatio * timeDiff);

                // draw
                f32 alpha = 1.0f;
                f32 followAnimPercent = Mc::clamp<f32>((f32)(curPos - fadeInTime) / followPointFadeTime, 0.0f, 1.0f);
                followAnimPercent = -followAnimPercent * (followAnimPercent - 2.0f);  // quad out

                // NOTE: only internal osu default skin uses scale + move transforms here, it is impossible to achieve
                // this effect with user skins
                const f32 scale = cv::followpoints_anim.getBool() ? 1.5f - 0.5f * followAnimPercent : 1.0f;
                const vec2 followPos = cv::followpoints_anim.getBool()
                                           ? animPosStart + (finalPos - animPosStart) * followAnimPercent
                                           : finalPos;

                // bullshit performance optimization: only draw followpoints if within screen bounds (plus a bit of a
                // margin) there is only one beatmap where this matters currently: https://osu.ppy.sh/b/1145513
                if(followPos.x < -screenWidth || followPos.x > screenWidth * 2 || followPos.y < -screenHeight ||
                   followPos.y > screenHeight * 2)
                    continue;

                // calculate trail alpha
                if(curPos >= fadeInTime && curPos < fadeOutTime) {
                    // future trail
                    const f32 delta = (f32)(curPos - fadeInTime);
                    alpha = Mc::min(delta / followPointFadeTime, 1.0f);
                } else if(curPos >= fadeOutTime && curPos < (fadeOutTime + (i32)followPointFadeTime)) {
                    // previous trail
                    const f32 delta = (f32)(curPos - fadeOutTime);
                    alpha = 1.0f - delta / followPointFadeTime;
                } else
                    alpha = 0.0f;

                // draw it
                g->setColor(Color(0xffffffff).setA(alpha));

                g->pushTransform();
                {
                    g->rotate(vec::degrees(std::atan2(yDiff, xDiff)));

                    skin->i_followpoint.setAnimationTimeOffset(skin->anim_speed, fadeInTime);

                    // NOTE: getSizeBaseRaw() depends on the current animation time being set correctly beforehand!
                    // (otherwise you get incorrect scales, e.g. for animated elements with inconsistent @2x mixed in)
                    // the followpoints are scaled by one eighth of the hitcirclediameter (not the raw diameter, but the
                    // scaled diameter)
                    const f32 followPointImageScale =
                        ((view.getHitcircleDiameter() / 8.0f) / skin->i_followpoint.getSizeBaseRaw().x) *
                        followPointScaleMultiplier;

                    skin->i_followpoint.drawRaw(followPos, followPointImageScale * scale);
                }
                g->popTransform();
            }
        }

        // store current index as previous index
        lastObjectIndex = index;

        // iterate up until the "nextest" element
        if(objects.clickTimes[index] >= curPos + followPointApproachTime) break;
    }
}

void rebuildMesh(const PlayfieldView &view, HitObjects &objects, uSz index) {
    if(objects.types[index] == HitObjectType::SLIDER) rebuildSliderMesh(view, objects, index);
}

void draw(const PlayfieldView &view, HitObjects &objects, std::span<const u32> indices, RenderTarget *sliderBodies,
          bool spinnersFirst) {
    // in order to avoid covering circles/sliders with spinner skin elements, draw spinners first
    // and overlay circles/sliders on top
    // this logic could be embedded in the sort order itself but i'm lazy to check if that would mess anything else up
    if(spinnersFirst) {
        for(const u32 i : indices) {
            if(objects.types[i] == HitObjectType::SPINNER) drawSpinnerObject(view, objects, i);
        }
    }

    SliderRenderer::Batch batch{sliderBodies};
    for(const u32 i : indices) {
        if(objects.types[i] == HitObjectType::SLIDER) addSliderBody(view, objects, i, batch);
    }
    for(const u32 i : indices) {
        switch(objects.types[i]) {
            case HitObjectType::CIRCLE:
                drawCircleObject(view, objects, i);
                break;
            case HitObjectType::SLIDER:
                drawSliderObject(view, objects, i, batch);
                break;
            case HitObjectType::SPINNER:
                if(!spinnersFirst) drawSpinnerObject(view, objects, i);
                break;
        }
    }
}

void draw2(const PlayfieldView &view, HitObjects &objects, uSz index) {
    switch(objects.types[index]) {
        case HitObjectType::CIRCLE:
            drawCircleObject2(view, objects, index);
            break;
        case HitObjectType::SLIDER:
            drawSliderObject2(view, objects, index, true, false);
            break;
        case HitObjectType::SPINNER:
            drawHitResults(view, objects, index);
            break;
    }
}

}  // namespace HitObjectDrawing
}  // namespace neomod
