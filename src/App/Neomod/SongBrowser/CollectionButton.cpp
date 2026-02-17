// Copyright (c) 2016, PG, All rights reserved.
#include "CollectionButton.h"

#include <utility>

#include "Font.h"
#include "SongBrowser.h"
#include "SongButton.h"
// ---

#include "OsuConVars.h"
#include "Database.h"
#include "MakeDelegateWrapper.h"
#include "Engine.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "Osu.h"
#include "Skin.h"
#include "Graphics.h"
#include "UIContextMenu.h"

using namespace neomod::sbr;

CollectionButton::CollectionButton(float xPos, float yPos, float xSize, float ySize, UString name,
                                   const UString &collectionName, std::vector<SongButton *> children)
    : CarouselButton(xPos, yPos, xSize, ySize, std::move(name)) {
    this->sCollectionName = collectionName.utf8View();
    this->setChildren(std::move(children));

    this->fTitleScale = 0.35f;

    // settings
    this->setOffsetPercent(0.075f * 0.5f);
}

void CollectionButton::draw() {
    if(!this->bVisible) return;
    CarouselButton::draw();

    const auto &skin = osu->getSkin();

    // scaling
    const vec2 pos = this->getActualPos();
    const vec2 size = this->getActualSize();

    // draw title
    UString titleString{this->sCollectionName};
    titleString.append(fmt::format(" ({} map{})", this->numVisibleChildren, this->numVisibleChildren == 1 ? "" : "s"));
    int textXOffset = size.x * 0.02f;
    float titleScale = (size.y * this->fTitleScale) / this->font->getHeight();
    g->setColor(this->bSelected ? skin->c_song_select_active_text : skin->c_song_select_inactive_text);
    g->pushTransform();
    {
        g->scale(titleScale, titleScale);
        g->translate(pos.x + textXOffset, pos.y + size.y / 2 + (this->font->getHeight() * titleScale) / 2);
        g->drawString(this->font, titleString);
    }
    g->popTransform();
}

void CollectionButton::onSelected(bool wasSelected, SelOpts opts) {
    CarouselButton::onSelected(wasSelected, opts);

    g_songbrowser->onSelectionChange(this, true);
    g_songbrowser->scrollToSongButton(this, true);
}

void CollectionButton::onRightMouseUpInside() { this->triggerContextMenu(mouse->getPos()); }

void CollectionButton::triggerContextMenu(vec2 pos) {
    if(g_songbrowser->getGroupingMode() != SongBrowser::GroupType::COLLECTIONS) return;

    assert(g_songbrowser->contextMenu);
    auto *cmenu{g_songbrowser->contextMenu};

    cmenu->setPos(pos);
    cmenu->setRelPos(pos);
    cmenu->begin(0, true);
    {
        cmenu->addButton("[...]      Rename Collection", 1);

        CBaseUIButton *spacer = cmenu->addButton("---");
        spacer->setEnabled(false);
        spacer->setTextColor(0xff888888);
        spacer->setTextDarkColor(0xff000000);

        cmenu->addButton("[-]         Delete Collection", 2);

        CBaseUIButton *spacer2 = cmenu->addButton("---");
        spacer2->setEnabled(false);
        spacer2->setTextColor(0xff888888);
        spacer2->setTextDarkColor(0xff000000);

        cmenu->addButton("[=]         Export Collection", 5);
    }
    cmenu->end(false, false);
    cmenu->setClickCallback(SA::MakeDelegate<&CollectionButton::onContextMenu>(this));
    cmenu->clampToRightScreenEdge();
    cmenu->clampToBottomScreenEdge();
}

void CollectionButton::onContextMenu(const UString &text, int id) {
    assert(g_songbrowser->contextMenu);
    auto *cmenu{g_songbrowser->contextMenu};

    if(id == 1) {
        cmenu->begin(0, true);
        {
            CBaseUIButton *label = cmenu->addButton("Enter Collection Name:");
            label->setEnabled(false);

            CBaseUIButton *spacer = cmenu->addButton("---");
            spacer->setEnabled(false);
            spacer->setTextColor(0xff888888);
            spacer->setTextDarkColor(0xff000000);

            cmenu->addTextbox(this->sCollectionName.c_str(), id)->setCursorPosRight();

            spacer = cmenu->addButton("---");
            spacer->setEnabled(false);
            spacer->setTextColor(0xff888888);
            spacer->setTextDarkColor(0xff000000);

            label = cmenu->addButton("(Press ENTER to confirm.)", id);
            label->setTextColor(0xff555555);
            label->setTextDarkColor(0xff000000);
        }
        cmenu->end(false, false);
        cmenu->setClickCallback(SA::MakeDelegate<&CollectionButton::onRenameCollectionConfirmed>(this));
        cmenu->clampToRightScreenEdge();
        cmenu->clampToBottomScreenEdge();
    } else if(id == 2) {
        if(keyboard->isShiftDown())
            this->onDeleteCollectionConfirmed(text, id);
        else {
            cmenu->begin(0, true);
            {
                cmenu->addButton("Really delete collection?")->setEnabled(false);
                CBaseUIButton *spacer = cmenu->addButton("---");
                spacer->setEnabled(false);
                spacer->setTextColor(0xff888888);
                spacer->setTextDarkColor(0xff000000);
                cmenu->addButton("Yes", 2);
                cmenu->addButton("No");
            }
            cmenu->end(false, false);
            cmenu->setClickCallback(SA::MakeDelegate<&CollectionButton::onDeleteCollectionConfirmed>(this));
            cmenu->clampToRightScreenEdge();
            cmenu->clampToBottomScreenEdge();
        }
    } else if(id == 5) {
        // TODO: custom export names, maybe
        g_songbrowser->onCollectionButtonContextMenu(this, this->sCollectionName, id);
    }
}

void CollectionButton::onRenameCollectionConfirmed(const UString &text, int /*id*/) {
    if(text.lengthUtf8() > 0) {
        // forward it
        g_songbrowser->onCollectionButtonContextMenu(this, text, 3);
    }
}

void CollectionButton::onDeleteCollectionConfirmed(const UString & /*text*/, int id) {
    if(id != 2) return;

    // just forward it
    g_songbrowser->onCollectionButtonContextMenu(this, this->sCollectionName.c_str(), id);
}

Color CollectionButton::getActiveBackgroundColor() const {
    return argb(std::clamp<int>(cv::songbrowser_button_collection_active_color_a.getInt(), 0, 255),
                std::clamp<int>(cv::songbrowser_button_collection_active_color_r.getInt(), 0, 255),
                std::clamp<int>(cv::songbrowser_button_collection_active_color_g.getInt(), 0, 255),
                std::clamp<int>(cv::songbrowser_button_collection_active_color_b.getInt(), 0, 255));
}

Color CollectionButton::getInactiveBackgroundColor() const {
    return argb(std::clamp<int>(cv::songbrowser_button_collection_inactive_color_a.getInt(), 0, 255),
                std::clamp<int>(cv::songbrowser_button_collection_inactive_color_r.getInt(), 0, 255),
                std::clamp<int>(cv::songbrowser_button_collection_inactive_color_g.getInt(), 0, 255),
                std::clamp<int>(cv::songbrowser_button_collection_inactive_color_b.getInt(), 0, 255));
}
