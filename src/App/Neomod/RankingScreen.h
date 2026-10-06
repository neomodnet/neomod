#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "ScreenBackable.h"
#include "StaticPImpl.h"

struct FinishedScore;

struct Skin;
class SkinImage;

enum class ScoreGrade : uint8_t;

class RankingScreen final : public ScreenBackable {
    NOCOPY_NOMOVE(RankingScreen)
   public:
    RankingScreen();
    ~RankingScreen() override;

    void draw() override;
    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;

    CBaseUIContainer *setVisible(bool visible) override;

    void onRetryClicked();
    void onWatchClicked();

    void setScore(const FinishedScore &score);
    const FinishedScore &getScore();

   private:
    void updateLayout() override;
    void onBack() override;

    void drawModImage(const SkinImage &image, vec2 &pos, vec2 &max) const;

    void setGrade(ScoreGrade grade);
    void setIndex(int index);

    [[nodiscard]] std::string getPPString() const;
    [[nodiscard]] vec2 getPPPosRaw() const;

    struct RankingScreenImpl;
    StaticPImpl<RankingScreenImpl, 768> m_impl;
};
