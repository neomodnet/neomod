#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "AnimationHandler.h"
#include "CBaseUIButton.h"

struct Skin;
class SkinImage;
class ModSelector;
class ConVar;

class UIModSelectorModButton final : public CBaseUIButton {
    NOCOPY_NOMOVE(UIModSelectorModButton);

   public:
    UIModSelectorModButton(ModSelector *osuModSelector, float xPos, float yPos, float xSize, float ySize,
                           std::string name);
    ~UIModSelectorModButton() override;

    using SkinImageGetter = SA::delegate<const SkinImage *(const Skin *)>;

    void draw() override;
    void updateInput(CBaseUIEventCtx &c) override;
    void onClicked(bool left = true, bool right = false) override;

    void resetState();

    void setState(int state);
    void setState(unsigned int state, bool initialState, ConVar *cvar, std::string modName,
                  std::string_view tooltipText, SkinImageGetter skinImageGetter);
    void setBaseScale(float xScale, float yScale);
    void setAvailable(bool available) { this->bAvailable = available; }

    [[nodiscard]] std::string_view getActiveModName() const;
    [[nodiscard]] inline int getState() const { return this->iState; }
    [[nodiscard]] inline bool isOn() const { return this->bOn; }
    void onFocusStolen() override;

   private:
    friend class ModSelector;  // just for setOn()
    void setOn(bool on, bool silent = false, bool write_cvars = true);

    [[nodiscard]] const SkinImage *getActiveSkinImage() const;
    ModSelector *osuModSelector;

    bool bOn;
    bool bAvailable;
    int iState;
    float fEnabledScaleMultiplier;
    float fEnabledRotationDeg;
    vec2 vBaseScale{0.f};

    struct STATE {
        ConVar *cvar;
        std::string modName;
        std::vector<std::string> tooltipTextLines;
        SkinImageGetter skinImageGetFunc{nullptr};
    };
    std::vector<STATE> states;

    AnimFloat fScaleX{0.f}, fScaleY{0.f};
    AnimFloat fRot;
    SkinImageGetter currentSkinImageGetFunc{nullptr};

    bool bFocusStolenDelay;
};
