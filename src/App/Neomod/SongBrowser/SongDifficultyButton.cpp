// Copyright (c) 2016, PG, All rights reserved.
#include "SongDifficultyButton.h"

#include <utility>

#include "BeatmapCarousel.h"
#include "Font.h"
#include "SongBrowser.h"
// #include "Logging.h"
// ---

#include "AnimationHandler.h"
#include "BackgroundImageHandler.h"
#include "BeatmapInterface.h"
#include "OsuConVars.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "LegacyReplay.h"
#include "Graphics.h"
#include "Osu.h"
#include "Skin.h"
#include "SoundEngine.h"

using namespace neomod::sbr;

SongDifficultyButton::SongDifficultyButton(float xPos, float yPos, float xSize, float ySize, 
                                           BeatmapDifficulty* diff, SongButton* parentSongButton)
    : SongButton(xPos, yPos, xSize, ySize) {
    // must exist and be a difficulty
    assert(diff && diff->getDifficulties().empty());

    this->databaseBeatmap = diff;
    this->parentSongButton = parentSongButton;

    this->fOffsetPercentAnim = 0.f;

    // settings
    this->setHideIfSelected(false);
    this->bUpdateGradeScheduled = true;

    if(g_songbrowser->curSortMethod == SongBrowser::SortType::RANKACHIEVED) {
        this->updateGrade();
    }

    this->updateLayoutEx();
}

SongDifficultyButton::~SongDifficultyButton() { anim::deleteExistingAnimation(&this->fOffsetPercentAnim); }

void SongDifficultyButton::draw() {
    // NOTE(spec): we don't need this check because the updateClipping() call in the scrollview already sets visibility
    /*  || this->getPos().y + this->getSize().y < 0 || this->getPos().y > osu->getVirtScreenHeight() */
    if(!this->bVisible) {
        return;
    }
    CarouselButton::draw();

    const bool isIndependentDiff = this->isIndependentDiffButton();

    const auto& skin = osu->getSkin();

    // scaling
    const vec2 pos = this->getActualPos();
    const vec2 size = this->getActualSize();

    // delay requesting the image itself a bit
    if(this->fVisibleFor >= ((std::clamp<f32>(cv::background_image_loading_delay.getFloat(), 0.f, 2.f)) / 4.f)) {
        // draw background image
        this->drawBeatmapBackgroundThumbnail(
            osu->getBackgroundImageHandler()->getLoadBackgroundImage(this->databaseBeatmap));
    }

    if(this->grade != ScoreGrade::N) this->drawGrade();

    this->drawTitle(!isIndependentDiff ? 0.2f : 1.0f);
    this->drawSubTitle(!isIndependentDiff ? 0.2f : 1.0f);

    // draw diff name
    McFont* fontBold = osu->getSongBrowserFontBold();
    const float titleScale = (size.y * this->fTitleScale) / this->font->getHeight();
    const float subTitleScale = (size.y * this->fSubTitleScale) / this->font->getHeight();
    const float diffScale = (size.y * this->fDiffScale) / fontBold->getHeight();
    g->setColor(this->bSelected ? skin->c_song_select_active_text : skin->c_song_select_inactive_text);
    g->pushTransform();
    {
        g->scale(diffScale, diffScale);
        g->translate(pos.x + this->fTextOffset,
                     pos.y + size.y * this->fTextMarginScale + this->font->getHeight() * titleScale +
                         size.y * this->fTextSpacingScale + this->font->getHeight() * subTitleScale * 0.85f +
                         size.y * this->fTextSpacingScale + fontBold->getHeight() * diffScale * 0.8f);
        g->drawString(fontBold, this->databaseBeatmap->getDifficultyName());
    }
    g->popTransform();

    // draw stars
    // NOTE: stars can sometimes be infinity! (e.g. broken osu!.db database)
    float stars = this->databaseBeatmap->getStarRating(StarPrecalc::active_idx);
    if(stars > 0) {
        const float starOffsetY = (size.y * 0.85);
        const float starWidth = (size.y * 0.2);
        const float starScale = starWidth / skin->i_star->getHeight();
        const int numFullStars = std::clamp<int>((int)stars, 0, 25);
        const float partialStarScale =
            std::max(0.5f, std::clamp<float>(stars - numFullStars, 0.0f, 1.0f));  // at least 0.5x

        g->setColor(this->bSelected ? skin->c_song_select_active_text : skin->c_song_select_inactive_text);

        // full stars
        for(int i = 0; i < numFullStars; i++) {
            const float scale =
                std::min(starScale * 1.175f, starScale + i * 0.015f);  // more stars = getting bigger, up to a limit

            g->pushTransform();
            {
                g->scale(scale, scale);
                g->translate(pos.x + this->fTextOffset + starWidth / 2 + i * starWidth * 1.75f, pos.y + starOffsetY);
                g->drawImage(skin->i_star);
            }
            g->popTransform();
        }

        // partial star
        g->pushTransform();
        {
            g->scale(starScale * partialStarScale, starScale * partialStarScale);
            g->translate(pos.x + this->fTextOffset + starWidth / 2 + numFullStars * starWidth * 1.75f,
                         pos.y + starOffsetY);
            g->drawImage(skin->i_star);
        }
        g->popTransform();

        // fill leftover space up to 10 stars total (background stars)
        g->setColor(0x1effffff);
        const float backgroundStarScale = 0.6f;

        g->setBlendMode(DrawBlendMode::ADDITIVE);
        {
            for(int i = (numFullStars + 1); i < 10; i++) {
                g->pushTransform();
                {
                    g->scale(starScale * backgroundStarScale, starScale * backgroundStarScale);
                    g->translate(pos.x + this->fTextOffset + starWidth / 2 + i * starWidth * 1.75f,
                                 pos.y + starOffsetY);
                    g->drawImage(skin->i_star);
                }
                g->popTransform();
            }
        }
        g->setBlendMode(DrawBlendMode::ALPHA);
    }
}

void SongDifficultyButton::update(CBaseUIEventCtx& c) {
    if(!this->bVisible) {
        return;
    }
    CarouselButton::update(c);

    // don't try to load images while scrolling fast to avoid lag
    if(!g_carousel->isScrollingFast())
        this->fVisibleFor += engine->getFrameTime();
    else
        this->fVisibleFor = 0.f;

    // dynamic settings (moved from constructor to here)
    const bool newOffsetPercentSelectionState = (this->bSelected || !this->isIndependentDiffButton());

    if(this->lastOffsetState == OffsetState::UNINITIALIZED ||
       newOffsetPercentSelectionState != (this->lastOffsetState == OffsetState::SELECTED)) {
        this->lastOffsetState = newOffsetPercentSelectionState ? OffsetState::SELECTED : OffsetState::DESELECTED;
        const f32 targetAnim = newOffsetPercentSelectionState ? 1.f : 0.f;

        if(targetAnim != this->fOffsetPercentAnim) {
            anim::moveQuadOut(&this->fOffsetPercentAnim, targetAnim,
                              0.25f * (std::abs(targetAnim - this->fOffsetPercentAnim)), true);
        }
    }

    this->setOffsetPercent(std::lerp(0.0f, 0.075f, this->fOffsetPercentAnim));

    if(this->bUpdateGradeScheduled) {
        this->updateGrade();
    }
}

void SongDifficultyButton::resetAnimations() {
    CarouselButton::resetAnimations();

    if(this->fOffsetPercentAnim != 1.f && this->fOffsetPercentAnim != 0.f) {
        anim::deleteExistingAnimation(&this->fOffsetPercentAnim);
    }
    // force reanimate in update()
    this->lastOffsetState = OffsetState::UNINITIALIZED;
    this->fOffsetPercentAnim = 0.f;
}

void SongDifficultyButton::onClicked(bool left, bool right) {
    // NOTE: Intentionally not calling Button::onClicked(left, right), since that one plays another sound
    CBaseUIButton::onClicked(left, right);

    if(left) {
        soundEngine->play(osu->getSkin()->s_select_difficulty);

        this->select();
    }
}

void SongDifficultyButton::onSelected(bool wasSelected, SelOpts opts) {
    CarouselButton::onSelected(wasSelected, opts);

    const bool wasParentActuallySelected =
        !this->isIndependentDiffButton() && !(opts.parentUnselected) && this->parentSongButton->isSelected();

    this->updateGrade();

    // debugLog(
    //     "wasParentActuallySelected {} isIndependentDiffButton {} parentSongButton {} "
    //     "parentSongButton->isSelected {}",
    //     wasParentActuallySelected, isIndependentDiffButton(), !!parentSongButton, parentSongButton->isSelected());

    if(!wasParentActuallySelected) {
        // debugLog("running scroll jump fix for {}",
        //          this->databaseBeatmap ? this->databaseBeatmap->getFilePath() : "???");
        g_songbrowser->requestNextScrollToSongButtonJumpFix(this);
    }

    g_songbrowser->onSelectionChange(this, true);
    g_songbrowser->onDifficultySelected(this->databaseBeatmap, wasSelected);
    g_songbrowser->scrollToSongButton(this, /*alignOnTop=*/false, /*knownVisible=*/true);
}

void SongDifficultyButton::updateGrade() {
    if(!cv::scores_enabled.getBool()) {
        return;
    }
    this->bUpdateGradeScheduled = false;

    Sync::shared_lock lock(db->scores_mtx);
    const auto& dbScoreIt = db->getScores().find(this->databaseBeatmap->getMD5());
    if(dbScoreIt == db->getScores().end()) {
        return;
    }

    for(const auto& score : dbScoreIt->second) {
        if(score.grade < this->grade) {
            this->grade = score.grade;

            if(this->parentSongButton->grade > this->grade) {
                this->parentSongButton->grade = this->grade;
            }
        }
    }
}

bool SongDifficultyButton::isIndependentDiffButton() const {
    if(!this->parentSongButton->isSelected()) return true;

    // TODO: this logic is very weird and only works "accidentally";
    // you'd think returning true IFF (sibling->isSearchMatch() && sibling == this) would be enough,
    // but it doesn't work as expected...

    // check if this is the only visible sibling
    int visibleSiblings = 0;
    for(const auto* sibling : this->getSiblingsAndSelf()) {
        if(sibling->isSearchMatch()) {
            visibleSiblings++;
            if(visibleSiblings > 1) return false;  // early exit
        }
    }

    return (visibleSiblings == 1);
}

Color SongDifficultyButton::getInactiveBackgroundColor() const {
    if(this->isIndependentDiffButton())
        return SongButton::getInactiveBackgroundColor();
    else
        return argb(std::clamp<int>(cv::songbrowser_button_difficulty_inactive_color_a.getInt(), 0, 255),
                    std::clamp<int>(cv::songbrowser_button_difficulty_inactive_color_r.getInt(), 0, 255),
                    std::clamp<int>(cv::songbrowser_button_difficulty_inactive_color_g.getInt(), 0, 255),
                    std::clamp<int>(cv::songbrowser_button_difficulty_inactive_color_b.getInt(), 0, 255));
}
