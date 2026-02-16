// Copyright (c) 2016, PG, All rights reserved.
#include "TooltipOverlay.h"

#include "AnimationHandler.h"
#include "OsuConVars.h"
#include "Engine.h"
#include "Mouse.h"
#include "Osu.h"
#include "ResourceManager.h"
#include "Font.h"
#include "Graphics.h"

TooltipOverlay::TooltipOverlay() : UIScreen() {
    this->fAnim = 0.0f;
    this->bDelayFadeout = false;
}

TooltipOverlay::~TooltipOverlay() = default;

void TooltipOverlay::draw() {
    if(this->bDelayFadeout) {
        this->bDelayFadeout = false;
    } else if(this->fAnim > 0.f) {
        anim::moveLinear(&this->fAnim, 0.f, (this->fAnim) * cv::tooltip_anim_duration.getFloat(), true);
    }

    if(this->fAnim > 0.0f) {
        const float dpiScale = Osu::getUIScale();

        McFont* font = engine->getDefaultFont();

        const vec2 offset = vec2(10, 10) * dpiScale;
        const int margin = 5 * dpiScale;
        const int lineSpacing = 8 * dpiScale;
        const float borderTextAlpha = this->fAnim * this->fAnim * this->fAnim;
        const float backgroundAlpha = borderTextAlpha * 0.9f;  // make the background semi-transparent at all times

        int width = 0;
        for(const auto& line : this->lines) {
            float lineWidth = font->getStringWidth(line);
            if(lineWidth > width) width = lineWidth;
        }
        const int height =
            font->getHeight() * this->lines.size() + lineSpacing * (this->lines.size() - 1) + 3 * dpiScale;

        const bool isPlayingFPoSu = (cv::mod_fposu.getBool() && osu->isInPlayMode());

        vec2 cursorPos = isPlayingFPoSu ? osu->getVirtScreenRect().getCenter() : mouse->getPos();
        if(!isPlayingFPoSu) {
            // clamp to right edge
            if(cursorPos.x + width + offset.x + 2 * margin > osu->getVirtScreenWidth())
                cursorPos.x -= (cursorPos.x + width + offset.x + 2 * margin) - osu->getVirtScreenWidth() + 1;

            // clamp to bottom edge
            if(cursorPos.y + height + offset.y + 2 * margin > osu->getVirtScreenHeight())
                cursorPos.y -= (cursorPos.y + height + offset.y + 2 * margin) - osu->getVirtScreenHeight() + 1;

            // clamp to left edge
            if(cursorPos.x < 0) cursorPos.x += std::abs(cursorPos.x);

            // clamp to top edge
            if(cursorPos.y < 0) cursorPos.y += std::abs(cursorPos.y);
        }

        // draw background
        g->setColor(Color(0xff000000).setA(backgroundAlpha));

        g->fillRect((int)cursorPos.x + offset.x, (int)cursorPos.y + offset.y, width + 2 * margin, height + 2 * margin);

        // draw text
        g->setColor(Color(0xffffffff).setA(borderTextAlpha));

        g->pushTransform();
        g->translate((int)(cursorPos.x + offset.x + margin),
                     (int)(cursorPos.y + offset.y + margin + font->getHeight()));
        for(const auto& line : this->lines) {
            g->drawString(font, line);
            g->translate(0, (int)(font->getHeight() + lineSpacing));
        }
        g->popTransform();

        // draw border
        g->setColor(Color(0xffffffff).setA(borderTextAlpha));

        g->drawRect((int)cursorPos.x + offset.x, (int)cursorPos.y + offset.y, width + 2 * margin, height + 2 * margin);
    }
}

void TooltipOverlay::begin() {
    this->lines.clear();
    this->bDelayFadeout = true;
}

void TooltipOverlay::addLine(UString text) { this->lines.push_back(std::move(text)); }

void TooltipOverlay::end() {
    anim::moveLinear(&this->fAnim, 1.0f, (1.0f - this->fAnim) * cv::tooltip_anim_duration.getFloat(), true);
}
