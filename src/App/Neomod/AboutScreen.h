#pragma once
// Copyright (c) 2017, PG, All rights reserved.
#include "ScreenBackable.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CBaseUIContainer;
class CBaseUIScrollView;
class CBaseUIImage;
class CBaseUILabel;
class CBaseUIButton;

class AboutScreen final : public ScreenBackable {
    NOCOPY_NOMOVE(AboutScreen)
   public:
    AboutScreen();
    ~AboutScreen() override;

    CBaseUIContainer *setVisible(bool visible) override;

    void updateInput(CBaseUIEventCtx &c) override;

    // the tab strip isn't a scroll surface, so a wheel over it would fall through to the volume
    // sink: claim it screen-wide and forward to the active tab (same as SongBrowser's carousel)
    bool onWheel(int deltaVertical, int deltaHorizontal) override;

   private:
    void updateLayout() override;
    void onBack() override;

    void onChangeClicked(CBaseUIButton *button);

    struct CHANGELOG {
        std::string title;
        std::vector<std::string> changes;
    };

    struct CHANGELOG_UI {
        CBaseUILabel *title;
        std::vector<CBaseUIButton *> changes;
    };

    enum class Tab : uint8_t { CHANGELOG, CREDITS, LICENSES };
    static constexpr size_t NUM_TABS{3};

    // one scrollable page per tab, only built once its tab is selected for the first time.
    // the scroll view owns its labels, the pointers/vectors here are non-owning aliases for layouting
    struct TabPage {
        CBaseUIScrollView *view{nullptr};  // owned by us
        CBaseUIButton *button{nullptr};    // owned by us
        CBaseUILabel *header{nullptr};     // owned by view->container
        CBaseUILabel *spacer{nullptr};     // owned by view->container
        std::vector<CBaseUIButton *> lines;
        bool built{false};
    };

    void setActiveTab(Tab tab);
    void buildTab(Tab tab);
    void buildChangelog();
    void buildTextLines(TabPage &page, std::string_view embedKey);
    void addAllChangelogs(std::vector<CHANGELOG> &&logtexts);

    void layoutTab(Tab tab);

    std::array<TabPage, NUM_TABS> tabs;
    Tab activeTab{Tab::CHANGELOG};

    // same deal, aliases into the changelog tab's scroll view
    std::vector<CHANGELOG_UI> changelogs;
};
