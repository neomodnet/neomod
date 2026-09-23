// Copyright (c) 2026, WH, All rights reserved.
#include "LaunchArgs.h"
#include "SString.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <initializer_list>
#include <string_view>
#include <vector>

#ifdef MCENGINE_PLATFORM_WINDOWS
#include "UniString.h"

#include "WinDebloatDefs.h"
#include <windows.h>
#include <shellapi.h>  // CommandLineToArgvW
#endif

namespace Mc::LaunchArgs {

namespace {
ArgMap s_map;
std::vector<std::string> s_array;
std::vector<std::string> s_switches;
std::vector<size_t> s_switch_indices;  // where s_switches are in s_array
const char *const *s_original_argv{nullptr};

// what a switch takes as its value from the argument after it
enum class Takes : unsigned char {
    NOTHING,
    WORD,      // a plain word like "vk" or "4"
    ANYTHING,  // anything that doesn't start with '-'
};

struct KnownSwitch {
    std::string_view name;
    Takes takes;
};

// every switch has_arg() looks for
constexpr auto KNOWN_SWITCHES = std::to_array<KnownSwitch>({
    {"-headless", Takes::NOTHING},   {"-headless-audio", Takes::NOTHING},
    {"-gl", Takes::NOTHING},         {"-opengl", Takes::NOTHING},
    {"-dx11", Takes::NOTHING},       {"-directx", Takes::NOTHING},
    {"-sdlgpu", Takes::WORD},        {"-gpu", Takes::WORD},
    {"-sound", Takes::WORD},         {"-console", Takes::NOTHING},
    {"-diffcalc", Takes::NOTHING},   {"-testapp", Takes::WORD},
    {"-multi", Takes::NOTHING},      {"-info", Takes::NOTHING},
    {"-print", Takes::NOTHING},      {"-printinfo", Takes::NOTHING},
    {"-debugctx", Takes::NOTHING},   {"-aa", Takes::WORD},
    {"-exclusive", Takes::NOTHING},  {"-ime", Takes::NOTHING},
    {"-nodpi", Takes::NOTHING},      {"-nofpu", Takes::NOTHING},
    {"-async_threads", Takes::WORD}, {"-datadir", Takes::ANYTHING},
});

const KnownSwitch *find_known(std::string_view name) {
    const auto it = std::ranges::find(KNOWN_SWITCHES, name, &KnownSwitch::name);
    return it != KNOWN_SWITCHES.end() ? &*it : nullptr;
}

bool can_be_value(Takes takes, std::string_view next) {
    if(next.starts_with('-')) return false;
    switch(takes) {
        case Takes::NOTHING:
            return false;
        case Takes::WORD:
            return !next.empty() &&
                   std::ranges::all_of(next, [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
        case Takes::ANYTHING:
            return true;
    }
    return false;
}

// returns the value of the first alias that was passed with one, an empty string if any
// alias is present without a value, nullopt if none are present
std::optional<std::string> find_switch(std::initializer_list<std::string_view> aliases) noexcept {
    std::optional<std::string> found;
    for(const auto alias : aliases) {
        assert(find_known(alias) && "LaunchArgs::find_switch: switch missing from KNOWN_SWITCHES");
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
            const KnownSwitch *known = find_known(arg);
            const bool has_value = i + 1 < argc && can_be_value(known ? known->takes : Takes::ANYTHING, argv[i + 1]);
            if(known) {
                s_switch_indices.push_back(i);
                if(has_value) s_switch_indices.push_back(i + 1);
            }
            if(has_value) {
                s_map[arg] = argv[i + 1];
                ++i;
            } else {
                s_map[arg] = std::nullopt;
            }
        } else {
            s_map[arg] = std::nullopt;
        }
    }

    for(const size_t i : s_switch_indices) s_switches.push_back(s_array[i]);
}
}  // namespace detail

const ArgMap &get_map() noexcept { return s_map; }

std::span<const std::string> get_array() noexcept { return {s_array.data(), s_array.size()}; }

std::span<const std::string> get_switches() noexcept { return s_switches; }

#ifdef MCENGINE_PLATFORM_WINDOWS
std::optional<std::string> get_switches_cmdline() noexcept {
    const std::wstring_view cmdline{GetCommandLineW()};

    const auto parse = [](std::wstring_view str) -> std::vector<std::wstring> {
        int argc = 0;
        LPWSTR *argv = CommandLineToArgvW(std::wstring{str}.c_str(), &argc);
        if(!argv) return {};
        std::vector<std::wstring> ret(argv, argv + argc);
        LocalFree(static_cast<HLOCAL>(argv));
        return ret;
    };

    const std::vector<std::wstring> args = parse(cmdline);
    if(args.size() != s_array.size()) return std::nullopt;

    // an argument ends at a space or tab outside of quotes, i.e. where cutting the command line leaves a part that
    // parses into whole arguments. letting the parser say where that is avoids a second copy of its quoting rules
    std::vector<size_t> ends;
    for(size_t cut = 1; cut <= cmdline.size() && ends.size() < args.size(); cut++) {
        if(cut < cmdline.size() && cmdline[cut] != L' ' && cmdline[cut] != L'\t') continue;
        const auto head = parse(cmdline.substr(0, cut));
        if(head.size() == ends.size() + 1 && std::ranges::equal(head, std::span{args}.first(head.size()))) {
            ends.push_back(cut);
        }
    }
    if(ends.size() != args.size()) return std::nullopt;

    std::wstring out;
    std::vector<std::wstring> expected{L"x"};
    for(const size_t i : s_switch_indices) {
        const size_t begin = cmdline.find_first_not_of(L" \t", ends[i - 1]);
        if(!out.empty()) out += L' ';
        out += cmdline.substr(begin, ends[i] - begin);
        expected.push_back(args[i]);
    }

    // the pieces have to read back as exactly the switches, and end outside of quotes so that an argument after them
    // stays separate
    expected.emplace_back(L"x");
    if(parse(L"x " + out + L" x") != expected) return std::nullopt;
    return UniString::to_utf8(out);
}
#endif

CArgs get_c() noexcept {
    CArgs ret{};
    ret.argc = static_cast<int>(s_array.size());
    ret.argv = s_original_argv;
    return ret;
}

std::optional<std::string> has_arg(ArgSwitch arg_switch) noexcept {
    switch(arg_switch) {
        case REND_HEADLESS:
            return find_switch({"-headless", "-headless-audio"});
        case REND_HEADLESS_AUDIO:
            return find_switch({"-headless-audio"});
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
        case MISC_ASYNC_THREADS:
            return find_switch({"-async_threads"});
        case MISC_DATA_DIR:
            return find_switch({"-datadir"});
    }
    return std::nullopt;
}

}  // namespace Mc::LaunchArgs
