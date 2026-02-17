// Copyright (c) 2016, PG, All rights reserved.
#include "InfoLabel.h"

#include <algorithm>
#include <utility>

#include "Keyboard.h"
#include "SString.h"
#include "SongBrowser.h"
// ---

#include "BeatmapInterface.h"
#include "OsuConVars.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "Environment.h"
#include "GameRules.h"
#include "AsyncPPCalculator.h"
#include "Mouse.h"
#include "NotificationOverlay.h"
#include "OptionsOverlay.h"
#include "Graphics.h"
#include "Osu.h"
#include "Font.h"
#include "ResourceManager.h"
#include "Skin.h"
#include "TooltipOverlay.h"
#include "UI.h"

InfoLabel::InfoLabel(f32 xPos, f32 yPos, f32 xSize, f32 ySize, UString name)
    : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name), "") {
    // font for non-title parts (song info)
    this->setFont(osu->getSubTitleFont());

    // slightly abusing songbrowser font here, but the subtitle font is just too low DPI
    // and looks bad if it's even slightly upscaled (even at 1080p, we're upscaling it by ~1.4x)
    // the songbrowser font is about 1.5x larger, so it looks sharper due to not needing to be upscaled
    this->titleFont = osu->getSongBrowserFont();

    this->updateScaling();

    this->sArtist = "Artist";
    this->sTitle = "Title";
    this->sDiff = "Difficulty";
    this->sMapper = "Mapper";

    this->iLengthMS = 0;
    this->iMinBPM = 0;
    this->iMaxBPM = 0;
    this->iMostCommonBPM = 0;
    this->iNumObjects = 0;

    this->fCS = 5.0f;
    this->fAR = 5.0f;
    this->fOD = 5.0f;
    this->fHP = 5.0f;
    this->fStarsNomod = 5.0f;

    this->iLocalOffset = 0;
    this->iOnlineOffset = 0;

    this->iBeatmapId = -1;
}

void InfoLabel::draw() {
    // debug bounding box
    if(cv::debug_osu.getBool()) {
        g->setColor(0xffff0000);
        g->drawLine(this->getPos().x, this->getPos().y, this->getPos().x + this->getSize().x, this->getPos().y);
        g->drawLine(this->getPos().x, this->getPos().y, this->getPos().x, this->getPos().y + this->getSize().y);
        g->drawLine(this->getPos().x, this->getPos().y + this->getSize().y, this->getPos().x + this->getSize().x,
                    this->getPos().y + this->getSize().y);
        g->drawLine(this->getPos().x + this->getSize().x, this->getPos().y, this->getPos().x + this->getSize().x,
                    this->getPos().y + this->getSize().y);
    }

    // build strings
    const UString titleText{fmt::format("{} - {} [{}]", this->sArtist, this->sTitle, this->sDiff)};
    const UString subTitleText{fmt::format("Mapped by {}", this->sMapper)};

    const UString songInfoText{this->buildSongInfoString()};
    const UString diffInfoText{this->buildDiffInfoString()};
    const UString offsetInfoText{this->buildOffsetInfoString()};

    const f32 globalScale = std::max((this->getSize().y / this->getMinimumHeight()) * 0.91f, 1.0f);

    const i32 shadowOffset = std::round(1.0f * ((f32)this->font->getDPI() / 96.0f));  // NOTE: abusing font dpi

    i32 yCounter = this->getPos().y;

    // draw title
    g->pushTransform();
    {
        const i32 titleShadowOffset =
            std::round(1.0f * ((f32)this->titleFont->getDPI() / 96.0f));  // NOTE: abusing font dpi

        const f32 scale = this->fTitleScale * globalScale;

        yCounter += this->titleFont->getHeight() * scale;

        g->scale(scale, scale);
        g->translate((i32)(this->getPos().x), yCounter);

        g->translate(titleShadowOffset, titleShadowOffset);
        g->setColor(0xff000000);
        g->drawString(this->titleFont, titleText);
        g->translate(-titleShadowOffset, -titleShadowOffset);
        g->setColor(0xffffffff);
        g->drawString(this->titleFont, titleText);
    }
    g->popTransform();

    // draw subtitle (mapped by)
    g->pushTransform();
    {
        const f32 scale = this->fSubTitleScale * globalScale;
        // account for slightly taller/larger title font glyphs
        const f32 extraTitleFontMargin = (this->iMargin / 2.f) * this->getTitleFontRatio();

        yCounter += this->font->getHeight() * scale + extraTitleFontMargin * globalScale * 1.0f;

        g->scale(scale, scale);
        g->translate((i32)(this->getPos().x), yCounter);

        g->translate(shadowOffset, shadowOffset);
        g->setColor(0xff000000);
        g->drawString(this->font, subTitleText);
        g->translate(-shadowOffset, -shadowOffset);
        g->setColor(0xffffffff);
        g->drawString(this->font, subTitleText);
    }
    g->popTransform();

    // draw song info (length, bpm, objects)
    const Color songInfoColor = (osu->getMapInterface()->getSpeedMultiplier() != 1.0f
                                     ? (osu->getMapInterface()->getSpeedMultiplier() > 1.0f ? 0xffff7f7f : 0xffadd8e6)
                                     : 0xffffffff);
    g->pushTransform();
    {
        const f32 scale = this->fSongInfoScale * globalScale * 0.9f;

        yCounter += this->font->getHeight() * scale + (this->iMargin / 2) * globalScale * 1.0f;

        g->scale(scale, scale);
        g->translate((i32)(this->getPos().x), yCounter);

        g->translate(shadowOffset, shadowOffset);
        g->setColor(0xff000000);
        g->drawString(this->font, songInfoText);
        g->translate(-shadowOffset, -shadowOffset);
        g->setColor(songInfoColor);
        g->drawString(this->font, songInfoText);
    }
    g->popTransform();

    // draw diff info (CS, AR, OD, HP, Stars)
    const Color diffInfoColor = osu->getModEZ() ? 0xffadd8e6 : (osu->getModHR() ? 0xffff7f7f : 0xffffffff);
    g->pushTransform();
    {
        const f32 scale = this->fDiffInfoScale * globalScale * 0.9f;

        yCounter += this->font->getHeight() * scale + this->iMargin * globalScale * 0.85f;

        g->scale(scale, scale);
        g->translate((i32)(this->getPos().x), yCounter);

        g->translate(shadowOffset, shadowOffset);
        g->setColor(0xff000000);
        g->drawString(this->font, diffInfoText);
        g->translate(-shadowOffset, -shadowOffset);
        g->setColor(diffInfoColor);
        g->drawString(this->font, diffInfoText);
    }
    g->popTransform();

    // draw offset (local, online)
    if(this->iLocalOffset != 0 || this->iOnlineOffset != 0) {
        g->pushTransform();
        {
            const f32 scale = this->fOffsetInfoScale * globalScale * 0.8f;

            yCounter += this->font->getHeight() * scale + this->iMargin * globalScale * 0.85f;

            g->scale(scale, scale);
            g->translate((i32)(this->getPos().x), yCounter);

            g->translate(shadowOffset, shadowOffset);
            g->setColor(0xff000000);
            g->drawString(this->font, offsetInfoText);
            g->translate(-shadowOffset, -shadowOffset);
            g->setColor(0xffffffff);
            g->drawString(this->font, offsetInfoText);
        }
        g->popTransform();
    }
}

void InfoLabel::update(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;

    CBaseUIButton::update(c);

    // detail info tooltip when hovering over diff info
    if(this->isMouseInside() && !ui->getOptionsOverlay()->isMouseInside()) {
        const auto &pf = osu->getMapInterface();

        const f32 speedMultiplierInv = (1.0f / pf->getSpeedMultiplier());

        const f32 approachTimeRoundedCompensated = ((i32)pf->getApproachTime()) * speedMultiplierInv;
        const f32 hitWindow300RoundedCompensated = ((i32)pf->getHitWindow300() - 0.5f) * speedMultiplierInv;
        const f32 hitWindow100RoundedCompensated = ((i32)pf->getHitWindow100() - 0.5f) * speedMultiplierInv;
        const f32 hitWindow50RoundedCompensated = ((i32)pf->getHitWindow50() - 0.5f) * speedMultiplierInv;
        const f32 hitobjectRadiusRoundedCompensated = (GameRules::getRawHitCircleDiameter(pf->getCS()) / 2.0f);

        const auto *bmDiff2{pf->getBeatmap()};
        const auto &tooltipOverlay{ui->getTooltipOverlay()};
        tooltipOverlay->begin();
        {
            tooltipOverlay->addLine(fmt::format("Approach time: {:.2f}ms"_cf, approachTimeRoundedCompensated));
            tooltipOverlay->addLine(fmt::format("300: +-{:.2f}ms"_cf, hitWindow300RoundedCompensated));
            tooltipOverlay->addLine(fmt::format("100: +-{:.2f}ms"_cf, hitWindow100RoundedCompensated));
            tooltipOverlay->addLine(fmt::format(" 50: +-{:.2f}ms"_cf, hitWindow50RoundedCompensated));
            tooltipOverlay->addLine(
                fmt::format("Spinner difficulty: {:.2f}"_cf, GameRules::getSpinnerSpinsPerSecond(pf.get())));
            tooltipOverlay->addLine(fmt::format("Hit object radius: {:.2f}"_cf, hitobjectRadiusRoundedCompensated));

            if(bmDiff2 != nullptr) {
                i32 numObjects{bmDiff2->getNumObjects()};
                i32 numCircles{bmDiff2->getNumCircles()};
                i32 numSliders{bmDiff2->getNumSliders()};
                u32 lengthMS{bmDiff2->getLengthMS()};

                f32 opm{0.f}, cpm{0.f}, spm{0.f};
                if(lengthMS > 0) {
                    const f32 durMinutes{(static_cast<f32>(lengthMS) / 1000.0f / 60.0f) / pf->getSpeedMultiplier()};

                    opm = static_cast<f32>(numObjects) / durMinutes;
                    cpm = static_cast<f32>(numCircles) / durMinutes;
                    spm = static_cast<f32>(numSliders) / durMinutes;
                }

                tooltipOverlay->addLine(fmt::format("Circles: {:d}, Sliders: {:d}, Spinners: {:d}"_cf, numCircles,
                                                    numSliders, std::max(0, numObjects - numCircles - numSliders)));
                tooltipOverlay->addLine(
                    fmt::format("OPM: {:d}, CPM: {:d}, SPM: {:d}"_cf, (i32)opm, (i32)cpm, (i32)spm));
                tooltipOverlay->addLine(fmt::format("ID: {:d}, SetID: {:d}"_cf, bmDiff2->getID(), bmDiff2->getSetID()));
                tooltipOverlay->addLine(fmt::format("MD5: {:s}"_cf, bmDiff2->getMD5()));
                // mostly for debugging
                if(keyboard->isShiftDown()) {
                    tooltipOverlay->addLine(fmt::format("Active precalc: {:#02x}={:s}", StarPrecalc::active_idx, StarPrecalc::dbgstr_idx(StarPrecalc::active_idx)));

                    tooltipOverlay->addLine(fmt::format("Title: {:s}"_cf, bmDiff2->getTitleLatin()));
                    tooltipOverlay->addLine(fmt::format("TitleUnicode: {:s}"_cf, bmDiff2->getTitleUnicode()));
                    tooltipOverlay->addLine(fmt::format("Artist: {:s}"_cf, bmDiff2->getArtistLatin()));
                    tooltipOverlay->addLine(fmt::format("ArtistUnicode: {:s}"_cf, bmDiff2->getArtistUnicode()));
                }
            }
        }
        tooltipOverlay->end();
    }
}

// since it took me a while to figure out where to hook into to update this, this is called through:
// Osu::onResolutionChanged (for all screens) ->
//   SongBrowser::onResolutionChange ->
//     ScreenBackable::onResolutionChange ->
//       SongBrowser::updateLayout ->
//         this->songInfo->setSize (which is here)

void InfoLabel::onResized() {
    CBaseUIButton::onResized();
    this->updateScaling();
}

void InfoLabel::updateScaling() {
    // TODO: this seems wrong...

    const auto screen = osu->getVirtScreenSize();
    const bool is_widescreen = ((i32)(std::max(0, (i32)((screen.x - (screen.y * 4.f / 3.f)) / 2.f))) > 0);

    this->fGlobalScale = screen.x / (is_widescreen ? 1366.f : 1024.f);
    this->fTitleScale = (1.f * this->fGlobalScale) / this->getTitleFontRatio();
    this->fSubTitleScale = 0.65f * this->fGlobalScale;
    this->fSongInfoScale = 0.8f * this->fGlobalScale;
    this->fDiffInfoScale = 0.65f * this->fGlobalScale;
    this->fOffsetInfoScale = 0.65f * this->fGlobalScale;
}

f32 InfoLabel::getTitleFontRatio() const { return (f32)this->titleFont->getSize() / (f32)this->font->getSize(); }

void InfoLabel::setFromBeatmap(const DatabaseBeatmap *map) {
    this->iBeatmapId = map->getID();

    this->setArtist(map->getArtist());
    this->setTitle(map->getTitle());
    this->setDiff(map->getDifficultyName());
    this->setMapper(map->getCreator());

    this->setLengthMS(map->getLengthMS());
    this->setBPM(map->getMinBPM(), map->getMaxBPM(), map->getMostCommonBPM());
    this->setNumObjects(map->getNumObjects());

    this->setCS(map->getCS());
    this->setAR(map->getAR());
    this->setOD(map->getOD());
    this->setHP(map->getHP());
    this->setStarsNomod(map->getStarsNomod());

    this->setLocalOffset(map->getLocalOffset());
    this->setOnlineOffset(map->getOnlineOffset());
}

void InfoLabel::setArtist(std::string_view artist) {
    SString::trim_inplace(artist);
    this->sArtist.assign(artist);
}

void InfoLabel::setTitle(std::string_view title) {
    SString::trim_inplace(title);
    this->sTitle.assign(title);
}

void InfoLabel::setDiff(std::string_view diff) {
    SString::trim_inplace(diff);
    this->sDiff.assign(diff);
}

void InfoLabel::setMapper(std::string_view mapper) {
    SString::trim_inplace(mapper);
    this->sMapper.assign(mapper);
}

UString InfoLabel::buildSongInfoString() const {
    const u32 lengthMS = this->iLengthMS;
    const f32 speed = osu->getMapInterface()->getSpeedMultiplier();

    const u32 fullSeconds = (lengthMS * (1.0 / speed)) / 1000.0;
    const i32 minutes = fullSeconds / 60;
    const i32 seconds = fullSeconds % 60;

    const i32 minBPM = this->iMinBPM * speed;
    const i32 maxBPM = this->iMaxBPM * speed;
    const i32 mostCommonBPM = this->iMostCommonBPM * speed;

    i32 numObjects = this->iNumObjects;
    if(this->iMinBPM == this->iMaxBPM) {
        return fmt::format("Length: {:02d}:{:02d} BPM: {} Objects: {}", minutes, seconds, maxBPM, numObjects);
    } else {
        return fmt::format("Length: {:02d}:{:02d} BPM: {}-{} ({}) Objects: {}", minutes, seconds, minBPM, maxBPM,
                           mostCommonBPM, numObjects);
    }
}

UString InfoLabel::buildDiffInfoString() const {
    const auto &pf = osu->getMapInterface();
    auto *map = pf->getBeatmap();
    if(!map) return "";

    const f32 CS = pf->getCS();
    const f32 AR = pf->getApproachRateForSpeedMultiplier();
    const f32 OD = pf->getOverallDifficultyForSpeedMultiplier();
    const f32 HP = pf->getHP();

    const f32 nomodStars = this->fStarsNomod;
    f32 modStars = nomodStars;
    f32 modPp = 0.f;

    bool pp_available = false;

    // pp calc for currently selected mods
    {
        const auto &pp = pf->getWholeMapPPInfo();
        if(pp.pp != -1.0) {
            modStars = pp.total_stars;
            modPp = pp.pp;
            pp_available = true;
        }
    }

    const f32 starComparisonEpsilon = 0.01f;
    const bool starsAndModStarsAreEqual = (std::abs(nomodStars - modStars) < starComparisonEpsilon);

    UString finalString;
    if(pp_available) {
        const i32 clampedModPp = static_cast<i32>(
            std::round<i32>((std::isfinite(modPp) && modPp >= static_cast<f32>(std::numeric_limits<i32>::min()) &&
                             modPp <= static_cast<f32>(std::numeric_limits<i32>::max()))
                                ? static_cast<i32>(modPp)
                                : 0));
        if(starsAndModStarsAreEqual) {
            finalString = fmt::format("CS:{:.3g} AR:{:.3g} OD:{:.3g} HP:{:.3g} Stars:{:.3g} ({}pp)", CS, AR, OD, HP,
                                      nomodStars, clampedModPp);
        } else {
            finalString = fmt::format("CS:{:.3g} AR:{:.3g} OD:{:.3g} HP:{:.3g} Stars:{:.3g} -> {:.3g} ({}pp)", CS, AR,
                                      OD, HP, nomodStars, modStars, clampedModPp);
        }
    } else {
        finalString =
            fmt::format("CS:{:.3g} AR:{:.3g} OD:{:.3g} HP:{:.3g} Stars:{:.3g} * (??? pp)", CS, AR, OD, HP, nomodStars);
    }

    return finalString;
}

UString InfoLabel::buildOffsetInfoString() const {
    return fmt::format("Your Offset: {:d} ms / Online Offset: {:d} ms", this->iLocalOffset, this->iOnlineOffset);
}

f32 InfoLabel::getMinimumWidth() const {
    const f32 titleWidth = 0;
    const f32 subTitleWidth = 0;
    const f32 songInfoWidth = this->font->getStringWidth(this->buildSongInfoString()) * this->fSongInfoScale;
    const f32 diffInfoWidth = this->font->getStringWidth(this->buildDiffInfoString()) * this->fDiffInfoScale;
    const f32 offsetInfoWidth = this->font->getStringWidth(this->buildOffsetInfoString()) * this->fOffsetInfoScale;

    return std::max({titleWidth, subTitleWidth, songInfoWidth, diffInfoWidth, offsetInfoWidth});
}

f32 InfoLabel::getMinimumHeight() const {
    const f32 titleFontHeight = this->titleFont->getHeight();
    const f32 fontHeight = this->font->getHeight();

    const f32 titleHeight = titleFontHeight * this->fTitleScale;
    const f32 subTitleHeight = fontHeight * this->fSubTitleScale;
    const f32 songInfoHeight = fontHeight * this->fSongInfoScale;
    const f32 diffInfoHeight = fontHeight * this->fDiffInfoScale;
    const f32 offsetInfoHeight = fontHeight * this->fOffsetInfoScale;

    return titleHeight + subTitleHeight + songInfoHeight + diffInfoHeight + offsetInfoHeight + this->iMargin * 6;
}
