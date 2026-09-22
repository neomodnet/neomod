// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUISelectableTextView.h"

#include "CBaseUIDispatch.h"
#include "Engine.h"
#include "Environment.h"
#include "Font.h"
#include "Graphics.h"
#include "Mouse.h"

#include <algorithm>
#include <cmath>
#include <ranges>

vec2 CBaseUISelectableTextView::getContentOrigin() const {
    // where the base puts its container: whole pixels (CBaseUIScrollView::tick)
    return vec2{vec::round(dvec2{this->getPos()} + dvec2{this->vScrollPos})};
}

void CBaseUISelectableTextView::updateInput(CBaseUIEventCtx &c) {
    CBaseUIScrollView::updateInput(c);

    // text cursor over the text, the scrollbar keeps the arrow
    if(this->isMouseInside()) {
        const bool overScrollbar =
            this->vScrollSize.y > this->getSize().y && this->verticalScrollbar.contains(mouse->getPos());
        env->setCursor(overScrollbar ? CURSORTYPE::CURSOR_NORMAL : CURSORTYPE::CURSOR_TEXT);
    }
}

void CBaseUISelectableTextView::onMouseOutside() {
    CBaseUIScrollView::onMouseOutside();
    env->setCursor(CURSORTYPE::CURSOR_NORMAL);
}

CBaseUISelectableTextView::TextPos CBaseUISelectableTextView::hitTest(vec2 pos) const {
    const size_t count = this->getRunCount();
    if(count == 0) return {};

    const vec2 origin = this->getContentOrigin();
    const auto indices = std::views::iota(size_t{0}, count);

    // the row under pos: the last run starting above it and the runs sharing its y (above the first row: its start)
    const auto next =
        std::ranges::upper_bound(indices, pos.y, {}, [&](size_t i) { return origin.y + this->getRun(i).pos.y; });
    if(next == indices.begin()) return {};
    const size_t last = static_cast<size_t>(next - indices.begin()) - 1;
    const float rowY = this->getRun(last).pos.y;
    size_t first = last;
    while(first > 0 && this->getRun(first - 1).pos.y == rowY) first--;

    // below the row (past the last one, or in a gap between rows): its end
    const TextRun lastRun = this->getRun(last);
    if(pos.y >= origin.y + lastRun.pos.y + lastRun.height)
        return {.run = last, .byte = lastRun.text.size(), .x = lastRun.width};

    // the run under pos.x: the first one not ending before it (right of them all: the last one's end)
    size_t i = first;
    for(; i < last; i++) {
        const TextRun run = this->getRun(i);
        if(pos.x < origin.x + run.pos.x + run.width * run.scale) break;
    }
    const TextRun run = this->getRun(i);
    const size_t byte = run.font->hitTest(run.text, (pos.x - origin.x - run.pos.x) / run.scale);
    return {.run = i, .byte = byte, .x = run.font->getStringWidth(run.text.substr(0, byte))};
}

std::string CBaseUISelectableTextView::getSelectedText() const {
    if(!this->hasSelection()) return {};

    const auto [from, to] = std::minmax(this->selAnchor, this->selHead);
    std::string out;
    for(size_t i = from.run; i <= to.run && i < this->getRunCount(); i++) {
        const TextRun run = this->getRun(i);
        if(i > from.run) out += run.separator;
        const size_t b0 = std::min((i == from.run) ? from.byte : 0, run.text.size());
        const size_t b1 = std::min((i == to.run) ? to.byte : run.text.size(), run.text.size());
        if(b1 > b0) out += run.text.substr(b0, b1 - b0);
    }
    return out;
}

void CBaseUISelectableTextView::drawSelection() const {
    if(!this->hasSelection()) return;

    const size_t count = this->getRunCount();
    const auto [from, to] = std::minmax(this->selAnchor, this->selHead);
    if(from.run >= count) return;

    const vec2 origin = this->getContentOrigin();
    const McRect clip = this->getClipRect();

    // only the runs inside the view (reading order: a run's bottom edge is never above the previous one's)
    const auto indices = std::views::iota(from.run, std::min(to.run + 1, count));
    auto it = std::ranges::lower_bound(indices, clip.getY(), {}, [&](size_t i) {
        const TextRun run = this->getRun(i);
        return origin.y + run.pos.y + run.height;
    });

    g->pushClipRect(clip);
    g->setColor(this->selectionColor);
    for(; it != indices.end(); ++it) {
        const size_t i = *it;
        const TextRun run = this->getRun(i);
        const float y = origin.y + run.pos.y;
        if(y >= clip.getMaxY()) break;

        // as wide as the selected characters; a run the selection continues past gets a space-wide sliver after its
        // text for the separator (newline or wrap space) it copies
        const float x0 = (i == from.run) ? from.x : 0.f;
        float x1 = (i == to.run) ? to.x : run.width;
        if(i != to.run && i + 1 < count && !this->getRun(i + 1).separator.empty()) x1 += run.font->getGlyphWidth(U' ');
        if(x1 <= x0) continue;  // the selection ends at the start of its last run

        g->fillRect((int)(origin.x + run.pos.x + x0 * run.scale), (int)y, (int)((x1 - x0) * run.scale),
                    (int)run.height);
    }
    g->popClipRect();
}

void CBaseUISelectableTextView::shiftSelection(size_t removedRuns) {
    if(!this->bHasSelection) return;
    for(TextPos *pos : {&this->selAnchor, &this->selHead}) {
        if(pos->run < removedRuns)
            *pos = {};
        else
            pos->run -= removedRuns;
    }
}

void CBaseUISelectableTextView::beginSelection(vec2 pos) {
    this->selAnchor = this->selHead = this->hitTest(pos);
    this->bHasSelection = this->bSelecting = true;
}

void CBaseUISelectableTextView::onMouseDownInside(bool left, bool /*right*/) {
    if(!left) return;

    // the scrollbar keeps its drag, a press on the text starts a selection instead of the base's drag-scroll
    this->bBusy = true;
    if(!this->tryBeginScrollbarDrag(mouse->getPos())) this->beginSelection(mouse->getPos());
    this->lockCapture();
}

void CBaseUISelectableTextView::onMouseUpInside(bool left, bool right) {
    CBaseUIScrollView::onMouseUpInside(left, right);
    this->bSelecting = false;
}

void CBaseUISelectableTextView::onMouseUpOutside(bool left, bool right) {
    CBaseUIScrollView::onMouseUpOutside(left, right);
    this->bSelecting = false;
}

void CBaseUISelectableTextView::onMouseCancel() {
    CBaseUIScrollView::onMouseCancel();
    this->bSelecting = false;
}

void CBaseUISelectableTextView::onCapturedMouseMove() {
    if(!this->bSelecting) {
        CBaseUIScrollView::onCapturedMouseMove();  // scrollbar drag
        return;
    }
    if(this->getRunCount() == 0) return;  // the text went away under the drag

    const vec2 pos = mouse->getPos();
    this->selHead = this->hitTest(pos);

    // dragging past the top/bottom edge scrolls the text along (a row per 1/30 s), no further than the content
    const int step =
        std::max(1, (int)std::round(engine->getFrameTime() * 30. * this->getRun(this->selHead.run).height));
    if(pos.y < this->getPos().y)
        this->scrollToY((int)std::round(this->vScrollPos.y) + step, false);
    else if(pos.y > this->getPos().y + this->getSize().y)
        this->scrollToY((int)std::round(this->vScrollPos.y) - step, false);
}

void CBaseUISelectableTextView::onCapturedMoveThrough() {
    // a child took a left press inside us: it keeps its click, but a drag past the scroll resistance takes the
    // capture away from it and selects from where the press landed (where the base would drag-scroll)
    if(!flags::has<MouseButtonFlags::MF_LEFT>(CBaseUIDispatch::getCaptorButtons())) return;

    const vec2 pos = mouse->getPos();
    if(!this->bBusy) {
        // first observed frame: the press
        if(!this->bHandleLeftMouse || !this->bMouseInside || !this->isEnabled()) return;
        this->bBusy = true;
        this->vPressPos = pos;

        // a press on the scrollbar force-steals even from a child underneath it
        if(!this->bBlockScrolling && (this->bVerticalScrolling || this->bHorizontalScrolling) &&
           (this->verticalScrollbar.contains(pos) || this->horizontalScrollbar.contains(pos))) {
            if(this->stealCapture()) this->tryBeginScrollbarDrag(pos);
            return;
        }
    }

    const vec2 pull = vec::abs(pos - this->vPressPos);
    if(std::max(pull.x, pull.y) > (float)this->iScrollResistance && this->stealCapture()) {
        this->beginSelection(this->vPressPos);
        this->selHead = this->hitTest(pos);
    }
}
