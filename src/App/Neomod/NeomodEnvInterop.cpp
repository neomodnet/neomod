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
#include "Skin.h"
#include "SongBrowser/SongBrowser.h"
#include "UI.h"
#include "score.h"
#include "Logging.h"
#include "SString.h"

namespace neomod {
namespace {
struct NeomodEnvInterop : public Environment::Interop {
    NOCOPY_NOMOVE(NeomodEnvInterop)
   public:
    NeomodEnvInterop() = delete;
    NeomodEnvInterop(Environment *env_ptr) : Interop(env_ptr) {}
    ~NeomodEnvInterop() override = default;

    bool handle_cmdline_args(const std::vector<std::string> &args) override;
    void setup_system_integrations() override;
};
}  // namespace

void *createInterop(void *void_envptr) {
    assert(void_envptr);
    auto *envptr = static_cast<Environment *>(void_envptr);
    return new NeomodEnvInterop(envptr);
}

// drag-drop/file associations/registry stuff below
bool handle_osk(std::string_view osk_path) {
    if(!ui || !osu || !Skin::unpack(osk_path)) return false;

    auto folder_name = Environment::getFileNameFromFilePath(osk_path);
    folder_name.erase(folder_name.size() - 4);  // remove .osk extension

    cv::skin.setValue(Environment::getFileNameFromFilePath(folder_name));
    ui->getOptionsOverlay()->updateSkinNameLabel();

    return true;
}

bool NeomodEnvInterop::handle_cmdline_args(const std::vector<std::string> &args) {
    if(!osu || !db) return false;
    using namespace neomod;

    bool got_db_related_import = false;
    std::vector<std::string> replay_imports;
    bool need_to_reload_database = false;

    for(const auto &arg : args) {
        // XXX: naive way of ignoring '-sound soloud' type params, might break in the future
        if(arg[0] == '-') continue;
        if(arg.length() < 4) continue;
        if(arg.ends_with(PACKAGE_NAME) || arg.ends_with(PACKAGE_NAME ".exe")) continue;

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
                    // external file: import async through the installer and navigate to it, but never
                    // delete the user's file. the installer defers the import until the db is loaded.
                    osu->getBeatmapInstaller()->enqueue_local(arg, /*auto_select=*/true, /*delete_after=*/false);
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

    if(!got_db_related_import) return false;

    // Don't import maps, replays or reload database while playing
    // TODO: bug prone since there are many other possible edge cases...
    if(!osu->isInPlayMode()) {
        SongBrowser *sbr = ui->getSongBrowser();
        RankingScreen *rankingscreen = ui->getRankingScreen();
        // .osz imports run through the installer (enqueued above); only replays need post-load handling.
        auto finish_importing = [sbr, rankingscreen, notif = ui->getNotificationOverlay(), replay_imports] {
            FinishedScore last_imported_replay;

            for(const auto &path : replay_imports) {
                FinishedScore replay;
                if(LegacyReplay::load_osr(path, replay)) {
                    last_imported_replay = replay;
                } else {
                    notif->addToast("Failed to load replay.", ERROR_TOAST);
                }
            }

            if(last_imported_replay != FinishedScore{}) {
                BeatmapDifficulty *map = db->getBeatmapDifficulty(last_imported_replay.beatmap_hash);
                if(map) {
                    last_imported_replay.map = map;
                    sbr->onDifficultySelected(map, false);
                    rankingscreen->setScore(last_imported_replay);
                    ui->setScreen(rankingscreen);
                } else {
                    // TODO: auto-download
                    notif->addToast("This replay's beatmap is not installed.", ERROR_TOAST);
                }
            }
        };

        if(need_to_reload_database) {
            sbr->refreshBeatmaps(ui->getActiveScreen(), std::move(finish_importing));
        } else {
            finish_importing();
        }
    }
    return true;
}

namespace {
// not used on WASM
[[maybe_unused]] bool is_multi_instance_desired(int argc, char *argv[]) {
    if(argc <= 1) return false;
    for(int i = 1; i < argc; ++i) {
        if(!argv[i]) continue;
        if(SString::to_lower(std::string_view{argv[i]}) == "-multi"sv) {
            return true;
        }
    }
    return false;
}
}  // namespace
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

#define NEOMOD_CMDLINE_WINDOW_MSG_ID TEXT("NEOMOD_CMDLINE")
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

bool sdl_windows_message_hook(void *userdata, MSG *msg) {
    static UINT cmdline_msg = RegisterWindowMessage(NEOMOD_CMDLINE_WINDOW_MSG_ID);

    // true == continue processing
    if(!userdata || !msg || ((msg->message != cmdline_msg) && (msg->message != WM_HOTKEY))) {
        return true;
    }

    // check if its a hotkey
    if(msg->message == WM_HOTKEY) {
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

    // else check the custom registered message
    NeomodEnvInterop *interop{static_cast<NeomodEnvInterop *>(userdata)};

    // reconstruct the mapping/event names from the identifier passed in lParam
    auto sender_pid = static_cast<DWORD>(msg->wParam);
    auto identifier = static_cast<DWORD>(msg->lParam);

    const std::string mapping_name = fmt::format(PACKAGE_NAME "_cmdline_{}_{}", sender_pid, identifier);
    const std::string event_name = fmt::format(PACKAGE_NAME "_event_{}_{}", sender_pid, identifier);

    HANDLE hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, mapping_name.c_str());
    if(hMapFile) {
        bool signaled = false;

        auto signal_completion = [&signaled, &event_name]() -> void {
            if(signaled) return;
            signaled = true;
            HANDLE hEvent = OpenEventA(EVENT_MODIFY_STATE, FALSE, event_name.c_str());
            if(hEvent) {
                SetEvent(hEvent);
                CloseHandle(hEvent);
            }
        };

        LPVOID pBuf = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if(pBuf) {
            // first 4 bytes contain the data size
            DWORD data_size = *((DWORD *)pBuf);
            const char *data = ((const char *)pBuf) + sizeof(DWORD);

            // parse null-separated arguments
            std::vector<std::string> args;
            const char *current = data;
            const char *end = data + data_size;

            while(current < end) {
                size_t len = strnlen(current, end - current);
                if(len > 0) {
                    args.emplace_back(current, len);
                }
                current += len + 1;  // split on null, skip null
            }

            UnmapViewOfFile(pBuf);

            // we're done with the mapped file, signal the event
            signal_completion();

            // handle the arguments
            if(!args.empty()) {
                debugLog("handling external arguments: {}", SString::join(args));
                interop->handle_cmdline_args(args);
            }
        }
        CloseHandle(hMapFile);
        signal_completion();
    }

    // focus current window
    interop->env_p->restoreWindow();

    return false;  // we already processed everything, don't fallthrough to sdl
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

    SDL_SetWindowsMessageHook(sdl_windows_message_hook, (void *)this);

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

    // Add current launch arguments, so doing "Open with -> neomod"
    // will always use the last launch options the player used.
    auto cmdline = env->getCommandLine();
    assert(!cmdline.empty());
    cmdline.erase(cmdline.begin());  // remove program name

    const std::wstring uLaunchArgs{UniString::to_wide(SString::join(cmdline))};
    const std::wstring uExePath{UniString::to_wide(Environment::getPathToSelf())};

    std::wstring command;
    command.resize(uExePath.length() + uLaunchArgs.length() + 10);

    swprintf_s(command.data(), command.size(), LR"("%s" %s "%%1")", uExePath.c_str(), uLaunchArgs.c_str());
    command.shrink_to_fit();

    RegSetValueExW(cmd_key, L"", 0, REG_SZ, (BYTE *)command.data(), command.size() * sizeof(wchar_t));
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

void handleExistingWindow(int argc, char *argv[]) {
    // if a neomod instance is already running, send it a message then quit
    HWND existing_window = FindWindow(TEXT(PACKAGE_NAME), nullptr);
    if(existing_window) {
        if(is_multi_instance_desired(argc, argv)) {
            // return early (this instance was started with the -multi argument)
            return;
        }
        // send even if we only have the exe name as args, just use it as a request to focus the existing window
        size_t total_size = 0;
        for(int i = 0; i < argc; i++) {
            total_size += strlen(argv[i]) + 1;  // +1 for null terminator
        }

        if(total_size > 0 && total_size < 4096) {  // reasonable size limit...?
            // need to create a unique identifier for this message
            DWORD sender_pid = GetCurrentProcessId();
            DWORD identifier = GetTickCount();

            // create unique names for mapping and event
            const std::string mapping_name = fmt::format(PACKAGE_NAME "_cmdline_{}_{}", sender_pid, identifier);
            const std::string event_name = fmt::format(PACKAGE_NAME "_event_{}_{}", sender_pid, identifier);

            // create completion event first, so we know when we can exit this process
            HANDLE hEvent = CreateEventA(nullptr, FALSE, FALSE, event_name.c_str());

            // create named shared memory for the data
            // for some reason, WM_COPYDATA hooks don't work with the SDL message loop... this is the next best solution
            HANDLE hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                                 total_size + sizeof(DWORD),  // extra space for size header
                                                 mapping_name.c_str());

            if(hMapFile && hEvent) {
                LPVOID pBuf = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
                if(pBuf) {
                    // store the size first
                    *((DWORD *)pBuf) = (DWORD)total_size;
                    char *data = ((char *)pBuf) + sizeof(DWORD);
                    char *current = data;

                    // pack arguments with null separators, so we can split them easily
                    for(int i = 0; i < argc; i++) {
                        size_t len = strlen(argv[i]);
                        memcpy(current, argv[i], len);
                        current[len] = '\0';
                        current += len + 1;
                    }

                    UnmapViewOfFile(pBuf);

                    // post the identifier message
                    UINT neomod_msg = RegisterWindowMessage(NEOMOD_CMDLINE_WINDOW_MSG_ID);

                    if(PostMessage(existing_window, neomod_msg, (WPARAM)sender_pid, (LPARAM)identifier)) {
                        // wait for the receiver to signal completion (with 5 second timeout)
                        DWORD wait_result = WaitForSingleObject(hEvent, 5000);
                        switch(wait_result) {
                            case WAIT_OBJECT_0:
                                // success
                                break;
                            case WAIT_TIMEOUT:
                                debugLog("timeout waiting for message processing completion");
                                break;
                            case WAIT_FAILED:
                                debugLog("failed to wait for completion event, error: {}", GetLastError());
                                break;
                            default:
                                debugLog("unexpected wait result: {}", wait_result);
                                break;
                        }
                    } else {
                        debugLog("failed to post message to existing HWND {}, error: {}",
                                 static_cast<const void *>(existing_window), GetLastError());
                    }
                }
            }
            if(hMapFile) CloseHandle(hMapFile);
            if(hEvent) CloseHandle(hEvent);
        }

        SetForegroundWindow(existing_window);
        std::exit(0);
    }
}
}  // namespace neomod

#elif defined(MCENGINE_PLATFORM_LINUX) || defined(MCENGINE_PLATFORM_MACOS)

#include "NetworkHandler.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace neomod {
namespace {
constexpr bool USING_ABSTRACT_SOCKETS{Env::cfg(OS::LINUX)};

// stored globally so it can be passed to NetworkHandler after it's created
int g_ipc_socket_fd{-1};

#ifndef MCENGINE_PLATFORM_LINUX
// pathname socket needs explicit cleanup on exit; linux abstract sockets clean up on close
char *g_ipc_socket_path{nullptr};

void cleanup_ipc_socket_atexit() {
    if(g_ipc_socket_path) {
        unlink(g_ipc_socket_path);
        free(g_ipc_socket_path);
        g_ipc_socket_path = nullptr;
    }
}
#endif

struct IpcAddr {
    sockaddr_un addr{};
    socklen_t len{0};
};

// linux: abstract socket (leading null in sun_path).
// macos/bsd: pathname socket under $TMPDIR (or /tmp), per-uid.
IpcAddr build_ipc_addr() {
    IpcAddr a{};
    a.addr.sun_family = AF_UNIX;

    if constexpr(USING_ABSTRACT_SOCKETS) {
        constexpr char NAME[] = "\0" PACKAGE_NAME "-instance";
        constexpr size_t NAME_LEN = sizeof(NAME) - 1;  // exclude trailing null from sizeof
        static_assert(NAME_LEN <= sizeof(a.addr.sun_path));
        memcpy(&a.addr.sun_path[0], &NAME[0], NAME_LEN);
        a.len = offsetof(sockaddr_un, sun_path) + NAME_LEN;
    } else {
        const char *tmpdir = std::getenv("TMPDIR");
        if(!tmpdir || !*tmpdir) tmpdir = "/tmp";

        // strip trailing slashes so we can concat cleanly
        size_t tmplen = strlen(tmpdir);
        while(tmplen > 1 && tmpdir[tmplen - 1] == '/') --tmplen;

        // include uid so the /tmp fallback can't collide between users on the same machine
        const std::string path = fmt::format("{}/" PACKAGE_NAME "-{}.sock", std::string_view{tmpdir, tmplen}, getuid());

        if(path.size() + 1 > sizeof(a.addr.sun_path)) {
            debugLog("IPC: socket path too long: {}", path);
            return {};
        }
        memcpy(&a.addr.sun_path[0], path.data(), path.size() + 1);  // include null terminator
        a.len = offsetof(sockaddr_un, sun_path) + path.size() + 1;
    }

    return a;
}

enum class BindOutcome : u8 { Owner, AnotherAlive, HardError };

// try to bind sock_fd. on EADDRINUSE, probe with a separate fd to tell apart
// a live owner from a stale socket file; if stale, unlink and retry.
//
// note: there's a tiny race window between unlink and rebind where two concurrent
// launchers can both end up bound to fresh files (one orphaned). closing it would
// require a separate flock lockfile. probably will never happen, so not handling it
BindOutcome try_bind(int sock_fd, const IpcAddr &a) {
    const auto *sa = reinterpret_cast<const sockaddr *>(&a.addr);

    if(bind(sock_fd, sa, a.len) == 0) return BindOutcome::Owner;
    if(errno != EADDRINUSE) return BindOutcome::HardError;

    // someone has the address. probe with a throwaway fd to see if they're alive.
    const int probe_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(probe_fd < 0) return BindOutcome::AnotherAlive;
    const bool probe_alive = connect(probe_fd, sa, a.len) == 0;
    close(probe_fd);

    if(probe_alive) return BindOutcome::AnotherAlive;

    // stale socket file, remove it
    if constexpr(!USING_ABSTRACT_SOCKETS) {
        unlink(&a.addr.sun_path[0]);
    }

    if(bind(sock_fd, sa, a.len) == 0) return BindOutcome::Owner;
    return BindOutcome::AnotherAlive;  // lost a race
}
}  // namespace

void NeomodEnvInterop::setup_system_integrations() {
    if(g_ipc_socket_fd < 0) return;

    // set up the IPC callback to handle incoming arguments
    networkHandler->setIPCSocket(g_ipc_socket_fd, [this](const std::vector<std::string> &args) {
        this->handle_cmdline_args(args);
        this->env_p->restoreWindow();
    });
}

void handleExistingWindow(int argc, char *argv[]) {
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(sock_fd < 0) {
        debugLog("IPC: failed to create socket: {}", strerror(errno));
        return;
    }

    const IpcAddr a = build_ipc_addr();
    if(a.len == 0) {
        close(sock_fd);
        return;
    }

    switch(try_bind(sock_fd, a)) {
        case BindOutcome::Owner: {
#ifndef MCENGINE_PLATFORM_LINUX
            g_ipc_socket_path = strdup(&a.addr.sun_path[0]);
            std::atexit(cleanup_ipc_socket_atexit);
#endif
            if(listen(sock_fd, 5) < 0) {
                debugLog("IPC: socket listen failed: {}", strerror(errno));
                close(sock_fd);
                return;
            }
            // hand off ownership of sock_fd; NetworkHandler will close it on shutdown
            g_ipc_socket_fd = sock_fd;
            return;
        }

        case BindOutcome::AnotherAlive: {
            // return early (this instance was started with the -multi argument)
            if(is_multi_instance_desired(argc, argv)) {
                close(sock_fd);
                return;
            }

            const auto *sa = reinterpret_cast<const sockaddr *>(&a.addr);
            if(connect(sock_fd, sa, a.len) == 0) {
                u32 total_size = 0;
                for(int i = 0; i < argc; i++) {
                    total_size += strlen(argv[i]) + 1;
                }

                if(total_size > 0 && total_size < 4096) {
                    send(sock_fd, &total_size, sizeof(total_size), 0);
                    for(int i = 0; i < argc; i++) {
                        send(sock_fd, argv[i], strlen(argv[i]) + 1, 0);
                    }

                    // wait for acknowledgment (with timeout via select)
                    fd_set fds;
                    FD_ZERO(&fds);
                    FD_SET(sock_fd, &fds);
                    timeval tv{.tv_sec = 5, .tv_usec = 0};
                    if(select(sock_fd + 1, &fds, nullptr, nullptr, &tv) > 0) {
                        char ack = 0;
                        recv(sock_fd, &ack, 1, 0);
                    }
                }
            }

            close(sock_fd);
            std::exit(0);
        }

        case BindOutcome::HardError: {
            debugLog("IPC: bind error: {}", strerror(errno));
            close(sock_fd);
            return;
        }
    }
}
}  // namespace neomod

#else  // other platforms - not implemented
namespace neomod {

void NeomodEnvInterop::setup_system_integrations() { return; }

void handleExistingWindow(int /*argc*/, char * /*argv*/[]) {}
}  // namespace neomod

#endif
