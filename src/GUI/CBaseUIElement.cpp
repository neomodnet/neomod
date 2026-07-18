// Copyright (c) 2013, PG, All rights reserved.
#include "CBaseUIElement.h"

#include "Engine.h"
#include "Logging.h"
#include "ConVar.h"
#include "Mouse.h"
#include "CBaseUIDispatch.h"

#include "neotrace/neotrace.h"  // demangling

#include <utility>
#include <memory>
#include <typeinfo>

namespace CBaseUIDebug {
namespace {
bool dumpElems{false};
int traceLvl{0};
}  // namespace
void onDumpElemsChangeCallback(float newvalue) { dumpElems = !!static_cast<int>(newvalue); }
void onTraceChangeCallback(float newvalue) { traceLvl = static_cast<int>(newvalue); }

int traceLevel() { return traceLvl; }

void traceEvent(const CBaseUIElement *elem, std::string_view evt) {
    logRaw("uitrace evt={} elem={}", evt, elemName(elem));
}

std::string elemName(const CBaseUIElement *elem) {
    if(!elem) return "<null>";
    if(!elem->getName().empty()) return std::string{elem->getName()};

    return neotrace::demangle(typeid(*elem).name());
}
}  // namespace CBaseUIDebug

CBaseUIElement::CBaseUIElement(float xPos, float yPos, float xSize, float ySize, std::nullptr_t /**/)
    : rect(xPos, yPos, xSize, ySize), relRect(this->rect) {}

CBaseUIElement::CBaseUIElement(float xPos, float yPos, float xSize, float ySize, std::string name)
    : sName(std::move(name)), rect(xPos, yPos, xSize, ySize), relRect(this->rect) {}

CBaseUIElement::~CBaseUIElement() { CBaseUIDispatch::onElementDestroyed(this); }

// keyboard input
void CBaseUIElement::onKeyUp(KeyboardEvent &e) { (void)e; }
void CBaseUIElement::onKeyDown(KeyboardEvent &e) { (void)e; }
void CBaseUIElement::onChar(KeyboardEvent &e) { (void)e; }

// getters
std::string_view CBaseUIElement::getName() const { return this->sName; }

const McRect &CBaseUIElement::getRect() const { return this->rect; }

const vec2 &CBaseUIElement::getPos() const { return this->rect.getPos(); }
const vec2 &CBaseUIElement::getSize() const { return this->rect.getSize(); }

const McRect &CBaseUIElement::getRelRect() const { return this->relRect; }

const vec2 &CBaseUIElement::getRelPos() const { return this->relRect.getPos(); }
const vec2 &CBaseUIElement::getRelSize() const { return this->relRect.getSize(); }

bool CBaseUIElement::isActive() { return this->bActive || this->isBusy(); }
bool CBaseUIElement::isVisible() { return this->bVisible; }

// engine rectangle contains rect
bool CBaseUIElement::isVisibleOnScreen(const McRect &rect) { return engine->getScreenRect().intersects(rect); }
bool CBaseUIElement::isVisibleOnScreen(CBaseUIElement *elem) {
    return CBaseUIElement::isVisibleOnScreen(elem->getRect());
}
bool CBaseUIElement::isVisibleOnScreen() const { return CBaseUIElement::isVisibleOnScreen(this->getRect()); }

bool CBaseUIElement::isEnabled() { return this->bEnabled; }
bool CBaseUIElement::isBusy() { return this->bBusy && this->isVisible(); }
bool CBaseUIElement::isMouseInside() { return this->bMouseInside && this->isVisible(); }

CBaseUIElement *CBaseUIElement::setPos(vec2 newPos) {
    if(newPos != this->rect.getPos()) {
        this->rect.setPos(newPos);
        this->onMoved();
    }
    return this;
}
CBaseUIElement *CBaseUIElement::setPos(float xPos, float yPos) { return this->setPos({xPos, yPos}); }
CBaseUIElement *CBaseUIElement::setPosX(float xPos) { return this->setPos({xPos, this->rect.getPos().y}); }
CBaseUIElement *CBaseUIElement::setPosY(float yPos) { return this->setPos({this->rect.getPos().x, yPos}); }
CBaseUIElement *CBaseUIElement::setRelPos(vec2 newRelPos) {
    this->relRect.setPos(newRelPos);
    return this;
}
CBaseUIElement *CBaseUIElement::setRelPos(float xPos, float yPos) { return this->setRelPos({xPos, yPos}); }
CBaseUIElement *CBaseUIElement::setRelPosX(float xPos) {
    this->relRect.setPosX(xPos);
    return this;
}
CBaseUIElement *CBaseUIElement::setRelPosY(float yPos) {
    this->relRect.setPosY(yPos);
    return this;
}

CBaseUIElement *CBaseUIElement::setSize(vec2 newSize) {
    if(newSize != this->rect.getSize()) {
        this->rect.setSize(newSize);
        this->onResized();
        this->onMoved();
    }
    return this;
}
CBaseUIElement *CBaseUIElement::setSize(float xSize, float ySize) { return this->setSize({xSize, ySize}); }
CBaseUIElement *CBaseUIElement::setSizeX(float xSize) { return this->setSize({xSize, this->rect.getSize().y}); }
CBaseUIElement *CBaseUIElement::setSizeY(float ySize) { return this->setSize({this->rect.getSize().x, ySize}); }

CBaseUIElement *CBaseUIElement::setRect(McRect rect) {
    this->rect = std::move(rect);
    return this;
}

CBaseUIElement *CBaseUIElement::setRelRect(McRect rect) {
    this->relRect = std::move(rect);
    return this;
}

CBaseUIElement *CBaseUIElement::setVisible(bool visible) {
    this->bVisible = visible;
    return this;
}
CBaseUIElement *CBaseUIElement::setActive(bool active) {
    this->bActive = active;
    return this;
}
CBaseUIElement *CBaseUIElement::setEnabled(bool enabled) {
    if(enabled != this->bEnabled) {
        this->bEnabled = enabled;
        if(this->bEnabled) {
            this->onEnabled();
        } else {
            this->onDisabled();
        }
    }
    return this;
}
CBaseUIElement *CBaseUIElement::setBusy(bool busy) {
    this->bBusy = busy;
    return this;
}
CBaseUIElement *CBaseUIElement::setName(std::string name) {
    this->sName = std::move(name);
    return this;
}
CBaseUIElement *CBaseUIElement::setHandleLeftMouse(bool handle) {
    this->bHandleLeftMouse = handle;
    return this;
}
CBaseUIElement *CBaseUIElement::setHandleRightMouse(bool handle) {
    this->bHandleRightMouse = handle;
    return this;
}

CBaseUIElement *CBaseUIElement::setDrawsOnTop(bool drawsOnTop) {
    this->bDrawsOnTop = drawsOnTop;
    return this;
}

void CBaseUIElement::onResized() { ; }
void CBaseUIElement::onMoved() { ; }

void CBaseUIElement::onFocusStolen() { ; }
void CBaseUIElement::onEnabled() { ; }
void CBaseUIElement::onDisabled() { ; }

void CBaseUIElement::onMouseInside() { ; }
void CBaseUIElement::onMouseOutside() { ; }
void CBaseUIElement::onMouseDownInside(bool /*left*/, bool /*right*/) { ; }
void CBaseUIElement::onMouseDownOutside(bool /*left*/, bool /*right*/) { ; }
void CBaseUIElement::onMouseUpInside(bool /*left*/, bool /*right*/) { ; }
void CBaseUIElement::onMouseUpOutside(bool /*left*/, bool /*right*/) { ; }
bool CBaseUIElement::onWheel(int /*deltaVertical*/, int /*deltaHorizontal*/) { return false; }
void CBaseUIElement::onMouseCancel() { ; }
void CBaseUIElement::onCapturedMouseMove() { ; }
void CBaseUIElement::onCapturedMoveThrough() { ; }
void CBaseUIElement::onCapturedEndThrough() { ; }

void CBaseUIElement::lockCapture() { return CBaseUIDispatch::lockCapture(this); }
bool CBaseUIElement::stealCapture() { return CBaseUIDispatch::stealCapture(this); }

void CBaseUIElement::stealFocus() {
    this->bActive = false;
    CBaseUIDispatch::clearFocusIf(this);
    this->onFocusStolen();
}

void CBaseUIElement::requestFocus() { CBaseUIDispatch::setFocus(this); }

bool CBaseUIElement::isFocused() { return CBaseUIDispatch::getFocus() == this; }

void CBaseUIElement::tick() {
    if(unlikely(CBaseUIDebug::dumpElems)) this->dumpElem();
}

// pass A of the two-pass mouse model (pass B is CBaseUIDispatch::dispatchEvents): this walk does NOT
// deliver clicks. it registers hit candidates, resolves hover LOSS (gain is deferred to pass B) and
// broadcasts outside-downs. see the model overview atop CBaseUIDispatch.h.
void CBaseUIElement::updateInput(CBaseUIEventCtx &c) {
    if(!this->bVisible || !this->bEnabled) return;

    this->lastInputFrame = engine->getFrameCount();

    // raise the hit tier for the duration of this element's candidacy (and, via the container/
    // scrollview child walkers, its descendants') when it draws on top; balanced -= below so
    // later-visited siblings are unaffected
    c.currentHitTier += this->bDrawsOnTop;

    const bool oldMouseInsideState = this->bMouseInside;

    // rect membership with enter-strict / leave-loose hysteresis, recomputed FRESH every frame
    // (there is no stored occlusion bit that could get stuck): the single basis for both hit
    // candidacy and hover.
    const bool rectInside = oldMouseInsideState ? this->getRect().contains(mouse->getPos())
                                                : this->getRect().containsStrict(mouse->getPos());

    // hover. a hit candidate (!bClickThroughSelf) GAINS hover only in CBaseUIDispatch::resolveHover after
    // the whole walk: only the single top-most candidate (+ its ancestor path) gains, so an element
    // occluded by a higher one never briefly gains hover (and plays a hover sound). LOSS on
    // rect-leave stays local here. transparent wrappers/screens (bClickThroughSelf) are never
    // candidates, so they self-determine here as before; their isMouseInside() override aggregates
    // their children, whose hover the dispatcher resolves.
    if(!this->bClickThroughSelf) {
        if(oldMouseInsideState && !rectInside) this->bMouseInside = false;
    } else {
        // while a capture is held, nothing but the captor may GAIN hover (losing it stays allowed)
        const CBaseUIElement *captor = CBaseUIDispatch::getCaptor();
        if(!oldMouseInsideState) {
            if((captor == nullptr || captor == this) && rectInside) this->bMouseInside = true;
        } else if(!rectInside) {
            this->bMouseInside = false;
        }
    }

    // re-check to account for a possible isMouseInside() override (aggregate screens). for a plain
    // candidate widget isMouseInside() == bMouseInside, so this is a no-op and its gain comes from
    // the dispatcher.
    this->bMouseInside = this->isMouseInside();

    if(oldMouseInsideState != this->bMouseInside) {
        UI_TRACE_EVENT(0, this, this->bMouseInside ? "hoverIn" : "hoverOut");
        if(this->bMouseInside) {
            this->onMouseInside();
        } else {
            this->onMouseOutside();
        }
    }

    // hit candidacy: who MAY receive this frame's button/wheel events, and the set the dispatcher
    // ranks to pick the single hovered element. rect-based (NOT bMouseInside: the gain is deferred)
    // and NOT handle-gated (clicks filter on bHandleLeftMouse/Right, wheel on onWheel, at dispatch
    // time); only a transparent wrapper (bClickThroughSelf) opts out. gated on propagate_clicks so a
    // click-blocked element is not a candidate and the dispatcher never grants it hover.
    if(c.propagate_clicks && rectInside && !this->bClickThroughSelf) c.addHitCandidate(this);

    if(c.propagate_clicks && (this->bHandleLeftMouse || this->bHandleRightMouse)) {
        // outside-downs stay a per-element broadcast: the rect-based "pressed elsewhere" signal
        // (popup close-on-outside, contextmenu/textbox deactivation). KEPT deliberately - it is a
        // mouse concept, not keyboard focus: a context menu holding clickable items cannot be the
        // focus holder, so the broadcast is the right tool, not the focus pointer.
        // the captor is excluded; its events are routed in CBaseUIDispatch::dispatchEvents.
        const u8 pressedMask = (u8)((this->bHandleLeftMouse && mouse->isLeftPressed()) << 1) |
                               (u8)(this->bHandleRightMouse && mouse->isRightPressed());
        if(pressedMask && !rectInside && this != CBaseUIDispatch::getCaptor()) {
            UI_TRACE_EVENT(1, this, "downOutside");
            this->onMouseDownOutside((pressedMask & 0b10), (pressedMask & 0b01));
        }
    }

    c.currentHitTier -= this->bDrawsOnTop;
}

void CBaseUIElement::dumpElem() const {
    using namespace CBaseUIDebug;
    u64 currentFrame = engine->getFrameCount();
    logRaw(R"(==== UI ELEMENT {:p} DEBUG ====
frame:              {}
sName:              {}
bVisible:           {}
bActive:            {}
bBusy:              {}
bEnabled:           {}
bMouseInside:       {}
bHandleLeftMouse:   {}
bHandleRightMouse:  {}
rect:               {}
relRect:            {}
==== END UI ELEMENT DEBUG ====)",
           fmt::ptr(this), currentFrame, this->getName(), this->bVisible, this->bActive, this->bBusy, this->bEnabled,
           this->bMouseInside, this->bHandleLeftMouse, this->bHandleRightMouse, this->rect, this->relRect);
}

std::span<CBaseUIElement *const> CBaseUIElement::getAllChildren() const { return {}; }
