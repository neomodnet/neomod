// Copyright (c) 2013, PG, All rights reserved.
#include "CBaseUIElement.h"

#include "Engine.h"
#include "Logging.h"
#include "ConVar.h"
#include "Mouse.h"

namespace CBaseUIDebug {
namespace {
bool dumpElems{false};
}
void onDumpElemsChangeCallback(float newvalue) { dumpElems = !!static_cast<int>(newvalue); }
}  // namespace CBaseUIDebug

bool CBaseUIElement::isVisibleOnScreen(const McRect &rect) { return engine->getScreenRect().intersects(rect); }

void CBaseUIElement::stealFocus() {
    this->mouseInsideCheck = (u8)(this->bHandleLeftMouse << 1) | (u8)this->bHandleRightMouse;
    this->bActive = false;
    this->onFocusStolen();
}

void CBaseUIElement::update(CBaseUIEventCtx &c) {
    if(unlikely(CBaseUIDebug::dumpElems)) this->dumpElem();
    if(!this->bVisible || !this->bEnabled) return;

    // TODO: hover "consumption"
    {
        const bool oldMouseInsideState = this->bMouseInside;

        // to avoid issues with mouse position right along the boundaries
        if(!oldMouseInsideState) {
            // going into strictly-contains area from outside
            if(this->getRect().containsStrict(mouse->getPos())) {
                this->bMouseInside = true;
            }
        } else {
            // leaving deadzone area from inside
            if(!this->getRect().contains(mouse->getPos())) {
                this->bMouseInside = false;
            }
        }

        // re-check to account for possible isMouseInside override
        if((this->bMouseInside = this->isMouseInside())) {
            c.propagate_hover = false;  // doesn't really do anything much atm
        }

        if(oldMouseInsideState != this->bMouseInside) {
            if(this->bMouseInside) {
                this->onMouseInside();
            } else {
                this->onMouseOutside();
            }
        }
    }

    const u8 buttonMask = (u8)((this->bHandleLeftMouse && mouse->isLeftDown()) << 1) |
                          (u8)(this->bHandleRightMouse && mouse->isRightDown());

    if(buttonMask && c.propagate_clicks) {
        this->mouseUpCheck |= buttonMask;
        if(this->bMouseInside) {
            c.propagate_clicks &= !this->grabs_clicks;
        }

        // onMouseDownOutside
        if(!this->bMouseInside && !(this->mouseInsideCheck & buttonMask)) {
            this->mouseInsideCheck |= buttonMask;
            this->onMouseDownOutside((buttonMask & 0b10), (buttonMask & 0b01));
        }

        // onMouseDownInside
        if(this->bMouseInside && !(this->mouseInsideCheck & buttonMask)) {
            this->bActive = true;
            this->mouseInsideCheck |= buttonMask;
            this->onMouseDownInside((buttonMask & 0b10), (buttonMask & 0b01));
        }
    }

    // detect which buttons were released for mouse up events
    const u8 releasedButtons = this->mouseUpCheck & ~buttonMask;
    if(releasedButtons && this->bActive) {
        if(this->bMouseInside)
            this->onMouseUpInside((releasedButtons & 0b10), (releasedButtons & 0b01));
        else
            this->onMouseUpOutside((releasedButtons & 0b10), (releasedButtons & 0b01));

        if(!this->bKeepActive) this->bActive = false;
    }

    // remove released buttons from mouseUpCheck
    this->mouseUpCheck &= buttonMask;

    // reset mouseInsideCheck if all buttons are released
    if(!buttonMask) {
        this->mouseInsideCheck = 0b00;
    }
}

void CBaseUIElement::dumpElem() const {
    using namespace CBaseUIDebug;
    size_t currentFrame = engine->getFrameCount();
    logRaw(R"(==== UI ELEMENT {:p} DEBUG ====
frame:              {}
sName:              {}
bVisible:           {}
bActive:            {}
bBusy:              {}
bEnabled:           {}
bKeepActive:        {}
bMouseInside:       {}
bHandleLeftMouse:   {}
bHandleRightMouse:  {}
rect:               {}
relRect:            {}
mouseInsideCheck:   {:02b}
mouseUpCheck:       {:02b}
==== END UI ELEMENT DEBUG ====)",
           fmt::ptr(this), currentFrame, this->getName(), this->bVisible, this->bActive, this->bBusy, this->bEnabled,
           this->bKeepActive, this->bMouseInside, this->bHandleLeftMouse, this->bHandleRightMouse, this->rect,
           this->relRect, this->mouseInsideCheck, this->mouseUpCheck);
}
