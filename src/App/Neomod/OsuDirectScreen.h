#pragma once
// Copyright (c) 2025, kiwec, All rights reserved.
#include "ScreenBackable.h"

#include "types.h"

#include <memory>
// "OsuDirectScreen" is a cumbersome name, but "SearchScreen" is too generic,
// and we might have a download screen in the future, plus we already
// have "Song Browser", so I'd rather make it obvious.

class CBaseUICheckbox;
class CBaseUILabel;
class CBaseUIScrollView;
class CBaseUITextbox;
class UIButton;
class OnlineMapListing;

class OsuDirectScreen final : public ScreenBackable {
    NOCOPY_NOMOVE(OsuDirectScreen)
   public:
    OsuDirectScreen();
    ~OsuDirectScreen() override;

    enum RankingStatusFilter : u8 {
        RANKED = 0,
        PENDING = 2,
        QUALIFIED = 3,
        ALL = 4,
        GRAVEYARD = 5,
        PLAYED = 7,
        LOVED = 8,
    };

    CBaseUIContainer* setVisible(bool visible) override;
    void draw() override;
    void update(CBaseUIEventCtx &c) override;
    void onBack() override;
    void onResolutionChange(vec2 newResolution) override;

    void reset();
    void search(std::string_view query);

   private:
    void onRankedCheckboxChange(CBaseUICheckbox* checkbox);

    CBaseUILabel* title{nullptr};
    CBaseUITextbox* search_bar{nullptr};
    UIButton* newest_btn{nullptr};
    UIButton* best_rated_btn{nullptr};
    CBaseUICheckbox* ranked_only{nullptr};
    CBaseUIScrollView* results{nullptr};

    std::string current_query{"Newest"};

    vec2 spinner_pos{1.f, 1.f};  // init on onresolutionchange

    uSz request_id{1};
    f64 last_search_time{0.0};

    // Beatmapset to auto-select once download is completed
    friend class OnlineMapListing;
    i32 auto_select_set{0};

    bool loading{false};
};
