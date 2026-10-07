// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIStyledButton.h"

#include "Font.h"
#include "UIStyle.h"

#include <utility>

CBaseUIStyledButton::CBaseUIStyledButton(std::string text, std::string name)
    : CBaseUIButton(0, 0, 0, 0, std::move(name), std::move(text)) {}

CBaseUIStyledButton::~CBaseUIStyledButton() = default;

CBaseUIStyledButton *CBaseUIStyledButton::setChecked(bool checked) {
    this->checked = checked;
    return this;
}

void CBaseUIStyledButton::draw() {
    if(!this->isVisible()) return;

    using enum UIStyle::ControlState;
    const UIStyle::ControlState state = !this->isEnabled()                    ? DISABLED
                                        : this->bActive && this->bMouseInside ? PRESSED
                                        : this->bMouseInside                  ? HOVERED
                                                                              : NORMAL;
    const UIStyle &style = uiStyle();
    style.drawControl(this->getRect(), state, this->checked);
    style.drawText(this->getRect(), this->getText(), TEXT_JUSTIFICATION::CENTERED, state);
}

vec2 CBaseUIStyledButton::getNaturalSize() {
    const UIStyle &style = uiStyle();
    return {style.font()->getStringWidth(this->getText()) + 2.f * style.px(style.metrics.controlPadding),
            style.px(style.metrics.controlHeight)};
}
