#pragma once
// Copyright (c) 2025, WH, All rights reserved.

#include "CBaseUIScrollView.h"
#include "CarouselData.h"
#include "Hashing.h"

#include <array>
#include <memory>

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

    // the rows in display order with their target y (ascending)
    void setRows(std::vector<CarouselNode> rows, std::vector<f32> rowY);
    void clearRows();
    // new target y for the same rows
    void setRowPositions(std::vector<f32> rowY);

    [[nodiscard]] inline const std::vector<CarouselNode> &getRows() const { return this->rows; }
    [[nodiscard]] inline f32 getRowY(size_t row) const { return this->rowY[row]; }
    // the row index for node, or getRows().size() if it isn't a row
    [[nodiscard]] size_t findRow(CarouselNode node) const;

    void resetAnimations();

    void setHoveredRow(CarouselButton *row);
    void clearHoveredRow(CarouselButton *row);

    // if we are actually scrolling at a "noticeable" velocity, so that we can skip
    // drawing some things for elements which the user will probably not notice anyways (backgrounds)
    [[nodiscard]] inline bool isScrollingFast() const { return this->bIsScrollingFast; }

   private:
    bool updateWindow();
    void applyMoveAwayState(CarouselButton *row, bool animate) const;
    CarouselButton *acquireButton(CarouselNode node);
    void releaseButton(CarouselButton *button);

    std::vector<CarouselNode> rows;
    std::vector<f32> rowY;

    size_t windowBegin{0};
    size_t windowEnd{0};
    std::vector<CarouselButton *> windowButtons;
    bool bWindowDirty{false};

    std::vector<std::unique_ptr<CarouselButton>> buttons;
    std::array<std::vector<CarouselButton *>, 4> freeButtons;

    Hash::flat::set<u32> diffsShownSinceReset;

    CarouselNode hoveredRow;
    f32 hoveredRowY{0.f};

    // updated at the end of update()
    bool bIsScrollingFast{false};

    static bool songButtonComparator(const CBaseUIElement *a, const CBaseUIElement *b);
};
