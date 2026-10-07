// Copyright (c) 2015, PG, 2026, WH, All rights reserved.
#pragma once

#ifndef BASEFRAMEWORKTEST_H
#define BASEFRAMEWORKTEST_H

#include "App.h"
#include "MouseListener.h"

#include <memory>

struct CBaseUIEventCtx;

namespace Mc::Tests {

class FrameworkTestButton;

class BaseFrameworkTest : public App, public MouseListener {
    NOCOPY_NOMOVE(BaseFrameworkTest)
   public:
    BaseFrameworkTest();
    ~BaseFrameworkTest() override;

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

    // UI
    std::unique_ptr<FrameworkTestButton> m_testButton;
};
}  // namespace Mc::Tests

#endif
