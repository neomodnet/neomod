#include "HitObjects.h"

#include <cmath>
#include <utility>

#include "AnimationHandler.h"
#include "Bancho.h"
#include "OsuConVars.h"
#include "Engine.h"
#include "GameRules.h"
#include "HUD.h"
#include "ModFPoSu.h"
#include "Osu.h"
#include "Sound.h"
#include "Font.h"
#include "VertexArrayObject.h"
#include "BeatmapInterface.h"
#include "RenderTarget.h"
#include "ResourceManager.h"
#include "Skin.h"
#include "SkinImage.h"
#include "SliderCurves.h"
#include "SliderRenderer.h"
#include "SoundEngine.h"
#include "Logging.h"
#include "UI.h"
#include "crypto.h"

using namespace flags::operators;

void HitObject::drawHitResult(BeatmapInterface *pf, vec2 rawPos, LiveScore::HIT result, float animPercentInv,
                              float hitDeltaRangePercent) {
    drawHitResult(pf->getSkin(), pf->fHitcircleDiameter, pf->fRawHitcircleDiameter, rawPos, result, animPercentInv,
                  hitDeltaRangePercent);
}

void HitObject::drawHitResult(const Skin *skin, float hitcircleDiameter, float rawHitcircleDiameter, vec2 rawPos,
                              LiveScore::HIT result, float animPercentInv, float hitDeltaRangePercent) {
    if(animPercentInv <= 0.0f) return;

    const float animPercent = 1.0f - animPercentInv;

    const float fadeInEndPercent = cv::hitresult_fadein_duration.getFloat() / cv::hitresult_duration.getFloat();

    // determine color/transparency
    {
        if(!cv::hitresult_delta_colorize.getBool() || result == LiveScore::HIT::HIT_MISS)
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
                hitDeltaRangePercent = std::clamp<float>(
                    hitDeltaRangePercent * cv::hitresult_delta_colorize_multiplier.getFloat(), -1.0f, 1.0f);

                const float rf = lerp3f(cv::hitresult_delta_colorize_early_r.getFloat() / 255.0f, 1.0f,
                                        cv::hitresult_delta_colorize_late_r.getFloat() / 255.0f,
                                        cv::hitresult_delta_colorize_interpolate.getBool()
                                            ? hitDeltaRangePercent / 2.0f + 0.5f
                                            : (hitDeltaRangePercent < 0.0f ? -1.0f : 1.0f));
                const float gf = lerp3f(cv::hitresult_delta_colorize_early_g.getFloat() / 255.0f, 1.0f,
                                        cv::hitresult_delta_colorize_late_g.getFloat() / 255.0f,
                                        cv::hitresult_delta_colorize_interpolate.getBool()
                                            ? hitDeltaRangePercent / 2.0f + 0.5f
                                            : (hitDeltaRangePercent < 0.0f ? -1.0f : 1.0f));
                const float bf = lerp3f(cv::hitresult_delta_colorize_early_b.getFloat() / 255.0f, 1.0f,
                                        cv::hitresult_delta_colorize_late_b.getFloat() / 255.0f,
                                        cv::hitresult_delta_colorize_interpolate.getBool()
                                            ? hitDeltaRangePercent / 2.0f + 0.5f
                                            : (hitDeltaRangePercent < 0.0f ? -1.0f : 1.0f));

                g->setColor(argb(1.0f, rf, gf, bf));
            }
        }

        const float fadeOutStartPercent =
            cv::hitresult_fadeout_start_time.getFloat() / cv::hitresult_duration.getFloat();
        const float fadeOutDurationPercent =
            cv::hitresult_fadeout_duration.getFloat() / cv::hitresult_duration.getFloat();

        g->setAlpha(std::clamp<float>(animPercent < fadeInEndPercent
                                          ? animPercent / fadeInEndPercent
                                          : 1.0f - ((animPercent - fadeOutStartPercent) / fadeOutDurationPercent),
                                      0.0f, 1.0f));
    }

    g->pushTransform();
    {
        const float osuCoordScaleMultiplier = hitcircleDiameter / rawHitcircleDiameter;

        bool doScaleOrRotateAnim = true;
        bool hasParticle = true;
        float hitImageScale = 1.0f;

        switch(result) {
            using enum LiveScore::HIT;
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
        float scale = 1.0f;
        if(doScaleOrRotateAnim && cv::hitresult_animated.getBool()) {
            if(!hasParticle) {
                if(animPercent < fadeInEndPercent * 0.8f)
                    scale =
                        std::lerp(0.6f, 1.1f, std::clamp<float>(animPercent / (fadeInEndPercent * 0.8f), 0.0f, 1.0f));
                else if(animPercent < fadeInEndPercent * 1.2f)
                    scale = std::lerp(1.1f, 0.9f,
                                      std::clamp<float>((animPercent - fadeInEndPercent * 0.8f) /
                                                            (fadeInEndPercent * 1.2f - fadeInEndPercent * 0.8f),
                                                        0.0f, 1.0f));
                else if(animPercent < fadeInEndPercent * 1.4f)
                    scale = std::lerp(0.9f, 1.0f,
                                      std::clamp<float>((animPercent - fadeInEndPercent * 1.2f) /
                                                            (fadeInEndPercent * 1.4f - fadeInEndPercent * 1.2f),
                                                        0.0f, 1.0f));
            } else
                scale = std::lerp(0.9f, 1.05f, std::clamp<float>(animPercent, 0.0f, 1.0f));

            // TODO: osu draws an additive copy of the hitresult on top (?) with 0.5 alpha anim and negative timing, if
            // the skin hasParticle. in this case only the copy does the wobble anim, while the main result just scales
        }

        switch(result) {
            using enum LiveScore::HIT;

            case HIT_MISS: {
                // special case: animated misses don't move down, and skins with version <= 1 also don't move down
                vec2 downAnim{0.f};
                if(skin->i_hit0.getNumImages() < 2 && skin->version > 1.0f)
                    downAnim.y = std::lerp(-5.0f, 40.0f,
                                           std::clamp<float>(animPercent * animPercent * animPercent, 0.0f, 1.0f)) *
                                 osuCoordScaleMultiplier;

                float missScale = 1.0f + std::clamp<float>((1.0f - (animPercent / fadeInEndPercent)), 0.0f, 1.0f) *
                                             (cv::hitresult_miss_fadein_scale.getFloat() - 1.0f);
                if(!cv::hitresult_animated.getBool()) missScale = 1.0f;

                // TODO: rotation anim (only for all non-animated skins), rot = rng(-0.15f, 0.15f), anim1 = 120 ms to
                // rot, anim2 = rest to rot*2, all ease in

                skin->i_hit0.drawRaw(rawPos + downAnim, (doScaleOrRotateAnim ? missScale : 1.0f) * hitImageScale *
                                                            cv::hitresult_scale.getFloat());
            } break;

            case HIT_50:
                skin->i_hit50.drawRaw(
                    rawPos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                break;

            case HIT_100:
                skin->i_hit100.drawRaw(
                    rawPos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                break;

            case HIT_300:
                if(cv::hitresult_draw_300s.getBool()) {
                    skin->i_hit300.drawRaw(
                        rawPos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                }
                break;

            case HIT_100K:
                skin->i_hit100k.drawRaw(
                    rawPos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                break;

            case HIT_300K:
                if(cv::hitresult_draw_300s.getBool()) {
                    skin->i_hit300k.drawRaw(
                        rawPos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                }
                break;

            case HIT_300G:
                if(cv::hitresult_draw_300s.getBool()) {
                    skin->i_hit300g.drawRaw(
                        rawPos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                }
                break;

            default:
                break;
        }
    }
    g->popTransform();
}

HitObject::HitObject(i32 timeMS, HitSamples samples, int comboNumber, bool isEndOfCombo, int colorCounter,
                     int colorOffset, AbstractBeatmapInterface *pi)
    : m_pi(pi),
      m_pf(dynamic_cast<BeatmapInterface *>(pi)),  // should be NULL if SimulatedBeatmapInterface
      m_clickTimeMS(timeMS),
      m_comboNumber(comboNumber),
      m_hitSamples(std::move(samples)),
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
       && (hitresultanim.timeSecs + cv::hitresult_duration_max.getFloat() * (1.0f / m_pf->getBaseAnimationSpeed())) >
              engine->getTime()) {
        const auto &skin = m_pf->getSkin();
        {
            const i32 skinAnimationTimeStartOffset =
                m_clickTimeMS + (hitresultanim.addObjectDurationToSkinAnimationTimeStartOffset ? m_durationMS : 0) +
                hitresultanim.deltaMS;

            skin->i_hit0.setAnimationTimeOffset(skinAnimationTimeStartOffset);
            skin->i_hit0.setAnimationFrameClampUp();
            skin->i_hit50.setAnimationTimeOffset(skinAnimationTimeStartOffset);
            skin->i_hit50.setAnimationFrameClampUp();
            skin->i_hit100.setAnimationTimeOffset(skinAnimationTimeStartOffset);
            skin->i_hit100.setAnimationFrameClampUp();
            skin->i_hit100k.setAnimationTimeOffset(skinAnimationTimeStartOffset);
            skin->i_hit100k.setAnimationFrameClampUp();
            skin->i_hit300.setAnimationTimeOffset(skinAnimationTimeStartOffset);
            skin->i_hit300.setAnimationFrameClampUp();
            skin->i_hit300g.setAnimationTimeOffset(skinAnimationTimeStartOffset);
            skin->i_hit300g.setAnimationFrameClampUp();
            skin->i_hit300k.setAnimationTimeOffset(skinAnimationTimeStartOffset);
            skin->i_hit300k.setAnimationFrameClampUp();

            const float animPercentInv =
                1.0f - (((engine->getTime() - hitresultanim.timeSecs) * m_pf->getBaseAnimationSpeed()) /
                        cv::hitresult_duration.getFloat());

            drawHitResult(m_pf, m_pf->osuCoords2Pixels(hitresultanim.rawPos), hitresultanim.result, animPercentInv,
                          std::clamp<float>((float)hitresultanim.deltaMS / m_pi->getHitWindow50(), -1.0f, 1.0f));
        }
    }
}

void HitObject::update(i32 curPosMS, f64 /*frame_time*/) {
    m_alphaForApproachCircle = 0.0f;
    m_hittableDimRGBColorMultiplierPct = 1.0f;

    const auto mods = m_pi->getMods();

    const double animationSpeedMultiplier = m_pi->getSpeedAdjustedAnimationSpeed();
    m_fadeInTimeMS = GameRules::getFadeInTime() * animationSpeedMultiplier;
    m_approachTimeMS = (m_useFadeInTimeAsApproachTime ? m_fadeInTimeMS : (i32)m_pi->getCachedApproachTimeForUpdate());

    m_deltaMS = m_clickTimeMS - curPosMS;

    // 1 ms fudge by using >=, shouldn't really be a problem
    if(curPosMS >= (m_clickTimeMS - m_approachTimeMS) && curPosMS < (getEndTime())) {
        // approach circle scale
        const float scale = std::clamp<float>((float)m_deltaMS / (float)m_approachTimeMS, 0.0f, 1.0f);
        m_approachScale = 1 + (scale * cv::approach_scale_multiplier.getFloat());
        if(cv::mod_approach_different.getBool()) {
            constexpr float back_const = 1.70158;

            float time = 1.0f - scale;
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
        const i32 fadeInStart = m_clickTimeMS - m_approachTimeMS;
        const i32 fadeInEnd =
            std::min(m_clickTimeMS,
                     m_clickTimeMS - m_approachTimeMS +
                         m_fadeInTimeMS);  // std::min() ensures that the fade always finishes at click_time
                                           // (even if the fadeintime is longer than the approachtime)
        m_alpha =
            std::clamp<float>(1.0f - ((float)(fadeInEnd - curPosMS) / (float)(fadeInEnd - fadeInStart)), 0.0f, 1.0f);
        m_alphaWithoutHidden = m_alpha;

        if(mods.has(ModFlags::Hidden)) {
            // hidden hitobject body fadein
            const float fin_start_percent = cv::mod_hd_circle_fadein_start_percent.getFloat();
            const float fin_end_percent = cv::mod_hd_circle_fadein_end_percent.getFloat();
            const i32 hiddenFadeInStartMS = m_clickTimeMS - (i32)(m_approachTimeMS * fin_start_percent);
            const i32 hiddenFadeInEndMS = m_clickTimeMS - (i32)(m_approachTimeMS * fin_end_percent);
            m_alpha = std::clamp<float>(
                1.0f - ((float)(hiddenFadeInEndMS - curPosMS) / (float)(hiddenFadeInEndMS - hiddenFadeInStartMS)), 0.0f,
                1.0f);

            // hidden hitobject body fadeout
            const float fout_start_percent = cv::mod_hd_circle_fadeout_start_percent.getFloat();
            const float fout_end_percent = cv::mod_hd_circle_fadeout_end_percent.getFloat();
            const i32 hiddenFadeOutStart = m_clickTimeMS - (i32)(m_approachTimeMS * fout_start_percent);
            const i32 hiddenFadeOutEnd = m_clickTimeMS - (i32)(m_approachTimeMS * fout_end_percent);
            if(curPosMS >= hiddenFadeOutStart)
                m_alpha = std::clamp<float>(
                    ((float)(hiddenFadeOutEnd - curPosMS) / (float)(hiddenFadeOutEnd - hiddenFadeOutStart)), 0.0f,
                    1.0f);
        }

        // approach circle fadein (doubled fadeintime)
        const i32 approachCircleFadeStart = m_clickTimeMS - m_approachTimeMS;
        const i32 approachCircleFadeEnd =
            std::min(m_clickTimeMS,
                     m_clickTimeMS - m_approachTimeMS +
                         2 * m_fadeInTimeMS);  // std::min() ensures that the fade always finishes at click_time
                                               // (even if the fadeintime is longer than the approachtime)
        m_alphaForApproachCircle = std::clamp<float>(1.0f - ((float)(approachCircleFadeEnd - curPosMS) /
                                                             (float)(approachCircleFadeEnd - approachCircleFadeStart)),
                                                     0.0f, 1.0f);

        // hittable dim, see https://github.com/ppy/osu/pull/20572
        if(cv::hitobject_hittable_dim.getBool() &&
           (!flags::has<ModFlags::Mafham>(mods.flags) || !cv::mod_mafham_ignore_hittable_dim.getBool())) {
            const i32 hittableDimFadeStart = m_clickTimeMS - (i32)GameRules::getHitWindowMiss();

            // yes, this means the un-dim animation cuts into the already clickable range
            const i32 hittableDimFadeEnd = hittableDimFadeStart + (i32)cv::hitobject_hittable_dim_duration.getInt();

            m_hittableDimRGBColorMultiplierPct =
                std::lerp(cv::hitobject_hittable_dim_start_percent.getFloat(), 1.0f,
                          std::clamp<float>(1.0f - (float)(hittableDimFadeEnd - curPosMS) /
                                                       (float)(hittableDimFadeEnd - hittableDimFadeStart),
                                            0.0f, 1.0f));
        }

        m_visible = true;
    } else {
        m_approachScale = 1.0f;
        m_visible = false;
    }
}

void HitObject::addHitResult(LiveScore::HIT result, i32 delta, bool isEndOfCombo, vec2 posRaw, float targetDelta,
                             float targetAngle, bool ignoreOnHitErrorBar, bool ignoreCombo, bool ignoreHealth,
                             bool addObjectDurationToSkinAnimationTimeStartOffset) {
    if(m_pf != nullptr && m_pi->getMods().has(ModFlags::Target) && result != LiveScore::HIT::HIT_MISS &&
       targetDelta >= 0.0f) {
        const float p300 = cv::mod_target_300_percent.getFloat();
        const float p100 = cv::mod_target_100_percent.getFloat();
        const float p50 = cv::mod_target_50_percent.getFloat();

        if(targetDelta < p300 && (result == LiveScore::HIT::HIT_300 || result == LiveScore::HIT::HIT_100))
            result = LiveScore::HIT::HIT_300;
        else if(targetDelta < p100)
            result = LiveScore::HIT::HIT_100;
        else if(targetDelta < p50)
            result = LiveScore::HIT::HIT_50;
        else
            result = LiveScore::HIT::HIT_MISS;

        ui->getHUD()->addTarget(targetDelta, targetAngle);
    }

    const LiveScore::HIT returnedHit = m_pi->addHitResult(this, result, delta, isEndOfCombo, ignoreOnHitErrorBar, false,
                                                          ignoreCombo, false, ignoreHealth);
    if(m_pf == nullptr) return;

    HITRESULTANIM hitresultanim;
    {
        hitresultanim.result = (returnedHit != LiveScore::HIT::HIT_MISS ? returnedHit : result);
        hitresultanim.rawPos = posRaw;
        hitresultanim.deltaMS = delta;
        hitresultanim.timeSecs = engine->getTime();
        hitresultanim.addObjectDurationToSkinAnimationTimeStartOffset = addObjectDurationToSkinAnimationTimeStartOffset;
    }

    // currently a maximum of 2 simultaneous results are supported (for drawing, per hitobject)
    if(engine->getTime() >
       m_hitresultanim1.timeSecs + cv::hitresult_duration_max.getFloat() * (1.0f / m_pf->getBaseAnimationSpeed()))
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

float HitObject::lerp3f(float a, float b, float c, float percent) {
    if(percent <= 0.5f)
        return std::lerp(a, b, percent * 2.0f);
    else
        return std::lerp(b, c, (percent - 0.5f) * 2.0f);
}

int Circle::rainbowNumber = 0;
int Circle::rainbowColorCounter = 0;

void Circle::drawApproachCircle(BeatmapInterface *pf, vec2 rawPos, int number, int colorCounter, int colorOffset,
                                float colorRGBMultiplier, float approachScale, float alpha,
                                bool overrideHDApproachCircle) {
    rainbowNumber = number;
    rainbowColorCounter = colorCounter;

    Color comboColor = Colors::scale(pf->getSkin()->getComboColorForCounter(colorCounter, colorOffset),
                                     colorRGBMultiplier * cv::circle_color_saturation.getFloat());

    drawApproachCircle(pf->getSkin(), pf->osuCoords2Pixels(rawPos), comboColor, pf->fHitcircleDiameter, approachScale,
                       alpha, pf->getMods().has(ModFlags::Hidden), overrideHDApproachCircle);
}

void Circle::drawCircle(BeatmapInterface *pf, vec2 rawPos, int number, int colorCounter, int colorOffset,
                        float colorRGBMultiplier, float approachScale, float alpha, float numberAlpha, bool drawNumber,
                        bool overrideHDApproachCircle) {
    drawCircle(pf->getSkin(), pf->osuCoords2Pixels(rawPos), pf->fHitcircleDiameter, pf->getNumberScale(),
               pf->getHitcircleOverlapScale(), number, colorCounter, colorOffset, colorRGBMultiplier, approachScale,
               alpha, numberAlpha, drawNumber, overrideHDApproachCircle);
}

void Circle::drawCircle(const Skin *skin, vec2 pos, float hitcircleDiameter, float numberScale, float overlapScale,
                        int number, int colorCounter, int colorOffset, float colorRGBMultiplier,
                        float /*approachScale*/, float alpha, float numberAlpha, bool drawNumber,
                        bool /*overrideHDApproachCircle*/) {
    if(alpha <= 0.0f || !cv::draw_circles.getBool()) return;

    rainbowNumber = number;
    rainbowColorCounter = colorCounter;

    Color comboColor = Colors::scale(skin->getComboColorForCounter(colorCounter, colorOffset),
                                     colorRGBMultiplier * cv::circle_color_saturation.getFloat());

    // approach circle
    /// drawApproachCircle(skin, pos, comboColor, hitcircleDiameter, approachScale, alpha, modHD,
    /// overrideHDApproachCircle); // they are now drawn separately in draw2()

    // circle
    const float circleImageScale = hitcircleDiameter / (128.0f * (skin->i_hitcircle.scale()));
    drawHitCircle(skin->i_hitcircle, pos, comboColor, circleImageScale, alpha);

    // overlay
    const float circleOverlayImageScale = hitcircleDiameter / skin->i_hitcircleoverlay.getSizeBaseRaw().x;
    if(!skin->o_hitcircle_overlay_above_number)
        drawHitCircleOverlay(skin->i_hitcircleoverlay, pos, circleOverlayImageScale, alpha, colorRGBMultiplier);

    // number
    if(drawNumber) drawHitCircleNumber(skin, numberScale, overlapScale, pos, number, numberAlpha, colorRGBMultiplier);

    // overlay
    if(skin->o_hitcircle_overlay_above_number)
        drawHitCircleOverlay(skin->i_hitcircleoverlay, pos, circleOverlayImageScale, alpha, colorRGBMultiplier);
}

void Circle::drawCircle(const Skin *skin, vec2 pos, float hitcircleDiameter, Color color, float alpha) {
    // this function is only used by the target practice heatmap

    // circle
    const float circleImageScale = hitcircleDiameter / (128.0f * (skin->i_hitcircle.scale()));
    drawHitCircle(skin->i_hitcircle, pos, color, circleImageScale, alpha);

    // overlay
    const float circleOverlayImageScale = hitcircleDiameter / skin->i_hitcircleoverlay.getSizeBaseRaw().x;
    drawHitCircleOverlay(skin->i_hitcircleoverlay, pos, circleOverlayImageScale, alpha, 1.0f);
}

void Circle::drawSliderStartCircle(BeatmapInterface *pf, vec2 rawPos, int number, int colorCounter, int colorOffset,
                                   float colorRGBMultiplier, float approachScale, float alpha, float numberAlpha,
                                   bool drawNumber, bool overrideHDApproachCircle) {
    drawSliderStartCircle(pf->getSkin(), pf->osuCoords2Pixels(rawPos), pf->fHitcircleDiameter, pf->getNumberScale(),
                          pf->getHitcircleOverlapScale(), number, colorCounter, colorOffset, colorRGBMultiplier,
                          approachScale, alpha, numberAlpha, drawNumber, overrideHDApproachCircle);
}

void Circle::drawSliderStartCircle(const Skin *skin, vec2 pos, float hitcircleDiameter, float numberScale,
                                   float hitcircleOverlapScale, int number, int colorCounter, int colorOffset,
                                   float colorRGBMultiplier, float approachScale, float alpha, float numberAlpha,
                                   bool drawNumber, bool overrideHDApproachCircle) {
    if(alpha <= 0.0f || !cv::draw_circles.getBool()) return;

    // if no sliderstartcircle image is preset, fallback to default circle
    if(skin->i_slider_start_circle == MISSING_TEXTURE) {
        drawCircle(skin, pos, hitcircleDiameter, numberScale, hitcircleOverlapScale, number, colorCounter, colorOffset,
                   colorRGBMultiplier, approachScale, alpha, numberAlpha, drawNumber,
                   overrideHDApproachCircle);  // normal
        return;
    }

    rainbowNumber = number;
    rainbowColorCounter = colorCounter;

    Color comboColor = Colors::scale(skin->getComboColorForCounter(colorCounter, colorOffset),
                                     colorRGBMultiplier * cv::circle_color_saturation.getFloat());

    // circle
    const float circleImageScale = hitcircleDiameter / (128.0f * (skin->i_slider_start_circle.scale()));
    drawHitCircle(skin->i_slider_start_circle, pos, comboColor, circleImageScale, alpha);

    // overlay
    const float circleOverlayImageScale = hitcircleDiameter / skin->i_slider_start_circle_overlay2.getSizeBaseRaw().x;
    if(skin->i_slider_start_circle_overlay != MISSING_TEXTURE) {
        if(!skin->o_hitcircle_overlay_above_number)
            drawHitCircleOverlay(skin->i_slider_start_circle_overlay2, pos, circleOverlayImageScale, alpha,
                                 colorRGBMultiplier);
    }

    // number
    if(drawNumber)
        drawHitCircleNumber(skin, numberScale, hitcircleOverlapScale, pos, number, numberAlpha, colorRGBMultiplier);

    // overlay
    if(skin->i_slider_start_circle_overlay != MISSING_TEXTURE) {
        if(skin->o_hitcircle_overlay_above_number)
            drawHitCircleOverlay(skin->i_slider_start_circle_overlay2, pos, circleOverlayImageScale, alpha,
                                 colorRGBMultiplier);
    }
}

void Circle::drawSliderEndCircle(BeatmapInterface *pf, vec2 rawPos, int number, int colorCounter, int colorOffset,
                                 float colorRGBMultiplier, float approachScale, float alpha, float numberAlpha,
                                 bool drawNumber, bool overrideHDApproachCircle) {
    drawSliderEndCircle(pf->getSkin(), pf->osuCoords2Pixels(rawPos), pf->fHitcircleDiameter, pf->getNumberScale(),
                        pf->getHitcircleOverlapScale(), number, colorCounter, colorOffset, colorRGBMultiplier,
                        approachScale, alpha, numberAlpha, drawNumber, overrideHDApproachCircle);
}

void Circle::drawSliderEndCircle(const Skin *skin, vec2 pos, float hitcircleDiameter, float numberScale,
                                 float overlapScale, int number, int colorCounter, int colorOffset,
                                 float colorRGBMultiplier, float approachScale, float alpha, float numberAlpha,
                                 bool drawNumber, bool overrideHDApproachCircle) {
    if(alpha <= 0.0f || !cv::slider_draw_endcircle.getBool() || !cv::draw_circles.getBool()) return;

    // if no sliderendcircle image is preset, fallback to default circle
    if(skin->i_slider_end_circle == MISSING_TEXTURE) {
        drawCircle(skin, pos, hitcircleDiameter, numberScale, overlapScale, number, colorCounter, colorOffset,
                   colorRGBMultiplier, approachScale, alpha, numberAlpha, drawNumber, overrideHDApproachCircle);
        return;
    }

    rainbowNumber = number;
    rainbowColorCounter = colorCounter;

    Color comboColor = Colors::scale(skin->getComboColorForCounter(colorCounter, colorOffset),
                                     colorRGBMultiplier * cv::circle_color_saturation.getFloat());

    // circle
    const float circleImageScale = hitcircleDiameter / (128.0f * (skin->i_slider_end_circle.scale()));
    drawHitCircle(skin->i_slider_end_circle, pos, comboColor, circleImageScale, alpha);

    // overlay
    if(skin->i_slider_end_circle_overlay != MISSING_TEXTURE) {
        const float circleOverlayImageScale = hitcircleDiameter / skin->i_slider_end_circle_overlay2.getSizeBaseRaw().x;
        drawHitCircleOverlay(skin->i_slider_end_circle_overlay2, pos, circleOverlayImageScale, alpha,
                             colorRGBMultiplier);
    }
}

void Circle::drawApproachCircle(const Skin *skin, vec2 pos, Color comboColor, float hitcircleDiameter,
                                float approachScale, float alpha, bool modHD, bool overrideHDApproachCircle) {
    if((!modHD || overrideHDApproachCircle) && cv::draw_approach_circles.getBool() && !cv::mod_mafham.getBool()) {
        if(approachScale > 1.0f) {
            const float approachCircleImageScale = hitcircleDiameter / (128.0f * (skin->i_approachcircle.scale()));

            g->setColor(comboColor);

            if(cv::circle_rainbow.getBool()) {
                float frequency = 0.3f;
                double time = engine->getTime() * 20.0;
                const float offset = std::fmod(frequency * time + rainbowNumber * rainbowColorCounter, 2.0 * PI);

                float red1 = 0.5f + (std::sin(offset + 0) * 0.5f);
                float green1 = 0.5f + (std::sin(offset + 2) * 0.5f);
                float blue1 = 0.5f + (std::sin(offset + 4) * 0.5f);

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

void Circle::drawHitCircleOverlay(const SkinImage &hitCircleOverlayImage, vec2 pos, float circleOverlayImageScale,
                                  float alpha, float colorRGBMultiplier) {
    g->setColor(argb(alpha, colorRGBMultiplier, colorRGBMultiplier, colorRGBMultiplier));
    hitCircleOverlayImage.drawRaw(pos, circleOverlayImageScale);
}

void Circle::drawHitCircle(Image *hitCircleImage, vec2 pos, Color comboColor, float circleImageScale, float alpha) {
    g->setColor(comboColor);

    if(cv::circle_rainbow.getBool()) {
        float frequency = 0.3f;
        double time = engine->getTime() * 20.0;
        const float offset =
            std::fmod(frequency * time + rainbowNumber * rainbowNumber * rainbowColorCounter, 2.0 * PI);

        float red1 = 0.5f + (std::sin(offset + 0) * 0.5f);
        float green1 = 0.5f + (std::sin(offset + 2) * 0.5f);
        float blue1 = 0.5f + (std::sin(offset + 4) * 0.5f);

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

void Circle::drawHitCircleNumber(const Skin *skin, float numberScale, float overlapScale, vec2 pos, int number,
                                 float numberAlpha, float /*colorRGBMultiplier*/) {
    if(!cv::draw_numbers.getBool()) return;

    // extract digits
    int digits[10];
    int digitCount = 0;

    do {
        digits[digitCount++] = number % 10;
        number /= 10;
    } while(number > 0);

    // set color
    // g->setColor(argb(1.0f, colorRGBMultiplier, colorRGBMultiplier, colorRGBMultiplier)); // see
    // https://github.com/ppy/osu/issues/24506
    g->setColor(0xffffffff);
    if(cv::circle_number_rainbow.getBool()) {
        float frequency = 0.3f;
        double time = engine->getTime() * 20.0;
        const float offset =
            std::fmod(frequency * time + rainbowNumber * rainbowNumber * rainbowNumber * rainbowColorCounter, 2.0 * PI);

        float red1 = 0.5f + (std::sin(offset + 0) * 0.5f);
        float green1 = 0.5f + (std::sin(offset + 2) * 0.5f);
        float blue1 = 0.5f + (std::sin(offset + 4) * 0.5f);

        g->setColor(rgb(red1, green1, blue1));
    }
    g->setAlpha(numberAlpha);

    const auto &defaultImgs = skin->i_defaults;

    // get total width for centering
    float digitWidthCombined = 0.0f;
    for(int i = 0; i < digitCount; i++) {
        digitWidthCombined += defaultImgs[digits[i]]->getWidth();
    }

    // draw digits, start at correct offset
    g->pushTransform();
    {
        g->scale(numberScale, numberScale);
        g->translate(pos.x, pos.y);

        const int digitOverlapCount = digitCount - 1;
        const float firstDigitWidth = defaultImgs[digits[digitCount - 1]]->getWidth();
        g->translate(
            -(digitWidthCombined * numberScale - skin->hitcircle_overlap_amt * digitOverlapCount * overlapScale) *
                    0.5f +
                firstDigitWidth * numberScale * 0.5f,
            0);

        // draw from most significant to least significant
        for(int i = digitCount - 1; i >= 0; i--) {
            g->drawImage(defaultImgs[digits[i]]);

            float offset = defaultImgs[digits[i]]->getWidth() * numberScale;
            if(i > 0) {
                offset += defaultImgs[digits[i - 1]]->getWidth() * numberScale;
            }

            g->translate(offset * 0.5f - skin->hitcircle_overlap_amt * overlapScale, 0);
        }
    }
    g->popTransform();
}

Circle::Circle(int x, int y, i32 timeMS, HitSamples samples, int comboNumber, bool isEndOfCombo, int colorCounter,
               int colorOffset, AbstractBeatmapInterface *pi)
    : HitObject(timeMS, std::move(samples), comboNumber, isEndOfCombo, colorCounter, colorOffset, pi),
      m_rawPos(x, y),
      m_originalRawPos(m_rawPos) {
    m_type = HitObjectType::CIRCLE;
}

Circle::~Circle() { onReset(0); }

void Circle::draw() {
    HitObject::draw();

    const Skin *skin = m_pf->getSkin();
    const bool hd = m_pi->getMods().has(ModFlags::Hidden);

    // draw hit animation
    if(!hd && !cv::instafade.getBool() && m_hitAnimation > 0.0f && m_hitAnimation != 1.0f) {
        float alpha = 1.0f - m_hitAnimation;

        float scale = m_hitAnimation;
        scale = -scale * (scale - 2.0f);  // quad out scale

        const bool drawNumber = skin->version > 1.0f ? false : true;
        const float foscale = cv::circle_fade_out_scale.getFloat();

        g->pushTransform();
        {
            g->scale((1.0f + scale * foscale), (1.0f + scale * foscale));
            skin->i_hitcircleoverlay.setAnimationTimeOffset(
                !m_pf->isInMafhamRenderChunk() ? m_clickTimeMS - m_approachTimeMS : m_pf->getCurMusicPosWithOffsets());
            drawCircle(m_pf, m_rawPos, m_comboNumber, m_colorCounter, m_colorOffset, 1.0f, 1.0f, alpha, alpha,
                       drawNumber);
        }
        g->popTransform();
    }

    if(m_finished ||
       (!m_visible && !m_waiting))  // special case needed for when we are past this objects time, but still
                                    // within not-miss range, because we still need to draw the object
        return;

    // draw circle
    vec2 shakeCorrectedPos = m_rawPos;
    if(engine->getTime() < m_shakeAnimation && !m_pf->isInMafhamRenderChunk())  // handle note blocking shaking
    {
        float smooth =
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
    skin->i_hitcircleoverlay.setAnimationTimeOffset(!m_pf->isInMafhamRenderChunk() ? m_clickTimeMS - m_approachTimeMS
                                                                                   : m_pf->getCurMusicPosWithOffsets());

    {
        const float approachScale = m_waiting && !hd ? 1.0f : m_approachScale;
        const float alpha = m_waiting && !hd ? 1.0f : m_alpha;
        const float numberAlpha = m_waiting && !hd ? 1.0f : m_alpha;

        drawCircle(m_pf, shakeCorrectedPos, m_comboNumber, m_colorCounter, m_colorOffset,
                   m_hittableDimRGBColorMultiplierPct, approachScale, alpha, numberAlpha, true,
                   m_overrideHDApproachCircle);
    }
}

void Circle::draw2() {
    HitObject::draw2();
    if(m_finished || (!m_visible && !m_waiting))
        return;  // special case needed for when we are past this objects time, but still within not-miss range, because
                 // we still need to draw the object

    // draw approach circle
    const bool hd = m_pi->getMods().has(ModFlags::Hidden);

    // HACKHACK: don't fucking change this piece of code here, it fixes a heisenbug
    // (https://github.com/McKay42/McOsu/issues/165)
    if(cv::bug_flicker_log.getBool()) {
        const float approachCircleImageScale =
            m_pf->fHitcircleDiameter / (128.0f * (m_pf->getSkin()->i_approachcircle.scale()));
        debugLog("click_time = {:d}, aScale = {:f}, iScale = {:f}", m_clickTimeMS, m_approachScale,
                 approachCircleImageScale);
    }

    drawApproachCircle(m_pf, m_rawPos, m_comboNumber, m_colorCounter, m_colorOffset, m_hittableDimRGBColorMultiplierPct,
                       m_waiting && !hd ? 1.0f : m_approachScale, m_waiting && !hd ? 1.0f : m_alphaForApproachCircle,
                       m_overrideHDApproachCircle);
}

void Circle::update(i32 curPosMS, f64 frameTimeSecs) {
    HitObject::update(curPosMS, frameTimeSecs);
    if(m_finished) return;

    const ModFlags &curIFaceMods = m_pi->getMods().flags;
    const i32 deltaMS = curPosMS - m_clickTimeMS;

    if(flags::has<ModFlags::Autoplay>(curIFaceMods)) {
        if(curPosMS >= m_clickTimeMS) {
            onHit(LiveScore::HIT::HIT_300, 0);
        }
        return;
    }

    if(flags::has<ModFlags::Relax>(curIFaceMods)) {
        if(curPosMS >= m_clickTimeMS + (i32)cv::relax_offset.getInt() && !m_pi->isPaused() &&
           !m_pi->isContinueScheduled()) {
            const vec2 pos = m_pi->osuCoords2Pixels(m_rawPos);
            const float cursorDelta = vec::length(m_pi->getCursorPos() - pos);
            if((cursorDelta < m_pi->fHitcircleDiameter / 2.0f && (flags::has<ModFlags::Relax>(curIFaceMods)))) {
                LiveScore::HIT result = m_pi->getHitResult(deltaMS);

                if(result != LiveScore::HIT::HIT_NULL) {
                    const float targetDelta = cursorDelta / (m_pi->fHitcircleDiameter / 2.0f);
                    const float targetAngle =
                        glm::degrees(std::atan2(m_pi->getCursorPos().y - pos.y, m_pi->getCursorPos().x - pos.x));

                    onHit(result, deltaMS, targetDelta, targetAngle);
                }
            }
        }
    }

    if(deltaMS >= 0) {
        m_waiting = true;

        // if this is a miss after waiting
        if(deltaMS > (i32)m_pi->getHitWindow50()) {
            onHit(LiveScore::HIT::HIT_MISS, deltaMS);
        }
    } else {
        m_waiting = false;
    }
}

void Circle::updateStackPosition(float stackOffset) {
    m_rawPos = m_originalRawPos - vec2(m_stackNum * stackOffset,
                                       m_stackNum * stackOffset *
                                           ((flags::has<ModFlags::HardRock>(m_pi->getMods().flags)) ? -1.0f : 1.0f));
}

void Circle::miss(i32 curPosMS) {
    if(m_finished) return;

    const i32 deltaMS = curPosMS - m_clickTimeMS;

    onHit(LiveScore::HIT::HIT_MISS, deltaMS);
}

void Circle::onClickEvent(std::vector<Click> &clicks) {
    if(m_finished) return;

    const vec2 cursorPos = clicks[0].cursorPos;
    const vec2 pos = m_pi->osuCoords2Pixels(m_rawPos);
    const float cursorDelta = vec::length(cursorPos - pos);

    if(cursorDelta < m_pi->fHitcircleDiameter / 2.0f) {
        // note blocking & shake
        if(m_blocked) {
            m_shakeAnimation = engine->getTime() + cv::circle_shake_duration.getFloat();
            return;  // ignore click event completely
        }

        const i32 deltaMS = clicks[0].musicPosMS - m_clickTimeMS;

        LiveScore::HIT result = m_pi->getHitResult(deltaMS);
        if(result != LiveScore::HIT::HIT_NULL) {
            const float targetDelta = cursorDelta / (m_pi->fHitcircleDiameter / 2.0f);
            const float targetAngle = glm::degrees(std::atan2(cursorPos.y - pos.y, cursorPos.x - pos.x));

            clicks.erase(clicks.begin());
            onHit(result, deltaMS, targetDelta, targetAngle);
        }
    }
}

void Circle::onHit(LiveScore::HIT result, i32 delta, float targetDelta, float targetAngle) {
    // sound and hit animation
    if(m_pf != nullptr && result != LiveScore::HIT::HIT_MISS) {
        const vec2 osuCoords = m_pf->pixels2OsuCoords(m_pf->osuCoords2Pixels(m_rawPos));
        f32 pan = GameRules::osuCoords2Pan(osuCoords.x);
        m_hitSamples.play(pan, delta, m_clickTimeMS);

        m_hitAnimation = 0.001f;  // quickfix for 1 frame missing images
        m_hitAnimation.set(1.0f, GameRules::getFadeOutTime(m_pi->getBaseAnimationSpeed()), anim::QuadOut);
    }

    // add it, and we are finished
    addHitResult(result, delta, m_endOfCombo, m_rawPos, targetDelta, targetAngle);
    m_finished = true;
}

void Circle::onReset(i32 curPosMS) {
    HitObject::onReset(curPosMS);

    m_waiting = false;
    m_shakeAnimation = 0.0f;

    if(m_pf != nullptr) {
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

vec2 Circle::getAutoCursorPos(i32 /*curPos*/) const { return m_pi->osuCoords2Pixels(m_rawPos); }

Slider::Slider(SLIDERCURVETYPE stype, int repeat, float pixelLength, std::vector<vec2> points,
               const std::vector<float> &ticks, float sliderTimeMS, float sliderTimeMSWithoutRepeats, i32 timeMS,
               HitSamples hoverSamples, std::vector<HitSamples> edgeSamples, int comboNumber, bool isEndOfCombo,
               int colorCounter, int colorOffset, AbstractBeatmapInterface *pi)
    : HitObject(timeMS, std::move(hoverSamples), comboNumber, isEndOfCombo, colorCounter, colorOffset, pi),
      m_points(std::move(points)),
      m_edgeSamples(std::move(edgeSamples)),
      m_pixelLength(std::abs(pixelLength)),
      m_sliderTimeMS(sliderTimeMS),
      m_sliderTimeMSWithoutRepeats(sliderTimeMSWithoutRepeats),
      m_repeat(repeat),
      m_curveType(stype) {
    m_type = HitObjectType::SLIDER;

    // build raw ticks
    for(float tick : ticks) {
        m_ticks.emplace_back(SLIDERTICK{.percent = tick, .finished = false});
    }

    // build curve
    m_curve = SliderCurve::createCurve(m_curveType, m_points, m_pixelLength);

    // build repeats
    for(int i = 0; i < (m_repeat - 1); i++) {
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
    for(int i = 0; i < m_repeat; i++) {
        for(int t = 0; t < m_ticks.size(); t++) {
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

            const float tickPercentRelativeToRepeatFromStartAbs =
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

void Slider::draw() {
    if(m_points.size() <= 0) return;

    const float foscale = cv::circle_fade_out_scale.getFloat();
    const Skin *skin = m_pf->getSkin();

    const ModFlags &curGameplayFlags = m_pf->getMods().flags;

    const bool hd = flags::has<ModFlags::Hidden>(curGameplayFlags);

    const bool isCompletelyFinished = m_startFinished && m_endFinished && m_finished;
    if((m_visible || (m_startFinished && !m_finished)) &&
       !isCompletelyFinished)  // extra possibility to avoid flicker between HitObject::m_bVisible delay and the
                               // fadeout animation below this if block
    {
        const float alpha = (cv::mod_hd_slider_fast_fade.getBool() ? m_alpha : m_bodyAlpha);
        float sliderSnake = cv::snaking_sliders.getBool() ? m_sliderSnakePercent : 1.0f;

        // shrinking sliders
        float sliderSnakeStart = 0.0f;
        if(cv::slider_shrink.getBool() && m_reverseArrowPos == 0) {
            sliderSnakeStart = (m_inReverse ? 0.0f : m_slidePct);
            if(m_inReverse) sliderSnake = m_slidePct;
        }

        // draw slider body
        if(alpha > 0.0f && cv::slider_draw_body.getBool()) drawBody(alpha, sliderSnakeStart, sliderSnake);

        // draw slider ticks
        Color tickColor = 0xffffffff;
        tickColor = Colors::scale(tickColor, m_hittableDimRGBColorMultiplierPct);
        const float tickImageScale =
            (m_pf->fHitcircleDiameter / (16.0f * (skin->i_slider_score_point.scale()))) * 0.125f;
        for(auto &tick : m_ticks) {
            if(tick.finished || tick.percent > sliderSnake) continue;

            vec2 pos = m_pf->osuCoords2Pixels(m_curve->pointAt(tick.percent));

            g->setColor(Color(tickColor).setA(alpha));

            g->pushTransform();
            {
                g->scale(tickImageScale, tickImageScale);
                g->translate(pos.x, pos.y);
                g->drawImage(skin->i_slider_score_point);
            }
            g->popTransform();
        }

        // draw start & end circle & reverse arrows
        if(m_points.size() > 1) {
            // HACKHACK: very dirty code
            bool sliderRepeatStartCircleFinished = (m_repeat < 2);
            bool sliderRepeatEndCircleFinished = false;
            bool endCircleIsAtActualSliderEnd = true;
            for(auto &click : m_clicks) {
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
                (!cv::mod_strict_tracking.getBool() || m_endResult != LiveScore::HIT::HIT_MISS);

            {
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

                const float circle_alpha = m_alpha;

                // end circle
                if(draw_end) drawEndCircle(circle_alpha, sliderSnake);

                // start circle
                if(draw_start) drawStartCircle(circle_alpha);
            }
            // reverse arrows
            if(m_reverseArrowAlpha > 0.0f) {
                // if the combo color is nearly white, blacken the reverse arrow
                Color comboColor = skin->getComboColorForCounter(m_colorCounter, m_colorOffset);
                Color reverseArrowColor = 0xffffffff;
                if((comboColor.Rf() + comboColor.Gf() + comboColor.Bf()) / 3.0f >
                   cv::slider_reverse_arrow_black_threshold.getFloat())
                    reverseArrowColor = 0xff000000;

                reverseArrowColor = Colors::scale(reverseArrowColor, m_hittableDimRGBColorMultiplierPct);

                float div = 0.30f;
                float pulse = (div - std::fmod(std::abs(m_pf->getCurMusicPos()) / 1000.0f, div)) / div;
                pulse *= pulse;  // quad in

                if(!cv::slider_reverse_arrow_animated.getBool() || m_pf->isInMafhamRenderChunk()) pulse = 0.0f;

                // end
                if(m_reverseArrowPos == 2 || m_reverseArrowPos == 3) {
                    vec2 pos = m_pf->osuCoords2Pixels(m_curve->pointAt(1.0f));
                    float rotation =
                        m_curve->getEndAngle() - cv::playfield_rotation.getFloat() - m_pf->getPlayfieldRotation();
                    if((flags::has<ModFlags::HardRock>(curGameplayFlags))) rotation = 360.0f - rotation;
                    if(cv::playfield_mirror_horizontal.getBool()) rotation = 360.0f - rotation;
                    if(cv::playfield_mirror_vertical.getBool()) rotation = 180.0f - rotation;

                    const float osuCoordScaleMultiplier = m_pf->fHitcircleDiameter / m_pf->fRawHitcircleDiameter;
                    float reverseArrowImageScale =
                        (m_pf->fRawHitcircleDiameter / (128.0f * (skin->i_reversearrow.scale()))) *
                        osuCoordScaleMultiplier;

                    reverseArrowImageScale *= 1.0f + pulse * 0.30f;

                    g->setColor(Color(reverseArrowColor).setA(m_reverseArrowAlpha));

                    g->pushTransform();
                    {
                        g->rotate(rotation);
                        g->scale(reverseArrowImageScale, reverseArrowImageScale);
                        g->translate(pos.x, pos.y);
                        g->drawImage(skin->i_reversearrow);
                    }
                    g->popTransform();
                }

                // start
                if(m_reverseArrowPos == 1 || m_reverseArrowPos == 3) {
                    vec2 pos = m_pf->osuCoords2Pixels(m_curve->pointAt(0.0f));
                    float rotation =
                        m_curve->getStartAngle() - cv::playfield_rotation.getFloat() - m_pf->getPlayfieldRotation();
                    if((flags::has<ModFlags::HardRock>(curGameplayFlags))) rotation = 360.0f - rotation;
                    if(cv::playfield_mirror_horizontal.getBool()) rotation = 360.0f - rotation;
                    if(cv::playfield_mirror_vertical.getBool()) rotation = 180.0f - rotation;

                    const float osuCoordScaleMultiplier = m_pf->fHitcircleDiameter / m_pf->fRawHitcircleDiameter;
                    float reverseArrowImageScale =
                        (m_pf->fRawHitcircleDiameter / (128.0f * (skin->i_reversearrow.scale()))) *
                        osuCoordScaleMultiplier;

                    reverseArrowImageScale *= 1.0f + pulse * 0.30f;

                    g->setColor(Color(reverseArrowColor).setA(m_reverseArrowAlpha));

                    g->pushTransform();
                    {
                        g->rotate(rotation);
                        g->scale(reverseArrowImageScale, reverseArrowImageScale);
                        g->translate(pos.x, pos.y);
                        g->drawImage(skin->i_reversearrow);
                    }
                    g->popTransform();
                }
            }
        }
    }

    // slider body fade animation, draw start/end circle hit animation
    const bool instafade_slider_body = cv::instafade_sliders.getBool();
    const bool instafade_slider_head = cv::instafade.getBool();

    const bool slider_fading_out = m_endSliderBodyFadeAnimation > 0.0f && m_endSliderBodyFadeAnimation != 1.0f;

    if(!hd && !instafade_slider_body && slider_fading_out) {
        std::vector<vec2> emptyVector;
        std::vector<vec2> alwaysPoints;
        alwaysPoints.push_back(m_pf->osuCoords2Pixels(m_curve->pointAt(m_slidePct)));
        if(!cv::slider_shrink.getBool())
            drawBody(1.0f - m_endSliderBodyFadeAnimation, 0, 1);
        else if(cv::slider_body_lazer_fadeout_style.getBool())
            SliderRenderer::draw(emptyVector, alwaysPoints, m_pf->fHitcircleDiameter, 0.0f, 0.0f,
                                 m_pf->getSkin()->getComboColorForCounter(m_colorCounter, m_colorOffset), 1.0f,
                                 1.0f - m_endSliderBodyFadeAnimation, m_clickTimeMS);
    }

    const bool do_endhit_animations = !hd && !instafade_slider_head;
    const bool do_starthit_animations = do_endhit_animations && cv::slider_sliderhead_fadeout.getBool();
    for(auto anim_it = m_clickAnimations.begin(); anim_it != m_clickAnimations.end();) {
        if(!anim_it->isAnimating()) {
            anim_it = m_clickAnimations.erase(anim_it);
            continue;
        }
        auto &anim = *anim_it;
        ++anim_it;

        if(do_starthit_animations && anim.type & HitAnim::HEAD) {
            const float alpha = 1.0f - anim.percent;
            const float number_alpha = alpha;

            float scale = anim.percent;
            scale = -scale * (scale - 2.0f);  // quad out scale

            const bool drawNumber = (skin->version > 1.0f ? false : true) && m_curRepeat < 1;

            g->pushTransform();
            {
                g->scale((1.0f + scale * foscale), (1.0f + scale * foscale));
                if(m_curRepeat < 1) {
                    skin->i_hitcircleoverlay.setAnimationTimeOffset(!m_pf->isInMafhamRenderChunk()
                                                                        ? m_clickTimeMS - m_approachTimeMS
                                                                        : m_pf->getCurMusicPosWithOffsets());
                    skin->i_slider_start_circle_overlay2.setAnimationTimeOffset(
                        !m_pf->isInMafhamRenderChunk() ? m_clickTimeMS - m_approachTimeMS
                                                       : m_pf->getCurMusicPosWithOffsets());

                    Circle::drawSliderStartCircle(m_pf, m_curve->pointAt(0.0f), m_comboNumber, m_colorCounter,
                                                  m_colorOffset, 1.0f, 1.0f, alpha, number_alpha, drawNumber);
                } else {
                    skin->i_hitcircleoverlay.setAnimationTimeOffset(
                        !m_pf->isInMafhamRenderChunk() ? m_clickTimeMS : m_pf->getCurMusicPosWithOffsets());
                    skin->i_slider_end_circle_overlay2.setAnimationTimeOffset(
                        !m_pf->isInMafhamRenderChunk() ? m_clickTimeMS : m_pf->getCurMusicPosWithOffsets());

                    Circle::drawSliderEndCircle(m_pf, m_curve->pointAt(0.0f), m_comboNumber, m_colorCounter,
                                                m_colorOffset, 1.0f, 1.0f, alpha, alpha, drawNumber);
                }
            }
            g->popTransform();
        }
        if(do_endhit_animations && anim.type & HitAnim::TAIL) {
            const float alpha = 1.0f - anim.percent;

            float scale = anim.percent;
            scale = -scale * (scale - 2.0f);  // quad out scale

            g->pushTransform();
            {
                g->scale((1.0f + scale * foscale), (1.0f + scale * foscale));
                {
                    skin->i_hitcircleoverlay.setAnimationTimeOffset(!m_pf->isInMafhamRenderChunk()
                                                                        ? m_clickTimeMS - m_fadeInTimeMS
                                                                        : m_pf->getCurMusicPosWithOffsets());
                    skin->i_slider_end_circle_overlay2.setAnimationTimeOffset(!m_pf->isInMafhamRenderChunk()
                                                                                  ? m_clickTimeMS - m_fadeInTimeMS
                                                                                  : m_pf->getCurMusicPosWithOffsets());

                    Circle::drawSliderEndCircle(m_pf, m_curve->pointAt(1.0f), m_comboNumber, m_colorCounter,
                                                m_colorOffset, 1.0f, 1.0f, alpha, 0.0f, false);
                }
            }
            g->popTransform();
        }
    }

    HitObject::draw();
}

void Slider::draw2(bool drawApproachCircle, bool drawOnlyApproachCircle) {
    HitObject::draw2();

    const Skin *skin = m_pf->getSkin();

    // HACKHACK: so much code duplication aaaaaaah
    if((m_visible || (m_startFinished && !m_finished)) &&
       drawApproachCircle)  // extra possibility to avoid flicker between HitObject::m_bVisible delay and the fadeout
                            // animation below this if block
    {
        if(m_points.size() > 1) {
            // HACKHACK: very dirty code
            bool sliderRepeatStartCircleFinished = m_repeat < 2;
            for(auto &click : m_clicks) {
                if(click.type == 0) {
                    if(!click.sliderend) sliderRepeatStartCircleFinished = click.finished;
                }
            }

            // start circle
            if(!m_startFinished || !sliderRepeatStartCircleFinished || (!m_endFinished && m_repeat % 2 == 0)) {
                Circle::drawApproachCircle(m_pf, m_curve->pointAt(0.0f), m_comboNumber, m_colorCounter, m_colorOffset,
                                           m_hittableDimRGBColorMultiplierPct, m_approachScale,
                                           m_alphaForApproachCircle, m_overrideHDApproachCircle);
            }
        }
    }

    if(drawApproachCircle && drawOnlyApproachCircle) return;

    const ModFlags &curGameplayFlags = m_pf->getMods().flags;

    // draw followcircle
    // HACKHACK: this is not entirely correct (due to m_bHeldTillEnd, if held within 300 range but then released, will
    // flash followcircle at the end)
    bool is_holding_click = isClickHeldSlider();
    is_holding_click |= flags::any<ModFlags::Autoplay | ModFlags::Relax>(curGameplayFlags);

    bool should_draw_followcircle = (m_visible && m_cursorInside && is_holding_click);
    should_draw_followcircle |= (m_finished && m_followCircleAnimationAlpha > 0.0f && m_heldTillEnd);

    if(should_draw_followcircle) {
        vec2 point = m_pf->osuCoords2Pixels(m_curPointRaw);

        // HACKHACK: this is shit
        float tickAnimation =
            (m_followCircleTickAnimationScale < 0.1f ? m_followCircleTickAnimationScale / 0.1f
                                                     : (1.0f - m_followCircleTickAnimationScale) / 0.9f);
        if(m_followCircleTickAnimationScale < 0.1f) {
            tickAnimation = -tickAnimation * (tickAnimation - 2.0f);
            tickAnimation = std::clamp<float>(tickAnimation / 0.02f, 0.0f, 1.0f);
        }
        float tickAnimationScale = 1.0f + tickAnimation * cv::slider_followcircle_tick_pulse_scale.getFloat();

        g->setColor(Color(0xffffffff).setA(m_followCircleAnimationAlpha));

        skin->i_slider_follow_circle.setAnimationTimeOffset(m_clickTimeMS);
        skin->i_slider_follow_circle.drawRaw(
            point,
            (m_pf->fSliderFollowCircleDiameter / skin->i_slider_follow_circle.getSizeBaseRaw().x) * tickAnimationScale *
                m_followCircleAnimationScale * 0.85f);  // this is a bit strange, but seems to work perfectly with 0.85
    }

    const bool isCompletelyFinished = m_startFinished && m_endFinished && m_finished;

    // draw sliderb on top of everything
    if((m_visible || (m_startFinished && !m_finished)) &&
       !isCompletelyFinished)  // extra possibility in the if-block to avoid flicker between HitObject::m_bVisible
                               // delay and the fadeout animation below this if-block
    {
        if(m_slidePct > 0.0f) {
            // draw sliderb
            vec2 point = m_pf->osuCoords2Pixels(m_curPointRaw);
            vec2 c1 =
                m_pf->osuCoords2Pixels(m_curve->pointAt(m_slidePct + 0.01f <= 1.0f ? m_slidePct : m_slidePct - 0.01f));
            vec2 c2 =
                m_pf->osuCoords2Pixels(m_curve->pointAt(m_slidePct + 0.01f <= 1.0f ? m_slidePct + 0.01f : m_slidePct));
            float ballAngle = glm::degrees(std::atan2(c2.y - c1.y, c2.x - c1.x));
            if(skin->o_sliderball_flip) ballAngle += (m_curRepeat % 2 == 0) ? 0 : 180;

            g->setColor(skin->o_allow_sliderball_tint
                            ? (cv::slider_ball_tint_combo_color.getBool()
                                   ? skin->getComboColorForCounter(m_colorCounter, m_colorOffset)
                                   : skin->c_slider_ball)
                            : rgb(255, 255, 255));
            g->pushTransform();
            {
                g->rotate(ballAngle);
                skin->i_sliderb.setAnimationTimeOffset(m_clickTimeMS);
                skin->i_sliderb.drawRaw(point, m_pf->fHitcircleDiameter / skin->i_sliderb.getSizeBaseRaw().x);
            }
            g->popTransform();
        }
    }
}

void Slider::drawStartCircle(float alpha) {
    const Skin *skin = m_pf->getSkin();

    if(m_startFinished) {
        skin->i_hitcircleoverlay.setAnimationTimeOffset(
            !m_pf->isInMafhamRenderChunk() ? m_clickTimeMS : m_pf->getCurMusicPosWithOffsets());
        skin->i_slider_end_circle_overlay2.setAnimationTimeOffset(
            !m_pf->isInMafhamRenderChunk() ? m_clickTimeMS : m_pf->getCurMusicPosWithOffsets());

        Circle::drawSliderEndCircle(m_pf, m_curve->pointAt(0.0f), m_comboNumber, m_colorCounter, m_colorOffset,
                                    m_hittableDimRGBColorMultiplierPct, 1.0f, alpha, 0.0f, false, false);
    } else {
        skin->i_hitcircleoverlay.setAnimationTimeOffset(
            !m_pf->isInMafhamRenderChunk() ? m_clickTimeMS - m_approachTimeMS : m_pf->getCurMusicPosWithOffsets());
        skin->i_slider_start_circle_overlay2.setAnimationTimeOffset(
            !m_pf->isInMafhamRenderChunk() ? m_clickTimeMS - m_approachTimeMS : m_pf->getCurMusicPosWithOffsets());

        Circle::drawSliderStartCircle(m_pf, m_curve->pointAt(0.0f), m_comboNumber, m_colorCounter, m_colorOffset,
                                      m_hittableDimRGBColorMultiplierPct, m_approachScale, alpha, alpha,
                                      !m_hideNumberAfterFirstRepeatHit, m_overrideHDApproachCircle);
    }
}

void Slider::drawEndCircle(float alpha, float sliderSnake) {
    const Skin *skin = m_pf->getSkin();

    skin->i_hitcircleoverlay.setAnimationTimeOffset(!m_pf->isInMafhamRenderChunk() ? m_clickTimeMS - m_fadeInTimeMS
                                                                                   : m_pf->getCurMusicPosWithOffsets());
    skin->i_slider_end_circle_overlay2.setAnimationTimeOffset(
        !m_pf->isInMafhamRenderChunk() ? m_clickTimeMS - m_fadeInTimeMS : m_pf->getCurMusicPosWithOffsets());

    Circle::drawSliderEndCircle(m_pf, m_curve->pointAt(sliderSnake), m_comboNumber, m_colorCounter, m_colorOffset,
                                m_hittableDimRGBColorMultiplierPct, 1.0f, alpha, 0.0f, false, false);
}

void Slider::drawBody(float alpha, float from, float to) {
    // smooth begin/end while snaking/shrinking
    std::vector<vec2> alwaysPoints;
    if(cv::slider_body_smoothsnake.getBool()) {
        if(cv::slider_shrink.getBool() && m_sliderSnakePercent > 0.999f) {
            alwaysPoints.push_back(m_pf->osuCoords2Pixels(m_curve->pointAt(m_slidePct)));  // curpoint
            alwaysPoints.push_back(m_pf->osuCoords2Pixels(
                getRawPosAt(getEndTime() + 1)));  // endpoint (because setDrawPercent() causes the last
                                                  // circle mesh to become invisible too quickly)
        }
        if(cv::snaking_sliders.getBool() && m_sliderSnakePercent < 1.0f)
            alwaysPoints.push_back(m_pf->osuCoords2Pixels(
                m_curve->pointAt(m_sliderSnakePercent)));  // snakeoutpoint (only while snaking out)
    }

    const Color undimmedComboColor = m_pf->getSkin()->getComboColorForCounter(m_colorCounter, m_colorOffset);

    if(osu->shouldFallBackToLegacySliderRenderer()) {
        std::vector<vec2> screenPoints = m_curve->getPoints();
        for(auto &screenPoint : screenPoints) {
            screenPoint = m_pf->osuCoords2Pixels(screenPoint);
        }

        // peppy sliders
        SliderRenderer::draw(screenPoints, alwaysPoints, m_pf->fHitcircleDiameter, from, to, undimmedComboColor,
                             m_hittableDimRGBColorMultiplierPct, alpha, m_clickTimeMS);
    } else {
        // vertex buffered sliders
        // as the base mesh is centered at (0, 0, 0) and in raw osu coordinates, we have to scale and translate it to
        // make it fit the actual desktop playfield
        const float scale = GameRules::getPlayfieldScaleFactor();
        vec2 translation = GameRules::getPlayfieldCenter();

        if(m_pf->hasFailed())
            translation = m_pf->osuCoords2Pixels(vec2(GameRules::OSU_COORD_WIDTH / 2, GameRules::OSU_COORD_HEIGHT / 2));

        if(cv::mod_fps.getBool()) translation += m_pf->getFirstPersonCursorDelta();

        SliderRenderer::draw(m_vao.get(), alwaysPoints, translation, scale, m_pf->fHitcircleDiameter, from, to,
                             undimmedComboColor, m_hittableDimRGBColorMultiplierPct, alpha, m_clickTimeMS);
    }
}

void Slider::update(i32 curPosMS, f64 frameTimeSecs) {
    HitObject::update(curPosMS, frameTimeSecs);

    if(m_pf != nullptr) {
        // stop slide sound while paused
        if(m_pf->isPaused() || !m_pf->isPlaying() || m_pf->hasFailed()) {
            m_hitSamples.stop();
        }

        // animations must be updated even if we are finished
        updateAnimations(curPosMS);
    }

    // all further calculations are only done while we are active
    if(m_finished) return;

    const ModFlags &curIFaceMods = m_pi->getMods().flags;

    // slider slide percent
    m_slidePct = 0.0f;
    if(curPosMS > m_clickTimeMS)
        m_slidePct = std::clamp<float>(
            std::clamp<i32>((curPosMS - (m_clickTimeMS)), 0, (i32)m_sliderTimeMS) / m_sliderTimeMS, 0.0f, 1.0f);

    const float sliderSnakeDuration =
        (1.0f / 3.0f) * m_approachTimeMS * cv::slider_snake_duration_multiplier.getFloat();
    m_sliderSnakePercent = std::min(1.0f, (curPosMS - (m_clickTimeMS - m_approachTimeMS)) / (sliderSnakeDuration));

    const i32 reverseArrowFadeInStart =
        m_clickTimeMS - (cv::snaking_sliders.getBool() ? (m_approachTimeMS - sliderSnakeDuration) : m_approachTimeMS);
    const i32 reverseArrowFadeInEnd = reverseArrowFadeInStart + cv::slider_reverse_arrow_fadein_duration.getInt();
    m_reverseArrowAlpha = 1.0f - std::clamp<float>(((float)(reverseArrowFadeInEnd - curPosMS) /
                                                    (float)(reverseArrowFadeInEnd - reverseArrowFadeInStart)),
                                                   0.0f, 1.0f);
    m_reverseArrowAlpha *= cv::slider_reverse_arrow_alpha_multiplier.getFloat();

    m_bodyAlpha = m_alpha;
    if(flags::has<ModFlags::Hidden>(curIFaceMods)) {  // hidden modifies the body alpha
        m_bodyAlpha = m_alphaWithoutHidden;           // fade in as usual

        // fade out over the duration of the slider, starting exactly when the default fadein finishes
        const i32 hiddenSliderBodyFadeOutStart =
            std::min(m_clickTimeMS,
                     m_clickTimeMS - m_approachTimeMS +
                         m_fadeInTimeMS);  // std::min() ensures that the fade always starts at click_time
                                           // (even if the fadeintime is longer than the approachtime)
        const float fade_percent = cv::mod_hd_slider_fade_percent.getFloat();
        const i32 hiddenSliderBodyFadeOutEnd = m_clickTimeMS + (i32)(fade_percent * m_sliderTimeMS);
        if(curPosMS >= hiddenSliderBodyFadeOutStart) {
            m_bodyAlpha = std::clamp<float>(((float)(hiddenSliderBodyFadeOutEnd - curPosMS) /
                                             (float)(hiddenSliderBodyFadeOutEnd - hiddenSliderBodyFadeOutStart)),
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

            float part = 1.0f / (float)m_repeat;
            m_curRepeat = (int)(m_slidePct * m_repeat);
            float baseSlidePercent = part * m_curRepeat;
            float partSlidePercent = (m_slidePct - baseSlidePercent) / part;
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

        m_curPointRaw = m_curve->pointAt(m_slidePct);
        m_curPoint = m_pi->osuCoords2Pixels(m_curPointRaw);
    } else {
        m_curPointRaw = m_curve->pointAt(0.0f);
        m_curPoint = m_pi->osuCoords2Pixels(m_curPointRaw);
    }

    // No longer ignore keys that were released since entering the slider
    // see isClickHeldSlider()
    m_ignoredKeys &= m_pi->getKeys();

    // handle dynamic followradius
    float followRadius = m_cursorLeft ? m_pi->fHitcircleDiameter / 2.0f : m_pi->fSliderFollowCircleDiameter / 2.0f;
    const bool isPlayfieldCursorInside = (vec::length(m_pi->getCursorPos() - m_curPoint) < followRadius);
    const bool isAutoCursorInside =
        ((flags::has<ModFlags::Autoplay>(curIFaceMods)) &&
         (!cv::auto_cursordance.getBool() || (vec::length(m_pi->getCursorPos() - m_curPoint) < followRadius)));
    m_cursorInside = (isAutoCursorInside || isPlayfieldCursorInside);
    m_cursorLeft = !m_cursorInside;

    // handle slider start
    if(!m_startFinished) {
        if((flags::has<ModFlags::Autoplay>(curIFaceMods))) {
            if(curPosMS >= m_clickTimeMS) {
                onHit(LiveScore::HIT::HIT_300, 0, false);
                m_pi->holding_slider = true;
            }
        } else {
            i32 deltaMS = curPosMS - m_clickTimeMS;

            if((flags::has<ModFlags::Relax>(curIFaceMods))) {
                if(curPosMS >= m_clickTimeMS + (i32)cv::relax_offset.getInt() && !m_pi->isPaused() &&
                   !m_pi->isContinueScheduled()) {
                    const vec2 pos = m_pi->osuCoords2Pixels(m_curve->pointAt(0.0f));
                    const float cursorDelta = vec::length(m_pi->getCursorPos() - pos);
                    if((cursorDelta < m_pi->fHitcircleDiameter / 2.0f && (flags::has<ModFlags::Relax>(curIFaceMods)))) {
                        LiveScore::HIT result = m_pi->getHitResult(deltaMS);

                        if(result != LiveScore::HIT::HIT_NULL) {
                            const float targetDelta = cursorDelta / (m_pi->fHitcircleDiameter / 2.0f);
                            const float targetAngle = glm::degrees(
                                std::atan2(m_pi->getCursorPos().y - pos.y, m_pi->getCursorPos().x - pos.x));

                            m_startResult = result;
                            onHit(m_startResult, deltaMS, false, targetDelta, targetAngle);
                            m_pi->holding_slider = true;
                        }
                    }
                }
            }

            // wait for a miss
            if(deltaMS >= 0) {
                // if this is a miss after waiting
                if(deltaMS > (i32)m_pi->getHitWindow50()) {
                    m_startResult = LiveScore::HIT::HIT_MISS;
                    onHit(m_startResult, deltaMS, false);
                    m_pi->holding_slider = false;
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
                    if(m_endResult == LiveScore::HIT::HIT_NULL) {
                        // force miss endcircle
                        onSliderBreak();

                        m_heldTillEnd = false;
                        m_heldTillEndForLenienceHack = false;
                        m_heldTillEndForLenienceHackCheck = true;
                        m_endResult = LiveScore::HIT::HIT_MISS;

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
                onHit(LiveScore::HIT::HIT_300, 0, true);
                m_pi->holding_slider = false;
            }
        } else {
            if(curPosMS >= getEndTime()) {
                // handle leftover startcircle
                {
                    // this may happen (if the slider time is shorter than the miss window of the startcircle)
                    if(m_startResult == LiveScore::HIT::HIT_NULL) {
                        // we still want to cause a sliderbreak in this case!
                        onSliderBreak();

                        // special case: missing the startcircle drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS
                        // health)
                        m_pi->addHitResult(this, LiveScore::HIT::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                                           false);  // only decrease health

                        m_startResult = LiveScore::HIT::HIT_MISS;
                    }
                }

                // handle endcircle
                bool isEndResultComingFromStrictTrackingMod = false;
                if(m_endResult == LiveScore::HIT::HIT_NULL) {
                    m_heldTillEnd = m_heldTillEndForLenienceHack;
                    m_endResult = m_heldTillEnd ? LiveScore::HIT::HIT_300 : LiveScore::HIT::HIT_MISS;

                    // handle total slider result (currently startcircle + repeats + ticks + endcircle)
                    // clicks = (repeats + ticks)
                    const float numMaxPossibleHits = 1 + m_clicks.size() + 1;
                    float numActualHits = 0;

                    if(m_startResult != LiveScore::HIT::HIT_MISS) numActualHits++;
                    if(m_endResult != LiveScore::HIT::HIT_MISS) numActualHits++;

                    for(auto &click : m_clicks) {
                        if(click.successful) numActualHits++;
                    }

                    const float percent = numActualHits / numMaxPossibleHits;

                    const bool allow300 = (flags::has<ModFlags::ScoreV2>(curIFaceMods))
                                              ? (m_startResult == LiveScore::HIT::HIT_300)
                                              : true;
                    const bool allow100 =
                        (flags::has<ModFlags::ScoreV2>(curIFaceMods))
                            ? (m_startResult == LiveScore::HIT::HIT_300 || m_startResult == LiveScore::HIT::HIT_100)
                            : true;

                    // rewrite m_endResult as the whole slider result, then use it for the final onHit()
                    if(percent >= 0.999f && allow300)
                        m_endResult = LiveScore::HIT::HIT_300;
                    else if(percent >= 0.5f && allow100 && !flags::has<ModFlags::Ming3012>(curIFaceMods) &&
                            !flags::has<ModFlags::No100s>(curIFaceMods))
                        m_endResult = LiveScore::HIT::HIT_100;
                    else if(percent > 0.0f && !flags::has<ModFlags::No100s>(curIFaceMods) &&
                            !flags::has<ModFlags::No50s>(curIFaceMods))
                        m_endResult = LiveScore::HIT::HIT_50;
                    else
                        m_endResult = LiveScore::HIT::HIT_MISS;

                    // debugLog("percent = {:f}", percent);

                    if(!m_heldTillEnd && cv::slider_end_miss_breaks_combo.getBool()) onSliderBreak();
                } else
                    isEndResultComingFromStrictTrackingMod = true;

                onHit(m_endResult, 0, true, 0.0f, 0.0f, isEndResultComingFromStrictTrackingMod);
                m_pi->holding_slider = false;
            }
        }

        // handle sliderslide sound
        // TODO @kiwec: move this to draw()
        if(m_pf != nullptr) {
            const ModFlags &curGameplayFlags = m_pf->getMods().flags;

            const bool sliding = m_startFinished && !m_endFinished && m_cursorInside && m_deltaMS <= 0             //
                                 && (isClickHeldSlider() || (flags::has<ModFlags::Autoplay>(curGameplayFlags)) ||  //
                                     (flags::has<ModFlags::Relax>(curGameplayFlags)))                              //
                                 && !m_pf->isPaused() && !m_pf->isWaiting() && m_pf->isPlaying()                   //
                                 && !m_pf->bWasSeekFrame;

            if(sliding) {
                const vec2 osuCoords = m_pf->pixels2OsuCoords(m_pf->osuCoords2Pixels(m_curPointRaw));
                f32 pan = GameRules::osuCoords2Pan(osuCoords.x);
                m_lastSliderSampleSets = m_hitSamples.play(pan, 0, -1, true);
            } else if(!m_lastSliderSampleSets.empty()) {
                // debugLog("not sliding, stopping");
                // debugLog(
                //     "bStartFinished {} bEndFinished {} bCursorInside {} iDelta {} "
                //     "isClickHeldSlider() {} pf->isPaused() {} pf->isWaiting() {} "
                //     "pf->isPlaying() {} pf->bWasSeekFrame {}",
                //     !!bStartFinished, !!bEndFinished, !!bCursorInside, iDelta,
                //     isClickHeldSlider(), pf->isPaused(), pf->isWaiting(), pf->isPlaying(),
                //     pf->bWasSeekFrame);
                m_hitSamples.stop(m_lastSliderSampleSets);
                m_lastSliderSampleSets.clear();
            }
        }
    }
}

void Slider::updateAnimations(i32 curPosMS) {
    float animation_multiplier = m_pf->getSpeedAdjustedAnimationSpeed();

    float fadein_fade_time = cv::slider_followcircle_fadein_fade_time.getFloat() * animation_multiplier;
    float fadeout_fade_time = cv::slider_followcircle_fadeout_fade_time.getFloat() * animation_multiplier;
    float fadein_scale_time = cv::slider_followcircle_fadein_scale_time.getFloat() * animation_multiplier;
    float fadeout_scale_time = cv::slider_followcircle_fadeout_scale_time.getFloat() * animation_multiplier;

    // handle followcircle animations
    m_followCircleAnimationAlpha =
        std::clamp<float>((float)((curPosMS - m_clickTimeMS)) / 1000.0f /
                              std::clamp<float>(fadein_fade_time, 0.0f, m_durationMS / 1000.0f),
                          0.0f, 1.0f);
    if(m_finished) {
        m_followCircleAnimationAlpha =
            1.0f - std::clamp<float>((float)((curPosMS - (getEndTime()))) / 1000.0f / fadeout_fade_time, 0.0f, 1.0f);
        m_followCircleAnimationAlpha *= m_followCircleAnimationAlpha;  // quad in
    }

    m_followCircleAnimationScale =
        std::clamp<float>((float)((curPosMS - m_clickTimeMS)) / 1000.0f /
                              std::clamp<float>(fadein_scale_time, 0.0f, m_durationMS / 1000.0f),
                          0.0f, 1.0f);
    if(m_finished) {
        m_followCircleAnimationScale =
            std::clamp<float>((float)((curPosMS - (getEndTime()))) / 1000.0f / fadeout_scale_time, 0.0f, 1.0f);
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

void Slider::updateStackPosition(float stackOffset) {
    if(m_curve != nullptr)
        m_curve->updateStackPosition(m_stackNum * stackOffset, (flags::has<ModFlags::HardRock>(m_pi->getMods().flags)));
}

void Slider::miss(i32 curPosMS) {
    if(m_finished) return;

    const i32 deltaMS = curPosMS - m_clickTimeMS;

    // startcircle
    if(!m_startFinished) {
        m_startResult = LiveScore::HIT::HIT_MISS;
        onHit(m_startResult, deltaMS, false);
        m_pi->holding_slider = false;
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

            m_endResult = LiveScore::HIT::HIT_MISS;
            onHit(m_endResult, 0, true);
            m_pi->holding_slider = false;
        }
    }
}

vec2 Slider::getRawPosAt(i32 posMS) const {
    if(m_curve == nullptr) return vec2(0, 0);

    if(posMS <= m_clickTimeMS)
        return m_curve->pointAt(0.0f);
    else if(posMS >= m_clickTimeMS + m_sliderTimeMS) {
        if(m_repeat % 2 == 0)
            return m_curve->pointAt(0.0f);
        else
            return m_curve->pointAt(1.0f);
    } else
        return m_curve->pointAt(getT(posMS, false));
}

vec2 Slider::getOriginalRawPosAt(i32 posMS) const {
    if(m_curve == nullptr) return vec2(0, 0);

    if(posMS <= m_clickTimeMS)
        return m_curve->originalPointAt(0.0f);
    else if(posMS >= m_clickTimeMS + m_sliderTimeMS) {
        if(m_repeat % 2 == 0)
            return m_curve->originalPointAt(0.0f);
        else
            return m_curve->originalPointAt(1.0f);
    } else
        return m_curve->originalPointAt(getT(posMS, false));
}

float Slider::getT(i32 posMS, bool raw) const {
    float t = (float)((i32)posMS - m_clickTimeMS) / m_sliderTimeMSWithoutRepeats;
    if(raw)
        return t;
    else {
        auto floorVal = (float)std::floor(t);
        return ((int)floorVal % 2 == 0) ? t - floorVal : floorVal + 1 - t;
    }
}

void Slider::onClickEvent(std::vector<Click> &clicks) {
    if(m_points.size() == 0 || m_blocked)
        return;  // also handle note blocking here (doesn't need fancy shake logic, since sliders don't shake in
                 // osu!stable)

    if(!m_startFinished) {
        const vec2 cursorPos = clicks[0].cursorPos;
        const vec2 pos = m_pi->osuCoords2Pixels(m_curve->pointAt(0.0f));
        const float cursorDelta = vec::length(cursorPos - pos);

        if(cursorDelta < m_pi->fHitcircleDiameter / 2.0f) {
            const i32 deltaMS = clicks[0].musicPosMS - m_clickTimeMS;

            LiveScore::HIT result = m_pi->getHitResult(deltaMS);
            if(result != LiveScore::HIT::HIT_NULL) {
                const float targetDelta = cursorDelta / (m_pi->fHitcircleDiameter / 2.0f);
                const float targetAngle = glm::degrees(std::atan2(cursorPos.y - pos.y, cursorPos.x - pos.x));

                clicks.erase(clicks.begin());
                m_startResult = result;
                onHit(m_startResult, deltaMS, false, targetDelta, targetAngle);
                m_pi->holding_slider = true;
            }
        }
    }
}

void Slider::onHit(LiveScore::HIT result, i32 delta, bool isEndCircle, float targetDelta, float targetAngle,
                   bool isEndResultFromStrictTrackingMod) {
    if(m_points.size() == 0) return;

    // start + end of a slider add +30 points, if successful

    // debugLog("isEndCircle = {:d},    m_iCurRepeat = {:d}", (int)isEndCircle, iCurRepeat);

    // sound and hit animation and also sliderbreak combo drop
    {
        if(result == LiveScore::HIT::HIT_MISS) {
            if(!isEndResultFromStrictTrackingMod) onSliderBreak();
        } else if(m_pf != nullptr) {
            if(m_edgeSamples.size() > 0) {
                const vec2 osuCoords = m_pf->pixels2OsuCoords(m_pf->osuCoords2Pixels(m_curPointRaw));
                const f32 pan = GameRules::osuCoords2Pan(osuCoords.x);
                if(isEndCircle) {
                    m_edgeSamples.back().play(pan, delta, getEndTime());
                } else {
                    m_edgeSamples[0].play(pan, delta, m_clickTimeMS);
                }
            }

            const float fadeoutTimeSecs = GameRules::getFadeOutTime(m_pi->getBaseAnimationSpeed());

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

        // end body fadeout
        if(m_pf != nullptr && isEndCircle) {
            m_endSliderBodyFadeAnimation = 0.001f;  // quickfix for 1 frame missing images
            m_endSliderBodyFadeAnimation.set(1.0f,
                                             GameRules::getFadeOutTime(m_pi->getBaseAnimationSpeed()) *
                                                 cv::slider_body_fade_out_time_multiplier.getFloat(),
                                             anim::QuadOut);
            // debugLog("stopping due to end body fadeout");
            m_hitSamples.stop();
        }
    }

    // add score, and we are finished
    if(!isEndCircle) {
        // startcircle

        m_startFinished = true;

        // ignore all keys that were held prior to entering the slider
        // except the one used to tap the slider head (or, "hold into" the slider)
        // see isClickHeldSlider()
        m_ignoredKeys = (m_pi->getKeys() & ~m_pi->lastPressedKey);

        if(flags::has<ModFlags::Target>(m_pi->getMods().flags)) {
            // not end of combo, show in hiterrorbar, use for accuracy, increase combo, increase
            // score, ignore for health, don't add object duration to result anim
            addHitResult(result, delta, false, m_curve->pointAt(0.0f), targetDelta, targetAngle, false, false, true,
                         false);
        } else {
            // not end of combo, show in hiterrorbar, ignore for accuracy, increase combo,
            // don't count towards score, depending on scorev2 ignore for health or not
            m_pi->addHitResult(this, result, delta, false, false, true, false, true, true);
        }

        // add bonus score + health manually
        if(result != LiveScore::HIT::HIT_MISS) {
            LiveScore::HIT resultForHealth = LiveScore::HIT::HIT_SLIDER30;

            m_pi->addHitResult(this, resultForHealth, 0, false, true, true, true, true,
                               false);  // only increase health
            m_pi->addScorePoints(30);
        } else {
            // special case: missing the startcircle drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS health)
            m_pi->addHitResult(this, LiveScore::HIT::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
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
                m_pi->addHitResult(this, LiveScore::HIT::HIT_SLIDER30, 0, false, true, true, true, true,
                                   false);  // only increase health
                m_pi->addScorePoints(30);
            } else {
                // special case: missing the endcircle drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS health)
                // NOTE: yes, this will drain twice for the end of a slider (once for the judgement of the whole slider
                // above, and once for the endcircle here)
                m_pi->addHitResult(this, LiveScore::HIT::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                                   false);  // only decrease health
            }
        }
    }

    m_curRepeatCounterForHitSounds++;
}

void Slider::onRepeatHit(const SLIDERCLICK &click) {
    if(m_points.size() == 0) return;

    // repeat hit of a slider adds +30 points, if successful

    // sound and hit animation
    if(!click.successful) {
        onSliderBreak();
    } else if(m_pf != nullptr) {
        const vec2 osuCoords = m_pf->pixels2OsuCoords(m_pf->osuCoords2Pixels(m_curPointRaw));
        f32 pan = GameRules::osuCoords2Pan(osuCoords.x);

        // Try to play a repeat sample based on what the mapper gave us
        // NOTE: iCurRepeatCounterForHitSounds starts at 1
        const uSz nb_edge_samples = m_edgeSamples.size();
        assert(nb_edge_samples > 0);
        if(std::cmp_less(m_curRepeatCounterForHitSounds + 1, nb_edge_samples)) {
            m_edgeSamples[m_curRepeatCounterForHitSounds].play(pan, 0, click.timeMS);
        } else {
            // We have more repeats than edge samples!
            // Just play whatever we can (either the last repeat sample, or the start sample)
            m_edgeSamples[nb_edge_samples - 2].play(pan, 0, click.timeMS);
        }

        float animation_multiplier = m_pf->getSpeedAdjustedAnimationSpeed();
        float tick_pulse_time = cv::slider_followcircle_tick_pulse_time.getFloat() * animation_multiplier;

        m_followCircleTickAnimationScale = 0.0f;
        m_followCircleTickAnimationScale.set(1.0f, tick_pulse_time, anim::Linear);

        const float fadeoutTimeSecs = GameRules::getFadeOutTime(m_pi->getBaseAnimationSpeed());

        if(click.sliderend) {
            addHitAnim(HitAnim::TAIL, fadeoutTimeSecs);
        } else {
            addHitAnim(HitAnim::HEAD, fadeoutTimeSecs);
        }
    }

    // add score
    if(!click.successful) {
        // add health manually
        // special case: missing a repeat drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS health)
        m_pi->addHitResult(this, LiveScore::HIT::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                           false);  // only decrease health
    } else {
        m_pi->addHitResult(this, LiveScore::HIT::HIT_SLIDER30, 0, false, true, true, false, true,
                           false);  // not end of combo, ignore in hiterrorbar, ignore for accuracy, increase
                                    // combo, don't count towards score, increase health

        // add bonus score manually
        m_pi->addScorePoints(30);
    }

    m_curRepeatCounterForHitSounds++;
}

void Slider::onTickHit(const SLIDERCLICK &click) {
    if(m_points.size() == 0) return;

    // tick hit of a slider adds +10 points, if successful

    // tick drawing visibility
    int numMissingTickClicks = 0;
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
    } else if(m_pf != nullptr) {
        if(const auto *skin = m_pf->getSkin()) {
            static constexpr std::array SLIDERTICK_SAMPLESET_METHODS{
                &Skin::s_normal_slidertick,  //
                &Skin::s_soft_slidertick,    //
                &Skin::s_drum_slidertick,    //
            };

            const BeatmapDifficulty *beatmap = m_pf->getBeatmap();
            const auto ti = (click.timeMS != -1 && beatmap) ? beatmap->getTimingInfoForTime(click.timeMS)
                                                            : m_pf->getCurrentTimingInfo();
            HitSoundContext ctx{
                .timingPointSampleSet = ti.sampleSet,
                .timingPointVolume = ti.volume,
                .defaultSampleSet = m_pf->getDefaultSampleSet(),
                .layeredHitSounds = false,  // unused by sliderticks
                .forcedSampleSet = cv::skin_force_hitsound_sample_set.getInt(),
                .ignoreSampleVolume = cv::ignore_beatmap_sample_volume.getBool(),
                .boostVolume = false,  // unused by sliderticks
            };

            if(const auto tick = m_hitSamples.resolveSliderTick(ctx);
               tick.set >= 0 && tick.set < (i32)SLIDERTICK_SAMPLESET_METHODS.size()) {
                if(Sound *skin_sound = skin->*SLIDERTICK_SAMPLESET_METHODS[tick.set]) {
                    const vec2 osuCoords = m_pf->pixels2OsuCoords(m_pf->osuCoords2Pixels(m_curPointRaw));
                    f32 pan = GameRules::osuCoords2Pan(osuCoords.x);
                    if(!cv::sound_panning.getBool() ||
                       (cv::mod_fposu.getBool() && !cv::mod_fposu_sound_panning.getBool()) ||
                       (cv::mod_fps.getBool() && !cv::mod_fps_sound_panning.getBool())) {
                        pan = 0.0f;
                    } else {
                        pan *= cv::sound_panning_multiplier.getFloat();
                    }
                    soundEngine->play(skin_sound, pan, 0.f, tick.volume);
                }
            }
        }

        float animation_multiplier = m_pf->getSpeedAdjustedAnimationSpeed();
        float tick_pulse_time = cv::slider_followcircle_tick_pulse_time.getFloat() * animation_multiplier;

        m_followCircleTickAnimationScale = 0.0f;
        m_followCircleTickAnimationScale.set(1.0f, tick_pulse_time, anim::Linear);
    }

    // add score
    if(!click.successful) {
        // add health manually
        // special case: missing a tick drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS health)
        m_pi->addHitResult(this, LiveScore::HIT::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                           false);  // only decrease health
    } else {
        m_pi->addHitResult(this, LiveScore::HIT::HIT_SLIDER10, 0, false, true, true, false, true,
                           false);  // not end of combo, ignore in hiterrorbar, ignore for accuracy, increase
                                    // combo, don't count towards score, increase health

        // add bonus score manually
        m_pi->addScorePoints(10);
    }
}

void Slider::onSliderBreak() { m_pi->addSliderBreak(); }

void Slider::onReset(i32 curPosMS) {
    HitObject::onReset(curPosMS);

    if(m_pf != nullptr) {
        // debugLog("stopping due to onReset");
        m_hitSamples.stop();

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
    m_startResult = LiveScore::HIT::HIT_NULL;
    m_endResult = LiveScore::HIT::HIT_NULL;

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

    for(int i = 0; i < m_ticks.size(); i++) {
        int numMissingTickClicks = 0;
        for(const auto &click : m_clicks) {
            if(click.type == 1 && click.tickIndex == i && !click.finished) {
                numMissingTickClicks++;
            }
        }
        m_ticks[i].finished = numMissingTickClicks == 0;
    }
}

Slider::HitAnim &Slider::addHitAnim(u8 typeFlags, float duration) {
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

void Slider::rebuildVertexBuffer(bool useRawCoords) {
    // base mesh (background) (raw unscaled, size in raw osu coordinates centered at (0, 0, 0))
    // this mesh needs to be scaled and translated appropriately since we are not 1:1 with the playfield
    std::vector<vec2> osuCoordPoints = m_curve->getPoints();
    if(!useRawCoords) {
        for(auto &osuCoordPoint : osuCoordPoints) {
            osuCoordPoint = m_pi->osuCoords2LegacyPixels(osuCoordPoint);
        }
    }
    m_vao = SliderRenderer::generateVAO(osuCoordPoints, m_pi->fRawHitcircleDiameter);
}

Slider::~Slider() { onReset(0); }

bool Slider::isClickHeldSlider() {
    // osu! has a weird slider quirk, that I'll explain in detail here.
    // When holding K1 before the slider, tapping K2 on slider head, and releasing K2 later,
    // the slider is no longer considered being "held" until K2 is pressed again, or K1 is released and pressed again.

    // The reason this exists is to prevent people from holding K1 the whole map and tapping with K2.
    // Holding is part of the rhythm flow, and this is a rhythm game right?

    // Note that the restriction only applies to the slider head.
    // Any key pressed *after* entering the slider counts as a hold.

    u8 held_gameplay_keys = m_pi->getKeys() & ~LegacyReplay::Smoke;
    return (held_gameplay_keys & ~m_ignoredKeys);
}

Spinner::Spinner(int x, int y, i32 timeMS, HitSamples samples, bool isEndOfCombo, i32 endTimeMS,
                 AbstractBeatmapInterface *pi)
    : HitObject(timeMS, std::move(samples), -1, isEndOfCombo, -1, -1, pi), m_rawPos(x, y), m_originalRawPos(m_rawPos) {
    m_type = HitObjectType::SPINNER;
    m_durationMS = endTimeMS - timeMS;

    constexpr int minVel = 12;
    constexpr int maxVel = 48;
    constexpr int minTimeMS = 2000;
    constexpr int maxTimeMS = 5000;
    m_maxStoredDeltaAngles = std::clamp<int>(
        (int)((endTimeMS - timeMS - minTimeMS) * (maxVel - minVel) / (maxTimeMS - minTimeMS) + minVel), minVel, maxVel);
    m_storedDeltaAngles = std::make_unique<float[]>(m_maxStoredDeltaAngles);

    // spinners don't need misaims
    m_misAim = true;

    // spinners don't use AR-dependent fadein, instead they always fade in with hardcoded 400 ms (see
    // GameRules::getFadeInTime())
    m_useFadeInTimeAsApproachTime = !cv::spinner_use_ar_fadein.getBool();
}

Spinner::~Spinner() { onReset(0); }

void Spinner::draw() {
    HitObject::draw();
    const float fadeOutMultiplier = cv::spinner_fade_out_time_multiplier.getFloat();
    const i32 fadeOutTimeMS =
        (i32)(GameRules::getFadeOutTime(m_pi->getBaseAnimationSpeed()) * 1000.0f * fadeOutMultiplier);
    const i32 deltaEnd = m_deltaMS + m_durationMS;
    if((m_finished || !m_visible) && (deltaEnd > 0 || (deltaEnd < -fadeOutTimeMS))) return;

    const Skin *skin = m_pf->getSkin();
    const vec2 center = m_pf->osuCoords2Pixels(m_rawPos);

    // only used for fade out anim atm
    const f32 alphaMultiplier =
        std::clamp<f32>((deltaEnd < 0 ? 1.f - ((f32)std::abs(deltaEnd) / (f32)fadeOutTimeMS) : 1.f), 0.f, 1.f);

    const f32 spinnerScale = m_pf->getPlayfieldSize().y / 667.f;

    // the spinner grows until reaching 100% during spinning, depending on how many spins are left
    const f32 clampedRatio = std::clamp<float>(m_ratio, 0.0f, 1.0f);
    const f32 finishScaleRatio = -clampedRatio * (clampedRatio - 2);
    const f32 finishScale = 0.80f + finishScaleRatio * 0.20f;

    // TODO: fix scaling/positioning, see https://osu.ppy.sh/wiki/en/Skinning/osu%21#spinner
    // TODO: skin->bSpinnerFadePlayfield

    if(skin->i_spinner_bg != MISSING_TEXTURE || skin->version < 2.0f)  // old style
    {
        // draw background
        g->pushTransform();
        {
            f32 backgroundScale = spinnerScale / (skin->i_spinner_bg.scale());
            g->setColor(Color(skin->c_spinner_bg).setA(m_alphaWithoutHidden * alphaMultiplier));
            g->scale(backgroundScale, backgroundScale);
            g->translate(center.x, center.y);
            g->drawImage(skin->i_spinner_bg);
        }
        g->popTransform();

        // draw spinner metre
        if(cv::skin_use_spinner_metre.getBool() && skin->i_spinner_metre != MISSING_TEXTURE) {
            f32 metreScale = spinnerScale / (skin->i_spinner_metre.scale());
            g->setColor(Color(0xffffffff).setA(m_alphaWithoutHidden * alphaMultiplier));

            f32 metreWidth = (f32)skin->i_spinner_metre.getWidth() / (skin->i_spinner_metre.scale());
            f32 metreHeight = (f32)skin->i_spinner_metre.getHeight() / (skin->i_spinner_metre.scale());

            g->pushTransform();
            {
                // TODO: "steps" instead of smooth progress
                // TODO: blinking (unless skin->bSpinnerNoBlink or cv::avoid_flashes)
                f32 y = (1.f - clampedRatio) * metreHeight;
                McRect clip{0.f, y, metreWidth, metreHeight};

                g->scale(metreScale, metreScale);
                g->translate(center.x - (metreWidth / 2.f * spinnerScale), 46.f);
                g->drawImage(skin->i_spinner_metre, AnchorPoint::TOP_LEFT, 0.f, clip);
            }
            g->popTransform();
        }

        // draw spinner circle
        if(skin->i_spinner_circle != MISSING_TEXTURE) {
            const f32 spinnerCircleScale = spinnerScale / (skin->i_spinner_circle.scale());
            g->setColor(Color(0xffffffff).setA(m_alphaWithoutHidden * alphaMultiplier));

            g->pushTransform();
            {
                g->rotate(m_drawRot);
                g->scale(spinnerCircleScale, spinnerCircleScale);
                g->translate(center.x, center.y);
                g->drawImage(skin->i_spinner_circle);
            }
            g->popTransform();
        }

        // draw approach circle
        if(!(flags::has<ModFlags::Hidden>(m_pi->getMods().flags)) && m_percent > 0.0f) {
            const f32 spinnerApproachCircleImageScale = (spinnerScale * 2) / (skin->i_spinner_approach_circle.scale());
            g->setColor(Color(skin->c_spinner_approach_circle).setA(m_alphaWithoutHidden * alphaMultiplier));

            g->pushTransform();
            {
                g->scale(spinnerApproachCircleImageScale * m_percent, spinnerApproachCircleImageScale * m_percent);
                g->translate(center.x, center.y);
                g->drawImage(skin->i_spinner_approach_circle);
            }
            g->popTransform();
        }
    } else  // new style
    {
        // bottom
        if(skin->i_spinner_bottom != MISSING_TEXTURE) {
            const f32 spinnerBottomImageScale = spinnerScale / (skin->i_spinner_bottom.scale());
            g->setColor(Color(0xffffffff).setA(m_alphaWithoutHidden * alphaMultiplier));

            g->pushTransform();
            {
                g->rotate(m_drawRot / 7.0f);
                g->scale(spinnerBottomImageScale * finishScale, spinnerBottomImageScale * finishScale);
                g->translate(center.x, center.y);
                g->drawImage(skin->i_spinner_bottom);
            }
            g->popTransform();
        }

        // top
        if(skin->i_spinner_top != MISSING_TEXTURE) {
            const f32 spinnerTopImageScale = spinnerScale / (skin->i_spinner_top.scale());
            g->setColor(Color(0xffffffff).setA(m_alphaWithoutHidden * alphaMultiplier));

            g->pushTransform();
            {
                g->rotate(m_drawRot / 2.0f);
                g->scale(spinnerTopImageScale * finishScale, spinnerTopImageScale * finishScale);
                g->translate(center.x, center.y);
                g->drawImage(skin->i_spinner_top);
            }
            g->popTransform();
        }

        // middle
        if(skin->i_spinner_middle2 != MISSING_TEXTURE) {
            const f32 spinnerMiddle2ImageScale = spinnerScale / (skin->i_spinner_middle2.scale());
            g->setColor(Color(0xffffffff).setA(m_alphaWithoutHidden * alphaMultiplier));

            g->pushTransform();
            {
                g->rotate(m_drawRot);
                g->scale(spinnerMiddle2ImageScale * finishScale, spinnerMiddle2ImageScale * finishScale);
                g->translate(center.x, center.y);
                g->drawImage(skin->i_spinner_middle2);
            }
            g->popTransform();
        }
        if(skin->i_spinner_middle != MISSING_TEXTURE) {
            const f32 spinnerMiddleImageScale = spinnerScale / (skin->i_spinner_middle.scale());
            g->setColor(argb(m_alphaWithoutHidden * alphaMultiplier, 1.f, (1.f * m_percent), (1.f * m_percent)));
            g->pushTransform();
            {
                g->rotate(m_drawRot / 2.0f);  // apparently does not rotate in osu
                g->scale(spinnerMiddleImageScale * finishScale, spinnerMiddleImageScale * finishScale);
                g->translate(center.x, center.y);
                g->drawImage(skin->i_spinner_middle);
            }
            g->popTransform();
        }

        // approach circle
        // TODO: only use when spinner-circle or spinner-top are skinned
        if(!(flags::has<ModFlags::Hidden>(m_pi->getMods().flags)) && m_percent > 0.0f) {
            const f32 spinnerApproachCircleImageScale = (spinnerScale * 2) / (skin->i_spinner_approach_circle.scale());

            // fun fact, peppy removed it: https://osu.ppy.sh/community/forums/topics/100765
            g->setColor(Color(skin->c_spinner_approach_circle).setA(m_alphaWithoutHidden * alphaMultiplier));

            g->pushTransform();
            {
                g->scale(spinnerApproachCircleImageScale * m_percent, spinnerApproachCircleImageScale * m_percent);
                g->translate(center.x, center.y); /* 397.f wtf is this hardcoded number? its completely off */
                g->drawImage(skin->i_spinner_approach_circle);
            }
            g->popTransform();
        }
    }

    // "CLEAR!"
    if(m_ratio >= 1.0f) {
        const f32 spinnerClearImageScale = spinnerScale / (skin->i_spinner_clear.scale());
        g->setColor(Color(0xffffffff).setA(alphaMultiplier));

        g->pushTransform();
        {
            g->scale(spinnerClearImageScale, spinnerClearImageScale);
            g->translate(center.x, 230.f);
            g->drawImage(skin->i_spinner_clear);
        }
        g->popTransform();
    }

    // "SPIN!"
    // TODO: correct scale/positioning
    if(clampedRatio < 0.03f) {
        f32 spinerSpinImageScale = Osu::getImageScale(skin->i_spinner_spin, 80);
        g->setColor(Color(0xffffffff).setA(m_alphaWithoutHidden * alphaMultiplier));

        g->pushTransform();
        {
            g->scale(spinerSpinImageScale, spinerSpinImageScale);
            g->translate(center.x, 582.f);
            g->drawImage(skin->i_spinner_spin);
        }
        g->popTransform();
    }

    // draw RPM
    // TODO: draw spinner-rpm if skinned, x = center - 139px, y = 712px, origin = top left
    if(m_deltaMS < 0) {
        McFont *rpmFont = engine->getDefaultFont();
        const float stringWidth = rpmFont->getStringWidth("RPM: 477");
        g->setColor(Color(0xffffffff)
                        .setA(m_alphaWithoutHidden * m_alphaWithoutHidden * m_alphaWithoutHidden * alphaMultiplier));

        g->pushTransform();
        {
            g->translate(
                (int)(osu->getVirtScreenWidth() / 2 - stringWidth / 2),
                (int)(osu->getVirtScreenHeight() - 5 + (5 + rpmFont->getHeight()) * (1.0f - m_alphaWithoutHidden)));
            g->drawString(rpmFont, fmt::format("RPM: {}", (int)(m_RPM + 0.4f)));
        }
        g->popTransform();
    }
}

void Spinner::update(i32 curPosMS, f64 frameTimeSecs) {
    HitObject::update(curPosMS, frameTimeSecs);

    // stop spinner sound and don't update() while paused
    if(m_pi->isPaused() || !m_pi->isPlaying() || (m_pf && m_pf->hasFailed())) {
        const auto spinner_spinsound = m_pf && m_pf->getSkin() ? m_pf->getSkin()->s_spinner_spin : nullptr;
        if(spinner_spinsound && spinner_spinsound->isPlaying()) {
            soundEngine->stop(spinner_spinsound);
        }
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

        m_rotationsNeeded = GameRules::getSpinnerRotationsForSpeedMultiplier(m_pi, m_durationMS);

        const float DELTA_UPDATE_TIME_MS = (frameTimeSecs * 1000.0f);
        const float AUTO_MULTIPLIER = (1.0f / 20.0f);

        // scale percent calculation
        i32 deltaMS = m_clickTimeMS - (i32)curPosMS;
        m_percent = 1.0f - std::clamp<float>((float)deltaMS / -(float)(m_durationMS), 0.0f, 1.0f);

        // handle auto, mouse spinning movement
        float angleDiff = 0;
        if(flags::any<ModFlags::Autoplay | ModFlags::Autopilot | ModFlags::SpunOut>(m_pi->getMods().flags)) {
            angleDiff = frameTimeSecs * 1000.0f * AUTO_MULTIPLIER * m_pi->getSpeedMultiplier();
        } else {  // user spin
            vec2 mouseDelta = m_pi->getCursorPos() - m_pi->osuCoords2Pixels(m_rawPos);
            const auto currentMouseAngle = (float)std::atan2(mouseDelta.y, mouseDelta.x);
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
                m_pi->isClickHeld() ||
                flags::any<ModFlags::Autoplay | ModFlags::Relax | ModFlags::SpunOut>(m_pi->getMods().flags);

            m_deltaOverflowMS += frameTimeSecs * 1000.0f;

            if(angleDiff < -PI)
                angleDiff += 2 * PI;
            else if(angleDiff > PI)
                angleDiff -= 2 * PI;

            if(isSpinning) m_deltaAngleOverflow += angleDiff;

            while(m_deltaOverflowMS >= DELTA_UPDATE_TIME_MS) {
                // spin caused by the cursor
                float deltaAngle = 0;
                if(isSpinning) {
                    deltaAngle = m_deltaAngleOverflow * DELTA_UPDATE_TIME_MS / m_deltaOverflowMS;
                    m_deltaAngleOverflow -= deltaAngle;
                    // deltaAngle = std::clamp<float>(deltaAngle, -MAX_ANG_DIFF, MAX_ANG_DIFF);
                }

                m_deltaOverflowMS -= DELTA_UPDATE_TIME_MS;

                m_sumDeltaAngle -= m_storedDeltaAngles[m_deltaAngleIndex];
                m_sumDeltaAngle += deltaAngle;
                m_storedDeltaAngles[m_deltaAngleIndex++] = deltaAngle;
                m_deltaAngleIndex %= m_maxStoredDeltaAngles;

                float rotationAngle = m_sumDeltaAngle / m_maxStoredDeltaAngles;
                float rotationPerSec = rotationAngle * (1000.0f / DELTA_UPDATE_TIME_MS) / (2.0f * PI);

                f32 decay = std::pow(0.01f, (f32)frameTimeSecs);
                m_RPM = m_RPM * decay + (1.0 - decay) * std::abs(rotationPerSec) * 60;
                m_RPM = std::min(m_RPM, 477.0f);

                if(std::abs(rotationAngle) > 0.0001f) rotate(rotationAngle);
            }

            m_ratio = m_rotations / (m_rotationsNeeded * 360.0f);
        }
    }
}

void Spinner::onReset(i32 curPosMS) {
    HitObject::onReset(curPosMS);

    {
        const auto spinner_spinsound = m_pf && m_pf->getSkin() ? m_pf->getSkin()->s_spinner_spin : nullptr;
        if(spinner_spinsound && spinner_spinsound->isPlaying()) {
            soundEngine->stop(spinner_spinsound);
        }
    }

    m_RPM = 0.0f;
    m_drawRot = 0.0f;
    m_rotations = 0.0f;
    m_deltaOverflowMS = 0.0f;
    m_sumDeltaAngle = 0.0f;
    m_deltaAngleIndex = 0;
    m_deltaAngleOverflow = 0.0f;
    m_ratio = 0.0f;

    // spinners don't need misaims
    m_misAim = true;

    for(int i = 0; i < m_maxStoredDeltaAngles; i++) {
        m_storedDeltaAngles[i] = 0.0f;
    }

    if(curPosMS > getEndTime())
        m_finished = true;
    else
        m_finished = false;
}

void Spinner::onHit() {
    // calculate hit result
    LiveScore::HIT result = LiveScore::HIT::HIT_NULL;
    if(m_ratio >= 1.0f || (flags::has<ModFlags::Autoplay>(m_pi->getMods().flags)))
        result = LiveScore::HIT::HIT_300;
    else if(m_ratio >= 0.9f && !cv::mod_ming3012.getBool() && !cv::mod_no100s.getBool())
        result = LiveScore::HIT::HIT_100;
    else if(m_ratio >= 0.75f && !cv::mod_no100s.getBool() && !cv::mod_no50s.getBool())
        result = LiveScore::HIT::HIT_50;
    else
        result = LiveScore::HIT::HIT_MISS;

    // sound
    if(m_pf != nullptr && result != LiveScore::HIT::HIT_MISS) {
        const vec2 osuCoords = m_pf->pixels2OsuCoords(m_pf->osuCoords2Pixels(m_rawPos));
        f32 pan = GameRules::osuCoords2Pan(osuCoords.x);
        m_hitSamples.play(pan, 0);
    }

    // add it, and we are finished
    addHitResult(result, 0, m_endOfCombo, m_rawPos, -1.0f, 0.f, /*ignoreOnHitErrorBar=*/true);
    m_finished = true;

    const auto spinner_spinsound = m_pf && m_pf->getSkin() ? m_pf->getSkin()->s_spinner_spin : nullptr;
    if(spinner_spinsound && spinner_spinsound->isPlaying()) {
        soundEngine->stop(spinner_spinsound);
    }
}

void Spinner::rotate(float rad) {
    m_drawRot += glm::degrees(rad);

    rad = std::abs(rad);
    const float newRotations = m_rotations + glm::degrees(rad);

    // added one whole rotation
    if(std::floor(newRotations / 360.0f) > m_rotations / 360.0f) {
        if((int)(newRotations / 360.0f) > (int)(m_rotationsNeeded) + 1) {
            // extra rotations and bonus sound
            if(m_pf != nullptr && !m_pf->bWasSeekFrame && m_pf->getSkin()->s_spinner_bonus) {
                soundEngine->play(m_pf->getSkin()->s_spinner_bonus);
            }
            m_pi->addHitResult(this, LiveScore::HIT::HIT_SPINNERBONUS, 0, false, true, true, true, true,
                               false);  // only increase health
            m_pi->addHitResult(this, LiveScore::HIT::HIT_SPINNERBONUS, 0, false, true, true, true, true,
                               false);  // HACKHACK: compensating for rotation logic differences
            m_pi->addScorePoints(1100, true);
        } else {
            // normal whole rotation
            m_pi->addHitResult(this, LiveScore::HIT::HIT_SPINNERSPIN, 0, false, true, true, true, true,
                               false);  // only increase health
            m_pi->addHitResult(this, LiveScore::HIT::HIT_SPINNERSPIN, 0, false, true, true, true, true,
                               false);  // HACKHACK: compensating for rotation logic differences
            m_pi->addScorePoints(100, true);
        }
    }

    // spinner sound
    if(m_pf != nullptr && !m_pf->bWasSeekFrame) {
        const Skin *skin = m_pf->getSkin();
        Sound *spinner_spinsound = skin ? skin->s_spinner_spin : nullptr;
        if(spinner_spinsound) {
            if(!spinner_spinsound->isPlaying()) {
                soundEngine->play(spinner_spinsound);
            }
            if(skin->o_spinner_frequency_modulate) {
                const float frequency = 20000.0f + (int)(std::clamp<float>(m_ratio, 0.0f, 2.5f) * 40000.0f);
                spinner_spinsound->setFrequency(frequency);
            } else {
                // sanity reset
                spinner_spinsound->setFrequency(0);
            }
        }
    }

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

    vec2 actualPos = m_pi->osuCoords2Pixels(m_rawPos);
    const float AUTO_MULTIPLIER = (1.0f / 20.0f);
    float multiplier =
        flags::any<ModFlags::Autoplay | ModFlags::Autopilot>(m_pi->getMods().flags) ? AUTO_MULTIPLIER : 1.0f;
    float angle = (deltaMS * multiplier) - PI / 2.0f;
    float r = GameRules::getPlayfieldSize().y / 10.0f;  // XXX: slow?
    return vec2((float)(actualPos.x + r * std::cos(angle)), (float)(actualPos.y + r * std::sin(angle)));
}
