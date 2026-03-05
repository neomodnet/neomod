// Copyright (c) 2015, PG & 2025, WH, All rights reserved.
#include "Mouse.h"

#include "ConVar.h"
#include "Engine.h"
#include "Environment.h"
#include "MakeDelegateWrapper.h"
#include "ResourceManager.h"
#include "Logging.h"
#include "Graphics.h"
#include "Font.h"

Mouse::Mouse() : InputDevice(), vPos(env->getMousePos()), vPosWithoutOffsets(this->vPos) {
    this->fSensitivity = cv::mouse_sensitivity.getFloat();
    this->bIsRawInputDesired = cv::mouse_raw_input.getBool();
    cv::mouse_raw_input.setCallback(SA::MakeDelegate<&Mouse::onRawInputChanged>(this));
    cv::mouse_sensitivity.setCallback(SA::MakeDelegate<&Mouse::onSensitivityChanged>(this));
}

void Mouse::reset() {
    this->resetWheelDelta();
    this->buttonsHeldMask = {};
    this->vDelta = {0.f, 0.f};
    this->vRawDelta = {0.f, 0.f};
}

void Mouse::draw() {
    if(!cv::debug_mouse.getBool()) return;

    this->drawDebug();

    // green rect = virtual cursor pos
    g->setColor(0xff00ff00);
    float size = 20.0f;
    g->drawRect(this->vPos.x - size / 2, this->vPos.y - size / 2, size, size);

    // red rect = real cursor pos
    g->setColor(0xffff0000);
    vec2 envPos = env->getMousePos();
    g->drawRect(envPos.x - size / 2, envPos.y - size / 2, size, size);

    // green dot = asynchronous OS mouse pos
    g->setColor(rgb(0, 255, 0));
    vec2 truePos = env->getAsyncMousePos();
    g->fillRect(truePos.x - (size / 4) / 2, truePos.y - (size / 4) / 2, (size / 4), (size / 4));

    // red = cursor clip
    if(env->isCursorClipped()) {
        g->setColor(0xffff0000);
        McRect cursorClip = env->getCursorClip();
        g->drawRect(cursorClip.getMinX(), cursorClip.getMinY(), cursorClip.getWidth() - 1, cursorClip.getHeight() - 1);
    }

    // green = scaled & offset virtual area
    const vec2 scaledOffset = this->vOffset;
    const vec2 scaledEngineScreenSize = engine->getScreenSize() * this->vScale;
    g->setColor(0xff00ff00);
    g->drawRect(-scaledOffset.x, -scaledOffset.y, scaledEngineScreenSize.x, scaledEngineScreenSize.y);
}

void Mouse::drawDebug() {
    vec2 pos = this->getPos();

    g->setColor(0xff000000);
    g->drawLine(pos.x - 1, pos.y - 1, 0 - 1, pos.y - 1);
    g->drawLine(pos.x - 1, pos.y - 1, engine->getScreenWidth() - 1, pos.y - 1);
    g->drawLine(pos.x - 1, pos.y - 1, pos.x - 1, 0 - 1);
    g->drawLine(pos.x - 1, pos.y - 1, pos.x - 1, engine->getScreenHeight() - 1);

    g->setColor(0xffffffff);
    g->drawLine(pos.x, pos.y, 0, pos.y);
    g->drawLine(pos.x, pos.y, engine->getScreenWidth(), pos.y);
    g->drawLine(pos.x, pos.y, pos.x, 0);
    g->drawLine(pos.x, pos.y, pos.x, engine->getScreenHeight());

    float rectSizePercent = 0.05f;
    float aspectRatio = (float)engine->getScreenWidth() / (float)engine->getScreenHeight();
    vec2 rectSize = vec2(engine->getScreenWidth(), engine->getScreenHeight() * aspectRatio) * rectSizePercent;

    g->setColor(0xff000000);
    g->drawRect(pos.x - rectSize.x / 2.0f - 1, pos.y - rectSize.y / 2.0f - 1, rectSize.x, rectSize.y);

    g->setColor(0xffffffff);
    g->drawRect(pos.x - rectSize.x / 2.0f, pos.y - rectSize.y / 2.0f, rectSize.x, rectSize.y);

    McFont *posFont = engine->getDefaultFont();
    UString posString = fmt::format("[{:.2f}, {:.2f}]", pos.x, pos.y);
    float stringWidth = posFont->getStringWidth(posString);
    float stringHeight = posFont->getHeight();
    vec2 textOffset = vec2(
        pos.x + rectSize.x / 2.0f + stringWidth + 5 > engine->getScreenWidth() ? -rectSize.x / 2.0f - stringWidth - 5
                                                                               : rectSize.x / 2.0f + 5,
        (pos.y + rectSize.y / 2.0f + stringHeight > engine->getScreenHeight()) ? -rectSize.y / 2.0f - stringHeight
                                                                               : rectSize.y / 2.0f + stringHeight);

    g->pushTransform();
    g->translate(vec::round(pos + textOffset));
    g->drawString(posFont, posString);
    g->popTransform();
}

void Mouse::update() {
    this->vDelta = {0.f, 0.f};
    this->vRawDelta = {0.f, 0.f};

    auto [newRel, newAbs, pixelScale, needsClipping] = env->consumeCursorPositionCache();
    if(vec::length(newRel) <= 0.) goto out;  // early return for no motion

    // vRawDelta doesn't include sensitivity or clipping, which is useful for fposu
    this->vRawDelta = newRel;

    // correct SDL mouse events to match actual resolution
    // TODO: this is fishy
    newRel *= pixelScale;
    newAbs *= pixelScale;

    if(env->isOSMouseInputRaw()) {
        // only relative input (raw) can have sensitivity
        newRel *= this->fSensitivity;
        // we only base the absolute position off of the relative motion for raw input
        newAbs = this->vPosWithoutOffsets + newRel;
    }

    if(needsClipping) {
        // apply clipping manually for rawinput, because it only clips the absolute position
        // which is decoupled from the relative position in relative mode
        // do this after applying sensitivity (if applicable)
        McRect clipRect;
        bool doClip = false;
        if(env->isCursorClipped()) {
            clipRect = env->getCursorClip();
            doClip = true;
        } else if(env->winFullscreened() && !env->isPointValid(env->getWindowPos() + vec2{newAbs})) {
            // quickfix to avoid flashing cursor along the edges of the window when unconfined + in raw input
            clipRect = engine->getScreenRect();
            doClip = true;
        }
        if(doClip && !clipRect.contains(newAbs)) {
            // re-calculate clamped cursor position
            if(newAbs.x < clipRect.getMinX()) {
                newAbs.x = clipRect.getMinX();
            } else if(newAbs.x > clipRect.getMaxX()) {
                newAbs.x = clipRect.getMaxX();
            }
            if(newAbs.y < clipRect.getMinY()) {
                newAbs.y = clipRect.getMinY();
            } else if(newAbs.y > clipRect.getMaxY()) {
                newAbs.y = clipRect.getMaxY();
            }
            newRel = newAbs - this->vPosWithoutOffsets;
            if(vec::length(newRel) == 0.f) {
                goto out;  // early return for the trivial case (like if we're confined in a corner)
            }
        }
    }

    // if we got here, we have a motion delta to apply to the virtual cursor

    // vDelta includes transformations
    this->vDelta = newRel;

    // vPosWithoutOffsets should always match the post-transformation newAbs
    this->vPosWithoutOffsets = newAbs;

    this->onPosChange(this->vPosWithoutOffsets);

out:
    // relay collected button/wheel events to listeners (after updating position)
    for(auto &fullEvent : this->eventQueue) {
        switch(fullEvent.type) {
            case Type::BUTTON:
                this->onButtonChange_internal(fullEvent.orig);
                break;
            case Type::WHEELV:
                this->onWheelVertical_internal(fullEvent.wheelVDelta);
                break;
            case Type::WHEELH:
                this->onWheelHorizontal_internal(fullEvent.wheelHDelta);
                break;
        }
    }
    this->eventQueue.clear();

    this->resetWheelDelta();
}

void Mouse::resetWheelDelta() {
    this->iWheelDeltaVertical = this->iWheelDeltaVerticalActual;
    this->iWheelDeltaVerticalActual = 0;

    this->iWheelDeltaHorizontal = this->iWheelDeltaHorizontalActual;
    this->iWheelDeltaHorizontalActual = 0;
}

void Mouse::onPosChange(dvec2 pos) {
    this->vPosWithoutOffsets = pos;
    this->vPos = (dvec2{this->vOffset} + pos);

    // notify environment of the virtual cursor position
    env->updateCachedMousePos(this->vPosWithoutOffsets);
}

void Mouse::onWheelVertical(int delta) {
    this->eventQueue.emplace_back(FullEvent{.orig = {}, .wheelVDelta = delta, .wheelHDelta = {}, .type = Type::WHEELV});
}

void Mouse::onWheelHorizontal(int delta) {
    this->eventQueue.emplace_back(FullEvent{.orig = {}, .wheelVDelta = {}, .wheelHDelta = delta, .type = Type::WHEELH});
}

void Mouse::onButtonChange(ButtonEvent ev) {
    this->eventQueue.emplace_back(FullEvent{.orig = ev, .wheelVDelta = {}, .wheelHDelta = {}, .type = Type::BUTTON});
}

void Mouse::onWheelVertical_internal(int delta) {
    this->iWheelDeltaVerticalActual += delta;

    for(auto *listener : this->listeners) {
        listener->onWheelVertical(delta);
    }
}

void Mouse::onWheelHorizontal_internal(int delta) {
    this->iWheelDeltaHorizontalActual += delta;

    for(auto *listener : this->listeners) {
        listener->onWheelHorizontal(delta);
    }
}

void Mouse::onButtonChange_internal(ButtonEvent &ev) {
    using namespace flags::operators;

    if(!ev.btn || ev.btn >= MouseButtonFlags::MF_COUNT) return;

    if(ev.down) {
        this->buttonsHeldMask |= ev.btn;
    } else {
        this->buttonsHeldMask &= ~ev.btn;
    }

    // notify listeners
    for(auto *listener : this->listeners) {
        listener->onButtonChange(ev);
    }
}

void Mouse::setPos(vec2 newPos) { this->vPos = newPos; }

void Mouse::setOffset(vec2 offset) {
    vec2 oldOffset = this->vOffset;
    this->vOffset = offset;

    // update position to maintain visual position after offset change
    vec2 posAdjustment = this->vOffset - oldOffset;
    this->vPos += posAdjustment;
}

void Mouse::addListener(MouseListener *mouseListener, bool insertOnTop) {
    if(mouseListener == nullptr) {
        engine->showMessageError("Mouse Error", "addListener(NULL)!");
        return;
    }

    if(insertOnTop)
        this->listeners.insert(this->listeners.begin(), mouseListener);
    else
        this->listeners.push_back(mouseListener);
}

void Mouse::removeListener(MouseListener *mouseListener) { std::erase(this->listeners, mouseListener); }

void Mouse::onRawInputChanged(float newval) {
    this->bIsRawInputDesired = !!static_cast<int>(newval);
    env->setRawMouseInput(
        this->bIsRawInputDesired);  // request environment to change the real OS cursor state (may or may
                                    // not take effect immediately)

    // non-rawinput with sensitivity != 1 is unsupported
    if(!this->bIsRawInputDesired && (this->fSensitivity < 0.999f || this->fSensitivity > 1.001f)) {
        debugLog("forced sensitivity to 1.0 due to raw input being disabled");
        cv::mouse_sensitivity.setValue(1.0f);
    }
}

void Mouse::onSensitivityChanged(float newSens) {
    this->fSensitivity = newSens;

    // non-rawinput with sensitivity != 1 is unsupported
    if(!this->bIsRawInputDesired && (this->fSensitivity < 0.999f || this->fSensitivity > 1.001f)) {
        debugLog("forced raw input enabled due to sensitivity != 1.0");
        cv::mouse_raw_input.setValue(true);
    }
}
