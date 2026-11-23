// Copyright (c) 2024, kiwec, All rights reserved.
#include "SettingsImporter.h"

#include "ACFParser.h"
#include "Console.h"
#include "Engine.h"
#include "File.h"
#include "KeyBindings.h"
#include "Database.h"
#include "OptionsOverlay.h"
#include "Osu.h"
#include "OsuConVars.h"
#include "OsuKeyBinds.h"
#include "Environment.h"
#include "Parsing.h"
#include "Logging.h"
#include "SString.h"

#include <string_view>
#include <utility>

#ifdef MCENGINE_PLATFORM_WINDOWS
#include "WinDebloatDefs.h"

#include <windows.h>
#include <winreg.h>

#include "UniString.h"

#endif
namespace SettingsImporter {
namespace {  // static namespace
void try_set_key(const std::string& key, OsuKeyBinds::Bind& bind) {
    static constexpr std::pair<std::string_view, SCANCODE> keys[] = {
        {"None", 0},
        {"A", KEY_A},
        {"B", KEY_B},
        {"C", KEY_C},
        {"D", KEY_D},
        {"E", KEY_E},
        {"F", KEY_F},
        {"G", KEY_G},
        {"H", KEY_H},
        {"I", KEY_I},
        {"J", KEY_J},
        {"K", KEY_K},
        {"L", KEY_L},
        {"M", KEY_M},
        {"N", KEY_N},
        {"O", KEY_O},
        {"P", KEY_P},
        {"Q", KEY_Q},
        {"R", KEY_R},
        {"S", KEY_S},
        {"T", KEY_T},
        {"U", KEY_U},
        {"V", KEY_V},
        {"W", KEY_W},
        {"X", KEY_X},
        {"Y", KEY_Y},
        {"Z", KEY_Z},
        {"0", KEY_0},
        {"1", KEY_1},
        {"2", KEY_2},
        {"3", KEY_3},
        {"4", KEY_4},
        {"5", KEY_5},
        {"6", KEY_6},
        {"7", KEY_7},
        {"8", KEY_8},
        {"9", KEY_9},
        {"F1", KEY_F1},
        {"F2", KEY_F2},
        {"F3", KEY_F3},
        {"F4", KEY_F4},
        {"F5", KEY_F5},
        {"F6", KEY_F6},
        {"F7", KEY_F7},
        {"F8", KEY_F8},
        {"F9", KEY_F9},
        {"F10", KEY_F10},
        {"F11", KEY_F11},
        {"F12", KEY_F12},
        {"Left", KEY_LEFT},
        {"Right", KEY_RIGHT},
        {"Up", KEY_UP},
        {"Down", KEY_DOWN},
        {"Tab", KEY_TAB},
        {"Return", KEY_ENTER},
        {"Enter", KEY_ENTER},
        {"Shift", KEY_LSHIFT},
        {"Control", KEY_LCONTROL},
        {"LeftAlt", KEY_LALT},
        {"RightAlt", KEY_RALT},
        {"Escape", KEY_ESCAPE},
        {"Space", KEY_SPACE},
        {"Back", KEY_BACKSPACE},
        {"End", KEY_END},
        {"Insert", KEY_INSERT},
        {"Delete", KEY_DELETE},
        {"Help", KEY_HELP},
        {"Home", KEY_HOME},
        {"PageUp", KEY_PAGEUP},
        {"PageDown", KEY_PAGEDOWN},
    };
    for(const auto& [name, code] : keys) {
        if(key == name) {
            bind.set(code);
            return;
        }
    }
    debugLog("No key code found for '{}'!", key);
}

void update_osu_folder_from_registry() {
#ifdef _WIN32
    i32 err;
    HKEY key;

    auto key_path = L"Software\\Classes\\osustable.File.osk\\Shell\\Open\\Command";
    err = RegOpenKeyExW(HKEY_CURRENT_USER, key_path, 0, KEY_READ, &key);
    if(err != ERROR_SUCCESS) {
        // Older registry key, in case the user isn't on latest osu!stable version
        // See https://osu.ppy.sh/home/changelog/cuttingedge/20250111
        err = RegOpenKeyExW(HKEY_CLASSES_ROOT, L"osu\\shell\\open\\command", 0, KEY_READ, &key);
    }
    if(err != ERROR_SUCCESS) {
        debugLog("osu!stable not found in registry!");
        return;
    }

    DWORD dwType = REG_SZ;
    WCHAR szPath[MAX_PATH];
    DWORD dwSize = sizeof(szPath);
    err = RegQueryValueExW(key, NULL, NULL, &dwType, (LPBYTE)szPath, &dwSize);
    RegCloseKey(key);
    if(err != ERROR_SUCCESS) {
        debugLog("Failed to get path of osu:// protocol handler.");
        return;
    }

    // The path is in format: "C:\Path\To\osu!.exe" "%1"
    WCHAR* endQuote = wcschr(szPath + 1, L'"');
    if(endQuote) {
        *endQuote = L'\0';
    }
    WCHAR* path = szPath + 1;
    WCHAR* lastBackslash = wcsrchr(path, L'\\');
    if(lastBackslash) {
        *lastBackslash = L'\0';
        std::string new_osu_folder{UniString::to_utf8(path)};
        debugLog("Found osu! folder from registry: {:s}", new_osu_folder);
        cv::osu_folder.setValue(new_osu_folder);
        return;
    }
#endif
}

std::string get_steam_path() {
#ifdef _WIN32
    i32 err;
    HKEY key;

    constexpr const wchar_t* key_path{L"SOFTWARE\\WOW6432Node\\Valve\\Steam"};
    err = RegOpenKeyExW(HKEY_LOCAL_MACHINE, key_path, 0, KEY_READ, &key);
    if(err == ERROR_SUCCESS) {
        DWORD dwType = REG_SZ;
        WCHAR szPath[MAX_PATH];
        DWORD dwSize = sizeof(szPath);
        err = RegQueryValueExW(key, L"InstallPath", NULL, &dwType, (LPBYTE)szPath, &dwSize);
        RegCloseKey(key);
        if(err == ERROR_SUCCESS) {
            return std::string(UniString::to_utf8(szPath));
        }
    }

    debugLog("Failed to get Steam path from registry!");
    return "";
#else
    // Linux: assume ~/.steam/root
    // McOsu isn't on Steam for other platforms, but just return this anyways
    return Environment::getEnvVariable("HOME") + "/.steam/root";
#endif
}

std::string get_mcosu_path() {
    auto steam_path = get_steam_path();
    if(steam_path.empty()) return "";
    namespace ACF = Parsing::ACF;

    // Get all steamapps folders
    std::vector<std::string> steamapps_paths;

    File vdfFile(steam_path + "/steamapps/libraryfolders.vdf");
    ACF::Section vdf = ACF::parse(vdfFile.readToString());

    auto it = vdf.map.find("libraryfolders");
    if(it == vdf.map.end()) return "";
    auto* libraryfolders = std::get_if<ACF::Section>(&it->second);
    if(libraryfolders == nullptr) return "";

    for(auto [_, variant] : libraryfolders->map) {
        auto* section = std::get_if<ACF::Section>(&variant);
        const std::string path = ACF::getValue(section, {"path"});
        if(!path.empty()) {
            steamapps_paths.push_back(path + "/steamapps");
        }
    }

    // Find McOsu's app manifest
    for(const auto& steamapps : steamapps_paths) {
        std::string appmanifest = steamapps + "/appmanifest_607260.acf";
        if(!env->fileExists(appmanifest)) continue;

        File acfFile(appmanifest);
        ACF::Section acf = ACF::parse(acfFile.readToString());

        const std::string installdir = ACF::getValue(&acf, {"AppState", "installdir"});
        if(installdir.empty()) continue;

        // We found it!!!
        return fmt::format("{}/common/{}", steamapps, installdir);
    }

    return "";
}
}  // namespace

bool import_from_mcosu(std::string mcosu_path) {
    if(mcosu_path.empty()) {
        mcosu_path = get_mcosu_path();
        if(mcosu_path.empty()) {
            return false;
        }
    }

    std::string cfg_path = mcosu_path + "/cfg/osu.cfg";
    if(!env->fileExists(cfg_path)) return false;

    // HACK: Temporarily disable some callbacks
    // (this one would reload the skin before cv::skin is updated, preventing correct skin from loading)
    cv::skin_use_skin_hitsounds.setCallback([]() -> void {});

    // "No conversion step?"
    // Yes, Console::execConfigFile already handles that
    Console::execConfigFile(cfg_path);

    // HACK: Restore callbacks
    cv::skin_use_skin_hitsounds.setCallback([]() -> void { osu->reloadSkin(); });

    // Also enqueue collections and scores to get imported on next db reload
    db->addPathToImport(mcosu_path + "/collections.db");
    db->addPathToImport(mcosu_path + "/scores.db");

    return true;
}

bool import_from_osu_stable() {
    auto osu_folder = cv::osu_folder.getString();
    if(osu_folder.empty() || !env->directoryExists(osu_folder)) {
        update_osu_folder_from_registry();
        osu_folder = cv::osu_folder.getString();
    }

    const auto username = env->getUsername();
    if(username.length() == 0) {
        debugLog("Failed to get username; not going to import settings from osu!stable.");
        return false;
    }

    std::string cfg_path = fmt::format("{}/osu!.{}.cfg", osu_folder, username);
    File file(cfg_path);
    if(!file.canRead()) {
        debugLog("{} doesn't exist/isn't readable", cfg_path);
        return false;
    }

    std::string str;
    bool b;
    f32 flt;

    for(auto line = file.readLine(); !line.empty() || file.canRead(); line = file.readLine()) {
        if(line.empty()) continue;
        if(Parsing::parse(line, "BeatmapDirectory", '=', &str)) {
            if(str.length() > 2) cv::songs_folder.setValue(str);
        } else if(Parsing::parse(line, "VolumeUniversal", '=', &flt))
            cv::volume_master.setValue(std::clamp<float>(flt / 100.f, 0.1f, 1.f));
        else if(Parsing::parse(line, "VolumeEffect", '=', &flt))
            cv::volume_effects.setValue(flt / 100.f);
        else if(Parsing::parse(line, "VolumeMusic", '=', &flt))
            cv::volume_music.setValue(std::clamp<float>(flt / 100.f, 0.1f, 1.f));
        else if(Parsing::parse(line, "AllowPublicInvites", '=', &b))
            cv::allow_mp_invites.setValue(b);
        else if(Parsing::parse(line, "AutoChatHide", '=', &b))
            cv::chat_auto_hide.setValue(b);
        else if(Parsing::parse(line, "BlockNonFriendPM", '=', &b))
            cv::allow_stranger_dms.setValue(b);
        else if(Parsing::parse(line, "ChatAudibleHighlight", '=', &b))
            cv::chat_ping_on_mention.setValue(b);
        else if(Parsing::parse(line, "ChatHighlightName", '=', &b))
            cv::chat_notify_on_mention.setValue(b);
        else if(Parsing::parse(line, "ChatMessageNotification", '=', &b))
            cv::chat_notify_on_dm.setValue(b);
        else if(Parsing::parse(line, "CursorSize", '=', &flt))
            cv::cursor_scale.setValue(flt);
        else if(Parsing::parse(line, "AutomaticCursorSizing", '=', &b))
            cv::automatic_cursor_size.setValue(b);
        else if(Parsing::parse(line, "DimLevel", '=', &flt))
            cv::background_dim.setValue(flt / 100.f);
        else if(Parsing::parse(line, "IHateHavingFun", '=', &b))
            cv::background_dont_fade_during_breaks.setValue(b);
        else if(Parsing::parse(line, "DiscordRichPresence", '=', &b))
            cv::rich_presence.setValue(b);
        else if(Parsing::parse(line, "FpsCounter", '=', &b))
            cv::draw_fps.setValue(b);
        else if(Parsing::parse(line, "CursorRipple", '=', &b))
            cv::draw_cursor_ripples.setValue(b);
        else if(Parsing::parse(line, "HighlightWords", '=', &str))
            cv::chat_highlight_words.setValue(str);
        else if(Parsing::parse(line, "HighResolution", '=', &b))
            cv::skin_hd.setValue(b);
        else if(Parsing::parse(line, "IgnoreBeatmapSamples", '=', &b)) {
            cv::ignore_beatmap_samples.setValue(b);
            cv::ignore_beatmap_sample_volume.setValue(b);  // Even though this isn't vanilla, it is "expected"
        } else if(Parsing::parse(line, "IgnoreBeatmapSkins", '=', &b)) {
            cv::ignore_beatmap_skins.setValue(b);
            cv::ignore_beatmap_combo_colors.setValue(b);  // Apparently, this setting also affects combo colors!
        } else if(Parsing::parse(line, "IgnoreList", '=', &str))
            cv::chat_ignore_list.setValue(str);
        else if(Parsing::parse(line, "KeyOverlay", '=', &b))
            cv::draw_inputoverlay.setValue(b);
        else if(Parsing::parse(line, "Language", '=', &str))
            cv::language.setValue(str);
        else if(Parsing::parse(line, "ShowInterface", '=', &b))
            cv::draw_hud.setValue(b);
        else if(Parsing::parse(line, "LowResolution", '=', &b))
            cv::skin_hd.setValue(!b);  // note the '!' (this is mirror of HighResolution setting)
        else if(Parsing::parse(line, "MouseDisableButtons", '=', &b))
            cv::disable_mousebuttons.setValue(b);
        else if(Parsing::parse(line, "MouseDisableWheel", '=', &b))
            cv::disable_mousewheel.setValue(b);
        else if(Parsing::parse(line, "MouseSpeed", '=', &flt))
            cv::mouse_sensitivity.setValue(flt);
        else if(Parsing::parse(line, "Offset", '=', &flt))
            cv::universal_offset.setValue(flt);
        else if(Parsing::parse(line, "ScoreMeterScale", '=', &flt))
            cv::hud_hiterrorbar_scale.setValue(flt);
        else if(Parsing::parse(line, "NotifyFriends", '=', &b))
            cv::notify_friend_status_change.setValue(b);
        else if(Parsing::parse(line, "PopupDuringGameplay", '=', &b))
            cv::notify_during_gameplay.setValue(b);
        else if(Parsing::parse(line, "ScoreboardVisible", '=', &b)) {
            cv::draw_scoreboard.setValue(b);
            cv::draw_scoreboard_mp.setValue(b);
        } else if(Parsing::parse(line, "SongSelectThumbnails", '=', &b))
            cv::draw_songbrowser_thumbnails.setValue(b);
        else if(Parsing::parse(line, "ShowSpectators", '=', &b))
            cv::draw_spectator_list.setValue(b);
        else if(Parsing::parse(line, "AutoSendNowPlaying", '=', &b))
            cv::spec_share_map.setValue(b);
        else if(Parsing::parse(line, "ShowStoryboard", '=', &b))
            cv::draw_storyboard.setValue(b);
        else if(Parsing::parse(line, "Skin", '=', &str))
            cv::skin.setValue(str);
        else if(Parsing::parse(line, "SkinSamples", '=', &b))
            cv::skin_use_skin_hitsounds.setValue(b);
        else if(Parsing::parse(line, "SnakingSliders", '=', &b))
            cv::snaking_sliders.setValue(b);
        else if(Parsing::parse(line, "Video", '=', &b))
            cv::draw_video.setValue(b);
        else if(Parsing::parse(line, "RawInput", '=', &b))
            cv::mouse_raw_input.setValue(b);
        else if(Parsing::parse(line, "ConfineMouse", '=', &str))
            cv::confine_cursor.setValue(SString::to_lower(str));
        else if(Parsing::parse(line, "HiddenShowFirstApproach", '=', &b))
            cv::show_approach_circle_on_first_hidden_object.setValue(b);
        else if(Parsing::parse(line, "ComboColourSliderBall", '=', &b))
            cv::slider_ball_tint_combo_color.setValue(b);
        else if(Parsing::parse(line, "Username", '=', &str))
            cv::name.setValue(str);
        else if(Parsing::parse(line, "Letterboxing", '=', &b))
            cv::letterboxing.setValue(b);
        else if(Parsing::parse(line, "LetterboxPositionX", '=', &flt))
            cv::letterboxing_offset_x.setValue(flt / 100.f);
        else if(Parsing::parse(line, "LetterboxPositionY", '=', &flt))
            cv::letterboxing_offset_y.setValue(flt / 100.f);
        else if(Parsing::parse(line, "ShowUnicode", '=', &b))
            cv::prefer_cjk.setValue(b);
        else if(Parsing::parse(line, "Ticker", '=', &b))
            cv::chat_ticker.setValue(b);
        else if(Parsing::parse(line, "keyOsuLeft", '=', &str))
            try_set_key(str, binds::LEFT_CLICK);
        else if(Parsing::parse(line, "keyOsuRight", '=', &str))
            try_set_key(str, binds::RIGHT_CLICK);
        else if(Parsing::parse(line, "keyOsuSmoke", '=', &str))
            try_set_key(str, binds::SMOKE);
        else if(Parsing::parse(line, "keyPause", '=', &str))
            try_set_key(str, binds::GAME_PAUSE);
        else if(Parsing::parse(line, "keySkip", '=', &str))
            try_set_key(str, binds::SKIP_CUTSCENE);
        else if(Parsing::parse(line, "keyToggleScoreboard", '=', &str))
            try_set_key(str, binds::TOGGLE_SCOREBOARD);
        else if(Parsing::parse(line, "keyToggleChat", '=', &str))
            try_set_key(str, binds::TOGGLE_CHAT);
        else if(Parsing::parse(line, "keyToggleExtendedChat", '=', &str))
            try_set_key(str, binds::TOGGLE_EXTENDED_CHAT);
        else if(Parsing::parse(line, "keyScreenshot", '=', &str))
            try_set_key(str, binds::SAVE_SCREENSHOT);
        else if(Parsing::parse(line, "keyIncreaseAudioOffset", '=', &str))
            try_set_key(str, binds::INCREASE_LOCAL_OFFSET);
        else if(Parsing::parse(line, "keyDecreaseAudioOffset", '=', &str))
            try_set_key(str, binds::DECREASE_LOCAL_OFFSET);
        else if(Parsing::parse(line, "keyQuickRetry", '=', &str))
            try_set_key(str, binds::QUICK_RETRY);
        else if(Parsing::parse(line, "keyVolumeIncrease", '=', &str))
            try_set_key(str, binds::INCREASE_VOLUME);
        else if(Parsing::parse(line, "keyVolumeDecrease", '=', &str))
            try_set_key(str, binds::DECREASE_VOLUME);
        else if(Parsing::parse(line, "keyDisableMouseButtons", '=', &str))
            try_set_key(str, binds::DISABLE_MOUSE_BUTTONS);
        else if(Parsing::parse(line, "keyBossKey", '=', &str))
            try_set_key(str, binds::BOSS_KEY);
        else if(Parsing::parse(line, "keyEasy", '=', &str))
            try_set_key(str, binds::MOD_EASY);
        else if(Parsing::parse(line, "keyNoFail", '=', &str))
            try_set_key(str, binds::MOD_NOFAIL);
        else if(Parsing::parse(line, "keyHalfTime", '=', &str))
            try_set_key(str, binds::MOD_HALFTIME);
        else if(Parsing::parse(line, "keyHardRock", '=', &str))
            try_set_key(str, binds::MOD_HARDROCK);
        else if(Parsing::parse(line, "keySuddenDeath", '=', &str))
            try_set_key(str, binds::MOD_SUDDENDEATH);
        else if(Parsing::parse(line, "keyDoubleTime", '=', &str))
            try_set_key(str, binds::MOD_DOUBLETIME);
        else if(Parsing::parse(line, "keyHidden", '=', &str))
            try_set_key(str, binds::MOD_HIDDEN);
        else if(Parsing::parse(line, "keyFlashlight", '=', &str))
            try_set_key(str, binds::MOD_FLASHLIGHT);
        else if(Parsing::parse(line, "keyRelax", '=', &str))
            try_set_key(str, binds::MOD_RELAX);
        else if(Parsing::parse(line, "keyAutopilot", '=', &str))
            try_set_key(str, binds::MOD_AUTOPILOT);
        else if(Parsing::parse(line, "keySpunOut", '=', &str))
            try_set_key(str, binds::MOD_SPUNOUT);
        else if(Parsing::parse(line, "keyAuto", '=', &str))
            try_set_key(str, binds::MOD_AUTO);
        else if(Parsing::parse(line, "keyScoreV2", '=', &str))
            try_set_key(str, binds::MOD_SCOREV2);
    }

    return true;
}
}  // namespace SettingsImporter
