#pragma once
// Copyright (c) 2018, PG, All rights reserved.
#include "CBaseUIButton.h"
#include "Skin.h"

class UIPauseMenuButton final : public CBaseUIButton {
   public:
    using ImageSkinMember = BasicSkinImage Skin::*;

    UIPauseMenuButton(ImageSkinMember imageMember, float xPos, float yPos, float xSize, float ySize, UString name);

    void draw() override;

    void onMouseInside() override;
    void onMouseOutside() override;

    void setBaseScale(float xScale, float yScale);
    void setAlpha(float alpha) { this->fAlpha = alpha; }

    [[nodiscard]] Image* getImage() const;

   private:
    vec2 vScale{0.f};
    vec2 vBaseScale{0.f};
    float fScaleMultiplier;

    float fAlpha;

    ImageSkinMember imageMember{nullptr};
};
