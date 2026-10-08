#include "HitObjects.h"

#include <cmath>
#include <utility>

#include "AbstractBeatmapInterface.h"
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
#include "LegacyReplay.h"
#include "crypto.h"

#define WANT_PDQSORT

namespace neomod {
using namespace flags::operators;

namespace {
// what a hit animation started at its hit (from 0.001, so it shows from the hit's frame on) is at elapsedMS of durationMS
f32 hitAnimationAt(i32 elapsedMS, i32 durationMS) {
    if(elapsedMS >= durationMS) return 1.0f;
    const f32 eased = anim::ease(anim::QuadOut, (f32)elapsedMS / (f32)durationMS);
    return 0.001f * (1.0f - eased) + eased;
}

// a trail (see HitObject::pose()) elapsedMS after the hit: the circle's alpha, from 1 to 0 over durationMS
f32 trailAlphaAt(i32 elapsedMS, i32 durationMS) {
    return 1.0f - std::clamp((f32)elapsedMS / (f32)durationMS, 0.0f, 1.0f);
}

// and its approach circle's scale, a tenth larger over the first 100 ms
f32 trailApproachScaleAt(i32 elapsedMS) { return 1.0f + 0.1f * std::clamp((f32)elapsedMS / 100.0f, 0.0f, 1.0f); }
}  // namespace

void HitObject::drawHitResult(const PlayfieldView &view, vec2 pos, LiveHitResult result, f32 animPercentInv,
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
                hitDeltaRangePercent = std::clamp<f32>(
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

        g->setAlpha(std::clamp<f32>(animPercent < fadeInEndPercent
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
                    scale = std::lerp(0.6f, 1.1f, std::clamp<f32>(animPercent / (fadeInEndPercent * 0.8f), 0.0f, 1.0f));
                else if(animPercent < fadeInEndPercent * 1.2f)
                    scale = std::lerp(1.1f, 0.9f,
                                      std::clamp<f32>((animPercent - fadeInEndPercent * 0.8f) /
                                                          (fadeInEndPercent * 1.2f - fadeInEndPercent * 0.8f),
                                                      0.0f, 1.0f));
                else if(animPercent < fadeInEndPercent * 1.4f)
                    scale = std::lerp(0.9f, 1.0f,
                                      std::clamp<f32>((animPercent - fadeInEndPercent * 1.2f) /
                                                          (fadeInEndPercent * 1.4f - fadeInEndPercent * 1.2f),
                                                      0.0f, 1.0f));
            } else
                scale = std::lerp(0.9f, 1.05f, std::clamp<f32>(animPercent, 0.0f, 1.0f));

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
                        std::lerp(-5.0f, 40.0f, std::clamp<f32>(animPercent * animPercent * animPercent, 0.0f, 1.0f)) *
                        osuCoordScaleMultiplier;

                f32 missScale = 1.0f + std::clamp<f32>((1.0f - (animPercent / fadeInEndPercent)), 0.0f, 1.0f) *
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

HitObject::HitObject(i32 timeMS, DatabaseBeatmapTypes::HITSAMPLE_BITS samples, i32 comboNumber, bool isEndOfCombo,
                     i32 colorCounter, i32 colorOffset, AbstractBeatmapInterface *judge, const PlayfieldView *view)
    : m_judge(judge),
      m_view(view),
      m_clickTimeMS(timeMS),
      m_comboNumber(comboNumber),
      m_hitSamples(samples),
      m_colorCounter(colorCounter),
      m_colorOffset(colorOffset),
      m_endOfCombo(isEndOfCombo) {}

void HitObject::draw2() {
    drawHitResultAnim(m_hitresultanim1);
    drawHitResultAnim(m_hitresultanim2);
}

void HitObject::drawHitResultAnim(const HITRESULTANIM &hitresultanim) {
    if((hitresultanim.timeSecs - cv::hitresult_duration.getFloat()) <
           engine->getTime()  // NOTE: this is written like that on purpose, don't change it ("future" results can be
                              // scheduled with it, e.g. for slider end)
       && (hitresultanim.timeSecs + cv::hitresult_duration_max.getFloat() * (1.0f / m_view->getBaseAnimationSpeed())) >
              engine->getTime()) {
        const Skin *skin = m_view->getSkin();
        const f32 skinSpeedMultiplier = skin->anim_speed;

        const i32 skinAnimationTimeStartOffset =
            m_clickTimeMS + (hitresultanim.addObjectDurationToSkinAnimationTimeStartOffset ? m_durationMS : 0) +
            hitresultanim.deltaMS;

        for(SkinImage *image : {&skin->i_hit0, &skin->i_hit50, &skin->i_hit100, &skin->i_hit100k, &skin->i_hit300,
                                &skin->i_hit300g, &skin->i_hit300k}) {
            image->setAnimationTimeOffset(skinSpeedMultiplier, skinAnimationTimeStartOffset);
            image->setAnimationFrameClampUp();
        }

        const f32 animPercentInv =
            1.0f - (((engine->getTime() - hitresultanim.timeSecs) * m_view->getBaseAnimationSpeed()) /
                    cv::hitresult_duration.getFloat());

        drawHitResult(*m_view, m_view->osuCoords2Pixels(hitresultanim.rawPos), hitresultanim.result, animPercentInv,
                      hitresultanim.deltaRangePercent);
    }
}

void HitObject::update(i32 curPosMS, f64 /*frame_time*/) {
    // (the view's mods include the experimental ones; objects without a view aren't drawn)
    this->updateLook(curPosMS, m_view ? m_view->getModFlags() : m_judge->getMods().flags,
                     m_judge->getCachedApproachTimeForUpdate(), m_judge->getSpeedAdjustedAnimationSpeed());
}

void HitObject::updateLook(i32 curPosMS, ModFlags mods, f32 approachTimeMS, f32 speedAdjustedAnimationSpeed) {
    m_alphaForApproachCircle = 0.0f;
    m_hittableDimRGBColorMultiplierPct = 1.0f;

    const f64 animationSpeedMultiplier = speedAdjustedAnimationSpeed;
    const i32 visibleTms = (flags::has<ModFlags::FreezeFrame>(mods) ? m_comboStartMS : m_clickTimeMS);
    m_fadeInTimeMS = GameRules::getFadeInTime() * animationSpeedMultiplier;
    m_approachTimeMS = (m_useFadeInTimeAsApproachTime ? m_fadeInTimeMS : (i32)approachTimeMS);
    m_deltaMS = m_clickTimeMS - curPosMS;

    // 1 ms fudge by using >=, shouldn't really be a problem
    if(curPosMS >= (visibleTms - m_approachTimeMS) && curPosMS < (getEndTime())) {
        // approach circle scale
        const f32 scale = std::clamp<f32>((f32)m_deltaMS / (f32)m_approachTimeMS, 0.0f, 1.0f);
        m_approachScale = 1 + (scale * cv::approach_scale_multiplier.getFloat());
        if(flags::has<ModFlags::ApproachDifferent>(mods)) {
            constexpr f32 back_const = 1.70158;

            f32 time = 1.0f - scale;
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
            m_approachScale = 1 + std::lerp(cv::mod_approach_different_initial_size.getFloat() - 1.0f, 0.0f, time);
        }

        // hitobject body fadein
        const i32 fadeInStart = visibleTms - m_approachTimeMS;
        // std::min() ensures that the fade always finishes at click_time
        // (even if the fadeintime is longer than the approachtime)
        const i32 fadeInEnd = std::min(visibleTms, visibleTms - m_approachTimeMS + m_fadeInTimeMS);
        m_alpha = std::clamp<f32>(1.0f - ((f32)(fadeInEnd - curPosMS) / (f32)(fadeInEnd - fadeInStart)), 0.0f, 1.0f);
        m_alphaWithoutHidden = m_alpha;

        if(flags::has<ModFlags::FreezeFrame>(mods)) {
            // HACK: set m_alphaWithoutHidden as "alpha without freeze time or hidden"
            //       this makes slider bodies & spinners draw correctly
            const i32 fadeInStart = m_clickTimeMS - m_approachTimeMS;
            const i32 fadeInEnd = std::min(m_clickTimeMS, m_clickTimeMS - m_approachTimeMS + m_fadeInTimeMS);
            m_alphaWithoutHidden =
                std::clamp<f32>(1.0f - ((f32)(fadeInEnd - curPosMS) / (f32)(fadeInEnd - fadeInStart)), 0.0f, 1.0f);
        }

        if(flags::has<ModFlags::Hidden>(mods)) {
            // hidden hitobject body fadein
            const f32 fin_start_percent = cv::mod_hd_circle_fadein_start_percent.getFloat();
            const f32 fin_end_percent = cv::mod_hd_circle_fadein_end_percent.getFloat();
            const i32 hiddenFadeInStartMS = visibleTms - (i32)(m_approachTimeMS * fin_start_percent);
            const i32 hiddenFadeInEndMS = visibleTms - (i32)(m_approachTimeMS * fin_end_percent);
            m_alpha = std::clamp<f32>(
                1.0f - ((f32)(hiddenFadeInEndMS - curPosMS) / (f32)(hiddenFadeInEndMS - hiddenFadeInStartMS)), 0.0f,
                1.0f);

            // hidden hitobject body fadeout
            const f32 fout_start_percent = cv::mod_hd_circle_fadeout_start_percent.getFloat();
            const f32 fout_end_percent = cv::mod_hd_circle_fadeout_end_percent.getFloat();
            const i32 hiddenFadeOutStart = visibleTms - (i32)(m_approachTimeMS * fout_start_percent);
            const i32 hiddenFadeOutEnd = visibleTms - (i32)(m_approachTimeMS * fout_end_percent);
            if(curPosMS >= hiddenFadeOutStart)
                m_alpha = std::clamp<f32>(
                    ((f32)(hiddenFadeOutEnd - curPosMS) / (f32)(hiddenFadeOutEnd - hiddenFadeOutStart)), 0.0f, 1.0f);
        }

        // approach circle fadein (doubled fadeintime)
        const i32 approachCircleFadeStart = m_clickTimeMS - m_approachTimeMS;
        const i32 approachCircleFadeEnd =
            std::min(m_clickTimeMS,
                     m_clickTimeMS - m_approachTimeMS +
                         2 * m_fadeInTimeMS);  // std::min() ensures that the fade always finishes at click_time
                                               // (even if the fadeintime is longer than the approachtime)
        m_alphaForApproachCircle = std::clamp<f32>(
            1.0f - ((f32)(approachCircleFadeEnd - curPosMS) / (f32)(approachCircleFadeEnd - approachCircleFadeStart)),
            0.0f, 1.0f);

        // hittable dim, see https://github.com/ppy/osu/pull/20572
        if(cv::hitobject_hittable_dim.getBool() &&
           (!flags::has<ModFlags::Mafham>(mods) || !cv::mod_mafham_ignore_hittable_dim.getBool())) {
            const i32 hittableDimFadeStart = m_clickTimeMS - (i32)GameRules::HITWINDOW_MISS;

            // yes, this means the un-dim animation cuts into the already clickable range
            const i32 hittableDimFadeEnd = hittableDimFadeStart + (i32)cv::hitobject_hittable_dim_duration.getInt();

            m_hittableDimRGBColorMultiplierPct =
                std::lerp(cv::hitobject_hittable_dim_start_percent.getFloat(), 1.0f,
                          std::clamp<f32>(1.0f - (f32)(hittableDimFadeEnd - curPosMS) /
                                                     (f32)(hittableDimFadeEnd - hittableDimFadeStart),
                                          0.0f, 1.0f));
        }

        m_visible = true;
    } else {
        m_approachScale = 1.0f;
        m_visible = false;
    }
}

void HitObject::addHitResult(LiveHitResult result, i32 delta, bool isEndOfCombo, vec2 posRaw, f32 targetDelta,
                             f32 targetAngle, bool ignoreOnHitErrorBar, bool ignoreCombo, bool ignoreHealth,
                             bool addObjectDurationToSkinAnimationTimeStartOffset) {
    if(m_judge->getMods().has(ModFlags::Target) && result != LiveHitResult::HIT_MISS && targetDelta >= 0.0f) {
        const f32 p300 = cv::mod_target_300_percent.getFloat();
        const f32 p100 = cv::mod_target_100_percent.getFloat();
        const f32 p50 = cv::mod_target_50_percent.getFloat();

        if(targetDelta < p300 && (result == LiveHitResult::HIT_300 || result == LiveHitResult::HIT_100))
            result = LiveHitResult::HIT_300;
        else if(targetDelta < p100)
            result = LiveHitResult::HIT_100;
        else if(targetDelta < p50)
            result = LiveHitResult::HIT_50;
        else
            result = LiveHitResult::HIT_MISS;

        m_judge->addTargetHit(targetDelta, targetAngle);
    }

    const LiveHitResult returnedHit = m_judge->addHitResult(this, result, delta, isEndOfCombo, ignoreOnHitErrorBar,
                                                            false, ignoreCombo, false, ignoreHealth);
    if(m_view == nullptr) return;

    HITRESULTANIM hitresultanim;
    {
        hitresultanim.result = (returnedHit != LiveHitResult::HIT_MISS ? returnedHit : result);
        hitresultanim.rawPos = posRaw;
        hitresultanim.deltaMS = delta;
        hitresultanim.deltaRangePercent = std::clamp<f32>((f32)delta / m_judge->getHitWindow50(), -1.0f, 1.0f);
        hitresultanim.timeSecs = engine->getTime();
        hitresultanim.addObjectDurationToSkinAnimationTimeStartOffset = addObjectDurationToSkinAnimationTimeStartOffset;
    }

    // currently a maximum of 2 simultaneous results are supported (for drawing, per hitobject)
    if(engine->getTime() >
       m_hitresultanim1.timeSecs + cv::hitresult_duration_max.getFloat() * (1.0f / m_view->getBaseAnimationSpeed()))
        m_hitresultanim1 = hitresultanim;
    else
        m_hitresultanim2 = hitresultanim;
}

void HitObject::onReset(i32 /*curPos*/) {
    m_misAim = false;
    m_autopilotDeltaMS = 0;

    m_hitresultanim1.timeSecs = -9999.0f;
    m_hitresultanim2.timeSecs = -9999.0f;
}

f32 HitObject::lerp3f(f32 a, f32 b, f32 c, f32 percent) {
    if(percent <= 0.5f)
        return std::lerp(a, b, percent * 2.0f);
    else
        return std::lerp(b, c, (percent - 0.5f) * 2.0f);
}

i32 Circle::rainbowNumber = 0;
i32 Circle::rainbowColorCounter = 0;

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
    // this function is only used by the target practice heatmap

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

Circle::Circle(vec2 pos, i32 timeMS, DatabaseBeatmapTypes::HITSAMPLE_BITS samples, i32 comboNumber, bool isEndOfCombo,
               i32 colorCounter, i32 colorOffset, AbstractBeatmapInterface *judge, const PlayfieldView *view)
    : HitObject(timeMS, samples, comboNumber, isEndOfCombo, colorCounter, colorOffset, judge, view),
      m_rawPos(pos),
      m_originalRawPos(m_rawPos) {
    m_type = HitObjectType::CIRCLE;
}

Circle::~Circle() { onReset(0); }

void Circle::draw() {
    HitObject::draw();

    const Skin *skin = m_view->getSkin();

    const ModFlags curGameplayFlags = m_view->getModFlags();

    if(flags::has<ModFlags::Traceable>(curGameplayFlags)) {
        // draw nothing for traceable (approach circles are drawn in draw2())
        return;
    }

    const bool hd = flags::has<ModFlags::Hidden>(curGameplayFlags);

    const i32 animTimeOffset =
        !m_view->isInMafhamRenderChunk() ? m_clickTimeMS - m_approachTimeMS : m_view->getCurMusicPosWithOffsets();

    // draw hit animation (if not hidden)
    if(!hd && !cv::instafade.getBool() && m_hitAnimation > 0.0f && m_hitAnimation != 1.0f) {
        f32 alpha = 1.0f - m_hitAnimation;

        f32 scale = m_hitAnimation;
        scale = -scale * (scale - 2.0f);  // quad out scale

        const bool drawNumber = skin->version > 1.0f ? false : true;
        const f32 foscale = cv::circle_fade_out_scale.getFloat();

        g->pushTransform();
        {
            g->scale((1.0f + scale * foscale), (1.0f + scale * foscale));

            skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
            drawCircle(*m_view, m_rawPos, m_comboNumber, m_colorCounter, m_colorOffset, 1.0f, 1.0f, alpha, alpha,
                       drawNumber);
        }
        g->popTransform();
    }

    // posed with a trail: white, fading out
    if(m_trailAlpha > 0.0f) {
        skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
        drawCircleIn(*m_view, m_rawPos, m_comboNumber, m_colorCounter, 0xffffffff, 1.0f, m_trailAlpha, m_trailAlpha,
                     true, false);
        return;
    }

    if(m_finished ||
       (!m_visible && !m_waiting))  // special case needed for when we are past this objects time, but still
                                    // within not-miss range, because we still need to draw the object
        return;

    // draw circle
    vec2 shakeCorrectedPos = m_rawPos;
    if(engine->getTime() < m_shakeAnimation && !m_view->isInMafhamRenderChunk())  // handle note blocking shaking
    {
        f32 smooth =
            1.0f - ((m_shakeAnimation - engine->getTime()) / cv::circle_shake_duration.getFloat());  // goes from 0 to 1
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
        const f32 approachScale = m_waiting && !hd ? 1.0f : m_approachScale;
        const f32 alpha = m_waiting && !hd ? 1.0f : m_alpha;
        const f32 numberAlpha = m_waiting && !hd ? 1.0f : m_alpha;

        drawCircle(*m_view, shakeCorrectedPos, m_comboNumber, m_colorCounter, m_colorOffset,
                   m_hittableDimRGBColorMultiplierPct, approachScale, alpha, numberAlpha, true,
                   m_overrideHDApproachCircle);
    }
}

void Circle::draw2() {
    HitObject::draw2();
    if(m_trailAlpha > 0.0f) {
        drawApproachCircle(*m_view, m_rawPos, m_comboNumber, m_colorCounter, m_colorOffset, 1.0f, m_trailApproachScale,
                           m_trailAlpha);
        return;
    }
    if(m_finished || (!m_visible && !m_waiting))
        return;  // special case needed for when we are past this objects time, but still within not-miss range, because
                 // we still need to draw the object

    // draw approach circle
    const bool hd = flags::has<ModFlags::Hidden>(m_view->getModFlags());

    // HACKHACK: don't fucking change this piece of code here, it fixes a heisenbug
    // (https://github.com/McKay42/McOsu/issues/165)
    if(cv::bug_flicker_log.getBool()) {
        const f32 approachCircleImageScale =
            m_view->getHitcircleDiameter() / (128.0f * (m_view->getSkin()->i_approachcircle.scale()));
        debugLog("click_time = {:d}, aScale = {:f}, iScale = {:f}", m_clickTimeMS, m_approachScale,
                 approachCircleImageScale);
    }

    drawApproachCircle(*m_view, m_rawPos, m_comboNumber, m_colorCounter, m_colorOffset,
                       m_hittableDimRGBColorMultiplierPct, m_waiting && !hd ? 1.0f : m_approachScale,
                       m_waiting && !hd ? 1.0f : m_alphaForApproachCircle, m_overrideHDApproachCircle);
}

void Circle::update(i32 curPosMS, f64 frameTimeSecs) {
    HitObject::update(curPosMS, frameTimeSecs);
    if(m_finished) return;

    const ModFlags curIFaceMods = m_judge->getMods().flags;
    const i32 deltaMS = curPosMS - m_clickTimeMS;

    if(flags::has<ModFlags::Autoplay>(curIFaceMods)) {
        if(curPosMS >= m_clickTimeMS) {
            onHit(LiveHitResult::HIT_300, 0);
        }
        return;
    }

    if(flags::has<ModFlags::Relax>(curIFaceMods)) {
        if(curPosMS >= m_clickTimeMS + (i32)cv::relax_offset.getInt() && !m_judge->isPaused() &&
           !m_judge->isContinueScheduled()) {
            const vec2 pos = m_judge->osuCoords2Pixels(m_rawPos);
            const f32 cursorDelta = vec::length(m_judge->getCursorPos() - pos);
            if((cursorDelta < m_judge->fHitcircleDiameter / 2.0f && (flags::has<ModFlags::Relax>(curIFaceMods)))) {
                LiveHitResult result = m_judge->getHitResult(deltaMS);

                if(result != LiveHitResult::HIT_NULL) {
                    const f32 targetDelta = cursorDelta / (m_judge->fHitcircleDiameter / 2.0f);
                    const f32 targetAngle =
                        vec::degrees(std::atan2(m_judge->getCursorPos().y - pos.y, m_judge->getCursorPos().x - pos.x));

                    onHit(result, deltaMS, targetDelta, targetAngle);
                }
            }
        }
    }

    if(deltaMS >= 0) {
        m_waiting = true;

        // if this is a miss after waiting
        if(deltaMS > (i32)m_judge->getHitWindow50()) {
            onHit(LiveHitResult::HIT_MISS, deltaMS);
        }
    } else {
        m_waiting = false;
    }
}

void Circle::pose(i32 timeMS, i32 fadeOutMS, i32 trailMS) {
    this->updateLook(timeMS, m_view->getModFlags(), m_view->getApproachTime(),
                     m_view->getSpeedAdjustedAnimationSpeed());

    m_waiting = false;
    m_finished = timeMS >= m_clickTimeMS;
    const i32 sinceHitMS = timeMS - m_clickTimeMS;
    const bool trail = m_finished && trailMS > 0;
    m_hitAnimation = m_finished && !trail ? hitAnimationAt(sinceHitMS, fadeOutMS) : 0.0f;
    m_trailAlpha = trail ? trailAlphaAt(sinceHitMS, trailMS) : 0.0f;
    m_trailApproachScale = trail ? trailApproachScaleAt(sinceHitMS) : 1.0f;
    m_shakeAnimation = 0.0f;
}

void Circle::addSoundCues(std::vector<SoundCue> &out) const {
    out.push_back({.timeMS = m_clickTimeMS,
                   .endTimeMS = m_clickTimeMS,
                   .kind = SoundCue::Kind::HIT,
                   .samples = m_hitSamples,
                   .rawPos = m_rawPos});
}

void Circle::updateStackPosition(f32 stackOffset, bool hardRock) {
    m_rawPos = m_originalRawPos - vec2(m_stackNum * stackOffset, m_stackNum * stackOffset * (hardRock ? -1.0f : 1.0f));
}

void Circle::miss(i32 curPosMS) {
    if(m_finished) return;

    const i32 deltaMS = curPosMS - m_clickTimeMS;

    onHit(LiveHitResult::HIT_MISS, deltaMS);
}

bool Circle::isClickableFrom(i32 music_pos, vec2 cursor_pos) const {
    if(m_finished || m_blocked) return false;
    if(m_judge->getHitResult(music_pos - m_clickTimeMS) == LiveHitResult::HIT_NULL) return false;

    const vec2 pos = m_judge->osuCoords2Pixels(m_rawPos);
    const f32 cursorDelta = vec::length(cursor_pos - pos);
    if(cursorDelta >= m_judge->fHitcircleDiameter / 2.0f) return false;

    return true;
}

void Circle::onClickEvent(std::vector<Click> &clicks) {
    if(m_finished) return;

    const vec2 cursorPos = clicks[0].cursorPos;
    const vec2 pos = m_judge->osuCoords2Pixels(m_rawPos);
    const f32 cursorDelta = vec::length(cursorPos - pos);

    if(cursorDelta < m_judge->fHitcircleDiameter / 2.0f) {
        // note blocking & shake
        if(m_blocked) {
            m_shakeAnimation = engine->getTime() + cv::circle_shake_duration.getFloat();
            return;  // ignore click event completely
        }

        const i32 deltaMS = clicks[0].musicPosMS - m_clickTimeMS;

        LiveHitResult result = m_judge->getHitResult(deltaMS);
        if(result != LiveHitResult::HIT_NULL) {
            const f32 targetDelta = cursorDelta / (m_judge->fHitcircleDiameter / 2.0f);
            const f32 targetAngle = vec::degrees(std::atan2(cursorPos.y - pos.y, cursorPos.x - pos.x));

            clicks.erase(clicks.begin());
            onHit(result, deltaMS, targetDelta, targetAngle);
        }
    }
}

void Circle::onHit(LiveHitResult result, i32 delta, f32 targetDelta, f32 targetAngle) {
    // sound and hit animation
    if(result != LiveHitResult::HIT_MISS) {
        m_judge->playHitSound(m_hitSamples, m_rawPos, delta, m_clickTimeMS);

        if(m_view != nullptr) {
            m_hitAnimation = 0.001f;  // quickfix for 1 frame missing images
            m_hitAnimation.set(1.0f, GameRules::getFadeOutTime(m_view->getBaseAnimationSpeed()), anim::QuadOut);
        }
    }

    // add it, and we are finished
    addHitResult(result, delta, m_endOfCombo, m_rawPos, targetDelta, targetAngle);
    m_finished = true;
}

void Circle::onReset(i32 curPosMS) {
    HitObject::onReset(curPosMS);

    m_waiting = false;
    m_shakeAnimation = 0.0f;

    if(m_view != nullptr) {
        m_hitAnimation.stop();
    }

    if(m_clickTimeMS > curPosMS) {
        m_finished = false;
        m_hitAnimation = 0.0f;
    } else {
        m_finished = true;
        m_hitAnimation = 1.0f;
    }
}

vec2 Circle::getAutoCursorPos(i32 /*curPos*/) const { return m_judge->osuCoords2Pixels(m_rawPos); }

Slider::Slider(SLIDERCURVETYPE stype, i32 repeat, f32 pixelLength, std::vector<vec2> points,
               const std::vector<f32> &ticks, f32 sliderTimeMS, f32 sliderTimeMSWithoutRepeats, i32 timeMS,
               DatabaseBeatmapTypes::HITSAMPLE_BITS hoverSamples,
               std::vector<DatabaseBeatmapTypes::HITSAMPLE_BITS> edgeSamples, i32 comboNumber, bool isEndOfCombo,
               i32 colorCounter, i32 colorOffset, AbstractBeatmapInterface *judge, const PlayfieldView *view)
    : HitObject(timeMS, hoverSamples, comboNumber, isEndOfCombo, colorCounter, colorOffset, judge, view),
      m_ctrlPoints(std::move(points)),
      m_edgeSamples(std::move(edgeSamples)),
      // build curve
      m_curve(stype, m_ctrlPoints, std::abs(pixelLength)),
      m_sliderTimeMS(sliderTimeMS),
      m_sliderTimeMSWithoutRepeats(sliderTimeMSWithoutRepeats),
      m_repeat(repeat) {
    m_type = HitObjectType::SLIDER;

    // build raw ticks
    for(f32 tick : ticks) {
        m_ticks.emplace_back(SLIDERTICK{.percent = tick, .finished = false});
    }

    // build repeats
    for(i32 i = 0; i < (m_repeat - 1); i++) {
        m_clicks.emplace_back(SLIDERCLICK{
            .timeMS = m_clickTimeMS + (i32)(m_sliderTimeMSWithoutRepeats * (i + 1)),
            .type = 0,
            .tickIndex = 0,
            .finished = false,
            .successful = false,
            .sliderend = ((i % 2) == 0),  // for hit animation on repeat hit
        });
    }

    // build ticks
    for(i32 i = 0; i < m_repeat; i++) {
        for(i32 t = 0; t < m_ticks.size(); t++) {
            // NOTE: repeat ticks are not necessarily symmetric.
            //
            // e.g. this slider: [1]=======*==[2]
            //
            // the '*' is where the tick is, let's say percent = 0.75
            // on repeat 0, the tick is at: click_time + 0.75*m_fSliderTimeWithoutRepeats
            // but on repeat 1, the tick is at: click_time + 1*m_fSliderTimeWithoutRepeats + (1.0 -
            // 0.75)*m_fSliderTimeWithoutRepeats this gives better readability at the cost of invalid rhythms: ticks are
            // guaranteed to always be at the same position, even in repeats so, depending on which repeat we are in
            // (even or odd), we either do (percent) or (1.0 - percent)

            const f32 tickPercentRelativeToRepeatFromStartAbs =
                (((i + 1) % 2) != 0 ? m_ticks[t].percent : 1.0f - m_ticks[t].percent);

            m_clicks.emplace_back(
                SLIDERCLICK{.timeMS = m_clickTimeMS + (i32)(m_sliderTimeMSWithoutRepeats * i) +
                                      (i32)(tickPercentRelativeToRepeatFromStartAbs * m_sliderTimeMSWithoutRepeats),
                            .type = 1,
                            .tickIndex = t,
                            .finished = false,
                            .successful = false,
                            .sliderend = false});
        }
    }

    m_durationMS = (i32)m_sliderTimeMS;
    m_durationMS = m_durationMS >= 0 ? m_durationMS : 1;  // force clamp to positive range
}

// shared to avoid per-slider per-draw allocations
static CONSTINIT std::vector<vec2> alwaysPointsBuf;
static CONSTINIT std::vector<vec2> legacyScreenPointsBuf;

void Slider::draw() {
    if(m_ctrlPoints.size() <= 0) return;

    const f32 foscale = cv::circle_fade_out_scale.getFloat();
    const Skin *skin = m_view->getSkin();

    const ModFlags curGameplayFlags = m_view->getModFlags();

    const bool hd = flags::has<ModFlags::Hidden>(curGameplayFlags);
    const bool tc = flags::has<ModFlags::Traceable>(curGameplayFlags);

    SliderRenderer::draw(*this);

    const bool isCompletelyFinished = m_startFinished && m_endFinished && m_finished;
    // extra possibility to avoid flicker between HitObject::m_bVisible delay and the fadeout animation below
    const bool drawParts = (m_visible || (m_startFinished && !m_finished)) && !isCompletelyFinished;
    const bool has_points = m_ctrlPoints.size() > 1;
    if(drawParts) {
        const f32 alpha = (cv::mod_hd_slider_fast_fade.getBool() ? m_alpha : m_bodyAlpha);
        const f32 sliderSnake = getSnakeRange().second;

        // draw slider ticks
        Color tickColor = 0xffffffff;
        tickColor = Colors::scale(tickColor, m_hittableDimRGBColorMultiplierPct);
        const f32 tickImageScale =
            (m_view->getHitcircleDiameter() / (16.0f * (skin->i_slider_score_point.scale()))) * 0.125f;
        for(const auto &tick : m_ticks) {
            if(tick.finished || tick.percent > sliderSnake) continue;

            vec2 pos = m_view->osuCoords2Pixels(curvePointAt(tick.percent));

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
            bool sliderRepeatStartCircleFinished = (m_repeat < 2);
            bool sliderRepeatEndCircleFinished = false;
            bool endCircleIsAtActualSliderEnd = true;
            for(const auto &click : m_clicks) {
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
                (!flags::has<ModFlags::StrictTracking>(curGameplayFlags) || m_endResult != LiveHitResult::HIT_MISS);

            const bool draw_end =
                ((!m_endFinished && m_repeat % 2 != 0 && ifStrictTrackingModShouldDrawEndCircle) ||
                 (!sliderRepeatEndCircleFinished &&
                  (ifStrictTrackingModShouldDrawEndCircle || (m_repeat > 1 && endCircleIsAtActualSliderEnd) ||
                   (m_repeat > 1 && std::abs(m_repeat - m_curRepeat) > 2))));

            const bool draw_start =
                (!m_startFinished ||
                 (!sliderRepeatStartCircleFinished &&
                  (ifStrictTrackingModShouldDrawEndCircle || (m_repeat > 1 && !endCircleIsAtActualSliderEnd) ||
                   (m_repeat > 1 && std::abs(m_repeat - m_curRepeat) > 2))) ||
                 (!m_endFinished && m_repeat % 2 == 0 && ifStrictTrackingModShouldDrawEndCircle));

            const f32 circle_alpha = m_alpha;

            // end circle
            if(draw_end) drawEndCircle(circle_alpha, sliderSnake);

            // start circle
            if(draw_start) drawStartCircle(circle_alpha);
        }
    }

    // posed with a trail: the head stays, white, fading out (under the reverse arrows, as in osu!stable)
    if(m_trailAlpha > 0.0f && !tc) {
        const i32 animTimeOffset = m_clickTimeMS - m_approachTimeMS;
        skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
        skin->i_slider_start_circle_overlay2.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
        Circle::drawCircleIn(*m_view, curvePointAt(0.0f), m_comboNumber, m_colorCounter, 0xffffffff, 1.0f, m_trailAlpha,
                             m_trailAlpha, true, true);
    }

    if(drawParts) {
        // draw reverse arrows
        const bool reversePossible = has_points && m_reverseArrowAlpha > 0.0f;
        const bool reverseEnd = reversePossible && (m_reverseArrowPos == 2 || m_reverseArrowPos == 3);
        const bool reverseStart = reversePossible && (m_reverseArrowPos == 1 || m_reverseArrowPos == 3);
        if(reverseEnd || reverseStart) {
            // if the combo color is nearly white, blacken the reverse arrow
            Color comboColor = m_view->getComboColor(m_colorCounter, m_colorOffset);
            Color reverseArrowColor = 0xffffffff;
            if((comboColor.Rf() + comboColor.Gf() + comboColor.Bf()) / 3.0f >
               cv::slider_reverse_arrow_black_threshold.getFloat())
                reverseArrowColor = 0xff000000;

            reverseArrowColor = Colors::scale(reverseArrowColor, m_hittableDimRGBColorMultiplierPct);

            f32 div = 0.30f;
            f32 pulse = (div - std::fmod(std::abs(m_view->getCurMusicPos()) / 1000.0f, div)) / div;
            pulse *= pulse;  // quad in

            if(!cv::slider_reverse_arrow_animated.getBool() || m_view->isInMafhamRenderChunk()) {
                pulse = 0.0f;
            }

            const auto &raImage = skin->i_reversearrow;
            const f32 osuCoordScaleMultiplier = m_view->getHitcircleDiameter() / m_view->getRawHitcircleDiameter();
            const f32 reverseArrowImageScale =
                ((m_view->getRawHitcircleDiameter() / (128.0f * raImage.scale())) * osuCoordScaleMultiplier) *
                (1.0f + pulse * 0.30f);

            // end and/or start
            for(i32 rev = 0; rev < 2; ++rev) {
                const bool isEnd = rev == 0;
                if(isEnd && !reverseEnd) continue;
                if(!isEnd && !reverseStart) continue;
                vec2 pos = m_view->osuCoords2Pixels(curvePointAt(isEnd ? 1.f : 0.f));
                const f32 rotation =
                    m_view->osuAngle2PixelAngle(isEnd ? m_curve.getEndAngle() : m_curve.getStartAngle());

                g->setColor(Color(reverseArrowColor).setA(m_reverseArrowAlpha));

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
    const bool instafade_slider_head = cv::instafade.getBool();

    const bool do_endhit_animations = !tc && !hd && !instafade_slider_head;  // no animations with traceable/hidden here
    const bool do_starthit_animations = do_endhit_animations && cv::slider_sliderhead_fadeout.getBool();
    for(uSz i = 0; i < m_clickAnimations.size();) {
        if(!m_clickAnimations[i].isAnimating()) {
            m_clickAnimations[i] = std::move(m_clickAnimations.back());
            m_clickAnimations.pop_back();
            continue;
        }
        auto &anim = m_clickAnimations[i];
        ++i;

        if(do_starthit_animations && (anim.type & HitAnim::HEAD)) {
            const f32 alpha = 1.0f - anim.percent;
            const f32 number_alpha = alpha;

            f32 scale = anim.percent;
            scale = -scale * (scale - 2.0f);  // quad out scale

            const bool drawNumber = (skin->version > 1.0f ? false : true) && m_curRepeat < 1;

            g->pushTransform();
            {
                g->scale((1.0f + scale * foscale), (1.0f + scale * foscale));
                if(m_curRepeat < 1) {
                    const i32 animTimeOffset = !m_view->isInMafhamRenderChunk() ? m_clickTimeMS - m_approachTimeMS
                                                                                : m_view->getCurMusicPosWithOffsets();

                    skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
                    skin->i_slider_start_circle_overlay2.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);

                    Circle::drawSliderStartCircle(*m_view, curvePointAt(0.0f), m_comboNumber, m_colorCounter,
                                                  m_colorOffset, 1.0f, 1.0f, alpha, number_alpha, drawNumber);
                } else {
                    const i32 animTimeOffset =
                        !m_view->isInMafhamRenderChunk() ? m_clickTimeMS : m_view->getCurMusicPosWithOffsets();

                    skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
                    skin->i_slider_end_circle_overlay2.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);

                    Circle::drawSliderEndCircle(*m_view, curvePointAt(0.0f), m_comboNumber, m_colorCounter,
                                                m_colorOffset, 1.0f, 1.0f, alpha, alpha, drawNumber);
                }
            }
            g->popTransform();
        }
        if(do_endhit_animations && (anim.type & HitAnim::TAIL)) {
            const f32 alpha = 1.0f - anim.percent;

            f32 scale = anim.percent;
            scale = -scale * (scale - 2.0f);  // quad out scale

            g->pushTransform();
            {
                g->scale((1.0f + scale * foscale), (1.0f + scale * foscale));

                const i32 animTimeOffset = !m_view->isInMafhamRenderChunk() ? m_clickTimeMS - m_fadeInTimeMS
                                                                            : m_view->getCurMusicPosWithOffsets();

                skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
                skin->i_slider_end_circle_overlay2.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);

                Circle::drawSliderEndCircle(*m_view, curvePointAt(1.0f), m_comboNumber, m_colorCounter, m_colorOffset,
                                            1.0f, 1.0f, alpha, 0.0f, false);
            }
            g->popTransform();
        }
    }

    HitObject::draw();
}

void Slider::draw2(bool drawApproachCircle, bool drawOnlyApproachCircle) {
    HitObject::draw2();

    const Skin *skin = m_view->getSkin();

    // HACKHACK: so much code duplication aaaaaaah
    if((m_visible || (m_startFinished && !m_finished)) &&
       drawApproachCircle)  // extra possibility to avoid flicker between HitObject::m_bVisible delay and the fadeout
                            // animation below this if block
    {
        if(m_ctrlPoints.size() > 1) {
            // HACKHACK: very dirty code
            bool sliderRepeatStartCircleFinished = m_repeat < 2;
            for(auto &click : m_clicks) {
                if(click.type == 0) {
                    if(!click.sliderend) sliderRepeatStartCircleFinished = click.finished;
                }
            }

            // start circle
            if(!m_startFinished || !sliderRepeatStartCircleFinished || (!m_endFinished && m_repeat % 2 == 0)) {
                Circle::drawApproachCircle(*m_view, curvePointAt(0.0f), m_comboNumber, m_colorCounter, m_colorOffset,
                                           m_hittableDimRGBColorMultiplierPct, m_approachScale,
                                           m_alphaForApproachCircle, m_overrideHDApproachCircle);
            }
        }
    }

    if(drawApproachCircle && m_trailAlpha > 0.0f) {
        Circle::drawApproachCircle(*m_view, curvePointAt(0.0f), m_comboNumber, m_colorCounter, m_colorOffset, 1.0f,
                                   m_trailApproachScale, m_trailAlpha);
    }

    if(drawApproachCircle && drawOnlyApproachCircle) return;

    // draw followcircle
    // HACKHACK: this is not entirely correct (due to m_bHeldTillEnd, if held within 300 range but then released, will
    // flash followcircle at the end)
    bool should_draw_followcircle = (m_visible && m_tracking);
    should_draw_followcircle |= (m_finished && m_followCircleAnimationAlpha > 0.0f && m_heldTillEnd);

    if(should_draw_followcircle) {
        vec2 point = m_view->osuCoords2Pixels(m_curPointRaw);

        // HACKHACK: this is shit
        f32 tickAnimation =
            (m_followCircleTickAnimationScale < 0.1f ? m_followCircleTickAnimationScale / 0.1f
                                                     : (1.0f - m_followCircleTickAnimationScale) / 0.9f);
        if(m_followCircleTickAnimationScale < 0.1f) {
            tickAnimation = -tickAnimation * (tickAnimation - 2.0f);
            tickAnimation = std::clamp<f32>(tickAnimation / 0.02f, 0.0f, 1.0f);
        }
        f32 tickAnimationScale = 1.0f + tickAnimation * cv::slider_followcircle_tick_pulse_scale.getFloat();

        g->setColor(Color(0xffffffff).setA(m_followCircleAnimationAlpha));

        skin->i_slider_follow_circle.setAnimationTimeOffset(skin->anim_speed, m_clickTimeMS);
        skin->i_slider_follow_circle.drawRaw(
            point,
            (m_view->getSliderFollowCircleDiameter() / skin->i_slider_follow_circle.getSizeBaseRaw().x) *
                tickAnimationScale * m_followCircleAnimationScale *
                0.85f);  // this is a bit strange, but seems to work perfectly with 0.85
    }

    const bool isCompletelyFinished = m_startFinished && m_endFinished && m_finished;

    // draw sliderb on top of everything
    if((m_visible || (m_startFinished && !m_finished)) &&
       !isCompletelyFinished)  // extra possibility in the if-block to avoid flicker between HitObject::m_bVisible
                               // delay and the fadeout animation below this if-block
    {
        if(m_slidePct > 0.0f) {
            // draw sliderb
            vec2 point = m_view->osuCoords2Pixels(m_curPointRaw);
            vec2 c1 =
                m_view->osuCoords2Pixels(curvePointAt(m_slidePct + 0.01f <= 1.0f ? m_slidePct : m_slidePct - 0.01f));
            vec2 c2 =
                m_view->osuCoords2Pixels(curvePointAt(m_slidePct + 0.01f <= 1.0f ? m_slidePct + 0.01f : m_slidePct));
            f32 ballAngle = vec::degrees(std::atan2(c2.y - c1.y, c2.x - c1.x));
            if(skin->o_sliderball_flip) ballAngle += (m_curRepeat % 2 == 0) ? 0 : 180;

            g->setColor(skin->o_allow_sliderball_tint ? (cv::slider_ball_tint_combo_color.getBool()
                                                             ? m_view->getComboColor(m_colorCounter, m_colorOffset)
                                                             : skin->c_slider_ball)
                                                      : rgb(255, 255, 255));
            g->pushTransform();
            {
                g->rotate(ballAngle);
                skin->i_sliderb.setAnimationTimeOffset(skin->anim_speed, m_clickTimeMS);
                skin->i_sliderb.drawRaw(point, m_view->getHitcircleDiameter() / skin->i_sliderb.getSizeBaseRaw().x);
            }
            g->popTransform();
        }
    }
}

void Slider::drawStartCircle(f32 alpha) {
    const Skin *skin = m_view->getSkin();

    if(m_startFinished) {
        const i32 animTimeOffset =
            !m_view->isInMafhamRenderChunk() ? m_clickTimeMS : m_view->getCurMusicPosWithOffsets();

        skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
        skin->i_slider_end_circle_overlay2.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);

        Circle::drawSliderEndCircle(*m_view, curvePointAt(0.0f), m_comboNumber, m_colorCounter, m_colorOffset,
                                    m_hittableDimRGBColorMultiplierPct, 1.0f, alpha, 0.0f, false, false);
    } else {
        const i32 visibleTms =
            flags::has<ModFlags::FreezeFrame>(m_view->getModFlags()) ? m_comboStartMS : m_clickTimeMS;
        const i32 animTimeOffset =
            !m_view->isInMafhamRenderChunk() ? visibleTms - m_approachTimeMS : m_view->getCurMusicPosWithOffsets();

        skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
        skin->i_slider_start_circle_overlay2.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);

        Circle::drawSliderStartCircle(*m_view, curvePointAt(0.0f), m_comboNumber, m_colorCounter, m_colorOffset,
                                      m_hittableDimRGBColorMultiplierPct, m_approachScale, alpha, alpha,
                                      !m_hideNumberAfterFirstRepeatHit, m_overrideHDApproachCircle);
    }
}

void Slider::drawEndCircle(f32 alpha, f32 sliderSnake) {
    const Skin *skin = m_view->getSkin();
    const i32 animTimeOffset =
        !m_view->isInMafhamRenderChunk() ? m_clickTimeMS - m_fadeInTimeMS : m_view->getCurMusicPosWithOffsets();

    skin->i_hitcircleoverlay.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);
    skin->i_slider_end_circle_overlay2.setAnimationTimeOffset(skin->anim_speed, animTimeOffset);

    Circle::drawSliderEndCircle(*m_view, curvePointAt(sliderSnake), m_comboNumber, m_colorCounter, m_colorOffset,
                                m_hittableDimRGBColorMultiplierPct, 1.0f, alpha, 0.0f, false, false);
}

std::optional<SliderRenderer::Body> Slider::getBody() const {
    if(m_ctrlPoints.size() <= 0) return std::nullopt;

    const bool isCompletelyFinished = m_startFinished && m_endFinished && m_finished;
    if((m_visible || (m_startFinished && !m_finished)) && !isCompletelyFinished) {
        const f32 alpha = (cv::mod_hd_slider_fast_fade.getBool() ? m_alpha : m_bodyAlpha);
        if(alpha <= 0.0f || !cv::slider_draw_body.getBool()) return std::nullopt;

        const auto [from, to] = getSnakeRange();
        return makeBody(alpha, from, to);
    }

    // fading out after the end hit (which finishes the slider, so never together with the live body above)
    const bool slider_fading_out = m_endSliderBodyFadeAnimation > 0.0f && m_endSliderBodyFadeAnimation != 1.0f;
    if(flags::has<ModFlags::Hidden>(m_view->getModFlags()) || cv::instafade_sliders.getBool() || !slider_fading_out)
        return std::nullopt;

    if(!cv::slider_shrink.getBool()) return makeBody(1.0f - m_endSliderBodyFadeAnimation, 0, 1);
    if(!cv::slider_body_lazer_fadeout_style.getBool()) return std::nullopt;

    alwaysPointsBuf.clear();
    alwaysPointsBuf.push_back(m_view->osuCoords2Pixels(curvePointAt(m_slidePct)));
    return SliderRenderer::Body{.alwaysPoints = alwaysPointsBuf,
                                .hitcircleDiameter = m_view->getHitcircleDiameter(),
                                .from = 0.0f,
                                .to = 0.0f,
                                .skinSettings = {m_view->getSkin()},
                                .undimmedColor = m_view->getComboColor(m_colorCounter, m_colorOffset),
                                .alpha = 1.0f - m_endSliderBodyFadeAnimation,
                                .sliderTimeForRainbow = m_clickTimeMS};
}

SliderRenderer::Body Slider::makeBody(f32 alpha, f32 from, f32 to) const {
    alwaysPointsBuf.clear();
    // smooth begin/end while snaking/shrinking
    if(cv::slider_body_smoothsnake.getBool()) {
        if(cv::slider_shrink.getBool() && m_sliderSnakePercent > 0.999f) {
            alwaysPointsBuf.push_back(m_view->osuCoords2Pixels(curvePointAt(m_slidePct)));  // curpoint
            alwaysPointsBuf.push_back(m_view->osuCoords2Pixels(
                getRawPosAt(getEndTime() + 1)));  // endpoint (because setDrawPercent() causes the last
                                                  // circle mesh to become invisible too quickly)
        }
        if(cv::snaking_sliders.getBool() && m_sliderSnakePercent < 1.0f)
            alwaysPointsBuf.push_back(m_view->osuCoords2Pixels(
                curvePointAt(m_sliderSnakePercent)));  // snakeoutpoint (only while snaking out)
    }

    SliderRenderer::Body body{.alwaysPoints = alwaysPointsBuf,
                              .hitcircleDiameter = m_view->getHitcircleDiameter(),
                              .from = from,
                              .to = to,
                              .skinSettings = {m_view->getSkin()},
                              .undimmedColor = m_view->getComboColor(m_colorCounter, m_colorOffset),
                              .colorRGBMultiplier = m_hittableDimRGBColorMultiplierPct,
                              .alpha = alpha,
                              .sliderTimeForRainbow = m_clickTimeMS};

    if(m_view->slidersRenderDynamically()) {
        // peppy sliders: the shape changes every frame
        legacyScreenPointsBuf.clear();
        Mc::ranges::assign(legacyScreenPointsBuf, m_curve.getPoints());
        for(auto &screenPoint : legacyScreenPointsBuf) {
            screenPoint = m_view->osuCoords2Pixels(screenPoint - m_stackOffset);
        }
        body.points = legacyScreenPointsBuf;
    } else {
        // vertex buffered sliders
        // as the base mesh is centered at (0, 0, 0) and in raw osu coordinates, we have to scale and translate it to
        // make it fit the actual desktop playfield
        body.mesh = &m_mesh;
        body.scale = m_view->getPlayfieldScaleFactor();
        body.translation = m_view->getPlayfieldCenter();

        if(m_view->hasFailed())
            body.translation =
                m_view->osuCoords2Pixels(vec2(GameRules::OSU_COORD_WIDTH / 2, GameRules::OSU_COORD_HEIGHT / 2));

        if(flags::has<ModFlags::FPS>(m_view->getModFlags())) body.translation += m_view->getFirstPersonCursorDelta();
    }
    return body;
}

std::pair<f32, f32> Slider::getSnakeRange() const {
    f32 from = 0.0f;
    f32 to = cv::snaking_sliders.getBool() ? m_sliderSnakePercent : 1.0f;

    // shrinking sliders
    if(cv::slider_shrink.getBool() && m_reverseArrowPos == 0) {
        from = m_inReverse ? 0.0f : m_slidePct;
        if(m_inReverse) to = m_slidePct;
    }
    return {from, to};
}

void Slider::update(i32 curPosMS, f64 frameTimeSecs) {
    HitObject::update(curPosMS, frameTimeSecs);

    // stop slide sound while paused
    if(m_judge->isPaused() || !m_judge->isPlaying() || m_judge->hasFailed()) {
        m_judge->stopSliderSounds(m_lastSliderSampleSets);
    }

    // animations must be updated even if we are finished
    if(m_view != nullptr) updateAnimations(curPosMS, m_view->getSpeedAdjustedAnimationSpeed());

    // all further calculations are only done while we are active
    if(m_finished) {
        this->updateTracking();  // (the keys still change)
        return;
    }

    const ModFlags curIFaceMods = m_judge->getMods().flags;

    this->updateSlideLook(curPosMS, curIFaceMods);
    m_curPoint = m_judge->osuCoords2Pixels(m_curPointRaw);

    // No longer ignore keys that were released since entering the slider
    // see isClickHeldSlider()
    m_ignoredKeys &= m_judge->getKeys();

    // handle dynamic followradius
    f32 followRadius = m_cursorLeft ? m_judge->fHitcircleDiameter / 2.0f : m_judge->fSliderFollowCircleDiameter / 2.0f;
    const bool isPlayfieldCursorInside = (vec::length(m_judge->getCursorPos() - m_curPoint) < followRadius);
    const bool isAutoCursorInside =
        ((flags::has<ModFlags::Autoplay>(curIFaceMods)) &&
         (!cv::auto_cursordance.getBool() || (vec::length(m_judge->getCursorPos() - m_curPoint) < followRadius)));
    m_cursorInside = (isAutoCursorInside || isPlayfieldCursorInside);
    m_cursorLeft = !m_cursorInside;
    this->updateTracking();

    // handle slider start
    if(!m_startFinished) {
        if((flags::has<ModFlags::Autoplay>(curIFaceMods))) {
            if(curPosMS >= m_clickTimeMS) {
                onHit(LiveHitResult::HIT_300, 0, false);
                m_judge->holding_slider = true;
            }
        } else {
            i32 deltaMS = curPosMS - m_clickTimeMS;

            if((flags::has<ModFlags::Relax>(curIFaceMods))) {
                if(curPosMS >= m_clickTimeMS + (i32)cv::relax_offset.getInt() && !m_judge->isPaused() &&
                   !m_judge->isContinueScheduled()) {
                    const vec2 pos = m_judge->osuCoords2Pixels(curvePointAt(0.0f));
                    const f32 cursorDelta = vec::length(m_judge->getCursorPos() - pos);
                    if((cursorDelta < m_judge->fHitcircleDiameter / 2.0f &&
                        (flags::has<ModFlags::Relax>(curIFaceMods)))) {
                        LiveHitResult result = m_judge->getHitResult(deltaMS);

                        if(result != LiveHitResult::HIT_NULL) {
                            const f32 targetDelta = cursorDelta / (m_judge->fHitcircleDiameter / 2.0f);
                            const f32 targetAngle = vec::degrees(
                                std::atan2(m_judge->getCursorPos().y - pos.y, m_judge->getCursorPos().x - pos.x));

                            m_startResult = result;
                            onHit(m_startResult, deltaMS, false, targetDelta, targetAngle);
                            m_judge->holding_slider = true;
                        }
                    }
                }
            }

            // wait for a miss
            if(deltaMS >= 0) {
                // if this is a miss after waiting
                if(deltaMS > (i32)m_judge->getHitWindow50()) {
                    m_startResult = LiveHitResult::HIT_MISS;
                    onHit(m_startResult, deltaMS, false);
                    m_judge->holding_slider = false;
                }
            }
        }
    }

    // handle slider end, repeats, ticks
    if(!m_endFinished) {
        // NOTE: we have 2 timing conditions after which we start checking for strict tracking: 1) startcircle was
        // clicked, 2) slider has started timing wise it is easily possible to hit the startcircle way before the
        // sliderball would become active, which is why the first check exists. even if the sliderball has not yet
        // started sliding, you will be punished for leaving the (still invisible) followcircle area after having
        // clicked the startcircle, always.
        const bool isTrackingStrictTrackingMod =
            ((m_startFinished || curPosMS >= m_clickTimeMS) && cv::mod_strict_tracking.getBool());

        // slider tail lenience bullshit: see
        // https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Objects/Slider.cs#L123 being "inside the slider"
        // (for the end of the slider) is NOT checked at the exact end of the slider, but somewhere random before,
        // because fuck you
        const i32 offsetMS = (i32)cv::slider_end_inside_check_offset.getInt();
        const i32 lenienceHackEndTimeMS = std::max(m_clickTimeMS + m_durationMS / 2, (getEndTime()) - offsetMS);
        const bool isTrackingCorrectly =
            (isClickHeldSlider() || (flags::has<ModFlags::Relax>(curIFaceMods))) && m_cursorInside;
        if(isTrackingCorrectly) {
            if(isTrackingStrictTrackingMod) {
                m_strictTrackingModLastClickHeldTime = curPosMS;
                if(m_strictTrackingModLastClickHeldTime ==
                   0)  // (prevent frame perfect inputs from not triggering the strict tracking miss because we use != 0
                       // comparison to detect if tracking correctly at least once)
                    m_strictTrackingModLastClickHeldTime = 1;
            }

            // only check it at the exact point in time ...
            if(curPosMS >= lenienceHackEndTimeMS) {
                // ... once (like a tick)
                if(!m_heldTillEndForLenienceHackCheck) {
                    // player was correctly clicking/holding inside slider at lenienceHackEndTime
                    m_heldTillEndForLenienceHackCheck = true;
                    m_heldTillEndForLenienceHack = true;
                }
            }
        } else {
            // do not allow empty clicks outside of the circle radius to prevent the
            // m_bCursorInside flag from resetting
            m_cursorLeft = true;
        }

        // can't be "inside the slider" after lenienceHackEndTime
        // (even though the slider is still going, which is madness)
        if(curPosMS >= lenienceHackEndTimeMS) m_heldTillEndForLenienceHackCheck = true;

        // handle strict tracking mod
        if(isTrackingStrictTrackingMod) {
            const bool wasTrackingCorrectlyAtLeastOnce = (m_strictTrackingModLastClickHeldTime != 0);
            if(wasTrackingCorrectlyAtLeastOnce && !isTrackingCorrectly) {
                // if past lenience end time then don't trigger a strict tracking miss,
                // since the slider is then already considered fully finished gameplay wise
                if(!m_heldTillEndForLenienceHack) {
                    // force miss the end once, if it has not already been force missed by notelock
                    if(m_endResult == LiveHitResult::HIT_NULL) {
                        // force miss endcircle
                        onSliderBreak();

                        m_heldTillEnd = false;
                        m_heldTillEndForLenienceHack = false;
                        m_heldTillEndForLenienceHackCheck = true;
                        m_endResult = LiveHitResult::HIT_MISS;

                        // end of combo, ignore in hiterrorbar, ignore combo, subtract health
                        addHitResult(m_endResult, 0, m_endOfCombo, getRawPosAt(getEndTime()), -1.0f, 0.0f, true, true,
                                     false);
                    }
                }
            }
        }

        // handle repeats and ticks
        for(auto &click : m_clicks) {
            if(!click.finished && curPosMS >= click.timeMS) {
                click.finished = true;
                click.successful = (isClickHeldSlider() && m_cursorInside) ||
                                   (flags::has<ModFlags::Autoplay>(curIFaceMods)) ||
                                   ((flags::has<ModFlags::Relax>(curIFaceMods)) && m_cursorInside);

                if(click.type == 0) {
                    onRepeatHit(click);
                } else {
                    onTickHit(click);
                }
            }
        }

        // handle auto, and the last circle
        if((flags::has<ModFlags::Autoplay>(curIFaceMods))) {
            if(curPosMS >= getEndTime()) {
                m_heldTillEnd = true;
                onHit(LiveHitResult::HIT_300, 0, true);
                m_judge->holding_slider = false;
            }
        } else {
            if(curPosMS >= getEndTime()) {
                // handle leftover startcircle
                {
                    // this may happen (if the slider time is shorter than the miss window of the startcircle)
                    if(m_startResult == LiveHitResult::HIT_NULL) {
                        // we still want to cause a sliderbreak in this case!
                        onSliderBreak();

                        // special case: missing the startcircle drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS
                        // health)
                        m_judge->addHitResult(this, LiveHitResult::HIT_MISS_SLIDERBREAK, 0, false, true, true, true,
                                              true,
                                              false);  // only decrease health

                        m_startResult = LiveHitResult::HIT_MISS;
                    }
                }

                // handle endcircle
                bool isEndResultComingFromStrictTrackingMod = false;
                if(m_endResult == LiveHitResult::HIT_NULL) {
                    m_heldTillEnd = m_heldTillEndForLenienceHack;
                    m_endResult = m_heldTillEnd ? LiveHitResult::HIT_300 : LiveHitResult::HIT_MISS;

                    // handle total slider result (currently startcircle + repeats + ticks + endcircle)
                    // clicks = (repeats + ticks)
                    const f32 numMaxPossibleHits = 1 + m_clicks.size() + 1;
                    f32 numActualHits = 0;

                    if(m_startResult != LiveHitResult::HIT_MISS) numActualHits++;
                    if(m_endResult != LiveHitResult::HIT_MISS) numActualHits++;

                    for(auto &click : m_clicks) {
                        if(click.successful) numActualHits++;
                    }

                    const f32 percent = numActualHits / numMaxPossibleHits;

                    const bool allow300 = (flags::has<ModFlags::ScoreV2>(curIFaceMods))
                                              ? (m_startResult == LiveHitResult::HIT_300)
                                              : true;
                    const bool allow100 =
                        (flags::has<ModFlags::ScoreV2>(curIFaceMods))
                            ? (m_startResult == LiveHitResult::HIT_300 || m_startResult == LiveHitResult::HIT_100)
                            : true;

                    // rewrite m_endResult as the whole slider result, then use it for the final onHit()
                    if(percent >= 0.999f && allow300)
                        m_endResult = LiveHitResult::HIT_300;
                    else if(percent >= 0.5f && allow100 && !flags::has<ModFlags::Ming3012>(curIFaceMods) &&
                            !flags::has<ModFlags::No100s>(curIFaceMods))
                        m_endResult = LiveHitResult::HIT_100;
                    else if(percent > 0.0f && !flags::has<ModFlags::No100s>(curIFaceMods) &&
                            !flags::has<ModFlags::No50s>(curIFaceMods))
                        m_endResult = LiveHitResult::HIT_50;
                    else
                        m_endResult = LiveHitResult::HIT_MISS;

                    // debugLog("percent = {:f}", percent);

                    if(!m_heldTillEnd && cv::slider_end_miss_breaks_combo.getBool()) onSliderBreak();
                } else
                    isEndResultComingFromStrictTrackingMod = true;

                onHit(m_endResult, 0, true, 0.0f, 0.0f, isEndResultComingFromStrictTrackingMod);
                m_judge->holding_slider = false;
            }
        }

        // handle sliderslide sound
        // TODO @kiwec: move this to draw()
        const ModFlags curGameplayFlags = m_judge->getMods().flags;
        const bool sliding = m_startFinished && !m_endFinished && m_cursorInside && m_deltaMS <= 0             //
                             && (isClickHeldSlider() || (flags::has<ModFlags::Autoplay>(curGameplayFlags)) ||  //
                                 (flags::has<ModFlags::Relax>(curGameplayFlags)))                              //
                             && !m_judge->isPaused() && !m_judge->isWaiting() && m_judge->isPlaying();
        m_lastSliderSampleSets =
            m_judge->updateSliderSlideSounds(sliding, m_hitSamples, m_curPointRaw, m_lastSliderSampleSets);
    }
}

void Slider::pose(i32 timeMS, i32 fadeOutMS, i32 trailMS) {
    const ModFlags mods = m_view->getModFlags();
    const f32 animationSpeed = m_view->getSpeedAdjustedAnimationSpeed();
    this->updateLook(timeMS, mods, m_view->getApproachTime(), animationSpeed);

    const i32 endTimeMS = this->getEndTime();
    m_startFinished = timeMS >= m_clickTimeMS;
    m_endFinished = m_finished = timeMS >= endTimeMS;
    m_heldTillEnd = true;
    m_endResult = m_finished ? LiveHitResult::HIT_300 : LiveHitResult::HIT_NULL;
    for(auto &click : m_clicks) {
        click.finished = click.successful = timeMS >= click.timeMS;
    }
    for(auto &tick : m_ticks) {
        tick.finished = true;
    }
    for(const auto &click : m_clicks) {
        if(click.type == 1 && !click.finished) m_ticks[click.tickIndex].finished = false;
    }

    // the slide stops where the end was hit, as in play
    this->updateSlideLook(std::min(timeMS, endTimeMS), mods);
    m_tracking = m_startFinished;
    this->updateAnimations(timeMS, animationSpeed);

    // the head at the start, the repeats at theirs and the tail at the end, as onHit() and onRepeatHit() add them
    m_clickAnimations.clear();
    const auto addHitAnimAt = [&](i32 hitTimeMS, u8 typeFlags) {
        if(timeMS < hitTimeMS || timeMS - hitTimeMS >= fadeOutMS || m_clickAnimations.size() >= 128) return;
        m_clickAnimations.push_back(HitAnim{.percent{hitAnimationAt(timeMS - hitTimeMS, fadeOutMS)}, .type{typeFlags}});
    };
    const bool trail = m_startFinished && trailMS > 0;
    if(trailMS <= 0) {
        addHitAnimAt(m_clickTimeMS, HitAnim::HEAD);
        for(const auto &click : m_clicks) {
            if(click.type == 0) addHitAnimAt(click.timeMS, click.sliderend ? HitAnim::TAIL : HitAnim::HEAD);
        }
        addHitAnimAt(endTimeMS, m_repeat % 2 != 0 ? HitAnim::TAIL : HitAnim::HEAD);
    }
    m_trailAlpha = trail ? trailAlphaAt(timeMS - m_clickTimeMS, trailMS) : 0.0f;
    m_trailApproachScale = trail ? trailApproachScaleAt(timeMS - m_clickTimeMS) : 1.0f;

    m_endSliderBodyFadeAnimation =
        m_finished
            ? hitAnimationAt(timeMS - endTimeMS, (i32)(fadeOutMS * cv::slider_body_fade_out_time_multiplier.getFloat()))
            : 0.0f;

    // the follow circle pulses on every tick and repeat
    i32 lastPulseMS = -1;
    for(const auto &click : m_clicks) {
        if(click.finished) lastPulseMS = std::max(lastPulseMS, click.timeMS);
    }
    const f32 pulseMS = cv::slider_followcircle_tick_pulse_time.getFloat() * animationSpeed * 1000.0f;
    m_followCircleTickAnimationScale =
        lastPulseMS < 0 ? 0.0f : std::clamp<f32>((f32)(timeMS - lastPulseMS) / pulseMS, 0.0f, 1.0f);
}

void Slider::addSoundCues(std::vector<SoundCue> &out) const {
    if(m_ctrlPoints.size() == 0) return;

    const auto cue = [&](i32 timeMS, SoundCue::Kind kind, const DatabaseBeatmapTypes::HITSAMPLE_BITS &samples,
                         i32 endTimeMS) {
        out.push_back({.timeMS = timeMS,
                       .endTimeMS = endTimeMS,
                       .kind = kind,
                       .samples = samples,
                       .rawPos = this->getRawPosAt(timeMS)});
    };
    const i32 endTimeMS = this->getEndTime();
    cue(m_clickTimeMS, SoundCue::Kind::HIT, m_edgeSamples.front(), m_clickTimeMS);
    cue(m_clickTimeMS, SoundCue::Kind::SLIDE, m_hitSamples, endTimeMS);
    i32 repeat = 0;
    for(const auto &click : m_clicks) {
        if(click.type == 0) {
            cue(click.timeMS, SoundCue::Kind::HIT, this->getRepeatSamples(++repeat), click.timeMS);
        } else {
            cue(click.timeMS, SoundCue::Kind::TICK, m_hitSamples, click.timeMS);
        }
    }
    cue(endTimeMS, SoundCue::Kind::HIT, m_edgeSamples.back(), endTimeMS);
}

void Slider::updateSlideLook(i32 curPosMS, ModFlags mods) {
    // slider slide percent
    m_slidePct = 0.0f;
    if(curPosMS > m_clickTimeMS)
        m_slidePct = std::clamp<f32>(
            std::clamp<i32>((curPosMS - (m_clickTimeMS)), 0, (i32)m_sliderTimeMS) / m_sliderTimeMS, 0.0f, 1.0f);

    const i32 visibleTms = flags::has<ModFlags::FreezeFrame>(mods) ? m_comboStartMS : m_clickTimeMS;
    const f32 sliderSnakeDuration = (1.0f / 3.0f) * m_approachTimeMS * cv::slider_snake_duration_multiplier.getFloat();
    m_sliderSnakePercent = std::min(1.0f, (curPosMS - (visibleTms - m_approachTimeMS)) / (sliderSnakeDuration));

    const i32 reverseArrowFadeInStart =
        m_clickTimeMS - (cv::snaking_sliders.getBool() ? (m_approachTimeMS - sliderSnakeDuration) : m_approachTimeMS);
    const i32 reverseArrowFadeInEnd = reverseArrowFadeInStart + cv::slider_reverse_arrow_fadein_duration.getInt();
    m_reverseArrowAlpha = 1.0f - std::clamp<f32>(((f32)(reverseArrowFadeInEnd - curPosMS) /
                                                  (f32)(reverseArrowFadeInEnd - reverseArrowFadeInStart)),
                                                 0.0f, 1.0f);
    m_reverseArrowAlpha *= cv::slider_reverse_arrow_alpha_multiplier.getFloat();

    m_bodyAlpha = m_alpha;
    if(flags::has<ModFlags::Hidden>(mods)) {  // hidden modifies the body alpha
        m_bodyAlpha = m_alphaWithoutHidden;   // fade in as usual

        // fade out over the duration of the slider, starting exactly when the default fadein finishes
        // std::min() ensures that the fade always starts at click_time
        // (even if the fadeintime is longer than the approachtime)
        const i32 hiddenSliderBodyFadeOutStart = std::min(visibleTms, visibleTms - m_approachTimeMS + m_fadeInTimeMS);
        const f32 fade_percent = cv::mod_hd_slider_fade_percent.getFloat();
        const i32 hiddenSliderBodyFadeOutEnd = m_clickTimeMS + (i32)(fade_percent * m_sliderTimeMS);
        if(curPosMS >= hiddenSliderBodyFadeOutStart) {
            m_bodyAlpha = std::clamp<f32>(((f32)(hiddenSliderBodyFadeOutEnd - curPosMS) /
                                           (f32)(hiddenSliderBodyFadeOutEnd - hiddenSliderBodyFadeOutStart)),
                                          0.0f, 1.0f);
            m_bodyAlpha *= m_bodyAlpha;  // quad in body fadeout
        }
    }

    // if this slider is active, recalculate sliding/curve position and general state
    if(m_slidePct > 0.0f || m_visible) {
        // handle reverse sliders
        m_inReverse = false;
        m_hideNumberAfterFirstRepeatHit = false;
        if(m_repeat > 1) {
            if(m_slidePct > 0.0f && m_startFinished) m_hideNumberAfterFirstRepeatHit = true;

            f32 part = 1.0f / (f32)m_repeat;
            m_curRepeat = (i32)(m_slidePct * m_repeat);
            f32 baseSlidePercent = part * m_curRepeat;
            f32 partSlidePercent = (m_slidePct - baseSlidePercent) / part;
            if(m_curRepeat % 2 == 0) {
                m_slidePct = partSlidePercent;
                m_reverseArrowPos = 2;
            } else {
                m_slidePct = 1.0f - partSlidePercent;
                m_reverseArrowPos = 1;
                m_inReverse = true;
            }

            // no reverse arrow on the last repeat
            if(m_curRepeat == m_repeat - 1) m_reverseArrowPos = 0;

            // osu style: immediately show all coming reverse arrows (even on the circle we just started from)
            if(m_curRepeat < m_repeat - 2 && m_slidePct > 0.0f && m_repeat > 2) m_reverseArrowPos = 3;
        }

        m_curPointRaw = curvePointAt(m_slidePct);
    } else {
        m_curPointRaw = curvePointAt(0.0f);
    }
}

void Slider::updateAnimations(i32 curPosMS, f32 speedAdjustedAnimationSpeed) {
    f32 animation_multiplier = speedAdjustedAnimationSpeed;

    f32 fadein_fade_time = cv::slider_followcircle_fadein_fade_time.getFloat() * animation_multiplier;
    f32 fadeout_fade_time = cv::slider_followcircle_fadeout_fade_time.getFloat() * animation_multiplier;
    f32 fadein_scale_time = cv::slider_followcircle_fadein_scale_time.getFloat() * animation_multiplier;
    f32 fadeout_scale_time = cv::slider_followcircle_fadeout_scale_time.getFloat() * animation_multiplier;

    // handle followcircle animations
    m_followCircleAnimationAlpha = std::clamp<f32>(
        (f32)((curPosMS - m_clickTimeMS)) / 1000.0f / std::clamp<f32>(fadein_fade_time, 0.0f, m_durationMS / 1000.0f),
        0.0f, 1.0f);
    if(m_finished) {
        m_followCircleAnimationAlpha =
            1.0f - std::clamp<f32>((f32)((curPosMS - (getEndTime()))) / 1000.0f / fadeout_fade_time, 0.0f, 1.0f);
        m_followCircleAnimationAlpha *= m_followCircleAnimationAlpha;  // quad in
    }

    m_followCircleAnimationScale = std::clamp<f32>(
        (f32)((curPosMS - m_clickTimeMS)) / 1000.0f / std::clamp<f32>(fadein_scale_time, 0.0f, m_durationMS / 1000.0f),
        0.0f, 1.0f);
    if(m_finished) {
        m_followCircleAnimationScale =
            std::clamp<f32>((f32)((curPosMS - (getEndTime()))) / 1000.0f / fadeout_scale_time, 0.0f, 1.0f);
    }
    m_followCircleAnimationScale = -m_followCircleAnimationScale * (m_followCircleAnimationScale - 2.0f);  // quad out

    if(!m_finished)
        m_followCircleAnimationScale =
            cv::slider_followcircle_fadein_scale.getFloat() +
            (1.0f - cv::slider_followcircle_fadein_scale.getFloat()) * m_followCircleAnimationScale;
    else
        m_followCircleAnimationScale =
            1.0f - (1.0f - cv::slider_followcircle_fadeout_scale.getFloat()) * m_followCircleAnimationScale;
}

void Slider::updateStackPosition(f32 stackOffset, bool hardRock) {
    m_stackOffset = vec2{m_stackNum * stackOffset, m_stackNum * stackOffset * (hardRock ? -1.0f : 1.0f)};
}

void Slider::miss(i32 curPosMS) {
    if(m_finished) return;

    const i32 deltaMS = curPosMS - m_clickTimeMS;

    // startcircle
    if(!m_startFinished) {
        m_startResult = LiveHitResult::HIT_MISS;
        onHit(m_startResult, deltaMS, false);
        m_judge->holding_slider = false;
    }

    // endcircle, repeats, ticks
    if(!m_endFinished) {
        // repeats, ticks
        {
            for(auto &click : m_clicks) {
                if(!click.finished) {
                    click.finished = true;
                    click.successful = false;

                    if(click.type == 0)
                        onRepeatHit(click);
                    else
                        onTickHit(click);
                }
            }
        }

        // endcircle
        {
            m_heldTillEnd = m_heldTillEndForLenienceHack;

            if(!m_heldTillEnd && cv::slider_end_miss_breaks_combo.getBool()) onSliderBreak();

            m_endResult = LiveHitResult::HIT_MISS;
            onHit(m_endResult, 0, true);
            m_judge->holding_slider = false;
        }
    }
}

vec2 Slider::getRawPosAt(i32 posMS) const {
    if(posMS <= m_clickTimeMS)
        return curvePointAt(0.0f);
    else if(posMS >= m_clickTimeMS + m_sliderTimeMS) {
        if(m_repeat % 2 == 0)
            return curvePointAt(0.0f);
        else
            return curvePointAt(1.0f);
    } else
        return curvePointAt(getT(posMS, false));
}

vec2 Slider::getOriginalRawPosAt(i32 posMS) const {
    if(posMS <= m_clickTimeMS)
        return m_curve.pointAt(0.0f);
    else if(posMS >= m_clickTimeMS + m_sliderTimeMS) {
        if(m_repeat % 2 == 0)
            return m_curve.pointAt(0.0f);
        else
            return m_curve.pointAt(1.0f);
    } else
        return m_curve.pointAt(getT(posMS, false));
}

f32 Slider::getT(i32 posMS, bool raw) const {
    f32 t = (f32)((i32)posMS - m_clickTimeMS) / m_sliderTimeMSWithoutRepeats;
    if(raw)
        return t;
    else {
        auto floorVal = (f32)std::floor(t);
        return ((i32)floorVal % 2 == 0) ? t - floorVal : floorVal + 1 - t;
    }
}

bool Slider::isClickableFrom(i32 music_pos, vec2 cursor_pos) const {
    if(m_ctrlPoints.size() == 0 || m_startFinished || m_blocked) return false;
    if(m_judge->getHitResult(music_pos - m_clickTimeMS) == LiveHitResult::HIT_NULL) return false;

    const vec2 pos = m_judge->osuCoords2Pixels(curvePointAt(0.0f));
    const f32 cursorDelta = vec::length(cursor_pos - pos);
    if(cursorDelta >= m_judge->fHitcircleDiameter / 2.0f) return false;

    return true;
}

void Slider::onClickEvent(std::vector<Click> &clicks) {
    if(m_ctrlPoints.size() == 0 || m_blocked)
        return;  // also handle note blocking here (doesn't need fancy shake logic, since sliders don't shake in
                 // osu!stable)

    if(!m_startFinished) {
        const vec2 cursorPos = clicks[0].cursorPos;
        const vec2 pos = m_judge->osuCoords2Pixels(curvePointAt(0.0f));
        const f32 cursorDelta = vec::length(cursorPos - pos);

        if(cursorDelta < m_judge->fHitcircleDiameter / 2.0f) {
            const i32 deltaMS = clicks[0].musicPosMS - m_clickTimeMS;

            LiveHitResult result = m_judge->getHitResult(deltaMS);
            if(result != LiveHitResult::HIT_NULL) {
                const f32 targetDelta = cursorDelta / (m_judge->fHitcircleDiameter / 2.0f);
                const f32 targetAngle = vec::degrees(std::atan2(cursorPos.y - pos.y, cursorPos.x - pos.x));

                clicks.erase(clicks.begin());
                m_startResult = result;
                onHit(m_startResult, deltaMS, false, targetDelta, targetAngle);
                m_judge->holding_slider = true;
            }
        }
    }
}

void Slider::onHit(LiveHitResult result, i32 delta, bool isEndCircle, f32 targetDelta, f32 targetAngle,
                   bool isEndResultFromStrictTrackingMod) {
    if(m_ctrlPoints.size() == 0) return;

    // start + end of a slider add +30 points, if successful

    // debugLog("isEndCircle = {:d},    m_iCurRepeat = {:d}", (i32)isEndCircle, iCurRepeat);

    // sound and hit animation and also sliderbreak combo drop
    {
        if(result == LiveHitResult::HIT_MISS) {
            if(!isEndResultFromStrictTrackingMod) onSliderBreak();
        } else {
            if(m_edgeSamples.size() > 0) {
                if(isEndCircle) {
                    m_judge->playHitSound(m_edgeSamples.back(), m_curPointRaw, delta, getEndTime());
                } else {
                    m_judge->playHitSound(m_edgeSamples[0], m_curPointRaw, delta, m_clickTimeMS);
                }
            }

            if(m_view != nullptr) {
                const f32 fadeoutTimeSecs = GameRules::getFadeOutTime(m_view->getBaseAnimationSpeed());

                if(!isEndCircle) {
                    addHitAnim(HitAnim::HEAD, fadeoutTimeSecs);
                } else {
                    if(m_repeat % 2 != 0) {
                        addHitAnim(HitAnim::TAIL, fadeoutTimeSecs);
                    } else {
                        addHitAnim(HitAnim::HEAD, fadeoutTimeSecs);
                    }
                }
            }
        }

        // end body fadeout
        if(isEndCircle) {
            if(m_view != nullptr) {
                m_endSliderBodyFadeAnimation = 0.001f;  // quickfix for 1 frame missing images
                m_endSliderBodyFadeAnimation.set(1.0f,
                                                 GameRules::getFadeOutTime(m_view->getBaseAnimationSpeed()) *
                                                     cv::slider_body_fade_out_time_multiplier.getFloat(),
                                                 anim::QuadOut);
            }
            // debugLog("stopping due to end body fadeout");
            m_judge->stopSliderSounds(m_lastSliderSampleSets);
        }
    }

    // add score, and we are finished
    if(!isEndCircle) {
        // startcircle

        m_startFinished = true;

        // ignore all keys that were held prior to entering the slider
        // except the one used to tap the slider head (or, "hold into" the slider)
        // see isClickHeldSlider()
        m_ignoredKeys = (m_judge->getKeys() & ~m_judge->lastPressedKey);
        this->updateTracking();

        if(flags::has<ModFlags::Target>(m_judge->getMods().flags)) {
            // not end of combo, show in hiterrorbar, use for accuracy, increase combo, increase
            // score, ignore for health, don't add object duration to result anim
            addHitResult(result, delta, false, curvePointAt(0.0f), targetDelta, targetAngle, false, false, true, false);
        } else {
            // not end of combo, show in hiterrorbar, ignore for accuracy, increase combo,
            // don't count towards score, depending on scorev2 ignore for health or not
            m_judge->addHitResult(this, result, delta, false, false, true, false, true, true);
        }

        // add bonus score + health manually
        if(result != LiveHitResult::HIT_MISS) {
            LiveHitResult resultForHealth = LiveHitResult::HIT_SLIDER30;

            m_judge->addHitResult(this, resultForHealth, 0, false, true, true, true, true,
                                  false);  // only increase health
            m_judge->addScorePoints(30);
        } else {
            // special case: missing the startcircle drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS health)
            m_judge->addHitResult(this, LiveHitResult::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                                  false);  // only decrease health
        }
    } else {
        // endcircle

        m_startFinished = true;
        m_endFinished = true;
        m_finished = true;

        if(!isEndResultFromStrictTrackingMod) {
            // special case: osu!lazer 2020 only returns 1 judgement for the whole slider, but via the startcircle. i.e.
            // we are not allowed to drain again here in mcosu logic (because startcircle judgement is handled at the
            // end here)
            // XXX: remove this
            const bool isLazer2020Drain = false;

            addHitResult(result, delta, m_endOfCombo, getRawPosAt(getEndTime()), -1.0f, 0.0f, true, !m_heldTillEnd,
                         isLazer2020Drain);  // end of combo, ignore in hiterrorbar, depending on heldTillEnd increase
                                             // combo or not, increase score, increase health depending on drain type

            // add bonus score + extra health manually
            if(m_heldTillEnd) {
                m_judge->addHitResult(this, LiveHitResult::HIT_SLIDER30, 0, false, true, true, true, true,
                                      false);  // only increase health
                m_judge->addScorePoints(30);
            } else {
                // special case: missing the endcircle drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS health)
                // NOTE: yes, this will drain twice for the end of a slider (once for the judgement of the whole slider
                // above, and once for the endcircle here)
                m_judge->addHitResult(this, LiveHitResult::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                                      false);  // only decrease health
            }
        }
    }

    m_curRepeatCounterForHitSounds++;
}

void Slider::onRepeatHit(const SLIDERCLICK &click) {
    if(m_ctrlPoints.size() == 0) return;

    // repeat hit of a slider adds +30 points, if successful

    // sound and hit animation
    if(!click.successful) {
        onSliderBreak();
    } else {
        // NOTE: iCurRepeatCounterForHitSounds starts at 1
        m_judge->playHitSound(this->getRepeatSamples(m_curRepeatCounterForHitSounds), m_curPointRaw, 0, click.timeMS);

        if(m_view != nullptr) {
            f32 animation_multiplier = m_view->getSpeedAdjustedAnimationSpeed();
            f32 tick_pulse_time = cv::slider_followcircle_tick_pulse_time.getFloat() * animation_multiplier;

            m_followCircleTickAnimationScale = 0.0f;
            m_followCircleTickAnimationScale.set(1.0f, tick_pulse_time, anim::Linear);

            const f32 fadeoutTimeSecs = GameRules::getFadeOutTime(m_view->getBaseAnimationSpeed());

            if(click.sliderend) {
                addHitAnim(HitAnim::TAIL, fadeoutTimeSecs);
            } else {
                addHitAnim(HitAnim::HEAD, fadeoutTimeSecs);
            }
        }
    }

    // add score
    if(!click.successful) {
        // add health manually
        // special case: missing a repeat drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS health)
        m_judge->addHitResult(this, LiveHitResult::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                              false);  // only decrease health
    } else {
        m_judge->addHitResult(this, LiveHitResult::HIT_SLIDER30, 0, false, true, true, false, true,
                              false);  // not end of combo, ignore in hiterrorbar, ignore for accuracy, increase
                                       // combo, don't count towards score, increase health

        // add bonus score manually
        m_judge->addScorePoints(30);
    }

    m_curRepeatCounterForHitSounds++;
}

const DatabaseBeatmapTypes::HITSAMPLE_BITS &Slider::getRepeatSamples(i32 repeat) const {
    // Try to play a repeat sample based on what the mapper gave us
    // (the map loader gives every slider at least a start and an end sample)
    const uSz nb_edge_samples = m_edgeSamples.size();
    assert(nb_edge_samples >= 2);
    if(std::cmp_less(repeat + 1, nb_edge_samples)) return m_edgeSamples[repeat];
    // We have more repeats than edge samples!
    // Just play whatever we can (either the last repeat sample, or the start sample)
    return m_edgeSamples[nb_edge_samples - 2];
}

void Slider::onTickHit(const SLIDERCLICK &click) {
    if(m_ctrlPoints.size() == 0) return;

    // tick hit of a slider adds +10 points, if successful

    // tick drawing visibility
    i32 numMissingTickClicks = 0;
    for(const auto &c : m_clicks) {
        if(c.type == 1 && c.tickIndex == click.tickIndex && !c.finished) {
            numMissingTickClicks++;
        }
    }
    if(numMissingTickClicks == 0) {
        m_ticks[click.tickIndex].finished = true;
    }

    // sound and hit animation
    if(!click.successful) {
        onSliderBreak();
    } else {
        m_judge->playSliderTickSound(m_hitSamples, m_curPointRaw, click.timeMS);

        if(m_view != nullptr) {
            f32 animation_multiplier = m_view->getSpeedAdjustedAnimationSpeed();
            f32 tick_pulse_time = cv::slider_followcircle_tick_pulse_time.getFloat() * animation_multiplier;

            m_followCircleTickAnimationScale = 0.0f;
            m_followCircleTickAnimationScale.set(1.0f, tick_pulse_time, anim::Linear);
        }
    }

    // add score
    if(!click.successful) {
        // add health manually
        // special case: missing a tick drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS health)
        m_judge->addHitResult(this, LiveHitResult::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                              false);  // only decrease health
    } else {
        m_judge->addHitResult(this, LiveHitResult::HIT_SLIDER10, 0, false, true, true, false, true,
                              false);  // not end of combo, ignore in hiterrorbar, ignore for accuracy, increase
                                       // combo, don't count towards score, increase health

        // add bonus score manually
        m_judge->addScorePoints(10);
    }
}

void Slider::onSliderBreak() { m_judge->addSliderBreak(); }

void Slider::onReset(i32 curPosMS) {
    HitObject::onReset(curPosMS);

    if(m_judge != nullptr) {
        // debugLog("stopping due to onReset");
        m_judge->stopSliderSounds(m_lastSliderSampleSets);
    }
    if(m_view != nullptr) {
        m_followCircleTickAnimationScale.stop();
        m_endSliderBodyFadeAnimation.stop();
    }
    m_clickAnimations.clear();

    m_lastSliderSampleSets.clear();
    m_strictTrackingModLastClickHeldTime = 0;
    m_ignoredKeys = 0;
    m_cursorLeft = true;
    m_heldTillEnd = false;
    m_heldTillEndForLenienceHack = false;
    m_heldTillEndForLenienceHackCheck = false;
    m_startResult = LiveHitResult::HIT_NULL;
    m_endResult = LiveHitResult::HIT_NULL;

    m_curRepeatCounterForHitSounds = 0;

    if(m_clickTimeMS > curPosMS) {
        m_startFinished = false;
        m_endFinished = false;
        m_finished = false;
        m_endSliderBodyFadeAnimation = 0.0f;
    } else if(curPosMS < getEndTime()) {
        m_startFinished = true;
        m_endFinished = false;
        m_finished = false;
        m_endSliderBodyFadeAnimation = 0.0f;
    } else {
        m_startFinished = true;
        m_endFinished = true;
        m_finished = true;
        m_endSliderBodyFadeAnimation = 1.0f;
    }

    for(auto &click : m_clicks) {
        if(curPosMS > click.timeMS) {
            click.finished = true;
            click.successful = true;
        } else {
            click.finished = false;
            click.successful = false;
        }
    }

    for(i32 i = 0; i < m_ticks.size(); i++) {
        i32 numMissingTickClicks = 0;
        for(const auto &click : m_clicks) {
            if(click.type == 1 && click.tickIndex == i && !click.finished) {
                numMissingTickClicks++;
            }
        }
        m_ticks[i].finished = numMissingTickClicks == 0;
    }
}

Slider::HitAnim &Slider::addHitAnim(u8 typeFlags, f32 duration) {
    // percent = 0.001f: quickfix for 1 frame missing images
    // sanity check, avoid bogus maps with insanely fast buzzsliders overloading animationhandler
    if(m_clickAnimations.size() >= 128) {
        // just overwrite a random one, no one would notice anyways with this many on screen at once
        auto &ret = m_clickAnimations[prand() % 128];
        ret.percent = 0.001f;
        ret.type = decltype(ret.type)(typeFlags);
        ret.percent.set(1.0f, duration, anim::QuadOut);
        return ret;
    } else {
        auto &ret = m_clickAnimations.emplace_back(HitAnim{.percent{0.001f}, .type{typeFlags}});
        ret.percent.set(1.0f, duration, anim::QuadOut);
        return ret;
    }
}

void Slider::rebuildVertexBuffer() {
    // base mesh (background) (raw unscaled, size in raw osu coordinates centered at (0, 0, 0))
    // this mesh needs to be scaled and translated appropriately since we are not 1:1 with the playfield
    const auto rawPoints = m_curve.getPoints();
    std::vector<vec2> osuCoordPoints{rawPoints.begin(), rawPoints.end()};
    for(auto &p : osuCoordPoints) p = m_view->osuCoords2LegacyPixels(p - m_stackOffset);
    m_mesh = SliderRenderer::generateMesh(m_view->getScreenSize(), osuCoordPoints, m_view->getRawHitcircleDiameter(),
                                          /*skipOOBPoints=*/true);
}

Slider::~Slider() { onReset(0); }

void Slider::updateTracking() {
    m_tracking = m_cursorInside &&
                 (isClickHeldSlider() || flags::any<ModFlags::Autoplay | ModFlags::Relax>(m_judge->getMods().flags));
}

bool Slider::isClickHeldSlider() const {
    // osu! has a weird slider quirk, that I'll explain in detail here.
    // When holding K1 before the slider, tapping K2 on slider head, and releasing K2 later,
    // the slider is no longer considered being "held" until K2 is pressed again, or K1 is released and pressed again.

    // The reason this exists is to prevent people from holding K1 the whole map and tapping with K2.
    // Holding is part of the rhythm flow, and this is a rhythm game right?

    // Note that the restriction only applies to the slider head.
    // Any key pressed *after* entering the slider counts as a hold.

    u8 held_gameplay_keys = m_judge->getKeys() & ~LegacyReplay::Smoke;
    return (held_gameplay_keys & ~m_ignoredKeys);
}

static CONSTINIT VertexArrayObject spinnerMetreVAO{DrawPrimitive::QUADS};

Spinner::Spinner(vec2 pos, i32 timeMS, DatabaseBeatmapTypes::HITSAMPLE_BITS samples, bool isEndOfCombo, i32 endTimeMS,
                 AbstractBeatmapInterface *judge, const PlayfieldView *view)
    : HitObject(timeMS, samples, -1, isEndOfCombo, -1, -1, judge, view), m_rawPos(pos), m_originalRawPos(m_rawPos) {
    m_type = HitObjectType::SPINNER;
    m_durationMS = endTimeMS - timeMS;

    constexpr i32 minVel = 12;
    constexpr i32 maxVel = 48;
    constexpr i32 minTimeMS = 2000;
    constexpr i32 maxTimeMS = 5000;
    m_maxStoredDeltaAngles = std::clamp<i32>(
        (i32)((endTimeMS - timeMS - minTimeMS) * (maxVel - minVel) / (maxTimeMS - minTimeMS) + minVel), minVel, maxVel);
    m_storedDeltaAngles = std::make_unique<f32[]>(m_maxStoredDeltaAngles);

    // spinners don't need misaims
    m_misAim = true;

    // spinners don't use AR-dependent fadein, instead they always fade in with hardcoded 400 ms (see
    // GameRules::getFadeInTime())
    m_useFadeInTimeAsApproachTime = !cv::spinner_use_ar_fadein.getBool();
}

Spinner::~Spinner() { onReset(0); }

void Spinner::draw() {
    HitObject::draw();
    const f32 fadeOutMultiplier = cv::spinner_fade_out_time_multiplier.getFloat();
    const i32 fadeOutTimeMS =
        (i32)(GameRules::getFadeOutTime(m_view->getBaseAnimationSpeed()) * 1000.0f * fadeOutMultiplier);
    const i32 deltaEnd = m_deltaMS + m_durationMS;

    const Skin *skin = m_view->getSkin();
    const vec2 center = m_view->osuCoords2Pixels(m_rawPos);

    // osu!stable lays the spinner out in its 640x480 window space (with the spinner centered at (320, 248), which is
    // the playfield center), and draws 1x sprites at 0.625x of that space, see
    // https://osu.ppy.sh/wiki/en/Skinning/osu%21#spinner
    const f32 windowScale = m_view->getPlayfieldSize().y / (f32)GameRules::OSU_COORD_HEIGHT;
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
    if(m_hitSuccess && deltaEnd <= 0 && skin->version < 2.0f && skin->i_spinner_osu != MISSING_TEXTURE) {
        const f32 t = (f32)-deltaEnd / 1000.f;
        const f32 osuAlpha = std::clamp<f32>(t / cv::hitresult_fadein_duration.getFloat(), 0.f, 1.f) *
                             (1.f - std::clamp<f32>((t - cv::hitresult_fadeout_start_time.getFloat()) /
                                                        cv::hitresult_fadeout_duration.getFloat(),
                                                    0.f, 1.f));
        if(osuAlpha > 0.f) {
            g->setColor(Color(0xffffffff).setA(osuAlpha));
            drawSprite(skin->i_spinner_osu, windowPos(0.f, -68.f), spinnerScale / skin->i_spinner_osu.scale());
        }
    }

    if((m_finished || !m_visible) && (deltaEnd > 0 || (deltaEnd < -fadeOutTimeMS))) return;

    const i32 curPosMS = m_clickTimeMS - m_deltaMS;
    const i32 endTimeMS = getEndTime();

    // only used for fade out anim atm
    const f32 alphaMultiplier =
        std::clamp<f32>((deltaEnd < 0 ? 1.f - ((f32)std::abs(deltaEnd) / (f32)fadeOutTimeMS) : 1.f), 0.f, 1.f);
    const f32 alpha = m_alphaWithoutHidden * alphaMultiplier;
    const auto easeOut = [](f32 t) { return 1.f - (1.f - t) * (1.f - t); };

    // the spinner grows until reaching 100% during spinning, depending on how many spins are left
    const f32 clampedRatio = std::clamp<f32>(m_ratio, 0.0f, 1.0f);
    const f32 finishScale = 0.80f + easeOut(clampedRatio) * 0.20f;

    // spun out / autopilot spinners are drawn dimmed
    const Color tint = flags::any<ModFlags::SpunOut | ModFlags::Autopilot>(m_view->getModFlags()) ? Color(0xff808080)
                                                                                                  : Color(0xffffffff);

    // "SPIN!" starts fading out on the first (half) spin, but not before 500 ms in, and "CLEAR!" can't show up before
    // that either
    const i32 spinStartMS = m_firstSpinTimeMS < 0 ? -1 : std::max(m_firstSpinTimeMS, m_clickTimeMS + 500);
    const i32 clearTimeMS = (m_completedTimeMS < 0 || spinStartMS < 0) ? -1 : std::max(m_completedTimeMS, spinStartMS);

    if(skin->o_spinner_fade_playfield) {
        // black bars above and below the spinner-background, covering the rest of stable's 4:3 window
        g->setColor(Color(0xff000000).setA(alpha));
        g->fillRect(windowPos(-320.f, -248.f), vec2{640.f, 29.f} * windowScale);
        g->fillRect(windowPos(-320.f, 213.f), vec2{640.f, 19.f} * windowScale);
    }

    // the approach circle is only shown when the disc is actually skinned (peppy removed it from the default skin:
    // https://osu.ppy.sh/community/forums/topics/100765)
    const auto drawSpinnerApproachCircle = [&](const BasicSkinImage &disc) {
        if(flags::has<ModFlags::Hidden>(m_view->getModFlags()) || disc.isFromDefault() ||
           skin->i_spinner_approach_circle == MISSING_TEXTURE)
            return;

        // shrinks from 1.86x to 0.1x over the duration
        g->setColor(Color(tint).setA(alpha));
        drawSprite(skin->i_spinner_approach_circle, center,
                   spinnerScale / skin->i_spinner_approach_circle.scale() * std::lerp(0.1f, 1.86f, m_percent));
    };

    const bool oldStyle = skin->i_spinner_bg != MISSING_TEXTURE || skin->version < 2.0f;
    if(oldStyle) {
        if(skin->i_spinner_bg != MISSING_TEXTURE) {
            g->setColor(Color(skin->c_spinner_bg).setA(alpha));
            drawSprite(skin->i_spinner_bg, center, spinnerScale / skin->i_spinner_bg.scale());
        }

        if(skin->i_spinner_circle != MISSING_TEXTURE) {
            g->setColor(Color(tint).setA(alpha));
            drawSprite(skin->i_spinner_circle, center, spinnerScale / skin->i_spinner_circle.scale(), m_drawRot);
        }

        // the metre fills up from the bottom in 10 whole bars, the next bar blinks in randomly (more often the closer
        // it is to filling up), or is always shown with SpinnerNoBlink; it only updates while the spinner is running
        if(skin->i_spinner_metre != MISSING_TEXTURE && curPosMS >= m_clickTimeMS) {
            const bool noBlink = skin->o_spinner_no_blink || cv::avoid_flashes.getBool();
            // capped at 99 so that the top bar keeps blinking at 100%
            const i32 progress = std::min((i32)(clampedRatio * 100.f), 99);
            i32 bars = progress / 10;
            if(noBlink || m_finished || (prand() % 10) < (progress % 10)) bars++;

            if(bars > 0) {
                const f32 visible = (f32)bars / 10.f;
                const f32 width = (f32)skin->i_spinner_metre.getWidth();
                const f32 height = (f32)skin->i_spinner_metre.getHeight();
                const f32 metreScale = spinnerScale / skin->i_spinner_metre.scale();
                // anchored to the top left corner of stable's 4:3 window, not to the spinner itself
                const vec2 topLeft = windowPos(-320.f, -219.f);

                g->setColor(Color(tint).setA(alpha));
                g->pushTransform();
                {
                    g->scale(metreScale, metreScale);
                    g->translate(topLeft.x, topLeft.y);

                    spinnerMetreVAO.clear();
                    spinnerMetreVAO.addVertex(0.f, height * (1.f - visible));
                    spinnerMetreVAO.addTexcoord(0.f, 1.f - visible);
                    spinnerMetreVAO.addVertex(0.f, height);
                    spinnerMetreVAO.addTexcoord(0.f, 1.f);
                    spinnerMetreVAO.addVertex(width, height);
                    spinnerMetreVAO.addTexcoord(1.f, 1.f);
                    spinnerMetreVAO.addVertex(width, height * (1.f - visible));
                    spinnerMetreVAO.addTexcoord(1.f, 1.f - visible);

                    skin->i_spinner_metre.bind();
                    g->drawVAO(&spinnerMetreVAO);
                    skin->i_spinner_metre.unbind();
                }
                g->popTransform();
            }
        }

        drawSpinnerApproachCircle(skin->i_spinner_circle);
    } else {  // new style
        const f32 topRotation = m_drawRot * (skin->i_spinner_middle2 != MISSING_TEXTURE ? 0.5f : 1.f);

        if(skin->i_spinner_glow != MISSING_TEXTURE) {
            // additive cyan glow which fills in with progress, and flashes white on bonus spins
            const f32 flash =
                m_bonusTimeMS < 0 ? 1.f : std::clamp<f32>((f32)(curPosMS - m_bonusTimeMS) / 200.f, 0.f, 1.f);
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
                       m_drawRot);
        }

        drawSpinnerApproachCircle(skin->i_spinner_top);

        if(skin->i_spinner_middle != MISSING_TEXTURE) {
            // does not rotate, tints red as the time runs out
            g->setColor(argb(alpha, 1.f, m_percent, m_percent));
            drawSprite(skin->i_spinner_middle, center, spinnerScale / skin->i_spinner_middle.scale() * finishScale);
        }
    }

    // "CLEAR!"
    if(clearTimeMS >= 0 && curPosMS >= clearTimeMS && skin->i_spinner_clear != MISSING_TEXTURE) {
        // fades in with a bounce (cut short if the spinner ends sooner), and out over the last 50 ms
        const f32 t = (f32)(curPosMS - clearTimeMS);
        const f32 fadeInMS = (f32)std::clamp(endTimeMS - clearTimeMS, 1, 400);
        const f32 bounceMS = (f32)std::clamp(endTimeMS - clearTimeMS, 1, 240);
        const f32 fadeIn = easeOut(std::clamp<f32>(t / fadeInMS, 0.f, 1.f));
        const f32 fadeOut = 1.f - std::clamp<f32>((f32)(curPosMS - (endTimeMS - 50)) / 50.f, 0.f, 1.f);
        const f32 bounce =
            t < bounceMS
                ? std::lerp(2.f, 0.8f, easeOut(t / bounceMS))
                : std::lerp(0.8f, 1.f, std::clamp<f32>((t - bounceMS) / std::max(1.f, fadeInMS - bounceMS), 0.f, 1.f));

        g->setColor(Color(0xffffffff).setA(fadeIn * fadeOut));
        drawSprite(skin->i_spinner_clear, windowPos(0.f, -104.f),
                   spinnerScale / skin->i_spinner_clear.scale() * bounce);
    }

    // "SPIN!"
    if(skin->i_spinner_spin != MISSING_TEXTURE) {
        // fades in over the second half of the fadein and out over the last 400 ms, unless the first spin fades it
        // out for good (300 ms, restarted as a 100 ms fade if the spinner gets cleared before that finishes)
        const f32 halfFadeInMS = std::max(1.f, (f32)m_fadeInTimeMS / 2.f);
        const i32 endFadeMS = std::clamp(m_durationMS, 1, 400);
        const auto idleAlpha = [&](i32 t) {
            if(t >= endTimeMS - endFadeMS)
                return 1.f - std::clamp<f32>((f32)(t - (endTimeMS - endFadeMS)) / (f32)endFadeMS, 0.f, 1.f);
            return std::clamp<f32>((f32)(t - m_clickTimeMS) / halfFadeInMS + 1.f, 0.f, 1.f);
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
            spinAlpha = fadeFrom * (1.f - std::clamp<f32>((f32)(curPosMS - fadeStartMS) / fadeMS, 0.f, 1.f));
        }

        if(spinAlpha > 0.f) {
            g->setColor(Color(0xffffffff).setA(spinAlpha));
            drawSprite(skin->i_spinner_spin, windowPos(0.f, 116.f), spinnerScale / skin->i_spinner_spin.scale());
        }
    }

    // bonus score counter
    if(m_bonusSpins > 0 && curPosMS >= m_bonusTimeMS) {
        const f32 t = std::clamp<f32>((f32)(curPosMS - m_bonusTimeMS) / 800.f, 0.f, 1.f);
        if(t < 1.f) {
            const f32 digitScale = spinnerScale * std::lerp(2.f, 1.28f, easeOut(t)) / skin->i_scores[0].scale();
            const vec2 pos = windowPos(0.f, 80.f);

            g->setColor(Color(tint).setA((1.f - easeOut(t)) * alphaMultiplier));
            g->pushTransform();
            {
                g->scale(digitScale, digitScale);
                g->translate(pos.x, pos.y);
                HUD::drawNumberWithSkinDigits({.number = (u64)m_bonusSpins * 1000,
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
        const f32 slide = easeOut(std::clamp<f32>(
            (f32)(curPosMS - (m_clickTimeMS - m_fadeInTimeMS)) / (f32)std::max(1, m_fadeInTimeMS), 0.f, 1.f));
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
            HUD::drawNumberWithSkinDigits({.number = (u64)std::lround(m_RPM),
                                           .scale = digitScale,
                                           .combo = false,
                                           .anchor = AnchorPoint::RIGHT,
                                           .skin = skin});
        }
        g->popTransform();
    } else if(m_deltaMS < 0 && cv::skin_always_draw_spinner_rpm.getBool()) {
        McFont *rpmFont = engine->getDefaultFont();
        const f32 stringWidth = rpmFont->getStringWidth("RPM: 477");
        g->setColor(Color(0xffffffff)
                        .setA(m_alphaWithoutHidden * m_alphaWithoutHidden * m_alphaWithoutHidden * alphaMultiplier));

        g->pushTransform();
        {
            const vec2 screen = m_view->getScreenSize();
            g->translate((i32)((i32)screen.x / 2 - stringWidth / 2),
                         (i32)((i32)screen.y - 5 + (5 + rpmFont->getHeight()) * (1.0f - m_alphaWithoutHidden)));
            g->drawString(rpmFont, fmt::format("RPM: {}", (i32)(m_RPM + 0.4f)));
        }
        g->popTransform();
    }
}

void Spinner::update(i32 curPosMS, f64 frameTimeSecs) {
    HitObject::update(curPosMS, frameTimeSecs);

    // stop spinner sound and don't update() while paused
    if(m_judge->isPaused() || !m_judge->isPlaying() || m_judge->hasFailed()) {
        m_judge->stopSpinnerSpinSound();
        return;
    }

    // if we have not been clicked yet, check if we are in the timeframe of a miss, also handle auto and relax
    if(!m_finished) {
        // handle spinner ending
        if(curPosMS >= getEndTime()) {
            onHit();
            return;
        }

        // Skip calculations
        if(frameTimeSecs == 0.0) {
            return;
        }

        m_rotationsNeeded = GameRules::getSpinnerRotationsForSpeedMultiplier(m_judge, m_durationMS);

        const f32 DELTA_UPDATE_TIME_MS = (frameTimeSecs * 1000.0f);
        const f32 AUTO_MULTIPLIER = (1.0f / 20.0f);

        // scale percent calculation
        i32 deltaMS = m_clickTimeMS - (i32)curPosMS;
        m_percent = this->getTimeLeftPercent(curPosMS);

        // handle auto, mouse spinning movement
        f32 angleDiff = 0;
        if(flags::any<ModFlags::Autoplay | ModFlags::Autopilot | ModFlags::SpunOut>(m_judge->getMods().flags)) {
            angleDiff = frameTimeSecs * 1000.0f * AUTO_MULTIPLIER * m_judge->getSpeedMultiplier();
        } else {  // user spin
            vec2 mouseDelta = m_judge->getCursorPos() - m_judge->osuCoords2Pixels(m_rawPos);
            const auto currentMouseAngle = (f32)std::atan2(mouseDelta.y, mouseDelta.x);
            angleDiff = (currentMouseAngle - m_lastMouseAngle);

            if(std::abs(angleDiff) > 0.001f)
                m_lastMouseAngle = currentMouseAngle;
            else
                angleDiff = 0;
        }

        // handle spinning
        // HACKHACK: rewrite this
        if(deltaMS <= 0) {
            bool isSpinning =
                m_judge->isClickHeld() ||
                flags::any<ModFlags::Autoplay | ModFlags::Relax | ModFlags::SpunOut>(m_judge->getMods().flags);

            m_deltaOverflowMS += frameTimeSecs * 1000.0f;

            if(angleDiff < -PI_F)
                angleDiff += 2 * PI_F;
            else if(angleDiff > PI_F)
                angleDiff -= 2 * PI_F;

            if(isSpinning) m_deltaAngleOverflow += angleDiff;

            while(m_deltaOverflowMS >= DELTA_UPDATE_TIME_MS) {
                // spin caused by the cursor
                f32 deltaAngle = 0;
                if(isSpinning) {
                    deltaAngle = m_deltaAngleOverflow * DELTA_UPDATE_TIME_MS / m_deltaOverflowMS;
                    m_deltaAngleOverflow -= deltaAngle;
                    // deltaAngle = std::clamp<f32>(deltaAngle, -MAX_ANG_DIFF, MAX_ANG_DIFF);
                }

                m_deltaOverflowMS -= DELTA_UPDATE_TIME_MS;

                m_sumDeltaAngle -= m_storedDeltaAngles[m_deltaAngleIndex];
                m_sumDeltaAngle += deltaAngle;
                m_storedDeltaAngles[m_deltaAngleIndex++] = deltaAngle;
                m_deltaAngleIndex %= m_maxStoredDeltaAngles;

                f32 rotationAngle = m_sumDeltaAngle / m_maxStoredDeltaAngles;
                f32 rotationPerSec = rotationAngle * (1000.0f / DELTA_UPDATE_TIME_MS) / (2.0f * PI_F);

                f32 decay = std::pow(0.01f, (f32)frameTimeSecs);
                m_RPM = m_RPM * decay + (1.0 - decay) * std::abs(rotationPerSec) * 60;
                m_RPM = std::min(m_RPM, 477.0f);

                if(std::abs(rotationAngle) > 0.0001f) rotate(rotationAngle);
            }

            m_ratio = m_rotations / (m_rotationsNeeded * 360.0f);
            if(m_completedTimeMS < 0 && m_ratio >= 1.0f) m_completedTimeMS = curPosMS;
        }
    }
}

void Spinner::pose(i32 timeMS, i32 /*fadeOutMS*/, i32 /*trailMS*/) {
    this->updateLook(timeMS, m_view->getModFlags(), m_view->getApproachTime(),
                     m_view->getSpeedAdjustedAnimationSpeed());

    m_finished = timeMS >= this->getEndTime();
    m_percent = this->getTimeLeftPercent(std::min(timeMS, this->getEndTime()));

    // at rest, without a result
    m_drawRot = 0.0f;
    m_rotations = 0.0f;
    m_ratio = 0.0f;
    m_RPM = 0.0f;
    m_completedTimeMS = -1;
    m_firstSpinTimeMS = -1;
    m_bonusTimeMS = -1;
    m_bonusSpins = 0;
    m_hitSuccess = false;
}

void Spinner::addSoundCues(std::vector<SoundCue> &out) const {
    const i32 endTimeMS = this->getEndTime();
    out.push_back({.timeMS = endTimeMS,
                   .endTimeMS = endTimeMS,
                   .kind = SoundCue::Kind::HIT,
                   .samples = m_hitSamples,
                   .rawPos = m_rawPos});
}

f32 Spinner::getTimeLeftPercent(i32 curPosMS) const {
    const i32 deltaMS = m_clickTimeMS - curPosMS;
    return 1.0f - std::clamp<f32>((f32)deltaMS / -(f32)(m_durationMS), 0.0f, 1.0f);
}

void Spinner::onReset(i32 curPosMS) {
    HitObject::onReset(curPosMS);

    if(m_judge != nullptr) m_judge->stopSpinnerSpinSound();

    m_RPM = 0.0f;
    m_drawRot = 0.0f;
    m_rotations = 0.0f;
    m_deltaOverflowMS = 0.0f;
    m_sumDeltaAngle = 0.0f;
    m_deltaAngleIndex = 0;
    m_deltaAngleOverflow = 0.0f;
    m_ratio = 0.0f;
    m_completedTimeMS = -1;
    m_firstSpinTimeMS = -1;
    m_bonusTimeMS = -1;
    m_bonusSpins = 0;
    m_hitSuccess = false;

    // spinners don't need misaims
    m_misAim = true;

    for(i32 i = 0; i < m_maxStoredDeltaAngles; i++) {
        m_storedDeltaAngles[i] = 0.0f;
    }

    if(curPosMS > getEndTime())
        m_finished = true;
    else
        m_finished = false;
}

void Spinner::onHit() {
    // calculate hit result
    LiveHitResult result = LiveHitResult::HIT_NULL;
    if(m_ratio >= 1.0f || (flags::has<ModFlags::Autoplay>(m_judge->getMods().flags)))
        result = LiveHitResult::HIT_300;
    else if(m_ratio >= 0.9f && !cv::mod_ming3012.getBool() && !cv::mod_no100s.getBool())
        result = LiveHitResult::HIT_100;
    else if(m_ratio >= 0.75f && !cv::mod_no100s.getBool() && !cv::mod_no50s.getBool())
        result = LiveHitResult::HIT_50;
    else
        result = LiveHitResult::HIT_MISS;

    m_hitSuccess = result != LiveHitResult::HIT_MISS;

    // sound
    if(result != LiveHitResult::HIT_MISS) m_judge->playHitSound(m_hitSamples, m_rawPos, 0);

    // add it, and we are finished
    addHitResult(result, 0, m_endOfCombo, m_rawPos, -1.0f, 0.f, /*ignoreOnHitErrorBar=*/true);
    m_finished = true;

    m_judge->stopSpinnerSpinSound();
}

void Spinner::rotate(f32 rad) {
    m_drawRot += vec::degrees(rad);

    rad = std::abs(rad);
    const f32 newRotations = m_rotations + vec::degrees(rad);

    // stable counts spins in half rotations, the first one is what fades out "SPIN!"
    if(m_firstSpinTimeMS < 0 && newRotations >= 180.0f) m_firstSpinTimeMS = m_clickTimeMS - m_deltaMS;

    // added one whole rotation
    if(std::floor(newRotations / 360.0f) > m_rotations / 360.0f) {
        if((i32)(newRotations / 360.0f) > (i32)(m_rotationsNeeded) + 1) {
            // extra rotations and bonus sound
            m_bonusSpins++;
            m_bonusTimeMS = m_clickTimeMS - m_deltaMS;
            m_judge->playSpinnerBonusSound();
            m_judge->addHitResult(this, LiveHitResult::HIT_SPINNERBONUS, 0, false, true, true, true, true,
                                  false);  // only increase health
            m_judge->addHitResult(this, LiveHitResult::HIT_SPINNERBONUS, 0, false, true, true, true, true,
                                  false);  // HACKHACK: compensating for rotation logic differences
            m_judge->addScorePoints(1100, true);
        } else {
            // normal whole rotation
            m_judge->addHitResult(this, LiveHitResult::HIT_SPINNERSPIN, 0, false, true, true, true, true,
                                  false);  // only increase health
            m_judge->addHitResult(this, LiveHitResult::HIT_SPINNERSPIN, 0, false, true, true, true, true,
                                  false);  // HACKHACK: compensating for rotation logic differences
            m_judge->addScorePoints(100, true);
        }
    }

    // spinner sound
    m_judge->playSpinnerSpinSound(m_ratio);

    m_rotations = newRotations;
}

vec2 Spinner::getAutoCursorPos(i32 curPosMS) const {
    // calculate point
    i32 deltaMS = 0;
    if(curPosMS <= m_clickTimeMS)
        deltaMS = 0;
    else if(curPosMS >= getEndTime())
        deltaMS = m_durationMS;
    else
        deltaMS = curPosMS - m_clickTimeMS;

    vec2 actualPos = m_judge->osuCoords2Pixels(m_rawPos);
    const f32 AUTO_MULTIPLIER = (1.0f / 20.0f);
    f32 multiplier =
        flags::any<ModFlags::Autoplay | ModFlags::Autopilot>(m_judge->getMods().flags) ? AUTO_MULTIPLIER : 1.0f;
    f32 angle = (deltaMS * multiplier) - PI_F / 2.0f;
    f32 r = GameRules::getPlayfieldSize().y / 10.0f;  // XXX: slow?
    return vec2((f32)(actualPos.x + r * std::cos(angle)), (f32)(actualPos.y + r * std::sin(angle)));
}

namespace HitObjects {

std::vector<std::unique_ptr<HitObject>> create(const Primitives::PRIMITIVE_CONTAINER &primitives,
                                               AbstractBeatmapInterface *judge, const PlayfieldView *view) {
    std::vector<std::unique_ptr<HitObject>> objects;
    objects.reserve(primitives.objectsByTime.size());
    for(const auto [kind, index] : primitives.objectsByTime) {
        switch(kind) {
            case Primitives::ObjectRef::Kind::CIRCLE: {
                const auto &h = primitives.hitcircles[index];
                objects.emplace_back(new Circle(vec2{h.x, h.y}, h.time, h.samples, h.number, h.isEndOfCombo,
                                                h.colorCounter, h.colorOffset, judge, view));
                break;
            }
            case Primitives::ObjectRef::Kind::SLIDER: {
                const auto &s = primitives.sliders[index];
                objects.emplace_back(new Slider(s.type, s.repeat, s.pixelLength, s.points, s.ticks, s.sliderTime,
                                                s.sliderTimeWithoutRepeats, s.time, s.hoverSamples, s.edgeSamples,
                                                s.number, s.isEndOfCombo, s.colorCounter, s.colorOffset, judge, view));
                break;
            }
            case Primitives::ObjectRef::Kind::SPINNER: {
                const auto &s = primitives.spinners[index];
                objects.emplace_back(
                    new Spinner(vec2{s.x, s.y}, s.time, s.samples, s.isEndOfCombo, s.endTime, judge, view));
                break;
            }
        }
    }

    i32 comboStartTime = objects.empty() ? 0 : objects[0]->getClickTime();
    for(uSz i = 0; i < objects.size(); i++) {
        objects[i]->setComboStartTime(comboStartTime);
        if(objects[i]->isEndOfCombo() && i + 1 < objects.size()) comboStartTime = objects[i + 1]->getClickTime();
    }

    return objects;
}

void stack(std::span<const std::unique_ptr<HitObject>> objects, f32 AR, i32 beatmapVersion, f32 stackLeniency,
           f32 rawHitcircleDiameter, bool hardRock) {
    // reset
    for(const auto &hitobject : objects) {
        hitobject->setStack(0);
    }

    Primitives::calculateStacks(
        Primitives::ObjectGetter<HitObject>{[objects](uSz idx) -> HitObject * { return objects[idx].get(); }},
        objects.size(), AR, beatmapVersion, stackLeniency);

    // update hitobject positions
    const f32 STACK_OFFSET = 0.05f;
    const f32 stackOffset = rawHitcircleDiameter * STACK_OFFSET;
    for(const auto &hitobject : objects) {
        if(hitobject->getStack() != 0) hitobject->updateStackPosition(stackOffset, hardRock);
    }
}

void drawFollowPoints(const PlayfieldView &view, std::span<const std::unique_ptr<HitObject>> objects, uSz firstIndex) {
    const Skin *skin = view.getSkin();

    const i32 curPos = view.getCurMusicPosWithOffsets();

    // I absolutely hate this, followpoints can be abused for cheesing high AR reading since they always fade in with a
    // fixed 800 ms custom approach time. Capping it at the current approach rate seems sensible, but unfortunately
    // that's not what osu is doing. It was non-osu-compliant-clamped since this client existed, but let's see how many
    // people notice a change after all this time (26.02.2020)

    // 0.7x means animation lasts only 0.7 of it's time
    const f64 animationMultiplier = view.getSpeedAdjustedAnimationSpeed();
    const i32 followPointApproachTime =
        animationMultiplier *
        (cv::followpoints_clamp.getBool()
             ? std::min((i32)view.getApproachTime(), (i32)cv::followpoints_approachtime.getFloat())
             : (i32)cv::followpoints_approachtime.getFloat());
    const bool followPointsConnectCombos = cv::followpoints_connect_combos.getBool();
    const bool followPointsConnectSpinners = cv::followpoints_connect_spinners.getBool();
    const f32 followPointSeparationMultiplier = std::max(cv::followpoints_separation_multiplier.getFloat(), 0.1f);
    const f32 followPointPrevFadeTime = animationMultiplier * cv::followpoints_prevfadetime.getFloat();
    const f32 followPointScaleMultiplier = cv::followpoints_scale_multiplier.getFloat();
    const int screenWidth = (int)view.getScreenSize().x;
    const int screenHeight = (int)view.getScreenSize().y;

    // include previous object in followpoints
    int lastObjectIndex = -1;

    for(int index = (int)firstIndex; index < objects.size(); index++) {
        lastObjectIndex = index - 1;

        // ignore future spinners
        auto *spinnerPointer = objects[index] && objects[index]->getType() == HitObjectType::SPINNER
                                   ? static_cast<Spinner *>(objects[index].get())
                                   : nullptr;
        if(spinnerPointer != nullptr && !followPointsConnectSpinners)  // if this is a spinner
        {
            lastObjectIndex = -1;
            continue;
        }

        const bool isCurrentHitObjectNewCombo =
            (lastObjectIndex >= 0 ? objects[lastObjectIndex]->isEndOfCombo() : false);
        const bool isCurrentHitObjectSpinner =
            (lastObjectIndex >= 0 && followPointsConnectSpinners
                 ? objects[lastObjectIndex] && objects[lastObjectIndex]->getType() == HitObjectType::SPINNER
                 : false);
        if(lastObjectIndex >= 0 && (!isCurrentHitObjectNewCombo || followPointsConnectCombos ||
                                    (isCurrentHitObjectSpinner && followPointsConnectSpinners))) {
            // ignore previous spinners
            spinnerPointer = objects[lastObjectIndex] && objects[lastObjectIndex]->getType() == HitObjectType::SPINNER
                                 ? static_cast<Spinner *>(objects[lastObjectIndex].get())
                                 : nullptr;
            if(spinnerPointer != nullptr && !followPointsConnectSpinners)  // if this is a spinner
            {
                lastObjectIndex = -1;
                continue;
            }

            // get time & pos of the last and current object
            const i32 lastObjectEndTime =
                objects[lastObjectIndex]->getClickTime() + objects[lastObjectIndex]->getDuration() + 1;
            const i32 objectStartTime = objects[index]->getClickTime();
            const i32 timeDiff = objectStartTime - lastObjectEndTime;

            const vec2 startPointRaw = objects[lastObjectIndex]->getRawPosAt(lastObjectEndTime);
            const vec2 endPointRaw = objects[index]->getRawPosAt(objectStartTime);
            const vec2 startPoint = view.osuCoords2Pixels(startPointRaw);
            const vec2 endPoint = view.osuCoords2Pixels(endPointRaw);

            const f32 xDiff = endPoint.x - startPoint.x;
            const f32 yDiff = endPoint.y - startPoint.y;
            const vec2 diff = endPoint - startPoint;

            // NOTE: dist and separation are in osu!pixels, so that followpoint placement is independent of how the
            // playfield is scaled to the screen (only the final positions are mapped to screen space)
            const f32 dist = vec::length(endPointRaw - startPointRaw);

            // draw all points between the two objects
            const int followPointSeparation = 32.0f * followPointSeparationMultiplier;
            for(int j = (int)(followPointSeparation * 1.5f); j < (dist - followPointSeparation);
                j += followPointSeparation) {
                const f32 animRatio = ((f32)j / dist);

                const vec2 animPosStart = startPoint + (animRatio - 0.1f) * diff;
                const vec2 finalPos = startPoint + animRatio * diff;

                const i32 fadeInTime = (i32)(lastObjectEndTime + animRatio * timeDiff) - followPointApproachTime;
                const i32 fadeOutTime = (i32)(lastObjectEndTime + animRatio * timeDiff);

                // draw
                f32 alpha = 1.0f;
                f32 followAnimPercent =
                    std::clamp<f32>((f32)(curPos - fadeInTime) / (f32)followPointPrevFadeTime, 0.0f, 1.0f);
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
                    const f32 delta = curPos - fadeInTime;
                    alpha = (f32)delta / (f32)followPointApproachTime;
                } else if(curPos >= fadeOutTime && curPos < (fadeOutTime + (i32)followPointPrevFadeTime)) {
                    // previous trail
                    const i32 delta = curPos - fadeOutTime;
                    alpha = 1.0f - (f32)delta / (f32)(followPointPrevFadeTime);
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
        if(objects[index]->getClickTime() >= curPos + followPointApproachTime) break;
    }
}

void draw(std::span<HitObject *const> objects, RenderTarget *sliderBodies) {
    // in order to avoid covering circles/sliders with spinner skin elements, draw spinners first
    // and overlay circles/sliders on top
    // this logic could be embedded in the sort order itself but i'm lazy to check if that would mess anything else up
    for(HitObject *obj : objects) {
        if(obj->isSpinner()) obj->draw();
    }

    SliderRenderer::Batch bodies{sliderBodies};
    for(const HitObject *obj : objects) {
        if(obj->isSlider()) bodies.queue(*static_cast<const Slider *>(obj));
    }
    for(HitObject *obj : objects) {
        if(!obj->isSpinner()) obj->draw();
    }
}

}  // namespace HitObjects
}  // namespace neomod
