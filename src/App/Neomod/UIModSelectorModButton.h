#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "CBaseUIButton.h"
#include "Skin.h"

class SkinImage;
class ModSelector;
class ConVar;

class UIModSelectorModButton final : public CBaseUIButton {
   public:
    UIModSelectorModButton(ModSelector *osuModSelector, float xPos, float yPos, float xSize, float ySize, UString name);
    using SkinImageSkinMember = SkinImage *Skin::*;

    void draw() override;
    void update(CBaseUIEventCtx &c) override;
    void onClicked(bool left = true, bool right = false) override;

    void resetState();

    void setState(int state);
    void setState(unsigned int state, bool initialState, ConVar *cvar, UString modName, const UString &tooltipText,
                  SkinImageSkinMember skinMember);
    void setBaseScale(float xScale, float yScale);
    void setAvailable(bool available) { this->bAvailable = available; }

    [[nodiscard]] const UString &getActiveModName() const;
    [[nodiscard]] inline int getState() const { return this->iState; }
    [[nodiscard]] inline bool isOn() const { return this->bOn; }
    void onFocusStolen() override;

    // this was not supposed to be a public function
    void setOn(bool on, bool silent = false);

   private:
    [[nodiscard]] SkinImage *getActiveSkinImage() const;
    ModSelector *osuModSelector;

    bool bOn;
    bool bAvailable;
    int iState;
    float fEnabledScaleMultiplier;
    float fEnabledRotationDeg;
    vec2 vBaseScale{0.f};

    struct STATE {
        ConVar *cvar;
        UString modName;
        std::vector<UString> tooltipTextLines;
        SkinImageSkinMember skinImageMember{nullptr};
    };
    std::vector<STATE> states;

    vec2 vScale{0.f};
    float fRot;
    SkinImageSkinMember activeSkinImageMember{nullptr};

    bool bFocusStolenDelay;
};
