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
#include "ContainerRanges.h"
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
#include "OsuKeyBinds.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "OptionsOverlay.h"
#include "Osu.h"
#include "RichPresence.h"
#include "RoomScreen.h"
#include "Graphics.h"
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
#include "UniString.h"

class ModSelectorOverrideSliderDescButton final : public CBaseUIButton {
   public:
    ModSelectorOverrideSliderDescButton(float xPos, float yPos, float xSize, float ySize, std::string name,
                                        std::string text)
        : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name), std::move(text)) {}

    void update(CBaseUIEventCtx &c) override {
        if(!this->bVisible) return;
        CBaseUIButton::update(c);

        if(this->isMouseInside() && this->sTooltipText.length() > 0) {
            TooltipOverlay *ttoverlay = ui->getTooltipOverlay();
            ttoverlay->begin();
            {
                ttoverlay->addLine(this->sTooltipText);
            }
            ttoverlay->end();
        }
    }

    void setTooltipText(std::string tooltipText) { this->sTooltipText = std::move(tooltipText); }

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

    std::string sTooltipText;
};

namespace {

class ModSelectorOverrideSliderLockButton final : public CBaseUICheckbox {
   public:
    ModSelectorOverrideSliderLockButton(float xPos, float yPos, float xSize, float ySize, std::string name,
                                        std::string text)
        : CBaseUICheckbox(xPos, yPos, xSize, ySize, std::move(name), std::move(text)) {
        this->fAnim = 1.0f;
    }

    void draw() override {
        if(!this->bVisible) return;

        const auto icon = (this->bChecked ? Icons::LOCK : Icons::UNLOCK);

        std::string iconString;
        const char32_t charray[]{icon, U'\0'};
        iconString.append(UniString::to_utf8(std::u32string_view{&charray[0]}));

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
            this->fAnim.stop();
            this->fAnim = 1.0f;
        }
    }

    AnimFloat fAnim;
};
}  // namespace

ModSelector::ModSelector() : UIScreen() {
    const vec2 osuScreen = osu->getVirtScreenSize();

    this->setSize(osuScreen.x, osuScreen.y);
    this->overrideSliderContainer = std::make_unique<CBaseUIContainer>(0.f, 0.f, osuScreen.x, osuScreen.y, "");
    this->experimentalContainer.reset((new CBaseUIScrollView(-1.f, 0.f, osuScreen.x, osuScreen.y, ""))
                                          ->setHorizontalScrolling(false)
                                          ->setVerticalScrolling(true)
                                          ->setDrawFrame(false)
                                          ->setDrawBackground(false));

    // build mod grid buttons
    for(auto &button : this->modButtons) {
        auto *imageButton = new UIModSelectorModButton(this, 50, 50, 100, 100, "");
        imageButton->setDrawBackground(false);
        imageButton->setVisible(false);

        this->addBaseUIElement(imageButton);
        button = imageButton;
    }

    // build override sliders
    OVERRIDE_SLIDER overrideCS = this->addOverrideSlider(_("CS Override"), _("CS:"), &cv::cs_override, 0.0f, 12.5f,
                                                         _("Circle Size (higher number = smaller circles)."));
    OVERRIDE_SLIDER overrideAR =
        this->addOverrideSlider(_("AR Override"), _("AR:"), &cv::ar_override, 0.0f, 12.5f,
                                _("Approach Rate (higher number = faster circles)."), &cv::ar_override_lock);
    OVERRIDE_SLIDER overrideOD =
        this->addOverrideSlider(_("OD Override"), _("OD:"), &cv::od_override, 0.0f, 12.5f,
                                _("Overall Difficulty (higher number = harder accuracy)."), &cv::od_override_lock);
    OVERRIDE_SLIDER overrideHP = this->addOverrideSlider(_("HP Override"), _("HP:"), &cv::hp_override, 0.0f, 12.5f,
                                                         _("Hit/Health Points (higher number = harder survival)."));

    overrideCS.slider->setAnimated(false);  // quick fix for otherwise possible inconsistencies due to slider vertex
                                            // buffers and animated CS changes
    auto changeCallback = SA::MakeDelegate<&ModSelector::onOverrideSliderChange>(this);
    for(auto *slider : std::array{overrideCS.slider, overrideAR.slider, overrideOD.slider, overrideHP.slider}) {
        slider->setChangeCallback(changeCallback);
    }

    overrideAR.desc->setClickCallback(SA::MakeDelegate<&ModSelector::onOverrideARSliderDescClicked>(this));
    overrideOD.desc->setClickCallback(SA::MakeDelegate<&ModSelector::onOverrideODSliderDescClicked>(this));

    this->CSSlider = overrideCS.slider;
    this->ARSlider = overrideAR.slider;
    this->ODSlider = overrideOD.slider;
    this->HPSlider = overrideHP.slider;
    this->ARLock = overrideAR.lock;
    this->ODLock = overrideOD.lock;

    OVERRIDE_SLIDER overrideSpeed =
        this->addOverrideSlider(_("Speed/BPM Multiplier"), "x", &cv::speed_override, 0.9f, 2.5f);

    overrideSpeed.slider->setChangeCallback(SA::MakeDelegate<&ModSelector::onOverrideSliderChange>(this));
    // overrideSpeed.slider->setValue(-1.0f, false);
    overrideSpeed.slider->setAnimated(false);  // same quick fix as above
    overrideSpeed.slider->setLiveUpdate(false);

    this->speedSlider = overrideSpeed.slider;

    // build experimental buttons
    this->addExperimentalLabel(_(" Experimental Mods (!)"));
    this->addExperimentalCheckbox(_("Flip Up/Down"),
                                  _("Playfield is flipped upside down (mirrored at horizontal axis)."),
                                  &cv::playfield_mirror_horizontal);
    this->addExperimentalCheckbox(_("Flip Left/Right"),
                                  _("Playfield is flipped left/right (mirrored at vertical axis)."),
                                  &cv::playfield_mirror_vertical);
    this->addExperimentalCheckbox(_("Singletap"), _("You can only press one key."), &cv::mod_singletap);
    this->addExperimentalCheckbox(_("Alternate"), _("You can never use the same key twice in a row."),
                                  &cv::mod_fullalternate);
    this->addExperimentalCheckbox(_("No keylock"), _("You can use 4 keys instead of only 2."), &cv::mod_no_keylock);
    this->addExperimentalCheckbox(_("DKS"), _("Also click hitcircles when releasing a key."), &cv::mod_dks);
    this->addExperimentalCheckbox(_("Traceable"), _("Hitcircles are invisible. Good luck if you use this with Hidden!"),
                                  &cv::mod_traceable);
    this->addExperimentalCheckbox(
        _("Freeze Frame"),
        _("Draw all hitobjects in a combo group together. Try it with Hidden for an extra challenge!"),
        &cv::mod_freeze_frame);
    this->addExperimentalCheckbox(_("No pausing"), _("Pausing is cheating"), &cv::mod_no_pausing);
    this->addExperimentalCheckbox(
        _("FPoSu: Strafing"), _("Playfield moves in 3D space (see fposu_mod_strafing_...).\nOnly works in FPoSu mode!"),
        &cv::fposu_mod_strafing);
    this->addExperimentalCheckbox(_("Wobble"), _("Playfield rotates and moves."), &cv::mod_wobble);
    this->addExperimentalCheckbox(_("AR Wobble"), _("Approach rate oscillates between -1 and +1."), &cv::mod_arwobble);
    this->addExperimentalCheckbox(_("Approach Different"),
                                  _("Customize the approach circle animation.\nSee mod_approach_different_style.\nSee "
                                    "mod_approach_different_initial_size."),
                                  &cv::mod_approach_different);
    this->addExperimentalCheckbox(_("Timewarp"), _("Speed increases from 100% to 150% over the course of the beatmap."),
                                  &cv::mod_timewarp);
    this->addExperimentalCheckbox(_("AR Timewarp"),
                                  _("Approach rate decreases from 100% to 50% over the course of the beatmap."),
                                  &cv::mod_artimewarp);
    this->addExperimentalCheckbox(
        _("Minimize"), _("Circle size decreases from 100% to 50% over the course of the beatmap."), &cv::mod_minimize);
    this->addExperimentalCheckbox(_("Fading Cursor"),
                                  _("The cursor fades the higher the combo, becoming invisible at 50."),
                                  &cv::mod_fadingcursor);
    this->addExperimentalCheckbox(_("First Person"), _("Centered cursor."), &cv::mod_fps);
    this->addExperimentalCheckbox(_("Precise sliders"), _("Massively reduced slider follow circle radius."),
                                  &cv::mod_jigsaw2);
    this->addExperimentalCheckbox(_("Reverse Sliders"),
                                  _("Reverses the direction of all sliders. (Reload beatmap to apply!)"),
                                  &cv::mod_reverse_sliders);
    this->addExperimentalCheckbox(_("No 50s"), _("Only 300s or 100s. Try harder."), &cv::mod_no50s);
    this->addExperimentalCheckbox(_("No 100s no 50s"), _("300 or miss. PF \"lite\""), &cv::mod_no100s);
    this->addExperimentalCheckbox(_("MinG3012"), _("No 100s. Only 300s or 50s. Git gud."), &cv::mod_ming3012);
    this->addExperimentalCheckbox(_("Half Timing Window"),
                                  _("The hit timing window is cut in half. Hit early or perfect (300)."),
                                  &cv::mod_halfwindow);
    this->addExperimentalCheckbox(_("MillhioreF"), _("Go below AR 0. Doubled approach time."), &cv::mod_millhioref);
    this->addExperimentalCheckbox(
        _("Mafham"),
        _("Approach rate is set to negative infinity. See the entire beatmap at once.\nUses very "
          "aggressive optimizations to keep the framerate high, you have been warned!"),
        &cv::mod_mafham);
    this->addExperimentalCheckbox(_("Strict clicks"), _("Unnecessary clicks count as misses."), &cv::mod_jigsaw1);
    this->addExperimentalCheckbox(
        _("Strict Tracking"),
        _("Leaving sliders in any way counts as a miss and combo break. (Reload beatmap to apply!)"),
        &cv::mod_strict_tracking);

    this->nonSubmittableWarning = new CBaseUILabel();
    this->nonSubmittableWarning
        ->setDrawFrame(false)                                                                         //
        ->setDrawBackground(false)                                                                    //
        ->setText(_("WARNING: Score submission will be disabled due to non-vanilla mod selection."))  //
        ->setTextColor(0xffff0000)                                                                    //
        ->setTextJustification(TEXT_JUSTIFICATION::CENTERED)                                          //
        ->setVisible(false);                                                                          //
    this->addBaseUIElement(this->nonSubmittableWarning);

    // build score multiplier label
    this->scoreMultiplierLabel = new CBaseUILabel();
    this->scoreMultiplierLabel                                 //
        ->setDrawFrame(false)                                  //
        ->setDrawBackground(false)                             //
        ->setTextJustification(TEXT_JUSTIFICATION::CENTERED);  //
    this->addBaseUIElement(this->scoreMultiplierLabel);

    // build action buttons
    this->resetModsButton = this->addActionButton(_("1. Reset All Mods"));
    this->resetModsButton->setColor(0xffc62b00);
    this->resetModsButton->setClickCallback(SA::MakeDelegate<&ModSelector::resetModsUserInitiated>(this));
    this->closeButton = this->addActionButton(_("2. Close"));
    this->closeButton->setClickCallback(SA::MakeDelegate<&ModSelector::close>(this));
    this->closeButton->setColor(0xff636363);

    this->updateButtons(true);
    this->updateLayout();
}

void ModSelector::updateButtons(bool initial) {
    using SkinImageGetter = UIModSelectorModButton::SkinImageGetter;
    static auto setGridModbtn = [](UIModSelectorModButton *modButton, int state, bool initialState, ConVar *modCvar,
                                   std::string modName, std::string_view tooltipText,
                                   SkinImageGetter skinImageGetter) -> UIModSelectorModButton * {
        if(modButton != nullptr) {
            modButton->setState(state, initialState, modCvar, std::move(modName), tooltipText,
                                std::move(skinImageGetter));
            modButton->setVisible(true);
        }

        return modButton;
    };

#define MKIMGGETR(sipmr) SA::MakeDelegate([](const Skin *skin) -> const SkinImage * { return &(skin->*&Skin::sipmr); })

    this->modButtonEZ = setGridModbtn(
        this->getGridButton(EZ_POS), 0, initial && osu->getModEZ(), &cv::mod_easy, "ez",
        _("Reduces overall difficulty - larger circles, more forgiving HP drain, less accuracy required."),
        MKIMGGETR(i_modselect_ez));
    this->modButtonNF =
        setGridModbtn(this->getGridButton(NF_POS), 0, initial && osu->getModNF(), &cv::mod_nofail, "nf",
                      _("You can't fail. No matter what.\nNOTE: To disable drain completely:\nOptions > Gameplay > "
                        "Mechanics > \"Disable HP Drain\"."),
                      MKIMGGETR(i_modselect_nf));

    this->modButtonHR = setGridModbtn(this->getGridButton(HR_POS), 0, initial && osu->getModHR(), &cv::mod_hardrock,
                                      "hr", _("Everything just got a bit harder..."), MKIMGGETR(i_modselect_hr));
    this->modButtonSDPF =
        setGridModbtn(this->getGridButton(SDPF_POS), 0, initial && osu->getModSD(), &cv::mod_suddendeath, "sd",
                      _("Miss a note and fail."), MKIMGGETR(i_modselect_sd));
    setGridModbtn(this->getGridButton(SDPF_POS), 1, initial && osu->getModSS(), &cv::mod_perfect, "ss",
                  _("SS or quit."), MKIMGGETR(i_modselect_pf));

    {
        const bool nce = cv::nightcore_enjoyer.getBool();
        // clang-format off
        // TRANSLATORS: "A E S T H E T I C" is a vaporwave aesthetic meme. This is a joke tooltip for Half Time when Nightcore enjoyer mode is on.
        std::string_view HTTooltip     = nce ? _("A E S T H E T I C") : _("Less zoom.");
        std::string_view HTName        = nce ? "dc"                : "ht";
        const SkinImageGetter HTMember = nce ?
                                         MKIMGGETR(i_modselect_dc) :
                                         MKIMGGETR(i_modselect_ht);

        // TRANSLATORS: "uguuuuuuuu" is an anime-style meme sound/expression. This is a joke tooltip for Double Time when Nightcore enjoyer mode is on.
        std::string_view DTTooltip     = nce ? _("uguuuuuuuu")     : _("Zoooooooooom.");
        std::string_view DTName        = nce ? "nc"             : "dt";
        const SkinImageGetter DTMember = nce ?
                                         MKIMGGETR(i_modselect_nc) :
                                         MKIMGGETR(i_modselect_dt);
        // clang-format on
        this->modButtonHT = setGridModbtn(this->getGridButton(HT_POS), 0, initial && cv::mod_halftime_dummy.getBool(),
                                          &cv::mod_halftime_dummy, std::string{HTName}, HTTooltip, HTMember);
        this->modButtonDT = setGridModbtn(this->getGridButton(DT_POS), 0, initial && cv::mod_doubletime_dummy.getBool(),
                                          &cv::mod_doubletime_dummy, std::string{DTName}, DTTooltip, DTMember);
    }

    this->modButtonHD = setGridModbtn(this->getGridButton(HD_POS), 0, initial && osu->getModHD(), &cv::mod_hidden, "hd",
                                      _("Play with no approach circles and fading notes for a slight score advantage."),
                                      MKIMGGETR(i_modselect_hd));

    this->modButtonFL = setGridModbtn(this->getGridButton(FL_POS), 0, initial && osu->getModFlashlight(),
                                      &cv::mod_flashlight, "fl", _("Restricted view area."), MKIMGGETR(i_modselect_fl));
    setGridModbtn(this->getGridButton(FL_POS), 1, initial && cv::mod_actual_flashlight.getBool(),
                  &cv::mod_actual_flashlight, "afl", _("Actual flashlight."), MKIMGGETR(i_modselect_fl));

    this->modButtonRX =
        setGridModbtn(this->getGridButton(RX_POS), 0, initial && osu->getModRelax(), &cv::mod_relax, "relax",
                      _("You don't need to click.\nGive your clicking/tapping fingers a break from the heat of "
                        "things.\n** UNRANKED **"),
                      MKIMGGETR(i_modselect_rx));
    this->modButtonAP = setGridModbtn(
        this->getGridButton(AP_POS), 0, initial && osu->getModAutopilot(), &cv::mod_autopilot, "autopilot",
        _("Automatic cursor movement - just follow the rhythm.\n** UNRANKED **"), MKIMGGETR(i_modselect_ap));
    this->modButtonSO =
        setGridModbtn(this->getGridButton(SO_POS), 0, initial && osu->getModSpunout(), &cv::mod_spunout, "spunout",
                      _("Spinners will be automatically completed."), MKIMGGETR(i_modselect_so));
    this->modButtonAUTO =
        setGridModbtn(this->getGridButton(AUTO_POS), 0, initial && osu->getModAuto(), &cv::mod_autoplay, "auto",
                      _("Watch a perfect automated play through the song."), MKIMGGETR(i_modselect_auto));
    this->modButtonTGT = setGridModbtn(
        this->getGridButton(TGT_POS), 0, initial && osu->getModTarget(), &cv::mod_target, "practicetarget",
        _("Accuracy is based on the distance to the center of all hitobjects.\n300s still require at "
          "least being in the hit window of a 100 in addition to the rule above."),
        MKIMGGETR(i_modselect_target));
    this->modButtonSV2 =
        setGridModbtn(this->getGridButton(SV2_POS), 0, initial && osu->getModScorev2(), &cv::mod_scorev2, "v2",
                      _("Try the future scoring system.\n** UNRANKED **"), MKIMGGETR(i_modselect_sv2));

    const bool isMulti = BanchoState::is_in_a_multi_room();
    const bool isHostEquivalent = !isMulti || BanchoState::room.is_host();

    // Only enable these in singleplayer or if we're the host of a multi lobby
    this->modButtonHT->setAvailable(isHostEquivalent);
    this->modButtonDT->setAvailable(isHostEquivalent);
    this->modButtonTGT->setAvailable(isHostEquivalent);

    // Only enable these in singleplayer
    this->modButtonSV2->setAvailable(!isMulti);  // we use win condition instead in multi
    this->modButtonAUTO->setAvailable(!isMulti);

#undef MKIMGGETR
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

    this->scoreMultiplierLabel->setText(fmt::format(fmt::runtime(_("Score Multiplier: {:.2f}X")), scoreMultiplier));
}

void ModSelector::updateExperimentalButtons() {
    for(const auto &experimentalMod : this->experimentalMods) {
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
        for(auto *button : this->modButtons) {
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
        for(const auto &overrideSlider : this->overrideSliders) {
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
        for(auto *button : this->modButtons) {
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

            static std::string_view experimentalText = _("Experimental Mods");
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
        const auto &expCont = this->experimentalContainer;
        const McRect expContRect = expCont->getRect();
        g->pushTransform();
        {
            g->translate(experimentalModsAnimationTranslation, 0);
            g->setColor(backgroundColor);
            g->fillRect(expContRect.getPos().x - margin,                                      //
                        expContRect.getPos().y - margin,                                      //
                        expContRect.getSize().x + 2 * margin * this->fExperimentalAnimation,  //
                        expContRect.getSize().y + 2 * margin);                                //
            expCont->draw();
        }
        g->popTransform();
    }
}

void ModSelector::update(CBaseUIEventCtx &c) {
    // HACKHACK: updating while invisible is stupid, but the only quick solution for still animating otherwise stuck
    // sliders while closed
    if(!this->bVisible) {
        for(const auto &overrideSlider : this->overrideSliders) {
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
            TooltipOverlay *ttoverlay = ui->getTooltipOverlay();
            ttoverlay->begin();
            for(const auto &cvar : nonSubmittableCvars) {
                ttoverlay->addLine(cvar->getName());
            }
            ttoverlay->end();
        }
    }

    if(!BanchoState::is_in_a_multi_room()) {
        this->overrideSliderContainer->update(c);

        // override slider tooltips (ALT)
        if(this->bShowOverrideSliderALTHint) {
            for(const auto &overrideSlider : this->overrideSliders) {
                if(overrideSlider.slider->isBusy()) {
                    TooltipOverlay *ttoverlay = ui->getTooltipOverlay();
                    ttoverlay->begin();
                    {
                        ttoverlay->addLine(_("Hold [ALT] to slide in 0.01 increments."));
                    }
                    ttoverlay->end();

                    if(keyboard->isAltDown()) this->bShowOverrideSliderALTHint = false;
                }
            }
        }

        // handle experimental mods visibility
        bool experimentalModEnabled = false;
        for(const auto &experimentalMod : this->experimentalMods) {
            const auto *checkboxPointer = dynamic_cast<CBaseUICheckbox *>(experimentalMod.element);
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
                this->fExperimentalAnimation.set(1.0f, (1.0f - this->fExperimentalAnimation) * 0.11f, anim::QuadOut);
            }
        } else if(this->bExperimentalVisible && !this->experimentalContainer->isMouseInside() &&
                  !this->experimentalContainer->isActive() && !experimentalModEnabled) {
            this->bExperimentalVisible = false;
            this->fExperimentalAnimation.set(0.0f, this->fExperimentalAnimation * 0.11f, anim::QuadIn);
        }
    }

    // delayed onModUpdate() triggers when changing some values
    {
        // this logic is hard to follow but it's what was here before
        static const auto modUpdateFunc = [](CBaseUISlider *slider, bool &waitForFinished) {
            if(slider != nullptr && (slider->isActive() || slider->hasChanged())) {
                waitForFinished = true;
            } else if(waitForFinished) {
                return true;
            }
            return false;
        };

        // handle dynamic CS and slider vertex buffer updates
        // handle dynamic live pp calculation updates (when CS or Speed/BPM changes)
        // handle dynamic HP drain updates
        bool doneWaiting = false;
        for(auto [slider, boolean] : std::array{std::pair{this->CSSlider, &this->bWaitForCSChangeFinished},
                                                std::pair{this->speedSlider, &this->bWaitForSpeedChangeFinished},
                                                std::pair{this->HPSlider, &this->bWaitForHPChangeFinished}}) {
            if(modUpdateFunc(slider, *boolean)) {
                this->bWaitForCSChangeFinished = this->bWaitForSpeedChangeFinished = this->bWaitForHPChangeFinished =
                    false;
                doneWaiting = true;
            }
        }

        if(doneWaiting && osu->isInPlayMode()) {
            osu->getMapInterface()->onModUpdate();
        }
    }
}

void ModSelector::onKeyDown(KeyboardEvent &key) {
    UIScreen::onKeyDown(key);  // only used for options menu
    if(!this->bVisible || key.isConsumed()) return;

    this->overrideSliderContainer->onKeyDown(key);
    if(key.isConsumed()) return;

    const SCANCODE scanCode = key.getScanCode();

    const bool forceClose = scanCode == binds::GAME_PAUSE || scanCode == KEY_ESCAPE;
    if(forceClose ||                           //
       scanCode == KEY_F1 ||                   //
       scanCode == binds::TOGGLE_MODSELECT ||  //
       scanCode == KEY_2 ||                    //
       scanCode == KEY_ENTER || scanCode == KEY_NUMPAD_ENTER) {
        this->close(forceClose);
    } else if(scanCode == KEY_1) {
        this->resetModsUserInitiated();
    } else {
        // mod hotkeys
        // clang-format off
        if(scanCode == binds::MOD_EASY) this->modButtonEZ->click();
        else if(scanCode == binds::MOD_NOFAIL) this->modButtonNF->click();
        else if(scanCode == binds::MOD_HARDROCK) this->modButtonHR->click();
        else if(scanCode == binds::MOD_SUDDENDEATH) this->modButtonSDPF->click();
        else if(scanCode == binds::MOD_HIDDEN) this->modButtonHD->click();
        else if(scanCode == binds::MOD_FLASHLIGHT) this->modButtonFL->click();
        else if(scanCode == binds::MOD_RELAX) this->modButtonRX->click();
        else if(scanCode == binds::MOD_AUTOPILOT) this->modButtonAP->click();
        else if(scanCode == binds::MOD_SPUNOUT) this->modButtonSO->click();
        else if(scanCode == binds::MOD_AUTO) this->modButtonAUTO->click();
        else if(scanCode == binds::MOD_SCOREV2) this->modButtonSV2->click();
        else if(scanCode == binds::MOD_HALFTIME) this->modButtonHT->click();
        else if(scanCode == binds::MOD_DOUBLETIME) this->modButtonDT->click();
        // clang-format on
    }
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
        this->fAnimation.set(1.0f, 0.1f, anim::QuadOut);

        bool experimentalModEnabled = false;
        for(const auto &experimentalMod : this->experimentalMods) {
            const auto *checkboxPointer = dynamic_cast<CBaseUICheckbox *>(experimentalMod.element);
            if(checkboxPointer != nullptr && checkboxPointer->isChecked()) {
                experimentalModEnabled = true;
                break;
            }
        }
        if(experimentalModEnabled) {
            this->bExperimentalVisible = true;
            if(this->isInCompactMode())
                this->fExperimentalAnimation.set(1.0f, (1.0f - this->fExperimentalAnimation) * 0.06f, anim::QuadOut);
            else {
                this->fExperimentalAnimation.stop();
                this->fExperimentalAnimation = 1.0f;
            }
        } else {
            this->bExperimentalVisible = false;
            this->fExperimentalAnimation.stop();
            this->fExperimentalAnimation = 0.0f;
        }
    } else if(!visible && this->bVisible) {
        this->bScheduledHide = this->isInCompactMode();

        this->fAnimation = 1.0f;
        this->fAnimation.set(0.0f, 0.06f, anim::QuadIn);
        this->updateScoreMultiplierLabelText();
        this->updateOverrideSliderLabels();

        this->bExperimentalVisible = false;
        this->fExperimentalAnimation.set(0.0f, 0.06f, anim::QuadIn);
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
    if(this->modButtons[0] == nullptr || this->overrideSliders.size() < 1) return;

    const float dpiScale = Osu::getUIScale();
    const float uiScale = Osu::getRawUIScale();

    if(!this->isInCompactMode())  // normal layout
    {
        // mod grid buttons
        vec2 center = osu->getVirtScreenSize() / 2.0f;
        vec2 size = osu->getSkin()->i_modselect_ez.getSizeBase() * uiScale;
        vec2 offset = vec2(size.x * 1.0f, size.y * 0.33f);
        vec2 start = vec2(center.x - (size.x * GRID_WIDTH) / 2.0f - (offset.x * (GRID_WIDTH - 1)) / 2.0f,
                          center.y - (size.y * GRID_HEIGHT) / 2.0f - (offset.y * (GRID_HEIGHT - 1)) / 2.0f);

        for(int x = 0; x < GRID_WIDTH; x++) {
            for(int y = 0; y < GRID_HEIGHT; y++) {
                UIModSelectorModButton *button = this->getGridButton({x, y});

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
        float actionMinY =
            start.y + size.y * GRID_HEIGHT + offset.y * (GRID_HEIGHT - 1);  // exact bottom of the mod buttons
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
        const float modGridMaxY =
            start.y + size.y * GRID_HEIGHT + offset.y * (GRID_HEIGHT - 1);  // exact bottom of the mod buttons
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
        vec2 blockSize = osu->getSkin()->i_modselect_ez.getSizeBase() * uiScale;
        vec2 offset = vec2(blockSize.x * 0.15f, blockSize.y * 0.05f);
        vec2 size = vec2((blockSize.x * GRID_WIDTH) + (offset.x * (GRID_WIDTH - 1)),
                         (blockSize.y * GRID_HEIGHT) + (offset.y * (GRID_HEIGHT - 1)));
        center.y = osu->getVirtScreenHeight() - size.y / 2 - offset.y * 3.0f;
        vec2 start = vec2(center.x - size.x / 2.0f, center.y - size.y / 2.0f);

        for(int x = 0; x < GRID_WIDTH; x++) {
            for(int y = 0; y < GRID_HEIGHT; y++) {
                UIModSelectorModButton *button = this->getGridButton({x, y});

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

    auto &expCont = this->experimentalContainer;
    expCont
        ->setSizeX(experimentalMaxWidth + 25 * dpiScale /*, yCounter*/)  //
        ->setPosY(-1);                                                   //
    expCont
        ->setScrollSizeToContent(1 * dpiScale)  //
        ->container.update_pos();               //
    expCont->setVisible(!BanchoState::is_in_a_multi_room());
}

ModSelector::OVERRIDE_SLIDER ModSelector::addOverrideSlider(const std::string &text, const std::string &labelText,
                                                            ConVar *cvar, float min, float max,
                                                            const std::string &tooltipText, ConVar *lockCvar) {
    const float height = 25;

    OVERRIDE_SLIDER os;
    if(lockCvar != nullptr) {
        os.lock = new ModSelectorOverrideSliderLockButton(0.f, 0.f, height, height, "", "");
        os.lock->setChangeCallback(SA::MakeDelegate<&ModSelector::onOverrideSliderLockChange>(this));
    }
    os.desc = new ModSelectorOverrideSliderDescButton(0.f, 0.f, 100.f, height, "", text);
    os.desc->setTooltipText(tooltipText);
    os.slider = new UISlider(0.f, 0.f, 100.f, height, "");
    os.label = new CBaseUILabel(0.f, 0.f, 100.f, height, labelText, labelText);
    os.cvar = cvar;
    os.lockCvar = lockCvar;

    const bool debugDrawFrame = false;
    constexpr Color color = rgb(119, 119, 119);

    os.slider                               //
        ->setDrawFrame(debugDrawFrame)      //
        ->setDrawBackground(false)          //
        ->setFrameColor(color);             //
    os.desc                                 //
        ->setDrawFrame(debugDrawFrame)      //
        ->setDrawBackground(false)          //
        ->setTextColor(color)               //
        ->setEnabled(lockCvar != nullptr);  //
    os.label                                //
        ->setDrawFrame(debugDrawFrame)      //
        ->setDrawBackground(false)          //
        ->setTextColor(color);              //
    os.slider                               //
        ->setBounds(min, max + 1.0f)        //
        ->setKeyDelta(0.1f)                 //
        ->setLiveUpdate(true)               //
        ->setAllowMouseWheel(false);        //

    if(os.cvar != nullptr) os.slider->setValue(os.cvar->getFloat() + 1.0f, false);

    this->overrideSliderContainer->addBaseUIElements(
        std::array<CBaseUIElement *const, 4>{os.lock, os.desc, os.slider, os.label});
    this->overrideSliders.push_back(os);

    return os;
}

UIButton *ModSelector::addActionButton(const std::string &text) {
    auto *actionButton = new UIButton(50, 50, 100, 100, text, text);
    this->actionButtons.push_back(actionButton);
    this->addBaseUIElement(actionButton);

    return actionButton;
}

CBaseUILabel *ModSelector::addExperimentalLabel(const std::string &text) {
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

UICheckbox *ModSelector::addExperimentalCheckbox(const std::string &text, const std::string &tooltipText,
                                                 ConVar *cvar) {
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

    for(const auto &overrideSlider : this->overrideSliders) {
        if(overrideSlider.lock != nullptr) overrideSlider.lock->setChecked(false);
    }

    for(const auto &overrideSlider : this->overrideSliders) {
        // HACKHACK: force small delta to force an update (otherwise values could get stuck, e.g. for "Use Mods" context
        // menu)
        // HACKHACK: only animate while visible to workaround "Use mods" bug (if custom speed multiplier already
        // set and then "Use mods" with different custom speed multiplier would reset to 1.0x because of anim)
        overrideSlider.slider->setValue(overrideSlider.slider->getMin() + 0.0001f, this->bVisible);
        overrideSlider.slider->setValue(overrideSlider.slider->getMin(), this->bVisible);
    }

    for(auto *modButton : this->modButtons) {
        modButton->resetState();
    }

    for(const auto &experimentalMod : this->experimentalMods) {
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
        this->modButtonSDPF->setState(1);
        this->modButtonSDPF->setOn(true, true);
    } else if(flags::has<LegacyFlags::SuddenDeath>(flags)) {
        this->modButtonSDPF->setState(0);
        this->modButtonSDPF->setOn(true, true);
    }

    this->modButtonNF->setOn(flags::has<LegacyFlags::NoFail>(flags), true);
    this->modButtonEZ->setOn(flags::has<LegacyFlags::Easy>(flags), true);
    this->modButtonHD->setOn(flags::has<LegacyFlags::Hidden>(flags), true);
    this->modButtonHR->setOn(flags::has<LegacyFlags::HardRock>(flags), true);
    this->modButtonRX->setOn(flags::has<LegacyFlags::Relax>(flags), true);
    this->modButtonSO->setOn(flags::has<LegacyFlags::SpunOut>(flags), true);
    this->modButtonAP->setOn(flags::has<LegacyFlags::Autopilot>(flags), true);
    this->modButtonTGT->setOn(flags::has<LegacyFlags::Target>(flags), true);
    this->modButtonFL->setOn(flags::has<LegacyFlags::Flashlight>(flags), true);
    this->modButtonSV2->setOn(flags::has<LegacyFlags::ScoreV2>(flags), true);

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
    const auto *mapIface = osu->getMapInterface();
    const BeatmapDifficulty *beatmap = mapIface->getBeatmap();

    for(const auto &overrideSlider : this->overrideSliders) {
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
            if(beatmap) {
                if(overrideSlider.label->getName().find("BPM") != std::string_view::npos) {
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
            if(overrideSlider.label->getName().find("BPM") != std::string_view::npos) {
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
                if(beatmap) {
                    const float newAR = mapIface->getConstantApproachRateForSpeedMultiplier();
                    const float newOD = mapIface->getConstantOverallDifficultyForSpeedMultiplier();

                    // '+1' to compensate for turn-off area of the override sliders
                    this->ARSlider->setValue(newAR + 1.0f, false);
                    this->ODSlider->setValue(newOD + 1.0f, false);
                }
            }
        }

        // update convar with final value (e.g. cv::ar_override, etc.)
        overrideSlider.cvar->setValue(sliderValue);

        // HACKHACK: since updateOverrideSliderLabels depends on mods currently applied to beatmap, we need to apply them from cvars here
        osu->updateMods();

        this->updateOverrideSliderLabels();

        break;
    }
}

void ModSelector::onOverrideSliderLockChange(CBaseUICheckbox *checkbox) {
    const auto *mapIface = osu->getMapInterface();

    for(const auto &overrideSlider : this->overrideSliders) {
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
                            mapIface->getRawAR() + 1.0f,
                            false);  // '+1' to compensate for turn-off area of the override sliders
                } else if(checkbox == this->ODLock) {
                    if(this->ODSlider->getFloat() < 1.0f)
                        this->ODSlider->setValue(
                            mapIface->getRawOD() + 1.0f,
                            false);  // '+1' to compensate for turn-off area of the override sliders
                }
            }

            // HACKHACK: since updateOverrideSliderLabels depends on mods currently applied to beatmap, we need to apply them from cvars here
            osu->updateMods();

            this->updateOverrideSliderLabels();

            break;
        }
    }
}

void ModSelector::onOverrideARSliderDescClicked(CBaseUIButton * /*button*/) { this->ARLock->click(); }

void ModSelector::onOverrideODSliderDescClicked(CBaseUIButton * /*button*/) { this->ODLock->click(); }

void ModSelector::updateOverrideSliderLabels() {
    const Color inactiveColor = 0xff777777;
    const Color activeColor = 0xffffffff;
    const Color inactiveLabelColor = 0xff1166ff;

    for(const auto &overrideSlider : this->overrideSliders) {
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

std::string ModSelector::getOverrideSliderLabelText(const ModSelector::OVERRIDE_SLIDER &s, bool active) {
    float convarValue = s.cvar->getFloat();
    const auto *mapIface = osu->getMapInterface();

    std::string newLabelText{s.label->getName()};
    if(const BeatmapDifficulty *beatmap = mapIface->getBeatmap()) {
        // for relevant values (AR/OD), any non-1.0x speed multiplier should show the fractional parts caused by such a
        // speed multiplier (same for non-1.0x difficulty multiplier)
        const bool forceDisplayTwoDecimalDigits =
            (mapIface->getSpeedMultiplier() != 1.0f || Osu::getDifficultyMultiplier() != 1.0f ||
             Osu::getCSDifficultyMultiplier() != 1.0f);

        // HACKHACK: dirty
        bool wasSpeedSlider = false;
        float beatmapValue = 1.0f;
        if(s.label->getName().find("CS") != std::string_view::npos) {
            beatmapValue = std::clamp<float>(beatmap->getCS() * Osu::getCSDifficultyMultiplier(), 0.0f, 10.0f);
            convarValue = mapIface->getCS();
        } else if(s.label->getName().find("AR") != std::string_view::npos) {
            beatmapValue =
                active ? mapIface->getRawARForSpeedMultiplier() : mapIface->getApproachRateForSpeedMultiplier();

            // compensate and round
            convarValue = mapIface->getApproachRateForSpeedMultiplier();
            if(!keyboard->isAltDown() && !forceDisplayTwoDecimalDigits)
                convarValue = std::round(convarValue * 10.0f) / 10.0f;
            else
                convarValue = std::round(convarValue * 100.0f) / 100.0f;
        } else if(s.label->getName().find("OD") != std::string_view::npos) {
            beatmapValue =
                active ? mapIface->getRawODForSpeedMultiplier() : mapIface->getOverallDifficultyForSpeedMultiplier();

            // compensate and round
            convarValue = mapIface->getOverallDifficultyForSpeedMultiplier();
            if(!keyboard->isAltDown() && !forceDisplayTwoDecimalDigits)
                convarValue = std::round(convarValue * 10.0f) / 10.0f;
            else
                convarValue = std::round(convarValue * 100.0f) / 100.0f;
        } else if(s.label->getName().find("HP") != std::string_view::npos) {
            beatmapValue = std::clamp<float>(beatmap->getHP() * Osu::getDifficultyMultiplier(), 0.0f, 10.0f);
            convarValue = mapIface->getHP();
        } else if(s.desc->getText().find("Speed") != std::string_view::npos) {
            beatmapValue = active ? 1.f : mapIface->getSpeedMultiplier();

            wasSpeedSlider = true;
            {
                {
                    if(!active)
                        newLabelText.append(fmt::format(" {:.4g}", beatmapValue));
                    else
                        newLabelText.append(fmt::format(" {:.4g} -> {:.4g}", beatmapValue, convarValue));
                }

                newLabelText.append("  (BPM: ");

                int minBPM = beatmap->getMinBPM();
                int maxBPM = beatmap->getMaxBPM();
                int mostCommonBPM = beatmap->getMostCommonBPM();
                int newMinBPM = minBPM * mapIface->getSpeedMultiplier();
                int newMaxBPM = maxBPM * mapIface->getSpeedMultiplier();
                int newMostCommonBPM = mostCommonBPM * mapIface->getSpeedMultiplier();
                if(!active || mapIface->getSpeedMultiplier() == 1.0f) {
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
        if(s.desc->getText().find("Speed") == std::string_view::npos) {
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
    if(!this->modButtonAUTO->isOn()) this->modButtonAUTO->click();
}

void ModSelector::toggleAuto() { this->modButtonAUTO->click(); }

void ModSelector::onCheckboxChange(CBaseUICheckbox *checkbox) {
    for(const auto &experimentalMod : this->experimentalMods) {
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
