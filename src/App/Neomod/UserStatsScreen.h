#pragma once
// Copyright (c) 2019, PG, All rights reserved.
#include "ScreenBackable.h"

#include <memory>

class ConVar;
class CBaseUIContainer;
class CBaseUIScrollView;
class CBaseUIButton;
class UIContextMenu;
class UserCard;
class ScoreButton;

class UserStatsScreen final : public ScreenBackable {
    NOCOPY_NOMOVE(UserStatsScreen)
   public:
    UserStatsScreen();
    ~UserStatsScreen() override = default;

    void draw() override;
    void update(CBaseUIEventCtx &c) override;

    CBaseUIContainer *setVisible(bool visible) override;

    void rebuildScoreButtons();

   private:
    void onBack() override;
    void updateLayout() override;

    void onMenuClicked(CBaseUIButton *button);
    void onMenuSelected(UString text, int id);
    void onCopyAllScoresClicked();
    void onCopyAllScoresUserSelected(UString text, int id);
    void onCopyAllScoresConfirmed(UString text, int id);
    void onDeleteAllScoresClicked();
    void onDeleteAllScoresConfirmed(UString text, int id);

    UserCard *m_userCard = nullptr;
    std::unique_ptr<UIContextMenu> m_contextMenu{nullptr};

    CBaseUIScrollView *m_scores = nullptr;
    std::vector<ScoreButton *> m_scoreButtons;
};
