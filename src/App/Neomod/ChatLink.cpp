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

#include "ctre.hpp"

#include <utility>

ChatLink::ChatLink(float xPos, float yPos, float xSize, float ySize, const UString& link, const UString& label)
    : CBaseUILabel(xPos, yPos, xSize, ySize, link, label) {
    this->link = link;
    this->setDrawFrame(false);
    this->setDrawBackground(true);
    this->setBackgroundColor(0xff2e3784);
}

void ChatLink::update(CBaseUIEventCtx& c) {
    CBaseUILabel::update(c);

    if(this->isMouseInside()) {
        ui->getTooltipOverlay()->begin();
        ui->getTooltipOverlay()->addLine(fmt::format("link: {}", this->link.toUtf8()));
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
        env->openURLInDefaultBrowser(this->link.toUtf8());
    }
}

void ChatLink::onMouseUpInside(bool /*left*/, bool /*right*/) {
    std::wstring link_wstr = this->link.to_wstring();

    // Detect multiplayer invite links
    if(this->link.startsWith(US_("osump://"))) {
        if(ui->getRoom()->isVisible()) {
            ui->getNotificationOverlay()->addNotification("You are already in a multiplayer room.");
            return;
        }

        // If the password has a space in it, parsing will break, but there's no way around it...
        // osu!stable also considers anything after a space to be part of the lobby title :(
        static constexpr ctll::fixed_string osump_pattern{LR"(osump://(\d+)/(\S*))"};
        if(auto match = ctre::search<osump_pattern>(link_wstr)) {
            const UString match_ustr = match.get<1>().to_view();
            u32 invite_id = Parsing::strto<u32>(match_ustr.utf8View());
            UString password = match.get<2>().to_view();
            ui->getLobby()->joinRoom(invite_id, password);
        }
        return;
    }

    const std::wstring endpoint_wstr{UString{BanchoState::endpoint}.to_wstring()};

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
            const UString match_ustr = match.get<2>().to_view();
            i32 user_id = Parsing::strto<i32>(match_ustr.utf8View());
            ui->getUserActions()->open(user_id);
            return;
        }
    }

    // Detect beatmap links
    // https://((osu.)?{endpoint}|osu.ppy.sh)/b(eatmaps)?/{id}
    static constexpr ctll::fixed_string map_pattern{LR"(https?://([^/]+)/b(?:eatmaps)?/(\d+))"};
    if(auto match = ctre::search<map_pattern>(link_wstr)) {
        auto domain = match.get<1>().to_view();
        if(matches_endpoint(domain) || domain == L"osu.ppy.sh") {
            const UString match_ustr = match.get<2>().to_view();
            i32 map_id = Parsing::strto<i32>(match_ustr.utf8View());
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
            const UString match_ustr = match.get<2>().to_view();
            i32 set_id = Parsing::strto<i32>(match_ustr.utf8View());
            i32 map_id = 0;
            if(auto map_group = match.get<3>(); map_group) {
                const UString uGroup = map_group.to_view();
                map_id = Parsing::strto<i32>(uGroup.utf8View());
            }
            this->open_beatmap_link(map_id, set_id);
            return;
        }
    }

    env->openURLInDefaultBrowser(this->link.toUtf8());
}
