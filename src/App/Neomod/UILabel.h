#pragma once
// Copyright (c) 2025, kiwec, 2026, WH, All rights reserved.
#include "CBaseUILabel.h"

class UILabel final : public CBaseUILabel {
   public:
    UILabel(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, UString name = {},
            const UString &text = {})
        : CBaseUILabel(xPos, yPos, xSize, ySize, std::move(name), text) {}

    void update(CBaseUIEventCtx &c) override;
    void setTooltipText(const UString &text);

    // debugging
    [[nodiscard]] inline UString getTooltipText() const { return UString::join(this->tooltipTextLines, "\n"); }

   private:
    void onFocusStolen() override;

    std::vector<UString> tooltipTextLines;

    bool bFocusStolenDelay{false};
};
