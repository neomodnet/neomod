// Copyright (c) 2012, PG, All rights reserved.
#include "Engine.h"

#include "Environment.h"

#include "AppDescriptor.h"
#include "App.h"
#include "AppRunner.h"
#include "MakeDelegateWrapper.h"

#include "AsyncIOHandler.h"
#include "AsyncPool.h"
#include "AnimationHandler.h"
#include "CBaseUIContainer.h"
#include "CBaseUIDispatch.h"
#include "ConVar.h"
#include "Graphics.h"
#include "ConsoleBox.h"
#include "DirectoryWatcher.h"
#include "DiscordInterface.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "NetworkHandler.h"
#include "Profiler.h"
#include "ResourceManager.h"
#include "SoundEngine.h"
#include "Touch.h"
#include "Timing.h"
#include "Logging.h"
#include "VisualProfiler.h"
#include "SString.h"
#include "Parsing.h"
#include "crypto.h"
#include "Font.h"
#include "Image.h"
#include "Console.h"
#include "ConsoleReader.h"
#include "LaunchArgs.h"

#include <iostream>

Image *MISSING_TEXTURE{nullptr};

Mouse *mouse{nullptr};
Touch *touch{nullptr};
Keyboard *keyboard{nullptr};
App *app{nullptr};
Graphics *g{nullptr};
SoundEngine *soundEngine{nullptr};
ResourceManager *resourceManager{nullptr};
NetworkHandler *networkHandler{nullptr};
AsyncIOHandler *io{nullptr};
DirectoryWatcher *directoryWatcher{nullptr};

Mc::atomic_sharedptr<ConsoleBox> Engine::consoleBox{nullptr};

Engine *engine{nullptr};
Engine::Engine() {
    engine = this;

    // init crypto/rng seeding
    crypto::init();

    // always keep a dummy App() alive so we don't have to null-check for "app" inside engine code
    app = new App();

    this->guiContainer = nullptr;
    this->visualProfiler = nullptr;

    // print debug information
    debugLog("-= Engine Startup =-");
    debugLog("cmdline: {:s}", SString::join(Mc::LaunchArgs::get_array()));

    // timing
    this->iFrameCount = 0;
    this->iVsyncFrameCount = 0;
    this->fVsyncFrameCounterTime = 0.;
    this->dFrameTime = 0.016;

    cv::engine_throttle.setCallback(SA::MakeDelegate<&Engine::onEngineThrottleChanged>(this));
    this->bEngineThrottle = cv::engine_throttle.getBool();

    // screen
    this->bResolutionChange = false;
    this->screenRect = {{}, env->getWindowSize()};
    this->vNewScreenSize = this->screenRect.getSize();

    debugLog("Engine: ScreenSize = ({}x{})", (int)this->screenRect.getWidth(), (int)this->screenRect.getHeight());

    // custom
    this->bDrawing = false;
    this->bShuttingDown = false;

    // initialize all engine subsystems (the order does matter!)
    debugLog("Engine: Initializing subsystems ...");
    {
        // async io
        io = new AsyncIOHandler();
        directoryWatcher = new DirectoryWatcher();
        this->runtime_assert(!!io && io->succeeded() && !!directoryWatcher, "I/O subsystem failed to initialize!");

        // shared freetype init
        this->runtime_assert(McFont::initSharedResources(), "FreeType failed to initialize!");

        // input devices
        // put mouse before keyboard in inputDevices, so that mouse position is updated before relaying keyboard/mouse events
        mouse = new Mouse();
        this->runtime_assert(!!mouse, "Mouse failed to initialize!");
        this->inputDevices.push_back(mouse);
        this->mice.push_back(mouse);

        touch = new Touch();
        this->runtime_assert(!!touch, "Touch failed to initialize!");

        keyboard = new Keyboard();
        this->runtime_assert(!!keyboard, "Keyboard failed to initialize!");
        this->inputDevices.push_back(keyboard);
        this->keyboards.push_back(keyboard);

        // create graphics through environment
        g = env->createRenderer();
        // needs init() separation due to potential graphics access
        this->runtime_assert(!!g, "Graphics failed to initialize!");
        // TODO: transparent fallback like SoundEngine
        if constexpr(Env::cfg(OS::WINDOWS)) {
            this->runtime_assert(g->init(), fmt::format(
                                                R"({:s} failed to initialize!
Try running with "-opengl" or "-dx11" added to
the "Target:" field in a shortcut to)" PACKAGE_NAME ".",
                                                g->getName()));
        } else {
            this->runtime_assert(
                g->init(),
                fmt::format(
                    "{:s} failed to initialize!\nTry running with -opengl or -dx11 as extra commandline arguments.",
                    g->getName()));
        }

        // make unique_ptrs for the rest
        networkHandler = new NetworkHandler();
        this->runtime_assert(!!networkHandler, "Network handler failed to initialize!");

        soundEngine = SoundEngine::initialize();
        this->runtime_assert(!!soundEngine && soundEngine->succeeded(), "Sound engine failed to initialize!");

        resourceManager = new ResourceManager();
        this->runtime_assert(!!resourceManager, "Resource manager menu failed to initialize!");
        resourceManager->setSyncLoadMaxBatchSize(512);  // this decays back down to a small number quickly by itself

        DiscRPC::init();

        // default launch overrides
        g->setVSync(false);

        // engine time starts now
        this->dTime = Timing::getTimeReal();
    }
    debugLog("Engine: Initializing subsystems done.");
}

Engine::~Engine() {
    debugLog("-= Engine Shutdown =-");

    if(this->consoleReader) {
        debugLog("Engine: Freeing console reader...");
        this->consoleReader.reset();
    }

    // reset() all global unique_ptrs
    debugLog("Engine: Freeing app...");
    SAFE_DELETE(app);
    app =
        new App();  // re-create a dummy app and delete it again at the end (paranoia, just in case something tries to dereference app in its destructor)

    debugLog("Engine: Freeing engine GUI...");
    if(const auto &cbox = Engine::consoleBox.load(std::memory_order_acquire); cbox != nullptr) {
        // don't allow CBaseUI to delete it, it might still be in use (being flushed) by Logger
        this->guiContainer->removeBaseUIElement(cbox.get());
    }

    // sanity, wait until logger has stopped logging messages to console box before continuing to delete uiDispatch
    // (this is spaghetti but should prevent the need to null-check uiDispatcher)
    {
        auto cbox = Engine::consoleBox.load(std::memory_order_acquire);
        Engine::consoleBox.store(nullptr, std::memory_order_release);
        while(cbox.use_count() > 1) Timing::sleep(0);
    }
    SAFE_DELETE(this->guiContainer);

    if(mouse) mouse->removeListener(this->uiMouseSink.get());
    this->uiMouseSink.reset();
    CBaseUIDispatch::clear();

    DiscRPC::destroy();

    debugLog("Engine: Freeing animation handler...");
    anim::clearAll();

    debugLog("Engine: Freeing resource manager...");
    SAFE_DELETE(resourceManager);
    MISSING_TEXTURE = nullptr;

    debugLog("Engine: Stopping threads...");
    Async::shutdown();

    debugLog("Engine: Freeing Sound...");
    SAFE_DELETE(soundEngine);

    debugLog("Engine: Freeing network handler...");
    SAFE_DELETE(networkHandler);

    debugLog("Engine: Freeing graphics...");
    SAFE_DELETE(g);

    debugLog("Engine: Freeing input devices...");
    // delete input devices
    for(auto &device : this->inputDevices) {
        SAFE_DELETE(device);
    }
    this->inputDevices.clear();
    this->mice.clear();
    this->keyboards.clear();

    mouse = nullptr;
    keyboard = nullptr;

    // TODO: make touch an input device
    SAFE_DELETE(touch);

    debugLog("Engine: Freeing fonts...");
    McFont::cleanupSharedResources();

    debugLog("Engine: Stopping I/O subsystem...");
    SAFE_DELETE(directoryWatcher);

    io->cleanup();
    SAFE_DELETE(io);

    debugLog("Engine: Goodbye.");

    SAFE_DELETE(app);  // delete the dummy App() for real
    engine = nullptr;
}

bool Engine::loadApp() {
    if(this->bShuttingDown) return false;
    // load core default resources
    debugLog("Engine: Loading default resources ...");
    this->defaultFont = resourceManager->loadFont("weblysleekuisb", "FONT_DEFAULT", 15, true, env->getDPI());
    this->consoleFont = resourceManager->loadFont("tahoma", "FONT_CONSOLE", 8, false, 96);

    // load other default resources and things which are not strictly necessary
    {
        MISSING_TEXTURE = resourceManager->createImage(512, 512);
        for(int x = 0; x < 512; x++) {
            for(int y = 0; y < 512; y++) {
                int rowCounter = (x / 64);
                int columnCounter = (y / 64);
                Color color = (((rowCounter + columnCounter) % 2 == 0) ? rgb(255, 0, 221) : rgb(0, 0, 0));
                MISSING_TEXTURE->setPixel(x, y, color);
            }
        }
        MISSING_TEXTURE->loadAsync();
        MISSING_TEXTURE->load();

        // create engine gui
        this->guiContainer = new CBaseUIContainer(0, 0, this->getScreenWidth(), this->getScreenHeight(), "");
        Engine::consoleBox.store(std::make_shared<ConsoleBox>(), std::memory_order_release);
        this->guiContainer->addBaseUIElement(Engine::consoleBox.load(std::memory_order_acquire).get());
        this->visualProfiler = new VisualProfiler();
        this->guiContainer->addBaseUIElement(this->visualProfiler);

        // (engine hardcoded hotkeys come first, then engine gui)
        keyboard->addListener(this->guiContainer, true);
        keyboard->addListener(this, true);

        // the UI layer receives mouse button events through the same relay as everyone else;
        // CBaseUIDispatch routes them after each root's updateInput pass
        this->uiMouseSink = std::make_unique<CBaseUIDispatch::MouseSink>();
        mouse->addListener(this->uiMouseSink.get());
    }

    debugLog("Engine: Loading app ...");
    {
        //*****************//
        //	Load App here  //
        //*****************//

#ifndef BUILD_TOOLS_ONLY
#ifdef MCENGINE_TESTS
        {
            const auto testApp = Mc::LaunchArgs::has_arg(Mc::LaunchArgs::MODE_TESTAPP);
            // keep old dummy global "app" alive during ctor so that "app" can still be safely dereferenced during new construction
            auto *new_app = new AppRunner(testApp.has_value(), testApp.value_or(""));
            SAFE_DELETE(app);
            app = new_app;
        }
#else
        if(const auto &defaultApp = Mc::getDefaultAppDescriptor(); !!defaultApp.create) {
            auto *new_app = defaultApp.create();
            SAFE_DELETE(app);
            app = new_app;
        }
#endif  // MCENGINE_TESTS
        this->runtime_assert(!!app, "App failed to initialize!");
#endif  // BUILD_TOOLS_ONLY

        // start listening to the default keyboard input
        keyboard->addListener(app);

        // start stdin reader for headless/console mode
        if(env->isHeadless() || Mc::LaunchArgs::has_arg(Mc::LaunchArgs::MODE_CONSOLE)) {
            this->consoleReader = std::make_unique<Mc::ConsoleReader>();
        }
    }
    debugLog("Engine: Loading app done.");
    return true;
}

void Engine::onPaint() {
    if(this->bShuttingDown) return;
    VPROF_BUDGET("Engine::onPaint", VPROF_BUDGETGROUP_DRAW);

    this->bDrawing = true;
    {
        // begin
        {
            VPROF_BUDGET("Graphics::beginScene", VPROF_BUDGETGROUP_DRAW);
            g->beginScene();
        }

        // middle
        {
            {
                VPROF_BUDGET("App::draw", VPROF_BUDGETGROUP_DRAW);
                app->draw();
            }

            if(this->guiContainer) this->guiContainer->draw();

            // debug input devices
            for(auto *inputDevice : this->inputDevices) {
                inputDevice->draw();
            }

            // debug fonts
            for(auto *font : resourceManager->getFonts()) {
                font->drawDebug();
            }
        }

        // end
        {
            VPROF_BUDGET("Graphics::endScene", VPROF_BUDGETGROUP_DRAW_SWAPBUFFERS);
            g->endScene();
        }
    }
    this->bDrawing = false;

    this->iFrameCount++;
}

void Engine::onUpdate() {
    if(this->bShuttingDown) return;

    VPROF_BUDGET("Engine::onUpdate", VPROF_BUDGETGROUP_UPDATE);

    {
        // update time
        VPROF_BUDGET("Timer::update", VPROF_BUDGETGROUP_UPDATE);

        // frame time
        f64 frameTime;
        if(const f64 fixedDt = cv::debug_fixed_frametime.getDouble(); fixedDt > 0.0) {
            // deterministic stepping for headless testing: simulation time advances by a fixed
            // step per frame, decoupled from wall clock (so anims and @wait_secs are reproducible)
            frameTime = this->dFrameTime = fixedDt;
            this->dTime += fixedDt;
        } else {
            const f64 now = Timing::getTimeReal();
            frameTime = this->dFrameTime = std::max<f64>(now - this->dTime, 0.00005);
            // total engine runtime
            this->dTime = now;
        }
        if(this->bEngineThrottle) {
            const f64 refreshTime = env->getDisplayRefreshTime();
            // it's more like a crude estimate but it gets the job done for use as a throttle
            this->fVsyncFrameCounterTime += frameTime;
            // update immediately if we are running slower than the refresh rate
            // or if we have accumulated enough time to fill 1 vsync frame time
            if((frameTime >= refreshTime) || ((this->fVsyncFrameCounterTime + (frameTime / 2.)) >= refreshTime)) {
                this->fVsyncFrameCounterTime = 0.;
                ++this->iVsyncFrameCount;
            }
        }
    }

    // handle pending queued resolution changes
    if(this->bResolutionChange) {
        this->bResolutionChange = false;

        logIfCV(debug_engine, "executing pending queued resolution change to ({})", this->vNewScreenSize);

        this->onResolutionChange(this->vNewScreenSize);
    }

    // process stdin in headless
    if(this->consoleReader) {
        this->consoleReader->processStdin(this->dTime);
    }

    // update miscellaneous engine subsystems
    {
        {
            VPROF_BUDGET("AsyncIO::update", VPROF_BUDGETGROUP_UPDATE);
            io->update();
        }

        {
            VPROF_BUDGET("Async::update", VPROF_BUDGETGROUP_UPDATE);
            Async::update();
        }

        {
            VPROF_BUDGET("DirectoryWatcher::update", VPROF_BUDGETGROUP_UPDATE);
            directoryWatcher->update();
        }

        {
            // VPROF_BUDGET("SoundEngine::update", VPROF_BUDGETGROUP_UPDATE);
            soundEngine->update();  // currently does nothing anyways
        }

        {
            VPROF_BUDGET("ResourceManager::update", VPROF_BUDGETGROUP_UPDATE);
            resourceManager->update();
        }

        {
            VPROF_BUDGET("NetworkHandler::update", VPROF_BUDGETGROUP_UPDATE);
            // run networking response callbacks, if any
            networkHandler->update();
        }

        {
            VPROF_BUDGET("AnimationHandler::update", VPROF_BUDGETGROUP_UPDATE);
            anim::update(this->dFrameTime);
        }

        // dispatch events + update gui
        {
            VPROF_BUDGET("InputDevices::update", VPROF_BUDGETGROUP_UPDATE);
            for(auto *inputDevice : this->inputDevices) {
                inputDevice->update();
            }
        }

        {
            VPROF_BUDGET("GUI::update", VPROF_BUDGETGROUP_UPDATE);
            if(this->guiContainer) {
                this->guiContainer->tick();
                CBaseUIEventCtx c;
                this->guiContainer->updateInput(c);
                // engine root dispatches (and consumes) before the app root: it draws on top
                CBaseUIDispatch::dispatchEvents(c, CBaseUIDispatch::Root::ENGINE);
            }
        }
    }

    // update app
    {
        VPROF_BUDGET("App::update", VPROF_BUDGETGROUP_UPDATE);
        app->update();
    }

    // update discord presence
    {
        VPROF_BUDGET("DiscRPC::tick", VPROF_BUDGETGROUP_UPDATE);
        DiscRPC::tick();
    }

    // update environment (after app, at the end here)
    {
        VPROF_BUDGET("Environment::update", VPROF_BUDGETGROUP_UPDATE);
        env->update();
    }
}

void Engine::onFocusGained() {
    logIfCV(debug_engine, "(Engine) called");

    for(auto *device : this->inputDevices) {
        device->reset();
    }

    soundEngine->onFocusGained();  // switch shared->exclusive if applicable
    app->onFocusGained();
}

void Engine::onFocusLost() {
    logIfCV(debug_engine, "(Engine) called");

    for(auto *device : this->inputDevices) {
        device->reset();
    }

    soundEngine->onFocusLost();  // switch exclusive->shared if applicable
    app->onFocusLost();

    // auto minimize on certain conditions
    if(env->winFullscreened() && (cv::minimize_on_focus_lost_if_borderless_windowed_fullscreen.getBool() ||
                                  cv::minimize_on_focus_lost_if_fullscreen.getBool())) {
        env->minimizeWindow();
    }
}

void Engine::onMinimized() {
    logIfCV(debug_engine, "(Engine) called");

    app->onMinimized();
}

void Engine::onMaximized() { logIfCV(debug_engine, "(Engine) called"); }

void Engine::onRestored() {
    logIfCV(debug_engine, "(Engine) called");

    g->onRestored();
    app->onRestored();
}

void Engine::onResolutionChange(vec2 newResolution) {
    debugLog("(Engine) ({:d}, {:d}) -> ({:d}, {:d})", (int)this->screenRect.getWidth(),
             (int)this->screenRect.getHeight(), (int)newResolution.x, (int)newResolution.y);

    // NOTE: Windows [Show Desktop] button in the superbar causes (0,0)
    if(newResolution.x < 2 || newResolution.y < 2) {
        newResolution = vec2(2, 2);
    }

    // to avoid double resolutionChange
    this->bResolutionChange = false;
    this->vNewScreenSize = newResolution;
    this->screenRect = {vec2{}, newResolution};

    if(this->guiContainer) this->guiContainer->setSize(newResolution.x, newResolution.y);
    if(const auto &cbox = Engine::consoleBox.load(std::memory_order_relaxed); cbox != nullptr) {
        cbox->onResolutionChange(newResolution);
    }

    // update everything
    g->onResolutionChange(newResolution);
    app->onResolutionChanged(newResolution);
}

void Engine::onDPIChange() {
    debugLog("(Engine) DPI: {:d}", env->getDPI());

    app->onDPIChanged();
}

void Engine::onShutdown() {
    logIfCV(debug_engine, "(Engine) called");
    if(this->bShuttingDown || !app->onShutdown()) return;

    this->bShuttingDown = true;
    if(soundEngine) soundEngine->shutdown();
    env->shutdown();
}

// hardcoded engine hotkeys
void Engine::onKeyDown(KeyboardEvent &e) {
    auto keyCode = e.getScanCode();
    if(keyboard->isAltDown()) {
        if(keyCode == KEY_F4) {
            // handle ALT+F4 quit
            this->shutdown();
            e.consume();
        } else if((keyCode == KEY_ENTER || keyCode == KEY_NUMPAD_ENTER)) {
            // handle ALT+ENTER fullscreen toggle
            this->toggleFullscreen();
            e.consume();
        }
    } else if(keyboard->isControlDown()) {
        if(keyCode == KEY_F11) {
            // handle CTRL+F11 profiler toggle
            cv::vprof.setValue(cv::vprof.getBool() ? false : true);
            e.consume();
        } else if(keyCode == KEY_TAB && cv::vprof.getBool()) {
            // handle profiler display mode change
            if(keyboard->isShiftDown())
                this->visualProfiler->decrementInfoBladeDisplayMode();
            else
                this->visualProfiler->incrementInfoBladeDisplayMode();
            e.consume();
        }
    }
}

void Engine::restart() {
    this->onShutdown();
    env->restart();
}

void Engine::focus() { env->restoreWindow(); }

void Engine::center() { env->centerWindow(); }

void Engine::toggleFullscreen() {
    if(env->winFullscreened())
        env->disableFullscreen();
    else
        env->enableFullscreen();
}

void Engine::disableFullscreen() { env->disableFullscreen(); }

void Engine::showMessageInfo(std::string_view title, std::string_view message) {
    debugLog("INFO: [{:s}] | {:s}", title, message);
    env->showMessageInfo(title, message);
}

void Engine::showMessageWarning(std::string_view title, std::string_view message) {
    debugLog("WARNING: [{:s}] | {:s}", title, message);
    env->showMessageWarning(title, message);
}

void Engine::showMessageError(std::string_view title, std::string_view message) {
    debugLog("ERROR: [{:s}] | {:s}", title, message);
    Logger::flush();
    env->showMessageError(title, message);
}

void Engine::showMessageErrorFatal(std::string_view title, std::string_view message) {
    debugLog("FATAL ERROR: [{:s}] | {:s}", title, message);
    Logger::flush();
    env->showMessageErrorFatal(title, message);
}

void Engine::runtime_assert(bool cond, std::string_view reason) {
    if(cond) return;
    this->showMessageErrorFatal("Engine Error", reason);
    fubar_abort();
}

void Engine::requestResolutionChange(vec2 newResolution) {
    logIfCV(debug_engine, "(Engine) {}", newResolution);
    if(env->winMinimized()) return;
    if(newResolution == this->vNewScreenSize) return;

    this->vNewScreenSize = newResolution;
    this->bResolutionChange = true;
}

void Engine::onEngineThrottleChanged(float newVal) {
    const bool enable = !!static_cast<int>(newVal);
    if(!enable) {
        this->fVsyncFrameCounterTime = 0.;
        this->iVsyncFrameCount = 0;
        this->bEngineThrottle = false;
    } else {
        this->bEngineThrottle = true;
    }
}

double Engine::getSimulatedVsyncFrameDelta() const {
    if(this->bEngineThrottle) {
        if(this->fVsyncFrameCounterTime == 0.) {
            return env->getDisplayRefreshTime();
        } else {
            return 0;
        }
    } else {
        return this->dFrameTime;
    }
}

//**********************//
//	Engine ConCommands	//
//**********************//

void _printsize() {
    vec2 s = engine->getScreenSize();
    debugLog("Engine: screenSize = ({:f}, {:f})", s.x, s.y);
}

void _borderless() {
    if(cv::fullscreen_windowed_borderless.getBool()) {
        cv::fullscreen_windowed_borderless.setValue(0.0f);
        if(env->winFullscreened()) env->disableFullscreen();
    } else {
        cv::fullscreen_windowed_borderless.setValue(1.0f);
        if(!env->winFullscreened()) env->enableFullscreen();
    }
}

void _errortest() {
    engine->showMessageError(
        "Error Test",
        "This is an error message, fullscreen mode should be disabled and you should be able to read this");
}

void _restart() { engine->restart(); }
void _minimize() { env->minimizeWindow(); }
void _maximize() { env->maximizeWindow(); }
void _toggleresizable() { env->setWindowResizable(!env->winResizable()); }
void _focus() { engine->focus(); }
void _center() { engine->center(); }
void _dpiinfo() { debugLog("env->getDPI() = {:d}, env->getDPIScale() = {:f}", env->getDPI(), env->getDPIScale()); }
