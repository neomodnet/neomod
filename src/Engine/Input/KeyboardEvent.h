// Copyright (c) 2015, PG, All rights reserved.
#ifndef KEYBOARDEVENT_H
#define KEYBOARDEVENT_H

#include "noinclude.h"

#include <cstdint>

using SCANCODE = uint16_t;
using KEYMOD = uint16_t;
using KEYCODE = uint32_t;
using KEYCODES = KEYCODE;

class KeyboardEvent {
   public:
    KeyboardEvent(SCANCODE scanCode, KEYCODE keyCode, uint64_t timestamp, KEYMOD modifiers, bool repeat = false)
        : timestamp(timestamp), keyCode(keyCode), scanCode(scanCode), modifiers(modifiers), bRepeat(repeat) {}

    constexpr forceinline void consume() { this->bConsumed = true; }

    [[nodiscard]] constexpr forceinline bool isConsumed() const { return this->bConsumed; }
    [[nodiscard]] constexpr forceinline bool isRepeat() const { return this->bRepeat; }
    [[nodiscard]] constexpr forceinline KEYCODE getKeyCode() const { return this->keyCode; }
    [[nodiscard]] constexpr forceinline SCANCODE getScanCode() const { return this->scanCode; }
    [[nodiscard]] constexpr forceinline uint64_t getTimestamp() const { return this->timestamp; }
    // the KEYMOD_* mask held when the event happened
    [[nodiscard]] constexpr forceinline KEYMOD getModifiers() const { return this->modifiers; }

    inline bool operator==(SCANCODE rhs) const { return this->scanCode == rhs; }
    inline bool operator!=(SCANCODE rhs) const { return this->scanCode != rhs; }

    explicit operator SCANCODE() const { return this->scanCode; }

   private:
    uint64_t timestamp;
    KEYCODE keyCode;
    SCANCODE scanCode;
    KEYMOD modifiers;
    bool bRepeat;
    bool bConsumed{false};
};

#endif
