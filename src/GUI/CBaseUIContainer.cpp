// Copyright (c) 2011, PG, All rights reserved.
#include "CBaseUIContainer.h"

#include <utility>

#include "Engine.h"
#include "Logging.h"
#include "Graphics.h"
#include "ContainerRanges.h"

CBaseUIContainer::CBaseUIContainer(float Xpos, float Ypos, float Xsize, float Ysize, UString name)
    : CBaseUIElement(Xpos, Ypos, Xsize, Ysize, std::move(name)) {}

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

CBaseUIContainer *CBaseUIContainer::addBaseUIElements(const std::vector<CBaseUIElement *> &elements) {
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

CBaseUIElement *CBaseUIContainer::getBaseUIElement(const UString &name) {
    for(size_t i = 0; i < this->vElements.size(); i++) {
        if(this->vElements[i]->getName() == name) return this->vElements[i];
    }
    debugLog("CBaseUIContainer ERROR: GetBaseUIElement() \"{:s}\" does not exist!!!", name.toUtf8());
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
    g->drawLine(this->rect.getPos().x, this->rect.getPos().y, this->rect.getPos().x + this->getSize().x,
                this->rect.getPos().y);
    g->drawLine(this->rect.getPos().x, this->rect.getPos().y, this->rect.getPos().x,
                this->rect.getPos().y + this->getSize().y);
    g->drawLine(this->rect.getPos().x, this->rect.getPos().y + this->getSize().y,
                this->rect.getPos().x + this->getSize().x, this->rect.getPos().y + this->getSize().y);
    g->drawLine(this->rect.getPos().x + this->getSize().x, this->rect.getPos().y,
                this->rect.getPos().x + this->getSize().x, this->rect.getPos().y + this->getSize().y);

    g->setColor(0xff0000ff);
    for(const auto *e : this->vElements) {
        const auto ePos = e->rect.getPos();
        const auto eSize = e->getSize();
        g->drawLine(ePos.x, ePos.y, ePos.x + eSize.x, ePos.y);
        g->drawLine(ePos.x, ePos.y, ePos.x, ePos.y + eSize.y);
        g->drawLine(ePos.x, ePos.y + eSize.y, ePos.x + eSize.x, ePos.y + eSize.y);
        g->drawLine(ePos.x + eSize.x, ePos.y, ePos.x + eSize.x, ePos.y + eSize.y);
    }
}

void CBaseUIContainer::update(CBaseUIEventCtx &c) {
    CBaseUIElement::update(c);
    if(!this->isVisible()) return;

    // NOTE: do NOT use a range-based for loop here, update() might invalidate iterators by changing the container contents...
    const auto &elements = this->vElements;
    for(size_t i = 0; i < elements.size(); i++) {
        auto *e = elements[i];
        if(e->isVisible()) e->update(c);
    }
}

void CBaseUIContainer::update_pos() {
    if(!this->isVisible()) return;
    const vec2 thisPos = this->rect.getPos();

    vec2 eRelPos{};
    vec2 ePos{};
    vec2 newPos{};
    for(auto *e : this->vElements) {
        // setPos already has this logic, but inline it manually here
        // to avoid unnecessary indirection
        eRelPos = e->relRect.getPos();
        ePos = e->rect.getPos();
        if(std::abs(ePos.y - (newPos.y = (thisPos.y + eRelPos.y))) > 0.1f) {
            newPos.x = (thisPos.x + eRelPos.x);
            e->rect.setPos(newPos);
            e->onMoved();
        } else if(std::abs(ePos.x - (newPos.x = (thisPos.x + eRelPos.x))) > 0.1f) {
            e->rect.setPos(newPos);
            e->onMoved();
        }
    }
}

void CBaseUIContainer::onKeyUp(KeyboardEvent &e) {
    for(auto *elem : this->vElements) {
        if(elem->isVisible()) elem->onKeyUp(e);
    }
}
void CBaseUIContainer::onKeyDown(KeyboardEvent &e) {
    for(auto *elem : this->vElements) {
        if(elem->isVisible()) elem->onKeyDown(e);
    }
}

void CBaseUIContainer::onChar(KeyboardEvent &e) {
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
