#pragma once
// Copyright (c) 2016 PG, All rights reserved.
#include "Resource.h"

#include <string>
#include <vector>

class SongButton;
class DatabaseBeatmap;

class AsyncSongButtonMatcher final : public Resource {
    NOCOPY_NOMOVE(AsyncSongButtonMatcher)
   public:
    AsyncSongButtonMatcher();
    ~AsyncSongButtonMatcher() override { this->destroy(); }

    [[nodiscard]] inline bool isDead() const { return this->bDead.load(std::memory_order_acquire); }
    inline void kill() { this->bDead.store(true, std::memory_order_release); }
    inline void revive() { this->bDead.store(false, std::memory_order_release); }

    void setSongButtonsAndSearchString(const std::vector<SongButton *> &songButtons, const std::string &searchString,
                                       const std::string &hardcodedSearchString, float currentSpeedMultiplier);

   protected:
    inline void init() override { this->setReady(true); }

    void initAsync() override;
    inline void destroy() override {}

    std::string sSearchString;
    std::string sHardcodedSearchString;
    std::vector<SongButton *> vSongButtons;

    // this is literally just to avoid needing to include Osu.h and BeatmapInterface.h
    float fSpeedMultiplier{1.f};

    std::atomic<bool> bDead{true};  // NOTE: start dead! need to revive() before use
};
