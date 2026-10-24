// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "StaticPImpl.h"
#include "UIScreen.h"

// global progress overlay for in-flight BeatmapInstaller entries.
// bottom-left anchored stack, one row per active entry. visible iff any entries exist,
// not in play mode or kiosk mode, and cv::draw_beatmap_install_overlay is on.
class BeatmapInstallOverlay final : public UIScreen {
    NOCOPY_NOMOVE(BeatmapInstallOverlay)
   public:
    BeatmapInstallOverlay();
    ~BeatmapInstallOverlay() override;

    void draw() override;
    void tick() override;
    void updateInput(CBaseUIEventCtx& c) override;
    void onResolutionChange(vec2 newResolution) override;

   private:
    struct BIOImpl;
    StaticPImpl<BIOImpl, 48> m_impl;
};
