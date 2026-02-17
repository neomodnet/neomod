// Copyright (c) 2025, kiwec, 2026, WH, All rights reserved.
#include "UILabel.h"

#include "Osu.h"
#include "TooltipOverlay.h"
#include "UI.h"

void UILabel::update(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;
    CBaseUILabel::update(c);

    if(this->isMouseInside() && this->tooltipTextLines.size() > 0 && !this->bFocusStolenDelay) {
        ui->getTooltipOverlay()->begin();
        for(const auto& tooltipTextLine : this->tooltipTextLines) {
            ui->getTooltipOverlay()->addLine(tooltipTextLine);
        }
        ui->getTooltipOverlay()->end();
    }

    this->bFocusStolenDelay = false;
}

void UILabel::onFocusStolen() {
    CBaseUILabel::onFocusStolen();
    this->bMouseInside = false;
    this->bFocusStolenDelay = true;
}

void UILabel::setTooltipText(const UString& text) { this->tooltipTextLines = text.split(US_("\n")); }
