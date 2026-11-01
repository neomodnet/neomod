// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "StaticPImpl.h"
#include "UIScreen.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

// global progress overlay for in-flight BeatmapInstaller entries and other transfers (see addTransfer).
// bottom-left anchored stack, one row per active entry. visible iff any entries exist,
// not in play mode or kiosk mode, and cv::draw_beatmap_install_overlay is on.
class TransferOverlay final : public UIScreen {
    NOCOPY_NOMOVE(TransferOverlay)
   public:
    TransferOverlay();
    ~TransferOverlay() override;

    void draw() override;
    void tick() override;
    void updateInput(CBaseUIEventCtx& c) override;
    void onResolutionChange(vec2 newResolution) override;

    // shows a transfer that isn't a BeatmapInstaller entry (e.g. an upload) as a row until removeTransfer().
    // progress is read every tick, so any thread can write it.
    // the X button calls cancel and removes the row.
    u32 addTransfer(std::string name, std::shared_ptr<const std::atomic<f32>> progress, std::function<void()> cancel);
    void removeTransfer(u32 id);

   private:
    struct TOImpl;
    StaticPImpl<TOImpl, 80> m_impl;
};
