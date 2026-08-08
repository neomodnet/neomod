// Copyright (c) 2024, kiwec, All rights reserved.
#include "UIUserContextMenu.h"

#include <algorithm>

#include "Bancho.h"
#include "BanchoNetworking.h"
#include "BanchoUsers.h"
#include "Chat.h"
#include "OsuConVars.h"
#include "Database.h"
#include "Engine.h"
#include "Environment.h"
#include "MakeDelegateWrapper.h"
#include "Mouse.h"
#include "NotificationOverlay.h"
#include "Osu.h"
#include "SpectatorScreen.h"
#include "SString.h"
#include "UI.h"
#include "UIContextMenu.h"
#include "UserCard.h"
#include "UserStatsScreen.h"
#include "i18n.h"

namespace {
enum UserActions : uint8_t {
    UA_TRANSFER_HOST,
    UA_KICK,
    UA_VIEW_PROFILE,
    UA_TOGGLE_SPECTATE,
    UA_START_CHAT,
    UA_INVITE_TO_GAME,
    UA_ADD_FRIEND,
    UA_REMOVE_FRIEND,
    UA_VIEW_TOP_PLAYS,
    UA_SWITCH_USER,
    UA_CHANGE_NAME,
    UA_CHANGE_ONLINE_SETTINGS
};
}  // namespace

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
    this->from_user_button = is_song_browser_button;

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

    // offline user switcher (own card only)
    if(!BanchoState::is_online() && user_id == BanchoState::get_uid()) {
        auto *header = this->menu->addButtonJustified(_("Switch User:"));
        header->setTextColor(0xff888888);
        header->setTextDarkColor(0xff000000);
        header->setEnabled(false);

        for(const auto &name : db->getPlayerNamesWithScoresForUserSwitcher()) {
            auto *nameButton = this->menu->addButton(name, UA_SWITCH_USER);
            if(name == cv::name.getString()) nameButton->setTextBrightColor(0xff00ff00);
        }

        CBaseUIButton *spacer = this->menu->addButton("---");
        spacer->setEnabled(false);
        spacer->setTextColor(0xff888888);
        spacer->setTextDarkColor(0xff000000);

        this->menu->addButton(_("Set custom name"), UA_CHANGE_NAME);
    }

    const bool is_online = BANCHO::User::is_online_id(user_id);
    const bool is_self = user_id == BanchoState::get_uid();
    if(!ui->getUserStatsScreen()->isVisible() && (is_self || !is_online)) {
        this->menu->addButton("View top plays", UA_VIEW_TOP_PLAYS);
    }

    if(is_online) {
        this->menu->addButton("View profile page", UA_VIEW_PROFILE);
        if(is_self && BanchoState::endpoint == NEOMOD_DOMAIN) {
            this->menu->addButton("Change online settings", UA_CHANGE_ONLINE_SETTINGS);
        }
    };

    if(!is_self) {
        if(BanchoState::room.is_host() && slot_number != -1) {
            this->menu->addButton("Set as Host", UA_TRANSFER_HOST);
            this->menu->addButton("Kick", UA_KICK);
        }

        const UserInfo *user_info = BANCHO::User::get_user_info(user_id, true);
        if(user_info->has_presence) {
            // Without user info, we don't have the username
            this->menu->addButton("Start Chat", UA_START_CHAT);

            // XXX: Not implemented
            // menu->addButton("Invite to game", INVITE_TO_GAME);
        }

        if(user_info->is_friend()) {
            this->menu->addButton("Revoke friendship", UA_REMOVE_FRIEND);
        } else {
            this->menu->addButton("Add as friend", UA_ADD_FRIEND);
        }

        if(BanchoState::spectated_player_id == user_id) {
            menu->addButton("Stop spectating", UA_TOGGLE_SPECTATE);
        } else {
            menu->addButton("Spectate", UA_TOGGLE_SPECTATE);
        }
    }

    if(is_song_browser_button) {
        // Menu would open halfway off-screen, extra code to remove the jank.
        // Position before end() so vertical clamping can kick in for tall menus (many user names).
        auto userPos = osu->getUserButton()->getPos();
        this->menu->setPos(userPos.x, userPos.y - this->menu->getSize().y);
        this->menu->end(true, UIContextMenu::EndStyle::CLAMP_TOP);
    } else {
        this->menu->setPos(mouse->getPos());
        this->menu->end(false, UIContextMenu::EndStyle::CLAMP_BOT);
    }
    this->menu->setClickCallback(SA::MakeDelegate<&UIUserContextMenuScreen::on_action>(this));
}

void UIUserContextMenuScreen::close() { this->menu->setVisible2(false); }

void UIUserContextMenuScreen::on_action(std::string_view text, int user_action) {
    UserInfo *user_info = BANCHO::User::get_user_info(this->user_id);
    int slot_number = -1;
    if(BanchoState::is_in_a_multi_room()) {
        for(int i = 0; i < 16; i++) {
            if(BanchoState::room.slots[i].player_id == this->user_id) {
                slot_number = i;
                break;
            }
        }
    }

    switch(user_action) {
        case UA_TRANSFER_HOST: {
            Packet packet;
            packet.id = OUTP_TRANSFER_HOST;
            packet.write<u32>(slot_number);
            BANCHO::Net::send_packet(packet);
        } break;
        case UA_KICK: {
            Packet packet;
            packet.id = OUTP_MATCH_LOCK;
            packet.write<u32>(slot_number);
            BANCHO::Net::send_packet(packet);  // kick by locking the slot
            BANCHO::Net::send_packet(packet);  // unlock the slot
        } break;
        case UA_START_CHAT: {
            ui->getChat()->openChannel(user_info->name);
        } break;
        case UA_VIEW_PROFILE: {
            // Fallback in case we're offline
            auto endpoint = BanchoState::endpoint;
            if(endpoint == "") endpoint = "ppy.sh";

            auto scheme = cv::use_https.getBool() ? "https://" : "http://";
            auto url = fmt::format("{}osu.{}/u/{}", scheme, endpoint, this->user_id);
            ui->getNotificationOverlay()->addNotification("Opening browser, please wait ...", 0xffffffff, false, 0.75f);
            env->openURLInDefaultBrowser(url);
        } break;
        case UA_ADD_FRIEND: {
            Packet packet;
            packet.id = OUTP_FRIEND_ADD;
            packet.write<i32>(this->user_id);
            BANCHO::Net::send_packet(packet);
            BANCHO::User::friends.insert(this->user_id);
        } break;
        case UA_REMOVE_FRIEND: {
            Packet packet;
            packet.id = OUTP_FRIEND_REMOVE;
            packet.write<i32>(this->user_id);
            BANCHO::Net::send_packet(packet);

            auto it = std::ranges::find(BANCHO::User::friends, this->user_id);
            if(it != BANCHO::User::friends.end()) {
                BANCHO::User::friends.erase(it);
            }
        } break;
        case UA_TOGGLE_SPECTATE: {
            if(BanchoState::spectated_player_id == this->user_id) {
                Spectating::stop();
            } else {
                Spectating::start(this->user_id);
            }
        } break;
        case UA_VIEW_TOP_PLAYS: {
            ui->setScreen(ui->getUserStatsScreen());
        } break;
        case UA_SWITCH_USER: {
            std::string newName{text};
            SString::trim_inplace(newName);
            if(!newName.empty() && newName != cv::name.getString()) {
                cv::name.setValue(newName);
                osu->onUserCardChange(newName);  // sync options username textbox + card id
                if(ui->getUserStatsScreen()->isVisible()) ui->getUserStatsScreen()->rebuildScoreButtons();
            }
        } break;
        case UA_CHANGE_ONLINE_SETTINGS: {
            // sanity check to see if we're still connected to the same server
            if(BanchoState::endpoint == NEOMOD_DOMAIN) {
                auto scheme = cv::use_https.getBool() ? "https://" : "http://";
                auto url = fmt::format("{}" NEOMOD_DOMAIN "/settings/", scheme);
                ui->getNotificationOverlay()->addNotification("Opening browser, please wait ...", 0xffffffff, false,
                                                              0.75f);
                env->openURLInDefaultBrowser(url);
            }
        } break;
        case UA_CHANGE_NAME: {
            // second-step textbox menu; the textbox id routes enter back through UA_SWITCH_USER
            // (buttons can't coexist with a textbox, since clicks then deliver the textbox text)
            this->menu->begin(this->from_user_button ? osu->getUserButton()->getSize().x : 0);
            {
                this->menu->addButtonJustified(_("Enter Username:"))->setEnabled(false);

                CBaseUIButton *spacer = this->menu->addButton("---");
                spacer->setEnabled(false);
                spacer->setTextColor(0xff888888);
                spacer->setTextDarkColor(0xff000000);

                this->menu->addTextbox(cv::name.getString(), UA_SWITCH_USER)->setCursorPosRight();

                spacer = this->menu->addButton("---");
                spacer->setEnabled(false);
                spacer->setTextColor(0xff888888);
                spacer->setTextDarkColor(0xff000000);

                CBaseUIButton *hint = this->menu->addButton(_("(Press ENTER to confirm.)"), UA_SWITCH_USER);
                hint->setTextColor(0xff555555);
                hint->setTextDarkColor(0xff000000);
            }
            if(this->from_user_button) {
                auto userPos = osu->getUserButton()->getPos();
                this->menu->setPos(userPos.x, userPos.y - this->menu->getSize().y);
                this->menu->end(true, UIContextMenu::EndStyle::CLAMP_TOP);
            } else {
                this->menu->end(false, UIContextMenu::EndStyle::CLAMP_BOT);
            }
            this->menu->setClickCallback(SA::MakeDelegate<&UIUserContextMenuScreen::on_action>(this));
            return;  // keep the rebuilt menu open
        } break;
        default:
            break;
    }

    this->menu->setVisible2(false);
}

UIUserLabel::UIUserLabel(i32 user_id, std::string username) : CBaseUILabel() {
    this->user_id = user_id;
    this->setText(std::move(username));
    this->setDrawFrame(false);
    this->setDrawBackground(false);
}

void UIUserLabel::onMouseUpInside(bool /*left*/, bool /*right*/) {
    ui->getUIUserContextMenuScreen()->open(this->user_id);
}
