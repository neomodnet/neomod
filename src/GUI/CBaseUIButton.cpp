// Copyright (c) 2013, PG, All rights reserved.
#include "CBaseUIButton.h"

#include <utility>

#include "Engine.h"
#include "Graphics.h"
#include "Mouse.h"
#include "ResourceManager.h"
#include "Font.h"

CBaseUIButton::CBaseUIButton(std::nullptr_t notext, f32 xPos, f32 yPos, f32 xSize, f32 ySize)
    : CBaseUIElement(xPos, yPos, xSize, ySize, notext) {
    this->font = engine->getDefaultFont();
}

CBaseUIButton::CBaseUIButton(f32 xPos, f32 yPos, f32 xSize, f32 ySize, std::string name, std::string text)
    : CBaseUIElement(xPos, yPos, xSize, ySize, std::move(name)) {
    this->font = engine->getDefaultFont();

    if(!text.empty()) {
        this->setText(std::move(text));
    }
}

CBaseUIButton::~CBaseUIButton() = default;

void CBaseUIButton::click(bool left, bool right) { this->onClicked(left, right); }

// set
CBaseUIButton *CBaseUIButton::setDrawFrame(bool drawFrame) {
    this->bDrawFrame = drawFrame;
    return this;
}
CBaseUIButton *CBaseUIButton::setDrawBackground(bool drawBackground) {
    this->bDrawBackground = drawBackground;
    return this;
}
CBaseUIButton *CBaseUIButton::setDrawShadow(bool enabled) {
    this->bDrawShadow = enabled;
    return this;
}

CBaseUIButton *CBaseUIButton::setFrameColor(Color frameColor) {
    this->frameColor = frameColor;
    return this;
}
CBaseUIButton *CBaseUIButton::setBackgroundColor(Color backgroundColor) {
    this->backgroundColor = backgroundColor;
    return this;
}
CBaseUIButton *CBaseUIButton::setTextColor(Color textColor) {
    this->textColor = textColor;
    this->textBrightColor = this->textDarkColor = 0;
    return this;
}
CBaseUIButton *CBaseUIButton::setTextBrightColor(Color textBrightColor) {
    this->textBrightColor = textBrightColor;
    return this;
}
CBaseUIButton *CBaseUIButton::setTextDarkColor(Color textDarkColor) {
    this->textDarkColor = textDarkColor;
    return this;
}
CBaseUIButton *CBaseUIButton::setTextJustification(TEXT_JUSTIFICATION j) {
    this->textJustification = j;
    return this;
}

CBaseUIButton *CBaseUIButton::setText(std::string text) {
    this->sText = std::move(text);
    this->updateStringMetrics();
    // (a natural size may depend on it)
    this->requestLayout();
    return this;
}

CBaseUIButton *CBaseUIButton::setFont(McFont *font) {
    this->font = font;
    this->updateStringMetrics();
    return this;
}

CBaseUIButton *CBaseUIButton::setSizeToContent(int horizontalBorderSize, int verticalBorderSize) {
    this->setSize(this->fStringWidth + 2 * horizontalBorderSize, this->fStringHeight + 2 * verticalBorderSize);
    return this;
}

CBaseUIButton *CBaseUIButton::setWidthToContent(int horizontalBorderSize) {
    this->setSizeX(this->fStringWidth + 2 * horizontalBorderSize);
    return this;
}

// get
Color CBaseUIButton::getFrameColor() const { return this->frameColor; }
Color CBaseUIButton::getBackgroundColor() const { return this->backgroundColor; }
Color CBaseUIButton::getTextColor() const { return this->textColor; }
std::string_view CBaseUIButton::getText() const { return this->sText; }
McFont *CBaseUIButton::getFont() const { return this->font; }

void CBaseUIButton::onResized() { this->updateStringMetrics(); }

void CBaseUIButton::draw() {
    if(!this->isVisible() || !this->isVisibleOnScreen()) return;

    // draw background
    if(this->bDrawBackground) {
        this->drawBackground();
    }

    // draw frame
    if(this->bDrawFrame) {
        this->drawFrame();
    }

    // draw hover rects
    const int hoverRectOffset = (int)std::round(3.f * ((f32)this->font->getDPI() / 96.f));  // NOTE: abusing font dpi
    g->setColor(this->frameColor);
    const bool drawClickHeldRect = this->bActive && this->bEnabled;
    const bool drawHoverRect =
        !drawClickHeldRect && (this->bEnabled && this->isMouseInside() && (this->bActive || (!mouse->isLeftDown())));
    if(drawHoverRect || drawClickHeldRect) {
        this->drawHoverRect(hoverRectOffset, drawClickHeldRect);
    }

    // draw text
    this->drawText();
}

void CBaseUIButton::drawBackground() {
    g->setColor(this->backgroundColor);
    g->fillRect((int)this->getPos().x + 1, (int)this->getPos().y + 1, (int)this->getSize().x - 1,
                (int)this->getSize().y - 1);
}

void CBaseUIButton::drawFrame() {
    g->setColor(this->frameColor);
    g->drawRect(this->getPos(), this->getSize());
}

void CBaseUIButton::drawText() {
    if(this->font == nullptr || !this->isVisible() || !this->isVisibleOnScreen() || this->getText().length() < 1)
        return;

    // NOTE: don't decrease the size of this clip rect, console box suggestion text gets cut off at the bottom
    g->pushClipRect(McRect(this->getPos().x + 1, this->getPos().y + 1, this->getSize().x - 1, this->getSize().y));
    {
        g->pushTransform();
        {
            // Justification logic pasted from CBaseUILabel::drawText()
            f32 xPosAdd = 0;
            switch(this->textJustification) {
                case TEXT_JUSTIFICATION::LEFT:
                    break;
                case TEXT_JUSTIFICATION::CENTERED:
                    xPosAdd = this->getSize().x / 2.0f - this->fStringWidth / 2.0f;
                    break;
                case TEXT_JUSTIFICATION::RIGHT:
                    xPosAdd = this->getSize().x - this->fStringWidth;
                    break;
            }

            g->translate((i32)(this->getPos().x + xPosAdd),
                         (i32)(this->getPos().y + this->getSize().y / 2.f + this->fStringHeight / 2.f));

            const Color actualTextColor{this->textBrightColor ? this->textBrightColor : this->textColor};
            if(this->bDrawShadow) {
                const f32 shadowOffset = std::round((f32)this->font->getDPI() / 96.f);  // NOTE: abusing font dpi
                const Color shadowColor{this->textDarkColor ? this->textDarkColor : Colors::invert(this->textColor)};
                g->drawString(this->font, this->getText(),
                              TextFX{.col_text = actualTextColor, .col_shadow = shadowColor, .offs_px = shadowOffset});
            } else {
                g->setColor(actualTextColor);
                g->drawString(this->font, this->getText());
            }
        }
        g->popTransform();
    }
    g->popClipRect();
}

void CBaseUIButton::drawHoverRect(int distance, bool isClickHeld) {
    if(isClickHeld) {
        distance *= 2;
    }
    const ivec2 pos{this->getPos()};
    const ivec2 size{this->getSize()};
    g->drawLine(pos.x, pos.y - distance, pos.x + size.x + 1, pos.y - distance);
    g->drawLine(pos.x, pos.y + size.y + distance, pos.x + size.x + 1, pos.y + size.y + distance);
    g->drawLine(pos.x - distance, pos.y, pos.x - distance, pos.y + size.y + 1);
    g->drawLine(pos.x + size.x + distance, pos.y, pos.x + size.x + distance, pos.y + size.y + 1);
}

void CBaseUIButton::onMouseUpInside(bool left, bool right) { this->onClicked(left, right); }

void CBaseUIButton::onClicked(bool left, bool right) {
    if(this->clickCallback && (*this->clickCallback)) {
        (*this->clickCallback)(this, left, right);
    }
}

void CBaseUIButton::updateStringMetrics() {
    if(this->font == nullptr) return;

    this->fStringHeight = this->font->getHeight();
    this->fStringWidth = this->font->getStringWidth(this->getText());
}
