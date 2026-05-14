// Copyright (c) 2016, PG, All rights reserved.
#include "RankingScreen.h"

#include <cmath>

#include "AnimationHandler.h"
#include "BackgroundImageHandler.h"
#include "Bancho.h"
#include "BeatmapInterface.h"
#include "CBaseUIContainer.h"
#include "CBaseUIImage.h"
#include "CBaseUILabel.h"
#include "CBaseUIScrollView.h"
#include "Chat.h"
#include "OsuConVars.h"
#include "Graphics.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "GameRules.h"
#include "Font.h"
#include "Sound.h"
#include "Keyboard.h"
#include "LegacyReplay.h"
#include "MakeDelegateWrapper.h"
#include "ModSelector.h"
#include "Mouse.h"
#include "OptionsOverlay.h"
#include "Osu.h"
#include "ResourceManager.h"
#include "Timing.h"
#include "RoomScreen.h"
#include "Skin.h"
#include "SkinImage.h"
#include "AsyncPPCalculator.h"
#include "SongBrowser/ScoreButton.h"
#include "SongBrowser/SongBrowser.h"
#include "SoundEngine.h"
#include "SpectatorScreen.h"
#include "TooltipOverlay.h"
#include "UI.h"
#include "UIButton.h"
#include "UIBackButton.h"
#include "UIRankingScreenInfoLabel.h"
#include "UIRankingScreenRankingPanel.h"
#include "URLHistory.h"
#include "score.h"

namespace {
class RankingScreenIndexLabel final : public CBaseUILabel {
   public:
    RankingScreenIndexLabel() : CBaseUILabel(-1, 0, 0, 0, "", _("You achieved the #1 score on local rankings!")) {
        this->bVisible2 = false;
    }

    void draw() override {
        if(!this->bVisible || !this->bVisible2) return;

        // draw background gradient
        const Color topColor = 0xdd634e13;
        const Color bottomColor = 0xdd785f15;
        g->fillGradient(this->getPos(), this->getSize(), topColor, topColor, bottomColor, bottomColor);

        // draw ranking index text
        const float textScale = 0.45f;
        g->pushTransform();
        {
            const float scale = (this->getSize().y / this->fStringHeight) * textScale;

            g->scale(scale, scale);
            g->translate((int)(this->getPos().x + this->getSize().x / 2 - this->fStringWidth * scale / 2),
                         (int)(this->getPos().y + this->getSize().y / 2 + this->font->getHeight() * scale / 2));
            g->translate(1, 1);
            g->setColor(0xff000000);
            g->drawString(this->font, this->sText);
            g->translate(-1, -1);
            g->setColor(this->textColor);
            g->drawString(this->font, this->sText);
        }
        g->popTransform();
    }

    void setVisible2(bool visible2) { this->bVisible2 = visible2; }

    [[nodiscard]] inline bool isVisible2() const { return this->bVisible2; }

   private:
    bool bVisible2;
};

class RankingScreenBottomElement final : public CBaseUILabel {
   public:
    RankingScreenBottomElement() : CBaseUILabel(-1, 0, 0, 0, "", "") { this->bVisible2 = false; }

    void draw() override {
        if(!this->bVisible || !this->bVisible2) return;

        // draw background gradient
        const Color topColor = 0xdd3a2e0c;
        const Color bottomColor = 0xdd493a0f;
        g->fillGradient(this->getPos(), this->getSize(), topColor, topColor, bottomColor, bottomColor);
    }

    void setVisible2(bool visible2) { this->bVisible2 = visible2; }

    [[nodiscard]] inline bool isVisible2() const { return this->bVisible2; }

   private:
    bool bVisible2;
};

class RankingScreenScrollDownInfoButton final : public CBaseUIButton {
   public:
    RankingScreenScrollDownInfoButton() : CBaseUIButton(0, 0, 0, 0, "") {
        this->bVisible2 = false;
        this->fAlpha = 1.0f;
    }

    void draw() override {
        if(!this->bVisible || !this->bVisible2) return;

        const float textScale = 0.45f;
        g->pushTransform();
        {
            const float scale = (this->getSize().y / this->fStringHeight) * textScale;

            float animation = std::fmod((float)(engine->getTime() - 0.0f) * 3.2f, 2.0f);
            if(animation > 1.0f) animation = 2.0f - animation;

            animation = -animation * (animation - 2);  // quad out
            const float offset = -this->fStringHeight * scale * 0.25f + animation * this->fStringHeight * scale * 0.25f;

            g->scale(scale, scale);
            g->translate((int)(this->getPos().x + this->getSize().x / 2 - this->fStringWidth * scale / 2),
                         (int)(this->getPos().y + this->getSize().y / 2 + this->fStringHeight * scale / 2 - offset));
            g->translate(2, 2);
            g->setColor(Color(0xff000000).setA(this->fAlpha));

            g->drawString(this->font, this->getText());
            g->translate(-2, -2);
            g->setColor(Color(0xffffffff).setA(this->fAlpha));

            g->drawString(this->font, this->getText());
        }
        g->popTransform();
    }

    void setAlpha(float alpha) { this->fAlpha = alpha; }

    void setVisible2(bool visible2) { this->bVisible2 = visible2; }

    [[nodiscard]] inline bool isVisible2() const { return this->bVisible2; }

    bool bVisible2;
    float fAlpha;
};
}  // namespace

struct RankingScreen::RankingScreenImpl {
    RankingScreenImpl(RankingScreen *parent) {
        this->rankings = new CBaseUIScrollView(-1, 0, 0, 0, "");
        this->rankings->setHorizontalScrolling(false);
        this->rankings->setVerticalScrolling(false);
        this->rankings->setDrawFrame(false);
        this->rankings->setDrawBackground(false);
        this->rankings->setDrawScrollbars(false);
        parent->addBaseUIElement(this->rankings);

        this->songInfo = new UIRankingScreenInfoLabel(5, 5, 0, 0, "");
        parent->addBaseUIElement(this->songInfo);

        this->rankingTitle = new CBaseUIImage(osu->getSkin()->i_ranking_title.getName(), 0, 0, 0, 0, "");
        this->rankingTitle->setDrawBackground(false);
        this->rankingTitle->setDrawFrame(false);
        parent->addBaseUIElement(this->rankingTitle);

        this->rankingPanel = new UIRankingScreenRankingPanel();
        this->rankingPanel->setDrawBackground(false);
        this->rankingPanel->setDrawFrame(false);
        this->rankings->container.addBaseUIElement(this->rankingPanel);

        this->rankingGrade = new CBaseUIImage(osu->getSkin()->i_ranking_a.getName(), 0, 0, 0, 0, "");
        this->rankingGrade->setDrawBackground(false);
        this->rankingGrade->setDrawFrame(false);
        this->rankings->container.addBaseUIElement(this->rankingGrade);

        this->rankingBottom = new RankingScreenBottomElement();
        this->rankings->container.addBaseUIElement(this->rankingBottom);

        this->rankingIndex = new RankingScreenIndexLabel();
        this->rankingIndex->setDrawFrame(false);
        this->rankingIndex->setTextJustification(TEXT_JUSTIFICATION::CENTERED);
        this->rankingIndex->setTextColor(0xffffcb21);
        this->rankings->container.addBaseUIElement(this->rankingIndex);

        this->retry_btn = new UIButton(0, 0, 0, 0, "", _("Retry"));
        this->retry_btn->setClickCallback(SA::MakeDelegate<&RankingScreen::onRetryClicked>(parent));
        this->rankings->container.addBaseUIElement(this->retry_btn);
        this->watch_btn = new UIButton(0, 0, 0, 0, "", _("Watch replay"));
        this->watch_btn->setClickCallback(SA::MakeDelegate<&RankingScreen::onWatchClicked>(parent));
        this->rankings->container.addBaseUIElement(this->watch_btn);

        parent->setGrade(ScoreGrade::D);
        parent->setIndex(0);  // TEMP

        this->fUnstableRate = 0.0f;
        this->fHitErrorAvgMin = 0.0f;
        this->fHitErrorAvgMax = 0.0f;

        this->bIsUnranked = false;
    }
    CBaseUIScrollView *rankings;

    UIRankingScreenInfoLabel *songInfo;
    UIRankingScreenRankingPanel *rankingPanel;
    CBaseUIImage *rankingTitle;
    CBaseUIImage *rankingGrade;
    RankingScreenIndexLabel *rankingIndex;
    RankingScreenBottomElement *rankingBottom;

    UIButton *retry_btn;
    UIButton *watch_btn;

    ScoreGrade grade{ScoreGrade::D};  // TEMP
    float fUnstableRate;
    float fHitErrorAvgMin;
    float fHitErrorAvgMax;

    std::string sMods;
    std::vector<SkinImage Skin::*> modImages;
    std::vector<ConVar *> extraMods;

    // custom
    FinishedScore storedScore;
    bool bIsUnranked;
};

RankingScreen::RankingScreen() : ScreenBackable(), m_impl(this) {}
RankingScreen::~RankingScreen() = default;

void RankingScreen::draw() {
    if(!this->bVisible) return;

    // draw background image
    if(cv::draw_rankingscreen_background_image.getBool()) {
        osu->getBackgroundImageHandler()->draw(m_impl->storedScore.map);

        // draw top black bar
        g->setColor(0xff000000);
        g->fillRect(0, 0, osu->getVirtScreenWidth(),
                    m_impl->rankingTitle->getSize().y * cv::rankingscreen_topbar_height_percent.getFloat());
    }

    ScreenBackable::draw();

    // draw active mods
    const vec2 modPosStart = vec2(m_impl->rankings->getSize().x - Osu::getUIScale(20),
                                  m_impl->rankings->getRelPosY() + Osu::getUIScale(260));
    vec2 modPos = modPosStart;
    vec2 modPosMax{0.f};
    const auto *skin = osu->getSkin();
    for(const auto imgMemb : m_impl->modImages) {
        this->drawModImage(skin->*imgMemb, modPos, modPosMax);
    }

    // draw experimental mods
    if(m_impl->extraMods.size() > 0) {
        McFont *experimentalModFont = osu->getSubTitleFont();
        const std::string prefix = "+ ";

        float maxStringWidth = 0.0f;
        for(auto &enabledExperimentalMod : m_impl->extraMods) {
            std::string experimentalModName{enabledExperimentalMod->getName()};
            experimentalModName.insert(0, prefix);
            const float width = experimentalModFont->getStringWidth(experimentalModName);
            if(width > maxStringWidth) maxStringWidth = width;
        }

        const int backgroundMargin = 6;
        const float heightMultiplier = 1.25f;
        const int experimentalModHeight = (experimentalModFont->getHeight() * heightMultiplier);
        const vec2 experimentalModPos = vec2(modPosStart.x - maxStringWidth - backgroundMargin,
                                             std::max(modPosStart.y, modPosMax.y) + Osu::getUIScale(10) +
                                                 experimentalModFont->getHeight() * heightMultiplier);
        const int backgroundWidth = maxStringWidth + 2 * backgroundMargin;
        const int backgroundHeight = experimentalModHeight * m_impl->extraMods.size() + 2 * backgroundMargin;

        g->setColor(0x77000000);
        g->fillRect((int)experimentalModPos.x - backgroundMargin,
                    (int)experimentalModPos.y - experimentalModFont->getHeight() - backgroundMargin, backgroundWidth,
                    backgroundHeight);

        g->pushTransform();
        {
            g->translate((int)experimentalModPos.x, (int)experimentalModPos.y);
            for(auto &enabledExperimentalMod : m_impl->extraMods) {
                std::string experimentalModName{enabledExperimentalMod->getName()};
                experimentalModName.insert(0, prefix);

                g->translate(1.5f, 1.5f);
                g->setColor(0xff000000);
                g->drawString(experimentalModFont, experimentalModName);
                g->translate(-1.5f, -1.5f);
                g->setColor(0xffffffff);
                g->drawString(experimentalModFont, experimentalModName);
                g->translate(0, experimentalModHeight);
            }
        }
        g->popTransform();
    }

    // draw pp
    if(cv::rankingscreen_pp.getBool()) {
        const std::string ppString = this->getPPString();
        const vec2 ppPos = this->getPPPosRaw();

        g->pushTransform();
        {
            g->translate((int)ppPos.x + 2, (int)ppPos.y + 2);
            g->setColor(0xff000000);
            g->drawString(osu->getTitleFont(), ppString);
            g->translate(-2, -2);
            g->setColor(0xffffffff);
            g->drawString(osu->getTitleFont(), ppString);
        }
        g->popTransform();
    }
}

void RankingScreen::drawModImage(const SkinImage &image, vec2 &pos, vec2 &max) const {
    g->setColor(0xffffffff);
    image.draw(vec2(pos.x - image.getSize().x / 2.0f, pos.y));

    pos.x -= Osu::getUIScale(20);

    if(pos.y + image.getSize().y / 2 > max.y) max.y = pos.y + image.getSize().y / 2;
}

void RankingScreen::update(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;
    ScreenBackable::update(c);

    // tooltip (pp + accuracy + unstable rate)
    if(!ui->getOptionsOverlay()->isMouseInside() && mouse->getPos().x < osu->getVirtScreenWidth() * 0.5f) {
        auto *tto = ui->getTooltipOverlay();
        tto->begin();
        {
            auto &sc = m_impl->storedScore;
            tto->addLine(fmt::format(fmt::runtime(_("{:.2f}pp")), sc.get_or_calc_pp()));
            if(sc.ppv2_total_stars > 0.0) {
                tto->addLine(fmt::format(fmt::runtime(_("Stars: {:.2f} ({:.2f} aim, {:.2f} speed)")), sc.ppv2_total_stars,
                                         sc.ppv2_aim_stars, sc.ppv2_speed_stars));
            }
            tto->addLine(fmt::format(fmt::runtime(_("Speed: {:.3g}x")), sc.mods.speed));

            const f32 AR = GameRules::arWithSpeed(sc.mods.get_naive_ar(sc.map), sc.mods.speed);
            const f32 OD = GameRules::odWithSpeed(sc.mods.get_naive_od(sc.map), sc.mods.speed);
            const f32 CS = sc.mods.get_naive_cs(sc.map);
            const f32 HP = sc.mods.get_naive_hp(sc.map);

            tto->addLine(fmt::format(fmt::runtime(_("CS:{:.2f} AR:{:.2f} OD:{:.2f} HP:{:.2f}")), CS, AR, OD, HP));

            if(m_impl->sMods.length() > 0) tto->addLine(m_impl->sMods);

            if(m_impl->fUnstableRate > 0.f) {
                tto->addLine(_("Accuracy:"));
                tto->addLine(
                    fmt::format(fmt::runtime(_("Error: {:.2f}ms - {:.2f}ms avg")), m_impl->fHitErrorAvgMin, m_impl->fHitErrorAvgMax));
                tto->addLine(fmt::format(fmt::runtime(_("Unstable Rate: {:.2f}")), m_impl->fUnstableRate));
            }
        }
        tto->end();
    }
}

CBaseUIContainer *RankingScreen::setVisible(bool visible) {
    ScreenBackable::setVisible(visible);

    if(this->bVisible) {
        this->backButton->resetAnimation();
        m_impl->rankings->scrollToY(0, false);

        this->updateLayout();
    } else {
        // Stop applause sound
        soundEngine->stop(osu->getSkin()->s_applause);
    }

    // update url history
    if(i64 score_id = m_impl->storedScore.bancho_score_id; visible && score_id != 0) {
        Mc::URLHistory::replaceState(fmt::format("/scores/{:d}", score_id).c_str());
    } else {
        Mc::URLHistory::replaceState(Osu::isBleedingEdge() ? "/online/bleedingedge/" : "/online/");
    }

    return this;
}

void RankingScreen::onRetryClicked() {
    ui->setScreen(ui->getSongBrowser());
    osu->getMapInterface()->play();
}

void RankingScreen::onWatchClicked() { LegacyReplay::load_and_watch(m_impl->storedScore); }

void RankingScreen::setScore(const FinishedScore &newscore) {
    m_impl->storedScore = newscore;
    auto &sc = m_impl->storedScore;

    m_impl->songInfo->setFromBeatmap(sc.map);
    m_impl->storedScore.map = sc.map;

    const std::string scorePlayer =
        m_impl->storedScore.playerName.empty() ? BanchoState::get_username() : m_impl->storedScore.playerName;

    m_impl->songInfo->setPlayer(m_impl->bIsUnranked ? PACKAGE_NAME : scorePlayer);
    // @PPV3: update m_score.ppv3_score, this->score.ppv3_aim_stars, this->score.ppv3_speed_stars,
    //        m_fHitErrorAvgMin, m_impl->fHitErrorAvgMax, m_impl->fUnstableRate

    bool is_same_player = !sc.playerName.compare(BanchoState::get_username());
    m_impl->retry_btn->bVisible2 = is_same_player && !BanchoState::is_in_a_multi_room();

    if(!sc.has_possible_replay()) {  // e.g. mcosu scores will never have replays
        m_impl->watch_btn->setEnabled(false);
        m_impl->watch_btn->setTextColor(0xff888888);
        m_impl->watch_btn->setTextDarkColor(0xff000000);
    } else {
        m_impl->watch_btn->setEnabled(true);
        // why isnt there just a way to have enabled/disabled buttons present differently
        // instead of manually resetting colors...?
        m_impl->watch_btn->setTextColor(argb(255, 255, 255, 255));
    }

    m_impl->watch_btn->bVisible2 = !BanchoState::is_in_a_multi_room();
    m_impl->bIsUnranked = false;

    struct tm tm;
    std::time_t timestamp = sc.unixTimestamp;
    localtime_x(&timestamp, &tm);

    std::array<char, 64> dateString{};
    size_t written = std::strftime(dateString.data(), dateString.size(), "%d-%b-%y %H:%M:%S", &tm);

    m_impl->songInfo->setDate(std::string(dateString.data(), written));
    m_impl->songInfo->setPlayer(sc.playerName);

    m_impl->rankingPanel->setScore(sc);
    this->setGrade(sc.calculate_grade());
    this->setIndex(-1);

    m_impl->fUnstableRate = sc.unstableRate;
    m_impl->fHitErrorAvgMin = sc.hitErrorAvgMin;
    m_impl->fHitErrorAvgMax = sc.hitErrorAvgMax;

    const std::string modsString = ScoreButton::getModsStringForDisplay(sc.mods);
    if(modsString.length() > 0) {
        m_impl->sMods = _("Mods: ");
        m_impl->sMods.append(modsString);
    } else
        m_impl->sMods = "";

    m_impl->modImages.clear();
    Skin::getModImagesForMods(m_impl->modImages, sc.mods);

    using enum ModFlags;

    m_impl->extraMods.clear();
    if(flags::has<FPoSu_Strafing>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::fposu_mod_strafing);
    if(flags::has<Wobble1>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_wobble);
    if(flags::has<Wobble2>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_wobble2);
    if(flags::has<ARWobble>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_arwobble);
    if(flags::has<Timewarp>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_timewarp);
    if(flags::has<ARTimewarp>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_artimewarp);
    if(flags::has<Minimize>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_minimize);
    if(flags::has<FadingCursor>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_fadingcursor);
    if(flags::has<FPS>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_fps);
    if(flags::has<StrictClicks>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_jigsaw1);
    if(flags::has<PreciseSliders>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_jigsaw2);
    if(flags::has<FullAlternate>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_fullalternate);
    if(flags::has<ReverseSliders>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_reverse_sliders);
    if(flags::has<No50s>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_no50s);
    if(flags::has<No100s>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_no100s);
    if(flags::has<Ming3012>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_ming3012);
    if(flags::has<HalfWindow>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_halfwindow);
    if(flags::has<Millhioref>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_millhioref);
    if(flags::has<Mafham>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_mafham);
    if(flags::has<StrictTracking>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_strict_tracking);
    if(flags::has<MirrorHorizontal>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::playfield_mirror_horizontal);
    if(flags::has<MirrorVertical>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::playfield_mirror_vertical);
    if(flags::has<Shirone>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_shirone);
    if(flags::has<ApproachDifferent>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_approach_different);
    if(flags::has<Singletap>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_singletap);
    if(flags::has<NoKeylock>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_no_keylock);
    if(flags::has<NoPausing>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_no_pausing);
    if(flags::has<NoHP>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::drain_disabled);
    if(flags::has<DKS>(sc.mods.flags)) m_impl->extraMods.push_back(&cv::mod_dks);
}

const FinishedScore &RankingScreen::getScore() { return m_impl->storedScore; }

void RankingScreen::updateLayout() {
    ScreenBackable::updateLayout();

    const float uiScale = Osu::getRawUIScale();
    const float dpiScale = Osu::getUIScale();

    this->setSize(osu->getVirtScreenSize());

    m_impl->rankingTitle->setImage(osu->getSkin()->i_ranking_title);
    m_impl->rankingTitle->setScale(Osu::getImageScale(m_impl->rankingTitle->getImage(), 75.0f) * uiScale,
                                   Osu::getImageScale(m_impl->rankingTitle->getImage(), 75.0f) * uiScale);
    m_impl->rankingTitle->setSize(m_impl->rankingTitle->getImage()->getWidth() * m_impl->rankingTitle->getScale().x,
                                  m_impl->rankingTitle->getImage()->getHeight() * m_impl->rankingTitle->getScale().y);
    m_impl->rankingTitle->setRelPos(this->getSize().x - m_impl->rankingTitle->getSize().x - Osu::getUIScale(20.0f), 0);

    m_impl->songInfo->setSize(
        osu->getVirtScreenWidth(),
        std::max(m_impl->songInfo->getMinimumHeight(),
                 m_impl->rankingTitle->getSize().y * cv::rankingscreen_topbar_height_percent.getFloat()));

    m_impl->rankings->setSize(osu->getVirtScreenSize().x + 2,
                              osu->getVirtScreenSize().y - m_impl->songInfo->getSize().y + 3);
    m_impl->rankings->setRelPosY(m_impl->songInfo->getSize().y - 1);

    float btn_width = 150.f * dpiScale;
    float btn_height = 50.f * dpiScale;
    m_impl->retry_btn->setSize(btn_width, btn_height);
    m_impl->watch_btn->setSize(btn_width, btn_height);
    vec2 btn_pos(m_impl->rankings->getSize().x * 0.98f - btn_width, m_impl->rankings->getSize().y * 0.90f - btn_height);
    m_impl->watch_btn->setRelPos(btn_pos);
    btn_pos.y -= btn_height + 5.f * dpiScale;
    m_impl->retry_btn->setRelPos(btn_pos);

    this->update_pos();

    // NOTE: no uiScale for rankingPanel and rankingGrade, doesn't really work due to legacy layout expectations
    const vec2 hardcodedOsuRankingPanelImageSize = vec2(622, 505) * (osu->getSkin()->i_ranking_panel.scale());
    m_impl->rankingPanel->setImage(osu->getSkin()->i_ranking_panel);
    m_impl->rankingPanel->setScale(Osu::getRectScale(hardcodedOsuRankingPanelImageSize, 317.0f),
                                   Osu::getRectScale(hardcodedOsuRankingPanelImageSize, 317.0f));
    m_impl->rankingPanel->setSize(
        std::max(hardcodedOsuRankingPanelImageSize.x * m_impl->rankingPanel->getScale().x,
                 m_impl->rankingPanel->getImage()->getWidth() * m_impl->rankingPanel->getScale().x),
        std::max(hardcodedOsuRankingPanelImageSize.y * m_impl->rankingPanel->getScale().y,
                 m_impl->rankingPanel->getImage()->getHeight() * m_impl->rankingPanel->getScale().y));

    m_impl->rankingIndex->setFont(osu->getSongBrowserFont());
    m_impl->rankingIndex->setSize(m_impl->rankings->getSize().x + 2, osu->getVirtScreenHeight() * 0.07f * uiScale);
    m_impl->rankingIndex->setBackgroundColor(0xff745e13);
    m_impl->rankingIndex->setRelPosY(m_impl->rankings->getSize().y + 1);

    m_impl->rankingBottom->setSize(m_impl->rankings->getSize().x + 2, osu->getVirtScreenHeight() * 0.2f);
    m_impl->rankingBottom->setRelPosY(m_impl->rankingIndex->getRelPos().y + m_impl->rankingIndex->getSize().y);

    this->setGrade(m_impl->grade);

    this->update_pos();
    m_impl->rankings->container.update_pos();
    m_impl->rankings->setScrollSizeToContent(0);
}

void RankingScreen::onBack() {
    if(BanchoState::is_in_a_multi_room()) {
        ui->setScreen(ui->getRoom());

        // Since we prevented on_map_change() from running while the ranking screen was visible, run it now.
        ui->getRoom()->on_map_change();
    } else {
        ui->setScreen(ui->getSongBrowser());
    }
}

void RankingScreen::setGrade(ScoreGrade grade) {
    m_impl->grade = grade;

    const auto &gradeImage = osu->getSkin()->getGradeImageLarge(grade);
    if(!gradeImage->isReady()) return;

    const vec2 hardcodedOsuRankingGradeImageSize = vec2(369, 422) * gradeImage.scale();
    m_impl->rankingGrade->setImage(gradeImage);

    const float uiScale = /*Osu::getRawUIScale()*/ 1.0f;  // NOTE: no uiScale for rankingPanel and rankingGrade,
                                                          // doesn't really work due to legacy layout expectations

    const float rankingGradeImageScale = Osu::getRectScale(hardcodedOsuRankingGradeImageSize, 230.0f) * uiScale;
    m_impl->rankingGrade->setScale(rankingGradeImageScale, rankingGradeImageScale);
    m_impl->rankingGrade->setSize(m_impl->rankingGrade->getImage()->getWidth() * m_impl->rankingGrade->getScale().x,
                                  m_impl->rankingGrade->getImage()->getHeight() * m_impl->rankingGrade->getScale().y);
    m_impl->rankingGrade->setRelPos(
        m_impl->rankings->getSize().x - Osu::getUIScale(120) -
            m_impl->rankingGrade->getImage()->getWidth() * m_impl->rankingGrade->getScale().x / 2.0f,
        -m_impl->rankings->getRelPos().y + Osu::getUIScale(osu->getSkin()->version > 1.0f ? 200 : 170) -
            m_impl->rankingGrade->getImage()->getHeight() * m_impl->rankingGrade->getScale().x / 2.0f);
}

void RankingScreen::setIndex(int index) {
    if(!cv::scores_enabled.getBool()) index = -1;

    if(index > -1) {
        m_impl->rankingIndex->setText(fmt::format(fmt::runtime(_("You achieved the #{} score on local rankings!")), (index + 1)));
        m_impl->rankingIndex->setVisible2(true);
        m_impl->rankingBottom->setVisible2(true);
    } else {
        m_impl->rankingIndex->setVisible2(false);
        m_impl->rankingBottom->setVisible2(false);
    }
}

std::string RankingScreen::getPPString() const {
    f32 pp = m_impl->storedScore.get_pp();
    if(pp == -1.0f) {
        return {};
    } else {
        return fmt::format("{:d}pp", (int)std::round(pp));
    }
}

vec2 RankingScreen::getPPPosRaw() const {
    const std::string ppString = this->getPPString();
    float ppStringWidth = osu->getTitleFont()->getStringWidth(ppString);
    return vec2(m_impl->rankingGrade->getPos().x, Osu::getRawUIScale() * 10.f) +
           vec2(m_impl->rankingGrade->getSize().x / 2 - (ppStringWidth / 2 + Osu::getRawUIScale() * 100.f),
                m_impl->rankings->getRelPosY() + Osu::getUIScale(400) + osu->getTitleFont()->getHeight() / 2);
}
