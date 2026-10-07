// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#ifndef AUDIOTESTER_H
#define AUDIOTESTER_H

#include "config.h"

#include "App.h"
#include "MouseListener.h"

#if (defined(MCENGINE_FEATURE_SOLOUD) && defined(MCENGINE_FEATURE_BASS))

#include <string>
#include <vector>

namespace Mc::Tests {

class AudioTesterImpl;

class AudioTester : public App, public MouseListener {
    NOCOPY_NOMOVE(AudioTester)
   public:
    AudioTester();
    ~AudioTester() override;

    void draw() override;
    void update() override;

    void onResolutionChanged(vec2 newResolution) override;
    void onDPIChanged() override;

    void onFocusGained() override;
    void onFocusLost() override;

    void onMinimized() override;
    void onRestored() override;

    [[nodiscard]] bool isInGameplay() const override;
    [[nodiscard]] bool isInUnpausedGameplay() const override;

    bool onShutdown() override;

    [[nodiscard]] Sound *getSound(ActionSound action) const override;

    void showNotification(const NotificationInfo &notif) override;

   public:
    // keyboard
    void onKeyDown(KeyboardEvent &e) override;
    void onKeyUp(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;

    // mouse
    void onButtonChange(ButtonEvent &event) override;
    void onWheelVertical(WheelDelta wheel) override;
    void onWheelHorizontal(WheelDelta wheel) override;

   private:
    friend AudioTesterImpl;
    std::unique_ptr<AudioTesterImpl> m_impl;
};
}  // namespace Mc::Tests

#else
// nothing (need both bass and soloud to test for now)
namespace Mc::Tests {

class AudioTester : public App, public MouseListener {
   public:
    AudioTester() : App(), MouseListener() {}
    ~AudioTester() override = default;
};

}  // namespace Mc::Tests
#endif
#endif
