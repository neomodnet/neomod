#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.

#include "UIScreen.h"
#include "Delegate.h"
#include "Registration.h"

class CBaseUILabel;
class CBaseUITextbox;
class UIButton;

class PromptOverlay final : public UIScreen {
   public:
    PromptOverlay();
    void onResolutionChange(vec2 newResolution) override;

    void draw() override;
    void onKeyDown(KeyboardEvent &e) override;
    void onKeyUp(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;

    using PromptResponseCallback = SA::delegate<void(std::string_view)>;
    // asks for a line of text; `callback` gets it on OK. resetting the Registration closes the prompt if it's still
    // this one
    Mc::Registration prompt(std::string msg, const PromptResponseCallback &callback);

   private:
    void on_ok();
    void close();

    CBaseUILabel *prompt_label;
    CBaseUITextbox *prompt_input;
    UIButton *ok_btn;
    UIButton *cancel_btn;
    PromptResponseCallback callback;
    u64 promptId{0};  // the latest prompt()
};
