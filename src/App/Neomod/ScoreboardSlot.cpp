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

ScoreboardSlot::ScoreboardSlot(const SCORE_ENTRY &score, int index, bool use_dummy_avatar, int override_is_friend) {
    if(!use_dummy_avatar) {
        this->avatar = std::make_unique<UIAvatar>(nullptr, score.player_id, 0.f, 0.f, 0.f, 0.f);
    }
    this->score = score;
    this->index = index;

    if(override_is_friend >= 0) {
        this->is_friend = override_is_friend > 0;
    } else {
        const UserInfo *user = BANCHO::User::try_get_user_info(score.player_id);
        this->is_friend = user && user->is_friend();
    }
}

void ScoreboardSlot::drawAvatar(vec2 pos, vec2 size, float alpha) const {
    if(this->avatar) {
        this->avatar->setPos(pos);
        this->avatar->setSize(size);
        this->avatar->setVisible(true);
        this->avatar->draw_avatar(alpha);
    } else if(const auto &avatar_image = osu->getSkin()->i_user_icon; avatar_image != MISSING_TEXTURE) {
        // mostly copied from UIAvatar::drawAvatar
        g->setColor(argb(alpha, 1.f, 1.f, 1.f));
        g->pushTransform();
        {
            g->scale(size.x / avatar_image.getWidth(), size.y / avatar_image.getHeight());
            g->translate(pos.x + size.x / 2.0f, pos.y + size.y / 2.0f);
            g->drawImage(avatar_image);
        }
        g->popTransform();
    }
}

void ScoreboardSlot::draw(WinCondition scoring_metric, float override_alpha) {
    const float alpha = override_alpha > -1.f ? override_alpha : this->fAlpha;
    if(alpha < 0.001f) return;

    g->pushTransform();

    g->setBlendMode(DrawBlendMode::PREMUL_ALPHA);

    McFont *font_normal = osu->getSongBrowserFont();
    McFont *font_bold = osu->getSongBrowserFontBold();

    const SlotColEffect cur_slot_color_effect = this->getCurSlotEffect();

    const float height = std::round(osu->getVirtScreenHeight() * 0.07f);
    const float width = std::round(height * 2.6f);  // does not include avatar_width
    const float avatar_height = height;
    const float avatar_width = avatar_height;
    const float padding = std::round(height * 0.05f);

    float start_y = osu->getVirtScreenHeight() / 2.0f - (height * 2.5f);
    start_y += this->y * height;
    start_y = std::round(start_y);

    if(this->fFlash > 0.f && !cv::avoid_flashes.getBool()) {
        g->setColor(Color(0xffffffff).setA((f32)this->fFlash));

        g->fillRect(0, start_y, avatar_width + width, height);
    }

    // Draw background
    g->setColor(slot_colors[BKGND][cur_slot_color_effect]);

    g->setAlpha(0.3f * alpha);

    if(cv::hud_scoreboard_use_menubuttonbackground.getBool()) {
        // XXX: Doesn't work on resolutions more vertical than 4:3
        float bg_scale = 0.625f;
        const auto &bg_img = osu->getSkin()->i_menu_button_bg2;
        float oScale = bg_img.getResolutionScale() * 0.99f;
        g->fillRect(0, start_y, avatar_width, height);
        bg_img.draw(vec2(avatar_width + (bg_img.getSizeBase().x / 2) * bg_scale - (470 * oScale) * bg_scale,
                         start_y + height / 2),
                    bg_scale);
    } else {
        g->fillRect(0, start_y, avatar_width + width, height);
    }

    // Draw avatar
    this->drawAvatar({0, start_y}, {avatar_width, avatar_height}, 0.8f * alpha);

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
        g->setColor(Color(0xff000000).setA(0.3f * alpha));

        g->drawString(font_bold, indexString);

        g->translate(-0.5f, -0.5f);
        g->setColor(Color(0xffffffff).setA(0.7f * alpha));

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
            g->setColor(Color(textShadowColor).setA((f32)alpha));

            g->drawString(font_normal, this->score.name);
            g->translate(-1, -1);
        }

        g->setColor(slot_colors[OTHER][cur_slot_color_effect]);

        g->setAlpha(alpha);
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
            g->setColor(Color(textShadowColor).setA((f32)alpha));

            g->drawString(font_normal, comboString);
            g->translate(-1, -1);
        }

        g->setColor(slot_colors[COMBOACC][cur_slot_color_effect]);

        g->setAlpha(alpha);
        g->drawString(font_normal, comboString);
    }
    g->popTransform();

    // draw win condition score text
    {
        UString wincond_based_scoretext;
        SlotColType wincond_based_coltype = OTHER;
        switch(scoring_metric) {
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
                g->setColor(Color(textShadowColor).setA((f32)alpha));

                g->drawString(font_normal, wincond_based_scoretext);
                g->translate(-1, -1);
            }

            g->setColor(slot_colors[wincond_based_coltype][cur_slot_color_effect]);

            g->setAlpha(alpha);
            g->drawString(font_normal, wincond_based_scoretext);
        }
        g->popTransform();
    }

    g->setBlendMode(DrawBlendMode::ALPHA);

    g->popTransform();
}

void ScoreboardSlot::updateIndex(int new_index, bool animate, int player_index) {
    const bool is_player = player_index == -1;
    if(is_player) {
        if(animate && new_index < this->index) {
            this->fFlash = 1.f;
            this->fFlash.set(0.0f, 0.5f, anim::QuartOut);
        }

        // Ensure the player is always visible
        player_index = new_index;
    }

    int min_visible_idx = player_index - 4;
    if(min_visible_idx < 0) min_visible_idx = 0;

    int max_visible_idx = player_index;
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
            this->y.set(scoreboard_y, 0.5f, anim::QuartOut);
            this->fAlpha.set(0.0f, 0.5f, anim::QuartOut);
        } else {
            this->y = scoreboard_y;
            this->fAlpha = 0.0f;
        }
        this->was_visible = false;
    } else if(!this->was_visible && is_visible) {
        this->y.stop();
        this->y = scoreboard_y;
        if(animate) {
            this->fAlpha = 0.f;
            this->fAlpha.set(1.0f, 0.5f, anim::QuartOut);
        } else {
            this->fAlpha = 1.0f;
        }
        this->was_visible = true;
    } else if(this->was_visible || is_visible) {
        if(animate) {
            this->y.set(scoreboard_y, 0.5f, anim::QuartOut);
        } else {
            this->y = scoreboard_y;
        }
    }

    this->index = new_index;
}
