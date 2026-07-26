// Copyright (c) 2015-2018, PG & 2025, WH, All rights reserved.
#pragma once

#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "BaseEnvironment.h"

#include "Cursors.h"
#include "KeyboardEvent.h"
#include "Rect.h"
#include "StaticPImpl.h"
#include "Vectors.h"

#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>
#include <array>
#include <span>

typedef uint32_t SDL_WindowID;
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Cursor SDL_Cursor;
typedef struct SDL_Environment SDL_Environment;
typedef struct SDL_Rect SDL_Rect;

class Graphics;
class Engine;
namespace Mc {
struct AppDescriptor;
void initEnvBlock();
namespace LaunchArgs {
extern std::span<const std::string> get_array() noexcept;
}
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

enum class MouseButtonFlags : uint8_t;

class Environment {
    NOCOPY_NOMOVE(Environment)
   public:
    struct Interop {
        NOCOPY_NOMOVE(Interop)
       public:
        Interop() = delete;
        Interop(Environment *env_ptr) : env_p(env_ptr) {}
        virtual ~Interop() { env_p = nullptr; }
        // handle initial program startup args
        bool handle_cmdline_args() { return handle_cmdline_args(Mc::LaunchArgs::get_array()); }

        virtual bool handle_cmdline_args(std::span<const std::string> /*args*/) { return true; }
        virtual void setup_system_integrations() {}

        Environment *env_p;
    };

   protected:
    friend struct Interop;
    std::unique_ptr<Interop> m_interop;

   public:
    Environment(const Mc::AppDescriptor &appDesc);
    virtual ~Environment();

    void update();

    // engine/factory
    std::unique_ptr<Graphics> createRenderer();

    [[nodiscard]] constexpr forceinline bool usingNullGraphics() const {
        return m_renderer == RuntimeRenderer::NULLGRAPHICS;
    }
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
    // isUnset is an out variable, if the variable was unset it will be set to true
    [[nodiscard]] static std::string getEnvVariable(std::string_view varToQuery, bool *isUnset = nullptr) noexcept;
    // i.e. setenv()
    static bool setEnvVariable(std::string_view varToSet, std::string_view varValue, bool overwrite = true) noexcept;
    // i.e. unsetenv()
    static bool unsetEnvVariable(std::string_view varToUnset) noexcept;

    static const std::string &getExeFolder();

    void openURLInDefaultBrowser(std::string_view url, bool preventFocusSteal = false) noexcept;

    // user
    [[nodiscard]] std::string_view getUsername() const noexcept;
    [[nodiscard]] const std::string &getDefaultLocale() const noexcept;
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
    // deletes the directory at the given path: removes the files inside it, recurses into subdirectories
    // up to maxRecursionLevels deep, then removes the directory itself.
    // returns true if it ended up fully removed && it actually existed in the first place.
    static bool deletePathsRecursive(const std::string &path, int maxRecursionLevels = 1) noexcept;

    static bool renameFile(const std::string &oldFileName, const std::string &newFileName) noexcept;
    static bool deleteFile(const std::string &filePath) noexcept;
    [[nodiscard]] static std::vector<std::string> getFilesInFolder(std::string_view folder) noexcept;
    [[nodiscard]] static std::vector<std::string> getFoldersInFolder(std::string_view folder) noexcept;
    [[nodiscard]] static std::vector<std::string> getEntriesInFolder(std::string_view folder) noexcept;
    [[nodiscard]] static std::vector<std::string> getLogicalDrives() noexcept;
    // returns an absolute (i.e. fully-qualified) filesystem path
    [[nodiscard]] static std::string getFolderFromFilePath(std::string_view filepath) noexcept;
    [[nodiscard]] static std::string getFileExtensionFromFilePath(std::string_view filepath) noexcept;
    [[nodiscard]] static std::string getFileNameFromFilePath(std::string_view filePath) noexcept;
    [[nodiscard]] static std::string normalizeDirectory(std::string dirPath) noexcept;
    [[nodiscard]] static bool isAbsolutePath(std::string_view filePath) noexcept;

    // URL-encodes a string, but keeps slashes intact (for file:/// URIs)
    [[nodiscard]] static std::string encodeStringToURI(std::string_view unencodedString) noexcept;

    // clipboard
    [[nodiscard]] std::string_view getClipBoardText();
    void setClipBoardText(std::string text);
    // returns false if failed/unsupported
    bool setClipBoardImage(std::vector<u8> pngData);

    // dialogs & message boxes
    static void showDialog(const char *title, const char *message,
                           unsigned int flags = 0x00000010u /* SDL_MESSAGEBOX_ERROR */,
                           void /*SDL_Window*/ *modalWindow = nullptr);
    void showMessageInfo(std::string_view title, std::string_view message) const;
    void showMessageWarning(std::string_view title, std::string_view message) const;
    void showMessageError(std::string_view title, std::string_view message) const;
    void showMessageErrorFatal(std::string_view title, std::string_view message) const;

    using FileDialogCallback = std::function<void(const std::vector<std::string> &paths)>;
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
    void setWindowTitle(const std::string &title);
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
    [[nodiscard]] forceinline const McRect &getCursorClip() const { return m_cursorClipRect; }
    [[nodiscard]] constexpr CURSORTYPE getCursor() const { return m_cursorType; }
    [[nodiscard]] constexpr bool isOSMouseInputRaw() const {
        return !m_bForceAbsCursor && flags::has<WinFlags::F_MOUSE_RELATIVE_MODE>(m_winflags);
    }
    [[nodiscard]] constexpr bool isMouseInputGrabbed() const {
        return flags::has<WinFlags::F_MOUSE_GRABBED>(m_winflags);
    }

    void setCursor(CURSORTYPE cur);
    void setCursorVisible(bool visible);
    void setCursorClip(bool clip, const McRect &rect);
    void setRawMouseInput(bool raw);  // enable/disable OS-level rawinput

    void setOSMousePos(vec2 pos);
    inline void setOSMousePos(float x, float y) { setOSMousePos(vec2{x, y}); }

    // keyboard
    [[nodiscard]] std::string scanCodeToString(SCANCODE scanCode) const;
    [[nodiscard]] std::string keyCodeToString(KEYCODE keyCode) const;
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
    // arg map + monitor map live behind a pimpl so including TUs don't pay to instantiate the unordered_maps
    struct EnvironmentImpl;
    StaticPImpl<EnvironmentImpl, sizeof(void *) == 8 ? 128 : 64> m_impl;
    std::unique_ptr<Engine> m_engine;

    SDL_Window *m_window;
    SDL_WindowID m_windowID;
    std::string m_sdldriver;
    enum class RuntimeRenderer : uint8_t { GL, GLES, DX11, SDLGPU, NULLGRAPHICS };
    RuntimeRenderer m_renderer;

    bool m_bRunning;
    bool m_bIsRestartScheduled;
    bool m_bHeadless;

    bool m_bRestoreFullscreen;
    bool m_bMinimizeSupported;

    // cache
    mutable std::string m_sUsername;
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
    mutable McRect m_fullDesktopBoundingBox;

    float m_fDisplayHz;
    float m_fDisplayHzSecs;

    // window

    // updates m_winflags, m_vLastKnownWindowSize, m_vLastKnownWindowPos, m_vLastKnownNativeScreenSize
    // to be called during event collection on window/display events, to avoid expensive API calls
    void updateWindowStateCache();
    void updateWindowSizeCache();  // only updates window size (separated out for live resize callback)

    std::string windowFlagsDbgStr() const;

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
        dvec2 rel;  // relative *since last call*
        dvec2 abs;  // mouse absolute
        // if we are actually getting relative deltas or emulating them from absolute position changes
        bool isRelativeMode;
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

    // this is basically to work around issues with wayland not providing a mouse position until the window is actually focused,
    // so we can't put the game cursor where the mouse is when the window opened until we get a mouse enter event
    // (a mouse enter event seems to just happen after some arbitrary time after creating the window...)
    bool m_bVirtualMousePositionInitialized{false};

    // is used to track relative tablet motion, is zeroed after consumeMousePositionCache
    // on some platforms, SDL automatically tracks relative motion deltas from absolute pen motion, but not others...
    // so we'll do it manually in that case
    vec2 m_vCurrentAbsPenPos{0.f, 0.f};
    vec2 m_vLastAbsPenPos{0.f, 0.f};

    vec2 m_vLastAbsMousePos{0.f, 0.f};

    // synthetic cursor position for headless/scripted UI testing (mouse_to command)
    inline void setInjectedCursorPos(vec2 pos) {
        m_bInjectedCursorDirty = true;
        m_vInjectedCursorPos = pos;
    }
    vec2 m_vInjectedCursorPos{0.f, 0.f};
    vec2 m_vLastInjectedCursorPos{0.f, 0.f};
    bool m_bInjectedCursorDirty{true};  // dirty to initialize mouse cursor pos

    bool m_bIsCursorInsideWindow;
    bool m_bCursorClipped;
    bool m_bHideCursorPending;
    McRect m_cursorClipRect;
    CURSORTYPE m_cursorType;
    std::array<SDL_Cursor *, (size_t)CURSORTYPE::CURSORTYPE_MAX> m_cursorIcons;

    // for state reconciliation when re-focusing the window
    friend class Keyboard;
    MouseButtonFlags getCurrentlyHeldMouseButtons() const;
    KEYMOD getCurrentlyHeldKeyModifiers() const;

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
    std::string m_sCurrClipboardText;

    // misc platform
    bool m_bIsX11;
    bool m_bIsKMSDRM;
    bool m_bIsWayland;

    // locale (cached)
    mutable std::string m_sLocaleString;

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

    static SDL_Rect McRectToSDLRect(const McRect &mcrect) noexcept;
    static McRect SDLRectToMcRect(const SDL_Rect &sdlrect) noexcept;
};

#endif
