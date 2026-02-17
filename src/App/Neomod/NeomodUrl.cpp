// Copyright (c) 2025, kiwec, All rights reserved.
#include "NeomodUrl.h"

#include "crypto.h"
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

namespace neomod {
void handle_neomod_url(std::string_view url) {
    if (url.starts_with(NEOMOD_URL_SCHEME)) {
        url.remove_prefix(NEOMOD_URL_SCHEME ""sv.length());
    } else if (url.starts_with("neosu://")) {
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

    if(url.starts_with("spectate/")) {
        // TODO @kiwec: user id
        return;
    }

    if(url.starts_with("watch_replay/")) {
        // TODO @kiwec: replay md5
        return;
    }
}
}
