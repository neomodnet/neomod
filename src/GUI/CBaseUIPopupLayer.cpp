// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIPopupLayer.h"

#include "CBaseUIDispatch.h"
#include "Engine.h"
#include "KeyBindings.h"
#include "KeyboardEvent.h"
#include "Mouse.h"

#include <algorithm>
#include <utility>

CBaseUIPopupLayer::CBaseUIPopupLayer(std::string name) : CBaseUIContainer(0, 0, 0, 0, std::move(name)) {
    this->bVisible = false;
    // (its surface is a hit target of its own: presses beside the popups end there)
    this->bClickThroughSelf = false;
    this->bDrawsOnTop = true;
    this->setHandleRightMouse(true);
}

CBaseUIPopupLayer::~CBaseUIPopupLayer() {
    for(CBaseUIElement *popup : this->closed) delete popup;
}

u32 CBaseUIPopupLayer::open(CBaseUIElement *popup, vec2 pos, const McRect &passThrough) {
    const vec2 size = popup->getNaturalSize();
    popup->setSize(size);
    const vec2 min = this->getPos();
    const vec2 max = glm::max(min, this->getPos() + this->getSize() - size);
    this->addBaseUIElement(popup, glm::clamp(pos, min, max) - this->getPos());

    const u32 id = this->nextId++;
    this->popups.push_back({.id = id, .passThrough = passThrough});
    if(!this->isVisible()) {
        this->setVisible(true);
        this->requestFocus();
    }
    return id;
}

void CBaseUIPopupLayer::close(u32 id) {
    const auto it = std::ranges::find(this->popups, id, &Popup::id);
    if(it == this->popups.end()) return;

    const uSz from = it - this->popups.begin();
    for(uSz i = from; i < this->vElements.size(); i++) {
        this->vElements[i]->setVisible(false);
        this->closed.push_back(this->vElements[i]);
    }
    this->vElements.resize(from);
    this->popups.resize(from);
    if(this->popups.empty()) {
        this->setVisible(false);
        CBaseUIDispatch::clearFocusIf(this);
    }
}

void CBaseUIPopupLayer::closeAll() {
    if(!this->popups.empty()) this->close(this->popups.front().id);
}

bool CBaseUIPopupLayer::isOpen(u32 id) const { return std::ranges::contains(this->popups, id, &Popup::id); }

void CBaseUIPopupLayer::tick() {
    for(CBaseUIElement *popup : this->closed) delete popup;
    this->closed.clear();
    CBaseUIContainer::tick();
}

void CBaseUIPopupLayer::updateInput(CBaseUIEventCtx &c) {
    const vec2 cursor = mouse->getPos();
    this->bClickThroughSelf =
        std::ranges::any_of(this->popups, [&cursor](const Popup &p) { return p.passThrough.contains(cursor); });
    CBaseUIContainer::updateInput(c);
}

void CBaseUIPopupLayer::onKeyDown(KeyboardEvent &e) {
    if(!this->isOpen()) return;
    this->vElements.back()->onKeyDown(e);
    if(!e.isConsumed() && e == KEY_ESCAPE) this->close(this->popups.back().id);
    // (nothing beneath acts on keys while a popup is open)
    e.consume();
}

void CBaseUIPopupLayer::onKeyUp(KeyboardEvent &e) {
    if(this->isOpen()) this->vElements.back()->onKeyUp(e);
}

void CBaseUIPopupLayer::onChar(KeyboardEvent &e) {
    if(!this->isOpen()) return;
    this->vElements.back()->onChar(e);
    e.consume();
}

void CBaseUIPopupLayer::onMouseDownInside(bool /*left*/, bool /*right*/) { this->closeAll(); }

bool CBaseUIPopupLayer::onWheel(WheelDelta /*vertical*/, WheelDelta /*horizontal*/) { return true; }
