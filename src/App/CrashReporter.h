// Copyright (c) 2026, kiwec, All rights reserved.
#pragma once
#include "App.h"
#include "SyncStoptoken.h"

#include <memory>

class CBaseUIBox;
class CBaseUICheckbox;
class CBaseUIStyledButton;
class CBaseUIStyledLabel;
class CBaseUIStyledTextbox;

// asks the player whether to send the crash data and the game log (see CrashHandler.h).
// the crash handler starts it in a new process with -crashreport <dir>
class CrashReporter final : public App {
    NOCOPY_NOMOVE(CrashReporter)
   public:
    CrashReporter();
    ~CrashReporter() override;

    void draw() override;
    void update() override;
    void onKeyDown(KeyboardEvent &e) override;
    void onKeyUp(KeyboardEvent &e) override;
    void onResolutionChanged(vec2 newResolution) override;

   private:
    void sendReportThen(void (*leave)());

    std::unique_ptr<CBaseUIBox> root;
    CBaseUICheckbox *sendCheckbox;
    CBaseUIStyledTextbox *comment;
    CBaseUIStyledLabel *status;
    CBaseUIStyledButton *quitButton;
    CBaseUIStyledButton *restartButton;
    Sync::stop_source uploadCancel;
};
