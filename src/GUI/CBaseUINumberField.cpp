// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUINumberField.h"

#include "ContainerRanges.h"
#include "Engine.h"
#include "KeyBindings.h"
#include "Keyboard.h"
#include "KeyboardEvent.h"
#include "Mouse.h"
#include "Parsing.h"
#include "UIStyle.h"

#include "fmt/format.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

namespace {
// how far the cursor goes before a press is a drag (units)
constexpr float DRAG_THRESHOLD{3.f};
// how far it goes for a step: wider for a range of few steps, so that the whole of it is some way to drag
constexpr float STEP_WIDTH{4.f};
constexpr float MAX_STEP_WIDTH{20.f};
constexpr float RANGE_WIDTH{200.f};

// (no number is the same as no number)
bool same(f64 a, f64 b) { return a == b || (std::isnan(a) && std::isnan(b)); }
}  // namespace

CBaseUINumberField::CBaseUINumberField(std::string name) : CBaseUIStyledTextbox(std::move(name)) {
    this->setNaturalWidth(72.f);
    this->setFilter([](char32_t c) { return (c >= U'0' && c <= U'9') || c == U'.' || c == U',' || c == U'-'; });
    this->setText(this->format(false));
}

CBaseUINumberField::~CBaseUINumberField() = default;

CBaseUINumberField *CBaseUINumberField::setRange(f64 min, f64 max) {
    this->min = min;
    this->max = max;
    return this;
}

CBaseUINumberField *CBaseUINumberField::setStep(f64 step, f64 fine) {
    assert(step > 0.0 && fine > 0.0);
    this->step = step;
    this->fine = fine;
    return this;
}

CBaseUINumberField *CBaseUINumberField::setDecimals(int decimals) {
    this->decimals = decimals;
    if(!this->typing) this->setText(this->format(false));
    return this;
}

CBaseUINumberField *CBaseUINumberField::setSuffix(std::string suffix) {
    this->suffix = std::move(suffix);
    if(!this->typing) this->setText(this->format(false));
    return this;
}

CBaseUINumberField *CBaseUINumberField::setNamedValue(f64 value, std::string text) {
    this->named.emplace(value, std::move(text));
    if(!this->typing) this->setText(this->format(false));
    return this;
}

CBaseUINumberField *CBaseUINumberField::setValue(f64 value) {
    this->settled = value;
    if(same(value, this->value)) return this;
    this->value = value;
    if(!this->typing) this->setText(this->format(false));
    return this;
}

CBaseUINumberField *CBaseUINumberField::setChangeCallback(std::function<void(f64)> callback) {
    this->changeCallback = std::move(callback);
    return this;
}

void CBaseUINumberField::visit() {
    this->typing = true;
    this->setText(this->format(true));
    this->typingText = this->text;
    CBaseUIStyledTextbox::visit();
}

void CBaseUINumberField::commit() {
    if(this->typing) {
        this->takeTyped();
        this->setText(this->format(true));
        this->typingText = this->text;
        this->settle();
    }
    CBaseUIStyledTextbox::commit();
}

void CBaseUINumberField::revert() {
    CBaseUIStyledTextbox::revert();
    this->value = this->settled;
    if(this->typing) {
        this->setText(this->format(true));
        this->typingText = this->text;
    }
}

std::optional<CURSORTYPE> CBaseUINumberField::getCursorShape() {
    return this->typing || std::isnan(this->value) ? CURSORTYPE::CURSOR_TEXT : CURSORTYPE::CURSOR_SIZE_H;
}

void CBaseUINumberField::onKeyDown(KeyboardEvent &e) {
    if(this->typing && this->isVisible() && (e == KEY_UP || e == KEY_DOWN)) {
        e.consume();
        // (from what's typed so far)
        this->takeTyped();
        if(std::isnan(this->value)) return;
        const f64 by = (e.getModifiers() & KEYMOD_SHIFT) ? this->fine : this->step;
        this->value = this->fit(this->value + (e == KEY_UP ? by : -by));
        this->setText(this->format(true));
        this->typingText = this->text;
        this->selectAll();
        return;
    }
    CBaseUIStyledTextbox::onKeyDown(e);
}

void CBaseUINumberField::onFocusStolen() {
    CBaseUIStyledTextbox::onFocusStolen();
    if(this->typing) {
        this->typing = false;
        this->setText(this->format(false));
    }
}

void CBaseUINumberField::onMouseDownInside(bool left, bool right) {
    // (typed in, it's a textbox)
    if(this->typing) {
        CBaseUIStyledTextbox::onMouseDownInside(left, right);
        return;
    }
    CBaseUIElement::onMouseDownInside(left, right);
    if(!left) return;

    this->lockCapture();
    this->pressed = true;
    this->pressX = mouse->getPos().x;
    this->pressValue = this->value;
    this->seenSteps = this->steps = this->fineSteps = 0;
}

void CBaseUINumberField::onCapturedMouseMove() {
    if(!this->pressed) {
        if(this->typing) CBaseUIStyledTextbox::onCapturedMouseMove();
        return;
    }
    const UIStyle &style = uiStyle();
    const float moved = mouse->getPos().x - this->pressX;
    if(std::isnan(this->pressValue) || (!this->dragging && std::abs(moved) <= style.px(DRAG_THRESHOLD))) return;
    this->dragging = true;

    // the steps the cursor made since the last update count as fine ones with Shift; back where it was pressed, with
    // none left, the value is the one it had
    const f64 inRange = (this->max - this->min) / this->step;
    const float width = style.px((float)std::clamp<f64>(RANGE_WIDTH / inRange, STEP_WIDTH, MAX_STEP_WIDTH));
    const i32 now = (i32)(moved / width);
    (keyboard->isShiftDown() ? this->fineSteps : this->steps) += now - this->seenSteps;
    this->seenSteps = now;
    this->show(this->steps == 0 && this->fineSteps == 0
                   ? this->pressValue
                   : this->fit(this->pressValue + this->steps * this->step + this->fineSteps * this->fine));
}

void CBaseUINumberField::onMouseUpInside(bool left, bool right) {
    if(!this->pressed) {
        if(this->typing) CBaseUIStyledTextbox::onMouseUpInside(left, right);
        return;
    }
    CBaseUIElement::onMouseUpInside(left, right);
    const bool clicked = !this->dragging;
    this->pressed = this->dragging = false;
    if(clicked) {
        this->visit();
    } else {
        this->settle();
    }
}

void CBaseUINumberField::onMouseUpOutside(bool left, bool right) {
    if(!this->pressed) {
        if(this->typing) CBaseUIStyledTextbox::onMouseUpOutside(left, right);
        return;
    }
    CBaseUIElement::onMouseUpOutside(left, right);
    this->pressed = this->dragging = false;
    this->settle();
}

void CBaseUINumberField::onMouseCancel() {
    if(this->dragging) this->show(this->settled);
    this->pressed = this->dragging = false;
    CBaseUIStyledTextbox::onMouseCancel();
}

std::string CBaseUINumberField::format(bool typing) const {
    if(std::isnan(this->value)) return {};
    if(!typing && this->named && this->named->first == this->value) return this->named->second;
    // (a value with more decimals than the user can set shows them all)
    const f64 scale = std::pow(10.0, this->decimals);
    std::string out = std::round(this->value * scale) / scale == this->value
                          ? fmt::format("{:.{}f}", this->value, this->decimals)
                          : fmt::format("{}", this->value);
    if(out.contains('.') && !out.contains('e')) {
        out.erase(out.find_last_not_of('0') + 1);
        if(out.ends_with('.')) out.pop_back();
    }
    if(out == "-0") out = "0";
    if(!typing) out.append(this->suffix);
    return out;
}

f64 CBaseUINumberField::fit(f64 v) const {
    const f64 scale = std::pow(10.0, this->decimals);
    return std::round(std::clamp(v, this->min, this->max) * scale) / scale;
}

void CBaseUINumberField::show(f64 v) {
    if(same(v, this->value)) return;
    this->value = v;
    if(!this->typing) this->setText(this->format(false));
}

void CBaseUINumberField::settle() {
    if(same(this->value, this->settled)) return;
    this->settled = this->value;
    if(this->changeCallback) this->changeCallback(this->value);
}

void CBaseUINumberField::takeTyped() {
    if(this->text == this->typingText) return;
    std::string typed = this->text;
    Mc::ranges::replace(typed, ',', '.');
    if(f64 number{0.0}; Parsing::parse(typed, &number) && std::isfinite(number)) this->value = this->fit(number);
}
