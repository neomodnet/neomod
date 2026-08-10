#pragma once
// Copyright (c) 2015, PG, All rights reserved.
#include "AnimationHandler.h"
#include "noinclude.h"
#include "ModFlags.h"
#include "UIScreen.h"

#include <array>
#include <memory>

class SongBrowser;

class CBaseUIElement;
class CBaseUIContainer;
class CBaseUIScrollView;
class CBaseUIButton;
class CBaseUILabel;
class CBaseUISlider;
class CBaseUICheckbox;

class UIModSelectorModButton;
class UIButton;
class UICheckbox;

class ConVar;

class ModSelector final : public UIScreen {
    NOCOPY_NOMOVE(ModSelector)
   public:
    ModSelector();
    ~ModSelector() override;

    void draw() override;
    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;

    void onKeyDown(KeyboardEvent &key) override;
    void onResolutionChange(vec2 newResolution) override;

    CBaseUIContainer *setVisible(bool visible) override;

    void enableAuto();
    void toggleAuto();
    void resetModsUserInitiated();
    void resetMods();
    void useCurrentMods();
    [[nodiscard]] LegacyFlags getModFlags() const;

    [[nodiscard]] bool isInCompactMode() const;
    [[nodiscard]] bool isCSOverrideSliderActive() const;
    bool isMouseInside() override;

    void updateButtons(bool initial = false);
    void updateExperimentalButtons();
    void updateOverrideSliderLabels();
    void updateScoreMultiplierLabelText();
    void updateLayout();
    void updateExperimentalLayout();

    void close(bool force);

    [[nodiscard]] std::span<CBaseUIElement *const> getAllChildren() const override;

   private:
    enum class OvrSliderType : u8 { CS, AR, OD, HP, SPEED };

    struct OVERRIDE_SLIDER {
        CBaseUICheckbox *lock;
        CBaseUIButton *desc;
        CBaseUISlider *slider;
        CBaseUILabel *label;
        ConVar *cvar;
        ConVar *lockCvar;
        OvrSliderType type;
    };

    struct EXPERIMENTAL_MOD {
        CBaseUIElement *element{nullptr};
        ConVar *cvar{nullptr};
    };

    const OVERRIDE_SLIDER addOverrideSlider(OvrSliderType typeEnum, const std::string &text,
                                            const std::string &labelText, ConVar *cvar, float min, float max,
                                            const std::string &tooltipText = {}, ConVar *lockCvar = nullptr);
    void onOverrideSliderChange(CBaseUISlider *slider);
    void onOverrideSliderLockChange(CBaseUICheckbox *checkbox);
    std::string getOverrideSliderLabelText(const OVERRIDE_SLIDER &s, bool active) const;

    CBaseUILabel *addExperimentalLabel(const std::string &text);
    UICheckbox *addExperimentalCheckbox(const std::string &text, const std::string &tooltipText,
                                        ConVar *cvar = nullptr);
    void onCheckboxChange(CBaseUICheckbox *checkbox);

    UIButton *addActionButton(const std::string &text);

   private:
    AnimFloat fAnimation;
    AnimFloat fExperimentalAnimation;
    bool bScheduledHide{false};
    bool bExperimentalVisible{false};
    std::unique_ptr<CBaseUIContainer> overrideSliderContainer;
    std::unique_ptr<CBaseUIScrollView> experimentalContainer;
    // vElements + the two manual containers above; rebuilt on each getAllChildren() call (debug-only path)
    mutable std::vector<CBaseUIElement *> allChildren;

    bool bWaitForCSChangeFinished{false};
    bool bWaitForSpeedChangeFinished{false};
    bool bWaitForHPChangeFinished{false};

    // override sliders
    std::vector<OVERRIDE_SLIDER> overrideSliders;
    bool bShowOverrideSliderALTHint{true};

    // mod grid buttons
    static constexpr int GRID_WIDTH{6};
    static constexpr int GRID_HEIGHT{3};

    std::array<UIModSelectorModButton *, GRID_WIDTH * GRID_HEIGHT> modButtons{};

#define MKMODBTN(name, x, y)                          \
    UIModSelectorModButton *modButton##name{nullptr}; \
                                                      \
   public:                                            \
    static inline ivec2 name##_POS{x, y};             \
                                                      \
   private:
    // clang-format off
    MKMODBTN(EZ,0,0); MKMODBTN(NF,  1,0);MKMODBTN(HT,2,0);
    MKMODBTN(HR,0,1); MKMODBTN(SDPF,1,1);MKMODBTN(DT,2,1);MKMODBTN(HD,  3,1);MKMODBTN(FL, 4,1);
    MKMODBTN(RX,0,2); MKMODBTN(AP,  1,2);MKMODBTN(SO,2,2);MKMODBTN(AUTO,3,2);MKMODBTN(TGT,4,2);MKMODBTN(SV2,5,2);
    // clang-format on
#undef MKMODBTN

    // experimental mods
    std::vector<EXPERIMENTAL_MOD> experimentalMods;

    // score multiplier info label
    CBaseUILabel *scoreMultiplierLabel;

    // action buttons
    std::vector<UIButton *> actionButtons;
    UIButton *resetModsButton;
    UIButton *closeButton;

    // should not be public
   public:
    [[nodiscard]] inline UIModSelectorModButton *getGridButton(ivec2 pos) const {
        if(likely((pos.x >= 0 && pos.y >= 0) && pos.x <= GRID_WIDTH && pos.y <= GRID_HEIGHT)) {
            const int index = pos.x * GRID_HEIGHT + pos.y;
            return this->modButtons[index];
        } else {
            return nullptr;
        }
    }
    CBaseUILabel *nonSubmittableWarning;

    CBaseUISlider *CSSlider;
    CBaseUISlider *ARSlider;
    CBaseUISlider *ODSlider;
    CBaseUISlider *HPSlider;
    CBaseUISlider *speedSlider{nullptr};
    CBaseUICheckbox *ARLock;
    CBaseUICheckbox *ODLock;
};
