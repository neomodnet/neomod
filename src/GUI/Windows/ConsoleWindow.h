#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIWindow.h"

class CBaseUIButton;
class ConsoleLogView;
class ConsoleSuggestionList;
class ConsoleTextbox;

// source engine style console: a movable/resizable window with the log scrollback and an input box with suggestions
class ConsoleWindow final : public CBaseUIWindow {
    NOCOPY_NOMOVE(ConsoleWindow)
   public:
    ConsoleWindow();
    ~ConsoleWindow() override;

    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;

    void onKeyDown(KeyboardEvent &e) override;

    void onResized() override;

    void toggle();
    void show();

   protected:
    void onClosed() override;

   private:
    void layout();
    void placeSuggestions();

    // owned by the window's content container
    ConsoleLogView *logView;
    ConsoleTextbox *input;
    CBaseUIButton *submitButton;
    ConsoleSuggestionList *suggestions;

    bool bPlaced{false};  // the default geometry is derived from the screen size on first show
};
