// Copyright (c) 2016, PG, All rights reserved.
#include "UIBackButton.h"

#include <utility>

#include "AnimationHandler.h"
#include "OsuConVars.h"
#include "Engine.h"
#include "OptionsOverlay.h"
#include "Osu.h"
#include "Skin.h"
#include "SkinImage.h"
#include "SoundEngine.h"
#include "UI.h"
#include "Graphics.h"

UIBackButton::UIBackButton(float xPos, float yPos, float xSize, float ySize, std::string name)
    : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name), "") {
    // the back button draws on top of the screen body but is visited first, so rank it above the body
    this->bDrawsOnTop = true;
}

UIBackButton::~UIBackButton() = default;

void UIBackButton::draw() {
    if(!this->bVisible) return;

    // draw button image
    g->pushTransform();
    {
        g->setColor(0xffffffff);

        const SkinImage &backimg =
            this->bUseDefaultBack ? osu->getSkin()->i_menu_back2_DEFAULTSKIN : osu->getSkin()->i_menu_back2;

        backimg.draw(this->getPos() + (backimg.getSize() / 2.f), 1.f,
                     this->fAnimation * 0.25f /* hover animation brightness */);
    }
    g->popTransform();

    this->bFocusStolenDelay = false;
}

void UIBackButton::updateInput(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;
    CBaseUIButton::updateInput(c);
}

void UIBackButton::onMouseDownInside(MouseButtonFlags buttons) {
    CBaseUIButton::onMouseDownInside(buttons);

    soundEngine->play(osu->getSkin()->s_click_back_button);
}

static float button_sound_cooldown = 0.f;
void UIBackButton::onMouseInside() {
    CBaseUIButton::onMouseInside();
    if(this->bFocusStolenDelay) return;

    this->fAnimation.set(1.0f, 0.15f, anim::QuadOut);
    if(button_sound_cooldown + 0.05f < engine->getTime()) {
        button_sound_cooldown = engine->getTime();
        soundEngine->play(osu->getSkin()->s_hover_back_button);
    }
}

void UIBackButton::onMouseOutside() {
    CBaseUIButton::onMouseOutside();

    this->fAnimation.set(0.0f, this->fAnimation * 0.1f, anim::QuadOut);
}

void UIBackButton::updateLayout() {
    const SkinImage *backimg = &osu->getSkin()->i_menu_back2;

    if(OptionsOverlay *optmenu = ui ? ui->getOptionsOverlay() : nullptr;
       optmenu && optmenu->isVisible() && backimg->getSize().y > (optmenu->getSize().y / 4) &&
       (osu->getSkin()->i_menu_back2_DEFAULTSKIN.isReady())) {
        // always show default back button when options menu is showing, if its height is > 1/4 the options menu height
        backimg = &osu->getSkin()->i_menu_back2_DEFAULTSKIN;
        this->bUseDefaultBack = true;
    } else {
        this->bUseDefaultBack = false;
    }

    this->setSize(backimg->getSize());
}

void UIBackButton::resetAnimation() {
    this->fAnimation.stop();
    this->fAnimation = 0.0f;
}

void UIBackButton::onFocusStolen() {
    CBaseUIButton::onFocusStolen();

    this->bMouseInside = false;
    this->bFocusStolenDelay = true;
}
