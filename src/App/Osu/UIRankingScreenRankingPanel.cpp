// Copyright (c) 2016, PG, All rights reserved.
#include "UIRankingScreenRankingPanel.h"

#include "Engine.h"
#include "HUD.h"
#include "Osu.h"
#include "Skin.h"
#include "SkinImage.h"
#include "score.h"

UIRankingScreenRankingPanel::UIRankingScreenRankingPanel() : CBaseUIImage("", 0, 0, 0, 0, "") {
    this->setImage(osu->getSkin()->i_ranking_panel);
    this->setDrawFrame(true);

    this->iScore = 0;
    this->iNum300s = 0;
    this->iNum300gs = 0;
    this->iNum100s = 0;
    this->iNum100ks = 0;
    this->iNum50s = 0;
    this->iNumMisses = 0;
    this->iCombo = 0;
    this->fAccuracy = 0.0f;
    this->bPerfect = false;
}

void UIRankingScreenRankingPanel::draw() {
    CBaseUIImage::draw();
    if(!this->bVisible) return;

    const Skin *skin = osu->getSkin();
    const auto &score0img = skin->i_scores[0];

    const float uiScale = /*cv::ui_scale.getFloat()*/ 1.0f;  // NOTE: commented for now, doesn't really work due to
                                                             // legacy layout expectations

    const float globalScoreScale = (skin->version > 1.0f ? 1.3f : 1.05f) * uiScale;

    const int globalYOffsetRaw = -1;
    const int globalYOffset = Osu::getUIScale(globalYOffsetRaw);

    // draw score
    g->setColor(0xffffffff);
    float scale = Osu::getImageScale(score0img, 20.0f) * globalScoreScale;
    g->pushTransform();
    {
        g->scale(scale, scale);
        g->translate(this->getPos().x + Osu::getUIScale(111.0f) * uiScale,
                     this->getPos().y + (score0img->getHeight() / 2) * scale +
                         (Osu::getUIScale(11.0f) + globalYOffset) * uiScale);
        HUD::drawNumberWithSkinDigits({.number = this->iScore, .scale = scale, .combo = false, .minDigits = 8});
    }
    g->popTransform();

    // draw hit images
    const vec2 hitImageStartPos = vec2(40, 100 + globalYOffsetRaw);
    const vec2 hitGridOffsetX = vec2(200, 0);
    const vec2 hitGridOffsetY = vec2(0, 60);

    this->drawHitImage(skin->i_hit300, scale, hitImageStartPos);
    this->drawHitImage(skin->i_hit100, scale, hitImageStartPos + hitGridOffsetY);
    this->drawHitImage(skin->i_hit50, scale, hitImageStartPos + hitGridOffsetY * 2.f);
    this->drawHitImage(skin->i_hit300g, scale, hitImageStartPos + hitGridOffsetX);
    this->drawHitImage(skin->i_hit100k, scale, hitImageStartPos + hitGridOffsetX + hitGridOffsetY);
    this->drawHitImage(skin->i_hit0, scale, hitImageStartPos + hitGridOffsetX + hitGridOffsetY * 2.f);

    // draw numHits
    const vec2 numHitStartPos = hitImageStartPos + vec2(40, skin->version > 1.0f ? -16 : -25);
    scale = Osu::getImageScale(score0img, 17.0f) * globalScoreScale;

    this->drawNumHits(this->iNum300s, scale, numHitStartPos);
    this->drawNumHits(this->iNum100s, scale, numHitStartPos + hitGridOffsetY);
    this->drawNumHits(this->iNum50s, scale, numHitStartPos + hitGridOffsetY * 2.f);

    this->drawNumHits(this->iNum300gs, scale, numHitStartPos + hitGridOffsetX);
    this->drawNumHits(this->iNum100ks, scale, numHitStartPos + hitGridOffsetX + hitGridOffsetY);
    this->drawNumHits(this->iNumMisses, scale, numHitStartPos + hitGridOffsetX + hitGridOffsetY * 2.f);

    const int row4 = 260;
    const int row4ImageOffset = (skin->version > 1.0f ? 20 : 8) - 20;

    // draw combo
    scale = Osu::getImageScale(score0img, 17.0f) * globalScoreScale;
    g->pushTransform();
    {
        g->scale(scale, scale);
        g->translate(this->getPos().x + Osu::getUIScale(15.0f) * uiScale,
                     this->getPos().y + (score0img->getHeight() / 2.f) * scale +
                         (Osu::getUIScale(row4 + 10) + globalYOffset) * uiScale);
        HUD::drawComboSimple(this->iCombo, scale);
    }
    g->popTransform();

    // draw maxcombo label
    vec2 hardcodedOsuRankingMaxComboImageSize = vec2(162, 50) * (skin->i_ranking_max_combo.scale());
    scale = Osu::getRectScale(hardcodedOsuRankingMaxComboImageSize, 32.0f) * uiScale;
    g->pushTransform();
    {
        g->scale(scale, scale);
        g->translate(
            this->getPos().x + skin->i_ranking_max_combo->getWidth() * scale * 0.5f + Osu::getUIScale(4.0f) * uiScale,
            this->getPos().y + (Osu::getUIScale(row4 - 5 - row4ImageOffset) + globalYOffset) * uiScale);
        g->drawImage(skin->i_ranking_max_combo);
    }
    g->popTransform();

    // draw accuracy
    scale = Osu::getImageScale(score0img, 17.0f) * globalScoreScale;
    g->pushTransform();
    {
        g->scale(scale, scale);
        g->translate(this->getPos().x + Osu::getUIScale(195.0f) * uiScale,
                     this->getPos().y + (score0img->getHeight() / 2) * scale +
                         (Osu::getUIScale(row4 + 10) + globalYOffset) * uiScale);
        HUD::drawAccuracySimple(this->fAccuracy * 100.0f, scale);
    }
    g->popTransform();

    // draw accuracy label
    vec2 hardcodedOsuRankingAccuracyImageSize = vec2(192, 58) * (skin->i_ranking_accuracy.scale());
    scale = Osu::getRectScale(hardcodedOsuRankingAccuracyImageSize, 36.0f) * uiScale;
    g->pushTransform();
    {
        g->scale(scale, scale);
        g->translate(
            this->getPos().x + skin->i_ranking_accuracy->getWidth() * scale * 0.5f + Osu::getUIScale(183.0f) * uiScale,
            this->getPos().y + (Osu::getUIScale(row4 - 3 - row4ImageOffset) + globalYOffset) * uiScale);
        g->drawImage(skin->i_ranking_accuracy);
    }
    g->popTransform();

    // draw perfect
    if(this->bPerfect) {
        scale = Osu::getRectScale(skin->i_ranking_perfect->getSizeBaseRaw(), 94.0f) * uiScale;
        skin->i_ranking_perfect->drawRaw(
            this->getPos() +
                vec2(Osu::getUIScale(skin->version > 1.0f ? 260 : 200), Osu::getUIScale(430.0f) + globalYOffset) *
                    vec2(1.0f, 0.97f) * uiScale -
                vec2(0, skin->i_ranking_perfect->getSizeBaseRaw().y) * scale * 0.5f,
            scale);
    }
}

void UIRankingScreenRankingPanel::drawHitImage(SkinImage *img, float /*scale*/, vec2 pos) const {
    const float uiScale = /*cv::ui_scale.getFloat()*/ 1.0f;  // NOTE: commented for now, doesn't really work due to
                                                             // legacy layout expectations

    // img->setAnimationFrameForce(0);
    img->draw(
        vec2(this->getPos().x + Osu::getUIScale(pos.x) * uiScale, this->getPos().y + Osu::getUIScale(pos.y) * uiScale),
        uiScale, 0.f, false /* try non-animated */);
}

void UIRankingScreenRankingPanel::drawNumHits(int numHits, float scale, vec2 pos) const {
    const float uiScale = /*cv::ui_scale.getFloat()*/ 1.0f;  // NOTE: commented for now, doesn't really work due to
                                                             // legacy layout expectations

    g->pushTransform();
    {
        g->scale(scale, scale);
        g->translate(this->getPos().x + Osu::getUIScale(pos.x) * uiScale,
                     this->getPos().y + (osu->getSkin()->i_scores[0]->getHeight() / 2) * scale +
                         Osu::getUIScale(pos.y) * uiScale);
        HUD::drawComboSimple(numHits, scale);
    }
    g->popTransform();
}

void UIRankingScreenRankingPanel::setScore(LiveScore *score) {
    this->iScore = score->getScore();
    this->iNum300s = score->getNum300s();
    this->iNum300gs = score->getNum300gs();
    this->iNum100s = score->getNum100s();
    this->iNum100ks = score->getNum100ks();
    this->iNum50s = score->getNum50s();
    this->iNumMisses = score->getNumMisses();
    this->iCombo = score->getComboMax();
    this->fAccuracy = score->getAccuracy();
    this->bPerfect = (score->getComboFull() > 0 && this->iCombo >= score->getComboFull());
}

void UIRankingScreenRankingPanel::setScore(const FinishedScore &score) {
    this->iScore = score.score;
    this->iNum300s = score.num300s;
    this->iNum300gs = score.numGekis;
    this->iNum100s = score.num100s;
    this->iNum100ks = score.numKatus;
    this->iNum50s = score.num50s;
    this->iNumMisses = score.numMisses;
    this->iCombo = score.comboMax;
    this->fAccuracy = LiveScore::calculateAccuracy(score.num300s, score.num100s, score.num50s, score.numMisses);
    this->bPerfect = score.perfect;

    // round acc up from two decimal places
    if(this->fAccuracy > 0.0f) this->fAccuracy = std::round(this->fAccuracy * 10000.0f) / 10000.0f;
}
