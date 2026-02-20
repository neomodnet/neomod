// Copyright (c) 2015-2018, PG & 2025, WH, All rights reserved.
#pragma once

#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "BaseEnvironment.h"

#include "UString.h"
#include "Cursors.h"
#include "KeyboardEvent.h"
#include "Rect.h"

#include <unordered_map>
#include <vector>
#include <filesystem>
#include <functional>

typedef uint32_t SDL_WindowID;
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Cursor SDL_Cursor;
typedef struct SDL_Environment SDL_Environment;
typedef struct SDL_Rect SDL_Rect;

class Graphics;
class UString;
class Engine;
namespace Mc {
struct AppDescriptor;
void initEnvBlock();
}  // namespace Mc

class Environment;
extern Environment *env;

// clang-format off
// copied from SDL3/SDL_video.h::SDL_WindowFlags
enum class WinFlags : uint64_t {
    F_FULLSCREEN =          0x0000000000000001, /**< window is in fullscreen mode */
    F_OPENGL =              0x0000000000000002, /**< window usable with OpenGL context */
    F_OCCLUDED =            0x0000000000000004, /**< window is occluded */
    F_HIDDEN =              0x0000000000000008, /**< window is neither mapped onto the desktop nor shown in the taskbar/dock/window list; ShowWindow() is required for it to become visible */
    F_BORDERLESS =          0x0000000000000010, /**< no window decoration */
    F_RESIZABLE =           0x0000000000000020, /**< window can be resized */
    F_MINIMIZED =           0x0000000000000040, /**< window is minimized */
    F_MAXIMIZED =           0x0000000000000080, /**< window is maximized */
    F_MOUSE_GRABBED =       0x0000000000000100, /**< window has grabbed mouse input */
    F_INPUT_FOCUS =         0x0000000000000200, /**< window has input focus */
    F_MOUSE_FOCUS =         0x0000000000000400, /**< window has mouse focus */
    F_EXTERNAL =            0x0000000000000800, /**< window not created by SDL */
    F_MODAL =               0x0000000000001000, /**< window is modal */
    F_HIGH_PIXEL_DENSITY =  0x0000000000002000, /**< window uses high pixel density back buffer if possible */
    F_MOUSE_CAPTURE =       0x0000000000004000, /**< window has mouse captured (unrelated to MOUSE_GRABBED) */
    F_MOUSE_RELATIVE_MODE = 0x0000000000008000, /**< window has relative mode enabled */
    F_ALWAYS_ON_TOP =       0x0000000000010000, /**< window should always be above others */
    F_UTILITY =             0x0000000000020000, /**< window should be treated as a utility window, not showing in the task bar and window list */
    F_TOOLTIP =             0x0000000000040000, /**< window should be treated as a tooltip and does not get mouse or keyboard focus, requires a parent window */
    F_POPUP_MENU =          0x0000000000080000, /**< window should be treated as a popup menu, requires a parent window */
    F_KEYBOARD_GRABBED =    0x0000000000100000, /**< window has grabbed keyboard input */
    F_FILL_DOCUMENT =       0x0000000000200000, /**< window is in fill-document mode (Emscripten only), since SDL 3.4.0 */
    F_VULKAN =              0x0000000010000000, /**< window usable for Vulkan surface */
    F_METAL =               0x0000000020000000, /**< window usable for Metal view */
    F_TRANSPARENT =         0x0000000040000000, /**< window with transparent buffer */
    F_NOT_FOCUSABLE =       0x0000000080000000  /**< window should not be focusable */
};
MAKE_FLAG_ENUM(WinFlags);
// clang-format on

class Environment {
    NOCOPY_NOMOVE(Environment)
   public:
    struct Interop {
        NOCOPY_NOMOVE(Interop)
       public:
        Interop() = delete;
        Interop(Environment *env_ptr) : env_p(env_ptr) {}
        virtual ~Interop() { env_p = nullptr; }
        bool handle_cmdline_args() { return handle_cmdline_args(this->env_p->getCommandLine()); }

        virtual bool handle_cmdline_args(const std::vector<std::string> & /*args*/) { return false; }
        virtual bool handle_osk(const char * /*osk_path*/) { return false; }
        virtual bool handle_osz(const char * /*osz_path*/) { return false; }
        virtual void setup_system_integrations() {}

        Environment *env_p;
    };

   protected:
    friend struct Interop;
    std::unique_ptr<Interop> m_interop;

   public:
    Environment(const Mc::AppDescriptor &appDesc, std::unordered_map<std::string, std::optional<std::string>> argMap,
                std::vector<std::string> cmdlineVec);
    virtual ~Environment();

    void update();

    // engine/factory
    Graphics *createRenderer();
#ifdef MCENGINE_FEATURE_DIRECTX11
    [[nodiscard]] inline bool usingDX11() const { return m_renderer == RuntimeRenderer::DX11; }
#else
    [[nodiscard]] constexpr forceinline bool usingDX11() const { return false; }
#endif
#if defined(MCENGINE_FEATURE_OPENGL) || defined(MCENGINE_FEATURE_GLES32)
    [[nodiscard]] inline bool usingGL() const {
        return m_renderer == RuntimeRenderer::GL || m_renderer == RuntimeRenderer::GLES;
    }
#else
    [[nodiscard]] constexpr forceinline bool usingGL() const { return false; }
#endif
#ifdef MCENGINE_FEATURE_SDLGPU
    [[nodiscard]] inline bool usingSDLGPU() const { return m_renderer == RuntimeRenderer::SDLGPU; }
#else
    [[nodiscard]] constexpr forceinline bool usingSDLGPU() const { return false; }
#endif

    // system
    void shutdown();
    void restart();
    [[nodiscard]] inline bool isRunning() const { return m_bRunning; }
    [[nodiscard]] inline bool isRestartScheduled() const { return m_bIsRestartScheduled; }
    [[nodiscard]] inline bool isHeadless() const { return m_bHeadless; }
    [[nodiscard]] inline Interop &getEnvInterop() { return *m_interop; }

    // resolved and cached at early startup with argv[0]
    // contains the full canonical path to the current exe
    static const std::string &getPathToSelf(const char *argv0 = nullptr);

    // i.e. getenv()
    static std::string getEnvVariable(std::string_view varToQuery) noexcept;
    // i.e. setenv()
    static bool setEnvVariable(std::string_view varToSet, std::string_view varValue, bool overwrite = true) noexcept;
    // i.e. unsetenv()
    static bool unsetEnvVariable(std::string_view varToUnset) noexcept;

    static const std::string &getExeFolder();

    static void openURLInDefaultBrowser(std::string_view url) noexcept;

    [[nodiscard]] inline const std::unordered_map<std::string, std::optional<std::string>> &getLaunchArgs() const {
        return m_mArgMap;
    }
    [[nodiscard]] inline const std::vector<std::string> &getCommandLine() const { return m_vCmdLine; }

    // user
    [[nodiscard]] const UString &getUsername() const noexcept;
    [[nodiscard]] const std::string &getUserDataPath() const noexcept;
    [[nodiscard]] const std::string &getLocalDataPath() const noexcept;
    [[nodiscard]] const std::string &getCacheDir() const noexcept;

    // file IO
    [[nodiscard]] static bool fileExists(std::string &filename) noexcept;  // passthroughs to McFile
    [[nodiscard]] static bool directoryExists(std::string &directoryName) noexcept;
    [[nodiscard]] static bool fileExists(std::string_view filename) noexcept;
    [[nodiscard]] static bool directoryExists(std::string_view directoryName) noexcept;

    // NOTE: createDirectory creates recursively
    static bool createDirectory(const std::string &directoryName) noexcept;
    // be extremely careful with this!
    static bool deletePathsRecursive(const std::string &path, int maxRecursionLevels = 1) noexcept;

    static bool renameFile(const std::string &oldFileName, const std::string &newFileName) noexcept;
    static bool deleteFile(const std::string &filePath) noexcept;
    [[nodiscard]] static std::vector<std::string> getFilesInFolder(std::string_view folder) noexcept;
    [[nodiscard]] static std::vector<std::string> getFoldersInFolder(std::string_view folder) noexcept;
    [[nodiscard]] static std::vector<UString> getLogicalDrives();
    // returns an absolute (i.e. fully-qualified) filesystem path
    [[nodiscard]] static std::string getFolderFromFilePath(std::string_view filepath) noexcept;
    [[nodiscard]] static std::string getFileExtensionFromFilePath(std::string_view filepath) noexcept;
    [[nodiscard]] static std::string getFileNameFromFilePath(std::string_view filePath) noexcept;
    [[nodiscard]] static std::string normalizeDirectory(std::string dirPath) noexcept;
    [[nodiscard]] static bool isAbsolutePath(std::string_view filePath) noexcept;

    // URL-encodes a string, but keeps slashes intact (for file:/// URIs)
    [[nodiscard]] static std::string encodeStringToURI(std::string_view unencodedString) noexcept;

    // clipboard
    [[nodiscard]] const UString &getClipBoardText();
    void setClipBoardText(const UString &text);

    // dialogs & message boxes
    static void showDialog(const char *title, const char *message,
                           unsigned int flags = 0x00000010u /* SDL_MESSAGEBOX_ERROR */,
                           void /*SDL_Window*/ *modalWindow = nullptr);
    void showMessageInfo(const UString &title, const UString &message) const;
    void showMessageWarning(const UString &title, const UString &message) const;
    void showMessageError(const UString &title, const UString &message) const;
    void showMessageErrorFatal(const UString &title, const UString &message) const;

    using FileDialogCallback = std::function<void(const std::vector<UString> &paths)>;
    void openFileWindow(FileDialogCallback callback, const char *filetypefilters, std::string_view title,
                        std::string_view initialpath = "") const noexcept;
    void openFolderWindow(FileDialogCallback callback, std::string_view initialpath = "") const noexcept;
    void openFileBrowser(std::string_view initialpath) const noexcept;

    // window
    void restoreWindow();
    void centerWindow();
    bool minimizeWindow();  // if it returns false, minimize is not supported
    void maximizeWindow();
    void enableFullscreen();
    void disableFullscreen();
    void syncWindow();
    void setWindowTitle(const UString &title);
    bool setWindowPos(int x, int y);
    bool setWindowSize(int width, int height);
    void setWindowResizable(bool resizable);
    void setMonitor(int monitor);

    [[nodiscard]] HWND getHwnd() const;

    [[nodiscard]] constexpr float getDisplayRefreshRate() const { return m_fDisplayHz; }
    [[nodiscard]] constexpr float getDisplayRefreshTime() const { return m_fDisplayHzSecs; }

    [[nodiscard]] constexpr vec2 getWindowPos() const { return m_vLastKnownWindowPos; }
    [[nodiscard]] constexpr vec2 getWindowSize() const { return m_vLastKnownWindowSize; }
    [[nodiscard]] McRect getWindowRect() const;

    [[nodiscard]] int getMonitor() const;
    [[nodiscard]] const std::unordered_map<unsigned int, McRect> &getMonitors() const;

    [[nodiscard]] constexpr vec2 getNativeScreenSize() const { return m_vLastKnownNativeScreenSize; }
    [[nodiscard]] McRect getDesktopRect() const;

    [[nodiscard]] int getDPI() const;
    [[nodiscard]] float getPixelDensity() const;  // like DPI but more annoying
    [[nodiscard]] inline float getDPIScale() const { return (float)getDPI() / 96.0f; }

    [[nodiscard]] bool isPointValid(vec2 point) const;  // whether an x,y coordinate lands on an actual display

    // window state queries
    [[nodiscard]] constexpr bool winFullscreened() const {
        using enum WinFlags;
        using namespace flags::operators;

        if constexpr(Env::cfg(OS::WASM)) {
            // no point checking window size in wasm since it's always 100% of the page
            // ...and on firefox specifically, makes the game think it's fullscreened when it's not
            return m_bRestoreFullscreen || flags::has<F_FULLSCREEN>(m_winflags);
        }

        // we do not use "real" fullscreen mode, so maximized+borderless+unoccluded is the same as fullscreen
        // also return true if our window size is == the desktop res, i dont understand why sdl doesn't update this in tandem
        // also return true if we are pending a re-fullscreen, to avoid issues related to that and event ordering on alt-tab
        return m_bRestoreFullscreen || flags::has<F_FULLSCREEN>(m_winflags) ||
               (!flags::has<F_OCCLUDED>(m_winflags) &&
                ((getWindowSize() == getNativeScreenSize()) || flags::has<F_BORDERLESS | F_MAXIMIZED>(m_winflags)));
    }
    [[nodiscard]] constexpr bool winResizable() const { return flags::has<WinFlags::F_RESIZABLE>(m_winflags); }
    [[nodiscard]] constexpr bool winFocused() const {
        using namespace flags::operators;
        return flags::any<WinFlags::F_MOUSE_FOCUS | WinFlags::F_INPUT_FOCUS>(m_winflags);
    }
    [[nodiscard]] constexpr bool winMinimized() const { return flags::has<WinFlags::F_MINIMIZED>(m_winflags); }
    [[nodiscard]] constexpr bool winMaximized() const { return flags::has<WinFlags::F_MAXIMIZED>(m_winflags); }

    // mouse
    [[nodiscard]] constexpr bool isCursorInWindow() const { return m_bIsCursorInsideWindow; }
    [[nodiscard]] bool isCursorVisible() const;
    [[nodiscard]] constexpr bool isCursorClipped() const { return m_bCursorClipped; }
    [[nodiscard]] constexpr vec2 getMousePos() const { return m_vLastAbsMousePos; }
    [[nodiscard]] constexpr const McRect &getCursorClip() const { return m_cursorClipRect; }
    [[nodiscard]] constexpr CURSORTYPE getCursor() const { return m_cursorType; }
    [[nodiscard]] constexpr bool isOSMouseInputRaw() const {
        return !m_bForceAbsCursor && flags::has<WinFlags::F_MOUSE_RELATIVE_MODE>(m_winflags);
    }
    [[nodiscard]] constexpr bool isMouseInputGrabbed() const {
        return flags::has<WinFlags::F_MOUSE_GRABBED>(m_winflags);
    }

    void setCursor(CURSORTYPE cur);
    void setCursorVisible(bool visible);
    void setCursorClip(bool clip, McRect rect);
    void setRawMouseInput(bool raw);  // enable/disable OS-level rawinput

    void setOSMousePos(vec2 pos);
    inline void setOSMousePos(float x, float y) { setOSMousePos(vec2{x, y}); }

    // keyboard
    [[nodiscard]] UString keyCodeToString(SCANCODE keyCode) const;
    void listenToTextInput(bool listen);
    bool setWindowsKeyDisabled(bool disable);
    void setRawKeyboardInput(bool raw);  // enable/disable OS-level rawinput

    [[nodiscard]] constexpr bool isOSKeyboardInputRaw() const { return Env::cfg(OS::WINDOWS) && m_bRawKB; }
    [[nodiscard]] constexpr bool isKeyboardInputGrabbed() const {
        return flags::has<WinFlags::F_KEYBOARD_GRABBED>(m_winflags);
    }

    // debug
    [[nodiscard]] inline bool envDebug() const { return m_bEnvDebug; }

    // platform
    [[nodiscard]] constexpr bool isX11() const { return m_bIsX11; }
    [[nodiscard]] constexpr bool isKMSDRM() const { return m_bIsKMSDRM; }
    [[nodiscard]] constexpr bool isWayland() const { return m_bIsWayland; }

   protected:
    std::unordered_map<std::string, std::optional<std::string>> m_mArgMap;
    std::vector<std::string> m_vCmdLine;
    std::unique_ptr<Engine> m_engine;

    SDL_Window *m_window;
    SDL_WindowID m_windowID;
    std::string m_sdldriver;
    enum class RuntimeRenderer : uint8_t { GL, GLES, DX11, SDLGPU };
    RuntimeRenderer m_renderer;

    bool m_bRunning;
    bool m_bIsRestartScheduled;
    bool m_bHeadless;

    bool m_bRestoreFullscreen;
    bool m_bMinimizeSupported;

    // cache
    mutable UString m_sUsername;
    mutable std::string m_sProgDataPath;
    mutable std::string m_sAppDataPath;
    mutable std::string m_sCacheDir;

    // logging
    inline bool envDebug(bool enable) {
        m_bEnvDebug = enable;
        return m_bEnvDebug;
    }
    void onLogLevelChange(float newval);
    bool m_bEnvDebug;

    // monitors
    void initMonitors(bool force = false) const;

    // mutable due to lazy init
    mutable std::unordered_map<unsigned int, McRect> m_mMonitors;
    mutable McRect m_fullDesktopBoundingBox;

    float m_fDisplayHz;
    float m_fDisplayHzSecs;

    // window

    // updates m_winflags, m_vLastKnownWindowSize, m_vLastKnownWindowPos, m_vLastKnownNativeScreenSize
    // to be called during event collection on window/display events, to avoid expensive API calls
    void updateWindowStateCache();
    void updateWindowSizeCache();  // only updates window size (separated out for live resize callback)

    std::string windowFlagsDbgStr() const;
    void onDPIChange();

    WinFlags m_winflags{};  // initialized when window is created, updated on new window events in the event loop

    float m_fPixelDensity{1.f};
    float m_fDisplayScale{1.f};
    bool m_bDPIOverride;
    inline void onMonitorChange(float oldValue, float newValue) {
        if(oldValue != newValue) setMonitor(static_cast<int>(newValue));
    }

    // save the last position obtained from SDL so that we can return something sensible if the SDL API fails
    vec2 m_vLastKnownWindowSize{320.f, 240.f};
    vec2 m_vLastKnownWindowPos{};
    vec2 m_vLastKnownNativeScreenSize{320.f, 240.f};

    // mouse
    friend class Mouse;
    [[nodiscard]] vec2 getAsyncMousePos() const;  // debug

    struct CursorPosition {
        vec2 rel;     // relative *since last call*
        vec2 abs;     // mouse absolute
        float scale;  // unscaled from pixel density (seems macOS specific?) (TODO: must be a better way to do this...)
        // if the cursor is already clipped to the clip rectangle or if it needs to be clipped manually
        // (TODO: very ugly to be putting this here)
        bool needsClipping;
    };

    // enabled if we had pen events and no relative motion reported from SDL
    // disabled once we receive relative motion events from SDL again
    bool m_bForceAbsCursor{false};

    CursorPosition consumeCursorPositionCache();

    // allow Mouse to update the cached environment position post-sensitivity/clipping
    // the difference between setOSMousePos and this is that it doesn't actually warp the OS cursor
    inline void updateCachedMousePos(vec2 pos) { m_vLastAbsMousePos = pos; }

    // is used to track relative tablet motion, is zeroed after consumeMousePositionCache
    // on some platforms, SDL automatically tracks relative motion deltas from absolute pen motion, but not others...
    // so we'll do it manually in that case
    vec2 m_vCurrentAbsPenPos{0.f, 0.f};
    vec2 m_vLastAbsPenPos{0.f, 0.f};

    vec2 m_vLastAbsMousePos{0.f, 0.f};
    bool m_bIsCursorInsideWindow;
    bool m_bCursorClipped;
    bool m_bHideCursorPending;
    McRect m_cursorClipRect;
    CURSORTYPE m_cursorType;
    std::array<SDL_Cursor *, (size_t)CURSORTYPE::CURSORTYPE_MAX> m_cursorIcons;

    // keyboard
    inline void onRawKeyboardChange(float oldValue, float newValue) {
        if((oldValue > 0) != (newValue > 0)) {
            this->setRawKeyboardInput(newValue > 0);
        }
    }
    void onUseIMEChange(float newValue);
    void onDebugDrawHardwareCursorChange(float newValue);

    bool m_bShouldListenToTextInput;
    bool m_bRawKB;
    bool m_bWinKeyDisabled;

    // clipboard
    UString m_sCurrClipboardText;

    // misc platform
    bool m_bIsX11;
    bool m_bIsKMSDRM;
    bool m_bIsWayland;

    // process environment (declared here for restart access)
    friend void Mc::initEnvBlock();
    static SDL_Environment *s_sdlenv;

   private:
    // lazy inits
    void initCursors();

    // static callbacks/helpers
    static void sdlFileDialogCallback(void *userdata, const char *const *filelist, int filter) noexcept;

    static std::string getThingFromPathHelper(
        std::string_view path,
        bool folder) noexcept;  // code sharing for getFolderFromFilePath/getFileNameFromFilePath

    // internal path conversion helper, SDL_URLOpen needs a URL-encoded URI on Unix (because it goes to xdg-open)
    [[nodiscard]] static std::string filesystemPathToURI(const std::filesystem::path &path) noexcept;

    static SDL_Rect McRectToSDLRect(const McRect &mcrect) noexcept;
    static McRect SDLRectToMcRect(const SDL_Rect &sdlrect) noexcept;
};

#endif
