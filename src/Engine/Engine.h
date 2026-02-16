#pragma once
// Copyright (c) 2012, PG, All rights reserved.

#include "EngineConfig.h"

#include "noinclude.h"
#include "types.h"

#include "Rect.h"
#include "KeyboardListener.h"
#include "AtomicSharedPtr.h"
#include "SyncMutex.h"
#include "SyncJthread.h"

#include <vector>
#include <memory>
#include <deque>

#ifndef APP_H
class App;
#endif

class Graphics;
class Mouse;
class ConVar;
class Keyboard;
class McFont;
class InputDevice;
class SoundEngine;

namespace Mc::Net {
class NetworkHandler;
}

using Mc::Net::NetworkHandler;
class ResourceManager;
class AsyncIOHandler;
class DirectoryWatcher;

class CBaseUIContainer;
class VisualProfiler;
class ConsoleBox;
class Console;
class UString;
class Image;

class Engine final : public KeyboardListener {
    NOCOPY_NOMOVE(Engine)
   public:
    Engine();
    ~Engine() override;

    // app
    void loadApp();

    // render/update
    void onPaint();
    void onUpdate();

    // window messages
    void onFocusGained();
    void onFocusLost();
    void onMinimized();
    void onMaximized();
    void onRestored();
    void onResolutionChange(vec2 newResolution);
    void onDPIChange();
    void onShutdown();

    // primary keyboard messages
    void onKeyDown(KeyboardEvent &e) override;
    void onKeyUp(KeyboardEvent & /**/) override {}
    void onChar(KeyboardEvent & /**/) override {}

    // convenience functions (passthroughs)
    inline void shutdown() { this->onShutdown(); }
    void restart();
    void focus();
    void center();
    void toggleFullscreen();
    void disableFullscreen();

    // UI focus hacks (passthrough to app + engine UI)
    void stealUIFocus();

    void showMessageInfo(const UString &title, const UString &message);
    void showMessageWarning(const UString &title, const UString &message);
    void showMessageError(const UString &title, const UString &message);
    void showMessageErrorFatal(const UString &title, const UString &message);

    // engine specifics
    [[nodiscard]] inline bool isShuttingDown() const { return this->bShuttingDown; }

    // interfaces
   public:
    [[nodiscard]] inline const std::vector<Mouse *> &getMice() const { return this->mice; }
    [[nodiscard]] inline const std::vector<Keyboard *> &getKeyboards() const { return this->keyboards; }

    // screen
    void requestResolutionChange(vec2 newResolution);
    [[nodiscard]] constexpr McRect getScreenRect() const { return this->screenRect; }
    [[nodiscard]] constexpr vec2 getScreenSize() const { return this->screenRect.getSize(); }
    [[nodiscard]] constexpr int getScreenWidth() const { return (int)this->screenRect.getWidth(); }
    [[nodiscard]] constexpr int getScreenHeight() const { return (int)this->screenRect.getHeight(); }

    // vars
    [[nodiscard]] constexpr double getTime() const { return this->dTime; }
    [[nodiscard]] constexpr double getFrameTime() const { return this->dFrameTime; }
    [[nodiscard]] constexpr u64 getFrameCount() const { return this->iFrameCount; }
    [[nodiscard]] double getSimulatedVsyncFrameDelta() const;  // 0 on non-vsync frames

    // clang-format off
    // NOTE: if engine_throttle cvar is off, this will always return true
    [[nodiscard]] inline bool throttledShouldRun(unsigned int howManyVsyncFramesToWaitBetweenExecutions) {
        return howManyVsyncFramesToWaitBetweenExecutions == 0 ||
               ((this->fVsyncFrameCounterTime == 0.) && !(this->iVsyncFrameCount % howManyVsyncFramesToWaitBetweenExecutions));
    }
    // clang-format on

    [[nodiscard]] constexpr bool isDrawing() const { return this->bDrawing; }

    // debugging/console
    [[nodiscard]] static inline std::shared_ptr<ConsoleBox> getConsoleBox() {
        return Engine::consoleBox.load(std::memory_order_acquire);
    }
    [[nodiscard]] constexpr CBaseUIContainer *getGUI() const { return this->guiContainer; }

    [[nodiscard]] constexpr McFont *getDefaultFont() const { return this->defaultFont; }
    [[nodiscard]] constexpr McFont *getConsoleFont() const { return this->consoleFont; }

   private:
    void runtime_assert(bool cond, const char *reason);

    // input devices
    std::vector<Mouse *> mice;
    std::vector<Keyboard *> keyboards;
    std::vector<InputDevice *> inputDevices;

    // timing
    f64 dTime;
    u64 iFrameCount;
    double dFrameTime;

    // this will wrap quickly, and that's fine, it should be used as a dividend in a modular expression anyways
    double fVsyncFrameCounterTime;
    uint8_t iVsyncFrameCount;
    bool bEngineThrottle;

    void onEngineThrottleChanged(float newVal);

    // primary screen
    McRect screenRect;
    vec2 vNewScreenSize{0.f};
    bool bResolutionChange;

    // engine gui, mostly for debugging
    CBaseUIContainer *guiContainer;
    VisualProfiler *visualProfiler;
    static Mc::atomic_sharedptr<ConsoleBox> consoleBox;

    McFont *consoleFont{nullptr};
    McFont *defaultFont{nullptr};

    // custom
    bool bShuttingDown;
    bool bDrawing;

    // stdin input for headless/console mode
    bool bShouldProcessStdin;
    Sync::jthread stdinThread;
    Sync::mutex stdinMutex;
    std::deque<std::string> stdinQueue;
    int stdinWaitFrames{0};  // @wait support: skip N frames before processing more commands
    static void stdinReaderThread(const Sync::stop_token &stopToken);
    void processStdinCommands();
};

extern std::unique_ptr<Mouse> mouse;
extern std::unique_ptr<Keyboard> keyboard;
extern std::unique_ptr<App> app;
extern std::unique_ptr<Graphics> g;
extern std::unique_ptr<SoundEngine> soundEngine;
extern std::unique_ptr<ResourceManager> resourceManager;
extern std::unique_ptr<NetworkHandler> networkHandler;
extern std::unique_ptr<AsyncIOHandler> io;
extern std::unique_ptr<DirectoryWatcher> directoryWatcher;

extern Engine *engine;

void _restart();
void _printsize();
void _fullscreen();
void _borderless();
void _minimize();
void _maximize();
void _toggleresizable();
void _focus();
void _center();
void _errortest();
void _dpiinfo();

// black and purple placeholder texture, valid from engine startup to shutdown
extern Image *MISSING_TEXTURE;
