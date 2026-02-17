#include "NeomodEnvInterop.h"
#include "Environment.h"

#include "OsuConVars.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "Downloader.h"  // for extract_beatmapset
#include "File.h"
#include "FixedSizeArray.h"
#include "NeomodUrl.h"
#include "OptionsOverlay.h"
#include "Osu.h"
#include "Parsing.h"
#include "Skin.h"
#include "SongBrowser/SongBrowser.h"
#include "UI.h"
#include "Logging.h"

struct NeomodEnvInterop : public Environment::Interop {
    NOCOPY_NOMOVE(NeomodEnvInterop)
   public:
    NeomodEnvInterop() = delete;
    NeomodEnvInterop(Environment *env_ptr) : Interop(env_ptr) {}
    ~NeomodEnvInterop() override = default;

    bool handle_cmdline_args(const std::vector<std::string> &args) override;
    bool handle_osk(const char *osk_path) override;
    bool handle_osz(const char *osz_path) override;
    void setup_system_integrations() override;
};

namespace neomod {
void *createInterop(void *void_envptr) {
    assert(void_envptr);
    auto *envptr = static_cast<Environment *>(void_envptr);
    return new NeomodEnvInterop(envptr);
}
}  // namespace neomod

// drag-drop/file associations/registry stuff below
bool NeomodEnvInterop::handle_osk(const char *osk_path) {
    if(!ui || !osu || !Skin::unpack(osk_path)) return false;

    auto folder_name = Environment::getFileNameFromFilePath(osk_path);
    folder_name.erase(folder_name.size() - 4);  // remove .osk extension

    cv::skin.setValue(Environment::getFileNameFromFilePath(folder_name));
    ui->getOptionsOverlay()->updateSkinNameLabel();

    return true;
}

bool NeomodEnvInterop::handle_osz(const char *osz_path) {
    if(!osu) return false;

    if(osu->isInPlayMode()) {
        ui->getNotificationOverlay()->addToast(fmt::format("Can't import {} while playing.", osz_path), ERROR_TOAST);
        return false;
    } else if(!db->isFinished() || db->isCancelled()) {
        ui->getNotificationOverlay()->addToast(fmt::format("Can't import {} before songs have been loaded.", osz_path),
                                               ERROR_TOAST);
        return false;
    }

    FixedSizeArray<u8> osz_data;
    {
        File osz(osz_path);
        uSz osz_filesize = osz.getFileSize();
        osz_data = FixedSizeArray{osz.takeFileBuffer(), osz_filesize};
        if(!osz.canRead() || !osz_filesize || !osz_data.data()) {
            ui->getNotificationOverlay()->addToast(fmt::format("Failed to import {}", osz_path), ERROR_TOAST);
            return false;
        }
    }

    i32 set_id = Downloader::extract_beatmapset_id(osz_data.data(), osz_data.size());
    if(set_id < 0) {
        // special case: legacy fallback behavior for invalid beatmapSetID, try to parse the ID from the
        // path
        std::string mapset_name = Environment::getFileNameFromFilePath(osz_path);
        if(!mapset_name.empty() && std::isdigit(static_cast<unsigned char>(mapset_name[0]))) {
            if(!Parsing::parse(mapset_name, &set_id)) {
                set_id = -1;
            }
        }
    }
    if(set_id == -1) {
        ui->getNotificationOverlay()->addToast(US_("Beatmapset doesn't have a valid ID."), ERROR_TOAST);
        return false;
    }

    std::string mapset_dir = fmt::format(NEOMOD_MAPS_PATH "/{}/", set_id);
    Environment::createDirectory(mapset_dir);
    if(!Downloader::extract_beatmapset(osz_data.data(), osz_data.size(), mapset_dir)) {
        ui->getNotificationOverlay()->addToast(US_("Failed to extract beatmapset"), ERROR_TOAST);
        return false;
    }

    BeatmapSet *set = db->addBeatmapSet(mapset_dir, set_id);
    if(!set) {
        ui->getNotificationOverlay()->addToast(US_("Failed to import beatmapset"), ERROR_TOAST);
        return false;
    }

    // FIXME: dont call this here
    ui->getSongBrowser()->selectBeatmapset(set_id);

    return true;
}

bool NeomodEnvInterop::handle_cmdline_args(const std::vector<std::string> &args) {
    if(!osu || !db) return false;

    bool need_to_reload_database = false;

    for(const auto &arg : args) {
        // XXX: naive way of ignoring '-sound soloud' type params, might break in the future
        if(arg[0] == '-') continue;
        if(arg.length() < 4) continue;

        if(arg.starts_with(NEOMOD_URL_SCHEME) || arg.starts_with("neosu://")) {
            neomod::handle_neomod_url(arg.c_str());
        } else {
            auto extension = Environment::getFileExtensionFromFilePath(arg);
            SString::lower_inplace(extension);

            if(extension == "osz") {
                // NOTE: we're assuming db is loaded here?
                handle_osz(arg.c_str());
            } else if(extension == "osk" || extension == "zip") {
                handle_osk(arg.c_str());
            } else if(extension == "db") {
                db->addPathToImport(arg);
                need_to_reload_database = (osu && !osu->isInPlayMode() /* don't immediately import if playing */);
            }
        }
    }

    if(need_to_reload_database) {
        // TODO: bug prone since it can be called from any state...
        ui->getSongBrowser()->refreshBeatmaps(ui->getActiveScreen());
    }

    return need_to_reload_database;
}

#ifdef MCENGINE_PLATFORM_WINDOWS

#include "Engine.h"
#include "SString.h"
#include "Timing.h"
#include "dynutils.h"

#include "WinDebloatDefs.h"
#include <objbase.h>
#include <winreg.h>

#include <SDL3/SDL_system.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>

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

    const UString uLaunchArgs{SString::join(cmdline)};
    const UString uExePath{Environment::getPathToSelf()};

    std::wstring command;
    command.resize(uExePath.length() + uLaunchArgs.length() + 10);

    swprintf_s(command.data(), command.size(), LR"("%s" %s "%%1")", uExePath.wchar_str(), uLaunchArgs.wchar_str());
    command.shrink_to_fit();

    RegSetValueExW(cmd_key, L"", 0, REG_SZ, (BYTE *)command.data(), command.size() * sizeof(wchar_t));
    RegCloseKey(cmd_key);

    RegCloseKey(neomod_key);

    // Remove neosu to avoid duplicate launch option
    if(auto advapi_handle = dynutils::load_lib_system("advapi32.dll")) {
        using RegDeleteTreeW_t = LSTATUS WINAPI(HKEY hKey, LPCWSTR lpSubKey);
        if(auto *pRegDeleteTreeW = dynutils::load_func<RegDeleteTreeW_t>(advapi_handle, "RegDeleteTreeW")) {
            pRegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\neosu");
        }
        dynutils::unload_lib(advapi_handle);
    }

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

void neomod::handleExistingWindow(int argc, char *argv[]) {
    // if a neomod instance is already running, send it a message then quit
    HWND existing_window = FindWindow(TEXT(PACKAGE_NAME), nullptr);
    if(existing_window) {
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

#elif defined(MCENGINE_PLATFORM_LINUX)

#include "NetworkHandler.h"
#include "SString.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstddef>
#include <cstdlib>

namespace {
// stored globally so it can be passed to NetworkHandler after it's created
int g_ipc_socket_fd = -1;

// abstract socket name (leading null byte)
constexpr char IPC_SOCKET_NAME[] = "\0" PACKAGE_NAME "-instance";
constexpr size_t IPC_SOCKET_NAME_LEN = sizeof(IPC_SOCKET_NAME) - 1;  // exclude trailing null from sizeof
}  // namespace

void NeomodEnvInterop::setup_system_integrations() {
    if(g_ipc_socket_fd < 0) return;

    // set up the IPC callback to handle incoming arguments
    networkHandler->setIPCSocket(g_ipc_socket_fd, [this](const std::vector<std::string> &args) {
        this->handle_cmdline_args(args);
        this->env_p->restoreWindow();
    });
}

namespace neomod {
void handleExistingWindow(int argc, char *argv[]) {
    // create abstract unix socket for instance detection
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(sock_fd < 0) {
        debugLog("IPC: failed to create socket: {}", strerror(errno));
        return;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, IPC_SOCKET_NAME, IPC_SOCKET_NAME_LEN);

    // try to bind, if it fails with EADDRINUSE, another instance is running
    socklen_t addr_len = offsetof(sockaddr_un, sun_path) + IPC_SOCKET_NAME_LEN;
    if(bind(sock_fd, reinterpret_cast<sockaddr *>(&addr), addr_len) < 0) {
        if(errno == EADDRINUSE) {
            // another instance is running, connect to it and send our args
            if(connect(sock_fd, reinterpret_cast<sockaddr *>(&addr), addr_len) == 0) {
                // calculate total size of args
                u32 total_size = 0;
                for(int i = 0; i < argc; i++) {
                    total_size += strlen(argv[i]) + 1;
                }

                if(total_size > 0 && total_size < 4096) {
                    // send size header
                    send(sock_fd, &total_size, sizeof(total_size), 0);

                    // send null-separated arguments
                    for(int i = 0; i < argc; i++) {
                        size_t len = strlen(argv[i]) + 1;
                        send(sock_fd, argv[i], len, 0);
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
        } else {
            close(sock_fd);
            return;
        }
    }

    // bind succeeded. we're the first instance, so start listening
    if(listen(sock_fd, 5) < 0) {
        close(sock_fd);
        return;
    }

    // store for later use by setup_system_integrations
    g_ipc_socket_fd = sock_fd;
}
}  // namespace neomod

#else  // other platforms - not implemented

void NeomodEnvInterop::setup_system_integrations() { return; }

namespace neomod {
void handleExistingWindow(int /*argc*/, char * /*argv*/[]) {}
}  // namespace neomod

#endif
