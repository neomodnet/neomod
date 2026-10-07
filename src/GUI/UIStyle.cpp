// Copyright (c) 2026, WH, All rights reserved.
#include "UIStyle.h"

#include "CBaseUIElement.h"
#include "Engine.h"
#include "Font.h"
#include "Graphics.h"

#include <cmath>

void UIStyle::setScale(float scale) {
    if(scale == this->scale) return;
    this->scale = scale;
    this->generation++;
}

McFont *UIStyle::font() const { return engine->getDefaultFont(); }

void UIStyle::drawPanel(const McRect &rect) const {
    g->setColor(this->palette.panel);
    g->fillRect(rect);
    g->setColor(this->palette.border);
    g->drawRect(rect);
}

void UIStyle::drawControl(const McRect &rect, ControlState state) const {
    switch(state) {
        case ControlState::NORMAL:
            g->setColor(this->palette.control);
            break;
        case ControlState::HOVERED:
            g->setColor(this->palette.controlHovered);
            break;
        case ControlState::PRESSED:
            g->setColor(this->palette.accent);
            break;
        case ControlState::DISABLED:
            g->setColor(this->palette.panel);
            break;
    }
    g->fillRect(rect);
    g->setColor(this->palette.border);
    g->drawRect(rect);
}

void UIStyle::drawText(const McRect &rect, std::string_view text, TEXT_JUSTIFICATION justification, ControlState state,
                       bool dim) const {
    McFont *font = this->font();
    float x = rect.getX();
    if(justification != TEXT_JUSTIFICATION::LEFT) {
        const float free = rect.getWidth() - font->getStringWidth(text);
        x += justification == TEXT_JUSTIFICATION::CENTERED ? free / 2.f : free;
    }

    g->setColor(state == ControlState::DISABLED ? this->palette.textDisabled
                : dim                           ? this->palette.textDim
                                                : this->palette.text);
    g->pushTransform();
    {
        g->translate(std::round(x), std::round(rect.getY() + rect.getHeight() / 2.f + font->getHeight() / 2.f));
        g->drawString(font, text);
    }
    g->popTransform();
}

UIStyle &uiStyle() {
    static UIStyle style;
    return style;
}
