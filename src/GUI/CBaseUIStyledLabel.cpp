// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIStyledLabel.h"

#include "Font.h"
#include "UIStyle.h"

#include <utility>

CBaseUIStyledLabel::CBaseUIStyledLabel(std::string text, std::string name)
    : CBaseUIElement(0, 0, 0, 0, std::move(name)), text(std::move(text)) {
    // (text, not a button: a click goes to what's beneath)
    this->setHandleLeftMouse(false);
}

CBaseUIStyledLabel::~CBaseUIStyledLabel() = default;

CBaseUIStyledLabel *CBaseUIStyledLabel::setText(std::string text) {
    if(text == this->text) return this;
    this->text = std::move(text);
    this->requestLayout();
    return this;
}

CBaseUIStyledLabel *CBaseUIStyledLabel::setJustification(TEXT_JUSTIFICATION justification) {
    this->justification = justification;
    return this;
}

CBaseUIStyledLabel *CBaseUIStyledLabel::setDim(bool dim) {
    this->dim = dim;
    return this;
}

void CBaseUIStyledLabel::draw() {
    if(!this->isVisible()) return;
    uiStyle().drawText(this->getRect(), this->text, this->justification,
                       this->isEnabled() ? UIStyle::ControlState::NORMAL : UIStyle::ControlState::DISABLED, this->dim);
}

vec2 CBaseUIStyledLabel::getNaturalSize() {
    const McFont *font = uiStyle().font();
    return {font->getStringWidth(this->text), font->getHeight()};
}
