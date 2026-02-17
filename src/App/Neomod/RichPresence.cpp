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
#include "Sound.h"
#include "score.h"
#include "UI.h"

#include <chrono>

#define NEOMOD_STATUS_PFX "[" PACKAGE_NAME "]"

namespace RichPresence {
namespace {  // static

std::string last_status{NEOMOD_STATUS_PFX "\nWaking up"};
Action last_action = Action::IDLE;

void crop_to(const std::string& str, char* output, int max_len) {
    if(str.length() < max_len) {
        strcpy(output, str.c_str());
    } else {
        strncpy(output, str.c_str(), max_len - 4);
        output[max_len - 4] = '.';
        output[max_len - 3] = '.';
        output[max_len - 2] = '.';
        output[max_len - 1] = '\0';
    }
}

// output is assumed to be a char[128] string
void mapstr(DatabaseBeatmap* map, char* output, bool /*include_difficulty*/) {
    if(map == nullptr) {
        strcpy(output, "No map selected");
        return;
    }

    std::string playingInfo = fmt::format("{} - {}", map->getArtist(), map->getTitle());

    std::string diffStr = fmt::format(" [{}]", map->getDifficultyName());
    if(playingInfo.length() + diffStr.length() < 128) {
        playingInfo.append(diffStr);
    }

    crop_to(playingInfo.c_str(), output, 128);
}

void set_activity_with_image(struct DiscordActivity* to_set) {
#ifdef MCENGINE_FEATURE_DISCORD
    const auto map = osu->getMapInterface()->getBeatmap();
    const auto music = osu->getMapInterface()->getMusic();
    const bool listening = !!map && !!music && music->isPlaying();
    const bool playing = !!map && osu->isInPlayMode();
    const bool bg_visible = !!map && map->draw_background && cv::rich_presence_map_backgrounds.getBool();

    strcpy(&to_set->assets.large_image[0], PACKAGE_NAME "_icon");
    to_set->assets.small_image[0] = '\0';
    to_set->assets.small_text[0] = '\0';

    std::string endpoint{"ppy.sh"};
    std::string server_icon_url{""};
    if(BanchoState::is_online()) {
        endpoint = BanchoState::endpoint;
        server_icon_url = BanchoState::server_icon_url;
    }

    if(bg_visible && (listening || playing)) {
        auto url = fmt::format("b.{}/thumb/{}l.jpg", endpoint, map->getSetID());
        strncpy(&to_set->assets.large_image[0], url.c_str(), 127);

        if(server_icon_url.length() > 0 && cv::main_menu_use_server_logo.getBool()) {
            strncpy(&to_set->assets.small_image[0], server_icon_url.c_str(), 127);
            strncpy(&to_set->assets.small_text[0], endpoint.c_str(), 127);
        } else {
            strcpy(&to_set->assets.small_image[0], PACKAGE_NAME "_icon");
            to_set->assets.small_text[0] = '\0';
        }
    }

    DiscRPC::set_activity(to_set);
#else
    (void)to_set;
#endif
}
}  // namespace

void setBanchoStatus(const char* info_text, Action action) {
    if(osu == nullptr) return;

    MD5Hash map_md5;
    i32 map_id = 0;

    auto map = osu->getMapInterface()->getBeatmap();
    if(map != nullptr) {
        map_md5 = map->getMD5();
        map_id = map->getID();
    }

    char fancy_text[1024] = {0};
    snprintf(fancy_text, 1023, NEOMOD_STATUS_PFX "\n%s", info_text);

    last_status = fancy_text;
    last_action = action;

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
    db->getOnlineScores().clear();
    ui->getSongBrowser()->onGotNewLeaderboard(map_md5);
}

void onMainMenu() {
    bool force_not_afk =
        BanchoState::spectating || (ui->getChat()->isVisible() && ui->getChat()->user_list->isVisible());
    setBanchoStatus("Main Menu", force_not_afk ? Action::IDLE : Action::AFK);

    // NOTE: As much as I would like to show "Listening to", the Discord SDK ignores the activity 'type'
    struct DiscordActivity activity{};

    activity.type = DiscordActivityType_Listening;

    auto map = osu->getMapInterface()->getBeatmap();
    auto music = osu->getMapInterface()->getMusic();
    bool listening = map != nullptr && music != nullptr && music->isPlaying();
    if(listening) {
        mapstr(map, activity.details, false);
    }

    strcpy(activity.state, "Main menu");
    set_activity_with_image(&activity);
}

void onSongBrowser() {
    struct DiscordActivity activity{};

    activity.type = DiscordActivityType_Playing;
    strcpy(activity.details, "Picking a map");

    if(ui->getRoom()->isVisible()) {
        setBanchoStatus("Picking a map", Action::MULTIPLAYER);

        strcpy(activity.state, "Multiplayer");
        activity.party.size.current_size = BanchoState::room.nb_players;
        activity.party.size.max_size = BanchoState::room.nb_open_slots;
    } else {
        setBanchoStatus("Song selection", Action::IDLE);

        strcpy(activity.state, "Singleplayer");
        activity.party.size.current_size = 0;
        activity.party.size.max_size = 0;
    }

    set_activity_with_image(&activity);
    env->setWindowTitle(PACKAGE_NAME);
}

void onPlayStart() {
    auto map = osu->getMapInterface()->getBeatmap();

    static DatabaseBeatmap* last_diff = nullptr;
    static int64_t tms = 0;
    if(tms == 0 || last_diff != map) {
        last_diff = map;
        tms = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                  .count();
    }

    struct DiscordActivity activity{};

    activity.type = DiscordActivityType_Playing;
    activity.timestamps.start = tms;
    activity.timestamps.end = 0;

    mapstr(map, activity.details, true);

    if(BanchoState::is_in_a_multi_room()) {
        setBanchoStatus(activity.details, Action::MULTIPLAYER);

        strcpy(activity.state, "Playing in a lobby");
        activity.party.size.current_size = BanchoState::room.nb_players;
        activity.party.size.max_size = BanchoState::room.nb_open_slots;
    } else if(BanchoState::spectating) {
        setBanchoStatus(activity.details, Action::WATCHING);
        activity.party.size.current_size = 0;
        activity.party.size.max_size = 0;

        const auto* user = BANCHO::User::get_user_info(BanchoState::spectated_player_id, true);
        if(user->has_presence) {
            snprintf(activity.state, 128, "Spectating %s", user->name.c_str());
        } else {
            strcpy(activity.state, "Spectating");
        }
    } else {
        setBanchoStatus(activity.details, Action::PLAYING);

        strcpy(activity.state, "Playing Solo");
        activity.party.size.current_size = 0;
        activity.party.size.max_size = 0;
    }

    // also update window title
    auto windowTitle = fmt::format(PACKAGE_NAME " - {}", activity.details);
    env->setWindowTitle(windowTitle);

    set_activity_with_image(&activity);
}

void onPlayEnd(bool quit) {
    if(quit) return;

    // e.g.: 230pp 900x 95.50% HDHRDT 6*

    // pp
    UString scoreInfo = fmt::format("{}pp", (int)(std::round(osu->getScore()->getPPv2())));

    // max combo
    scoreInfo.append(fmt::format(" {}x", osu->getScore()->getComboMax()));

    // accuracy
    scoreInfo.append(fmt::format(" {:.2f}%", osu->getScore()->getAccuracy() * 100.0f));

    // mods
    UString mods = osu->getScore()->getModsStringForRichPresence();
    if(mods.length() > 0) {
        scoreInfo.append(" ");
        scoreInfo.append(mods);
    }

    // stars
    scoreInfo.append(fmt::format(" {:.2f}*", osu->getScore()->getStarsTomTotal()));

    setBanchoStatus(scoreInfo.toUtf8(), Action::SUBMITTING);
}

void onMultiplayerLobby() {
    struct DiscordActivity activity{};

    activity.type = DiscordActivityType_Playing;

    crop_to(BanchoState::endpoint.c_str(), activity.state, 128);
    crop_to(BanchoState::room.name.c_str(), activity.details, 128);
    activity.party.size.current_size = BanchoState::room.nb_players;
    activity.party.size.max_size = BanchoState::room.nb_open_slots;

    set_activity_with_image(&activity);
}

}  // namespace RichPresence
