// Copyright (c) 2018, PG, All rights reserved.
#include "ScoreButton.h"

#include "OptionsOverlay.h"
#include "SongBrowser.h"
// ---

#include <chrono>
#include <utility>

#include "SString.h"
#include "AnimationHandler.h"
#include "BanchoUsers.h"
#include "OsuConVars.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "Icons.h"
#include "Environment.h"
#include "MakeDelegateWrapper.h"
#include "Keyboard.h"
#include "LegacyReplay.h"
#include "ModSelector.h"
#include "Mouse.h"
#include "Osu.h"
#include "Timing.h"
#include "ResourceManager.h"
#include "Skin.h"
#include "SkinImage.h"
#include "SoundEngine.h"
#include "Font.h"
#include "TooltipOverlay.h"
#include "UI.h"
#include "UIAvatar.h"
#include "UIContextMenu.h"
#include "UserStatsScreen.h"
#include "Logging.h"
#include "SongDifficultyButton.h"

using namespace neomod::sbr;

namespace {
inline const char *comboBasedSuffix(bool perfect, bool FC) { return perfect ? " PFC" : (FC ? " FC" : ""); }
}  // namespace

UString ScoreButton::recentScoreIconString;

ScoreButton::ScoreButton(UIContextMenu *contextMenu, float xPos, float yPos, float xSize, float ySize, STYLE style)
    : CBaseUIButton(xPos, yPos, xSize, ySize, "", ""), contextMenu(contextMenu), style(style) {
    if(recentScoreIconString.length() < 1) recentScoreIconString.insert(0, Icons::ARROW_CIRCLE_UP);
}

ScoreButton::~ScoreButton() { anim::deleteExistingAnimation(&this->fIndexNumberAnim); }

void ScoreButton::draw() {
    if(!this->bVisible) return;

    // background
    if(this->style == STYLE::SONG_BROWSER) {
        // XXX: Make it flash with song BPM
        g->setColor(Color(0xff000000).setA(0.59f * (0.5f + 0.5f * this->fIndexNumberAnim)));

        const auto &backgroundImage = osu->getSkin()->i_menu_button_bg;
        g->pushTransform();
        {
            // TODO/FIXME: this is bullshit?
            const f32 scale = (SongBrowser::getUIScale() * 0.548f) / backgroundImage.scale();
            g->scale(scale, scale);
            g->translate(this->getPos().x + 2, (this->getPos().y + this->getSize().y / 2));
            g->drawImage(backgroundImage, AnchorPoint::LEFT);
        }
        g->popTransform();
    } else if(this->style == STYLE::TOP_RANKS) {
        g->setColor(Color(0xff666666).setA(0.59f * (0.5f + 0.5f * this->fIndexNumberAnim)));  // from 33413c to 4e7466

        g->fillRect(this->getPos(), this->getSize());
    }

    const int yPos = (int)this->getPos().y;  // avoid max shimmering

    if(this->avatar) {
        const float margin = this->getSize().y * 0.1;
        f32 avatar_width = this->getSize().y - (2.f * margin);
        this->avatar->setPos(this->getPos().x + margin, this->getPos().y + margin);
        this->avatar->setSize(avatar_width, avatar_width);
        this->avatar->draw_avatar(1.f);
    }

    // index number
    const float indexNumberScale = 0.35f;
    const float indexNumberWidthPercent = (this->style == STYLE::TOP_RANKS ? 0.075f : 0.15f);
    McFont *indexNumberFont = osu->getSongBrowserFontBold();
    g->pushTransform();
    {
        UString indexNumberString = fmt::format("{}", this->iScoreIndexNumber);
        const float scale = (this->getSize().y / indexNumberFont->getHeight()) * indexNumberScale;

        g->scale(scale, scale);
        g->translate((int)(this->getPos().x + this->getSize().x * indexNumberWidthPercent * 0.5f -
                           indexNumberFont->getStringWidth(indexNumberString) * scale / 2.0f),
                     (int)(yPos + this->getSize().y / 2.0f + indexNumberFont->getHeight() * scale / 2.0f));
        g->translate(0.5f, 0.5f);
        g->setColor(Color(0xff000000).setA(1.0f - (1.0f - this->fIndexNumberAnim)));

        g->drawString(indexNumberFont, indexNumberString);
        g->translate(-0.5f, -0.5f);
        g->setColor(Color(0xffffffff).setA(1.0f - (1.0f - this->fIndexNumberAnim) * (1.0f - this->fIndexNumberAnim)));

        g->drawString(indexNumberFont, indexNumberString);
    }
    g->popTransform();

    // grade
    const float gradeHeightPercent = 0.8f;
    const auto &gradeImg = osu->getSkin()->getGradeImageSmall(this->scoreGrade);
    int gradeWidth = 0;
    g->pushTransform();
    {
        const float scale = Osu::getRectScaleToFitResolution(
            gradeImg->getSizeBaseRaw(),
            vec2(this->getSize().x * (1.0f - indexNumberWidthPercent), this->getSize().y * gradeHeightPercent));
        gradeWidth = gradeImg->getSizeBaseRaw().x * scale;

        g->setColor(0xffffffff);
        gradeImg->drawRaw(
            vec2((int)(this->getPos().x + this->getSize().x * indexNumberWidthPercent + gradeWidth / 2.0f),
                 (int)(this->getPos().y + this->getSize().y / 2.0f)),
            scale);
    }
    g->popTransform();

    const float gradePaddingRight = this->getSize().y * 0.165f;

    // username | (artist + songName + diffName)
    const float usernameScale = (this->style == STYLE::TOP_RANKS ? 0.6f : 0.7f);
    McFont *usernameFont = osu->getSongBrowserFont();
    g->pushClipRect(McRect(this->getPos(), this->getSize()));
    g->pushTransform();
    {
        const float height = this->getSize().y * 0.5f;
        const float paddingTopPercent = (1.0f - usernameScale) * (this->style == STYLE::TOP_RANKS ? 0.15f : 0.4f);
        const float paddingTop = height * paddingTopPercent;
        const float scale = (height / usernameFont->getHeight()) * usernameScale;

        UString &string = (this->style == STYLE::TOP_RANKS ? this->sScoreTitle : this->sScoreUsername);

        g->scale(scale, scale);
        g->translate(
            (int)(this->getPos().x + this->getSize().x * indexNumberWidthPercent + gradeWidth + gradePaddingRight),
            (int)(yPos + height / 2.0f + usernameFont->getHeight() * scale / 2.0f + paddingTop));
        g->translate(0.75f, 0.75f);
        g->setColor(Color(0xff000000).setA(0.75f));

        g->drawString(usernameFont, string);
        g->translate(-0.75f, -0.75f);
        g->setColor(this->is_friend ? 0xffD424B0 : 0xffffffff);
        g->drawString(usernameFont, string);
    }
    g->popTransform();
    g->popClipRect();

    // score | pp | weighted 95% (pp)
    const float scoreScale = 0.5f;
    McFont *scoreFont = (this->getSize().y < 50 ? engine->getDefaultFont()
                                                : usernameFont);  // HACKHACK: switch font for very low resolutions
    g->pushTransform();
    {
        const float height = this->getSize().y * 0.5f;
        const float paddingBottomPercent = (1.0f - scoreScale) * (this->style == STYLE::TOP_RANKS ? 0.1f : 0.25f);
        const float paddingBottom = height * paddingBottomPercent;
        const float scale = (height / scoreFont->getHeight()) * scoreScale;

        // TODO: these variable names like "sScoreScorePPWeightedWeight" are so ridiculous
        constexpr Color topRanksColor = 0xffdeff87;
        constexpr Color topRanksScorePPWeightedWeightColor = 0xffbbbbbb;
        constexpr Color songBrowserColor = 0xffffffff;
        constexpr Color textShadowColor = argb(0.75f, 0.f, 0.f, 0.f);

        TextShadow shadow{.col_text = (this->style == STYLE::TOP_RANKS ? topRanksColor : songBrowserColor),
                          .col_shadow = textShadowColor,
                          .offs_px = 0.75f};

        const UString &mainString = [&]() {
            // top ranks: draw pp + weight % and weighted pp
            if(this->style == STYLE::TOP_RANKS) {
                // misleading name, this is just pp but formatted differently...
                return this->sScoreScorePPWeightedPP;
            }

            // override behavior
            if(cv::scores_always_display_pp.getBool()) {
                return this->sFmtedScorePPWithCombo;
            }

            // show score for score sorting, pp for pp sorting
            const auto &sortTypeString{cv::songbrowser_scores_sortingtype.getString()};
            if(sortTypeString == "By pp"sv) {
                return this->sFmtedScorePPWithCombo;
            } else if(sortTypeString == "By score"sv) {
                return this->sFmtedScoreScoreWithCombo;
            } else {
                // fallback behavior for other sorting types (misleading convar name)
                if(cv::scores_sort_by_pp.getBool()) {
                    return this->sFmtedScorePPWithCombo;
                } else {
                    return this->sFmtedScoreScoreWithCombo;
                }
            }
        }();

        g->scale(scale, scale);
        g->translate(
            (int)(this->getPos().x + this->getSize().x * indexNumberWidthPercent + gradeWidth + gradePaddingRight),
            (int)(yPos + height * 1.5f + scoreFont->getHeight() * scale / 2.0f - paddingBottom));

        g->drawString(scoreFont, mainString, shadow);

        if(this->style == STYLE::TOP_RANKS) {
            shadow.col_text = topRanksScorePPWeightedWeightColor;

            g->translate(scoreFont->getStringWidth(mainString) * scale, 0);
            g->drawString(scoreFont, this->sScoreScorePPWeightedWeight, shadow);
        }
    }
    g->popTransform();

    const float rightSideThirdHeight = this->getSize().y * 0.333f;
    const float rightSidePaddingRight = (this->style == STYLE::TOP_RANKS ? 5 : this->getSize().x * 0.025f);

    // mods
    const float modScale = 0.7f;
    McFont *modFont = osu->getSubTitleFont();
    g->pushTransform();
    {
        const float height = rightSideThirdHeight;
        const float paddingTopPercent = (1.0f - modScale) * 0.45f;
        const float paddingTop = height * paddingTopPercent;
        const float scale = (height / modFont->getHeight()) * modScale;

        g->scale(scale, scale);
        g->translate((int)(this->getPos().x + this->getSize().x - modFont->getStringWidth(this->sScoreMods) * scale -
                           rightSidePaddingRight),
                     (int)(yPos + height * 0.5f + modFont->getHeight() * scale / 2.0f + paddingTop));
        g->translate(0.75f, 0.75f);
        g->setColor(Color(0xff000000).setA(0.75f));

        g->drawString(modFont, this->sScoreMods);
        g->translate(-0.75f, -0.75f);
        g->setColor(0xffffffff);
        g->drawString(modFont, this->sScoreMods);
    }
    g->popTransform();

    // accuracy
    const float accScale = 0.65f;
    McFont *accFont = osu->getSubTitleFont();
    g->pushTransform();
    {
        const UString &scoreAccuracy =
            (this->style == STYLE::TOP_RANKS ? this->sScoreAccuracyFC : this->sScoreAccuracy);

        const float height = rightSideThirdHeight;
        const float paddingTopPercent = (1.0f - modScale) * 0.45f;
        const float paddingTop = height * paddingTopPercent;
        const float scale = (height / accFont->getHeight()) * accScale;

        g->scale(scale, scale);
        g->translate((int)(this->getPos().x + this->getSize().x - accFont->getStringWidth(scoreAccuracy) * scale -
                           rightSidePaddingRight),
                     (int)(yPos + height * 1.5f + accFont->getHeight() * scale / 2.0f + paddingTop));
        g->translate(0.75f, 0.75f);
        g->setColor(Color(0xff000000).setA(0.75f));

        g->drawString(accFont, scoreAccuracy);
        g->translate(-0.75f, -0.75f);
        g->setColor((this->style == STYLE::TOP_RANKS ? 0xffffcc22 : 0xffffffff));
        g->drawString(accFont, scoreAccuracy);
    }
    g->popTransform();

    // custom info (Spd.)
    if(this->style == STYLE::SONG_BROWSER && this->sCustom.length() > 0) {
        const float customScale = 0.50f;
        McFont *customFont = osu->getSubTitleFont();
        g->pushTransform();
        {
            const float height = rightSideThirdHeight;
            const float paddingTopPercent = (1.0f - modScale) * 0.45f;
            const float paddingTop = height * paddingTopPercent;
            const float scale = (height / customFont->getHeight()) * customScale;

            g->scale(scale, scale);
            g->translate((int)(this->getPos().x + this->getSize().x -
                               customFont->getStringWidth(this->sCustom) * scale - rightSidePaddingRight),
                         (int)(yPos + height * 2.325f + customFont->getHeight() * scale / 2.0f + paddingTop));
            g->translate(0.75f, 0.75f);
            g->setColor(Color(0xff000000).setA(0.75f));

            g->drawString(customFont, this->sCustom);
            g->translate(-0.75f, -0.75f);
            g->setColor(0xffffffff);
            g->drawString(customFont, this->sCustom);
        }
        g->popTransform();
    }

    if(this->style == STYLE::TOP_RANKS) {
        // weighted percent
        const float weightScale = 0.65f;
        McFont *weightFont = osu->getSubTitleFont();
        g->pushTransform();
        {
            const float height = rightSideThirdHeight;
            const float paddingBottomPercent = (1.0f - weightScale) * 0.05f;
            const float paddingBottom = height * paddingBottomPercent;
            const float scale = (height / weightFont->getHeight()) * weightScale;

            g->scale(scale, scale);
            g->translate((int)(this->getPos().x + this->getSize().x -
                               weightFont->getStringWidth(this->sScoreWeight) * scale - rightSidePaddingRight),
                         (int)(yPos + height * 2.5f + weightFont->getHeight() * scale / 2.0f - paddingBottom));
            g->translate(0.75f, 0.75f);
            g->setColor(Color(0xff000000).setA(0.75f));

            g->drawString(weightFont, this->sScoreWeight);
            g->translate(-0.75f, -0.75f);
            g->setColor(0xff999999);
            g->drawString(weightFont, this->sScoreWeight);
        }
        g->popTransform();
    }

    // recent icon + elapsed time since score
    const float upIconScale = 0.35f;
    const float timeElapsedScale = accScale;
    McFont *iconFont = osu->getFontIcons();
    McFont *timeFont = accFont;
    if(this->iScoreUnixTimestamp > 0) {
        const float iconScale = (this->getSize().y / iconFont->getHeight()) * upIconScale;
        const float iconHeight = iconFont->getHeight() * iconScale;
        f32 iconPaddingLeft = 2;
        if(this->style == STYLE::TOP_RANKS) iconPaddingLeft += this->getSize().y * 0.125f;

        g->pushTransform();
        {
            g->scale(iconScale, iconScale);
            g->translate((int)(this->getPos().x + this->getSize().x + iconPaddingLeft),
                         (int)(yPos + this->getSize().y / 2 + iconHeight / 2));
            g->translate(1, 1);
            g->setColor(Color(0xff000000).setA(0.75f));

            g->drawString(iconFont, recentScoreIconString);
            g->translate(-1, -1);
            g->setColor(0xffffffff);
            g->drawString(iconFont, recentScoreIconString);
        }
        g->popTransform();

        // elapsed time since score
        if(this->sScoreTime.length() > 0) {
            const float timeHeight = rightSideThirdHeight;
            const float timeScale = (timeHeight / timeFont->getHeight()) * timeElapsedScale;
            const float timePaddingLeft = 8;

            g->pushTransform();
            {
                g->scale(timeScale, timeScale);
                g->translate((int)(this->getPos().x + this->getSize().x + iconPaddingLeft +
                                   iconFont->getStringWidth(recentScoreIconString) * iconScale + timePaddingLeft),
                             (int)(yPos + this->getSize().y / 2 + timeFont->getHeight() * timeScale / 2));
                g->translate(0.75f, 0.75f);
                g->setColor(Color(0xff000000).setA(0.85f));

                g->drawString(timeFont, this->sScoreTime);
                g->translate(-0.75f, -0.75f);
                g->setColor(0xffffffff);
                g->drawString(timeFont, this->sScoreTime);
            }
            g->popTransform();
        }
    }

    // TODO: difference to below score in list, +12345

    if(this->style == STYLE::TOP_RANKS) {
        g->setColor(0xff111111);
        g->drawRect(this->getPos(), this->getSize());
    }
}

void ScoreButton::update(CBaseUIEventCtx &c) {
    // Update pp
    auto &sc = this->storedScore;
    if(sc.get_pp() == -1.0) {
        if(sc.get_or_calc_pp() != -1.0) {
            // NOTE: Allows dropped sliderends. Should fix with @PPV3
            const bool fullCombo = (sc.maxPossibleCombo > 0 && sc.numMisses == 0 && sc.numSliderBreaks == 0);

            if(!sc.is_online_score) {
                db->scores_mtx.lock();
            }
            auto &hashToScore = sc.is_online_score ? db->getOnlineScores() : db->getScoresMutable();
            if(const auto &it = hashToScore.find(sc.beatmap_hash); it != hashToScore.end()) {
                if(auto scorevecIt = std::ranges::find(it->second, sc); scorevecIt != it->second.end()) {
                    g_songbrowser->score_resort_scheduled = true;
                    *scorevecIt = sc;
                }
            }
            if(!sc.is_online_score) {
                db->scores_mtx.unlock();
            }

            this->sFmtedScorePPWithCombo =
                fmt::format("PP: {}pp ({}x{:s})", (int)std::round(sc.get_pp()), SString::thousands(sc.comboMax),
                            comboBasedSuffix(sc.perfect, fullCombo));
        }
    }

    if(!this->bVisible) {
        return;
    }

    // dumb hack to avoid taking focus and drawing score button tooltips over options menu
    if(ui->getOptionsOverlay()->isVisible()) {
        return;
    }

    if(this->avatar) {
        this->avatar->update(c);
        if(c.mouse_consumed()) return;
    }

    CBaseUIButton::update(c);

    // HACKHACK: this should really be part of the UI base
    // right click detection
    if(mouse->isRightDown()) {
        if(!this->bRightClickCheck) {
            this->bRightClickCheck = true;
            this->bRightClick = this->isMouseInside();
        }
    } else {
        if(this->bRightClick) {
            if(this->isMouseInside()) this->onRightMouseUpInside();
        }

        this->bRightClickCheck = false;
        this->bRightClick = false;
    }

    // tooltip (extra stats)
    if(this->isMouseInside()) {
        if(!this->isContextMenuVisible()) {
            if(this->fIndexNumberAnim > 0.0f) {
                const auto &tooltipOverlay{ui->getTooltipOverlay()};
                tooltipOverlay->begin();
                {
                    for(const auto &tooltipLine : this->tooltipLines) {
                        if(tooltipLine.length() > 0) tooltipOverlay->addLine(tooltipLine);
                    }
                    // debug
                    if(keyboard->isShiftDown()) {
                        tooltipOverlay->addLine(fmt::format("Client: {:s}", sc.client));
                    }
                }
                tooltipOverlay->end();
            }
        } else {
            anim::deleteExistingAnimation(&this->fIndexNumberAnim);
            this->fIndexNumberAnim = 0.0f;
        }
    }

    // update elapsed time string
    this->updateElapsedTimeString();

    // stuck anim reset
    if(!this->isMouseInside() && !anim::isAnimating(&this->fIndexNumberAnim)) this->fIndexNumberAnim = 0.0f;
}

void ScoreButton::highlight() {
    this->bIsPulseAnim = true;

    const int numPulses = 10;
    const float timescale = 1.75f;
    for(int i = 0; i < 2 * numPulses; i++) {
        if(i % 2 == 0)
            anim::moveQuadOut(&this->fIndexNumberAnim, 1.0f, 0.125f * timescale,
                              ((i / 2) * (0.125f + 0.15f)) * timescale - 0.001f, (i == 0));
        else
            anim::moveLinear(&this->fIndexNumberAnim, 0.0f, 0.15f * timescale,
                             (0.125f + (i / 2) * (0.125f + 0.15f)) * timescale - 0.001f);
    }
}

void ScoreButton::resetHighlight() {
    this->bIsPulseAnim = false;
    anim::deleteExistingAnimation(&this->fIndexNumberAnim);
    this->fIndexNumberAnim = 0.0f;
}

void ScoreButton::updateElapsedTimeString() {
    if(this->iScoreUnixTimestamp > 0) {
        const u64 curUnixTime =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        const u64 delta = curUnixTime - this->iScoreUnixTimestamp;

        const u64 deltaInSeconds = delta;
        const u64 deltaInMinutes = delta / 60;
        const u64 deltaInHours = deltaInMinutes / 60;
        const u64 deltaInDays = deltaInHours / 24;
        const u64 deltaInYears = deltaInDays / 365;

        if(deltaInHours < 96 || this->style == STYLE::TOP_RANKS) {
            if(deltaInDays > 364)
                this->sScoreTime = fmt::format("{}y", (int)(deltaInYears));
            else if(deltaInHours > 47)
                this->sScoreTime = fmt::format("{}d", (int)(deltaInDays));
            else if(deltaInHours >= 1)
                this->sScoreTime = fmt::format("{}h", (int)(deltaInHours));
            else if(deltaInMinutes > 0)
                this->sScoreTime = fmt::format("{}m", (int)(deltaInMinutes));
            else
                this->sScoreTime = fmt::format("{}s", (int)(deltaInSeconds));
        } else {
            this->iScoreUnixTimestamp = 0;

            if(this->sScoreTime.length() > 0) this->sScoreTime.clear();
        }
    }
}

void ScoreButton::onClicked(bool left, bool right) {
    soundEngine->play(osu->getSkin()->s_menu_hit);
    CBaseUIButton::onClicked(left, right);
}

void ScoreButton::onMouseInside() {
    this->bIsPulseAnim = false;

    if(!this->isContextMenuVisible())
        anim::moveQuadOut(&this->fIndexNumberAnim, 1.0f, 0.125f * (1.0f - this->fIndexNumberAnim), true);
}

void ScoreButton::onMouseOutside() {
    this->bIsPulseAnim = false;

    anim::moveLinear(&this->fIndexNumberAnim, 0.0f, 0.15f * this->fIndexNumberAnim, true);
}

void ScoreButton::onFocusStolen() {
    CBaseUIButton::onFocusStolen();

    this->bRightClick = false;

    if(!this->bIsPulseAnim) {
        anim::deleteExistingAnimation(&this->fIndexNumberAnim);
        this->fIndexNumberAnim = 0.0f;
    }
}

void ScoreButton::onRightMouseUpInside() {
    const vec2 pos = mouse->getPos();
    auto &sc = this->storedScore;

    if(this->contextMenu != nullptr) {
        this->contextMenu->setPos(pos);
        this->contextMenu->setRelPos(pos);
        this->contextMenu->begin(0, true);
        {
            if(!sc.server.empty() && sc.player_id != 0) {
                this->contextMenu->addButton("View Profile", 4);
            }

            this->contextMenu->addButton("Use Mods", 1);  // for scores without mods this will just nomod
            auto *replayButton = this->contextMenu->addButton("Watch replay", 2);
            if(!sc.has_possible_replay())  // e.g. mcosu scores will never have replays
            {
                replayButton->setEnabled(false);
                replayButton->setTextColor(0xff888888);
                replayButton->setTextDarkColor(0xff000000);
                // debugLog("disallowing replay button, client: {}", this->score.client);
            }

            CBaseUIButton *spacer = this->contextMenu->addButton("---");
            spacer->setEnabled(false);
            spacer->setTextColor(0xff888888);
            spacer->setTextDarkColor(0xff000000);
            CBaseUIButton *deleteButton = this->contextMenu->addButton("Delete Score", 3);
            if(sc.is_peppy_imported()) {
                // XXX: gray it out and have hover reason why user can't delete instead
                //      ...or allow delete and just store it as hidden in db
                deleteButton->setEnabled(false);
                deleteButton->setTextColor(0xff888888);
                deleteButton->setTextDarkColor(0xff000000);
            }
        }
        this->contextMenu->end(false, false);
        this->contextMenu->setClickCallback(SA::MakeDelegate<&ScoreButton::onContextMenu>(this));
        this->contextMenu->clampToRightScreenEdge();
        this->contextMenu->clampToBottomScreenEdge();
    }
}

void ScoreButton::onContextMenu(const UString &text, int id) {
    auto &sc = this->storedScore;

    if(ui->getUserStatsScreen()->isVisible()) {
        ui->setScreen(ui->getSongBrowser());
        SongDifficultyButton *song_button = g_songbrowser->getDiffButtonByHash(sc.beatmap_hash);
        g_songbrowser->selectSongButton(song_button);
    }

    if(id == 1) {
        this->onUseModsClicked();
        return;
    }

    if(id == 2) {
        LegacyReplay::load_and_watch(sc);
        return;
    }

    if(id == 3) {
        if(keyboard->isShiftDown())
            this->onDeleteScoreConfirmed(text, 1);
        else
            this->onDeleteScoreClicked();

        return;
    }

    if(id == 4) {
        auto scheme = cv::use_https.getBool() ? "https://" : "http://";
        auto user_url = fmt::format("{}osu.{}/u/{}", scheme, sc.server, sc.player_id);
        env->openURLInDefaultBrowser(user_url);
        return;
    }
}

void ScoreButton::onUseModsClicked() {
    Replay::Mods::use(this->storedScore.mods);
    soundEngine->play(osu->getSkin()->s_check_on);
}

void ScoreButton::onDeleteScoreClicked() {
    if(this->contextMenu != nullptr) {
        this->contextMenu->begin(0, true);
        {
            this->contextMenu->addButton("Really delete score?")->setEnabled(false);
            CBaseUIButton *spacer = this->contextMenu->addButton("---");
            spacer->setEnabled(false);
            spacer->setTextColor(0xff888888);
            spacer->setTextDarkColor(0xff000000);
            this->contextMenu->addButton("Yes", 1);
            this->contextMenu->addButton("No");
        }
        this->contextMenu->end(false, false);
        this->contextMenu->setClickCallback(SA::MakeDelegate<&ScoreButton::onDeleteScoreConfirmed>(this));
        this->contextMenu->clampToRightScreenEdge();
        this->contextMenu->clampToBottomScreenEdge();
    }
}

void ScoreButton::onDeleteScoreConfirmed(const UString & /*text*/, int id) {
    if(id != 1) return;

    debugLog("Deleting score");

    // absolutely disgusting
    g_songbrowser->onScoreContextMenu(this, 2);

    ui->getUserStatsScreen()->rebuildScoreButtons();
}

void ScoreButton::setScore(const FinishedScore &newscore, const DatabaseBeatmap *map, int index,
                           const UString &titleString, float weight) {
    this->storedScore = newscore;
    auto &sc = this->storedScore;

    sc.beatmap_hash = map->getMD5();
    sc.map = map;
    // debugLog(sc.dbgstr());

    this->iScoreIndexNumber = index;

    f32 AR = sc.mods.ar_override;
    f32 OD = sc.mods.od_override;
    f32 HP = sc.mods.hp_override;
    f32 CS = sc.mods.cs_override;

    const float accuracy = LiveScore::calculateAccuracy(sc.num300s, sc.num100s, sc.num50s, sc.numMisses) * 100.0f;

    // NOTE: Allows dropped sliderends. Should fix with @PPV3
    const bool fullCombo = (sc.maxPossibleCombo > 0 && sc.numMisses == 0 && sc.numSliderBreaks == 0);

    this->is_friend = false;

    this->avatar.reset();
    if(sc.player_id != 0) {
        this->avatar = std::make_unique<UIAvatar>(this, sc.player_id, this->getPos().x, this->getPos().y,
                                                  this->getSize().y, this->getSize().y);

        const UserInfo *user = BANCHO::User::try_get_user_info(sc.player_id);
        this->is_friend = user && user->is_friend();
    }

    // display
    const std::string_view comboSuffix = comboBasedSuffix(sc.perfect, fullCombo);

    this->scoreGrade = sc.calculate_grade();
    this->sScoreUsername = sc.playerName;
    this->sFmtedScoreScoreWithCombo =
        fmt::format("Score: {} ({}x{:s})", SString::thousands(sc.score), SString::thousands(sc.comboMax), comboSuffix);

    if(f64 pp = sc.get_pp(); pp == -1.0) {
        this->sFmtedScorePPWithCombo = fmt::format("PP: ??? ({}x{:s})", SString::thousands(sc.comboMax), comboSuffix);
    } else {
        // TODO: should PP use thousands separators?
        this->sFmtedScorePPWithCombo =
            fmt::format("PP: {}pp ({}x{:s})", (int)std::round(pp), SString::thousands(sc.comboMax), comboSuffix);
    }

    this->sScoreAccuracy = fmt::format("{:.2f}%", accuracy);
    this->sScoreAccuracyFC = fmt::format("{}{:.2f}%", sc.perfect ? "PFC " : (fullCombo ? "FC" : ""), accuracy);

    this->sScoreMods = getModsStringForDisplay(sc.mods);
    this->sCustom = (sc.mods.speed != 1.0f ? fmt::format("Spd: {:g}x", sc.mods.speed) : US_(""));
    if(map != nullptr) {
        const LegacyReplay::BEATMAP_VALUES beatmapValuesForModsLegacy = LegacyReplay::getBeatmapValuesForModsLegacy(
            sc.mods.to_legacy(), map->getAR(), map->getCS(), map->getOD(), map->getHP());
        if(AR == -1.f) AR = beatmapValuesForModsLegacy.AR;
        if(OD == -1.f) OD = beatmapValuesForModsLegacy.OD;
        if(HP == -1.f) HP = beatmapValuesForModsLegacy.HP;
        if(CS == -1.f) CS = beatmapValuesForModsLegacy.CS;

        // only show these values if they are not default (or default with applied mods)
        // only show these values if they are not default with applied mods

        if(beatmapValuesForModsLegacy.CS != CS) {
            if(this->sCustom.length() > 0) this->sCustom.append(", ");

            this->sCustom.append(fmt::format("CS:{:.4g}", CS));
        }

        if(beatmapValuesForModsLegacy.AR != AR) {
            if(this->sCustom.length() > 0) this->sCustom.append(", ");

            this->sCustom.append(fmt::format("AR:{:.4g}", AR));
        }

        if(beatmapValuesForModsLegacy.OD != OD) {
            if(this->sCustom.length() > 0) this->sCustom.append(", ");

            this->sCustom.append(fmt::format("OD:{:.4g}", OD));
        }

        if(beatmapValuesForModsLegacy.HP != HP) {
            if(this->sCustom.length() > 0) this->sCustom.append(", ");

            this->sCustom.append(fmt::format("HP:{:.4g}", HP));
        }
    }

    struct tm tm;
    std::time_t timestamp = sc.unixTimestamp;
    localtime_x(&timestamp, &tm);

    std::array<char, 64> dateString{};
    int written = std::strftime(dateString.data(), dateString.size(), "%d-%b-%y %H:%M:%S", &tm);

    this->sScoreDateTime = UString{dateString.data(), written};
    this->iScoreUnixTimestamp = sc.unixTimestamp;

    UString achievedOn = "Achieved on ";
    achievedOn.append(this->sScoreDateTime);

    // tooltip
    this->tooltipLines.clear();
    this->tooltipLines.push_back(achievedOn);

    this->tooltipLines.emplace_back(fmt::format("300:{} 100:{} 50:{} Miss:{} SBreak:{}", sc.num300s, sc.num100s,
                                                sc.num50s, sc.numMisses, sc.numSliderBreaks));

    this->tooltipLines.emplace_back(fmt::format("Accuracy: {:.2f}%", accuracy));

    UString tooltipMods = "Mods: ";
    if(this->sScoreMods.length() > 0)
        tooltipMods.append(this->sScoreMods);
    else
        tooltipMods.append("None");

    using enum ModFlags;

    this->tooltipLines.push_back(tooltipMods);
    if(flags::has<NoHP>(sc.mods.flags)) this->tooltipLines.emplace_back("+ no HP drain");
    if(flags::has<ApproachDifferent>(sc.mods.flags)) this->tooltipLines.emplace_back("+ approach different");
    if(flags::has<ARTimewarp>(sc.mods.flags)) this->tooltipLines.emplace_back("+ AR timewarp");
    if(flags::has<ARWobble>(sc.mods.flags)) this->tooltipLines.emplace_back("+ AR wobble");
    if(flags::has<FadingCursor>(sc.mods.flags)) this->tooltipLines.emplace_back("+ fading cursor");
    if(flags::has<FullAlternate>(sc.mods.flags)) this->tooltipLines.emplace_back("+ alternate");
    if(flags::has<FPoSu_Strafing>(sc.mods.flags)) this->tooltipLines.emplace_back("+ FPoSu strafing");
    if(flags::has<FPS>(sc.mods.flags)) this->tooltipLines.emplace_back("+ FPS");
    if(flags::has<HalfWindow>(sc.mods.flags)) this->tooltipLines.emplace_back("+ half window");
    if(flags::has<Jigsaw1>(sc.mods.flags)) this->tooltipLines.emplace_back("+ jigsaw1");
    if(flags::has<Jigsaw2>(sc.mods.flags)) this->tooltipLines.emplace_back("+ jigsaw2");
    if(flags::has<Mafham>(sc.mods.flags)) this->tooltipLines.emplace_back("+ mafham");
    if(flags::has<Millhioref>(sc.mods.flags)) this->tooltipLines.emplace_back("+ millhioref");
    if(flags::has<Minimize>(sc.mods.flags)) this->tooltipLines.emplace_back("+ minimize");
    if(flags::has<Ming3012>(sc.mods.flags)) this->tooltipLines.emplace_back("+ ming3012");
    if(flags::has<MirrorHorizontal>(sc.mods.flags)) this->tooltipLines.emplace_back("+ mirror (horizontal)");
    if(flags::has<MirrorVertical>(sc.mods.flags)) this->tooltipLines.emplace_back("+ mirror (vertical)");
    if(flags::has<No50s>(sc.mods.flags)) this->tooltipLines.emplace_back("+ no 50s");
    if(flags::has<No100s>(sc.mods.flags)) this->tooltipLines.emplace_back("+ no 100s");
    if(flags::has<ReverseSliders>(sc.mods.flags)) this->tooltipLines.emplace_back("+ reverse sliders");
    if(flags::has<Timewarp>(sc.mods.flags)) this->tooltipLines.emplace_back("+ timewarp");
    if(flags::has<Shirone>(sc.mods.flags)) this->tooltipLines.emplace_back("+ shirone");
    if(flags::has<StrictTracking>(sc.mods.flags)) this->tooltipLines.emplace_back("+ strict tracking");
    if(flags::has<Wobble1>(sc.mods.flags)) this->tooltipLines.emplace_back("+ wobble1");
    if(flags::has<Wobble2>(sc.mods.flags)) this->tooltipLines.emplace_back("+ wobble2");
    // TODO: missing mods such as singletap?

    if(this->style == STYLE::TOP_RANKS) {
        const int weightRounded = std::round(weight * 100.0f);
        const int ppWeightedRounded = std::round(sc.get_pp() * weight);

        this->sScoreTitle = titleString;
        this->sScoreScorePPWeightedPP = fmt::format("{}pp", (int)std::round(sc.get_pp()));
        this->sScoreScorePPWeightedWeight = fmt::format("     weighted {}% ({}pp)", weightRounded, ppWeightedRounded);
        this->sScoreWeight = fmt::format("weighted {}%", weightRounded);

        this->tooltipLines.emplace_back(fmt::format("Stars: {:.2f} ({:.2f} aim, {:.2f} speed)", sc.ppv2_total_stars,
                                                    sc.ppv2_aim_stars, sc.ppv2_speed_stars));
        this->tooltipLines.emplace_back(fmt::format("Speed: {:.3g}x", sc.mods.speed));
        this->tooltipLines.emplace_back(fmt::format("CS:{:.4g} AR:{:.4g} OD:{:.4g} HP:{:.4g}", CS, AR, OD, HP));
        this->tooltipLines.emplace_back(
            fmt::format("Error: {:.2f}ms - {:.2f}ms avg", sc.hitErrorAvgMin, sc.hitErrorAvgMax));
        this->tooltipLines.emplace_back(fmt::format("Unstable Rate: {:.2f}", sc.unstableRate));
    }

    // custom
    this->updateElapsedTimeString();
}

bool ScoreButton::isContextMenuVisible() { return (this->contextMenu != nullptr && this->contextMenu->isVisible()); }

UString ScoreButton::getModsStringForDisplay(const Replay::Mods &mods) {
    using enum ModFlags;
    using namespace flags::operators;

    UString modsString;

    // only for exact values
    const bool nc = mods.speed == 1.5f && flags::has<NoPitchCorrection>(mods.flags);
    const bool dt = mods.speed == 1.5f && !nc;  // only show dt/nc, not both
    const bool ht = mods.speed == 0.75f;

    const bool pf = flags::has<Perfect>(mods.flags);
    const bool sd = !pf && flags::has<SuddenDeath>(mods.flags);

    if(flags::has<NoFail>(mods.flags)) modsString.append("NF,");
    if(flags::has<Easy>(mods.flags)) modsString.append("EZ,");
    if(flags::has<TouchDevice>(mods.flags)) modsString.append("TD,");
    if(flags::has<Hidden>(mods.flags)) modsString.append("HD,");
    if(flags::has<HardRock>(mods.flags)) modsString.append("HR,");
    if(sd) modsString.append("SD,");
    if(dt) modsString.append("DT,");
    if(nc) modsString.append("NC,");
    if(flags::has<Relax>(mods.flags)) modsString.append("Relax,");
    if(ht) modsString.append("HT,");
    if(flags::has<Flashlight>(mods.flags)) modsString.append("FL,");
    if(flags::has<SpunOut>(mods.flags)) modsString.append("SO,");
    if(flags::has<Autopilot>(mods.flags)) modsString.append("AP,");
    if(pf) modsString.append("PF,");
    if(flags::has<ScoreV2>(mods.flags)) modsString.append("v2,");
    if(flags::has<Target>(mods.flags)) modsString.append("Target,");
    if(flags::has<Nightmare>(mods.flags)) modsString.append("Nightmare,");
    if(flags::any<MirrorHorizontal | MirrorVertical>(mods.flags)) modsString.append("Mirror,");
    if(flags::has<FPoSu>(mods.flags)) modsString.append("FPoSu,");
    if(flags::has<Singletap>(mods.flags)) modsString.append("1K,");
    if(flags::has<NoKeylock>(mods.flags)) modsString.append("4K,");

    if(modsString.length() > 0) modsString.pop_back();  // remove trailing comma

    return modsString;
}
