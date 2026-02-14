// Copyright (c) 2015, PG, All rights reserved.
#include "ModSelector.h"

#include <memory>
#include <utility>

#include "AnimationHandler.h"
#include "Bancho.h"
#include "BanchoNetworking.h"
#include "BeatmapInterface.h"
#include "CBaseUICheckbox.h"
#include "CBaseUIContainer.h"
#include "CBaseUILabel.h"
#include "CBaseUIScrollView.h"
#include "CBaseUISlider.h"
#include "Logging.h"
#include "OsuConVars.h"
#include "ConVarHandler.h"
#include "MakeDelegateWrapper.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "Environment.h"
#include "Font.h"
#include "HUD.h"
#include "Icons.h"
#include "KeyBindings.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "OptionsOverlay.h"
#include "Osu.h"
#include "RichPresence.h"
#include "RoomScreen.h"
#include "Skin.h"
#include "SkinImage.h"
#include "SongBrowser/SongBrowser.h"
#include "SoundEngine.h"
#include "TooltipOverlay.h"
#include "UI.h"
#include "UIButton.h"
#include "UICheckbox.h"
#include "UIModSelectorModButton.h"
#include "UISlider.h"

class ModSelectorOverrideSliderDescButton final : public CBaseUIButton {
   public:
    ModSelectorOverrideSliderDescButton(float xPos, float yPos, float xSize, float ySize, UString name, UString text)
        : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name), std::move(text)) {}

    void update(CBaseUIEventCtx &c) override {
        if(!this->bVisible) return;
        CBaseUIButton::update(c);

        if(this->isMouseInside() && this->sTooltipText.length() > 0) {
            ui->getTooltipOverlay()->begin();
            {
                ui->getTooltipOverlay()->addLine(this->sTooltipText);
            }
            ui->getTooltipOverlay()->end();
        }
    }

    void setTooltipText(UString tooltipText) { this->sTooltipText = std::move(tooltipText); }

   private:
    void drawText() override {
        if(this->font != nullptr && this->getText().length() > 0) {
            float xPosAdd = this->getSize().x / 2.0f - this->fStringWidth / 2.0f;

            // g->pushClipRect(McRect(this->getPos(), this->getSize()));
            {
                g->setColor(this->textColor);
                g->pushTransform();
                {
                    g->translate((int)(this->getPos().x + (xPosAdd)),
                                 (int)(this->getPos().y + this->getSize().y / 2.0f + this->fStringHeight / 2.0f));
                    g->drawString(this->font, this->getText());
                }
                g->popTransform();
            }
            // g->popClipRect();
        }
    }

    UString sTooltipText;
};

class ModSelectorOverrideSliderLockButton final : public CBaseUICheckbox {
   public:
    ModSelectorOverrideSliderLockButton(float xPos, float yPos, float xSize, float ySize, UString name, UString text)
        : CBaseUICheckbox(xPos, yPos, xSize, ySize, std::move(name), std::move(text)) {
        this->fAnim = 1.0f;
    }

    void draw() override {
        if(!this->bVisible) return;

        const auto icon = (this->bChecked ? Icons::LOCK : Icons::UNLOCK);
        UString iconString;
        iconString.insert(0, icon);

        McFont *iconFont = osu->getFontIcons();
        const float scale = (this->getSize().y / iconFont->getHeight()) * this->fAnim;
        g->setColor(this->bChecked ? 0xffffffff : 0xff1166ff);

        g->pushTransform();
        {
            g->scale(scale, scale);
            g->translate(
                this->getPos().x + this->getSize().x / 2.0f - iconFont->getStringWidth(iconString) * scale / 2.0f,
                this->getPos().y + this->getSize().y / 2.0f + (iconFont->getHeight() * scale / 2.0f) * 0.8f);
            g->drawString(iconFont, iconString);
        }
        g->popTransform();
    }

   private:
    void onPressed() override {
        CBaseUICheckbox::onPressed();
        soundEngine->play(this->isChecked() ? osu->getSkin()->s_check_on : osu->getSkin()->s_check_off);

        if(this->isChecked()) {
            // anim::moveQuadOut(&m_fAnim, 1.5f, 0.060f, true);
            // anim::moveQuadIn(&m_fAnim, 1.0f, 0.250f, 0.150f, false);
        } else {
            anim::deleteExistingAnimation(&this->fAnim);
            this->fAnim = 1.0f;
        }
    }

    float fAnim;
};

ModSelector::ModSelector() : UIScreen() {
    this->fAnimation = 0.0f;
    this->fExperimentalAnimation = 0.0f;
    this->bScheduledHide = false;
    this->bExperimentalVisible = false;

    const vec2 osuScreen = osu->getVirtScreenSize();

    this->setSize(osuScreen.x, osuScreen.y);
    this->overrideSliderContainer = std::make_unique<CBaseUIContainer>(0.f, 0.f, osuScreen.x, osuScreen.y, "");
    this->experimentalContainer.reset((new CBaseUIScrollView(-1.f, 0.f, osuScreen.x, osuScreen.y, ""))
                                          ->setHorizontalScrolling(false)
                                          ->setVerticalScrolling(true)
                                          ->setDrawFrame(false)
                                          ->setDrawBackground(false));

    this->bWaitForCSChangeFinished = false;
    this->bWaitForSpeedChangeFinished = false;
    this->bWaitForHPChangeFinished = false;

    this->speedSlider = nullptr;
    this->bShowOverrideSliderALTHint = true;

    // build mod grid buttons
    this->iGridWidth = 6;
    this->iGridHeight = 3;

    for(int x = 0; x < this->iGridWidth; x++) {
        for(int y = 0; y < this->iGridHeight; y++) {
            auto *imageButton = new UIModSelectorModButton(this, 50, 50, 100, 100, "");
            imageButton->setDrawBackground(false);
            imageButton->setVisible(false);

            this->addBaseUIElement(imageButton);
            this->modButtons.push_back(imageButton);
        }
    }

    // build override sliders
    OVERRIDE_SLIDER overrideCS = this->addOverrideSlider("CS Override", "CS:", &cv::cs_override, 0.0f, 12.5f,
                                                         "Circle Size (higher number = smaller circles).");
    OVERRIDE_SLIDER overrideAR =
        this->addOverrideSlider("AR Override", "AR:", &cv::ar_override, 0.0f, 12.5f,
                                "Approach Rate (higher number = faster circles).", &cv::ar_override_lock);
    OVERRIDE_SLIDER overrideOD =
        this->addOverrideSlider("OD Override", "OD:", &cv::od_override, 0.0f, 12.5f,
                                "Overall Difficulty (higher number = harder accuracy).", &cv::od_override_lock);
    OVERRIDE_SLIDER overrideHP = this->addOverrideSlider("HP Override", "HP:", &cv::hp_override, 0.0f, 12.5f,
                                                         "Hit/Health Points (higher number = harder survival).");

    overrideCS.slider->setAnimated(false);  // quick fix for otherwise possible inconsistencies due to slider vertex
                                            // buffers and animated CS changes
    overrideCS.slider->setChangeCallback(SA::MakeDelegate<&ModSelector::onOverrideSliderChange>(this));
    overrideAR.slider->setChangeCallback(SA::MakeDelegate<&ModSelector::onOverrideSliderChange>(this));
    overrideOD.slider->setChangeCallback(SA::MakeDelegate<&ModSelector::onOverrideSliderChange>(this));
    overrideHP.slider->setChangeCallback(SA::MakeDelegate<&ModSelector::onOverrideSliderChange>(this));

    overrideAR.desc->setClickCallback(SA::MakeDelegate<&ModSelector::onOverrideARSliderDescClicked>(this));
    overrideOD.desc->setClickCallback(SA::MakeDelegate<&ModSelector::onOverrideODSliderDescClicked>(this));

    this->CSSlider = overrideCS.slider;
    this->ARSlider = overrideAR.slider;
    this->ODSlider = overrideOD.slider;
    this->HPSlider = overrideHP.slider;
    this->ARLock = overrideAR.lock;
    this->ODLock = overrideOD.lock;

    OVERRIDE_SLIDER overrideSpeed =
        this->addOverrideSlider("Speed/BPM Multiplier", "x", &cv::speed_override, 0.9f, 2.5f);

    overrideSpeed.slider->setChangeCallback(SA::MakeDelegate<&ModSelector::onOverrideSliderChange>(this));
    // overrideSpeed.slider->setValue(-1.0f, false);
    overrideSpeed.slider->setAnimated(false);  // same quick fix as above
    overrideSpeed.slider->setLiveUpdate(false);

    this->speedSlider = overrideSpeed.slider;

    // build experimental buttons
    this->addExperimentalLabel(" Experimental Mods (!)");
    this->addExperimentalCheckbox("Flip Up/Down", "Playfield is flipped upside down (mirrored at horizontal axis).",
                                  &cv::playfield_mirror_horizontal);
    this->addExperimentalCheckbox("Flip Left/Right", "Playfield is flipped left/right (mirrored at vertical axis).",
                                  &cv::playfield_mirror_vertical);
    this->addExperimentalCheckbox("Singletap", "You can only press one key.", &cv::mod_singletap);
    this->addExperimentalCheckbox("Alternate", "You can never use the same key twice in a row.",
                                  &cv::mod_fullalternate);
    this->addExperimentalCheckbox("No keylock", "You can use 4 keys instead of only 2.", &cv::mod_no_keylock);
    this->addExperimentalCheckbox("No pausing", "Pausing is cheating", &cv::mod_no_pausing);
    this->addExperimentalCheckbox(
        "FPoSu: Strafing", "Playfield moves in 3D space (see fposu_mod_strafing_...).\nOnly works in FPoSu mode!",
        &cv::fposu_mod_strafing);
    this->addExperimentalCheckbox("Wobble", "Playfield rotates and moves.", &cv::mod_wobble);
    this->addExperimentalCheckbox("AR Wobble", "Approach rate oscillates between -1 and +1.", &cv::mod_arwobble);
    this->addExperimentalCheckbox("Approach Different",
                                  "Customize the approach circle animation.\nSee mod_approach_different_style.\nSee "
                                  "mod_approach_different_initial_size.",
                                  &cv::mod_approach_different);
    this->addExperimentalCheckbox("Timewarp", "Speed increases from 100% to 150% over the course of the beatmap.",
                                  &cv::mod_timewarp);
    this->addExperimentalCheckbox(
        "AR Timewarp", "Approach rate decreases from 100% to 50% over the course of the beatmap.", &cv::mod_artimewarp);
    this->addExperimentalCheckbox("Minimize", "Circle size decreases from 100% to 50% over the course of the beatmap.",
                                  &cv::mod_minimize);
    this->addExperimentalCheckbox("Fading Cursor", "The cursor fades the higher the combo, becoming invisible at 50.",
                                  &cv::mod_fadingcursor);
    this->addExperimentalCheckbox("First Person", "Centered cursor.", &cv::mod_fps);
    this->addExperimentalCheckbox("Jigsaw 1", "Unnecessary clicks count as misses.", &cv::mod_jigsaw1);
    this->addExperimentalCheckbox("Jigsaw 2", "Massively reduced slider follow circle radius.", &cv::mod_jigsaw2);
    this->addExperimentalCheckbox("Reverse Sliders",
                                  "Reverses the direction of all sliders. (Reload beatmap to apply!)",
                                  &cv::mod_reverse_sliders);
    this->addExperimentalCheckbox("No 50s", "Only 300s or 100s. Try harder.", &cv::mod_no50s);
    this->addExperimentalCheckbox("No 100s no 50s", "300 or miss. PF \"lite\"", &cv::mod_no100s);
    this->addExperimentalCheckbox("MinG3012", "No 100s. Only 300s or 50s. Git gud.", &cv::mod_ming3012);
    this->addExperimentalCheckbox(
        "Half Timing Window", "The hit timing window is cut in half. Hit early or perfect (300).", &cv::mod_halfwindow);
    this->addExperimentalCheckbox("MillhioreF", "Go below AR 0. Doubled approach time.", &cv::mod_millhioref);
    this->addExperimentalCheckbox(
        "Mafham",
        "Approach rate is set to negative infinity. See the entire beatmap at once.\nUses very "
        "aggressive optimizations to keep the framerate high, you have been warned!",
        &cv::mod_mafham);
    this->addExperimentalCheckbox(
        "Strict Tracking", "Leaving sliders in any way counts as a miss and combo break. (Reload beatmap to apply!)",
        &cv::mod_strict_tracking);

    this->nonSubmittableWarning = new CBaseUILabel();
    this->nonSubmittableWarning->setDrawFrame(false);
    this->nonSubmittableWarning->setDrawBackground(false);
    this->nonSubmittableWarning->setText(
        "WARNING: Score submission will be disabled due to non-vanilla mod selection.");
    this->nonSubmittableWarning->setTextColor(0xffff0000);
    this->nonSubmittableWarning->setTextJustification(TEXT_JUSTIFICATION::CENTERED);
    this->nonSubmittableWarning->setVisible(false);
    this->addBaseUIElement(this->nonSubmittableWarning);

    // build score multiplier label
    this->scoreMultiplierLabel = new CBaseUILabel();
    this->scoreMultiplierLabel->setDrawFrame(false);
    this->scoreMultiplierLabel->setDrawBackground(false);
    this->scoreMultiplierLabel->setTextJustification(TEXT_JUSTIFICATION::CENTERED);
    this->addBaseUIElement(this->scoreMultiplierLabel);

    // build action buttons
    this->resetModsButton = this->addActionButton("1. Reset All Mods");
    this->resetModsButton->setClickCallback(SA::MakeDelegate<&ModSelector::resetModsUserInitiated>(this));
    this->resetModsButton->setColor(0xffc62b00);
    this->closeButton = this->addActionButton("2. Close");
    this->closeButton->setClickCallback(SA::MakeDelegate<&ModSelector::close>(this));
    this->closeButton->setColor(0xff636363);

    this->updateButtons(true);
    this->updateLayout();
}

void ModSelector::updateButtons(bool initial) {
    this->modButtonEasy = this->setModButtonOnGrid(
        0, 0, 0, initial && osu->getModEZ(), &cv::mod_easy, "ez",
        "Reduces overall difficulty - larger circles, more forgiving HP drain, less accuracy required.",
        &Skin::i_modselect_ez);
    this->modButtonNofail = this->setModButtonOnGrid(
        1, 0, 0, initial && osu->getModNF(), &cv::mod_nofail, "nf",
        "You can't fail. No matter what.\nNOTE: To disable drain completely:\nOptions > Gameplay > "
        "Mechanics > \"Disable HP Drain\".",
        &Skin::i_modselect_nf);
    this->setModButtonOnGrid(4, 0, 0, initial && osu->getModNightmare(), &cv::mod_nightmare, "nightmare",
                             "Unnecessary clicks count as misses.\nMassively reduced slider follow circle radius.",
                             &Skin::i_modselect_nightmare);

    this->modButtonHardrock = this->setModButtonOnGrid(0, 1, 0, initial && osu->getModHR(), &cv::mod_hardrock, "hr",
                                                       "Everything just got a bit harder...", &Skin::i_modselect_hr);
    this->modButtonSuddendeath = this->setModButtonOnGrid(1, 1, 0, initial && osu->getModSD(), &cv::mod_suddendeath,
                                                          "sd", "Miss a note and fail.", &Skin::i_modselect_sd);
    this->setModButtonOnGrid(1, 1, 1, initial && osu->getModSS(), &cv::mod_perfect, "ss", "SS or quit.",
                             &Skin::i_modselect_pf);

    if(cv::nightcore_enjoyer.getBool()) {
        this->modButtonHalftime =
            this->setModButtonOnGrid(2, 0, 0, initial && cv::mod_halftime_dummy.getBool(), &cv::mod_halftime_dummy,
                                     "dc", "A E S T H E T I C", &Skin::i_modselect_dc);
        this->modButtonDoubletime =
            this->setModButtonOnGrid(2, 1, 0, initial && cv::mod_doubletime_dummy.getBool(), &cv::mod_doubletime_dummy,
                                     "nc", "uguuuuuuuu", &Skin::i_modselect_nc);
    } else {
        this->modButtonHalftime =
            this->setModButtonOnGrid(2, 0, 0, initial && cv::mod_halftime_dummy.getBool(), &cv::mod_halftime_dummy,
                                     "ht", "Less zoom.", &Skin::i_modselect_ht);
        this->modButtonDoubletime =
            this->setModButtonOnGrid(2, 1, 0, initial && cv::mod_doubletime_dummy.getBool(), &cv::mod_doubletime_dummy,
                                     "dt", "Zoooooooooom.", &Skin::i_modselect_dt);
    }

    this->modButtonHidden = this->setModButtonOnGrid(
        3, 1, 0, initial && osu->getModHD(), &cv::mod_hidden, "hd",
        "Play with no approach circles and fading notes for a slight score advantage.", &Skin::i_modselect_hd);

    this->modButtonFlashlight =
        this->setModButtonOnGrid(4, 1, 0, initial && osu->getModFlashlight(), &cv::mod_flashlight, "fl",
                                 "Restricted view area.", &Skin::i_modselect_fl);
    this->setModButtonOnGrid(4, 1, 1, initial && cv::mod_actual_flashlight.getBool(), &cv::mod_actual_flashlight, "afl",
                             "Actual flashlight.", &Skin::i_modselect_fl);

    this->modButtonTD = this->setModButtonOnGrid(5, 1, 0, initial && osu->getModTD(), &cv::mod_touchdevice, "nerftd",
                                                 "Simulate pp nerf for touch devices.\nOnly affects pp calculation.",
                                                 &Skin::i_modselect_td);
    this->getModButtonOnGrid(5, 1)->setAvailable(!cv::mod_touchdevice_always.getBool());

    this->modButtonRelax = this->setModButtonOnGrid(
        0, 2, 0, initial && osu->getModRelax(), &cv::mod_relax, "relax",
        "You don't need to click.\nGive your clicking/tapping fingers a break from the heat of things.\n** UNRANKED **",
        &Skin::i_modselect_rx);
    this->modButtonAutopilot = this->setModButtonOnGrid(
        1, 2, 0, initial && osu->getModAutopilot(), &cv::mod_autopilot, "autopilot",
        "Automatic cursor movement - just follow the rhythm.\n** UNRANKED **", &Skin::i_modselect_ap);
    this->modButtonSpunout =
        this->setModButtonOnGrid(2, 2, 0, initial && osu->getModSpunout(), &cv::mod_spunout, "spunout",
                                 "Spinners will be automatically completed.", &Skin::i_modselect_so);
    this->modButtonAuto =
        this->setModButtonOnGrid(3, 2, 0, initial && osu->getModAuto(), &cv::mod_autoplay, "auto",
                                 "Watch a perfect automated play through the song.", &Skin::i_modselect_auto);
    this->setModButtonOnGrid(
        4, 2, 0, initial && osu->getModTarget(), &cv::mod_target, "practicetarget",
        "Accuracy is based on the distance to the center of all hitobjects.\n300s still require at "
        "least being in the hit window of a 100 in addition to the rule above.",
        &Skin::i_modselect_target);
    this->modButtonScoreV2 =
        this->setModButtonOnGrid(5, 2, 0, initial && osu->getModScorev2(), &cv::mod_scorev2, "v2",
                                 "Try the future scoring system.\n** UNRANKED **", &Skin::i_modselect_sv2);

    // Enable all mods that we disable conditionally below
    this->getModButtonOnGrid(2, 0)->setAvailable(true);
    this->getModButtonOnGrid(2, 1)->setAvailable(true);
    this->getModButtonOnGrid(3, 2)->setAvailable(true);
    this->getModButtonOnGrid(4, 2)->setAvailable(true);
    this->getModButtonOnGrid(4, 0)->setAvailable(true);
    this->getModButtonOnGrid(5, 2)->setAvailable(true);

    if(BanchoState::is_in_a_multi_room()) {
        if(!BanchoState::room.is_host()) {
            this->getModButtonOnGrid(2, 0)->setAvailable(false);  // Disable DC/HT
            this->getModButtonOnGrid(2, 1)->setAvailable(false);  // Disable DT/NC
            this->getModButtonOnGrid(4, 2)->setAvailable(false);  // Disable Target
        }

        this->getModButtonOnGrid(5, 2)->setAvailable(false);  // Disable ScoreV2 (we use win condition instead)
        this->getModButtonOnGrid(4, 0)->setAvailable(false);  // Disable nightmare mod
        this->getModButtonOnGrid(3, 2)->setAvailable(false);  // Disable auto mod
    }
}

void ModSelector::updateScoreMultiplierLabelText() {
    const float scoreMultiplier = osu->getScoreMultiplier();

    const int alpha = 200;
    if(scoreMultiplier > 1.0f)
        this->scoreMultiplierLabel->setTextColor(argb(alpha, 173, 255, 47));
    else if(scoreMultiplier == 1.0f)
        this->scoreMultiplierLabel->setTextColor(argb(alpha, 255, 255, 255));
    else
        this->scoreMultiplierLabel->setTextColor(argb(alpha, 255, 69, 00));

    this->scoreMultiplierLabel->setText(fmt::format("Score Multiplier: {:.2f}X", scoreMultiplier));
}

void ModSelector::updateExperimentalButtons() {
    for(auto &experimentalMod : this->experimentalMods) {
        ConVar *cvar = experimentalMod.cvar;
        if(cvar != nullptr) {
            auto *checkboxPointer = dynamic_cast<CBaseUICheckbox *>(experimentalMod.element);
            if(checkboxPointer != nullptr) {
                if(cvar->getBool() != checkboxPointer->isChecked()) checkboxPointer->setChecked(cvar->getBool(), false);
            }
        }
    }
}

ModSelector::~ModSelector() = default;

void ModSelector::draw() {
    if(!this->bVisible && !this->bScheduledHide) return;

    // for compact mode (and experimental mods)
    const int margin = 10;
    const Color backgroundColor = 0x88000000;

    const float experimentalModsAnimationTranslation =
        -(this->experimentalContainer->getSize().x + 2.0f) * (1.0f - this->fExperimentalAnimation);

    if(BanchoState::is_in_a_multi_room()) {
        // get mod button element bounds
        vec2 modGridButtonsStart = vec2(osu->getVirtScreenWidth(), osu->getVirtScreenHeight());
        vec2 modGridButtonsSize = vec2(0, osu->getVirtScreenHeight());
        for(auto button : this->modButtons) {
            if(button->getPos().x < modGridButtonsStart.x) modGridButtonsStart.x = button->getPos().x;
            if(button->getPos().y < modGridButtonsStart.y) modGridButtonsStart.y = button->getPos().y;

            if(button->getPos().x + button->getSize().x > modGridButtonsSize.x)
                modGridButtonsSize.x = button->getPos().x + button->getSize().x;
            if(button->getPos().y < modGridButtonsSize.y) modGridButtonsSize.y = button->getPos().y;
        }
        modGridButtonsSize.x -= modGridButtonsStart.x;

        // draw mod grid buttons
        g->pushTransform();
        {
            g->translate(0, (1.0f - this->fAnimation) * modGridButtonsSize.y);
            g->setColor(backgroundColor);
            g->fillRect(modGridButtonsStart.x - margin, modGridButtonsStart.y - margin,
                        modGridButtonsSize.x + 2 * margin, modGridButtonsSize.y + 2 * margin);
            UIScreen::draw();
        }
        g->popTransform();
    } else if(this->isInCompactMode()) {
        // if we are in compact mode, draw some backgrounds under the override sliders & mod grid buttons

        // get override slider element bounds
        vec2 overrideSlidersStart = vec2(osu->getVirtScreenWidth(), 0);
        vec2 overrideSlidersSize{0.f};
        for(auto &overrideSlider : this->overrideSliders) {
            CBaseUIButton *desc = overrideSlider.desc;
            CBaseUILabel *label = overrideSlider.label;

            if(desc->getPos().x < overrideSlidersStart.x) overrideSlidersStart.x = desc->getPos().x;

            if((label->getPos().x + label->getSize().x) > overrideSlidersSize.x)
                overrideSlidersSize.x = (label->getPos().x + label->getSize().x);
            if(desc->getPos().y + desc->getSize().y > overrideSlidersSize.y)
                overrideSlidersSize.y = desc->getPos().y + desc->getSize().y;
        }
        overrideSlidersSize.x -= overrideSlidersStart.x;

        // get mod button element bounds
        vec2 modGridButtonsStart = vec2(osu->getVirtScreenWidth(), osu->getVirtScreenHeight());
        vec2 modGridButtonsSize = vec2(0, osu->getVirtScreenHeight());
        for(auto button : this->modButtons) {
            if(button->getPos().x < modGridButtonsStart.x) modGridButtonsStart.x = button->getPos().x;
            if(button->getPos().y < modGridButtonsStart.y) modGridButtonsStart.y = button->getPos().y;

            if(button->getPos().x + button->getSize().x > modGridButtonsSize.x)
                modGridButtonsSize.x = button->getPos().x + button->getSize().x;
            if(button->getPos().y < modGridButtonsSize.y) modGridButtonsSize.y = button->getPos().y;
        }
        modGridButtonsSize.x -= modGridButtonsStart.x;

        // draw mod grid buttons
        g->pushTransform();
        {
            g->translate(0, (1.0f - this->fAnimation) * modGridButtonsSize.y);
            g->setColor(backgroundColor);
            g->fillRect(modGridButtonsStart.x - margin, modGridButtonsStart.y - margin,
                        modGridButtonsSize.x + 2 * margin, modGridButtonsSize.y + 2 * margin);
            UIScreen::draw();
        }
        g->popTransform();

        // draw override sliders
        g->pushTransform();
        {
            g->translate(0, -(1.0f - this->fAnimation) * overrideSlidersSize.y);
            g->setColor(backgroundColor);
            g->fillRect(overrideSlidersStart.x - margin, 0, overrideSlidersSize.x + 2 * margin,
                        overrideSlidersSize.y + margin);
            this->overrideSliderContainer->draw();
        }
        g->popTransform();
    } else  // normal mode, just draw everything
    {
        // draw hint text on left edge of screen
        {
            const float dpiScale = Osu::getUIScale();

            UString experimentalText = "Experimental Mods";
            McFont *experimentalFont = osu->getSubTitleFont();

            const float experimentalTextWidth = experimentalFont->getStringWidth(experimentalText);
            const float experimentalTextHeight = experimentalFont->getHeight();

            const float rectMargin = 5 * dpiScale;
            const float rectWidth = experimentalTextWidth + 2 * rectMargin;
            const float rectHeight = experimentalTextHeight + 2 * rectMargin;

            g->pushTransform();
            {
                g->rotate(90);
                g->translate(
                    (int)(experimentalTextHeight / 3.0f + std::max(0.0f, experimentalModsAnimationTranslation +
                                                                             this->experimentalContainer->getSize().x)),
                    (int)(osu->getVirtScreenHeight() / 2 - experimentalTextWidth / 2));
                g->setColor(Color(0xff777777).setA(1.0f - this->fExperimentalAnimation * this->fExperimentalAnimation));

                g->drawString(experimentalFont, experimentalText);
            }
            g->popTransform();

            g->pushTransform();
            {
                g->rotate(90);
                g->translate((int)(rectHeight + std::max(0.0f, experimentalModsAnimationTranslation +
                                                                   this->experimentalContainer->getSize().x)),
                             (int)(osu->getVirtScreenHeight() / 2 - rectWidth / 2));
                g->drawRect(0, 0, rectWidth, rectHeight);
            }
            g->popTransform();
        }

        UIScreen::draw();
        this->overrideSliderContainer->draw();
    }

    // draw experimental mods
    if(!BanchoState::is_in_a_multi_room()) {
        g->pushTransform();
        {
            g->translate(experimentalModsAnimationTranslation, 0);
            g->setColor(backgroundColor);
            g->fillRect(this->experimentalContainer->getPos().x - margin,
                        this->experimentalContainer->getPos().y - margin,
                        this->experimentalContainer->getSize().x + 2 * margin * this->fExperimentalAnimation,
                        this->experimentalContainer->getSize().y + 2 * margin);
            this->experimentalContainer->draw();
        }
        g->popTransform();
    }
}

void ModSelector::update(CBaseUIEventCtx &c) {
    // HACKHACK: updating while invisible is stupid, but the only quick solution for still animating otherwise stuck
    // sliders while closed
    if(!this->bVisible) {
        for(auto &overrideSlider : this->overrideSliders) {
            if(overrideSlider.slider->hasChanged()) overrideSlider.slider->update(c);
        }
        if(this->bScheduledHide) {
            if(this->fAnimation == 0.0f) {
                this->bScheduledHide = false;
            }
        }
        return;
    }

    // update experimental mods, they take focus precedence over everything else
    if(this->bExperimentalVisible) {
        this->experimentalContainer->update(c);
    }

    // update
    UIScreen::update(c);

    this->nonSubmittableWarning->setVisible(BanchoState::can_submit_scores() && !cvars().areAllCvarsSubmittable());
    if(this->nonSubmittableWarning->isVisible() && this->nonSubmittableWarning->isMouseInside()) {
        auto nonSubmittableCvars = cvars().getNonSubmittableCvars();
        if(!nonSubmittableCvars.empty()) {
            ui->getTooltipOverlay()->begin();
            for(const auto &cvar : nonSubmittableCvars) {
                ui->getTooltipOverlay()->addLine(cvar->getName());
            }
            ui->getTooltipOverlay()->end();
        }
    }

    if(!BanchoState::is_in_a_multi_room()) {
        this->overrideSliderContainer->update(c);

        // override slider tooltips (ALT)
        if(this->bShowOverrideSliderALTHint) {
            for(auto &overrideSlider : this->overrideSliders) {
                if(overrideSlider.slider->isBusy()) {
                    ui->getTooltipOverlay()->begin();
                    {
                        ui->getTooltipOverlay()->addLine("Hold [ALT] to slide in 0.01 increments.");
                    }
                    ui->getTooltipOverlay()->end();

                    if(keyboard->isAltDown()) this->bShowOverrideSliderALTHint = false;
                }
            }
        }

        // handle experimental mods visibility
        bool experimentalModEnabled = false;
        for(auto &experimentalMod : this->experimentalMods) {
            auto *checkboxPointer = dynamic_cast<CBaseUICheckbox *>(experimentalMod.element);
            if(checkboxPointer != nullptr && checkboxPointer->isChecked()) {
                experimentalModEnabled = true;
                break;
            }
        }

        McRect experimentalTrigger = McRect(
            0, 0,
            this->bExperimentalVisible ? this->experimentalContainer->getSize().x : osu->getVirtScreenWidth() * 0.05f,
            osu->getVirtScreenHeight());
        if(experimentalTrigger.contains(mouse->getPos())) {
            if(!this->bExperimentalVisible) {
                this->bExperimentalVisible = true;
                anim::moveQuadOut(&this->fExperimentalAnimation, 1.0f, (1.0f - this->fExperimentalAnimation) * 0.11f,
                                  0.0f, true);
            }
        } else if(this->bExperimentalVisible && !this->experimentalContainer->isMouseInside() &&
                  !this->experimentalContainer->isActive() && !experimentalModEnabled) {
            this->bExperimentalVisible = false;
            anim::moveQuadIn(&this->fExperimentalAnimation, 0.0f, this->fExperimentalAnimation * 0.11f, 0.0f, true);
        }
    }

    // delayed onModUpdate() triggers when changing some values
    {
        // handle dynamic CS and slider vertex buffer updates
        if(this->CSSlider != nullptr && (this->CSSlider->isActive() || this->CSSlider->hasChanged())) {
            this->bWaitForCSChangeFinished = true;
        } else if(this->bWaitForCSChangeFinished) {
            this->bWaitForCSChangeFinished = false;
            this->bWaitForSpeedChangeFinished = false;
            this->bWaitForHPChangeFinished = false;

            if(osu->isInPlayMode()) osu->getMapInterface()->onModUpdate();
        }

        // handle dynamic live pp calculation updates (when CS or Speed/BPM changes)
        if(this->speedSlider != nullptr && (this->speedSlider->isActive() || this->speedSlider->hasChanged())) {
            this->bWaitForSpeedChangeFinished = true;
        } else if(this->bWaitForSpeedChangeFinished) {
            this->bWaitForCSChangeFinished = false;
            this->bWaitForSpeedChangeFinished = false;
            this->bWaitForHPChangeFinished = false;

            if(osu->isInPlayMode()) osu->getMapInterface()->onModUpdate();
        }

        // handle dynamic HP drain updates
        if(this->HPSlider != nullptr && (this->HPSlider->isActive() || this->HPSlider->hasChanged())) {
            this->bWaitForHPChangeFinished = true;
        } else if(this->bWaitForHPChangeFinished) {
            this->bWaitForCSChangeFinished = false;
            this->bWaitForSpeedChangeFinished = false;
            this->bWaitForHPChangeFinished = false;
            if(osu->isInPlayMode()) osu->getMapInterface()->onModUpdate();
        }
    }
}

void ModSelector::onKeyDown(KeyboardEvent &key) {
    UIScreen::onKeyDown(key);  // only used for options menu
    if(!this->bVisible || key.isConsumed()) return;

    this->overrideSliderContainer->onKeyDown(key);

    if(key == KEY_1) this->resetModsUserInitiated();

    if(key == KEY_F1 || key == cv::TOGGLE_MODSELECT.getVal<SCANCODE>() || key == KEY_2 ||
       key == cv::GAME_PAUSE.getVal<SCANCODE>() || key == KEY_ESCAPE || key == KEY_ENTER || key == KEY_NUMPAD_ENTER)
        this->close(key == cv::GAME_PAUSE.getVal<SCANCODE>() || key == KEY_ESCAPE);

    // mod hotkeys
    if(key == cv::MOD_EASY.getVal<SCANCODE>()) this->modButtonEasy->click();
    if(key == cv::MOD_NOFAIL.getVal<SCANCODE>()) this->modButtonNofail->click();
    if(key == cv::MOD_HARDROCK.getVal<SCANCODE>()) this->modButtonHardrock->click();
    if(key == cv::MOD_SUDDENDEATH.getVal<SCANCODE>()) this->modButtonSuddendeath->click();
    if(key == cv::MOD_HIDDEN.getVal<SCANCODE>()) this->modButtonHidden->click();
    if(key == cv::MOD_FLASHLIGHT.getVal<SCANCODE>()) this->modButtonFlashlight->click();
    if(key == cv::MOD_RELAX.getVal<SCANCODE>()) this->modButtonRelax->click();
    if(key == cv::MOD_AUTOPILOT.getVal<SCANCODE>()) this->modButtonAutopilot->click();
    if(key == cv::MOD_SPUNOUT.getVal<SCANCODE>()) this->modButtonSpunout->click();
    if(key == cv::MOD_AUTO.getVal<SCANCODE>()) this->modButtonAuto->click();
    if(key == cv::MOD_SCOREV2.getVal<SCANCODE>()) this->modButtonScoreV2->click();
    if(key == cv::MOD_HALFTIME.getVal<SCANCODE>()) this->modButtonHalftime->click();
    if(key == cv::MOD_DOUBLETIME.getVal<SCANCODE>()) this->modButtonDoubletime->click();

    key.consume();
}

CBaseUIContainer *ModSelector::setVisible(bool visible) {
    if(visible && !this->bVisible) {
        this->bScheduledHide = false;

        this->updateButtons(true);          // force state update without firing callbacks
        this->updateExperimentalButtons();  // force state update without firing callbacks
        this->updateLayout();
        this->updateScoreMultiplierLabelText();
        this->updateOverrideSliderLabels();

        this->fAnimation = 0.0f;
        anim::moveQuadOut(&this->fAnimation, 1.0f, 0.1f, 0.0f, true);

        bool experimentalModEnabled = false;
        for(auto &experimentalMod : this->experimentalMods) {
            auto *checkboxPointer = dynamic_cast<CBaseUICheckbox *>(experimentalMod.element);
            if(checkboxPointer != nullptr && checkboxPointer->isChecked()) {
                experimentalModEnabled = true;
                break;
            }
        }
        if(experimentalModEnabled) {
            this->bExperimentalVisible = true;
            if(this->isInCompactMode())
                anim::moveQuadOut(&this->fExperimentalAnimation, 1.0f, (1.0f - this->fExperimentalAnimation) * 0.06f,
                                  0.0f, true);
            else
                this->fExperimentalAnimation = 1.0f;
        } else {
            this->bExperimentalVisible = false;
            this->fExperimentalAnimation = 0.0f;
            anim::deleteExistingAnimation(&this->fExperimentalAnimation);
        }
    } else if(!visible && this->bVisible) {
        this->bScheduledHide = this->isInCompactMode();

        this->fAnimation = 1.0f;
        anim::moveQuadIn(&this->fAnimation, 0.0f, 0.06f, 0.0f, true);
        this->updateScoreMultiplierLabelText();
        this->updateOverrideSliderLabels();

        this->bExperimentalVisible = false;
        anim::moveQuadIn(&this->fExperimentalAnimation, 0.0f, 0.06f, 0.0f, true);
    }

    this->bVisible = visible;
    return this;
}

bool ModSelector::isInCompactMode() const { return osu->isInPlayMode(); }

bool ModSelector::isCSOverrideSliderActive() const { return this->CSSlider->isActive(); }

bool ModSelector::isMouseInScrollView() const {
    // isVisible is not const due to bad ui framework design, but it is const (it's not even overridden in this case)
    return this->experimentalContainer->isMouseInside() && const_cast<ModSelector *>(this)->isVisible();
}

bool ModSelector::isMouseInside() {
    return this->isVisible()                                                                                      //
           &&                                                                                                     //
           (                                                                                                      //
               this->experimentalContainer->isMouseInside()                                                       //
               ||                                                                                                 //
               std::ranges::contains(this->vElements, true,                                                       //
                                     [](const auto &elem) -> bool { return elem->isMouseInside(); })              //
               ||                                                                                                 //
               std::ranges::contains(this->modButtons, true,                                                      //
                                     [](const auto &modButton) -> bool { return modButton->isMouseInside(); })    //
               ||                                                                                                 //
               std::ranges::contains(this->overrideSliders, true,                                                 //
                                     [](const auto &overrideSlider) -> bool {                                     //
                                         return (overrideSlider.lock && overrideSlider.lock->isMouseInside()) ||  //
                                                overrideSlider.desc->isMouseInside() ||                           //
                                                overrideSlider.slider->isMouseInside() ||                         //
                                                overrideSlider.label->isMouseInside();                            //
                                     })                                                                           //
           );                                                                                                     //
}

void ModSelector::updateLayout() {
    if(this->modButtons.size() < 1 || this->overrideSliders.size() < 1) return;

    const float dpiScale = Osu::getUIScale();
    const float uiScale = Osu::getRawUIScale();

    if(!this->isInCompactMode())  // normal layout
    {
        // mod grid buttons
        vec2 center = osu->getVirtScreenSize() / 2.0f;
        vec2 size = osu->getSkin()->i_modselect_ez->getSizeBase() * uiScale;
        vec2 offset = vec2(size.x * 1.0f, size.y * 0.33f);
        vec2 start = vec2(center.x - (size.x * this->iGridWidth) / 2.0f - (offset.x * (this->iGridWidth - 1)) / 2.0f,
                          center.y - (size.y * this->iGridHeight) / 2.0f - (offset.y * (this->iGridHeight - 1)) / 2.0f);

        for(int x = 0; x < this->iGridWidth; x++) {
            for(int y = 0; y < this->iGridHeight; y++) {
                UIModSelectorModButton *button = this->getModButtonOnGrid(x, y);

                if(button != nullptr) {
                    button->setPos(start + vec2(size.x * x + offset.x * x, size.y * y + offset.y * y));
                    button->setBaseScale(1.0f * uiScale, 1.0f * uiScale);
                    button->setSize(size);
                }
            }
        }

        // override sliders (down here because they depend on the mod grid button alignment)
        const int margin = 10 * dpiScale;
        const int overrideSliderWidth = Osu::getUIScale(250.0f);
        const int overrideSliderHeight = 25 * dpiScale;
        const int overrideSliderOffsetY =
            ((start.y - this->overrideSliders.size() * overrideSliderHeight) / (this->overrideSliders.size() - 1)) *
            0.35f;
        const vec2 overrideSliderStart =
            vec2(osu->getVirtScreenWidth() / 2 - overrideSliderWidth / 2,
                 start.y / 2 - (this->overrideSliders.size() * overrideSliderHeight +
                                (this->overrideSliders.size() - 1) * overrideSliderOffsetY) /
                                   1.75f);
        for(int i = -1; const auto &ovsl : this->overrideSliders) {
            ++i;

            ovsl.desc->setSizeToContent(5 * dpiScale, 0);
            ovsl.desc->setSizeY(overrideSliderHeight);

            ovsl.slider->setBlockSize(20 * dpiScale, 20 * dpiScale);
            ovsl.slider->setPos(overrideSliderStart.x,
                                overrideSliderStart.y + i * overrideSliderHeight + i * overrideSliderOffsetY);
            ovsl.slider->setSize(overrideSliderWidth, overrideSliderHeight);

            ovsl.desc->setPos(ovsl.slider->getPos().x - ovsl.desc->getSize().x - margin, ovsl.slider->getPos().y);

            if(ovsl.lock != nullptr && this->overrideSliders.size() > 1) {
                ovsl.lock->setPos(
                    this->overrideSliders[1].desc->getPos().x - ovsl.lock->getSize().x - margin - 3 * dpiScale,
                    ovsl.desc->getPos().y);
                ovsl.lock->setSizeY(overrideSliderHeight);
            }

            ovsl.label->setPos(ovsl.slider->getPos().x + ovsl.slider->getSize().x + margin, ovsl.slider->getPos().y);
            ovsl.label->setSizeToContent(0, 0);
            ovsl.label->setSizeY(overrideSliderHeight);
        }

        // action buttons
        float actionMinY = start.y + size.y * this->iGridHeight +
                           offset.y * (this->iGridHeight - 1);  // exact bottom of the mod buttons
        vec2 actionSize = vec2(Osu::getUIScale(448.0f) * uiScale, size.y * 0.75f);
        float actionOffsetY = actionSize.y * 0.5f;
        vec2 actionStart = vec2(
            osu->getVirtScreenWidth() / 2.0f - actionSize.x / 2.0f,
            actionMinY + (osu->getVirtScreenHeight() - actionMinY) / 2.0f -
                (actionSize.y * this->actionButtons.size() + actionOffsetY * (this->actionButtons.size() - 1)) / 2.0f);
        for(int i = -1; auto *actbtn : this->actionButtons) {
            ++i;

            actbtn->setVisible(true);
            actbtn->setPos(actionStart.x, actionStart.y + actionSize.y * i + actionOffsetY * i);
            actbtn->onResized();  // HACKHACK: framework, setSize*() does not update string metrics
            actbtn->setSize(actionSize);
        }

        // score multiplier info label
        const float modGridMaxY = start.y + size.y * this->iGridHeight +
                                  offset.y * (this->iGridHeight - 1);  // exact bottom of the mod buttons
        this->nonSubmittableWarning->setSizeToContent()
            ->setSize(vec2(osu->getVirtScreenWidth(), 20 * uiScale))
            ->setPos(
                0, modGridMaxY + std::abs(actionStart.y - modGridMaxY) / 2 - this->nonSubmittableWarning->getSize().y);

        static_cast<CBaseUILabel *>(this->scoreMultiplierLabel->setVisible(true))
            ->setSizeToContent()
            ->setSize(vec2(osu->getVirtScreenWidth(), 30 * uiScale))
            ->setPos(0, this->nonSubmittableWarning->getPos().y + 20 * uiScale);
    } else  // compact in-beatmap mode
    {
        // mod grid buttons
        vec2 center = osu->getVirtScreenSize() / 2.0f;
        vec2 blockSize = osu->getSkin()->i_modselect_ez->getSizeBase() * uiScale;
        vec2 offset = vec2(blockSize.x * 0.15f, blockSize.y * 0.05f);
        vec2 size = vec2((blockSize.x * this->iGridWidth) + (offset.x * (this->iGridWidth - 1)),
                         (blockSize.y * this->iGridHeight) + (offset.y * (this->iGridHeight - 1)));
        center.y = osu->getVirtScreenHeight() - size.y / 2 - offset.y * 3.0f;
        vec2 start = vec2(center.x - size.x / 2.0f, center.y - size.y / 2.0f);

        for(int x = 0; x < this->iGridWidth; x++) {
            for(int y = 0; y < this->iGridHeight; y++) {
                UIModSelectorModButton *button = this->getModButtonOnGrid(x, y);

                if(button != nullptr) {
                    button->setPos(start + vec2(blockSize.x * x + offset.x * x, blockSize.y * y + offset.y * y));
                    button->setBaseScale(1 * uiScale, 1 * uiScale);
                    button->setSize(blockSize);
                }
            }
        }

        // override sliders (down here because they depend on the mod grid button alignment)
        const int margin = 10 * dpiScale;
        int overrideSliderWidth = Osu::getUIScale(250.0f);
        int overrideSliderHeight = 25 * dpiScale;
        int overrideSliderOffsetY = 5 * dpiScale;
        vec2 overrideSliderStart = vec2(osu->getVirtScreenWidth() / 2 - overrideSliderWidth / 2, 5 * dpiScale);
        for(int i = -1; const auto &ovsl : this->overrideSliders) {
            ++i;

            ovsl.desc->setSizeToContent(5 * dpiScale, 0);
            ovsl.desc->setSizeY(overrideSliderHeight);

            ovsl.slider->setPos(overrideSliderStart.x,
                                overrideSliderStart.y + i * overrideSliderHeight + i * overrideSliderOffsetY);
            ovsl.slider->setSizeX(overrideSliderWidth);

            ovsl.desc->setPos(ovsl.slider->getPos().x - ovsl.desc->getSize().x - margin, ovsl.slider->getPos().y);

            if(ovsl.lock != nullptr && this->overrideSliders.size() > 1) {
                ovsl.lock->setPos(
                    this->overrideSliders[1].desc->getPos().x - ovsl.lock->getSize().x - margin - 3 * dpiScale,
                    ovsl.desc->getPos().y);
                ovsl.lock->setSizeY(overrideSliderHeight);
            }

            ovsl.label->setPos(ovsl.slider->getPos().x + ovsl.slider->getSize().x + margin, ovsl.slider->getPos().y);
            ovsl.label->setSizeToContent(0, 0);
            ovsl.label->setSizeY(overrideSliderHeight);
        }

        // action buttons
        for(auto *actionButton : this->actionButtons) {
            actionButton->setVisible(false);
        }

        // score multiplier info label
        this->scoreMultiplierLabel->setVisible(false);
    }

    this->updateExperimentalLayout();
}

void ModSelector::updateExperimentalLayout() {
    const float dpiScale = Osu::getUIScale();

    // experimental mods
    int yCounter = 5 * dpiScale;
    int experimentalMaxWidth = 0;
    int experimentalOffsetY = 6 * dpiScale;
    for(int i = 0; i < this->experimentalMods.size(); i++) {
        CBaseUIElement *e = this->experimentalMods[i].element;
        e->setRelPosY(yCounter);
        e->setSizeY(e->getRelSize().y * dpiScale);

        // custom
        {
            auto *checkboxPointer = dynamic_cast<CBaseUICheckbox *>(e);
            if(checkboxPointer != nullptr) {
                checkboxPointer->onResized();
                checkboxPointer->setWidthToContent(0);
            }

            auto *labelPointer = dynamic_cast<CBaseUILabel *>(e);
            if(labelPointer != nullptr) {
                labelPointer->onResized();
                labelPointer->setWidthToContent(0);
            }
        }

        if(e->getSize().x > experimentalMaxWidth) experimentalMaxWidth = e->getSize().x;

        yCounter += e->getSize().y + experimentalOffsetY;

        if(i == 0) yCounter += 8 * dpiScale;
    }

    // laziness
    if(osu->getVirtScreenHeight() > yCounter)
        yCounter = 5 * dpiScale + osu->getVirtScreenHeight() / 2.0f - yCounter / 2.0f;
    else
        yCounter = 5 * dpiScale;

    for(int i = 0; i < this->experimentalMods.size(); i++) {
        CBaseUIElement *e = this->experimentalMods[i].element;
        e->setRelPosY(yCounter);

        if(e->getSize().x > experimentalMaxWidth) experimentalMaxWidth = e->getSize().x;

        yCounter += e->getSize().y + experimentalOffsetY;

        if(i == 0) yCounter += 8 * dpiScale;
    }

    this->experimentalContainer->setSizeX(experimentalMaxWidth + 25 * dpiScale /*, yCounter*/);
    this->experimentalContainer->setPosY(-1);
    this->experimentalContainer->setScrollSizeToContent(1 * dpiScale);
    this->experimentalContainer->container.update_pos();
    this->experimentalContainer->setVisible(!BanchoState::is_in_a_multi_room());
}

UIModSelectorModButton *ModSelector::setModButtonOnGrid(int x, int y, int state, bool initialState, ConVar *modCvar,
                                                        UString modName, const UString &tooltipText,
                                                        SkinImageSkinMember getSkinImageMember) {
    UIModSelectorModButton *modButton = this->getModButtonOnGrid(x, y);

    if(modButton != nullptr) {
        modButton->setState(state, initialState, modCvar, std::move(modName), tooltipText, getSkinImageMember);
        modButton->setVisible(true);
    }

    return modButton;
}

UIModSelectorModButton *ModSelector::getModButtonOnGrid(int x, int y) {
    const int index = x * this->iGridHeight + y;

    if(index < this->modButtons.size())
        return this->modButtons[index];
    else
        return nullptr;
}

ModSelector::OVERRIDE_SLIDER ModSelector::addOverrideSlider(UString text, const UString &labelText, ConVar *cvar,
                                                            float min, float max, UString tooltipText,
                                                            ConVar *lockCvar) {
    const float height = 25;

    OVERRIDE_SLIDER os;
    if(lockCvar != nullptr) {
        os.lock = new ModSelectorOverrideSliderLockButton(0.f, 0.f, height, height, "", "");
        os.lock->setChangeCallback(SA::MakeDelegate<&ModSelector::onOverrideSliderLockChange>(this));
    }
    os.desc = new ModSelectorOverrideSliderDescButton(0.f, 0.f, 100.f, height, "", std::move(text));
    os.desc->setTooltipText(std::move(tooltipText));
    os.slider = new UISlider(0.f, 0.f, 100.f, height, "");
    os.label = new CBaseUILabel(0.f, 0.f, 100.f, height, labelText, labelText);
    os.cvar = cvar;
    os.lockCvar = lockCvar;

    bool debugDrawFrame = false;

    os.slider->setDrawFrame(debugDrawFrame);
    os.slider->setDrawBackground(false);
    os.slider->setFrameColor(0xff777777);
    os.desc->setEnabled(lockCvar != nullptr);
    os.desc->setDrawFrame(debugDrawFrame);
    os.desc->setDrawBackground(false);
    os.desc->setTextColor(0xff777777);
    os.label->setDrawFrame(debugDrawFrame);
    os.label->setDrawBackground(false);
    os.label->setTextColor(0xff777777);

    os.slider->setBounds(min, max + 1.0f);
    os.slider->setKeyDelta(0.1f);
    os.slider->setLiveUpdate(true);
    os.slider->setAllowMouseWheel(false);

    if(os.cvar != nullptr) os.slider->setValue(os.cvar->getFloat() + 1.0f, false);

    if(os.lock != nullptr) this->overrideSliderContainer->addBaseUIElement(os.lock);

    this->overrideSliderContainer->addBaseUIElement(os.desc);
    this->overrideSliderContainer->addBaseUIElement(os.slider);
    this->overrideSliderContainer->addBaseUIElement(os.label);
    this->overrideSliders.push_back(os);

    return os;
}

UIButton *ModSelector::addActionButton(const UString &text) {
    auto *actionButton = new UIButton(50, 50, 100, 100, text, text);
    this->actionButtons.push_back(actionButton);
    this->addBaseUIElement(actionButton);

    return actionButton;
}

CBaseUILabel *ModSelector::addExperimentalLabel(const UString &text) {
    auto *label = new CBaseUILabel(0, 0, 0, 25, text, text);
    label->setFont(osu->getSubTitleFont());
    label->setWidthToContent(0);
    label->setDrawBackground(false);
    label->setDrawFrame(false);
    this->experimentalContainer->container.addBaseUIElement(label);

    EXPERIMENTAL_MOD em;
    em.element = label;
    em.cvar = nullptr;
    this->experimentalMods.push_back(em);

    return label;
}

UICheckbox *ModSelector::addExperimentalCheckbox(const UString &text, const UString &tooltipText, ConVar *cvar) {
    auto *checkbox = new UICheckbox(0, 0, 0, 35, text, text);
    checkbox->setTooltipText(tooltipText);
    checkbox->setWidthToContent(0);
    if(cvar != nullptr) {
        checkbox->setChecked(cvar->getBool());
        checkbox->setChangeCallback(SA::MakeDelegate<&ModSelector::onCheckboxChange>(this));
    }
    this->experimentalContainer->container.addBaseUIElement(checkbox);

    EXPERIMENTAL_MOD em;
    em.element = checkbox;
    em.cvar = cvar;
    this->experimentalMods.push_back(em);

    return checkbox;
}

void ModSelector::resetModsUserInitiated() {
    using namespace flags::operators;

    this->resetMods();

    soundEngine->play(osu->getSkin()->s_check_off);
    this->resetModsButton->animateClickColor();

    if(BanchoState::is_in_a_multi_room()) {
        LegacyFlags minimum_mods{};
        if(!BanchoState::room.is_host()) {
            minimum_mods = BanchoState::room.mods;
            minimum_mods &= (LegacyFlags::DoubleTime | LegacyFlags::HalfTime | LegacyFlags::Target);
        }

        Packet packet;
        packet.id = OUTP_MATCH_CHANGE_MODS;
        packet.write<LegacyFlags>(minimum_mods);
        BANCHO::Net::send_packet(packet);

        // Don't wait on server response to update UI
        this->enableModsFromFlags(minimum_mods);
    }

    if(BanchoState::is_online()) {
        RichPresence::updateBanchoMods();
    }
}

void ModSelector::resetMods() {
    // cv::mod_fposu.setValue(false);  // intentionally commented out so people can play it in multi

    for(auto &overrideSlider : this->overrideSliders) {
        if(overrideSlider.lock != nullptr) overrideSlider.lock->setChecked(false);
    }

    for(auto &overrideSlider : this->overrideSliders) {
        // HACKHACK: force small delta to force an update (otherwise values could get stuck, e.g. for "Use Mods" context
        // menu)
        // HACKHACK: only animate while visible to workaround "Use mods" bug (if custom speed multiplier already
        // set and then "Use mods" with different custom speed multiplier would reset to 1.0x because of anim)
        overrideSlider.slider->setValue(overrideSlider.slider->getMin() + 0.0001f, this->bVisible);
        overrideSlider.slider->setValue(overrideSlider.slider->getMin(), this->bVisible);
    }

    for(auto &modButton : this->modButtons) {
        modButton->resetState();
    }

    for(auto &experimentalMod : this->experimentalMods) {
        ConVar *cvar = experimentalMod.cvar;
        auto *checkboxPointer = dynamic_cast<CBaseUICheckbox *>(experimentalMod.element);
        if(checkboxPointer != nullptr) {
            // HACKHACK: we update both just in case because if the mod selector was not yet visible after a convar
            // change (e.g. because of "Use mods") then the checkbox has not yet updated its internal state
            checkboxPointer->setChecked(false);
            if(cvar != nullptr) cvar->setValue(0.0f);
        }
    }
}

LegacyFlags ModSelector::getModFlags() const {
    // We need the mod flags to always be up to date
    auto mods = Replay::Mods::from_cvars();
    return mods.to_legacy();
}

void ModSelector::enableModsFromFlags(LegacyFlags flags) {
    using namespace flags::operators;

    if(flags::any<LegacyFlags::DoubleTime | LegacyFlags::Nightcore>(flags)) {
        cv::speed_override.setValue(1.5f);
    } else if(flags::has<LegacyFlags::HalfTime>(flags)) {
        cv::speed_override.setValue(0.75f);
    } else {
        cv::speed_override.setValue(-1.f);
    }

    cv::mod_suddendeath.setValue(false);
    cv::mod_perfect.setValue(false);
    if(flags::has<LegacyFlags::Perfect>(flags)) {
        this->modButtonSuddendeath->setState(1);
        this->modButtonSuddendeath->setOn(true, true);
    } else if(flags::has<LegacyFlags::SuddenDeath>(flags)) {
        this->modButtonSuddendeath->setState(0);
        this->modButtonSuddendeath->setOn(true, true);
    }

    this->modButtonNofail->setOn(flags::has<LegacyFlags::NoFail>(flags), true);
    this->modButtonEasy->setOn(flags::has<LegacyFlags::Easy>(flags), true);
    this->modButtonTD->setOn(flags::has<LegacyFlags::TouchDevice>(flags), true);
    this->modButtonHidden->setOn(flags::has<LegacyFlags::Hidden>(flags), true);
    this->modButtonHardrock->setOn(flags::has<LegacyFlags::HardRock>(flags), true);
    this->modButtonRelax->setOn(flags::has<LegacyFlags::Relax>(flags), true);
    this->modButtonSpunout->setOn(flags::has<LegacyFlags::SpunOut>(flags), true);
    this->modButtonAutopilot->setOn(flags::has<LegacyFlags::Autopilot>(flags), true);
    this->getModButtonOnGrid(4, 2)->setOn(flags::has<LegacyFlags::Target>(flags), true);
    this->modButtonFlashlight->setOn(flags::has<LegacyFlags::Flashlight>(flags), true);
    this->modButtonScoreV2->setOn(flags::has<LegacyFlags::ScoreV2>(flags), true);

    osu->updateMods();
}

void ModSelector::close(bool force) {
    this->closeButton->animateClickColor();

    if(osu->isInPlayMode()) {
        if(force) this->setVisible(false);
    } else if(BanchoState::is_in_a_multi_room()) {
        ui->setScreen(ui->getRoom());
    } else {
        ui->setScreen(ui->getSongBrowser());
    }
}

void ModSelector::onOverrideSliderChange(CBaseUISlider *slider) {
    for(auto &overrideSlider : this->overrideSliders) {
        if(overrideSlider.slider != slider) continue;

        float sliderValue = slider->getFloat() - 1.0f;
        const float rawSliderValue = slider->getFloat();

        // alt key allows rounding to only 1 decimal digit
        if(!keyboard->isAltDown())
            sliderValue = std::round(sliderValue * 10.0f) / 10.0f;
        else
            sliderValue = std::round(sliderValue * 100.0f) / 100.0f;

        if(sliderValue < 0.0f) {
            sliderValue = -1.0f;
            overrideSlider.label->setWidthToContent(0);

            // HACKHACK: dirty
            if(osu->getMapInterface()->getBeatmap() != nullptr) {
                if(overrideSlider.label->getName().find("BPM") != -1) {
                    // reset AR and OD override sliders if the bpm slider was reset
                    if(!this->ARLock->isChecked()) this->ARSlider->setValue(0.0f, false);
                    if(!this->ODLock->isChecked()) this->ODSlider->setValue(0.0f, false);
                }
            }

            // usability: auto disable lock if override slider is fully set to -1.0f (disabled)
            if(rawSliderValue == 0.0f) {
                if(overrideSlider.lock != nullptr && overrideSlider.lock->isChecked())
                    overrideSlider.lock->setChecked(false);
            }
        } else {
            // HACKHACK: dirty
            if(overrideSlider.label->getName().find("BPM") != -1) {
                // HACKHACK: force Speed/BPM slider to have a min value of 0.05 instead of 0 (because that's the
                // minimum for BASS) note that the BPM slider is just a 'fake' slider, it directly controls the
                // speed slider to do its thing (thus it needs the same limits)
                sliderValue = std::max(sliderValue, 0.05f);

                // speed slider may not be used in conjunction
                this->speedSlider->setValue(0.0f, false);

                // force early update
                overrideSlider.cvar->setValue(sliderValue);

                // AR/OD lock may not be used in conjunction with BPM
                this->ARLock->setChecked(false);
                this->ODLock->setChecked(false);

                // force change all other depending sliders
                if(osu->getMapInterface()->getBeatmap() != nullptr) {
                    const float newAR = osu->getMapInterface()->getConstantApproachRateForSpeedMultiplier();
                    const float newOD = osu->getMapInterface()->getConstantOverallDifficultyForSpeedMultiplier();

                    // '+1' to compensate for turn-off area of the override sliders
                    this->ARSlider->setValue(newAR + 1.0f, false);
                    this->ODSlider->setValue(newOD + 1.0f, false);
                }
            }
        }

        // update convar with final value (e.g. cv::ar_override, etc.)
        overrideSlider.cvar->setValue(sliderValue);

        this->updateOverrideSliderLabels();

        break;
    }

    osu->updateMods();
}

void ModSelector::onOverrideSliderLockChange(CBaseUICheckbox *checkbox) {
    for(auto &overrideSlider : this->overrideSliders) {
        if(overrideSlider.lock == checkbox) {
            const bool locked = overrideSlider.lock->isChecked();
            const bool wasLocked = overrideSlider.lockCvar->getBool();

            // update convar with final value (e.g. osu_ar_override_lock, cv::od_override_lock)
            overrideSlider.lockCvar->setValue(locked ? 1.0f : 0.0f);

            // usability: if we just got locked, and the override slider value is < 0.0f (disabled), then set override
            // to current value
            if(locked && !wasLocked) {
                if(checkbox == this->ARLock) {
                    if(this->ARSlider->getFloat() < 1.0f)
                        this->ARSlider->setValue(
                            osu->getMapInterface()->getRawAR() + 1.0f,
                            false);  // '+1' to compensate for turn-off area of the override sliders
                } else if(checkbox == this->ODLock) {
                    if(this->ODSlider->getFloat() < 1.0f)
                        this->ODSlider->setValue(
                            osu->getMapInterface()->getRawOD() + 1.0f,
                            false);  // '+1' to compensate for turn-off area of the override sliders
                }
            }

            this->updateOverrideSliderLabels();

            break;
        }
    }

    osu->updateMods();
}

void ModSelector::onOverrideARSliderDescClicked(CBaseUIButton * /*button*/) { this->ARLock->click(); }

void ModSelector::onOverrideODSliderDescClicked(CBaseUIButton * /*button*/) { this->ODLock->click(); }

void ModSelector::updateOverrideSliderLabels() {
    const Color inactiveColor = 0xff777777;
    const Color activeColor = 0xffffffff;
    const Color inactiveLabelColor = 0xff1166ff;

    for(auto &overrideSlider : this->overrideSliders) {
        const float convarValue = overrideSlider.cvar->getFloat();
        const bool isLocked = (overrideSlider.lock != nullptr && overrideSlider.lock->isChecked());

        // update colors
        if(convarValue < 0.0f && !isLocked) {
            overrideSlider.label->setTextColor(inactiveLabelColor);
            overrideSlider.desc->setTextColor(inactiveColor);
            overrideSlider.slider->setFrameColor(inactiveColor);
        } else {
            overrideSlider.label->setTextColor(activeColor);
            overrideSlider.desc->setTextColor(activeColor);
            overrideSlider.slider->setFrameColor(activeColor);
        }

        overrideSlider.desc->setDrawFrame(isLocked);

        // update label text
        overrideSlider.label->setText(this->getOverrideSliderLabelText(overrideSlider, convarValue >= 0.0f));
        overrideSlider.label->setWidthToContent(0);

        // update lock checkbox
        if(overrideSlider.lock != nullptr && overrideSlider.lockCvar != nullptr &&
           overrideSlider.lock->isChecked() != overrideSlider.lockCvar->getBool())
            overrideSlider.lock->setChecked(overrideSlider.lockCvar->getBool());
    }
}

UString ModSelector::getOverrideSliderLabelText(const ModSelector::OVERRIDE_SLIDER &s, bool active) {
    float convarValue = s.cvar->getFloat();

    UString newLabelText = s.label->getName();
    if(osu->getMapInterface()->getBeatmap() != nullptr) {
        // for relevant values (AR/OD), any non-1.0x speed multiplier should show the fractional parts caused by such a
        // speed multiplier (same for non-1.0x difficulty multiplier)
        const bool forceDisplayTwoDecimalDigits =
            (osu->getMapInterface()->getSpeedMultiplier() != 1.0f || Osu::getDifficultyMultiplier() != 1.0f ||
             Osu::getCSDifficultyMultiplier() != 1.0f);

        // HACKHACK: dirty
        bool wasSpeedSlider = false;
        float beatmapValue = 1.0f;
        if(s.label->getName().find("CS") != -1) {
            beatmapValue = std::clamp<float>(
                osu->getMapInterface()->getBeatmap()->getCS() * Osu::getCSDifficultyMultiplier(), 0.0f, 10.0f);
            convarValue = osu->getMapInterface()->getCS();
        } else if(s.label->getName().find("AR") != -1) {
            beatmapValue = active ? osu->getMapInterface()->getRawARForSpeedMultiplier()
                                  : osu->getMapInterface()->getApproachRateForSpeedMultiplier();

            // compensate and round
            convarValue = osu->getMapInterface()->getApproachRateForSpeedMultiplier();
            if(!keyboard->isAltDown() && !forceDisplayTwoDecimalDigits)
                convarValue = std::round(convarValue * 10.0f) / 10.0f;
            else
                convarValue = std::round(convarValue * 100.0f) / 100.0f;
        } else if(s.label->getName().find("OD") != -1) {
            beatmapValue = active ? osu->getMapInterface()->getRawODForSpeedMultiplier()
                                  : osu->getMapInterface()->getOverallDifficultyForSpeedMultiplier();

            // compensate and round
            convarValue = osu->getMapInterface()->getOverallDifficultyForSpeedMultiplier();
            if(!keyboard->isAltDown() && !forceDisplayTwoDecimalDigits)
                convarValue = std::round(convarValue * 10.0f) / 10.0f;
            else
                convarValue = std::round(convarValue * 100.0f) / 100.0f;
        } else if(s.label->getName().find("HP") != -1) {
            beatmapValue = std::clamp<float>(
                osu->getMapInterface()->getBeatmap()->getHP() * Osu::getDifficultyMultiplier(), 0.0f, 10.0f);
            convarValue = osu->getMapInterface()->getHP();
        } else if(s.desc->getText().find("Speed") != -1) {
            beatmapValue = active ? 1.f : osu->getMapInterface()->getSpeedMultiplier();

            wasSpeedSlider = true;
            {
                {
                    if(!active)
                        newLabelText.append(fmt::format(" {:.4g}", beatmapValue));
                    else
                        newLabelText.append(fmt::format(" {:.4g} -> {:.4g}", beatmapValue, convarValue));
                }

                newLabelText.append("  (BPM: ");

                int minBPM = osu->getMapInterface()->getBeatmap()->getMinBPM();
                int maxBPM = osu->getMapInterface()->getBeatmap()->getMaxBPM();
                int mostCommonBPM = osu->getMapInterface()->getBeatmap()->getMostCommonBPM();
                int newMinBPM = minBPM * osu->getMapInterface()->getSpeedMultiplier();
                int newMaxBPM = maxBPM * osu->getMapInterface()->getSpeedMultiplier();
                int newMostCommonBPM = mostCommonBPM * osu->getMapInterface()->getSpeedMultiplier();
                if(!active || osu->getMapInterface()->getSpeedMultiplier() == 1.0f) {
                    if(minBPM == maxBPM)
                        newLabelText.append(fmt::format("{}", newMaxBPM));
                    else
                        newLabelText.append(fmt::format("{}-{} ({})", newMinBPM, newMaxBPM, newMostCommonBPM));
                } else {
                    if(minBPM == maxBPM)
                        newLabelText.append(fmt::format("{} -> {}", maxBPM, newMaxBPM));
                    else
                        newLabelText.append(fmt::format("{}-{} ({}) -> {}-{} ({})", minBPM, maxBPM, mostCommonBPM,
                                                        newMinBPM, newMaxBPM, newMostCommonBPM));
                }

                newLabelText.append(")");
            }
        }

        // always round beatmapValue to 1 decimal digit, except for the speed slider, and except for non-1.0x speed
        // multipliers, and except for non-1.0x difficulty multipliers HACKHACK: dirty
        if(s.desc->getText().find("Speed") == -1) {
            if(forceDisplayTwoDecimalDigits)
                beatmapValue = std::round(beatmapValue * 100.0f) / 100.0f;
            else
                beatmapValue = std::round(beatmapValue * 10.0f) / 10.0f;
        }

        // update label
        if(!wasSpeedSlider) {
            if(!active)
                newLabelText.append(fmt::format(" {:.4g}", beatmapValue));
            else
                newLabelText.append(fmt::format(" {:.4g} -> {:.4g}", beatmapValue, convarValue));
        }
    }

    return newLabelText;
}

void ModSelector::enableAuto() {
    if(!this->modButtonAuto->isOn()) this->modButtonAuto->click();
}

void ModSelector::toggleAuto() { this->modButtonAuto->click(); }

void ModSelector::onCheckboxChange(CBaseUICheckbox *checkbox) {
    for(auto &experimentalMod : this->experimentalMods) {
        if(experimentalMod.element == checkbox) {
            if(experimentalMod.cvar != nullptr) experimentalMod.cvar->setValue(checkbox->isChecked());

            // force mod update
            if(osu->isInPlayMode()) osu->getMapInterface()->onModUpdate();

            break;
        }
    }

    osu->updateMods();
}

void ModSelector::onResolutionChange(vec2 newResolution) {
    this->setSize(newResolution);
    this->overrideSliderContainer->setSize(newResolution);
    this->experimentalContainer->setSizeY(newResolution.y + 1);

    this->updateLayout();
}
