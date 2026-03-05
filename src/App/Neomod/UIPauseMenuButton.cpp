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

// #include "Logging.h"
// #include "Timing.h"

UIPauseMenuButton::UIPauseMenuButton(ImageSkinMember imageMember, float xPos, float yPos, float xSize, float ySize,
                                     UString name)
    : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name)) {
    this->imageMember = imageMember;

    this->fScaleX = 1.f;
    this->fScaleY = 1.f;
    this->fScaleMultiplier = 1.1f;

    this->fAlpha = 1.0f;
}

UIPauseMenuButton::~UIPauseMenuButton() = default;

void UIPauseMenuButton::draw() {
    if(!this->bVisible) return;

    // draw image
    if(Image *image = this->getImage(); image && image != MISSING_TEXTURE) {
        g->setColor(argb(this->fAlpha, this->fBrightness, this->fBrightness, this->fBrightness));
        g->pushTransform();
        {
            // scale
            g->scale(this->fScaleX, this->fScaleY);

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

    this->fScaleX = this->vBaseScale.x;
    this->fScaleY = this->vBaseScale.y;
}

void UIPauseMenuButton::onMouseInside() {
    // debugLog("{} {}", Timing::getTicksNS(), engine->getFrameCount());
    CBaseUIButton::onMouseInside();

    const float animationDuration = 0.09f;
    this->fScaleX.set(this->vBaseScale.x * this->fScaleMultiplier, animationDuration, anim::Linear);
    this->fScaleY.set(this->vBaseScale.y * this->fScaleMultiplier, animationDuration, anim::Linear);

    if(!env->winFocused()) return;

    if(auto *skin = osu->getSkin()) {
        Sound *toPlay = nullptr;
        const UString &name = this->getName();
        if(name == US_("Resume")) {
            toPlay = skin->s_hover_pause_continue;
        } else if(name == US_("Retry")) {
            toPlay = skin->s_hover_pause_retry;
        } else if(name == US_("Quit")) {
            toPlay = skin->s_hover_pause_back;
        }
        soundEngine->play(toPlay);
    }
}

void UIPauseMenuButton::onMouseOutside() {
    CBaseUIButton::onMouseOutside();

    const float animationDuration = 0.09f;
    this->fScaleX.set(this->vBaseScale.x, animationDuration, anim::Linear);
    this->fScaleY.set(this->vBaseScale.y, animationDuration, anim::Linear);
}

void UIPauseMenuButton::onDisabled() {
    CBaseUIButton::onDisabled();
    if(this->bMouseInside) {
        this->bMouseInside = false;
        this->onMouseOutside();
    }
}

Image *UIPauseMenuButton::getImage() const {
    if(!this->imageMember) return MISSING_TEXTURE;
    if(const auto *skin = osu->getSkin()) {
        return skin->*this->imageMember;
    }
    return MISSING_TEXTURE;
}
