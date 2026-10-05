#include "PlayfieldView.h"

#include "GameRules.h"
#include "ModFlags.h"
#include "Osu.h"
#include "OsuConVars.h"
#include "Skin.h"

f32 PlayfieldView::numberScale(const Skin *skin, f32 rawHitcircleDiameter, f32 hitcircleDiameter) {
    const f32 osuCoordScaleMultiplier = hitcircleDiameter / rawHitcircleDiameter;
    return (rawHitcircleDiameter / (160.0f * (skin->i_defaults[1].scale()))) * osuCoordScaleMultiplier *
           cv::number_scale_multiplier.getFloat();
}

f32 PlayfieldView::hitcircleOverlapScale(f32 rawHitcircleDiameter, f32 hitcircleDiameter) {
    const f32 osuCoordScaleMultiplier = hitcircleDiameter / rawHitcircleDiameter;
    return (rawHitcircleDiameter / (160.0f)) * osuCoordScaleMultiplier * cv::number_scale_multiplier.getFloat();
}

const Skin *PlainPlayfieldView::getSkin() const { return osu->getSkin(); }

vec2 PlainPlayfieldView::osuCoords2LegacyPixels(vec2 coords) const {
    return coords - vec2{GameRules::OSU_COORD_WIDTH / 2, GameRules::OSU_COORD_HEIGHT / 2};
}

vec2 PlainPlayfieldView::getPlayfieldCenter() const {
    return this->osuCoords2Pixels(vec2{GameRules::OSU_COORD_WIDTH / 2.f, GameRules::OSU_COORD_HEIGHT / 2.f});
}

vec2 PlainPlayfieldView::getPlayfieldSize() const {
    return vec2{GameRules::OSU_COORD_WIDTH, GameRules::OSU_COORD_HEIGHT} * this->scale;
}

f32 PlainPlayfieldView::getSliderFollowCircleDiameter() const {
    return this->getHitcircleDiameter() * GameRules::SLIDER_FOLLOW_CIRCLE_MULTIPLIER;
}

f32 PlainPlayfieldView::getNumberScale() const {
    return numberScale(this->getSkin(), this->rawHitcircleDiameter, this->getHitcircleDiameter());
}

f32 PlainPlayfieldView::getHitcircleOverlapScale() const {
    return hitcircleOverlapScale(this->rawHitcircleDiameter, this->getHitcircleDiameter());
}

ModFlags PlainPlayfieldView::getModFlags() const { return ModFlags::None; }
