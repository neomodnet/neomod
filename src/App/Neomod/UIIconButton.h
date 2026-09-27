// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "AnimationHandler.h"
#include "CBaseUIButton.h"

#include <string>

// a button drawn as one glyph of the icon font (see Icons.h), centered and sized relative to the button's height;
// it lights up while hovered and dips while held
class UIIconButton : public CBaseUIButton {
    NOCOPY_NOMOVE(UIIconButton)
   public:
    UIIconButton(char32_t icon, std::string name = {});
    ~UIIconButton() override = default;

    void draw() override;
    void updateInput(CBaseUIEventCtx &c) override;

    // how tall the glyph is drawn, as a fraction of the button's height
    UIIconButton *setIconHeight(f32 fraction) {
        this->iconHeight = fraction;
        return this;
    }
    UIIconButton *setTooltipText(std::string text) {
        this->tooltipText = std::move(text);
        return this;
    }

   protected:
    void onMouseInside() override;
    void onMouseOutside() override;

    char32_t icon;
    f32 iconHeight{0.6f};
    AnimFloat iconRotation{0.f};  // degrees, around the glyph's center
    std::string tooltipText;
    AnimFloat hoverAnim{0.f};
};

// plays and pauses the selected beatmap's music, showing what a click would do
class PauseButton final : public UIIconButton {
    NOCOPY_NOMOVE(PauseButton)
   public:
    PauseButton(std::string name = {});
    ~PauseButton() override = default;

    void tick() override;
};
