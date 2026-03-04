#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "AnimationHandler.h"
#include "CBaseUIButton.h"

class UIBackButton final : public CBaseUIButton {
    NOCOPY_NOMOVE(UIBackButton)
   public:
    UIBackButton(float xPos, float yPos, float xSize, float ySize, UString name);
    ~UIBackButton() override;

    void draw() override;
    void update(CBaseUIEventCtx &c) override;

    void onMouseDownInside(bool left = true, bool right = false) override;
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
