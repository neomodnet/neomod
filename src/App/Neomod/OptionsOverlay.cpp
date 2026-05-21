// Copyright (c) 2016, PG & 2025-2026 WH, All rights reserved.
#include "OptionsOverlay.h"

#include "OsuConVars.h"

#include "UniString.h"
#include "crypto.h"
#include "ConVarHandler.h"
#include "CBaseUICheckbox.h"
#include "CBaseUIContainer.h"
#include "CBaseUIScrollView.h"
#include "CBaseUITextbox.h"
#include "Chat.h"
#include "Database.h"
#include "Engine.h"
#include "Environment.h"
#include "File.h"
#include "HitObjects.h"
#include "HUD.h"
#include "Font.h"
#include "RenderTarget.h"
#include "KeyBindings.h"
#include "OsuKeyBinds.h"
#include "BeatmapInterface.h"
#include "MainMenu.h"
#include "ModSelector.h"
#include "Osu.h"
#include "Skin.h"
#include "SongBrowser/SongBrowser.h"
#include "UI.h"
#include "UIButton.h"
#include "UIContextMenu.h"
#include "UISearchOverlay.h"
#include "UISlider.h"
#include "UIBackButton.h"
#include "UILabel.h"

#include "VolNormalization.h"
#include "NetworkHandler.h"
#include "Bancho.h"
#include "Mouse.h"
#include "Keyboard.h"
#include "Icons.h"
#include "Logging.h"
#include "SliderRenderer.h"
#include "VertexArrayObject.h"
#include "AnimationHandler.h"
#include "DiscordInterface.h"
#include "SettingsImporter.h"
#include "UICheckbox.h"
#include "GameRules.h"
#include "SString.h"
#include "TooltipOverlay.h"
#include "AsyncIOHandler.h"
#include "AsyncPool.h"
#include "Parsing.h"

#include "Graphics.h"
#include "Sound.h"
#include "SoundEngine.h"
#if defined(MCENGINE_PLATFORM_WINDOWS) && defined(MCENGINE_FEATURE_BASS)
#include "BassManager.h"
#include "BassSoundEngine.h"  // for ASIO-specific stuff
#endif

#include "fmt/chrono.h"

#include <cwctype>
#include <algorithm>
#include <atomic>
#include <utility>

#include <atomic>
#include <utility>

namespace {
class SliderPreviewElement;
class SkinPreviewElement;
class CategoryButton;
class KeyBindButton;
class ResetButton;

enum class ElementType : int8_t {
    SPCR = 0,       // spacer
    SECT = 1,       // section
    SUBSECT = 2,    // sub-section
    LABEL = 3,      // ...
    BTN = 4,        // button
    BINDBTN = 5,    // keybind button
    CBX = 6,        // checkbox
    SLDR = 7,       // slider
    TBX = 8,        // textbox
    SKNPRVW = 9,    // skin preview
    SLDRPRVW = 10,  // slider preview
    CBX_BTN = 11    // checkbox+button
};
using enum ElementType;
struct OptionsElement;

const int NB_LANGUAGES = 4;
const std::array<std::pair<std::string_view, std::string_view>, NB_LANGUAGES> LANGUAGES{{
    {"en"sv, "English"sv},
    {"ru"sv, "Русский"sv},
    {"pl"sv, "Polski"sv},
    {"fr"sv, "Français"sv},
    // Make sure to update NB_LANGUAGES above if you add one!
}};
}  // namespace

struct OptionsOverlayImpl final {
    NOCOPY_NOMOVE(OptionsOverlayImpl)
   public:
    OptionsOverlayImpl() = delete;
    OptionsOverlayImpl(OptionsOverlay *parent);
    ~OptionsOverlayImpl();

    void draw();
    void update(CBaseUIEventCtx &c);
    void onKeyDown(KeyboardEvent &e);
    void onChar(KeyboardEvent &e);
    void onResolutionChange(vec2 newResolution);
    void onKey(KeyboardEvent &e);
    CBaseUIContainer *setVisible(bool visible);

    void save();

    void openAndScrollToSkinSection();
    void setUsername(std::string username);
    bool isMouseInside();
    bool isBusy();

    void scheduleLayoutUpdate();
    void onSkinSelectOpened();
    void onOutputDeviceChange();
    void updateOsuFolderTextbox(std::string_view newFolder);
    void askForLoginDetails();
    void update_login_button(bool loggedIn = false);
    void updateSkinNameLabel();
    UIContextMenu *getContextMenu();

    void updateLayout();
    void onBack();

   private:
    void onSkinSelectFoldersFinished(const std::vector<std::string> &skinFolders);

    void setVisibleInt(bool visible, bool fromOnBack = false);
    void scheduleSearchUpdate();

    void updateFposuDPI();
    void updateFposuCMper360();
    void updateNotelockSelectLabel();

    // options
    void onLanguageSelect();
    void onLanguageSelected(std::string_view newLanguage, int id = -1);
    void onFullscreenChange(CBaseUICheckbox *checkbox);
    void onDPIScalingChange(CBaseUICheckbox *checkbox);
    void openCurrentSkinFolder();
    void onSkinSelect2(std::string_view skinName, int id = -1);
    void onResolutionSelect();
    void onResolutionSelect2(std::string_view resolution, int id = -1);
    void onOutputDeviceResetUpdate();
    void onOutputDeviceSelect();
    void onOutputDeviceSelect2(std::string_view outputDeviceName, int id = -1);
    void onLogInClicked(bool left, bool right);
    void onNotelockSelect();
    void onNotelockSelect2(std::string_view notelockType, int id = -1);
    void onNotelockSelectResetClicked();
    void onNotelockSelectResetUpdate();

    void onCheckboxChange(CBaseUICheckbox *checkbox);
    void onSliderChange(CBaseUISlider *slider);
    void onSliderChangeOneDecimalPlace(CBaseUISlider *slider);
    void onSliderChangeTwoDecimalPlaces(CBaseUISlider *slider);
    void onSliderChangeOneDecimalPlaceMeters(CBaseUISlider *slider);
    void onSliderChangeInt(CBaseUISlider *slider);
    void onSliderChangeIntMS(CBaseUISlider *slider);
    void onSliderChangeFloatMS(CBaseUISlider *slider);
    void onSliderChangePercent(CBaseUISlider *slider);
    void onFPSSliderChange(CBaseUISlider *slider);

    void onKeyBindingButtonPressed(CBaseUIButton *button);
    void onKeyUnbindButtonPressed(CBaseUIButton *button);
    void onKeyBindingsResetAllPressed(CBaseUIButton *button);
    void onSliderChangeSliderQuality(CBaseUISlider *slider);
    void onSliderChangeLetterboxingOffset(CBaseUISlider *slider);
    void onSliderChangeUIScale(CBaseUISlider *slider);

    void setupASIOClampedChangeCallback();
    void OpenASIOSettings();
    void onASIOBufferChange(CBaseUISlider *slider);
    void onWASAPIBufferChange(CBaseUISlider *slider);
    void onWASAPIPeriodChange(CBaseUISlider *slider);
    void onLoudnessNormalizationToggle(CBaseUICheckbox *checkbox);
    void onModChangingToggle(CBaseUICheckbox *checkbox);

    void onHighQualitySlidersCheckboxChange(CBaseUICheckbox *checkbox);
    void onHighQualitySlidersConVarChange(float newValue);

    // categories
    void onCategoryClicked(CategoryButton *button);

    // reset
    void onResetUpdate(ResetButton *button);
    void onResetClicked(ResetButton *button);
    void onResetEverythingClicked(CBaseUIButton *button);

    // server/skin-forced cvar locking
    void applyForcedCvarLocks();
    void pushForcedCvarTooltipIfHovered();

    // categories
    CategoryButton *addCategory(CBaseUIElement *section, char32_t icon);

    // elements
    void addSpacer();
    CBaseUILabel *addSection(const std::string &text);
    CBaseUILabel *addSubSection(const std::string &text, const std::string &searchTags = {});
    CBaseUILabel *addLabel(const std::string &text);
    UIButton *addButton(const std::string &text, ConVar *cvar = nullptr);
    OptionsElement *addButton(const std::string &text, const std::string &labelText, bool withResetButton = false,
                              ConVar *cvar = nullptr);
    OptionsElement *addButtonButton(const std::string &text1, const std::string &text2, ConVar *cvar = nullptr);
    OptionsElement *addButtonButtonLabel(const std::string &text1, const std::string &text2,
                                         const std::string &labelText, bool withResetButton = false,
                                         ConVar *cvar = nullptr);
    KeyBindButton *addKeyBindButton(const std::string &text, OsuKeyBinds::Bind *bind);
    CBaseUICheckbox *addCheckbox(const std::string &text, ConVar *cvar);
    CBaseUICheckbox *addCheckbox(const std::string &text, const std::string &tooltipText = {}, ConVar *cvar = nullptr);
    OptionsElement *addButtonCheckbox(const std::string &buttontext, const std::string &cbxtooltip);
    UISlider *addSlider(const std::string &text, float min = 0.0f, float max = 1.0f, ConVar *cvar = nullptr,
                        float label1Width = 0.0f, bool allowOverscale = false, bool allowUnderscale = false);
    CBaseUITextbox *addTextbox(const std::string &text, ConVar *cvar = nullptr);
    CBaseUITextbox *addTextbox(const std::string &text, const std::string &labelText, ConVar *cvar = nullptr);

    SkinPreviewElement *addSkinPreview();
    SliderPreviewElement *addSliderPreview();

    // vars
    CBaseUIScrollView *categories{nullptr};
    CBaseUIScrollView *options{nullptr};
    UIContextMenu *contextMenu{nullptr};
    UISearchOverlay *search{nullptr};
    CBaseUILabel *spacer{nullptr};
    CategoryButton *fposuCategoryButton{nullptr};
    std::vector<CategoryButton *> categoryButtons;
    std::vector<std::unique_ptr<OptionsElement>> elemContainers;
    Hash::flat::map<CBaseUIElement *, OptionsElement *> uiToOptElemMap;

    CBaseUICheckbox *fullscreenCheckbox{nullptr};
    CBaseUISlider *backgroundDimSlider{nullptr};
    CBaseUISlider *backgroundBrightnessSlider{nullptr};
    CBaseUISlider *hudSizeSlider{nullptr};
    CBaseUISlider *hudComboScaleSlider{nullptr};
    CBaseUISlider *hudScoreScaleSlider{nullptr};
    CBaseUISlider *hudAccuracyScaleSlider{nullptr};
    CBaseUISlider *hudHiterrorbarScaleSlider{nullptr};
    CBaseUISlider *hudHiterrorbarURScaleSlider{nullptr};
    CBaseUISlider *hudProgressbarScaleSlider{nullptr};
    CBaseUISlider *hudScoreBarScaleSlider{nullptr};
    CBaseUISlider *hudScoreBoardScaleSlider{nullptr};
    CBaseUISlider *hudInputoverlayScaleSlider{nullptr};
    CBaseUISlider *playfieldBorderSizeSlider{nullptr};
    CBaseUISlider *statisticsOverlayScaleSlider{nullptr};
    CBaseUISlider *statisticsOverlayXOffsetSlider{nullptr};
    CBaseUISlider *statisticsOverlayYOffsetSlider{nullptr};
    CBaseUISlider *cursorSizeSlider{nullptr};
    CBaseUILabel *skinLabel{nullptr};
    UIButton *skinSelectLocalButton{nullptr};
    CBaseUIButton *languageSelectButton{nullptr};
    CBaseUIButton *resolutionSelectButton{nullptr};
    CBaseUILabel *resolutionLabel{nullptr};
    CBaseUIButton *outputDeviceSelectButton{nullptr};
    CBaseUILabel *outputDeviceLabel{nullptr};
    ResetButton *outputDeviceResetButton{nullptr};
    CBaseUISlider *wasapiBufferSizeSlider{nullptr};
    CBaseUISlider *wasapiPeriodSizeSlider{nullptr};
    CBaseUISlider *asioBufferSizeSlider{nullptr};
    ResetButton *asioBufferSizeResetButton{nullptr};
    ResetButton *wasapiBufferSizeResetButton{nullptr};
    ResetButton *wasapiPeriodSizeResetButton{nullptr};
    CBaseUISlider *sliderQualitySlider{nullptr};
    CBaseUISlider *letterboxingOffsetXSlider{nullptr};
    CBaseUISlider *letterboxingOffsetYSlider{nullptr};
    ResetButton *letterboxingOffsetResetButton{nullptr};
    SliderPreviewElement *sliderPreviewElement{nullptr};
    CBaseUITextbox *dpiTextbox{nullptr};
    CBaseUITextbox *cm360Textbox{nullptr};
    CBaseUIElement *skinSection{nullptr};
    CBaseUISlider *uiScaleSlider{nullptr};
    ResetButton *uiScaleResetButton{nullptr};
    CBaseUIElement *notelockSelectButton{nullptr};
    CBaseUILabel *notelockSelectLabel{nullptr};
    ResetButton *notelockSelectResetButton{nullptr};

    CBaseUIElement *sectionGeneral{nullptr};
    CBaseUITextbox *serverTextbox{nullptr};
    CBaseUICheckbox *submitScoresCheckbox{nullptr};
    CBaseUITextbox *nameTextbox{nullptr};
    CBaseUITextbox *passwordTextbox{nullptr};
    UIButton *logInButton{nullptr};

    CBaseUITextbox *osuFolderTextbox{nullptr};

    ConVar *waitingKey{nullptr};

    // custom
    AnimFloat fAnimation;

    float fOsuFolderTextboxInvalidAnim{0.f};
    bool bLetterboxingOffsetUpdateScheduled{false};
    bool bUIScaleChangeScheduled{false};
    bool bUIScaleScrollToSliderScheduled{false};
    bool bDPIScalingScrollToSliderScheduled{false};
    bool bASIOBufferChangeScheduled{false};
    bool bWASAPIBufferChangeScheduled{false};
    bool bWASAPIPeriodChangeScheduled{false};
    std::atomic<bool> bLayoutUpdateScheduled{false};

    int iNumResetAllKeyBindingsPressed{0};
    int iNumResetEverythingPressed{0};

    // non-blocking skin folder enumerator (clicking select skin can lag spike with large folders otherwise)
    Async::Future<std::vector<std::string>> skinFolderEnumHandle;

    // search
    std::string sSearchString{};
    float fSearchOnCharKeybindHackTime{0.f};

    // notelock
    std::vector<std::string> notelockTypes;

    bool updating_layout{false};

    [[nodiscard]] bool should_use_oauth_login() const;

    OptionsOverlay *parent;
};

// passthroughs
OptionsOverlay::OptionsOverlay() : ScreenBackable(), NotificationOverlayKeyListener(), pImpl(this) {}
OptionsOverlay::~OptionsOverlay() = default;

void OptionsOverlay::draw() { return pImpl->draw(); }
void OptionsOverlay::update(CBaseUIEventCtx &c) { return pImpl->update(c); }
void OptionsOverlay::onKeyDown(KeyboardEvent &e) { return pImpl->onKeyDown(e); }
void OptionsOverlay::onChar(KeyboardEvent &e) { return pImpl->onChar(e); }
void OptionsOverlay::onResolutionChange(vec2 newResolution) { return pImpl->onResolutionChange(newResolution); }
void OptionsOverlay::onKey(KeyboardEvent &e) { return pImpl->onKey(e); }
CBaseUIContainer *OptionsOverlay::setVisible(bool visible) { return pImpl->setVisible(visible); }
void OptionsOverlay::save() { return pImpl->save(); }
void OptionsOverlay::openAndScrollToSkinSection() { return pImpl->openAndScrollToSkinSection(); }
void OptionsOverlay::setUsername(std::string username) { return pImpl->setUsername(std::move(username)); }
bool OptionsOverlay::isMouseInside() { return pImpl->isMouseInside(); }
bool OptionsOverlay::isBusy() { return pImpl->isBusy(); }
void OptionsOverlay::scheduleLayoutUpdate() { return pImpl->scheduleLayoutUpdate(); }
void OptionsOverlay::onSkinSelect() { return pImpl->onSkinSelectOpened(); }
void OptionsOverlay::onOutputDeviceChange() { return pImpl->onOutputDeviceChange(); }
void OptionsOverlay::updateOsuFolderTextbox(std::string_view newFolder) {
    return pImpl->updateOsuFolderTextbox(newFolder);
}
void OptionsOverlay::askForLoginDetails() { return pImpl->askForLoginDetails(); }
void OptionsOverlay::update_login_button(bool loggedIn) { return pImpl->update_login_button(loggedIn); }
void OptionsOverlay::updateSkinNameLabel() { return pImpl->updateSkinNameLabel(); }
UIContextMenu *OptionsOverlay::getContextMenu() { return pImpl->getContextMenu(); }
void OptionsOverlay::updateLayout() { return pImpl->updateLayout(); }
void OptionsOverlay::onBack() { return pImpl->onBack(); }

namespace {

struct RenderCondition {
    // allow using either enum equivalence or a custom boolean-returning function/delegate to check if we should render this element
    using RenderConditionFunc = SA::delegate<bool(void)>;

    enum r : uint8_t { NONE, ASIO_ENABLED, WASAPI_ENABLED, SCORE_SUBMISSION_POLICY, PASSWORD_AUTH } rc{NONE};
    RenderConditionFunc shouldrender{};

    RenderCondition() = default;
    RenderCondition(r rc) : rc(rc) {}
    RenderCondition(RenderConditionFunc shouldrender) : shouldrender(std::move(shouldrender)) {}

    ~RenderCondition() = default;
    RenderCondition &operator=(const RenderCondition &) = default;
    RenderCondition &operator=(RenderCondition &&) = default;
    RenderCondition(const RenderCondition &) = default;
    RenderCondition(RenderCondition &&) = default;

    auto operator()() { return this->shouldrender != nullptr ? this->shouldrender() : true; }

    bool operator==(const RenderCondition &other) const { return this->rc != NONE && this->rc == other.rc; };
    bool operator==(r cond) const { return this->rc != NONE && this->rc == cond; };
};

class ResetButton final : public CBaseUIButton {
    NOCOPY_NOMOVE(ResetButton)
   public:
    ResetButton(OptionsElement *elemContainer, float xPos, float yPos, float xSize, float ySize, std::string name,
                std::string text)
        : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name), std::move(text)), elemContainer(elemContainer) {
        this->fAnim = 1.0f;
    }

    ~ResetButton() override = default;

    bool isAvailable();

    void draw() override {
        if(!this->bVisible || this->fAnim <= 0.0f) return;
        if(!this->isAvailable()) return;

        const int fullColorBlockSize = 4 * Osu::getUIScale();

        Color left = argb((int)(255 * this->fAnim), 255, 233, 50);
        Color middle = argb((int)(255 * this->fAnim), 255, 211, 50);
        Color right = 0x00000000;

        g->fillGradient(this->getPos().x, this->getPos().y, this->getSize().x * 1.25f, this->getSize().y, middle, right,
                        middle, right);
        g->fillGradient(this->getPos().x, this->getPos().y, fullColorBlockSize, this->getSize().y, left, middle, left,
                        middle);
    }

    void update(CBaseUIEventCtx &c) override {
        if(!this->bVisible || !this->bEnabled) return;
        CBaseUIButton::update(c);

        if(this->isMouseInside() && this->isAvailable()) {
            ui->getTooltipOverlay()->begin();
            {
                ui->getTooltipOverlay()->addLine(_("Reset"));
            }
            ui->getTooltipOverlay()->end();
        }
    }

    OptionsElement *elemContainer;

   private:
    void onClicked(bool left = true, bool right = false) override {
        if(this->isAvailable()) CBaseUIButton::onClicked(left, right);
    }

    void onEnabled() override {
        CBaseUIButton::onEnabled();
        this->fAnim.set(1.0f, (1.0f - this->fAnim) * 0.15f, anim::QuadOut);
    }

    void onDisabled() override {
        CBaseUIButton::onDisabled();
        this->fAnim.set(0.0f, this->fAnim * 0.15f, anim::QuadOut);
    }

    AnimFloat fAnim;
};

struct OptionsElement {
    OptionsElement(ElementType type) : type(type) {}

    ConVar *cvar{nullptr};
    std::vector<std::unique_ptr<CBaseUIElement>> baseElems{};

    std::string searchTags;

    RenderCondition render_condition{RenderCondition::NONE};
    std::unique_ptr<ResetButton> resetButton{nullptr};

    float label1Width{0.f};
    float relSizeDPI{96.f};

    ElementType type;
    bool allowOverscale{false};
    bool allowUnderscale{false};

    // tracks whether the centralized cvar-lock sweep currently has baseElems forced bEnabled=false
    // (so we can restore the state set by other code, e.g. the high-quality-sliders toggle, when the lock is released).
    bool cvarLocked{false};
    std::vector<bool> enabledBeforeCvarLock;
};

bool ResetButton::isAvailable() {
    if(!this->elemContainer->cvar) return true;
    return this->elemContainer->cvar->getMaster() == CvarEditor::CLIENT;
}

class SkinPreviewElement final : public CBaseUIElement {
   public:
    SkinPreviewElement(float xPos, float yPos, float xSize, float ySize, std::string name)
        : CBaseUIElement(xPos, yPos, xSize, ySize, std::move(name)) {
        this->iMode = 0;
    }

    void draw() override {
        if(!this->bVisible) return;

        const Skin *skin = osu->getSkin();

        float hitcircleDiameter = this->getSize().y * 0.5f;
        float numberScale =
            (hitcircleDiameter / (160.0f * (skin->i_defaults[1].scale()))) * 1 * cv::number_scale_multiplier.getFloat();
        float overlapScale = (hitcircleDiameter / (160.0f)) * 1 * cv::number_scale_multiplier.getFloat();
        float scoreScale = 0.5f;

        if(this->iMode == 0) {
            float approachScale = std::clamp<float>(1.0f + 1.5f - fmod(engine->getTime() * 3, 3.0f), 0.0f, 2.5f);
            float approachAlpha = std::clamp<float>(fmod(engine->getTime() * 3, 3.0f) / 1.5f, 0.0f, 1.0f);
            approachAlpha = -approachAlpha * (approachAlpha - 2.0f);
            approachAlpha = -approachAlpha * (approachAlpha - 2.0f);
            float approachCircleAlpha = approachAlpha;
            approachAlpha = 1.0f;

            const int number = 1;
            const int colorCounter = 42;
            const int colorOffset = 0;
            const float colorRGBMultiplier = 1.0f;
            using enum LiveHitResult;

            Circle::drawCircle(
                skin, this->getPos() + vec2(0, this->getSize().y / 2) + vec2(this->getSize().x * (1.0f / 5.0f), 0.0f),
                hitcircleDiameter, numberScale, overlapScale, number, colorCounter, colorOffset, colorRGBMultiplier,
                approachScale, approachAlpha, approachAlpha, true, false);
            Circle::drawHitResult(
                skin, hitcircleDiameter, hitcircleDiameter,
                this->getPos() + vec2(0, this->getSize().y / 2) + vec2(this->getSize().x * (2.0f / 5.0f), 0.0f),
                HIT_100, 0.45f, 0.33f);
            Circle::drawHitResult(
                skin, hitcircleDiameter, hitcircleDiameter,
                this->getPos() + vec2(0, this->getSize().y / 2) + vec2(this->getSize().x * (3.0f / 5.0f), 0.0f), HIT_50,
                0.45f, 0.66f);
            Circle::drawHitResult(
                skin, hitcircleDiameter, hitcircleDiameter,
                this->getPos() + vec2(0, this->getSize().y / 2) + vec2(this->getSize().x * (4.0f / 5.0f), 0.0f),
                HIT_MISS, 0.45f, 1.0f);
            Circle::drawApproachCircle(
                skin, this->getPos() + vec2(0, this->getSize().y / 2) + vec2(this->getSize().x * (1.0f / 5.0f), 0.0f),
                skin->getComboColorForCounter(colorCounter, colorOffset), hitcircleDiameter, approachScale,
                approachCircleAlpha, false, false);
        } else if(this->iMode == 1) {
            const int numNumbers = 6;
            for(int i = 1; i < numNumbers + 1; i++) {
                Circle::drawHitCircleNumber(skin, numberScale, overlapScale,
                                            this->getPos() + vec2(0, this->getSize().y / 2) +
                                                vec2(this->getSize().x * ((float)i / (numNumbers + 1.0f)), 0.0f),
                                            i - 1, 1.0f, 1.0f);
            }
        } else if(this->iMode == 2) {
            const int numNumbers = 6;
            for(int i = 1; i < numNumbers + 1; i++) {
                vec2 pos = this->getPos() + vec2(0, this->getSize().y / 2) +
                           vec2(this->getSize().x * ((float)i / (numNumbers + 1.0f)), 0.0f);

                g->pushTransform();
                g->scale(scoreScale, scoreScale);
                g->translate(pos.x - skin->i_scores[0]->getWidth() * scoreScale, pos.y);
                HUD::drawNumberWithSkinDigits({.number = (u64)(i - 1), .scale = 1.0f, .combo = false});
                g->popTransform();
            }
        }
    }

    void onMouseUpInside(bool /*left*/, bool /*right*/) override {
        this->iMode++;
        this->iMode = this->iMode % 3;
    }

   private:
    int iMode;
};

class SliderPreviewElement final : public CBaseUIElement {
   public:
    SliderPreviewElement(float xPos, float yPos, float xSize, float ySize, std::string name)
        : CBaseUIElement(xPos, yPos, xSize, ySize, std::move(name)) {}

    void draw() override {
        if(!this->bVisible) return;

        const float hitcircleDiameter = this->getSize().y * 0.5f;
        const float numberScale = (hitcircleDiameter / (160.0f * (osu->getSkin()->i_defaults[1].scale()))) * 1 *
                                  cv::number_scale_multiplier.getFloat();
        const float overlapScale = (hitcircleDiameter / (160.0f)) * 1 * cv::number_scale_multiplier.getFloat();

        const float approachScale = std::clamp<float>(1.0f + 1.5f - fmod(engine->getTime() * 3, 3.0f), 0.0f, 2.5f);
        float approachAlpha = std::clamp<float>(fmod(engine->getTime() * 3, 3.0f) / 1.5f, 0.0f, 1.0f);

        approachAlpha = -approachAlpha * (approachAlpha - 2.0f);
        approachAlpha = -approachAlpha * (approachAlpha - 2.0f);

        const float approachCircleAlpha = approachAlpha;
        approachAlpha = 1.0f;

        const float length = (this->getSize().x - hitcircleDiameter);
        const int numPoints = length;
        const float pointDist = length / numPoints;

        static std::vector<vec2> emptyVector;
        std::vector<vec2> points;

        const bool useLegacyRenderer =
            (cv::options_slider_preview_use_legacy_renderer.getBool() || cv::force_legacy_slider_renderer.getBool());

        for(int i = 0; i < numPoints; i++) {
            int heightAdd = i;
            if(i > numPoints / 2) heightAdd = numPoints - i;

            float heightAddPercent = (float)heightAdd / (float)(numPoints / 2.0f);
            float temp = 1.0f - heightAddPercent;
            temp *= temp;
            heightAddPercent = 1.0f - temp;

            points.emplace_back((useLegacyRenderer ? this->getPos().x : 0) + hitcircleDiameter / 2 + i * pointDist,
                                (useLegacyRenderer ? this->getPos().y : 0) + this->getSize().y / 2 -
                                    hitcircleDiameter / 3 +
                                    heightAddPercent * (this->getSize().y / 2 - hitcircleDiameter / 2));
        }

        if(points.size() > 0) {
            // draw regular circle with animated approach circle beneath slider
            {
                const int number = 2;
                const int colorCounter = 420;
                const int colorOffset = 0;
                const float colorRGBMultiplier = 1.0f;

                Circle::drawCircle(osu->getSkin(),
                                   points[numPoints / 2] + (!useLegacyRenderer ? this->getPos() : vec2(0, 0)),
                                   hitcircleDiameter, numberScale, overlapScale, number, colorCounter, colorOffset,
                                   colorRGBMultiplier, approachScale, approachAlpha, approachAlpha, true, false);
                Circle::drawApproachCircle(osu->getSkin(),
                                           points[numPoints / 2] + (!useLegacyRenderer ? this->getPos() : vec2(0, 0)),
                                           osu->getSkin()->getComboColorForCounter(420, 0), hitcircleDiameter,
                                           approachScale, approachCircleAlpha, false, false);
            }

            // draw slider body
            {
                // recursive shared usage of the same RenderTarget is invalid, therefore we block slider rendering while
                // the options menu is animating
                if(this->bDrawSliderHack) {
                    if(useLegacyRenderer)
                        SliderRenderer::draw(points, emptyVector, hitcircleDiameter, 0, 1,
                                             osu->getSkin()->getComboColorForCounter(420, 0));
                    else {
                        // (lazy generate vao)
                        if(!this->vao || length != this->fPrevLength) {
                            this->fPrevLength = length;

                            debugLog("Regenerating options menu slider preview vao ...");

                            this->vao = SliderRenderer::generateVAO(points, hitcircleDiameter, vec3{}, false);
                        }
                        vec4 emptyBounds{};
                        SliderRenderer::draw(this->vao.get(), emptyBounds, emptyVector, this->getPos(), 1,
                                             hitcircleDiameter, 0, 1, osu->getSkin()->getComboColorForCounter(420, 0));
                    }
                }
            }

            // and slider head/tail circles
            {
                const int number = 1;
                const int colorCounter = 420;
                const int colorOffset = 0;
                const float colorRGBMultiplier = 1.0f;

                Circle::drawSliderStartCircle(
                    osu->getSkin(), points[0] + (!useLegacyRenderer ? this->getPos() : vec2(0, 0)), hitcircleDiameter,
                    numberScale, overlapScale, number, colorCounter, colorOffset, colorRGBMultiplier);
                Circle::drawSliderEndCircle(osu->getSkin(),
                                            points.back() + (!useLegacyRenderer ? this->getPos() : vec2(0, 0)),
                                            hitcircleDiameter, numberScale, overlapScale, number, colorCounter,
                                            colorOffset, colorRGBMultiplier, 1.0f, 1.0f, 0.0f, false, false);
            }
        }
    }

    void setDrawSliderHack(bool drawSliderHack) { this->bDrawSliderHack = drawSliderHack; }

   private:
    std::unique_ptr<VertexArrayObject> vao{nullptr};
    float fPrevLength{0.f};
    bool bDrawSliderHack{true};
};

class OptionsMenuKeyBindLabel final : public CBaseUILabel {
   public:
    OptionsMenuKeyBindLabel(float xPos, float yPos, float xSize, float ySize, std::string name, const std::string &text,
                            OsuKeyBinds::Bind *bind, CBaseUIButton *bindButton)
        : CBaseUILabel(xPos, yPos, xSize, ySize, std::move(name), text) {
        this->bind = bind;
        this->scanCode = -1;
        this->bindButton = bindButton;

        this->textColorBound = 0xffffd700;
        this->textColorUnbound = 0xffbb0000;
    }

    void update(CBaseUIEventCtx &c) override {
        if(!this->bVisible) return;
        CBaseUILabel::update(c);

        const SCANCODE newKeyCode = this->bind->get();
        if(this->scanCode == newKeyCode) return;

        this->scanCode = newKeyCode;

        // succ
        std::string labelText;

        // handle bound/unbound
        // HACKHACK: show mouse left/right for LEFT_CLICK_2/RIGHT_CLICK_2 if not bound to keyboard keys
        const bool isUnboundKey1_2 = this->bind->cvar == binds::LEFT_CLICK_2.cvar && this->scanCode == 0;
        const bool isUnboundKey2_2 = this->bind->cvar == binds::RIGHT_CLICK_2.cvar && this->scanCode == 0;
        const bool isUnbound = this->scanCode == 0 && !(isUnboundKey1_2 || isUnboundKey2_2);
        if(isUnboundKey1_2) {
            labelText = _("Mouse Left");
        } else if(isUnboundKey2_2) {
            labelText = _("Mouse Right");
        } else if(this->scanCode == 0) {
            labelText = _("<UNBOUND>");
        } else {
            labelText = env->scanCodeToString(this->scanCode);
            if(labelText.find('?') != std::string::npos) {
                labelText.append(fmt::format("  ({})", newKeyCode));
            }
        }

        this->setTextColor(isUnbound ? this->textColorUnbound : this->textColorBound);

        // update text
        this->setText(labelText);
    }

    void setTextColorBound(Color textColorBound) { this->textColorBound = textColorBound; }
    void setTextColorUnbound(Color textColorUnbound) { this->textColorUnbound = textColorUnbound; }

   private:
    void onMouseUpInside(bool left, bool right) override {
        CBaseUILabel::onMouseUpInside(left, right);
        this->bindButton->click(left, right);
    }

    OsuKeyBinds::Bind *bind;
    CBaseUIButton *bindButton;

    Color textColorBound;
    Color textColorUnbound;
    SCANCODE scanCode;
};

class KeyBindButton final : public UIButton {
   public:
    KeyBindButton(float xPos, float yPos, float xSize, float ySize, std::string name, std::string text)
        : UIButton(xPos, yPos, xSize, ySize, std::move(name), std::move(text)) {
        this->bDisallowLeftMouseClickBinding = false;
    }

    void setDisallowLeftMouseClickBinding(bool disallowLeftMouseClickBinding) {
        this->bDisallowLeftMouseClickBinding = disallowLeftMouseClickBinding;
    }

    [[nodiscard]] inline bool isLeftMouseClickBindingAllowed() const { return !this->bDisallowLeftMouseClickBinding; }

   private:
    bool bDisallowLeftMouseClickBinding;
};

class CategoryButton final : public CBaseUIButton {
   public:
    CategoryButton(CBaseUIElement *section, float xPos, float yPos, float xSize, float ySize, std::string name,
                   std::string text)
        : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name), std::move(text)) {
        this->section = section;
        this->bActiveCategory = false;
        this->setTextJustification(TEXT_JUSTIFICATION::CENTERED);
        this->setDrawShadow(false);
    }

    void setActiveCategory(bool activeCategory) { this->bActiveCategory = activeCategory; }

    [[nodiscard]] inline CBaseUIElement *getSection() const { return this->section; }
    [[nodiscard]] inline bool isActiveCategory() const { return this->bActiveCategory; }

   private:
    CBaseUIElement *section;
    bool bActiveCategory;
};

}  // namespace

OptionsOverlayImpl::OptionsOverlayImpl(OptionsOverlay *parent) : parent(parent) {
    parent->backable = true;
    this->elemContainers.reserve(1024);

    // convar callbacks
    cv::skin_use_skin_hitsounds.setCallback([]() -> void { osu->reloadSkin(); });

    cv::options_slider_quality.setCallback([](float newValue) -> void {
        // wrapper callback
        float value = std::lerp(1.0f, 2.5f, 1.0f - newValue);
        cv::slider_curve_points_separation.setValue(value);
    });

    cv::options_high_quality_sliders.setCallback(
        SA::MakeDelegate<&OptionsOverlayImpl::onHighQualitySlidersConVarChange>(this));

    this->notelockTypes = {_("None"), _("McOsu"), _("osu!stable (default)"), _("osu!lazer 2020")};

    parent->setPos(-1, 0);

    this->options = new CBaseUIScrollView(0, -1, 0, 0, "");
    this->options->setDrawFrame(true);
    this->options->setDrawBackground(true);
    this->options->setBackgroundColor(0xdd000000);
    this->options->setHorizontalScrolling(false);
    parent->addBaseUIElement(this->options);

    this->categories = new CBaseUIScrollView(0, -1, 0, 0, "");
    this->categories->setDrawFrame(true);
    this->categories->setDrawBackground(true);
    this->categories->setBackgroundColor(0xff000000);
    this->categories->setHorizontalScrolling(false);
    this->categories->setVerticalScrolling(true);
    this->categories->setScrollResistance(30);  // since all categories are always visible anyway
    parent->addBaseUIElement(this->categories);

    this->contextMenu = new UIContextMenu(50, 50, 150, 0, "", this->options);
    this->contextMenu->setBackgroundColor(argb(200, 36, 36, 48));
    this->contextMenu->setFrameColor(argb(240, 240, 240, 255));

    this->search = new UISearchOverlay(0, 0, 0, 0, "");
    this->search->setOffsetRight(20);
    parent->addBaseUIElement(this->search);

    this->spacer = new CBaseUILabel(0, 0, 1, 40, "", "");
    this->spacer->setDrawBackground(false);
    this->spacer->setDrawFrame(false);

    //**************************************************************************************************************************//

    this->sectionGeneral = this->addSection(_("General"));

    this->addSubSection(_("Online server"));

    // Only renders if server submission policy is unknown
    {
        this->addLabel(tformat("If the server admins don't explicitly allow {},", PACKAGE_NAME))
            ->setTextColor(0xff666666);
        this->elemContainers.back()->render_condition = RenderCondition::SCORE_SUBMISSION_POLICY;
        this->addLabel(_("you might get banned!"))->setTextColor(0xff666666);
        this->elemContainers.back()->render_condition = RenderCondition::SCORE_SUBMISSION_POLICY;
        this->addLabel("");
        this->elemContainers.back()->render_condition = RenderCondition::SCORE_SUBMISSION_POLICY;
    }

    this->serverTextbox = this->addTextbox(cv::mp_server.getString(), _("Server address:"), &cv::mp_server);

    // Only renders if server submission policy is unknown
    {
        this->submitScoresCheckbox = this->addCheckbox(_("Submit scores"), &cv::submit_scores);
        this->elemContainers.back()->render_condition = RenderCondition::SCORE_SUBMISSION_POLICY;
    }

    // Only renders if server isn't OAuth
    {
        this->addSubSection(_("Login details (username/password)"));
        this->elemContainers.back()->render_condition = RenderCondition::PASSWORD_AUTH;
        this->nameTextbox = this->addTextbox(cv::name.getString(), &cv::name);
        this->elemContainers.back()->render_condition = RenderCondition::PASSWORD_AUTH;
        const auto &md5pass = cv::mp_password_md5.getString();
        this->passwordTextbox = this->addTextbox(md5pass.empty() ? "" : md5pass, &cv::mp_password);
        this->passwordTextbox->is_password = true;
        this->elemContainers.back()->render_condition = RenderCondition::PASSWORD_AUTH;
    }

    {
        enum ELEMS : uint8_t { LOGINBTN = 0, KEEPSIGNEDCBX = 1 };
        OptionsElement *loginElement = this->addButtonCheckbox(_("Log in"), _("Keep me logged in"));
        loginElement->cvar = &cv::mp_autologin;

        this->logInButton = static_cast<UIButton *>(loginElement->baseElems[LOGINBTN].get());

        this->logInButton->setHandleRightMouse(true);  // for canceling logins
        this->logInButton->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onLogInClicked>(this));
        this->logInButton->setColor(0xff00d900);
        this->logInButton->setTextColor(0xffffffff);

        auto *keepCbx = static_cast<UICheckbox *>(loginElement->baseElems[KEEPSIGNEDCBX].get());
        keepCbx->setChecked(cv::mp_autologin.getBool());
        keepCbx->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onCheckboxChange>(this));
    }

    {
        this->addSubSection(_("Localization"));

#ifdef HAVE_LIBINTL
        std::string currentLanguage = "English";
        for(const auto &[locale, language] : LANGUAGES) {
            if(locale == cv::language.getString()) {
                currentLanguage = language;
                break;
            }
        }

        auto languageElement = this->addButton(_("Select language"), currentLanguage, false, &cv::language);
        this->languageSelectButton = (CBaseUIButton *)languageElement->baseElems[0].get();
        this->languageSelectButton->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onLanguageSelect>(this));
#endif

        // Fallback font support is currently implemented for these platforms
        // Remember to update this if adding support for another platform
        if constexpr(Env::cfg(OS::LINUX | OS::WINDOWS)) {
            this->addCheckbox(_("Prefer metadata in original language"), &cv::prefer_cjk);
            this->elemContainers.back()->searchTags = "native character cjk";
        }
    }

#ifndef MCENGINE_PLATFORM_WASM
    this->addSubSection(_("osu! folder"));
    this->addLabel(_("1) If you have an existing osu!stable installation:"))->setTextColor(0xff666666);
    this->addLabel(_("2) osu! > Options > \"Open osu! folder\""))->setTextColor(0xff666666);
    this->addLabel(_("3) Copy paste the full path into the textbox:"))->setTextColor(0xff666666);
    this->addLabel("");
    this->osuFolderTextbox = this->addTextbox(cv::osu_folder.getString(), &cv::osu_folder);
    UIButton *importPeppySettingsButton = this->addButton(_("Import settings from osu!stable"));
    importPeppySettingsButton->setClickCallback(SA::MakeDelegate([]() -> void {
        if(SettingsImporter::import_from_osu_stable()) {
            ui->getNotificationOverlay()->addToast(_("Successfully imported settings from osu!stable."), SUCCESS_TOAST);
        } else {
            ui->getNotificationOverlay()->addToast(
                _("Error: Couldn't find osu!stable install directory or config file!"), ERROR_TOAST);
        }
    }));
    this->addCheckbox(
        _("Use osu!.db database (read-only)"),
        _("If you have an existing osu! installation,\nthen this will speed up the initial loading process."),
        &cv::database_enabled);
    this->addCheckbox(
        _("Load osu! collection.db (read-only)"),
        _("If you have an existing osu! installation,\nalso load and display your created collections from there."),
        &cv::collections_legacy_enabled);
#endif

    this->addSpacer();
    this->addCheckbox(
        _("Include Relax/Autopilot for total weighted pp/acc"),
        _("NOTE: osu! does not allow this (since these mods are unranked).\nShould relax/autopilot scores be "
          "included in the weighted pp/acc calculation?"),
        &cv::user_include_relax_and_autopilot_for_stats);
    this->addCheckbox(_("Always show pp instead of score in scorebrowser"), _("Ignore score sorting type entirely."),
                      &cv::scores_always_display_pp);

    this->addSubSection(_("Songbrowser"));
    this->addCheckbox(_("Draw Strain Graph in Songbrowser"),
                      _("Hold either SHIFT/CTRL to show only speed/aim strains.\nSpeed strain is red, aim strain is "
                        "green.\n(See osu_hud_scrubbing_timeline_strains_*)"),
                      &cv::draw_songbrowser_strain_graph);
    this->addCheckbox(_("Draw Strain Graph in Scrubbing Timeline"),
                      _("Speed strain is red, aim strain is green.\n(See osu_hud_scrubbing_timeline_strains_*)"),
                      &cv::draw_scrubbing_timeline_strain_graph);
    this->addCheckbox(_("Song Buttons Velocity Animation"),
                      _("If enabled, then song buttons are pushed to the right depending on the scrolling velocity."),
                      &cv::songbrowser_button_anim_x_push);
    this->addCheckbox(_("Song Buttons Curved Layout"),
                      _("If enabled, then song buttons are positioned on a vertically centered curve."),
                      &cv::songbrowser_button_anim_y_curve);

    this->addSubSection(_("Window"));
    this->addCheckbox(_("Pause on Focus Loss"), _("Should the game pause when you switch to another application?"),
                      &cv::pause_on_focus_loss);

    this->addSubSection(_("Alerts"));
    this->addCheckbox(_("Notify when friends change status"), &cv::notify_friend_status_change);
    this->addCheckbox(_("Notify when receiving a direct message"), &cv::chat_notify_on_dm);
    this->addCheckbox(_("Notify when mentioned"), &cv::chat_notify_on_mention);
    this->addCheckbox(_("Ping when mentioned"), &cv::chat_ping_on_mention);
    this->addCheckbox(_("Show notifications during gameplay"), &cv::notify_during_gameplay);

    this->addSubSection(_("In-game chat"));
    this->addCheckbox(_("Chat ticker"), &cv::chat_ticker);
    this->addCheckbox(_("Automatically hide chat during gameplay"), &cv::chat_auto_hide);
    this->addTextbox(cv::chat_ignore_list.getString(), _("Chat word ignore list (space-separated):"),
                     &cv::chat_ignore_list);
    // this->addTextbox(cv::chat_highlight_words.getString(), _("Chat word highlight list (space-separated):"), &cv::chat_highlight_words);

    this->addSubSection(_("Privacy"));
#ifndef MCENGINE_PLATFORM_WASM
    this->addCheckbox(_("Automatically update to the latest version"), &cv::auto_update);
#endif
    // this->addCheckbox(_("Allow private messages from strangers"), &cv::allow_stranger_dms);
    // this->addCheckbox(_("Allow game invites from strangers"), &cv::allow_mp_invites);
    this->addCheckbox(_("Replace main menu logo with server logo"), &cv::main_menu_use_server_logo);
    this->addCheckbox(_("Show spectator list"), &cv::draw_spectator_list);
    this->addCheckbox(_("Share currently played map with spectators"), &cv::spec_share_map);
    if constexpr(Env::cfg(FEAT::DISCORD)) {
        this->addCheckbox(
            _("Enable Discord Rich Presence"),
            _("Shows your current game state in your friends' friendslists.\ne.g.: Playing Gavin G - Reach Out "
              "[Cherry Blossom's Insane]"),
            &cv::rich_presence);
        this->addCheckbox(_("Draw map backgrounds in Discord Rich Presence"), &cv::rich_presence_map_backgrounds);

        // XXX: have a generic "update_activity"
        cv::rich_presence_map_backgrounds.setCallback([]() { DiscRPC::clear_activity(); });
    }

    //**************************************************************************************************************************//

    CBaseUIElement *sectionGraphics = this->addSection(_("Graphics"));

    this->addSubSection(_("Renderer"));
    // makes the game run worse
    if constexpr(!Env::cfg(OS::WASM)) {
        this->addCheckbox(_("VSync"), _("If enabled: plz enjoy input lag."), &cv::vsync);
        this->addCheckbox(_("High Priority"), _("Sets the game process priority to high"), &cv::win_processpriority);
    }

    this->addCheckbox(_("Show FPS Counter"), &cv::draw_fps);

    CBaseUISlider *prerenderedFramesSlider =
        this->addSlider(_("Max Queued Frames"), 1.0f, 3.0f, &cv::r_sync_max_frames, -1.0f, true);
    prerenderedFramesSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangeInt>(this));
    prerenderedFramesSlider->setKeyDelta(1);
    this->addLabel(_("Raise for higher fps, decrease for lower latency"))->setTextColor(0xff666666);

    this->addSpacer();

    CBaseUISlider *fpsSlider = this->addSlider(_("FPS Limiter:"), 0.0f, 1000.0f, &cv::fps_max, -1.0f, true);
    fpsSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onFPSSliderChange>(this));
    fpsSlider->setKeyDelta(1);

    CBaseUISlider *fpsSlider2 =
        this->addSlider(_("FPS Limiter (menus):"), 0.0f, 1000.0f, &cv::fps_max_menu, -1.0f, true);
    fpsSlider2->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onFPSSliderChange>(this));
    fpsSlider2->setKeyDelta(1);

    this->addSubSection(_("Layout"));
    OptionsElement *resolutionSelect = this->addButton(
        _("Select Resolution"), fmt::format("{}x{}", osu->getVirtScreenWidth(), osu->getVirtScreenHeight()), false,
        &cv::resolution);
    this->resolutionSelectButton = (CBaseUIButton *)resolutionSelect->baseElems[0].get();
    this->resolutionSelectButton->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onResolutionSelect>(this));
    this->resolutionLabel = (CBaseUILabel *)resolutionSelect->baseElems[1].get();
    this->fullscreenCheckbox = this->addCheckbox(_("Fullscreen"), &cv::fullscreen);
    this->fullscreenCheckbox->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onFullscreenChange>(this));
    this->addCheckbox(_("Keep Aspect Ratio"),
                      _("Black borders instead of a stretched image.\nOnly relevant if fullscreen is enabled, and "
                        "letterboxing is disabled.\nUse the two position sliders below to move the viewport around."),
                      &cv::resolution_keep_aspect_ratio);
    this->addCheckbox(
        _("Letterboxing"),
        _("Useful to get the low latency of fullscreen with a smaller game resolution.\nUse the two position "
          "sliders below to move the viewport around."),
        &cv::letterboxing);
    this->letterboxingOffsetXSlider =
        this->addSlider(_("Horizontal position"), -1.0f, 1.0f, &cv::letterboxing_offset_x, 170)
            ->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangeLetterboxingOffset>(this))
            ->setKeyDelta(0.01f)
            ->setAnimated(false);
    this->letterboxingOffsetYSlider =
        this->addSlider(_("Vertical position"), -1.0f, 1.0f, &cv::letterboxing_offset_y, 170)
            ->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangeLetterboxingOffset>(this))
            ->setKeyDelta(0.01f)
            ->setAnimated(false);

    this->addSubSection(_("UI Scaling"));
    this->addCheckbox(
            _("DPI Scaling"),
            fmt::format(
                fmt::runtime(
                    _("Automatically scale to the DPI of your display: {} DPI.\nScale factor = {} / 96 = {:.2g}x")),
                env->getDPI(), env->getDPI(), env->getDPIScale()),
            &cv::ui_scale_to_dpi)
        ->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onDPIScalingChange>(this));
    this->uiScaleSlider = this->addSlider(_("UI Scale:"), 1.0f, 1.5f, &cv::ui_scale, 0.f, false, true);
    this->uiScaleSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangeUIScale>(this));
    this->uiScaleSlider->setKeyDelta(0.01f);
    this->uiScaleSlider->setAnimated(false);

    this->addSubSection(_("Detail Settings"));
    this->addCheckbox(_("Animate scoreboard"), _("Use fancy animations for the in-game scoreboard"),
                      &cv::scoreboard_animations);
    this->addCheckbox(
        _("Avoid flashing elements"),
        _("Disables cosmetic flash effects\nDisables dimming when holding silders with Flashlight mod enabled"),
        &cv::avoid_flashes);
    this->addCheckbox(
        _("Mipmaps"),
        _("Reload your skin to apply! (CTRL + ALT + S)\nGenerate mipmaps for each skin element, at the cost of "
          "VRAM.\nProvides smoother visuals on lower resolutions for @2x-only skins."),
        &cv::skin_mipmaps);
    this->addSpacer();
    this->addCheckbox(
        _("Snaking in sliders"),
        _("\"Growing\" sliders.\nSliders gradually snake out from their starting point while fading in.\nHas no "
          "impact on performance whatsoever."),
        &cv::snaking_sliders);
    this->addCheckbox(_("Snaking out sliders"),
                      _("\"Shrinking\" sliders.\nSliders will shrink with the sliderball while sliding.\nCan improve "
                        "performance a tiny bit, since there will be less to draw overall."),
                      &cv::slider_shrink);
    this->addSpacer();
    this->addCheckbox(
        _("Legacy Slider Renderer (!)"),
        _("WARNING: Only try enabling this on shitty old computers!\nMay or may not improve fps while few "
          "sliders are visible.\nGuaranteed lower fps while many sliders are visible!"),
        &cv::force_legacy_slider_renderer);
    this->addCheckbox(_("Higher Quality Sliders (!)"),
                      _("Disable this if your fps drop too low while sliders are visible."),
                      &cv::options_high_quality_sliders)
        ->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onHighQualitySlidersCheckboxChange>(this));

    this->sliderQualitySlider = this->addSlider(_("Slider Quality"), 0.0f, 1.0f, &cv::options_slider_quality);
    this->sliderQualitySlider->setChangeCallback(
        SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangeSliderQuality>(this));

    //**************************************************************************************************************************//

    CBaseUIElement *sectionAudio = this->addSection(_("Audio"));

    this->addSubSection(_("Devices"));
    {
        OptionsElement *outputDeviceSelect =
            this->addButton(_("Select Output Device"), _("Default"), true, &cv::snd_output_device);
        this->outputDeviceResetButton = outputDeviceSelect->resetButton.get();
        this->outputDeviceResetButton->setClickCallback(
            SA::MakeDelegate([]() -> void { soundEngine->setOutputDevice(soundEngine->getDefaultDevice()); }));

        this->outputDeviceSelectButton = (CBaseUIButton *)outputDeviceSelect->baseElems[0].get();
        this->outputDeviceSelectButton->setClickCallback(
            SA::MakeDelegate<&OptionsOverlayImpl::onOutputDeviceSelect>(this));

        this->outputDeviceLabel = (CBaseUILabel *)outputDeviceSelect->baseElems[1].get();

        {
            OptionsElement *soloudBackendSelect =
                this->addButtonButtonLabel(_("MiniAudio"), _("SDL"), _("Backend"), false, &cv::snd_soloud_backend);
            const auto &MAButton = static_cast<UIButton *>(soloudBackendSelect->baseElems[0].get());
            const auto &SDLButton = static_cast<UIButton *>(soloudBackendSelect->baseElems[1].get());
            static auto MAButton_static = MAButton;
            static auto SDLButton_static = SDLButton;
            MAButton_static = MAButton;  // always update
            SDLButton_static = SDLButton;

            const auto &backendLabel = static_cast<UILabel *>(soloudBackendSelect->baseElems[2].get());
            backendLabel->setVisible(true);
            backendLabel->setTooltipText(
                _("Pick the one you feel works best,\nthere should be very little difference."));

            soloudBackendSelect->render_condition = {[]() -> bool {
                bool ret =
                    soundEngine ? soundEngine->getOutputDriverType() >= SoundEngine::OutputDriver::SOLOUD_MA : false;
                return ret;
            }};

            static auto setActiveColors = []() -> void {
                const std::string &current = cv::snd_soloud_backend.getString();
                const bool MAactive = !SString::contains_ncase(current, "sdl");

                constexpr const auto inactiveGrey = rgba(150, 150, 150, 255);
                constexpr const auto activeGreen = rgba(50, 155, 50, 255);

                MAButton_static->setColor(MAactive ? activeGreen : inactiveGrey);
                SDLButton_static->setColor(MAactive ? inactiveGrey : activeGreen);
            };

            // set default active colors
            setActiveColors();

            MAButton->setClickCallback(
                SA::MakeDelegate([](CBaseUIButton *btn) -> void { cv::snd_soloud_backend.setValue(btn->getName()); }));
            SDLButton->setClickCallback(
                SA::MakeDelegate([](CBaseUIButton *btn) -> void { cv::snd_soloud_backend.setValue(btn->getName()); }));

            // need to use a change callback here because we already have a single-arg callback for the convar...
            cv::snd_soloud_backend.setCallback([](float /**/, float /**/) -> void { setActiveColors(); });

            this->addCheckbox(
                _("Auto-disable exclusive mode"),
                _("Toggle exclusive mode off/on\nwhen losing/gaining focus, if already\nin exclusive mode."),
                &cv::snd_disable_exclusive_unfocused);

            this->elemContainers.back()->render_condition = {[]() -> bool {
                if constexpr(!Env::cfg(OS::WINDOWS)) return false;
                bool ret =
                    soundEngine ? soundEngine->getOutputDriverType() == SoundEngine::OutputDriver::SOLOUD_MA : false;
                return ret;
            }};
        }

        static auto onOutputDeviceRestartCB = SA::MakeDelegate([]() -> void { soundEngine->restart(); });

        // Dirty...
        auto wasapi_idx = this->elemContainers.size();
        {
            this->addSubSection(_("WASAPI"));

            this->addCheckbox(_("Low-latency Callbacks"),
                              _("Run BASSWASAPI in callback mode to potentially further decrease "
                                "latency.\n(period/buffer size are ignored)"),
                              &cv::win_snd_wasapi_event_callbacks);

            this->wasapiBufferSizeSlider =
                this->addSlider(_("Buffer Size:"), 0.000f, 0.050f, &cv::win_snd_wasapi_buffer_size);
            this->wasapiBufferSizeSlider->setChangeCallback(
                SA::MakeDelegate<&OptionsOverlayImpl::onWASAPIBufferChange>(this));
            this->wasapiBufferSizeSlider->setKeyDelta(0.001f);
            this->wasapiBufferSizeSlider->setAnimated(false);
            this->addLabel(_("Windows 7: Start at 11 ms,"))->setTextColor(0xff666666);
            this->addLabel(_("Windows 10: Start at 1 ms,"))->setTextColor(0xff666666);
            this->addLabel(_("and if crackling: increment until fixed."))->setTextColor(0xff666666);
            this->addLabel(_("(lower is better, non-wasapi has ~40 ms minimum)"))->setTextColor(0xff666666);
            this->addCheckbox(
                _("Exclusive Mode"),
                _("Dramatically reduces latency, but prevents other applications from capturing/playing audio."),
                &cv::win_snd_wasapi_exclusive);
            this->addLabel("");
            this->addLabel("");
            this->addLabel(_("WARNING: Only if you know what you are doing"))->setTextColor(0xffff0000);
            this->wasapiPeriodSizeSlider =
                this->addSlider(_("Period Size:"), 0.0f, 0.050f, &cv::win_snd_wasapi_period_size);
            this->wasapiPeriodSizeSlider->setChangeCallback(
                SA::MakeDelegate<&OptionsOverlayImpl::onWASAPIPeriodChange>(this));
            this->wasapiPeriodSizeSlider->setKeyDelta(0.001f);
            this->wasapiPeriodSizeSlider->setAnimated(false);
            UIButton *restartSoundEngine = this->addButton(_("Restart SoundEngine"));
            restartSoundEngine->setClickCallback(onOutputDeviceRestartCB);
            restartSoundEngine->setColor(0xff003947);
            this->addLabel("");
        }
        auto wasapi_end_idx = this->elemContainers.size();
        for(auto i = wasapi_idx; i < wasapi_end_idx; i++) {
            this->elemContainers[i]->render_condition = RenderCondition::WASAPI_ENABLED;
        }

        // Dirty...
        auto asio_idx = this->elemContainers.size();
        {
            this->addSubSection(_("ASIO"));
            this->asioBufferSizeSlider = this->addSlider(_("Buffer Size:"), 0, 44100, &cv::asio_buffer_size);
            this->asioBufferSizeSlider->setKeyDelta(512);
            this->asioBufferSizeSlider->setAnimated(false);
            this->asioBufferSizeSlider->setLiveUpdate(false);
            this->asioBufferSizeSlider->setChangeCallback(
                SA::MakeDelegate<&OptionsOverlayImpl::onASIOBufferChange>(this));
            this->addLabel("");
            UIButton *asio_settings_btn = this->addButton(_("Open ASIO settings"));
            asio_settings_btn->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::OpenASIOSettings>(this));
            asio_settings_btn->setColor(0xff003947);
            UIButton *restartSoundEngine = this->addButton(_("Restart SoundEngine"));
            restartSoundEngine->setClickCallback(onOutputDeviceRestartCB);
            restartSoundEngine->setColor(0xff003947);

            // FIXME: hacky
            this->setupASIOClampedChangeCallback();
        }
        auto asio_end_idx = this->elemContainers.size();
        for(auto i = asio_idx; i < asio_end_idx; i++) {
            this->elemContainers[i]->render_condition = RenderCondition::ASIO_ENABLED;
        }
    }

    this->addSubSection(_("Volume"));

    this->addCheckbox(_("Normalize loudness across songs"), &cv::normalize_loudness)
        ->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onLoudnessNormalizationToggle>(this));

    CBaseUISlider *masterVolumeSlider = this->addSlider(_("Master:"), 0.0f, 1.0f, &cv::volume_master, 70.0f);
    masterVolumeSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    masterVolumeSlider->setKeyDelta(0.01f);
    CBaseUISlider *inactiveVolumeSlider =
        this->addSlider(_("Inactive:"), 0.0f, 1.0f, &cv::volume_master_inactive, 70.0f);
    inactiveVolumeSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    inactiveVolumeSlider->setKeyDelta(0.01f);
    CBaseUISlider *musicVolumeSlider = this->addSlider(_("Music:"), 0.0f, 1.0f, &cv::volume_music, 70.0f);
    musicVolumeSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    musicVolumeSlider->setKeyDelta(0.01f);
    CBaseUISlider *effectsVolumeSlider = this->addSlider(_("Effects:"), 0.0f, 1.0f, &cv::volume_effects, 70.0f);
    effectsVolumeSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    effectsVolumeSlider->setKeyDelta(0.01f);

    this->addSubSection(_("Offset Adjustment"));
    {
        this->addSlider(_("Scaled Offset:"), -300.0f, 300.0f, &cv::universal_offset)
            ->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangeIntMS>(this))
            ->setKeyDelta(1);

        auto *label1 = static_cast<UILabel *>(this->elemContainers.back()->baseElems[0].get());
        label1->setTooltipText(_("Behaves the same as \"Universal Offset\" in osu! (is multiplied by playback rate)."));
    }

    {
        this->addSlider(_("Unscaled Offset:"), -300.0f, 300.0f, &cv::universal_offset_norate)
            ->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangeIntMS>(this))
            ->setKeyDelta(1);

        auto *label1 = static_cast<UILabel *>(this->elemContainers.back()->baseElems[0].get());
        label1->setTooltipText(
            _("\"Universal Offset\", but unaffected by rate adjustments.\n+15 unscaled offset is the same as using -15 "
              "local offset on every map."));
    }

    this->addSubSection(_("Gameplay"));
    this->addCheckbox(_("Boost hitsound volume"),
                      _("Apply a small logarithmic multiplier to non-sliderslide hitsounds."),
                      &cv::snd_boost_hitsound_volume);
    this->addCheckbox(_("Change hitsound pitch based on accuracy"), &cv::snd_pitch_hitsounds);
    this->addCheckbox(_("Prefer Nightcore over Double Time"), &cv::nightcore_enjoyer)
        ->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onModChangingToggle>(this));

    this->addSubSection(_("Songbrowser"));
    this->addCheckbox(_("Apply speed/pitch mods while browsing"),
                      _("Whether to always apply all mods, or keep the preview music normal."),
                      &cv::beatmap_preview_mods_live);

    //**************************************************************************************************************************//

    this->skinSection = this->addSection(_("Skin"));

    this->addSubSection(_("Skin"));
    this->addSkinPreview();
    {
        if constexpr(Env::cfg(OS::WASM)) {
            this->addLabel(_("To import a skin, just drop the .osk file on this window!"))->setTextColor(0xff666666);
            this->addSpacer();
        }

        {
            OptionsElement *skinSelect = this->addButton(_("Select Skin"), _("default"), false, &cv::skin);
            this->skinSelectLocalButton = static_cast<UIButton *>(skinSelect->baseElems[0].get());
            this->skinLabel = static_cast<CBaseUILabel *>(skinSelect->baseElems[1].get());
        }

        this->skinSelectLocalButton->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSkinSelectOpened>(this));
        this->skinSelectLocalButton->setTooltipText(
            _("Shift-click a skin to set it as fallback.\nMissing elements fall back to it instead of \"default\"."));

        if constexpr(!Env::cfg(OS::WASM)) {
            this->addButton(_("Open current Skin folder"))
                ->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::openCurrentSkinFolder>(this));
        }

        OptionsElement *skinReload = this->addButtonButton(_("Reload Skin"), _("Random Skin"), &cv::skin);
        auto *skinReloadBtn = static_cast<UIButton *>(skinReload->baseElems[0].get());
        auto *skinRandomBtn = static_cast<UIButton *>(skinReload->baseElems[1].get());

        skinReloadBtn->setClickCallback(SA::MakeDelegate([]() -> void { osu->reloadSkin(); }));
        skinReloadBtn->setTooltipText(_("(CTRL + ALT + S)"));

        skinRandomBtn->setClickCallback(SA::MakeDelegate([]() -> void {
            const bool isRandomSkinEnabled = cv::skin_random.getBool();
            if(!isRandomSkinEnabled) cv::skin_random.setValue(1.0f);
            osu->reloadSkin();
            if(!isRandomSkinEnabled) cv::skin_random.setValue(0.0f);
        }));

        skinRandomBtn->setTooltipText(
            _("Temporary, does not change your configured skin (reload to reset).\nUse \"skin_random 1\" to "
              "randomize on every skin reload.\nUse \"skin_random_elements 1\" to mix multiple skins.\nUse "
              "\"skin_export\" to export the currently active skin."));
        skinRandomBtn->setColor(0xff003947);
    }
    this->addSpacer();
    this->addCheckbox(_("Sort Skins Alphabetically"),
                      _("Less like stable, but useful if you don't\nlike obnoxious skin names floating to the top."),
                      &cv::sort_skins_cleaned);
    CBaseUISlider *numberScaleSlider =
        this->addSlider(_("Number Scale:"), 0.01f, 3.0f, &cv::number_scale_multiplier, 135.0f);
    numberScaleSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    numberScaleSlider->setKeyDelta(0.01f);
    CBaseUISlider *hitResultScaleSlider =
        this->addSlider(_("HitResult Scale:"), 0.01f, 3.0f, &cv::hitresult_scale, 135.0f);
    hitResultScaleSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    hitResultScaleSlider->setKeyDelta(0.01f);
    this->addCheckbox(_("Draw Numbers"), &cv::draw_numbers);
    this->addCheckbox(_("Draw Approach Circles"), &cv::draw_approach_circles);
    this->addCheckbox(_("Instafade Circles"), &cv::instafade);
    this->addCheckbox(_("Instafade Sliders"), &cv::instafade_sliders);
    this->addSpacer();
    this->addCheckbox(
        _("Ignore Beatmap Sample Volume"),
        _("Ignore beatmap timingpoint effect volumes.\nQuiet hitsounds can destroy accuracy and concentration, "
          "enabling this will fix that."),
        &cv::ignore_beatmap_sample_volume);
    this->addCheckbox(_("Ignore Beatmap Combo Colors"), &cv::ignore_beatmap_combo_colors);
    this->addCheckbox(_("Use skin's sound samples"),
                      _("If this is not selected, then the default skin hitsounds will be used."),
                      &cv::skin_use_skin_hitsounds);
    this->addCheckbox(_("Load HD @2x"),
                      _("On very low resolutions (below 1600x900) you can disable this to get smoother visuals."),
                      &cv::skin_hd);
    this->addSpacer();
    this->addCheckbox(_("Draw Cursor Trail"), &cv::draw_cursor_trail);
    this->addCheckbox(
        _("Force Smooth Cursor Trail"),
        _("Usually, the presence of the cursormiddle.png skin image enables smooth cursortrails.\nThis option "
          "allows you to force enable smooth cursortrails for all skins."),
        &cv::cursor_trail_smooth_force);
    this->addCheckbox(_("Always draw Cursor Trail"), _("Draw the cursor trail even when the cursor isn't moving"),
                      &cv::always_render_cursor_trail);
    this->addSlider(_("Cursor trail spacing:"), 0.f, 30.f, &cv::cursor_trail_spacing, -1.f, true)
        ->setAnimated(false)
        ->setKeyDelta(0.01f);
    this->cursorSizeSlider = this->addSlider(_("Cursor Size:"), 0.01f, 5.0f, &cv::cursor_scale, -1.0f, true);
    this->cursorSizeSlider->setAnimated(false);
    this->cursorSizeSlider->setKeyDelta(0.01f);
    this->addCheckbox(_("Automatic Cursor Size"), _("Cursor size will adjust based on the CS of the current beatmap."),
                      &cv::automatic_cursor_size);
    this->addSpacer();
    this->sliderPreviewElement = this->addSliderPreview();
    this->addSlider(_("Slider Border Size"), 0.0f, 9.0f, &cv::slider_border_size_multiplier)->setKeyDelta(0.01f);
    this->addSlider(_("Slider Opacity"), 0.0f, 1.0f, &cv::slider_alpha_multiplier, 200.0f);
    this->addSlider(_("Slider Body Opacity"), 0.0f, 1.0f, &cv::slider_body_alpha_multiplier, 200.0f, true);
    this->addSlider(_("Slider Body Saturation"), 0.0f, 1.0f, &cv::slider_body_color_saturation, 200.0f, true);
    this->addCheckbox(
        _("Use slidergradient.png"),
        _("Enabling this will improve performance,\nbut also block all dynamic slider (color/border) features."),
        &cv::slider_use_gradient_image);
    this->addCheckbox(_("Use osu!lazer Slider Style"),
                      _("Only really looks good if your skin doesn't \"SliderTrackOverride\" too dark."),
                      &cv::slider_osu_next_style);
    this->addCheckbox(_("Use combo color as tint for slider ball"), &cv::slider_ball_tint_combo_color);
    this->addCheckbox(_("Use combo color as tint for slider border"), &cv::slider_border_tint_combo_color);
    this->addCheckbox(_("Draw Slider End Circle"), &cv::slider_draw_endcircle);

    //**************************************************************************************************************************//

    CBaseUIElement *sectionInput = this->addSection(_("Input"));

    this->addSubSection(_("Mouse"), "scroll sensitivity");

    {
        UISlider *sensSlider = this->addSlider(_("Sensitivity:"), 0.1f, 6.0f, &cv::mouse_sensitivity);
        sensSlider->setKeyDelta(0.01f);
        sensSlider->setUpdateRelPosOnChange(true);

        static const RenderCondition sensWarningCondition{
            []() -> bool { return cv::mouse_sensitivity.getFloat() != 1.f; }};
        this->addLabel("");
        this->elemContainers.back()->render_condition = sensWarningCondition;
        this->addLabel(_("WARNING: Set Sensitivity to 1 for tablets!"))->setTextColor(0xffff0000);
        this->elemContainers.back()->render_condition = sensWarningCondition;
        this->addLabel("");
        this->elemContainers.back()->render_condition = sensWarningCondition;
    }

    this->addCheckbox(_("Raw Mouse Input"), _("Not recommended if you're using a tablet."), &cv::mouse_raw_input);
    this->addCheckbox(_("Confine Cursor (Windowed)"), &cv::confine_cursor_windowed);
    this->addCheckbox(_("Confine Cursor (Fullscreen)"), &cv::confine_cursor_fullscreen);
    this->addCheckbox(_("Confine Cursor (NEVER)"), _("Overrides automatic cursor clipping during gameplay."),
                      &cv::confine_cursor_never);
    this->addCheckbox(_("Disable Mouse Wheel in Play Mode"), &cv::disable_mousewheel);
    this->addCheckbox(_("Disable Mouse Buttons in Play Mode"), &cv::disable_mousebuttons);
    this->addCheckbox(_("Cursor ripples"), _("The cursor will ripple outwards on clicking."), &cv::draw_cursor_ripples);

    this->addSpacer();
    const std::string keyboardSectionTags{"keyboard keys key bindings binds keybinds keybindings"};
    CBaseUIElement *subSectionKeyboard = this->addSubSection(_("Keyboard"), keyboardSectionTags);
    if constexpr(Env::cfg(OS::WINDOWS)) {
        this->addCheckbox(_("Raw Keyboard Input"), &cv::keyboard_raw_input);
        if(cv::win_global_media_hotkeys.getDefaultDouble() != -1.) {  // it's set to -1 if it doesn't work
            this->addCheckbox(
                _("Global Media Hotkeys"),
                _("Allows controlling main menu music with\nkeyboard media shortcuts, even while alt-tabbed"),
                &cv::win_global_media_hotkeys);
        }
    }
    UIButton *resetAllKeyBindingsButton = this->addButton(_("Reset all key bindings"));
    resetAllKeyBindingsButton->setColor(0xffd90000);
    resetAllKeyBindingsButton->setClickCallback(
        SA::MakeDelegate<&OptionsOverlayImpl::onKeyBindingsResetAllPressed>(this));
    this->addSubSection(_("Keys - In-Game"), keyboardSectionTags);
    this->addKeyBindButton(_("Left Click"), &binds::LEFT_CLICK);
    this->addKeyBindButton(_("Right Click"), &binds::RIGHT_CLICK);
    this->addKeyBindButton(_("Left Click (2)"), &binds::LEFT_CLICK_2);
    this->addKeyBindButton(_("Right Click (2)"), &binds::RIGHT_CLICK_2);
    this->addKeyBindButton(_("Smoke"), &binds::SMOKE);
    this->addKeyBindButton(_("Game Pause"), &binds::GAME_PAUSE)->setDisallowLeftMouseClickBinding(true);
    this->addKeyBindButton(_("Skip Cutscene"), &binds::SKIP_CUTSCENE);
    this->addKeyBindButton(_("Toggle Scoreboard"), &binds::TOGGLE_SCOREBOARD);
    this->addKeyBindButton(_("Scrubbing (+ Click Drag!)"), &binds::SEEK_TIME);
    this->addKeyBindButton(_("Quick Seek -5sec <<<"), &binds::SEEK_TIME_BACKWARD);
    this->addKeyBindButton(_("Quick Seek +5sec >>>"), &binds::SEEK_TIME_FORWARD);
    this->addKeyBindButton(_("Increase Local Song Offset"), &binds::INCREASE_LOCAL_OFFSET);
    this->addKeyBindButton(_("Decrease Local Song Offset"), &binds::DECREASE_LOCAL_OFFSET);
    this->addKeyBindButton(_("Quick Retry (hold briefly)"), &binds::QUICK_RETRY);
    this->addKeyBindButton(_("Quick Save"), &binds::QUICK_SAVE);
    this->addKeyBindButton(_("Quick Load"), &binds::QUICK_LOAD);
    this->addKeyBindButton(_("Instant Replay"), &binds::INSTANT_REPLAY);
    this->addSubSection(_("Keys - FPoSu"), keyboardSectionTags);
    this->addKeyBindButton(_("Zoom"), &binds::FPOSU_ZOOM);
    this->addSubSection(_("Keys - Universal"), keyboardSectionTags);
    this->addKeyBindButton(_("Toggle chat"), &binds::TOGGLE_CHAT);
    this->addKeyBindButton(_("Toggle user list"), &binds::TOGGLE_EXTENDED_CHAT);
    if(cv::enable_screenshots.getBool()) {
        this->addKeyBindButton(_("Save Screenshot"), &binds::SAVE_SCREENSHOT);
    }
    this->addKeyBindButton(_("Increase Volume"), &binds::INCREASE_VOLUME);
    this->addKeyBindButton(_("Decrease Volume"), &binds::DECREASE_VOLUME);
    this->addKeyBindButton(_("Disable Mouse Buttons"), &binds::DISABLE_MOUSE_BUTTONS);
    this->addKeyBindButton(_("Toggle Map Background"), &binds::TOGGLE_MAP_BACKGROUND);
    this->addKeyBindButton(_("Boss Key (Minimize)"), &binds::BOSS_KEY);
    this->addKeyBindButton(_("Open Skin Selection Menu"), &binds::OPEN_SKIN_SELECT_MENU);
    this->addSubSection(_("Keys - Song Select"), keyboardSectionTags);
    this->addKeyBindButton(_("Toggle Mod Selection Screen"), &binds::TOGGLE_MODSELECT)
        ->setTooltipText(_("(F1 can not be unbound. This is just an additional key.)"));
    this->addKeyBindButton(_("Random Beatmap"), &binds::RANDOM_BEATMAP)
        ->setTooltipText(_("(F2 can not be unbound. This is just an additional key.)"));
    this->addSubSection(_("Keys - Mod Select"), keyboardSectionTags);
    this->addKeyBindButton(_("Easy"), &binds::MOD_EASY);
    this->addKeyBindButton(_("No Fail"), &binds::MOD_NOFAIL);
    this->addKeyBindButton(_("Half Time"), &binds::MOD_HALFTIME);
    this->addKeyBindButton(_("Hard Rock"), &binds::MOD_HARDROCK);
    this->addKeyBindButton(_("Sudden Death"), &binds::MOD_SUDDENDEATH);
    this->addKeyBindButton(_("Double Time"), &binds::MOD_DOUBLETIME);
    this->addKeyBindButton(_("Hidden"), &binds::MOD_HIDDEN);
    this->addKeyBindButton(_("Flashlight"), &binds::MOD_FLASHLIGHT);
    this->addKeyBindButton(_("Relax"), &binds::MOD_RELAX);
    this->addKeyBindButton(_("Autopilot"), &binds::MOD_AUTOPILOT);
    this->addKeyBindButton(_("Spunout"), &binds::MOD_SPUNOUT);
    this->addKeyBindButton(_("Auto"), &binds::MOD_AUTO);
    this->addKeyBindButton(_("Score V2"), &binds::MOD_SCOREV2);

    //**************************************************************************************************************************//

    CBaseUIElement *sectionGameplay = this->addSection(_("Gameplay"));

    this->addSubSection(_("General"));
    this->backgroundDimSlider = this->addSlider(_("Background Dim:"), 0.0f, 1.0f, &cv::background_dim, 220.0f);
    this->backgroundDimSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    this->backgroundBrightnessSlider =
        this->addSlider(_("Background Brightness:"), 0.0f, 1.0f, &cv::background_brightness, 220.0f);
    this->backgroundBrightnessSlider->setChangeCallback(
        SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    this->addSpacer();
    this->addCheckbox(_("Don't change dim level during breaks"),
                      _("Makes the background basically impossible to see during breaks.\nNot recommended."),
                      &cv::background_dont_fade_during_breaks);
    this->addCheckbox(_("Show approach circle on first \"Hidden\" object"),
                      &cv::show_approach_circle_on_first_hidden_object);
    this->addCheckbox(_("SuddenDeath restart on miss"),
                      _("Skips the failing animation, and instantly restarts like SS/PF."),
                      &cv::mod_suddendeath_restart);
    this->addCheckbox(_("Show Skip Button during Intro"), _("Skip intro to first hitobject."), &cv::skip_intro_enabled);
    this->addCheckbox(_("Show Skip Button during Breaks"), _("Skip breaks in the middle of beatmaps."),
                      &cv::skip_breaks_enabled);
    // FIXME: broken
    // this->addCheckbox(_("Save Failed Scores"), _("Allow failed scores to be saved as F ranks."),
    //                   &cv::save_failed_scores);
    this->addSpacer();
    this->addSubSection(_("Mechanics"), "health drain notelock lock block blocking noteblock");
    this->addCheckbox(
        _("Kill Player upon Failing"),
        _("Enabled: Singleplayer default. You die upon failing and the beatmap stops.\nDisabled: Multiplayer "
          "default. Allows you to keep playing even after failing."),
        &cv::drain_kill);
    this->addCheckbox(_("Disable HP drain"),
                      _("Like NF, but entirely disables HP mechanics. Will block online score submission."),
                      &cv::drain_disabled)
        ->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onModChangingToggle>(this));

    this->addSpacer();
    this->addLabel("");

    OptionsElement *notelockSelect = this->addButton(_("Select [Notelock]"), _("None"), true, &cv::notelock_type);
    ((CBaseUIButton *)notelockSelect->baseElems[0].get())
        ->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onNotelockSelect>(this));
    this->notelockSelectButton = notelockSelect->baseElems[0].get();
    this->notelockSelectLabel = (CBaseUILabel *)notelockSelect->baseElems[1].get();
    this->notelockSelectResetButton = notelockSelect->resetButton.get();
    this->notelockSelectResetButton->setClickCallback(
        SA::MakeDelegate<&OptionsOverlayImpl::onNotelockSelectResetClicked>(this));
    this->addLabel("");
    this->addLabel(_("Info about different notelock algorithms:"))->setTextColor(0xff666666);
    this->addLabel("");
    this->addLabel(_("- McOsu: Auto miss previous circle, always."))->setTextColor(0xff666666);
    this->addLabel(_("- osu!stable: Locked until previous circle is miss."))->setTextColor(0xff666666);
    this->addLabel(_("- osu!lazer 2020: Auto miss previous circle if > time."))->setTextColor(0xff666666);
    this->addLabel("");
    this->addSpacer();
    this->addSubSection(_("Backgrounds"), "image thumbnail");
    this->addCheckbox(_("Load Background Images (!)"),
                      _("NOTE: Disabling this will disable ALL beatmap images everywhere!"),
                      &cv::load_beatmap_background_images);
    this->addCheckbox(_("Draw Background in Beatmap"), &cv::draw_beatmap_background_image);
    this->addCheckbox(_("Draw Background in SongBrowser"),
                      _("NOTE: You can disable this if you always want menu-background."),
                      &cv::draw_songbrowser_background_image);
    this->addCheckbox(_("Draw Background Thumbnails in SongBrowser"), &cv::draw_songbrowser_thumbnails);
    this->addCheckbox(_("Draw Background in Ranking/Results Screen"), &cv::draw_rankingscreen_background_image);
    this->addCheckbox(_("Draw menu-background in Menu"), &cv::draw_menu_background);
    this->addCheckbox(_("Draw menu-background in SongBrowser"),
                      _("NOTE: Only applies if \"Draw Background in SongBrowser\" is disabled."),
                      &cv::draw_songbrowser_menu_background_image);
    this->addSpacer();
    // addCheckbox(_("Show pp on ranking screen"), &cv::rankingscreen_pp);

    this->addSubSection(_("HUD"));
    this->addCheckbox(_("Draw HUD"), _("NOTE: You can also press SHIFT + TAB while playing to toggle this."),
                      &cv::draw_hud);
    this->addCheckbox(
        _("SHIFT + TAB toggles everything"),
        _("Enabled: " PACKAGE_NAME
          " default (toggle \"Draw HUD\")\nDisabled: osu! default (always show hiterrorbar + key overlay)"),
        &cv::hud_shift_tab_toggles_everything);
    this->addSpacer();
    this->addCheckbox(_("Draw Score"), &cv::draw_score);
    this->addCheckbox(_("Draw Combo"), &cv::draw_combo);
    this->addCheckbox(_("Draw Accuracy"), &cv::draw_accuracy);
    this->addCheckbox(_("Draw ProgressBar"), &cv::draw_progressbar);
    this->addCheckbox(_("Draw HitErrorBar"), &cv::draw_hiterrorbar);
    this->addCheckbox(_("Draw HitErrorBar UR"), _("Unstable Rate"), &cv::draw_hiterrorbar_ur);
    this->addCheckbox(_("Draw ScoreBar"), _("Health/HP Bar."), &cv::draw_scorebar);
    this->addCheckbox(
        _("Draw ScoreBar-bg"),
        _("Some skins abuse this as the playfield background image.\nIt is actually just the background image "
          "for the Health/HP Bar."),
        &cv::draw_scorebarbg);
    this->addCheckbox(_("Draw ScoreBoard in singleplayer"), &cv::draw_scoreboard);
    this->addCheckbox(_("Draw ScoreBoard in multiplayer"), &cv::draw_scoreboard_mp);
    this->addCheckbox(_("Draw Key Overlay"), &cv::draw_inputoverlay);
    this->addCheckbox(_("Draw Scrubbing Timeline"), &cv::draw_scrubbing_timeline);
    this->addCheckbox(_("Draw Miss Window on HitErrorBar"), &cv::hud_hiterrorbar_showmisswindow);
    this->addSpacer();
    this->addCheckbox(
        _("Draw Stats: pp"),
        _("Realtime pp counter.\nDynamically calculates earned pp by incrementally updating the star rating."),
        &cv::draw_statistics_pp);
    this->addCheckbox(_("Draw Stats: pp (SS)"), _("Max possible total pp for active mods (full combo + perfect acc)."),
                      &cv::draw_statistics_perfectpp);
    this->addCheckbox(_("Draw Stats: Misses"), _("Number of misses."), &cv::draw_statistics_misses);
    this->addCheckbox(_("Draw Stats: SliderBreaks"), _("Number of slider breaks."), &cv::draw_statistics_sliderbreaks);
    this->addCheckbox(_("Draw Stats: Max Possible Combo"), &cv::draw_statistics_maxpossiblecombo);
    this->addCheckbox(_("Draw Stats: Stars*** (Until Now)"),
                      _("Incrementally updates the star rating (aka \"realtime stars\")."),
                      &cv::draw_statistics_livestars);
    this->addCheckbox(_("Draw Stats: Stars* (Total)"), _("Total stars for active mods."),
                      &cv::draw_statistics_totalstars);
    this->addCheckbox(_("Draw Stats: BPM"), &cv::draw_statistics_bpm);
    this->addCheckbox(_("Draw Stats: AR"), &cv::draw_statistics_ar);
    this->addCheckbox(_("Draw Stats: CS"), &cv::draw_statistics_cs);
    this->addCheckbox(_("Draw Stats: OD"), &cv::draw_statistics_od);
    this->addCheckbox(_("Draw Stats: HP"), &cv::draw_statistics_hp);
    this->addCheckbox(_("Draw Stats: 300 hitwindow"), _("Timing window for hitting a 300 (e.g. +-25ms)."),
                      &cv::draw_statistics_hitwindow300);
    this->addCheckbox(_("Draw Stats: Notes Per Second"), _("How many clicks per second are currently required."),
                      &cv::draw_statistics_nps);
    this->addCheckbox(_("Draw Stats: Note Density"), _("How many objects are visible at the same time."),
                      &cv::draw_statistics_nd);
    this->addCheckbox(_("Draw Stats: Unstable Rate"), &cv::draw_statistics_ur);
    this->addCheckbox(_("Draw Stats: Accuracy Error"),
                      _("Average hit error delta (e.g. -5ms +15ms).\nSee \"hud_statistics_hitdelta_chunksize 30\",\nit "
                        "defines how many recent hit deltas are averaged."),
                      &cv::draw_statistics_hitdelta);
    this->addSpacer();
    this->hudSizeSlider = this->addSlider(_("HUD Scale:"), 0.01f, 3.0f, &cv::hud_scale, 165.0f);
    this->hudSizeSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    this->hudSizeSlider->setKeyDelta(0.01f);
    this->addSpacer();
    this->hudScoreScaleSlider = this->addSlider(_("Score Scale:"), 0.01f, 3.0f, &cv::hud_score_scale, 165.0f);
    this->hudScoreScaleSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    this->hudScoreScaleSlider->setKeyDelta(0.01f);
    this->hudComboScaleSlider = this->addSlider(_("Combo Scale:"), 0.01f, 3.0f, &cv::hud_combo_scale, 165.0f);
    this->hudComboScaleSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    this->hudComboScaleSlider->setKeyDelta(0.01f);
    this->hudAccuracyScaleSlider = this->addSlider(_("Accuracy Scale:"), 0.01f, 3.0f, &cv::hud_accuracy_scale, 165.0f);
    this->hudAccuracyScaleSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    this->hudAccuracyScaleSlider->setKeyDelta(0.01f);
    this->hudHiterrorbarScaleSlider =
        this->addSlider(_("HitErrorBar Scale:"), 0.01f, 3.0f, &cv::hud_hiterrorbar_scale, 165.0f);
    this->hudHiterrorbarScaleSlider->setChangeCallback(
        SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    this->hudHiterrorbarScaleSlider->setKeyDelta(0.01f);
    this->hudHiterrorbarURScaleSlider =
        this->addSlider(_("HitErrorBar UR Scale:"), 0.01f, 3.0f, &cv::hud_hiterrorbar_ur_scale, 165.0f);
    this->hudHiterrorbarURScaleSlider->setChangeCallback(
        SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    this->hudHiterrorbarURScaleSlider->setKeyDelta(0.01f);
    this->hudProgressbarScaleSlider =
        this->addSlider(_("ProgressBar Scale:"), 0.01f, 3.0f, &cv::hud_progressbar_scale, 165.0f);
    this->hudProgressbarScaleSlider->setChangeCallback(
        SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    this->hudProgressbarScaleSlider->setKeyDelta(0.01f);
    this->hudScoreBarScaleSlider = this->addSlider(_("ScoreBar Scale:"), 0.01f, 3.0f, &cv::hud_scorebar_scale, 165.0f);
    this->hudScoreBarScaleSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    this->hudScoreBarScaleSlider->setKeyDelta(0.01f);
    this->hudScoreBoardScaleSlider =
        this->addSlider(_("ScoreBoard Scale:"), 0.01f, 3.0f, &cv::hud_scoreboard_scale, 165.0f);
    this->hudScoreBoardScaleSlider->setChangeCallback(
        SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    this->hudScoreBoardScaleSlider->setKeyDelta(0.01f);
    this->hudInputoverlayScaleSlider =
        this->addSlider(_("Key Overlay Scale:"), 0.01f, 3.0f, &cv::hud_inputoverlay_scale, 165.0f);
    this->hudInputoverlayScaleSlider->setChangeCallback(
        SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    this->hudInputoverlayScaleSlider->setKeyDelta(0.01f);
    this->statisticsOverlayScaleSlider =
        this->addSlider(_("Statistics Scale:"), 0.01f, 3.0f, &cv::hud_statistics_scale, 165.0f);
    this->statisticsOverlayScaleSlider->setChangeCallback(
        SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));
    this->statisticsOverlayScaleSlider->setKeyDelta(0.01f);
    this->addSpacer();
    this->statisticsOverlayXOffsetSlider =
        this->addSlider(_("Statistics X Offset:"), 0.0f, 2000.0f, &cv::hud_statistics_offset_x, 165.0f, true);
    this->statisticsOverlayXOffsetSlider->setChangeCallback(
        SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangeInt>(this));
    this->statisticsOverlayXOffsetSlider->setKeyDelta(1.0f);
    this->statisticsOverlayYOffsetSlider =
        this->addSlider(_("Statistics Y Offset:"), 0.0f, 1000.0f, &cv::hud_statistics_offset_y, 165.0f, true);
    this->statisticsOverlayYOffsetSlider->setChangeCallback(
        SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangeInt>(this));
    this->statisticsOverlayYOffsetSlider->setKeyDelta(1.0f);

    this->addSubSection(_("Playfield"));
    this->addCheckbox(_("Draw FollowPoints"), &cv::draw_followpoints);
    this->addCheckbox(_("Draw Playfield Border"), _("Correct border relative to the current Circle Size."),
                      &cv::draw_playfield_border);
    this->addSpacer();
    this->playfieldBorderSizeSlider =
        this->addSlider(_("Playfield Border Size:"), 0.0f, 500.0f, &cv::hud_playfield_border_size);
    this->playfieldBorderSizeSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangeInt>(this));
    this->playfieldBorderSizeSlider->setKeyDelta(1.0f);

    this->addSubSection(_("Hitobjects"));
    this->addCheckbox(
        _("Use Fast Hidden Fading Sliders (!)"),
        _("NOTE: osu! doesn't do this, so don't enable it for serious practicing.\nIf enabled: Fade out sliders "
          "with the same speed as circles."),
        &cv::mod_hd_slider_fast_fade);

    //**************************************************************************************************************************//

    CBaseUIElement *sectionFposu = this->addSection(_("FPoSu (3D)"));

    this->addSubSection(_("FPoSu - General"));
    this->addCheckbox(_("FPoSu"),
                      _("The real 3D FPS mod.\nPlay from a first person shooter perspective in a 3D environment.\nThis "
                        "is only intended for mouse! (Enable \"Tablet/Absolute Mode\" for tablets.)"),
                      &cv::mod_fposu);
    this->addLabel("");
    this->addLabel(_("NOTE: Use CTRL + O during gameplay to get here!"))->setTextColor(0xff555555);
    this->addLabel("");
    this->addLabel(_("LEFT/RIGHT arrow keys to precisely adjust sliders."))->setTextColor(0xff555555);
    this->addLabel("");
    CBaseUISlider *fposuDistanceSlider = this->addSlider(_("Distance:"), 0.01f, 5.0f, &cv::fposu_distance, -1.0f, true);
    fposuDistanceSlider->setKeyDelta(0.01f);
    this->addCheckbox(_("Vertical FOV"), _("If enabled: Vertical FOV.\nIf disabled: Horizontal FOV (default)."),
                      &cv::fposu_vertical_fov);
    CBaseUISlider *fovSlider = this->addSlider(_("FOV:"), 10.0f, 160.0f, &cv::fposu_fov, -1.0f, true, true);
    fovSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangeTwoDecimalPlaces>(this));
    fovSlider->setKeyDelta(0.01f);
    CBaseUISlider *zoomedFovSlider =
        this->addSlider(_("FOV (Zoom):"), 10.0f, 160.0f, &cv::fposu_zoom_fov, -1.0f, true, true);
    zoomedFovSlider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangeTwoDecimalPlaces>(this));
    zoomedFovSlider->setKeyDelta(0.01f);
    this->addCheckbox(_("Zoom Key Toggle"),
                      _("Enabled: Zoom key toggles zoom.\nDisabled: Zoom while zoom key is held."),
                      &cv::fposu_zoom_toggle);
    this->addSubSection(_("FPoSu - Playfield"));
    this->addCheckbox(_("Curved play area"), &cv::fposu_curved);
    this->addCheckbox(_("Background cube"), &cv::fposu_cube);
    this->addCheckbox(_("Skybox"),
                      _("NOTE: Overrides \"Background cube\".\nSee skybox_example.png for cubemap layout."),
                      &cv::fposu_skybox);
    this->addSlider(_("Background Opacity"), 0.0f, 1.0f, &cv::background_alpha)
        ->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChangePercent>(this));

    this->addSubSection(_("FPoSu - Mouse"));

    UIButton *cm360CalculatorLinkButton = this->addButton(_("https://www.mouse-sensitivity.com/"));
    cm360CalculatorLinkButton->setClickCallback(SA::MakeDelegate([]() -> void {
        ui->getNotificationOverlay()->addNotification(_("Opening browser, please wait ..."), 0xffffffff, false, 0.75f);
        Environment::openURLInDefaultBrowser("https://www.mouse-sensitivity.com/");
    }));

    cm360CalculatorLinkButton->setColor(0xff0e4a59);
    this->addLabel("");
    this->dpiTextbox = this->addTextbox(cv::fposu_mouse_dpi.getString(), _("DPI:"), &cv::fposu_mouse_dpi);
    this->cm360Textbox =
        this->addTextbox(cv::fposu_mouse_cm_360.getString(), _("cm per 360:"), &cv::fposu_mouse_cm_360);
    this->addLabel("");
    this->addCheckbox(_("Invert Vertical"), &cv::fposu_invert_vertical);
    this->addCheckbox(_("Invert Horizontal"), &cv::fposu_invert_horizontal);
    this->addCheckbox(_("Tablet/Absolute Mode (!)"),
                      _("Don't enable this if you are using a mouse.\nIf this is enabled, then DPI and cm per "
                        "360 will be ignored!"),
                      &cv::fposu_absolute_mode);

    //**************************************************************************************************************************//

    // TRANSLATORS: Name of a settings category containing joke/unnecessary options
    this->addSection(_("Bullshit"));

    // TRANSLATORS: Subcategory of "Bullshit" - contains joke visual settings (rainbow circles, etc.)
    this->addSubSection(_("Why"));
    this->addCheckbox(_("Rainbow Circles"), &cv::circle_rainbow);
    this->addCheckbox(_("Rainbow Sliders"), &cv::slider_rainbow);
    this->addCheckbox(_("Rainbow Numbers"), &cv::circle_number_rainbow);
    this->addCheckbox(_("Draw 300s"), &cv::hitresult_draw_300s);

    auto *sectionMisc = this->addSection(_("Miscellaneous"));
#ifndef MCENGINE_PLATFORM_WASM
    this->addSubSection(_("Testing"));
    this->addCheckbox(_("Use bleeding edge release stream"), &cv::bleedingedge);
#endif

    if(cv::enable_screenshots.getBool()) {  // TODO: why is this even a convar (its only used during constructor)
        this->addSubSection(_("Screenshots"));
        this->addCheckbox(_("Crop Screenshots"),
                          _("Crop screenshots to the letterbox resolution,\nif letterboxing is enabled."),
                          &cv::crop_screenshots);
        this->addCheckbox(_("Copy Screenshots to Clipboard"),
                          _("If screenshots should be copied to the system clipboard\nalong with saving them to the "
                            "screenshots/ folder."),
                          &cv::screenshot_clipboard);
    }

    this->addSubSection(_("Nags"), "tip menu social links");

    this->addCheckbox(_("Show Main Menu Tips"), &cv::main_menu_tips);
    this->addCheckbox(_("Hide Discord/X Links"), &cv::adblock);

    this->addSubSection(_("Import/Reset"), "mcosu stable");

#ifndef MCENGINE_PLATFORM_WASM
    UIButton *importMcOsuSettingsButton = this->addButton(_("Import collections/scores/settings from McOsu"));
    importMcOsuSettingsButton->setClickCallback(SA::MakeDelegate([]() -> void {
        auto conclude_import = [](bool success) {
            if(success) {
                // To finish importing collections.db/scores.db, we need to trigger a database reload
                if(db->isFinished() || db->isCancelled()) {
                    // TODO: bug prone since it can be called from any state...
                    ui->getSongBrowser()->refreshBeatmaps(ui->getActiveScreen());
                }

                ui->getNotificationOverlay()->addToast(_("Successfully imported settings from McOsu."), SUCCESS_TOAST);

            } else {
                ui->getNotificationOverlay()->addToast(
                    _("Couldn't find McOsu config files! Make sure to select the directory containing McOsu.exe."),
                    ERROR_TOAST);
            }
        };

        bool imported = SettingsImporter::import_from_mcosu();
        if(imported) {
            conclude_import(true);
            return;
        }

        ui->getNotificationOverlay()->addNotification(_("Opening file browser ..."), 0xffffffff, false, 0.75f);
        env->openFolderWindow([conclude_import](const std::vector<std::string> &paths) {
            if(paths.empty()) {
                ui->getNotificationOverlay()->addToast(_("You must select the McOsu folder to import its settings."),
                                                       ERROR_TOAST);
                return;
            }

            // use the first selected path
            const std::string &mcosu_path = paths[0];
            const bool imported = SettingsImporter::import_from_mcosu(mcosu_path);
            conclude_import(imported);
        });
    }));
#endif

    UIButton *resetAllSettingsButton = this->addButton(_("Reset all settings"));
    resetAllSettingsButton->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onResetEverythingClicked>(this));
    resetAllSettingsButton->setColor(0xffd90000);

    this->addSpacer();
    this->addSpacer();
    this->addSpacer();
    this->addSpacer();
    this->addSpacer();
    this->addSpacer();
    this->addSpacer();
    this->addSpacer();

    //**************************************************************************************************************************//

    // build categories
    this->addCategory(this->sectionGeneral, Icons::GEAR);
    this->addCategory(sectionGraphics, Icons::DESKTOP);
    this->addCategory(sectionAudio, Icons::VOLUME_UP);
    this->addCategory(this->skinSection, Icons::PAINTBRUSH);
    this->addCategory(sectionInput, Icons::GAMEPAD);
    this->addCategory(subSectionKeyboard, Icons::KEYBOARD);
    this->addCategory(sectionGameplay, Icons::CIRCLE);
    this->fposuCategoryButton = this->addCategory(sectionFposu, Icons::CUBE);
    this->addCategory(sectionMisc, Icons::WRENCH);

    //**************************************************************************************************************************//

    // the context menu gets added last (drawn on top of everything)
    this->options->container.addBaseUIElement(this->contextMenu);

    // HACKHACK: force current value update
    if(this->sliderQualitySlider != nullptr)
        this->onHighQualitySlidersConVarChange(cv::options_high_quality_sliders.getFloat());

    this->elemContainers.shrink_to_fit();
}

OptionsOverlayImpl::~OptionsOverlayImpl() {
    this->options->invalidate();

    SAFE_DELETE(this->spacer);
    SAFE_DELETE(this->contextMenu);
    this->elemContainers.clear();

    cv::skin_use_skin_hitsounds.reset();
    cv::options_slider_quality.reset();
    cv::options_high_quality_sliders.reset();
    cv::rich_presence_map_backgrounds.reset();
    cv::snd_soloud_backend.removeChangeCallback();  // SoLoudSoundEngine sets a single-arg callback
}

void OptionsOverlayImpl::draw() {
    const bool isAnimating = this->fAnimation.animating();
    if(!parent->bVisible && !isAnimating) {
        this->contextMenu->draw();
        return;
    }

    this->sliderPreviewElement->setDrawSliderHack(!isAnimating);

    if(isAnimating) {
        osu->getSliderFrameBuffer()->enable();

        g->setBlendMode(DrawBlendMode::PREMUL_ALPHA);
    }

    const bool isPlayingBeatmap = osu->isInPlayMode();

    // interactive sliders

    if(this->backgroundBrightnessSlider->isActive()) {
        if(!isPlayingBeatmap) {
            const float brightness = std::clamp<float>(this->backgroundBrightnessSlider->getFloat(), 0.0f, 1.0f);
            const short red = std::clamp<float>(brightness * cv::background_color_r.getFloat(), 0.0f, 255.0f);
            const short green = std::clamp<float>(brightness * cv::background_color_g.getFloat(), 0.0f, 255.0f);
            const short blue = std::clamp<float>(brightness * cv::background_color_b.getFloat(), 0.0f, 255.0f);
            if(brightness > 0.0f) {
                g->setColor(argb(255, red, green, blue));
                g->fillRect(0, 0, osu->getVirtScreenWidth(), osu->getVirtScreenHeight());
            }
        }
    }

    if(this->backgroundDimSlider->isActive()) {
        if(!isPlayingBeatmap) {
            const short dim = std::clamp<float>(this->backgroundDimSlider->getFloat(), 0.0f, 1.0f) * 255.0f;
            g->setColor(argb(dim, 0, 0, 0));
            g->fillRect(0, 0, osu->getVirtScreenWidth(), osu->getVirtScreenHeight());
        }
    }

    parent->ScreenBackable::draw();

    if(this->hudSizeSlider->isActive() || this->hudComboScaleSlider->isActive() ||
       this->hudScoreScaleSlider->isActive() || this->hudAccuracyScaleSlider->isActive() ||
       this->hudHiterrorbarScaleSlider->isActive() || this->hudHiterrorbarURScaleSlider->isActive() ||
       this->hudProgressbarScaleSlider->isActive() || this->hudScoreBarScaleSlider->isActive() ||
       this->hudScoreBoardScaleSlider->isActive() || this->hudInputoverlayScaleSlider->isActive() ||
       this->statisticsOverlayScaleSlider->isActive() || this->statisticsOverlayXOffsetSlider->isActive() ||
       this->statisticsOverlayYOffsetSlider->isActive()) {
        if(!isPlayingBeatmap) ui->getHUD()->drawDummy();
    } else if(this->playfieldBorderSizeSlider->isActive()) {
        ui->getHUD()->drawPlayfieldBorder(GameRules::getPlayfieldCenter(), GameRules::getPlayfieldSize(), 100);
    }

    // Re-drawing context menu to make sure it's drawn on top of the back button
    // Context menu input still gets processed first, so this is fine
    this->contextMenu->draw();  // TODO: remove this (double-drawing hurts performance and increases opacity)

    if(this->cursorSizeSlider->getFloat() < 0.15f) mouse->drawDebug();

    if(isAnimating) {
        // HACKHACK:
        if(!parent->bVisible) parent->backButton->draw();

        g->setBlendMode(DrawBlendMode::ALPHA);

        osu->getSliderFrameBuffer()->disable();

        g->push3DScene(McRect(0, 0, this->options->getSize().x, this->options->getSize().y));
        {
            g->rotate3DScene(0, -(1.0f - this->fAnimation) * 90, 0);
            g->translate3DScene(-(1.0f - this->fAnimation) * this->options->getSize().x * 1.25f, 0,
                                -(1.0f - this->fAnimation) * 700);

            osu->getSliderFrameBuffer()->setColor(argb((f32)this->fAnimation, 1.0f, 1.0f, 1.0f));
            osu->getSliderFrameBuffer()->draw(0, 0);
        }
        g->pop3DScene();
    }
}

void OptionsOverlayImpl::update_login_button(bool loggedIn) {
    if(loggedIn || BanchoState::is_online()) {
        this->logInButton->setText(_("Disconnect"));
        this->logInButton->setColor(0xffd90000);
    } else if(BanchoState::get_online_status() == OnlineStatus::POLLING) {
        this->logInButton->setText(_("Waiting for browser..."));
        this->logInButton->setColor(0xffd9d900);
    } else {
        this->logInButton->setText(_("Log in"));
        this->logInButton->setColor(0xff00d900);
    }
    this->logInButton->is_loading = BanchoState::is_logging_in();
}

void OptionsOverlayImpl::update(CBaseUIEventCtx &c) {
    // handle skin folder loading finish
    if(this->skinFolderEnumHandle.valid() && this->skinFolderEnumHandle.is_ready()) {
        auto skinFolders = this->skinFolderEnumHandle.get();
        this->onSkinSelectFoldersFinished(skinFolders);
    }

    if(this->bLayoutUpdateScheduled.load(std::memory_order_relaxed)) {
        this->updateLayout();
    }

    const bool optionsMenuVisible = parent->bVisible;
    const bool contextMenuVisible = this->contextMenu->isVisible();

    if(!optionsMenuVisible && !contextMenuVisible) return;
    const bool onlyContextMenuVisible = contextMenuVisible && !optionsMenuVisible;

    // force context menu click handling focus
    this->contextMenu->update(c);
    if(c.mouse_consumed()) return;
    if(onlyContextMenuVisible) return;  // HACK: not returning early if options menu is hidden, for skins menu dropdown

    // force context menu mouse-inside focus
    // NOTE: "propagate_clicks" does not solve the issue of mouse hover focus
    // this hack is still required, otherwise hovering underneath e.g. skin dropdown will play duplicate sounds for
    // each element selected (at all z-orders!)
    if(contextMenuVisible) {
        parent->backButton->stealFocus();
    }

    // disable widgets bound to a server/skin-forced cvar before their update runs,
    // so input handlers + their own tooltip code don't fire for forced settings
    this->applyForcedCvarLocks();

    parent->ScreenBackable::update(c);
    if(c.mouse_consumed()) return;

    // and show a single "forced by ..." tooltip when hovering any locked widget
    this->pushForcedCvarTooltipIfHovered();

    if(contextMenuVisible) {
        // eyes are bleeding...
        for(auto e : this->options->container.getElements()) {
            if(e == this->contextMenu) continue;
            e->stealFocus();
        }
    }

    if(this->bDPIScalingScrollToSliderScheduled) {
        this->bDPIScalingScrollToSliderScheduled = false;
        this->options->scrollToElement(this->uiScaleSlider, 0, 200 * Osu::getUIScale());
    }

    // flash osu!folder textbox red if incorrect
    if(this->fOsuFolderTextboxInvalidAnim > engine->getTime()) {
        float redness = std::fabs(std::sin((this->fOsuFolderTextboxInvalidAnim - engine->getTime()) * 3.)) * 0.5f;
        this->osuFolderTextbox->setBackgroundColor(argb(1.f, redness, 0.f, 0.f));
    } else
        this->osuFolderTextbox->setBackgroundColor(0xff000000);

    // hack to avoid entering search text while binding keys
    if(ui->getNotificationOverlay()->isVisible() && ui->getNotificationOverlay()->isWaitingForKey())
        this->fSearchOnCharKeybindHackTime = engine->getTime() + 0.1f;

    // highlight active category depending on scroll position
    if(this->categoryButtons.size() > 0) {
        CategoryButton *activeCategoryButton = this->categoryButtons[0];
        for(auto categoryButton : this->categoryButtons) {
            if(categoryButton != nullptr && categoryButton->getSection() != nullptr) {
                categoryButton->setActiveCategory(false);
                categoryButton->setTextColor(0xff737373);

                if(categoryButton->getSection()->getPos().y < this->options->getSize().y * 0.4)
                    activeCategoryButton = categoryButton;
            }
        }
        if(activeCategoryButton != nullptr) {
            activeCategoryButton->setActiveCategory(true);
            activeCategoryButton->setTextColor(0xffffffff);
        }
    }

    // delayed update letterboxing mouse scale/offset settings
    if(this->bLetterboxingOffsetUpdateScheduled) {
        if(!this->letterboxingOffsetXSlider->isActive() && !this->letterboxingOffsetYSlider->isActive()) {
            this->bLetterboxingOffsetUpdateScheduled = false;

            cv::letterboxing_offset_x.setValue(this->letterboxingOffsetXSlider->getFloat());
            cv::letterboxing_offset_y.setValue(this->letterboxingOffsetYSlider->getFloat());

            // and update reset buttons as usual
            this->onResetUpdate(this->letterboxingOffsetResetButton);
        }
    }

    if(this->bUIScaleScrollToSliderScheduled) {
        this->bUIScaleScrollToSliderScheduled = false;
        this->options->scrollToElement(this->uiScaleSlider, 0, 200 * Osu::getUIScale());
    }

    // delayed UI scale change
    if(this->bUIScaleChangeScheduled) {
        if(!this->uiScaleSlider->isActive()) {
            this->bUIScaleChangeScheduled = false;

            const float oldUIScale = Osu::getUIScale();

            cv::ui_scale.setValue(this->uiScaleSlider->getFloat());

            const float newUIScale = Osu::getUIScale();

            // and update reset buttons as usual
            this->onResetUpdate(this->uiScaleResetButton);

            // additionally compensate scroll pos (but delay 1 frame)
            if(oldUIScale != newUIScale) this->bUIScaleScrollToSliderScheduled = true;
        }
    }

    // delayed WASAPI buffer/period change
    if(this->bASIOBufferChangeScheduled) {
        if(!this->asioBufferSizeSlider->isActive()) {
            this->bASIOBufferChangeScheduled = false;

            cv::asio_buffer_size.setValue(this->asioBufferSizeSlider->getFloat());

            // and update reset buttons as usual
            this->onResetUpdate(this->asioBufferSizeResetButton);
        }
    }

    if(!cv::win_snd_wasapi_event_callbacks.getBool()) {
        if(this->bWASAPIBufferChangeScheduled) {
            if(!this->wasapiBufferSizeSlider->isActive()) {
                this->bWASAPIBufferChangeScheduled = false;

                cv::win_snd_wasapi_buffer_size.setValue(this->wasapiBufferSizeSlider->getFloat());

                // and update reset buttons as usual
                this->onResetUpdate(this->wasapiBufferSizeResetButton);
            }
        }

        if(this->bWASAPIPeriodChangeScheduled) {
            if(!this->wasapiPeriodSizeSlider->isActive()) {
                this->bWASAPIPeriodChangeScheduled = false;

                // ignore if event callback mode is enabled
                cv::win_snd_wasapi_period_size.setValue(this->wasapiPeriodSizeSlider->getFloat());

                // and update reset buttons as usual
                this->onResetUpdate(this->wasapiPeriodSizeResetButton);
            }
        }
    }

    // apply textbox changes on enter key
    if(this->osuFolderTextbox->hitEnter()) cv::osu_folder.setValue(this->osuFolderTextbox->getText());

    // HACKHACK (should just use callback)
    // XXX: disable serverTextbox while logging in
    // XXX: jank logic split between update_login_button/setLogingLoadingState
    const auto server_text{this->serverTextbox->getText()};
    if(cv::mp_server.getString() != server_text) {
        cv::mp_server.setValue(server_text);
        this->update_login_button();
        this->scheduleLayoutUpdate();  // toggle user/pass fields based on oauthness
    }

    // why does all of this need to run in update()...
    const auto name_text{this->nameTextbox->getText()};
    if(!name_text.empty() && cv::name.getString() != name_text) {
        cv::name.setValue(name_text);
    }
    const auto password_text{this->passwordTextbox->getText()};
    if(!password_text.empty() && password_text != cv::mp_password_md5.getString()) {
        // passwordTextbox gets overwritten with mp_password_md5 on login
        // mp_password should remain empty after that
        cv::mp_password.setValue(password_text);
    }
    if(this->nameTextbox->hitEnter()) {
        this->nameTextbox->stealFocus();
        this->passwordTextbox->focus();
    }
    if(this->passwordTextbox->hitEnter()) {
        this->passwordTextbox->stealFocus();
        this->logInButton->click();
    }
    if(this->serverTextbox->hitEnter()) {
        this->serverTextbox->stealFocus();
        this->logInButton->click();
    }

    if(this->dpiTextbox != nullptr && this->dpiTextbox->hitEnter()) this->updateFposuDPI();
    if(this->cm360Textbox != nullptr && this->cm360Textbox->hitEnter()) this->updateFposuCMper360();
}

void OptionsOverlayImpl::onKeyDown(KeyboardEvent &e) {
    if(!parent->bVisible) {
        // allow closing skins dropdown even when options menu isn't open
        // this is such a hack...
        if(this->contextMenu->isVisible()) this->contextMenu->onKeyDown(e);
        return;
    }

    this->contextMenu->onKeyDown(e);
    if(e.isConsumed()) return;

    if(e.getScanCode() == KEY_TAB) {
        if(this->serverTextbox->isActive()) {
            this->serverTextbox->stealFocus();
            this->nameTextbox->focus();
            e.consume();
            return;
        } else if(this->nameTextbox->isActive()) {
            this->nameTextbox->stealFocus();
            this->passwordTextbox->focus();
            e.consume();
            return;
        }
    }

    // searching text delete
    if(!this->sSearchString.empty()) {
        switch(e.getScanCode()) {
            case KEY_DELETE:
            case KEY_BACKSPACE: {
                std::u32string uSearch{UniString::to_utf32(this->sSearchString)};
                if(keyboard->isControlDown()) {
                    // delete everything from the current caret position to the left, until after the first
                    // non-space character (but including it)
                    bool foundNonSpaceChar = false;
                    while(!uSearch.empty()) {
                        const auto &curChar = uSearch.back();

                        const bool whitespace = std::iswspace(static_cast<wint_t>(curChar)) != 0;
                        if(foundNonSpaceChar && whitespace) break;

                        if(!whitespace) foundNonSpaceChar = true;

                        uSearch.pop_back();
                    }
                } else {
                    uSearch.pop_back();
                }
                this->sSearchString = UniString::to_utf8(uSearch);

                e.consume();
                this->scheduleSearchUpdate();
                return;
            }

            case KEY_ESCAPE:
                this->sSearchString.clear();
                this->scheduleSearchUpdate();
                e.consume();
                return;
            default:
                break;
        }
    }

    parent->ScreenBackable::onKeyDown(e);

    // paste clipboard support
    if(!e.isConsumed() && keyboard->isControlDown() && e == KEY_V) {
        const auto clipstring = env->getClipBoardText();
        if(!clipstring.empty()) {
            this->sSearchString.append(clipstring);
            this->scheduleSearchUpdate();
            e.consume();
            return;
        }
    }

    // Consuming all keys so they're not forwarded to main menu or chat when searching for a setting
    e.consume();
}

void OptionsOverlayImpl::onChar(KeyboardEvent &e) {
    if(!parent->bVisible) return;

    parent->ScreenBackable::onChar(e);
    if(e.isConsumed()) return;

    // handle searching
    if(e.getCharCode() < 32 || !parent->bVisible ||
       ((keyboard->isSuperDown() || (keyboard->isControlDown() && !keyboard->isAltDown()))) ||
       this->fSearchOnCharKeybindHackTime > engine->getTime())
        return;

    char32_t ch = e.getCharCode();
    std::u32string_view uChar{&ch, 1};
    this->sSearchString.append(UniString::to_utf8(uChar));

    this->scheduleSearchUpdate();

    e.consume();
}

void OptionsOverlayImpl::onResolutionChange(vec2 newResolution) {
    parent->ScreenBackable::onResolutionChange(newResolution);

    // HACKHACK: magic
    if constexpr(Env::cfg(OS::WINDOWS)) {
        if((env->winFullscreened() && (int)newResolution.y == (int)env->getNativeScreenSize().y + 1)) newResolution.y--;
    }

    if(this->resolutionLabel != nullptr)
        this->resolutionLabel->setText(fmt::format("{}x{}", (int)newResolution.x, (int)newResolution.y));
}

void OptionsOverlayImpl::onKey(KeyboardEvent &e) {
    // from NotificationOverlay
    if(this->waitingKey != nullptr) {
        if(e.getScanCode() != this->waitingKey->getVal<SCANCODE>()) {
            this->bLayoutUpdateScheduled.store(true, std::memory_order_relaxed);
        }
        this->waitingKey->setValue(e.getScanCode());
        this->waitingKey = nullptr;
    }
}

CBaseUIContainer *OptionsOverlayImpl::setVisible(bool visible) {
    this->setVisibleInt(visible, false);
    return parent;
}

void OptionsOverlayImpl::setVisibleInt(bool visible, bool fromOnBack) {
    if(visible != parent->bVisible) {
        // open/close animation
        if(!parent->bVisible)
            this->fAnimation.set(1.0f, 0.25f * (1.0f - this->fAnimation), anim::QuartOut);
        else
            this->fAnimation.set(0.0f, 0.25f * this->fAnimation, anim::QuadOut);

        // save even if not closed via onBack(), e.g. if closed via setVisible(false) from outside
        if(!visible && !fromOnBack) {
            ui->getNotificationOverlay()->stopWaitingForKey();
            this->save();
        }
    }

    parent->bVisible = visible;
    ui->getChat()->updateVisibility();

    if(visible) {
        this->updateLayout();
    } else {
        this->contextMenu->setVisible2(false);
    }

    // usability: auto scroll to fposu settings if opening options while in fposu gamemode
    if(visible && osu->isInPlayMode() && cv::mod_fposu.getBool() && !this->fposuCategoryButton->isActiveCategory())
        this->onCategoryClicked(this->fposuCategoryButton);

    if(visible) {
        // reset reset counters
        this->iNumResetAllKeyBindingsPressed = 0;
        this->iNumResetEverythingPressed = 0;
    }
}

void OptionsOverlayImpl::setUsername(std::string username) { this->nameTextbox->setText(std::move(username)); }

bool OptionsOverlayImpl::isMouseInside() {
    return (parent->backButton->isMouseInside() || this->options->isMouseInside() ||
            this->categories->isMouseInside()) &&
           parent->isVisible();
}

bool OptionsOverlayImpl::isBusy() {
    return (parent->backButton->isActive() || this->options->isBusy() || this->categories->isBusy()) &&
           parent->isVisible();
}

void OptionsOverlayImpl::scheduleLayoutUpdate() { this->bLayoutUpdateScheduled.store(true, std::memory_order_release); }

void OptionsOverlayImpl::updateLayout() {
    this->bLayoutUpdateScheduled.store(false, std::memory_order_release);
    this->updating_layout = true;

    const bool oauth = this->should_use_oauth_login();
    this->update_login_button();

    // set all elements to the current convar values, and update the reset button states
    for(const auto &element : this->elemContainers) {
        if(!element->cvar) continue;
        for(const auto &baseElem : element->baseElems) {
            switch(element->type) {
                case CBX:
                case CBX_BTN: {
                    auto *checkboxPointer = dynamic_cast<CBaseUICheckbox *>(baseElem.get());
                    if(checkboxPointer != nullptr) checkboxPointer->setChecked(element->cvar->getBool());
                } break;
                case SLDR:
                    if(element->baseElems.size() == 3) {
                        auto *sliderPointer = dynamic_cast<CBaseUISlider *>(element->baseElems[1].get());
                        if(sliderPointer != nullptr) {
                            // allow users to overscale certain values via the console
                            if(element->allowOverscale && element->cvar->getFloat() > sliderPointer->getMax())
                                sliderPointer->setBounds(sliderPointer->getMin(), element->cvar->getFloat());

                            // allow users to underscale certain values via the console
                            if(element->allowUnderscale && element->cvar->getFloat() < sliderPointer->getMin())
                                sliderPointer->setBounds(element->cvar->getFloat(), sliderPointer->getMax());

                            sliderPointer->setValue(element->cvar->getFloat(), false);
                            sliderPointer->fireChangeCallback();
                        }
                    }
                    break;
                case TBX:
                    if(element->baseElems.size() == 1) {
                        auto *textboxPointer = dynamic_cast<CBaseUITextbox *>(element->baseElems[0].get());
                        if(textboxPointer != nullptr) {
                            // HACKHACK: don't override textbox with mp_password (which gets deleted on login)
                            std::string textToSet{element->cvar->getString()};
                            if(element->cvar == &cv::mp_password && cv::mp_password.getString().empty()) {
                                textToSet = cv::mp_password_md5.getString();
                            }
                            textboxPointer->setText(std::move(textToSet));
                        }
                    } else if(element->baseElems.size() == 2) {
                        auto *textboxPointer = dynamic_cast<CBaseUITextbox *>(element->baseElems[1].get());
                        if(textboxPointer != nullptr) textboxPointer->setText(element->cvar->getString());
                    }
                    break;
                default:
                    break;
            }
        }

        this->onResetUpdate(element->resetButton.get());
    }

    if(this->fullscreenCheckbox != nullptr) this->fullscreenCheckbox->setChecked(env->winFullscreened(), false);

    this->updateSkinNameLabel();
    this->updateNotelockSelectLabel();

    if(this->outputDeviceLabel != nullptr)
        this->outputDeviceLabel->setText(std::string{soundEngine->getOutputDeviceName()});

    this->onOutputDeviceResetUpdate();
    this->onNotelockSelectResetUpdate();

    //************************************************************************************************************************************//

    // TODO: correctly scale individual UI elements to dpiScale (depend on initial value in e.g. addCheckbox())

    parent->ScreenBackable::updateLayout();

    const float dpiScale = Osu::getUIScale();

    parent->setSize(osu->getVirtScreenSize());

    // options panel
    const float optionsScreenWidthPercent = 0.5f;
    const float categoriesOptionsPercent = 0.135f;

    int optionsWidth = (int)(osu->getVirtScreenWidth() * optionsScreenWidthPercent);
    optionsWidth = std::min((int)(725.0f * (1.0f - categoriesOptionsPercent)), optionsWidth) * dpiScale;
    const int categoriesWidth = optionsWidth * categoriesOptionsPercent;

    this->options->setRelPosX(categoriesWidth - 1);
    this->options->setSize(optionsWidth, osu->getVirtScreenHeight() + 1);

    this->search->setRelPos(this->options->getRelPos());
    this->search->setSize(this->options->getSize());

    this->categories->setRelPosX(this->options->getRelPos().x - categoriesWidth);
    this->categories->setSize(categoriesWidth, osu->getVirtScreenHeight() + 1);

    // reset
    this->options->invalidate();

    // build layout
    bool enableHorizontalScrolling = false;
    int sideMargin = 25 * 2 * dpiScale;
    int spaceSpacing = 25 * dpiScale;
    int sectionSpacing = -15 * dpiScale;            // section title to first element
    int subsectionSpacing = 15 * dpiScale;          // subsection title to first element
    int sectionEndSpacing = /*70*/ 120 * dpiScale;  // last section element to next section title
    int subsectionEndSpacing = 65 * dpiScale;       // last subsection element to next subsection title
    int elementSpacing = 5 * dpiScale;
    int elementTextStartOffset = 11 * dpiScale;  // e.g. labels in front of sliders
    int yCounter = sideMargin + 20 * dpiScale;
    bool inSkipSection = false;
    bool inSkipSubSection = false;
    bool sectionTitleMatch = false;
    bool subSectionTitleMatch = false;
    const std::string search = SString::to_lower(this->sSearchString);
    for(int i = 0; i < this->elemContainers.size(); i++) {
        if(!this->elemContainers[i]->render_condition()) continue;
        if(this->elemContainers[i]->render_condition == RenderCondition::ASIO_ENABLED &&
           !(soundEngine->getOutputDriverType() == SoundEngine::OutputDriver::BASS_ASIO))
            continue;
        if(this->elemContainers[i]->render_condition == RenderCondition::WASAPI_ENABLED &&
           !(soundEngine->getOutputDriverType() == SoundEngine::OutputDriver::BASS_WASAPI))
            continue;

        // XXX: we should hide the checkbox for hardcoded servers that ban it (before connecting)
        //      ...and disable serverTextbox editing while connected
        if(this->elemContainers[i]->render_condition == RenderCondition::SCORE_SUBMISSION_POLICY &&
           (BanchoState::score_submission_policy != ServerPolicy::NO_PREFERENCE || oauth))
            continue;

        if(this->elemContainers[i]->render_condition == RenderCondition::PASSWORD_AUTH && oauth) continue;

        // searching logic happens here:
        // section
        //     content
        //     subsection
        //           content

        // case 1: match in content -> section + subsection + content     -> section + subsection matcher
        // case 2: match in content -> section + content                  -> section matcher
        // if match in section or subsection -> display entire section (disregard content match)
        // matcher is run through all remaining elements at every section + subsection

        if(!search.empty()) {
            const std::string searchTags{SString::to_lower(this->elemContainers[i]->searchTags)};

            // if this is a section
            if(this->elemContainers[i]->type == SECT) {
                bool sectionMatch = false;

                const std::string sectionTitle{SString::to_lower(this->elemContainers[i]->baseElems[0]->getName())};
                sectionTitleMatch = sectionTitle.find(search) != std::string::npos;

                subSectionTitleMatch = false;
                if(inSkipSection) inSkipSection = false;

                for(int s = i + 1; s < this->elemContainers.size(); s++) {
                    if(this->elemContainers[s]->type == SECT)  // stop at next section
                        break;

                    if(!this->elemContainers[s]->searchTags.empty()) {
                        const std::string sTags{SString::to_lower(this->elemContainers[s]->searchTags)};
                        if(sTags.find(search) != std::string::npos) {
                            sectionMatch = true;
                            break;
                        }
                    }

                    for(const auto &element : this->elemContainers[s]->baseElems) {
                        if(!element->getName().empty()) {
                            const std::string tags{SString::to_lower(element->getName())};

                            if(tags.find(search) != std::string::npos) {
                                sectionMatch = true;
                                break;
                            }
                        }
                    }
                }

                inSkipSection = !sectionMatch;
                if(!inSkipSection) inSkipSubSection = false;
            }

            // if this is a subsection
            if(this->elemContainers[i]->type == SUBSECT) {
                bool subSectionMatch = false;

                const std::string subSectionTitle{SString::to_lower(this->elemContainers[i]->baseElems[0]->getName())};
                subSectionTitleMatch =
                    subSectionTitle.find(search) != std::string::npos || searchTags.find(search) != std::string::npos;

                if(inSkipSubSection) inSkipSubSection = false;

                for(int s = i + 1; s < this->elemContainers.size(); s++) {
                    if(this->elemContainers[s]->type == SUBSECT)  // stop at next subsection
                        break;

                    if(!this->elemContainers[s]->searchTags.empty()) {
                        const std::string sTags{SString::to_lower(this->elemContainers[s]->searchTags)};
                        if(sTags.find(search) != std::string::npos) {
                            subSectionMatch = true;
                            break;
                        }
                    }

                    for(const auto &element : this->elemContainers[s]->baseElems) {
                        if(!element->getName().empty()) {
                            const std::string tags{SString::to_lower(element->getName())};

                            if(tags.find(search) != std::string::npos) {
                                subSectionMatch = true;
                                break;
                            }
                        }
                    }
                }

                inSkipSubSection = !subSectionMatch;
            }

            bool inSkipContent = false;
            if(!inSkipSection && !inSkipSubSection) {
                bool contentMatch = false;

                if(this->elemContainers[i]->type > SUBSECT) {
                    for(const auto &element : this->elemContainers[i]->baseElems) {
                        if(!element) continue;
                        if(!element->getName().empty()) {
                            const std::string tags{SString::to_lower(element->getName())};

                            if(tags.find(search) != std::string::npos) {
                                contentMatch = true;
                                break;
                            }
                        }
                    }

                    if(!contentMatch && !searchTags.empty() && searchTags.find(search) != std::string::npos) {
                        contentMatch = true;
                    }
                    // if section or subsection titles match, then include all content of that (sub)section (even if
                    // content doesn't match)
                    inSkipContent = !contentMatch;
                }
            }

            if((inSkipSection || inSkipSubSection || inSkipContent) && !sectionTitleMatch && !subSectionTitleMatch)
                continue;
        }

        // add all elements of the current entry
        {
            // reset button
            if(this->elemContainers[i]->resetButton != nullptr)
                this->options->container.addBaseUIElement(this->elemContainers[i]->resetButton.get());

            // (sub-)elements
            for(auto &element : this->elemContainers[i]->baseElems) {
                this->options->container.addBaseUIElement(element.get());
            }
        }

        // and build the layout

        // if this element is a new section, add even more spacing
        if(i > 0 && this->elemContainers[i]->type == SECT) yCounter += sectionEndSpacing;

        // if this element is a new subsection, add even more spacing
        if(i > 0 && this->elemContainers[i]->type == SUBSECT) yCounter += subsectionEndSpacing;

        const int elementWidth = optionsWidth - 2 * sideMargin - 2 * dpiScale;
        const bool isKeyBindButton = (this->elemContainers[i]->type == BINDBTN);
        const bool isButtonCheckbox = (this->elemContainers[i]->type == CBX_BTN);

        if(this->elemContainers[i]->resetButton != nullptr) {
            CBaseUIButton *resetButton = this->elemContainers[i]->resetButton.get();
            resetButton->setSize(vec2(35, 50) * dpiScale);
            resetButton->setRelPosY(yCounter);
            resetButton->setRelPosX(0);
        }

        for(const auto &e : this->elemContainers[i]->baseElems) {
            e->setSizeY(e->getRelSize().y * dpiScale);
        }

        if(this->elemContainers[i]->baseElems.size() == 1) {
            CBaseUIElement *e = this->elemContainers[i]->baseElems[0].get();

            int sideMarginAdd = 0;
            auto *labelPointer = dynamic_cast<CBaseUILabel *>(e);
            if(labelPointer != nullptr) {
                labelPointer->onResized();  // HACKHACK: framework, setSize*() does not update string metrics
                labelPointer->setSizeToContent(0, 0);
                sideMarginAdd += elementTextStartOffset;
            }

            auto *buttonPointer = dynamic_cast<CBaseUIButton *>(e);
            if(buttonPointer != nullptr)
                buttonPointer->onResized();  // HACKHACK: framework, setSize*() does not update string metrics

            auto *checkboxPointer = dynamic_cast<CBaseUICheckbox *>(e);
            if(checkboxPointer != nullptr) {
                checkboxPointer->onResized();  // HACKHACK: framework, setWidth*() does not update string metrics
                checkboxPointer->setWidthToContent(0);
                if(checkboxPointer->getSize().x > elementWidth)
                    enableHorizontalScrolling = true;
                else
                    e->setSizeX(elementWidth);
            } else
                e->setSizeX(elementWidth);

            e->setRelPosX(sideMargin + sideMarginAdd);
            e->setRelPosY(yCounter);

            yCounter += e->getSize().y;
        } else if(this->elemContainers[i]->baseElems.size() == 2 || isKeyBindButton) {
            CBaseUIElement *e1 = this->elemContainers[i]->baseElems[0].get();
            CBaseUIElement *e2 = this->elemContainers[i]->baseElems[1].get();

            int spacing = 15 * dpiScale;

            int sideMarginAdd = 0;
            auto *labelPointer = dynamic_cast<CBaseUILabel *>(e1);
            if(labelPointer != nullptr) sideMarginAdd += elementTextStartOffset;

            auto *buttonPointer = dynamic_cast<CBaseUIButton *>(e1);
            if(buttonPointer != nullptr)
                buttonPointer->onResized();  // HACKHACK: framework, setSize*() does not update string metrics

            // button-button spacing
            auto *buttonPointer2 = dynamic_cast<CBaseUIButton *>(e2);
            if(buttonPointer != nullptr && buttonPointer2 != nullptr) spacing *= 0.35f;

            if(isKeyBindButton) {
                CBaseUIElement *e3 = this->elemContainers[i]->baseElems[2].get();

                const float dividerMiddle = 5.0f / 8.0f;
                const float dividerEnd = 2.0f / 8.0f;

                e1->setRelPos(sideMargin, yCounter);
                e1->setSizeX(e1->getSize().y);

                e2->setRelPos(sideMargin + e1->getSize().x + 0.5f * spacing, yCounter);
                e2->setSizeX(elementWidth * dividerMiddle - spacing);

                e3->setRelPos(sideMargin + e1->getSize().x + e2->getSize().x + 1.5f * spacing, yCounter);
                e3->setSizeX(elementWidth * dividerEnd - spacing);

                yCounter += e1->getSize().y;
            } else if(isButtonCheckbox) {
                // make checkbox square (why the hell does this always need to be done here?)
                e2->setSizeX(e2->getSize().y);

                // button
                e1->setRelPos(sideMargin, yCounter);
                e1->setSizeX(elementWidth - e2->getSize().x - 2 * spacing);

                //  checkbox
                e2->setRelPos(sideMargin + e1->getSize().x + spacing, yCounter);

                yCounter += e1->getSize().y;
            } else if(labelPointer != nullptr) {
                // Labeled textbox
                e1->setRelPos(sideMargin, yCounter);
                e1->setSizeX(elementWidth);
                yCounter += e1->getSize().y;
                e2->setRelPos(sideMargin, yCounter);
                e2->setSizeX(elementWidth);
                yCounter += e2->getSize().y;
            } else {
                float dividerEnd = 1.0f / 2.0f;
                float dividerBegin = 1.0f - dividerEnd;

                e1->setRelPos(sideMargin + sideMarginAdd, yCounter);
                e1->setSizeX(elementWidth * dividerBegin - spacing);

                e2->setRelPos(sideMargin + e1->getSize().x + 2 * spacing, yCounter);
                e2->setSizeX(elementWidth * dividerEnd - spacing);

                yCounter += e1->getSize().y;
            }
        } else if(this->elemContainers[i]->baseElems.size() == 3) {
            CBaseUIElement *e1 = this->elemContainers[i]->baseElems[0].get();
            CBaseUIElement *e2 = this->elemContainers[i]->baseElems[1].get();
            CBaseUIElement *e3 = this->elemContainers[i]->baseElems[2].get();

            if(this->elemContainers[i]->type == BTN) {
                const int buttonButtonLabelOffset = 10 * dpiScale;

                const int buttonSize = elementWidth / 3 - 2 * buttonButtonLabelOffset;

                auto *button1Pointer = dynamic_cast<CBaseUIButton *>(e1);
                if(button1Pointer != nullptr)
                    button1Pointer->onResized();  // HACKHACK: framework, setSize*() does not update string metrics

                auto *button2Pointer = dynamic_cast<CBaseUIButton *>(e2);
                if(button2Pointer != nullptr)
                    button2Pointer->onResized();  // HACKHACK: framework, setSize*() does not update string metrics

                e1->setSizeX(buttonSize);
                e2->setSizeX(buttonSize);
                e3->setSizeX(buttonSize);

                e1->setRelPos(sideMargin, yCounter);
                e2->setRelPos(e1->getRelPos().x + e1->getSize().x + buttonButtonLabelOffset, yCounter);
                e3->setRelPos(e2->getRelPos().x + e2->getSize().x + buttonButtonLabelOffset, yCounter);
            } else {
                const int labelSliderLabelOffset = 15 * dpiScale;

                // this is a big mess, because some elements rely on fixed initial widths from default strings, combined
                // with variable font dpi on startup, will clean up whenever
                auto *label1Pointer = dynamic_cast<CBaseUILabel *>(e1);
                if(label1Pointer != nullptr) {
                    label1Pointer->onResized();  // HACKHACK: framework, setSize*() does not update string metrics
                    if(this->elemContainers[i]->label1Width > 0.0f)
                        label1Pointer->setSizeX(this->elemContainers[i]->label1Width * dpiScale);
                    else
                        label1Pointer->setSizeX(label1Pointer->getRelSize().x *
                                                (96.0f / this->elemContainers[i]->relSizeDPI) * dpiScale);
                }

                auto *sliderPointer = dynamic_cast<CBaseUISlider *>(e2);
                if(sliderPointer != nullptr) sliderPointer->setBlockSize(20 * dpiScale, 20 * dpiScale);

                auto *label2Pointer = dynamic_cast<CBaseUILabel *>(e3);
                if(label2Pointer != nullptr) {
                    label2Pointer->onResized();  // HACKHACK: framework, setSize*() does not update string metrics
                    label2Pointer->setSizeX(label2Pointer->getRelSize().x *
                                            (96.0f / this->elemContainers[i]->relSizeDPI) * dpiScale);
                }

                int sliderSize = elementWidth - e1->getSize().x - e3->getSize().x;
                if(sliderSize < 100) {
                    enableHorizontalScrolling = true;
                    sliderSize = 100;
                }

                e1->setRelPos(sideMargin + elementTextStartOffset, yCounter);

                e2->setRelPos(e1->getRelPos().x + e1->getSize().x + labelSliderLabelOffset, yCounter);
                e2->setSizeX(sliderSize - 2 * elementTextStartOffset - labelSliderLabelOffset * 2);

                e3->setRelPos(e2->getRelPos().x + e2->getSize().x + labelSliderLabelOffset, yCounter);
            }

            yCounter += e2->getSize().y;
        }

        yCounter += elementSpacing;

        switch(this->elemContainers[i]->type) {
            case SPCR:
                yCounter += spaceSpacing;
                break;
            case SECT:
                yCounter += sectionSpacing;
                break;
            case SUBSECT:
                yCounter += subsectionSpacing;
                break;
            default:
                break;
        }
    }
    this->options->container.addBaseUIElement(this->spacer, 0, yCounter);

    this->options->setScrollSizeToContent();
    if(!enableHorizontalScrolling) this->options->scrollToLeft();
    this->options->setHorizontalScrolling(enableHorizontalScrolling);

    this->options->container.addBaseUIElement(this->contextMenu);

    this->options->container.update_pos();

    // TODO: wrong? look at button borders when hovering... ew...
    f32 sidebarHeight = this->categories->getSize().y - parent->backButton->getSize().y;
    i32 categoryPaddingTopBottom = sidebarHeight * 0.15f;
    i32 categoryHeight = (sidebarHeight - categoryPaddingTopBottom * 2) / this->categoryButtons.size();
    for(int i = 0; i < this->categoryButtons.size(); i++) {
        CategoryButton *category = this->categoryButtons[i];
        category->onResized();  // HACKHACK: framework, setSize*() does not update string metrics
        category->setRelPosY(categoryPaddingTopBottom + categoryHeight * i);
        category->setSize(this->categories->getSize().x - 1, categoryHeight);
    }
    this->categories->container.update_pos();
    this->categories->setScrollSizeToContent();

    parent->update_pos();

    this->updating_layout = false;
}

UIContextMenu *OptionsOverlayImpl::getContextMenu() { return this->contextMenu; }

void OptionsOverlayImpl::onBack() {
    ui->getNotificationOverlay()->stopWaitingForKey();
    this->save();
    this->setVisibleInt(false, true);
}

void OptionsOverlayImpl::scheduleSearchUpdate() {
    this->updateLayout();
    this->options->scrollToTop();
    this->search->setSearchString(this->sSearchString);

    if(this->contextMenu->isVisible()) this->contextMenu->setVisible2(false);
}

void OptionsOverlayImpl::askForLoginDetails() {
    this->setVisible(true);
    this->options->scrollToElement(this->sectionGeneral, 0, 100 * Osu::getUIScale());
    if(this->nameTextbox->isVisible()) this->nameTextbox->focus();
}

void OptionsOverlayImpl::updateOsuFolderTextbox(std::string_view newFolder) {
    // don't recurse
    if(this->osuFolderTextbox && this->osuFolderTextbox->getText() != newFolder) {
        this->osuFolderTextbox->stealFocus();  // what's the point of this stealFocus?
        this->osuFolderTextbox->setText(std::string{newFolder});
    }
}

void OptionsOverlayImpl::updateFposuDPI() {
    if(this->dpiTextbox == nullptr) return;

    this->dpiTextbox->stealFocus();

    const std::string_view text = this->dpiTextbox->getText();
    std::string value;
    for(int i = 0; i < text.length(); i++) {
        if(text[i] == ',')
            value.push_back('.');
        else
            value.push_back(text[i]);
    }
    cv::fposu_mouse_dpi.setValue(value);
}

void OptionsOverlayImpl::updateFposuCMper360() {
    if(this->cm360Textbox == nullptr) return;

    this->cm360Textbox->stealFocus();

    const std::string_view text = this->cm360Textbox->getText();
    std::string value;
    for(int i = 0; i < text.length(); i++) {
        if(text[i] == ',')
            value.push_back('.');
        else
            value.push_back(text[i]);
    }
    cv::fposu_mouse_cm_360.setValue(value);
}

void OptionsOverlayImpl::updateSkinNameLabel() {
    if(this->skinLabel == nullptr) return;

    if(const auto &fallback = cv::skin_fallback.getString(); !fallback.empty()) {
        this->skinLabel->setText(fmt::format("{:s} (+{:s})", cv::skin.getString(), fallback));
    } else {
        this->skinLabel->setText(cv::skin.getString());
    }
    this->skinLabel->setTextColor(0xffffffff);
}

void OptionsOverlayImpl::updateNotelockSelectLabel() {
    if(this->notelockSelectLabel == nullptr) return;

    this->notelockSelectLabel->setText(
        this->notelockTypes[std::clamp<int>(cv::notelock_type.getInt(), 0, this->notelockTypes.size() - 1)]);
}

void OptionsOverlayImpl::onLanguageSelect() {
    // Just close the language selection menu if it's already open
    if(this->contextMenu->isVisible()) {
        this->contextMenu->setVisible2(false);
        return;
    }

    this->contextMenu->setPos(this->languageSelectButton->getPos());
    this->contextMenu->setRelPos(this->languageSelectButton->getPos());
    this->contextMenu->begin();
    for(const auto &[locale, language] : LANGUAGES) {
        this->contextMenu->addButton(std::string(language));
    }
    this->contextMenu->end(false, UIContextMenu::EndStyle{0});
    this->contextMenu->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onLanguageSelected>(this));
}

void OptionsOverlayImpl::onLanguageSelected(std::string_view newLanguage, int /*id*/) {
    // Because we have so many places where the UI only sets its strings once,
    // the simplest way to make locales work is to just restart the entire game.
    //
    // Unless someone is willing to refactor the entire codebase to account for this,
    // and deal with the additional cognitive load when adding any new UI elements,
    // this is how this setting will always be applied.
    //
    // We don't restart on cvar change because there are possibly many places where
    // the cvar could get set (eg when reloading config).
    for(const auto &[locale, language] : LANGUAGES) {
        if(language != newLanguage) continue;

        cv::language.setValue(locale);
        engine->restart();  // calls onShutdown() which calls save()
    }
}

void OptionsOverlayImpl::onFullscreenChange(CBaseUICheckbox *checkbox) {
    if(checkbox->isChecked())
        env->enableFullscreen();
    else
        env->disableFullscreen();

    // and update reset button as usual
    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(checkbox);
       it != this->uiToOptElemMap.end() && (element = it->second)) {
        this->onResetUpdate(element->resetButton.get());
    }
}

void OptionsOverlayImpl::onDPIScalingChange(CBaseUICheckbox *checkbox) {
    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(checkbox);
       it != this->uiToOptElemMap.end() && (element = it->second)) {
        const float prevUIScale = Osu::getUIScale();
        cv::ui_scale_to_dpi.setValue(checkbox->isChecked());

        this->onResetUpdate(element->resetButton.get());

        if(Osu::getUIScale() != prevUIScale) this->bDPIScalingScrollToSliderScheduled = true;
    }
}

void OptionsOverlayImpl::openCurrentSkinFolder() {
    auto current_skin = cv::skin.getString();
    if(strcasecmp(current_skin.c_str(), "default") == 0) {
        env->openFileBrowser(MCENGINE_IMAGES_PATH "/default");
    } else {
        std::string neomodSkinFolder = fmt::format(NEOMOD_SKINS_PATH "/{}", current_skin);
        if(env->directoryExists(neomodSkinFolder)) {
            env->openFileBrowser(neomodSkinFolder);
        } else {
            std::string currentSkinFolder =
                fmt::format("{}{}{}", cv::osu_folder.getString(), cv::osu_folder_sub_skins.getString(), current_skin);
            env->openFileBrowser(currentSkinFolder);
        }
    }
}

void OptionsOverlayImpl::onSkinSelectOpened() {
    // XXX: Instead of a dropdown, we should make a dedicated skin select screen with search bar

    // Just close the skin selection menu if it's already open
    if(this->contextMenu->isVisible()) {
        this->contextMenu->setVisible2(false);
        return;
    }

    if(osu->isSkinLoading()) return;
    if(this->skinFolderEnumHandle.valid()) return;  // we are waiting

    this->skinSelectLocalButton->is_loading = true;

    cv::osu_folder.setValue(this->osuFolderTextbox->getText());
    std::string skinFolder{cv::osu_folder.getString()};
    if(!skinFolder.ends_with('/')) skinFolder.push_back('/');
    skinFolder.append(cv::osu_folder_sub_skins.getString());

    this->skinFolderEnumHandle = Async::submit(
        [skinFolder] {
            std::vector<std::string> skinFolders;
            for(const auto &dir :
                {Environment::getFoldersInFolder(NEOMOD_SKINS_PATH "/"), Environment::getFoldersInFolder(skinFolder)}) {
                for(const auto &skin : dir) {
                    skinFolders.push_back(skin);
                }
            }
            if(cv::sort_skins_cleaned.getBool()) {
                // Sort skins only by alphanum characters, ignore the others
                std::ranges::sort(skinFolders, SString::alnum_comp);
            } else {
                // more stable-like sorting (i.e. "-     Cookiezi" comes before "Cookiezi")
                std::ranges::sort(skinFolders, SString::strcase_comp);
            }
            return skinFolders;
        },
        Lane::Background);
}

void OptionsOverlayImpl::onSkinSelectFoldersFinished(const std::vector<std::string> &skinFolders) {
    this->skinSelectLocalButton->is_loading = false;

    if(skinFolders.size() > 0) {
        if(parent->bVisible) {
            this->contextMenu->setPos(this->skinSelectLocalButton->getPos());
            this->contextMenu->setRelPos(this->skinSelectLocalButton->getRelPos());
            this->options->setScrollSizeToContent();
        } else {
            // Put it 50px from top, we'll move it later
            this->contextMenu->setPos(vec2{0, 100});
        }

        this->contextMenu->begin();

        constexpr Color fallbackColor = 0xffff8800;
        constexpr Color selectedColor = 0xff00ff00;

        const auto &fallbackSkin = cv::skin_fallback.getString();
        CBaseUIButton *buttonDefault = this->contextMenu->addButton("default");
        if(cv::skin.getString() == "default")
            buttonDefault->setTextBrightColor(selectedColor);
        else if(fallbackSkin == "default" || fallbackSkin.empty())
            buttonDefault->setTextBrightColor(fallbackColor);

        for(const auto &skinFolder : skinFolders) {
            if(skinFolder.compare(".") == 0 || skinFolder.compare("..") == 0) continue;

            CBaseUIButton *button = this->contextMenu->addButton(skinFolder);
            if(skinFolder.compare(cv::skin.getString()) == 0)
                button->setTextBrightColor(selectedColor);
            else if(skinFolder.compare(fallbackSkin) == 0)
                button->setTextBrightColor(fallbackColor);
        }
        {
            using namespace flags::operators;
            using enum UIContextMenu::EndStyle;
            auto flags = !parent->bVisible ? CLAMP_VERTICAL | STANDALONE_SCROLL : UIContextMenu::EndStyle{0};

            // we want to be able to scroll the skin dropdown itself
            this->contextMenu->end(false, flags);
        }
        this->contextMenu->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSkinSelect2>(this));

        if(!parent->bVisible) {
            // Center context menu
            this->contextMenu->setPos(vec2{
                osu->getVirtScreenWidth() / 2.f - this->contextMenu->getSize().x / 2.f,
                osu->getVirtScreenHeight() / 2.f - this->contextMenu->getSize().y / 2.f,
            });
        }
    } else {
        ui->getNotificationOverlay()->addToast(_("Error: Couldn't find any skins"), ERROR_TOAST);
        this->options->scrollToTop();
        this->fOsuFolderTextboxInvalidAnim = engine->getTime() + 3.0f;
    }
}

void OptionsOverlayImpl::onSkinSelect2(std::string_view skinName, int /*id*/) {
    if(keyboard->isShiftDown()) {
        // shift-click: set/toggle fallback skin
        if(skinName == cv::skin.getString())
            return;  // can't be both primary and fallback

        else if(skinName == "default"sv || skinName == cv::skin_fallback.getString())
            cv::skin_fallback.setValue("");  // toggle off
        else
            cv::skin_fallback.setValue(skinName);

        osu->reloadSkin();
    } else {
        cv::skin.setValue(skinName);
    }
    this->updateSkinNameLabel();
}

void OptionsOverlayImpl::onResolutionSelect() {
    std::vector<ivec2> resolutions{{800, 600},  // 4:3
                                   {1024, 768},  {1152, 864},  {1280, 960},  {1280, 1024}, {1600, 1200}, {1920, 1440},
                                   {2560, 1920}, {1024, 600},  // 16:9 and 16:10
                                   {1280, 720},  {1280, 768},  {1280, 800},  {1360, 768},  {1366, 768},  {1440, 900},
                                   {1600, 900},  {1600, 1024}, {1680, 1050}, {1920, 1080}, {1920, 1200}, {2560, 1440},
                                   {2560, 1600}, {3840, 2160}, {5120, 2880}, {7680, 4320}, {4096, 2160}};  // wtf

    // get custom resolutions
    std::vector<ivec2> customResolutions;
    {
        File customres(MCENGINE_CFG_PATH "/customres.cfg");
        for(auto line = customres.readLine(); !line.empty() || customres.canRead(); line = customres.readLine()) {
            if(SString::is_comment(line, "#") || SString::is_comment(line, "//")) continue;  // ignore comments
            if(auto parsed = Parsing::parse_resolution(line); parsed.has_value()) {
                customResolutions.emplace_back(*parsed);
            }
        }
    }

    // native resolution at the end
    const ivec2 nativeResolution = env->getNativeScreenSize();
    if(!std::ranges::contains(resolutions, nativeResolution)) resolutions.push_back(nativeResolution);

    // build context menu
    this->contextMenu->begin();
    for(const auto i : resolutions) {
        if(i.x > nativeResolution.x || i.y > nativeResolution.y) continue;

        std::string resolution{fmt::format("{}x{}", i.x, i.y)};
        CBaseUIButton *button = this->contextMenu->addButton(resolution);
        if(this->resolutionLabel != nullptr && resolution == this->resolutionLabel->getText())
            button->setTextBrightColor(0xff00ff00);
    }
    for(const auto customResolution : customResolutions) {
        this->contextMenu->addButton(fmt::format("{}x{}", customResolution.x, customResolution.y));
    }
    this->contextMenu->end(false, false);
    this->contextMenu->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onResolutionSelect2>(this));

    // reposition context menu
    f32 menu_width = this->contextMenu->getSize().x;
    f32 btn_width = this->resolutionSelectButton->getSize().x;
    f32 menu_offset = btn_width / 2.f - menu_width / 2.f;
    this->contextMenu->setPos(this->resolutionSelectButton->getPos().x + menu_offset,
                              this->resolutionSelectButton->getPos().y);
    this->contextMenu->setRelPos(this->resolutionSelectButton->getRelPos().x + menu_offset,
                                 this->resolutionSelectButton->getRelPos().y);
    this->options->setScrollSizeToContent();
}

void OptionsOverlayImpl::onResolutionSelect2(std::string_view resolution, int /*id*/) {
    const bool win_fs = env->winFullscreened();
    if(win_fs && cv::letterboxing.getBool()) {
        cv::letterboxed_resolution.setValue(resolution);
    } else if(win_fs) {
        cv::resolution.setValue(resolution);
    } else {
        cv::windowed_resolution.setValue(resolution);
    }
}

void OptionsOverlayImpl::onOutputDeviceSelect() {
    // Just close the device selection menu if it's already open
    if(this->contextMenu->isVisible()) {
        this->contextMenu->setVisible2(false);
        return;
    }

    // build context menu
    this->contextMenu->setPos(this->outputDeviceSelectButton->getPos());
    this->contextMenu->setRelPos(this->outputDeviceSelectButton->getRelPos());
    this->contextMenu->begin();
    for(const auto &device : soundEngine->getOutputDevices()) {
        CBaseUIButton *button = this->contextMenu->addButton(device.name);
        if(device.name == soundEngine->getOutputDeviceName()) button->setTextBrightColor(0xff00ff00);
    }
    this->contextMenu->end(false, true);
    this->contextMenu->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onOutputDeviceSelect2>(this));
    this->options->setScrollSizeToContent();
}

void OptionsOverlayImpl::onOutputDeviceSelect2(std::string_view outputDeviceName, int /*id*/) {
    if(outputDeviceName == soundEngine->getOutputDeviceName()) {
        if(this->outputDeviceLabel != nullptr)
            this->outputDeviceLabel->setText(std::string{soundEngine->getOutputDeviceName()});
        debugLog("SoundEngine::setOutputDevice() \"{:s}\" already is the current device.", outputDeviceName);
        return;
    }

    for(const auto &device : soundEngine->getOutputDevices()) {
        if(device.name != outputDeviceName) continue;

        soundEngine->setOutputDevice(device);
        if((soundEngine->getOutputDriverType() == SoundEngine::OutputDriver::SOLOUD_MA)) {
            if(device.name.find("(Exclusive)") != std::string_view::npos &&
               soundEngine->getOutputDeviceName().find("(Exclusive)") == std::string_view::npos) {
                ui->getNotificationOverlay()->addToast(_("Tried to enable exclusive mode, but couldn't :("),
                                                       ERROR_TOAST);
            }
        }
        if(this->outputDeviceLabel != nullptr)
            this->outputDeviceLabel->setText(std::string{soundEngine->getOutputDeviceName()});
        return;
    }

    debugLog("SoundEngine::setOutputDevice() couldn't find output device \"{:s}\"!", outputDeviceName);
}

void OptionsOverlayImpl::onOutputDeviceChange() {
    if(this->outputDeviceLabel) {
        this->outputDeviceLabel->setText(std::string{soundEngine->getOutputDeviceName()});
    }

    this->onOutputDeviceResetUpdate();
}

void OptionsOverlayImpl::onOutputDeviceResetUpdate() {
    if(this->outputDeviceResetButton != nullptr) {
        this->outputDeviceResetButton->setEnabled(soundEngine->getOutputDeviceName() !=
                                                  soundEngine->getDefaultDevice().name);
    }
}

void OptionsOverlayImpl::onLogInClicked(bool left, bool right) {
    if(left && BanchoState::is_logging_in()) {
        return;
    }
    soundEngine->play(osu->getSkin()->s_menu_hit);

    // Clear mp_oauth_token if the user is connecting to a non-oauth server
    // This makes it easy to check what we need to do when building the login packet
    if(!this->should_use_oauth_login()) {
        cv::mp_oauth_token.setValue("");
    }

    const bool is_polling = BanchoState::get_online_status() == OnlineStatus::POLLING;
    if((right && BanchoState::is_logging_in()) || BanchoState::is_online() || is_polling) {
        BanchoState::disconnect();

        // Manually clicked disconnect button: clear oauth token
        cv::mp_oauth_token.setValue("");
    } else {
        if(this->should_use_oauth_login() && cv::mp_oauth_token.getString().empty()) {
            BanchoState::endpoint = cv::mp_server.getString();
            BanchoState::game_endpoint = "c." + BanchoState::endpoint;

            crypto::rng::get_rand(BanchoState::oauth_verifier);
            crypto::hash::sha256(&BanchoState::oauth_verifier[0], 32, &BanchoState::oauth_challenge[0]);

            auto challenge_b64 = Mc::Net::urlEncode(crypto::conv::encode64(BanchoState::oauth_challenge));
            auto scheme = cv::use_https.getBool() ? "https://" : "http://";
            auto url = fmt::format("{}{}/connect/start?challenge={}&client={}", scheme, BanchoState::endpoint,
                                   challenge_b64, BanchoState::neomod_version);

            env->openURLInDefaultBrowser(url);

            BanchoState::update_online_status(OnlineStatus::POLLING);
            BanchoState::poll_login();
        } else {
            BanchoState::reconnect();
        }
    }
}

void OptionsOverlayImpl::onNotelockSelect() {
    // build context menu
    this->contextMenu->setPos(this->notelockSelectButton->getPos());
    this->contextMenu->setRelPos(this->notelockSelectButton->getRelPos());
    this->contextMenu->begin(this->notelockSelectButton->getSize().x);
    {
        for(int i = 0; i < this->notelockTypes.size(); i++) {
            CBaseUIButton *button = this->contextMenu->addButton(this->notelockTypes[i], i);
            if(i == cv::notelock_type.getInt()) button->setTextBrightColor(0xff00ff00);
        }
    }
    this->contextMenu->end(false, false);
    this->contextMenu->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onNotelockSelect2>(this));
    this->options->setScrollSizeToContent();
}

void OptionsOverlayImpl::onNotelockSelect2(std::string_view /*notelockType*/, int id) {
    cv::notelock_type.setValue(id);
    this->updateNotelockSelectLabel();

    // and update the reset button as usual
    this->onNotelockSelectResetUpdate();
}

void OptionsOverlayImpl::onNotelockSelectResetClicked() {
    if(this->notelockTypes.size() > 1 && (size_t)cv::notelock_type.getDefaultFloat() < this->notelockTypes.size())
        this->onNotelockSelect2(this->notelockTypes[(size_t)cv::notelock_type.getDefaultFloat()],
                                (int)cv::notelock_type.getDefaultFloat());
}

void OptionsOverlayImpl::onNotelockSelectResetUpdate() {
    if(this->notelockSelectResetButton != nullptr)
        this->notelockSelectResetButton->setEnabled(cv::notelock_type.getInt() !=
                                                    (int)cv::notelock_type.getDefaultFloat());
}

#define DO_UPDATE_LAYOUT_CHECK(slider__)                               \
    if(!this->updating_layout && !(slider__)->isBusy() &&              \
       static_cast<UISlider *>(slider__)->getUpdateRelPosOnChange()) { \
        this->scheduleLayoutUpdate();                                  \
    }

void OptionsOverlayImpl::onCheckboxChange(CBaseUICheckbox *checkbox) {
    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(checkbox);
       it != this->uiToOptElemMap.end() && (element = it->second)) {
        if(element->cvar) {
            element->cvar->setValue(checkbox->isChecked());
        }

        this->onResetUpdate(element->resetButton.get());
    }
}

void OptionsOverlayImpl::onSliderChange(CBaseUISlider *slider) {
    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(slider); it != this->uiToOptElemMap.end() && (element = it->second)) {
        element->cvar->setValue(std::round(slider->getFloat() * 100.0f) / 100.0f);  // round to 2 decimal places
        if(element->baseElems.size() == 3) {
            auto *labelPointer = dynamic_cast<CBaseUILabel *>(element->baseElems[2].get());
            labelPointer->setText(element->cvar->getString());
        }

        this->onResetUpdate(element->resetButton.get());
        DO_UPDATE_LAYOUT_CHECK(slider);
    }
}

void OptionsOverlayImpl::onFPSSliderChange(CBaseUISlider *slider) {
    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(slider); it != this->uiToOptElemMap.end() && (element = it->second)) {
        if(slider->getFloat() < 60.f) {
            element->cvar->setValue(0.f);
            if(element->baseElems.size() == 3) {
                auto *labelPointer = dynamic_cast<CBaseUILabel *>(element->baseElems[2].get());
                labelPointer->setText("∞"s);
            }
        } else {
            element->cvar->setValue(std::round(slider->getFloat()));  // round to int
            if(element->baseElems.size() == 3) {
                auto *labelPointer = dynamic_cast<CBaseUILabel *>(element->baseElems[2].get());
                labelPointer->setText(element->cvar->getString());
            }
        }

        this->onResetUpdate(element->resetButton.get());
        DO_UPDATE_LAYOUT_CHECK(slider);
    }
}

void OptionsOverlayImpl::onSliderChangeOneDecimalPlace(CBaseUISlider *slider) {
    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(slider); it != this->uiToOptElemMap.end() && (element = it->second)) {
        element->cvar->setValue(std::round(slider->getFloat() * 10.0f) / 10.0f);  // round to 1 decimal place
        if(element->baseElems.size() == 3) {
            auto *labelPointer = dynamic_cast<CBaseUILabel *>(element->baseElems[2].get());
            labelPointer->setText(element->cvar->getString());
        }

        this->onResetUpdate(element->resetButton.get());
        DO_UPDATE_LAYOUT_CHECK(slider);
    }
}

void OptionsOverlayImpl::onSliderChangeTwoDecimalPlaces(CBaseUISlider *slider) {
    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(slider); it != this->uiToOptElemMap.end() && (element = it->second)) {
        element->cvar->setValue(std::round(slider->getFloat() * 100.0f) / 100.0f);  // round to 2 decimal places
        if(element->baseElems.size() == 3) {
            auto *labelPointer = dynamic_cast<CBaseUILabel *>(element->baseElems[2].get());
            labelPointer->setText(element->cvar->getString());
        }

        this->onResetUpdate(element->resetButton.get());
        DO_UPDATE_LAYOUT_CHECK(slider);
    }
}

void OptionsOverlayImpl::onSliderChangeOneDecimalPlaceMeters(CBaseUISlider *slider) {
    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(slider); it != this->uiToOptElemMap.end() && (element = it->second)) {
        element->cvar->setValue(std::round(slider->getFloat() * 10.0f) / 10.0f);  // round to 1 decimal place
        if(element->baseElems.size() == 3) {
            auto *labelPointer = dynamic_cast<CBaseUILabel *>(element->baseElems[2].get());
            labelPointer->setText(fmt::format("{:.1f} m", element->cvar->getFloat()));
        }

        this->onResetUpdate(element->resetButton.get());
        DO_UPDATE_LAYOUT_CHECK(slider);
    }
}

void OptionsOverlayImpl::onSliderChangeInt(CBaseUISlider *slider) {
    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(slider); it != this->uiToOptElemMap.end() && (element = it->second)) {
        element->cvar->setValue(std::round(slider->getFloat()));  // round to int
        if(element->baseElems.size() == 3) {
            auto *labelPointer = dynamic_cast<CBaseUILabel *>(element->baseElems[2].get());
            labelPointer->setText(element->cvar->getString());
        }

        this->onResetUpdate(element->resetButton.get());
        DO_UPDATE_LAYOUT_CHECK(slider);
    }
}

void OptionsOverlayImpl::onSliderChangeIntMS(CBaseUISlider *slider) {
    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(slider); it != this->uiToOptElemMap.end() && (element = it->second)) {
        element->cvar->setValue(std::round(slider->getFloat()));  // round to int
        if(element->baseElems.size() == 3) {
            auto *labelPointer = dynamic_cast<CBaseUILabel *>(element->baseElems[2].get());
            std::string text = element->cvar->getString();
            text.append(" ms");
            labelPointer->setText(std::move(text));
        }

        this->onResetUpdate(element->resetButton.get());
        DO_UPDATE_LAYOUT_CHECK(slider);
    }
}

void OptionsOverlayImpl::onSliderChangeFloatMS(CBaseUISlider *slider) {
    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(slider); it != this->uiToOptElemMap.end() && (element = it->second)) {
        element->cvar->setValue(slider->getFloat());
        if(element->baseElems.size() == 3) {
            auto *labelPointer = dynamic_cast<CBaseUILabel *>(element->baseElems[2].get());
            std::string text = fmt::format("{}", (int)std::round(element->cvar->getFloat() * 1000.0f));
            text.append(" ms");
            labelPointer->setText(std::move(text));
        }

        this->onResetUpdate(element->resetButton.get());
        DO_UPDATE_LAYOUT_CHECK(slider);
    }
}

void OptionsOverlayImpl::onSliderChangePercent(CBaseUISlider *slider) {
    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(slider); it != this->uiToOptElemMap.end() && (element = it->second)) {
        element->cvar->setValue(std::round(slider->getFloat() * 100.0f) / 100.0f);
        if(element->baseElems.size() == 3) {
            int percent = std::round(element->cvar->getFloat() * 100.0f);

            auto *labelPointer = dynamic_cast<CBaseUILabel *>(element->baseElems[2].get());
            labelPointer->setText(fmt::format("{}%", percent));
        }

        this->onResetUpdate(element->resetButton.get());
        DO_UPDATE_LAYOUT_CHECK(slider);
    }
}

void OptionsOverlayImpl::onKeyBindingButtonPressed(CBaseUIButton *button) {
    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(button); it != this->uiToOptElemMap.end() && (element = it->second)) {
        this->waitingKey = element->cvar;

        const bool waitForKey = true;
        auto notificationText = tformat("Press new key for {}:", button->getText());
        ui->getNotificationOverlay()->addNotification(notificationText, 0xffffffff, waitForKey);
        ui->getNotificationOverlay()->setDisallowWaitForKeyLeftClick(
            !(dynamic_cast<KeyBindButton *>(button)->isLeftMouseClickBindingAllowed()));
    }
}

void OptionsOverlayImpl::onKeyUnbindButtonPressed(CBaseUIButton *button) {
    soundEngine->play(osu->getSkin()->s_check_off);

    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(button); it != this->uiToOptElemMap.end() && (element = it->second)) {
        element->cvar->setValue(0.0f);
    }
}

void OptionsOverlayImpl::onKeyBindingsResetAllPressed(CBaseUIButton * /*button*/) {
    this->iNumResetAllKeyBindingsPressed++;

    const int numRequiredPressesUntilReset = 4;
    const int remainingUntilReset = numRequiredPressesUntilReset - this->iNumResetAllKeyBindingsPressed;

    if(this->iNumResetAllKeyBindingsPressed > (numRequiredPressesUntilReset - 1)) {
        this->iNumResetAllKeyBindingsPressed = 0;

        for(auto *bind : OsuKeyBinds::getAll()) {
            bind->reset();
        }

        this->scheduleLayoutUpdate();

        ui->getNotificationOverlay()->addNotification(_("All key bindings have been reset."), 0xff00ff00);
    } else {
        if(remainingUntilReset > 1)
            ui->getNotificationOverlay()->addNotification(
                tformat("Press {:d} more times to confirm.", remainingUntilReset));
        else
            ui->getNotificationOverlay()->addNotification(
                tformat("Press {:d} more time to confirm!", remainingUntilReset), 0xffffff00);
    }
}

void OptionsOverlayImpl::onSliderChangeSliderQuality(CBaseUISlider *slider) {
    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(slider); it != this->uiToOptElemMap.end() && (element = it->second)) {
        element->cvar->setValue(std::round(slider->getFloat() * 100.0f) / 100.0f);  // round to 2 decimal places
        if(element->baseElems.size() == 3) {
            auto *labelPointer = dynamic_cast<CBaseUILabel *>(element->baseElems[2].get());

            int percent = std::round((slider->getPercent()) * 100.0f);
            labelPointer->setText(fmt::format("{}{}", percent, percent > 49 ? " !" : ""));
        }

        this->onResetUpdate(element->resetButton.get());
        DO_UPDATE_LAYOUT_CHECK(slider);
    }
}

void OptionsOverlayImpl::onSliderChangeLetterboxingOffset(CBaseUISlider *slider) {
    this->bLetterboxingOffsetUpdateScheduled = true;

    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(slider); it != this->uiToOptElemMap.end() && (element = it->second)) {
        const float newValue = std::round(slider->getFloat() * 100.0f) / 100.0f;

        if(element->baseElems.size() == 3) {
            const int percent = std::round(newValue * 100.0f);

            auto *labelPointer = dynamic_cast<CBaseUILabel *>(element->baseElems[2].get());
            labelPointer->setText(fmt::format("{}%", percent));
        }

        this->letterboxingOffsetResetButton = element->resetButton.get();  // HACKHACK: disgusting

        this->onResetUpdate(element->resetButton.get());
        DO_UPDATE_LAYOUT_CHECK(slider);
    }
}

void OptionsOverlayImpl::onSliderChangeUIScale(CBaseUISlider *slider) {
    this->bUIScaleChangeScheduled = true;

    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(slider); it != this->uiToOptElemMap.end() && (element = it->second)) {
        const float newValue = std::round(slider->getFloat() * 100.0f) / 100.0f;

        if(element->baseElems.size() == 3) {
            const int percent = std::round(newValue * 100.0f);

            auto *labelPointer = dynamic_cast<CBaseUILabel *>(element->baseElems[2].get());
            labelPointer->setText(fmt::format("{}%", percent));
        }

        this->uiScaleResetButton = element->resetButton.get();  // HACKHACK: disgusting

        this->onResetUpdate(element->resetButton.get());
        DO_UPDATE_LAYOUT_CHECK(slider);
    }
}

// FIXME: hacky
void OptionsOverlayImpl::setupASIOClampedChangeCallback() {
#if defined(MCENGINE_PLATFORM_WINDOWS) && defined(MCENGINE_FEATURE_BASS)
    if(soundEngine->getTypeId() != SoundEngine::SndEngineType::BASS) return;

    static_cast<BassSoundEngine *>(soundEngine.get())
        ->setOnASIOBufferChangeCB(
            [asioBufSizeSlider = &this->asioBufferSizeSlider](const BASS_ASIO_INFO &info) -> void {
                if(!asioBufSizeSlider || !*asioBufSizeSlider) return;

                (*asioBufSizeSlider)->setBounds(info.bufmin, info.bufmax);
                (*asioBufSizeSlider)->setKeyDelta(info.bufgran == -1 ? info.bufmin : info.bufgran);
            });
#endif
}

void OptionsOverlayImpl::OpenASIOSettings() {
#if defined(MCENGINE_PLATFORM_WINDOWS) && defined(MCENGINE_FEATURE_BASS)
    if(soundEngine->getTypeId() != SoundEngine::SndEngineType::BASS) return;
    BASS_ASIO_ControlPanel();
#endif
}

void OptionsOverlayImpl::onASIOBufferChange([[maybe_unused]] CBaseUISlider *slider) {
#if defined(MCENGINE_PLATFORM_WINDOWS) && defined(MCENGINE_FEATURE_BASS)
    if(soundEngine->getTypeId() != SoundEngine::SndEngineType::BASS) return;
    if(!this->updating_layout) this->bASIOBufferChangeScheduled = true;

    BASS_ASIO_INFO info{};
    BASS_ASIO_GetInfo(&info);
    cv::asio_buffer_size.setDefaultDouble(info.bufpref);
    slider->setBounds(info.bufmin, info.bufmax);
    slider->setKeyDelta(info.bufgran == -1 ? info.bufmin : info.bufgran);

    u32 bufsize = slider->getInt();
    bufsize = BassSoundEngine::ASIO_clamp(info, bufsize);
    double latency = 1000.0 * (double)bufsize / std::max(BASS_ASIO_GetRate(), 44100.0);

    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(slider); it != this->uiToOptElemMap.end() && (element = it->second)) {
        if(element->baseElems.size() == 3) {
            auto *labelPointer = dynamic_cast<CBaseUILabel *>(element->baseElems[2].get());
            if(labelPointer) {
                std::string text = fmt::format("{:.1f} ms", latency);
                labelPointer->setText(std::move(text));
            }
        }

        this->asioBufferSizeResetButton = element->resetButton.get();  // HACKHACK: disgusting
        DO_UPDATE_LAYOUT_CHECK(slider);
    }
#endif
}

void OptionsOverlayImpl::onWASAPIBufferChange(CBaseUISlider *slider) {
    if(!this->updating_layout) this->bWASAPIBufferChangeScheduled = true;

    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(slider); it != this->uiToOptElemMap.end() && (element = it->second)) {
        if(element->baseElems.size() == 3) {
            auto *labelPointer = dynamic_cast<CBaseUILabel *>(element->baseElems[2].get());
            if(labelPointer) {
                std::string text = fmt::format("{}", (int)std::round(slider->getFloat() * 1000.0f));
                text.append(" ms");
                labelPointer->setText(std::move(text));
            }
        }

        this->wasapiBufferSizeResetButton = element->resetButton.get();  // HACKHACK: disgusting
        DO_UPDATE_LAYOUT_CHECK(slider);
    }
}

void OptionsOverlayImpl::onWASAPIPeriodChange(CBaseUISlider *slider) {
    if(!this->updating_layout) this->bWASAPIPeriodChangeScheduled = true;

    OptionsElement *element = nullptr;
    if(const auto &it = this->uiToOptElemMap.find(slider); it != this->uiToOptElemMap.end() && (element = it->second)) {
        if(element->baseElems.size() == 3) {
            auto *labelPointer = dynamic_cast<CBaseUILabel *>(element->baseElems[2].get());
            if(labelPointer) {
                std::string text = fmt::format("{}", (int)std::round(slider->getFloat() * 1000.0f));
                text.append(" ms");
                labelPointer->setText(std::move(text));
            }
        }

        this->wasapiPeriodSizeResetButton = element->resetButton.get();  // HACKHACK: disgusting
        DO_UPDATE_LAYOUT_CHECK(slider);
    }
}

void OptionsOverlayImpl::onLoudnessNormalizationToggle(CBaseUICheckbox *checkbox) {
    this->onCheckboxChange(checkbox);

    auto music = osu->getMapInterface()->getMusic();
    if(music != nullptr) {
        music->setBaseVolume(osu->getMapInterface()->getIdealVolume());
    }

    if(cv::normalize_loudness.getBool()) {
        VolNormalization::start_calc(db->loudness_to_calc);
    } else {
        VolNormalization::abort();
    }
}

void OptionsOverlayImpl::onModChangingToggle(CBaseUICheckbox *checkbox) {
    this->onCheckboxChange(checkbox);
    ui->getModSelector()->updateButtons();
    osu->updateMods();
}

void OptionsOverlayImpl::onHighQualitySlidersCheckboxChange(CBaseUICheckbox *checkbox) {
    this->onCheckboxChange(checkbox);

    // special case: if the checkbox is clicked and enabled via the UI, force set the quality to 100
    if(checkbox->isChecked()) this->sliderQualitySlider->setValue(1.0f, false);
}

void OptionsOverlayImpl::onHighQualitySlidersConVarChange(float newValue) {
    const bool enabled = newValue > 0;
    for(auto &element : this->elemContainers) {
        bool contains = false;
        for(const auto &e : element->baseElems) {
            if(e.get() == this->sliderQualitySlider) {
                contains = true;
                break;
            }
        }

        if(contains) {
            // HACKHACK: show/hide quality slider, this is ugly as fuck
            // TODO: containers use setVisible() on all elements. there needs to be a separate API for hiding elements
            // while inside any kind of container
            for(const auto &e : element->baseElems) {
                e->setEnabled(enabled);

                auto *sliderPointer = dynamic_cast<UISlider *>(e.get());
                auto *labelPointer = dynamic_cast<CBaseUILabel *>(e.get());

                if(sliderPointer != nullptr) sliderPointer->setFrameColor(enabled ? 0xffffffff : 0xff000000);
                if(labelPointer != nullptr) labelPointer->setTextColor(enabled ? 0xffffffff : 0xff000000);
            }

            // reset value if disabled
            if(!enabled && element->cvar) {
                this->sliderQualitySlider->setValue(static_cast<float>(element->cvar->getDefaultDouble()), false);
                element->cvar->setValue(element->cvar->getDefaultDouble());
            }

            this->onResetUpdate(element->resetButton.get());

            break;
        }
    }
}

void OptionsOverlayImpl::onCategoryClicked(CategoryButton *button) {
    // reset search
    this->sSearchString.clear();
    this->scheduleSearchUpdate();

    // scroll to category
    this->options->scrollToElement(button->getSection(), 0, 100 * Osu::getUIScale());
}

void OptionsOverlayImpl::onResetUpdate(ResetButton *resbtn) {
    if(resbtn == nullptr) return;

    OptionsElement *optelem = resbtn->elemContainer;
    switch(optelem->type) {
        case CBX:
            resbtn->setEnabled(optelem->cvar->getBool() != (bool)optelem->cvar->getDefaultDouble());
            break;
        case SLDR:
            resbtn->setEnabled(optelem->cvar->getDouble() != optelem->cvar->getDefaultDouble());
            break;
        case BINDBTN: {
            const auto binds = OsuKeyBinds::getAll();
            if(const auto &it = std::ranges::find(binds, optelem->cvar, &OsuKeyBinds::Bind::cvar); it != binds.end()) {
                resbtn->setEnabled(!(*it)->isDefault());
            }
            break;
        }
        default:
            break;
    }
}

void OptionsOverlayImpl::applyForcedCvarLocks() {
    for(auto &e : this->elemContainers) {
        if(e->cvar == nullptr) continue;
        const bool nowLocked = (e->cvar->getMaster() != CvarEditor::CLIENT);
        if(nowLocked == e->cvarLocked) continue;  // no transition

        if(nowLocked) {
            // save each baseElem's bEnabled so we can restore it later, even if
            // some unrelated code (e.g. onHighQualitySlidersConVarChange) has it
            // disabled for its own reasons
            e->enabledBeforeCvarLock.clear();
            e->enabledBeforeCvarLock.reserve(e->baseElems.size());
            for(auto &elem : e->baseElems) {
                e->enabledBeforeCvarLock.push_back(elem->isEnabled());
                elem->setEnabled(false);
            }
        } else {
            for(size_t i = 0; i < e->baseElems.size(); i++) {
                const bool restored = i < e->enabledBeforeCvarLock.size() ? e->enabledBeforeCvarLock[i] : true;
                e->baseElems[i]->setEnabled(restored);
            }
            e->enabledBeforeCvarLock.clear();
        }
        e->cvarLocked = nowLocked;
    }
}

void OptionsOverlayImpl::pushForcedCvarTooltipIfHovered() {
    if(!this->options->getRect().contains(mouse->getPos())) return;

    for(const auto &e : this->elemContainers) {
        if(!e->cvarLocked) continue;
        for(const auto &elem : e->baseElems) {
            if(!elem->isVisible() || !elem->getRect().contains(mouse->getPos())) continue;

            auto *ttoverlay = ui->getTooltipOverlay();
            ttoverlay->begin();
            switch(e->cvar->getMaster()) {
                case CvarEditor::SERVER:
                    ttoverlay->addLine(_("This setting is forced by the server."));
                    break;
                case CvarEditor::SKIN:
                    ttoverlay->addLine(_("This setting is forced by the current skin."));
                    break;
                case CvarEditor::CLIENT:
                    break;  // unreachable: cvarLocked implies non-CLIENT master
            }
            ttoverlay->end();
            return;
        }
    }
}

void OptionsOverlayImpl::onResetClicked(ResetButton *resbtn) {
    if(resbtn == nullptr) return;

    OptionsElement *optelem = resbtn->elemContainer;
    switch(optelem->type) {
        case CBX:
            for(const auto &e : optelem->baseElems) {
                auto *checkboxPointer = dynamic_cast<CBaseUICheckbox *>(e.get());
                if(checkboxPointer != nullptr) checkboxPointer->setChecked((bool)optelem->cvar->getDefaultDouble());
            }
            break;
        case SLDR:
            if(optelem->baseElems.size() == 3) {
                auto *sliderPointer = dynamic_cast<CBaseUISlider *>(optelem->baseElems[1].get());
                if(sliderPointer != nullptr) {
                    sliderPointer->setValue(static_cast<float>(optelem->cvar->getDefaultDouble()), false);
                    sliderPointer->fireChangeCallback();
                }
            }
            break;
        case BINDBTN: {
            const auto binds = OsuKeyBinds::getAll();
            if(const auto &it = std::ranges::find(binds, optelem->cvar, &OsuKeyBinds::Bind::cvar); it != binds.end()) {
                (*it)->reset();
            }
        } break;
        default:
            break;
    }

    this->onResetUpdate(resbtn);
}

void OptionsOverlayImpl::onResetEverythingClicked(CBaseUIButton * /*button*/) {
    this->iNumResetEverythingPressed++;

    const int numRequiredPressesUntilReset = 4;
    const int remainingUntilReset = numRequiredPressesUntilReset - this->iNumResetEverythingPressed;

    if(this->iNumResetEverythingPressed > (numRequiredPressesUntilReset - 1)) {
        this->iNumResetEverythingPressed = 0;

        // reset all settings
        for(const auto &element : this->elemContainers) {
            ResetButton *resetButton = element->resetButton.get();
            if(resetButton != nullptr && resetButton->isEnabled()) resetButton->click();
        }

        ui->getNotificationOverlay()->addNotification(_("All settings have been reset."), 0xff00ff00);
    } else {
        if(remainingUntilReset > 1)
            ui->getNotificationOverlay()->addNotification(
                tformat("Press {:d} more times to confirm.", remainingUntilReset));
        else
            ui->getNotificationOverlay()->addNotification(
                tformat("Press {:d} more time to confirm!", remainingUntilReset), 0xffffff00);
    }
}

void OptionsOverlayImpl::addSpacer() { this->elemContainers.emplace_back(new OptionsElement{SPCR}); }

CBaseUILabel *OptionsOverlayImpl::addSection(const std::string &text) {
    auto *label = new CBaseUILabel(0, 0, this->options->getSize().x, 25, text, text);
    // label->setTextColor(0xff58dafe);
    label->setFont(osu->getTitleFont());
    label->setSizeToContent(0, 0);
    label->setTextJustification(TEXT_JUSTIFICATION::RIGHT);
    label->setDrawFrame(false);
    label->setDrawBackground(false);
    this->options->container.addBaseUIElement(label);

    auto e = std::make_unique<OptionsElement>(SECT);
    e->baseElems.emplace_back(label);
    const auto &last = this->elemContainers.emplace_back(std::move(e));
    this->uiToOptElemMap[label] = last.get();

    return label;
}

CBaseUILabel *OptionsOverlayImpl::addSubSection(const std::string &text, const std::string &searchTags) {
    auto *label = new CBaseUILabel(0, 0, this->options->getSize().x, 25, text, text);
    label->setFont(osu->getSubTitleFont());
    label->setSizeToContent(0, 0);
    label->setDrawFrame(false);
    label->setDrawBackground(false);
    this->options->container.addBaseUIElement(label);

    auto e = std::make_unique<OptionsElement>(SUBSECT);
    e->baseElems.emplace_back(label);
    e->searchTags = searchTags;
    const auto &last = this->elemContainers.emplace_back(std::move(e));
    this->uiToOptElemMap[label] = last.get();

    return label;
}

CBaseUILabel *OptionsOverlayImpl::addLabel(const std::string &text) {
    auto *label = new CBaseUILabel(0, 0, this->options->getSize().x, 25, text, text);
    label->setSizeToContent(0, 0);
    label->setDrawFrame(false);
    label->setDrawBackground(false);
    this->options->container.addBaseUIElement(label);

    auto e = std::make_unique<OptionsElement>(LABEL);
    e->baseElems.emplace_back(label);
    const auto &last = this->elemContainers.emplace_back(std::move(e));
    this->uiToOptElemMap[label] = last.get();

    return label;
}

UIButton *OptionsOverlayImpl::addButton(const std::string &text, ConVar *cvar) {
    auto *button = new UIButton(0, 0, this->options->getSize().x, 50, text, text);
    button->setColor(0xff0c7c99);
    button->setUseDefaultSkin();
    this->options->container.addBaseUIElement(button);

    auto e = std::make_unique<OptionsElement>(BTN);
    e->baseElems.emplace_back(button);
    e->cvar = cvar;
    const auto &last = this->elemContainers.emplace_back(std::move(e));
    this->uiToOptElemMap[button] = last.get();

    return button;
}

OptionsElement *OptionsOverlayImpl::addButton(const std::string &text, const std::string &labelText,
                                              bool withResetButton, ConVar *cvar) {
    auto *button = new UIButton(0, 0, this->options->getSize().x, 50, text, text);
    button->setColor(0xff0c7c99);
    button->setUseDefaultSkin();
    this->options->container.addBaseUIElement(button);

    auto *label = new CBaseUILabel(0, 0, this->options->getSize().x, 50, labelText, labelText);
    label->setDrawFrame(false);
    label->setDrawBackground(false);
    this->options->container.addBaseUIElement(label);

    auto e = std::make_unique<OptionsElement>(BTN);
    if(withResetButton) {
        e->resetButton = std::make_unique<ResetButton>(e.get(), 0.f, 0.f, 35.f, 50.f, "", "");
    }
    e->cvar = cvar;
    e->baseElems.emplace_back(button);
    e->baseElems.emplace_back(label);
    const auto &last = this->elemContainers.emplace_back(std::move(e));
    this->uiToOptElemMap[button] = last.get();
    this->uiToOptElemMap[label] = last.get();

    return this->elemContainers.back().get();
}

OptionsElement *OptionsOverlayImpl::addButtonButton(const std::string &text1, const std::string &text2, ConVar *cvar) {
    auto *button = new UIButton(0, 0, this->options->getSize().x, 50, text1, text1);
    button->setColor(0xff0c7c99);
    button->setUseDefaultSkin();
    this->options->container.addBaseUIElement(button);

    auto *button2 = new UIButton(0, 0, this->options->getSize().x, 50, text2, text2);
    button2->setColor(0xff0c7c99);
    button2->setUseDefaultSkin();
    this->options->container.addBaseUIElement(button2);

    auto e = std::make_unique<OptionsElement>(BTN);
    e->cvar = cvar;
    e->baseElems.emplace_back(button);
    e->baseElems.emplace_back(button2);
    const auto &last = this->elemContainers.emplace_back(std::move(e));
    this->uiToOptElemMap[button] = last.get();
    this->uiToOptElemMap[button2] = last.get();

    return this->elemContainers.back().get();
}

OptionsElement *OptionsOverlayImpl::addButtonButtonLabel(const std::string &text1, const std::string &text2,
                                                         const std::string &labelText, bool withResetButton,
                                                         ConVar *cvar) {
    auto *button = new UIButton(0, 0, this->options->getSize().x, 50, text1, text1);
    button->setColor(0xff0c7c99);
    button->setUseDefaultSkin();
    this->options->container.addBaseUIElement(button);

    auto *button2 = new UIButton(0, 0, this->options->getSize().x, 50, text2, text2);
    button2->setColor(0xff0c7c99);
    button2->setUseDefaultSkin();
    this->options->container.addBaseUIElement(button2);

    auto *label = new UILabel(0, 0, this->options->getSize().x, 50, labelText, labelText);
    label->setDrawFrame(false);
    label->setDrawBackground(false);
    this->options->container.addBaseUIElement(label);

    auto e = std::make_unique<OptionsElement>(BTN);
    if(withResetButton) {
        e->resetButton = std::make_unique<ResetButton>(e.get(), 0.f, 0.f, 35.f, 50.f, "", "");
    }
    e->cvar = cvar;
    e->baseElems.emplace_back(button);
    e->baseElems.emplace_back(button2);
    e->baseElems.emplace_back(label);
    const auto &last = this->elemContainers.emplace_back(std::move(e));
    this->uiToOptElemMap[button] = last.get();
    this->uiToOptElemMap[button2] = last.get();
    this->uiToOptElemMap[label] = last.get();

    return this->elemContainers.back().get();
}

KeyBindButton *OptionsOverlayImpl::addKeyBindButton(const std::string &text, OsuKeyBinds::Bind *bind) {
    /// UString unbindIconString; unbindIconString.insert(0, Icons::UNDO);
    auto *unbindButton = new UIButton(0, 0, this->options->getSize().x, 50, text, "");
    unbindButton->setTooltipText(_("Unbind"));
    unbindButton->setColor(0x77d90000);
    unbindButton->setUseDefaultSkin();
    unbindButton->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onKeyUnbindButtonPressed>(this));
    /// unbindButton->setFont(osu->getFontIcons());
    this->options->container.addBaseUIElement(unbindButton);

    auto *bindButton = new KeyBindButton(0, 0, this->options->getSize().x, 50, text, text);
    bindButton->setColor(0xff0c7c99);
    bindButton->setUseDefaultSkin();
    bindButton->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onKeyBindingButtonPressed>(this));
    this->options->container.addBaseUIElement(bindButton);

    auto *label = new OptionsMenuKeyBindLabel(0, 0, this->options->getSize().x, 50, "", "", bind, bindButton);
    label->setDrawFrame(false);
    label->setDrawBackground(false);
    this->options->container.addBaseUIElement(label);

    auto e = std::make_unique<OptionsElement>(BINDBTN);
    e->resetButton = std::make_unique<ResetButton>(e.get(), 0.f, 0.f, 35.f, 50.f, "", "");
    e->resetButton->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onResetClicked>(this));
    e->baseElems.emplace_back(unbindButton);
    e->baseElems.emplace_back(bindButton);
    e->baseElems.emplace_back(label);
    e->cvar = bind->cvar;
    const auto &last = this->elemContainers.emplace_back(std::move(e));
    this->uiToOptElemMap[unbindButton] = last.get();
    this->uiToOptElemMap[bindButton] = last.get();
    this->uiToOptElemMap[label] = last.get();

    return bindButton;
}

CBaseUICheckbox *OptionsOverlayImpl::addCheckbox(const std::string &text, ConVar *cvar) {
    return this->addCheckbox(text, "", cvar);
}

CBaseUICheckbox *OptionsOverlayImpl::addCheckbox(const std::string &text, const std::string &tooltipText,
                                                 ConVar *cvar) {
    assert(cvar != nullptr);

    auto *checkbox = new UICheckbox(0, 0, this->options->getSize().x, 50, text, text);
    checkbox->setDrawFrame(false);
    checkbox->setDrawBackground(false);

    if(tooltipText.length() > 0) checkbox->setTooltipText(tooltipText);

    checkbox->setChecked(cvar->getBool());
    checkbox->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onCheckboxChange>(this));

    this->options->container.addBaseUIElement(checkbox);

    auto e = std::make_unique<OptionsElement>(CBX);
    e->resetButton = std::make_unique<ResetButton>(e.get(), 0.f, 0.f, 35.f, 50.f, "", "");
    e->resetButton->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onResetClicked>(this));
    e->baseElems.emplace_back(checkbox);
    e->cvar = cvar;
    const auto &last = this->elemContainers.emplace_back(std::move(e));
    this->uiToOptElemMap[checkbox] = last.get();

    return checkbox;
}

OptionsElement *OptionsOverlayImpl::addButtonCheckbox(const std::string &buttontext, const std::string &cbxtooltip) {
    auto *button = new UIButton(0, 0, this->options->getSize().x, 50, buttontext, buttontext);
    button->setColor(0xff0c7c99);
    button->setUseDefaultSkin();
    this->options->container.addBaseUIElement(button);

    auto *checkbox =
        new UICheckbox(button->getSize().x, 0, this->options->getSize().x - button->getSize().x, 50, "", "");
    checkbox->setTooltipText(cbxtooltip);
    checkbox->setWidthToContent(0);
    checkbox->setDrawFrame(false);
    checkbox->setDrawBackground(false);
    this->options->container.addBaseUIElement(checkbox);

    auto e = std::make_unique<OptionsElement>(CBX_BTN);
    e->baseElems.emplace_back(button);
    e->baseElems.emplace_back(checkbox);
    const auto &last = this->elemContainers.emplace_back(std::move(e));
    this->uiToOptElemMap[button] = last.get();
    this->uiToOptElemMap[checkbox] = last.get();

    return this->elemContainers.back().get();
}

UISlider *OptionsOverlayImpl::addSlider(const std::string &text, float min, float max, ConVar *cvar, float label1Width,
                                        bool allowOverscale, bool allowUnderscale) {
    assert(cvar != nullptr);

    auto *slider = new UISlider(0, 0, 100, 50, text);
    slider->setAllowMouseWheel(false);
    slider->setBounds(min, max);
    slider->setLiveUpdate(true);
    slider->setValue(cvar->getFloat(), false);
    slider->setChangeCallback(SA::MakeDelegate<&OptionsOverlayImpl::onSliderChange>(this));
    this->options->container.addBaseUIElement(slider);

    // UILabel vs CBaseUILabel: UILabel allows tooltips
    auto *label1 = new UILabel(0, 0, this->options->getSize().x, 50, text, text);
    label1->setDrawFrame(false);
    label1->setDrawBackground(false);
    label1->setWidthToContent();
    if(label1Width > 1) label1->setSizeX(label1Width);
    label1->setRelSizeX(label1->getSize().x);
    this->options->container.addBaseUIElement(label1);

    auto *label2 = new UILabel(0, 0, this->options->getSize().x, 50, "", "8.81");
    label2->setDrawFrame(false);
    label2->setDrawBackground(false);
    label2->setWidthToContent();
    label2->setRelSizeX(label2->getSize().x);
    this->options->container.addBaseUIElement(label2);

    auto e = std::make_unique<OptionsElement>(SLDR);
    e->resetButton = std::make_unique<ResetButton>(e.get(), 0.f, 0.f, 35.f, 50.f, "", "");
    e->resetButton->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onResetClicked>(this));
    e->baseElems.emplace_back(label1);
    e->baseElems.emplace_back(slider);
    e->baseElems.emplace_back(label2);
    e->cvar = cvar;
    e->label1Width = label1Width;
    e->relSizeDPI = label1->getFont()->getDPI();
    e->allowOverscale = allowOverscale;
    e->allowUnderscale = allowUnderscale;
    const auto &last = this->elemContainers.emplace_back(std::move(e));
    this->uiToOptElemMap[label1] = last.get();
    this->uiToOptElemMap[slider] = last.get();
    this->uiToOptElemMap[label2] = last.get();

    return slider;
}

CBaseUITextbox *OptionsOverlayImpl::addTextbox(const std::string &text, ConVar *cvar) {
    assert(cvar != nullptr);

    auto *textbox = new CBaseUITextbox(0, 0, this->options->getSize().x, 40, "");
    textbox->setText(text);
    this->options->container.addBaseUIElement(textbox);

    auto e = std::make_unique<OptionsElement>(TBX);
    e->baseElems.emplace_back(textbox);
    e->cvar = cvar;
    const auto &last = this->elemContainers.emplace_back(std::move(e));
    this->uiToOptElemMap[textbox] = last.get();

    return textbox;
}

CBaseUITextbox *OptionsOverlayImpl::addTextbox(const std::string &text, const std::string &labelText, ConVar *cvar) {
    assert(cvar != nullptr);

    auto *textbox = new CBaseUITextbox(0, 0, this->options->getSize().x, 40, "");
    textbox->setText(text);
    this->options->container.addBaseUIElement(textbox);

    auto *label = new CBaseUILabel(0, 0, this->options->getSize().x, 35, labelText, labelText);
    label->setDrawFrame(false);
    label->setDrawBackground(false);
    label->setTextColor(rgb(200, 200, 200));
    label->setWidthToContent();
    label->setScale(0.9f);
    this->options->container.addBaseUIElement(label);

    auto e = std::make_unique<OptionsElement>(TBX);
    e->baseElems.emplace_back(label);
    e->baseElems.emplace_back(textbox);
    e->cvar = cvar;
    const auto &last = this->elemContainers.emplace_back(std::move(e));
    this->uiToOptElemMap[label] = last.get();
    this->uiToOptElemMap[textbox] = last.get();

    return textbox;
}

SkinPreviewElement *OptionsOverlayImpl::addSkinPreview() {
    auto *skinPreview = new SkinPreviewElement(0, 0, 0, 200, "skincirclenumberhitresultpreview");
    this->options->container.addBaseUIElement(skinPreview);

    auto e = std::make_unique<OptionsElement>(SKNPRVW);
    e->baseElems.emplace_back(skinPreview);
    const auto &last = this->elemContainers.emplace_back(std::move(e));
    this->uiToOptElemMap[skinPreview] = last.get();

    return skinPreview;
}

SliderPreviewElement *OptionsOverlayImpl::addSliderPreview() {
    auto *sliderPreview = new SliderPreviewElement(0, 0, 0, 200, "skinsliderpreview");
    this->options->container.addBaseUIElement(sliderPreview);

    auto e = std::make_unique<OptionsElement>(SLDRPRVW);
    e->baseElems.emplace_back(sliderPreview);
    const auto &last = this->elemContainers.emplace_back(std::move(e));
    this->uiToOptElemMap[sliderPreview] = last.get();

    return sliderPreview;
}

CategoryButton *OptionsOverlayImpl::addCategory(CBaseUIElement *section, char32_t icon) {
    auto *button = new CategoryButton(section, 0, 0, 50, 50, "", UniString::to_utf8(std::u32string_view{&icon, 1}));
    button->setFont(osu->getFontIcons());
    button->setDrawBackground(false);
    button->setDrawFrame(false);
    button->setClickCallback(SA::MakeDelegate<&OptionsOverlayImpl::onCategoryClicked>(this));
    this->categories->container.addBaseUIElement(button);
    this->categoryButtons.push_back(button);

    return button;
}

void OptionsOverlayImpl::save() {
    if(!cv::options_save_on_back.getBool()) {
        debugLog("DEACTIVATED SAVE!!!! @ {:f}", engine->getTime());
        return;
    }

    cv::osu_folder.setValue(this->osuFolderTextbox->getText());
    this->updateFposuDPI();
    this->updateFposuCMper360();

    // Update windowed resolution now, so we're always saving the correct window size
    // (this function is also called on shutdown)
    const bool fs = env->winFullscreened();
    const bool fs_letterboxed = fs && cv::letterboxing.getBool();
    if(!fs && !fs_letterboxed) {
        const auto res_str = fmt::format("{:d}x{:d}", (i32)env->getWindowSize().x, (i32)env->getWindowSize().y);
        cv::windowed_resolution.setValue(res_str, false);
    }

    debugLog("Osu: Saving user config file ...");

    static constexpr const std::string_view cfg_name = NEOMOD_CFG_PATH "/osu.cfg"sv;
    static AsyncIOHandler::WriteCallback wr_callback = [](bool success) -> void {
        if(!success) {
            if(osu) {
                ui->getNotificationOverlay()->addToast(_("Failed to save osu.cfg"), ERROR_TOAST);
            } else {
                debugLog("Failed to save osu.cfg");
            }
        } else if(cv::debug_file.getBool()) {
            debugLog("Successfully wrote osu.cfg");
        }
    };

    static AsyncIOHandler::ReadCallback rd_callback = [](std::vector<u8> read_data) -> void {
        std::vector<std::string> read_lines;
        if(read_data.empty()) {
            if(Environment::fileExists(cfg_name)) {
                debugLog("WARNING: read no data from previous osu.cfg!");
                // back it up just in case
                const std::string backup_name = fmt::format("{}.{:%F}.bak", cfg_name, fmt::gmtime(std::time(nullptr)));
                if(File::copy(cfg_name, backup_name)) {
                    debugLog("backed up {} -> {}", cfg_name, backup_name);
                }
            }
        } else {
            read_lines = SString::split<std::string>(
                std::string{reinterpret_cast<const char *>(read_data.data()), read_data.size()}, '\n');
        }

        std::string write_lines;
        write_lines.reserve(read_lines.size());

        for(auto &line : read_lines) {
            SString::trim_inplace(line);
            if(line.empty()) continue;
            if(SString::is_comment(line, "#") || SString::is_comment(line, "//")) {
                if(!line.ends_with('\n')) line.push_back('\n');
                write_lines.append(line);
                continue;
            }

            bool cvar_found = false;
            const auto parts = SString::split(line, ' ');
            for(auto convar : cvars().getConVarArray()) {
                if(convar->isFlagSet(cv::NOSAVE)) continue;
                if(convar->getName() == parts[0]) {
                    cvar_found = true;
                    break;
                }
            }

            if(!cvar_found) {
                if(!line.ends_with('\n')) line.push_back('\n');
                write_lines.append(line);
                continue;
            }
        }

        if(!write_lines.empty()) {
            write_lines.append("\n\n");
        }

        for(auto convar : cvars().getConVarArray()) {
            if(!convar->canHaveValue() || convar->isFlagSet(cv::NOSAVE)) continue;
            if(convar->isDefault()) continue;
            write_lines.append(fmt::format("{} {}\n", convar->getName(), convar->getString()));
        }

        io->write(cfg_name, std::move(write_lines), wr_callback);
    };

    // let the nested write callback handle any error message
    io->read(cfg_name, rd_callback);
}

void OptionsOverlayImpl::openAndScrollToSkinSection() {
    const bool wasVisible = parent->isVisible();
    if(!wasVisible) parent->setVisible(true);

    if(!this->skinSelectLocalButton->isVisible() || !wasVisible)
        this->options->scrollToElement(this->skinSection, 0, 100 * Osu::getUIScale());
}

bool OptionsOverlayImpl::should_use_oauth_login() const {
    if(cv::force_oauth.getBool()) {
        return true;
    }

    // Addresses for which we should use OAuth login instead of username:password login
    static constexpr const auto oauth_servers = std::array{
        "neosu.local"sv, "neosu.net"sv, PACKAGE_NAME ".localhost"sv, PACKAGE_NAME ".local"sv, PACKAGE_NAME ".net"sv,
    };

    const std::string server_endpoint{this->serverTextbox->getText()};
    if(server_endpoint.empty() || !std::ranges::contains(oauth_servers, server_endpoint)) {
        return false;
    }

    // in whitelist
    return true;
}
