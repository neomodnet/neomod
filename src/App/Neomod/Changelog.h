#pragma once
// Copyright (c) 2017, PG, All rights reserved.
#include "ScreenBackable.h"

class CBaseUIContainer;
class CBaseUIScrollView;
class CBaseUIImage;
class CBaseUILabel;
class CBaseUIButton;

class Changelog final : public ScreenBackable {
    NOCOPY_NOMOVE(Changelog)
   public:
    Changelog();
    ~Changelog() override;

    CBaseUIContainer *setVisible(bool visible) override;

   private:
    void updateLayout() override;
    void onBack() override;

    void onChangeClicked(CBaseUIButton *button);

    struct CHANGELOG {
        UString title;
        std::vector<UString> changes;
    };

    struct CHANGELOG_UI {
        CBaseUILabel *title;
        std::vector<CBaseUIButton *> changes;
    };

    void addAllChangelogs(std::vector<CHANGELOG> &&logtexts);

    CBaseUIScrollView *scrollView;

    std::vector<CHANGELOG_UI> changelogs;
};
