#pragma once
// Copyright (c) 2025, WH, All rights reserved.

#include "CBaseUIScrollView.h"

class SongBrowser;

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

    // if we are actually scrolling at a "noticeable" velocity, so that we can skip
    // drawing some things for elements which the user will probably not notice anyways (backgrounds)
    [[nodiscard]] inline bool isScrollingFast() const { return this->bIsScrollingFast; }

   private:
    // updated at the end of update()
    bool bIsScrollingFast{false};

    static bool songButtonComparator(const CBaseUIElement *a, const CBaseUIElement *b);
};
