// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIStyledTextbox.h"

#include "KeyBindings.h"
#include "KeyboardEvent.h"
#include "UIStyle.h"
#include "UniString.h"

#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace {
// a key a focused field leaves to whatever handles keys after it
bool passesOn(const KeyboardEvent &e) {
    const SCANCODE key = e.getScanCode();
    if(key == KEY_TAB || (key >= KEY_F1 && key <= KEY_F12)) return true;

    const KEYMOD held = e.getModifiers();
    if(held & KEYMOD_SUPER) return true;
    // (Ctrl with Alt is AltGr, which types)
    if(!(held & KEYMOD_CONTROL) || (held & KEYMOD_ALT)) return false;
    switch(key) {
        case KEY_A:
        case KEY_C:
        case KEY_V:
        case KEY_X:
        case KEY_Z:
        case KEY_BACKSPACE:
        case KEY_DELETE:
        case KEY_LEFT:
        case KEY_RIGHT:
        case KEY_HOME:
        case KEY_END:
            return false;
        default:
            return true;
    }
}
}  // namespace

CBaseUIStyledTextbox::CBaseUIStyledTextbox(std::string name) : CBaseUITextbox(0, 0, 0, 0, std::move(name)) {
    // (the style draws them)
    this->setDrawBackground(false);
    this->setDrawFrame(false);
}

CBaseUIStyledTextbox::~CBaseUIStyledTextbox() = default;

CBaseUIStyledTextbox *CBaseUIStyledTextbox::setNaturalWidth(float units) {
    if(units == this->naturalWidth) return this;
    this->naturalWidth = units;
    this->requestLayout();
    return this;
}

CBaseUIStyledTextbox *CBaseUIStyledTextbox::setChangeCallback(std::function<void(std::string_view)> callback) {
    this->changeCallback = std::move(callback);
    return this;
}

CBaseUIStyledTextbox *CBaseUIStyledTextbox::setFilter(std::function<bool(char32_t)> filter) {
    this->filter = std::move(filter);
    return this;
}

void CBaseUIStyledTextbox::visit() {
    this->requestFocus();
    this->selectAll();
    this->tickCaret();
}

void CBaseUIStyledTextbox::commit() {
    if(!this->visitText) return;
    const bool changed = this->text != *this->visitText;
    this->visitText.reset();
    if(changed && this->changeCallback) this->changeCallback(this->text);
}

void CBaseUIStyledTextbox::revert() {
    if(!this->visitText) return;
    this->setText(*this->visitText);
    this->visitText.reset();
}

void CBaseUIStyledTextbox::draw() {
    if(!this->isVisible()) return;

    using enum UIStyle::ControlState;
    const UIStyle::ControlState state = !this->isEnabled() ? DISABLED : this->bMouseInside ? HOVERED : NORMAL;
    const UIStyle &style = uiStyle();
    style.drawField(this->getRect(), state, this->isFocused());

    // the text, the selection and the caret (which keeps its blinking) in the style's colours
    this->textColor = state == DISABLED ? style.palette.textDisabled : style.palette.text;
    this->selectionColor = style.palette.accent;
    const float caretAlpha = this->caretColor.Af();
    this->caretColor = style.palette.text;
    this->caretColor.setA(caretAlpha);
    CBaseUITextbox::draw();
}

void CBaseUIStyledTextbox::onKeyDown(KeyboardEvent &e) {
    if(!this->isFocused() || !this->isVisible() || passesOn(e)) return;

    const SCANCODE key = e.getScanCode();
    if(key == KEY_ESCAPE || (key == KEY_Z && (e.getModifiers() & KEYMOD_CONTROL) && !(e.getModifiers() & KEYMOD_ALT))) {
        e.consume();
        this->revert();
        this->stealFocus();
        return;
    }
    if(key == KEY_ENTER || key == KEY_NUMPAD_ENTER) {
        e.consume();
        this->stealFocus();
        return;
    }

    if(!this->visitText) this->visitText = this->text;
    const std::string before = this->text;
    CBaseUITextbox::onKeyDown(e);
    this->edited(before);
}

void CBaseUIStyledTextbox::onTextInput(std::string_view text) {
    std::string taken;
    for(const char32_t character : UniString::codepoints(text)) {
        if(this->takes(character)) taken += UniString::to_utf8(std::u32string_view{&character, 1});
    }
    if(taken.empty()) return;

    if(!this->visitText) this->visitText = this->text;
    const std::string before = this->text;
    CBaseUITextbox::onTextInput(taken);
    this->edited(before);
}

void CBaseUIStyledTextbox::onTextComposition(const TextComposition &composition) {
    // composing over the selection edits the text too
    if(!composition.text.empty() && !this->visitText) this->visitText = this->text;
    CBaseUITextbox::onTextComposition(composition);
}

void CBaseUIStyledTextbox::onFocusStolen() {
    CBaseUITextbox::onFocusStolen();
    this->commit();
}

vec2 CBaseUIStyledTextbox::getNaturalSize() {
    const UIStyle &style = uiStyle();
    return {style.px(this->naturalWidth), style.px(style.metrics.controlHeight)};
}

void CBaseUIStyledTextbox::onResized() {
    const UIStyle &style = uiStyle();
    this->iTextOffsetX = (int)std::round(style.px(style.metrics.fieldPadding));
    CBaseUITextbox::onResized();
}

bool CBaseUIStyledTextbox::takes(char32_t character) const {
    return character >= 32 && character != 127 && (!this->filter || this->filter(character));
}

void CBaseUIStyledTextbox::edited(const std::string &before) {
    if(this->text == before) return;

    // (a paste brings whatever the clipboard has)
    std::u32string kept;
    for(const char32_t character : UniString::to_utf32(this->text)) {
        if(this->takes(character)) kept.push_back(character);
    }
    if(std::string filtered = UniString::to_utf8(kept); filtered != this->text) this->setText(std::move(filtered));
}
