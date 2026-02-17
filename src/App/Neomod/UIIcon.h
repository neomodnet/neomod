#pragma once
// Copyright (c) 2025, kiwec, All rights reserved.
#include "CBaseUILabel.h"

class UIIcon final : public CBaseUILabel {
   public:
    UIIcon(char16_t icon);

    void update(CBaseUIEventCtx &c) override;
    void setTooltipText(const UString& text);

    // debugging
    [[nodiscard]] inline UString getTooltipText() const { return UString::join(this->tooltipTextLines, "\n"); }

   private:
    void onFocusStolen() override;

    std::vector<UString> tooltipTextLines;

    bool bFocusStolenDelay{false};
};
