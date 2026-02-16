// Copyright (c) 2024, kiwec, All rights reserved.
#include "UIUserContextMenu.h"

#include <algorithm>

#include "Bancho.h"
#include "BanchoNetworking.h"
#include "BanchoUsers.h"
#include "Chat.h"
#include "OsuConVars.h"
#include "Engine.h"
#include "Environment.h"
#include "MakeDelegateWrapper.h"
#include "Mouse.h"
#include "NotificationOverlay.h"
#include "Osu.h"
#include "SpectatorScreen.h"
#include "UI.h"
#include "UIContextMenu.h"
#include "UserCard.h"
#include "UserStatsScreen.h"

UIUserContextMenuScreen::UIUserContextMenuScreen() : UIScreen() {
    this->bVisible = true;
    this->menu = new UIContextMenu();
    this->addBaseUIElement(this->menu);
}

void UIUserContextMenuScreen::onResolutionChange(vec2 newResolution) {
    this->setSize(newResolution);
    UIScreen::onResolutionChange(newResolution);
}

void UIUserContextMenuScreen::stealFocus() {
    UIScreen::stealFocus();
    this->close();
}

void UIUserContextMenuScreen::open(i32 user_id, bool is_song_browser_button) {
    this->close();
    this->user_id = user_id;

    int slot_number = -1;
    if(BanchoState::is_in_a_multi_room()) {
        for(int i = 0; i < 16; i++) {
            if(BanchoState::room.slots[i].player_id == user_id) {
                slot_number = i;
                break;
            }
        }
    }

    this->menu->begin(is_song_browser_button ? osu->getUserButton()->getSize().x : 0);

    const bool is_online = BANCHO::User::is_online_id(user_id);
    if(!ui->getUserStatsScreen()->isVisible() && ((user_id == BanchoState::get_uid()) || !is_online)) {
        this->menu->addButton("View top plays", VIEW_TOP_PLAYS);
    }

    if(is_online) {
        this->menu->addButton("View profile page", VIEW_PROFILE);
    };

    if(user_id != BanchoState::get_uid()) {
        if(BanchoState::room.is_host() && slot_number != -1) {
            this->menu->addButton("Set as Host", UA_TRANSFER_HOST);
            this->menu->addButton("Kick", KICK);
        }

        const UserInfo* user_info = BANCHO::User::get_user_info(user_id, true);
        if(user_info->has_presence) {
            // Without user info, we don't have the username
            this->menu->addButton("Start Chat", START_CHAT);

            // XXX: Not implemented
            // menu->addButton("Invite to game", INVITE_TO_GAME);
        }

        if(user_info->is_friend()) {
            this->menu->addButton("Revoke friendship", UA_REMOVE_FRIEND);
        } else {
            this->menu->addButton("Add as friend", UA_ADD_FRIEND);
        }

        if(cv::enable_spectating.getBool()) {
            if(BanchoState::spectated_player_id == user_id) {
                menu->addButton("Stop spectating", TOGGLE_SPECTATE);
            } else {
                menu->addButton("Spectate", TOGGLE_SPECTATE);
            }
        }
    }

    if(is_song_browser_button) {
        // Menu would open halfway off-screen, extra code to remove the jank
        this->menu->end(true, false);
        auto userPos = osu->getUserButton()->getPos();
        this->menu->setPos(userPos.x, userPos.y - this->menu->getSize().y);
    } else {
        this->menu->end(false, false);
        this->menu->setPos(mouse->getPos());
    }
    this->menu->setClickCallback(SA::MakeDelegate<&UIUserContextMenuScreen::on_action>(this));
    this->menu->setVisible(true);
}

void UIUserContextMenuScreen::close() { this->menu->setVisible(false); }

void UIUserContextMenuScreen::on_action(const UString& /*text*/, int user_action) {
    UserInfo* user_info = BANCHO::User::get_user_info(this->user_id);
    int slot_number = -1;
    if(BanchoState::is_in_a_multi_room()) {
        for(int i = 0; i < 16; i++) {
            if(BanchoState::room.slots[i].player_id == this->user_id) {
                slot_number = i;
                break;
            }
        }
    }

    if(user_action == UA_TRANSFER_HOST) {
        Packet packet;
        packet.id = OUTP_TRANSFER_HOST;
        packet.write<u32>(slot_number);
        BANCHO::Net::send_packet(packet);
    } else if(user_action == KICK) {
        Packet packet;
        packet.id = OUTP_MATCH_LOCK;
        packet.write<u32>(slot_number);
        BANCHO::Net::send_packet(packet);  // kick by locking the slot
        BANCHO::Net::send_packet(packet);  // unlock the slot
    } else if(user_action == START_CHAT) {
        ui->getChat()->openChannel(user_info->name);
    } else if(user_action == VIEW_PROFILE) {
        // Fallback in case we're offline
        auto endpoint = BanchoState::endpoint;
        if(endpoint == "") endpoint = "ppy.sh";

        auto scheme = cv::use_https.getBool() ? "https://" : "http://";
        auto url = fmt::format("{}osu.{}/u/{}", scheme, endpoint, this->user_id);
        ui->getNotificationOverlay()->addNotification("Opening browser, please wait ...", 0xffffffff, false, 0.75f);
        env->openURLInDefaultBrowser(url);
    } else if(user_action == UA_ADD_FRIEND) {
        Packet packet;
        packet.id = OUTP_FRIEND_ADD;
        packet.write<i32>(this->user_id);
        BANCHO::Net::send_packet(packet);
        BANCHO::User::friends.insert(this->user_id);
    } else if(user_action == UA_REMOVE_FRIEND) {
        Packet packet;
        packet.id = OUTP_FRIEND_REMOVE;
        packet.write<i32>(this->user_id);
        BANCHO::Net::send_packet(packet);

        auto it = std::ranges::find(BANCHO::User::friends, this->user_id);
        if(it != BANCHO::User::friends.end()) {
            BANCHO::User::friends.erase(it);
        }
    } else if(user_action == TOGGLE_SPECTATE) {
        if(BanchoState::spectated_player_id == this->user_id) {
            Spectating::stop();
        } else {
            Spectating::start(this->user_id);
        }
    } else if(user_action == VIEW_TOP_PLAYS) {
        ui->setScreen(ui->getUserStatsScreen());
    }

    this->menu->setVisible(false);
}

UIUserLabel::UIUserLabel(i32 user_id, const UString& username) : CBaseUILabel() {
    this->user_id = user_id;
    this->setText(username);
    this->setDrawFrame(false);
    this->setDrawBackground(false);
}

void UIUserLabel::onMouseUpInside(bool /*left*/, bool /*right*/) { ui->getUserActions()->open(this->user_id); }
