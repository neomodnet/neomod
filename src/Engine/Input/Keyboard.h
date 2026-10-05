#pragma once
// Copyright (c) 2015, PG, 2026, WH, All rights reserved.
#include "types.h"

#include "InputDevice.h"
#include "noinclude.h"
#include "KeyBindings.h"  // IWYU pragma: keep
#include "StaticPImpl.h"

#include <span>

typedef struct SDL_KeyboardEvent SDL_KeyboardEvent;
typedef struct SDL_TextInputEvent SDL_TextInputEvent;

using SCANCODE = uint16_t;
using KEYCODE = uint32_t;
using KEYMOD = uint16_t;

class KeyboardListener;
class SDLMain;

class Keyboard final : public InputDevice {
    NOCOPY_NOMOVE(Keyboard)

   public:
    Keyboard();
    ~Keyboard() override;

    void reset() override;
    void draw() override;
    void update() override;

    MC_UNREVOCABLE void addListener(KeyboardListener *keyboardListener, bool insertOnTop = false);
    void removeListener(KeyboardListener *keyboardListener);

    [[nodiscard]] bool isControlDown() const;
    [[nodiscard]] bool isAltDown() const;
    [[nodiscard]] bool isShiftDown() const;
    [[nodiscard]] bool isSuperDown() const;

    [[nodiscard]] bool areModsHeld(KEYMOD keymodMask) const;

    // don't use KEY_ prefixed keys here, use KEYMOD_ ones!
    [[nodiscard]] bool areModsHeld(KEYCODE keymodMask) const = delete;

   private:
    // events received in main loop
    friend SDLMain;

    void onKeyEvent(u64 timestamp, u32 keyboardID, KEYCODE layoutDependentKeycode, KEYMOD heldModifiersAsOfEvent,
                    SCANCODE layoutIndependentScancode, bool isKeyDown, bool isRepeatEvent);
    void onCharEvent(u64 timestamp, const char *text);

    struct KeyboardImpl;
    StaticPImpl<KeyboardImpl, sizeof(void *) == 8 ? 64 : 32> m_impl;
};
