#pragma once
// Copyright (c) 2011, PG, All rights reserved.

#include "AnimationHandler.h"
#include "CBaseUIElement.h"
#include "Console.h"

#include <array>
#include <memory>
#include <vector>

class McFont;
class ConsoleTextbox;
class ConsoleSuggestionList;

// quake style console: a textbox sliding in at the bottom of the screen, plus the fading log overlay
class ConsoleBox : public CBaseUIElement {
    NOCOPY_NOMOVE(ConsoleBox)
   public:
    ConsoleBox();
    ~ConsoleBox() override;

    void draw() override;
    void drawLogOverlay();
    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;

    void onKeyDown(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;

    void onResolutionChange(vec2 newResolution);

    // returns false while an open/close animation blocks the toggle
    bool toggle();
    void show();
    void hide();
    [[nodiscard]] bool isOpen() const;

    // get
    bool isBusy() override;
    bool isActive() override;

    [[nodiscard]] std::span<CBaseUIElement *const> getAllChildren() const override { return this->children; }

   private:
    float getAnimTargetY();

    float getDPIScale();

    std::unique_ptr<ConsoleTextbox> textbox{nullptr};
    std::unique_ptr<ConsoleSuggestionList> suggestion{nullptr};
    std::array<CBaseUIElement *, 2> children{};  // for tree walkers

    bool bConsoleAnimateOnce{false};  // set to true for on-launch anim in
    float fConsoleDelay;
    AnimFloat fConsoleAnimation;
    bool bConsoleAnimateIn{false};
    bool bConsoleAnimateOut{false};

    // the overlay shows the newest scrollback entries until they fade out
    float fLogTime{0.f};
    AnimFloat fLogYPos;
    std::vector<Console::LogEntry> log_entries;
    u64 iLogSequence{0};
    u64 iLogClearGeneration{0};
    McFont *logFont;
};
