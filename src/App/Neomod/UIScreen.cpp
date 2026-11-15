// Copyright (c) 2026, WH, All rights reserved.
#include "UIScreen.h"
#include "UI.h"
#include "Engine.h"

void UIScreen::tick() {
    this->lastTickFrame = engine->getFrameCount();
    CBaseUIContainer::tick();
}

CBaseUIElement* UIScreen::getTextTarget(CBaseUIElement* focus) {
    if(!this->isVisible()) return nullptr;
    if(focus) return focus;
    return this->isModal() ? this : nullptr;
}

UIOverlay::UIOverlay(UIScreen* parent) : UIScreen(), parent(parent) {}

UIScreen* UIOverlay::getParent() {
    if(!this->parent) return ui->getActiveScreen();
    return this->parent;
}

void UIOverlay::setParent(UIScreen* parent) { this->parent = parent; }
