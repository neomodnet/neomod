#include "NeomodEnvInterop.h"
#include "Environment.h"

#include "OsuConVars.h"
#include "BeatmapInstaller.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "NeomodUrl.h"
#include "OptionsOverlay.h"
#include "Osu.h"
#include "RankingScreen.h"
#include "SkinArchive.h"
#include "SongBrowser/SongBrowser.h"
#include "UI.h"
#include "score.h"
#include "Logging.h"
#include "SString.h"
#include "LaunchArgs.h"
#include "Paths.h"

namespace neomod {
namespace {
struct NeomodEnvInterop : public Environment::Interop {
    NOCOPY_NOMOVE(NeomodEnvInterop)
   public:
    NeomodEnvInterop() : Interop() {}
    ~NeomodEnvInterop() override = default;

    void setup_system_integrations() override;
};
}  // namespace

void *createInterop(void * /*env_ptr*/) { return new NeomodEnvInterop(); }

// drag-drop/file associations/registry stuff below
bool handle_osk(std::string_view osk_path, bool auto_select) {
    if(!ui || !osu || !SkinArchive::unpack(osk_path, Mc::Paths::skins())) return false;

    if(auto_select) {
        auto folder_name = Environment::getFileNameFromFilePath(osk_path);
        folder_name.erase(folder_name.size() - 4);  // remove .osk extension

        cv::skin.setValue(Environment::getFileNameFromFilePath(folder_name));
        ui->getOptionsOverlay()->updateSkinNameLabel();
    }

    return true;
}

void handle_open_requests(std::span<const std::string> requests) {
    bool got_db_related_import = false;
    std::vector<std::string> osz_imports;
    std::vector<std::string> replay_imports;
    bool need_to_reload_database = false;

    for(const auto &arg : requests) {
        if(arg.length() < 4) continue;

        if(arg.starts_with(NEOMOD_URL_SCHEME) || arg.starts_with("neosu://")) {
            debugLog("Handling {}...", arg);

            neomod::handle_neomod_url(arg.c_str());
            got_db_related_import = true;  // ?
        } else {
            auto extension = Environment::getFileExtensionFromFilePath(arg);
            SString::lower_inplace(extension);

            if(!extension.empty()) {
                debugLog("Handling {}...", arg);

                if(extension == "osz") {
                    osz_imports.push_back(arg);
                    need_to_reload_database |= (!db->isFinished() || db->isCancelled());
                    got_db_related_import = true;
                } else if(extension == "osr") {
                    replay_imports.push_back(arg);
                    need_to_reload_database |= (!db->isFinished() || db->isCancelled());
                    got_db_related_import = true;
                } else if(extension == "osk" || extension == "zip") {
                    handle_osk(arg.c_str());
                } else if(extension == "db") {
                    db->addPathToImport(arg);
                    need_to_reload_database = true;
                    got_db_related_import = true;
                }
            }
        }
    }

    if(!got_db_related_import) return;

    // external files: import async through the installer, but never delete the user's file. the
    // installer defers the actual db import until the db is loaded. enqueued even while playing (a
    // background import); we just don't navigate or reload the db in that case.
    for(const auto &path : osz_imports) {
        osu->getBeatmapInstaller()->enqueue_local(path, /*auto_select=*/true, /*delete_after=*/false);
    }

    // Don't navigate, import replays or reload the database while playing
    // TODO: bug prone since there are many other possible edge cases...
    if(osu->isInPlayMode()) return;

    // load the replays now (local files, no db needed); we only open the score screen of the last one.
    // resolving its beatmap and navigating is deferred to Osu::openScoreScreenWhenReady, since the
    // beatmap may still be installing from a .osz above, or the db may still be loading below.
    FinishedScore last_replay;
    for(const auto &path : replay_imports) {
        FinishedScore replay;
        if(LegacyReplay::load_osr(path, replay)) {
            last_replay = replay;
        } else {
            ui->getNotificationOverlay()->addToast("Failed to load replay.", ERROR_TOAST);
        }
    }

    if(need_to_reload_database) {
        ui->getSongBrowser()->refreshBeatmaps(ui->getActiveScreen());
    }

    if(last_replay != FinishedScore{}) {
        osu->openScoreScreenWhenReady(last_replay);
    }
}

}  // namespace neomod

#ifdef MCENGINE_PLATFORM_WINDOWS

#include "Engine.h"
#include "UniString.h"
#include "Timing.h"

#include "WinDebloatDefs.h"
#include <objbase.h>
#include <winreg.h>

#include <SDL3/SDL_system.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_keycode.h>

namespace neomod {

namespace {  // static
constexpr std::array<UINT, 5> VK_LIST{0xB3 /* VK_MEDIA_PLAY_PAUSE */, 0xFA /* VK_PLAY */, 0xB2 /* VK_MEDIA_STOP */,
                                      0xB1 /* VK_MEDIA_PREV_TRACK */, 0xB0 /* VK_MEDIA_NEXT_TRACK */};
bool hotkeys_enabled{false};

// cvar callbacks
bool register_hotkeys() {
    if(!env) return false;
    const HWND hwnd = env->getHwnd();

    bool success = false;
    int id = 0x0000;
    UINT modifiers = 0x4000 /* MOD_NOREPEAT */;
    for(UINT vk : VK_LIST) {
        if(!(success = RegisterHotKey(hwnd, id, modifiers, vk))) {
            if(!modifiers) break;  // its all over
            modifiers = 0;         // try without MOD_NOREPEAT
            id = 0x01A4;
            if(!(success = RegisterHotKey(hwnd, id, modifiers, vk))) {
                break;
            }
        }
        id++;  // need unique id for each one
    }
    if(!success) {
        debugLog("could not register global hotkeys: {}", GetLastError());
    }
    hotkeys_enabled = success;
    return success;
}

void unregister_hotkeys() {
    if(!env) return;

    const HWND hwnd = env->getHwnd();
    for(int idunregstart : {0, 0x01A4}) {
        int id = idunregstart;
        for(UINT _ : VK_LIST) {
            UnregisterHotKey(hwnd, id);  // just unregister everything
            id++;
        }
    }

    hotkeys_enabled = false;
    return;
}

bool sdl_windows_message_hook(void * /*userdata*/, MSG *msg) {
    // true == continue processing
    if(!msg || msg->message != WM_HOTKEY) {
        return true;
    }

    // debugLog("WM_HOTKEY received, {} {}", msg->lParam, msg->wParam);
    SDL_Event keydownev{}, keyupev{};
    keydownev.key = {.type = SDL_EVENT_KEY_DOWN,
                     .reserved = {},
                     .timestamp = Timing::getTicksNS(),
                     .windowID = {},
                     .which = {},
                     .scancode = {},  // to set in the switch-case
                     .key = {},
                     .mod = {},
                     .raw = {},
                     .down = true,
                     .repeat = (msg->wParam < 0x01A4) ? false : false /* maybe? */};

    switch(HIWORD(msg->lParam)) {
        case VK_LIST[0]:
            keydownev.key.scancode = SDL_SCANCODE_MEDIA_PLAY_PAUSE;
            break;
        case VK_LIST[1]:
            keydownev.key.scancode = SDL_SCANCODE_MEDIA_PLAY;
            break;
        case VK_LIST[2]:
            keydownev.key.scancode = SDL_SCANCODE_MEDIA_STOP;
            break;
        case VK_LIST[3]:
            keydownev.key.scancode = SDL_SCANCODE_MEDIA_PREVIOUS_TRACK;
            break;
        case VK_LIST[4]:
            keydownev.key.scancode = SDL_SCANCODE_MEDIA_NEXT_TRACK;
            break;
        default:
            return false;  // not our hotkey?
    }
    // send a key up after too just in case...
    keydownev.key.key = SDL_SCANCODE_TO_KEYCODE(keydownev.key.scancode);
    keyupev = keydownev;
    keyupev.key.type = SDL_EVENT_KEY_UP;
    keyupev.key.down = false;
    SDL_PushEvent(&keydownev);
    SDL_PushEvent(&keyupev);
    // debugLog("pushed events for {}", std::to_underlying(keydownev.key.scancode));
    return false;
}

}  // namespace

void NeomodEnvInterop::setup_system_integrations() {
    if(!register_hotkeys()) {
        unregister_hotkeys();
        // don't set up a callback
        cv::win_global_media_hotkeys.setDefaultDouble(-1.);
        cv::win_global_media_hotkeys.setValue(-1.);
    } else {
        cv::win_global_media_hotkeys.setCallback([](float newf) -> void {
            const bool enable = !!static_cast<int>(newf);
            cv::win_global_media_hotkeys.setValue(enable, false);  // clamp it to 0/1
            if(enable != hotkeys_enabled) {
                if(enable) {
                    register_hotkeys();
                } else {
                    unregister_hotkeys();
                }
            }
        });
    }

    SDL_SetWindowsMessageHook(sdl_windows_message_hook, nullptr);

    // Register neomod as an application
    HKEY neomod_key;
    i32 err = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\neomod", 0, nullptr, REG_OPTION_NON_VOLATILE,
                              KEY_WRITE, nullptr, &neomod_key, nullptr);
    if(err != ERROR_SUCCESS) {
        debugLog("Failed to register " PACKAGE_NAME " as an application. Error: {} (root)", err);
        return;
    }
    RegSetValueExW(neomod_key, L"", 0, REG_SZ, (BYTE *)L"neomod", 12);
    RegSetValueExW(neomod_key, L"URL Protocol", 0, REG_SZ, (BYTE *)L"", 2);

    HKEY app_key;
    err = RegCreateKeyExW(neomod_key, L"Application", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &app_key,
                          nullptr);
    if(err != ERROR_SUCCESS) {
        debugLog("Failed to register " PACKAGE_NAME " as an application. Error: {} (app)", err);
        RegCloseKey(neomod_key);
        return;
    }
    RegSetValueExW(app_key, L"ApplicationName", 0, REG_SZ, (BYTE *)L"neomod", 12);
    RegCloseKey(app_key);

    HKEY cmd_key;
    err = RegCreateKeyExW(neomod_key, L"shell\\open\\command", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                          &cmd_key, nullptr);
    if(err != ERROR_SUCCESS) {
        debugLog("Failed to register " PACKAGE_NAME " as an application. Error: {} (command)", err);
        RegCloseKey(neomod_key);
        return;
    }

    // Add current launch options, so doing "Open with -> neomod"
    // will always use the last launch options the player used.
    // (just the switches, not the files/links this launch was asked to open)
    std::wstring command = L'"' + UniString::to_wide(Mc::Paths::exe()) + L'"';
    if(const auto switches = Mc::LaunchArgs::get_switches_cmdline(); !switches) {
        debugLog("Launch options not passed on to file associations: can't cut them out of the command line");
    } else if(switches->contains('%')) {
        // the shell would take it for one of its placeholders, like the "%1" below
        debugLog("Launch options not passed on to file associations: they contain a '%'");
    } else if(!switches->empty()) {
        command += L' ' + UniString::to_wide(*switches);
    }
    command += LR"( "%1")";

    RegSetValueExW(cmd_key, L"", 0, REG_SZ, (const BYTE *)command.c_str(), (command.size() + 1) * sizeof(wchar_t));
    RegCloseKey(cmd_key);

    RegCloseKey(neomod_key);

    // Register neomod as .osk handler
    HKEY osk_key;
    err = RegCreateKeyEx(HKEY_CURRENT_USER, TEXT("Software\\Classes\\.osk\\OpenWithProgids"), 0, nullptr,
                         REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &osk_key, nullptr);
    if(err != ERROR_SUCCESS) {
        debugLog("Failed to register " PACKAGE_NAME " as .osk format handler. Error: {}", err);
        return;
    }
    RegSetValueEx(osk_key, TEXT("neomod"), 0, REG_SZ, (BYTE *)TEXT(""), sizeof(TEXT("")));
    RegDeleteValue(osk_key, TEXT("neosu"));
    RegCloseKey(osk_key);

    // Register neomod as .osr handler
    HKEY osr_key;
    err = RegCreateKeyEx(HKEY_CURRENT_USER, TEXT("Software\\Classes\\.osr\\OpenWithProgids"), 0, nullptr,
                         REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &osr_key, nullptr);
    if(err != ERROR_SUCCESS) {
        debugLog("Failed to register " PACKAGE_NAME " as .osr format handler. Error: {}", err);
        return;
    }
    RegSetValueEx(osr_key, TEXT("neomod"), 0, REG_SZ, (BYTE *)TEXT(""), sizeof(TEXT("")));
    RegDeleteValue(osr_key, TEXT("neosu"));
    RegCloseKey(osr_key);

    // Register neomod as .osz handler
    HKEY osz_key;
    err = RegCreateKeyEx(HKEY_CURRENT_USER, TEXT("Software\\Classes\\.osz\\OpenWithProgids"), 0, nullptr,
                         REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &osz_key, nullptr);
    if(err != ERROR_SUCCESS) {
        debugLog("Failed to register " PACKAGE_NAME " as .osz format handler. Error: {}", err);
        return;
    }
    RegSetValueEx(osz_key, TEXT("neomod"), 0, REG_SZ, (BYTE *)TEXT(""), sizeof(TEXT("")));
    RegDeleteValue(osz_key, TEXT("neosu"));
    RegCloseKey(osz_key);
}
}  // namespace neomod

#else  // nothing to set up elsewhere
namespace neomod {

void NeomodEnvInterop::setup_system_integrations() { return; }
}  // namespace neomod

#endif
