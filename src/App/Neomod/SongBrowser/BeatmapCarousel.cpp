// Copyright (c) 2025, WH, All rights reserved.

#include "BeatmapCarousel.h"
#include "CBaseUIEventCtx.h"
#include "ContainerRanges.h"
#include "CollectionButton.h"
#include "CarouselButton.h"
#include "SongButton.h"
#include "SongDifficultyButton.h"
#include "UI.h"
#include "OptionsOverlay.h"
#include "Engine.h"
#include "Mouse.h"
#include "Algorithms.h"

bool BeatmapCarousel::songButtonComparator(const CBaseUIElement *a, const CBaseUIElement *b) {
    return a->getRelPos().y < b->getRelPos().y;
}

BeatmapCarousel::BeatmapCarousel(float xPos, float yPos, float xSize, float ySize, std::string name)
    : CBaseUIScrollView(xPos, yPos, xSize, ySize, std::move(name)) {
    this->setDrawBackground(false);
    this->setDrawFrame(false);
    this->setHorizontalScrolling(false);
    this->setScrollResistance(15);
    this->container.setDrawOrderComparatorFunc(BeatmapCarousel::songButtonComparator);
}

BeatmapCarousel::~BeatmapCarousel() {
    // the buttons are owned by the pool, so invalidate the container to avoid double-free
    this->invalidate();
}

void BeatmapCarousel::draw() {
    this->updateWindow();
    CBaseUIScrollView::draw();
}

void BeatmapCarousel::tick() {
    this->updateWindow();
    CBaseUIScrollView::tick();
    for(auto *button : this->windowButtons) {
        if(button->node.isDiff() && button->isVisible()) this->diffsShownSinceReset.insert(button->node.idx);
    }
    if(this->updateWindow() && !(this->bScrolling || this->bScrollbarScrolling)) this->updateClipping();

    if(!this->isVisible()) {
        // just reset this as a precaution
        this->bIsScrollingFast = false;
        return;
    }

    // update scrolling relative velocity, as a cache for isScrollingFast
    const f64 absYVelocity = std::abs(this->getVelocity().y);
    if(absYVelocity == 0.0) {
        // sanity
        this->bIsScrollingFast = false;
        return;
    }

    if(this->rows.empty()) {
        return;
    }

    const f64 elemSizeY = CarouselButton::getScaledBaseSize().y;
    if(elemSizeY == 0) {
        return;
    }

    const f64 sizeY = this->getSize().y;
    if(sizeY == 0.0) {
        return;
    }

    const f64 wholeElementsScrolled = absYVelocity / elemSizeY;

    // if our velocity is more than 4 vertical screens' worth of items, we are scrolling fast
    this->bIsScrollingFast = wholeElementsScrolled > ((sizeY / elemSizeY) * 4);
}

void BeatmapCarousel::onCapturedMouseMove() {
    CBaseUIScrollView::onCapturedMouseMove();
    if(this->updateWindow()) this->updateClipping();
}

bool BeatmapCarousel::setRows(std::vector<CarouselNode> newRows, std::vector<f32> newRowY) {
    assert(newRows.size() == newRowY.size());
    const bool changed = newRows != this->rows;
    if(changed) {
        this->resetAnimations();
        this->rows = std::move(newRows);
        this->hoveredRow = {};
    }
    this->setRowPositions(std::move(newRowY));
    return changed;
}

void BeatmapCarousel::clearRows() {
    this->rows.clear();
    this->rowY.clear();
    this->hoveredRow = {};
    this->bWindowDirty = true;
}

void BeatmapCarousel::setRowPositions(std::vector<f32> newRowY) {
    this->rowY = std::move(newRowY);

    const vec2 rowSize = CarouselButton::getScaledBaseSize();
    const f64 border = (f64)(i32)(this->getSize().y / 2);
    const dvec2 extent{this->rowY.empty() ? 0. : rowSize.x, this->rowY.empty() ? 0. : this->rowY.back() + rowSize.y};
    this->setScrollSize(extent + dvec2{border, border});
    if(const f64 lowest = Mc::min(this->getSize().y - this->getScrollSize().y, 1.); this->getRelPosY() < lowest) {
        this->scrollToY((i32)lowest, false);
    }

    this->bWindowDirty = true;
}

size_t BeatmapCarousel::findRow(CarouselNode node) const {
    return Mc::ranges::find(this->rows, node) - this->rows.begin();
}

void BeatmapCarousel::resetAnimations() {
    this->diffsShownSinceReset.clear();
    for(auto *button : this->windowButtons) button->resetAnimations();
}

CarouselButton *BeatmapCarousel::acquireButton(CarouselNode node) {
    auto &free = this->freeButtons[(size_t)node.kind];
    CarouselButton *button = nullptr;
    if(!free.empty()) {
        button = free.back();
        free.pop_back();
    } else {
        switch(node.kind) {
            case CarouselNode::Kind::SET:
                button = this->buttons.emplace_back(std::make_unique<SongButton>(*this)).get();
                break;
            case CarouselNode::Kind::DIFF:
                button = this->buttons.emplace_back(std::make_unique<SongDifficultyButton>(*this)).get();
                break;
            case CarouselNode::Kind::GROUP:
            case CarouselNode::Kind::NONE:
                button = this->buttons.emplace_back(std::make_unique<CollectionButton>(*this)).get();
                break;
        }
    }

    button->assign(node, node.isDiff() && this->diffsShownSinceReset.contains(node.idx));
    return button;
}

void BeatmapCarousel::releaseButton(CarouselButton *button) {
    if(button->isVisible()) button->setVisible(false);
    button->bMouseInside = false;
    button->stealFocus();
    this->freeButtons[(size_t)button->node.kind].push_back(button);
    button->node = {};
}

bool BeatmapCarousel::updateWindow() {
    size_t begin = 0;
    size_t end = 0;
    if(!this->rowY.empty()) {
        const f32 rowHeight = CarouselButton::getScaledBaseSize().y;
        const f32 slack = this->getSize().y * (CLIPPING_SCALE - 1.f) / 2.f + rowHeight;
        const f32 top = -(f32)this->getRelPosY() - slack;
        const f32 bottom = top + this->getSize().y + 2.f * slack;
        begin = Mc::lower_bound(this->rowY.begin(), this->rowY.end(), top - rowHeight) - this->rowY.begin();
        end = Mc::lower_bound(this->rowY.begin() + (sSz)begin, this->rowY.end(), bottom) - this->rowY.begin();
    }
    if(!this->bWindowDirty && begin == this->windowBegin && end == this->windowEnd) return false;
    this->bWindowDirty = false;

    std::vector<CarouselButton *> previous = std::move(this->windowButtons);
    this->windowButtons.clear();
    this->container.invalidate();
    for(size_t i = begin; i < end; i++) {
        const CarouselNode node = this->rows[i];
        CarouselButton *button = nullptr;
        if(auto it = Mc::ranges::find(previous, node, &CarouselButton::node); it != previous.end()) {
            button = *it;
            previous.erase(it);
            button->setTargetRelPosY(this->rowY[i]);
        } else {
            button = this->acquireButton(node);
            button->setTargetRelPosY(this->rowY[i]);
            this->applyMoveAwayState(button, false);
        }
        button->updateLayoutEx();
        this->windowButtons.push_back(button);
        this->container.addBaseUIElementRaw(button);
    }
    for(auto *button : previous) this->releaseButton(button);

    this->windowBegin = begin;
    this->windowEnd = end;
    this->bClippingDirty = true;
    return true;
}

void BeatmapCarousel::applyMoveAwayState(CarouselButton *row, bool animate) const {
    using enum CarouselButton::MOVE_AWAY_STATE;
    CarouselButton::MOVE_AWAY_STATE state = MOVE_CENTER;
    if(this->hoveredRow && row->node != this->hoveredRow) {
        state = row->fTargetRelPosY < this->hoveredRowY ? MOVE_UP : MOVE_DOWN;
    }
    row->setMoveAwayState(state, animate);
}

void BeatmapCarousel::setHoveredRow(CarouselButton *row) {
    this->hoveredRow = row->node;
    this->hoveredRowY = row->fTargetRelPosY;
    for(auto *button : this->windowButtons) this->applyMoveAwayState(button, true);
}

void BeatmapCarousel::clearHoveredRow(CarouselButton *row) {
    // only reset all other rows if we still should do so (possible frame delay of onMouseOutside coming
    // together with the next row already getting onMouseInside!)
    if(row->moveAwayState != CarouselButton::MOVE_AWAY_STATE::MOVE_CENTER) return;
    this->hoveredRow = {};
    for(auto *button : this->windowButtons) this->applyMoveAwayState(button, true);
}

void BeatmapCarousel::updateInput(CBaseUIEventCtx &c) {
    this->updateWindow();
    CBaseUIScrollView::updateInput(c);
    if(!this->isVisible()) {
        return;
    }

    // handle right click absolute scrolling
    if(c.propagate_clicks) {
        if(mouse->isRightDown()) {
            if(!this->bRightClickScrollCheck) {
                this->bRightClickScrollCheck = true;

                bool isMouseInsideAnySongButton = false;
                for(auto *button : this->windowButtons) {
                    if(button->isMouseInside()) {
                        isMouseInsideAnySongButton = true;
                        break;
                    }
                }

                this->bRightClickScrolling =
                    this->isMouseInside() && !ui->getOptionsOverlay()->isMouseInside() && !isMouseInsideAnySongButton;
            }
        } else {
            this->bRightClickScrollCheck = false;
            this->bRightClickScrolling = false;
        }

        if(this->bRightClickScrolling) {
            // map onto the scrollbar's track, the upper/lower bounds of the carousel are hidden behind the bars
            const f64 trackTop = this->getPos().y + this->fScrollbarInsetTop;
            const f64 trackHeight = this->getSize().y - this->fScrollbarInsetTop - this->fScrollbarInsetBottom;
            const f64 mouseYPct = Mc::clamp((mouse->getPos().y - 2.0 - trackTop) / trackHeight, 0.0, 1.0);
            this->scrollToY(static_cast<int>(-mouseYPct * this->getScrollSize().y));
        }
    }
}
