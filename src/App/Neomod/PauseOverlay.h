#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "AnimationHandler.h"
#include "UIScreen.h"
#include "Skin.h"

class SongBrowser;
class CBaseUIContainer;
class UIPauseMenuButton;

class PauseOverlay final : public UIScreen {
    NOCOPY_NOMOVE(PauseOverlay)
   public:
    using ImageSkinMember = BasicSkinImage Skin::*;

    PauseOverlay();
    ~PauseOverlay() override;

    void draw() override;
    void update(CBaseUIEventCtx &c) override;

    void onKeyDown(KeyboardEvent &e) override;
    void onKeyUp(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;

    void onResolutionChange(vec2 newResolution) override;

    CBaseUIContainer *setVisible(bool visible) override;

    void setContinueEnabled(bool continueEnabled);

   private:
    void updateLayout();

    void onContinueClicked();
    void onRetryClicked();
    void onBackClicked();

    void onSelectionChange();
    [[nodiscard]] bool areButtonsActive() const;

    void scheduleVisibilityChange(bool visible);

    UIPauseMenuButton *addButton(ImageSkinMember getImageFunc, UString name);

    std::vector<UIPauseMenuButton *> buttons;
    UIPauseMenuButton *selectedButton{nullptr};
    double fButtonsActiveTime{0.0};
    AnimFloat fButtonBrightnessAnim{1.f};
    AnimFloat fDimAnim;

    float fWarningArrowsAnimStartTime{0.f};
    AnimFloat fWarningArrowsAnimAlpha;
    AnimFloat fWarningArrowsAnimX;
    AnimFloat fWarningArrowsAnimY;

    bool bScheduledVisibilityChange{false};
    bool bScheduledVisibility{false};

    bool bInitialWarningArrowFlyIn{true};

    bool bContinueEnabled{true};
    bool bClick1Down{false};
    bool bClick2Down{false};
};
