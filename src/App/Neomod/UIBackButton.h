#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "AnimationHandler.h"
#include "CBaseUIButton.h"

class UIBackButton final : public CBaseUIButton {
    NOCOPY_NOMOVE(UIBackButton)
   public:
    UIBackButton(float xPos, float yPos, float xSize, float ySize, std::string name);
    ~UIBackButton() override;

    void draw() override;
    void updateInput(CBaseUIEventCtx &c) override;

    void onMouseDownInside(MouseButtonFlags buttons = MouseButtonFlags::MF_LEFT) override;
    void onMouseInside() override;
    void onMouseOutside() override;

    void updateLayout();

    void resetAnimation();

   private:
    void onFocusStolen() override;

    AnimFloat fAnimation;

    bool bUseDefaultBack{false};
    bool bFocusStolenDelay{false};
};
