// Copyright (c) 2026, kiwec, All rights reserved.
#include "UIDropdownTextbox.h"

#include "Font.h"
#include "Graphics.h"
#include "Icons.h"
#include "Engine.h"
#include "Mouse.h"
#include "Osu.h"
#include "Skin.h"
#include "SoundEngine.h"
#include "UniString.h"

UIDropdownTextbox::UIDropdownTextbox(float xPos, float yPos, float xSize, float ySize, std::string name)
    : CBaseUITextbox(xPos, yPos, xSize, ySize, std::move(name)) {}

void UIDropdownTextbox::draw() {
    CBaseUITextbox::draw();
    if(!this->bVisible || !this->bShowDropdown) return;

    const McRect rect = this->getDropdownRect();
    g->setColor(this->backgroundColor);
    g->fillRect(rect.getPos().x, rect.getPos().y + 1, rect.getSize().x - 1, rect.getSize().y - 1);
    g->setColor(this->frameColor);
    g->drawLine(rect.getPos().x, rect.getPos().y, rect.getPos().x, rect.getPos().y + rect.getSize().y);

    McFont *iconFont = osu->getFontIcons();
    const std::string glyph = UniString::to_utf8(std::u32string_view{&Icons::CARET_DOWN, 1});
    const float scale = rect.getSize().y * 0.5f / iconFont->getHeight();
    g->setColor(this->isMouseInsideDropdown() ? 0xffffffff : 0xffaaaaaa);
    g->pushTransform();
    {
        g->scale(scale, scale);
        g->translate(std::round(rect.getCenter().x - iconFont->getStringWidth(glyph) * scale / 2.f),
                     std::round(rect.getCenter().y + iconFont->getHeight() * scale / 2.f));
        g->drawString(iconFont, glyph);
    }
    g->popTransform();
}

std::optional<CURSORTYPE> UIDropdownTextbox::getCursorShape() {
    if(this->isMouseInsideDropdown()) return std::nullopt;
    return CBaseUITextbox::getCursorShape();
}

void UIDropdownTextbox::onMouseDownInside(MouseButtonFlags buttons) {
    this->bDropdownPressed = this->isMouseInsideDropdown();
    if(this->bDropdownPressed) {
        CBaseUIElement::onMouseDownInside(buttons);
        soundEngine->play(osu->getSkin()->s_click_button);
        return;
    }
    CBaseUITextbox::onMouseDownInside(buttons);
}

void UIDropdownTextbox::onMouseUpInside(MouseButtonFlags buttons) {
    if(this->bDropdownPressed) {
        this->bDropdownPressed = false;
        CBaseUIElement::onMouseUpInside(buttons);
        if(this->isMouseInsideDropdown() && this->dropdownCallback != nullptr) this->dropdownCallback();
        return;
    }
    CBaseUITextbox::onMouseUpInside(buttons);
}

void UIDropdownTextbox::onCapturedMouseMove() {
    if(!this->bDropdownPressed) CBaseUITextbox::onCapturedMouseMove();
}

McRect UIDropdownTextbox::getDropdownRect() const {
    return {this->getPos().x + this->getSize().x - this->getSize().y, this->getPos().y, this->getSize().y,
            this->getSize().y};
}

bool UIDropdownTextbox::isMouseInsideDropdown() const {
    return this->bShowDropdown && this->getDropdownRect().contains(mouse->getPos());
}
