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
#include "SString.h"
#include "Font.h"

void UIButton::draw() {
    if(!this->bVisible || !this->bVisible2) return;
    const auto *skin = osu->getSkin();

    const Image *buttonLeft = this->bDefaultSkin ? skin->i_button_left_default : skin->i_button_left;
    const Image *buttonMiddle = this->bDefaultSkin ? skin->i_button_mid_default : skin->i_button_mid;
    const Image *buttonRight = this->bDefaultSkin ? skin->i_button_right_default : skin->i_button_right;

    f32 leftScale = Osu::getImageScaleToFitResolution(buttonLeft, this->getSize());
    f32 leftWidth = buttonLeft->getWidth() * leftScale;

    f32 rightScale = Osu::getImageScaleToFitResolution(buttonRight, this->getSize());
    f32 rightWidth = buttonRight->getWidth() * rightScale;

    f32 middleWidth = this->getSize().x - leftWidth - rightWidth;

    {
        const bool inactive = this->is_loading || !this->bEnabled;
        auto color = inactive ? rgba(0x33, 0x33, 0x33, 0xff) : this->color;

        f32 brightness = inactive ? 1.f : 1.f + (this->fHoverAlpha * 0.2f);
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
        const f32 scale = (this->getSize().y * 0.8) / skin->i_loading_spinner.getSize().y;
        g->setColor(0xffffffff);
        g->pushTransform();
        g->rotate((f32)std::fmod(engine->getTime(), 2.) * 180.f, 0, 0, 1);
        g->scale(scale, scale);
        g->translate(this->getPos().x + this->getSize().x / 2.0f, this->getPos().y + this->getSize().y / 2.0f);
        g->drawImage(skin->i_loading_spinner);
        g->popTransform();
    } else {
        this->drawText();
    }
}

void UIButton::updateInput(CBaseUIEventCtx &c) {
    if(!this->bVisible || !this->bVisible2 || !this->bEnabled) return;
    CBaseUIButton::updateInput(c);

    if(this->isMouseInside() && this->tooltipTextLines.size() > 0 && !this->bFocusStolenDelay) {
        auto *ttoverlay = ui->getTooltipOverlay();
        ttoverlay->begin();
        for(const auto &tooltipTextLine : this->tooltipTextLines) {
            ttoverlay->addLine(tooltipTextLine);
        }
        ttoverlay->end();
    }

    this->bFocusStolenDelay = false;
}

static f32 button_sound_cooldown = 0.f;
void UIButton::onMouseInside() {
    CBaseUIButton::onMouseInside();
    if(this->bFocusStolenDelay) return;

    // There's actually no animation, it just goes to 1 instantly
    this->fHoverAlpha = 1.f;

    if(button_sound_cooldown + 0.05f < engine->getTime()) {
        button_sound_cooldown = engine->getTime();
        soundEngine->play(osu->getSkin()->s_hover_button);
    }
}

void UIButton::onMouseOutside() { this->fHoverAlpha = 0.f; }

void UIButton::onClicked(MouseButtonFlags buttons) {
    CBaseUIButton::onClicked(buttons);

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
    this->fClickAnim.set(0.0f, 0.5f, anim::Linear);
}

UIButton *UIButton::setTooltipText(std::string_view text) {
    this->tooltipTextLines = SString::split_newlines<std::string>(text);
    return this;
}

UIButtonVertical *UIButtonVertical::setSizeToContent(int horizontalBorderSize, int verticalBorderSize) {
    this->setSize(this->fStringHeight + 2 * verticalBorderSize, this->fStringWidth + 2 * horizontalBorderSize);
    return this;
}

void UIButtonVertical::drawText() {
    if(this->font == nullptr || !this->isVisible() || !this->isVisibleOnScreen() || this->getText().length() < 1)
        return;

    g->pushTransform();
    {
        const f32 scale = 1.f;
        f32 xPosAdd = this->getSize().x / 2.f + (this->fStringHeight / 2.f * scale);
        g->rotate(-90);
        g->scale(scale, scale);
        g->translate((i32)(this->getPos().x + xPosAdd),
                     (i32)(this->getPos().y + (this->fStringWidth / 2.f) * scale + this->getSize().y / 2.f));

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
