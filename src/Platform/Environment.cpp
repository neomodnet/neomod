// Copyright (c) 2018, PG & 2025, WH, All rights reserved.

#include "Environment.h"

#include "Engine.h"
#include "MakeDelegateWrapper.h"
#include "Mouse.h"
#include "File.h"
#include "RuntimePlatform.h"
#include "SString.h"
#include "Timing.h"
#include "Logging.h"
#include "ConVar.h"
#include "Thread.h"

#include "AppDescriptor.h"

#if defined(MCENGINE_FEATURE_DIRECTX11)
#include "DirectX11Interface.h"
#endif

#if defined(MCENGINE_FEATURE_SDLGPU)
#include "SDLGPUInterface.h"
#endif

#ifdef MCENGINE_PLATFORM_WASM
#include "NullGraphics.h"
#endif

#include "SDLGLInterface.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <utility>
#include <string>
#include <sstream>
#include <iomanip>

#if defined(MCENGINE_PLATFORM_WINDOWS)
#include "WinDebloatDefs.h"
#include <lmcons.h>
#include <libloaderapi.h>
#include <winbase.h>
#elif defined(__APPLE__) || defined(MCENGINE_PLATFORM_LINUX)
#include <pwd.h>
#include <unistd.h>
#ifdef MCENGINE_PLATFORM_LINUX
#include <X11/Xlib.h>
#endif
#elif defined(__EMSCRIPTEN__)
// TODO (?)
#endif

#include <SDL3/SDL.h>

using namespace flags::operators;

// sanity check
#define SDL_WF_EQ(fname__) (WinFlags::F_##fname__ == (WinFlags)SDL_WINDOW_##fname__)
static_assert(SDL_WF_EQ(FULLSCREEN) && SDL_WF_EQ(OPENGL) && SDL_WF_EQ(OCCLUDED) && SDL_WF_EQ(HIDDEN) &&
                  SDL_WF_EQ(BORDERLESS) && SDL_WF_EQ(RESIZABLE) && SDL_WF_EQ(MINIMIZED) && SDL_WF_EQ(MAXIMIZED) &&
                  SDL_WF_EQ(MOUSE_GRABBED) && SDL_WF_EQ(INPUT_FOCUS) && SDL_WF_EQ(MOUSE_FOCUS) && SDL_WF_EQ(EXTERNAL) &&
                  SDL_WF_EQ(MODAL) && SDL_WF_EQ(HIGH_PIXEL_DENSITY) && SDL_WF_EQ(MOUSE_CAPTURE) &&
                  SDL_WF_EQ(MOUSE_RELATIVE_MODE) && SDL_WF_EQ(ALWAYS_ON_TOP) && SDL_WF_EQ(UTILITY) &&
                  SDL_WF_EQ(TOOLTIP) && SDL_WF_EQ(POPUP_MENU) && SDL_WF_EQ(KEYBOARD_GRABBED) && SDL_WF_EQ(VULKAN) &&
                  SDL_WF_EQ(METAL) && SDL_WF_EQ(TRANSPARENT) && SDL_WF_EQ(NOT_FOCUSABLE),
              "outdated WinFlags enum");
#undef SDL_WF_EQ

Environment *env{nullptr};

SDL_Environment *Environment::s_sdlenv{nullptr};

void Mc::initEnvBlock() { Environment::s_sdlenv = SDL_GetEnvironment(); }

Environment::Environment(const Mc::AppDescriptor &appDesc,
                         std::unordered_map<std::string, std::optional<std::string>> argMap,
                         std::vector<std::string> cmdlineVec)
    : m_interop(appDesc.createInterop ? static_cast<Interop *>(appDesc.createInterop(this)) : new Interop(this)),
      m_mArgMap(std::move(argMap)),
      m_vCmdLine(std::move(cmdlineVec)),
      m_cursorIcons(/*lazy init*/) {
    env = this;

    if(!s_sdlenv) {
        s_sdlenv = SDL_GetEnvironment();
    }

    m_engine = nullptr;  // will be initialized by the mainloop once setup is complete
    m_window = nullptr;  // ditto
    m_windowID = 0;      // ditto

    m_bRunning = true;
    m_bIsRestartScheduled = false;
    m_bHeadless = m_mArgMap.contains("-headless");

    m_fDisplayHz = 360.0f;
    m_fDisplayHzSecs = 1.0f / m_fDisplayHz;

    // make env->getDPI() always return 96
    // the hint set in main.cpp, before SDL_Init, will do the rest of the dirty work
    m_bDPIOverride = m_mArgMap.contains("-nodpi");

    m_bEnvDebug = false;

    m_bRestoreFullscreen = false;  // if minimizing, whether we need to restore fullscreen state on restore
    m_bMinimizeSupported = true;   // will set to false if our minimize request didn't actually result in minimizing

    m_sUsername = {};
    m_sProgDataPath = {};  // local data for McEngine files
    m_sAppDataPath = {};

    m_bIsCursorInsideWindow = true;
    m_bCursorClipped = false;
    m_bHideCursorPending = false;
    m_cursorType = CURSORTYPE::CURSOR_NORMAL;

    // lazy init
    m_vLastAbsMousePos = vec2{};

    m_sCurrClipboardText = {};
    // lazy init
    m_mMonitors = {};
    // lazy init (with initMonitors)
    m_fullDesktopBoundingBox = McRect{};

    m_sdldriver = SDL_GetCurrentVideoDriver();
    m_bIsX11 = (m_sdldriver == "x11");
    m_bIsKMSDRM = (m_sdldriver == "kmsdrm");
    m_bIsWayland = (m_sdldriver == "wayland");

    m_bShouldListenToTextInput = cv::use_ime.getBool();
    m_bRawKB = SDL_GetHintBoolean(SDL_HINT_WINDOWS_RAW_KEYBOARD, false);
    // the hints might already be set from the startup environment, so respect that here (1)
    m_bWinKeyDisabled = m_bRawKB ? SDL_GetHintBoolean(SDL_HINT_WINDOWS_RAW_KEYBOARD_EXCLUDE_HOTKEYS, false) : false;

    // use directx if:
    // we we built with support for it, and
    // (either OpenGL(ES) + SDLGPU is missing, or
    // (-directx or -dx11 are specified on the command line))
    // use SDLGPU if:
    // we we built with support for it, and
    // (either OpenGL(ES) + DX11 is missing, or
    // (-sdlgpu or -gpu are specified on the command line))
    // otherwise, use whichever of GLES32/GL are available
    {
        using enum RuntimeRenderer;
        // clang-format off
        m_renderer = 
            (Env::cfg(REND::DX11) &&
                (!(Env::cfg(REND::GL | REND::GLES32 | REND::SDLGPU)) ||
                  (m_mArgMap.contains("-directx") || m_mArgMap.contains("-dx11"))))
            ? DX11
        : (Env::cfg(REND::SDLGPU) && 
                (!(Env::cfg(REND::GL | REND::GLES32 | REND::DX11)) ||
                  (m_mArgMap.contains("-sdlgpu") || m_mArgMap.contains("-gpu"))))
            ? SDLGPU
        : Env::cfg(REND::GLES32) 
            ? GLES
        : GL;
        // clang-format on
    }

    // setup callbacks
    cv::debug_env.setCallback(SA::MakeDelegate<&Environment::onLogLevelChange>(this));
    cv::monitor.setCallback(SA::MakeDelegate<&Environment::onMonitorChange>(this));
    cv::keyboard_raw_input.setValue(m_bRawKB);  // (2)
    cv::keyboard_raw_input.setCallback(SA::MakeDelegate<&Environment::onRawKeyboardChange>(this));
    cv::debug_draw_hardware_cursor.setCallback(SA::MakeDelegate<&Environment::onDebugDrawHardwareCursorChange>(this));

    // set high priority right away
    McThread::set_current_thread_prio(cv::win_processpriority.getVal<McThread::Priority>());
}

Environment::~Environment() {
    for(auto &sdl_cur : m_cursorIcons) {
        if(sdl_cur) {
            SDL_DestroyCursor(sdl_cur);
            sdl_cur = nullptr;
        }
    }
    m_cursorIcons = {};

#if defined(MCENGINE_FEATURE_OPENGL) || defined(MCENGINE_FEATURE_GLES32)
    SDLGLInterface::unload();
#endif

    env = nullptr;
}

// well this doesn't do much atm... called at the end of engine->onUpdate
void Environment::update() {
    // should be handled by the event loop
    // m_bIsCursorInsideWindow = winFocused() && m_engine->getScreenRect().contains(getMousePos());
}

Graphics *Environment::createRenderer() {
#ifdef MCENGINE_PLATFORM_WASM
    if(m_bHeadless) return new NullGraphics();
#endif
#ifdef MCENGINE_FEATURE_DIRECTX11
    if(usingDX11())  // only if specified on the command line, for now
        return new DirectX11Interface(Env::cfg(OS::WINDOWS) ? getHwnd() : reinterpret_cast<HWND>(m_window));
#endif
#ifdef MCENGINE_FEATURE_SDLGPU
    if(usingSDLGPU()) return new SDLGPUInterface(m_window);
#endif
#if defined(MCENGINE_FEATURE_OPENGL) || defined(MCENGINE_FEATURE_GLES32)
    // need to load stuff dynamically before the base class constructors
    SDLGLInterface::load();
    return new SDLGLInterface(m_window);
#endif
}

void Environment::shutdown() {
    if(!isRunning()) return;

    setRawMouseInput(false);

    SDL_Event event{};
    event.quit = {.type = SDL_EVENT_QUIT, .reserved = {}, .timestamp = Timing::getTicksNS()};

    SDL_PushEvent(&event);
}

void Environment::restart() {
    m_bIsRestartScheduled = true;
    shutdown();
}

const std::string &Environment::getExeFolder() {
    static std::string pathStr{};
    if(!pathStr.empty()) return pathStr;
    // sdl caches this internally, but we'll cache a std::string representation of it
    const char *path = SDL_GetBasePath();
    if(path) {
        pathStr = path;
    } else {
        pathStr = "./";
    }
    return pathStr;
}

void Environment::openURLInDefaultBrowser(std::string_view url) noexcept {
    if(!SDL_OpenURL(std::string{url}.c_str())) {
        debugLog("Failed to open URL: {:s}", SDL_GetError());
    }
}

const UString &Environment::getUsername() const noexcept {
    if(!m_sUsername.isEmpty()) return m_sUsername;
#if defined(MCENGINE_PLATFORM_WINDOWS)
    DWORD username_len = UNLEN + 1;
    std::array<wchar_t, UNLEN + 1> username{};

    if(GetUserNameW(username.data(), &username_len)) {
        m_sUsername = UString{username.data(), std::max(0, (int)username_len - 1)};
    }
#elif defined(__APPLE__) || defined(MCENGINE_PLATFORM_LINUX) || defined(MCENGINE_PLATFORM_WASM)
    std::string user = getEnvVariable("USER");
    if(!user.empty()) m_sUsername = {user};
#ifndef MCENGINE_PLATFORM_WASM
    else {
        struct passwd *pwd = getpwuid(getuid());
        if(pwd != nullptr) m_sUsername = {pwd->pw_name};
    }
#endif
#endif
    // fallback
    if(m_sUsername.isEmpty()) m_sUsername = {PACKAGE_NAME "-user"};
    return m_sUsername;
}

// i.e. toplevel appdata path (or ~/.local/share/)
const std::string &Environment::getUserDataPath() const noexcept {
    if(!m_sAppDataPath.empty()) return m_sAppDataPath;

    m_sAppDataPath = MCENGINE_DATA_DIR;  // set it to non-empty to avoid endlessly failing if SDL_GetPrefPath fails once

    char *path = SDL_GetPrefPath("", "");
    if(path != nullptr) {
        m_sAppDataPath = path;
        SDL_free(path);

        // since this is kind of an abuse of SDL_GetPrefPath, we remove the extra slashes at the end
        File::normalizeSlashes(m_sAppDataPath, '\\', '/');
    }

    return m_sAppDataPath;
}

// i.e. ~/.local/share/PACKAGE_NAME
const std::string &Environment::getLocalDataPath() const noexcept {
    if(!m_sProgDataPath.empty()) return m_sProgDataPath;

    char *path = SDL_GetPrefPath("McEngine", PACKAGE_NAME);
    if(path != nullptr) m_sProgDataPath = path;

    SDL_free(path);

    if(m_sProgDataPath.empty())  // fallback to exe dir
        m_sProgDataPath = getExeFolder();

    return m_sProgDataPath;
}

const std::string &Environment::getCacheDir() const noexcept {
    if(!m_sCacheDir.empty()) return m_sCacheDir;

    if constexpr(Env::cfg(OS::LINUX) || Env::cfg(OS::MAC)) {
        // $XDG_CACHE_HOME/neomod
        const std::string xdg_cache_home = Environment::getEnvVariable("XDG_CACHE_HOME");
        if(!xdg_cache_home.empty()) {
            m_sCacheDir = xdg_cache_home + "/" PACKAGE_NAME;
            return m_sCacheDir;
        }

        // $HOME/.cache/neomod
        const std::string home = Environment::getEnvVariable("HOME");
        if(!home.empty()) {
            m_sCacheDir = home + "/.cache/" PACKAGE_NAME;
            return m_sCacheDir;
        }
    }

    if constexpr(Env::cfg(OS::WASM)) {
        // /persist/cache
        m_sCacheDir = "/persist/cache";
    } else {
        // ./cache
        m_sCacheDir = MCENGINE_DATA_DIR "cache";
    }
    return m_sCacheDir;
}

// modifies the input filename! (checks case insensitively past the last slash)
bool Environment::fileExists(std::string &filename) noexcept {
    return File::existsCaseInsensitive(filename) == File::FILETYPE::FILE;
}

// modifies the input directoryName! (checks case insensitively past the last slash)
bool Environment::directoryExists(std::string &directoryName) noexcept {
    return File::existsCaseInsensitive(directoryName) == File::FILETYPE::FOLDER;
}

// same as the above, but for string literals (so we can't check insensitively and modify the input)
bool Environment::fileExists(std::string_view filename) noexcept {
    return File::exists(filename) == File::FILETYPE::FILE;
}

bool Environment::directoryExists(std::string_view directoryName) noexcept {
    return File::exists(directoryName) == File::FILETYPE::FOLDER;
}

bool Environment::createDirectory(const std::string &directoryName) noexcept {
    return SDL_CreateDirectory(directoryName.c_str());  // returns true if it already exists
}

bool Environment::deletePathsRecursive(const std::string &path, int recursionLevel) noexcept {
    if(path == getExeFolder()) {
        fubar_abort();
    }

    const std::string curFolder = getFolderFromFilePath(path);
    if(curFolder == getExeFolder()) {
        fubar_abort();
    }

    auto files = getFilesInFolder(curFolder);
    for(const auto &file : files) {
        deleteFile(curFolder + file);
    }

    --recursionLevel;
    if(recursionLevel <= 0) {
        return !SDL_RemovePath(curFolder.c_str());
    }

    auto folders = getFoldersInFolder(curFolder);
    for(const auto &folder : folders) {
        deletePathsRecursive(curFolder + folder, recursionLevel);
    }
    return true;  // ?
}

bool Environment::renameFile(const std::string &oldFileName, const std::string &newFileName) noexcept {
    if(oldFileName == newFileName) {
        return true;
    }
    if(!SDL_RenamePath(oldFileName.c_str(), newFileName.c_str())) {
        std::string tempFile{newFileName + ".tmp"};
        if(!SDL_CopyFile(oldFileName.c_str(), tempFile.c_str())) {
            return false;
        }
        if(!SDL_RenamePath(tempFile.c_str(), newFileName.c_str())) {
            return false;
        }
        // return true if we were able to copy the path (and the file exists in the end), even if removing the old file
        // didn't work
        SDL_RemovePath(oldFileName.c_str());
    }
    return Environment::fileExists(newFileName);
}

bool Environment::deleteFile(const std::string &filePath) noexcept { return SDL_RemovePath(filePath.c_str()); }

namespace {
// make sure we have a valid path for enumeration (ends with the right separator, and handles long paths on windows)
// TODO: fix duplication between here and File
std::string manualDirectoryFixup(std::string_view input) {
    assert(!input.empty());

    auto fsPath = File::getFsPath(input);
    // std::filesystem bogus (will crash if you try to get the wrong type of string from what you constructed it with)
    UString ret;
    if constexpr(Env::cfg(OS::WINDOWS)) {
        ret = fsPath.wstring().c_str();
    } else {
        ret = fsPath.string().c_str();
    }
    UString endSep{US_("/")};

    if constexpr(Env::cfg(OS::WINDOWS)) {
        // for UNC/long paths, make sure we use a backslash as the last separator
        if(ret.startsWith(US_(R"(\\?\)")) || ret.startsWith(US_(R"(\\.\)"))) {
            endSep = US_("\\");
        }
    }

    if(!ret.endsWith(endSep)) {
        ret.append(endSep);
    }

    return std::string{ret.utf8View()};
}

}  // namespace

// passthroughs, with some extra validation
std::vector<std::string> Environment::getFilesInFolder(std::string_view folder) noexcept {
    std::vector<std::string> out;
    File::getDirectoryEntries(manualDirectoryFixup(folder), File::DirContents::FILES, out);
    return out;
}

std::vector<std::string> Environment::getFoldersInFolder(std::string_view folder) noexcept {
    std::vector<std::string> out;
    File::getDirectoryEntries(manualDirectoryFixup(folder), File::DirContents::DIRECTORIES, out);
    return out;
}

std::vector<std::string> Environment::getEntriesInFolder(std::string_view folder) noexcept {
    std::vector<std::string> out;
    File::getDirectoryEntries(manualDirectoryFixup(folder), File::DirContents::ALL, out);
    return out;
}

std::string Environment::normalizeDirectory(std::string dirPath) noexcept {
    SString::trim_inplace(dirPath);
    if(dirPath.empty()) return dirPath;

    // remove drive letter prefix if switching to linux
    if constexpr(!Env::cfg(OS::WINDOWS)) {
        if(dirPath.find(':') == 1) {
            dirPath.erase(0, 2);
        }
    }

    while(dirPath.ends_with('\\') || dirPath.ends_with('/')) {
        dirPath.pop_back();
    }
    dirPath.push_back('/');

    // use std::filesystem lexically_normal to clean up the path (it doesn't make sure it exists, purely transforms it)
    std::filesystem::path fspath{dirPath};
    dirPath = fspath.lexically_normal().generic_string();

    if(dirPath == "./") {
        return "";
    } else {
        return dirPath;
    }
}

bool Environment::isAbsolutePath(std::string_view filePath) noexcept {
    bool is_absolute_path = filePath.starts_with('/');

    if constexpr(Env::cfg(OS::WINDOWS)) {
        // On Wine, linux paths are also valid, hence the OR
        is_absolute_path |=
            ((filePath.find(':') == 1) || (filePath.starts_with(R"(\\?\)") || filePath.starts_with(R"(\\.\)")));
    }

    return is_absolute_path;
}

std::string Environment::getFileNameFromFilePath(std::string_view filepath) noexcept {
    return getThingFromPathHelper(filepath, false);
}

std::string Environment::getFolderFromFilePath(std::string_view filepath) noexcept {
    return getThingFromPathHelper(filepath, true);
}

std::string Environment::getFileExtensionFromFilePath(std::string_view filepath) noexcept {
    const auto extIdx = filepath.find_last_of('.');
    if(extIdx != std::string::npos) {
        return std::string{filepath.substr(extIdx + 1)};
    } else {
        return "";
    }
}

// sadly, sdl doesn't give a way to do this
std::vector<UString> Environment::getLogicalDrives() {
    std::vector<UString> drives{};

    if constexpr(Env::cfg(OS::LINUX)) {
        drives.emplace_back("/");
    } else if constexpr(Env::cfg(OS::WINDOWS)) {
#if defined(MCENGINE_PLATFORM_WINDOWS)
        DWORD dwDrives = GetLogicalDrives();
        for(int i = 0; i < 26; i++)  // A-Z
        {
            if(dwDrives & (1 << i)) {
                char driveLetter = 'A' + i;
                UString drivePath = fmt::format("{:c}:/", driveLetter);

                SDL_PathInfo info;
                UString testPath = fmt::format("{:c}:\\", driveLetter);

                if(SDL_GetPathInfo(testPath.toUtf8(), &info)) drives.emplace_back(drivePath);
            }
        }
#endif
    } else if constexpr(Env::cfg(OS::WASM)) {
        // TODO: VFS
        drives.emplace_back("/");
    }

    return drives;
}

// cached on startup from main.cpp with argv[0] passed, after that argv0 can be null
const std::string &Environment::getPathToSelf(const char *argv0) {
    static std::string pathStr{};
    if(!pathStr.empty()) return pathStr;
    if constexpr(Env::cfg(OS::WASM)) {
        pathStr = MCENGINE_DATA_DIR;
        return pathStr;
    }

    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path exe_path{};

    if constexpr(Env::cfg(OS::LINUX))
        exe_path = fs::canonical("/proc/self/exe", ec);
    else {
        UString uPath{argv0};
        exe_path = fs::canonical(fs::path(uPath.plat_str()), ec);
    }

    if(!ec && !exe_path.empty())  // canonical path found
    {
        UString uPath{exe_path.string().c_str()};
        pathStr = uPath.toUtf8();
    } else {
#if defined(MCENGINE_PLATFORM_WINDOWS)  // fallback to GetModuleFileNameW
        std::array<wchar_t, MAX_PATH + 1> buf{};
        int length = static_cast<int>(GetModuleFileNameW(nullptr, buf.data(), MAX_PATH));
        UString uPath{buf.data(), length};
        pathStr = uPath.toUtf8();
#else
#ifndef MCENGINE_PLATFORM_LINUX
        debugLog("WARNING: unsupported platform");
#endif
        std::string sp;
        std::ifstream("/proc/self/comm") >> sp;
        if(!sp.empty()) {  // fallback to data dir + self
            pathStr = MCENGINE_DATA_DIR + sp;
        } else {  // fallback to data dir + package name
            pathStr = std::string{MCENGINE_DATA_DIR PACKAGE_NAME};
        }
#endif
    }
    return pathStr;
}

std::string Environment::getEnvVariable(std::string_view varToQuery) noexcept {
    const char *varVal = nullptr;
    if(s_sdlenv && !varToQuery.empty()) {
        varVal = SDL_GetEnvironmentVariable(s_sdlenv, std::string{varToQuery}.c_str());
        if(varVal) {
            return std::string{varVal};
        }
    }
    return {""};
}

bool Environment::setEnvVariable(std::string_view varToSet, std::string_view varValue, bool overwrite) noexcept {
    if(s_sdlenv && !varToSet.empty()) {
        return SDL_SetEnvironmentVariable(s_sdlenv, std::string{varToSet}.c_str(),
                                          varValue.empty() ? "" : std::string{varValue}.c_str(), overwrite);
    }
    return false;
}

bool Environment::unsetEnvVariable(std::string_view varToUnset) noexcept {
    if(s_sdlenv && !varToUnset.empty()) {
        return SDL_UnsetEnvironmentVariable(s_sdlenv, std::string{varToUnset}.c_str());
    }
    return false;
}

std::string Environment::encodeStringToURI(std::string_view unencodedString) noexcept {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for(const char c : unencodedString) {
        // keep alphanumerics and other accepted characters intact
        if(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            escaped << c;
        } else {
            // any other characters are percent-encoded
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }

    return escaped.str();
}

std::string Environment::filesystemPathToURI(const std::filesystem::path &path) noexcept {
    namespace fs = std::filesystem;
    // convert to absolute path and normalize
    auto abs_path = fs::absolute(path);
    // convert to path with forward slashes
    const UString path_str = UString{abs_path.generic_string()};
    // URI encode the path
    std::string uri = encodeStringToURI(path_str.toUtf8());

    // prepend with file:///
    if(uri[0] == '/')
        uri = fmt::format("file://{}", uri);
    else
        uri = fmt::format("file:///{}", uri);

    // add trailing slash if it's a directory
    if(fs::is_directory(abs_path) && !uri.ends_with('/')) {
        uri += '/';
    }
    return uri;
}

const UString &Environment::getClipBoardText() {
    char *newClip = SDL_GetClipboardText();
    if(newClip) m_sCurrClipboardText = newClip;

    SDL_free(newClip);

    return m_sCurrClipboardText;
}

void Environment::setClipBoardText(const UString &text) {
    m_sCurrClipboardText = text;
    SDL_SetClipboardText(text.toUtf8());
}

// static helper for class methods below (defaults to flags = error, modalWindow = null)
void Environment::showDialog(const char *title, const char *message, unsigned int flags, void *modalWindow) {
    auto *actualWin{static_cast<SDL_Window *>(modalWindow)};

    bool wasFullscreen = false;
    if(actualWin) {
        const SDL_WindowFlags winflags = SDL_GetWindowFlags(actualWin);
        if((winflags & SDL_WINDOW_HIDDEN) == SDL_WINDOW_HIDDEN) {
            // message does not show up for hidden windows
            actualWin = nullptr;
        } else if((winflags & SDL_WINDOW_FULLSCREEN) == SDL_WINDOW_FULLSCREEN) {
            // make sure to exit fullscreen so the dialog box shows up
            SDL_SetWindowFullscreen(actualWin, false);
            // don't allow these synthetic events to reach the event loop
            SDL_PumpEvents();
            SDL_FlushEvents(SDL_EVENT_WINDOW_FIRST, SDL_EVENT_WINDOW_LAST);
            wasFullscreen = true;
        }
    }

    SDL_ShowSimpleMessageBox(flags, title, message, actualWin);

    // re-enable fullscreen
    if(wasFullscreen) {
        SDL_SetWindowFullscreen(actualWin, true);
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_EVENT_WINDOW_FIRST, SDL_EVENT_WINDOW_LAST);
    }
}

void Environment::showMessageInfo(const UString &title, const UString &message) const {
    showDialog(title.toUtf8(), message.toUtf8(), SDL_MESSAGEBOX_INFORMATION, m_window);
}

void Environment::showMessageWarning(const UString &title, const UString &message) const {
    showDialog(title.toUtf8(), message.toUtf8(), SDL_MESSAGEBOX_WARNING, m_window);
}

void Environment::showMessageError(const UString &title, const UString &message) const {
    showDialog(title.toUtf8(), message.toUtf8(), SDL_MESSAGEBOX_ERROR, m_window);
}

// what is the point of this exactly?
void Environment::showMessageErrorFatal(const UString &title, const UString &message) const {
    showMessageError(title, message);
}

void Environment::openFileWindow(FileDialogCallback callback, const char *filetypefilters, std::string_view /*title*/,
                                 std::string_view initialpath) const noexcept {
    // convert filetypefilters (Windows-style)
    std::vector<std::string> filterNames;
    std::vector<std::string> filterPatterns;
    std::vector<SDL_DialogFileFilter> sdlFilters;

    if(filetypefilters && *filetypefilters) {
        const char *curr = filetypefilters;
        // add the filetype filters to the SDL dialog filter
        while(*curr) {
            const char *name = curr;
            curr += strlen(name) + 1;

            if(!*curr) break;

            const char *pattern = curr;
            curr += strlen(pattern) + 1;

            filterNames.emplace_back(name);
            filterPatterns.emplace_back(pattern);

            SDL_DialogFileFilter filter = {filterNames.back().c_str(), filterPatterns.back().c_str()};
            sdlFilters.push_back(filter);
        }
    }

    if(initialpath.length() > 0 && !directoryExists(initialpath)) {
        initialpath = getLocalDataPath();
    }

    auto *cbdata{new auto(std::move(callback))};

    // show it
    SDL_ShowOpenFileDialog(sdlFileDialogCallback, cbdata, m_window, sdlFilters.empty() ? nullptr : sdlFilters.data(),
                           static_cast<int>(sdlFilters.size()), std::string{initialpath}.c_str(), false);
}

void Environment::openFolderWindow(FileDialogCallback callback, std::string_view initialpath) const noexcept {
    if(initialpath.length() > 0 && !directoryExists(initialpath)) {
        initialpath = getLocalDataPath();
    }

    auto *cbdata{new auto(std::move(callback))};

    SDL_ShowOpenFolderDialog(sdlFileDialogCallback, cbdata, m_window, std::string{initialpath}.c_str(), false);
}

// just open the file manager in a certain folder, but not do anything with it
void Environment::openFileBrowser(std::string_view initialpath) const noexcept {
    std::string pathToOpen{initialpath};
    if(pathToOpen.empty())
        pathToOpen = getExeFolder();
    else {
        // XXX: On windows you can also open a folder while having a file selected
        //      Would be useful for screenshots, for example
        pathToOpen = getFolderFromFilePath(pathToOpen);
    }

    if(pathToOpen.empty() || pathToOpen == "/") {
        debugLog("Couldn't parse a path to open from {}!", initialpath);
        return;
    }

    std::string encodedPath;
    if constexpr(Env::cfg(OS::WINDOWS)) {
        // Apparently, "file://" URIs don't work with UNC paths on Windows (despite conflicting information from MSDN).
        // getFolderFromFilePath should do whatever cleanup is required anyways. Super deeply nested folders might not work, but that should be rare.
        encodedPath = fmt::format("file://{}", pathToOpen);
    } else {
        // On Linux/Unix, convert to a URI (for xdg-open to work).
        encodedPath = filesystemPathToURI(pathToOpen);
    }

    assert(!encodedPath.empty());
    bool success = SDL_OpenURL(encodedPath.c_str());
    if(!success && Env::cfg(OS::WINDOWS)) {
        // bullshit
        auto fspath = File::getFsPath(initialpath);
        if(!fspath.empty()) {
            namespace fs = std::filesystem;
            std::error_code ec;
            auto status = fs::status(fspath, ec);
            const bool isMaybeAlreadyDir = (ec || initialpath.ends_with('\\') || initialpath.ends_with('/') ||
                                            status.type() == fs::file_type::directory);
            // apparently "replace_filename" can throw, let's hope it doesn't :)
            encodedPath = fmt::format(
                "file://{}", UString{isMaybeAlreadyDir ? fspath.wstring() : fspath.replace_filename({}).wstring()});
            success = SDL_OpenURL(encodedPath.c_str());
        }
    }
    if(!success) {
        debugLog("Failed to open file URI {:s}: {:s}", encodedPath, SDL_GetError());
    }
}

void Environment::restoreWindow() {
    if(!SDL_RestoreWindow(m_window)) {
        debugLog("Failed to restore window: {:s}", SDL_GetError());
    }
    if(!SDL_RaiseWindow(m_window)) {
        debugLog("Failed to focus window: {:s}", SDL_GetError());
    }
    syncWindow();
}

void Environment::centerWindow() {
    syncWindow();
    const SDL_DisplayID di = SDL_GetDisplayForWindow(m_window);
    if(!di) {
        debugLog("Failed to obtain SDL_DisplayID for window: {:s}", SDL_GetError());
        return;
    }
    setWindowPos(SDL_WINDOWPOS_CENTERED_DISPLAY(di), SDL_WINDOWPOS_CENTERED_DISPLAY(di));
}

bool Environment::minimizeWindow() {
    // see SDL3/test/testautomation_video.c
    // if SDL is hardcoding not running tests on certain setups then i give up

    // TODO: make minimize-on-focus-lost an option in options menu and stop trying to be smart about it,
    // i don't think it's possible to cover all edge cases automatically

    if constexpr(Env::cfg(OS::WASM)) {
        m_bMinimizeSupported = false;
    }

    // also somehow disableFullscreen seems to go into an "infinite loop" on i3wm? so really try to avoid it...
    static bool hardcodedBrokenDesktopChecked{false};
    if(m_bMinimizeSupported && !hardcodedBrokenDesktopChecked &&
       ((m_bIsWayland || m_bIsX11) || (RuntimePlatform::current() & RuntimePlatform::WIN_WINE))) {
        auto desktop = getEnvVariable("XDG_CURRENT_DESKTOP");
        if(desktop.empty() && (RuntimePlatform::current() & RuntimePlatform::WIN_WINE)) {
            desktop = getEnvVariable("WINE_HOST_XDG_CURRENT_DESKTOP");
        }
        if(!desktop.empty() && (desktop == "sway" || desktop == "i3" || desktop == "i3wm")) {
            logIf(m_bEnvDebug, "Disabled minimize support due to XDG_CURRENT_DESKTOP: {}", desktop);
            m_bMinimizeSupported = false;
        }
        if(!getEnvVariable("I3SOCK").empty() || !getEnvVariable("SWAYSOCK").empty() ||
           !getEnvVariable("WINE_HOST_I3SOCK").empty() || !getEnvVariable("WINE_HOST_SWAYSOCK").empty()) {
            logIf(m_bEnvDebug, "Disabled minimize support due to being on sway/i3", desktop);
            m_bMinimizeSupported = false;
        }
        hardcodedBrokenDesktopChecked = true;
    }

    static int brokenMinimizeRepeatedSpamWorkaroundCounter{0};
    // a (harmless but wasteful of CPU) feedback loop can occur when alt tabbing for the first time and
    // calling SDL_SetWindowFullscreen(false)/SDL_SetWindowBordered(false), if they don't do anything
    // catch that here so it doesn't endlessly repeat
    if(brokenMinimizeRepeatedSpamWorkaroundCounter > 100) {
        m_bMinimizeSupported = false;
    }

    if(!m_bMinimizeSupported) {
        logIf(m_bEnvDebug, "Minimizing is unsupported, ignoring request.");
        return false;
    }

    if(winFullscreened()) {
        if(m_bRestoreFullscreen == true) {  // only increment the check if we're being called redundantly
            brokenMinimizeRepeatedSpamWorkaroundCounter++;
        }
        m_bRestoreFullscreen = true;
        disableFullscreen();
    } else {
        brokenMinimizeRepeatedSpamWorkaroundCounter = 0;
    }

    if(!SDL_MinimizeWindow(m_window)) {
        debugLog("Failed to minimize window: {:s}", SDL_GetError());
    }
    return true;
}

void Environment::maximizeWindow() {
    if(!SDL_MaximizeWindow(m_window)) {
        debugLog("Failed to maximize window: {:s}", SDL_GetError());
    }
}

void Environment::enableFullscreen() {
    // NOTE: "fake" fullscreen since we don't want a videomode change
    if constexpr(Env::cfg(OS::WASM)) {
        SDL_SetWindowFillDocument(m_window, true);
    }

    // some weird hack that apparently makes this behave better on macos?
    SDL_SetWindowFullscreenMode(m_window, nullptr);
    if(!SDL_SetWindowFullscreen(m_window, true)) {
        debugLog("Failed to enable fullscreen: {:s}", SDL_GetError());
    } else {
        SDL_SetWindowBordered(m_window, false);
    }
}

void Environment::disableFullscreen() {
    if(!SDL_SetWindowFullscreen(m_window, false)) {
        debugLog("Failed to disable fullscreen: {:s}", SDL_GetError());
    } else {
        SDL_SetWindowBordered(m_window, true);
    }

    if constexpr(Env::cfg(OS::WASM)) {
        SDL_SetWindowFillDocument(m_window, false);
    }
}

void Environment::setWindowTitle(const UString &title) { SDL_SetWindowTitle(m_window, title.toUtf8()); }

void Environment::syncWindow() { SDL_SyncWindow(m_window); }

bool Environment::setWindowPos(int x, int y) { return SDL_SetWindowPosition(m_window, x, y); }

bool Environment::setWindowSize(int width, int height) { return SDL_SetWindowSize(m_window, width, height); }

// NOTE: the SDL header states:
// "You can't change the resizable state of a fullscreen window."
void Environment::setWindowResizable(bool resizable) {
    if(m_bIsKMSDRM) {
        return;
    }
    if(!SDL_SetWindowResizable(m_window, resizable)) {
        debugLog("Failed to set window {:s} (currently {:s}): {:s}", resizable ? "resizable" : "non-resizable",
                 winResizable() ? "resizable" : "non-resizable", SDL_GetError());
    }
}

void Environment::setMonitor(int monitor) {
    if(monitor == 0 || monitor == getMonitor()) return centerWindow();

    bool success = false;

    if(!m_mMonitors.contains(monitor))  // try force reinit to check for new monitors
        initMonitors(true);
    if(m_mMonitors.contains(monitor)) {
        // SDL: "If the window is in an exclusive fullscreen or maximized state, this request has no effect."
        if(winFullscreened()) {
            disableFullscreen();
            syncWindow();
            success = setWindowPos(SDL_WINDOWPOS_CENTERED_DISPLAY(monitor), SDL_WINDOWPOS_CENTERED_DISPLAY(monitor));
            syncWindow();
            enableFullscreen();
        } else
            success = setWindowPos(SDL_WINDOWPOS_CENTERED_DISPLAY(monitor), SDL_WINDOWPOS_CENTERED_DISPLAY(monitor));

        if(!success)
            debugLog("WARNING: failed to setMonitor({:d}), centering instead. SDL error: {:s}", monitor,
                     SDL_GetError());
        else if(!(success = (monitor == getMonitor())))
            debugLog("WARNING: setMonitor({:d}) didn't actually change the monitor, centering instead.", monitor);
    } else
        debugLog("WARNING: tried to setMonitor({:d}) to invalid monitor, centering instead", monitor);

    if(!success) centerWindow();

    cv::monitor.setValue(getMonitor(), false);
}

HWND Environment::getHwnd() const {
    HWND hwnd = nullptr;
#if defined(MCENGINE_PLATFORM_WINDOWS)
    hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(m_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if(!hwnd) debugLog("(Windows) hwnd is null! SDL: {:s}", SDL_GetError());
#elif defined(__APPLE__)
    hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(m_window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if(!hwnd) debugLog("(OSX) hwnd is null! SDL: {:s}", SDL_GetError());
#elif defined(MCENGINE_PLATFORM_LINUX)
    if(SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0) {
        auto *xdisplay = (Display *)SDL_GetPointerProperty(SDL_GetWindowProperties(m_window),
                                                           SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        auto xwindow =
            (Window)SDL_GetNumberProperty(SDL_GetWindowProperties(m_window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
        if(xdisplay && xwindow)
            hwnd = (HWND)xwindow;
        else
            debugLog("(X11) no display/no surface! SDL: {:s}", SDL_GetError());
    } else if(SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
        struct wl_display *display = (struct wl_display *)SDL_GetPointerProperty(
            SDL_GetWindowProperties(m_window), SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        struct wl_surface *surface = (struct wl_surface *)SDL_GetPointerProperty(
            SDL_GetWindowProperties(m_window), SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        if(display && surface)
            hwnd = (HWND)surface;
        else
            debugLog("(Wayland) no display/no surface! SDL: {:s}", SDL_GetError());
    }
#endif

    return hwnd;
}

const std::unordered_map<unsigned int, McRect> &Environment::getMonitors() const {
    if(m_mMonitors.size() < 1)  // lazy init
        initMonitors();
    return m_mMonitors;
}

int Environment::getMonitor() const {
    const int display = static_cast<int>(SDL_GetDisplayForWindow(m_window));
    return display == 0 ? -1 : display;  // 0 == invalid, according to SDL
}

McRect Environment::getDesktopRect() const { return {{}, getNativeScreenSize()}; }

McRect Environment::getWindowRect() const { return {getWindowPos(), getWindowSize()}; }

bool Environment::isPointValid(vec2 point) const {  // whether an x,y coordinate lands on an actual display
    if(m_mMonitors.size() < 1) initMonitors();
    // check for the trivial case first
    const bool withinMinMaxBounds = m_fullDesktopBoundingBox.contains(point);
    if(!withinMinMaxBounds) {
        return false;
    }
    // if it's within the full min/max bounds, make sure it actually lands inside of a display rect within the
    // coordinate space (not in some empty space between/around, like with different monitor orientations)
    for(const auto &[_, dp] : m_mMonitors) {
        if(dp.contains(point)) return true;
    }
    return false;
}

int Environment::getDPI() const {
    if(m_bDPIOverride) {
        return 96;
    }

    float dpi = m_fDisplayScale * 96;

    return std::clamp<int>((int)dpi, 96, 96 * 2);  // sanity clamp
}

float Environment::getPixelDensity() const {
    if(m_bDPIOverride) {
        return 1.f;
    }

    return m_fPixelDensity;
}

void Environment::setCursor(CURSORTYPE cur) {
    if(!m_cursorIcons[0]) initCursors();
    if(m_cursorType != cur && cur >= CURSORTYPE::CURSOR_NORMAL && cur < CURSORTYPE::CURSORTYPE_MAX) {
        m_cursorType = cur;
        SDL_SetCursor(m_cursorIcons[(size_t)m_cursorType]);  // does not make visible if the cursor isn't visible
    }
}

void Environment::setRawMouseInput(bool raw) {
    if(raw == SDL_GetWindowRelativeMouseMode(m_window)) {
        // nothing to do
        goto done;
    }

    if(!raw && mouse) {
        // need to manually set the cursor position if we're disabling raw input
        setOSMousePos(mouse->getRealPos());
    }

    if(!SDL_SetWindowRelativeMouseMode(m_window, raw)) {
        debugLog("FIXME (handle error): SDL_SetWindowRelativeMouseMode failed: {:s}", SDL_GetError());
        raw = !raw;
    }

done:
    if(raw && !!(m_winflags & WinFlags::F_MOUSE_GRABBED)) {
        // release grab if we enabled raw input
        SDL_SetWindowMouseGrab(m_window, false);
    }

    // update MOUSE_RELATIVE_MODE flags
    m_winflags = static_cast<WinFlags>(SDL_GetWindowFlags(m_window));
}

void Environment::setRawKeyboardInput(bool raw) {
    if constexpr(!Env::cfg(OS::WINDOWS)) return;  // does not exist

    m_bRawKB = raw;

    SDL_SetHint(SDL_HINT_WINDOWS_RAW_KEYBOARD, raw ? "1" : "0");

    // different code paths for enabled/disabled, so update that here
    setWindowsKeyDisabled(m_bWinKeyDisabled);
}

bool Environment::isCursorVisible() const { return SDL_CursorVisible(); }

void Environment::setCursorVisible(bool visible) {
    if(visible) {
        m_bHideCursorPending = false;
        // disable rawinput (allow regular mouse movement)
        setRawMouseInput(false);
        SDL_SetWindowMouseGrab(m_window, false);  // release grab
        SDL_ShowCursor();
    } else {
        // wait for cursor to enter the window before hiding (during event collection)
        if(!m_bIsCursorInsideWindow) {
            m_bHideCursorPending = true;
            return;
        }
        m_bHideCursorPending = false;
        if(!cv::debug_draw_hardware_cursor.getBool()) {
            SDL_HideCursor();
        }
        setCursor(CURSORTYPE::CURSOR_NORMAL);

        if(mouse && mouse->isRawInputWanted()) {  // re-enable rawinput
            setRawMouseInput(true);
        } else if(isCursorClipped()) {
            // regrab if clipped
            SDL_SetWindowMouseGrab(m_window, true);
        }
    }
}

void Environment::setCursorClip(bool clip, McRect rect) {
    m_cursorClipRect = rect;
    if(clip) {
        const SDL_Rect sdlClip = McRectToSDLRect(rect);
        SDL_SetWindowMouseRect(m_window, &sdlClip);
        if(!(mouse && mouse->isRawInputWanted())) {
            // only grab if rawinput is disabled, we clip manually if rawinput is enabled
            SDL_SetWindowMouseGrab(m_window, true);
        }
        m_bCursorClipped = true;
    } else {
        m_bCursorClipped = false;
        SDL_SetWindowMouseRect(m_window, nullptr);
        SDL_SetWindowMouseGrab(m_window, false);
    }
}

void Environment::setOSMousePos(vec2 pos) {
    SDL_WarpMouseInWindow(m_window, pos.x, pos.y);
    m_vLastAbsMousePos = pos;
}

UString Environment::keyCodeToString(SCANCODE keyCode) const {
    const char *name = SDL_GetScancodeName((SDL_Scancode)keyCode);
    if(name == nullptr)
        return fmt::format("{}", keyCode);
    else {
        UString uName = UString(name);
        if(uName.length() < 1)
            return fmt::format("{}", keyCode);
        else
            return uName;
    }
}

bool Environment::setWindowsKeyDisabled(bool disable) {
    m_bWinKeyDisabled = disable;
    if constexpr(!Env::cfg(OS::WINDOWS)) {
        // grabbing keyboard is the only way to do this outside of windows
        m_bWinKeyDisabled = SDL_SetWindowKeyboardGrab(m_window, disable) && disable;
    } else {
        SDL_SetHint(SDL_HINT_WINDOWS_RAW_KEYBOARD_EXCLUDE_HOTKEYS, disable ? "1" : "0");
        if(m_bRawKB) {
            // always ungrab keyboard if we're using raw keyboard input (causes strange issues and isn't required)
            SDL_SetWindowKeyboardGrab(m_window, false);
        } else {
            // if we're not using raw input, grab the keyboard to disable windows keys
            m_bWinKeyDisabled = SDL_SetWindowKeyboardGrab(m_window, disable) && disable;
        }
    }

    return m_bWinKeyDisabled;
}

void Environment::listenToTextInput(bool listen) {
    m_bShouldListenToTextInput = listen;
    if(cv::use_ime.getBool()) {
        listen ? SDL_StartTextInput(m_window) : SDL_StopTextInput(m_window);
    } else if(!SDL_TextInputActive(m_window)) {
        // always keep text input active if we're not allowing IME events
        SDL_StartTextInput(m_window);
    }
}

//******************************//
//	internal helpers/callbacks  //
//******************************//

void Environment::onDPIChange() {
    const float oldDispScale = m_fDisplayScale;
    const float oldPixelDensity = m_fPixelDensity;
    m_fDisplayScale = SDL_GetWindowDisplayScale(m_window);
    m_fPixelDensity = SDL_GetWindowPixelDensity(m_window);
    if(m_engine && ((oldDispScale != m_fDisplayScale) || (oldPixelDensity != m_fPixelDensity))) {
        m_engine->onDPIChange();
    }
}

void Environment::onDebugDrawHardwareCursorChange(float newValue) {
    const bool enable = !!static_cast<int>(newValue);
    SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_CURSOR_VISIBLE, enable ? "1" : "0", SDL_HINT_NORMAL);

    if(enable) {
        SDL_ShowCursor();
    } else if(isOSMouseInputRaw() || isMouseInputGrabbed()) {
        SDL_HideCursor();
    }
}

void Environment::onUseIMEChange(float newValue) {
    const bool enable = !!static_cast<int>(newValue);
    SDL_SetEventEnabled(SDL_EVENT_TEXT_EDITING_CANDIDATES, enable);
    SDL_SetEventEnabled(SDL_EVENT_TEXT_EDITING, enable);
    if(enable) {
        // use OS IME input
        SDL_SetHintWithPriority(SDL_HINT_IME_IMPLEMENTED_UI, "none", SDL_HINT_NORMAL);
    } else {
        // tell SDL we're "handling it ourselves" so it doesn't pop up an OS IME input window, if we're disabling IME
        SDL_SetHintWithPriority(SDL_HINT_IME_IMPLEMENTED_UI, "candidates,composition", SDL_HINT_NORMAL);
    }
    listenToTextInput(enable && m_bShouldListenToTextInput);
}

// called by event loop on display or window events
void Environment::updateWindowSizeCache() {
    int width{320}, height{240};
    auto func = m_bDPIOverride ? SDL_GetWindowSize : SDL_GetWindowSizeInPixels;
    if(!func(m_window, &width, &height)) {
        debugLog("Failed to get window size (returning cached {},{}): {:s}", m_vLastKnownWindowSize.x,
                 m_vLastKnownWindowSize.y, SDL_GetError());
    } else {
        m_vLastKnownWindowSize = vec2{static_cast<float>(width), static_cast<float>(height)};
    }
}

// called by event loop on display or window events
void Environment::updateWindowStateCache() {
    // update window flags
    assert(m_window);
    m_winflags = static_cast<WinFlags>(SDL_GetWindowFlags(m_window));

    // update window pos
    {
        int x{0}, y{0};
        if(!SDL_GetWindowPosition(m_window, &x, &y)) {
            debugLog("Failed to get window position (cached {},{}): {:s}", m_vLastKnownWindowPos.x,
                     m_vLastKnownWindowPos.y, SDL_GetError());
        } else {
            m_vLastKnownWindowPos = vec2{static_cast<float>(x), static_cast<float>(y)};
        }
    }

    // update window size (separated out for live resize callback)
    updateWindowSizeCache();

    // update display rect
    {
        bool found = false;
        if(const SDL_DisplayID di = SDL_GetDisplayForWindow(m_window)) {
            const float scale = getPixelDensity();
            // fullscreen is currently buggy on mac, don't make other platforms shittier just because macos is finnicky
            const bool useWindowedBounds = Env::cfg(OS::MAC);  // || winFullscreened();
            if(useWindowedBounds) {
                // GetDisplayUsableBounds in windowed
                SDL_Rect bounds{};
                if(SDL_GetDisplayUsableBounds(di, &bounds)) {
                    m_vLastKnownNativeScreenSize =
                        vec2{static_cast<float>(bounds.w), static_cast<float>(bounds.h)} * scale;
                    found = true;
                }
            }

            // GetDesktopDisplayMode should return the actual, full resolution
            if(!found) {
                if(const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(di)) {
                    m_vLastKnownNativeScreenSize = vec2{static_cast<float>(dm->w), static_cast<float>(dm->h)} * scale;
                    found = true;
                }
            }
        }

        // fallback
        if(!found) {
            m_vLastKnownNativeScreenSize = getWindowSize();
        }
    }
}

std::string Environment::windowFlagsDbgStr() const {
    std::string ret;
    using enum WinFlags;
    if(!!(m_winflags & F_FULLSCREEN)) ret += "FULLSCREEN;";
    if(!!(m_winflags & F_OPENGL)) ret += "OPENGL;";
    if(!!(m_winflags & F_OCCLUDED)) ret += "OCCLUDED;";
    if(!!(m_winflags & F_HIDDEN)) ret += "HIDDEN;";
    if(!!(m_winflags & F_BORDERLESS)) ret += "BORDERLESS;";
    if(!!(m_winflags & F_RESIZABLE)) ret += "RESIZABLE;";
    if(!!(m_winflags & F_MINIMIZED)) ret += "MINIMIZED;";
    if(!!(m_winflags & F_MAXIMIZED)) ret += "MAXIMIZED;";
    if(!!(m_winflags & F_MOUSE_GRABBED)) ret += "MOUSE_GRABBED;";
    if(!!(m_winflags & F_INPUT_FOCUS)) ret += "INPUT_FOCUS;";
    if(!!(m_winflags & F_MOUSE_FOCUS)) ret += "MOUSE_FOCUS;";
    if(!!(m_winflags & F_EXTERNAL)) ret += "EXTERNAL;";
    if(!!(m_winflags & F_MODAL)) ret += "MODAL;";
    if(!!(m_winflags & F_HIGH_PIXEL_DENSITY)) ret += "HIGH_PIXEL_DENSITY;";
    if(!!(m_winflags & F_MOUSE_CAPTURE)) ret += "MOUSE_CAPTURE;";
    if(!!(m_winflags & F_MOUSE_RELATIVE_MODE)) ret += "MOUSE_RELATIVE_MODE;";
    if(!!(m_winflags & F_ALWAYS_ON_TOP)) ret += "ALWAYS_ON_TOP;";
    if(!!(m_winflags & F_UTILITY)) ret += "UTILITY;";
    if(!!(m_winflags & F_TOOLTIP)) ret += "TOOLTIP;";
    if(!!(m_winflags & F_POPUP_MENU)) ret += "POPUP_MENU;";
    if(!!(m_winflags & F_KEYBOARD_GRABBED)) ret += "KEYBOARD_GRABBED;";
    if(!!(m_winflags & F_VULKAN)) ret += "VULKAN;";
    if(!!(m_winflags & F_METAL)) ret += "METAL;";
    if(!!(m_winflags & F_TRANSPARENT)) ret += "TRANSPARENT;";
    if(!!(m_winflags & F_NOT_FOCUSABLE)) ret += "NOT_FOCUSABLE;";
    if(!ret.empty()) ret.pop_back();
    return ret;
}

// convar callback
void Environment::onLogLevelChange(float newval) {
    const bool enable = !!static_cast<int>(newval);
    if(enable && !m_bEnvDebug) {
        envDebug(true);
        SDL_SetLogPriorities(SDL_LOG_PRIORITY_TRACE);
    } else if(!enable && m_bEnvDebug) {
        envDebug(false);
        SDL_ResetLogPriorities();
    }
}

SDL_Rect Environment::McRectToSDLRect(const McRect &mcrect) noexcept {
    return {.x = static_cast<int>(mcrect.getX()),
            .y = static_cast<int>(mcrect.getY()),
            .w = static_cast<int>(mcrect.getWidth()),
            .h = static_cast<int>(mcrect.getHeight())};
}

McRect Environment::SDLRectToMcRect(const SDL_Rect &sdlrect) noexcept {
    return {static_cast<float>(sdlrect.x), static_cast<float>(sdlrect.y), static_cast<float>(sdlrect.w),
            static_cast<float>(sdlrect.h)};
}

vec2 Environment::getAsyncMousePos() const {
    float x{}, y{};
    SDL_GetGlobalMouseState(&x, &y);
    return {x, y};
}

Environment::CursorPosition Environment::consumeCursorPositionCache() {
    float xRel{0.f}, yRel{0.f};
    float x{m_vLastAbsMousePos.x}, y{m_vLastAbsMousePos.y};

    // this gets zeroed on every call to it, which is why this function "consumes" data
    // both of these calls are only updated with the last SDL_PumpEvents call
    SDL_GetRelativeMouseState(&xRel, &yRel);
    SDL_GetMouseState(&x, &y);

    vec2 newRel = {xRel, yRel};
    vec2 newAbs = {x, y};

    const bool hadRelative = vec::length(newRel) != 0.f;
    if(hadRelative) {
        m_bForceAbsCursor = false;
    }

    // these pen events are manually tracked and updated in our event loop
    if(m_vLastAbsPenPos != m_vCurrentAbsPenPos) {
        // if SDL's relative pen motion tracking isn't working for whatever reason, and we had pen motion events, then use that
        // otherwise trust what SDL is giving to us
        if(!hadRelative) {
            m_bForceAbsCursor = true;
            newRel = m_vCurrentAbsPenPos - m_vLastAbsPenPos;
            newAbs = m_vCurrentAbsPenPos;
        }

        // reset
        m_vLastAbsPenPos = m_vCurrentAbsPenPos;
    }

    const float scaleFactor = m_bForceAbsCursor ? 1.f : getPixelDensity();
    // if we're in raw input or forcing absolute cursor then SDL isn't clipping the motion for us
    const bool needsClipping = m_bForceAbsCursor || isOSMouseInputRaw();

    return CursorPosition{.rel = newRel, .abs = newAbs, .scale = scaleFactor, .needsClipping = needsClipping};
}

void Environment::initCursors() {
    m_cursorIcons = {{
        SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT),     /* CURSOR_NORMAL */
        SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT),        /* CURSOR_WAIT */
        SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE),   /* CURSOR_SIZE_H */
        SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE),   /* CURSOR_SIZE_V */
        SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NESW_RESIZE), /* CURSOR_SIZE_HV */
        SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NWSE_RESIZE), /* CURSOR_SIZE_VH */
        SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT),        /* CURSOR_TEXT */
    }};
}

void Environment::initMonitors(bool force) const {
    if(!force && !m_mMonitors.empty())
        return;
    else if(force)  // refresh
        m_mMonitors.clear();

    int count = -1;
    SDL_DisplayID *displays = SDL_GetDisplays(&count);

    m_fullDesktopBoundingBox = {};  // the min/max coordinates, for "valid point" lookups (checked first before
                                    // iterating through actual monitor rects)
    for(int i = 0; i < count; i++) {
        const SDL_DisplayID di = displays[i];

        if(di == 0) {  // should be impossible, but safeguard it anyways
            count--;
            continue;
        }

        McRect displayRect{};
        vec2 size{0.f};
        SDL_Rect sdlDisplayRect{};

        if(!SDL_GetDisplayBounds(di, &sdlDisplayRect)) {
            // fallback
            size = vec2{static_cast<float>(SDL_GetDesktopDisplayMode(di)->w),
                        static_cast<float>(SDL_GetDesktopDisplayMode(di)->h)};
            displayRect = McRect{{}, size};
            // expand the display bounds, we just have to assume that the displays are placed left-to-right, with Y
            // coordinates at 0 (in this fallback path)
            if(size.y > m_fullDesktopBoundingBox.getHeight()) {
                m_fullDesktopBoundingBox.setHeight(size.y);
            }
            m_fullDesktopBoundingBox.setWidth(m_fullDesktopBoundingBox.getWidth() + size.x);
        } else {
            displayRect = SDLRectToMcRect(sdlDisplayRect);
            size = displayRect.getSize();
            // otherwise we can get the min/max bounding box accurately
            m_fullDesktopBoundingBox = m_fullDesktopBoundingBox.Union(displayRect);
        }
        m_mMonitors.try_emplace(di, displayRect);
    }

    if(count < 1) {
        debugLog("WARNING: No monitors found! Adding default monitor ...");
        const vec2 windowSize = getWindowSize();
        m_mMonitors.try_emplace(1, McRect{{}, windowSize});
    }

    // make sure this is also valid
    if(m_fullDesktopBoundingBox.getSize() == vec2{0, 0}) {
        m_fullDesktopBoundingBox = getWindowRect();
    }

    SDL_free(displays);
}

// TODO: filter?
void Environment::sdlFileDialogCallback(void *userdata, const char *const *filelist, int /*filter*/) noexcept {
    if(!userdata) return;

    std::vector<UString> results;

    if(filelist) {
        for(const char *const *curr = filelist; *curr; curr++) {
            results.emplace_back(*curr);
        }
    } else if(strncmp(SDL_GetError(), "dialogg", sizeof("dialogg") - 1) == 0) {
        // expect to be called by fallback path next... weird stuff, seems like an SDL bug? (double calling callback)
        return;
    }

    SDL_SetError("cleared error in file dialog callback");

    auto *callback = static_cast<FileDialogCallback *>(userdata);

    // call the callback
    (*callback)(results);

    // callback no longer needed
    delete callback;
}

// folder = true means return the canonical filesystem path to the folder containing the given path
//			if the path is already a folder, just return it directly
// folder = false means to strip away the file path separators from the given path and return just the filename itself
std::string Environment::getThingFromPathHelper(std::string_view path, bool folder) noexcept {
    if(path.empty()) return std::string{path};
    namespace fs = std::filesystem;

    std::string retPath{path};

    const bool longPath = Env::cfg(OS::WINDOWS) && (retPath.starts_with(R"(\\?\)") || retPath.starts_with(R"(\\.\)"));
    const char prefSep = longPath ? '\\' : '/';
    const char otherSep = longPath ? '/' : '\\';

    // find the last path separator (either / or \)
    auto lastSlash = retPath.find_last_of(prefSep);
    if(lastSlash == std::string::npos) lastSlash = retPath.find_last_of(otherSep);

    if(folder) {
        // if path ends with separator, it's already a directory
        const bool endsWithSeparator = retPath.back() == prefSep || retPath.back() == otherSep;
        UString ustrPath;

        std::error_code ec;
        // const auto fsPath = File::getFsPath(retPath); // mingw-gcc bugs cause fs::canonical to just go crazy
        auto abs_path = fs::canonical(UString{retPath}.plat_str(), ec);

        if(!ec)  // canonical path found
        {
            auto status = fs::status(abs_path, ec);
            // if it's already a directory or it doesn't have a parent path then just return it directly
            if(ec || status.type() == fs::file_type::directory || !abs_path.has_parent_path())
                ustrPath = abs_path.string();
            // else return the parent directory for the file
            else if(abs_path.has_parent_path() && !abs_path.parent_path().empty())
                ustrPath = abs_path.parent_path().string();
        } else if(!endsWithSeparator)  // canonical failed, handle manually (if it's not already a directory)
        {
            if(lastSlash != std::string::npos)  // return parent
                ustrPath = retPath.substr(0, lastSlash);
            else  // no separators found, just use ./
                ustrPath = fmt::format(".{}{}", prefSep, retPath);
        }

        if(!ustrPath.isEmpty()) {
            retPath = ustrPath.utf8View();
        }

        // make sure whatever we got now ends with a slash
        if(retPath.back() != prefSep && retPath.back() != otherSep) {
            retPath = retPath + prefSep;
        }
    } else if(lastSlash != std::string::npos)  // just return the file
    {
        retPath = retPath.substr(lastSlash + 1);
    }
    // else: no separators found, entire path is the filename

    return retPath;
}
