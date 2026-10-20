#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "ScreenBackable.h"
#include "StaticPImpl.h"
#include "types.h"

struct FinishedScore;
namespace BANCHO::Net {
struct SubmissionResult;
}
namespace Mc::Net {
struct Response;
}

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

    // shows the online ranking of the score, if it is the one on screen
    void onScoreSubmitted(const FinishedScore &score, const BANCHO::Net::SubmissionResult &result);

   private:
    void updateLayout() override;
    void onBack() override;

    void drawModImage(const SkinImage &image, vec2 &pos, vec2 &max) const;

    void setGrade(ScoreGrade grade);
    void setIndex(int index);

    // rating 0 only asks whether the map can be rated
    void requestRating(i32 rating);
    void onRatingResponse(const Mc::Net::Response &response);
    void setOnlinePanelText();

    [[nodiscard]] std::string getPPString() const;
    [[nodiscard]] vec2 getPPPosRaw() const;

    struct RankingScreenImpl;
    StaticPImpl<RankingScreenImpl, 1280> m_impl;
};
