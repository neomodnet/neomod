// Copyright (c) 2016, PG, All rights reserved.
#include "UIRankingScreenInfoLabel.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "BeatmapInterface.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "Osu.h"
#include "Graphics.h"
#include "SString.h"
#include "Font.h"
#include "Timing.h"
#include "UI.h"

UIRankingScreenInfoLabel::UIRankingScreenInfoLabel(f32 xPos, f32 yPos, f32 xSize, f32 ySize, UString name)
    : CBaseUIElement(xPos, yPos, xSize, ySize, std::move(name)) {
    this->font = osu->getSubTitleFont();

    this->iMargin = 10;

    f32 globalScaler = 1.3f;
    this->fSubTitleScale = 0.6f * globalScaler;

    this->sArtist = "Artist";
    this->sTitle = "Title";
    this->sDiff = "Difficulty";
    this->sMapper = "Mapper";
    this->sPlayer = "Guest";
    this->sDate = "?";
}

void UIRankingScreenInfoLabel::draw() {
    // build strings
    const UString titleText{fmt::format("{} - {} [{}]", this->sArtist, this->sTitle, this->sDiff)};
    const UString subTitleText{fmt::format("Beatmap by {}", this->sMapper)};
    const UString playerText{this->buildPlayerString()};

    const f32 globalScale = std::max((this->getSize().y / this->getMinimumHeight()) * 0.741f, 1.0f);
    const f32 fontHeight = this->font->getHeight();

    // draw title
    g->setColor(0xffffffff);
    g->pushTransform();
    {
        const f32 scale = globalScale;

        g->scale(scale, scale);
        g->translate(this->getPos().x, this->getPos().y + fontHeight * scale);
        g->drawString(this->font, titleText);
    }
    g->popTransform();

    // draw subtitle
    g->setColor(0xffffffff);
    g->pushTransform();
    {
        const f32 scale = this->fSubTitleScale * globalScale;

        const f32 subTitleStringWidth = this->font->getStringWidth(subTitleText);

        g->translate((int)(-subTitleStringWidth / 2), (int)(fontHeight / 2));
        g->scale(scale, scale);
        g->translate((int)(this->getPos().x + (subTitleStringWidth / 2) * scale),
                     (int)(this->getPos().y + fontHeight * globalScale + (fontHeight / 2) * scale + this->iMargin));
        g->drawString(this->font, subTitleText);
    }
    g->popTransform();

    // draw subsubtitle (player text + datetime)
    g->setColor(0xffffffff);
    g->pushTransform();
    {
        const f32 scale = this->fSubTitleScale * globalScale;

        const f32 playerStringWidth = this->font->getStringWidth(playerText);

        g->translate((int)(-playerStringWidth / 2), (int)(fontHeight / 2));
        g->scale(scale, scale);
        g->translate((int)(this->getPos().x + (playerStringWidth / 2) * scale),
                     (int)(this->getPos().y + fontHeight * globalScale + fontHeight * scale + (fontHeight / 2) * scale +
                           this->iMargin * 2));
        g->drawString(this->font, playerText);
    }
    g->popTransform();
}

void UIRankingScreenInfoLabel::setFromBeatmap(const DatabaseBeatmap *map) {
    this->setArtist(map->getArtist());
    this->setTitle(map->getTitle());
    this->setDiff(map->getDifficultyName());
    this->setMapper(map->getCreator());

    std::time_t now_c = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    this->sDate.resize(26);
    ctime_x(&now_c, this->sDate.data());
    this->sDate.erase(25);               // remove embedded \0
    SString::trim_inplace(this->sDate);  // remove embedded \n or \r\n
}

void UIRankingScreenInfoLabel::setArtist(std::string_view artist) {
    SString::trim_inplace(artist);
    this->sArtist.assign(artist);
}

void UIRankingScreenInfoLabel::setTitle(std::string_view title) {
    SString::trim_inplace(title);
    this->sTitle.assign(title);
}

void UIRankingScreenInfoLabel::setDiff(std::string_view diff) {
    SString::trim_inplace(diff);
    this->sDiff.assign(diff);
}

void UIRankingScreenInfoLabel::setMapper(std::string_view mapper) {
    SString::trim_inplace(mapper);
    this->sMapper.assign(mapper);
}

void UIRankingScreenInfoLabel::setPlayer(std::string_view player) {
    SString::trim_inplace(player);
    this->sPlayer.assign(player);
}

void UIRankingScreenInfoLabel::setDate(std::string_view date) {
    SString::trim_inplace(date);
    this->sDate.assign(date);
}

UString UIRankingScreenInfoLabel::buildPlayerString() const {
    return fmt::format("Played by {} on {}", this->sPlayer, this->sDate);
}

f32 UIRankingScreenInfoLabel::getMinimumWidth() const {
    const f32 titleWidth = 0;
    const f32 subTitleWidth = 0;
    const f32 playerWidth = this->font->getStringWidth(this->buildPlayerString()) * this->fSubTitleScale;

    return std::max({titleWidth, subTitleWidth, playerWidth});
}

f32 UIRankingScreenInfoLabel::getMinimumHeight() const {
    const f32 fontHeight = this->font->getHeight();

    const f32 titleHeight = fontHeight;
    const f32 subTitleHeight = fontHeight * this->fSubTitleScale;
    const f32 playerHeight = fontHeight * this->fSubTitleScale;

    return titleHeight + subTitleHeight + playerHeight + this->iMargin * 2;
}
