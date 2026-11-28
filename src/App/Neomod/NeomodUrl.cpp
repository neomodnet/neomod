// Copyright (c) 2025, kiwec, All rights reserved.
#include "NeomodUrl.h"

#include "crypto.h"
#include "Accounts.h"
#include "ContainerRanges.h"
#include "Bancho.h"
#include "OsuConVars.h"
#include "Engine.h"
#include "Lobby.h"
#include "MainMenu.h"
#include "NetworkHandler.h"
#include "NotificationOverlay.h"
#include "OptionsOverlay.h"
#include "Environment.h"
#include "Parsing.h"
#include "SpectatorScreen.h"
#include "SongBrowser/SongBrowser.h"
#include "SString.h"
#include "UI.h"
#include "Logging.h"
#include "MD5Hash.h"
#include "Osu.h"
#include "i18n.h"

#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>

namespace neomod {
namespace {
struct PendingAction {
    std::string server;
    std::string action;
} pending;

std::optional<MD5Hash> parse_md5(std::string_view str) {
    std::array<char, 32> md5{};
    if(str.size() != md5.size() || !Mc::ranges::all_of(str, [](unsigned char c) { return std::isxdigit(c) != 0; })) {
        return std::nullopt;
    }
    std::copy(str.begin(), str.end(), md5.begin());
    return MD5Hash{md5};
}

void run_online_action(std::string_view action) {
    if(action.starts_with("join/")) {
        // <room id>[/<password>]
        const auto parts = SString::split(action.substr("join/"sv.length()), '/');
        u32 room_id{0};
        if(parts.empty() || !Parsing::strto_s(parts[0], room_id)) {
            ui->getNotificationOverlay()->addToast(_("This link isn't valid."), ERROR_TOAST);
            return;
        }
        ui->getLobby()->joinRoom(room_id, parts.size() > 1 ? parts[1] : std::string_view{});
        return;
    }

    if(action.starts_with("spectate/")) {
        // <user id>
        i32 user_id{0};
        if(!Parsing::strto_s(action.substr("spectate/"sv.length()), user_id)) {
            ui->getNotificationOverlay()->addToast(_("This link isn't valid."), ERROR_TOAST);
            return;
        }
        Spectating::start(user_id);
        return;
    }

    ui->getNotificationOverlay()->addToast(_("This link isn't valid."), ERROR_TOAST);
}
}  // namespace

void run_pending_online_action() {
    const PendingAction action = std::exchange(pending, {});
    if(action.server == BanchoState::endpoint) run_online_action(action.action);
}

void handle_neomod_url(std::string_view url) {
    if(url.starts_with(NEOMOD_URL_SCHEME)) {
        url.remove_prefix(NEOMOD_URL_SCHEME ""sv.length());
    } else if(url.starts_with("neosu://")) {
        url.remove_prefix("neosu://"sv.length());
    } else {
        return;
    }
    if(url.starts_with("run")) {
        // nothing to do
        return;
    }

    if(url.starts_with("online/")) {
        // <server>/<action>
        url.remove_prefix("online/"sv.length());
        const size_t slash = url.find('/');
        if(slash == 0 || slash == std::string_view::npos) {
            ui->getNotificationOverlay()->addToast(_("This link isn't valid."), ERROR_TOAST);
            return;
        }
        const std::string_view server = url.substr(0, slash);
        const std::string_view action = url.substr(slash + 1);

        if(BanchoState::endpoint == server) {
            if(BanchoState::is_online()) {
                run_online_action(action);
                return;
            }
            if(BanchoState::is_logging_in() || BanchoState::get_online_status() == OnlineStatus::POLLING) {
                pending = {.server = std::string{server}, .action = std::string{action}};
                return;
            }
        }

        ui->getOptionsOverlay()->setServer(server);
        const Account *account = Accounts::find(server);
        if(!account || (account->oauth_token.empty() && account->password_md5.empty())) {
            pending = {};
            ui->getNotificationOverlay()->addToast(tformat("Log in to {} to open this link.", server), ERROR_TOAST);
            ui->getOptionsOverlay()->askForLoginDetails();
            return;
        }

        pending = {.server = std::string{server}, .action = std::string{action}};
        BanchoState::reconnect();
        return;
    }

    if(url.starts_with("select_map/")) {
        // <beatmapset id>/<map md5 or map id>
        const auto parts = SString::split(url.substr("select_map/"sv.length()), '/');
        i32 set_id{0};
        i32 map_id{0};
        const auto md5 = parts.size() == 2 ? parse_md5(parts[1]) : std::nullopt;
        if(parts.size() != 2 || !Parsing::strto_s(parts[0], set_id) ||
           (!md5 && (!Parsing::strto_s(parts[1], map_id) || map_id <= 0))) {
            ui->getNotificationOverlay()->addToast(_("This link isn't valid."), ERROR_TOAST);
            return;
        }
        ui->getSongBrowser()->selectOrDownload(set_id, map_id, md5.value_or(MD5Hash{}));
        if(ui->getMainMenu()->isVisible()) ui->setScreen(ui->getSongBrowser());
        return;
    }

    if(url.starts_with("edit/")) {
        // <map md5>/<mm:ss:mmm>[/<combo numbers>]
        const auto parts = SString::split(url.substr("edit/"sv.length()), '/');
        const auto md5 = parts.size() >= 2 ? parse_md5(parts[0]) : std::nullopt;
        if(!md5) {
            ui->getNotificationOverlay()->addToast(_("This editor link isn't valid."), ERROR_TOAST);
            return;
        }
        osu->openEditorLink(*md5,
                            parts.size() > 2 ? fmt::format("{} ({})", parts[1], parts[2]) : std::string{parts[1]});
        return;
    }
}
}  // namespace neomod
