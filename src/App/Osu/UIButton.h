#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "CBaseUIButton.h"

class UIButton : public CBaseUIButton {
   public:
    UIButton(float xPos, float yPos, float xSize, float ySize, UString name, UString text)
        : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name), std::move(text)) {}

    void draw() override;
    void update(CBaseUIEventCtx &c) override;

    UIButton *setColor(Color color) {
        this->color = color;
        return this;
    }
    UIButton *setUseDefaultSkin() {
        this->bDefaultSkin = true;
        return this;
    }

    UIButton *setTooltipText(const UString &text);

    void onMouseInside() override;
    void onMouseOutside() override;

    void animateClickColor();
    bool is_loading = false;

    // HACKHACK: enough is enough
    bool bVisible2 = true;

   private:
    void onClicked(bool left = true, bool right = false) override;
    void onFocusStolen() override;

    bool bDefaultSkin{false};
    Color color{0xffffffff};
    float fClickAnim{0.f};
    float fHoverAnim{0.f};

    std::vector<UString> tooltipTextLines;
    bool bFocusStolenDelay{false};
};

// Hacky vertical button - only used for main menu's "Online Beatmaps" button
// I tried inheriting UIButton but it didn't like getting rotated :(
class UIButtonVertical : public CBaseUIButton {
   public:
    UIButtonVertical(float xPos, float yPos, float xSize, float ySize, UString name, UString text)
        : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name), std::move(text)) {}

    UIButtonVertical *setSizeToContent(int horizontalBorderSize = 1, int verticalBorderSize = 1) override;
    void drawText() override;
};
