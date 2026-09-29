// Copyright (c) 2026, kiwec, All rights reserved.
#include <SDL3/SDL_events.h>

#include "Engine.h"
#include "Environment.h"
#include "Touch.h"

Touch::Touch() = default;
Touch::~Touch() = default;

void Touch::addListener(TouchListener* listener, bool insertOnTop) {
    if(listener == nullptr) {
        engine->showMessageError("Touch Error", "addListener(NULL)!");
        return;
    }

    if(insertOnTop)
        this->listeners.insert(this->listeners.begin(), listener);
    else
        this->listeners.push_back(listener);
}

void Touch::removeListener(TouchListener* listener) { std::erase(this->listeners, listener); }

static inline vec2 event_to_abs_touch(SDL_TouchFingerEvent* ev) { return vec2(ev->x, ev->y) * env->getWindowSize(); }

void Touch::onFingerDown(SDL_TouchFingerEvent* ev) {
    assert(ev->type == SDL_EVENT_FINGER_DOWN);

    auto& finger = this->fingers.emplace_back(Finger{
        .id = ev->fingerID,
        .pos = event_to_abs_touch(ev),
        .pressed_since_ns = ev->timestamp,
        .last_event_ns = ev->timestamp,
    });

    for(auto* listener : this->listeners) {
        listener->onFingerPressed(finger);
    }
}

void Touch::onFingerMove(SDL_TouchFingerEvent* ev) {
    assert(ev->type == SDL_EVENT_FINGER_MOTION);

    for(auto& finger : this->fingers) {
        if(finger.id != ev->fingerID) continue;

        finger.pos = event_to_abs_touch(ev);
        finger.last_event_ns = ev->timestamp;

        for(auto* listener : this->listeners) {
            listener->onFingerMoved(finger);
        }

        break;
    }
}

void Touch::onFingerUp(SDL_TouchFingerEvent* ev) {
    assert(ev->type == SDL_EVENT_FINGER_UP);

    for(auto finger_it = this->fingers.begin(); finger_it != this->fingers.end();) {
        if(finger_it->id != ev->fingerID) {
            ++finger_it;
            continue;
        }

        auto finger = *finger_it;
        this->fingers.erase(finger_it);

        finger.pos = event_to_abs_touch(ev);
        finger.last_event_ns = ev->timestamp;

        for(auto* listener : this->listeners) {
            listener->onFingerReleased(finger);
        }

        break;
    }
}
