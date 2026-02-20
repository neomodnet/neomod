// Copyright (c) 2023, kiwec, All rights reserved.
#include "BanchoNetworking.h"

#include "Osu.h"
#include "Bancho.h"
#include "BanchoProtocol.h"
#include "BanchoUsers.h"
#include "BeatmapInterface.h"
#include "Chat.h"
#include "OsuConVars.h"
#include "ConVarHandler.h"
#include "Database.h"
#include "Downloader.h"
#include "Engine.h"
#include "File.h"
#include "Image.h"
#include "Lobby.h"
#include "MainMenu.h"
#include "NeomodUrl.h"
#include "NetworkHandler.h"
#include "OptionsOverlay.h"
#include "ResourceManager.h"
#include "SongBrowser.h"
#include "SString.h"
#include "Timing.h"
#include "UserCard.h"
#include "UI.h"
#include "Logging.h"
#include "crypto.h"

#include <ctime>
#include <vector>

// Bancho protocol

namespace BANCHO::Net {
namespace {  // static namespace

Packet outgoing;
u64 last_packet_ms{0};
double seconds_between_pings{1.0};
double pong_expected_before{-1.};
std::string auth_token = "";
bool use_websockets = false;
std::shared_ptr<Mc::Net::WSInstance> websocket{nullptr};
double login_poll_timeout{-1.};

void parse_packets(std::span<u8> packet_data) {
    Packet batch = {
        .memory = packet_data.data(),
        .size = packet_data.size(),
        .pos = 0,
    };

    // + 7 for packet header
    while(batch.pos + 7 <= batch.size) {
        u16 packet_id = batch.read<u16>();
        batch.pos++;  // skip compression flag
        u32 packet_len = batch.read<u32>();

        if(packet_len > 10485760) {
            debugLog("Received a packet over 10Mb! Dropping response.");
            break;
        }

        if(batch.pos + packet_len > batch.size) break;

        Packet incoming = {
            .id = packet_id,
            .memory = batch.memory + batch.pos,
            .size = packet_len,
            .pos = 0,
        };
        BanchoState::handle_packet(incoming);

        seconds_between_pings = 1.0;
        pong_expected_before = -1.0;
        batch.pos += packet_len;
    }
}

void attempt_logging_in() {
    assert(!BanchoState::is_online());

    Mc::Net::RequestOptions options{
        .post_data = BanchoState::build_login_packet(),
        .user_agent = "osu!",
        .timeout = 30,
        .connect_timeout = 5,
    };

    options.headers["x-mcosu-ver"] = BanchoState::neomod_version;

    auto query_url = fmt::format("c.{:s}/", BanchoState::endpoint);

    last_packet_ms = Timing::getTicksMS();

    // TODO: allow user to cancel login attempt
    networkHandler->httpRequestAsync(query_url, std::move(options), [](Mc::Net::Response response) {
        if(!response.success) {
            auto errmsg = fmt::format("Failed to log in: {}", response.error_msg);
            ui->getNotificationOverlay()->addToast(errmsg, ERROR_TOAST);
            BanchoState::update_online_status(OnlineStatus::LOGGED_OUT);

            if(Env::cfg(OS::WASM) && response.response_code == 0) {
                // Provide extra guidance since "Connection failed" isn't very descriptive
                ui->getNotificationOverlay()->addToast(
                    "Either you are offline, or the server doesn't support the web version of " PACKAGE_NAME ".", ERROR_TOAST);
            }

            return;
        }

        // Update auth token
        auto cho_token_it = response.headers.find("cho-token");
        if(cho_token_it != response.headers.end()) {
            auth_token = cho_token_it->second;

            // Emscripten seems to add a space at the start of the header... This is obviously wrong.
            // Maybe we shouldn't trim spaces at the *end*, but surely no server uses such weird tokens.
            SString::trim_inplace(auth_token);

            BanchoState::cho_token = auth_token;
            use_websockets = cv::prefer_websockets.getBool();
        }

        auto features_it = response.headers.find("x-mcosu-features");
        if(features_it != response.headers.end()) {
            if(strstr(features_it->second.c_str(), "submit=0") != nullptr) {
                BanchoState::score_submission_policy = ServerPolicy::NO;
                debugLog("Server doesn't want score submission. :(");
            } else if(strstr(features_it->second.c_str(), "submit=1") != nullptr) {
                BanchoState::score_submission_policy = ServerPolicy::YES;
                debugLog("Server wants score submission! :D");
            }
        }

        parse_packets({(u8 *)response.body.data(), response.body.length()});
    });
}

void send_bancho_packet_http(Packet outgoing) {
    if(auth_token.empty()) return;

    Mc::Net::RequestOptions options{
        .user_agent = "osu!",
        .timeout = 30,
        .connect_timeout = 5,
    };

    options.headers["x-mcosu-ver"] = BanchoState::neomod_version;
    options.headers["osu-token"] = auth_token;

    // copy outgoing packet data for POST
    options.post_data = std::string(reinterpret_cast<char *>(outgoing.memory), outgoing.pos);

    auto query_url = fmt::format("c.{:s}/", BanchoState::endpoint);

    networkHandler->httpRequestAsync(query_url, std::move(options), [](Mc::Net::Response response) {
        if(!response.success) {
            debugLog("Failed to send packet, HTTP error {}", response.response_code);
            return;
        }

        parse_packets({(u8 *)response.body.data(), response.body.length()});
    });
}

void send_bancho_packet_ws(Packet outgoing) {
    if(auth_token.empty()) return;

    if(websocket == nullptr || websocket->status == Mc::Net::WSStatus::DISCONNECTED) {
        // We have been disconnected in less than 5 seconds.
        // Don't try to reconnect, server clearly doesn't want us to.
        // (without this, we would be spamming retries every frame)
        if(websocket && websocket->time_created + 5.0 > engine->getTime()) {
            // XXX: dropping websocket->out here
            use_websockets = false;
            send_bancho_packet_http(outgoing);
            return;
        }

        Mc::Net::WSOptions options;
        options.user_agent = "osu!";
        options.headers["x-mcosu-ver"] = BanchoState::neomod_version;
        options.headers["osu-token"] = auth_token;

        std::string url = fmt::format("c.{}/ws/", BanchoState::endpoint);

        auto new_websocket = networkHandler->initWebsocket(url, options);
        if(websocket != nullptr) new_websocket->out = websocket->out;  // don't lose outgoing packet queue
        websocket = new_websocket;
    }

    if(websocket->status == Mc::Net::WSStatus::UNSUPPORTED) {
        // fallback to http!
        use_websockets = false;
        send_bancho_packet_http(outgoing);
    } else {
        // enqueue packets to be sent
        websocket->out.insert(websocket->out.end(), outgoing.memory, outgoing.memory + outgoing.pos);
    }
}

}  // namespace

void update_networking() {
    // Rate limit to every 1ms at most
    static double last_update = 0;
    const double current_time = engine->getTime();
    if(current_time - last_update < 0.001) return;
    last_update = current_time;

    // Initialize last_packet_tms on first call
    static bool initialized = false;
    if(!initialized) {
        last_packet_ms = Timing::getTicksMS();
        initialized = true;
    }

    // Poll login if we need to
    if(login_poll_timeout > 0 && current_time > login_poll_timeout) {
        login_poll_timeout = -1.;
        BanchoState::poll_login();
    }

    // Set ping timeout
    if(osu && ui->getLobby()->isVisible()) seconds_between_pings = 1;
    if(BanchoState::spectating) seconds_between_pings = 1;
    if(BanchoState::is_in_a_multi_room() && seconds_between_pings > 3) seconds_between_pings = 3;
    if(use_websockets) seconds_between_pings = 30;

    if(!BanchoState::is_online()) return;

    const bool should_ping = Timing::getTicksMS() - last_packet_ms > (u64)(seconds_between_pings * 1000);

    // Append missing presence/stats request packets
    // XXX: Rather than every second, this should be done once, and only once
    //      (if we remove the check, right now it could spam 1000x/second)
    static f64 last_presence_request = current_time;
    if(current_time > last_presence_request + 1.f) {
        last_presence_request = current_time;

        BANCHO::User::request_presence_batch();
        BANCHO::User::request_stats_batch();
    }

    // Handle login and outgoing packet processing
    if(should_ping && outgoing.pos == 0) {
        pong_expected_before = current_time + 10.0;

        outgoing.write<u16>(OUTP_PING);
        outgoing.write<u8>(0);
        outgoing.write<u32>(0);

        // Polling gets slower over time, but resets when we receive new data
        if(seconds_between_pings < 30.0) {
            seconds_between_pings += 1.0;
        }
    }

    // Server timed out
    if(pong_expected_before > 0.0 && current_time > pong_expected_before) {
        pong_expected_before = -1.;

        if(use_websockets) {
            // cloudflare might have silently dropped the connection, try opening a new websocket
            websocket = nullptr;
        } else {
            BanchoState::disconnect();
            return;
        }
    }

    if(outgoing.pos > 0) {
        last_packet_ms = Timing::getTicksMS();

        Packet out = outgoing;
        outgoing = Packet();

        if(cv::debug_network.getBool()) {
            // DEBUG: If we're not sending the right amount of bytes, bancho.py just
            // chugs along! To try to detect it faster, we'll send two packets per request.
            out.write<u16>(OUTP_PING);
            out.write<u8>(0);
            out.write<u32>(0);
        }

        if(use_websockets) {
            send_bancho_packet_ws(out);
        } else {
            send_bancho_packet_http(out);
        }
        free(out.memory);
    }

    if(websocket && !websocket->in.empty()) {
        parse_packets(websocket->in);
        websocket->in.clear();
    }
}

void send_packet(Packet &packet) {
    if(!BanchoState::is_online()) {
        // Don't queue any packets until we're logged in
        free(packet.memory);
        packet.memory = nullptr;
        packet.size = 0;
        return;
    }

    // debugLog("Sending packet of type {:}: ", packet.id);
    // for (int i = 0; i < packet.pos; i++) {
    //     logRaw("{:02x} ", packet.memory[i]);
    // }
    // logRaw("");

    // We're not sending it immediately, instead we just add it to the pile of
    // packets to send
    outgoing.write<u16>(packet.id);
    outgoing.write<u8>(0);
    outgoing.write<u32>(packet.pos);

    // Some packets have an empty payload
    if(packet.memory != nullptr) {
        outgoing.write_bytes(packet.memory, packet.pos);
        free(packet.memory);
    }

    packet.memory = nullptr;
    packet.size = 0;
}

void cleanup_networking() {
    // no thread to kill, just cleanup any remaining state
    auth_token = "";
    free(outgoing.memory);
    outgoing = Packet();
}

}  // namespace BANCHO::Net

void BanchoState::poll_login() {
    if(BanchoState::get_online_status() != OnlineStatus::POLLING) return;

    auto challenge = Mc::Net::urlEncode(crypto::conv::encode64(BanchoState::oauth_challenge));
    auto proof = Mc::Net::urlEncode(crypto::conv::encode64(BanchoState::oauth_verifier));
    auto url = fmt::format("{}/connect/finish?challenge={}&proof={}", BanchoState::endpoint, challenge, proof);

    Mc::Net::RequestOptions options{
        .user_agent = BanchoState::user_agent,
        .timeout = 30,
        .connect_timeout = 5,
        .flags = Mc::Net::RequestOptions::FOLLOW_REDIRECTS,
    };

    networkHandler->httpRequestAsync(url, std::move(options), [](Mc::Net::Response response) {
        if(response.success) {
            if(response.response_code == 204) {
                // callbacks already run on the main thread
                BANCHO::Net::login_poll_timeout = engine->getTime() + 0.5;
            } else {
                BANCHO::Net::login_poll_timeout = -1.;  // sanity reset
                cv::mp_oauth_token.setValue(response.body);
                BanchoState::reconnect();
            }
        } else {
            BanchoState::update_online_status(OnlineStatus::LOGGED_OUT);
            auto errmsg = fmt::format("Failed to log in: {}", response.error_msg);
            ui->getNotificationOverlay()->addToast(errmsg, ERROR_TOAST);
        }
    });
}

void BanchoState::disconnect(bool shutdown) {
    cvars().resetServerCvars();

    // reset
    BanchoState::nonsubmittable_notification_clicked = false;

    // Logout
    // This is a blocking call, but we *do* want this to block when quitting the game.
    if(BanchoState::is_online() && !BANCHO::Net::auth_token.empty()) {
        Packet packet;
        packet.write<u16>(OUTP_LOGOUT);
        packet.write<u8>(0);
        packet.write<u32>(4);
        packet.write<u32>(0);

        Mc::Net::RequestOptions options{
            .post_data = std::string(reinterpret_cast<char *>(packet.memory), packet.pos),
            .user_agent = "osu!",
            .timeout = 5,
            .connect_timeout = 5,
        };

        options.headers["x-mcosu-ver"] = BanchoState::neomod_version;
        options.headers["osu-token"] = BANCHO::Net::auth_token;
        BANCHO::Net::auth_token = "";

        auto query_url = fmt::format("c.{:s}/", BanchoState::endpoint);

        // use sync request for logout on shutdown to make sure it completes.
        // on WASM, KEEPALIVE uses fetch(keepalive) instead of blocking sync XHR.
        if(shutdown) {
            options.flags |= Mc::Net::RequestOptions::KEEPALIVE;
            networkHandler->httpRequestSynchronous(query_url, std::move(options));
        } else {
            networkHandler->httpRequestAsync(query_url, std::move(options));
        }

        free(packet.memory);
    } else if(BanchoState::is_logging_in()) {
        // HACKHACK: can't cancel existing in-progress request directly
        BanchoState::async_logout_pending = true;
    }

    free(BANCHO::Net::outgoing.memory);
    BANCHO::Net::outgoing = Packet();

    BanchoState::set_uid(0);
    osu->getUserButton()->setID(0);

    BanchoState::is_oauth = false;
    BanchoState::fully_supports_neomod = false;
    BanchoState::endpoint = "";
    BanchoState::spectating = false;
    BanchoState::spectated_player_id = 0;
    BanchoState::spectators.clear();
    BanchoState::fellow_spectators.clear();
    BanchoState::server_icon_url = "";
    if(BanchoState::server_icon != nullptr) {
        resourceManager->destroyResource(BanchoState::server_icon);
        BanchoState::server_icon = nullptr;
    }
    BanchoState::update_online_status(OnlineStatus::LOGGED_OUT);
    BanchoState::score_submission_policy = ServerPolicy::NO_PREFERENCE;

    BANCHO::User::logout_all_users();
    ui->getChat()->onDisconnect();
    ui->getSongBrowser()->onFilterScoresChange("Local", SongBrowser::LOGIN_STATE_FILTER_ID);

    // Exit out of any online-only screens
    if(UIScreen *s = ui->getActiveScreen();
       (s == (UIScreen *)ui->getSpectatorScreen()) || (s == (UIScreen *)ui->getLobby()) ||
       (s == (UIScreen *)ui->getOsuDirectScreen()) || (s == (UIScreen *)ui->getRoom())) {
        ui->setScreen(ui->getMainMenu());
    }

    Downloader::abort_downloads();
}

void BanchoState::reconnect() {
    BanchoState::disconnect();
    BanchoState::async_logout_pending = false;

    // Disable autologin, in case there's an error while logging in
    // Will be reenabled after the login succeeds
    cv::mp_autologin.setValue(false);

    // XXX: Put this in cv::mp_password callback?
    if(!cv::mp_password.getString().empty()) {
        const char *password = cv::mp_password.getString().c_str();
        const auto hash{crypto::hash::md5_hex((u8 *)password, strlen(password))};
        cv::mp_password_md5.setValue(hash.string());
        cv::mp_password.setValue("");
    }

    BanchoState::endpoint = cv::mp_server.getString();
    BanchoState::username = cv::name.getString().c_str();
    if(strlen(cv::mp_password_md5.getString().c_str()) == 32) {
        BanchoState::pw_md5 = {cv::mp_password_md5.getString().c_str()};
    }

    // Admins told me they don't want any clients to connect
    static constexpr const auto server_blacklist = std::array{
        "ppy.sh"sv,  // haven't asked, but the answer is obvious
        "gatari.pw"sv,
    };

    if(std::ranges::contains(server_blacklist, BanchoState::endpoint)) {
        ui->getNotificationOverlay()->addToast(US_("This server does not allow " PACKAGE_NAME " clients."), ERROR_TOAST);
        return;
    }

    // Admins told me they don't want score submission enabled
    static constexpr const auto submit_blacklist = std::array{
        "akatsuki.gg"sv,
        "ripple.moe"sv,
    };

    if(std::ranges::contains(submit_blacklist, BanchoState::endpoint)) {
        BanchoState::score_submission_policy = ServerPolicy::NO;
    }

    BanchoState::update_online_status(OnlineStatus::LOGIN_IN_PROGRESS);

    BANCHO::Net::attempt_logging_in();
}
