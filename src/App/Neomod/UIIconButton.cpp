// Copyright (c) 2026, WH, All rights reserved.
#include "UIIconButton.h"

#include "Font.h"
#include "Graphics.h"
#include "i18n.h"
#include "Icons.h"
#include "MusicTrack.h"
#include "Osu.h"
#include "TooltipOverlay.h"
#include "UI.h"
#include "UniString.h"

#include <cmath>

UIIconButton::UIIconButton(char32_t icon, std::string name)
    : CBaseUIButton(0, 0, 0, 0, std::move(name), ""), icon(icon) {
    this->setFont(osu->getFontIcons());

    this->setDrawBackground(false);
    this->setDrawFrame(false);
}

void UIIconButton::draw() {
    if(!this->isVisible() || !this->isVisibleOnScreen()) return;

    const f32 glyphTop = this->font->getGlyphHeight(this->icon);
    if(glyphTop <= 0.f) return;

    const f32 hover = this->hoverAnim;
    f32 height = this->getSize().y * this->iconHeight * (1.f + 0.1f * hover);
    if(this->bActive) height *= 0.9f;
    const f32 scale = height / glyphTop;
    const f32 alpha = this->bEnabled ? 0.75f + 0.25f * hover : 0.3f;

    // the icons barely reach below the baseline, so the middle of the part above it is the glyph's center, which goes
    // to the origin to be turned and then onto the button's center
    const std::string glyph = UniString::to_utf8(std::u32string_view{&this->icon, 1});
    const vec2 center = this->getRect().getCenter();
    g->pushTransform();
    {
        g->scale(scale, scale);
        g->translate(-std::round(this->font->getStringWidth(glyph) * scale / 2.f), std::round(height / 2.f));
        g->rotate(this->iconRotation);
        g->translate(std::round(center.x), std::round(center.y));
        g->drawString(this->font, glyph,
                      TextFX{.col_text = argb(alpha, 1.f, 1.f, 1.f),
                             .col_shadow = argb(alpha * 0.6f, 0.f, 0.f, 0.f),
                             .offs_px = std::round((f32)this->font->getDPI() / 96.0f),
                             .shadow_softness_px = 1.f});
    }
    g->popTransform();
}

void UIIconButton::updateInput(CBaseUIEventCtx &c) {
    CBaseUIButton::updateInput(c);
    if(this->tooltipText.empty() || !this->isMouseInside()) return;

    auto *tooltips = ui->getTooltipOverlay();
    tooltips->begin();
    tooltips->addLine(this->tooltipText);
    tooltips->end();
}

void UIIconButton::onMouseInside() {
    CBaseUIButton::onMouseInside();
    this->hoverAnim.set(1.f, 0.1f, anim::QuadOut);
}

void UIIconButton::onMouseOutside() {
    CBaseUIButton::onMouseOutside();
    this->hoverAnim.set(0.f, 0.15f, anim::QuadOut);
}

PauseButton::PauseButton(std::string name) : UIIconButton(Icons::PLAY, std::move(name)) {
    this->tooltipText = _("Play");
    this->setClickCallback([] { osu->getMusicTrack()->togglePause(); });
}

void PauseButton::tick() {
    UIIconButton::tick();

    const bool playing = osu->getMusicTrack()->isPlaying();
    if(playing == (this->icon == Icons::PAUSE)) return;
    this->icon = playing ? Icons::PAUSE : Icons::PLAY;
    this->tooltipText = playing ? _("Pause") : _("Play");
}
