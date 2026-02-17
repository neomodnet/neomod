// Copyright (c) 2026, WH, All rights reserved.
#include "UIScreen.h"
#include "UI.h"

UIOverlay::UIOverlay(UIScreen *parent) : UIScreen(), parent(parent) {}

UIScreen* UIOverlay::getParent() {
    if(!this->parent) return ui->getActiveScreen();
    return this->parent;
}

void UIOverlay::setParent(UIScreen* parent) { this->parent = parent; }
