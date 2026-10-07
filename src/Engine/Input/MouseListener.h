// Copyright (c) 2015, PG, All rights reserved.
#ifndef MOUSELISTENER_H
#define MOUSELISTENER_H

#include "noinclude.h"

#include <cstdint>
// clang-format off
enum class MouseButtonFlags : uint8_t {
    MF_NONE =   0u,
    MF_LEFT =   1u << 0,
    MF_MIDDLE = 1u << 1,
    MF_RIGHT =  1u << 2,
    MF_X1 =     1u << 3,
    MF_X2 =     1u << 4,
    MF_COUNT =  1u << 5
};
// clang-format on

MAKE_FLAG_ENUM(MouseButtonFlags)

// a wheel's movement on one axis: `delta` in 1/120 notches, as finely as the device reports it (a trackpad moves by
// fractions of a notch), and `ticks`, the whole notches it completed (the remainder carries over to the next movement).
// what moves continuously (a scroll view) takes the delta, what moves in steps (a volume or slider step) the ticks
struct WheelDelta {
    int delta;
    int ticks;
};

struct ButtonEvent {
    uint64_t timestamp;
    MouseButtonFlags btn;
    bool down;
    bool consumed;
    uint8_t clicks{1};  // presses of the button in a row, as the platform counts them (a double click's second: 2)
};

class MouseListener {
   public:
    MouseListener() = default;
    virtual ~MouseListener() = default;

    MouseListener(const MouseListener &) = default;
    MouseListener &operator=(const MouseListener &) = default;
    MouseListener(MouseListener &&) = default;
    MouseListener &operator=(MouseListener &&) = default;

    virtual void onButtonChange(ButtonEvent & /*event*/) {}

    virtual void onWheelVertical(WheelDelta /*wheel*/) {}
    virtual void onWheelHorizontal(WheelDelta /*wheel*/) {}
};

#endif
