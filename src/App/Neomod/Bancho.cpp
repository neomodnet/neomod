// Copyright (c) 2023, kiwec, All rights reserved.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Bancho.h"
#include "BanchoApi.h"
#include "BanchoNetworking.h"
#include "BanchoProtocol.h"
#include "BanchoUsers.h"
#include "BeatmapInterface.h"
#include "Chat.h"
#include "OsuConVars.h"
#include "Environment.h"
#include "ConVarHandler.h"
#include "Engine.h"
#include "Lobby.h"
#include "crypto.h"
#include "NetworkHandler.h"
#include "NotificationOverlay.h"
#include "OptionsOverlay.h"
#include "Osu.h"
#include "RankingScreen.h"
#include "RoomScreen.h"
#include "SongBrowser/SongBrowser.h"
#include "SoundEngine.h"
#include "SpectatorScreen.h"
#include "SString.h"
#include "Timing.h"
#include "Logging.h"
#include "UserCard.h"
#include "File.h"
#include "UI.h"

#ifdef MCENGINE_PLATFORM_WINDOWS

#include "WinDebloatDefs.h"
#include <windows.h>
#include <cinttypes>

#elif defined(MCENGINE_PLATFORM_WASM)
#include <emscripten/emscripten.h>
#elif defined(MCENGINE_PLATFORM_LINUX)

#include <linux/limits.h>
#include <sys/stat.h>
#include "dynutils.h"

#endif

// defs
// some of these are atomic due to multithreaded access
std::string BanchoState::endpoint;
std::string BanchoState::username;
MD5String BanchoState::pw_md5;

bool BanchoState::is_oauth{false};
bool BanchoState::fully_supports_neomod{false};
std::array<u8, 32> BanchoState::oauth_challenge{};
std::array<u8, 32> BanchoState::oauth_verifier{};

bool BanchoState::spectating{false};
i32 BanchoState::spectated_player_id{0};
std::vector<u32> BanchoState::spectators;
std::vector<u32> BanchoState::fellow_spectators;

std::string BanchoState::server_icon_url;
Image *BanchoState::server_icon{nullptr};

ServerPolicy BanchoState::score_submission_policy{ServerPolicy::NO_PREFERENCE};

std::string BanchoState::neomod_version{""};
std::string BanchoState::cho_token{""};
std::string BanchoState::user_agent{""};
std::string BanchoState::client_hashes{""};

Room BanchoState::room;
bool BanchoState::match_started{false};
std::array<Slot, 16> BanchoState::last_scores{};

Hash::unstable_stringmap<BanchoState::Channel *> BanchoState::chat_channels;

bool BanchoState::print_new_channels{true};
std::string BanchoState::disk_uuid;

std::atomic<i32> BanchoState::user_id{0};
bool BanchoState::was_in_a_multi_room{false};

bool BanchoState::async_logout_pending{false};
OnlineStatus BanchoState::online_status{OnlineStatus::LOGGED_OUT};
bool BanchoState::nonsubmittable_notification_clicked{false};

/*###################################################################################################*/

bool BanchoState::is_in_a_multi_room() {
    const bool now_multi = room.nb_players > 0;
    if(was_in_a_multi_room != now_multi) {
        was_in_a_multi_room = now_multi;
        // temporary... hopefully
        cvars().invalidateAllProtectedCaches();
    }
    return now_multi;
}

void BanchoState::set_uid(i32 new_uid) {
    const i32 old_uid = get_uid();
    user_id.store(new_uid, std::memory_order_release);

    if(is_logging_in() || old_uid != new_uid) {
        const bool is_online = (new_uid > 0) || (new_uid < -10000);
        update_online_status(is_online ? OnlineStatus::LOGGED_IN : OnlineStatus::LOGGED_OUT);
    }
}

void BanchoState::update_online_status(OnlineStatus new_status) {
    const OnlineStatus old_status = online_status;
    online_status = new_status;

    ui->getOptionsOverlay()->update_login_button(new_status == OnlineStatus::LOGGED_IN);

    // login failed, no update layout necessary
    if(old_status == OnlineStatus::LOGIN_IN_PROGRESS && new_status != OnlineStatus::LOGGED_IN) return;

    // in progress/logged out -> logged in, or logged in -> logged out
    if(old_status != new_status && (new_status == OnlineStatus::LOGGED_OUT || new_status == OnlineStatus::LOGGED_IN)) {
        // make sure we create these directories once, now that we know the endpoint is valid
        if(new_status == OnlineStatus::LOGGED_IN) {
            std::string avatar_dir = fmt::format("{}/avatars/{}", env->getCacheDir(), BanchoState::endpoint);
            Environment::createDirectory(avatar_dir);

            std::string replays_dir = fmt::format(NEOMOD_REPLAYS_PATH "/{}", BanchoState::endpoint);
            Environment::createDirectory(replays_dir);

            std::string thumbs_dir = fmt::format("{}/thumbs/{}", env->getCacheDir(), BanchoState::endpoint);
            Environment::createDirectory(thumbs_dir);
        }

        ui->getOptionsOverlay()->scheduleLayoutUpdate();
    }

    if(async_logout_pending && new_status == OnlineStatus::LOGGED_IN) {
        async_logout_pending = false;
        BanchoState::disconnect();
    }
}

void BanchoState::initialize_neomod_server_session() {
    // Because private servers don't give a shit about neomod, and we want to
    // be able to move fast without backwards compatibility being in the way,
    // we'll just roll with a custom protocol.
    //
    // Not only that, we expect the server implementation to fully support all
    // neomod-specific features.
    //
    // This might get relaxed in the future if someone else chooses to add
    // support for neomod clients. But as of now it wouldn't make sense to
    // cater to imaginary servers.
    BanchoState::fully_supports_neomod = true;

    // Here are some defaults that the server used to send in handshake
    // packets - let's save some bandwidth while we're at it.
    cv::sv_allow_speed_override.setValue(1, true, CvarEditor::SERVER);
    cv::sv_has_irc_users.setValue(0, true, CvarEditor::SERVER);

    // clang-format off
    const auto to_unprotect = {
        "ar_override", "ar_override_lock", "ar_overridenegative",
        "cs_override", "cs_overridenegative",
        "hp_override",
        "mod_actual_flashlight",
        "mod_nightmare",
        "mod_artimewarp", "mod_artimewarp_multiplier",
        "mod_arwobble", "mod_arwobble_interval", "mod_arwobble_strength",
        "mod_fadingcursor",
        "mod_fposu", "mod_fposu_sound_panning",
        "mod_fps", "mod_fps_sound_panning",
        "mod_hd_circle_fadein_end_percent", "mod_hd_circle_fadein_start_percent",
        "mod_jigsaw1", "mod_jigsaw2", "mod_jigsaw_followcircle_radius_factor",
        "mod_mafham", "mod_mafham_ignore_hittable_dim",
        "mod_mafham_render_chunksize", "mod_mafham_render_livesize",
        "mod_millhioref",
        "mod_minimize", "mod_minimize_multiplier",
        "mod_reverse_sliders",
        "mod_shirone", "mod_shirone_combo",
        "mod_strict_tracking",
        "mod_timewarp", "mod_timewarp_multiplier",
        "mod_wobble", "mod_wobble2",
        "mod_wobble_frequency", "mod_wobble_rotation_speed", "mod_wobble_strength",
        "mod_fullalternate", "mod_singletap", "mod_no_keylock", "notelock_type"
    };
    // clang-format on

    for(auto name : to_unprotect) {
        auto cvar = cvars().getConVarByName(name);
        cvar->setServerProtected(CvarProtection::UNPROTECTED);
    }
}

void BanchoState::check_and_notify_nonsubmittable() {
    // if we go from having (all cvars submittable)->(NOT all cvars submittable),
    // clear the "clicked" flag, so it shows up again if they become non-submittable
    // again later due to an incompatible setting/mod change
    const bool currently_submittable = cvars().areAllCvarsSubmittable();
    if(currently_submittable) {
        BanchoState::nonsubmittable_notification_clicked = false;
    }

    if(!currently_submittable && !BanchoState::nonsubmittable_notification_clicked) {
        ui->getNotificationOverlay()->addToast(
            US_("Score will not submit with current mods/settings"), ERROR_TOAST,
            []() -> void { BanchoState::nonsubmittable_notification_clicked = true; });
    }
}

void BanchoState::handle_packet(Packet &packet) {
    logIfCV(debug_network, "{} ({})", packet.id, Packet::inpacket_to_string(packet.id));

    switch(packet.id) {
        case INP_USER_ID: {
            i32 new_user_id = packet.read<i32>();
            BanchoState::set_uid(new_user_id);
            BanchoState::is_oauth = !cv::mp_oauth_token.getString().empty();

            if(BANCHO::User::is_online_id(new_user_id)) {
                // Prevent getting into an invalid state where we are "logged in" but can't send any packets
                if(BanchoState::cho_token.empty()) {
                    ui->getNotificationOverlay()->addToast("Failed to log in: Server didn't send a cho-token header.",
                                                           ERROR_TOAST);
                    if constexpr(Env::cfg(OS::WASM)) {
                        ui->getNotificationOverlay()->addToast(
                            "Most likely, some CORS headers are missing (did you set Access-Control-Expose-Headers?)",
                            ERROR_TOAST);
                    }

                    BanchoState::disconnect();
                    return;
                }

                debugLog("Logged in as user #{:d}.", new_user_id);
                cv::mp_autologin.setValue(true);
                BanchoState::print_new_channels = true;

                osu->onUserCardChange(BanchoState::username);
                ui->getSongBrowser()->onFilterScoresChange(US_("Global"), SongBrowser::LOGIN_STATE_FILTER_ID);

                // If server sent a score submission policy, update options menu to hide the checkbox
                ui->getOptionsOverlay()->scheduleLayoutUpdate();
            } else {
                cv::mp_autologin.setValue(false);
                cv::mp_oauth_token.setValue("");

                debugLog("Failed to log in, server returned code {:d}.", BanchoState::get_uid());
                UString errmsg =
                    fmt::format("Failed to log in: {} (code {})\n", BanchoState::cho_token, BanchoState::get_uid());
                if(new_user_id == -1) {
                    errmsg = US_("Incorrect username/password.");
                } else if(new_user_id == -2) {
                    errmsg = US_("Client version is too old to connect to this server.");
                } else if(new_user_id == -3 || new_user_id == -4) {
                    errmsg = US_("You are banned from this server.");
                } else if(new_user_id == -5) {
                    errmsg = US_("Server had an error while trying to log you in.");
                } else if(new_user_id == -6) {
                    errmsg = US_("You need to buy supporter to connect to this server.");
                } else if(new_user_id == -7) {
                    errmsg = US_("You need to reset your password to connect to this server.");
                } else if(new_user_id == -8) {
                    if(BanchoState::is_oauth) {
                        errmsg = US_("osu! session expired, please log in again.");
                    } else {
                        errmsg = US_("Open the verification link sent to your email, then log in again.");
                    }
                } else {
                    if(BanchoState::cho_token == "user-already-logged-in") {
                        errmsg = US_("Already logged in on another client.");
                    } else if(BanchoState::cho_token == "unknown-username") {
                        errmsg = fmt::format("No account by the username '{}' exists.", BanchoState::username);
                    } else if(BanchoState::cho_token == "incorrect-credentials") {
                        errmsg = US_("Incorrect username/password.");
                    } else if(BanchoState::cho_token == "incorrect-password") {
                        errmsg = US_("Incorrect password.");
                    } else if(BanchoState::cho_token == "contact-staff") {
                        errmsg = US_("Please contact an administrator of the server.");
                    }
                }
                ui->getNotificationOverlay()->addToast(errmsg, ERROR_TOAST);
            }
            break;
        }

        case INP_RECV_MESSAGE: {
            std::string sender = packet.read_stdstring();
            std::string text = packet.read_stdstring();
            std::string recipient = packet.read_stdstring();
            i32 sender_id = packet.read<i32>();

            auto msg = ChatMessage{
                .tms = time(nullptr),
                .author_id = sender_id,
                .author_name = sender,
                .text = text,
            };
            ui->getChat()->addMessage(recipient, msg, true);

            break;
        }

        case INP_PONG: {
            // (nothing to do)
            break;
        }

        case INP_USER_STATS: {
            i32 stats_user_id = packet.read<i32>();

            bool is_irc_user = false;
            if(cv::sv_has_irc_users.getBool()) {
                // Vanilla servers send negative IDs for IRC clients
                is_irc_user = stats_user_id < 0;
                stats_user_id = abs(stats_user_id);
            }

            auto action = (Action)packet.read<u8>();

            UserInfo *user = BANCHO::User::get_user_info(stats_user_id);
            if(action != user->action) {
                // TODO @kiwec: i think client is supposed to regularly poll for friend stats
                if(user->is_friend() && cv::notify_friend_status_change.getBool() &&
                   (size_t)action < (size_t)Action::MAX) {
                    static constexpr auto actions = std::array{
                        "idle"sv,         "afk"sv,           "playing"sv,
                        "editing"sv,      "modding"sv,       "in a multiplayer lobby"sv,
                        "spectating"sv,   "vibing"sv,        "testing"sv,
                        "submitting"sv,   "pausing"sv,       "testing"sv,
                        "multiplaying"sv, "browsing maps"sv,
                    };
                    static_assert((size_t)Action::MAX == actions.size(), "missing action name");
                    std::string text{fmt::format("{} is now {}", user->name, actions[(size_t)action])};
                    auto open_dms = [uid = stats_user_id]() -> void {
                        UserInfo *user = BANCHO::User::get_user_info(uid);
                        ui->getChat()->openChannel(user->name);
                    };

                    // TODO: figure out what stable does and do that. for now just throttling to avoid endless spam
                    if(user->stats_tms + 10000 < Timing::getTicksMS() && action != Action::SUBMITTING) {
                        ui->getNotificationOverlay()->addToast(text, STATUS_TOAST, open_dms, ToastElement::TYPE::CHAT);
                    }
                }
            }

            user->irc_user = is_irc_user;
            user->stats_tms = Timing::getTicksMS();
            user->action = action;
            user->info_text = packet.read_stdstring();
            user->map_md5 = packet.read_hash_chars();
            user->mods = packet.read<LegacyFlags>();
            user->mode = (GameMode)packet.read<u8>();
            user->map_id = packet.read<i32>();
            user->ranked_score = packet.read<i64>();
            user->accuracy = packet.read<f32>();
            user->plays = packet.read<i32>();
            user->total_score = packet.read<i64>();
            user->global_rank = packet.read<i32>();
            user->pp = packet.read<u16>();

            if(stats_user_id == BanchoState::get_uid()) {
                osu->getUserButton()->updateUserStats();
            }
            if(stats_user_id == BanchoState::spectated_player_id) {
                ui->getSpectatorScreen()->userCard->updateUserStats();
            }

            ui->getChat()->updateUserList();

            break;
        }

        case INP_USER_LOGOUT: {
            i32 logged_out_id = packet.read<i32>();
            packet.read<u8>();
            if(logged_out_id == BanchoState::get_uid()) {
                debugLog("Logged out.");
                BanchoState::disconnect();
            } else {
                BANCHO::User::logout_user(logged_out_id);
            }
            break;
        }

        case INP_SPECTATOR_JOINED: {
            i32 spectator_id = packet.read<i32>();
            if(std::ranges::find(BanchoState::spectators, spectator_id) == BanchoState::spectators.end()) {
                debugLog("Spectator joined: user id {:d}", spectator_id);
                BanchoState::spectators.push_back(spectator_id);
            }

            break;
        }

        case INP_SPECTATOR_LEFT: {
            i32 spectator_id = packet.read<i32>();
            auto it = std::ranges::find(BanchoState::spectators, spectator_id);
            if(it != BanchoState::spectators.end()) {
                debugLog("Spectator left: user id {:d}", spectator_id);
                BanchoState::spectators.erase(it);
            }

            break;
        }

        case INP_SPECTATE_FRAMES: {
            i32 extra = packet.read<i32>();
            (void)extra;  // this is mania seed or something we can't use

            if(BanchoState::spectating) {
                auto *map_iface = osu->getMapInterface();
                UserInfo *info = BANCHO::User::get_user_info(BanchoState::spectated_player_id, true);

                u16 nb_frames = packet.read<u16>();
                for(u16 i = 0; i < nb_frames; i++) {
                    auto frame = packet.read<LiveReplayFrame>();

                    if(frame.mouse_x < 0 || frame.mouse_x > 512 || frame.mouse_y < 0 || frame.mouse_y > 384) {
                        debugLog("WEIRD FRAME: time {:d}, x {:f}, y {:f}", frame.time, frame.mouse_x, frame.mouse_y);
                    }

                    map_iface->spectated_replay.push_back(LegacyReplay::Frame{
                        .cur_music_pos = frame.time,
                        .milliseconds_since_last_frame = 0,  // fixed below
                        .x = frame.mouse_x,
                        .y = frame.mouse_y,
                        .key_flags = frame.key_flags,
                    });
                }

                // NOTE: Server can send frames in the wrong order. So we're correcting it here.
                std::ranges::sort(map_iface->spectated_replay,
                                  [](const LegacyReplay::Frame &a, const LegacyReplay::Frame &b) {
                                      return a.cur_music_pos < b.cur_music_pos;
                                  });
                map_iface->last_frame_ms = 0;
                for(auto &frame : map_iface->spectated_replay) {
                    frame.milliseconds_since_last_frame = frame.cur_music_pos - map_iface->last_frame_ms;
                    map_iface->last_frame_ms = frame.cur_music_pos;
                }

                auto action = (LiveReplayAction)packet.read<u8>();
                info->spec_action = action;

                if(osu->isInPlayMode()) {
                    switch(action) {
                        case LiveReplayAction::NEW_SONG: {
                            map_iface->restart(true);
                            map_iface->update();
                        } break;
                        case LiveReplayAction::SKIP: {
                            map_iface->skipEmptySection();
                        } break;
                        case LiveReplayAction::FAIL: {
                            map_iface->fail(true);
                        } break;
                        case LiveReplayAction::PAUSE: {
                            map_iface->spectate_pause = true;
                        } break;
                        case LiveReplayAction::UNPAUSE: {
                            map_iface->spectate_pause = false;
                        } break;
                        case LiveReplayAction::SONG_SELECT: {
                            info->map_id = 0;
                            info->map_md5 = MD5Hash();
                            map_iface->stop(true);
                        } break;
                        // nothing
                        case LiveReplayAction::NONE:
                        case LiveReplayAction::COMPLETION:
                        case LiveReplayAction::WATCHING_OTHER:
                        case LiveReplayAction::MAX_ACTION:
                            break;
                    }
                }

                auto score_frame = packet.read<ScoreFrame>();
                map_iface->score_frames.push_back(score_frame);

                auto sequence = packet.read<u16>();
                (void)sequence;  // don't know how to use this
            }

            break;
        }

        case INP_VERSION_UPDATE: {
            // (nothing to do)
            break;
        }

        case INP_SPECTATOR_CANT_SPECTATE: {
            i32 spectator_id = packet.read<i32>();
            debugLog("Spectator can't spectate: user id {:d}", spectator_id);
            break;
        }

        case INP_GET_ATTENTION: {
            // (nothing to do)
            break;
        }

        case INP_NOTIFICATION: {
            std::string notification = packet.read_stdstring();
            // some servers do some BS with whitespace/padding
            // remove that but keep them on separate lines if they came in that way
            std::string cleaned_notification;
            for(auto line : SString::split_newlines(notification)) {
                if(line.empty()) continue;
                SString::trim_inplace(line);
                if(line.empty()) continue;
                cleaned_notification.append(std::string{line} + '\n');
            }
            if(!cleaned_notification.empty()) {
                cleaned_notification.pop_back();
            }

            ui->getNotificationOverlay()->addToast(cleaned_notification, INFO_TOAST);
            break;
        }

        case INP_ROOM_CREATED:  // fallthrough
        case INP_ROOM_UPDATED: {
            auto room = Room(packet);
            if(ui->getLobby()->isVisible()) {
                ui->getLobby()->updateRoom(room);
            } else if(room.id == BanchoState::room.id) {
                ui->getRoom()->on_room_updated(room);
            }

            break;
        }

        case INP_ROOM_CLOSED: {
            i32 room_id = packet.read<i32>();
            if(room_id == BanchoState::room.id) {
                ui->getRoom()->ragequit();
            }
            ui->getLobby()->removeRoom(room_id);
            break;
        }

        case INP_ROOM_JOIN_SUCCESS: {
            // Sanity, in case some trolley admins do funny business
            if(BanchoState::spectating) {
                Spectating::stop();
            }
            if(osu->isInPlayMode()) {
                osu->getMapInterface()->stop(true);
            }

            auto room = Room(packet);
            ui->getRoom()->on_room_joined(room);

            break;
        }

        case INP_ROOM_JOIN_FAIL: {
            ui->getNotificationOverlay()->addToast(US_("Failed to join room."), ERROR_TOAST);
            ui->getLobby()->on_room_join_failed();
            break;
        }

        case INP_FELLOW_SPECTATOR_JOINED: {
            i32 spectator_id = packet.read<i32>();
            if(std::ranges::find(BanchoState::fellow_spectators, spectator_id) ==
               BanchoState::fellow_spectators.end()) {
                debugLog("Fellow spectator joined: user id {:d}", spectator_id);
                BanchoState::fellow_spectators.push_back(spectator_id);
            }

            break;
        }

        case INP_FELLOW_SPECTATOR_LEFT: {
            i32 spectator_id = packet.read<i32>();
            auto it = std::ranges::find(BanchoState::fellow_spectators, spectator_id);
            if(it != BanchoState::fellow_spectators.end()) {
                debugLog("Fellow spectator left: user id {:d}", spectator_id);
                BanchoState::fellow_spectators.erase(it);
            }

            break;
        }

        case INP_MATCH_STARTED: {
            auto room = Room(packet);
            ui->getRoom()->on_match_started(room);
            break;
        }

        case INP_MATCH_SCORE_UPDATED: {
            ui->getRoom()->on_match_score_updated(packet);
            break;
        }

        case INP_HOST_CHANGED: {
            // (nothing to do)
            break;
        }

        case INP_MATCH_ALL_PLAYERS_LOADED: {
            osu->getMapInterface()->all_players_loaded = true;
            ui->getChat()->updateVisibility();
            break;
        }

        case INP_MATCH_PLAYER_FAILED: {
            i32 slot_id = packet.read<i32>();
            ui->getRoom()->on_player_failed(slot_id);
            break;
        }

        case INP_MATCH_FINISHED: {
            ui->getRoom()->on_match_finished();
            break;
        }

        case INP_MATCH_SKIP: {
            osu->getMapInterface()->all_players_skipped = true;
            break;
        }

        case INP_CHANNEL_JOIN_SUCCESS: {
            std::string name = packet.read_stdstring();
            auto msg = ChatMessage{
                .tms = time(nullptr),
                .author_id = 0,
                .author_name = {},
                .text = US_("Joined channel."),
            };
            ui->getChat()->addChannel(name, true);
            ui->getChat()->addMessage(name, msg, false);
            break;
        }

        case INP_CHANNEL_INFO: {
            std::string channel_name = packet.read_stdstring();
            std::string channel_topic = packet.read_stdstring();
            i32 nb_members = packet.read<i32>();
            BanchoState::update_channel(channel_name, channel_topic, nb_members, false);
            break;
        }

        case INP_LEFT_CHANNEL: {
            std::string name = packet.read_stdstring();
            ui->getChat()->removeChannel(name);
            break;
        }

        case INP_CHANNEL_AUTO_JOIN: {
            std::string channel_name = packet.read_stdstring();
            std::string channel_topic = packet.read_stdstring();
            i32 nb_members = packet.read<i32>();
            BanchoState::update_channel(channel_name, channel_topic, nb_members, true);
            break;
        }

        case INP_PRIVILEGES: {
            packet.read<u32>();  // not using it for anything
            break;
        }

        case INP_FRIENDS_LIST: {
            BANCHO::User::friends.clear();

            u16 nb_friends = packet.read<u16>();
            for(u16 i = 0; i < nb_friends; i++) {
                i32 friend_id = packet.read<i32>();
                BANCHO::User::friends.insert(friend_id);
            }
            break;
        }

        case INP_PROTOCOL_VERSION: {
            int protocol_version = packet.read<i32>();
            if(protocol_version == 128) {
                BanchoState::initialize_neomod_server_session();
            } else if(protocol_version != 19) {
                std::string text{
                    fmt::format("This server may use an unsupported protocol version ({}).", protocol_version)};
                ui->getNotificationOverlay()->addToast(text, ERROR_TOAST);
            }
            break;
        }

        case INP_MAIN_MENU_ICON: {
            std::string icon = packet.read_stdstring();
            auto urls = SString::split(icon, '|');
            if(urls.size() == 2 && ((urls[0].starts_with("http://")) || urls[0].starts_with("https://"))) {
                BanchoState::server_icon_url = urls[0];
            }
            break;
        }

        case INP_MATCH_PLAYER_SKIPPED: {
            i32 user_id = packet.read<i32>();
            ui->getRoom()->on_player_skip(user_id);
            break;
        }

        case INP_USER_PRESENCE: {
            i32 presence_user_id = packet.read<i32>();

            bool is_irc_user = false;
            if(cv::sv_has_irc_users.getBool()) {
                // Vanilla servers send negative IDs for IRC clients
                is_irc_user = presence_user_id < 0;
                presence_user_id = abs(presence_user_id);
            }

            UserInfo *user = BANCHO::User::get_user_info(presence_user_id);
            user->irc_user = is_irc_user;
            user->has_presence = true;
            user->name = packet.read_stdstring();
            user->utc_offset = packet.read<u8>();
            user->country = packet.read<u8>();
            user->privileges = packet.read<u8>();
            user->longitude = packet.read<f32>();
            user->latitude = packet.read<f32>();
            user->global_rank = packet.read<i32>();

            BANCHO::User::login_user(presence_user_id);

            // Server can decide what username we use
            if(presence_user_id == BanchoState::get_uid()) {
                BanchoState::username = user->name;
                osu->onUserCardChange(user->name);
            }

            ui->getChat()->updateUserList();
            break;
        }

        case INP_RESTART: {
            // XXX: wait 'ms' milliseconds before reconnecting
            i32 ms = packet.read<i32>();
            (void)ms;

            // Some servers send "restart" packets when password is incorrect
            // So, don't retry unless actually logged in
            if(BanchoState::is_online()) {
                BanchoState::reconnect();
            }
            break;
        }

        case INP_ROOM_INVITE: {
            break;
        }

        case INP_CHANNEL_INFO_END: {
            BanchoState::print_new_channels = false;
            break;
        }

        case INP_ROOM_PASSWORD_CHANGED: {
            std::string new_password = packet.read_stdstring();
            debugLog("Room changed password to {:s}", new_password);
            BanchoState::room.password = new_password;
            break;
        }

        case INP_SILENCE_END: {
            i32 delta = packet.read<i32>();
            debugLog("Silence ends in {:d} seconds.", delta);
            // XXX: Prevent user from sending messages while silenced
            break;
        }

        case INP_USER_SILENCED: {
            i32 user_id = packet.read<i32>();
            debugLog("User #{:d} silenced.", user_id);
            break;
        }

        case INP_USER_PRESENCE_SINGLE: {
            i32 user_id = packet.read<i32>();
            BANCHO::User::login_user(user_id);
            break;
        }

        case INP_USER_PRESENCE_BUNDLE: {
            u16 nb_users = packet.read<u16>();
            for(u16 i = 0; i < nb_users; i++) {
                i32 user_id = packet.read<i32>();
                BANCHO::User::login_user(user_id);
            }
            break;
        }

        case INP_USER_DM_BLOCKED: {
            packet.read_stdstring();
            packet.read_stdstring();
            std::string blocked = packet.read_stdstring();
            packet.read<u32>();
            debugLog("Blocked {:s}.", blocked);
            break;
        }

        case INP_TARGET_IS_SILENCED: {
            packet.read_stdstring();
            packet.read_stdstring();
            std::string blocked = packet.read_stdstring();
            packet.read<u32>();
            debugLog("Silenced {:s}.", blocked);
            break;
        }

        case INP_VERSION_UPDATE_FORCED: {
            BanchoState::disconnect();
            ui->getNotificationOverlay()->addToast(US_("This server requires a newer client version."), ERROR_TOAST);
            break;
        }

        case INP_SWITCH_SERVER: {
            break;
        }

        case INP_ACCOUNT_RESTRICTED: {
            ui->getNotificationOverlay()->addToast(US_("Account restricted."), ERROR_TOAST);
            BanchoState::disconnect();
            break;
        }

        case INP_MATCH_ABORT: {
            ui->getRoom()->on_match_aborted();
            break;
        }

            // neomod-specific below

        case INP_PROTECT_VARIABLES: {
            u16 nb_variables = packet.read<u16>();
            for(u16 i = 0; i < nb_variables; i++) {
                auto name = packet.read_stdstring();
                auto cvar = cvars().getConVarByName(name, false);
                if(cvar) {
                    cvar->setServerProtected(CvarProtection::PROTECTED);
                } else {
                    debugLog("Server wanted to protect cvar '{}', but it doesn't exist!", name);
                }
            }

            break;
        }

        case INP_UNPROTECT_VARIABLES: {
            u16 nb_variables = packet.read<u16>();
            for(u16 i = 0; i < nb_variables; i++) {
                auto name = packet.read_stdstring();
                auto cvar = cvars().getConVarByName(name, false);
                if(cvar) {
                    cvar->setServerProtected(CvarProtection::UNPROTECTED);
                } else {
                    debugLog("Server wanted to unprotect cvar '{}', but it doesn't exist!", name);
                }
            }

            break;
        }

        case INP_FORCE_VALUES: {
            u16 nb_variables = packet.read<u16>();
            for(u16 i = 0; i < nb_variables; i++) {
                auto name = packet.read_stdstring();
                auto val = packet.read_stdstring();
                auto cvar = cvars().getConVarByName(name, false);
                if(cvar) {
                    cvar->setValue(val, true, CvarEditor::SERVER);
                } else {
                    debugLog("Server wanted to set cvar '{}' to '{}', but it doesn't exist!", name, val);
                }
            }

            break;
        }

        // this should at least be in ConVarHandler...
        case INP_RESET_VALUES: {
            u16 nb_variables = packet.read<u16>();
            for(u16 i = 0; i < nb_variables; i++) {
                auto name = packet.read_stdstring();
                if(!cvars().removeServerValue(name)) {
                    debugLog("Server wanted to reset cvar '{}', but it doesn't exist!", name);
                }
            }

            break;
        }

        case INP_REQUEST_MAP: {
            MD5String md5 = packet.read_hash_chars();

            auto map = db->getBeatmapDifficulty(md5);
            if(!map) {
                // Incredibly rare, but this can happen if you enter song browser
                // on a difficulty the server doesn't have, then instantly refresh.
                debugLog("Server requested difficulty {} but we don't have it!", md5);
                break;
            }

            // craft submission url now, file read may complete after auth params changed
            std::string url = fmt::format("osu.{}/web/" PACKAGE_NAME "-submit-map.php?hash={}", BanchoState::endpoint, md5);
            BANCHO::Api::append_auth_params(url);

            std::string file_path{map->getFilePath()};

            DatabaseBeatmap::MapFileReadDoneCallback callback = [url, md5,
                                                                 file_path](std::vector<u8> osu_file) -> void {
                if(!networkHandler) return;  // quit if we got called while shutting down

                if(osu_file.empty()) {
                    debugLog("Failed to get map file data for md5: {} path: {}", md5, file_path);
                    return;
                }
                const MD5String md5_check = crypto::hash::md5_hex((u8 *)osu_file.data(), osu_file.size());
                if(md5 != md5_check) {
                    debugLog("After loading map {}, we got different md5 {}!", md5, md5_check);
                    return;
                }

                // Submit map
                Mc::Net::RequestOptions options{
                    .user_agent = "osu!",
                    .mime_parts{Mc::Net::RequestOptions::MimePart{
                        .filename = fmt::format("{}.osu", md5),
                        .name = "osu_file",
                        .data = std::move(osu_file),
                    }},
                    .timeout = 60,
                    .connect_timeout = 5,
                };
                networkHandler->httpRequestAsync(url, std::move(options));
            };

            // run async callback
            if(!map->getMapFileAsync(std::move(callback))) {
                debugLog("Immediately failed to get map file data for md5: {} path: {}", md5, file_path);
            }

            break;
        }

        default: {
            debugLog("Unknown packet ID {:d} ({:d} bytes)!", packet.id, packet.size);
            break;
        }
    }
}

std::string BanchoState::build_login_packet() {
    // Request format:
    // username\npasswd_md5\nosu_version|utc_offset|display_city|client_hashes|pm_private\n
    std::string req;

    if(cv::mp_oauth_token.getString().empty()) {
        req.append(BanchoState::username);
        req.append("\n");
        req.append(BanchoState::pw_md5.string());
        req.append("\n");
    } else {
        req.append("$oauth");
        req.append("\n");
        req.append(cv::mp_oauth_token.getString());
        req.append("\n");
    }

    // OSU_VERSION is something like "b20200201.2"
    req.append(OSU_VERSION "|");

    // UTC offset
    time_t now = time(nullptr);
    struct tm tmbuf;
    auto gmt = gmtime_x(&now, &tmbuf);
    auto local_time = localtime_x(&now, &tmbuf);
    i32 utc_offset = difftime(mktime(local_time), mktime(gmt)) / 3600;
    if(utc_offset < 0) {
        req.append("-");
        utc_offset *= -1;
    }
    req.push_back('0' + utc_offset);
    req.append("|");

    // Don't dox the user's city
    req.append("0|");

    const char *osu_path = Environment::getPathToSelf().c_str();
    MD5String osu_path_md5 = crypto::hash::md5_hex((u8 *)osu_path, strlen(osu_path));

    // XXX: Should get MAC addresses from network adapters
    // NOTE: Not sure how the MD5 is computed - does it include final "." ?
    const char *adapters = "runningunderwine";
    MD5String adapters_md5 = crypto::hash::md5_hex((u8 *)adapters, strlen(adapters));

    // XXX: Should remove '|' from the disk UUID just to be safe
    MD5String disk_md5 =
        crypto::hash::md5_hex((u8 *)BanchoState::get_disk_uuid().c_str(), BanchoState::get_disk_uuid().length());

    // XXX: Not implemented, I'm lazy so just reusing disk signature
    MD5String install_md5 =
        crypto::hash::md5_hex((u8 *)BanchoState::get_install_id().c_str(), BanchoState::get_install_id().length());

    BanchoState::client_hashes =
        fmt::format("{:s}:{:s}:{:s}:{:s}:{:s}:", osu_path_md5, adapters, adapters_md5, install_md5, disk_md5);

    req.append(BanchoState::client_hashes.c_str());
    req.append("|");

    // Allow PMs from strangers
    req.append("0\n");

    return req;
}

const std::string &BanchoState::get_username() {
    if(BanchoState::is_online()) {
        return BanchoState::username;
    } else {
        return cv::name.getString();
    }
}

bool BanchoState::can_submit_scores() {
    if(!BanchoState::is_online()) {
        return false;
    } else if(BanchoState::score_submission_policy == ServerPolicy::NO_PREFERENCE) {
        return cv::submit_scores.getBool();
    } else {
        return BanchoState::score_submission_policy == ServerPolicy::YES;
    }
}

void BanchoState::update_channel(const std::string &name, const std::string &topic, i32 nb_members, bool join) {
    Channel *chan{nullptr};
    auto it = BanchoState::chat_channels.find(name);
    if(it == BanchoState::chat_channels.end()) {
        chan = new Channel();
        chan->name = name;
        BanchoState::chat_channels[name] = chan;

        if(BanchoState::print_new_channels) {
            auto msg = ChatMessage{
                .tms = time(nullptr),
                .author_id = 0,
                .author_name = {},
                .text = fmt::format("{:s}: {:s}", name, topic),
            };
            ui->getChat()->addMessage(BanchoState::is_oauth ? "#" PACKAGE_NAME : "#osu", msg, false);
        }
    } else {
        chan = it->second;
    }

    if(join) {
        ui->getChat()->join(name);
    }

    if(chan) {
        chan->topic = topic;
        chan->nb_members = nb_members;
    } else {
        debugLog("WARNING: no channel found??");
    }
}

const std::string &BanchoState::get_disk_uuid() {
    static bool once = false;
    if(!once) {
        once = true;
        if constexpr(Env::cfg(OS::WINDOWS)) {
            BanchoState::disk_uuid = get_disk_uuid_win32();
        } else if constexpr(Env::cfg(OS::LINUX)) {
            BanchoState::disk_uuid = get_disk_uuid_blkid();
        } else if constexpr(Env::cfg(OS::WASM)) {
            BanchoState::disk_uuid = get_disk_uuid_wasm();
        } else {
            BanchoState::disk_uuid = "error getting disk uuid (unsupported platform)";
        }
    }
    return BanchoState::disk_uuid;
}

std::string BanchoState::get_disk_uuid_blkid() {
    std::string retuuid{"error getting disk UUID"};
#ifdef MCENGINE_PLATFORM_LINUX
    using blkid_cache = struct blkid_struct_cache *;

    using blkid_devno_to_devname_t = char *(unsigned long);
    using blkid_get_cache_t = int(blkid_struct_cache **, const char *);
    using blkid_put_cache_t = void(blkid_struct_cache *);
    using blkid_get_tag_value_t = char *(blkid_struct_cache *, const char *, const char *);

    using namespace dynutils;

    // we are only called once, only need libblkid temporarily
    lib_obj *blkid_lib = load_lib("libblkid.so.1");
    if(!blkid_lib) {
        debugLog("error loading blkid for obtaining disk UUID: {}", get_error());
        return retuuid;
    }

    auto pblkid_devno_to_devname = load_func<blkid_devno_to_devname_t>(blkid_lib, "blkid_devno_to_devname");
    auto pblkid_get_cache = load_func<blkid_get_cache_t>(blkid_lib, "blkid_get_cache");
    auto pblkid_put_cache = load_func<blkid_put_cache_t>(blkid_lib, "blkid_put_cache");
    auto pblkid_get_tag_value = load_func<blkid_get_tag_value_t>(blkid_lib, "blkid_get_tag_value");

    if(!(pblkid_devno_to_devname && pblkid_get_cache && pblkid_put_cache && pblkid_get_tag_value)) {
        debugLog("error loading blkid functions for obtaining disk UUID: {}", get_error());
        unload_lib(blkid_lib);
        return retuuid;
    }

    const std::string &exe_path = Environment::getPathToSelf();

    // get the device number of the device the current exe is running from
    struct stat st{};
    if(stat(exe_path.c_str(), &st) != 0) {
        unload_lib(blkid_lib);
        return retuuid;
    }

    char *devname = pblkid_devno_to_devname(st.st_dev);
    if(!devname) {
        unload_lib(blkid_lib);
        return retuuid;
    }

    // get the UUID of that device
    blkid_cache cache = nullptr;
    char *uuid = nullptr;

    if(pblkid_get_cache(&cache, nullptr) == 0) {
        uuid = pblkid_get_tag_value(cache, "UUID", devname);
        pblkid_put_cache(cache);
    }

    if(uuid) {
        retuuid = uuid;
        free(uuid);
    }

    free(devname);
    unload_lib(blkid_lib);
#endif
    return retuuid;
}

std::string BanchoState::get_disk_uuid_win32() {
    std::string retuuid{"error getting disk UUID"};
#ifdef MCENGINE_PLATFORM_WINDOWS

    // get the path to the executable
    const std::string &exe_path = Environment::getPathToSelf();
    if(exe_path.empty()) {
        return retuuid;
    }

    int w_exe_len = MultiByteToWideChar(CP_UTF8, 0, exe_path.c_str(), -1, NULL, 0);
    if(w_exe_len == 0) {
        return retuuid;
    }

    std::vector<wchar_t> w_exe_path(w_exe_len);
    if(MultiByteToWideChar(CP_UTF8, 0, exe_path.c_str(), -1, w_exe_path.data(), w_exe_len) == 0) {
        return retuuid;
    }

    // get the volume path for the executable
    std::array<wchar_t, MAX_PATH> volume_path{};
    if(!GetVolumePathNameW(w_exe_path.data(), volume_path.data(), MAX_PATH)) {
        return retuuid;
    }

    // get volume GUID path
    std::array<wchar_t, MAX_PATH> volume_name{};
    if(GetVolumeNameForVolumeMountPointW(volume_path.data(), volume_name.data(), MAX_PATH)) {
        int utf8_size = WideCharToMultiByte(CP_UTF8, 0, volume_name.data(), -1, NULL, 0, NULL, NULL);
        if(utf8_size > 0) {
            std::vector<char> utf8_buffer(utf8_size);
            if(WideCharToMultiByte(CP_UTF8, 0, volume_name.data(), -1, utf8_buffer.data(), utf8_size, NULL, NULL) > 0) {
                std::string volume_guid(utf8_buffer.data());

                // get the GUID from the path (i.e. \\?\Volume{GUID}\)
                size_t start = volume_guid.find('{');
                size_t end = volume_guid.find('}');
                if(start != std::string::npos && end != std::string::npos && end > start) {
                    // return just the GUID part without braces
                    retuuid = volume_guid.substr(start + 1, end - start - 1);
                } else {
                    // use the entire volume GUID path as a fallback
                    if(volume_guid.length() > 12) {
                        retuuid = volume_guid;
                    }
                }
            }
        }
    } else {  // the above might fail under Wine, this should work well enough as a fallback
        std::array<wchar_t, 4> drive_root{};  // "C:\" + null
        if(volume_path[0] != L'\0' && volume_path[1] == L':') {
            drive_root[0] = volume_path[0];
            drive_root[1] = L':';
            drive_root[2] = L'\\';
            drive_root[3] = L'\0';

            u32 volume_serial = 0;
            if(GetVolumeInformationW(drive_root.data(), NULL, 0, (DWORD *)(&volume_serial), NULL, NULL, NULL, 0)) {
                // format volume serial as hex string
                std::array<char, 16> serial_buffer{};
                snprintf(serial_buffer.data(), serial_buffer.size(), "%08x", volume_serial);
                retuuid = std::string{serial_buffer.data(), static_cast<int>(serial_buffer.size())};
            }
        }
    }

#endif
    return retuuid;
}

std::string BanchoState::get_disk_uuid_wasm() {
#ifdef MCENGINE_PLATFORM_WASM
    FILE *f = File::fopen_c(NEOMOD_DATA_DIR "client_id", "r");
    if(f) {
        std::array<char, 64> buf{};
        fgets(buf.data(), buf.size(), f);
        fclose(f);
        if(buf[0]) return std::string{std::string_view{buf.data()}};
    }

    const char *uuid = emscripten_run_script_string("crypto.randomUUID()");
    f = File::fopen_c(NEOMOD_DATA_DIR "client_id", "w");
    if(f) {
        fputs(uuid, f);
        fclose(f);
    }
    return uuid;
#else
    return "error getting disk uuid (unsupported platform)";
#endif
}
