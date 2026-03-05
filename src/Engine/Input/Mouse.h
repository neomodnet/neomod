// Copyright (c) 2015, PG & 2025, WH, All rights reserved.
#pragma once
#ifndef MOUSE_H
#define MOUSE_H

#include "InputDevice.h"
#include "MouseListener.h"
#include "Vectors.h"

#include <vector>

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
    void onPosChange(dvec2 pos);
    void onWheelVertical(int delta);
    void onWheelHorizontal(int delta);
    void onButtonChange(ButtonEvent ev);

    // position/coordinate handling
    void setPos(vec2 pos);  // NOT OS mouse pos, virtual mouse pos
    void setOffset(vec2 offset);
    inline void setScale(vec2 scale) { this->vScale = scale; }

    // state getters
    [[nodiscard]] constexpr forceinline vec2 getPos() const { return this->vPos; }
    [[nodiscard]] constexpr forceinline vec2 getRealPos() const { return this->vPosWithoutOffsets; }
    [[nodiscard]] constexpr forceinline vec2 getDelta() const { return this->vDelta; }
    [[nodiscard]] constexpr forceinline vec2 getRawDelta() const { return this->vRawDelta; }

    [[nodiscard]] constexpr forceinline vec2 getOffset() const { return this->vOffset; }
    [[nodiscard]] constexpr forceinline vec2 getScale() const { return this->vScale; }
    [[nodiscard]] constexpr forceinline float getSensitivity() const { return this->fSensitivity; }

    // TODO: this interface/design causes jank at low FPS:
    //   it's not unlikely that a click and release occurs inside the same frame,
    //   and UI elements explicitly check for isLeftDown->!isLeftDown across a minimum of 2 frames
    //   to detect if a click occurred

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

    [[nodiscard]] constexpr forceinline int getWheelDeltaVertical() const { return this->iWheelDeltaVertical; }
    [[nodiscard]] constexpr forceinline int getWheelDeltaHorizontal() const { return this->iWheelDeltaHorizontal; }

    void resetWheelDelta();

    [[nodiscard]] constexpr forceinline bool isRawInputWanted() const {
        return this->bIsRawInputDesired;
    }  // "desired" rawinput state, NOT actual OS raw input state!

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

    // position state
    vec2 vPos{0.f};                // position with offset applied
    dvec2 vPosWithoutOffsets{0.f};  // position without offset
    vec2 vDelta{0.f};  // movement delta in the current frame
    vec2 vRawDelta{0.f};  // movement delta in the current frame, without consideration for clipping or sensitivity

    // mode tracking
    bool bIsRawInputDesired{false};  // whether the user wants raw (relative) input
    float fSensitivity{1.0f};

    // button state (using our internal button index)
    MouseButtonFlags buttonsHeldMask{0};

    // wheel state
    int iWheelDeltaVertical{0};
    int iWheelDeltaHorizontal{0};
    int iWheelDeltaVerticalActual{0};
    int iWheelDeltaHorizontalActual{0};

    // listeners
    std::vector<MouseListener *> listeners;

    // transform parameters
    vec2 vOffset{0, 0};  // offset applied to coordinates
    vec2 vScale{1, 1};   // scale applied to coordinates
};

#endif
