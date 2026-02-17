// Copyright (c) 2025, kiwec, All rights reserved.
#include "BanchoApi.h"

#include "AsyncIOHandler.h"
#include "OsuConVars.h"
#include "Logging.h"

#include "LegacyReplay.h"
#include "Bancho.h"
#include "BanchoLeaderboard.h"
#include "Downloader.h"
#include "NetworkHandler.h"
#include "NotificationOverlay.h"
#include "score.h"
#include "UI.h"

namespace BANCHO::Api {

static void handle_api_response(const Packet &packet) {
    switch(packet.id) {
        case RequestType::GET_BEATMAPSET_INFO: {
            Downloader::process_beatmapset_info_response(packet);
            break;
        }

        case RequestType::GET_MAP_LEADERBOARD: {
            BANCHO::Leaderboard::process_leaderboard_response(packet);
            break;
        }

        case RequestType::GET_REPLAY: {
            if(packet.size == 0) {
                // Most likely, 404
                ui->getNotificationOverlay()->addToast(US_("Failed to download replay"), ERROR_TOAST);
                break;
            }

            assert(packet.extra && "handle_api_response(GET_REPLAY) got invalid packet for replay data");
            // we should look into this if it ever gets hit, shouldn't crash either way but still... a warning is a warning
            assert(reinterpret_cast<uintptr_t>(packet.extra) % alignof(FinishedScore) == 0 &&
                   "handle_api_response(GET_REPLAY) packet.extra is not aligned");
            // excuse all the preprocessor bloat
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
#endif
            auto *score = reinterpret_cast<FinishedScore *>(packet.extra);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

            auto replay_path =
                fmt::format(NEOMOD_REPLAYS_PATH "/{:s}/{:d}.replay.lzma", score->server, score->unixTimestamp);

            // TODO: progress bars? how do we make sure the user doesnt do anything weird while its saving to disk and break the flow
            debugLog("Saving replay to {}...", replay_path);

            io->write(replay_path, packet.memory, packet.size, [to_watch = *score](bool success) {
                if(success) {
                    LegacyReplay::load_and_watch(to_watch);
                } else {
                    ui->getNotificationOverlay()->addToast(US_("Failed to save replay"), ERROR_TOAST);
                }
            });

            break;
        }

        case RequestType::MARK_AS_READ: {
            // (nothing to do)
            break;
        }

        default: {
            // NOTE: API Response type is same as API Request type
            debugLog("No handler for API response type {:d}!", packet.id);
        }
    }
}

void send_request(const Request &request) {
    if(!BanchoState::is_online()) {
        debugLog("Cannot send API request of type {:d} since we are not logged in.",
                 static_cast<unsigned int>(request.type));
        // need to free this here, since we never send the http request
        free(request.extra);
        return;
    }

    Mc::Net::RequestOptions options{
        .user_agent = "osu!",
        .timeout = 60,
        .connect_timeout = 5,
    };

    auto query_url = fmt::format("osu.{:s}{:s}", BanchoState::endpoint, request.path);

    networkHandler->httpRequestAsync(query_url, std::move(options), [request](Mc::Net::Response response) {
        if(response.success) {
            Packet api_response;
            api_response.id = request.type;
            api_response.extra = request.extra;
            api_response.extra_int = request.extra_int;
            api_response.size = response.body.length();
            api_response.memory = (u8 *)response.body.data();
            handle_api_response(api_response);
        }

        free(request.extra);
    });
}

void append_auth_params(std::string &url, std::string user_param, std::string pw_param) {
    std::string user, pw;
    if(BanchoState::is_oauth) {
        user = "$token";
        pw = BanchoState::cho_token;
    } else {
        user = BanchoState::get_username();
        pw = BanchoState::pw_md5.string();
    }

    url.append(fmt::format("&{:s}={:s}&{:s}={:s}", user_param, user, pw_param, pw));
}

}  // namespace BANCHO::Api
