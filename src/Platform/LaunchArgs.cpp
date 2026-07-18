// Copyright (c) 2026, WH, All rights reserved.
#include "LaunchArgs.h"
#include "SString.h"

#include <initializer_list>
#include <vector>

namespace Mc::LaunchArgs {

namespace {
ArgMap s_map;
std::vector<std::string> s_array;
const char *const *s_original_argv{nullptr};

// returns the value of the first alias that was passed with one, an empty string if any
// alias is present without a value, nullopt if none are present
std::optional<std::string> find_switch(std::initializer_list<std::string_view> aliases) noexcept {
    std::optional<std::string> found;
    for(const auto alias : aliases) {
        const auto it = s_map.find(std::string{alias});
        if(it == s_map.end()) continue;
        if(it->second.has_value()) return it->second;
        if(!found.has_value()) found.emplace();
    }
    return found;
}

// like find_switch, but the switch only counts if its value (case-insensitively) contains one of the matchers
std::optional<std::string> match_value(std::initializer_list<std::string_view> aliases,
                                       std::initializer_list<std::string_view> matchers) noexcept {
    auto val = find_switch(aliases);
    if(!val.has_value() || val->empty()) return std::nullopt;
    for(const auto matcher : matchers) {
        if(SString::contains_ncase(*val, matcher)) return val;
    }
    return std::nullopt;
}
}  // namespace

namespace detail {
void init(int argc, char *argv[]) noexcept {
    s_original_argv = argv;
    s_array = std::vector<std::string>(argv, argv + argc);

    for(int i = 1; i < argc; ++i) {
        std::string arg{argv[i]};
        if(arg.empty()) continue;

        if(arg.starts_with('-')) {
            // lowercase switches so they're case-insensitive, but keep values untouched
            // (they may be case-sensitive names or file paths)
            SString::lower_inplace(arg);
            if(i + 1 < argc && !(argv[i + 1][0] == '-')) {
                s_map[arg] = argv[i + 1];
                ++i;
            } else {
                s_map[arg] = std::nullopt;
            }
        } else {
            s_map[arg] = std::nullopt;
        }
    }
}
}  // namespace detail

const ArgMap &get_map() noexcept { return s_map; }

std::span<const std::string> get_array() noexcept { return {s_array.data(), s_array.size()}; }

CArgs get_c() noexcept {
    CArgs ret{};
    ret.argc = static_cast<int>(s_array.size());
    ret.argv = s_original_argv;
    return ret;
}

std::optional<std::string> has_arg(ArgSwitch arg_switch) noexcept {
    switch(arg_switch) {
        case REND_HEADLESS:
            return find_switch({"-headless"});
        case REND_GL:
            return find_switch({"-gl", "-opengl"});
        case REND_DX11:
            return find_switch({"-dx11", "-directx"});
        case REND_SDLGPU:
            return find_switch({"-sdlgpu", "-gpu"});
        case REND_SDLGPU_D3D12:
            return match_value({"-sdlgpu", "-gpu"}, {"d3d", "dx"});
        case REND_SDLGPU_VK:
            return match_value({"-sdlgpu", "-gpu"}, {"vk", "vulkan"});
        case REND_SDLGPU_MTL:
            return match_value({"-sdlgpu", "-gpu"}, {"mtl", "metal"});
        case SND_BASS:
            return match_value({"-sound"}, {"bass"});
        case SND_SOLOUD:
            return match_value({"-sound"}, {"soloud"});
        case MODE_CONSOLE:
            return find_switch({"-console"});
        case MODE_DIFFCALC:
            return find_switch({"-diffcalc"});
        case MODE_TESTAPP:
            return find_switch({"-testapp"});
        case MODE_MULTI:
            return find_switch({"-multi"});
        case MISC_GL_VERBOSE:
            return find_switch({"-info", "-print", "-printinfo"});
        case MISC_GL_DEBUG:
            return find_switch({"-debugctx"});
        case MISC_GL_AA:
            return find_switch({"-aa"});
        case MISC_DX11_EXCLUSIVE:
            return find_switch({"-exclusive"});
        case MISC_ENABLE_IME:
            return find_switch({"-ime"});
        case MISC_NO_DPI:
            return find_switch({"-nodpi"});
        case MISC_NO_FPU:
            return find_switch({"-nofpu"});
    }
    return std::nullopt;
}

}  // namespace Mc::LaunchArgs
