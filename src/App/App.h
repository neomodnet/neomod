#pragma once
// Copyright (c) 2015, PG & 2025, WH, All rights reserved.
#ifndef APP_H
#define APP_H

#include "Vectors.h"
#include "KeyboardListener.h"
#include "Color.h"

#include <string_view>
#include <memory>
#include <functional>

class Sound;

enum class ActionSound : uint8_t {
    DELETING_TEXT,     // A sound to play when deleting text
    MOVE_TEXT_CURSOR,  // A sound to play when moving the next cursor position
    ADJUST_SLIDER,     // A sound to play when adjusting UI sliders
    TYPING1,           // A (preferably unique) sound to play when a key is pressed
    TYPING2,           // A (preferably unique) sound to play when a key is pressed
    TYPING3,           // A (preferably unique) sound to play when a key is pressed
    TYPING4,           // A (preferably unique) sound to play when a key is pressed
};

// Different categories' implementations may present differently.
enum class NotificationClass : uint8_t {
    CUSTOM,
    TOAST,
    BANNER,
};

// Different categories' implementations may present differently.
enum class NotificationPreset : uint8_t {
    CUSTOM,
    INFO,
    ERROR,
    SUCCESS,
    STATUS,
};

// Something that should be done if a notification is interacted with.
using NotificationCallback = std::function<void()>;

// The only required field is the text.
struct NotificationInfo {
    NotificationInfo() = default;
    NotificationInfo(std::string_view text) : NotificationInfo() { this->text = text; }
    NotificationInfo(std::string_view text, NotificationPreset preset) : NotificationInfo(text) {
        this->preset = preset;
    }

    NotificationCallback callback{nullptr};
    std::string text;
    Color custom_color{0xffffffff};
    float duration{-1.f};
    NotificationClass nclass{NotificationClass::TOAST};
    NotificationPreset preset{NotificationPreset::INFO};
};

class Engine;
class App : public KeyboardListener {
    NOCOPY_NOMOVE(App)
   protected:
    friend Engine;
    App() = default;

   public:
    ~App() override = default;

    virtual void draw() {}
    virtual void update() {}
    [[nodiscard]] virtual bool isInGameplay() const { return false; }
    [[nodiscard]] virtual bool isInUnpausedGameplay() const { return false; }

    void onKeyDown(KeyboardEvent& /*e*/) override {}
    void onKeyUp(KeyboardEvent& /*e*/) override {}
    void onChar(KeyboardEvent& /*e*/) override {}

    virtual void onResolutionChanged(vec2 /*newResolution*/) {}
    virtual void onDPIChanged() {}

    virtual void onFocusGained() {}
    virtual void onFocusLost() {}

    virtual void onMinimized() {}
    virtual void onRestored() {}

    virtual bool onShutdown() { return true; }

    // may return null!
    [[nodiscard]] virtual Sound* getSound(ActionSound /*action*/) const { return nullptr; }

    virtual void showNotification(const NotificationInfo& /*info*/) {}
};

extern App *app;

#endif
