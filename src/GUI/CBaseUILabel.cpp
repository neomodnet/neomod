// Copyright (c) 2014, PG, All rights reserved.
#include "CBaseUILabel.h"

#include <utility>

#include "Engine.h"
#include "ResourceManager.h"
#include "Font.h"
#include "Graphics.h"

CBaseUILabel::CBaseUILabel(float xPos, float yPos, float xSize, float ySize, UString name, const UString& text)
    : CBaseUIElement(xPos, yPos, xSize, ySize, std::move(name)) {
    this->font = engine->getDefaultFont();
    this->setText(text);
}

void CBaseUILabel::draw() {
    if(!this->bVisible) return;

    // draw background
    if(this->bDrawBackground) {
        g->setColor(this->backgroundColor);
        g->fillRect(this->getPos().x + 1, this->getPos().y + 1, this->getSize().x - 1, this->getSize().y - 1);
    }

    // draw frame
    if(this->bDrawFrame) {
        g->setColor(this->frameColor);
        g->drawRect(this->getPos(), this->getSize());
    }

    // draw text
    this->drawText();
}

void CBaseUILabel::drawText() {
    if(this->font != nullptr && this->sText.length() > 0) {
        f32 xPosAdd = 0;
        switch(this->textJustification) {
            case TEXT_JUSTIFICATION::LEFT:
                break;
            case TEXT_JUSTIFICATION::CENTERED:
                xPosAdd = this->getSize().x / 2.f - this->fStringWidth / 2.f;
                break;
            case TEXT_JUSTIFICATION::RIGHT:
                xPosAdd = this->getSize().x - this->fStringWidth;
                break;
        }

        // g->pushClipRect(McRect(this->getPos(), this->getSize()));

        g->pushTransform();
        {
            g->scale(this->fScale, this->fScale);  // XXX: not sure if scaling respects text justification
            g->translate((i32)(this->getPos().x + xPosAdd),
                         (i32)(this->getPos().y + this->getSize().y / 2.f + this->fStringHeight / 2.f));
            if(this->bDrawTextShadow) {
                const f32 shadowOffset =
                    std::round(1.0f * ((f32)this->font->getDPI() / 96.0f));  // NOTE: "abusing" font dpi

                g->drawString(
                    this->font, this->sText,
                    TextShadow{.col_text = this->textColor, .col_shadow = this->shadowColor, .offs_px = shadowOffset});
            } else {
                g->setColor(this->textColor);
                g->drawString(this->font, this->sText);
            }
        }
        g->popTransform();

        // g->popClipRect();
    }
}

void CBaseUILabel::update(CBaseUIEventCtx& c) { CBaseUIElement::update(c); }

void CBaseUILabel::updateStringMetrics() {
    if(this->font != nullptr) {
        this->fStringWidth = this->font->getStringWidth(this->sText);
        this->fStringHeight = this->font->getHeight();
    }
}
