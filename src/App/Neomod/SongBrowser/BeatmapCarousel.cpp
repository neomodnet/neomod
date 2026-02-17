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

BeatmapCarousel::BeatmapCarousel(float xPos, float yPos, float xSize, float ySize, const UString &name)
    : CBaseUIScrollView(xPos, yPos, xSize, ySize, name) {
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

void BeatmapCarousel::draw() { CBaseUIScrollView::draw(); }

void BeatmapCarousel::update(CBaseUIEventCtx &c) {
    CBaseUIScrollView::update(c);
    if(!this->isVisible()) {
        // just reset this as a precaution
        this->bIsScrollingFast = false;
        return;
    }

    // handle right click absolute scrolling
    if(c.propagate_clicks) {
        if(mouse->isRightDown() && !g_songbrowser->contextMenu->isMouseInside()) {
            if(!g_songbrowser->bSongBrowserRightClickScrollCheck) {
                g_songbrowser->bSongBrowserRightClickScrollCheck = true;

                bool isMouseInsideAnySongButton = false;
                {
                    const std::vector<CarouselButton *> &buttons = this->container.getElements<CarouselButton>();
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
            const f64 mouseYPos = std::clamp(mouse->getPos().y - 2.0f - this->getPos().y, 0.f, this->getSize().y);
            const f64 mouseYPct = std::abs(mouseYPos / this->getSize().y);
            // scroll slightly more towards each extreme, to compensate for upper/lower bounds of the
            // carousel being possibly hidden behind other elements and very close to the edge of the screen
            const f64 mouseYPctCompensated = (mouseYPct * 1.25) - 0.125;
            const f64 scrollYAbs = -mouseYPctCompensated * this->getScrollSize().y;
            this->scrollToY(static_cast<int>(scrollYAbs));
        }
    }

    // update scrolling relative velocity, as a cache for isScrollingFast
    const f64 absYVelocity = std::abs(this->getVelocity().y);
    if(absYVelocity == 0.0) {
        // sanity
        this->bIsScrollingFast = false;
        return;
    }

    const auto &elements = this->container.getElements();
    if(elements.empty()) {
        return;
    }

    const f64 elemSizeY = elements[0]->getSize().y;
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

    // debugLog(
    //     "bIsScrollingFast: {} absYVelocity: {} elemSize: {} wholeElementsScrolled: {} "
    //     "sizeY: {}, sizeY / elemSizeY: {}",
    //     this->bIsScrollingFast, absYVelocity, elemSizeY, wholeElementsScrolled, sizeY, sizeY / elemSizeY);
}

bool BeatmapCarousel::isMouseInside() {
    return CBaseUIScrollView::isMouseInside() && !g_songbrowser->contextMenu->isMouseInside();
}

void BeatmapCarousel::onKeyUp(KeyboardEvent & /*e*/) { /*this->container.onKeyUp(e);*/ ; }

// don't consume keys, we are not a keyboard listener, but called from SongBrowser::onKeyDown manually
void BeatmapCarousel::onKeyDown(KeyboardEvent &key) {
    /*this->container.onKeyDown(e);*/

    // all elements must be CarouselButtons, at least
    const auto &elements{this->container.getElements<CarouselButton>()};

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

            auto *songButton = nextButton->as<SongButton>();

            // if this is a song button, select top child
            if(songButton != nullptr) {
                const auto &children = songButton->getChildren();
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
                auto *nextCollectionButton = elements[nextSelectionIndex]->as<CollectionButton>();
                if(nextCollectionButton != nullptr) {
                    nextCollectionButton->select();

                    const auto &children = nextCollectionButton->getChildren();
                    if(children.size() > 0 && !children.back()->isSelected()) children.back()->select();
                }
            }
        }
    }

    if(key == KEY_LEFT && !g_songbrowser->bLeft) {
        g_songbrowser->bLeft = true;

        const bool jumpToNextGroup = keyboard->isShiftDown();

        bool foundSelected = false;
        for(sSz i = elements.size() - 1; i >= 0; i--) {
            const auto *diffButtonPointer = elements[i]->as<const SongDifficultyButton>();
            const auto *collectionButtonPointer = elements[i]->as<const CollectionButton>();

            auto *button = elements[i]->as<CarouselButton>();
            const bool isSongDifficultyButtonAndNotIndependent =
                (diffButtonPointer != nullptr && !diffButtonPointer->isIndependentDiffButton());

            if(foundSelected && button != nullptr && !button->isSelected() &&
               !isSongDifficultyButtonAndNotIndependent && (!jumpToNextGroup || collectionButtonPointer != nullptr)) {
                g_songbrowser->bNextScrollToSongButtonJumpFixUseScrollSizeDelta = true;
                {
                    button->select();

                    if(!jumpToNextGroup || collectionButtonPointer == nullptr) {
                        // automatically open collection below and go to bottom child
                        auto *collectionButton = elements[i]->as<CollectionButton>();
                        if(collectionButton != nullptr) {
                            const auto &children = collectionButton->getChildren();
                            if(children.size() > 0 && !children.back()->isSelected()) children.back()->select();
                        }
                    }
                }
                g_songbrowser->bNextScrollToSongButtonJumpFixUseScrollSizeDelta = false;

                break;
            }

            if(button != nullptr && button->isSelected()) foundSelected = true;
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
                const auto *diffButtonPointer = elements[i]->as<const SongDifficultyButton>();
                const auto *collectionButtonPointer = elements[i]->as<const CollectionButton>();

                auto *button = elements[i]->as<CarouselButton>();
                const bool isSongDifficultyButtonAndNotIndependent =
                    (diffButtonPointer != nullptr && !diffButtonPointer->isIndependentDiffButton());

                if(button != nullptr && !button->isSelected() && !isSongDifficultyButtonAndNotIndependent &&
                   (!jumpToNextGroup || collectionButtonPointer != nullptr)) {
                    button->select();
                    break;
                }
            }
        }
    }

    if(key == KEY_PAGEUP) this->scrollY(this->getSize().y);
    if(key == KEY_PAGEDOWN) this->scrollY(-this->getSize().y);

    // group open/close
    // NOTE: only closing works atm (no "focus" state on buttons yet)
    if((key == KEY_ENTER || key == KEY_NUMPAD_ENTER) && keyboard->isShiftDown()) {
        for(auto element : elements) {
            const auto *collectionButtonPointer = element->as<const CollectionButton>();

            auto *button = element->as<CarouselButton>();

            if(collectionButtonPointer != nullptr && button != nullptr && button->isSelected()) {
                button->select();  // deselect
                g_songbrowser->scrollToSongButton(button);
                break;
            }
        }
    }

    // selection select
    if((key == KEY_ENTER || key == KEY_NUMPAD_ENTER) && !keyboard->isShiftDown())
        g_songbrowser->playSelectedDifficulty();
}

void BeatmapCarousel::onChar(KeyboardEvent & /*e*/) { /*this->container.onChar(e);*/ ; }
