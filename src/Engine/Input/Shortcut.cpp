// Copyright (c) 2026, WH, All rights reserved.
#include "Shortcut.h"

#include "Environment.h"

#include <array>
#include <string_view>
#include <utility>

std::string Shortcut::text() const {
    static constexpr std::array<std::pair<KEYMOD, std::string_view>, 4> MODIFIERS{
        {{KEYMOD_CONTROL, "Ctrl+"}, {KEYMOD_SHIFT, "Shift+"}, {KEYMOD_ALT, "Alt+"}, {KEYMOD_SUPER, "Super+"}}};
    std::string out;
    for(const auto &[modifier, name] : MODIFIERS) {
        if(this->modifiers & modifier) out.append(name);
    }
    out.append(env->scanCodeToString(this->key));
    return out;
}
