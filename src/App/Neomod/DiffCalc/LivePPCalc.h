// Copyright (c) 2024, kiwec, 2025, WH, All rights reserved.
#pragma once

#include "noinclude.h"
#include "StaticPImpl.h"

class LiveScore;
class BeatmapInterface;

// for live PP/star calc. during gameplay
struct LivePPCalc {
    NOCOPY_NOMOVE(LivePPCalc);

   private:
    struct LivePPCalcImpl;
    StaticPImpl<LivePPCalcImpl, 450> pImpl;

   public:
    LivePPCalc() = delete;
    LivePPCalc(BeatmapInterface *parent);
    ~LivePPCalc();

    // call during BeatmapInterface::update
    void update(const LiveScore &score);

    // force refresh
    void invalidate();

    // current live values
    [[nodiscard]] float get_stars() const;
    [[nodiscard]] float get_pp() const;
};
