// Copyright (c) 2025, WH, All rights reserved.

#include "Logging.h"
#include "BeatmapCarousel.h"
#include "CollectionButton.h"
#include "SongBrowser.h"
#include "CarouselButton.h"
#include "SongButton.h"
#include "SongDifficultyButton.h"
#include "UI.h"
#include "UIContextMenu.h"
#include "OptionsOverlay.h"
#include "Engine.h"
#include "Mouse.h"
#include "Keyboard.h"

using namespace neomod::sbr;

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
    // elements are free'd manually/externally by SongBrowser, so invalidate the container to avoid double-free
    // TODO: factor this out from SongBrowser
    this->invalidate();
}

void BeatmapCarousel::draw() {
    this->updateWindow();
    CBaseUIScrollView::draw();
}

void BeatmapCarousel::tick() {
    CBaseUIScrollView::tick();
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

void BeatmapCarousel::setRows(std::vector<CarouselButton *> newRows) {
    this->container.invalidate();
    this->bClippingDirty = true;
    this->rows = std::move(newRows);
    this->rowY.clear();
    this->windowBegin = this->windowEnd = 0;
    this->hoveredRow = nullptr;
    this->bRowsReplaced = true;
}

void BeatmapCarousel::onRowsMoved() {
    dvec2 extent{0., 0.};
    this->rowY.resize(this->rows.size());
    for(size_t i = 0; i < this->rows.size(); i++) {
        const CarouselButton *row = this->rows[i];
        this->rowY[i] = row->fTargetRelPosY;
        extent = glm::max(extent, dvec2{row->getRelPos() + row->getSize()});
    }
    const f64 border = (f64)(i32)(this->getSize().y / 2);
    this->setScrollSize(extent + dvec2{border, border});

    this->updateWindow();

    // rows that were visible in what the rows were before and are now off screen, as the clipping would have
    if(this->bRowsReplaced) {
        this->bRowsReplaced = false;
        for(size_t i = 0; i < this->rows.size(); i++) {
            if(i >= this->windowBegin && i < this->windowEnd) continue;
            if(this->rows[i]->isVisible()) this->rows[i]->setVisible(false);
        }
    }
}

bool BeatmapCarousel::updateWindow() {
    size_t begin = 0;
    size_t end = 0;
    if(!this->rowY.empty()) {
        // the clipping slack of CBaseUIScrollView::updateClipping plus a row for the hover offsets
        const f32 rowHeight = CarouselButton::getScaledBaseSize().y;
        const f32 slack = this->getSize().y * 0.075f + rowHeight;
        const f32 top = this->getPos().y - this->container.getPos().y - slack;
        const f32 bottom = top + this->getSize().y + 2.f * slack;
        begin = std::lower_bound(this->rowY.begin(), this->rowY.end(), top - rowHeight) - this->rowY.begin();
        end = std::lower_bound(this->rowY.begin() + (sSz)begin, this->rowY.end(), bottom) - this->rowY.begin();
    }
    if(begin == this->windowBegin && end == this->windowEnd) return false;

    // rows scrolled out of the window get hidden, as the clipping would have
    for(size_t i = this->windowBegin; i < this->windowEnd; i++) {
        if((i < begin || i >= end) && this->rows[i]->isVisible()) this->rows[i]->setVisible(false);
    }

    this->container.invalidate();
    const vec2 containerPos = this->container.getPos();
    for(size_t i = begin; i < end; i++) {
        CarouselButton *row = this->rows[i];
        if(i < this->windowBegin || i >= this->windowEnd) {
            this->applyMoveAwayState(row, false);
            row->setRect(McRect{containerPos + row->getRelPos(), row->getSize()});
        }
        this->container.addBaseUIElementRaw(row);
    }

    this->windowBegin = begin;
    this->windowEnd = end;
    this->bClippingDirty = true;
    return true;
}

void BeatmapCarousel::applyMoveAwayState(CarouselButton *row, bool animate) const {
    using enum CarouselButton::MOVE_AWAY_STATE;
    CarouselButton::MOVE_AWAY_STATE state = MOVE_CENTER;
    if(this->hoveredRow != nullptr && row != this->hoveredRow) {
        state = row->fTargetRelPosY < this->hoveredRow->fTargetRelPosY ? MOVE_UP : MOVE_DOWN;
    }
    row->setMoveAwayState(state, animate);
}

void BeatmapCarousel::setHoveredRow(CarouselButton *row) {
    this->hoveredRow = row;
    for(size_t i = this->windowBegin; i < this->windowEnd; i++) {
        this->applyMoveAwayState(this->rows[i], true);
    }
}

void BeatmapCarousel::clearHoveredRow(CarouselButton *row) {
    // only reset all other rows if we still should do so (possible frame delay of onMouseOutside coming
    // together with the next row already getting onMouseInside!)
    if(row->moveAwayState != CarouselButton::MOVE_AWAY_STATE::MOVE_CENTER) return;
    this->hoveredRow = nullptr;
    for(size_t i = this->windowBegin; i < this->windowEnd; i++) {
        this->applyMoveAwayState(this->rows[i], true);
    }
}

void BeatmapCarousel::updateInput(CBaseUIEventCtx &c) {
    CBaseUIScrollView::updateInput(c);
    if(!this->isVisible()) {
        return;
    }

    // handle right click absolute scrolling
    if(c.propagate_clicks) {
        if(mouse->isRightDown() && !g_songbrowser->contextMenu->isMouseInside()) {
            if(!g_songbrowser->bSongBrowserRightClickScrollCheck) {
                g_songbrowser->bSongBrowserRightClickScrollCheck = true;

                bool isMouseInsideAnySongButton = false;
                {
                    // only rows in the container can be hovered
                    const std::vector<CarouselButton *> &buttons = this->container.getElementsAs<CarouselButton>();
                    for(auto *button : buttons) {
                        if(button->isMouseInside()) {
                            isMouseInsideAnySongButton = true;
                            break;
                        }
                    }
                }

                if(this->isMouseInside() && !ui->getOptionsOverlay()->isMouseInside() && !isMouseInsideAnySongButton)
                    g_songbrowser->bSongBrowserRightClickScrolling = true;
                else
                    g_songbrowser->bSongBrowserRightClickScrolling = false;
            }
        } else {
            g_songbrowser->bSongBrowserRightClickScrollCheck = false;
            g_songbrowser->bSongBrowserRightClickScrolling = false;
        }

        if(g_songbrowser->bSongBrowserRightClickScrolling) {
            // map onto the scrollbar's track, the upper/lower bounds of the carousel are hidden behind the bars
            const f64 trackTop = this->getPos().y + this->fScrollbarInsetTop;
            const f64 trackHeight = this->getSize().y - this->fScrollbarInsetTop - this->fScrollbarInsetBottom;
            const f64 mouseYPct = std::clamp((mouse->getPos().y - 2.0 - trackTop) / trackHeight, 0.0, 1.0);
            this->scrollToY(static_cast<int>(-mouseYPct * this->getScrollSize().y));
        }
    }
}

void BeatmapCarousel::onKeyUp(KeyboardEvent & /*e*/) { /*this->container.onKeyUp(e);*/ ; }

// don't consume keys, we are not a keyboard listener, but called from SongBrowser::onKeyDown manually
void BeatmapCarousel::onKeyDown(KeyboardEvent &key) {
    /*this->container.onKeyDown(e);*/

    const auto &elements{this->rows};

    // selection move
    if(!keyboard->isAltDown() && key == KEY_DOWN) {
        // get bottom selection
        int selectedIndex = -1;
        for(int i = 0; i < elements.size(); i++) {
            if(elements[i]->isSelected()) selectedIndex = i;
        }

        // select +1
        if(selectedIndex > -1 && selectedIndex + 1 < elements.size()) {
            int nextSelectionIndex = selectedIndex + 1;
            auto *nextButton = elements[nextSelectionIndex];

            nextButton->select({.noSelectBottomChild = true});

            // if this is a song button, select top child
            if(nextButton->isType<SongButton>()) {
                const auto &children = nextButton->getChildren();
                if(children.size() > 0 && !children[0]->isSelected())
                    children[0]->select({.noSelectBottomChild = true, .parentUnselected = true});
            }
        }
    }

    if(!keyboard->isAltDown() && key == KEY_UP) {
        // get bottom selection
        int selectedIndex = -1;
        for(int i = 0; i < elements.size(); i++) {
            if(elements[i]->isSelected()) selectedIndex = i;
        }

        // select -1
        if(selectedIndex > -1 && selectedIndex - 1 > -1) {
            int nextSelectionIndex = selectedIndex - 1;
            auto *nextButton = elements[nextSelectionIndex];

            nextButton->select();
            const bool isCollectionButton = nextButton->isType<CollectionButton>();

            // automatically open collection on top of this one and go to bottom child
            if(isCollectionButton && nextSelectionIndex - 1 > -1) {
                nextSelectionIndex = nextSelectionIndex - 1;

                if(elements[nextSelectionIndex]->isType<CollectionButton>()) {
                    elements[nextSelectionIndex]->select();

                    const auto &children = elements[nextSelectionIndex]->getChildren();
                    if(children.size() > 0 && !children.back()->isSelected()) children.back()->select();
                }
            }
        }
    }

    if(key == KEY_LEFT && !g_songbrowser->bLeft) {
        g_songbrowser->bLeft = true;

        const bool jumpToNextGroup = keyboard->isShiftDown();

        bool foundSelected = false;
        for(sSz i = static_cast<sSz>(elements.size()) - 1; i >= 0; i--) {
            const bool isDiffButton = elements[i]->isType<SongButton>();
            const bool isCollectionButton = elements[i]->isType<CollectionButton>();
            const bool isSongDifficultyButtonAndNotIndependent =
                isDiffButton && !elements[i]->isIndependentDiffButton();

            if(foundSelected && !elements[i]->isSelected() && !isSongDifficultyButtonAndNotIndependent &&
               (!jumpToNextGroup || isCollectionButton)) {
                g_songbrowser->bNextScrollToSongButtonJumpFixUseScrollSizeDelta = true;
                {
                    elements[i]->select();

                    if(isCollectionButton) {
                        // automatically open collection below and go to bottom child
                        const auto &children = elements[i]->getChildren();
                        if(children.size() > 0 && !children.back()->isSelected()) children.back()->select();
                    }
                }
                g_songbrowser->bNextScrollToSongButtonJumpFixUseScrollSizeDelta = false;

                break;
            }

            if(elements[i]->isSelected()) foundSelected = true;
        }
    }

    if(key == KEY_RIGHT && !g_songbrowser->bRight) {
        g_songbrowser->bRight = true;

        const bool jumpToNextGroup = keyboard->isShiftDown();

        // get bottom selection
        int selectedIndex = -1;
        for(int i = 0; i < elements.size(); i++) {
            if(elements[i]->isSelected()) selectedIndex = i;
        }

        if(selectedIndex > -1) {
            for(size_t i = selectedIndex; i < elements.size(); i++) {
                const bool isDiffButton = elements[i]->isType<SongButton>();
                const bool isCollectionButton = elements[i]->isType<CollectionButton>();
                const bool isSongDifficultyButtonAndNotIndependent =
                    isDiffButton && !elements[i]->isIndependentDiffButton();

                if(!elements[i]->isSelected() && !isSongDifficultyButtonAndNotIndependent &&
                   (!jumpToNextGroup || isCollectionButton)) {
                    elements[i]->select();
                    break;
                }
            }
        }
    }

    if(key == KEY_PAGEUP) this->scrollY(static_cast<int>(this->getSize().y));
    if(key == KEY_PAGEDOWN) this->scrollY(static_cast<int>(-this->getSize().y));

    // group open/close
    // NOTE: only closing works atm (no "focus" state on buttons yet)
    if((key == KEY_ENTER || key == KEY_NUMPAD_ENTER) && keyboard->isShiftDown()) {
        for(auto element : elements) {
            if(element->isType<CollectionButton>() && element->isSelected()) {
                element->select();  // deselect
                g_songbrowser->scrollToSongButton(element);
                break;
            }
        }
    }

    // selection select
    if((key == KEY_ENTER || key == KEY_NUMPAD_ENTER) && !keyboard->isShiftDown())
        g_songbrowser->playSelectedDifficulty();
}
