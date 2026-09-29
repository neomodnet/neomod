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

#include <algorithm>
#include <utility>

Mouse::Mouse() : InputDevice() {
    this->fSensitivity = cv::mouse_sensitivity.getFloat();
    this->bIsRawInputDesired = cv::mouse_raw_input.getBool();
    cv::mouse_raw_input.setCallback(SA::MakeDelegate<&Mouse::onRawInputChanged>(this));
    cv::mouse_sensitivity.setCallback(SA::MakeDelegate<&Mouse::onSensitivityChanged>(this));
}

Mouse::~Mouse() {
    cv::mouse_raw_input.removeAllCallbacks();
    cv::mouse_sensitivity.removeAllCallbacks();
}

void Mouse::reset() {
    this->resetWheelDelta();
    this->buttonsHeldMask = env->getCurrentlyHeldMouseButtons();
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
    const vec2 realPos = this->getRealPos();
    g->drawRect(realPos.x - size / 2, realPos.y - size / 2, size, size);

    // green dot = asynchronous OS mouse pos
    g->setColor(rgb(0, 255, 0));
    vec2 truePos = env->getAsyncMousePos();
    g->fillRect(truePos.x - (size / 4) / 2, truePos.y - (size / 4) / 2, (size / 4), (size / 4));

    // red = cursor clip (the viewport)
    const McRect viewport = this->getAppViewport();
    if(env->isCursorClipped()) {
        g->setColor(0xffff0000);
        g->drawRect(viewport.getMinX(), viewport.getMinY(), viewport.getWidth() - 1, viewport.getHeight() - 1);
    }

    // green = app viewport
    g->setColor(0xff00ff00);
    g->drawRect(viewport.getMinX(), viewport.getMinY(), viewport.getWidth(), viewport.getHeight());
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
    const std::string posString = fmt::format("[{:.2f}, {:.2f}]", pos.x, pos.y);
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
    this->buttonsPressedMask = {};

    dvec2 newPos = this->vPosWithoutOffsets;
    // vRawDelta doesn't include sensitivity or clipping, which is useful for fposu
    dvec2 rawDelta{0.};

    // an absolute sample owns its frame: os motion alongside it is mostly the same pen again (macOS relative mode)
    const std::optional<dvec2> absolutePos = std::exchange(this->newAbsolutePos, std::nullopt);
    const auto [rel, abs, isRaw] = env->consumeCursorPositionCache();
    const bool hadMouseMotion = !absolutePos && vec::length(rel) > 0.;
    const bool hadRawMotion = isRaw && hadMouseMotion;
    if(absolutePos) {
        if(this->lastAbsolutePos) rawDelta = *absolutePos - *this->lastAbsolutePos;
        this->lastAbsolutePos = absolutePos;
        newPos = *absolutePos;
    } else if(hadMouseMotion) {
        this->lastAbsolutePos.reset();
        rawDelta = rel;
        if(isRaw) {
            // only relative input (raw) can have sensitivity
            newPos += rel * (double)this->fSensitivity;
        } else {
            newPos = abs;
        }
    }

    // the os confines its own cursor, but neither raw motion nor absolute pointers
    McRect clipRect;
    bool doClip = false;
    if((absolutePos || hadRawMotion) && this->isCursorConfined()) {
        clipRect = this->getAppViewport();
        doClip = true;
    } else if(hadRawMotion && env->winFullscreened() && !env->isPointValid(env->getWindowPos() + vec2{newPos})) {
        // quickfix to avoid flashing cursor along the edges of the window when unconfined + in raw input
        clipRect = engine->getScreenRect();
        doClip = true;
    }
    if(doClip) {
        newPos.x = std::clamp<double>(newPos.x, clipRect.getMinX(), clipRect.getMaxX());
        newPos.y = std::clamp<double>(newPos.y, clipRect.getMinY(), clipRect.getMaxY());
    }

    this->vRawDelta = rawDelta;
    if(newPos != this->vPosWithoutOffsets) this->applyPos(newPos);

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

    // the os cursor state this frame calls for, the only place it changes
    const McRect viewport = this->getAppViewport();
    env->applyCursorState({
        .confineRect = viewport,
        .pos = vec2{this->vPosWithoutOffsets},
        .visible =
            this->bOSCursorRequired || !this->bAppCursorHidden || !viewport.contains(vec2{this->vPosWithoutOffsets}),
        .confined = this->isCursorConfined(),
        .raw = this->bIsRawInputDesired || this->bRawInputOverride,
    });
}

void Mouse::resetWheelDelta() {
    this->iWheelDeltaVertical = this->iWheelDeltaVerticalActual;
    this->iWheelDeltaVerticalActual = 0;

    this->iWheelDeltaHorizontal = this->iWheelDeltaHorizontalActual;
    this->iWheelDeltaHorizontalActual = 0;
}

void Mouse::onPosChange(dvec2 pos) {
    this->newAbsolutePos = pos;
    this->applyPos(pos);
}

void Mouse::applyPos(dvec2 pos) {
    this->vPosWithoutOffsets = pos;
    this->vPos = vec2{pos} - this->appViewport.getMin();
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
        this->buttonsPressedMask |= ev.btn;
    } else {
        this->buttonsHeldMask &= ~ev.btn;
    }

    // notify listeners
    for(auto *listener : this->listeners) {
        listener->onButtonChange(ev);
    }
}

void Mouse::setPos(vec2 newPos) { this->applyPos(dvec2{newPos + this->appViewport.getMin()}); }

void Mouse::setAppViewport(const McRect &viewport) {
    this->appViewport = viewport;
    this->vPos = vec2{this->vPosWithoutOffsets} - viewport.getMin();
}

McRect Mouse::getAppViewport() const {
    return this->appViewport.getSize() == vec2{} ? engine->getScreenRect() : this->appViewport;
}

Mouse::RealPosScope::RealPosScope(Mouse *m_) : m(m_), bPrevious(m_->bRealPos) { m_->bRealPos = true; }

Mouse::RealPosScope::~RealPosScope() { m->bRealPos = this->bPrevious; }

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
