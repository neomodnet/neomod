// Copyright (c) 2016, PG, All rights reserved.
#include "CollectionButton.h"

#include "fmt/format.h"

#include "Font.h"
#include "SongBrowser.h"
#include "CarouselData.h"
// ---

#include "OsuConVars.h"
#include "Engine.h"
#include "i18n.h"
#include "Osu.h"
#include "Skin.h"
#include "Graphics.h"

using namespace neomod::sbr;

CollectionButton::CollectionButton(BeatmapCarousel &carousel) : CarouselButton(carousel) {
    // settings
    this->setOffsetPercent(0.075f * 0.5f);
}

void CollectionButton::updateLayoutEx() {
    if(this->node.isMissingMaps()) this->setOffsetPercent(this->isSelected() ? 0.075f : 0.f);
    CarouselButton::updateLayoutEx();
}

void CollectionButton::draw() {
    if(!this->bVisible) return;
    CarouselButton::draw();

    const auto *skin = osu->getSkin();

    // scaling
    const vec2 pos = this->getActualPos();
    const vec2 size = this->getActualSize();

    // draw title
    const CarouselGroup &group = g_songbrowser->getCarouselData().groups[this->node.idx];
    std::string titleString;
    std::string subtitleString;
    if(this->node.isMissingMaps()) {
        if(const size_t queued = g_songbrowser->getQueuedMissingCollectionMapsCount(this->node.idx); queued > 0) {
            titleString = tformat("Downloading missing maps... ({} remaining)", queued);
            subtitleString = _("Click to cancel");
        } else {
            titleString = group.downloadableMaps.size() == 1
                              ? std::string{_("1 missing map")}
                              : tformat("{} missing maps", group.downloadableMaps.size());
            subtitleString = _("Click to download");
        }
    } else {
        titleString = group.name;
        titleString.append(
            fmt::format(" ({} map{})", group.numVisibleChildren, group.numVisibleChildren == 1 ? "" : "s"));
    }
    int textXOffset = size.x * 0.02f;
    float titleScale = (size.y * this->fTitleScale) / this->font->getHeight();
    g->setColor(this->isSelected() ? skin->c_song_select_active_text : skin->c_song_select_inactive_text);
    const auto drawLine = [&](const std::string &text, float scale, float baselineY) {
        g->pushTransform();
        {
            g->scale(scale, scale);
            g->translate(pos.x + textXOffset, baselineY);
            g->drawString(this->font, text);
        }
        g->popTransform();
    };
    if(subtitleString.empty()) {
        drawLine(titleString, titleScale, pos.y + size.y / 2 + (this->font->getHeight() * titleScale) / 2);
    } else {
        const float subtitleScale = titleScale * 0.75f;
        drawLine(titleString, titleScale, pos.y + size.y / 2 - size.y * 0.04f);
        drawLine(subtitleString, subtitleScale,
                 pos.y + size.y / 2 + size.y * 0.04f + this->font->getHeight() * subtitleScale);
    }
}

Color CollectionButton::getActiveBackgroundColor() const {
    if(this->node.isMissingMaps()) return argb(255, 220, 220, 220);
    return ARGB_CV_TO_COL(songbrowser_button_collection_active_color);
}
Color CollectionButton::getInactiveBackgroundColor() const {
    if(this->node.isMissingMaps()) return argb(255, 40, 40, 40);
    return ARGB_CV_TO_COL(songbrowser_button_collection_inactive_color);
}
