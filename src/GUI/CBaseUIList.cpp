// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIList.h"

#include "CBaseUIDispatch.h"
#include "Engine.h"
#include "Graphics.h"
#include "KeyBindings.h"
#include "Keyboard.h"
#include "KeyboardEvent.h"
#include "Mouse.h"
#include "UIStyle.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
// (units)
constexpr float TRACK_WIDTH{10.f};
constexpr float MIN_THUMB{24.f};
constexpr float BAR_WIDTH{4.f};
constexpr float FLEX_WIDTH{60.f};  // a column without a width needs this much
constexpr int WHEEL_ROWS{3};
constexpr uSz NATURAL_ROWS{6};
}  // namespace

CBaseUIList::CBaseUIList(const Model &model, std::vector<Column> columns, std::string name)
    : CBaseUIElement(0, 0, 0, 0, std::move(name)), model(model), columns(std::move(columns)) {}

CBaseUIList::~CBaseUIList() = default;

CBaseUIList *CBaseUIList::setSelection(std::span<const u64> keys) {
    this->selection.clear();
    this->selection.insert(keys.begin(), keys.end());
    if(this->anchor && !this->selection.contains(*this->anchor)) this->anchor.reset();
    if(this->cursor && !this->selection.contains(*this->cursor)) this->cursor.reset();
    if(keys.size() == 1) this->anchor = this->cursor = keys.front();
    return this;
}

CBaseUIList *CBaseUIList::setSelectionCallback(std::function<void()> callback) {
    this->selectionCallback = std::move(callback);
    return this;
}

CBaseUIList *CBaseUIList::setActivateCallback(std::function<void(uSz)> callback) {
    this->activateCallback = std::move(callback);
    return this;
}

McRect CBaseUIList::getRowsRect() const {
    const UIStyle &style = uiStyle();
    const McRect &rect = this->getRect();
    const float header = style.px(style.metrics.rowHeight);
    return {rect.getX(), rect.getY() + header, std::max(0.f, rect.getWidth() - style.px(TRACK_WIDTH)),
            std::max(0.f, rect.getHeight() - header)};
}

float CBaseUIList::getMaxScroll() const {
    const UIStyle &style = uiStyle();
    return std::max(
        0.f, (float)this->model.getRowCount() * style.px(style.metrics.rowHeight) - this->getRowsRect().getHeight());
}

std::optional<std::pair<float, float>> CBaseUIList::getThumb() const {
    const float max = this->getMaxScroll();
    const float track = this->getRowsRect().getHeight();
    if(max <= 0.f || track <= 0.f) return std::nullopt;
    const float height = std::min(track, std::max(uiStyle().px(MIN_THUMB), track * track / (track + max)));
    return std::pair{(track - height) * std::clamp(this->scroll, 0.f, max) / max, height};
}

std::pair<uSz, uSz> CBaseUIList::getRowsInView() const {
    const UIStyle &style = uiStyle();
    const float rowHeight = style.px(style.metrics.rowHeight);
    const uSz count = this->model.getRowCount();
    const float top = std::clamp(this->scroll, 0.f, this->getMaxScroll());
    const uSz first = std::min(count, (uSz)(top / rowHeight));
    const uSz last = std::min(count, (uSz)std::ceil((top + this->getRowsRect().getHeight()) / rowHeight));
    return {first, last};
}

std::optional<uSz> CBaseUIList::rowAt(float y) const {
    const UIStyle &style = uiStyle();
    const McRect rows = this->getRowsRect();
    if(y < rows.getY() || y >= rows.getY() + rows.getHeight()) return std::nullopt;
    const uSz row = (uSz)((y - rows.getY() + std::clamp(this->scroll, 0.f, this->getMaxScroll())) /
                          style.px(style.metrics.rowHeight));
    return row < this->model.getRowCount() ? std::optional{row} : std::nullopt;
}

std::optional<uSz> CBaseUIList::rowOf(u64 key) const {
    for(uSz row = 0, count = this->model.getRowCount(); row < count; row++) {
        if(this->model.getRowKey(row) == key) return row;
    }
    return std::nullopt;
}

void CBaseUIList::scrollBy(float px) { this->scroll = std::clamp(this->scroll + px, 0.f, this->getMaxScroll()); }

void CBaseUIList::scrollTo(uSz row) {
    const UIStyle &style = uiStyle();
    const float rowHeight = style.px(style.metrics.rowHeight);
    const float top = (float)row * rowHeight;
    const float view = this->getRowsRect().getHeight();
    // a list without room for a row (not laid out yet) shows it first
    if(top < this->scroll || view < rowHeight) {
        this->scroll = top;
    } else if(top + rowHeight > this->scroll + view) {
        this->scroll = top + rowHeight - view;
    }
    this->scroll = std::clamp(this->scroll, 0.f, this->getMaxScroll());
}

void CBaseUIList::selectionChanged() {
    if(this->selectionCallback) this->selectionCallback();
}

void CBaseUIList::pick(uSz row, bool extend) {
    const u64 key = this->model.getRowKey(row);
    const std::optional<uSz> from = extend && this->anchor ? this->rowOf(*this->anchor) : std::nullopt;
    this->selection.clear();
    if(from) {
        for(uSz i = std::min(*from, row); i <= std::max(*from, row); i++) {
            this->selection.insert(this->model.getRowKey(i));
        }
    } else {
        this->selection.insert(key);
        this->anchor = key;
    }
    this->cursor = key;
    this->scrollTo(row);
    this->selectionChanged();
}

void CBaseUIList::draw() {
    if(!this->isVisible()) return;

    using enum UIStyle::ControlState;
    const UIStyle &style = uiStyle();
    const McRect &rect = this->getRect();
    const McRect rows = this->getRowsRect();
    const float rowHeight = style.px(style.metrics.rowHeight);
    const float gutter = style.px(style.metrics.markWidth);
    const float padding = style.px(style.metrics.fieldPadding);
    style.drawField(rect, this->isEnabled() ? NORMAL : DISABLED, this->isFocused());

    // the columns' places: after the gutter, the ones without a width sharing what's left
    float fixed = gutter;
    uSz flexible = 0;
    for(const Column &column : this->columns) {
        fixed += style.px(column.width);
        flexible += column.width <= 0.f;
    }
    const float share = flexible > 0 ? std::max(0.f, rows.getWidth() - fixed) / (float)flexible : 0.f;
    const auto cells = [&](float y, bool header, uSz row) {
        float x = rows.getX() + gutter;
        for(uSz column = 0; column < this->columns.size(); column++) {
            const float width = this->columns[column].width > 0.f ? style.px(this->columns[column].width) : share;
            const McRect cell{x + padding, y, std::max(0.f, width - padding), rowHeight};
            if(header) {
                style.drawText(cell, this->columns[column].title, TEXT_JUSTIFICATION::LEFT, NORMAL, true);
            } else {
                style.drawText(cell, this->model.getCellText(row, column), TEXT_JUSTIFICATION::LEFT, NORMAL);
            }
            x += width;
        }
    };
    cells(rect.getY(), true, 0);
    style.drawSeparator({rect.getX(), rows.getY() - 1.f, rect.getWidth(), 2.f});

    this->scroll = std::clamp(this->scroll, 0.f, this->getMaxScroll());
    const std::optional<uSz> hovered =
        this->bMouseInside && !this->grabbed && mouse->getPos().x < rows.getX() + rows.getWidth()
            ? this->rowAt(mouse->getPos().y)
            : std::nullopt;
    const auto [first, last] = this->getRowsInView();
    g->pushClipRect(rows);
    for(uSz row = first; row < last; row++) {
        const float y = rows.getY() + (float)row * rowHeight - this->scroll;
        const McRect rowRect{rows.getX(), y, rows.getWidth(), rowHeight};
        style.drawRow(rowRect, hovered == row ? HOVERED : NORMAL, this->selection.contains(this->model.getRowKey(row)));
        const RowLook look = this->model.getRowLook(row);
        if(look.bar) {
            g->setColor(*look.bar);
            g->fillRect(McRect{rows.getX() + 1.f, y + 2.f, style.px(BAR_WIDTH), rowHeight - 4.f});
        }
        if(look.marked) {
            style.drawMark({rows.getX() + style.px(BAR_WIDTH), y, gutter - style.px(BAR_WIDTH), rowHeight},
                           UIStyle::Mark::SUBMENU, NORMAL);
        }
        cells(y, false, row);
    }
    g->popClipRect();

    if(const auto thumb = this->getThumb()) {
        const float x = rows.getX() + rows.getWidth();
        style.drawControl({x + 2.f, rows.getY() + thumb->first, style.px(TRACK_WIDTH) - 4.f, thumb->second},
                          this->grabbed ? PRESSED : NORMAL);
    }
}

void CBaseUIList::onKeyDown(KeyboardEvent &e) {
    if(!this->isFocused() || !this->isVisible() || !this->isEnabled()) return;
    const uSz count = this->model.getRowCount();
    if(count == 0) return;
    const KEYMOD held = e.getModifiers();
    if((held & KEYMOD_ALT) || (held & KEYMOD_SUPER)) return;

    if(held & KEYMOD_CONTROL) {
        if(e != KEY_A) return;
        e.consume();
        this->selection.clear();
        for(uSz row = 0; row < count; row++) this->selection.insert(this->model.getRowKey(row));
        this->selectionChanged();
        return;
    }

    const std::optional<uSz> at = this->cursor ? this->rowOf(*this->cursor) : std::nullopt;
    if(e == KEY_ENTER || e == KEY_NUMPAD_ENTER) {
        if(!at) return;
        e.consume();
        if(this->activateCallback) this->activateCallback(*at);
        return;
    }

    const UIStyle &style = uiStyle();
    const sSz page = std::max<sSz>(1, (sSz)(this->getRowsRect().getHeight() / style.px(style.metrics.rowHeight)) - 1);
    sSz to{0};
    switch(e.getScanCode()) {
        case KEY_UP:
            to = at ? (sSz)*at - 1 : (sSz)count - 1;
            break;
        case KEY_DOWN:
            to = at ? (sSz)*at + 1 : 0;
            break;
        case KEY_PAGEUP:
            to = at ? (sSz)*at - page : 0;
            break;
        case KEY_PAGEDOWN:
            to = at ? (sSz)*at + page : (sSz)count - 1;
            break;
        case KEY_HOME:
            to = 0;
            break;
        case KEY_END:
            to = (sSz)count - 1;
            break;
        default:
            return;
    }
    e.consume();
    this->pick((uSz)std::clamp<sSz>(to, 0, (sSz)count - 1), held & KEYMOD_SHIFT);
}

vec2 CBaseUIList::getNaturalSize() {
    const UIStyle &style = uiStyle();
    float width = style.px(style.metrics.markWidth) + style.px(TRACK_WIDTH);
    for(const Column &column : this->columns) width += style.px(column.width > 0.f ? column.width : FLEX_WIDTH);
    return {width, style.px(style.metrics.rowHeight) * (float)(NATURAL_ROWS + 1)};
}

void CBaseUIList::onMouseDownInside(bool left, bool right) {
    CBaseUIElement::onMouseDownInside(left, right);
    if(!left) return;
    this->requestFocus();

    const McRect rows = this->getRowsRect();
    const vec2 at = mouse->getPos();
    if(at.x >= rows.getX() + rows.getWidth()) {
        // the thumb is dragged; a press beside it brings its middle there first
        const auto thumb = this->getThumb();
        if(!thumb || at.y < rows.getY()) return;
        const float top = rows.getY() + thumb->first;
        this->grabbed = at.y >= top && at.y < top + thumb->second ? at.y - top : thumb->second / 2.f;
        this->lockCapture();
        this->onCapturedMouseMove();
        return;
    }

    const std::optional<uSz> row = this->rowAt(at.y);
    if(!row) {
        // (the header keeps the selection, the space under the rows drops it)
        if(at.y >= rows.getY() && !this->selection.empty()) {
            this->selection.clear();
            this->anchor.reset();
            this->cursor.reset();
            this->selectionChanged();
        }
        return;
    }
    if(CBaseUIDispatch::getClicks(this->model.getRowKey(*row)) >= 2) {
        if(this->activateCallback) this->activateCallback(*row);
        return;
    }
    if(keyboard->isShiftDown()) {
        this->pick(*row, true);
    } else if(keyboard->isControlDown()) {
        const u64 key = this->model.getRowKey(*row);
        if(!this->selection.erase(key)) this->selection.insert(key);
        this->anchor = this->cursor = key;
        this->selectionChanged();
    } else {
        this->pick(*row, false);
    }
}

void CBaseUIList::onCapturedMouseMove() {
    if(!this->grabbed) return;
    const auto thumb = this->getThumb();
    if(!thumb) return;
    const McRect rows = this->getRowsRect();
    const float free = rows.getHeight() - thumb->second;
    if(free <= 0.f) return;
    const float top = std::clamp(mouse->getPos().y - rows.getY() - *this->grabbed, 0.f, free);
    this->scroll = this->getMaxScroll() * top / free;
}

void CBaseUIList::onMouseUpInside(bool left, bool right) {
    CBaseUIElement::onMouseUpInside(left, right);
    this->grabbed.reset();
}

void CBaseUIList::onMouseUpOutside(bool left, bool right) {
    CBaseUIElement::onMouseUpOutside(left, right);
    this->grabbed.reset();
}

void CBaseUIList::onMouseCancel() {
    this->grabbed.reset();
    CBaseUIElement::onMouseCancel();
}

bool CBaseUIList::onWheel(WheelDelta vertical, WheelDelta /*horizontal*/) {
    const UIStyle &style = uiStyle();
    // (up goes back)
    this->scrollBy((float)(-vertical.ticks * WHEEL_ROWS) * style.px(style.metrics.rowHeight));
    return true;
}
