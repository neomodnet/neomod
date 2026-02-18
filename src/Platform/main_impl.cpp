// Copyright (c) 2025, WH, All rights reserved.
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_properties.h>

#define SDL_h_

#include "environment_private.h"

#include "App.h"

#include "RuntimePlatform.h"
#include "Timing.h"
#include "MakeDelegateWrapper.h"
#include "Engine.h"
#include "ConVar.h"
#include "FPSLimiter.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "Profiler.h"
#include "Logging.h"
#include "GPUDriverConfigurator.h"
#include "Graphics.h"
#include "SString.h"
#include "Parsing.h"

#ifdef MCENGINE_PLATFORM_WASM
#include <emscripten/em_js.h>

// EM_ASM_INT doesn't work in CI, if you have too much time feel free to find out why
EM_JS(int, js_get_canvas_width, (), { return document.getElementById('canvas').width; });
EM_JS(int, js_get_canvas_height, (), { return document.getElementById('canvas').height; });
#endif

// for sending keys synthetically from console
static void sendkey(std::string_view keyName) {
    SDL_Scancode sc = SDL_GetScancodeFromName(std::string(keyName).c_str());
    if(sc == SDL_SCANCODE_UNKNOWN) {
        // try parsing as numeric scancode
        if(auto num = Parsing::strto<int>(keyName); num > 0 && num < SDL_SCANCODE_COUNT) {
            sc = static_cast<SDL_Scancode>(num);
        } else {
            debugLog("unknown key '{}'", keyName);
            return;
        }
    }

    SDL_Event ev{};
    ev.key.type = SDL_EVENT_KEY_DOWN;
    ev.key.timestamp = Timing::getTicksNS();
    ev.key.scancode = sc;
    ev.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
    ev.key.down = true;
    ev.key.repeat = false;
    ev.key.windowID = SDL_GetWindowID(SDL_GetKeyboardFocus());
    SDL_PushEvent(&ev);

    ev.key.type = SDL_EVENT_KEY_UP;
    ev.key.down = false;
    ev.key.timestamp = ev.key.timestamp + 1;
    SDL_PushEvent(&ev);
}

static void sendtext(std::string_view text) {
    SDL_Event ev{};
    ev.text.type = SDL_EVENT_TEXT_INPUT;
    ev.text.timestamp = Timing::getTicksNS();
    ev.text.windowID = SDL_GetWindowID(SDL_GetKeyboardFocus());

    // SDL_EVENT_TEXT_INPUT expects a null-terminated string
    // (doesn't copy it)
    static std::array<std::string, 16> strings;
    static int stringsIndex = 0;

    const std::string &current = strings[stringsIndex] = text;
    stringsIndex = (stringsIndex + 1) % 16;

    ev.text.text = current.c_str();
    SDL_PushEvent(&ev);
}

namespace cv {
static ConVar sendkey_cmd("sendkey", CLIENT | NOLOAD | NOSAVE, CFUNC(sendkey));
static ConVar sendtext_cmd("sendtext", CLIENT | NOLOAD | NOSAVE, CFUNC(sendtext));
}  // namespace cv

SDLMain::SDLMain(const Mc::AppDescriptor &appDesc, std::unordered_map<std::string, std::optional<std::string>> argMap,
                 std::vector<std::string> argVec)
    : Environment(appDesc, std::move(argMap), std::move(argVec)),
      m_gpuConfigurator(std::make_unique<GPUDriverConfigurator>()) {
    // the reason we set up GPUDriverConfigurator here is because some things it does might need to happen before the window itself is created
    // setup callbacks
    cv::fps_max.setCallback(SA::MakeDelegate<&SDLMain::fps_max_callback>(this));
    cv::fps_max_background.setCallback(SA::MakeDelegate<&SDLMain::fps_max_background_callback>(this));
}

SDLMain::~SDLMain() {
    if constexpr(Env::cfg(OS::WINDOWS) && !Env::cfg(FEAT::MAINCB)) {
        SDL_RemoveEventWatch(SDLMain::resizeCallback, this);
    }

    m_engine.reset();

    // clean up GL context
    if(m_context) {
        SDL_GL_DestroyContext(m_context);
        m_context = nullptr;
    }
    // close/delete the window
    if(m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

void SDLMain::setFgFPS() {
    if constexpr(Env::cfg(OS::WASM)) {
        // actually just set it to 0 (requestAnimationFrame) for WASM
        SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "0");
        return;
    }

    if constexpr(Env::cfg(FEAT::MAINCB)) {
        SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, fmt::format("{}", m_iFpsMax).c_str());
    } else {
        FPSLimiter::reset();
    }
}

void SDLMain::setBgFPS() {
    if constexpr(Env::cfg(FEAT::MAINCB))
        SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, fmt::format("{}", m_iFpsMaxBG).c_str());
}

// convar change callbacks, to set app iteration rate
void SDLMain::fps_max_callback(float newVal) {
    int newFps = static_cast<int>(newVal);
    if((newFps == 0 || newFps >= 30)) m_iFpsMax = newFps;
    if(winFocused()) setFgFPS();
}

void SDLMain::fps_max_background_callback(float newVal) {
    int newFps = static_cast<int>(newVal);
    if(newFps >= 0) m_iFpsMaxBG = newFps;
    if(!winFocused()) setBgFPS();
}

SDL_AppResult SDLMain::initialize() {
    setupLogging();

    // WASM headless (Node.js): no window, no GL, no events, just engine + app
    if constexpr(Env::cfg(OS::WASM)) {
        if(isHeadless()) {
            m_engine = std::make_unique<Engine>();
            if(!m_engine || m_engine->isShuttingDown()) return SDL_APP_FAILURE;
            m_engine->loadApp();
            return SDL_APP_CONTINUE;
        }
    }

    // create window with props
    if(!createWindow()) {
        return SDL_APP_FAILURE;
    }

    // disable (filter) some SDL events we don't care about
    configureEvents();

    // initialize engine, now that all the setup is done
    m_engine = std::make_unique<Engine>();

    if(!m_engine || m_engine->isShuttingDown()) {
        return SDL_APP_FAILURE;
    }

    // set up platform-specific integrations (IPC, hotkeys, etc.)
    // must be after engine creation since it may use networkHandler
    m_interop->setup_system_integrations();

    // delay info until we know what gpu we're running
    // currently this only does anything for nvidia
    if(!m_gpuConfigurator->getInitInfo().empty()) {
        std::string graphicsVendor{g->getModel().toUtf8()};
        if(SString::contains_ncase(graphicsVendor, "nvidia")) {
            logRaw("[GPUDriverConfigurator]: {}", m_gpuConfigurator->getInitInfo());
        }
    }
    // if we got to this point, all relevant subsystems (input handling, graphics interface, etc.) have been initialized

    // load app
    m_engine->loadApp();

    // make window visible now, after we loaded the config and set the wanted window size & fullscreen state
    // (unless running headless, then just never show the window)
    if(!isHeadless()) {
        SDL_ShowWindow(m_window);
        SDL_RaiseWindow(m_window);
    }

    syncWindow();

    updateWindowStateCache();

    // clear spurious window minimize/unfocus events accumulated during startup
    SDL_PumpEvents();
    SDL_FlushEvent(SDL_EVENT_WINDOW_MINIMIZED);
    SDL_FlushEvent(SDL_EVENT_WINDOW_FOCUS_LOST);

    updateWindowStateCache();

    // initialize mouse position
    {
        float x{0.f}, y{0.f};
        SDL_GetGlobalMouseState(&x, &y);
        vec2 posInWindow = vec2{x, y} - getWindowPos();

        setOSMousePos(posInWindow);
        mouse->onPosChange(posInWindow);
    }

    // SDL3 stops listening to text input globally when window is created
    listenToTextInput(cv::use_ime.getBool());
    SDL_SetWindowKeyboardGrab(m_window, false);  // this allows windows key and such to work

    // set up live-resize event callback
    if constexpr(Env::cfg(OS::WINDOWS) && !Env::cfg(FEAT::MAINCB)) {
        SDL_AddEventWatch(SDLMain::resizeCallback, this);
    }

    // return init success
    return SDL_APP_CONTINUE;
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif

static_assert(SDL_EVENT_WINDOW_FIRST == SDL_EVENT_WINDOW_SHOWN);
static_assert(SDL_EVENT_WINDOW_LAST == SDL_EVENT_WINDOW_HDR_STATE_CHANGED);

SDL_AppResult SDLMain::handleEvent(SDL_Event *event) {
    using namespace flags::operators;

    if(m_bEnvDebug) {
        static std::array<char, 512> logBuf{};
        size_t logsz =
            std::min(logBuf.size(), static_cast<size_t>(SDL_GetEventDescription(event, logBuf.data(), logBuf.size())));
        if(logsz > 0) {
            logRaw("[handleEvent] frame: {}; event: {}"_cf, m_engine->getFrameCount(),
                   std::string_view{logBuf.data(), logsz});
        }
    }

    switch(event->type) {
        case SDL_EVENT_QUIT: {
            SDL_Window *source = SDL_GetWindowFromEvent(event);
            if(source && source != m_window) break;
            if(m_bRunning) {
                m_bRunning = false;
                if(m_engine && !m_engine->isShuttingDown()) {
                    m_engine->shutdown();
                }
                if constexpr(Env::cfg(FEAT::MAINCB))
                    return SDL_APP_SUCCESS;
                else
                    SDL_AppQuit(this, SDL_APP_SUCCESS);
            }
        } break;

            // drag-drop events
            // clang-format off
        case SDL_EVENT_DROP_FILE: case SDL_EVENT_DROP_TEXT: case SDL_EVENT_DROP_BEGIN:
        case SDL_EVENT_DROP_COMPLETE: case SDL_EVENT_DROP_POSITION:
            // clang-format on
            switch(event->drop.type) {
                case SDL_EVENT_DROP_BEGIN: {
                    m_vDroppedData.clear();
                } break;
                case SDL_EVENT_DROP_COMPLETE: {
                    m_interop->handle_cmdline_args(m_vDroppedData);
                    m_vDroppedData.clear();
                } break;
                case SDL_EVENT_DROP_TEXT:
                case SDL_EVENT_DROP_FILE: {
                    std::string dropped_data{event->drop.data};
                    if(dropped_data.length() < 2) {
                        break;
                    }
                    m_vDroppedData.push_back(dropped_data);
                    if(m_bEnvDebug) {
                        std::string logString =
                            fmt::format("DEBUG: got SDL drag-drop event {}, current dropped_data queue is ",
                                        static_cast<int>(event->drop.type));
                        for(const auto &d : m_vDroppedData) {
                            logString += fmt::format("{}", d);
                        }
                        logString += ".";
                        debugLog(logString);
                    }
                } break;
                case SDL_EVENT_DROP_POSITION:  // we don't really care
                default:
                    if(m_bEnvDebug)
                        debugLog("DEBUG: unhandled SDL drag-drop event {}", static_cast<int>(event->drop.type));
                    break;
            }
            break;

            // window events (i hate you msvc ffs)
            // clang-format off
        case SDL_EVENT_WINDOW_SHOWN:				 case SDL_EVENT_WINDOW_HIDDEN:			  case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_MOVED:				 case SDL_EVENT_WINDOW_RESIZED:			  case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:	 case SDL_EVENT_WINDOW_MINIMIZED:		  case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:				 case SDL_EVENT_WINDOW_MOUSE_ENTER:		  case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:			 case SDL_EVENT_WINDOW_FOCUS_LOST:		  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        case SDL_EVENT_WINDOW_HIT_TEST:				 case SDL_EVENT_WINDOW_ICCPROF_CHANGED:	  case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED: case SDL_EVENT_WINDOW_OCCLUDED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:		 case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:  case SDL_EVENT_WINDOW_DESTROYED:
        case SDL_EVENT_WINDOW_HDR_STATE_CHANGED:
            // clang-format on
            // just do this on any window/display event, who knows which events really change which state
            // it's not too expensive anyways since we shouldn't be getting these events spammed all the time
            updateWindowStateCache();
            switch(event->window.type) {
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                    SDL_Window *source = SDL_GetWindowFromEvent(event);
                    if(source && source != m_window) break;
                    if(m_bRunning) {
                        m_engine->shutdown();
                    }
                } break;

                case SDL_EVENT_WINDOW_FOCUS_GAINED: {
                    // add these window flags now to make env->winFocused() return true after this
                    m_winflags |= (WinFlags::F_MOUSE_FOCUS | WinFlags::F_INPUT_FOCUS);
                    // this check seems flaky and not required on windows, as far as i can tell
                    if(!Env::cfg(OS::WINDOWS) && (m_bRestoreFullscreen && !winMinimized())) {
                        debugLog(
                            "DEBUG: window not minimized while in restore fullscreen state, ignoring future minimize "
                            "requests.");
                        // we can get into this state if the current window manager doesn't support minimizing
                        // (i.e. re-gaining focus without first being restored, after we unfullscreened and tried to minimize the window)
                        // re-fullscreen once, then set a flag to ignore future minimize requests
                        m_bRestoreFullscreen = false;
                        m_bMinimizeSupported = false;
                        enableFullscreen();
                    }
                    m_engine->onFocusGained();
                    setFgFPS();
                } break;

                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    // remove these window flags now to avoid env->winFocused() returning true immediately
                    m_winflags &= ~(WinFlags::F_MOUSE_FOCUS | WinFlags::F_INPUT_FOCUS);
                    m_engine->onFocusLost();
                    setBgFPS();
                    break;

                case SDL_EVENT_WINDOW_MAXIMIZED:
                    m_engine->onMaximized();
                    setFgFPS();
                    break;

                case SDL_EVENT_WINDOW_MOUSE_ENTER:
                    m_bIsCursorInsideWindow = true;
                    if(m_bHideCursorPending) {
                        setCursorVisible(false);
                    }
                    break;

                case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                    m_bIsCursorInsideWindow = false;
                    setCursorVisible(true);
                    break;

                case SDL_EVENT_WINDOW_MINIMIZED:
                    m_engine->onMinimized();
                    setBgFPS();
                    break;

                case SDL_EVENT_WINDOW_RESTORED:
                    if(m_bRestoreFullscreen) {
                        m_bRestoreFullscreen = false;
                        enableFullscreen();
                    }
                    m_engine->onRestored();
                    setFgFPS();
                    break;

                case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
                    cv::fullscreen.setValue(true, false);
                    m_bRestoreFullscreen = false;
                    break;

                case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
                    cv::fullscreen.setValue(false, false);
                    if(!m_bRestoreFullscreen) {  // make sure we re-add window borders, unless we're in the minimize-on-focus-lost-hack-state
                        SDL_SetWindowBordered(m_window, true);
                    }
                    break;

                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED: {  // not really sure what to do with SAFE_AREA_CHANGED
                    // ignore these events if we are minimized or waiting to be restored to fullscreen
                    if(!winMinimized() && !m_bRestoreFullscreen) {
                        const SDL_EventType type = event->window.type;
                        vec2 actualResize{};
                        if(type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                            onDPIChange();  // TODO: look into this (not sure if its correct)
                        }
                        // SAFE_AREA_CHANGED doesn't have data1/data2 filled
                        if(type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED) {
                            if(winFullscreened()) {
                                actualResize = getNativeScreenSize();
                            } else {
                                actualResize = getWindowSize();
                            }
                        } else {
                            actualResize = vec2{(float)event->window.data1, (float)event->window.data2};
                        }
                        m_engine->requestResolutionChange(actualResize);
                        setFgFPS();
                    }
                } break;

                case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                    cv::monitor.setValue(event->window.data1, false);
                    m_engine->requestResolutionChange(getWindowSize());
                    m_fDisplayHzSecs = 1.0f / (m_fDisplayHz = queryDisplayHz());
                    break;

                case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                    onDPIChange();
                    break;

                case SDL_EVENT_WINDOW_EXPOSED: {
                    if constexpr(Env::cfg(OS::WINDOWS)) {
                        if(event->window.data1 == 1 /* live resize event */ && !winMinimized() &&
                           !m_bRestoreFullscreen) {
                            m_engine->requestResolutionChange(getWindowSize());
                            iterate();
                            break;
                        }
                    }
                }  // fallthrough

                default:
                    if(m_bEnvDebug)
                        debugLog("DEBUG: unhandled SDL window event {}", static_cast<int>(event->window.type));
                    break;
            }
            if(m_bEnvDebug) {  // print out current window flags after
                logRaw(fmt::format("[handleEvent] current window flags: {}", windowFlagsDbgStr()));
            }
            break;

            // display events
            // clang-format off
        case SDL_EVENT_DISPLAY_ORIENTATION:			  case SDL_EVENT_DISPLAY_ADDED:				   case SDL_EVENT_DISPLAY_REMOVED:
        case SDL_EVENT_DISPLAY_MOVED:				  case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED: case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
        case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
            // clang-format on
            updateWindowStateCache();
            switch(event->display.type) {
                case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
                    onDPIChange();
                    // fallthrough
                default:
                    // reinit monitors, and update hz in any case
                    initMonitors(true);
                    m_fDisplayHzSecs = 1.0f / (m_fDisplayHz = queryDisplayHz());
                    break;
            }
            break;

        // keyboard events
        case SDL_EVENT_KEY_DOWN:
            keyboard->onKeyDown({static_cast<SCANCODE>(event->key.scancode), static_cast<char16_t>(event->key.key),
                                 event->key.timestamp, event->key.repeat});
            break;

        case SDL_EVENT_KEY_UP:
            keyboard->onKeyUp({static_cast<SCANCODE>(event->key.scancode), static_cast<char16_t>(event->key.key),
                               event->key.timestamp, event->key.repeat});
            break;

        case SDL_EVENT_TEXT_INPUT: {
            const char *evtextstr = event->text.text;
            if(unlikely(!evtextstr || *evtextstr == '\0'))
                break;  // probably should be assert() but there's no point in microoptimizing that hard

            int length;
            if(likely((length = strlen(evtextstr)) == 1)) {
                keyboard->onChar({0, static_cast<char16_t>(evtextstr[0]), event->text.timestamp, false});
            } else {
                for(const auto &chr : UString(evtextstr, length))
                    keyboard->onChar({0, chr, event->text.timestamp, false});
            }
        } break;

        // mouse events
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            mouse->onButtonChange({event->button.timestamp, (MouseButtonFlags)(1 << (event->button.button - 1)), true});
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            mouse->onButtonChange(
                {event->button.timestamp, (MouseButtonFlags)(1 << (event->button.button - 1)), false});
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            if(event->wheel.x != 0)
                mouse->onWheelHorizontal(event->wheel.x > 0 ? 120 * std::abs(static_cast<int>(event->wheel.x))
                                                            : -120 * std::abs(static_cast<int>(event->wheel.x)));
            if(event->wheel.y != 0)
                mouse->onWheelVertical(event->wheel.y > 0 ? 120 * std::abs(static_cast<int>(event->wheel.y))
                                                          : -120 * std::abs(static_cast<int>(event->wheel.y)));
            break;

        case SDL_EVENT_PEN_MOTION:
            m_vCurrentAbsPenPos = vec2{event->pmotion.x, event->pmotion.y};
            break;

        default:
            if(m_bEnvDebug) debugLog("DEBUG: unhandled SDL event {}", event->type);
            break;
    }

    return SDL_APP_CONTINUE;
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

SDL_AppResult SDLMain::iterate() {
    if(!m_bRunning) return SDL_APP_SUCCESS;

    // WASM: measure true display Hz from rAF frame intervals (after init settles)
    if constexpr(Env::cfg(OS::WASM)) {
        if(!isHeadless()) calibrateDisplayHzWASM();
    }

    // update
    {
        m_engine->onUpdate();
    }

    // draw
    // (always draw in headless to make it more realistic/representative)
    if(m_bHeadless || (!winMinimized() && !m_bRestoreFullscreen)) {
        m_engine->onPaint();
    }

    if constexpr(!Env::cfg(FEAT::MAINCB))  // main callbacks use SDL iteration rate to limit fps
    {
        VPROF_BUDGET("FPSLimiter", VPROF_BUDGETGROUP_SLEEP);

        // if minimized or unfocused, use BG fps, otherwise use fps_max (if 0 it's unlimited)
        const bool minimizedOrUnfocused = winMinimized() || !winFocused();
        const bool inActiveGameplay = !minimizedOrUnfocused && (app && app->isInGameplay());
        const int targetFPS =
            minimizedOrUnfocused ? m_iFpsMaxBG : (inActiveGameplay ? m_iFpsMax : cv::fps_max_menu.getInt());
        FPSLimiter::limit_frames(targetFPS, /*precise_sleeps=*/inActiveGameplay);
    }

    return SDL_APP_CONTINUE;
}

// window configuration
static constexpr auto WINDOW_TITLE = PACKAGE_NAME;
static constexpr auto WINDOW_WIDTH = 1280L;
static constexpr auto WINDOW_HEIGHT = 720L;
static constexpr auto WINDOW_WIDTH_MIN = 320;
static constexpr auto WINDOW_HEIGHT_MIN = 240;

bool SDLMain::createWindow() {
    // pre window-creation settings
    if(usingGL()) {  // these are only for opengl
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);

        // we don't need alpha on the window visual, this is only required for making the window itself transparent
        // due to driver/external bugs and issues, only do this IF:
        // NOT windows (makes the game overly dark for some reason with some drivers)
        // OR running under wine (it does not find a suitable pixel format with xwayland+EGL otherwise! wine/driver bug)
        if(!Env::cfg(OS::WINDOWS) || (RuntimePlatform::current() & RuntimePlatform::WIN_WINE)) {
            SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
        }

        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            Env::cfg(REND::GLES32) ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        if constexpr(Env::cfg(REND::GLES32)) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        }  // otherwise just leave it as whatever is default

        // setup antialiasing from -aa command line argument
        if(m_mArgMap.contains("-aa") && m_mArgMap["-aa"].has_value()) {
            auto aaSamples = Parsing::strto<u64>(m_mArgMap["-aa"].value());
            if(aaSamples > 1) {
                aaSamples = std::clamp(std::bit_floor(aaSamples), (u64)2, (u64)16);
                SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
                SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, static_cast<int>(aaSamples));
            }
        }
        // create gl debug context
        if(m_mArgMap.contains("-debugctx")) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
            SDL_SetLogPriority(SDL_LOG_CATEGORY_VIDEO, SDL_LOG_PRIORITY_TRACE);
        } else {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_NO_ERROR, 1);
        }
    }

    // set vulkan for linux dxvk-native, opengl otherwise (or none for windows dx11)
    const i64 windowFlags =
        SDL_WINDOW_HIDDEN | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS |
        (m_bDPIOverride ? 0LL : SDL_WINDOW_HIGH_PIXEL_DENSITY) |  // respect -nodpi
        (usingGL() ? SDL_WINDOW_OPENGL : ((Env::cfg(OS::LINUX) && usingDX11()) ? SDL_WINDOW_VULKAN : 0LL));

    // limit default window size so it fits the screen
    i32 windowCreateWidth = WINDOW_WIDTH;
    i32 windowCreateHeight = WINDOW_HEIGHT;
    SDL_DisplayID initDisplayID = SDL_GetPrimaryDisplay();

    // start on the highest refresh rate monitor for kmsdrm (or if the primary display couldn't be found)
    if(m_bIsKMSDRM || !initDisplayID) {
        if(!m_bIsKMSDRM) {
            debugLog("NOTICE: Couldn't get primary display: {}", SDL_GetError());
        }
        int dispCount = 0;
        float maxHz = 0;
        SDL_DisplayID *ids = SDL_GetDisplays(&dispCount);
        for(int i = 0; i < dispCount; i++) {
            const SDL_DisplayMode *currentDisplayMode = SDL_GetCurrentDisplayMode(ids[i]);
            if(currentDisplayMode && currentDisplayMode->refresh_rate >= maxHz) {
                maxHz = currentDisplayMode->refresh_rate;
                initDisplayID = currentDisplayMode->displayID;
                windowCreateWidth = currentDisplayMode->w;
                windowCreateHeight = currentDisplayMode->h;
            }
        }
        SDL_free(ids);
    } else {
        const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(initDisplayID);
        if(dm) {
            if(dm->w < windowCreateWidth) windowCreateWidth = dm->w;
            if(dm->h < windowCreateHeight) windowCreateHeight = dm->h;
        }
    }

#ifdef MCENGINE_PLATFORM_WASM
    // Set the initial window size to the browser's current canvas render size
    // If we don't, emscripten will overwrite canvas.width/height to 1280x720, which will always be wrong
    //
    // By manually getting the attributes of the canvas element, we get the render size,
    // as opposed to the CSS size which is incorrect on HiDPI.
    const i32 tempWidth = js_get_canvas_width();
    const i32 tempHeight = js_get_canvas_height();

    // emrun starts with 300x300
    if(tempWidth > 480 && tempHeight > 320) {
        windowCreateWidth = tempWidth;
        windowCreateHeight = tempHeight;
    }
#endif

    // set this size as the initial fallback window size (for Environment::getWindowSize())
    m_vLastKnownWindowSize = vec2{static_cast<float>(windowCreateWidth), static_cast<float>(windowCreateHeight)};

    SDL_PropertiesID props = SDL_CreateProperties();
    if(usingDX11()) SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, true);
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, WINDOW_TITLE);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(initDisplayID));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(initDisplayID));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, windowCreateWidth);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, windowCreateHeight);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_MAXIMIZED_BOOLEAN, false);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, m_bIsKMSDRM ? true : false);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, false);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, windowFlags);

    if constexpr(Env::cfg(OS::LINUX)) {
        SDL_SetHintWithPriority(SDL_HINT_MOUSE_AUTO_CAPTURE, "0", SDL_HINT_NORMAL);
    } else if constexpr(Env::cfg(OS::WASM)) {
        SDL_SetHintWithPriority(SDL_HINT_MOUSE_AUTO_CAPTURE, "1", SDL_HINT_NORMAL);
    }

    SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_MODE_CENTER, "0", SDL_HINT_NORMAL);
    SDL_SetHintWithPriority(SDL_HINT_TOUCH_MOUSE_EVENTS, "0", SDL_HINT_NORMAL);
    SDL_SetHintWithPriority(SDL_HINT_MOUSE_EMULATE_WARP_WITH_RELATIVE, "0", SDL_HINT_NORMAL);
    // don't conflict with our handling of it
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0", SDL_HINT_NORMAL);

    // create window
    m_window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);

    if(m_window == nullptr) {
        debugLog("Couldn't SDL_CreateWindow(): {:s}", SDL_GetError());
        return false;
    }

    m_windowID = SDL_GetWindowID(m_window);

    if(m_bIsKMSDRM) {
        cv::monitor.setValue(initDisplayID, false);
    } else {
        cv::monitor.setValue(SDL_GetDisplayForWindow(m_window), false);
    }

    // create gl context
    if(usingGL()) {
        m_context = SDL_GL_CreateContext(m_window);
        if(!m_context) {
            debugLog("Couldn't create OpenGL context: {:s}", SDL_GetError());
            return false;
        }
        if(!SDL_GL_MakeCurrent(m_window, m_context)) {
            debugLog("Couldn't make OpenGL context current: {:s}", SDL_GetError());
            return false;
        }
    }

    if(m_bIsKMSDRM) {
        SDL_SetWindowMinimumSize(m_window, windowCreateWidth, windowCreateHeight);
    } else {
        SDL_SetWindowMinimumSize(m_window, WINDOW_WIDTH_MIN, WINDOW_HEIGHT_MIN);
    }

    // initialize with the display refresh rate of the current monitor
    m_fDisplayHzSecs = 1.0f / (m_fDisplayHz = queryDisplayHz());

    // wait for calibration to finish before setting custom iteration rates in WASM
    if constexpr(!Env::cfg(OS::WASM)) {
        const auto hz = std::round(m_fDisplayHz);
        const auto fourxhz = std::round(std::clamp<float>(hz * 4.0f, hz, 1000.0f));

        // also set fps_max to 4x the refresh rate
        cv::fps_max.setDefaultDouble(fourxhz);
        cv::fps_max.setValue(fourxhz);
        cv::fps_max_menu.setDefaultDouble(hz);
        cv::fps_max_menu.setValue(hz);
    } else {
        cv::fps_max.setDefaultDouble(0.);
        cv::fps_max.setValue(0.);
        cv::fps_max_menu.setDefaultDouble(0.);
        cv::fps_max_menu.setValue(0.);

        // set it to 0
        SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "0");
    }

    // init dpi
    m_fDisplayScale = SDL_GetWindowDisplayScale(m_window);
    m_fPixelDensity = SDL_GetWindowPixelDensity(m_window);

    // initialize window flags and state
    updateWindowStateCache();

    return true;
}

void SDLMain::calibrateDisplayHzWASM() {
    // redundant check to make sure this gets compiled out otherwise
    if constexpr(!Env::cfg(OS::WASM)) return;

    constexpr uint64_t kInitDelayNS = WASM_HZ_INIT_DELAY_SECONDS * Timing::NS_PER_SECOND;
    if(m_iHzMeasureFrames < WASM_HZ_FRAMES_TO_MEASURE) {
        const auto now = Timing::getTicksNS();
        if(m_iHzMeasureFrames == -1) {
            // wait for init to settle before measuring
            if(m_iHzMeasureStartNS == 0)
                m_iHzMeasureStartNS = now;
            else if(now - m_iHzMeasureStartNS >= kInitDelayNS)
                m_iHzMeasureFrames = 0;
        }
        if(m_iHzMeasureFrames == 0) {
            m_iHzMeasureStartNS = now;
        }
        if(m_iHzMeasureFrames >= 0) m_iHzMeasureFrames++;
        if(m_iHzMeasureFrames == WASM_HZ_FRAMES_TO_MEASURE) {
            const auto elapsedNS = now - m_iHzMeasureStartNS;
            const double avgFrameSecs =
                static_cast<double>(elapsedNS) / Timing::NS_PER_SECOND / (WASM_HZ_FRAMES_TO_MEASURE - 1);
            if(avgFrameSecs > 0.0) {
                const auto measuredHz = std::clamp(static_cast<float>(1.0 / avgFrameSecs), 30.0f, 540.0f);

                debugLog("Measured display refresh rate: {:.1f} Hz", measuredHz);
                m_fDisplayHzSecs = 1.0f / (m_fDisplayHz = measuredHz);

                // update these to the true result
                const auto hz = std::round(m_fDisplayHz);
                const auto fourxhz = std::round(std::clamp<float>(hz * 4.0f, hz, 1000.0f));

                cv::fps_max.setDefaultDouble(fourxhz);
                cv::fps_max.setValue(fourxhz);
                cv::fps_max_menu.setDefaultDouble(hz);
                cv::fps_max_menu.setValue(hz);
                setFgFPS();
            }
        }
    }
}

float SDLMain::queryDisplayHz() {
    // on WASM, once we've measured from rAF intervals, keep that value
    if constexpr(Env::cfg(OS::WASM)) {
        if(m_iHzMeasureFrames >= WASM_HZ_FRAMES_TO_MEASURE) return m_fDisplayHz;
    } else {  // get the screen refresh rate, and set fps_max to that as default
        // doesn't work in wasm
        const SDL_DisplayID display = SDL_GetDisplayForWindow(m_window);
        const SDL_DisplayMode *currentDisplayMode = [display, window = m_window]() -> const SDL_DisplayMode * {
            // fallbacks
            if(!display) {
                return SDL_GetWindowFullscreenMode(window);
            }

            if(const auto *curdisp = SDL_GetCurrentDisplayMode(display); curdisp) {
                return curdisp;
            } else if(const auto *curdesktop = SDL_GetDesktopDisplayMode(display); curdesktop) {
                return curdesktop;
            } else {
                return SDL_GetWindowFullscreenMode(window);
            }

            return nullptr;
        }();

        if(currentDisplayMode && currentDisplayMode->refresh_rate > 0) {
            if((m_fDisplayHz > currentDisplayMode->refresh_rate + 0.01) ||
               (m_fDisplayHz < currentDisplayMode->refresh_rate - 0.01)) {
                debugLog("Got refresh rate {:.3f} Hz for display {:d}.", currentDisplayMode->refresh_rate, display);
            }
            const auto refreshRateSanityClamped = std::clamp<float>(currentDisplayMode->refresh_rate, 60.0f, 540.0f);
            return refreshRateSanityClamped;
        } else {
            static int once;
            if(!once++)
                debugLog("Couldn't SDL_GetCurrentDisplayMode(SDL display: {:d}): {:s}", display, SDL_GetError());
        }
    }
    // if we couldn't get the refresh rate (or in wasm pre-measurement) just return a sane value to use for "vsync"-related calculations
    return std::clamp<float>(cv::fps_max.getFloat(), 60.0f, 360.0f);
}

void SDLMain::configureEvents() {
    // disable unused events
    SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, false);

    // joystick
    SDL_SetEventEnabled(SDL_EVENT_JOYSTICK_AXIS_MOTION, false);
    SDL_SetEventEnabled(SDL_EVENT_JOYSTICK_BALL_MOTION, false);
    SDL_SetEventEnabled(SDL_EVENT_JOYSTICK_HAT_MOTION, false);
    SDL_SetEventEnabled(SDL_EVENT_JOYSTICK_BUTTON_DOWN, false);
    SDL_SetEventEnabled(SDL_EVENT_JOYSTICK_BUTTON_UP, false);
    SDL_SetEventEnabled(SDL_EVENT_JOYSTICK_ADDED, false);
    SDL_SetEventEnabled(SDL_EVENT_JOYSTICK_REMOVED, false);
    SDL_SetEventEnabled(SDL_EVENT_JOYSTICK_BATTERY_UPDATED, false);
    SDL_SetEventEnabled(SDL_EVENT_JOYSTICK_UPDATE_COMPLETE, false);

    // pen
    SDL_SetEventEnabled(SDL_EVENT_PEN_PROXIMITY_IN, false);
    SDL_SetEventEnabled(SDL_EVENT_PEN_PROXIMITY_OUT, false);
    SDL_SetEventEnabled(SDL_EVENT_PEN_DOWN, false);
    SDL_SetEventEnabled(SDL_EVENT_PEN_UP, false);
    SDL_SetEventEnabled(SDL_EVENT_PEN_BUTTON_DOWN, false);
    SDL_SetEventEnabled(SDL_EVENT_PEN_BUTTON_UP, false);
    SDL_SetEventEnabled(SDL_EVENT_PEN_AXIS, false);

    // use pen motion events to track absolute cursor position
    SDL_SetEventEnabled(SDL_EVENT_PEN_MOTION, true);

    // allow callback to enable/disable pen input handling
    cv::pen_input.setCallback(
        [](float on) -> void { SDL_SetEventEnabled(SDL_EVENT_PEN_MOTION, !!static_cast<int>(on)); });

    // touch
    SDL_SetEventEnabled(SDL_EVENT_FINGER_DOWN, false);
    SDL_SetEventEnabled(SDL_EVENT_FINGER_UP, false);
    SDL_SetEventEnabled(SDL_EVENT_FINGER_MOTION, false);
    SDL_SetEventEnabled(SDL_EVENT_FINGER_CANCELED, false);

    // IME input
    SDL_SetEventEnabled(SDL_EVENT_TEXT_EDITING_CANDIDATES, cv::use_ime.getBool());
    SDL_SetEventEnabled(SDL_EVENT_TEXT_EDITING, cv::use_ime.getBool());

    // allow callback to enable/disable too
    cv::use_ime.setCallback(SA::MakeDelegate<&SDLMain::onUseIMEChange>(this));
}

void SDLMain::setupLogging() {
    static SDL_LogOutputFunction SDLLogCB = +[](void *, int category, SDL_LogPriority, const char *message) -> void {
        const char *catStr = "???";
        switch(category) {
            case SDL_LOG_CATEGORY_APPLICATION:
                catStr = "APP";
                break;
            case SDL_LOG_CATEGORY_ERROR:
                catStr = "ERR";
                break;
            case SDL_LOG_CATEGORY_SYSTEM:
                catStr = "SYS";
                break;
            case SDL_LOG_CATEGORY_AUDIO:
                catStr = "AUD";
                break;
            case SDL_LOG_CATEGORY_VIDEO:
                catStr = "VID";
                break;
            case SDL_LOG_CATEGORY_RENDER:
                catStr = "REN";
                break;
            case SDL_LOG_CATEGORY_INPUT:
                catStr = "INP";
                break;
            case SDL_LOG_CATEGORY_CUSTOM:
                catStr = "USR";
                break;
            default:
                break;
        }

        // avoid stray newlines
        std::string formatted = fmt::format("SDL[{}]: {}"_cf, catStr, message);
        while(formatted.back() == '\r' || formatted.back() == '\n') {
            formatted.pop_back();
        }

        logRaw(formatted);
    };

    SDL_SetLogOutputFunction(SDLLogCB, nullptr);
}

void SDLMain::shutdown(SDL_AppResult result) {
    if(result == SDL_APP_FAILURE)  // force quit now
        return;
    else if(m_window)
        SDL_StopTextInput(m_window);

    Environment::shutdown();
}

bool SDLMain::resizeCallback(void *userdata, SDL_Event *event) {
    assert(userdata && event);

    SDLMain *main_ptr;
    if(event->type != SDL_EVENT_WINDOW_EXPOSED || event->window.data1 != 1 /* live resize event */ ||
       (main_ptr = static_cast<SDLMain *>(userdata))->winMinimized() || main_ptr->m_bRestoreFullscreen) {
        return false;  // return value is ignored from AddEventWatch
    }
    assert(main_ptr->m_engine);
    main_ptr->updateWindowSizeCache();

    main_ptr->m_engine->requestResolutionChange(main_ptr->getWindowSize());
    main_ptr->iterate();

    return false;
}

#if defined(MCENGINE_PLATFORM_WINDOWS)
#if !defined(SDL_main_h_)
extern "C" SDL_DECLSPEC void SDLCALL SDL_UnregisterApp(void);
#endif
// avoid including windows.h here for 1 function
extern "C" __declspec(dllimport) char *__stdcall GetCommandLineA(void);
#endif

void SDLMain::restart(const std::vector<std::string> &args) {
    SDL_PropertiesID restartprops = SDL_CreateProperties();

    std::vector<const char *> restartArgsChar(args.size() + 1);

    restartArgsChar.back() = nullptr;

    for(int i = 0; const auto &arg : args) {
        restartArgsChar[i] = arg.c_str();
        i++;
    }

    if(cv::debug_env.getBool()) {
        std::string logString = "restart args: ";

        for(int i = -1; const auto entry : restartArgsChar) {
            i++;
            if(!entry) continue;
            logString += fmt::format("({}) {} ", i, entry);
        }
        logString += ".";
        debugLog(logString);
    }

    SDL_SetPointerProperty(restartprops, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)restartArgsChar.data());
    SDL_SetBooleanProperty(restartprops, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    if(s_sdlenv) {
        SDL_SetPointerProperty(restartprops, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, s_sdlenv);
    }

#ifdef MCENGINE_PLATFORM_WINDOWS
    const char *wincmdline = GetCommandLineA();
    if(wincmdline) {
        SDL_SetStringProperty(restartprops, SDL_PROP_PROCESS_CREATE_CMDLINE_STRING, wincmdline);
    }
    // so that handle_existing_window doesn't find the currently running instance by the class name
    SDL_UnregisterApp();
#endif

    if(!SDL_CreateProcessWithProperties(restartprops)) {
        debugLog("[restart]: WARNING: couldn't restart!");
    }

    SDL_DestroyProperties(restartprops);
}
