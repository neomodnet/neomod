#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "CBaseUIButton.h"

class McFont;
class DatabaseBeatmap;

class InfoLabel final : public CBaseUIButton {
   public:
    InfoLabel(f32 xPos, f32 yPos, f32 xSize, f32 ySize, std::string name);

    void draw() override;
    void updateInput(CBaseUIEventCtx &c) override;

    void onResized() override;

    void setFromBeatmap(const DatabaseBeatmap *map);

    void setArtist(std::string_view artist);
    void setTitle(std::string_view title);
    void setDiff(std::string_view diff);
    void setMapper(std::string_view mapper);

    void setLocalOffset(i32 localOffset) { this->iLocalOffset = localOffset; }
    void setOnlineOffset(i32 onlineOffset) { this->iOnlineOffset = onlineOffset; }

    [[nodiscard]] f32 getMinimumWidth() const;
    [[nodiscard]] f32 getMinimumHeight() const;

    [[nodiscard]] i32 getBeatmapID() const { return this->iBeatmapId; }
    [[nodiscard]] i32 getBeatmapSetID() const { return this->iBeatmapSetId; }

    // the selected map's length, BPM and object count, and its difficulty settings, star rating and pp, all with the
    // mods in effect (speed included): what anything showing the selected map's stats should show ("" without one)
    [[nodiscard]] static std::string buildSongInfoString();
    [[nodiscard]] static std::string buildDiffInfoString();

   private:
    void updateScaling();
    [[nodiscard]] f32 getTitleFontRatio() const;

    [[nodiscard]] std::string buildOffsetInfoString() const;

    McFont *titleFont;

    static constexpr f32 SPACING_MARGIN{8.f};

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

    i32 iLocalOffset;
    i32 iOnlineOffset;

    // custom
    i32 iBeatmapId;
    i32 iBeatmapSetId;
};
