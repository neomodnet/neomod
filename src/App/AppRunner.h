// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#ifndef APPRUNNER_H
#define APPRUNNER_H

#include "config.h"

#ifdef MCENGINE_TESTS

#include "App.h"
#include "MouseListener.h"

#include <memory>

namespace Mc {
struct AppDescriptor;
}

class AppRunner final : public App, public MouseListener {
    NOCOPY_NOMOVE(AppRunner)
   public:
    AppRunner() = delete;
    AppRunner(bool testMode, const Mc::AppDescriptor *appDesc);
    ~AppRunner() override;

    void draw() override;
    void update() override;

    void onKeyDown(KeyboardEvent &e) override;
    void onKeyUp(KeyboardEvent &e) override;

    void onButtonChange(ButtonEvent &event) override;
    void onWheelVertical(WheelDelta wheel) override;
    void onWheelHorizontal(WheelDelta wheel) override;

    void onResolutionChanged(vec2 newResolution) override;
    void onDPIChanged() override;

    void onFocusGained() override;
    void onFocusLost() override;

    void onMinimized() override;
    void onRestored() override;

    bool onShutdown() override;

    [[nodiscard]] bool isInGameplay() const override;
    [[nodiscard]] bool isInUnpausedGameplay() const override;

    [[nodiscard]] Sound *getSound(ActionSound action) const override;
    void showNotification(const NotificationInfo &info) override;

   private:
    void launchApp(const Mc::AppDescriptor &appDesc);
    void returnToMenu();

    std::unique_ptr<App> m_activeApp;
    int m_iHoveredIndex{-1};
    // let's launch the app when we release the mouse button instead of when initially pressing it
    // prevents window focus fuckery
    int m_iMouseDownIndex{-1};
    bool m_bTestMode;
};

#endif  // MCENGINE_TESTS

#endif
