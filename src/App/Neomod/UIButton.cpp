// Copyright (c) 2016, PG, All rights reserved.
#include "UIButton.h"

#include <utility>

#include "AnimationHandler.h"
#include "Engine.h"
#include "Osu.h"
#include "ResourceManager.h"
#include "Skin.h"
#include "SoundEngine.h"
#include "Graphics.h"
#include "TooltipOverlay.h"
#include "UI.h"
#include "Font.h"

void UIButton::draw() {
    if(!this->bVisible || !this->bVisible2) return;

    Image *buttonLeft = this->bDefaultSkin ? osu->getSkin()->i_button_left_default : osu->getSkin()->i_button_left;
    Image *buttonMiddle = this->bDefaultSkin ? osu->getSkin()->i_button_mid_default : osu->getSkin()->i_button_mid;
    Image *buttonRight = this->bDefaultSkin ? osu->getSkin()->i_button_right_default : osu->getSkin()->i_button_right;

    float leftScale = Osu::getImageScaleToFitResolution(buttonLeft, this->getSize());
    float leftWidth = buttonLeft->getWidth() * leftScale;

    float rightScale = Osu::getImageScaleToFitResolution(buttonRight, this->getSize());
    float rightWidth = buttonRight->getWidth() * rightScale;

    float middleWidth = this->getSize().x - leftWidth - rightWidth;

    {
        auto color = this->is_loading ? rgba(0x33, 0x33, 0x33, 0xff) : this->color;

        float brightness = 1.f + (this->fHoverAnim * 0.2f);
        color = Colors::scale(color, brightness);

        g->setColor(color);
    }

    buttonLeft->bind();
    {
        g->drawQuad((int)this->getPos().x, (int)this->getPos().y, (int)leftWidth, (int)this->getSize().y);
    }
    buttonLeft->unbind();

    buttonMiddle->bind();
    {
        g->drawQuad((int)this->getPos().x + (int)leftWidth, (int)this->getPos().y, (int)middleWidth,
                    (int)this->getSize().y);
    }
    buttonMiddle->unbind();

    buttonRight->bind();
    {
        g->drawQuad((int)this->getPos().x + (int)leftWidth + (int)middleWidth, (int)this->getPos().y, (int)rightWidth,
                    (int)this->getSize().y);
    }
    buttonRight->unbind();

    if(this->is_loading) {
        const float scale = (this->getSize().y * 0.8) / osu->getSkin()->i_loading_spinner->getSize().y;
        g->setColor(0xffffffff);
        g->pushTransform();
        g->rotate(engine->getTime() * 180, 0, 0, 1);
        g->scale(scale, scale);
        g->translate(this->getPos().x + this->getSize().x / 2.0f, this->getPos().y + this->getSize().y / 2.0f);
        g->drawImage(osu->getSkin()->i_loading_spinner);
        g->popTransform();
    } else {
        this->drawText();
    }
}

void UIButton::update(CBaseUIEventCtx &c) {
    if(!this->bVisible || !this->bVisible2) return;
    CBaseUIButton::update(c);

    if(this->isMouseInside() && this->tooltipTextLines.size() > 0 && !this->bFocusStolenDelay) {
        ui->getTooltipOverlay()->begin();
        {
            for(const auto &tooltipTextLine : this->tooltipTextLines) {
                ui->getTooltipOverlay()->addLine(tooltipTextLine);
            }
        }
        ui->getTooltipOverlay()->end();
    }

    this->bFocusStolenDelay = false;
}

static float button_sound_cooldown = 0.f;
void UIButton::onMouseInside() {
    CBaseUIButton::onMouseInside();
    if(this->bFocusStolenDelay) return;

    // There's actually no animation, it just goes to 1 instantly
    this->fHoverAnim = 1.f;

    if(button_sound_cooldown + 0.05f < engine->getTime()) {
        button_sound_cooldown = engine->getTime();
        soundEngine->play(osu->getSkin()->s_hover_button);
    }
}

void UIButton::onMouseOutside() { this->fHoverAnim = 0.f; }

void UIButton::onClicked(bool left, bool right) {
    CBaseUIButton::onClicked(left, right);

    if(this->is_loading) return;

    this->animateClickColor();

    soundEngine->play(osu->getSkin()->s_click_button);
}

void UIButton::onFocusStolen() {
    CBaseUIButton::onFocusStolen();

    this->bMouseInside = false;
    this->bFocusStolenDelay = true;
}

void UIButton::animateClickColor() {
    this->fClickAnim = 1.0f;
    anim::moveLinear(&this->fClickAnim, 0.0f, 0.5f, true);
}

UIButton *UIButton::setTooltipText(const UString &text) {
    this->tooltipTextLines = text.split(US_("\n"));
    return this;
}

UIButtonVertical *UIButtonVertical::setSizeToContent(int horizontalBorderSize, int verticalBorderSize) {
    this->setSize(this->fStringHeight + 2 * horizontalBorderSize, this->fStringWidth + 2 * verticalBorderSize);
    return this;
}

void UIButtonVertical::drawText() {
    if(this->font == nullptr || !this->isVisible() || !this->isVisibleOnScreen() || this->getText().length() < 1)
        return;

    const int shadowOffset = std::round(1.f * ((float)this->font->getDPI() / 96.f));  // NOTE: abusing font dpi

    g->setColor(this->textColor);
    g->pushTransform();
    {
        const f32 scale = 1.f;
        f32 xPosAdd = this->getSize().x / 2.f + (this->fStringHeight / 2.f * scale);
        g->rotate(-90);
        g->scale(scale, scale);
        g->translate((i32)(this->getPos().x + xPosAdd),
                     (i32)(this->getPos().y + (this->fStringWidth / 2.f) * scale + this->getSize().y / 2.f));

        // shadow
        if(this->bDrawShadow) {
            g->translate(shadowOffset, shadowOffset);
            g->setColor(this->textDarkColor ? this->textDarkColor : Colors::invert(this->textColor));
            g->drawString(this->font, this->getText());
            g->translate(-shadowOffset, -shadowOffset);
        }

        // top
        g->setColor(this->textBrightColor ? this->textBrightColor : this->textColor);
        g->drawString(this->font, this->getText());
    }
    g->popTransform();
}
