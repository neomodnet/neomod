#pragma once
// Copyright (c) 2025, kiwec, All rights reserved.
#include "ScreenBackable.h"

#include "types.h"
#include "SyncStoptoken.h"

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
class OnlineMapPreview;

class OsuDirectScreen final : public ScreenBackable {
    NOCOPY_NOMOVE(OsuDirectScreen)
   public:
    OsuDirectScreen();
    ~OsuDirectScreen() override;

    CBaseUIContainer* setVisible(bool visible) override;
    void draw() override;
    void tick() override;
    void updateInput(CBaseUIEventCtx& c) override;
    void onBack() override;
    void onResolutionChange(vec2 newResolution) override;

    void reset();
    void search(std::string_view query);

   private:
    void onRankedCheckboxChange(CBaseUICheckbox* checkbox);
    void onRankedStatusCvarChange(float oldValue, float newValue);
    void onAutoSelectCvarChange();

    CBaseUILabel* title{nullptr};
    CBaseUITextbox* search_bar{nullptr};
    UIButton* newest_btn{nullptr};
    UIButton* best_rated_btn{nullptr};
    CBaseUICheckbox* ranked_only{nullptr};
    CBaseUICheckbox* auto_select{nullptr};
    CBaseUIScrollView* results{nullptr};
    OnlineMapPreview* preview{nullptr};

    std::string current_query;

    vec2 spinner_pos{1.f, 1.f};  // init on onresolutionchange

    // armed per search so reset() / a new query can cancel the in-flight request
    Sync::stop_source search_cancel;
    f64 last_search_time{0.0};

    bool loading{false};
};
