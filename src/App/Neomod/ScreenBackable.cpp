// Copyright (c) 2016, PG, All rights reserved.
#include "ScreenBackable.h"

#include "Engine.h"
#include "OsuConVars.h"
#include "KeyBindings.h"
#include "Osu.h"
#include "Skin.h"
#include "SoundEngine.h"
#include "UIBackButton.h"
#include "MakeDelegateWrapper.h"
#include "Mouse.h"

ScreenBackable::ScreenBackable() : UIScreen(), backButton(std::make_unique<UIBackButton>(-1.f, 0.f, 0.f, 0.f, "")) {
    this->backButton->setClickCallback(SA::MakeDelegate<&ScreenBackable::onBack>(this));

    this->updateLayout();
}

ScreenBackable::~ScreenBackable() = default;

void ScreenBackable::draw() {
    if(!this->bVisible) return;
    UIScreen::draw();
    this->backButton->draw();
}

void ScreenBackable::update(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;
    this->backButton->update(c);
    if(c.mouse_consumed()) return;
    UIScreen::update(c);
}

void ScreenBackable::onKeyDown(KeyboardEvent &e) {
    UIScreen::onKeyDown(e);
    if(!this->bVisible || e.isConsumed()) return;

    if(e == KEY_ESCAPE || e == cv::GAME_PAUSE.getVal<SCANCODE>()) {
        soundEngine->play(osu->getSkin()->s_menu_back);
        this->onBack();
        e.consume();
        return;
    }
}

void ScreenBackable::updateLayout() {
    this->backButton->updateLayout();
    this->backButton->setPosY((float)osu->getVirtScreenHeight() - this->backButton->getSize().y);
}

void ScreenBackable::onResolutionChange(vec2 /*newResolution*/) { this->updateLayout(); }
