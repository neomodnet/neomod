#pragma once
// Copyright (c) 2026, kiwec, All rights reserved.
#include "config.h"

#include <span>
#include <string_view>

// TODO: add CMake support for translation generation
#if defined(MCENGINE_FEATURE_I18N)

#include "fmt/format.h"

#include "translations.h"

namespace i18n {

// Gets a compile-time index for any string to translate via O(log N) binary search
// (so we can access translations in O(1) at runtime, instead of using a hashmap).
consteval int string_index(std::string_view s) {
    size_t lo = 0, hi = std::size(TRANSLATABLE_STRINGS);
    while(lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if(TRANSLATABLE_STRINGS[mid] < s)
            lo = mid + 1;
        else
            hi = mid;
    }
    if(lo == std::size(TRANSLATABLE_STRINGS) || TRANSLATABLE_STRINGS[lo] != s) {
        // You could imagine an implementation where we throw a compiler error here,
        // but I prefer a soft fallback to the original string.
        //
        // This allows the build system to be more flexible instead of HAVING
        // to regenerate strings every time a source file is changed.
        return -1;
    }
    return static_cast<int>(lo);
}

void load(std::string_view locale);
const char* translate(int index, std::string_view original);
const char* translate_plural(int index, std::string_view singular, std::string_view plural, int n);

std::span<const Language> get_available_languages();

}  // namespace i18n

// Macro to mark string for translation (and get translated version)
#define _(String) i18n::translate(i18n::string_index(String), String)

// Format a string with translation (compile-time checked arguments)
template <typename... Args>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
std::string tformat_impl(fmt::format_string<Args...> /*original*/, const char* translated, Args&&... args) {
    return fmt::vformat(translated, fmt::make_format_args(args...));
}
#define tformat(String, ...) tformat_impl(String, _(String) __VA_OPT__(, ) __VA_ARGS__)

#else  // !(MCENGINE_FEATURE_I18N)

// BUILD_TOOLS_ONLY explicitly avoids dragging in fmt
#if defined(BUILD_TOOLS_ONLY)
#include <format>
#else
#include "fmt/format.h"
#endif

namespace i18n {

inline void load(std::string_view /*locale*/) {}

struct Language {
    std::string_view code;
    std::string_view name;
};

inline std::span<const Language> get_available_languages() {
    using namespace std::string_view_literals;
    static constexpr Language AVAILABLE_LANGUAGES[]{
        {.code = "en"sv, .name = "English"sv},
    };
    return {static_cast<const Language*>(AVAILABLE_LANGUAGES), 1};
}

}  // namespace i18n

#define _(String) (String)

#if defined(BUILD_TOOLS_ONLY)
#define tformat(String, ...) std::format(String __VA_OPT__(, ) __VA_ARGS__)
#else
#define tformat(String, ...) fmt::format(String __VA_OPT__(, ) __VA_ARGS__)
#endif

#endif  // MCENGINE_FEATURE_I18N
