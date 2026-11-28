// Copyright (c) 2016, PG, All rights reserved.
#include "SongButton.h"

#include "Font.h"
#include "SongBrowser.h"
#include "CarouselData.h"
#include "BeatmapCarousel.h"

// ---

#include "BackgroundImageHandler.h"
#include "OsuConVars.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "Osu.h"
#include "Skin.h"
#include "SkinImage.h"
#include "Graphics.h"
#include "score.h"

#include "fmt/format.h"

using namespace neomod::sbr;

namespace {
f32 getThumbnailYRatio() { return cv::draw_songbrowser_thumbnails.getBool() ? 1.333333f : 0.f; }
}  // namespace

SongButton::SongButton(BeatmapCarousel &carousel) : CarouselButton(carousel) {}

SongButton::~SongButton() = default;

const DatabaseBeatmap *SongButton::getDatabaseBeatmap() const { return g_songbrowser->getBeatmap(this->node); }

ScoreGrade SongButton::getGrade() const { return g_songbrowser->getGrade(this->node); }

void SongButton::draw() {
    if(!this->bVisible) {
        return;
    }

    CarouselButton::draw();

    const DatabaseBeatmap *map = this->getDatabaseBeatmap();
    if(map &&  // delay requesting the image itself a bit
       this->fVisibleFor >= ((std::clamp<f32>(cv::songbrowser_thumbnail_delay.getFloat(), 0.f, 2.f)) / 4.f)) {
        // draw background image
        this->drawBeatmapBackgroundThumbnail(osu->getBackgroundImageHandler()->getLoadBackgroundImage(map));
    }

    if(this->getGrade() != ScoreGrade::N) this->drawGrade();
    this->drawTitle();
    this->drawSubTitle();
}

void SongButton::tick() {
    CarouselButton::tick();
    if(!this->bVisible) {
        return;
    }

    // don't try to load images while scrolling fast to avoid lag
    if(!this->carousel.isScrollingFast())
        this->fVisibleFor += engine->getFrameTime();
    else
        this->fVisibleFor = 0.f;
}

void SongButton::drawBeatmapBackgroundThumbnail(const Image *image) {
    if(!cv::draw_songbrowser_thumbnails.getBool() || osu->getSkin()->version < 2.2f) return;

    float alpha = 1.0f;
    if(const f32 fadein_time = cv::songbrowser_thumbnail_fade_in_duration.getFloat(); fadein_time > 0.0f) {
        const f64 now = engine->getTime();
        if(image == nullptr || !image->isReady())
            this->fThumbnailFadeInTime = (f32)now;
        else if(this->fThumbnailFadeInTime > 0.0f && now > this->fThumbnailFadeInTime) {
            alpha = std::clamp<float>((f32)(now - this->fThumbnailFadeInTime) / fadein_time, 0.0f, 1.0f);
            alpha = 1.0f - (1.0f - alpha) * (1.0f - alpha);
        }
    }

    if(image == nullptr || !image->isReady()) return;

    // scaling
    const vec2 pos = this->getActualPos();
    const vec2 size = this->getActualSize();

    const f32 thumbnailYRatio = getThumbnailYRatio();
    const f32 beatmapBackgroundScale =
        Osu::getImageScaleToFillResolution(image, vec2(size.y * thumbnailYRatio, size.y)) * 1.05f;

    vec2 centerOffset = vec2((size.y * thumbnailYRatio) / 2.0f, size.y / 2.0f);
    McRect clipRect = McRect(pos.x - 2, pos.y + 1, (size.y * thumbnailYRatio) + 5, size.y + 1);

    g->setColor(argb(alpha, 1.f, 1.f, 1.f));
    g->pushTransform();
    {
        g->scale(beatmapBackgroundScale, beatmapBackgroundScale);
        g->translate(pos.x + centerOffset.x, pos.y + centerOffset.y);
        // draw with smooth edge clipping
        g->drawImage(image, AnchorPoint::CENTER, 1.f, clipRect);
    }
    g->popTransform();

    // debug cliprect bounding box
    if(cv::debug_osu.getBool()) {
        vec2 clipRectPos = vec2(clipRect.getX(), clipRect.getY() - 1);
        vec2 clipRectSize = vec2(clipRect.getWidth(), clipRect.getHeight());

        g->setColor(0xffffff00);
        g->drawLine(clipRectPos.x, clipRectPos.y, clipRectPos.x + clipRectSize.x, clipRectPos.y);
        g->drawLine(clipRectPos.x, clipRectPos.y, clipRectPos.x, clipRectPos.y + clipRectSize.y);
        g->drawLine(clipRectPos.x, clipRectPos.y + clipRectSize.y, clipRectPos.x + clipRectSize.x,
                    clipRectPos.y + clipRectSize.y);
        g->drawLine(clipRectPos.x + clipRectSize.x, clipRectPos.y, clipRectPos.x + clipRectSize.x,
                    clipRectPos.y + clipRectSize.y);
    }
}

void SongButton::drawGrade() {
    // scaling
    const vec2 pos = this->getActualPos();
    const vec2 size = this->getActualSize();

    const auto &gradeImg = osu->getSkin()->getGradeImageSmall(this->getGrade());
    g->pushTransform();
    {
        const float scale = this->calculateGradeScale();
        g->setColor(0xffffffff);
        gradeImg.drawRaw(vec2(pos.x + this->fGradeOffset, pos.y + size.y / 2), scale, AnchorPoint::LEFT);
    }
    g->popTransform();
}

void SongButton::drawTitle(float deselectedAlpha, bool forceSelectedStyle) {
    // scaling
    const vec2 pos = this->getActualPos();
    const vec2 size = this->getActualSize();

    const float titleScale = (size.y * this->fTitleScale) / this->font->getHeight();
    const bool selected = this->isSelected();
    g->setColor((selected || forceSelectedStyle) ? osu->getSkin()->c_song_select_active_text
                                                 : osu->getSkin()->c_song_select_inactive_text);
    if(!(selected || forceSelectedStyle)) g->setAlpha(deselectedAlpha);

    const DatabaseBeatmap *map = this->getDatabaseBeatmap();
    const std::string_view title{map ? map->getTitle() : ""sv};

    g->pushTransform();
    {
        g->scale(titleScale, titleScale);
        g->translate(pos.x + this->fTextOffset,
                     pos.y + size.y * this->fTextMarginScale + this->font->getHeight() * titleScale);
        g->drawString(this->font, title);
    }
    g->popTransform();
}

void SongButton::drawSubTitle(float deselectedAlpha, bool forceSelectedStyle) {
    // scaling
    const vec2 pos = this->getActualPos();
    const vec2 size = this->getActualSize();

    const float titleScale = (size.y * this->fTitleScale) / this->font->getHeight();
    const float subTitleScale = (size.y * this->fSubTitleScale) / this->font->getHeight();
    const bool selected = this->isSelected();
    g->setColor((selected || forceSelectedStyle) ? osu->getSkin()->c_song_select_active_text
                                                 : osu->getSkin()->c_song_select_inactive_text);
    if(!(selected || forceSelectedStyle)) g->setAlpha(deselectedAlpha);

    const DatabaseBeatmap *map = this->getDatabaseBeatmap();
    const std::string_view artist{map ? map->getArtist() : ""sv};
    const std::string_view mapper{map ? map->getCreator() : ""sv};

    g->pushTransform();
    {
        const std::string subTitleString{fmt::format("{:s} // {:s}", artist, mapper)};

        g->scale(subTitleScale, subTitleScale);
        g->translate(pos.x + this->fTextOffset,
                     pos.y + size.y * this->fTextMarginScale + this->font->getHeight() * titleScale +
                         size.y * this->fTextSpacingScale + this->font->getHeight() * subTitleScale * 0.85f);
        g->drawString(this->font, subTitleString);
    }
    g->popTransform();
}

void SongButton::updateLayoutEx() {
    CarouselButton::updateLayoutEx();

    // scaling
    const vec2 size = this->getActualSize();

    this->fTextOffset = 0.0f;
    this->fGradeOffset = 0.0f;

    const ScoreGrade grade = this->getGrade();
    if(grade != ScoreGrade::N) this->fTextOffset += this->calculateGradeWidth();

    if(osu->getSkin()->version < 2.2f) {
        this->fTextOffset += size.x * 0.02f * 2.0f;
        if(grade != ScoreGrade::N) this->fGradeOffset += this->calculateGradeWidth() / 2.f;
    } else {
        const f32 thumbnailYRatio = getThumbnailYRatio();
        this->fTextOffset += size.y * thumbnailYRatio + size.x * 0.02f;
        this->fGradeOffset += size.y * thumbnailYRatio + size.x * 0.0125f;
    }
}

SongButton *SongButton::setVisible(bool visible) {
    if(visible) {
        // update grade on children if necessary
        if(this->node.isSet()) {
            g_songbrowser->maybeUpdateSetGrades(g_songbrowser->getCarouselData().diffSet[this->node.idx]);
        }
    } else {
        // reset visible time
        this->fVisibleFor = 0.f;
    }
    CarouselButton::setVisible(visible);
    return this;
}

float SongButton::calculateGradeScale() {
    const vec2 size = this->getActualSize();
    const auto &gradeImg = osu->getSkin()->getGradeImageSmall(this->getGrade());
    return Osu::getRectScaleToFitResolution(gradeImg.getSizeBaseRaw(), vec2(size.x, size.y * this->fGradeScale));
}

float SongButton::calculateGradeWidth() {
    const auto &gradeImg = osu->getSkin()->getGradeImageSmall(this->getGrade());
    return gradeImg.getSizeBaseRaw().x * this->calculateGradeScale();
}
