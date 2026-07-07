// Copyright (c) 2024, kiwec, All rights reserved.
#include "UIAvatar.h"

#include "ThumbnailManager.h"
#include "Bancho.h"
#include "BanchoUsers.h"
#include "Engine.h"
#include "Environment.h"
#include "Font.h"
#include "Icons.h"
#include "Paths.h"
#include "Osu.h"
#include "UI.h"
#include "Graphics.h"
#include "Image.h"
#include "UIUserContextMenu.h"
#include "UniString.h"
#include "MakeDelegateWrapper.h"

#include <algorithm>
#include <cmath>

UIAvatar::UIAvatar(CBaseUIElement *parent, i32 player_id, float xPos, float yPos, float xSize, float ySize)
    : CBaseUIButton(xPos, yPos, xSize, ySize, "avatar", ""),
      parent(parent),
      thumb_id(new ThumbIdentifier{
          .save_path = fmt::format("{}/avatars/{}/{}", Mc::Paths::cache(), BanchoState::endpoint, player_id),
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
        if(ThumbnailManager *am = osu && osu->getThumbnailManager() ? osu->getThumbnailManager() : nullptr) {
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

    auto *thumbnail_manager = osu->getThumbnailManager();
    if(!this->load_requested) {
        this->load_requested = true;
        // add to load queue
        thumbnail_manager->request_image(*this->thumb_id);
    }

    if(const Image *avatar_image = thumbnail_manager->try_get_image(*this->thumb_id)) {
        g->pushTransform();
        {
            const vec2 scale{this->getSize() / vec2{avatar_image->getSize()}};

            g->setColor(Color(0xffffffff).setA(alpha));
            g->scale(scale);
            g->translate(this->getPos().x + this->getSize().x / 2.0f, this->getPos().y + this->getSize().y / 2.0f);
            g->drawImage(avatar_image);
        }
        g->popTransform();
    }

    if(Osu::isDiscordActivity()) {
        if(const auto *user = BANCHO::User::try_get_user_info(this->thumb_id->id)) {
            const vec2 pos = this->getPos();
            const vec2 size = this->getSize();

            if(user->speaking) {
                const f32 border = std::round(std::max(2.f, 3.f * Osu::getUIScale()));
                g->setColor(Color(0xff2dff7a).setA(alpha));
                g->fillRectf(pos.x, pos.y, size.x, border);
                g->fillRectf(pos.x, pos.y + size.y - border, size.x, border);
                g->fillRectf(pos.x, pos.y + border, border, size.y - 2.f * border);
                g->fillRectf(pos.x + size.x - border, pos.y + border, border, size.y - 2.f * border);
            }

            if(user->voice_muted || user->voice_deafened) {
                const char32_t icon = user->voice_deafened ? Icons::DEAF : Icons::MICROPHONE_SLASH;
                const std::string icon_str = UniString::to_utf8(std::u32string_view{&icon, 1});

                auto *font = osu->getFontIcons();
                const f32 scale = (size.y * 0.45f) / font->getHeight();
                const f32 box = size.y * 0.5f;

                g->setColor(Color(0xff000000).setA(alpha));
                g->fillRectf(pos.x + size.x - box, pos.y + size.y - box, box, box);

                g->pushTransform();
                {
                    g->scale(scale, scale);
                    g->translate(pos.x + size.x - box / 2.f - font->getStringWidth(icon_str) * scale / 2.f,
                                 pos.y + size.y - box / 2.f + font->getHeight() * scale / 2.f);
                    g->drawString(font, icon_str, TextFX{.col_text = Color(0xffff5050).setA(alpha)});
                }
                g->popTransform();
            }
        }
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

    ui->getUIUserContextMenuScreen()->open(this->thumb_id->id);
}
