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

void UIStyle::drawControl(const McRect &rect, ControlState state, bool checked) const {
    switch(state) {
        case ControlState::NORMAL:
            g->setColor(checked ? this->palette.accent : this->palette.control);
            break;
        case ControlState::HOVERED:
            g->setColor(checked ? this->palette.accentHovered : this->palette.controlHovered);
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

void UIStyle::drawPopup(const McRect &rect) const {
    g->setColor(this->palette.control);
    g->fillRect(rect);
    g->setColor(this->palette.border);
    g->drawRect(rect);
}

void UIStyle::drawRow(const McRect &rect, ControlState state) const {
    if(state != ControlState::HOVERED && state != ControlState::PRESSED) return;
    g->setColor(state == ControlState::PRESSED ? this->palette.accent : this->palette.controlHovered);
    g->fillRect(rect);
}

void UIStyle::drawSeparator(const McRect &rect) const {
    g->setColor(this->palette.border);
    g->fillRectf(rect.getX(), std::round(rect.getY() + rect.getHeight() / 2.f), rect.getWidth(), 1.f);
}

void UIStyle::drawMark(const McRect &rect, Mark mark, ControlState state) const {
    g->setColor(state == ControlState::DISABLED ? this->palette.textDisabled : this->palette.text);
    const vec2 c = rect.getCenter();
    const float r = std::round(this->px(4.f));
    // (two strokes a pixel apart for some weight)
    for(float o = 0.f; o < 2.f; o += 1.f) {
        switch(mark) {
            case Mark::CHECK:
                g->drawLinef(c.x - r, c.y + o, c.x - r / 3.f, c.y + r * 2.f / 3.f + o);
                g->drawLinef(c.x - r / 3.f, c.y + r * 2.f / 3.f + o, c.x + r, c.y - r * 2.f / 3.f + o);
                break;
            case Mark::SUBMENU:
                g->drawLinef(c.x - r / 2.f + o, c.y - r, c.x + r / 2.f + o, c.y);
                g->drawLinef(c.x + r / 2.f + o, c.y, c.x - r / 2.f + o, c.y + r);
                break;
        }
    }
}

UIStyle &uiStyle() {
    static UIStyle style;
    return style;
}
