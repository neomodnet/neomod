// Copyright (c) 2014, PG, All rights reserved.
#include "CBaseUICheckbox.h"

#include <utility>

#include "Color.h"
#include "Engine.h"
#include "MakeDelegateWrapper.h"
#include "Font.h"
#include "Graphics.h"

CBaseUICheckbox::CBaseUICheckbox(float xPos, float yPos, float xSize, float ySize, std::string name, std::string text)
    : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name), std::move(text)) {
    this->bChecked = false;
    this->changeCallback = {};

    CBaseUIButton::setClickCallback(SA::MakeDelegate<&CBaseUICheckbox::onPressed>(this));
}

void CBaseUICheckbox::draw() {
    if(!this->bVisible) return;

    const float dpiScale = ((float)this->font->getDPI() / 96.0f);  // NOTE: abusing font dpi

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

    // (dimmed while disabled)
    const Color frameColor = this->bEnabled ? this->frameColor : Colors::scale(this->frameColor, 0.5f);
    const Color textColor = this->bEnabled ? this->textColor : Colors::scale(this->textColor, 0.5f);

    // draw hover rects
    const int hoverRectOffset = std::round(3.0f * dpiScale);
    g->setColor(frameColor);
    const bool drawClickHeldRect = this->bActive;
    const bool drawHoverRect = !drawClickHeldRect && (this->bEnabled && this->isMouseInside());
    if(drawHoverRect || drawClickHeldRect) this->drawHoverRect(hoverRectOffset, drawClickHeldRect);

    // draw block
    const int innerBlockPosOffset = std::round(2.0f * dpiScale);
    const int blockBorder = std::round(this->getBlockBorder());
    const int blockSize = std::round(this->getBlockSize());
    const int innerBlockSizeOffset = 2 * innerBlockPosOffset - 1;
    g->drawRect(this->getPos().x + blockBorder, this->getPos().y + blockBorder, blockSize, blockSize);
    if(this->bChecked)
        g->fillRect(this->getPos().x + blockBorder + innerBlockPosOffset,
                    this->getPos().y + blockBorder + innerBlockPosOffset, blockSize - innerBlockSizeOffset,
                    blockSize - innerBlockSizeOffset);

    // draw text
    const int shadowOffset = std::round(1.0f * dpiScale);
    if(this->font != nullptr && this->getText().length() > 0) {
        // g->pushClipRect(McRect(this->getPos().x + 1, this->getPos().y + 1, this->getSize().x - 1, this->getSize().y - 1));

        g->setColor(textColor);
        g->pushTransform();
        {
            g->translate((int)(this->getPos().x + this->getBlockBorder() * 2 + this->getBlockSize()),
                         (int)(this->getPos().y + this->getSize().y / 2.0f + this->fStringHeight / 2.0f));
            /// g->translate(this->getPos().x + (this->getSize().x - blockBorder*2 - blockSize)/2.0f - m_fStringWidth/2.0f +
            /// blockBorder*2 + blockSize, this->getPos().y + m_vSize.y/2.0f + m_fStringHeight/2.0f);

            g->translate(shadowOffset, shadowOffset);
            g->setColor(0xff212121);
            g->drawString(this->font, this->getText());

            g->translate(-shadowOffset, -shadowOffset);
            g->setColor(textColor);
            g->drawString(this->font, this->getText());
        }
        g->popTransform();

        // g->popClipRect();
    }
}

CBaseUICheckbox *CBaseUICheckbox::setChecked(bool checked, bool fireChangeEvent) {
    if(this->bChecked != checked) {
        if(fireChangeEvent)
            this->onPressed();
        else
            this->bChecked = checked;
    }

    return this;
}

void CBaseUICheckbox::onPressed() {
    this->bChecked = !this->bChecked;
    if(this->changeCallback != nullptr) this->changeCallback(this);
}

CBaseUICheckbox *CBaseUICheckbox::setSizeToContent(int horizontalBorderSize, int verticalBorderSize) {
    // HACKHACK: broken
    CBaseUIButton::setSizeToContent(horizontalBorderSize, verticalBorderSize);
    /// setSize(this->fStringWidth+2*horizontalBorderSize, this->fStringHeight + 2*verticalBorderSize);
    this->setSize(this->getBlockBorder() * 2 + this->getBlockSize() + this->getBlockBorder() + this->fStringWidth +
                      horizontalBorderSize * 2,
                  this->fStringHeight + verticalBorderSize * 2);

    return this;
}

CBaseUICheckbox *CBaseUICheckbox::setWidthToContent(int horizontalBorderSize) {
    // HACKHACK: broken
    CBaseUIButton::setWidthToContent(horizontalBorderSize);
    /// setSize(this->fStringWidth+2*horizontalBorderSize, this->fStringHeight + 2*verticalBorderSize);
    this->setSizeX(this->getBlockBorder() * 2 + this->getBlockSize() + this->getBlockBorder() + this->fStringWidth +
                   horizontalBorderSize * 2);

    return this;
}
