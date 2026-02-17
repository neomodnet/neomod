// Copyright (c) 2016, PG, All rights reserved.
#include "CarouselButton.h"

#include <utility>

#include "StarPrecalc.h"
#include "Logging.h"
#include "SongBrowser.h"
#include "BeatmapCarousel.h"
// ---

#include "AnimationHandler.h"
#include "CBaseUIScrollView.h"
#include "OsuConVars.h"
#include "Environment.h"
#include "Engine.h"
#include "Graphics.h"
#include "Mouse.h"
#include "Osu.h"
#include "Skin.h"
#include "SoundEngine.h"
#include "UI.h"
#include "UIContextMenu.h"
#include "ContainerRanges.h"

using namespace neomod::sbr;

// Color Button::inactiveDifficultyBackgroundColor = argb(255, 0, 150, 236); // blue

CarouselButton::CarouselButton(float xPos, float yPos, float xSize, float ySize, std::nullptr_t nullp)
    : CBaseUIButton(nullp, xPos, yPos, xSize, ySize) {
    this->setHandleRightMouse(true);

    this->font = osu->getSongBrowserFont();

    this->bVisible = false;

    this->fTargetRelPosY = yPos;

    const float scale = Osu::getUIScale(baseOsuPixelsScale) * Osu::getUIScale();
    actualScaledOffsetWithMargin = vec::ceil(vec2{(int)marginPixelsX, (int)(marginPixelsY)} * scale);
    this->rect.setSize(vec::ceil(baseSize * scale));
}

CarouselButton::CarouselButton(float xPos, float yPos, float xSize, float ySize, UString name)
    : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name)) {
    this->setHandleRightMouse(true);

    this->font = osu->getSongBrowserFont();

    this->bVisible = false;

    this->fTargetRelPosY = yPos;

    const float scale = Osu::getUIScale(baseOsuPixelsScale) * Osu::getUIScale();
    actualScaledOffsetWithMargin = vec::ceil(vec2{(int)marginPixelsX, (int)(marginPixelsY)} * scale);
    this->rect.setSize(vec::ceil(baseSize * scale));
}

CarouselButton::~CarouselButton() { this->deleteAnimations(); }

void CarouselButton::deleteAnimations() {
    anim::deleteExistingAnimation(&this->fCenterOffsetAnimation);
    anim::deleteExistingAnimation(&this->fCenterOffsetVelocityAnimation);
    anim::deleteExistingAnimation(&this->fHoverOffsetAnimation);
    anim::deleteExistingAnimation(&this->fHoverMoveAwayAnimation);
}

void CarouselButton::draw() {
    if(!this->bVisible) return;

    this->drawMenuButtonBackground();

    // debug inner bounding box
    if(cv::debug_osu.getBool()) {
        // scaling
        const vec2 pos = this->getActualPos();
        const vec2 size = this->getActualSize();

        g->setColor(0xffff00ff);
        g->drawLine(pos.x, pos.y, pos.x + size.x, pos.y);
        g->drawLine(pos.x, pos.y, pos.x, pos.y + size.y);
        g->drawLine(pos.x, pos.y + size.y, pos.x + size.x, pos.y + size.y);
        g->drawLine(pos.x + size.x, pos.y, pos.x + size.x, pos.y + size.y);
    }

    // debug outer/actual bounding box
    if(cv::debug_osu.getBool()) {
        g->setColor(0xffff0000);
        g->drawLine(this->getPos().x, this->getPos().y, this->getPos().x + this->getSize().x, this->getPos().y);
        g->drawLine(this->getPos().x, this->getPos().y, this->getPos().x, this->getPos().y + this->getSize().y);
        g->drawLine(this->getPos().x, this->getPos().y + this->getSize().y, this->getPos().x + this->getSize().x,
                    this->getPos().y + this->getSize().y);
        g->drawLine(this->getPos().x + this->getSize().x, this->getPos().y, this->getPos().x + this->getSize().x,
                    this->getPos().y + this->getSize().y);
    }
}

void CarouselButton::drawMenuButtonBackground() {
    g->setColor(this->bSelected ? this->getActiveBackgroundColor() : this->getInactiveBackgroundColor());
    g->pushTransform();
    {
        g->scale(bgImageScale, bgImageScale);
        g->translate(this->getPos().x - 1.f /* random? */, this->getPos().y + this->getSize().y / 2);
        g->drawImage(osu->getSkin()->i_menu_button_bg, AnchorPoint::LEFT);
    }
    g->popTransform();
}

void CarouselButton::update(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;

    // HACKHACK: absolutely disgusting
    // temporarily fool CBaseUIElement with modified position and size
    {
        vec2 posBackup = this->getPos();
        vec2 sizeBackup = this->getSize();

        this->rect.setPos(this->getActualPos());
        this->rect.setSize(this->getActualSize());
        {
            CBaseUIButton::update(c);
        }
        this->rect.setPos(posBackup);
        this->rect.setSize(sizeBackup);
    }

    // animations need constant layout updates while visible
    this->updateLayoutEx();
}

bool CarouselButton::isMouseInside() {
    return CBaseUIButton::isMouseInside() && !g_songbrowser->contextMenu->isMouseInside() &&
           static_cast<int>(mouse->getPos().x) <= osu->getVirtScreenWidth();
}

void CarouselButton::updateLayoutEx() {
    // these should barely ever change but we have no way to detect that as of now
    {
        const float scale = Osu::getUIScale(baseOsuPixelsScale) * Osu::getUIScale();

        actualScaledOffsetWithMargin = vec::ceil(vec2{(int)marginPixelsX, (int)(marginPixelsY)} * scale);
        this->setSize(vec::ceil(baseSize * scale));

        // complete BS sizing/rounding/etc.
        // it seems that osu stable also doesn't scale these images in any way, though
        bgImageScale = (scale + 0.005f /* ??? */) / (osu->getSkin()->i_menu_button_bg.scale());
    }

    if(this->bVisible) {  // lag prevention (animationHandler overflow)
        const float centerOffsetAnimationTarget =
            ((cv::songbrowser_button_anim_y_curve.getBool() && !g_carousel->isScrollingFast())
                 ? 1.0f - std::clamp<float>(std::abs((this->getPos().y + (this->getSize().y / 2) -
                                                      g_carousel->getPos().y - g_carousel->getSize().y / 2) /
                                                     (g_carousel->getSize().y / 2)),
                                            0.0f, 1.0f)
                 : 1.f) +
            (this->bSelected && !cv::songbrowser_button_anim_y_curve.getBool() ? 0.5f : 0.0f);

        if(std::abs(this->fCenterOffsetAnimation - centerOffsetAnimationTarget) > 0.001f) {
            anim::moveQuadOut(&this->fCenterOffsetAnimation, centerOffsetAnimationTarget, 0.5f, true);
        }

        float centerOffsetVelocityAnimationTarget =
            std::clamp<float>((std::abs(g_carousel->getVelocity().y)) / 3500.0f, 0.0f, 1.0f);

        if(g_carousel->isScrollingFast() || !cv::songbrowser_button_anim_x_push.getBool())
            centerOffsetVelocityAnimationTarget = 0.0f;

        if(g_carousel->isScrolling())
            anim::moveQuadOut(&this->fCenterOffsetVelocityAnimation, 0.0f, 1.0f, true);
        else
            anim::moveQuadOut(&this->fCenterOffsetVelocityAnimation, centerOffsetVelocityAnimationTarget, 1.25f, true);
    }

    const float percentCenterOffsetAnimation = 0.05f;
    const float percentVelocityOffsetAnimation = 0.35f;
    const float percentHoverOffsetAnimation = 0.075f;

    // this is the minimum offset necessary to not clip into the score scrollview (including all possible max animations
    // which can push us to the left, worst case)
    float minOffset = g_carousel->getSize().x * (percentCenterOffsetAnimation + percentHoverOffsetAnimation);
    {
        // also respect the width of the button image: push to the right until the edge of the button image can never be
        // visible even if all animations are fully active the 0.91f here heuristically pushes the buttons a bit further
        // to the right than would be necessary, to make animations work better on lower resolutions (would otherwise
        // hit the left edge too early)
        // NOTE @spec: this 0.91 and 0.09 relationship is extremely important for the actual offset amount somehow
        const float buttonWidthCompensation = std::max(g_carousel->getSize().x - this->getActualSize().x * 0.91f, 0.0f);
        minOffset += buttonWidthCompensation;
    }

    float offsetX =
        minOffset - g_carousel->getSize().x *
                        (percentCenterOffsetAnimation * this->fCenterOffsetAnimation *
                             (1.0f - this->fCenterOffsetVelocityAnimation) +
                         percentHoverOffsetAnimation * this->fHoverOffsetAnimation -
                         percentVelocityOffsetAnimation * this->fCenterOffsetVelocityAnimation + this->fOffsetPercent);
    offsetX = std::clamp<float>(
        offsetX, 0.0f,
        g_carousel->getSize().x -
            this->getActualSize().x * 0.09f);  // WARNING: hardcoded to match 0.91f above for buttonWidthCompensation

    this->setRelPosX(offsetX);
    this->setRelPosY(this->fTargetRelPosY + this->getSize().y * 0.125f * this->fHoverMoveAwayAnimation);
    this->setPos(g_carousel->container.getPos() + this->getRelPos());
}

CarouselButton *CarouselButton::setVisible(bool visible) {
    CBaseUIButton::setVisible(visible);

    this->deleteAnimations();

    if(this->bVisible) {
        // scrolling pinch effect
        this->fCenterOffsetAnimation = 1.0f;
        this->fHoverOffsetAnimation = 0.0f;

        float centerOffsetVelocityAnimationTarget =
            std::clamp<float>((std::abs(g_carousel->getVelocity().y)) / 3500.0f, 0.0f, 1.0f);

        if(g_carousel->isScrollingFast() || !cv::songbrowser_button_anim_x_push.getBool())
            centerOffsetVelocityAnimationTarget = 0.0f;

        this->fCenterOffsetVelocityAnimation = centerOffsetVelocityAnimationTarget;

        // force early layout update
        this->updateLayoutEx();
    }

    return this;
}

void CarouselButton::select(SelOpts opts) {
    const bool wasSelected = this->bSelected;
    this->bSelected = true;

    // callback
    if(!opts.noCallbacks) this->onSelected(wasSelected, opts);
}

void CarouselButton::deselect() { this->bSelected = false; }

void CarouselButton::resetAnimations() { this->setMoveAwayState(MOVE_AWAY_STATE::MOVE_CENTER, false); }

void CarouselButton::onClicked(bool left, bool right) {
    CBaseUIButton::onClicked(left, right);
    if(left) {
        soundEngine->play(osu->getSkin()->s_select_difficulty);

        this->select();
    }
}

void CarouselButton::onMouseInside() {
    CBaseUIButton::onMouseInside();

    // hover sound
    if(engine->getTime() > lastHoverSoundTime + 0.05f)  // to avoid earraep
    {
        if(env->winFocused()) soundEngine->play(osu->getSkin()->s_menu_hover);

        lastHoverSoundTime = engine->getTime();
    }

    // hover anim
    anim::moveQuadOut(&this->fHoverOffsetAnimation, 1.0f, 1.0f * (1.0f - this->fHoverOffsetAnimation), true);

    // all elements must be CarouselButtons, at least
    const auto &elements{g_carousel->container.getElements<CarouselButton>()};

    // move the rest of the buttons away from hovered-over one
    bool foundCenter = false;
    for(auto element : elements) {
        if(element == this) {
            foundCenter = true;
            element->setMoveAwayState(MOVE_AWAY_STATE::MOVE_CENTER);
        } else
            element->setMoveAwayState(foundCenter ? MOVE_AWAY_STATE::MOVE_DOWN : MOVE_AWAY_STATE::MOVE_UP);
    }
}

void CarouselButton::onMouseOutside() {
    CBaseUIButton::onMouseOutside();

    // reverse hover anim
    anim::moveQuadOut(&this->fHoverOffsetAnimation, 0.0f, 1.0f * this->fHoverOffsetAnimation, true);

    // only reset all other elements' state if we still should do so (possible frame delay of onMouseOutside coming
    // together with the next element already getting onMouseInside!)
    if(this->moveAwayState == MOVE_AWAY_STATE::MOVE_CENTER) {
        const auto &elements{g_carousel->container.getElements<CarouselButton>()};
        for(auto *element : elements) {
            element->setMoveAwayState(MOVE_AWAY_STATE::MOVE_CENTER);
        }
    }
}

void CarouselButton::setTargetRelPosY(float targetRelPosY) {
    this->fTargetRelPosY = targetRelPosY;
    this->setRelPosY(this->fTargetRelPosY);
}

void CarouselButton::setMoveAwayState(CarouselButton::MOVE_AWAY_STATE moveAwayState, bool animate) {
    this->moveAwayState = moveAwayState;

    // if we are not visible, destroy possibly existing animation
    if(this->bWasAnimationEverStarted && (!this->isVisible() || !animate)) {
        anim::deleteExistingAnimation(&this->fHoverMoveAwayAnimation);
        this->bWasAnimationEverStarted = false;
    }

    // only submit a new animation if we are visible, otherwise we would overwhelm the animationhandler with a shitload
    // of requests every time for every button (if we are not visible then we can just directly set the new value)
    switch(this->moveAwayState) {
        case MOVE_AWAY_STATE::MOVE_CENTER: {
            if(!this->isVisible() || !animate)
                this->fHoverMoveAwayAnimation = 0.0f;
            else {
                this->bWasAnimationEverStarted = true;
                anim::moveQuartOut(&this->fHoverMoveAwayAnimation, 0.f, 0.7f, this->isMouseInside() ? 0.0f : 0.05f,
                                   true);  // add a tiny bit of delay to avoid jerky movement if the cursor is briefly
                                           // between songbuttons while moving
            }
        } break;

        case MOVE_AWAY_STATE::MOVE_UP: {
            if(!this->isVisible() || !animate)
                this->fHoverMoveAwayAnimation = -1.0f;
            else {
                this->bWasAnimationEverStarted = true;
                anim::moveQuartOut(&this->fHoverMoveAwayAnimation, -1.0f, 0.7f, true);
            }
        } break;

        case MOVE_AWAY_STATE::MOVE_DOWN: {
            if(!this->isVisible() || !animate)
                this->fHoverMoveAwayAnimation = 1.0f;
            else {
                this->bWasAnimationEverStarted = true;
                anim::moveQuartOut(&this->fHoverMoveAwayAnimation, 1.0f, 0.7f, true);
            }
        } break;
    }
}

void CarouselButton::setChildren(std::vector<SongButton *> children) {
    this->lastChildSortStarPrecalcIdx = 0xFF;
    this->children = std::move(children);
}

void CarouselButton::addChild(SongButton *child) {
    this->lastChildSortStarPrecalcIdx = 0xFF;
    this->children.push_back(child);
}

void CarouselButton::addChildren(std::vector<SongButton *> children) {
    this->lastChildSortStarPrecalcIdx = 0xFF;
    Mc::append_range(this->children, std::move(children));
}

bool CarouselButton::childrenNeedSorting() const {
    return this->lastChildSortStarPrecalcIdx != StarPrecalc::active_idx;
}

Color CarouselButton::getActiveBackgroundColor() const {
    return argb(std::clamp<int>(cv::songbrowser_button_active_color_a.getInt(), 0, 255),
                std::clamp<int>(cv::songbrowser_button_active_color_r.getInt(), 0, 255),
                std::clamp<int>(cv::songbrowser_button_active_color_g.getInt(), 0, 255),
                std::clamp<int>(cv::songbrowser_button_active_color_b.getInt(), 0, 255));
}

Color CarouselButton::getInactiveBackgroundColor() const {
    return argb(std::clamp<int>(cv::songbrowser_button_inactive_color_a.getInt(), 0, 255),
                std::clamp<int>(cv::songbrowser_button_inactive_color_r.getInt(), 0, 255),
                std::clamp<int>(cv::songbrowser_button_inactive_color_g.getInt(), 0, 255),
                std::clamp<int>(cv::songbrowser_button_inactive_color_b.getInt(), 0, 255));
}
