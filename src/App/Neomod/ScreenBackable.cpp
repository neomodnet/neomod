// Copyright (c) 2016, PG, All rights reserved.
#include "ScreenBackable.h"

#include "Engine.h"
#include "ConVar.h"
#include "OsuKeyBinds.h"
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

    // the discord activity still has screens to go back from, like the map selection of a room
    if(Osu::isKioskMode() && !Osu::isDiscordActivity()) {
        this->backable = false;
    }
}

ScreenBackable::~ScreenBackable() = default;

void ScreenBackable::draw() {
    if(!this->bVisible) return;
    UIScreen::draw();
    if(this->backable) this->backButton->draw();
}

void ScreenBackable::tick() {
    UIScreen::tick();
    this->backButton->tick();
}

void ScreenBackable::updateInput(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;
    if(this->backable) {
        // back button ranks above the body it draws over via its bDrawsOnTop flag (set in its ctor)
        this->backButton->updateInput(c);
    }
    UIScreen::updateInput(c);
}

void ScreenBackable::onKeyDown(KeyboardEvent &e) {
    UIScreen::onKeyDown(e);
    if(!this->bVisible || e.isConsumed() || !this->backable) return;
    if(e == KEY_ESCAPE || e == binds::GAME_PAUSE) {
        soundEngine->play(osu->getSkin()->s_menu_back);
        this->onBack();
        e.consume();
        return;
    }
}

void ScreenBackable::updateLayout() {
    if(!this->backable) return;
    this->backButton->updateLayout();
    this->backButton->setPosY((float)osu->getVirtScreenHeight() - this->backButton->getSize().y);
}

void ScreenBackable::onResolutionChange(vec2 /*newResolution*/) { this->updateLayout(); }
