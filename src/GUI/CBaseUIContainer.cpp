// Copyright (c) 2011, PG, All rights reserved.
#include "CBaseUIContainer.h"

#include <utility>

#include "Engine.h"
#include "Logging.h"
#include "Graphics.h"
#include "ContainerRanges.h"

CBaseUIContainer::CBaseUIContainer(float Xpos, float Ypos, float Xsize, float Ysize, std::string name)
    : CBaseUIElement(Xpos, Ypos, Xsize, Ysize, std::move(name)) {
    // a container is a transparent wrapper: clicks on its rect belong to whatever is beneath,
    // not to it (widget subclasses with a real self-surface opt back in, e.g. scrollview)
    this->bClickThroughSelf = true;
}

CBaseUIContainer::~CBaseUIContainer() { this->freeElements(); }

// free memory from children
void CBaseUIContainer::freeElements() {
    for(ssize_t i = static_cast<ssize_t>(this->vElements.size()) - 1; i >= 0; --i) {
        CBaseUIElement *toDelete = this->vElements[i];
        this->vElements.erase(this->vElements.begin() + i);
        delete toDelete;
    }
}

// invalidate children without freeing memory
void CBaseUIContainer::invalidate() { this->vElements.clear(); }

CBaseUIContainer *CBaseUIContainer::addBaseUIElement(CBaseUIElement *element, vec2 pos) {
    return this->addBaseUIElement(element, pos.x, pos.y);
}
CBaseUIContainer *CBaseUIContainer::addBaseUIElements(const std::vector<CBaseUIElement *> &elements) {
    return this->addBaseUIElements(std::span{elements.data(), elements.size()});
}
CBaseUIContainer *CBaseUIContainer::addBaseUIElementBack(CBaseUIElement *element, vec2 pos) {
    return this->addBaseUIElementBack(element, pos.x, pos.y);
}

CBaseUIContainer *CBaseUIContainer::addBaseUIElement(CBaseUIElement *element, float xPos, float yPos) {
    if(element == nullptr) return this;

    element->setRelPos(xPos, yPos);
    element->setPos(this->rect.getPos() + element->relRect.getPos());
    this->vElements.push_back(element);

    return this;
}

CBaseUIContainer *CBaseUIContainer::addBaseUIElement(CBaseUIElement *element) {
    if(element == nullptr) return this;

    element->relRect.setPos(element->rect.getPos());
    element->setPos(this->rect.getPos() + element->relRect.getPos());
    this->vElements.push_back(element);

    return this;
}

CBaseUIContainer *CBaseUIContainer::addBaseUIElements(std::span<CBaseUIElement *const> elements) {
    if(elements.empty()) return this;

    this->vElements.reserve(this->vElements.size() + elements.size());

    const vec2 thisPos = this->rect.getPos();

    for(auto *element : elements) {
        if(unlikely(element == nullptr)) continue;
        element->relRect.setPos(element->rect.getPos());
        element->setPos(thisPos + element->relRect.getPos());
        this->vElements.push_back(element);
    }

    return this;
}

CBaseUIContainer *CBaseUIContainer::addBaseUIElementBack(CBaseUIElement *element, float xPos, float yPos) {
    if(element == nullptr) return this;

    element->setRelPos(xPos, yPos);
    element->setPos(this->rect.getPos() + element->relRect.getPos());
    this->vElements.insert(this->vElements.begin(), element);

    return this;
}

CBaseUIContainer *CBaseUIContainer::addBaseUIElementBack(CBaseUIElement *element) {
    if(element == nullptr) return this;

    element->relRect.setPos(element->rect.getPos());
    element->setPos(this->rect.getPos() + element->relRect.getPos());
    this->vElements.insert(this->vElements.begin(), element);

    return this;
}

CBaseUIContainer *CBaseUIContainer::insertBaseUIElement(CBaseUIElement *element, CBaseUIElement *index) {
    if(element == nullptr || index == nullptr) return this;

    element->relRect.setPos(element->rect.getPos());
    element->setPos(this->rect.getPos() + element->relRect.getPos());
    for(ssize_t i = 0; i < this->vElements.size(); i++) {
        if(this->vElements[i] == index) {
            this->vElements.insert(
                this->vElements.begin() + std::clamp<ssize_t>(i, 0, static_cast<ssize_t>(this->vElements.size())),
                element);
            return this;
        }
    }

    debugLog("Warning: CBaseUIContainer::insertBaseUIElement() couldn't find index");

    return this;
}

CBaseUIContainer *CBaseUIContainer::insertBaseUIElementBack(CBaseUIElement *element, CBaseUIElement *index) {
    if(element == nullptr || index == nullptr) return this;

    element->relRect.setPos(element->rect.getPos());
    element->setPos(this->rect.getPos() + element->relRect.getPos());
    for(ssize_t i = 0; i < this->vElements.size(); i++) {
        if(this->vElements[i] == index) {
            this->vElements.insert(
                this->vElements.begin() + std::clamp<ssize_t>(i + 1, 0, static_cast<ssize_t>(this->vElements.size())),
                element);
            return this;
        }
    }

    debugLog("Warning: CBaseUIContainer::insertBaseUIElementBack() couldn't find index");

    return this;
}

CBaseUIContainer *CBaseUIContainer::removeBaseUIElement(CBaseUIElement *element) {
    for(ssize_t i = 0; i < this->vElements.size(); i++) {
        if(this->vElements[i] == element) {
            this->vElements.erase(this->vElements.begin() + i);
            return this;
        }
    }

    debugLog("Warning: CBaseUIContainer::removeBaseUIElement() couldn't find element");

    return this;
}

CBaseUIContainer *CBaseUIContainer::deleteBaseUIElement(CBaseUIElement *element) {
    for(ssize_t i = 0; i < this->vElements.size(); i++) {
        if(this->vElements[i] == element) {
            delete element;
            this->vElements.erase(this->vElements.begin() + i);
            return this;
        }
    }

    debugLog("Warning: CBaseUIContainer::deleteBaseUIElement() couldn't find element");

    return this;
}

CBaseUIElement *CBaseUIContainer::getBaseUIElement(std::string_view name) {
    for(size_t i = 0; i < this->vElements.size(); i++) {
        if(this->vElements[i]->getName() == name) return this->vElements[i];
    }
    debugLog("CBaseUIContainer ERROR: GetBaseUIElement() \"{:s}\" does not exist!!!", name);
    return nullptr;
}

void CBaseUIContainer::draw() {
    if(!this->isVisible()) return;

    for(auto *e : this->vElements) {
        if(e->isVisible()) {
            e->draw();
        }
    }
}

void CBaseUIContainer::draw_debug() {
    g->setColor(0xffffffff);
    g->drawRect(this->getRect());

    if(this->isMouseInside()) {
        g->setColor(0x44333377);
        g->fillRect(this->getRect());
    }

    g->setColor(0xff0000ff);
    for(auto *e : this->vElements) {
        g->drawRect(e->getRect());
        if(e->isMouseInside()) {
            g->setColor(0x55995555);
            g->fillRect(e->getRect());
        }
    }
}

void CBaseUIContainer::tick() {
    CBaseUIElement::tick();

    // NOTE: do NOT use a range-based for loop here, tick() might invalidate iterators by changing the container contents...
    const auto &elements = this->vElements;
    for(size_t i = 0; i < elements.size(); i++) {
        elements[i]->tick();
    }
}

void CBaseUIContainer::updateInput(CBaseUIEventCtx &c) {
    CBaseUIElement::updateInput(c);
    if(!this->isVisible()) return;

    // descendants inherit our draws-on-top bias (CBaseUIElement::updateInput already applied it to
    // our own candidacy, but balanced it back before returning here)
    c.currentHitTier += this->bDrawsOnTop;
    {
        CBaseUIEventCtx::HitPathScope scope(c, this);

        // NOTE: do NOT use a range-based for loop here, updateInput() might invalidate iterators by changing the container contents...
        const auto &elements = this->vElements;
        for(size_t i = 0; i < elements.size(); i++) {
            auto *e = elements[i];
            if(e->isVisible()) e->updateInput(c);
        }
    }
    c.currentHitTier -= this->bDrawsOnTop;
}

void CBaseUIContainer::update_pos() {
    if(!this->isVisible()) return;
    const vec2 thisPos = this->rect.getPos();

    MC_UNR_cnt(32) for(auto *e : this->vElements) {
        const vec2 newPos{thisPos + e->getRelPos()};
        if(std::abs(newPos.x - e->getPos().x) > 0.1f || std::abs(newPos.y - e->getPos().y) > 0.1f) {
            e->rect.setPos(newPos);
            e->onMoved();
        }
    }
}

void CBaseUIContainer::onKeyUp(KeyboardEvent &e) {
    if(!this->isVisible()) return;

    for(auto *elem : this->vElements) {
        if(elem->isVisible()) elem->onKeyUp(e);
    }
}
void CBaseUIContainer::onKeyDown(KeyboardEvent &e) {
    if(!this->isVisible()) return;

    for(auto *elem : this->vElements) {
        if(elem->isVisible()) elem->onKeyDown(e);
    }
}

void CBaseUIContainer::onChar(KeyboardEvent &e) {
    if(!this->isVisible()) return;

    for(auto *elem : this->vElements) {
        if(elem->isVisible()) elem->onChar(e);
    }
}

void CBaseUIContainer::onFocusStolen() {
    for(auto *elem : this->vElements) {
        elem->stealFocus();
    }
}

void CBaseUIContainer::onEnabled() {
    for(auto *elem : this->vElements) {
        elem->setEnabled(true);
    }
}

void CBaseUIContainer::onDisabled() {
    for(auto *elem : this->vElements) {
        elem->setEnabled(false);
    }
}

void CBaseUIContainer::onMouseDownOutside(bool /*left*/, bool /*right*/) { this->onFocusStolen(); }

bool CBaseUIContainer::isBusy() {
    if(!this->isVisible()) return false;

    for(auto *elem : this->vElements) {
        if(elem->isBusy()) return true;
    }

    return false;
}

bool CBaseUIContainer::isActive() {
    if(!this->isVisible()) return false;

    for(auto *elem : this->vElements) {
        if(elem->isActive()) return true;
    }

    return false;
}

std::span<CBaseUIElement *const> CBaseUIContainer::getAllChildren() const {
    return {this->vElements.data(), this->vElements.size()};
}
