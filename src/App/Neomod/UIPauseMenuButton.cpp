// Copyright (c) 2018, PG, All rights reserved.
#include "UIPauseMenuButton.h"

#include <utility>

#include "AnimationHandler.h"
#include "Engine.h"
#include "Osu.h"
#include "Skin.h"
#include "SoundEngine.h"
#include "Environment.h"
#include "Graphics.h"

UIPauseMenuButton::UIPauseMenuButton(ImageSkinMember imageMember, float xPos, float yPos, float xSize, float ySize,
                                     UString name)
    : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name)) {
    this->imageMember = imageMember;

    this->vScale = vec2(1, 1);
    this->fScaleMultiplier = 1.1f;

    this->fAlpha = 1.0f;
}

void UIPauseMenuButton::draw() {
    if(!this->bVisible) return;

    // draw image
    if(Image *image = this->getImage(); image && image != MISSING_TEXTURE) {
        g->setColor(argb(this->fAlpha, 1.0f, 1.0f, 1.0f));
        g->pushTransform();
        {
            // scale
            g->scale(this->vScale.x, this->vScale.y);

            // center and draw
            g->translate(this->getPos().x + (int)(this->getSize().x / 2),
                         this->getPos().y + (int)(this->getSize().y / 2));
            g->drawImage(image);
        }
        g->popTransform();
    }
}

void UIPauseMenuButton::setBaseScale(float xScale, float yScale) {
    this->vBaseScale.x = xScale;
    this->vBaseScale.y = yScale;

    this->vScale = this->vBaseScale;
}

void UIPauseMenuButton::onMouseInside() {
    CBaseUIButton::onMouseInside();

    if(env->winFocused()) soundEngine->play(osu->getSkin()->s_menu_hover);

    const float animationDuration = 0.09f;
    anim::moveLinear(&this->vScale.x, this->vBaseScale.x * this->fScaleMultiplier, animationDuration, true);
    anim::moveLinear(&this->vScale.y, this->vBaseScale.y * this->fScaleMultiplier, animationDuration, true);

    if(this->getName() == US_("Resume")) {
        soundEngine->play(osu->getSkin()->s_hover_pause_continue);
    } else if(this->getName() == US_("Retry")) {
        soundEngine->play(osu->getSkin()->s_hover_pause_retry);
    } else if(this->getName() == US_("Quit")) {
        soundEngine->play(osu->getSkin()->s_hover_pause_back);
    }
}

void UIPauseMenuButton::onMouseOutside() {
    CBaseUIButton::onMouseOutside();

    const float animationDuration = 0.09f;
    anim::moveLinear(&this->vScale.x, this->vBaseScale.x, animationDuration, true);
    anim::moveLinear(&this->vScale.y, this->vBaseScale.y, animationDuration, true);
}

Image *UIPauseMenuButton::getImage() const {
    if(!this->imageMember) return MISSING_TEXTURE;
    if(const auto *skin = osu->getSkin()) {
        return skin->*this->imageMember;
    }
    return MISSING_TEXTURE;
}
