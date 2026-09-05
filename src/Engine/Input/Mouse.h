// Copyright (c) 2015, PG & 2025, WH, All rights reserved.
#pragma once
#ifndef MOUSE_H
#define MOUSE_H

#include "InputDevice.h"
#include "MouseListener.h"
#include "Rect.h"
#include "Vectors.h"

#include <vector>

// the engine's pointer, in two coordinate spaces: getRealPos() is window pixels; the app renders its own coordinate
// space into a viewport on the window (letterboxing) and getPos() is relative to that viewport (setAppViewport).
// the engine gui is laid out in window pixels, so the engine runs it inside a RealPosScope, where getPos() reports
// window pixels too.
// the os cursor policy is composed here as well (applyCursorPolicy): the app draws its own cursor, so the os cursor
// hides over the viewport, and it can be confined there; an engine gui that needs the os cursor (the console)
// overrides both. the environment turns the result into the sdl state (Environment::applyCursorState).
class Mouse final : public InputDevice {
    NOCOPY_NOMOVE(Mouse)

   public:
    Mouse();
    ~Mouse() override = default;

    void reset() override;
    void draw() override;
    void update() override;

    void drawDebug();

    // event handling
    void addListener(MouseListener *mouseListener, bool insertOnTop = false);
    void removeListener(MouseListener *mouseListener);

    // input handling
    void onPosChange(dvec2 pos);  // window pixels
    void onWheelVertical(int delta);
    void onWheelHorizontal(int delta);
    void onButtonChange(ButtonEvent ev);

    // position/coordinate handling
    void setPos(vec2 pos);                        // app space; moves the virtual cursor, not the os one
    void setAppViewport(const McRect &viewport);  // window pixels; empty = the whole window
    [[nodiscard]] McRect getAppViewport() const;

    class RealPosScope {
        NOCOPY_NOMOVE(RealPosScope)
       public:
        RealPosScope() = delete;
        RealPosScope(Mouse *m);
        ~RealPosScope();

       private:
        Mouse *m;
        bool bPrevious;
    };

    // os cursor policy
    void setAppCursorHidden(bool hidden);      // the app draws its own cursor: the os cursor hides over the viewport
    void setAppCursorConfined(bool confined);  // the os cursor is confined to the viewport
    void setOSCursorRequired(bool required);   // an engine gui needs the os cursor: visible, unconfined, absolute

    // raw (relative) input runs while the os cursor is hidden if the user wants it (mouse_raw_input) or an app
    // feature needs raw deltas regardless of the setting (fposu)
    void setRawInputOverride(bool forced);

    // state getters
    [[nodiscard]] constexpr forceinline vec2 getPos() const {
        return this->bRealPos ? vec2{this->vPosWithoutOffsets} : this->vPos;
    }
    [[nodiscard]] constexpr forceinline vec2 getRealPos() const { return this->vPosWithoutOffsets; }
    [[nodiscard]] constexpr forceinline vec2 getDelta() const { return this->vDelta; }
    [[nodiscard]] constexpr forceinline vec2 getRawDelta() const { return this->vRawDelta; }

    [[nodiscard]] constexpr forceinline float getSensitivity() const { return this->fSensitivity; }

    // button state accessors
    [[nodiscard]] constexpr forceinline bool isLeftDown() const {
        return flags::has<MouseButtonFlags::MF_LEFT>(this->buttonsHeldMask);
    }
    [[nodiscard]] constexpr forceinline bool isMiddleDown() const {
        return flags::has<MouseButtonFlags::MF_MIDDLE>(this->buttonsHeldMask);
    }
    [[nodiscard]] constexpr forceinline bool isRightDown() const {
        return flags::has<MouseButtonFlags::MF_RIGHT>(this->buttonsHeldMask);
    }
    [[nodiscard]] constexpr forceinline bool isButton4Down() const {
        return flags::has<MouseButtonFlags::MF_X1>(this->buttonsHeldMask);
    }
    [[nodiscard]] constexpr forceinline bool isButton5Down() const {
        return flags::has<MouseButtonFlags::MF_X2>(this->buttonsHeldMask);
    }
    [[nodiscard]] constexpr forceinline MouseButtonFlags getHeldButtons() const { return this->buttonsHeldMask; }

    // buttons that went down during this frame's update (edge, not level; cleared every Mouse::update)
    [[nodiscard]] constexpr forceinline bool isLeftPressed() const {
        return flags::has<MouseButtonFlags::MF_LEFT>(this->buttonsPressedMask);
    }
    [[nodiscard]] constexpr forceinline bool isRightPressed() const {
        return flags::has<MouseButtonFlags::MF_RIGHT>(this->buttonsPressedMask);
    }

    [[nodiscard]] constexpr forceinline int getWheelDeltaVertical() const { return this->iWheelDeltaVertical; }
    [[nodiscard]] constexpr forceinline int getWheelDeltaHorizontal() const { return this->iWheelDeltaHorizontal; }

    void resetWheelDelta();

    [[nodiscard]] constexpr forceinline bool isRawInputWanted() const {
        return this->bIsRawInputDesired;
    }  // the user's setting, NOT the actual OS raw input state (see Environment::isOSMouseInputRaw)!

   private:
    // same as keyboard input,
    // mouse input events are only queued on onWheel/onButton, then dispatched during Engine::onUpdate
    enum class Type : uint8_t { BUTTON, WHEELV, WHEELH };

    struct FullEvent {
        ButtonEvent orig;
        int wheelVDelta;
        int wheelHDelta;
        Type type;
    };

    std::vector<FullEvent> eventQueue;

    void onWheelVertical_internal(int delta);
    void onWheelHorizontal_internal(int delta);
    void onButtonChange_internal(ButtonEvent &ev);

    // callbacks
    void onSensitivityChanged(float newSens);
    void onRawInputChanged(float newVal);

    // hands the os cursor state the current inputs call for to the environment
    void applyCursorPolicy();

    // position state
    vec2 vPos{0.f};                 // app space (window pixels relative to the viewport origin)
    dvec2 vPosWithoutOffsets{0.f};  // window pixels
    vec2 vDelta{0.f};               // movement delta in the current frame
    vec2 vRawDelta{0.f};  // movement delta in the current frame, without consideration for clipping or sensitivity

    McRect appViewport{};  // window pixels; empty = the whole window

    // mode tracking
    bool bIsRawInputDesired{false};  // whether the user wants raw (relative) input
    bool bRawInputOverride{false};   // an app feature needs raw deltas (setRawInputOverride)
    bool bRealPos{false};            // getPos() bypasses the viewport mapping (RealPosScope)
    float fSensitivity{1.0f};

    // os cursor policy inputs
    bool bAppCursorHidden{false};
    bool bAppCursorConfined{false};
    bool bOSCursorRequired{false};

    // button state (using our internal button index)
    MouseButtonFlags buttonsHeldMask{0};
    MouseButtonFlags buttonsPressedMask{0};

    // wheel state
    int iWheelDeltaVertical{0};
    int iWheelDeltaHorizontal{0};
    int iWheelDeltaVerticalActual{0};
    int iWheelDeltaHorizontalActual{0};

    // listeners
    std::vector<MouseListener *> listeners;
};

#endif
