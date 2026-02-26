// Copyright (c) 2018, PG, All rights reserved.
#include "UserCard.h"

#include "AnimationHandler.h"
#include "Bancho.h"
#include "BanchoUsers.h"
#include "OsuConVars.h"
#include "Database.h"
#include "Engine.h"
#include "Font.h"
#include "Osu.h"
#include "Skin.h"
#include "SongBrowser/SongBrowser.h"
#include "UI.h"
#include "Graphics.h"
#include "UIAvatar.h"
#include "UIUserContextMenu.h"
#include "UserStatsScreen.h"

// NOTE: selected username is stored in this->sText

UserCard::UserCard(i32 user_id) : CBaseUIButton() {
    this->setID(user_id);

    this->fPP = 0.0f;
    this->fAcc = 0.0f;
    this->iLevel = 0;
    this->fPercentToNextLevel = 0.0f;

    this->fPPDelta = 0.0f;
    this->fPPDeltaAnim = 0.0f;

    // We do not pass mouse events to this->avatar
    this->setClickCallback(SA::MakeDelegate(
        [](UserCard *card) { ui->getUserActions()->open(card->user_id, card == osu->getUserButton()); }));
}

UserCard::~UserCard() { anim::deleteExistingAnimation(&this->fPPDeltaAnim); }

void UserCard::draw() {
    if(!this->bVisible) return;

    int yCounter = 0;
    const float iconHeight = this->getSize().y;
    const float iconWidth = iconHeight;

    // draw user icon background
    g->setColor(argb(1.0f, 0.1f, 0.1f, 0.1f));
    g->fillRect(this->getPos().x + 1, this->getPos().y + 1, iconWidth, iconHeight);

    // draw user icon
    if(this->avatar) {
        this->avatar->setPos(this->getPos().x + 1, this->getPos().y + 1);
        this->avatar->setSize(iconWidth, iconHeight);
        this->avatar->draw_avatar(1.f);
    } else {
        g->setColor(0xffffffff);
        g->pushClipRect(McRect(this->getPos().x + 1, this->getPos().y + 2, iconWidth, iconHeight));
        g->pushTransform();
        {
            const float scale =
                Osu::getImageScaleToFillResolution(osu->getSkin()->i_user_icon, vec2(iconWidth, iconHeight));
            g->scale(scale, scale);
            g->translate(this->getPos().x + iconWidth / 2 + 1, this->getPos().y + iconHeight / 2 + 1);
            g->drawImage(osu->getSkin()->i_user_icon);
        }
        g->popTransform();
        g->popClipRect();
    }

    // draw username
    McFont *usernameFont = osu->getSongBrowserFont();
    const float usernameScale = 0.4f;
    float usernamePaddingLeft = 0.0f;
    g->pushClipRect(McRect(this->getPos().x, this->getPos().y, this->getSize().x, iconHeight));
    g->pushTransform();
    {
        const float height = this->getSize().y * 0.5f;
        const float paddingTopPercent = (1.0f - usernameScale) * 0.1f;
        const float paddingTop = height * paddingTopPercent;
        const float paddingLeftPercent = (1.0f - usernameScale) * 0.3f;
        usernamePaddingLeft = height * paddingLeftPercent;
        const float scale = (height / usernameFont->getHeight()) * usernameScale;

        yCounter += (int)(this->getPos().y + usernameFont->getHeight() * scale + paddingTop);

        g->scale(scale, scale);
        g->translate((int)(this->getPos().x + iconWidth + usernamePaddingLeft), yCounter);
        g->setColor(0xffffffff);
        g->drawString(usernameFont, this->getText());
    }
    g->popTransform();
    g->popClipRect();

    if(cv::scores_enabled.getBool()) {
        // draw performance (pp), and accuracy
        McFont *performanceFont = osu->getSubTitleFont();
        const float performanceScale = 0.3f;
        g->pushTransform();
        {
            UString performanceString = fmt::format("Performance:{}pp", (int)std::round(this->fPP));
            UString accuracyString = fmt::format("Accuracy:{:.2f}%", this->fAcc * 100.0f);

            const float height = this->getSize().y * 0.5f;
            const float paddingTopPercent = (1.0f - performanceScale) * 0.2f;
            const float paddingTop = height * paddingTopPercent;
            const float paddingMiddlePercent = (1.0f - performanceScale) * 0.15f;
            const float paddingMiddle = height * paddingMiddlePercent;
            const float scale = (height / performanceFont->getHeight()) * performanceScale;

            yCounter += performanceFont->getHeight() * scale + paddingTop;

            g->scale(scale, scale);
            g->translate((int)(this->getPos().x + iconWidth + usernamePaddingLeft), yCounter);
            g->setColor(0xffffffff);
            if(cv::user_draw_pp.getBool()) g->drawString(performanceFont, performanceString);

            yCounter += performanceFont->getHeight() * scale + paddingMiddle;

            g->translate(0, performanceFont->getHeight() * scale + paddingMiddle);
            if(cv::user_draw_accuracy.getBool()) g->drawString(performanceFont, accuracyString);
        }
        g->popTransform();

        // draw level
        McFont *scoreFont = osu->getSubTitleFont();
        const float scoreScale = 0.3f;
        g->pushTransform();
        {
            UString scoreString = fmt::format("Lv{}", this->iLevel);

            const float height = this->getSize().y * 0.5f;
            const float paddingTopPercent = (1.0f - scoreScale) * 0.2f;
            const float paddingTop = height * paddingTopPercent;
            const float scale = (height / scoreFont->getHeight()) * scoreScale;

            yCounter += scoreFont->getHeight() * scale + paddingTop;

            // HACK: Horizontal scaling a bit so font size is closer to stable
            g->scale(scale * 0.9f, scale);
            g->translate((int)(this->getPos().x + iconWidth + usernamePaddingLeft), yCounter);
            g->setColor(0xffffffff);
            if(cv::user_draw_level.getBool()) g->drawString(scoreFont, scoreString);
        }
        g->popTransform();

        // draw level percentage bar (to next level)
        if(cv::user_draw_level_bar.getBool()) {
            const float barBorder = 1.f;
            const float barHeight = (int)(this->getSize().y - 2 * barBorder) * 0.15f;
            const float barWidth = (int)((this->getSize().x - 2 * barBorder) * 0.61f);
            g->setColor(0xff272727);  // WYSI
            g->fillRect(this->getPos().x + this->getSize().x - barWidth - barBorder - 1,
                        this->getPos().y + this->getSize().y - barHeight - barBorder, barWidth, barHeight);
            g->setColor(0xff888888);
            g->drawRect(this->getPos().x + this->getSize().x - barWidth - barBorder - 1,
                        this->getPos().y + this->getSize().y - barHeight - barBorder, barWidth, barHeight);
            g->setColor(0xffbf962a);
            g->fillRect(this->getPos().x + this->getSize().x - barWidth - barBorder - 1,
                        this->getPos().y + this->getSize().y - barHeight - barBorder,
                        barWidth * std::clamp<float>(this->fPercentToNextLevel, 0.0f, 1.0f), barHeight);
        }

        // draw pp increase/decrease delta
        McFont *deltaFont = performanceFont;
        const float deltaScale = 0.4f;
        if(this->fPPDeltaAnim > 0.0f) {
            UString performanceDeltaString = fmt::format("{:.1f}pp", this->fPPDelta);
            if(this->fPPDelta > 0.0f) performanceDeltaString.insert(0, L'+');

            const float border = 1.f;

            const float height = this->getSize().y * 0.5f;
            const float scale = (height / deltaFont->getHeight()) * deltaScale;

            const float performanceDeltaStringWidth = deltaFont->getStringWidth(performanceDeltaString) * scale;

            const vec2 backgroundSize =
                vec2(performanceDeltaStringWidth + border, deltaFont->getHeight() * scale + border * 3);
            const vec2 pos = vec2(this->getPos().x + this->getSize().x - performanceDeltaStringWidth - border,
                                  this->getPos().y + border);
            const vec2 textPos = vec2(pos.x, pos.y + deltaFont->getHeight() * scale);

            // background (to ensure readability even with stupid long usernames)
            g->setColor(argb(1.0f - (1.0f - this->fPPDeltaAnim) * (1.0f - this->fPPDeltaAnim), 0.f, 0.f, 0.f));
            g->fillRect(pos.x, pos.y, backgroundSize.x, backgroundSize.y);

            // delta text
            g->pushTransform();
            {
                g->scale(scale, scale);
                g->translate((int)textPos.x, (int)textPos.y);

                g->translate(1, 1);
                g->setColor(Color(0xff000000).setA(this->fPPDeltaAnim));

                g->drawString(deltaFont, performanceDeltaString);

                g->translate(-1, -1);
                g->setColor(Color(this->fPPDelta > 0.0f ? 0xff00ff00 : 0xffff0000).setA(this->fPPDeltaAnim));

                g->drawString(deltaFont, performanceDeltaString);
            }
            g->popTransform();
        }
    }
}

void UserCard::update(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;

    if(BANCHO::User::is_online_id(this->user_id)) {
        const UserInfo *my = BANCHO::User::get_user_info(this->user_id, true);
        this->setText(my->name);
    } else {
        this->setText(BanchoState::get_username().c_str());
    }

    // calculatePlayerStats() does nothing unless username changed or scores changed
    // so this should not be an expensive operation
    // calling this on each update loop allows us to just set db->bDidScoresChangeForStats = true
    // to recalculate local user stats
    this->updateUserStats();

    CBaseUIButton::update(c);
}

void UserCard::updateUserStats() {
    Database::PlayerStats stats;

    const bool is_self = (this->user_id == BanchoState::get_uid()) || !BANCHO::User::is_online_id(this->user_id);
    if(is_self && !BanchoState::can_submit_scores()) {
        stats = db->calculatePlayerStats(this->getText().toUtf8());
    } else {
        const UserInfo *my = BANCHO::User::get_user_info(this->user_id, true);

        const int level = Database::getLevelForScore(my->total_score);
        float percentToNextLevel = 1.f;
        u32 score_for_current_level = Database::getRequiredScoreForLevel(level);
        u32 score_for_next_level = Database::getRequiredScoreForLevel(level + 1);
        if(score_for_next_level > score_for_current_level) {
            percentToNextLevel = (float)(my->total_score - score_for_current_level) /
                                 (float)(score_for_next_level - score_for_current_level);
        }

        stats = {
            .name = my->name,
            .pp = (float)my->pp,
            .accuracy = my->accuracy,
            .level = level,
            .percentToNextLevel = percentToNextLevel,
            .totalScore = (u32)my->total_score,
        };
    }

    const bool changedPP = (this->fPP != stats.pp);
    const bool changed = (changedPP || this->fAcc != stats.accuracy || this->iLevel != stats.level ||
                          this->fPercentToNextLevel != stats.percentToNextLevel);

    const bool isFirstLoad = (this->fPP == 0.0f);
    const float newPPDelta = (stats.pp - this->fPP);
    if(newPPDelta != 0.0f) this->fPPDelta = newPPDelta;

    this->fPP = stats.pp;
    this->fAcc = stats.accuracy;
    this->iLevel = stats.level;
    this->fPercentToNextLevel = stats.percentToNextLevel;

    if(changed) {
        if(changedPP && !isFirstLoad && this->fPPDelta != 0.0f && this->fPP != 0.0f) {
            this->fPPDeltaAnim = 1.0f;
            anim::moveLinear(&this->fPPDeltaAnim, 0.0f, 25.0f, true);
        }
    }
}

void UserCard::setID(i32 new_id) {
    this->avatar.reset();

    this->user_id = new_id;

    if(BANCHO::User::is_online_id(this->user_id)) {
        this->avatar = std::make_unique<UIAvatar>(this, this->user_id, 0.f, 0.f, 0.f, 0.f);
    }
}
