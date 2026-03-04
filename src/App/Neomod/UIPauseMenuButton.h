#pragma once
// Copyright (c) 2018, PG, All rights reserved.
#include "AnimationHandler.h"
#include "CBaseUIButton.h"
#include "Skin.h"

class UIPauseMenuButton final : public CBaseUIButton {
    NOCOPY_NOMOVE(UIPauseMenuButton);
   public:
    using ImageSkinMember = BasicSkinImage Skin::*;

    UIPauseMenuButton(ImageSkinMember imageMember, float xPos, float yPos, float xSize, float ySize, UString name);
    ~UIPauseMenuButton() override;

    void draw() override;

    void onMouseInside() override;
    void onMouseOutside() override;
    void onDisabled() override;

    void setBaseScale(float xScale, float yScale);
    void setAlpha(float alpha) { this->fAlpha = alpha; }
    void setBrightness(float brightness) { this->fBrightness = brightness; }

    [[nodiscard]] Image* getImage() const;

   private:
    AnimFloat fScaleX{0.f}, fScaleY{0.f};
    vec2 vBaseScale{0.f};
    float fScaleMultiplier;

    float fAlpha;
    float fBrightness{1.0f};

    ImageSkinMember imageMember{nullptr};
};
