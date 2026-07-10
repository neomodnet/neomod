#pragma once
// Copyright (c) 2026, WH, All rights reserved.
// enumeration of valid startup arguments that change persistent program behavior

#include <span>
#include <unordered_map>
#include <string>
#include <optional>

namespace Mc::LaunchArgs {

// intended to be called once inside of main()
namespace detail {
void init(int argc, char *argv[]) noexcept;
}

// get the arguments pre-parsed into a map, so it can be queried like e.g.:
// args.contains("-file")
// auto filename = args["-file"].value_or("default.txt");
// if (args["-output"].has_value())
// 	auto outfile = args["-output"].value();
// NOTE: switches (keys) are lowercased before being put into this, values are stored exactly as passed
// (they may be case-sensitive names or file paths)
using ArgMap = std::unordered_map<std::string, std::optional<std::string>>;
[[nodiscard]] const ArgMap &get_map() noexcept;

// simple string array of arguments without any further parsing
// NOTE: the program name is stored as the first element, but it may no longer point to the executable
// if it was launched from a different directory initially (since we change the working directory to the exe root on startup)
[[nodiscard]] std::span<const std::string> get_array() noexcept;

// get the arguments exactly as they were passed into the program
struct CArgs {
    int argc;
    const char *const *argv;
};
[[nodiscard]] CArgs get_c() noexcept;

// currently valid/parsed arguments, so that they aren't spread everywhere around the codebase
enum ArgSwitch : unsigned char {
    // renderer selection
    REND_HEADLESS,      // -headless (no visible window, offscreen/dummy video driver, stdin command processing)
    REND_GL,            // -gl, -opengl
    REND_DX11,          // -dx11, -directx
    REND_SDLGPU,        // -sdlgpu, -gpu
    REND_SDLGPU_D3D12,  // -sdlgpu/-gpu with a value matching d3d/dx
    REND_SDLGPU_VK,     // -sdlgpu/-gpu with a value matching vk/vulkan
    REND_SDLGPU_MTL,    // -sdlgpu/-gpu with a value matching mtl/metal
    // audio backend selection (values of -sound)
    SND_BASS,
    SND_SOLOUD,
    SND_SOLOUD_THREADED,
    // startup modes
    MODE_CONSOLE,   // -console (stdin command processing with a visible window)
    MODE_DIFFCALC,  // -diffcalc (run the standalone difficulty calculator tool and exit)
    MODE_TESTAPP,   // -testapp <name> (launch the given test app instead of the main game)
    MODE_MULTI,     // -multi (allow running alongside an already-running instance)
    // misc
    MISC_GL_VERBOSE,      // -info, -print, -printinfo (print verbose startup info for gl context)
    MISC_GL_DEBUG,        // -debugctx (immediately initialize opengl with debugging)
    MISC_GL_AA,           // -aa <samples> (full-window antialiasing setting)
    MISC_DX11_EXCLUSIVE,  // -exclusive (exclusive fullscreen instead of flip presentation)
    MISC_ENABLE_IME,      // -ime (enable unfinished IME (on-screen-keyboard) support)
    MISC_NO_DPI,          // -nodpi (ignore display DPI scaling)
    MISC_NO_FPU,          // -nofpu (don't change floating point behavior on thread init)
};

// use this instead of manually querying the arg map (and add new arguments to the enum)
// returns nullopt if the switch isn't in effect, otherwise an engaged optional holding the switch's
// value ("-aa 4" -> "4"), or an empty string if it was passed without a value
// switches with multiple aliases return the value of the first alias that was passed with one,
// and value-matching switches (e.g. SND_BASS, REND_SDLGPU_VK) are only in effect if the value matches
[[nodiscard]] std::optional<std::string> has_arg(ArgSwitch arg_switch) noexcept;

}  // namespace Mc::LaunchArgs
