// Copyright (c) 2024, kiwec, All rights reserved.
#include "ChatLink.h"

#include "Bancho.h"
#include "Lobby.h"
#include "MainMenu.h"
#include "NotificationOverlay.h"
#include "Osu.h"
#include "Environment.h"
#include "Parsing.h"
#include "RoomScreen.h"
#include "SongBrowser/SongBrowser.h"
#include "TooltipOverlay.h"
#include "UI.h"
#include "UIUserContextMenu.h"
#include "UniString.h"
#include "ctre.hpp"

#include <utility>

ChatLink::ChatLink(float xPos, float yPos, float xSize, float ySize, std::string link, std::string label)
    : CBaseUILabel(xPos, yPos, xSize, ySize, link, std::move(label)) {
    this->link = std::move(link);
    this->setDrawFrame(false);
    this->setDrawBackground(true);
    this->setBackgroundColor(0xff2e3784);
}

void ChatLink::updateInput(CBaseUIEventCtx &c) {
    CBaseUILabel::updateInput(c);

    if(this->isMouseInside()) {
        ui->getTooltipOverlay()->begin();
        ui->getTooltipOverlay()->addLine(fmt::format("link: {}", this->link));
        ui->getTooltipOverlay()->end();

        this->setBackgroundColor(0xff3d48ac);
    } else {
        this->setBackgroundColor(0xff2e3784);
    }
}

void ChatLink::open_beatmap_link(i32 map_id, i32 set_id) {
    if(ui->getSongBrowser()->isVisible()) {
        ui->getSongBrowser()->map_autodl = map_id;
        ui->getSongBrowser()->set_autodl = set_id;
    } else if(ui->getMainMenu()->isVisible()) {
        ui->setScreen(ui->getSongBrowser());
        ui->getSongBrowser()->map_autodl = map_id;
        ui->getSongBrowser()->set_autodl = set_id;
    } else {
        env->openURLInDefaultBrowser(this->link);
    }
}

void ChatLink::onMouseUpInside(bool /*left*/, bool /*right*/) {
    std::wstring link_wstr = UniString::to_wide(this->link);

    // Detect multiplayer invite links
    if(this->link.starts_with("osump://")) {
        if(ui->getRoomScreen()->isVisible()) {
            ui->getNotificationOverlay()->addNotification("You are already in a multiplayer room.");
            return;
        }

        // If the password has a space in it, parsing will break, but there's no way around it...
        // osu!stable also considers anything after a space to be part of the lobby title :(
        static constexpr ctll::fixed_string osump_pattern{LR"(osump://(\d+)/(\S*))"};
        if(auto match = ctre::search<osump_pattern>(link_wstr)) {
            std::wstring_view match_wstr = match.get<1>().to_view();
            u32 invite_id = Parsing::strto<u32>(UniString::to_utf8(match_wstr));
            std::wstring_view password = match.get<2>().to_view();
            ui->getLobby()->joinRoom(invite_id, UniString::to_utf8(password));
        }
        return;
    }

    const std::wstring endpoint_wstr{UniString::to_wide(BanchoState::endpoint)};

    // Helper to check if domain matches the configured endpoint (with optional "osu." prefix)
    auto matches_endpoint = [&endpoint_wstr](std::wstring_view domain) {
        if(domain == endpoint_wstr) return true;
        // Check for "osu." prefix
        if(domain.starts_with(L"osu.") && domain.substr(4) == endpoint_wstr) return true;
        return false;
    };

    // Detect user links
    // https://(osu.)?{endpoint}/u(sers)?/{id}
    static constexpr ctll::fixed_string user_pattern{LR"(https?://([^/]+)/u(?:sers)?/(\d+))"};
    if(auto match = ctre::search<user_pattern>(link_wstr)) {
        auto domain = match.get<1>().to_view();
        if(matches_endpoint(domain)) {
            std::wstring_view match_wstr = match.get<2>().to_view();
            i32 user_id = Parsing::strto<i32>(UniString::to_utf8(match_wstr));
            ui->getUIUserContextMenuScreen()->open(user_id);
            return;
        }
    }

    // Detect beatmap links
    // https://((osu.)?{endpoint}|osu.ppy.sh)/b(eatmaps)?/{id}
    static constexpr ctll::fixed_string map_pattern{LR"(https?://([^/]+)/b(?:eatmaps)?/(\d+))"};
    if(auto match = ctre::search<map_pattern>(link_wstr)) {
        auto domain = match.get<1>().to_view();
        if(matches_endpoint(domain) || domain == L"osu.ppy.sh") {
            std::wstring_view match_wstr = match.get<2>().to_view();
            i32 map_id = Parsing::strto<i32>(UniString::to_utf8(match_wstr));
            this->open_beatmap_link(map_id, 0);
            return;
        }
    }

    // Detect beatmapset links
    // https://((osu.)?{endpoint}|osu.ppy.sh)/beatmapsets/{id}(#osu/{id})?
    static constexpr ctll::fixed_string set_pattern{LR"(https?://([^/]+)/beatmapsets/(\d+)(?:#osu/(\d+))?)"};
    if(auto match = ctre::search<set_pattern>(link_wstr)) {
        auto domain = match.get<1>().to_view();
        if(matches_endpoint(domain) || domain == L"osu.ppy.sh") {
            std::wstring_view match_wstr = match.get<2>().to_view();
            i32 set_id = Parsing::strto<i32>(UniString::to_utf8(match_wstr));
            i32 map_id = 0;
            if(auto map_group = match.get<3>(); map_group) {
                std::wstring_view group_wstr = map_group.to_view();
                map_id = Parsing::strto<i32>(UniString::to_utf8(group_wstr));
            }
            this->open_beatmap_link(map_id, set_id);
            return;
        }
    }

    env->openURLInDefaultBrowser(this->link);
}
