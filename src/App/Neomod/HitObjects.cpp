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
                doScaleOrRotateAnim = skin->i_hit0->getNumImages() == 1;
                hitImageScale = (rawHitcircleDiameter / skin->i_hit0->getSizeBaseRaw().x) * osuCoordScaleMultiplier;
                break;

            case HIT_50:
                doScaleOrRotateAnim = skin->i_hit50->getNumImages() == 1;
                hasParticle = skin->i_particle50 != MISSING_TEXTURE;
                hitImageScale = (rawHitcircleDiameter / skin->i_hit50->getSizeBaseRaw().x) * osuCoordScaleMultiplier;
                break;

            case HIT_100:
                doScaleOrRotateAnim = skin->i_hit100->getNumImages() == 1;
                hasParticle = skin->i_particle100 != MISSING_TEXTURE;
                hitImageScale = (rawHitcircleDiameter / skin->i_hit100->getSizeBaseRaw().x) * osuCoordScaleMultiplier;
                break;

            case HIT_300:
                doScaleOrRotateAnim = skin->i_hit300->getNumImages() == 1;
                hasParticle = skin->i_particle300 != MISSING_TEXTURE;
                hitImageScale = (rawHitcircleDiameter / skin->i_hit300->getSizeBaseRaw().x) * osuCoordScaleMultiplier;
                break;

            case HIT_100K:
                doScaleOrRotateAnim = skin->i_hit100k->getNumImages() == 1;
                hasParticle = skin->i_particle100 != MISSING_TEXTURE;
                hitImageScale = (rawHitcircleDiameter / skin->i_hit100k->getSizeBaseRaw().x) * osuCoordScaleMultiplier;
                break;

            case HIT_300K:
                doScaleOrRotateAnim = skin->i_hit300k->getNumImages() == 1;
                hasParticle = skin->i_particle300 != MISSING_TEXTURE;
                hitImageScale = (rawHitcircleDiameter / skin->i_hit300k->getSizeBaseRaw().x) * osuCoordScaleMultiplier;
                break;

            case HIT_300G:
                doScaleOrRotateAnim = skin->i_hit300g->getNumImages() == 1;
                hasParticle = skin->i_particle300 != MISSING_TEXTURE;
                hitImageScale = (rawHitcircleDiameter / skin->i_hit300g->getSizeBaseRaw().x) * osuCoordScaleMultiplier;
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
                if(skin->i_hit0->getNumImages() < 2 && skin->version > 1.0f)
                    downAnim.y = std::lerp(-5.0f, 40.0f,
                                           std::clamp<float>(animPercent * animPercent * animPercent, 0.0f, 1.0f)) *
                                 osuCoordScaleMultiplier;

                float missScale = 1.0f + std::clamp<float>((1.0f - (animPercent / fadeInEndPercent)), 0.0f, 1.0f) *
                                             (cv::hitresult_miss_fadein_scale.getFloat() - 1.0f);
                if(!cv::hitresult_animated.getBool()) missScale = 1.0f;

                // TODO: rotation anim (only for all non-animated skins), rot = rng(-0.15f, 0.15f), anim1 = 120 ms to
                // rot, anim2 = rest to rot*2, all ease in

                skin->i_hit0->drawRaw(rawPos + downAnim, (doScaleOrRotateAnim ? missScale : 1.0f) * hitImageScale *
                                                             cv::hitresult_scale.getFloat());
            } break;

            case HIT_50:
                skin->i_hit50->drawRaw(
                    rawPos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                break;

            case HIT_100:
                skin->i_hit100->drawRaw(
                    rawPos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                break;

            case HIT_300:
                if(cv::hitresult_draw_300s.getBool()) {
                    skin->i_hit300->drawRaw(
                        rawPos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                }
                break;

            case HIT_100K:
                skin->i_hit100k->drawRaw(
                    rawPos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                break;

            case HIT_300K:
                if(cv::hitresult_draw_300s.getBool()) {
                    skin->i_hit300k->drawRaw(
                        rawPos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                }
                break;

            case HIT_300G:
                if(cv::hitresult_draw_300s.getBool()) {
                    skin->i_hit300g->drawRaw(
                        rawPos, (doScaleOrRotateAnim ? scale : 1.0f) * hitImageScale * cv::hitresult_scale.getFloat());
                }
                break;

            default:
                break;
        }
    }
    g->popTransform();
}

HitObject::HitObject(i32 time, HitSamples samples, int comboNumber, bool isEndOfCombo, int colorCounter,
                     int colorOffset, AbstractBeatmapInterface *pi)
    : click_time(time),
      combo_number(comboNumber),
      pi(pi),
      pf(dynamic_cast<BeatmapInterface *>(pi)),  // should be NULL if SimulatedBeatmapInterface
      samples(std::move(samples)),
      iColorCounter(colorCounter),
      iColorOffset(colorOffset),
      is_end_of_combo(isEndOfCombo) {}

void HitObject::draw2() {
    this->drawHitResultAnim(this->hitresultanim1);
    this->drawHitResultAnim(this->hitresultanim2);
}

void HitObject::drawHitResultAnim(const HITRESULTANIM &hitresultanim) {
    if((hitresultanim.time - cv::hitresult_duration.getFloat()) <
           engine->getTime()  // NOTE: this is written like that on purpose, don't change it ("future" results can be
                              // scheduled with it, e.g. for slider end)
       && (hitresultanim.time + cv::hitresult_duration_max.getFloat() * (1.0f / this->pf->getBaseAnimationSpeed())) >
              engine->getTime()) {
        const auto &skin = this->pf->getSkin();
        {
            const i32 skinAnimationTimeStartOffset =
                this->click_time +
                (hitresultanim.addObjectDurationToSkinAnimationTimeStartOffset ? this->duration : 0) +
                hitresultanim.delta;

            skin->i_hit0->setAnimationTimeOffset(skinAnimationTimeStartOffset);
            skin->i_hit0->setAnimationFrameClampUp();
            skin->i_hit50->setAnimationTimeOffset(skinAnimationTimeStartOffset);
            skin->i_hit50->setAnimationFrameClampUp();
            skin->i_hit100->setAnimationTimeOffset(skinAnimationTimeStartOffset);
            skin->i_hit100->setAnimationFrameClampUp();
            skin->i_hit100k->setAnimationTimeOffset(skinAnimationTimeStartOffset);
            skin->i_hit100k->setAnimationFrameClampUp();
            skin->i_hit300->setAnimationTimeOffset(skinAnimationTimeStartOffset);
            skin->i_hit300->setAnimationFrameClampUp();
            skin->i_hit300g->setAnimationTimeOffset(skinAnimationTimeStartOffset);
            skin->i_hit300g->setAnimationFrameClampUp();
            skin->i_hit300k->setAnimationTimeOffset(skinAnimationTimeStartOffset);
            skin->i_hit300k->setAnimationFrameClampUp();

            const float animPercentInv =
                1.0f - (((engine->getTime() - hitresultanim.time) * this->pf->getBaseAnimationSpeed()) /
                        cv::hitresult_duration.getFloat());

            drawHitResult(this->pf, this->pf->osuCoords2Pixels(hitresultanim.rawPos), hitresultanim.result,
                          animPercentInv,
                          std::clamp<float>((float)hitresultanim.delta / this->pi->getHitWindow50(), -1.0f, 1.0f));
        }
    }
}

void HitObject::update(i32 curPos, f64 /*frame_time*/) {
    this->fAlphaForApproachCircle = 0.0f;
    this->fHittableDimRGBColorMultiplierPercent = 1.0f;

    const auto mods = this->pi->getMods();

    const double animationSpeedMultiplier = this->pi->getSpeedAdjustedAnimationSpeed();
    this->iFadeInTime = GameRules::getFadeInTime() * animationSpeedMultiplier;
    this->iApproachTime =
        (this->bUseFadeInTimeAsApproachTime ? this->iFadeInTime : (i32)this->pi->getCachedApproachTimeForUpdate());

    this->iDelta = this->click_time - curPos;

    // 1 ms fudge by using >=, shouldn't really be a problem
    if(curPos >= (this->click_time - this->iApproachTime) && curPos < (this->getEndTime())) {
        // approach circle scale
        const float scale = std::clamp<float>((float)this->iDelta / (float)this->iApproachTime, 0.0f, 1.0f);
        this->fApproachScale = 1 + (scale * cv::approach_scale_multiplier.getFloat());
        if(cv::mod_approach_different.getBool()) {
            const float back_const = 1.70158;

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
            this->fApproachScale = 1 + std::lerp(cv::mod_approach_different_initial_size.getFloat() - 1.0f, 0.0f, time);
        }

        // hitobject body fadein
        const i32 fadeInStart = this->click_time - this->iApproachTime;
        const i32 fadeInEnd =
            std::min(this->click_time,
                     this->click_time - this->iApproachTime +
                         this->iFadeInTime);  // std::min() ensures that the fade always finishes at click_time
                                              // (even if the fadeintime is longer than the approachtime)
        this->fAlpha =
            std::clamp<float>(1.0f - ((float)(fadeInEnd - curPos) / (float)(fadeInEnd - fadeInStart)), 0.0f, 1.0f);
        this->fAlphaWithoutHidden = this->fAlpha;

        if(mods.has(ModFlags::Hidden)) {
            // hidden hitobject body fadein
            const float fin_start_percent = cv::mod_hd_circle_fadein_start_percent.getFloat();
            const float fin_end_percent = cv::mod_hd_circle_fadein_end_percent.getFloat();
            const i32 hiddenFadeInStart = this->click_time - (i32)(this->iApproachTime * fin_start_percent);
            const i32 hiddenFadeInEnd = this->click_time - (i32)(this->iApproachTime * fin_end_percent);
            this->fAlpha = std::clamp<float>(
                1.0f - ((float)(hiddenFadeInEnd - curPos) / (float)(hiddenFadeInEnd - hiddenFadeInStart)), 0.0f, 1.0f);

            // hidden hitobject body fadeout
            const float fout_start_percent = cv::mod_hd_circle_fadeout_start_percent.getFloat();
            const float fout_end_percent = cv::mod_hd_circle_fadeout_end_percent.getFloat();
            const i32 hiddenFadeOutStart = this->click_time - (i32)(this->iApproachTime * fout_start_percent);
            const i32 hiddenFadeOutEnd = this->click_time - (i32)(this->iApproachTime * fout_end_percent);
            if(curPos >= hiddenFadeOutStart)
                this->fAlpha = std::clamp<float>(
                    ((float)(hiddenFadeOutEnd - curPos) / (float)(hiddenFadeOutEnd - hiddenFadeOutStart)), 0.0f, 1.0f);
        }

        // approach circle fadein (doubled fadeintime)
        const i32 approachCircleFadeStart = this->click_time - this->iApproachTime;
        const i32 approachCircleFadeEnd =
            std::min(this->click_time,
                     this->click_time - this->iApproachTime +
                         2 * this->iFadeInTime);  // std::min() ensures that the fade always finishes at click_time
                                                  // (even if the fadeintime is longer than the approachtime)
        this->fAlphaForApproachCircle = std::clamp<float>(
            1.0f - ((float)(approachCircleFadeEnd - curPos) / (float)(approachCircleFadeEnd - approachCircleFadeStart)),
            0.0f, 1.0f);

        // hittable dim, see https://github.com/ppy/osu/pull/20572
        if(cv::hitobject_hittable_dim.getBool() &&
           (!flags::has<ModFlags::Mafham>(mods.flags) || !cv::mod_mafham_ignore_hittable_dim.getBool())) {
            const i32 hittableDimFadeStart = this->click_time - (i32)GameRules::getHitWindowMiss();

            // yes, this means the un-dim animation cuts into the already clickable range
            const i32 hittableDimFadeEnd = hittableDimFadeStart + (i32)cv::hitobject_hittable_dim_duration.getInt();

            this->fHittableDimRGBColorMultiplierPercent =
                std::lerp(cv::hitobject_hittable_dim_start_percent.getFloat(), 1.0f,
                          std::clamp<float>(1.0f - (float)(hittableDimFadeEnd - curPos) /
                                                       (float)(hittableDimFadeEnd - hittableDimFadeStart),
                                            0.0f, 1.0f));
        }

        this->bVisible = true;
    } else {
        this->fApproachScale = 1.0f;
        this->bVisible = false;
    }
}

void HitObject::addHitResult(LiveScore::HIT result, i32 delta, bool isEndOfCombo, vec2 posRaw, float targetDelta,
                             float targetAngle, bool ignoreOnHitErrorBar, bool ignoreCombo, bool ignoreHealth,
                             bool addObjectDurationToSkinAnimationTimeStartOffset) {
    if(this->pf != nullptr && this->pi->getMods().has(ModFlags::Target) && result != LiveScore::HIT::HIT_MISS &&
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

    const LiveScore::HIT returnedHit = this->pi->addHitResult(this, result, delta, isEndOfCombo, ignoreOnHitErrorBar,
                                                              false, ignoreCombo, false, ignoreHealth);
    if(this->pf == nullptr) return;

    HITRESULTANIM hitresultanim;
    {
        hitresultanim.result = (returnedHit != LiveScore::HIT::HIT_MISS ? returnedHit : result);
        hitresultanim.rawPos = posRaw;
        hitresultanim.delta = delta;
        hitresultanim.time = engine->getTime();
        hitresultanim.addObjectDurationToSkinAnimationTimeStartOffset = addObjectDurationToSkinAnimationTimeStartOffset;
    }

    // currently a maximum of 2 simultaneous results are supported (for drawing, per hitobject)
    if(engine->getTime() >
       this->hitresultanim1.time + cv::hitresult_duration_max.getFloat() * (1.0f / this->pf->getBaseAnimationSpeed()))
        this->hitresultanim1 = hitresultanim;
    else
        this->hitresultanim2 = hitresultanim;
}

void HitObject::onReset(i32 /*curPos*/) {
    this->bMisAim = false;
    this->iAutopilotDelta = 0;

    this->hitresultanim1.time = -9999.0f;
    this->hitresultanim2.time = -9999.0f;
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
    const float circleOverlayImageScale = hitcircleDiameter / skin->i_hitcircleoverlay->getSizeBaseRaw().x;
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
    const float circleOverlayImageScale = hitcircleDiameter / skin->i_hitcircleoverlay->getSizeBaseRaw().x;
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
    const float circleOverlayImageScale = hitcircleDiameter / skin->i_slider_start_circle_overlay2->getSizeBaseRaw().x;
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
        const float circleOverlayImageScale =
            hitcircleDiameter / skin->i_slider_end_circle_overlay2->getSizeBaseRaw().x;
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

void Circle::drawHitCircleOverlay(SkinImage *hitCircleOverlayImage, vec2 pos, float circleOverlayImageScale,
                                  float alpha, float colorRGBMultiplier) {
    g->setColor(argb(alpha, colorRGBMultiplier, colorRGBMultiplier, colorRGBMultiplier));
    hitCircleOverlayImage->drawRaw(pos, circleOverlayImageScale);
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

Circle::Circle(int x, int y, i32 time, HitSamples samples, int comboNumber, bool isEndOfCombo, int colorCounter,
               int colorOffset, AbstractBeatmapInterface *pi)
    : HitObject(time, std::move(samples), comboNumber, isEndOfCombo, colorCounter, colorOffset, pi),
      vRawPos(x, y),
      vOriginalRawPos(vRawPos) {
    this->type = HitObjectType::CIRCLE;
}

Circle::~Circle() { this->onReset(0); }

void Circle::draw() {
    HitObject::draw();

    const Skin *skin = this->pf->getSkin();
    const bool hd = this->pi->getMods().has(ModFlags::Hidden);

    // draw hit animation
    if(!hd && !cv::instafade.getBool() && this->fHitAnimation > 0.0f && this->fHitAnimation != 1.0f) {
        float alpha = 1.0f - this->fHitAnimation;

        float scale = this->fHitAnimation;
        scale = -scale * (scale - 2.0f);  // quad out scale

        const bool drawNumber = skin->version > 1.0f ? false : true;
        const float foscale = cv::circle_fade_out_scale.getFloat();

        g->pushTransform();
        {
            g->scale((1.0f + scale * foscale), (1.0f + scale * foscale));
            skin->i_hitcircleoverlay->setAnimationTimeOffset(!this->pf->isInMafhamRenderChunk()
                                                                 ? this->click_time - this->iApproachTime
                                                                 : this->pf->getCurMusicPosWithOffsets());
            drawCircle(this->pf, this->vRawPos, this->combo_number, this->iColorCounter, this->iColorOffset, 1.0f, 1.0f,
                       alpha, alpha, drawNumber);
        }
        g->popTransform();
    }

    if(this->bFinished ||
       (!this->bVisible && !this->bWaiting))  // special case needed for when we are past this objects time, but still
                                              // within not-miss range, because we still need to draw the object
        return;

    // draw circle
    vec2 shakeCorrectedPos = this->vRawPos;
    if(engine->getTime() < this->fShakeAnimation && !this->pf->isInMafhamRenderChunk())  // handle note blocking shaking
    {
        float smooth = 1.0f - ((this->fShakeAnimation - engine->getTime()) /
                               cv::circle_shake_duration.getFloat());  // goes from 0 to 1
        if(smooth < 0.5f)
            smooth = smooth / 0.5f;
        else
            smooth = (1.0f - smooth) / 0.5f;
        // (now smooth goes from 0 to 1 to 0 linearly)
        smooth = -smooth * (smooth - 2);  // quad out
        smooth = -smooth * (smooth - 2);  // quad out twice
        shakeCorrectedPos.x += std::sin(engine->getTime() * 120) * smooth * cv::circle_shake_strength.getFloat();
    }
    skin->i_hitcircleoverlay->setAnimationTimeOffset(!this->pf->isInMafhamRenderChunk()
                                                         ? this->click_time - this->iApproachTime
                                                         : this->pf->getCurMusicPosWithOffsets());

    {
        const float approachScale = this->bWaiting && !hd ? 1.0f : this->fApproachScale;
        const float alpha = this->bWaiting && !hd ? 1.0f : this->fAlpha;
        const float numberAlpha = this->bWaiting && !hd ? 1.0f : this->fAlpha;

        drawCircle(this->pf, shakeCorrectedPos, this->combo_number, this->iColorCounter, this->iColorOffset,
                   this->fHittableDimRGBColorMultiplierPercent, approachScale, alpha, numberAlpha, true,
                   this->bOverrideHDApproachCircle);
    }
}

void Circle::draw2() {
    HitObject::draw2();
    if(this->bFinished || (!this->bVisible && !this->bWaiting))
        return;  // special case needed for when we are past this objects time, but still within not-miss range, because
                 // we still need to draw the object

    // draw approach circle
    const bool hd = this->pi->getMods().has(ModFlags::Hidden);

    // HACKHACK: don't fucking change this piece of code here, it fixes a heisenbug
    // (https://github.com/McKay42/McOsu/issues/165)
    if(cv::bug_flicker_log.getBool()) {
        const float approachCircleImageScale =
            this->pf->fHitcircleDiameter / (128.0f * (this->pf->getSkin()->i_approachcircle.scale()));
        debugLog("click_time = {:d}, aScale = {:f}, iScale = {:f}", this->click_time, this->fApproachScale,
                 approachCircleImageScale);
    }

    drawApproachCircle(this->pf, this->vRawPos, this->combo_number, this->iColorCounter, this->iColorOffset,
                       this->fHittableDimRGBColorMultiplierPercent, this->bWaiting && !hd ? 1.0f : this->fApproachScale,
                       this->bWaiting && !hd ? 1.0f : this->fAlphaForApproachCircle, this->bOverrideHDApproachCircle);
}

void Circle::update(i32 curPos, f64 frame_time) {
    HitObject::update(curPos, frame_time);
    if(this->bFinished) return;

    const ModFlags &curIFaceMods = this->pi->getMods().flags;
    const i32 delta = curPos - this->click_time;

    if(flags::has<ModFlags::Autoplay>(curIFaceMods)) {
        if(curPos >= this->click_time) {
            this->onHit(LiveScore::HIT::HIT_300, 0);
        }
        return;
    }

    if(flags::has<ModFlags::Relax>(curIFaceMods)) {
        if(curPos >= this->click_time + (i32)cv::relax_offset.getInt() && !this->pi->isPaused() &&
           !this->pi->isContinueScheduled()) {
            const vec2 pos = this->pi->osuCoords2Pixels(this->vRawPos);
            const float cursorDelta = vec::length(this->pi->getCursorPos() - pos);
            if((cursorDelta < this->pi->fHitcircleDiameter / 2.0f && (flags::has<ModFlags::Relax>(curIFaceMods)))) {
                LiveScore::HIT result = this->pi->getHitResult(delta);

                if(result != LiveScore::HIT::HIT_NULL) {
                    const float targetDelta = cursorDelta / (this->pi->fHitcircleDiameter / 2.0f);
                    const float targetAngle = glm::degrees(
                        std::atan2(this->pi->getCursorPos().y - pos.y, this->pi->getCursorPos().x - pos.x));

                    this->onHit(result, delta, targetDelta, targetAngle);
                }
            }
        }
    }

    if(delta >= 0) {
        this->bWaiting = true;

        // if this is a miss after waiting
        if(delta > (i32)this->pi->getHitWindow50()) {
            this->onHit(LiveScore::HIT::HIT_MISS, delta);
        }
    } else {
        this->bWaiting = false;
    }
}

void Circle::updateStackPosition(float stackOffset) {
    this->vRawPos =
        this->vOriginalRawPos -
        vec2(this->iStack * stackOffset,
             this->iStack * stackOffset * ((flags::has<ModFlags::HardRock>(this->pi->getMods().flags)) ? -1.0f : 1.0f));
}

void Circle::miss(i32 curPos) {
    if(this->bFinished) return;

    const i32 delta = curPos - this->click_time;

    this->onHit(LiveScore::HIT::HIT_MISS, delta);
}

void Circle::onClickEvent(std::vector<Click> &clicks) {
    if(this->bFinished) return;

    const vec2 cursorPos = clicks[0].pos;
    const vec2 pos = this->pi->osuCoords2Pixels(this->vRawPos);
    const float cursorDelta = vec::length(cursorPos - pos);

    if(cursorDelta < this->pi->fHitcircleDiameter / 2.0f) {
        // note blocking & shake
        if(this->bBlocked) {
            this->fShakeAnimation = engine->getTime() + cv::circle_shake_duration.getFloat();
            return;  // ignore click event completely
        }

        const i32 delta = clicks[0].music_pos - (i32)this->click_time;

        LiveScore::HIT result = this->pi->getHitResult(delta);
        if(result != LiveScore::HIT::HIT_NULL) {
            const float targetDelta = cursorDelta / (this->pi->fHitcircleDiameter / 2.0f);
            const float targetAngle = glm::degrees(std::atan2(cursorPos.y - pos.y, cursorPos.x - pos.x));

            clicks.erase(clicks.begin());
            this->onHit(result, delta, targetDelta, targetAngle);
        }
    }
}

void Circle::onHit(LiveScore::HIT result, i32 delta, float targetDelta, float targetAngle) {
    // sound and hit animation
    if(this->pf != nullptr && result != LiveScore::HIT::HIT_MISS) {
        const vec2 osuCoords = this->pf->pixels2OsuCoords(this->pf->osuCoords2Pixels(this->vRawPos));
        f32 pan = GameRules::osuCoords2Pan(osuCoords.x);
        this->samples.play(pan, delta, this->click_time);

        this->fHitAnimation = 0.001f;  // quickfix for 1 frame missing images
        anim::moveQuadOut(&this->fHitAnimation, 1.0f, GameRules::getFadeOutTime(this->pi->getBaseAnimationSpeed()),
                          true);
    }

    // add it, and we are finished
    this->addHitResult(result, delta, this->is_end_of_combo, this->vRawPos, targetDelta, targetAngle);
    this->bFinished = true;
}

void Circle::onReset(i32 curPos) {
    HitObject::onReset(curPos);

    this->bWaiting = false;
    this->fShakeAnimation = 0.0f;

    if(this->pf != nullptr) {
        anim::deleteExistingAnimation(&this->fHitAnimation);
    }

    if(this->click_time > curPos) {
        this->bFinished = false;
        this->fHitAnimation = 0.0f;
    } else {
        this->bFinished = true;
        this->fHitAnimation = 1.0f;
    }
}

vec2 Circle::getAutoCursorPos(i32 /*curPos*/) const { return this->pi->osuCoords2Pixels(this->vRawPos); }

Slider::Slider(SLIDERCURVETYPE stype, int repeat, float pixelLength, std::vector<vec2> points,
               const std::vector<float> &ticks, float sliderTime, float sliderTimeWithoutRepeats, i32 time,
               HitSamples hoverSamples, std::vector<HitSamples> edgeSamples, int comboNumber, bool isEndOfCombo,
               int colorCounter, int colorOffset, AbstractBeatmapInterface *pi)
    : HitObject(time, std::move(hoverSamples), comboNumber, isEndOfCombo, colorCounter, colorOffset, pi),
      points(std::move(points)),
      edgeSamples(std::move(edgeSamples)),
      fPixelLength(std::abs(pixelLength)),
      fSliderTime(sliderTime),
      fSliderTimeWithoutRepeats(sliderTimeWithoutRepeats),
      iRepeat(repeat),
      cType(stype) {
    this->type = HitObjectType::SLIDER;

    // build raw ticks
    for(float tick : ticks) {
        this->ticks.emplace_back(SLIDERTICK{.percent = tick, .finished = false});
    }

    // build curve
    this->curve = SliderCurve::createCurve(this->cType, this->points, this->fPixelLength);

    // build repeats
    for(int i = 0; i < (this->iRepeat - 1); i++) {
        this->clicks.emplace_back(SLIDERCLICK{
            .time = this->click_time + (i32)(this->fSliderTimeWithoutRepeats * (i + 1)),
            .type = 0,
            .tickIndex = 0,
            .finished = false,
            .successful = false,
            .sliderend = ((i % 2) == 0),  // for hit animation on repeat hit
        });
    }

    // build ticks
    for(int i = 0; i < this->iRepeat; i++) {
        for(int t = 0; t < this->ticks.size(); t++) {
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
                (((i + 1) % 2) != 0 ? this->ticks[t].percent : 1.0f - this->ticks[t].percent);

            this->clicks.emplace_back(
                SLIDERCLICK{.time = this->click_time + (i32)(this->fSliderTimeWithoutRepeats * i) +
                                    (i32)(tickPercentRelativeToRepeatFromStartAbs * this->fSliderTimeWithoutRepeats),
                            .type = 1,
                            .tickIndex = t,
                            .finished = false,
                            .successful = false,
                            .sliderend = false});
        }
    }

    this->duration = (i32)this->fSliderTime;
    this->duration = this->duration >= 0 ? this->duration : 1;  // force clamp to positive range
}

void Slider::draw() {
    if(this->points.size() <= 0) return;

    const float foscale = cv::circle_fade_out_scale.getFloat();
    const Skin *skin = this->pf->getSkin();

    const ModFlags &curGameplayFlags = this->pf->getMods().flags;

    const bool hd = flags::has<ModFlags::Hidden>(curGameplayFlags);

    const bool isCompletelyFinished = this->bStartFinished && this->bEndFinished && this->bFinished;
    if((this->bVisible || (this->bStartFinished && !this->bFinished)) &&
       !isCompletelyFinished)  // extra possibility to avoid flicker between HitObject::m_bVisible delay and the
                               // fadeout animation below this if block
    {
        const float alpha = (cv::mod_hd_slider_fast_fade.getBool() ? this->fAlpha : this->fBodyAlpha);
        float sliderSnake = cv::snaking_sliders.getBool() ? this->fSliderSnakePercent : 1.0f;

        // shrinking sliders
        float sliderSnakeStart = 0.0f;
        if(cv::slider_shrink.getBool() && this->iReverseArrowPos == 0) {
            sliderSnakeStart = (this->bInReverse ? 0.0f : this->fSlidePercent);
            if(this->bInReverse) sliderSnake = this->fSlidePercent;
        }

        // draw slider body
        if(alpha > 0.0f && cv::slider_draw_body.getBool()) this->drawBody(alpha, sliderSnakeStart, sliderSnake);

        // draw slider ticks
        Color tickColor = 0xffffffff;
        tickColor = Colors::scale(tickColor, this->fHittableDimRGBColorMultiplierPercent);
        const float tickImageScale =
            (this->pf->fHitcircleDiameter / (16.0f * (skin->i_slider_score_point.scale()))) * 0.125f;
        for(auto &tick : this->ticks) {
            if(tick.finished || tick.percent > sliderSnake) continue;

            vec2 pos = this->pf->osuCoords2Pixels(this->curve->pointAt(tick.percent));

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
        if(this->points.size() > 1) {
            // HACKHACK: very dirty code
            bool sliderRepeatStartCircleFinished = (this->iRepeat < 2);
            bool sliderRepeatEndCircleFinished = false;
            bool endCircleIsAtActualSliderEnd = true;
            for(auto &click : this->clicks) {
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
                (!cv::mod_strict_tracking.getBool() || this->endResult != LiveScore::HIT::HIT_MISS);

            {
                const bool draw_end =
                    ((!this->bEndFinished && this->iRepeat % 2 != 0 && ifStrictTrackingModShouldDrawEndCircle) ||
                     (!sliderRepeatEndCircleFinished &&
                      (ifStrictTrackingModShouldDrawEndCircle || (this->iRepeat > 1 && endCircleIsAtActualSliderEnd) ||
                       (this->iRepeat > 1 && std::abs(this->iRepeat - this->iCurRepeat) > 2))));

                const bool draw_start =
                    (!this->bStartFinished ||
                     (!sliderRepeatStartCircleFinished &&
                      (ifStrictTrackingModShouldDrawEndCircle || (this->iRepeat > 1 && !endCircleIsAtActualSliderEnd) ||
                       (this->iRepeat > 1 && std::abs(this->iRepeat - this->iCurRepeat) > 2))) ||
                     (!this->bEndFinished && this->iRepeat % 2 == 0 && ifStrictTrackingModShouldDrawEndCircle));

                const float circle_alpha = this->fAlpha;

                // end circle
                if(draw_end) this->drawEndCircle(circle_alpha, sliderSnake);

                // start circle
                if(draw_start) this->drawStartCircle(circle_alpha);
            }
            // reverse arrows
            if(this->fReverseArrowAlpha > 0.0f) {
                // if the combo color is nearly white, blacken the reverse arrow
                Color comboColor = skin->getComboColorForCounter(this->iColorCounter, this->iColorOffset);
                Color reverseArrowColor = 0xffffffff;
                if((comboColor.Rf() + comboColor.Gf() + comboColor.Bf()) / 3.0f >
                   cv::slider_reverse_arrow_black_threshold.getFloat())
                    reverseArrowColor = 0xff000000;

                reverseArrowColor = Colors::scale(reverseArrowColor, this->fHittableDimRGBColorMultiplierPercent);

                float div = 0.30f;
                float pulse = (div - std::fmod(std::abs(this->pf->getCurMusicPos()) / 1000.0f, div)) / div;
                pulse *= pulse;  // quad in

                if(!cv::slider_reverse_arrow_animated.getBool() || this->pf->isInMafhamRenderChunk()) pulse = 0.0f;

                // end
                if(this->iReverseArrowPos == 2 || this->iReverseArrowPos == 3) {
                    vec2 pos = this->pf->osuCoords2Pixels(this->curve->pointAt(1.0f));
                    float rotation = this->curve->getEndAngle() - cv::playfield_rotation.getFloat() -
                                     this->pf->getPlayfieldRotation();
                    if((flags::has<ModFlags::HardRock>(curGameplayFlags))) rotation = 360.0f - rotation;
                    if(cv::playfield_mirror_horizontal.getBool()) rotation = 360.0f - rotation;
                    if(cv::playfield_mirror_vertical.getBool()) rotation = 180.0f - rotation;

                    const float osuCoordScaleMultiplier =
                        this->pf->fHitcircleDiameter / this->pf->fRawHitcircleDiameter;
                    float reverseArrowImageScale =
                        (this->pf->fRawHitcircleDiameter / (128.0f * (skin->i_reversearrow.scale()))) *
                        osuCoordScaleMultiplier;

                    reverseArrowImageScale *= 1.0f + pulse * 0.30f;

                    g->setColor(Color(reverseArrowColor).setA(this->fReverseArrowAlpha));

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
                if(this->iReverseArrowPos == 1 || this->iReverseArrowPos == 3) {
                    vec2 pos = this->pf->osuCoords2Pixels(this->curve->pointAt(0.0f));
                    float rotation = this->curve->getStartAngle() - cv::playfield_rotation.getFloat() -
                                     this->pf->getPlayfieldRotation();
                    if((flags::has<ModFlags::HardRock>(curGameplayFlags))) rotation = 360.0f - rotation;
                    if(cv::playfield_mirror_horizontal.getBool()) rotation = 360.0f - rotation;
                    if(cv::playfield_mirror_vertical.getBool()) rotation = 180.0f - rotation;

                    const float osuCoordScaleMultiplier =
                        this->pf->fHitcircleDiameter / this->pf->fRawHitcircleDiameter;
                    float reverseArrowImageScale =
                        (this->pf->fRawHitcircleDiameter / (128.0f * (skin->i_reversearrow.scale()))) *
                        osuCoordScaleMultiplier;

                    reverseArrowImageScale *= 1.0f + pulse * 0.30f;

                    g->setColor(Color(reverseArrowColor).setA(this->fReverseArrowAlpha));

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

    const bool slider_fading_out =
        this->fEndSliderBodyFadeAnimation > 0.0f && this->fEndSliderBodyFadeAnimation != 1.0f;

    if(!hd && !instafade_slider_body && slider_fading_out) {
        std::vector<vec2> emptyVector;
        std::vector<vec2> alwaysPoints;
        alwaysPoints.push_back(this->pf->osuCoords2Pixels(this->curve->pointAt(this->fSlidePercent)));
        if(!cv::slider_shrink.getBool())
            this->drawBody(1.0f - this->fEndSliderBodyFadeAnimation, 0, 1);
        else if(cv::slider_body_lazer_fadeout_style.getBool())
            SliderRenderer::draw(emptyVector, alwaysPoints, this->pf->fHitcircleDiameter, 0.0f, 0.0f,
                                 this->pf->getSkin()->getComboColorForCounter(this->iColorCounter, this->iColorOffset),
                                 1.0f, 1.0f - this->fEndSliderBodyFadeAnimation, this->click_time);
    }

    const bool do_endhit_animations = !hd && !instafade_slider_head;
    const bool do_starthit_animations = do_endhit_animations && cv::slider_sliderhead_fadeout.getBool();
    for(auto anim_it = this->clickAnimations.begin(); anim_it != this->clickAnimations.end();) {
        if(!anim_it->isAnimating()) {
            anim_it = this->clickAnimations.erase(anim_it);
            continue;
        }
        auto &anim = *anim_it;
        ++anim_it;

        if(do_starthit_animations && anim.type & HitAnim::HEAD) {
            const float alpha = 1.0f - anim.percent;
            const float number_alpha = alpha;

            float scale = anim.percent;
            scale = -scale * (scale - 2.0f);  // quad out scale

            const bool drawNumber = (skin->version > 1.0f ? false : true) && this->iCurRepeat < 1;

            g->pushTransform();
            {
                g->scale((1.0f + scale * foscale), (1.0f + scale * foscale));
                if(this->iCurRepeat < 1) {
                    skin->i_hitcircleoverlay->setAnimationTimeOffset(!this->pf->isInMafhamRenderChunk()
                                                                         ? this->click_time - this->iApproachTime
                                                                         : this->pf->getCurMusicPosWithOffsets());
                    skin->i_slider_start_circle_overlay2->setAnimationTimeOffset(
                        !this->pf->isInMafhamRenderChunk() ? this->click_time - this->iApproachTime
                                                           : this->pf->getCurMusicPosWithOffsets());

                    Circle::drawSliderStartCircle(this->pf, this->curve->pointAt(0.0f), this->combo_number,
                                                  this->iColorCounter, this->iColorOffset, 1.0f, 1.0f, alpha,
                                                  number_alpha, drawNumber);
                } else {
                    skin->i_hitcircleoverlay->setAnimationTimeOffset(
                        !this->pf->isInMafhamRenderChunk() ? this->click_time : this->pf->getCurMusicPosWithOffsets());
                    skin->i_slider_end_circle_overlay2->setAnimationTimeOffset(
                        !this->pf->isInMafhamRenderChunk() ? this->click_time : this->pf->getCurMusicPosWithOffsets());

                    Circle::drawSliderEndCircle(this->pf, this->curve->pointAt(0.0f), this->combo_number,
                                                this->iColorCounter, this->iColorOffset, 1.0f, 1.0f, alpha, alpha,
                                                drawNumber);
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
                    skin->i_hitcircleoverlay->setAnimationTimeOffset(!this->pf->isInMafhamRenderChunk()
                                                                         ? this->click_time - this->iFadeInTime
                                                                         : this->pf->getCurMusicPosWithOffsets());
                    skin->i_slider_end_circle_overlay2->setAnimationTimeOffset(
                        !this->pf->isInMafhamRenderChunk() ? this->click_time - this->iFadeInTime
                                                           : this->pf->getCurMusicPosWithOffsets());

                    Circle::drawSliderEndCircle(this->pf, this->curve->pointAt(1.0f), this->combo_number,
                                                this->iColorCounter, this->iColorOffset, 1.0f, 1.0f, alpha, 0.0f,
                                                false);
                }
            }
            g->popTransform();
        }
    }

    HitObject::draw();
}

void Slider::draw2(bool drawApproachCircle, bool drawOnlyApproachCircle) {
    HitObject::draw2();

    const Skin *skin = this->pf->getSkin();

    // HACKHACK: so much code duplication aaaaaaah
    if((this->bVisible || (this->bStartFinished && !this->bFinished)) &&
       drawApproachCircle)  // extra possibility to avoid flicker between HitObject::m_bVisible delay and the fadeout
                            // animation below this if block
    {
        if(this->points.size() > 1) {
            // HACKHACK: very dirty code
            bool sliderRepeatStartCircleFinished = this->iRepeat < 2;
            for(auto &click : this->clicks) {
                if(click.type == 0) {
                    if(!click.sliderend) sliderRepeatStartCircleFinished = click.finished;
                }
            }

            // start circle
            if(!this->bStartFinished || !sliderRepeatStartCircleFinished ||
               (!this->bEndFinished && this->iRepeat % 2 == 0)) {
                Circle::drawApproachCircle(this->pf, this->curve->pointAt(0.0f), this->combo_number,
                                           this->iColorCounter, this->iColorOffset,
                                           this->fHittableDimRGBColorMultiplierPercent, this->fApproachScale,
                                           this->fAlphaForApproachCircle, this->bOverrideHDApproachCircle);
            }
        }
    }

    if(drawApproachCircle && drawOnlyApproachCircle) return;

    const ModFlags &curGameplayFlags = this->pf->getMods().flags;

    // draw followcircle
    // HACKHACK: this is not entirely correct (due to m_bHeldTillEnd, if held within 300 range but then released, will
    // flash followcircle at the end)
    bool is_holding_click = this->isClickHeldSlider();
    is_holding_click |= flags::any<ModFlags::Autoplay | ModFlags::Relax>(curGameplayFlags);

    bool should_draw_followcircle = (this->bVisible && this->bCursorInside && is_holding_click);
    should_draw_followcircle |= (this->bFinished && this->fFollowCircleAnimationAlpha > 0.0f && this->bHeldTillEnd);

    if(should_draw_followcircle) {
        vec2 point = this->pf->osuCoords2Pixels(this->vCurPointRaw);

        // HACKHACK: this is shit
        float tickAnimation =
            (this->fFollowCircleTickAnimationScale < 0.1f ? this->fFollowCircleTickAnimationScale / 0.1f
                                                          : (1.0f - this->fFollowCircleTickAnimationScale) / 0.9f);
        if(this->fFollowCircleTickAnimationScale < 0.1f) {
            tickAnimation = -tickAnimation * (tickAnimation - 2.0f);
            tickAnimation = std::clamp<float>(tickAnimation / 0.02f, 0.0f, 1.0f);
        }
        float tickAnimationScale = 1.0f + tickAnimation * cv::slider_followcircle_tick_pulse_scale.getFloat();

        g->setColor(Color(0xffffffff).setA(this->fFollowCircleAnimationAlpha));

        skin->i_slider_follow_circle->setAnimationTimeOffset(this->click_time);
        skin->i_slider_follow_circle->drawRaw(
            point,
            (this->pf->fSliderFollowCircleDiameter / skin->i_slider_follow_circle->getSizeBaseRaw().x) *
                tickAnimationScale * this->fFollowCircleAnimationScale *
                0.85f);  // this is a bit strange, but seems to work perfectly with 0.85
    }

    const bool isCompletelyFinished = this->bStartFinished && this->bEndFinished && this->bFinished;

    // draw sliderb on top of everything
    if((this->bVisible || (this->bStartFinished && !this->bFinished)) &&
       !isCompletelyFinished)  // extra possibility in the if-block to avoid flicker between HitObject::m_bVisible
                               // delay and the fadeout animation below this if-block
    {
        if(this->fSlidePercent > 0.0f) {
            // draw sliderb
            vec2 point = this->pf->osuCoords2Pixels(this->vCurPointRaw);
            vec2 c1 = this->pf->osuCoords2Pixels(this->curve->pointAt(
                this->fSlidePercent + 0.01f <= 1.0f ? this->fSlidePercent : this->fSlidePercent - 0.01f));
            vec2 c2 = this->pf->osuCoords2Pixels(this->curve->pointAt(
                this->fSlidePercent + 0.01f <= 1.0f ? this->fSlidePercent + 0.01f : this->fSlidePercent));
            float ballAngle = glm::degrees(std::atan2(c2.y - c1.y, c2.x - c1.x));
            if(skin->o_sliderball_flip) ballAngle += (this->iCurRepeat % 2 == 0) ? 0 : 180;

            g->setColor(skin->o_allow_sliderball_tint
                            ? (cv::slider_ball_tint_combo_color.getBool()
                                   ? skin->getComboColorForCounter(this->iColorCounter, this->iColorOffset)
                                   : skin->c_slider_ball)
                            : rgb(255, 255, 255));
            g->pushTransform();
            {
                g->rotate(ballAngle);
                skin->i_sliderb->setAnimationTimeOffset(this->click_time);
                skin->i_sliderb->drawRaw(point, this->pf->fHitcircleDiameter / skin->i_sliderb->getSizeBaseRaw().x);
            }
            g->popTransform();
        }
    }
}

void Slider::drawStartCircle(float alpha) {
    const Skin *skin = this->pf->getSkin();

    if(this->bStartFinished) {
        skin->i_hitcircleoverlay->setAnimationTimeOffset(
            !this->pf->isInMafhamRenderChunk() ? this->click_time : this->pf->getCurMusicPosWithOffsets());
        skin->i_slider_end_circle_overlay2->setAnimationTimeOffset(
            !this->pf->isInMafhamRenderChunk() ? this->click_time : this->pf->getCurMusicPosWithOffsets());

        Circle::drawSliderEndCircle(this->pf, this->curve->pointAt(0.0f), this->combo_number, this->iColorCounter,
                                    this->iColorOffset, this->fHittableDimRGBColorMultiplierPercent, 1.0f, alpha, 0.0f,
                                    false, false);
    } else {
        skin->i_hitcircleoverlay->setAnimationTimeOffset(!this->pf->isInMafhamRenderChunk()
                                                             ? this->click_time - this->iApproachTime
                                                             : this->pf->getCurMusicPosWithOffsets());
        skin->i_slider_start_circle_overlay2->setAnimationTimeOffset(!this->pf->isInMafhamRenderChunk()
                                                                         ? this->click_time - this->iApproachTime
                                                                         : this->pf->getCurMusicPosWithOffsets());

        Circle::drawSliderStartCircle(this->pf, this->curve->pointAt(0.0f), this->combo_number, this->iColorCounter,
                                      this->iColorOffset, this->fHittableDimRGBColorMultiplierPercent,
                                      this->fApproachScale, alpha, alpha, !this->bHideNumberAfterFirstRepeatHit,
                                      this->bOverrideHDApproachCircle);
    }
}

void Slider::drawEndCircle(float alpha, float sliderSnake) {
    const Skin *skin = this->pf->getSkin();

    skin->i_hitcircleoverlay->setAnimationTimeOffset(!this->pf->isInMafhamRenderChunk()
                                                         ? this->click_time - this->iFadeInTime
                                                         : this->pf->getCurMusicPosWithOffsets());
    skin->i_slider_end_circle_overlay2->setAnimationTimeOffset(!this->pf->isInMafhamRenderChunk()
                                                                   ? this->click_time - this->iFadeInTime
                                                                   : this->pf->getCurMusicPosWithOffsets());

    Circle::drawSliderEndCircle(this->pf, this->curve->pointAt(sliderSnake), this->combo_number, this->iColorCounter,
                                this->iColorOffset, this->fHittableDimRGBColorMultiplierPercent, 1.0f, alpha, 0.0f,
                                false, false);
}

void Slider::drawBody(float alpha, float from, float to) {
    // smooth begin/end while snaking/shrinking
    std::vector<vec2> alwaysPoints;
    if(cv::slider_body_smoothsnake.getBool()) {
        if(cv::slider_shrink.getBool() && this->fSliderSnakePercent > 0.999f) {
            alwaysPoints.push_back(this->pf->osuCoords2Pixels(this->curve->pointAt(this->fSlidePercent)));  // curpoint
            alwaysPoints.push_back(this->pf->osuCoords2Pixels(
                this->getRawPosAt(this->getEndTime() + 1)));  // endpoint (because setDrawPercent() causes the last
                                                              // circle mesh to become invisible too quickly)
        }
        if(cv::snaking_sliders.getBool() && this->fSliderSnakePercent < 1.0f)
            alwaysPoints.push_back(this->pf->osuCoords2Pixels(
                this->curve->pointAt(this->fSliderSnakePercent)));  // snakeoutpoint (only while snaking out)
    }

    const Color undimmedComboColor =
        this->pf->getSkin()->getComboColorForCounter(this->iColorCounter, this->iColorOffset);

    if(osu->shouldFallBackToLegacySliderRenderer()) {
        std::vector<vec2> screenPoints = this->curve->getPoints();
        for(auto &screenPoint : screenPoints) {
            screenPoint = this->pf->osuCoords2Pixels(screenPoint);
        }

        // peppy sliders
        SliderRenderer::draw(screenPoints, alwaysPoints, this->pf->fHitcircleDiameter, from, to, undimmedComboColor,
                             this->fHittableDimRGBColorMultiplierPercent, alpha, this->click_time);
    } else {
        // vertex buffered sliders
        // as the base mesh is centered at (0, 0, 0) and in raw osu coordinates, we have to scale and translate it to
        // make it fit the actual desktop playfield
        const float scale = GameRules::getPlayfieldScaleFactor();
        vec2 translation = GameRules::getPlayfieldCenter();

        if(this->pf->hasFailed())
            translation =
                this->pf->osuCoords2Pixels(vec2(GameRules::OSU_COORD_WIDTH / 2, GameRules::OSU_COORD_HEIGHT / 2));

        if(cv::mod_fps.getBool()) translation += this->pf->getFirstPersonCursorDelta();

        SliderRenderer::draw(this->vao.get(), alwaysPoints, translation, scale, this->pf->fHitcircleDiameter, from, to,
                             undimmedComboColor, this->fHittableDimRGBColorMultiplierPercent, alpha, this->click_time);
    }
}

void Slider::update(i32 curPos, f64 frame_time) {
    HitObject::update(curPos, frame_time);

    if(this->pf != nullptr) {
        // stop slide sound while paused
        if(this->pf->isPaused() || !this->pf->isPlaying() || this->pf->hasFailed()) {
            this->samples.stop();
        }

        // animations must be updated even if we are finished
        this->updateAnimations(curPos);
    }

    // all further calculations are only done while we are active
    if(this->bFinished) return;

    const ModFlags &curIFaceMods = this->pi->getMods().flags;

    // slider slide percent
    this->fSlidePercent = 0.0f;
    if(curPos > this->click_time)
        this->fSlidePercent = std::clamp<float>(
            std::clamp<i32>((curPos - (this->click_time)), 0, (i32)this->fSliderTime) / this->fSliderTime, 0.0f, 1.0f);

    const float sliderSnakeDuration =
        (1.0f / 3.0f) * this->iApproachTime * cv::slider_snake_duration_multiplier.getFloat();
    this->fSliderSnakePercent =
        std::min(1.0f, (curPos - (this->click_time - this->iApproachTime)) / (sliderSnakeDuration));

    const i32 reverseArrowFadeInStart =
        this->click_time -
        (cv::snaking_sliders.getBool() ? (this->iApproachTime - sliderSnakeDuration) : this->iApproachTime);
    const i32 reverseArrowFadeInEnd = reverseArrowFadeInStart + cv::slider_reverse_arrow_fadein_duration.getInt();
    this->fReverseArrowAlpha = 1.0f - std::clamp<float>(((float)(reverseArrowFadeInEnd - curPos) /
                                                         (float)(reverseArrowFadeInEnd - reverseArrowFadeInStart)),
                                                        0.0f, 1.0f);
    this->fReverseArrowAlpha *= cv::slider_reverse_arrow_alpha_multiplier.getFloat();

    this->fBodyAlpha = this->fAlpha;
    if(flags::has<ModFlags::Hidden>(curIFaceMods)) {   // hidden modifies the body alpha
        this->fBodyAlpha = this->fAlphaWithoutHidden;  // fade in as usual

        // fade out over the duration of the slider, starting exactly when the default fadein finishes
        const i32 hiddenSliderBodyFadeOutStart =
            std::min(this->click_time,
                     this->click_time - this->iApproachTime +
                         this->iFadeInTime);  // std::min() ensures that the fade always starts at click_time
                                              // (even if the fadeintime is longer than the approachtime)
        const float fade_percent = cv::mod_hd_slider_fade_percent.getFloat();
        const i32 hiddenSliderBodyFadeOutEnd = this->click_time + (i32)(fade_percent * this->fSliderTime);
        if(curPos >= hiddenSliderBodyFadeOutStart) {
            this->fBodyAlpha = std::clamp<float>(((float)(hiddenSliderBodyFadeOutEnd - curPos) /
                                                  (float)(hiddenSliderBodyFadeOutEnd - hiddenSliderBodyFadeOutStart)),
                                                 0.0f, 1.0f);
            this->fBodyAlpha *= this->fBodyAlpha;  // quad in body fadeout
        }
    }

    // if this slider is active, recalculate sliding/curve position and general state
    if(this->fSlidePercent > 0.0f || this->bVisible) {
        // handle reverse sliders
        this->bInReverse = false;
        this->bHideNumberAfterFirstRepeatHit = false;
        if(this->iRepeat > 1) {
            if(this->fSlidePercent > 0.0f && this->bStartFinished) this->bHideNumberAfterFirstRepeatHit = true;

            float part = 1.0f / (float)this->iRepeat;
            this->iCurRepeat = (int)(this->fSlidePercent * this->iRepeat);
            float baseSlidePercent = part * this->iCurRepeat;
            float partSlidePercent = (this->fSlidePercent - baseSlidePercent) / part;
            if(this->iCurRepeat % 2 == 0) {
                this->fSlidePercent = partSlidePercent;
                this->iReverseArrowPos = 2;
            } else {
                this->fSlidePercent = 1.0f - partSlidePercent;
                this->iReverseArrowPos = 1;
                this->bInReverse = true;
            }

            // no reverse arrow on the last repeat
            if(this->iCurRepeat == this->iRepeat - 1) this->iReverseArrowPos = 0;

            // osu style: immediately show all coming reverse arrows (even on the circle we just started from)
            if(this->iCurRepeat < this->iRepeat - 2 && this->fSlidePercent > 0.0f && this->iRepeat > 2)
                this->iReverseArrowPos = 3;
        }

        this->vCurPointRaw = this->curve->pointAt(this->fSlidePercent);
        this->vCurPoint = this->pi->osuCoords2Pixels(this->vCurPointRaw);
    } else {
        this->vCurPointRaw = this->curve->pointAt(0.0f);
        this->vCurPoint = this->pi->osuCoords2Pixels(this->vCurPointRaw);
    }

    // No longer ignore keys that were released since entering the slider
    // see isClickHeldSlider()
    this->iIgnoredKeys &= this->pi->getKeys();

    // handle dynamic followradius
    float followRadius =
        this->bCursorLeft ? this->pi->fHitcircleDiameter / 2.0f : this->pi->fSliderFollowCircleDiameter / 2.0f;
    const bool isPlayfieldCursorInside = (vec::length(this->pi->getCursorPos() - this->vCurPoint) < followRadius);
    const bool isAutoCursorInside =
        ((flags::has<ModFlags::Autoplay>(curIFaceMods)) &&
         (!cv::auto_cursordance.getBool() || (vec::length(this->pi->getCursorPos() - this->vCurPoint) < followRadius)));
    this->bCursorInside = (isAutoCursorInside || isPlayfieldCursorInside);
    this->bCursorLeft = !this->bCursorInside;

    // handle slider start
    if(!this->bStartFinished) {
        if((flags::has<ModFlags::Autoplay>(curIFaceMods))) {
            if(curPos >= this->click_time) {
                this->onHit(LiveScore::HIT::HIT_300, 0, false);
                this->pi->holding_slider = true;
            }
        } else {
            i32 delta = curPos - this->click_time;

            if((flags::has<ModFlags::Relax>(curIFaceMods))) {
                if(curPos >= this->click_time + (i32)cv::relax_offset.getInt() && !this->pi->isPaused() &&
                   !this->pi->isContinueScheduled()) {
                    const vec2 pos = this->pi->osuCoords2Pixels(this->curve->pointAt(0.0f));
                    const float cursorDelta = vec::length(this->pi->getCursorPos() - pos);
                    if((cursorDelta < this->pi->fHitcircleDiameter / 2.0f &&
                        (flags::has<ModFlags::Relax>(curIFaceMods)))) {
                        LiveScore::HIT result = this->pi->getHitResult(delta);

                        if(result != LiveScore::HIT::HIT_NULL) {
                            const float targetDelta = cursorDelta / (this->pi->fHitcircleDiameter / 2.0f);
                            const float targetAngle = glm::degrees(
                                std::atan2(this->pi->getCursorPos().y - pos.y, this->pi->getCursorPos().x - pos.x));

                            this->startResult = result;
                            this->onHit(this->startResult, delta, false, targetDelta, targetAngle);
                            this->pi->holding_slider = true;
                        }
                    }
                }
            }

            // wait for a miss
            if(delta >= 0) {
                // if this is a miss after waiting
                if(delta > (i32)this->pi->getHitWindow50()) {
                    this->startResult = LiveScore::HIT::HIT_MISS;
                    this->onHit(this->startResult, delta, false);
                    this->pi->holding_slider = false;
                }
            }
        }
    }

    // handle slider end, repeats, ticks
    if(!this->bEndFinished) {
        // NOTE: we have 2 timing conditions after which we start checking for strict tracking: 1) startcircle was
        // clicked, 2) slider has started timing wise it is easily possible to hit the startcircle way before the
        // sliderball would become active, which is why the first check exists. even if the sliderball has not yet
        // started sliding, you will be punished for leaving the (still invisible) followcircle area after having
        // clicked the startcircle, always.
        const bool isTrackingStrictTrackingMod =
            ((this->bStartFinished || curPos >= this->click_time) && cv::mod_strict_tracking.getBool());

        // slider tail lenience bullshit: see
        // https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Objects/Slider.cs#L123 being "inside the slider"
        // (for the end of the slider) is NOT checked at the exact end of the slider, but somewhere random before,
        // because fuck you
        const i32 offset = (i32)cv::slider_end_inside_check_offset.getInt();
        const i32 lenienceHackEndTime = std::max(this->click_time + this->duration / 2, (this->getEndTime()) - offset);
        const bool isTrackingCorrectly =
            (this->isClickHeldSlider() || (flags::has<ModFlags::Relax>(curIFaceMods))) && this->bCursorInside;
        if(isTrackingCorrectly) {
            if(isTrackingStrictTrackingMod) {
                this->iStrictTrackingModLastClickHeldTime = curPos;
                if(this->iStrictTrackingModLastClickHeldTime ==
                   0)  // (prevent frame perfect inputs from not triggering the strict tracking miss because we use != 0
                       // comparison to detect if tracking correctly at least once)
                    this->iStrictTrackingModLastClickHeldTime = 1;
            }

            // only check it at the exact point in time ...
            if(curPos >= lenienceHackEndTime) {
                // ... once (like a tick)
                if(!this->bHeldTillEndForLenienceHackCheck) {
                    // player was correctly clicking/holding inside slider at lenienceHackEndTime
                    this->bHeldTillEndForLenienceHackCheck = true;
                    this->bHeldTillEndForLenienceHack = true;
                }
            }
        } else {
            // do not allow empty clicks outside of the circle radius to prevent the
            // m_bCursorInside flag from resetting
            this->bCursorLeft = true;
        }

        // can't be "inside the slider" after lenienceHackEndTime
        // (even though the slider is still going, which is madness)
        if(curPos >= lenienceHackEndTime) this->bHeldTillEndForLenienceHackCheck = true;

        // handle strict tracking mod
        if(isTrackingStrictTrackingMod) {
            const bool wasTrackingCorrectlyAtLeastOnce = (this->iStrictTrackingModLastClickHeldTime != 0);
            if(wasTrackingCorrectlyAtLeastOnce && !isTrackingCorrectly) {
                // if past lenience end time then don't trigger a strict tracking miss,
                // since the slider is then already considered fully finished gameplay wise
                if(!this->bHeldTillEndForLenienceHack) {
                    // force miss the end once, if it has not already been force missed by notelock
                    if(this->endResult == LiveScore::HIT::HIT_NULL) {
                        // force miss endcircle
                        this->onSliderBreak();

                        this->bHeldTillEnd = false;
                        this->bHeldTillEndForLenienceHack = false;
                        this->bHeldTillEndForLenienceHackCheck = true;
                        this->endResult = LiveScore::HIT::HIT_MISS;

                        // end of combo, ignore in hiterrorbar, ignore combo, subtract health
                        this->addHitResult(this->endResult, 0, this->is_end_of_combo,
                                           this->getRawPosAt(this->getEndTime()), -1.0f, 0.0f, true, true, false);
                    }
                }
            }
        }

        // handle repeats and ticks
        for(auto &click : this->clicks) {
            if(!click.finished && curPos >= click.time) {
                click.finished = true;
                click.successful = (this->isClickHeldSlider() && this->bCursorInside) ||
                                   (flags::has<ModFlags::Autoplay>(curIFaceMods)) ||
                                   ((flags::has<ModFlags::Relax>(curIFaceMods)) && this->bCursorInside);

                if(click.type == 0) {
                    this->onRepeatHit(click);
                } else {
                    this->onTickHit(click);
                }
            }
        }

        // handle auto, and the last circle
        if((flags::has<ModFlags::Autoplay>(curIFaceMods))) {
            if(curPos >= this->getEndTime()) {
                this->bHeldTillEnd = true;
                this->onHit(LiveScore::HIT::HIT_300, 0, true);
                this->pi->holding_slider = false;
            }
        } else {
            if(curPos >= this->getEndTime()) {
                // handle leftover startcircle
                {
                    // this may happen (if the slider time is shorter than the miss window of the startcircle)
                    if(this->startResult == LiveScore::HIT::HIT_NULL) {
                        // we still want to cause a sliderbreak in this case!
                        this->onSliderBreak();

                        // special case: missing the startcircle drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS
                        // health)
                        this->pi->addHitResult(this, LiveScore::HIT::HIT_MISS_SLIDERBREAK, 0, false, true, true, true,
                                               true,
                                               false);  // only decrease health

                        this->startResult = LiveScore::HIT::HIT_MISS;
                    }
                }

                // handle endcircle
                bool isEndResultComingFromStrictTrackingMod = false;
                if(this->endResult == LiveScore::HIT::HIT_NULL) {
                    this->bHeldTillEnd = this->bHeldTillEndForLenienceHack;
                    this->endResult = this->bHeldTillEnd ? LiveScore::HIT::HIT_300 : LiveScore::HIT::HIT_MISS;

                    // handle total slider result (currently startcircle + repeats + ticks + endcircle)
                    // clicks = (repeats + ticks)
                    const float numMaxPossibleHits = 1 + this->clicks.size() + 1;
                    float numActualHits = 0;

                    if(this->startResult != LiveScore::HIT::HIT_MISS) numActualHits++;
                    if(this->endResult != LiveScore::HIT::HIT_MISS) numActualHits++;

                    for(auto &click : this->clicks) {
                        if(click.successful) numActualHits++;
                    }

                    const float percent = numActualHits / numMaxPossibleHits;

                    const bool allow300 = (flags::has<ModFlags::ScoreV2>(curIFaceMods))
                                              ? (this->startResult == LiveScore::HIT::HIT_300)
                                              : true;
                    const bool allow100 = (flags::has<ModFlags::ScoreV2>(curIFaceMods))
                                              ? (this->startResult == LiveScore::HIT::HIT_300 ||
                                                 this->startResult == LiveScore::HIT::HIT_100)
                                              : true;

                    // rewrite m_endResult as the whole slider result, then use it for the final onHit()
                    if(percent >= 0.999f && allow300)
                        this->endResult = LiveScore::HIT::HIT_300;
                    else if(percent >= 0.5f && allow100 && !flags::has<ModFlags::Ming3012>(curIFaceMods) &&
                            !flags::has<ModFlags::No100s>(curIFaceMods))
                        this->endResult = LiveScore::HIT::HIT_100;
                    else if(percent > 0.0f && !flags::has<ModFlags::No100s>(curIFaceMods) &&
                            !flags::has<ModFlags::No50s>(curIFaceMods))
                        this->endResult = LiveScore::HIT::HIT_50;
                    else
                        this->endResult = LiveScore::HIT::HIT_MISS;

                    // debugLog("percent = {:f}", percent);

                    if(!this->bHeldTillEnd && cv::slider_end_miss_breaks_combo.getBool()) this->onSliderBreak();
                } else
                    isEndResultComingFromStrictTrackingMod = true;

                this->onHit(this->endResult, 0, true, 0.0f, 0.0f, isEndResultComingFromStrictTrackingMod);
                this->pi->holding_slider = false;
            }
        }

        // handle sliderslide sound
        // TODO @kiwec: move this to draw()
        if(this->pf != nullptr) {
            const ModFlags &curGameplayFlags = this->pf->getMods().flags;

            const bool sliding =
                this->bStartFinished && !this->bEndFinished && this->bCursorInside && this->iDelta <= 0  //
                && (this->isClickHeldSlider() || (flags::has<ModFlags::Autoplay>(curGameplayFlags)) ||   //
                    (flags::has<ModFlags::Relax>(curGameplayFlags)))                                     //
                && !this->pf->isPaused() && !this->pf->isWaiting() && this->pf->isPlaying()              //
                && !this->pf->bWasSeekFrame;

            if(sliding) {
                const vec2 osuCoords = this->pf->pixels2OsuCoords(this->pf->osuCoords2Pixels(this->vCurPointRaw));
                f32 pan = GameRules::osuCoords2Pan(osuCoords.x);
                this->lastSliderSampleSets = this->samples.play(pan, 0, -1, true);
            } else if(!this->lastSliderSampleSets.empty()) {
                // debugLog("not sliding, stopping");
                // debugLog(
                //     "this->bStartFinished {} this->bEndFinished {} this->bCursorInside {} this->iDelta {} "
                //     "this->isClickHeldSlider() {} this->pf->isPaused() {} this->pf->isWaiting() {} "
                //     "this->pf->isPlaying() {} this->pf->bWasSeekFrame {}",
                //     !!this->bStartFinished, !!this->bEndFinished, !!this->bCursorInside, this->iDelta,
                //     this->isClickHeldSlider(), this->pf->isPaused(), this->pf->isWaiting(), this->pf->isPlaying(),
                //     this->pf->bWasSeekFrame);
                this->samples.stop(this->lastSliderSampleSets);
                this->lastSliderSampleSets.clear();
            }
        }
    }
}

void Slider::updateAnimations(i32 curPos) {
    float animation_multiplier = this->pf->getSpeedAdjustedAnimationSpeed();

    float fadein_fade_time = cv::slider_followcircle_fadein_fade_time.getFloat() * animation_multiplier;
    float fadeout_fade_time = cv::slider_followcircle_fadeout_fade_time.getFloat() * animation_multiplier;
    float fadein_scale_time = cv::slider_followcircle_fadein_scale_time.getFloat() * animation_multiplier;
    float fadeout_scale_time = cv::slider_followcircle_fadeout_scale_time.getFloat() * animation_multiplier;

    // handle followcircle animations
    this->fFollowCircleAnimationAlpha =
        std::clamp<float>((float)((curPos - this->click_time)) / 1000.0f /
                              std::clamp<float>(fadein_fade_time, 0.0f, this->duration / 1000.0f),
                          0.0f, 1.0f);
    if(this->bFinished) {
        this->fFollowCircleAnimationAlpha =
            1.0f -
            std::clamp<float>((float)((curPos - (this->getEndTime()))) / 1000.0f / fadeout_fade_time, 0.0f, 1.0f);
        this->fFollowCircleAnimationAlpha *= this->fFollowCircleAnimationAlpha;  // quad in
    }

    this->fFollowCircleAnimationScale =
        std::clamp<float>((float)((curPos - this->click_time)) / 1000.0f /
                              std::clamp<float>(fadein_scale_time, 0.0f, this->duration / 1000.0f),
                          0.0f, 1.0f);
    if(this->bFinished) {
        this->fFollowCircleAnimationScale =
            std::clamp<float>((float)((curPos - (this->getEndTime()))) / 1000.0f / fadeout_scale_time, 0.0f, 1.0f);
    }
    this->fFollowCircleAnimationScale =
        -this->fFollowCircleAnimationScale * (this->fFollowCircleAnimationScale - 2.0f);  // quad out

    if(!this->bFinished)
        this->fFollowCircleAnimationScale =
            cv::slider_followcircle_fadein_scale.getFloat() +
            (1.0f - cv::slider_followcircle_fadein_scale.getFloat()) * this->fFollowCircleAnimationScale;
    else
        this->fFollowCircleAnimationScale =
            1.0f - (1.0f - cv::slider_followcircle_fadeout_scale.getFloat()) * this->fFollowCircleAnimationScale;
}

void Slider::updateStackPosition(float stackOffset) {
    if(this->curve != nullptr)
        this->curve->updateStackPosition(this->iStack * stackOffset,
                                         (flags::has<ModFlags::HardRock>(this->pi->getMods().flags)));
}

void Slider::miss(i32 curPos) {
    if(this->bFinished) return;

    const i32 delta = curPos - this->click_time;

    // startcircle
    if(!this->bStartFinished) {
        this->startResult = LiveScore::HIT::HIT_MISS;
        this->onHit(this->startResult, delta, false);
        this->pi->holding_slider = false;
    }

    // endcircle, repeats, ticks
    if(!this->bEndFinished) {
        // repeats, ticks
        {
            for(auto &click : this->clicks) {
                if(!click.finished) {
                    click.finished = true;
                    click.successful = false;

                    if(click.type == 0)
                        this->onRepeatHit(click);
                    else
                        this->onTickHit(click);
                }
            }
        }

        // endcircle
        {
            this->bHeldTillEnd = this->bHeldTillEndForLenienceHack;

            if(!this->bHeldTillEnd && cv::slider_end_miss_breaks_combo.getBool()) this->onSliderBreak();

            this->endResult = LiveScore::HIT::HIT_MISS;
            this->onHit(this->endResult, 0, true);
            this->pi->holding_slider = false;
        }
    }
}

vec2 Slider::getRawPosAt(i32 pos) const {
    if(this->curve == nullptr) return vec2(0, 0);

    if(pos <= this->click_time)
        return this->curve->pointAt(0.0f);
    else if(pos >= this->click_time + this->fSliderTime) {
        if(this->iRepeat % 2 == 0)
            return this->curve->pointAt(0.0f);
        else
            return this->curve->pointAt(1.0f);
    } else
        return this->curve->pointAt(this->getT(pos, false));
}

vec2 Slider::getOriginalRawPosAt(i32 pos) const {
    if(this->curve == nullptr) return vec2(0, 0);

    if(pos <= this->click_time)
        return this->curve->originalPointAt(0.0f);
    else if(pos >= this->click_time + this->fSliderTime) {
        if(this->iRepeat % 2 == 0)
            return this->curve->originalPointAt(0.0f);
        else
            return this->curve->originalPointAt(1.0f);
    } else
        return this->curve->originalPointAt(this->getT(pos, false));
}

float Slider::getT(i32 pos, bool raw) const {
    float t = (float)((i32)pos - (i32)this->click_time) / this->fSliderTimeWithoutRepeats;
    if(raw)
        return t;
    else {
        auto floorVal = (float)std::floor(t);
        return ((int)floorVal % 2 == 0) ? t - floorVal : floorVal + 1 - t;
    }
}

void Slider::onClickEvent(std::vector<Click> &clicks) {
    if(this->points.size() == 0 || this->bBlocked)
        return;  // also handle note blocking here (doesn't need fancy shake logic, since sliders don't shake in
                 // osu!stable)

    if(!this->bStartFinished) {
        const vec2 cursorPos = clicks[0].pos;
        const vec2 pos = this->pi->osuCoords2Pixels(this->curve->pointAt(0.0f));
        const float cursorDelta = vec::length(cursorPos - pos);

        if(cursorDelta < this->pi->fHitcircleDiameter / 2.0f) {
            const i32 delta = clicks[0].music_pos - (i32)this->click_time;

            LiveScore::HIT result = this->pi->getHitResult(delta);
            if(result != LiveScore::HIT::HIT_NULL) {
                const float targetDelta = cursorDelta / (this->pi->fHitcircleDiameter / 2.0f);
                const float targetAngle = glm::degrees(std::atan2(cursorPos.y - pos.y, cursorPos.x - pos.x));

                clicks.erase(clicks.begin());
                this->startResult = result;
                this->onHit(this->startResult, delta, false, targetDelta, targetAngle);
                this->pi->holding_slider = true;
            }
        }
    }
}

void Slider::onHit(LiveScore::HIT result, i32 delta, bool isEndCircle, float targetDelta, float targetAngle,
                   bool isEndResultFromStrictTrackingMod) {
    if(this->points.size() == 0) return;

    // start + end of a slider add +30 points, if successful

    // debugLog("isEndCircle = {:d},    m_iCurRepeat = {:d}", (int)isEndCircle, this->iCurRepeat);

    // sound and hit animation and also sliderbreak combo drop
    {
        if(result == LiveScore::HIT::HIT_MISS) {
            if(!isEndResultFromStrictTrackingMod) this->onSliderBreak();
        } else if(this->pf != nullptr) {
            const vec2 osuCoords = this->pf->pixels2OsuCoords(this->pf->osuCoords2Pixels(this->vCurPointRaw));
            f32 pan = GameRules::osuCoords2Pan(osuCoords.x);

            if(this->edgeSamples.size() > 0) {
                if(isEndCircle) {
                    this->edgeSamples.back().play(pan, delta, this->getEndTime());
                } else {
                    this->edgeSamples[0].play(pan, delta, this->click_time);
                }
            }

            const float fadeout_time = GameRules::getFadeOutTime(this->pi->getBaseAnimationSpeed());

            if(!isEndCircle) {
                this->addHitAnim(HitAnim::HEAD, fadeout_time);
            } else {
                if(this->iRepeat % 2 != 0) {
                    this->addHitAnim(HitAnim::TAIL, fadeout_time);
                } else {
                    this->addHitAnim(HitAnim::HEAD, fadeout_time);
                }
            }
        }

        // end body fadeout
        if(this->pf != nullptr && isEndCircle) {
            this->fEndSliderBodyFadeAnimation = 0.001f;  // quickfix for 1 frame missing images
            anim::moveQuadOut(&this->fEndSliderBodyFadeAnimation, 1.0f,
                              GameRules::getFadeOutTime(this->pi->getBaseAnimationSpeed()) *
                                  cv::slider_body_fade_out_time_multiplier.getFloat(),
                              true);
            // debugLog("stopping due to end body fadeout");
            this->samples.stop();
        }
    }

    // add score, and we are finished
    if(!isEndCircle) {
        // startcircle

        this->bStartFinished = true;

        // ignore all keys that were held prior to entering the slider
        // except the one used to tap the slider head (or, "hold into" the slider)
        // see isClickHeldSlider()
        this->iIgnoredKeys = (this->pi->getKeys() & ~this->pi->lastPressedKey);

        if(flags::has<ModFlags::Target>(this->pi->getMods().flags)) {
            // not end of combo, show in hiterrorbar, use for accuracy, increase combo, increase
            // score, ignore for health, don't add object duration to result anim
            this->addHitResult(result, delta, false, this->curve->pointAt(0.0f), targetDelta, targetAngle, false, false,
                               true, false);
        } else {
            // not end of combo, show in hiterrorbar, ignore for accuracy, increase combo,
            // don't count towards score, depending on scorev2 ignore for health or not
            this->pi->addHitResult(this, result, delta, false, false, true, false, true, true);
        }

        // add bonus score + health manually
        if(result != LiveScore::HIT::HIT_MISS) {
            LiveScore::HIT resultForHealth = LiveScore::HIT::HIT_SLIDER30;

            this->pi->addHitResult(this, resultForHealth, 0, false, true, true, true, true,
                                   false);  // only increase health
            this->pi->addScorePoints(30);
        } else {
            // special case: missing the startcircle drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS health)
            this->pi->addHitResult(this, LiveScore::HIT::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                                   false);  // only decrease health
        }
    } else {
        // endcircle

        this->bStartFinished = true;
        this->bEndFinished = true;
        this->bFinished = true;

        if(!isEndResultFromStrictTrackingMod) {
            // special case: osu!lazer 2020 only returns 1 judgement for the whole slider, but via the startcircle. i.e.
            // we are not allowed to drain again here in mcosu logic (because startcircle judgement is handled at the
            // end here)
            // XXX: remove this
            const bool isLazer2020Drain = false;

            this->addHitResult(
                result, delta, this->is_end_of_combo, this->getRawPosAt(this->getEndTime()), -1.0f, 0.0f, true,
                !this->bHeldTillEnd,
                isLazer2020Drain);  // end of combo, ignore in hiterrorbar, depending on heldTillEnd increase
                                    // combo or not, increase score, increase health depending on drain type

            // add bonus score + extra health manually
            if(this->bHeldTillEnd) {
                this->pi->addHitResult(this, LiveScore::HIT::HIT_SLIDER30, 0, false, true, true, true, true,
                                       false);  // only increase health
                this->pi->addScorePoints(30);
            } else {
                // special case: missing the endcircle drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS health)
                // NOTE: yes, this will drain twice for the end of a slider (once for the judgement of the whole slider
                // above, and once for the endcircle here)
                this->pi->addHitResult(this, LiveScore::HIT::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                                       false);  // only decrease health
            }
        }
    }

    this->iCurRepeatCounterForHitSounds++;
}

void Slider::onRepeatHit(const SLIDERCLICK &click) {
    if(this->points.size() == 0) return;

    // repeat hit of a slider adds +30 points, if successful

    // sound and hit animation
    if(!click.successful) {
        this->onSliderBreak();
    } else if(this->pf != nullptr) {
        const vec2 osuCoords = this->pf->pixels2OsuCoords(this->pf->osuCoords2Pixels(this->vCurPointRaw));
        f32 pan = GameRules::osuCoords2Pan(osuCoords.x);

        // Try to play a repeat sample based on what the mapper gave us
        // NOTE: iCurRepeatCounterForHitSounds starts at 1
        const uSz nb_edge_samples = this->edgeSamples.size();
        assert(nb_edge_samples > 0);
        if(std::cmp_less(this->iCurRepeatCounterForHitSounds + 1, nb_edge_samples)) {
            this->edgeSamples[this->iCurRepeatCounterForHitSounds].play(pan, 0, click.time);
        } else {
            // We have more repeats than edge samples!
            // Just play whatever we can (either the last repeat sample, or the start sample)
            this->edgeSamples[nb_edge_samples - 2].play(pan, 0, click.time);
        }

        float animation_multiplier = this->pf->getSpeedAdjustedAnimationSpeed();
        float tick_pulse_time = cv::slider_followcircle_tick_pulse_time.getFloat() * animation_multiplier;

        this->fFollowCircleTickAnimationScale = 0.0f;
        anim::moveLinear(&this->fFollowCircleTickAnimationScale, 1.0f, tick_pulse_time, true);

        const float fadeout_time = GameRules::getFadeOutTime(this->pi->getBaseAnimationSpeed());

        if(click.sliderend) {
            this->addHitAnim(HitAnim::TAIL, fadeout_time);
        } else {
            this->addHitAnim(HitAnim::HEAD, fadeout_time);
        }
    }

    // add score
    if(!click.successful) {
        // add health manually
        // special case: missing a repeat drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS health)
        this->pi->addHitResult(this, LiveScore::HIT::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                               false);  // only decrease health
    } else {
        this->pi->addHitResult(this, LiveScore::HIT::HIT_SLIDER30, 0, false, true, true, false, true,
                               false);  // not end of combo, ignore in hiterrorbar, ignore for accuracy, increase
                                        // combo, don't count towards score, increase health

        // add bonus score manually
        this->pi->addScorePoints(30);
    }

    this->iCurRepeatCounterForHitSounds++;
}

void Slider::onTickHit(const SLIDERCLICK &click) {
    if(this->points.size() == 0) return;

    // tick hit of a slider adds +10 points, if successful

    // tick drawing visibility
    int numMissingTickClicks = 0;
    for(auto &c : this->clicks) {
        if(c.type == 1 && c.tickIndex == click.tickIndex && !c.finished) numMissingTickClicks++;
    }
    if(numMissingTickClicks == 0) this->ticks[click.tickIndex].finished = true;

    // sound and hit animation
    if(!click.successful) {
        this->onSliderBreak();
    } else if(this->pf != nullptr) {
        if(const auto *skin = this->pf->getSkin()) {
            static constexpr std::array SLIDERTICK_SAMPLESET_METHODS{
                &Skin::s_normal_slidertick,  //
                &Skin::s_soft_slidertick,    //
                &Skin::s_drum_slidertick,    //
            };

            const BeatmapDifficulty *beatmap = this->pf->getBeatmap();
            const auto ti = (click.time != -1 && beatmap) ? beatmap->getTimingInfoForTime(click.time)
                                                          : this->pf->getCurrentTimingInfo();
            HitSoundContext ctx{
                .timingPointSampleSet = ti.sampleSet,
                .timingPointVolume = ti.volume,
                .defaultSampleSet = this->pf->getDefaultSampleSet(),
                .layeredHitSounds = false,  // unused by sliderticks
                .forcedSampleSet = cv::skin_force_hitsound_sample_set.getInt(),
                .ignoreSampleVolume = cv::ignore_beatmap_sample_volume.getBool(),
                .boostVolume = false,  // unused by sliderticks
            };

            if(const auto tick = this->samples.resolveSliderTick(ctx);
               tick.set >= 0 && tick.set < (i32)SLIDERTICK_SAMPLESET_METHODS.size()) {
                if(Sound *skin_sound = skin->*SLIDERTICK_SAMPLESET_METHODS[tick.set]) {
                    const vec2 osuCoords = this->pf->pixels2OsuCoords(this->pf->osuCoords2Pixels(this->vCurPointRaw));
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

        float animation_multiplier = this->pf->getSpeedAdjustedAnimationSpeed();
        float tick_pulse_time = cv::slider_followcircle_tick_pulse_time.getFloat() * animation_multiplier;

        this->fFollowCircleTickAnimationScale = 0.0f;
        anim::moveLinear(&this->fFollowCircleTickAnimationScale, 1.0f, tick_pulse_time, true);
    }

    // add score
    if(!click.successful) {
        // add health manually
        // special case: missing a tick drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS health)
        this->pi->addHitResult(this, LiveScore::HIT::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                               false);  // only decrease health
    } else {
        this->pi->addHitResult(this, LiveScore::HIT::HIT_SLIDER10, 0, false, true, true, false, true,
                               false);  // not end of combo, ignore in hiterrorbar, ignore for accuracy, increase
                                        // combo, don't count towards score, increase health

        // add bonus score manually
        this->pi->addScorePoints(10);
    }
}

void Slider::onSliderBreak() { this->pi->addSliderBreak(); }

void Slider::onReset(i32 curPos) {
    HitObject::onReset(curPos);

    if(this->pf != nullptr) {
        // debugLog("stopping due to onReset");
        this->samples.stop();

        anim::deleteExistingAnimation(&this->fFollowCircleTickAnimationScale);
        anim::deleteExistingAnimation(&this->fEndSliderBodyFadeAnimation);
    }
    if(!this->clickAnimations.empty()) {
        for(auto &anim : this->clickAnimations) {
            anim::deleteExistingAnimation(&anim.percent);
        }
        this->clickAnimations.clear();
    }

    this->lastSliderSampleSets.clear();
    this->iStrictTrackingModLastClickHeldTime = 0;
    this->iIgnoredKeys = 0;
    this->bCursorLeft = true;
    this->bHeldTillEnd = false;
    this->bHeldTillEndForLenienceHack = false;
    this->bHeldTillEndForLenienceHackCheck = false;
    this->startResult = LiveScore::HIT::HIT_NULL;
    this->endResult = LiveScore::HIT::HIT_NULL;

    this->iCurRepeatCounterForHitSounds = 0;

    if(this->click_time > curPos) {
        this->bStartFinished = false;
        this->bEndFinished = false;
        this->bFinished = false;
        this->fEndSliderBodyFadeAnimation = 0.0f;
    } else if(curPos < this->getEndTime()) {
        this->bStartFinished = true;
        this->bEndFinished = false;
        this->bFinished = false;
        this->fEndSliderBodyFadeAnimation = 0.0f;
    } else {
        this->bStartFinished = true;
        this->bEndFinished = true;
        this->bFinished = true;
        this->fEndSliderBodyFadeAnimation = 1.0f;
    }

    for(auto &click : this->clicks) {
        if(curPos > click.time) {
            click.finished = true;
            click.successful = true;
        } else {
            click.finished = false;
            click.successful = false;
        }
    }

    for(int i = 0; i < this->ticks.size(); i++) {
        int numMissingTickClicks = 0;
        for(auto &click : this->clicks) {
            if(click.type == 1 && click.tickIndex == i && !click.finished) numMissingTickClicks++;
        }
        this->ticks[i].finished = numMissingTickClicks == 0;
    }
}

Slider::HitAnim &Slider::addHitAnim(u8 typeFlags, float duration) {
    // percent = 0.001f: quickfix for 1 frame missing images
    // sanity check, avoid bogus maps with insanely fast buzzsliders overloading animationhandler
    if(this->clickAnimations.size() >= 128) {
        // just overwrite a random one, no one would notice anyways with this many on screen at once
        auto &ret = this->clickAnimations[prand() % 128];
        ret.percent = 0.001f;
        ret.type = decltype(ret.type)(typeFlags);
        anim::moveQuadOut(&ret.percent, 1.0f, duration, true);
        return ret;
    } else {
        auto &ret = this->clickAnimations.emplace_back(HitAnim{.percent = 0.001f, .type{typeFlags}});
        anim::moveQuadOut(&ret.percent, 1.0f, duration, true);
        return ret;
    }
}

void Slider::rebuildVertexBuffer(bool useRawCoords) {
    // base mesh (background) (raw unscaled, size in raw osu coordinates centered at (0, 0, 0))
    // this mesh needs to be scaled and translated appropriately since we are not 1:1 with the playfield
    std::vector<vec2> osuCoordPoints = this->curve->getPoints();
    if(!useRawCoords) {
        for(auto &osuCoordPoint : osuCoordPoints) {
            osuCoordPoint = this->pi->osuCoords2LegacyPixels(osuCoordPoint);
        }
    }
    this->vao = SliderRenderer::generateVAO(osuCoordPoints, this->pi->fRawHitcircleDiameter);
}

Slider::~Slider() { this->onReset(0); }

bool Slider::isClickHeldSlider() {
    // osu! has a weird slider quirk, that I'll explain in detail here.
    // When holding K1 before the slider, tapping K2 on slider head, and releasing K2 later,
    // the slider is no longer considered being "held" until K2 is pressed again, or K1 is released and pressed again.

    // The reason this exists is to prevent people from holding K1 the whole map and tapping with K2.
    // Holding is part of the rhythm flow, and this is a rhythm game right?

    // Note that the restriction only applies to the slider head.
    // Any key pressed *after* entering the slider counts as a hold.

    u8 held_gameplay_keys = this->pi->getKeys() & ~LegacyReplay::Smoke;
    return (held_gameplay_keys & ~this->iIgnoredKeys);
}

Spinner::Spinner(int x, int y, i32 time, HitSamples samples, bool isEndOfCombo, i32 endTime,
                 AbstractBeatmapInterface *pi)
    : HitObject(time, std::move(samples), -1, isEndOfCombo, -1, -1, pi), vRawPos(x, y), vOriginalRawPos(vRawPos) {
    this->type = HitObjectType::SPINNER;
    this->duration = endTime - time;

    int minVel = 12;
    int maxVel = 48;
    int minTime = 2000;
    int maxTime = 5000;
    this->iMaxStoredDeltaAngles = std::clamp<int>(
        (int)((endTime - time - minTime) * (maxVel - minVel) / (maxTime - minTime) + minVel), minVel, maxVel);
    this->storedDeltaAngles = std::make_unique<float[]>(this->iMaxStoredDeltaAngles);

    // spinners don't need misaims
    this->bMisAim = true;

    // spinners don't use AR-dependent fadein, instead they always fade in with hardcoded 400 ms (see
    // GameRules::getFadeInTime())
    this->bUseFadeInTimeAsApproachTime = !cv::spinner_use_ar_fadein.getBool();
}

Spinner::~Spinner() { this->onReset(0); }

void Spinner::draw() {
    HitObject::draw();
    const float fadeOutMultiplier = cv::spinner_fade_out_time_multiplier.getFloat();
    const i32 fadeOutTimeMS =
        (i32)(GameRules::getFadeOutTime(this->pi->getBaseAnimationSpeed()) * 1000.0f * fadeOutMultiplier);
    const i32 deltaEnd = this->iDelta + this->duration;
    if((this->bFinished || !this->bVisible) && (deltaEnd > 0 || (deltaEnd < -fadeOutTimeMS))) return;

    const Skin *skin = this->pf->getSkin();
    const vec2 center = this->pf->osuCoords2Pixels(this->vRawPos);

    // only used for fade out anim atm
    const f32 alphaMultiplier =
        std::clamp<f32>((deltaEnd < 0 ? 1.f - ((f32)std::abs(deltaEnd) / (f32)fadeOutTimeMS) : 1.f), 0.f, 1.f);

    const f32 spinnerScale = this->pf->getPlayfieldSize().y / 667.f;

    // the spinner grows until reaching 100% during spinning, depending on how many spins are left
    const f32 clampedRatio = std::clamp<float>(this->fRatio, 0.0f, 1.0f);
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
            g->setColor(Color(skin->c_spinner_bg).setA(this->fAlphaWithoutHidden * alphaMultiplier));
            g->scale(backgroundScale, backgroundScale);
            g->translate(center.x, center.y);
            g->drawImage(skin->i_spinner_bg);
        }
        g->popTransform();

        // draw spinner metre
        if(cv::skin_use_spinner_metre.getBool() && skin->i_spinner_metre != MISSING_TEXTURE) {
            f32 metreScale = spinnerScale / (skin->i_spinner_metre.scale());
            g->setColor(Color(0xffffffff).setA(this->fAlphaWithoutHidden * alphaMultiplier));

            f32 metreWidth = (f32)skin->i_spinner_metre->getWidth() / (skin->i_spinner_metre.scale());
            f32 metreHeight = (f32)skin->i_spinner_metre->getHeight() / (skin->i_spinner_metre.scale());

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
            g->setColor(Color(0xffffffff).setA(this->fAlphaWithoutHidden * alphaMultiplier));

            g->pushTransform();
            {
                g->rotate(this->fDrawRot);
                g->scale(spinnerCircleScale, spinnerCircleScale);
                g->translate(center.x, center.y);
                g->drawImage(skin->i_spinner_circle);
            }
            g->popTransform();
        }

        // draw approach circle
        if(!(flags::has<ModFlags::Hidden>(this->pi->getMods().flags)) && this->fPercent > 0.0f) {
            const f32 spinnerApproachCircleImageScale = (spinnerScale * 2) / (skin->i_spinner_approach_circle.scale());
            g->setColor(Color(skin->c_spinner_approach_circle).setA(this->fAlphaWithoutHidden * alphaMultiplier));

            g->pushTransform();
            {
                g->scale(spinnerApproachCircleImageScale * this->fPercent,
                         spinnerApproachCircleImageScale * this->fPercent);
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
            g->setColor(Color(0xffffffff).setA(this->fAlphaWithoutHidden * alphaMultiplier));

            g->pushTransform();
            {
                g->rotate(this->fDrawRot / 7.0f);
                g->scale(spinnerBottomImageScale * finishScale, spinnerBottomImageScale * finishScale);
                g->translate(center.x, center.y);
                g->drawImage(skin->i_spinner_bottom);
            }
            g->popTransform();
        }

        // top
        if(skin->i_spinner_top != MISSING_TEXTURE) {
            const f32 spinnerTopImageScale = spinnerScale / (skin->i_spinner_top.scale());
            g->setColor(Color(0xffffffff).setA(this->fAlphaWithoutHidden * alphaMultiplier));

            g->pushTransform();
            {
                g->rotate(this->fDrawRot / 2.0f);
                g->scale(spinnerTopImageScale * finishScale, spinnerTopImageScale * finishScale);
                g->translate(center.x, center.y);
                g->drawImage(skin->i_spinner_top);
            }
            g->popTransform();
        }

        // middle
        if(skin->i_spinner_middle2 != MISSING_TEXTURE) {
            const f32 spinnerMiddle2ImageScale = spinnerScale / (skin->i_spinner_middle2.scale());
            g->setColor(Color(0xffffffff).setA(this->fAlphaWithoutHidden * alphaMultiplier));

            g->pushTransform();
            {
                g->rotate(this->fDrawRot);
                g->scale(spinnerMiddle2ImageScale * finishScale, spinnerMiddle2ImageScale * finishScale);
                g->translate(center.x, center.y);
                g->drawImage(skin->i_spinner_middle2);
            }
            g->popTransform();
        }
        if(skin->i_spinner_middle != MISSING_TEXTURE) {
            const f32 spinnerMiddleImageScale = spinnerScale / (skin->i_spinner_middle.scale());
            g->setColor(
                argb(this->fAlphaWithoutHidden * alphaMultiplier, 1.f, (1.f * this->fPercent), (1.f * this->fPercent)));
            g->pushTransform();
            {
                g->rotate(this->fDrawRot / 2.0f);  // apparently does not rotate in osu
                g->scale(spinnerMiddleImageScale * finishScale, spinnerMiddleImageScale * finishScale);
                g->translate(center.x, center.y);
                g->drawImage(skin->i_spinner_middle);
            }
            g->popTransform();
        }

        // approach circle
        // TODO: only use when spinner-circle or spinner-top are skinned
        if(!(flags::has<ModFlags::Hidden>(this->pi->getMods().flags)) && this->fPercent > 0.0f) {
            const f32 spinnerApproachCircleImageScale = (spinnerScale * 2) / (skin->i_spinner_approach_circle.scale());

            // fun fact, peppy removed it: https://osu.ppy.sh/community/forums/topics/100765
            g->setColor(Color(skin->c_spinner_approach_circle).setA(this->fAlphaWithoutHidden * alphaMultiplier));

            g->pushTransform();
            {
                g->scale(spinnerApproachCircleImageScale * this->fPercent,
                         spinnerApproachCircleImageScale * this->fPercent);
                g->translate(center.x, center.y); /* 397.f wtf is this hardcoded number? its completely off */
                g->drawImage(skin->i_spinner_approach_circle);
            }
            g->popTransform();
        }
    }

    // "CLEAR!"
    if(this->fRatio >= 1.0f) {
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
        g->setColor(Color(0xffffffff).setA(this->fAlphaWithoutHidden * alphaMultiplier));

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
    if(this->iDelta < 0) {
        McFont *rpmFont = engine->getDefaultFont();
        const float stringWidth = rpmFont->getStringWidth("RPM: 477");
        g->setColor(Color(0xffffffff)
                        .setA(this->fAlphaWithoutHidden * this->fAlphaWithoutHidden * this->fAlphaWithoutHidden *
                              alphaMultiplier));

        g->pushTransform();
        {
            g->translate((int)(osu->getVirtScreenWidth() / 2 - stringWidth / 2),
                         (int)(osu->getVirtScreenHeight() - 5 +
                               (5 + rpmFont->getHeight()) * (1.0f - this->fAlphaWithoutHidden)));
            g->drawString(rpmFont, fmt::format("RPM: {}", (int)(this->fRPM + 0.4f)));
        }
        g->popTransform();
    }
}

void Spinner::update(i32 curPos, f64 frame_time) {
    HitObject::update(curPos, frame_time);

    // stop spinner sound and don't update() while paused
    if(this->pi->isPaused() || !this->pi->isPlaying() || (this->pf && this->pf->hasFailed())) {
        const auto spinner_spinsound = this->pf && this->pf->getSkin() ? this->pf->getSkin()->s_spinner_spin : nullptr;
        if(spinner_spinsound && spinner_spinsound->isPlaying()) {
            soundEngine->stop(spinner_spinsound);
        }
        return;
    }

    // if we have not been clicked yet, check if we are in the timeframe of a miss, also handle auto and relax
    if(!this->bFinished) {
        // handle spinner ending
        if(curPos >= this->getEndTime()) {
            this->onHit();
            return;
        }

        // Skip calculations
        if(frame_time == 0.0) {
            return;
        }

        this->fRotationsNeeded = GameRules::getSpinnerRotationsForSpeedMultiplier(this->pi, this->duration);

        const float DELTA_UPDATE_TIME = (frame_time * 1000.0f);
        const float AUTO_MULTIPLIER = (1.0f / 20.0f);

        // scale percent calculation
        i32 delta = (i32)this->click_time - (i32)curPos;
        this->fPercent = 1.0f - std::clamp<float>((float)delta / -(float)(this->duration), 0.0f, 1.0f);

        // handle auto, mouse spinning movement
        float angleDiff = 0;
        if(flags::any<ModFlags::Autoplay | ModFlags::Autopilot | ModFlags::SpunOut>(this->pi->getMods().flags)) {
            angleDiff = frame_time * 1000.0f * AUTO_MULTIPLIER * this->pi->getSpeedMultiplier();
        } else {  // user spin
            vec2 mouseDelta = this->pi->getCursorPos() - this->pi->osuCoords2Pixels(this->vRawPos);
            const auto currentMouseAngle = (float)std::atan2(mouseDelta.y, mouseDelta.x);
            angleDiff = (currentMouseAngle - this->fLastMouseAngle);

            if(std::abs(angleDiff) > 0.001f)
                this->fLastMouseAngle = currentMouseAngle;
            else
                angleDiff = 0;
        }

        // handle spinning
        // HACKHACK: rewrite this
        if(delta <= 0) {
            bool isSpinning =
                this->pi->isClickHeld() ||
                flags::any<ModFlags::Autoplay | ModFlags::Relax | ModFlags::SpunOut>(this->pi->getMods().flags);

            this->fDeltaOverflow += frame_time * 1000.0f;

            if(angleDiff < -PI)
                angleDiff += 2 * PI;
            else if(angleDiff > PI)
                angleDiff -= 2 * PI;

            if(isSpinning) this->fDeltaAngleOverflow += angleDiff;

            while(this->fDeltaOverflow >= DELTA_UPDATE_TIME) {
                // spin caused by the cursor
                float deltaAngle = 0;
                if(isSpinning) {
                    deltaAngle = this->fDeltaAngleOverflow * DELTA_UPDATE_TIME / this->fDeltaOverflow;
                    this->fDeltaAngleOverflow -= deltaAngle;
                    // deltaAngle = std::clamp<float>(deltaAngle, -MAX_ANG_DIFF, MAX_ANG_DIFF);
                }

                this->fDeltaOverflow -= DELTA_UPDATE_TIME;

                this->fSumDeltaAngle -= this->storedDeltaAngles[this->iDeltaAngleIndex];
                this->fSumDeltaAngle += deltaAngle;
                this->storedDeltaAngles[this->iDeltaAngleIndex++] = deltaAngle;
                this->iDeltaAngleIndex %= this->iMaxStoredDeltaAngles;

                float rotationAngle = this->fSumDeltaAngle / this->iMaxStoredDeltaAngles;
                float rotationPerSec = rotationAngle * (1000.0f / DELTA_UPDATE_TIME) / (2.0f * PI);

                f32 decay = pow(0.01f, (f32)frame_time);
                this->fRPM = this->fRPM * decay + (1.0 - decay) * std::abs(rotationPerSec) * 60;
                this->fRPM = std::min(this->fRPM, 477.0f);

                if(std::abs(rotationAngle) > 0.0001f) this->rotate(rotationAngle);
            }

            this->fRatio = this->fRotations / (this->fRotationsNeeded * 360.0f);
        }
    }
}

void Spinner::onReset(i32 curPos) {
    HitObject::onReset(curPos);

    {
        const auto spinner_spinsound = this->pf && this->pf->getSkin() ? this->pf->getSkin()->s_spinner_spin : nullptr;
        if(spinner_spinsound && spinner_spinsound->isPlaying()) {
            soundEngine->stop(spinner_spinsound);
        }
    }

    this->fRPM = 0.0f;
    this->fDrawRot = 0.0f;
    this->fRotations = 0.0f;
    this->fDeltaOverflow = 0.0f;
    this->fSumDeltaAngle = 0.0f;
    this->iDeltaAngleIndex = 0;
    this->fDeltaAngleOverflow = 0.0f;
    this->fRatio = 0.0f;

    // spinners don't need misaims
    this->bMisAim = true;

    for(int i = 0; i < this->iMaxStoredDeltaAngles; i++) {
        this->storedDeltaAngles[i] = 0.0f;
    }

    if(curPos > this->getEndTime())
        this->bFinished = true;
    else
        this->bFinished = false;
}

void Spinner::onHit() {
    // calculate hit result
    LiveScore::HIT result = LiveScore::HIT::HIT_NULL;
    if(this->fRatio >= 1.0f || (flags::has<ModFlags::Autoplay>(this->pi->getMods().flags)))
        result = LiveScore::HIT::HIT_300;
    else if(this->fRatio >= 0.9f && !cv::mod_ming3012.getBool() && !cv::mod_no100s.getBool())
        result = LiveScore::HIT::HIT_100;
    else if(this->fRatio >= 0.75f && !cv::mod_no100s.getBool() && !cv::mod_no50s.getBool())
        result = LiveScore::HIT::HIT_50;
    else
        result = LiveScore::HIT::HIT_MISS;

    // sound
    if(this->pf != nullptr && result != LiveScore::HIT::HIT_MISS) {
        const vec2 osuCoords = this->pf->pixels2OsuCoords(this->pf->osuCoords2Pixels(this->vRawPos));
        f32 pan = GameRules::osuCoords2Pan(osuCoords.x);
        // TODO: skin->bSpinnerFrequencyModulate (pitch up the longer the spinner goes)
        this->samples.play(pan, 0);
    }

    // add it, and we are finished
    this->addHitResult(result, 0, this->is_end_of_combo, this->vRawPos, -1.0f, 0.f, /*ignoreOnHitErrorBar=*/true);
    this->bFinished = true;

    const auto spinner_spinsound = this->pf && this->pf->getSkin() ? this->pf->getSkin()->s_spinner_spin : nullptr;
    if(spinner_spinsound && spinner_spinsound->isPlaying()) {
        soundEngine->stop(spinner_spinsound);
    }
}

void Spinner::rotate(float rad) {
    this->fDrawRot += glm::degrees(rad);

    rad = std::abs(rad);
    const float newRotations = this->fRotations + glm::degrees(rad);

    // added one whole rotation
    if(std::floor(newRotations / 360.0f) > this->fRotations / 360.0f) {
        if((int)(newRotations / 360.0f) > (int)(this->fRotationsNeeded) + 1) {
            // extra rotations and bonus sound
            if(this->pf != nullptr && !this->pf->bWasSeekFrame && this->pf->getSkin()->s_spinner_bonus) {
                soundEngine->play(this->pf->getSkin()->s_spinner_bonus);
            }
            this->pi->addHitResult(this, LiveScore::HIT::HIT_SPINNERBONUS, 0, false, true, true, true, true,
                                   false);  // only increase health
            this->pi->addHitResult(this, LiveScore::HIT::HIT_SPINNERBONUS, 0, false, true, true, true, true,
                                   false);  // HACKHACK: compensating for rotation logic differences
            this->pi->addScorePoints(1100, true);
        } else {
            // normal whole rotation
            this->pi->addHitResult(this, LiveScore::HIT::HIT_SPINNERSPIN, 0, false, true, true, true, true,
                                   false);  // only increase health
            this->pi->addHitResult(this, LiveScore::HIT::HIT_SPINNERSPIN, 0, false, true, true, true, true,
                                   false);  // HACKHACK: compensating for rotation logic differences
            this->pi->addScorePoints(100, true);
        }
    }

    // spinner sound
    if(this->pf != nullptr && !this->pf->bWasSeekFrame) {
        const auto spinner_spinsound = this->pf->getSkin() ? this->pf->getSkin()->s_spinner_spin : nullptr;
        if(spinner_spinsound) {
            if(!spinner_spinsound->isPlaying()) {
                soundEngine->play(spinner_spinsound);
            }
            const float frequency = 20000.0f + (int)(std::clamp<float>(this->fRatio, 0.0f, 2.5f) * 40000.0f);
            spinner_spinsound->setFrequency(frequency);
        }
    }

    this->fRotations = newRotations;
}

vec2 Spinner::getAutoCursorPos(i32 curPos) const {
    // calculate point
    i32 delta = 0;
    if(curPos <= this->click_time)
        delta = 0;
    else if(curPos >= this->getEndTime())
        delta = this->duration;
    else
        delta = curPos - this->click_time;

    vec2 actualPos = this->pi->osuCoords2Pixels(this->vRawPos);
    const float AUTO_MULTIPLIER = (1.0f / 20.0f);
    float multiplier =
        flags::any<ModFlags::Autoplay | ModFlags::Autopilot>(this->pi->getMods().flags) ? AUTO_MULTIPLIER : 1.0f;
    float angle = (delta * multiplier) - PI / 2.0f;
    float r = GameRules::getPlayfieldSize().y / 10.0f;  // XXX: slow?
    return vec2((float)(actualPos.x + r * std::cos(angle)), (float)(actualPos.y + r * std::sin(angle)));
}
