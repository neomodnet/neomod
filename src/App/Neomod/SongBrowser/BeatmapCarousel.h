#pragma once
// Copyright (c) 2025, WH, All rights reserved.

#include "CBaseUIScrollView.h"

class SongBrowser;
class CarouselButton;

class BeatmapCarousel final : public CBaseUIScrollView {
    NOCOPY_NOMOVE(BeatmapCarousel)
   public:
    BeatmapCarousel(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, std::string name = {});
    ~BeatmapCarousel() override;

    void onKeyUp(KeyboardEvent &e) override;
    void onKeyDown(KeyboardEvent &e) override;

    void draw() override;
    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;
    void onCapturedMouseMove() override;

    // the rows in display order, top to bottom. only the rows near the view are in the container,
    // so nothing per frame walks all of them
    void setRows(std::vector<CarouselButton *> rows);
    void clearRows() { this->setRows({}); }
    [[nodiscard]] inline const std::vector<CarouselButton *> &getRows() const { return this->rows; }

    // call after the rows got new target positions: updates the scroll size and the rows in the container
    void onRowsMoved();

    // the hovered row pushes the rows above it up and the rows below it down
    void setHoveredRow(CarouselButton *row);
    void clearHoveredRow(CarouselButton *row);

    // if we are actually scrolling at a "noticeable" velocity, so that we can skip
    // drawing some things for elements which the user will probably not notice anyways (backgrounds)
    [[nodiscard]] inline bool isScrollingFast() const { return this->bIsScrollingFast; }

   private:
    // puts the rows near the view into the container, returns whether that changed anything
    bool updateWindow();
    void applyMoveAwayState(CarouselButton *row, bool animate) const;

    std::vector<CarouselButton *> rows;
    // target y of each row, ascending
    std::vector<f32> rowY;
    // the rows in the container are [windowBegin, windowEnd)
    size_t windowBegin{0};
    size_t windowEnd{0};

    CarouselButton *hoveredRow{nullptr};
    // the first onRowsMoved() after setRows() hides the rows that are still visible from before
    bool bRowsReplaced{false};

    // updated at the end of update()
    bool bIsScrollingFast{false};

    static bool songButtonComparator(const CBaseUIElement *a, const CBaseUIElement *b);
};
