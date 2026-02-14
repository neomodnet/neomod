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
#include "score.h"

class RankingScreenIndexLabel final : public CBaseUILabel {
   public:
    RankingScreenIndexLabel() : CBaseUILabel(-1, 0, 0, 0, "", "You achieved the #1 score on local rankings!") {
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

RankingScreen::RankingScreen() : ScreenBackable() {
    this->rankings = new CBaseUIScrollView(-1, 0, 0, 0, "");
    this->rankings->setHorizontalScrolling(false);
    this->rankings->setVerticalScrolling(false);
    this->rankings->setDrawFrame(false);
    this->rankings->setDrawBackground(false);
    this->rankings->setDrawScrollbars(false);
    this->addBaseUIElement(this->rankings);

    this->songInfo = new UIRankingScreenInfoLabel(5, 5, 0, 0, "");
    this->addBaseUIElement(this->songInfo);

    this->rankingTitle = new CBaseUIImage(osu->getSkin()->i_ranking_title->getName(), 0, 0, 0, 0, "");
    this->rankingTitle->setDrawBackground(false);
    this->rankingTitle->setDrawFrame(false);
    this->addBaseUIElement(this->rankingTitle);

    this->rankingPanel = new UIRankingScreenRankingPanel();
    this->rankingPanel->setDrawBackground(false);
    this->rankingPanel->setDrawFrame(false);
    this->rankings->container.addBaseUIElement(this->rankingPanel);

    this->rankingGrade = new CBaseUIImage(osu->getSkin()->i_ranking_a->getName(), 0, 0, 0, 0, "");
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

    this->retry_btn = new UIButton(0, 0, 0, 0, "", "Retry");
    this->retry_btn->setClickCallback(SA::MakeDelegate<&RankingScreen::onRetryClicked>(this));
    this->rankings->container.addBaseUIElement(this->retry_btn);
    this->watch_btn = new UIButton(0, 0, 0, 0, "", "Watch replay");
    this->watch_btn->setClickCallback(SA::MakeDelegate<&RankingScreen::onWatchClicked>(this));
    this->rankings->container.addBaseUIElement(this->watch_btn);

    this->setGrade(ScoreGrade::D);
    this->setIndex(0);  // TEMP

    this->fUnstableRate = 0.0f;
    this->fHitErrorAvgMin = 0.0f;
    this->fHitErrorAvgMax = 0.0f;

    this->bModSS = false;
    this->bModSD = false;
    this->bModEZ = false;
    this->bModHD = false;
    this->bModHR = false;
    this->bModNightmare = false;
    this->bModScorev2 = false;
    this->bModTarget = false;
    this->bModSpunout = false;
    this->bModRelax = false;
    this->bModNF = false;
    this->bModAutopilot = false;
    this->bModAuto = false;
    this->bModTD = false;

    this->bModNC = false;
    this->bModDT = false;
    this->bModHT = false;

    this->bIsUnranked = false;
}

void RankingScreen::draw() {
    if(!this->bVisible) return;

    // draw background image
    if(cv::draw_rankingscreen_background_image.getBool()) {
        osu->getBackgroundImageHandler()->draw(osu->getMapInterface()->getBeatmap());

        // draw top black bar
        g->setColor(0xff000000);
        g->fillRect(0, 0, osu->getVirtScreenWidth(),
                    this->rankingTitle->getSize().y * cv::rankingscreen_topbar_height_percent.getFloat());
    }

    ScreenBackable::draw();

    // draw active mods
    const vec2 modPosStart =
        vec2(this->rankings->getSize().x - Osu::getUIScale(20), this->rankings->getRelPosY() + Osu::getUIScale(260));
    vec2 modPos = modPosStart;
    vec2 modPosMax{0.f};
    const auto &skin = osu->getSkin();
    if(this->bModTD) this->drawModImage(skin->i_modselect_td, modPos, modPosMax);
    if(this->bModSS)
        this->drawModImage(skin->i_modselect_pf, modPos, modPosMax);
    else if(this->bModSD)
        this->drawModImage(skin->i_modselect_sd, modPos, modPosMax);
    if(this->bModEZ) this->drawModImage(skin->i_modselect_ez, modPos, modPosMax);
    if(this->bModHD) this->drawModImage(skin->i_modselect_hd, modPos, modPosMax);
    if(this->bModHR) this->drawModImage(skin->i_modselect_hr, modPos, modPosMax);
    if(this->bModNC)
        this->drawModImage(skin->i_modselect_nc, modPos, modPosMax);
    else if(this->bModDT)
        this->drawModImage(skin->i_modselect_dt, modPos, modPosMax);
    if(this->bModNightmare) this->drawModImage(skin->i_modselect_nightmare, modPos, modPosMax);
    if(this->bModScorev2) this->drawModImage(skin->i_modselect_sv2, modPos, modPosMax);
    if(this->bModTarget) this->drawModImage(skin->i_modselect_target, modPos, modPosMax);
    if(this->bModSpunout) this->drawModImage(skin->i_modselect_so, modPos, modPosMax);
    if(this->bModRelax) this->drawModImage(skin->i_modselect_rx, modPos, modPosMax);
    if(this->bModNF) this->drawModImage(skin->i_modselect_nf, modPos, modPosMax);
    if(this->bModHT) this->drawModImage(skin->i_modselect_ht, modPos, modPosMax);
    if(this->bModAutopilot) this->drawModImage(skin->i_modselect_ap, modPos, modPosMax);
    if(this->bModAuto) this->drawModImage(skin->i_modselect_auto, modPos, modPosMax);

    // draw experimental mods
    if(this->extraMods.size() > 0) {
        McFont *experimentalModFont = osu->getSubTitleFont();
        const UString prefix = "+ ";

        float maxStringWidth = 0.0f;
        for(auto &enabledExperimentalMod : this->extraMods) {
            UString experimentalModName{enabledExperimentalMod->getName()};
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
        const int backgroundHeight = experimentalModHeight * this->extraMods.size() + 2 * backgroundMargin;

        g->setColor(0x77000000);
        g->fillRect((int)experimentalModPos.x - backgroundMargin,
                    (int)experimentalModPos.y - experimentalModFont->getHeight() - backgroundMargin, backgroundWidth,
                    backgroundHeight);

        g->pushTransform();
        {
            g->translate((int)experimentalModPos.x, (int)experimentalModPos.y);
            for(auto &enabledExperimentalMod : this->extraMods) {
                UString experimentalModName{enabledExperimentalMod->getName()};
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
        const UString ppString = this->getPPString();
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

void RankingScreen::drawModImage(SkinImage *image, vec2 &pos, vec2 &max) {
    g->setColor(0xffffffff);
    image->draw(vec2(pos.x - image->getSize().x / 2.0f, pos.y));

    pos.x -= Osu::getUIScale(20);

    if(pos.y + image->getSize().y / 2 > max.y) max.y = pos.y + image->getSize().y / 2;
}

void RankingScreen::update(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;
    ScreenBackable::update(c);

    // tooltip (pp + accuracy + unstable rate)
    if(!ui->getOptionsOverlay()->isMouseInside() && mouse->getPos().x < osu->getVirtScreenWidth() * 0.5f) {
        auto *tto = ui->getTooltipOverlay();
        tto->begin();
        {
            auto &sc = this->storedScore;
            tto->addLine(fmt::format("{:.2f}pp", sc.get_or_calc_pp()));
            if(sc.ppv2_total_stars > 0.0) {
                tto->addLine(fmt::format("Stars: {:.2f} ({:.2f} aim, {:.2f} speed)", sc.ppv2_total_stars,
                                         sc.ppv2_aim_stars, sc.ppv2_speed_stars));
            }
            tto->addLine(fmt::format("Speed: {:.3g}x", sc.mods.speed));

            const f32 AR = GameRules::arWithSpeed(sc.mods.get_naive_ar(sc.map), sc.mods.speed);
            const f32 OD = GameRules::odWithSpeed(sc.mods.get_naive_od(sc.map), sc.mods.speed);
            const f32 CS = sc.mods.get_naive_cs(sc.map);
            const f32 HP = sc.mods.get_naive_hp(sc.map);

            tto->addLine(fmt::format("CS:{:.2f} AR:{:.2f} OD:{:.2f} HP:{:.2f}", CS, AR, OD, HP));

            if(this->sMods.length() > 0) tto->addLine(this->sMods);

            if(this->fUnstableRate > 0.f) {
                tto->addLine("Accuracy:");
                tto->addLine(
                    fmt::format("Error: {:.2f}ms - {:.2f}ms avg", this->fHitErrorAvgMin, this->fHitErrorAvgMax));
                tto->addLine(fmt::format("Unstable Rate: {:.2f}", this->fUnstableRate));
            }
        }
        tto->end();
    }
}

CBaseUIContainer *RankingScreen::setVisible(bool visible) {
    ScreenBackable::setVisible(visible);

    if(this->bVisible) {
        this->backButton->resetAnimation();
        this->rankings->scrollToY(0, false);

        this->updateLayout();
    } else {
        // Stop applause sound
        soundEngine->stop(osu->getSkin()->s_applause);
    }

    return this;
}

void RankingScreen::onRetryClicked() {
    ui->setScreen(ui->getSongBrowser());
    osu->getMapInterface()->play();
}

void RankingScreen::onWatchClicked() { LegacyReplay::load_and_watch(this->storedScore); }

void RankingScreen::setScore(const FinishedScore &newscore) {
    this->storedScore = newscore;
    auto &sc = this->storedScore;

    this->songInfo->setFromBeatmap(sc.map);
    this->storedScore.map = sc.map;

    const std::string scorePlayer =
        this->storedScore.playerName.empty() ? BanchoState::get_username() : this->storedScore.playerName;

    this->songInfo->setPlayer(this->bIsUnranked ? "neosu" : scorePlayer);
    // @PPV3: update m_score.ppv3_score, this->score.ppv3_aim_stars, this->score.ppv3_speed_stars,
    //        m_fHitErrorAvgMin, this->fHitErrorAvgMax, this->fUnstableRate

    bool is_same_player = !sc.playerName.compare(BanchoState::get_username());
    this->retry_btn->bVisible2 = is_same_player && !BanchoState::is_in_a_multi_room();

    if(!sc.has_possible_replay()) {  // e.g. mcosu scores will never have replays
        this->watch_btn->setEnabled(false);
        this->watch_btn->setTextColor(0xff888888);
        this->watch_btn->setTextDarkColor(0xff000000);
    } else {
        this->watch_btn->setEnabled(true);
        // why isnt there just a way to have enabled/disabled buttons present differently
        // instead of manually resetting colors...?
        this->watch_btn->setTextColor(argb(255, 255, 255, 255));
    }

    this->watch_btn->bVisible2 = !BanchoState::is_in_a_multi_room();
    this->bIsUnranked = false;

    struct tm tm;
    std::time_t timestamp = sc.unixTimestamp;
    localtime_x(&timestamp, &tm);

    std::array<char, 64> dateString{};
    size_t written = std::strftime(dateString.data(), dateString.size(), "%d-%b-%y %H:%M:%S", &tm);

    this->songInfo->setDate(std::string(dateString.data(), written));
    this->songInfo->setPlayer(sc.playerName);

    this->rankingPanel->setScore(sc);
    this->setGrade(sc.calculate_grade());
    this->setIndex(-1);

    this->fUnstableRate = sc.unstableRate;
    this->fHitErrorAvgMin = sc.hitErrorAvgMin;
    this->fHitErrorAvgMax = sc.hitErrorAvgMax;

    const UString modsString = ScoreButton::getModsStringForDisplay(sc.mods);
    if(modsString.length() > 0) {
        this->sMods = "Mods: ";
        this->sMods.append(modsString);
    } else
        this->sMods = "";

    using enum ModFlags;

    this->bModSS = flags::has<Perfect>(sc.mods.flags);
    this->bModSD = flags::has<SuddenDeath>(sc.mods.flags);
    this->bModEZ = flags::has<Easy>(sc.mods.flags);
    this->bModHD = flags::has<Hidden>(sc.mods.flags);
    this->bModHR = flags::has<HardRock>(sc.mods.flags);
    this->bModNightmare = flags::has<Nightmare>(sc.mods.flags);
    this->bModScorev2 = flags::has<ScoreV2>(sc.mods.flags);
    this->bModTarget = flags::has<Target>(sc.mods.flags);
    this->bModSpunout = flags::has<SpunOut>(sc.mods.flags);
    this->bModRelax = flags::has<Relax>(sc.mods.flags);
    this->bModNF = flags::has<NoFail>(sc.mods.flags);
    this->bModAutopilot = flags::has<Autopilot>(sc.mods.flags);
    this->bModAuto = flags::has<Autoplay>(sc.mods.flags);
    this->bModTD = flags::has<TouchDevice>(sc.mods.flags);

    // only for exact values
    const bool nc = sc.mods.speed == 1.5f && flags::has<NoPitchCorrection>(sc.mods.flags);
    const bool dt = sc.mods.speed == 1.5f && !nc;  // only show dt/nc, not both
    const bool ht = sc.mods.speed == 0.75f;

    this->bModNC = nc;
    this->bModDT = dt;
    this->bModHT = ht;

    this->extraMods.clear();
    if(flags::has<FPoSu_Strafing>(sc.mods.flags)) this->extraMods.push_back(&cv::fposu_mod_strafing);
    if(flags::has<Wobble1>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_wobble);
    if(flags::has<Wobble2>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_wobble2);
    if(flags::has<ARWobble>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_arwobble);
    if(flags::has<Timewarp>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_timewarp);
    if(flags::has<ARTimewarp>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_artimewarp);
    if(flags::has<Minimize>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_minimize);
    if(flags::has<FadingCursor>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_fadingcursor);
    if(flags::has<FPS>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_fps);
    if(flags::has<Jigsaw1>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_jigsaw1);
    if(flags::has<Jigsaw2>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_jigsaw2);
    if(flags::has<FullAlternate>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_fullalternate);
    if(flags::has<ReverseSliders>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_reverse_sliders);
    if(flags::has<No50s>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_no50s);
    if(flags::has<No100s>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_no100s);
    if(flags::has<Ming3012>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_ming3012);
    if(flags::has<HalfWindow>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_halfwindow);
    if(flags::has<Millhioref>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_millhioref);
    if(flags::has<Mafham>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_mafham);
    if(flags::has<StrictTracking>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_strict_tracking);
    if(flags::has<MirrorHorizontal>(sc.mods.flags)) this->extraMods.push_back(&cv::playfield_mirror_horizontal);
    if(flags::has<MirrorVertical>(sc.mods.flags)) this->extraMods.push_back(&cv::playfield_mirror_vertical);
    if(flags::has<Shirone>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_shirone);
    if(flags::has<ApproachDifferent>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_approach_different);
    if(flags::has<Singletap>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_singletap);
    if(flags::has<NoKeylock>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_no_keylock);
    if(flags::has<NoPausing>(sc.mods.flags)) this->extraMods.push_back(&cv::mod_no_pausing);
    if(flags::has<NoHP>(sc.mods.flags)) this->extraMods.push_back(&cv::drain_disabled);
}

void RankingScreen::updateLayout() {
    ScreenBackable::updateLayout();

    const float uiScale = Osu::getRawUIScale();

    this->setSize(osu->getVirtScreenSize());

    this->rankingTitle->setImage(osu->getSkin()->i_ranking_title);
    this->rankingTitle->setScale(Osu::getImageScale(this->rankingTitle->getImage(), 75.0f) * uiScale,
                                 Osu::getImageScale(this->rankingTitle->getImage(), 75.0f) * uiScale);
    this->rankingTitle->setSize(this->rankingTitle->getImage()->getWidth() * this->rankingTitle->getScale().x,
                                this->rankingTitle->getImage()->getHeight() * this->rankingTitle->getScale().y);
    this->rankingTitle->setRelPos(this->getSize().x - this->rankingTitle->getSize().x - Osu::getUIScale(20.0f), 0);

    this->songInfo->setSize(
        osu->getVirtScreenWidth(),
        std::max(this->songInfo->getMinimumHeight(),
                 this->rankingTitle->getSize().y * cv::rankingscreen_topbar_height_percent.getFloat()));

    this->rankings->setSize(osu->getVirtScreenSize().x + 2,
                            osu->getVirtScreenSize().y - this->songInfo->getSize().y + 3);
    this->rankings->setRelPosY(this->songInfo->getSize().y - 1);

    float btn_width = 150.f * uiScale;
    float btn_height = 50.f * uiScale;
    this->retry_btn->setSize(btn_width, btn_height);
    this->watch_btn->setSize(btn_width, btn_height);
    vec2 btn_pos(this->rankings->getSize().x * 0.98f - btn_width, this->rankings->getSize().y * 0.90f - btn_height);
    this->watch_btn->setRelPos(btn_pos);
    btn_pos.y -= btn_height + 5.f * uiScale;
    this->retry_btn->setRelPos(btn_pos);

    this->update_pos();

    // NOTE: no uiScale for rankingPanel and rankingGrade, doesn't really work due to legacy layout expectations
    const vec2 hardcodedOsuRankingPanelImageSize = vec2(622, 505) * (osu->getSkin()->i_ranking_panel.scale());
    this->rankingPanel->setImage(osu->getSkin()->i_ranking_panel);
    this->rankingPanel->setScale(Osu::getRectScale(hardcodedOsuRankingPanelImageSize, 317.0f),
                                 Osu::getRectScale(hardcodedOsuRankingPanelImageSize, 317.0f));
    this->rankingPanel->setSize(
        std::max(hardcodedOsuRankingPanelImageSize.x * this->rankingPanel->getScale().x,
                 this->rankingPanel->getImage()->getWidth() * this->rankingPanel->getScale().x),
        std::max(hardcodedOsuRankingPanelImageSize.y * this->rankingPanel->getScale().y,
                 this->rankingPanel->getImage()->getHeight() * this->rankingPanel->getScale().y));

    this->rankingIndex->setFont(osu->getSongBrowserFont());
    this->rankingIndex->setSize(this->rankings->getSize().x + 2, osu->getVirtScreenHeight() * 0.07f * uiScale);
    this->rankingIndex->setBackgroundColor(0xff745e13);
    this->rankingIndex->setRelPosY(this->rankings->getSize().y + 1);

    this->rankingBottom->setSize(this->rankings->getSize().x + 2, osu->getVirtScreenHeight() * 0.2f);
    this->rankingBottom->setRelPosY(this->rankingIndex->getRelPos().y + this->rankingIndex->getSize().y);

    this->setGrade(this->grade);

    this->update_pos();
    this->rankings->container.update_pos();
    this->rankings->setScrollSizeToContent(0);
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
    this->grade = grade;

    const auto &gradeImage = osu->getSkin()->getGradeImageLarge(grade);
    if(!gradeImage->isReady()) return;

    const vec2 hardcodedOsuRankingGradeImageSize = vec2(369, 422) * gradeImage.scale();
    this->rankingGrade->setImage(gradeImage);

    const float uiScale = /*Osu::getRawUIScale()*/ 1.0f;  // NOTE: no uiScale for rankingPanel and rankingGrade,
                                                          // doesn't really work due to legacy layout expectations

    const float rankingGradeImageScale = Osu::getRectScale(hardcodedOsuRankingGradeImageSize, 230.0f) * uiScale;
    this->rankingGrade->setScale(rankingGradeImageScale, rankingGradeImageScale);
    this->rankingGrade->setSize(this->rankingGrade->getImage()->getWidth() * this->rankingGrade->getScale().x,
                                this->rankingGrade->getImage()->getHeight() * this->rankingGrade->getScale().y);
    this->rankingGrade->setRelPos(
        this->rankings->getSize().x - Osu::getUIScale(120) -
            this->rankingGrade->getImage()->getWidth() * this->rankingGrade->getScale().x / 2.0f,
        -this->rankings->getRelPos().y + Osu::getUIScale(osu->getSkin()->version > 1.0f ? 200 : 170) -
            this->rankingGrade->getImage()->getHeight() * this->rankingGrade->getScale().x / 2.0f);
}

void RankingScreen::setIndex(int index) {
    if(!cv::scores_enabled.getBool()) index = -1;

    if(index > -1) {
        this->rankingIndex->setText(fmt::format("You achieved the #{} score on local rankings!", (index + 1)));
        this->rankingIndex->setVisible2(true);
        this->rankingBottom->setVisible2(true);
    } else {
        this->rankingIndex->setVisible2(false);
        this->rankingBottom->setVisible2(false);
    }
}

UString RankingScreen::getPPString() const {
    f32 pp = this->storedScore.get_pp();
    if(pp == -1.0f) {
        return US_("??? pp");
    } else {
        return fmt::format("{:d}pp", (int)std::round(pp));
    }
}

vec2 RankingScreen::getPPPosRaw() const {
    const UString ppString = this->getPPString();
    float ppStringWidth = osu->getTitleFont()->getStringWidth(ppString);
    return vec2(this->rankingGrade->getPos().x, Osu::getRawUIScale() * 10.f) +
           vec2(this->rankingGrade->getSize().x / 2 - (ppStringWidth / 2 + Osu::getRawUIScale() * 100.f),
                this->rankings->getRelPosY() + Osu::getUIScale(400) + osu->getTitleFont()->getHeight() / 2);
}
