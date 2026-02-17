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
#include "Environment.h"
#include "Parsing.h"
#include "Logging.h"

#ifdef MCENGINE_PLATFORM_WINDOWS
#include "WinDebloatDefs.h"

#include <winbase.h>
#include <winreg.h>
#endif
namespace SettingsImporter {
namespace {  // static namespace
void try_set_key(const std::string& key, ConVar* cvar) {
    if(key == "None") {
        cvar->setValue(0);
    } else if(key == "A") {
        cvar->setValue<SCANCODE>(KEY_A);
    } else if(key == "B") {
        cvar->setValue<SCANCODE>(KEY_B);
    } else if(key == "C") {
        cvar->setValue<SCANCODE>(KEY_C);
    } else if(key == "D") {
        cvar->setValue<SCANCODE>(KEY_D);
    } else if(key == "E") {
        cvar->setValue<SCANCODE>(KEY_E);
    } else if(key == "F") {
        cvar->setValue<SCANCODE>(KEY_F);
    } else if(key == "G") {
        cvar->setValue<SCANCODE>(KEY_G);
    } else if(key == "H") {
        cvar->setValue<SCANCODE>(KEY_H);
    } else if(key == "I") {
        cvar->setValue<SCANCODE>(KEY_I);
    } else if(key == "J") {
        cvar->setValue<SCANCODE>(KEY_J);
    } else if(key == "K") {
        cvar->setValue<SCANCODE>(KEY_K);
    } else if(key == "L") {
        cvar->setValue<SCANCODE>(KEY_L);
    } else if(key == "M") {
        cvar->setValue<SCANCODE>(KEY_M);
    } else if(key == "N") {
        cvar->setValue<SCANCODE>(KEY_N);
    } else if(key == "O") {
        cvar->setValue<SCANCODE>(KEY_O);
    } else if(key == "P") {
        cvar->setValue<SCANCODE>(KEY_P);
    } else if(key == "Q") {
        cvar->setValue<SCANCODE>(KEY_Q);
    } else if(key == "R") {
        cvar->setValue<SCANCODE>(KEY_R);
    } else if(key == "S") {
        cvar->setValue<SCANCODE>(KEY_S);
    } else if(key == "T") {
        cvar->setValue<SCANCODE>(KEY_T);
    } else if(key == "U") {
        cvar->setValue<SCANCODE>(KEY_U);
    } else if(key == "V") {
        cvar->setValue<SCANCODE>(KEY_V);
    } else if(key == "W") {
        cvar->setValue<SCANCODE>(KEY_W);
    } else if(key == "X") {
        cvar->setValue<SCANCODE>(KEY_X);
    } else if(key == "Y") {
        cvar->setValue<SCANCODE>(KEY_Y);
    } else if(key == "Z") {
        cvar->setValue<SCANCODE>(KEY_Z);
    } else if(key == "0") {
        cvar->setValue<SCANCODE>(KEY_0);
    } else if(key == "1") {
        cvar->setValue<SCANCODE>(KEY_1);
    } else if(key == "2") {
        cvar->setValue<SCANCODE>(KEY_2);
    } else if(key == "3") {
        cvar->setValue<SCANCODE>(KEY_3);
    } else if(key == "4") {
        cvar->setValue<SCANCODE>(KEY_4);
    } else if(key == "5") {
        cvar->setValue<SCANCODE>(KEY_5);
    } else if(key == "6") {
        cvar->setValue<SCANCODE>(KEY_6);
    } else if(key == "7") {
        cvar->setValue<SCANCODE>(KEY_7);
    } else if(key == "8") {
        cvar->setValue<SCANCODE>(KEY_8);
    } else if(key == "9") {
        cvar->setValue<SCANCODE>(KEY_9);
    } else if(key == "F1") {
        cvar->setValue<SCANCODE>(KEY_F1);
    } else if(key == "F2") {
        cvar->setValue<SCANCODE>(KEY_F2);
    } else if(key == "F3") {
        cvar->setValue<SCANCODE>(KEY_F3);
    } else if(key == "F4") {
        cvar->setValue<SCANCODE>(KEY_F4);
    } else if(key == "F5") {
        cvar->setValue<SCANCODE>(KEY_F5);
    } else if(key == "F6") {
        cvar->setValue<SCANCODE>(KEY_F6);
    } else if(key == "F7") {
        cvar->setValue<SCANCODE>(KEY_F7);
    } else if(key == "F8") {
        cvar->setValue<SCANCODE>(KEY_F8);
    } else if(key == "F9") {
        cvar->setValue<SCANCODE>(KEY_F9);
    } else if(key == "F10") {
        cvar->setValue<SCANCODE>(KEY_F10);
    } else if(key == "F11") {
        cvar->setValue<SCANCODE>(KEY_F11);
    } else if(key == "F12") {
        cvar->setValue<SCANCODE>(KEY_F12);
    } else if(key == "Left") {
        cvar->setValue<SCANCODE>(KEY_LEFT);
    } else if(key == "Right") {
        cvar->setValue<SCANCODE>(KEY_RIGHT);
    } else if(key == "Up") {
        cvar->setValue<SCANCODE>(KEY_UP);
    } else if(key == "Down") {
        cvar->setValue<SCANCODE>(KEY_DOWN);
    } else if(key == "Tab") {
        cvar->setValue<SCANCODE>(KEY_TAB);
    } else if((key == "Return") || (key == "Enter")) {
        cvar->setValue<SCANCODE>(KEY_ENTER);
    } else if(key == "Shift") {
        cvar->setValue<SCANCODE>(KEY_LSHIFT);
    } else if(key == "Control") {
        cvar->setValue<SCANCODE>(KEY_LCONTROL);
    } else if(key == "LeftAlt") {
        cvar->setValue<SCANCODE>(KEY_LALT);
    } else if(key == "RightAlt") {
        cvar->setValue<SCANCODE>(KEY_RALT);
    } else if(key == "Escape") {
        cvar->setValue<SCANCODE>(KEY_ESCAPE);
    } else if(key == "Space") {
        cvar->setValue<SCANCODE>(KEY_SPACE);
    } else if(key == "Back") {
        cvar->setValue<SCANCODE>(KEY_BACKSPACE);
    } else if(key == "End") {
        cvar->setValue<SCANCODE>(KEY_END);
    } else if(key == "Insert") {
        cvar->setValue<SCANCODE>(KEY_INSERT);
    } else if(key == "Delete") {
        cvar->setValue<SCANCODE>(KEY_DELETE);
    } else if(key == "Help") {
        cvar->setValue<SCANCODE>(KEY_HELP);
    } else if(key == "Home") {
        cvar->setValue<SCANCODE>(KEY_HOME);
    } else if(key == "Escape") {
        cvar->setValue<SCANCODE>(KEY_ESCAPE);
    } else if(key == "PageUp") {
        cvar->setValue<SCANCODE>(KEY_PAGEUP);
    } else if(key == "PageDown") {
        cvar->setValue<SCANCODE>(KEY_PAGEDOWN);
    } else {
        debugLog("No key code found for '{}'!", key);
    }
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
        UString new_osu_folder{path};
        debugLog("Found osu! folder from registry: {:s}", new_osu_folder.toUtf8());
        cv::osu_folder.setValue(new_osu_folder.toUtf8());
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
            UString wPath{szPath};
            return std::string(wPath.toUtf8());
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

    const auto& username = env->getUsername();
    if(username.length() == 0) {
        debugLog("Failed to get username; not going to import settings from osu!stable.");
        return false;
    }

    std::string cfg_path = fmt::format("{}/osu!.{}.cfg", cv::osu_folder.getString(), username.toUtf8());
    File file(cfg_path);
    if(!file.canRead()) return false;

    std::string str;
    bool b;
    f32 flt;

    for(auto line = file.readLine(); !line.empty() || file.canRead(); line = file.readLine()) {
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
        else if(Parsing::parse(line, "ConfineMouse", '=', &str)) {
            const bool never = str == "Never";
            const bool fullscreen = !never && str == "Fullscreen";
            const bool always = !fullscreen && !never && str == "Always";
            cv::confine_cursor_fullscreen.setValue(fullscreen || always);
            cv::confine_cursor_windowed.setValue(always);
            cv::confine_cursor_never.setValue(never);
        } else if(Parsing::parse(line, "HiddenShowFirstApproach", '=', &b))
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
            try_set_key(str, &cv::LEFT_CLICK);
        else if(Parsing::parse(line, "keyOsuRight", '=', &str))
            try_set_key(str, &cv::RIGHT_CLICK);
        else if(Parsing::parse(line, "keyOsuSmoke", '=', &str))
            try_set_key(str, &cv::SMOKE);
        else if(Parsing::parse(line, "keyPause", '=', &str))
            try_set_key(str, &cv::GAME_PAUSE);
        else if(Parsing::parse(line, "keySkip", '=', &str))
            try_set_key(str, &cv::SKIP_CUTSCENE);
        else if(Parsing::parse(line, "keyToggleScoreboard", '=', &str))
            try_set_key(str, &cv::TOGGLE_SCOREBOARD);
        else if(Parsing::parse(line, "keyToggleChat", '=', &str))
            try_set_key(str, &cv::TOGGLE_CHAT);
        else if(Parsing::parse(line, "keyToggleExtendedChat \n]", '=', &str))
            try_set_key(str, &cv::TOGGLE_EXTENDED_CHAT);
        else if(Parsing::parse(line, "keyScreenshot", '=', &str))
            try_set_key(str, &cv::SAVE_SCREENSHOT);
        else if(Parsing::parse(line, "keyIncreaseAudioOffset", '=', &str))
            try_set_key(str, &cv::INCREASE_LOCAL_OFFSET);
        else if(Parsing::parse(line, "keyDecreaseAudioOffset", '=', &str))
            try_set_key(str, &cv::DECREASE_LOCAL_OFFSET);
        else if(Parsing::parse(line, "keyQuickRetry", '=', &str))
            try_set_key(str, &cv::QUICK_RETRY);
        else if(Parsing::parse(line, "keyVolumeIncrease", '=', &str))
            try_set_key(str, &cv::INCREASE_VOLUME);
        else if(Parsing::parse(line, "keyVolumeDecrease", '=', &str))
            try_set_key(str, &cv::DECREASE_VOLUME);
        else if(Parsing::parse(line, "keyDisableMouseButtons", '=', &str))
            try_set_key(str, &cv::DISABLE_MOUSE_BUTTONS);
        else if(Parsing::parse(line, "keyBossKey", '=', &str))
            try_set_key(str, &cv::BOSS_KEY);
        else if(Parsing::parse(line, "keyEasy", '=', &str))
            try_set_key(str, &cv::MOD_EASY);
        else if(Parsing::parse(line, "keyNoFail", '=', &str))
            try_set_key(str, &cv::MOD_NOFAIL);
        else if(Parsing::parse(line, "keyHalfTime", '=', &str))
            try_set_key(str, &cv::MOD_HALFTIME);
        else if(Parsing::parse(line, "keyHardRock", '=', &str))
            try_set_key(str, &cv::MOD_HARDROCK);
        else if(Parsing::parse(line, "keySuddenDeath", '=', &str))
            try_set_key(str, &cv::MOD_SUDDENDEATH);
        else if(Parsing::parse(line, "keyDoubleTime", '=', &str))
            try_set_key(str, &cv::MOD_DOUBLETIME);
        else if(Parsing::parse(line, "keyHidden", '=', &str))
            try_set_key(str, &cv::MOD_HIDDEN);
        else if(Parsing::parse(line, "keyFlashlight", '=', &str))
            try_set_key(str, &cv::MOD_FLASHLIGHT);
        else if(Parsing::parse(line, "keyRelax", '=', &str))
            try_set_key(str, &cv::MOD_RELAX);
        else if(Parsing::parse(line, "keyAutopilot", '=', &str))
            try_set_key(str, &cv::MOD_AUTOPILOT);
        else if(Parsing::parse(line, "keySpunOut", '=', &str))
            try_set_key(str, &cv::MOD_SPUNOUT);
        else if(Parsing::parse(line, "keyAuto", '=', &str))
            try_set_key(str, &cv::MOD_AUTO);
        else if(Parsing::parse(line, "keyScoreV2", '=', &str))
            try_set_key(str, &cv::MOD_SCOREV2);
    }

    return true;
}
}  // namespace SettingsImporter
