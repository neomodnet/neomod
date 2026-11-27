// Copyright (c) 2016, PG, All rights reserved.
#include "CollectionButton.h"

#include "fmt/format.h"

#include "Font.h"
#include "SongBrowser.h"
// ---

#include "OsuConVars.h"
#include "Engine.h"
#include "Osu.h"
#include "Skin.h"
#include "Graphics.h"

using namespace neomod::sbr;

CollectionButton::CollectionButton(BeatmapCarousel &carousel) : CarouselButton(carousel) {
    // settings
    this->setOffsetPercent(0.075f * 0.5f);
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
    std::string titleString{group.name};
    titleString.append(fmt::format(" ({} map{})", group.numVisibleChildren, group.numVisibleChildren == 1 ? "" : "s"));
    int textXOffset = size.x * 0.02f;
    float titleScale = (size.y * this->fTitleScale) / this->font->getHeight();
    g->setColor(this->isSelected() ? skin->c_song_select_active_text : skin->c_song_select_inactive_text);
    g->pushTransform();
    {
        g->scale(titleScale, titleScale);
        g->translate(pos.x + textXOffset, pos.y + size.y / 2 + (this->font->getHeight() * titleScale) / 2);
        g->drawString(this->font, titleString);
    }
    g->popTransform();
}

Color CollectionButton::getActiveBackgroundColor() const {
    return ARGB_CV_TO_COL(songbrowser_button_collection_active_color);
}
Color CollectionButton::getInactiveBackgroundColor() const {
    return ARGB_CV_TO_COL(songbrowser_button_collection_inactive_color);
}
