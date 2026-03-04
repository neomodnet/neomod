#pragma once
// Copyright (c) 2011, PG, All rights reserved.

#include "AnimationHandler.h"
#include "CBaseUIElement.h"
#include "SyncMutex.h"
#include "UString.h"
#include "Color.h"

#include <atomic>
#include <memory>
namespace Logger {
class ConsoleBoxSink;
}
class CBaseUITextbox;
class CBaseUIButton;
class CBaseUIScrollView;
class McFont;
class ConsoleBoxTextbox;

class ConsoleBox : public CBaseUIElement {
    NOCOPY_NOMOVE(ConsoleBox)
   public:
    ConsoleBox();
    ~ConsoleBox() override;

    void draw() override;
    void drawLogOverlay();
    void update(CBaseUIEventCtx &c) override;

    void onKeyDown(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;

    void onResolutionChange(vec2 newResolution);

    void processCommand(const UString &command);

    // set
    void setRequireShiftToActivate(bool requireShiftToActivate) {
        this->bRequireShiftToActivate = requireShiftToActivate;
    }

    // get
    bool isBusy() override;
    bool isActive() override;

   private:
    friend class Logger::ConsoleBoxSink;
    void log(const UString &text, Color textColor = 0xffffffff);

    struct LOG_ENTRY {
        UString text;
        Color textColor;
    };

   private:
    // callback
    inline void clear() { this->bClearPending = true; }

    void onSuggestionClicked(CBaseUIButton *suggestion);

    void addSuggestion(const UString &text, const UString &helpText, const UString &command);
    void clearSuggestions();

    void show();
    void toggle(KeyboardEvent &e);

    float getAnimTargetY();

    float getDPIScale();

    void processPendingLogAnimations();

    int iSuggestionCount{0};
    int iSelectedSuggestion{-1};  // for up/down buttons

    std::unique_ptr<ConsoleBoxTextbox> textbox{nullptr};
    std::unique_ptr<CBaseUIScrollView> suggestion{nullptr};
    std::vector<CBaseUIButton *> vSuggestionButtons;
    float fSuggestionY{0.f};

    bool bRequireShiftToActivate{false};
    bool bConsoleAnimateOnce{false};  // set to true for on-launch anim in
    float fConsoleDelay;
    AnimFloat fConsoleAnimation;
    bool bConsoleAnimateIn{false};
    bool bConsoleAnimateOut{false};

    bool bSuggestionAnimateIn{false};
    bool bSuggestionAnimateOut{false};
    float fSuggestionAnimation{0.f};

    float fLogTime{0.f};
    AnimFloat fLogYPos;
    std::vector<LOG_ENTRY> log_entries;
    McFont *logFont;

    std::vector<UString> commandHistory;
    int iSelectedHistory{-1};
    bool bClearPending{false};

    Sync::mutex logMutex;

    // thread-safe log animation state
    std::atomic<bool> bLogAnimationResetPending{false};
    std::atomic<float> fPendingLogTime{0.f};
    std::atomic<bool> bForceLogVisible{
        false};  // needed as an "ohshit" when a ton of lines are added in a single frame after
                 // the log has been hidden already
};
