#pragma once
#include "UIScreen.h"

#include <memory>

class CBaseUIContainer;
class UIVolumeSlider;
struct Skin;

class VolumeOverlay final : public UIScreen {
   public:
    VolumeOverlay();

    void animate();
    void draw() override;
    void update(CBaseUIEventCtx& c) override;
    void onResolutionChange(vec2 newResolution) override;
    void onKeyDown(KeyboardEvent& key) override;
    void updateLayout();
    bool isBusy() override;
    bool isVisible() override;
    bool canChangeVolume();
    void gainFocus();
    void loseFocus();

    void volumeUp(int multiplier = 1) { this->onVolumeChange(multiplier); }
    void volumeDown(int multiplier = 1) { this->onVolumeChange(-multiplier); }
    void onVolumeChange(int multiplier);
    void onMasterVolumeChange(float newValue);
    void onEffectVolumeChange();
    void updateEffectVolume(Skin* skin);
    void onMusicVolumeChange();

    float fLastVolume;
    float fVolumeChangeTime;
    float fVolumeChangeFade;
    bool bVolumeInactiveToActiveScheduled = false;
    float fVolumeInactiveToActiveAnim = 0.f;

    std::unique_ptr<CBaseUIContainer> volumeSliderOverlayContainer{nullptr};
    UIVolumeSlider* volumeMaster = nullptr;
    UIVolumeSlider* volumeEffects = nullptr;
    UIVolumeSlider* volumeMusic = nullptr;
};
