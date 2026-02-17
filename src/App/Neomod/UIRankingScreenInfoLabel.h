#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include <utility>

#include "CBaseUIElement.h"
#include "types.h"

class McFont;
class DatabaseBeatmap;

class UIRankingScreenInfoLabel final : public CBaseUIElement {
   public:
    UIRankingScreenInfoLabel(f32 xPos, f32 yPos, f32 xSize, f32 ySize, UString name);

    void draw() override;

    void setFromBeatmap(const DatabaseBeatmap *map);

    void setArtist(std::string_view artist);
    void setTitle(std::string_view title);
    void setDiff(std::string_view diff);
    void setMapper(std::string_view mapper);
    void setPlayer(std::string_view player);
    void setDate(std::string_view date);

    [[nodiscard]] f32 getMinimumWidth() const;
    [[nodiscard]] f32 getMinimumHeight() const;

   private:
    [[nodiscard]] UString buildPlayerString() const;

    McFont *font;

    int iMargin;
    f32 fSubTitleScale;

    std::string sArtist;
    std::string sTitle;
    std::string sDiff;
    std::string sMapper;
    std::string sPlayer;
    std::string sDate;
};
