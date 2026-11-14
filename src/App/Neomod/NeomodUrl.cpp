// Copyright (c) 2025, kiwec, All rights reserved.
#include "NeomodUrl.h"

#include "crypto.h"
#include "ContainerRanges.h"
#include "Bancho.h"
#include "OsuConVars.h"
#include "Engine.h"
#include "NetworkHandler.h"
#include "NotificationOverlay.h"
#include "OptionsOverlay.h"
#include "Environment.h"
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
#include <string>

namespace neomod {
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

    if(url.starts_with("join_lobby/")) {
        // TODO @kiwec: lobby id
        return;
    }

    if(url.starts_with("select_map/")) {
        // TODO @kiwec: beatmapset + md5 combo
        return;
    }

    if(url.starts_with("edit/")) {
        // <map md5>/<mm:ss:mmm>[/<combo numbers>]
        const auto parts = SString::split(url.substr("edit/"sv.length()), '/');
        std::array<char, 32> md5{};
        if(parts.size() < 2 || parts[0].size() != md5.size() ||
           !Mc::ranges::all_of(parts[0], [](unsigned char c) { return std::isxdigit(c) != 0; })) {
            ui->getNotificationOverlay()->addToast(_("This editor link isn't valid."), ERROR_TOAST);
            return;
        }
        std::copy(parts[0].begin(), parts[0].end(), md5.begin());
        osu->openEditorLink(MD5Hash{md5},
                            parts.size() > 2 ? fmt::format("{} ({})", parts[1], parts[2]) : std::string{parts[1]});
        return;
    }

    if(url.starts_with("spectate/")) {
        // TODO @kiwec: user id
        return;
    }
}
}  // namespace neomod
