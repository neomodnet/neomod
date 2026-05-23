#pragma once
// Copyright (c) 2026, kiwec, All rights reserved.
#include <fmt/format.h>
#include <span>
#include <string_view>

#include "translations.h"

namespace i18n {

// Gets a compile time index for any string to translate
// (so we can access translations in O(1), instead of using a hashmap)
consteval int string_index(std::string_view s) {
    for(int i = 0; i < TRANSLATABLE_STRINGS.size(); i++) {
        if(TRANSLATABLE_STRINGS[i] == s) return i;
    }

    // You could imagine an implementation where we throw a compiler error here,
    // but I prefer a soft fallback to the original string.
    //
    // This allows the build system to be more flexible instead of HAVING
    // to regenerate strings every time a source file is changed.
    return -1;
}

void load(std::string_view locale);
const char* translate(int index, std::string_view original);
const char* translate_plural(int index, std::string_view singular, std::string_view plural, int n);

struct Language {
    std::string_view code;
    std::string_view name;
};
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
