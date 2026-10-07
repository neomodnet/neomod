#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "KeyBindings.h"
#include "KeyboardEvent.h"

#include <initializer_list>
#include <string>

// a key pressed with exactly these modifiers held, e.g. {KEY_S, KEYMOD_CONTROL} for Ctrl+S. either side of a modifier
// counts as that modifier, lock keys don't count
struct Shortcut {
    SCANCODE key{};
    KEYMOD modifiers{KEYMOD_NONE};

    [[nodiscard]] constexpr bool matches(const KeyboardEvent &e) const {
        return e.getScanCode() == this->key && held(e.getModifiers()) == held(this->modifiers);
    }
    // as a menu shows it, e.g. "Ctrl+Shift+S"
    [[nodiscard]] std::string text() const;

   private:
    // which of ctrl, shift, alt and super a mask has, on either side
    [[nodiscard]] static constexpr KEYMOD held(KEYMOD mask) {
        KEYMOD out{KEYMOD_NONE};
        for(const KEYMOD modifier : {KEYMOD_CONTROL, KEYMOD_SHIFT, KEYMOD_ALT, KEYMOD_SUPER}) {
            if(mask & modifier) out |= modifier;
        }
        return out;
    }
};
