// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIStyledLabel.h"

#include "Font.h"
#include "UIStyle.h"
#include "UniString.h"

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
    this->fit();
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

CBaseUIStyledLabel *CBaseUIStyledLabel::setCutInMiddle(bool middle) {
    this->cutInMiddle = middle;
    this->fit();
    return this;
}

void CBaseUIStyledLabel::draw() {
    if(!this->isVisible()) return;
    uiStyle().drawText(this->getRect(), this->getShownText(), this->justification,
                       this->isEnabled() ? UIStyle::ControlState::NORMAL : UIStyle::ControlState::DISABLED, this->dim);
}

void CBaseUIStyledLabel::onResized() { this->fit(); }

void CBaseUIStyledLabel::fit() {
    this->cut.clear();
    const McFont *font = uiStyle().font();
    // a box rounds edges, which can leave a fitted label a pixel short of its text
    const float room = this->getSize().x + 1.f;
    if(this->getSize().x <= 0.f || font->getStringWidth(this->text) <= room) return;
    // a character less at a time: from the end, or by turns from either side of the middle
    const uSz size = this->text.size();
    uSz head = this->cutInMiddle ? UniString::prev(this->text, size / 2 + 1) : size;
    uSz tail = head;
    bool fromTail = false;
    do {
        if(tail < size && (fromTail || head == 0)) {
            tail = UniString::next(this->text, tail);
        } else {
            head = UniString::prev(this->text, head);
        }
        fromTail = this->cutInMiddle && !fromTail;
        this->cut = this->text.substr(0, head) + "..." + this->text.substr(tail);
    } while((head > 0 || tail < size) && font->getStringWidth(this->cut) > room);
}

vec2 CBaseUIStyledLabel::getNaturalSize() {
    const McFont *font = uiStyle().font();
    return {font->getStringWidth(this->text), font->getHeight()};
}
