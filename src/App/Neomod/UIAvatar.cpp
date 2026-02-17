// Copyright (c) 2024, kiwec, All rights reserved.
#include "UIAvatar.h"

#include "Logging.h"
#include "ThumbnailManager.h"
#include "Bancho.h"
#include "Engine.h"
#include "Environment.h"
#include "Osu.h"
#include "UI.h"
#include "Graphics.h"
#include "UIUserContextMenu.h"
#include "MakeDelegateWrapper.h"

UIAvatar::UIAvatar(CBaseUIElement *parent, i32 player_id, float xPos, float yPos, float xSize, float ySize)
    : CBaseUIButton(xPos, yPos, xSize, ySize, "avatar", ""),
      parent(parent),
      thumb_id(new ThumbIdentifier{
          .save_path = fmt::format("{}/avatars/{}/{}", env->getCacheDir(), BanchoState::endpoint, player_id),
          .download_url = fmt::format("a.{}/{:d}", BanchoState::endpoint, player_id),
          .id = player_id}) {
    this->setClickCallback(SA::MakeDelegate<&UIAvatar::onAvatarClicked>(this));

    if(this->isVisible()) {
        this->load_requested = true;
        // add to load queue
        osu->getThumbnailManager()->request_image(*this->thumb_id);
    } else {
        this->load_requested = false;
        // delay until visible
    }
}

UIAvatar::~UIAvatar() {
    if(this->load_requested) {
        // remove from load queue
        if(ThumbnailManager *am = osu && osu->getThumbnailManager() ? osu->getThumbnailManager().get() : nullptr) {
            am->discard_image(*this->thumb_id);
        }
    }
}

bool UIAvatar::isVisible() {
    if(this->parent) {
        return this->parent->isVisible() && osu->getVirtScreenRect().intersects(this->parent->getRect()) &&
               this->parent->getRect().intersects(this->getRect());
    } else {
        return this->bVisible && osu->getVirtScreenRect().intersects(this->getRect());
    }
}

void UIAvatar::draw_avatar(float alpha) {
    if(!this->isVisible()) {  // Comment when you need to debug on_screen logic
        // debugLog("not visible {} parent {}", this->getRect(), this->parent ? this->parent->getRect() : McRect{});
        return;
    }

    const auto &thumbnail_manager = osu->getThumbnailManager();
    if(!this->load_requested) {
        this->load_requested = true;
        // add to load queue
        thumbnail_manager->request_image(*this->thumb_id);
    }

    auto *avatar_image = thumbnail_manager->try_get_image(*this->thumb_id);
    if(avatar_image) {
        g->pushTransform();
        g->setColor(Color(0xffffffff).setA(alpha));

        g->scale(this->getSize().x / avatar_image->getWidth(), this->getSize().y / avatar_image->getHeight());
        g->translate(this->getPos().x + this->getSize().x / 2.0f, this->getPos().y + this->getSize().y / 2.0f);
        g->drawImage(avatar_image);
        g->popTransform();
    }

    // For debugging purposes
    // if(on_screen) {
    //     g->pushTransform();
    //     g->setColor(0xff00ff00);
    //     g->drawQuad((int)this->getPos().x, (int)this->getPos().y, (int)this->getSize().x, (int)this->getSize().y);
    //     g->popTransform();
    // } else {
    //     g->pushTransform();
    //     g->setColor(0xffff0000);
    //     g->drawQuad((int)this->getPos().x, (int)this->getPos().y, (int)this->getSize().x, (int)this->getSize().y);
    //     g->popTransform();
    // }
}

void UIAvatar::onAvatarClicked(CBaseUIButton * /*btn*/) {
    if(osu->isInPlayMode()) {
        // Don't want context menu to pop up while playing a map
        return;
    }

    ui->getUserActions()->open(this->thumb_id->id);
}
