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
#include "i18n.h"
#include "ModSelector.h"
#include "Osu.h"
#include "RoomScreen.h"
#include "SongBrowser/SongBrowser.h"
#include "Sound.h"
#include "score.h"
#include "UI.h"

#include <chrono>

namespace RichPresence {
namespace {  // static

std::string last_status{"\nWaking up"};
Action last_action = Action::IDLE;

enum class MusicDependentCallback : u8 {
    ON_NULL,
    ON_MAINMENU,
    ON_SONGBROWSER,
};
MusicDependentCallback last_callback{};

template <size_t N = 128>
std::array<char, N> crop_string_to_n(const std::string& str) {
    std::array<char, N> output{};
    if(str.length() < N) {
        strcpy(output.data(), str.c_str());
    } else {
        strncpy(output.data(), str.c_str(), N - 4);
        output[N - 4] = '.';
        output[N - 3] = '.';
        output[N - 2] = '.';
        output[N - 1] = '\0';
    }
    return output;
}

std::array<char, 128> beatmap_desc_str(const DatabaseBeatmap* map, bool include_difficulty) {
    std::array<char, 128> ret{};
    strncpy(ret.data(), _("No map selected"), ret.size() - 1);
    if(!map) return ret;

    std::string playingInfo = fmt::format("{:s} - {:s}", map->getArtist(), map->getTitle());

    if(include_difficulty) {
        std::string diffStr = fmt::format(" [{:s}]", map->getDifficultyName());
        if(playingInfo.length() + diffStr.length() < 128) {
            playingInfo.append(diffStr);
        }
    }

    ret = crop_string_to_n(playingInfo);
    return ret;
}

void set_activity_with_image(DiscordActivity& to_set) {
#ifdef MCENGINE_FEATURE_DISCORD
    const auto map = osu->getMapInterface()->getBeatmap();
    const auto music = osu->getMapInterface()->getMusic();
    const bool listening = !!map && !!music && music->isPlaying();
    const bool playing = !!map && osu->isInPlayMode();
    const bool bg_visible = !!map && map->draw_background && cv::rich_presence_map_backgrounds.getBool();

    to_set.assets.large_image = {PACKAGE_NAME "_icon"};
    to_set.assets.small_image = {};
    to_set.assets.small_text = {};

    if(bg_visible && (listening || playing)) {
        const bool online = BanchoState::is_online();
        const std::string endpoint = online ? BanchoState::endpoint : "ppy.sh";
        const std::string server_icon_url = online ? BanchoState::server_icon_url : "";

        const char* scheme = cv::use_https.getBool() ? "https://" : "http://";
        const std::string url = fmt::format("{:s}b.{:s}/thumb/{:d}l.jpg", scheme, endpoint, map->getSetID());
        strncpy(&to_set.assets.large_image[0], url.c_str(), 127);

        if(server_icon_url.length() > 0 && cv::main_menu_use_server_logo.getBool()) {
            strncpy(&to_set.assets.small_image[0], server_icon_url.c_str(), 127);
            strncpy(&to_set.assets.small_text[0], endpoint.c_str(), 127);
        } else {
            to_set.assets.small_image = {PACKAGE_NAME "_icon"};
            to_set.assets.small_text = {};
        }
    }

    DiscRPC::set_activity(to_set);
#else
    (void)to_set;
#endif
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
        case ON_NULL:
            break;
    }
}

void setBanchoStatus(const char* info_text, Action action) {
    if(osu == nullptr) return;

    MD5Hash map_md5;
    i32 map_id = 0;

    if(const auto* map = osu->getMapInterface()->getBeatmap(); map != nullptr) {
        map_md5 = map->getMD5();
        map_id = map->getID();
    }

    std::string fancy_text{fmt::format("\n{:s}", info_text)};
    if(fancy_text.length() > 1023) fancy_text.resize(1023);

    // Don't send status update if it's the same as our current status
    // (prevents situations like spamming main menu updates if song fails to loop)
    if(last_status == fancy_text && last_action == action) return;

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
    last_callback = MusicDependentCallback::ON_MAINMENU;
    bool force_not_afk =
        BanchoState::spectating || (ui->getChat()->isVisible() && ui->getChat()->user_list->isVisible());
    setBanchoStatus(_("Main Menu"), force_not_afk ? Action::IDLE : Action::AFK);

    auto activity = DiscRPC::create_base_activity();

    auto map = osu->getMapInterface()->getBeatmap();
    auto music = osu->getMapInterface()->getMusic();
    bool listening = map != nullptr && music != nullptr && music->isPlaying();
    if(listening) {
        activity.details = beatmap_desc_str(map, false);
    }

    strncpy(activity.state.data(), _("Main menu"), activity.state.size() - 1);
    set_activity_with_image(activity);
}

void onSongBrowser() {
    last_callback = MusicDependentCallback::ON_SONGBROWSER;
    auto activity = DiscRPC::create_base_activity();

    strncpy(activity.details.data(), _("Picking a map"), activity.details.size() - 1);

    if(ui->getRoom()->isVisible()) {
        setBanchoStatus(_("Picking a map"), Action::MULTIPLAYER);

        strncpy(activity.state.data(), _("Multiplayer"), activity.state.size() - 1);
        activity.party.size.current_size = BanchoState::room.nb_players;
        activity.party.size.max_size = BanchoState::room.nb_open_slots;
    } else {
        setBanchoStatus(_("Song selection"), Action::IDLE);

        strncpy(activity.state.data(), _("Singleplayer"), activity.state.size() - 1);
        activity.party.size.current_size = 0;
        activity.party.size.max_size = 0;
    }

    set_activity_with_image(activity);
    env->setWindowTitle(PACKAGE_NAME);
}

void onPlayStart() {
    const auto* map = osu->getMapInterface()->getBeatmap();

    static const DatabaseBeatmap* last_diff = nullptr;
    static int64_t tms = 0;
    if(tms == 0 || last_diff != map) {
        last_diff = map;
        tms = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                  .count();
    }

    auto activity = DiscRPC::create_base_activity();

    activity.timestamps.start = tms;
    activity.timestamps.end = 0;

    activity.details = beatmap_desc_str(map, true);

    if(BanchoState::is_in_a_multi_room()) {
        setBanchoStatus(activity.details.data(), Action::MULTIPLAYER);

        strncpy(activity.state.data(), _("Playing in a lobby"), activity.state.size() - 1);
        activity.party.size.current_size = BanchoState::room.nb_players;
        activity.party.size.max_size = BanchoState::room.nb_open_slots;
    } else if(BanchoState::spectating) {
        setBanchoStatus(activity.details.data(), Action::WATCHING);
        activity.party.size.current_size = 0;
        activity.party.size.max_size = 0;

        const auto* user = BANCHO::User::get_user_info(BanchoState::spectated_player_id, true);
        if(user->has_presence) {
            auto msg = tformat("Spectating {:s}", user->name.c_str());
            strncpy(activity.state.data(), msg.c_str(), activity.state.size() - 1);
        } else {
            strncpy(activity.state.data(), _("Spectating"), activity.state.size() - 1);
        }
    } else {
        setBanchoStatus(activity.details.data(), Action::PLAYING);

        strncpy(activity.state.data(), _("Playing Solo"), activity.state.size() - 1);
        activity.party.size.current_size = 0;
        activity.party.size.max_size = 0;
    }

    set_activity_with_image(activity);

    // also update window title
    env->setWindowTitle(fmt::format(PACKAGE_NAME " - {:s}", std::string_view{&activity.details[0]}));
}

void onPlayEnd(bool quit) {
    if(quit) return;

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
    auto activity = DiscRPC::create_base_activity();

    activity.state = crop_string_to_n(BanchoState::endpoint);
    activity.details = crop_string_to_n(BanchoState::room.name);
    activity.party.size.current_size = BanchoState::room.nb_players;
    activity.party.size.max_size = BanchoState::room.nb_open_slots;

    set_activity_with_image(activity);
}

}  // namespace RichPresence
