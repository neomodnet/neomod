#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "CBaseUIButton.h"

class McFont;
class DatabaseBeatmap;

class InfoLabel final : public CBaseUIButton {
   public:
    InfoLabel(f32 xPos, f32 yPos, f32 xSize, f32 ySize, UString name);

    void draw() override;
    void update(CBaseUIEventCtx &c) override;

    void onResized() override;

    void setFromBeatmap(const DatabaseBeatmap *map);

    void setArtist(std::string_view artist);
    void setTitle(std::string_view title);
    void setDiff(std::string_view diff);
    void setMapper(std::string_view mapper);

    inline void setLengthMS(u32 lengthMS) { this->iLengthMS = lengthMS; }
    inline void setBPM(i32 minBPM, i32 maxBPM, i32 mostCommonBPM) {
        this->iMinBPM = minBPM;
        this->iMaxBPM = maxBPM;
        this->iMostCommonBPM = mostCommonBPM;
    }
    inline void setNumObjects(i32 numObjects) { this->iNumObjects = numObjects; }

    inline void setCS(f32 CS) { this->fCS = CS; }
    inline void setAR(f32 AR) { this->fAR = AR; }
    inline void setOD(f32 OD) { this->fOD = OD; }
    inline void setHP(f32 HP) { this->fHP = HP; }
    inline void setStarsNomod(f32 stars) { this->fStarsNomod = stars; }

    void setLocalOffset(i32 localOffset) { this->iLocalOffset = localOffset; }
    void setOnlineOffset(i32 onlineOffset) { this->iOnlineOffset = onlineOffset; }

    [[nodiscard]] f32 getMinimumWidth() const;
    [[nodiscard]] f32 getMinimumHeight() const;

    [[nodiscard]] i32 getBeatmapID() const { return this->iBeatmapId; }

   private:
    void updateScaling();
    [[nodiscard]] f32 getTitleFontRatio() const;

    [[nodiscard]] UString buildSongInfoString() const;
    [[nodiscard]] UString buildDiffInfoString() const;
    [[nodiscard]] UString buildOffsetInfoString() const;

    McFont *titleFont;

    i32 iMargin{8};

    // updated in updateScaling
    f32 fGlobalScale{1.f};
    f32 fTitleScale{1.f};
    f32 fSubTitleScale{1.f};
    f32 fSongInfoScale{1.f};
    f32 fDiffInfoScale{1.f};
    f32 fOffsetInfoScale{1.f};

    std::string sArtist;
    std::string sTitle;
    std::string sDiff;
    std::string sMapper;

    u32 iLengthMS;
    i32 iMinBPM;
    i32 iMaxBPM;
    i32 iMostCommonBPM;
    i32 iNumObjects;

    f32 fCS;
    f32 fAR;
    f32 fOD;
    f32 fHP;
    f32 fStarsNomod;

    i32 iLocalOffset;
    i32 iOnlineOffset;

    // custom
    i32 iBeatmapId;
};
