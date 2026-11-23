// Copyright (c) 2018, PG, All rights reserved.
#include "RichPresence.h"

#include "Logging.h"
#include "OsuConVars.h"
#include "Bancho.h"
#include "BanchoNetworking.h"
#include "BanchoUsers.h"
#include "BeatmapInterface.h"
#include "Chat.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "DiscordInterface.h"
#include "Engine.h"
#include "Environment.h"
#include "ModSelector.h"
#include "Osu.h"
#include "RoomScreen.h"
#include "SongBrowser/SongBrowser.h"
#include "MusicTrack.h"
#include "score.h"
#include "Timing.h"
#include "UI.h"

#ifdef MCENGINE_PLATFORM_WASM
#include <emscripten/em_asm.h>
#endif

namespace RichPresence {
namespace {  // static

std::string last_status{"\nWaking up"};
Action last_action = Action::IDLE;

enum class MusicDependentCallback : u8 {
    ON_NULL,
    ON_MAINMENU,
    ON_SONGBROWSER,
    ON_PLAYSTART,
    //ON_PLAYEND, // this doesn't set discord status for some reason?
    ON_MULTI,
    ON_EDITOR,
};
MusicDependentCallback last_callback{};
MD5Hash editor_map;  // what onEditor() was last told about (by content: its record may be gone at a refresh)

DiscordActivity base_activity() {
    static const i64 session_start = Timing::getUnixTime();
    DiscordActivity activity;
    activity.start_timestamp = session_start;
    return activity;
}

std::string beatmap_desc_str(const DatabaseBeatmap* map, bool include_difficulty) {
    if(!map) return "No map selected";

    std::string playingInfo = fmt::format("{:s} - {:s}", map->getArtist(), map->getTitle());
    if(include_difficulty) playingInfo.append(fmt::format(" [{:s}]", map->getDifficultyName()));
    return playingInfo;
}

void set_activity_with_image(const DatabaseBeatmap* map, DiscordActivity& to_set, bool is_editor = false) {
#ifdef MCENGINE_PLATFORM_WASM
    if(Osu::isDiscordActivity()) {
        // clang-format off
        EM_ASM({
            if(window.neomod_set_activity) {
                window.neomod_set_activity(UTF8ToString($0), UTF8ToString($1), $2, $3, $4);
            }
        }, to_set.details.c_str(), to_set.state.c_str(), to_set.party_size, to_set.party_max, (double)to_set.start_timestamp);
        // clang-format on
    }
#endif

    const bool listening = !!map && (osu->getMusicTrack()->isPlaying() || is_editor);
    const bool playing = !!map && osu->isInPlayMode();
    const bool bg_visible = !!map && map->getSetID() > 0 && map->getSettings().draw_background &&
                            cv::rich_presence_map_backgrounds.getBool();

    to_set.large_image = PACKAGE_NAME "_icon";

    if(listening || playing) {
        const bool online = BanchoState::is_online();
        const std::string endpoint = online ? BanchoState::endpoint : "ppy.sh";
        const char* scheme = cv::use_https.getBool() ? "https://" : "http://";

        if(bg_visible) {
            const std::string server_icon_url = online ? BanchoState::server_icon_url : "";

            to_set.large_image = fmt::format("{:s}b.{:s}/thumb/{:d}l.jpg", scheme, endpoint, map->getSetID());

            if(server_icon_url.length() > 0 && cv::main_menu_use_server_logo.getBool()) {
                to_set.small_image = server_icon_url;
                to_set.small_text = endpoint;
            } else {
                to_set.small_image = PACKAGE_NAME "_icon";
            }
        }

        const i32 map_id = map->getID();
        const i32 set_id = map->getSetID();
        if((map_id > 0) || (set_id > 0)) {
            const bool use_set_id = map_id <= 0;
            const i32 real_id = use_set_id ? set_id : map_id;
            const auto b_or_s_endpoint = use_set_id ? "s" : "b";
            to_set.buttons[0] = {
                .label = "View beatmap",
                .url = fmt::format("{:s}osu.{:s}/{:s}/{:d}", scheme, endpoint, b_or_s_endpoint, real_id),
            };
        }
    }

    Discord::set_activity(to_set);
}

// what setBanchoStatus() sends, about `map`
void send_status(const char* info_text, Action action, const DatabaseBeatmap* map) {
    std::string fancy_text{fmt::format("\n{:s}", info_text)};
    if(fancy_text.length() > 1023) fancy_text.resize(1023);

    // Don't send status update if it's the same as our current status
    // (prevents situations like spamming main menu updates if song fails to loop)
    if(last_status == fancy_text && last_action == action) return;
    last_status = fancy_text;
    last_action = action;

    MD5Hash map_md5;
    i32 map_id = 0;
    if(map != nullptr) {
        map_md5 = map->getMD5();
        map_id = map->getID();
    }

    Packet packet;
    packet.id = OUTP_CHANGE_ACTION;
    packet.write<u8>((u8)action);
    packet.write_string(fancy_text);
    packet.write_hash_chars(map_md5);
    packet.write<LegacyFlags>(ui->getModSelector()->getModFlags());
    packet.write<u8>(0);  // osu!std
    packet.write<i32>(map_id);
    BANCHO::Net::send_packet(packet);
}
}  // namespace

void refreshStatus() {
    switch(last_callback) {
        using enum MusicDependentCallback;
        case ON_MAINMENU:
            onMainMenu();
            break;
        case ON_SONGBROWSER:
            onSongBrowser();
            break;
        case ON_PLAYSTART:
            onPlayStart();
            break;
        // case ON_PLAYEND:
        //     onPlayEnd(false);
        //     break;
        case ON_MULTI:
            onMultiplayerLobby();
            break;
        case ON_EDITOR:
            if(const DatabaseBeatmap* map = db->getBeatmapDifficulty(editor_map)) onEditor(*map);
            break;
        case ON_NULL:
            break;
    }
}

void setBanchoStatus(const char* info_text, Action action) {
    if(osu == nullptr) return;
    send_status(info_text, action, osu->getMapInterface()->getBeatmap());
}

void resetBanchoStatus() {
    last_status = "\nWaking up";
    last_action = Action::IDLE;
}

void updateBanchoMods() {
    MD5Hash map_md5;
    i32 map_id = 0;

    auto diff = osu->getMapInterface()->getBeatmap();
    if(diff != nullptr) {
        map_md5 = diff->getMD5();
        map_id = diff->getID();
    }

    Packet packet;
    packet.id = OUTP_CHANGE_ACTION;
    packet.write<u8>((u8)last_action);
    packet.write_string(last_status);
    packet.write_hash_chars(map_md5);
    packet.write<LegacyFlags>(ui->getModSelector()->getModFlags());
    packet.write<u8>(0);  // osu!std
    packet.write<i32>(map_id);
    BANCHO::Net::send_packet(packet);

    // Servers like akatsuki send different leaderboards based on what mods
    // you have selected. Reset leaderboard when switching mods.
    db->getOnlineLeaderboards().clear();
    ui->getSongBrowser()->onGotNewLeaderboard(map_md5);
}

void onMainMenu() {
    last_callback = MusicDependentCallback::ON_MAINMENU;

    Action action = Action::AFK;
    std::string msg = "Main Menu";

    if(ui->getChat()->isVisible() && ui->getChat()->user_list->isVisible()) {
        // use "idle" status when viewing users list, or else some servers won't update it
        action = Action::IDLE;
    }

    if(BanchoState::spectating) {
        action = Action::WATCHING;

        const auto* user = BANCHO::User::get_user_info(BanchoState::spectated_player_id, true);
        if(user->has_presence) {
            msg = fmt::format("Spectating {:s}", user->name.c_str());
        } else {
            msg = "Spectating";
        }
    }

    auto activity = base_activity();

    const auto* map = osu->getMapInterface()->getBeatmap();
    bool listening = map != nullptr && osu->getMusicTrack()->isPlaying();
    if(listening) {
        activity.details = beatmap_desc_str(map, false);
    }

    activity.state = msg;
    setBanchoStatus(msg.c_str(), action);
    set_activity_with_image(map, activity);
}

void onSongBrowser() {
    if(BanchoState::spectating) return;

    last_callback = MusicDependentCallback::ON_SONGBROWSER;
    auto activity = base_activity();

    activity.details = "Picking a map";

    if(ui->getRoomScreen()->isVisible()) {
        setBanchoStatus("Picking a map", Action::MULTIPLAYER);

        activity.state = "Multiplayer";
        activity.party_size = BanchoState::room.nb_players;
        activity.party_max = BanchoState::room.nb_open_slots;
    } else {
        setBanchoStatus("Song selection", Action::IDLE);

        activity.state = "Singleplayer";
        activity.party_size = 0;
        activity.party_max = 0;
    }

    set_activity_with_image(osu->getMapInterface()->getBeatmap(), activity);
    env->setWindowTitle(PACKAGE_NAME);
}

void onPlayStart() {
    last_callback = MusicDependentCallback::ON_PLAYSTART;
    const auto* map = osu->getMapInterface()->getBeatmap();

    static const DatabaseBeatmap* last_diff = nullptr;
    static i64 tms = 0;
    if(tms == 0 || last_diff != map) {
        last_diff = map;
        tms = Timing::getUnixTime();
    }

    auto activity = base_activity();

    activity.start_timestamp = tms;

    activity.details = beatmap_desc_str(map, true);

    if(osu->getMapInterface()->getKind() == BeatmapInterface::PlayKind::TEST) {
        setBanchoStatus(activity.details.c_str(), Action::TESTING);

        activity.state = "Testing";
        activity.party_size = 0;
        activity.party_max = 0;
    } else if(BanchoState::is_in_a_multi_room()) {
        setBanchoStatus(activity.details.c_str(), Action::MULTIPLAYER);

        activity.state = "Playing in a lobby";
        activity.party_size = BanchoState::room.nb_players;
        activity.party_max = BanchoState::room.nb_open_slots;
    } else if(BanchoState::spectating) {
        setBanchoStatus(activity.details.c_str(), Action::WATCHING);
        activity.party_size = 0;
        activity.party_max = 0;

        const auto* user = BANCHO::User::get_user_info(BanchoState::spectated_player_id, true);
        if(user->has_presence) {
            activity.state = fmt::format("Spectating {:s}", user->name.c_str());
        } else {
            activity.state = "Spectating";
        }
    } else {
        setBanchoStatus(activity.details.c_str(), Action::PLAYING);

        activity.state = "Playing Solo";
        activity.party_size = 0;
        activity.party_max = 0;
    }

    set_activity_with_image(map, activity);

    // also update window title
    env->setWindowTitle(fmt::format(PACKAGE_NAME " - {:s}", activity.details));
}

void onEditor(const DatabaseBeatmap& map) {
    last_callback = MusicDependentCallback::ON_EDITOR;
    editor_map = map.getMD5();

    auto activity = base_activity();
    activity.details = beatmap_desc_str(&map, true);
    activity.state = "Editing";

    send_status(activity.details.c_str(), Action::EDITING, &map);
    set_activity_with_image(&map, activity, /*is_editor=*/true);
}

void onPlayEnd(bool quit) {
    if(quit) return;
    if(BanchoState::spectating) return;
    // last_callback = MusicDependentCallback::ON_PLAYEND;

    // e.g.: 230pp 900x 95.50% HDHRDT 6*

    // pp
    std::string scoreInfo = fmt::format("{}pp", (int)(std::round(osu->getScore()->getPPv2())));

    // max combo
    scoreInfo.append(fmt::format(" {}x", osu->getScore()->getComboMax()));

    // accuracy
    scoreInfo.append(fmt::format(" {:.2f}%", osu->getScore()->getAccuracy() * 100.0f));

    // mods
    std::string mods = osu->getScore()->getModsStringForRichPresence();
    if(mods.length() > 0) {
        scoreInfo.append(" ");
        scoreInfo.append(mods);
    }

    // stars
    scoreInfo.append(fmt::format(" {:.2f}*", osu->getScore()->getStarsTomTotal()));

    setBanchoStatus(scoreInfo.c_str(), Action::SUBMITTING);
}

void onMultiplayerLobby() {
    last_callback = MusicDependentCallback::ON_MULTI;
    auto activity = base_activity();

    activity.state = BanchoState::endpoint;
    activity.details = BanchoState::room.name;
    activity.party_size = BanchoState::room.nb_players;
    activity.party_max = BanchoState::room.nb_open_slots;

    set_activity_with_image(osu->getMapInterface()->getBeatmap(), activity);
}

}  // namespace RichPresence
