// Copyright (c) 2024, kiwec, All rights reserved.
#include "ScoreboardSlot.h"

#include "AnimationHandler.h"
#include "BanchoUsers.h"
#include "OsuConVars.h"
#include "Engine.h"
#include "Font.h"
#include "Osu.h"
#include "SongBrowser/SongBrowser.h"
#include "Skin.h"
#include "SkinImage.h"
#include "SString.h"
#include "UI.h"

ScoreboardSlot::ScoreboardSlot(const SCORE_ENTRY &score, int index) {
    this->avatar = std::make_unique<UIAvatar>(nullptr, score.player_id, 0.f, 0.f, 0.f, 0.f);
    this->score = score;
    this->index = index;

    const UserInfo *user = BANCHO::User::try_get_user_info(score.player_id);
    this->is_friend = user && user->is_friend();
}

ScoreboardSlot::~ScoreboardSlot() {
    anim::deleteExistingAnimation(&this->fAlpha);
    anim::deleteExistingAnimation(&this->fFlash);
    anim::deleteExistingAnimation(&this->y);
}

void ScoreboardSlot::draw() {
    if(this->fAlpha == 0.f) return;

    g->pushTransform();

    g->setBlendMode(DrawBlendMode::PREMUL_ALPHA);

    McFont *font_normal = osu->getSongBrowserFont();
    McFont *font_bold = osu->getSongBrowserFontBold();

    const SlotColEffect cur_slot_color_effect = this->getCurSlotEffect();

    const float height = roundf(osu->getVirtScreenHeight() * 0.07f);
    const float width = roundf(height * 2.6f);  // does not include avatar_width
    const float avatar_height = height;
    const float avatar_width = avatar_height;
    const float padding = roundf(height * 0.05f);

    float start_y = osu->getVirtScreenHeight() / 2.0f - (height * 2.5f);
    start_y += this->y * height;
    start_y = roundf(start_y);

    if(this->fFlash > 0.f && !cv::avoid_flashes.getBool()) {
        g->setColor(Color(0xffffffff).setA(this->fFlash));

        g->fillRect(0, start_y, avatar_width + width, height);
    }

    // Draw background
    g->setColor(slot_colors[BKGND][cur_slot_color_effect]);

    g->setAlpha(0.3f * this->fAlpha);

    if(cv::hud_scoreboard_use_menubuttonbackground.getBool()) {
        // XXX: Doesn't work on resolutions more vertical than 4:3
        float bg_scale = 0.625f;
        const auto *bg_img = osu->getSkin()->i_menu_button_bg2;
        float oScale = bg_img->getResolutionScale() * 0.99f;
        g->fillRect(0, start_y, avatar_width, height);
        bg_img->draw(vec2(avatar_width + (bg_img->getSizeBase().x / 2) * bg_scale - (470 * oScale) * bg_scale,
                          start_y + height / 2),
                     bg_scale);
    } else {
        g->fillRect(0, start_y, avatar_width + width, height);
    }

    // Draw avatar
    this->avatar->setPos(0, start_y);
    this->avatar->setSize(avatar_width, avatar_height);
    this->avatar->setVisible(true);
    this->avatar->draw_avatar(0.8f * this->fAlpha);

    // Draw index
    g->pushTransform();
    {
        UString indexString = fmt::format("{:d}"_cf, this->index + 1);
        const float scale = (avatar_height / font_bold->getHeight()) * 0.5f;

        g->scale(scale, scale);

        // * 0.9f because the returned font height isn't accurate :c
        g->translate(avatar_width / 2.0f - (font_bold->getStringWidth(indexString) * scale / 2.0f),
                     start_y + (avatar_height / 2.0f) + font_bold->getHeight() * scale / 2.0f * 0.9f);

        g->translate(0.5f, 0.5f);
        g->setColor(Color(0xff000000).setA(0.3f * this->fAlpha));

        g->drawString(font_bold, indexString);

        g->translate(-0.5f, -0.5f);
        g->setColor(Color(0xffffffff).setA(0.7f * this->fAlpha));

        g->drawString(font_bold, indexString);
    }
    g->popTransform();

    // Draw name
    const bool drawTextShadow = (cv::background_dim.getFloat() < 0.7f);
    const Color textShadowColor = 0x66000000;
    const float nameScale = 0.315f;

    g->pushTransform();
    {
        g->pushClipRect(McRect(avatar_width, start_y, width, height));

        const float scale = (height / font_normal->getHeight()) * nameScale;
        g->scale(scale, scale);
        g->translate(avatar_width + padding, start_y + padding + font_normal->getHeight() * scale);
        if(drawTextShadow) {
            g->translate(1, 1);
            g->setColor(Color(textShadowColor).setA(this->fAlpha));

            g->drawString(font_normal, this->score.name);
            g->translate(-1, -1);
        }

        g->setColor(slot_colors[OTHER][cur_slot_color_effect]);

        g->setAlpha(this->fAlpha);
        g->drawString(font_normal, this->score.name);
        g->popClipRect();
    }
    g->popTransform();

    // Draw combo
    const f32 comboScale = 0.26f;
    const f32 scoreScale = (height / font_normal->getHeight()) * comboScale;

    // draw combo
    g->pushTransform();
    {
        const UString comboString{fmt::format("{}x", SString::thousands(this->score.maxCombo))};
        const float stringWidth = font_normal->getStringWidth(comboString);

        g->scale(scoreScale, scoreScale);
        g->translate(avatar_width + width - stringWidth * scoreScale - padding * 1.35f, start_y + height - 2 * padding);

        if(drawTextShadow) {
            g->translate(1, 1);
            g->setColor(Color(textShadowColor).setA(this->fAlpha));

            g->drawString(font_normal, comboString);
            g->translate(-1, -1);
        }

        g->setColor(slot_colors[COMBOACC][cur_slot_color_effect]);

        g->setAlpha(this->fAlpha);
        g->drawString(font_normal, comboString);
    }
    g->popTransform();

    // draw win condition score text
    {
        UString wincond_based_scoretext;
        SlotColType wincond_based_coltype = OTHER;
        switch(ui->getHUD()->getScoringMetric()) {
            case WinCondition::ACCURACY: {
                wincond_based_coltype = COMBOACC;
                wincond_based_scoretext = fmt::format("{:.2f}%"_cf, this->score.accuracy * 100.0f);
            } break;
            case WinCondition::MISSES: {
                wincond_based_scoretext = fmt::format("{:d} misses"_cf, this->score.misses);
            } break;
            case WinCondition::PP: {
                wincond_based_scoretext = fmt::format("{:.2f}pp"_cf, this->score.pp);
            } break;
            // other conditions fall through to scorev1
            default: {
                wincond_based_scoretext = SString::thousands(this->score.score);
            } break;
        }

        g->pushTransform();
        {
            g->scale(scoreScale, scoreScale);
            g->translate(avatar_width + padding * 1.35f, start_y + height - 2 * padding);

            if(drawTextShadow) {
                g->translate(1, 1);
                g->setColor(Color(textShadowColor).setA(this->fAlpha));

                g->drawString(font_normal, wincond_based_scoretext);
                g->translate(-1, -1);
            }

            g->setColor(slot_colors[wincond_based_coltype][cur_slot_color_effect]);

            g->setAlpha(this->fAlpha);
            g->drawString(font_normal, wincond_based_scoretext);
        }
        g->popTransform();
    }

    g->setBlendMode(DrawBlendMode::ALPHA);

    g->popTransform();
}

void ScoreboardSlot::updateIndex(int new_index, bool is_player, bool animate) {
    int player_idx = ui->getHUD()->player_slot->index;
    if(is_player) {
        if(animate && new_index < this->index) {
            this->fFlash = 1.f;
            anim::moveQuartOut(&this->fFlash, 0.0f, 0.5f, 0.0f, true);
        }

        // Ensure the player is always visible
        player_idx = new_index;
    }

    int min_visible_idx = player_idx - 4;
    if(min_visible_idx < 0) min_visible_idx = 0;

    int max_visible_idx = player_idx;
    if(max_visible_idx < 5) max_visible_idx = 5;

    bool is_visible = new_index == 0 || (new_index >= min_visible_idx && new_index <= max_visible_idx);

    float scoreboard_y = 0;
    if(min_visible_idx == 0) {
        scoreboard_y = new_index;
    } else if(new_index > 0) {
        scoreboard_y = (new_index + 1) - min_visible_idx;
    }

    if(this->was_visible && !is_visible) {
        if(animate) {
            anim::moveQuartOut(&this->y, scoreboard_y, 0.5f, 0.0f, true);
            anim::moveQuartOut(&this->fAlpha, 0.0f, 0.5f, 0.0f, true);
        } else {
            this->y = scoreboard_y;
            this->fAlpha = 0.0f;
        }
        this->was_visible = false;
    } else if(!this->was_visible && is_visible) {
        anim::deleteExistingAnimation(&this->y);
        this->y = scoreboard_y;
        if(animate) {
            this->fAlpha = 0.f;
            anim::moveQuartOut(&this->fAlpha, 1.0f, 0.5f, 0.0f, true);
        } else {
            this->fAlpha = 1.0f;
        }
        this->was_visible = true;
    } else if(this->was_visible || is_visible) {
        if(animate) {
            anim::moveQuartOut(&this->y, scoreboard_y, 0.5f, 0.0f, true);
        } else {
            this->y = scoreboard_y;
        }
    }

    this->index = new_index;
}
