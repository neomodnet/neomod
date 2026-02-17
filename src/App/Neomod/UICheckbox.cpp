// Copyright (c) 2016, PG, All rights reserved.
#include "UICheckbox.h"

#include <utility>

#include "Engine.h"
#include "TooltipOverlay.h"
#include "UI.h"

UICheckbox::UICheckbox(float xPos, float yPos, float xSize, float ySize, UString name, UString text)
    : CBaseUICheckbox(xPos, yPos, xSize, ySize, std::move(name), std::move(text)) {
    this->bFocusStolenDelay = false;
}

void UICheckbox::update(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;
    CBaseUICheckbox::update(c);

    if(this->isMouseInside() && this->tooltipTextLines.size() > 0 && !this->bFocusStolenDelay) {
        ui->getTooltipOverlay()->begin();
        {
            for(const auto& tooltipTextLine : this->tooltipTextLines) {
                ui->getTooltipOverlay()->addLine(tooltipTextLine);
            }
        }
        ui->getTooltipOverlay()->end();
    }

    this->bFocusStolenDelay = false;
}

void UICheckbox::onFocusStolen() {
    CBaseUICheckbox::onFocusStolen();

    this->bMouseInside = false;
    this->bFocusStolenDelay = true;
}

void UICheckbox::setTooltipText(const UString& text) { this->tooltipTextLines = text.split(US_("\n")); }
