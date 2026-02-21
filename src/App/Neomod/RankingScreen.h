#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "Database.h"
#include "ScreenBackable.h"
#include "score.h"
#include "Skin.h"

class CBaseUIContainer;
class CBaseUIScrollView;
class CBaseUIImage;
class CBaseUILabel;
class DatabaseBeatmap;
class SkinImage;
class UIButton;
class UIRankingScreenInfoLabel;
class UIRankingScreenRankingPanel;
class RankingScreenIndexLabel;
class RankingScreenBottomElement;

class ConVar;

class RankingScreen final : public ScreenBackable {
   public:
    RankingScreen();

    void draw() override;
    void update(CBaseUIEventCtx &c) override;

    CBaseUIContainer *setVisible(bool visible) override;

    void onRetryClicked();
    void onWatchClicked();

    void setScore(const FinishedScore &score);

   private:
    void updateLayout() override;
    void onBack() override;

    void drawModImage(const SkinImage *image, vec2 &pos, vec2 &max) const;

    void setGrade(ScoreGrade grade);
    void setIndex(int index);

    [[nodiscard]] UString getPPString() const;
    [[nodiscard]] vec2 getPPPosRaw() const;

    CBaseUIScrollView *rankings;

    UIRankingScreenInfoLabel *songInfo;
    UIRankingScreenRankingPanel *rankingPanel;
    CBaseUIImage *rankingTitle;
    CBaseUIImage *rankingGrade;
    RankingScreenIndexLabel *rankingIndex;
    RankingScreenBottomElement *rankingBottom;

    UIButton *retry_btn;
    UIButton *watch_btn;

    ScoreGrade grade{ScoreGrade::D};
    float fUnstableRate;
    float fHitErrorAvgMin;
    float fHitErrorAvgMax;

    UString sMods;
    std::vector<SkinImage * Skin::*> modImages;
    std::vector<ConVar *> extraMods;

    // custom
    FinishedScore storedScore;
    bool bIsUnranked;
};
