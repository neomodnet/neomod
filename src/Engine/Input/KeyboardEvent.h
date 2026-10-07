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
    KeyboardEvent(SCANCODE scanCode, KEYCODE keyCode, char32_t charCode, uint64_t timestamp, KEYMOD modifiers,
                  bool repeat = false)
        : timestamp(timestamp),
          keyCode(keyCode),
          charCode(charCode),
          scanCode(scanCode),
          modifiers(modifiers),
          bRepeat(repeat) {}

    constexpr forceinline void consume() { this->bConsumed = true; }

    [[nodiscard]] constexpr forceinline bool isConsumed() const { return this->bConsumed; }
    [[nodiscard]] constexpr forceinline bool isRepeat() const { return this->bRepeat; }
    [[nodiscard]] constexpr forceinline KEYCODE getKeyCode() const { return this->keyCode; }
    [[nodiscard]] constexpr forceinline SCANCODE getScanCode() const { return this->scanCode; }
    [[nodiscard]] constexpr forceinline char32_t getCharCode() const { return this->charCode; }
    [[nodiscard]] constexpr forceinline uint64_t getTimestamp() const { return this->timestamp; }
    // the KEYMOD_* mask held when the event happened (for a char, as of the last key event)
    [[nodiscard]] constexpr forceinline KEYMOD getModifiers() const { return this->modifiers; }

    inline bool operator==(SCANCODE rhs) const { return this->scanCode == rhs; }
    inline bool operator!=(SCANCODE rhs) const { return this->scanCode != rhs; }

    explicit operator SCANCODE() const { return this->scanCode; }

   private:
    uint64_t timestamp;
    KEYCODE keyCode;
    char32_t charCode;
    SCANCODE scanCode;
    KEYMOD modifiers;
    bool bRepeat;
    bool bConsumed{false};
};

#endif
