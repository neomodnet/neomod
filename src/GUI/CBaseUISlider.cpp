// Copyright (c) 2012, PG, All rights reserved.
#include "CBaseUISlider.h"

#include <utility>

#include "AnimationHandler.h"
#include "Engine.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "App.h"
#include "SoundEngine.h"
#include "Graphics.h"

CBaseUISlider::CBaseUISlider(float xPos, float yPos, float xSize, float ySize, UString name)
    : CBaseUIElement(xPos, yPos, xSize, ySize, std::move(name)) {
    this->setGrabClicks(true);

    this->bDrawFrame = true;
    this->bDrawBackground = true;
    this->bHorizontal = false;
    this->bHasChanged = false;
    this->bAnimated = true;
    this->bLiveUpdate = false;
    this->bAllowMouseWheel = true;

    this->backgroundColor = argb(255, 0, 0, 0);
    this->frameColor = argb(255, 255, 255, 255);

    this->fCurValue = 0.0f;
    this->fPrevValue = 0.0f;
    this->fCurPercent = 0.0f;
    this->fMinValue = 0.0f;
    this->fMaxValue = 1.0f;
    this->fKeyDelta = 0.1f;

    this->vBlockSize = vec2(xSize < ySize ? xSize : ySize, xSize < ySize ? xSize : ySize);

    this->sliderChangeCallback = {};

    this->setOrientation(xSize > ySize);
}

void CBaseUISlider::draw() {
    if(!this->bVisible) return;

    // draw background
    if(this->bDrawBackground) {
        g->setColor(this->backgroundColor);
        g->fillRect(this->getPos(), vec2{this->getSize().x, this->getSize().y + 1.f});
    }

    // draw frame
    g->setColor(this->frameColor);
    if(this->bDrawFrame) g->drawRect(this->getPos(), vec2{this->getSize().x, this->getSize().y + 1.f});

    // draw sliding line
    if(!this->bHorizontal)
        g->drawLine(this->getPos().x + this->getSize().x / 2.0f, this->getPos().y + this->vBlockSize.y / 2.0,
                    this->getPos().x + this->getSize().x / 2.0f,
                    this->getPos().y + this->getSize().y - this->vBlockSize.y / 2.0f);
    else
        g->drawLine(this->getPos().x + (this->vBlockSize.x - 1) / 2 + 1,
                    this->getPos().y + this->getSize().y / 2.0f + 1,
                    this->getPos().x + this->getSize().x - (this->vBlockSize.x - 1) / 2,
                    this->getPos().y + this->getSize().y / 2.0f + 1);

    this->drawBlock();
}

void CBaseUISlider::drawBlock() {
    // draw block
    vec2 center =
        this->getPos() + vec2(this->vBlockSize.x / 2 + (this->getSize().x - this->vBlockSize.x) * this->getPercent(),
                              this->getSize().y / 2);
    vec2 topLeft = center - this->vBlockSize / 2.f;
    vec2 topRight = center + vec2(this->vBlockSize.x / 2 + 1, -this->vBlockSize.y / 2);
    vec2 halfLeft = center + vec2(-this->vBlockSize.x / 2, 1);
    vec2 halfRight = center + vec2(this->vBlockSize.x / 2 + 1, 1);
    vec2 bottomLeft = center + vec2(-this->vBlockSize.x / 2, this->vBlockSize.y / 2 + 1);
    vec2 bottomRight = center + vec2(this->vBlockSize.x / 2 + 1, this->vBlockSize.y / 2 + 1);

    g->drawQuad(topLeft, topRight, halfRight + vec2(0, 1), halfLeft + vec2(0, 1), argb(255, 255, 255, 255),
                argb(255, 255, 255, 255), argb(255, 241, 241, 241), argb(255, 241, 241, 241));

    g->drawQuad(halfLeft, halfRight, bottomRight, bottomLeft, argb(255, 225, 225, 225), argb(255, 225, 225, 225),
                argb(255, 255, 255, 255), argb(255, 255, 255, 255));
}

void CBaseUISlider::update(CBaseUIEventCtx &c) {
    CBaseUIElement::update(c);
    if(!this->bVisible) return;

    vec2 mousepos{mouse->getPos()};
    // ignore unmoving mouse
    const bool activeMouseMotion{this->bActive && (mousepos != this->vLastMousePos)};
    this->vLastMousePos = mousepos;

    // handle moving
    if(activeMouseMotion) {
        // calculate new values
        if(!this->bHorizontal) {
            if(this->bAnimated) {
                anim::moveQuadOut(
                    &this->vBlockPos.y,
                    std::clamp<float>(mousepos.y - this->vGrabBackup.y, 0.0f, this->getSize().y - this->vBlockSize.y),
                    0.10f, 0.0f, true);
            } else {
                this->vBlockPos.y =
                    std::clamp<float>(mousepos.y - this->vGrabBackup.y, 0.0f, this->getSize().y - this->vBlockSize.y);
            }

            this->fCurPercent = std::clamp<float>(
                1.0f - (std::round(this->vBlockPos.y) / (this->getSize().y - this->vBlockSize.y)), 0.0f, 1.0f);
        } else {
            if(this->bAnimated) {
                anim::moveQuadOut(
                    &this->vBlockPos.x,
                    std::clamp<float>(mousepos.x - this->vGrabBackup.x, 0.0f, this->getSize().x - this->vBlockSize.x),
                    0.10f, 0.0f, true);
            } else {
                this->vBlockPos.x =
                    std::clamp<float>(mousepos.x - this->vGrabBackup.x, 0.0f, this->getSize().x - this->vBlockSize.x);
            }

            this->fCurPercent =
                std::clamp<float>(std::round(this->vBlockPos.x) / (this->getSize().x - this->vBlockSize.x), 0.0f, 1.0f);
        }

        // set new value
        if(this->bAnimated) {
            if(this->bLiveUpdate) {
                this->setValue(std::lerp(this->fMinValue, this->fMaxValue, this->fCurPercent), false);
            } else {
                this->fCurValue = std::lerp(this->fMinValue, this->fMaxValue, this->fCurPercent);
            }
        } else {
            this->setValue(std::lerp(this->fMinValue, this->fMaxValue, this->fCurPercent), false);
        }

        this->bHasChanged = true;
    } else {
        // handle mouse wheel
        if(this->bAllowMouseWheel && this->isMouseInside()) {
            int wheelDelta = mouse->getWheelDeltaVertical();
            if(wheelDelta != 0) {
                const int multiplier = std::max(1, std::abs(wheelDelta) / 120);

                if(wheelDelta > 0) {
                    this->setValue(this->fCurValue + this->fKeyDelta * multiplier, this->bAnimated);
                } else {
                    this->setValue(this->fCurValue - this->fKeyDelta * multiplier, this->bAnimated);
                }
            }
        }
    }

    // handle animation value settings after mouse release
    if(!activeMouseMotion) {
        if(anim::isAnimating(&this->vBlockPos.x)) {
            this->fCurPercent =
                std::clamp<float>(std::round(this->vBlockPos.x) / (this->getSize().x - this->vBlockSize.x), 0.0f, 1.0f);

            if(this->bLiveUpdate)
                this->setValue(std::lerp(this->fMinValue, this->fMaxValue, this->fCurPercent), false);
            else
                this->fCurValue = std::lerp(this->fMinValue, this->fMaxValue, this->fCurPercent);
        }

        if(anim::isAnimating(&this->vBlockPos.y)) {
            this->fCurPercent = std::clamp<float>(
                1.0f - (std::round(this->vBlockPos.y) / (this->getSize().y - this->vBlockSize.y)), 0.0f, 1.0f);

            if(this->bLiveUpdate)
                this->setValue(std::lerp(this->fMinValue, this->fMaxValue, this->fCurPercent), false);
            else
                this->fCurValue = std::lerp(this->fMinValue, this->fMaxValue, this->fCurPercent);
        }
    }
}

void CBaseUISlider::onKeyDown(KeyboardEvent &e) {
    if(!this->bVisible) return;

    if(this->isMouseInside()) {
        if(e == KEY_LEFT) {
            this->setValue(this->getFloat() - this->fKeyDelta, false);
            e.consume();
        } else if(e == KEY_RIGHT) {
            this->setValue(this->getFloat() + this->fKeyDelta, false);
            e.consume();
        }
    }
}

void CBaseUISlider::fireChangeCallback() {
    if(this->sliderChangeCallback != nullptr) {
        this->sliderChangeCallback(this);
    }
}

void CBaseUISlider::updateBlockPos() {
    if(!this->bHorizontal)
        this->vBlockPos.x = this->getSize().x / 2.0f - this->vBlockSize.x / 2.0f;
    else
        this->vBlockPos.y = this->getSize().y / 2.0f - this->vBlockSize.y / 2.0f;
}

CBaseUISlider *CBaseUISlider::setBounds(float minValue, float maxValue) {
    this->fMinValue = minValue;
    this->fMaxValue = maxValue;

    this->fKeyDelta = (this->fMaxValue - this->fMinValue) / 10.0f;

    return this;
}

CBaseUISlider *CBaseUISlider::setValue(float value, bool animate, bool call_callback) {
    bool changeCallbackCheck{false};
    bool playAudio{false};

    float valueClamped{std::clamp<float>(value, this->fMinValue, this->fMaxValue)};
    if(valueClamped != this->fCurValue) {
        this->bHasChanged = true;
        this->fCurValue = valueClamped;

        playAudio = this->fLastSoundPlayTime + 0.05f < engine->getTime();
        changeCallbackCheck = true;
    }

    float percent = this->getPercent();

    if(!this->bHorizontal) {
        if(animate)
            anim::moveQuadOut(&this->vBlockPos.y, (this->getSize().y - this->vBlockSize.y) * (1.0f - percent), 0.2f,
                              0.0f, true);
        else
            this->vBlockPos.y = (this->getSize().y - this->vBlockSize.y) * (1.0f - percent);
    } else {
        if(animate)
            anim::moveQuadOut(&this->vBlockPos.x, (this->getSize().x - this->vBlockSize.x) * percent, 0.2f, 0.0f, true);
        else
            this->vBlockPos.x = (this->getSize().x - this->vBlockSize.x) * percent;
    }

    if(call_callback && changeCallbackCheck && this->sliderChangeCallback != nullptr) {
        const float audioPlayTimeBefore{this->fLastSoundPlayTime};

        this->fireChangeCallback();

        const float audioPlayTimeAfter{this->fLastSoundPlayTime};

        if(audioPlayTimeBefore != audioPlayTimeAfter) {
            // avoid duplicated audio playback (callback might setValue)
            playAudio = false;
        }
    }

    this->updateBlockPos();

    if(playAudio) {
        soundEngine->play(app->getSound(ActionSound::ADJUST_SLIDER), 0.f, 0.75f + (.075f * percent));
        this->fLastSoundPlayTime = engine->getTime();
    }

    return this;
}

CBaseUISlider *CBaseUISlider::setInitialValue(float value) {
    this->fCurValue = std::clamp<float>(value, this->fMinValue, this->fMaxValue);
    float percent = this->getPercent();

    if(this->fCurValue == this->fMaxValue) percent = 1.0f;

    if(!this->bHorizontal)
        this->vBlockPos.y = (this->getSize().y - this->vBlockSize.y) * (1.0f - percent);
    else
        this->vBlockPos.x = (this->getSize().x - this->vBlockSize.x) * percent;

    this->updateBlockPos();

    return this;
}

float CBaseUISlider::getPercent() {
    return std::clamp<float>((this->fCurValue - this->fMinValue) / (std::abs(this->fMaxValue - this->fMinValue)), 0.0f,
                             1.0f);
}

bool CBaseUISlider::hasChanged() {
    if(anim::isAnimating(&this->vBlockPos.x)) return true;
    if(this->bHasChanged) {
        this->bHasChanged = false;
        return true;
    }
    return false;
}

void CBaseUISlider::onFocusStolen() { this->bBusy = false; }

void CBaseUISlider::onMouseUpInside(bool /*left*/, bool /*right*/) {
    this->bBusy = false;

    if(this->fCurValue != this->fPrevValue && this->sliderChangeCallback != nullptr) this->fireChangeCallback();
}

void CBaseUISlider::onMouseUpOutside(bool /*left*/, bool /*right*/) {
    this->bBusy = false;

    if(this->fCurValue != this->fPrevValue && this->sliderChangeCallback != nullptr) this->fireChangeCallback();
}

void CBaseUISlider::onMouseDownInside(bool /*left*/, bool /*right*/) {
    this->fPrevValue = this->fCurValue;

    if(McRect(this->getPos().x + this->vBlockPos.x, this->getPos().y + this->vBlockPos.y, this->vBlockSize.x,
              this->vBlockSize.y)
           .contains(mouse->getPos()))
        this->vGrabBackup = mouse->getPos() - this->vBlockPos;
    else
        this->vGrabBackup = this->getPos() + this->vBlockSize / 2.f;

    this->bBusy = true;
}

void CBaseUISlider::onResized() { this->setValue(this->getFloat(), false); }
