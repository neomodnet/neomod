#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.

#include "UIScreen.h"
#include "Delegate.h"

class CBaseUILabel;
class CBaseUITextbox;
class Graphics;
class UIButton;

class PromptOverlay final : public UIScreen {
   public:
    PromptOverlay();
    void onResolutionChange(vec2 newResolution) override;

    void draw() override;
    void update(CBaseUIEventCtx &c) override;
    void onKeyDown(KeyboardEvent &e) override;
    void onKeyUp(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;

    using PromptResponseCallback = SA::delegate<void(const UString &)>;
    void prompt(const UString &msg, const PromptResponseCallback &callback);

   private:
    void on_ok();
    void on_cancel();

    CBaseUILabel *prompt_label;
    CBaseUITextbox *prompt_input;
    UIButton *ok_btn;
    UIButton *cancel_btn;
    PromptResponseCallback callback;
};
