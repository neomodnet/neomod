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

CBaseUISlider::CBaseUISlider(float xPos, float yPos, float xSize, float ySize, std::string name)
    : CBaseUIElement(xPos, yPos, xSize, ySize, std::move(name)) {
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

    const vec2 pos = this->getPos();
    const vec2 size = this->getSize();
    // draw background
    if(this->bDrawBackground) {
        g->setColor(this->backgroundColor);
        g->fillRect(pos, vec2{size.x, size.y + 1.f});
    }

    // draw frame
    g->setColor(this->frameColor);
    if(this->bDrawFrame) g->drawRect(pos, vec2{size.x, size.y + 1.f});

    // draw sliding line
    if(!this->bHorizontal) {
        this->drawSlidingLine(pos.x + size.x / 2.0f, pos.y + this->vBlockSize.y / 2.0f,  //
                              pos.x + size.x / 2.0f, pos.y + size.y - this->vBlockSize.y / 2.0f);
    } else {
        this->drawSlidingLine(pos.x + (this->vBlockSize.x - 1) / 2 + 1, pos.y + size.y / 2.0f + 1,  //
                              pos.x + size.x - (this->vBlockSize.x - 1) / 2, pos.y + size.y / 2.0f + 1);
    }
    this->drawBlock();
}

void CBaseUISlider::drawSlidingLine(float x1, float y1, float x2, float y2) {
    // outline: draw parallel copies of the line in outlineColor, offset perpendicular to it, behind the line itself.
    // a horizontal slider's line runs along x, so its outline grows along y, and vice versa.
    if(this->iLineOutlineSize > 0) {
        const Color lineColor = g->getColor();
        const float ox = this->bHorizontal ? 0.f : 1.f;
        const float oy = this->bHorizontal ? 1.f : 0.f;

        g->setColor(this->outlineColor);
        for(int i = 1; i <= this->iLineOutlineSize; i++) {
            const float off = (float)i;
            g->drawLine(x1 + ox * off, y1 + oy * off, x2 + ox * off, y2 + oy * off);
            g->drawLine(x1 - ox * off, y1 - oy * off, x2 - ox * off, y2 - oy * off);
        }
        g->setColor(lineColor);
    }
    g->drawLine(x1, y1, x2, y2);
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

bool CBaseUISlider::onWheel(WheelDelta vertical, WheelDelta /*horizontal*/) {
    // wheel during a drag would fight the grab (the captured move recomputes the value from
    // the cursor position every frame)
    if(!this->bAllowMouseWheel || this->bActive || (vertical.delta == 0 && vertical.ticks == 0)) return false;

    if(vertical.ticks != 0) {
        this->setValue(this->fCurValue + this->fKeyDelta * static_cast<float>(vertical.ticks), this->bAnimated);
    }
    return true;
}

void CBaseUISlider::onCapturedMouseMove() {
    const vec2 mousepos{mouse->getPos()};
    // ignore unmoving mouse
    if(mousepos == this->vLastMousePos) return;
    this->vLastMousePos = mousepos;

    // calculate new values
    if(!this->bHorizontal) {
        if(this->bAnimated) {
            this->vBlockPos.y.set(
                std::clamp<float>(mousepos.y - this->vGrabBackup.y, 0.0f, this->getSize().y - this->vBlockSize.y),
                0.10f, anim::QuadOut);
        } else {
            this->vBlockPos.y =
                std::clamp<float>(mousepos.y - this->vGrabBackup.y, 0.0f, this->getSize().y - this->vBlockSize.y);
        }

        this->fCurPercent = std::round(std::clamp<float>(1.0f - (std::round(this->vBlockPos.y) /
                                                                 (this->getSize().y - this->vBlockSize.y)),
                                                         0.0f, 1.0f) *
                                       1000.f) /
                            1000.f;
    } else {
        if(this->bAnimated) {
            this->vBlockPos.x.set(
                std::clamp<float>(mousepos.x - this->vGrabBackup.x, 0.0f, this->getSize().x - this->vBlockSize.x),
                0.10f, anim::QuadOut);
        } else {
            this->vBlockPos.x =
                std::clamp<float>(mousepos.x - this->vGrabBackup.x, 0.0f, this->getSize().x - this->vBlockSize.x);
        }

        this->fCurPercent =
            std::round(std::clamp<float>(std::round(this->vBlockPos.x) / (this->getSize().x - this->vBlockSize.x), 0.0f,
                                         1.0f) *
                       1000.f) /
            1000.f;
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
}

void CBaseUISlider::tick() {
    CBaseUIElement::tick();

    // handle animation value settings after mouse release
    // deliberately not gated on visibility: a hidden slider's block animation still settles
    // (lets e.g. ModSelector close mid-animation without updating-while-invisible)
    const bool activeMouseMotion{this->bActive && (vec2{mouse->getPos()} != this->vLastMousePos)};
    if(!activeMouseMotion) {
        if(this->vBlockPos.x.animating()) {
            this->fCurPercent =
                std::round(std::clamp<float>(std::round(this->vBlockPos.x) / (this->getSize().x - this->vBlockSize.x),
                                             0.0f, 1.0f) *
                           1000.f) /
                1000.f;

            if(this->bLiveUpdate)
                this->setValue(std::lerp(this->fMinValue, this->fMaxValue, this->fCurPercent), false);
            else
                this->fCurValue = std::lerp(this->fMinValue, this->fMaxValue, this->fCurPercent);
        }

        if(this->vBlockPos.y.animating()) {
            this->fCurPercent = std::round(std::clamp<float>(1.0f - (std::round(this->vBlockPos.y) /
                                                                     (this->getSize().y - this->vBlockSize.y)),
                                                             0.0f, 1.0f) *
                                           1000.f) /
                                1000.f;

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
            this->vBlockPos.y.set((this->getSize().y - this->vBlockSize.y) * (1.0f - percent), 0.2f, anim::QuadOut);
        else
            this->vBlockPos.y = (this->getSize().y - this->vBlockSize.y) * (1.0f - percent);
    } else {
        if(animate)
            this->vBlockPos.x.set((this->getSize().x - this->vBlockSize.x) * percent, 0.2f, anim::QuadOut);
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

float CBaseUISlider::getPercent() const {
    return std::clamp<float>((this->fCurValue - this->fMinValue) / (std::abs(this->fMaxValue - this->fMinValue)), 0.0f,
                             1.0f);
}

bool CBaseUISlider::hasChanged() {
    if(this->vBlockPos.x.animating()) return true;
    if(this->bHasChanged) {
        this->bHasChanged = false;
        return true;
    }
    return false;
}

void CBaseUISlider::onFocusStolen() { this->bBusy = false; }

void CBaseUISlider::onMouseCancel() { this->bBusy = false; }

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
        this->vGrabBackup = mouse->getPos() - this->blockPos();
    else
        this->vGrabBackup = this->getPos() + this->vBlockSize / 2.f;

    this->bBusy = true;

    // the grab is not stealable (an enclosing scrollview must not turn it into a drag-scroll);
    // the block follows on actual motion only, so prime the motion gate with the grab position
    this->vLastMousePos = mouse->getPos();
    this->lockCapture();
}

void CBaseUISlider::onResized() { this->setValue(this->getFloat(), false); }
