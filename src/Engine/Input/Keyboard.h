#pragma once
// Copyright (c) 2015, PG, 2026, WH, All rights reserved.
#include "types.h"

#include "InputDevice.h"
#include "noinclude.h"
#include "Registration.h"
#include "KeyBindings.h"  // IWYU pragma: keep
#include "StaticPImpl.h"

using SCANCODE = uint16_t;
using KEYCODE = uint32_t;
using KEYMOD = uint16_t;

class KeyboardListener;
class SDLMain;
class TextInputClient;
struct TextComposition;
struct TextInputArea;

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

    // typed text and IME compositions go to the text target, and the os text input (IME, on-screen keyboard) is on
    // while there is one and the window has the focus. the ui offers one every frame, with where its text is (window
    // pixels); applyTextTarget() makes the frame's first offer the target, or ends it without one. ambient: typed into
    // without a focus (a type-to-search), which brings up no on-screen keyboard
    void offerTextTarget(TextInputClient *target, const TextInputArea &area, bool ambient);
    void applyTextTarget();
    [[nodiscard]] TextInputClient *getTextTarget() const;
    // what the IME is composing for client (null: nothing). key events wait until it's done
    [[nodiscard]] const TextComposition *getComposition(const TextInputClient *client) const;
    // makes what's being composed for client typed text and lets the IME start over, for a text target about to lose
    // the focus or to have its caret moved under the composition
    void commitComposition(const TextInputClient *client);
    // a dying client stops being the target (or the one offered)
    void forgetTextClient(const TextInputClient *client);

   private:
    // events received in main loop
    friend SDLMain;

    void onKeyEvent(u64 timestamp, u32 keyboardID, KEYCODE layoutDependentKeycode, KEYMOD heldModifiersAsOfEvent,
                    SCANCODE layoutIndependentScancode, bool isKeyDown, bool isRepeatEvent);
    void onTextEvent(u64 timestamp, const char *text);
    // start and length in codepoints, as SDL gives them (-1: not given)
    void onTextEditingEvent(u64 timestamp, const char *text, i32 start, i32 length);

    struct KeyboardImpl;
    StaticPImpl<KeyboardImpl, sizeof(void *) == 8 ? 256 : 160> m_impl;
};
