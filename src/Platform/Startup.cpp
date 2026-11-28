// Copyright (c) 2026, WH, All rights reserved.
#include "Startup.h"

#include "BaseEnvironment.h"
#include "Console.h"
#include "ConVar.h"
#include "ConVarHandler.h"
#include "ContainerRanges.h"
#include "Environment.h"
#include "File.h"
#include "LaunchArgs.h"
#include "Logging.h"
#include "Paths.h"
#include "RuntimePlatform.h"
#include "SString.h"
#include "i18n.h"

#include "fmt/format.h"
#include "Algorithms.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_messagebox.h>

#include <array>
#include <iterator>
#include <optional>
#include <span>
#include <utility>

#if defined(MCENGINE_PLATFORM_WINDOWS)
#include "WinDebloatDefs.h"
#include <windows.h>
#elif defined(MCENGINE_PLATFORM_MACOS)
#include <CoreGraphics/CGEventSource.h>
#elif defined(MCENGINE_PLATFORM_LINUX)
#include "dynutils.h"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#endif

using namespace std::string_view_literals;

namespace Mc::Startup {

namespace {
Settings s_settings;

constexpr Renderer GL_RENDERER{Env::cfg(REND::GLES32) ? Renderer::GLES : Renderer::GL};

// the progress file (see progress()): whether this start keeps one, and the renderers that stopped a start since the
// last one that got through
bool s_tracked{false};
std::vector<Renderer> s_stoppedIn;

std::string progressPath() { return Mc::Paths::data() + "/startup.state"; }

// every renderer this build has, the preferred one first
std::vector<Renderer> builtRenderers() {
    std::vector<Renderer> renderers;
    if(RuntimePlatform::current() & RuntimePlatform::WIN_WINE) {
        // opengl beats sdl_gpu under wine (d3d12 translation is higher overhead than opengl)
        if constexpr(Env::cfg(REND::GL | REND::GLES32)) renderers.push_back(GL_RENDERER);
        if constexpr(Env::cfg(REND::DX11)) renderers.push_back(Renderer::DX11);
        if constexpr(Env::cfg(REND::SDLGPU)) renderers.push_back(Renderer::SDLGPU);
    } else {
        if constexpr(Env::cfg(REND::SDLGPU)) renderers.push_back(Renderer::SDLGPU);
        if constexpr(Env::cfg(REND::DX11)) renderers.push_back(Renderer::DX11);
        if constexpr(Env::cfg(REND::GL | REND::GLES32)) renderers.push_back(GL_RENDERER);
    }
    return renderers;
}

// the APIs SDL_gpu runs on here, the one it prefers first: SDL's name for the driver, and what the UI calls it.
// vulkan on macOS needs a loader that isn't there by default
struct GPUAPI {
    std::string_view driver;
    std::string_view name;
};
std::span<const GPUAPI> sdlgpuAPIs() {
    static constexpr std::array WINDOWS{GPUAPI{"direct3d12", "Direct3D 12"}, GPUAPI{"vulkan", "Vulkan"}};
    static constexpr std::array MAC{GPUAPI{"metal", "Metal"}};
    static constexpr std::array OTHER{GPUAPI{"vulkan", "Vulkan"}};
    if constexpr(Env::cfg(OS::WINDOWS)) {
        return WINDOWS;
    } else if constexpr(Env::cfg(OS::MAC)) {
        return MAC;
    } else {
        return OTHER;
    }
}

// what the progress file calls a renderer: a start stops in SDL_gpu before the API it runs on is known
std::string_view progressName(Renderer renderer) { return rendererValue(renderer, "sdlgpu"); }

// the settings from the startup convars, with the switches on top
void resolve() {
    using LaunchArgs::has_arg;
    using enum LaunchArgs::ArgSwitch;

    s_settings = {};

    // a renderer this build doesn't have counts for nothing
    const std::optional<Renderer> switched = [] -> std::optional<Renderer> {
        if(Env::cfg(REND::DX11) && has_arg(REND_DX11)) return Renderer::DX11;
        if(Env::cfg(REND::SDLGPU) && has_arg(REND_SDLGPU)) return Renderer::SDLGPU;
        if(Env::cfg(REND::GL | REND::GLES32) && has_arg(REND_GL)) return GL_RENDERER;
        return std::nullopt;
    }();
    // r_renderer names a renderer of this build, SDL_gpu by the API it runs on
    const std::vector<Renderer> built = builtRenderers();
    std::optional<Renderer> chosen;
    std::string_view chosenDriver;
    for(const auto renderer : built) {
        if(renderer != Renderer::SDLGPU) {
            if(SString::strcase_equal(rendererValue(renderer, {}), cv::r_renderer.getString())) chosen = renderer;
            continue;
        }
        for(const auto driver : {"vulkan"sv, "direct3d12"sv, "metal"sv}) {
            if(!SString::strcase_equal(driver, cv::r_renderer.getString())) continue;
            chosen = renderer;
            chosenDriver = driver;
        }
    }

    auto &renderers = s_settings.renderers;
    if(Env::cfg(OS::WASM) && has_arg(REND_HEADLESS)) {
        renderers = {Renderer::NULLGRAPHICS};
    } else if(switched) {
        // what a switch asks for is all that gets tried: it's there to test that renderer
        renderers = {*switched};
        s_settings.rendererSwitched = true;
        s_settings.sdlgpuDriver = has_arg(REND_SDLGPU_VK)      ? "vulkan"sv
                                  : has_arg(REND_SDLGPU_D3D12) ? "direct3d12"sv
                                  : has_arg(REND_SDLGPU_MTL)   ? "metal"sv
                                                               : ""sv;
    } else {
        // the ones a start stopped in come last
        renderers = built;
        Mc::stable_partition(renderers.begin(), renderers.end(),
                             [](Renderer r) { return !Mc::ranges::contains(s_stoppedIn, r); });
        if(has_arg(REND_HEADLESS)) {
            // headless sets up its video driver for one renderer, which only a switch picks
            renderers.resize(1);
        } else if(chosen) {
            // the chosen one first, the others after it as usual
            const auto it = Mc::ranges::find(renderers, *chosen);
            Mc::rotate(renderers.begin(), it, std::next(it));
            s_settings.rendererChosen = true;
            s_settings.sdlgpuDriver = chosenDriver;
        }
    }

    s_settings.soundEngineSwitched = has_arg(SND_BASS) || has_arg(SND_SOLOUD);
    s_settings.preferBASS = s_settings.soundEngineSwitched ? has_arg(SND_BASS).has_value()
                                                           : SString::strcase_equal(cv::snd_engine.getString(), "bass");
    s_settings.highDPI = !has_arg(MISC_NO_DPI) && cv::win_highdpi.getBool();
    s_settings.dx11Exclusive = has_arg(MISC_DX11_EXCLUSIVE) || cv::r_dx11_exclusive.getBool();
}

// whether shift is held right now, asked before any window exists (false where that can't be told)
bool shiftHeld() {
#if defined(MCENGINE_PLATFORM_WINDOWS)
    return GetAsyncKeyState(VK_SHIFT) & 0x8000;
#elif defined(MCENGINE_PLATFORM_MACOS)
    return CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState) & kCGEventFlagMaskShift;
#elif defined(MCENGINE_PLATFORM_LINUX)
    // only x11 lets anyone see the keyboard (xwayland only sees what goes to its own windows)
    if(!Environment::getEnvVariable("WAYLAND_DISPLAY").empty()) return false;
    dynutils::lib_obj *x11 = dynutils::load_lib_system("libX11.so.6");
    if(!x11) return false;
    bool held = false;
    const auto openDisplay = dynutils::load_func<decltype(XOpenDisplay)>(x11, "XOpenDisplay");
    const auto queryKeymap = dynutils::load_func<decltype(XQueryKeymap)>(x11, "XQueryKeymap");
    const auto keysymToKeycode = dynutils::load_func<decltype(XKeysymToKeycode)>(x11, "XKeysymToKeycode");
    const auto closeDisplay = dynutils::load_func<decltype(XCloseDisplay)>(x11, "XCloseDisplay");
    if(Display *display =
           openDisplay && queryKeymap && keysymToKeycode && closeDisplay ? openDisplay(nullptr) : nullptr) {
        char keys[32]{};
        queryKeymap(display, keys);
        for(const KeySym shift : {XK_Shift_L, XK_Shift_R}) {
            const KeyCode code = keysymToKeycode(display, shift);
            held |= code != 0 && (keys[code / 8] & (1 << (code % 8)));
        }
        closeDisplay(display);
    }
    dynutils::unload_lib(x11);
    return held;
#else
    return false;
#endif
}

// one message box, with Enter on the first button and Escape on the last: which one was picked (-1 if the box was
// closed), none if no box could be shown
std::optional<int> ask(const std::string &message, std::span<const std::string> buttons) {
    std::vector<SDL_MessageBoxButtonData> data;
    data.reserve(buttons.size());
    for(int i = 0; i < static_cast<int>(buttons.size()); i++) {
        data.push_back({.flags = 0, .buttonID = i, .text = buttons[i].c_str()});
    }
    data.front().flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    data.back().flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;

    const SDL_MessageBoxData box{.flags = SDL_MESSAGEBOX_INFORMATION | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT,
                                 .window = nullptr,
                                 .title = _("Startup settings").c_str(),
                                 .message = message.c_str(),
                                 .numbuttons = static_cast<int>(data.size()),
                                 .buttons = data.data(),
                                 .colorScheme = nullptr};
    int picked = -1;
    if(!SDL_ShowMessageBox(&box, &picked)) {
        debugLog("couldn't show the startup settings: {}", SDL_GetError());
        return std::nullopt;
    }
    return picked;
}

// the startup settings in message boxes, which need nothing to have started: false if the player quit
bool showDialog(std::string_view notice) {
    const std::vector<Choice> renderers = rendererChoices();
    const std::vector<Choice> engines = soundEngineChoices();

    // the settings picked from a list
    struct Pick {
        std::string name;
        ConVar *cvar;
        std::span<const Choice> choices;
        bool switched;
    };
    std::vector<Pick> picks;
    if(!renderers.empty()) picks.push_back({_("Renderer"), &cv::r_renderer, renderers, s_settings.rendererSwitched});
    if(!engines.empty()) picks.push_back({_("Audio Engine"), &cv::snd_engine, engines, s_settings.soundEngineSwitched});

    // picking here only changes what is saved, a switch still wins in this launch
    const auto line = [](std::string_view name, std::string_view value, bool switched) {
        std::string text = fmt::format("\n{}: {}", name, value);
        if(switched) text.append(" ").append(_("(overridden by a launch option)"));
        return text;
    };

    for(;;) {
        std::string message{_("The game starts with these settings:")};
        message.push_back('\n');
        for(const auto &pick : picks) {
            message += line(pick.name, choiceName(pick.choices, pick.cvar->getString()), pick.switched);
        }
        message += line(_("High DPI"), cv::win_highdpi.getBool() ? _("On") : _("Off"),
                        LaunchArgs::has_arg(LaunchArgs::MISC_NO_DPI).has_value());
        message += line(_("Language"), i18n::get_current_language().name, false);
        if(!notice.empty()) message += fmt::format("\n\n{}", notice);

        // Launch, a button for each list, High DPI, Defaults, Quit
        std::vector<std::string> buttons{_("Launch")};
        for(const auto &pick : picks) buttons.push_back(pick.name);
        buttons.emplace_back(_("High DPI"));
        buttons.emplace_back(_("Defaults"));
        buttons.emplace_back(_("Quit"));

        // with no box at all, the game just starts
        const std::optional<int> picked = ask(message, buttons);
        if(!picked || *picked == 0) break;
        if(*picked < 0 || *picked == static_cast<int>(buttons.size()) - 1) return false;

        const auto i = static_cast<size_t>(*picked - 1);
        if(i < picks.size()) {
            const Pick &pick = picks[i];
            std::vector<std::string> choiceButtons;
            for(const auto &choice : pick.choices) choiceButtons.push_back(choice.name);
            choiceButtons.emplace_back(_("Back"));
            const std::optional<int> choice =
                ask(fmt::format("{}: {}", pick.name, choiceName(pick.choices, pick.cvar->getString())), choiceButtons);
            if(choice && *choice >= 0 && *choice < static_cast<int>(pick.choices.size())) {
                pick.cvar->setValue(pick.choices[*choice].value);
            }
        } else if(i == picks.size()) {
            cv::win_highdpi.setValue(!cv::win_highdpi.getBool());
        } else {
            // also the ones that aren't listed here. the language stays: nothing stops in it
            for(auto *var : cvars().getConVarArray()) {
                if(var->isFlagSet(cv::STARTUP) && var != &cv::language) var->clearValue(CvarEditor::CLIENT);
            }
        }
    }

    Console::saveEngineConfig();
    return true;
}
}  // namespace

namespace detail {
bool init() {
    using LaunchArgs::has_arg;
    using enum LaunchArgs::ArgSwitch;

    Console::loadEngineConfig();
    // the language is the one of the system until the player picks one
    cv::language.setDefaultString(Environment::getDefaultLocale());

    // a start that never got through: what it stopped in
    std::string notice;
    s_tracked = !Env::cfg(OS::WASM) && !has_arg(REND_HEADLESS) && !has_arg(MODE_CONSOLE) && !has_arg(MODE_TESTAPP) &&
                !has_arg(MODE_MULTI) && !has_arg(MODE_CRASHREPORT);
    if(File file(progressPath()); s_tracked && file.canRead()) {
        const std::vector<Renderer> built = builtRenderers();
        const auto renderer = [&built](std::string_view value) -> std::optional<Renderer> {
            const auto it = Mc::ranges::find(built, value, progressName);
            return it != built.end() ? std::optional{*it} : std::nullopt;
        };
        const std::string text = file.readToString();
        for(const auto line : SString::split_newlines(text)) {
            const size_t space = line.find(' ');
            const std::string_view what = line.substr(0, space);
            const std::optional<Renderer> r = renderer(space != std::string_view::npos ? line.substr(space + 1) : "");
            if(what == "app") {
                notice = _("The game stopped while loading last time.");
            } else if(r && (what == "renderer" || what == "frame" || what == "stopped")) {
                if(!Mc::ranges::contains(s_stoppedIn, *r)) s_stoppedIn.push_back(*r);
                if(what != "stopped")
                    notice = tformat("The game stopped while starting {} last time.", rendererName(*r));
            }
        }
        debugLog("the last start didn't get through:\n{}", text);
    }

    resolve();
    if(has_arg(REND_HEADLESS)) {
        // nobody would answer it
        if(has_arg(MODE_SETUP)) debugLog("the startup settings dialog isn't shown in headless runs");
    } else if(!Env::cfg(OS::WASM) && (has_arg(MODE_SETUP) || shiftHeld() || !notice.empty())) {
        if(!showDialog(notice)) return false;
        resolve();
    }
    return true;
}
}  // namespace detail

void progress(Progress progress, Renderer renderer) {
    if(!s_tracked) return;

    // what it is doing, then the renderers that stopped a start before
    const std::string_view what = progress == Progress::RENDERER ? "renderer"sv
                                  : progress == Progress::APP    ? "app"sv
                                                                 : "frame"sv;
    std::string text = fmt::format("{} {}\n", what, progressName(renderer));
    for(const auto r : s_stoppedIn) text += fmt::format("stopped {}\n", progressName(r));

    File::createDirectoryRecursive(Mc::Paths::data());
    if(File file(progressPath(), File::MODE::WRITE); !file.canWrite() || !file.writeLine(text, false)) {
        debugLog("WARNING: couldn't write {:s}", progressPath());
    }
}

void finished() {
    if(!s_tracked) return;
    SDL_RemovePath(progressPath().c_str());
    s_tracked = false;
}

const Settings &get() { return s_settings; }

std::string_view rendererValue(Renderer renderer, std::string_view sdlgpuDriver) {
    switch(renderer) {
        case Renderer::GL:
        case Renderer::GLES:
            return "opengl";
        case Renderer::DX11:
            return "dx11";
        case Renderer::SDLGPU:
            return sdlgpuDriver;
        case Renderer::NULLGRAPHICS:
            return "none";
    }
    std::unreachable();
}

std::string rendererName(Renderer renderer) {
    switch(renderer) {
        case Renderer::GL:
            return "OpenGL";
        case Renderer::GLES:
            return "OpenGL ES";
        case Renderer::DX11:
            return "Direct3D 11";
        case Renderer::SDLGPU: {
            std::string names;
            for(const auto &api : sdlgpuAPIs()) names.append(names.empty() ? "" : "/").append(api.name);
            return names;
        }
        case Renderer::NULLGRAPHICS:
            return "none";
    }
    std::unreachable();
}

std::vector<Choice> rendererChoices() {
    std::vector<Choice> choices{{"auto"sv, _("Automatic")}};
    for(const auto renderer : builtRenderers()) {
        if(renderer != Renderer::SDLGPU) {
            choices.push_back({rendererValue(renderer, {}), rendererName(renderer)});
            continue;
        }
        for(const auto &api : sdlgpuAPIs()) choices.push_back({api.driver, std::string{api.name}});
    }
    if(choices.size() < 3) return {};
    return choices;
}

std::vector<Choice> soundEngineChoices() {
    if constexpr(Env::cfg(AUD::SOLOUD) && Env::cfg(AUD::BASS)) {
        return {{"auto"sv, _("Automatic")}, {"soloud"sv, "SoLoud"}, {"bass"sv, "BASS"}};
    } else {
        return {};
    }
}

std::string choiceName(std::span<const Choice> choices, std::string_view value) {
    const auto it = Mc::ranges::find_if(
        choices, [value](const Choice &choice) { return SString::strcase_equal(choice.value, value); });
    return it != choices.end() ? it->name : std::string{value};
}

}  // namespace Mc::Startup
