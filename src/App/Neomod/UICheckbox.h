#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "CBaseUICheckbox.h"

class UICheckbox : public CBaseUICheckbox {
   public:
    UICheckbox(float xPos, float yPos, float xSize, float ySize, std::string name, std::string text);

    void updateInput(CBaseUIEventCtx &c) override;

    void setTooltipText(std::string_view text);

   private:
    void onFocusStolen() override;

    std::vector<std::string> tooltipTextLines;

    bool bFocusStolenDelay;
};
