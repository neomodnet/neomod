#pragma once
// Copyright (c) 2016, PG, All rights reserved.

#include "CBaseUIImage.h"

class LiveScore;
class SkinImage;
struct FinishedScore;

class UIRankingScreenRankingPanel final : public CBaseUIImage {
   public:
    UIRankingScreenRankingPanel();

    void draw() override;

    void setScore(LiveScore *score);
    void setScore(const FinishedScore &score);

   private:
    void drawHitImage(SkinImage *img, float scale, vec2 pos) const;
    void drawNumHits(int numHits, float scale, vec2 pos) const;

    u64 iScore;
    int iNum300s;
    int iNum300gs;
    int iNum100s;
    int iNum100ks;
    int iNum50s;
    int iNumMisses;
    int iCombo;
    float fAccuracy;
    bool bPerfect;
};
