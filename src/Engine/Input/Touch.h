// Copyright (c) 2026, kiwec, All rights reserved.
#pragma once
#include "InputDevice.h"
#include "noinclude.h"
#include "Vectors.h"

#include <vector>

struct Finger final {
    // unique id from the moment the finger is pressed until the finger is released
    u64 id;

    // position in window pixels
    vec2 pos;

    // timestamp at which the finger was pressed
    u64 pressed_since_ns;

    // last time the finger was updated
    u64 last_event_ns;
};

class TouchListener {
   public:
    TouchListener() = default;
    virtual ~TouchListener() = default;

    TouchListener(const TouchListener&) = default;
    TouchListener& operator=(const TouchListener&) = default;
    TouchListener(TouchListener&&) = default;
    TouchListener& operator=(TouchListener&&) = default;

    virtual void onFingerPressed(Finger /*finger*/) {}
    virtual void onFingerReleased(Finger /*finger*/) {}
    virtual void onFingerMoved(Finger /*finger*/) {}
};

struct SDL_TouchFingerEvent;

class Touch final {
    NOCOPY_NOMOVE(Touch)

   public:
    Touch();
    ~Touch();

    [[nodiscard]] const auto& getFingers() const { return this->fingers; }

    // event handling
    MC_UNREVOCABLE void addListener(TouchListener* listener, bool insertOnTop = false);
    void removeListener(TouchListener* listener);

    // input handling
    void onFingerDown(SDL_TouchFingerEvent* ev);
    void onFingerMove(SDL_TouchFingerEvent* ev);
    void onFingerUp(SDL_TouchFingerEvent* ev);

   private:
    std::vector<Finger> fingers;
    std::vector<TouchListener*> listeners;
};
