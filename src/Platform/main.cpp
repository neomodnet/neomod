// Copyright (c) 2025, WH, All rights reserved.
#include "BaseEnvironment.h"
#include "EngineConfig.h"
#include "Logging.h"

#if defined(MCENGINE_PLATFORM_WASM) || defined(MCENGINE_FEATURE_MAINCALLBACKS)
#define MAIN_FUNC SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
#define SDL_MAIN_USE_CALLBACKS  // this enables the use of SDL_AppInit/AppEvent/AppIterate instead of a traditional
                                // mainloop, needed for wasm (works on desktop too, but it's not necessary)
#else
#define MAIN_FUNC int main(int argc, char *argv[])
#endif

#include <SDL3/SDL_main.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_process.h>

#ifdef MCENGINE_PLATFORM_WINDOWS
#include "CrashHandler.h"
#endif
#include "Profiler.h"
#include "UString.h"
#include "Thread.h"
#include "Engine.h"
#include "DiffCalcTool.h"
#include "File.h"

#include "environment_private.h"
#include "AppDescriptor.h"

#if defined(_WIN32)
#include "WinDebloatDefs.h"
#include <consoleapi2.h>  // for SetConsoleOutputCP
#include <processenv.h>   // for GetCommandLine
#endif

#include <filesystem>
#include <locale>
#include <clocale>

#ifdef MCENGINE_PLATFORM_WASM
#include <emscripten/emscripten.h>

// Our html shell overrides window.alert to display fatal errors properly.
// (we override window.alert so this code also falls back nicely on the default shell)
EM_JS(void, js_fatal_error, (const char *str), { alert(UTF8ToString(str)); });
#endif

#ifdef WITH_LIVEPP
#include "LPP_API_x64_CPP.h"
#endif

namespace {
void setcwdexe(const std::string &exePathStr) noexcept {
    // Fix path in case user is running it from the wrong folder.
    // We only do this if MCENGINE_DATA_DIR is set to its default value, since if it's changed,
    // the packager clearly wants the executable in a different location.
    if constexpr(Env::cfg(OS::WASM) || (!(MCENGINE_DATA_DIR[0] == '.' && MCENGINE_DATA_DIR[1] == '/'))) {
        return;
    }
    namespace fs = std::filesystem;

    bool failed = true;
    std::error_code ec;

    UString uPath{exePathStr};
    fs::path exe_path{uPath.plat_str()};

    if(!exe_path.empty() && exe_path.has_parent_path()) {
        fs::current_path(exe_path.parent_path(), ec);
        failed = !!ec;
    }

    if(failed) {
        debugLog("WARNING: failed to set working directory to parent of {}", exePathStr.c_str());
    }
}
}  // namespace

//*********************************//
//	SDL CALLBACKS/MAINLOOP BEGINS  //
//*********************************//

// called when the SDL_APP_SUCCESS (normal exit) or SDL_APP_FAILURE (something bad happened) event is returned from
// Init/Iterate/Event
void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    if(!appstate || result == SDL_APP_FAILURE) {
        auto err_msg = SDL_GetError();
        debugLog("Force exiting now, a fatal error occurred. (SDL error: {})", err_msg);

#ifdef MCENGINE_PLATFORM_WASM
        // Display the error to the user (as opposed to a black screen)
        js_fatal_error(err_msg);
#endif

        std::exit(-1);
    }

    auto *fmain = static_cast<SDLMain *>(appstate);

    fmain->setCursorClip(false, {});  // release input devices
    fmain->setWindowsKeyDisabled(false);

    const bool restart = fmain->isRestartScheduled();
    std::vector<std::string> restartArgs{};
    if(restart) {
        restartArgs = fmain->getCommandLine();
    }

    // we might be called directly instead of through events, so check this again
    if(fmain->m_bRunning) {
        fmain->m_bRunning = false;
        if(fmain->m_engine && !fmain->m_engine->isShuttingDown()) {
            fmain->m_engine->shutdown();
        }
    }

    // flush IDBFS to IndexedDB after config/scores have been saved (only does anything on WASM)
    File::flushToDisk();

    if constexpr(Env::cfg(OS::WASM) || Env::cfg(FEAT::MAINCB)) {
        // we allocated it with new
        delete fmain;
    } else {
        // not heap-allocated
        fmain->~SDLMain();
    }

    if constexpr(!Env::cfg(OS::WASM)) {
        if(restart) {
            SDLMain::restart(restartArgs);
        }
        if constexpr(!Env::cfg(FEAT::MAINCB)) {
            SDL_Quit();
            printf("Shutdown success.\n");
            std::exit(0);
        }
    } else {
        printf("Shutdown success.\n");
    }
}

// we can just call handleEvent and iterate directly if we're not using main callbacks
#if defined(MCENGINE_PLATFORM_WASM) || defined(MCENGINE_FEATURE_MAINCALLBACKS)
// (event queue processing) serialized with SDL_AppIterate
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    return static_cast<SDLMain *>(appstate)->handleEvent(event);
}

// (update tick) serialized with SDL_AppEvent
SDL_AppResult SDL_AppIterate(void *appstate) {
    // exit sleep/event scope
    VPROF_EXIT_SCOPE();

    SDL_AppResult ret = static_cast<SDLMain *>(appstate)->iterate();

    // exit previous main scope
    VPROF_EXIT_SCOPE();

    // re-enter main+events scope
    g_profCurrentProfile.mainprof();
    VPROF_ENTER_SCOPE("Main", VPROF_BUDGETGROUP_ROOT);
    VPROF_ENTER_SCOPE("SDL", VPROF_BUDGETGROUP_BETWEENFRAMES);

    return ret;
}
#endif

// actual main/init, called once
MAIN_FUNC /* int argc, char *argv[] */
{
#if defined(_WIN32) && defined(_DEBUG)  // only debug allocates a console immediately
    SetConsoleOutputCP(65001 /*CP_UTF8*/);
#endif

// set locale for e.g. fmt::format("{:L}") to work as expected without explicitly setting it
#if (defined(__MINGW32__) || defined(__MINGW64__)) && defined(__GLIBCXX__)
    // MinGW's libstdc++ locale support is broken (only "C" locale works).
    // just set the C locale for things like strtod(), but don't touch std::locale.
    std::setlocale(LC_ALL, "");
#else
    if(!!std::setlocale(LC_ALL, "")) {
        std::locale::global(std::locale{""});
    }
#endif

#ifdef WITH_LIVEPP
    debugLog("Starting Live++");
    lpp::LppSynchronizedAgent lppAgent = lpp::LppCreateSynchronizedAgent(nullptr, L"../../../LivePP");
    if(!lpp::LppIsValidSynchronizedAgent(&lppAgent)) {
        return 1;
    }

    lppAgent.EnableModule(lpp::LppGetCurrentModulePath(), lpp::LPP_MODULES_OPTION_NONE, nullptr, nullptr);
#endif

    const bool diffcalcOnly = argc >= 2 && strncmp(argv[1], "-diffcalc", sizeof("-diffcalc") - 1) == 0;
    if(diffcalcOnly) {
        return (SDL_AppResult)NEOMOD_run_diffcalc(argc, argv);
    }

    // parse args here
    // simple vector representation of the whole cmdline including the program name (as the first element)
    auto arg_cmdline = std::vector<std::string>(argv, argv + argc);

    // more easily queryable representation of the args as a map
    auto arg_map = [&]() -> std::unordered_map<std::string, std::optional<std::string>> {
        // example usages:
        // args.contains("-file")
        // auto filename = args["-file"].value_or("default.txt");
        // if (args["-output"].has_value())
        // 	auto outfile = args["-output"].value();
        std::unordered_map<std::string, std::optional<std::string>> args;
        for(int i = 1; i < argc; ++i) {
            std::string_view arg{argv[i]};
            if(arg.starts_with('-'))
                if(i + 1 < argc && !(argv[i + 1][0] == '-')) {
                    args[std::string(arg)] = argv[i + 1];
                    ++i;
                } else
                    args[std::string(arg)] = std::nullopt;
            else
                args[std::string(arg)] = std::nullopt;
        }
        return args;
    }();

    // if we have an "existing window handler", let it run very early
    // use the handler for the desired app-to-launch, so we don't collide with a running instance of a different kind of app
    const Mc::AppDescriptor *appDesc{nullptr};
    if(arg_map.contains("-testapp") && arg_map["-testapp"].has_value()) {
        const auto &testappName = arg_map["-testapp"].value();
        for(const auto &entry : Mc::getAllAppDescriptors()) {
            if(testappName == entry.name) {
                appDesc = &entry;
                break;
            }
        }
    }
    if(!appDesc) {
        appDesc = &Mc::getDefaultAppDescriptor();
    }

    assert(appDesc);

    // for the neomod (default) implementation, this checks if an existing instance is running, and if it is,
    // sends it a message (with the current argc+argv) and quits the current instance (so we might never proceed further in this process)
    if(appDesc->handleExistingWindow) {
        appDesc->handleExistingWindow(argc, argv);
    }

#ifdef MCENGINE_PLATFORM_WINDOWS
    CrashHandler::init();
#endif

    // this sets and caches the path in getPathToSelf, so this must be called here
    const auto &selfpath = Environment::getPathToSelf(argv[0]);
    // set the current working directory to the executable directory, so that relative paths
    // work as expected
    setcwdexe(selfpath);

    // improve floating point perf in case this isn't already enabled by the compiler
    // -nofpu to disable (debug)
    if(arg_map.contains("-nofpu")) {
        McThread::debug_disable_thread_init_changes();
    } else {
        // this is also run at the start of each new thread (since the state is thread-local)
        McThread::on_thread_init();
    }

    const bool headless = arg_map.contains("-headless");

    // now set up spdlog logging
    Logger::init(headless || arg_map.contains("-console"));
    atexit(Logger::shutdown);

#if defined(_WIN32)
    {
        constexpr int CS_BYTEALIGNCLIENT_ = 0x1000;
        constexpr int CS_BYTEALIGNWINDOW_ = 0x2000;

        // required for handle_existing_window to find this running instance
        SDL_RegisterApp(PACKAGE_NAME, CS_BYTEALIGNCLIENT_ | CS_BYTEALIGNWINDOW_, nullptr);
    }
#endif

    // set up some common app metadata (SDL says these should be called as early as possible)
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING, PACKAGE_NAME);
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING, PACKAGE_VERSION);
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING, "net.neomodnet." PACKAGE_NAME);
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_CREATOR_STRING, "kiwec/spectator/McKay");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_COPYRIGHT_STRING, "MIT/GPL3");  // neomod is gpl3, mcengine is mit
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_URL_STRING, PACKAGE_URL);
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "game");

    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1", SDL_HINT_NORMAL);
    SDL_SetHintWithPriority(SDL_HINT_INVALID_PARAM_CHECKS, Env::cfg(BUILD::DEBUG) ? "2" : "1", SDL_HINT_NORMAL);

#if defined(_WIN32)
    // this hint needs to be set before SDL_Init
    if(arg_map.contains("-nodpi")) {
        // it's not even defined anywhere...
        SDL_SetHintWithPriority("SDL_WINDOWS_DPI_AWARENESS", "unaware", SDL_HINT_NORMAL);
    }
#endif

    // we have no way of knowing when "char" event listeners are actually interested in text input,
    // so we have SDL_StartTextInput enabled most of the time
    // disable IME input panels so that it isn't annoying
    // should be fixed in engine so that IME input is actually usable
    if(!arg_map.contains("-ime")) {
        SDL_SetHintWithPriority(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0", SDL_HINT_NORMAL);
    }

    if(headless) {
        // use a video driver that doesn't need a real display
        if constexpr(Env::cfg(OS::WASM)) {
            SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
            SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
        } else {
            // don't use offscreen for SDL_gpu headless, that would attempt to initialize a bunch of
            // offscreen openGL stuff we don't want
            if(!(Env::cfg(REND::SDLGPU) && (arg_map.contains("-sdlgpu") || arg_map.contains("-gpu")))) {
                SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);
            }
        }
    }

    if(!SDL_Init(SDL_INIT_VIDEO))  // other subsystems can be init later
    {
        debugLog("Couldn't SDL_Init(): {}", SDL_GetError());
        return SDL_APP_FAILURE;
    }

#if defined(MCENGINE_PLATFORM_WASM) || defined(MCENGINE_FEATURE_MAINCALLBACKS)
    // need to manually scope profiler nodes in main callbacks,
    // since we are called from SDL externally
    // start it here so we can exit/re-enter in each iterate() call afterwards
    g_profCurrentProfile.mainprof();
    VPROF_ENTER_SCOPE("Main", VPROF_BUDGETGROUP_ROOT);
    VPROF_ENTER_SCOPE("SDL", VPROF_BUDGETGROUP_BETWEENFRAMES);

    auto *fmain = new SDLMain(*appDesc, std::move(arg_map), std::move(arg_cmdline));  // need to allocate dynamically
    *appstate = fmain;
    return !fmain ? SDL_APP_FAILURE : fmain->initialize();
#else

    // otherwise just put it on the stack
    SDLMain fmain{*appDesc, std::move(arg_map), std::move(arg_cmdline)};
    if(fmain.initialize() == SDL_APP_FAILURE) {
        SDL_AppQuit(&fmain, SDL_APP_FAILURE);
    }

    constexpr int SIZE_EVENTS = 64;
    std::array<SDL_Event, SIZE_EVENTS> events{};

    int eventCount = 0;

    while(fmain.isRunning()) {
        VPROF_MAIN();
        {
            // event collection
            VPROF_BUDGET("SDL", VPROF_BUDGETGROUP_EVENTS);
            eventCount = 0;

            {
                VPROF_BUDGET("SDL_PumpEvents", VPROF_BUDGETGROUP_EVENTS);
                SDL_PumpEvents();
            }
            do {
                {
                    VPROF_BUDGET("SDL_PeepEvents", VPROF_BUDGETGROUP_EVENTS);
                    eventCount = SDL_PeepEvents(&events[0], SIZE_EVENTS, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST);
                }
                {
                    VPROF_BUDGET("handleEvent", VPROF_BUDGETGROUP_EVENTS);
                    for(int i = 0; i < eventCount; ++i) fmain.handleEvent(&events[i]);
                }
            } while(eventCount == SIZE_EVENTS);
        }
        {
#ifdef WITH_LIVEPP
            if(lppAgent.WantsReload(lpp::LPP_RELOAD_OPTION_SYNCHRONIZE_WITH_RELOAD)) {
                // XXX: Should pause/restart threads here instead of just yoloing
                lppAgent.Reload(lpp::LPP_RELOAD_BEHAVIOUR_WAIT_UNTIL_CHANGES_ARE_APPLIED);
            }

            if(lppAgent.WantsRestart()) {
                // XXX: Not sure if this works, but I don't think I'll be using live++ restart
                SDLMain::restart(arg_cmdline);
                lppAgent.Restart(lpp::LPP_RESTART_BEHAVIOUR_INSTANT_TERMINATION, 0u, nullptr);
            }
#endif

            // engine update + draw + fps limiter
            fmain.iterate();
        }
    }

    // i don't think this is reachable, but whatever
    // (we should hit SDL_AppQuit before this)
    if(fmain.isRestartScheduled()) {
        SDLMain::restart(arg_cmdline);
    }

#ifdef WITH_LIVEPP
    lpp::LppDestroySynchronizedAgent(&lppAgent);
#endif

    return 0;
#endif  // SDL_MAIN_USE_CALLBACKS
}
