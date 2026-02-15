// Copyright (c) 2013, PG, All rights reserved.
// TODO: refactor the spaghetti parts, this can be done way more elegantly

#include "CBaseUIScrollView.h"

#include "AnimationHandler.h"
#include "ConVar.h"
#include "Engine.h"
#include "Keyboard.h"
#include "Logging.h"
#include "Mouse.h"
#include "Graphics.h"

#include <unordered_set>

// #include "ResourceManager.h"
// #include "Logging.h"

namespace {
// UBER hack for "handling" mousewheel events on layered scrollviews
// much more proper would be to:
//  - actually track Z order globally and have a defined order of event handling
//  - stop doing the insane propagate_clicks thing (you can interact with gui elements in more ways than just clicking!!!)
// but that takes time and this seems to work in practice for now...
u64 lastScrollFrame{0};
u32 layeredScrollsHandledInFrame{0};

using ScrollContainer = CBaseUIScrollView::CBaseUIScrollView::CBaseUIScrollViewContainer;
}  // namespace

ScrollContainer::CBaseUIScrollViewContainer(float Xpos, float Ypos, float Xsize, float Ysize, UString name)
    : CBaseUIContainer(Xpos, Ypos, Xsize, Ysize, std::move(name)) {}

ScrollContainer::~CBaseUIScrollViewContainer() = default;

CBaseUIContainer *ScrollContainer::removeBaseUIElement(CBaseUIElement *element) {
    this->invalidateUpdate = true;
    std::erase(this->vVisibleElements, element);
    return CBaseUIContainer::removeBaseUIElement(element);
}

CBaseUIContainer *ScrollContainer::deleteBaseUIElement(CBaseUIElement *element) {
    this->invalidateUpdate = true;
    std::erase(this->vVisibleElements, element);
    return CBaseUIContainer::removeBaseUIElement(element);
}

void ScrollContainer::freeElements() {
    this->invalidateUpdate = true;

    this->vVisibleElements.clear();
    CBaseUIContainer::freeElements();
}

void ScrollContainer::invalidate() {
    this->invalidateUpdate = true;

    this->vVisibleElements.clear();
    CBaseUIContainer::invalidate();
}

void ScrollContainer::update(CBaseUIEventCtx &c) {
    // intentionally not calling parent
    CBaseUIElement::update(c);
    if(!this->isVisible()) return;

    this->invalidateUpdate = false;

    for(auto *e : this->vVisibleElements) {
        e->update(c);
        if(this->invalidateUpdate) {
            // iterators have been invalidated!
            // try again next time.
            break;
        }
    }

    this->invalidateUpdate = false;
}

void ScrollContainer::draw() {
    if(!this->isVisible()) return;

    this->invalidateUpdate = false;

    // if we were invalidated in the update() in this frame, clipping (elements to draw) will not have been updated
    // if there's a manageable amount of elements in the full vElements array, then just temporarily use the unoptimized
    // path of iterating over all of them to avoid 1 frame of flicker
    // otherwise draw only pre-clipped elements
    if(this->vVisibleElements.size() < 3 && this->vElements.size() < 1024) {
        for(auto *e : this->vElements) {
            if(e->isVisible() && e->isVisibleOnScreen()) {
                e->draw();
            }
            assert(!this->invalidateUpdate);
        }
    } else {
        for(auto *e : this->vVisibleElements) {
            // check actual screen visibility since we clipped "lazily"
            // shouldn't be too expensive since we're no longer iterating over hundreds of thousands of elements here
            if(e->isVisibleOnScreen()) {
                e->draw();
            }
            // programmer error (don't do this in draw(), ever)
            assert(!this->invalidateUpdate);
        }
    }
}

bool ScrollContainer::isBusy() {
    if(!this->isVisible()) return false;

    for(auto *e : this->vVisibleElements) {
        if(e->isBusy()) {
            return true;
        }
        if(this->invalidateUpdate) {
            return false;
        }
    }

    return false;
}

bool ScrollContainer::isActive() {
    if(!this->isVisible()) return false;

    for(auto *e : this->vVisibleElements) {
        if(e->isActive()) {
            return true;
        }
        if(this->invalidateUpdate) {
            return false;
        }
    }

    return false;
}

CBaseUIScrollView::CBaseUIScrollView(f32 xPos, f32 yPos, f32 xSize, f32 ySize, const UString &name)
    : CBaseUIElement(xPos, yPos, xSize, ySize, name), container(xPos, yPos, xSize, ySize, name) {
    this->setGrabClicks(true);

    this->iScrollResistance = cv::ui_scrollview_resistance.getInt();  // TODO: dpi handling
}

CBaseUIScrollView::~CBaseUIScrollView() { this->freeElements(); }

void CBaseUIScrollView::invalidate() {
    this->container.invalidate();
    this->bClippingDirty = true;
}

void CBaseUIScrollView::freeElements() {
    this->container.freeElements();

    this->bClippingDirty = true;

    anim::deleteExistingAnimation(&this->vKineticAverage.x);
    anim::deleteExistingAnimation(&this->vKineticAverage.y);

    anim::deleteExistingAnimation(&this->vScrollPos.y);
    anim::deleteExistingAnimation(&this->vScrollPos.x);

    anim::deleteExistingAnimation(&this->vVelocity.x);
    anim::deleteExistingAnimation(&this->vVelocity.y);

    this->vScrollSize.x = this->vScrollSize.y = 0;
    this->vScrollPos.x = this->vScrollPos.y = 0;
    this->vVelocity.x = this->vVelocity.y = 0;

    this->container.setPos(this->getPos());  // TODO: wtf is this doing here
}

void CBaseUIScrollView::draw() {
    if(!this->isVisible()) return;

    // draw background
    if(this->bDrawBackground) {
        g->setColor(this->backgroundColor);
        g->fillRect(this->getPos(), this->getSize());
    }

    // draw base frame
    if(this->bDrawFrame) {
        if(this->frameDarkColor != 0 || this->frameBrightColor != 0)
            g->drawRect(this->getPos(), this->getSize(), this->frameDarkColor, this->frameBrightColor,
                        this->frameBrightColor, this->frameDarkColor);
        else {
            g->setColor(this->frameColor);
            g->drawRect(this->getPos(), this->getSize());
        }
    }

    // draw elements & scrollbars
    if(this->bHorizontalClipping || this->bVerticalClipping) {
        auto clip_rect =
            McRect(this->bHorizontalClipping ? this->getPos().x : 0, this->bVerticalClipping ? this->getPos().y : 0,
                   this->bHorizontalClipping ? this->getSize().x : engine->getScreenWidth(),
                   this->bVerticalClipping ? this->getSize().y : engine->getScreenHeight());
        g->pushClipRect(clip_rect);
    }

    this->container.draw();

    if(this->bDrawScrollbars) {
        // vertical
        if(this->bVerticalScrolling && this->vScrollSize.y > this->getSize().y) {
            g->setColor(this->scrollbarColor);
            if(((this->bScrollbarScrolling && this->bScrollbarIsVerticalScrolling) ||
                this->verticalScrollbar.contains(mouse->getPos())) &&
               !this->bScrolling)
                g->setAlpha(1.0f);

            g->fillRect(this->verticalScrollbar.getX(), this->verticalScrollbar.getY(),
                        this->verticalScrollbar.getWidth(), this->verticalScrollbar.getHeight());
            // g->fillRoundedRect(this->verticalScrollbar.getX(), this->verticalScrollbar.getY(),
            // m_verticalScrollbar.getWidth(), this->verticalScrollbar.getHeight(),
            // this->verticalScrollbar.getWidth()/2);
        }

        // horizontal
        if(this->bHorizontalScrolling && this->vScrollSize.x > this->getSize().x) {
            g->setColor(this->scrollbarColor);
            if(((this->bScrollbarScrolling && !this->bScrollbarIsVerticalScrolling) ||
                this->horizontalScrollbar.contains(mouse->getPos())) &&
               !this->bScrolling)
                g->setAlpha(1.0f);

            g->fillRect(this->horizontalScrollbar.getX(), this->horizontalScrollbar.getY(),
                        this->horizontalScrollbar.getWidth(), this->horizontalScrollbar.getHeight());
            // g->fillRoundedRect(this->horizontalScrollbar.getX(), this->horizontalScrollbar.getY(),
            // m_horizontalScrollbar.getWidth(), this->horizontalScrollbar.getHeight(),
            // m_horizontalScrollbar.getHeight()/2);
        }
    }

    if(this->bHorizontalClipping || this->bVerticalClipping) {
        g->popClipRect();
    }
}

void CBaseUIScrollView::update(CBaseUIEventCtx &c) {
    if(!this->isVisible()) return;

    if(!c.propagate_clicks) {
        // big sigh...
        this->stealFocus();
    }

    const bool wasContainerBusyBeforeUpdate = this->container.isBusy();

    this->container.update(c);
    CBaseUIElement::update(c);

    const dvec2 curMousePos = mouse->getPos();

    if(this->bBusy) {
        const dvec2 deltaToAdd = (curMousePos - this->vMouseBackup2);
        // debugLog("+ ({})", deltaToAdd);

        anim::moveQuadOut(&this->vKineticAverage.x, deltaToAdd.x, cv::ui_scrollview_kinetic_approach_time.getDouble(),
                          true);
        anim::moveQuadOut(&this->vKineticAverage.y, deltaToAdd.y, cv::ui_scrollview_kinetic_approach_time.getDouble(),
                          true);

        this->vMouseBackup2 = curMousePos;
    }

    // scrolling logic
    if(this->bActive && !this->bBlockScrolling && (this->bVerticalScrolling || this->bHorizontalScrolling) &&
       this->isEnabled()) {
        if(!this->bScrollResistanceCheck) {
            this->bScrollResistanceCheck = true;
            this->vMouseBackup3 = curMousePos;
        }

        // get pull strength
        int diff = std::abs(curMousePos.x - this->vMouseBackup3.x);
        if(std::abs(curMousePos.y - this->vMouseBackup3.y) > diff)
            diff = std::abs(curMousePos.y - this->vMouseBackup3.y);

        // if we are above our resistance, try to steal the focus and enable scrolling for us
        if(this->container.isActive() && diff > this->iScrollResistance && !this->container.isBusy())
            this->container.stealFocus();

        // handle scrollbar scrolling start
        if(this->verticalScrollbar.contains(curMousePos) && !this->bScrollbarScrolling && !this->bScrolling) {
            // NOTE: scrollbar dragging always force steals focus
            if(!wasContainerBusyBeforeUpdate) {
                this->container.stealFocus();

                this->vMouseBackup.y = curMousePos.y - this->verticalScrollbar.getMaxY();
                this->bScrollbarScrolling = true;
                this->bScrollbarIsVerticalScrolling = true;
            }
        } else if(this->horizontalScrollbar.contains(curMousePos) && !this->bScrollbarScrolling && !this->bScrolling) {
            // NOTE: scrollbar dragging always force steals focus
            if(!wasContainerBusyBeforeUpdate) {
                this->container.stealFocus();

                this->vMouseBackup.x = curMousePos.x - this->horizontalScrollbar.getMaxX();
                this->bScrollbarScrolling = true;
                this->bScrollbarIsVerticalScrolling = false;
            }
        } else if(!this->bScrolling && !this->bScrollbarScrolling && !this->container.isBusy() &&
                  !this->container.isActive()) {
            if(!this->container.isBusy()) {
                // if we have successfully stolen the focus or the container is no longer busy, start scrolling
                this->bScrollbarIsVerticalScrolling = false;

                this->vMouseBackup = curMousePos;
                this->vScrollPosBackup = this->vScrollPos;
                this->bScrolling = true;
                this->bAutoScrollingX = false;
                this->bAutoScrollingY = false;

                anim::deleteExistingAnimation(&this->vScrollPos.x);
                anim::deleteExistingAnimation(&this->vScrollPos.y);

                anim::deleteExistingAnimation(&this->vVelocity.x);
                anim::deleteExistingAnimation(&this->vVelocity.y);
            }
        }
    } else if(this->bScrolling || this->bScrollbarScrolling)  // we were scrolling, stop it
    {
        this->bScrolling = false;
        this->bActive = false;

        const dvec2 delta = this->vKineticAverage;

        // calculate remaining kinetic energy
        if(!this->bScrollbarScrolling)
            this->vVelocity = cv::ui_scrollview_kinetic_energy_multiplier.getDouble() * delta *
                                  (engine->getFrameTime() != 0.0 ? 1.0 / engine->getFrameTime() : 60.0) / 60.0 +
                              this->vScrollPos;

        // debugLog("kinetic = ({}), velocity = ({:}), frametime = {:f}", delta, this->vVelocity, engine->getFrameTime());

        this->bScrollbarScrolling = false;
    } else
        this->bScrollResistanceCheck = false;

    // handle mouse wheel scrolling
    if(!this->bBlockScrolling && (this->bVerticalScrolling || this->bHorizontalScrolling) && !keyboard->isAltDown() &&
       this->isMouseInside() && this->isEnabled()) {
        const u64 curFrameCount = engine->getFrameCount();
        if(curFrameCount != lastScrollFrame) {
            lastScrollFrame = curFrameCount;
            layeredScrollsHandledInFrame = 0;
        }

        if(layeredScrollsHandledInFrame == 0) {
            bool handledThisFrame = false;
            if(this->bVerticalScrolling && mouse->getWheelDeltaVertical() != 0) {
                this->scrollY(mouse->getWheelDeltaVertical() * this->fScrollMouseWheelMultiplier *
                              cv::ui_scrollview_mousewheel_multiplier.getDouble());
                handledThisFrame = true;
            }

            if(this->bHorizontalScrolling && mouse->getWheelDeltaHorizontal() != 0) {
                this->scrollX(-mouse->getWheelDeltaHorizontal() * this->fScrollMouseWheelMultiplier *
                              cv::ui_scrollview_mousewheel_multiplier.getDouble());
                handledThisFrame = true;
            }

            if(handledThisFrame) {
                ++layeredScrollsHandledInFrame;
            }
        }
    }

    // handle drag scrolling and rubber banding
    if(this->bScrolling && this->bActive) {
        if(this->bVerticalScrolling)
            this->vScrollPos.y = this->vScrollPosBackup.y + (curMousePos.y - this->vMouseBackup.y);
        if(this->bHorizontalScrolling)
            this->vScrollPos.x = this->vScrollPosBackup.x + (curMousePos.x - this->vMouseBackup.x);

        this->container.setPos(vec::round(dvec2{this->getPos()} + this->vScrollPos));
    } else  // no longer scrolling, smooth the remaining velocity
    {
        this->vKineticAverage = {0., 0.};

        // rubber banding + kinetic scrolling

        // TODO: fix amount being dependent on fps due to double animation time indirection

        // y axis
        if(!this->bAutoScrollingY && this->bVerticalScrolling) {
            if(std::round(this->vScrollPos.y) > 1)  // rubber banding, top
            {
                // debugLog("y rubber banding, top");
                anim::moveQuadOut(&this->vVelocity.y, 1.0, 0.05, 0.0, true);
                anim::moveQuadOut(&this->vScrollPos.y, this->vVelocity.y, 0.2, 0.0, true);
            } else if(std::round(std::abs(this->vScrollPos.y) + this->getSize().y) > this->vScrollSize.y &&
                      std::round(this->vScrollPos.y) < 1)  // rubber banding, bottom
            {
                // debugLog("y rubber banding, bottom");
                anim::moveQuadOut(&this->vVelocity.y,
                                  (this->vScrollSize.y > this->getSize().y ? -this->vScrollSize.y : 1.0f) +
                                      (this->vScrollSize.y > this->getSize().y ? this->getSize().y : 0),
                                  0.05, 0.0, true);

                anim::moveQuadOut(&this->vScrollPos.y, this->vVelocity.y, 0.2, 0.0, true);
            } else if(std::round(this->vVelocity.y) != 0 &&
                      std::round(this->vScrollPos.y) != std::round(this->vVelocity.y)) {  // kinetic scrolling
                // debugLog("y kinetic scrolling, velocity: {} scrollpos: {} thispos: {}", this->vVelocity.y, this->vScrollPos.y, this->getPos().y);
                anim::moveQuadOut(&this->vScrollPos.y, this->vVelocity.y, 0.35, 0.0, true);
            }
        }

        // x axis
        if(!this->bAutoScrollingX && this->bHorizontalScrolling) {
            if(std::round(this->vScrollPos.x) > 1)  // rubber banding, left
            {
                // debugLog("x rubber banding, left");
                anim::moveQuadOut(&this->vVelocity.x, 1.0, 0.05, 0.0, true);
                anim::moveQuadOut(&this->vScrollPos.x, this->vVelocity.x, 0.2, 0.0, true);
            } else if(std::round(std::abs(this->vScrollPos.x) + this->getSize().x) > this->vScrollSize.x &&
                      std::round(this->vScrollPos.x) < 1)  // rubber banding, right
            {
                // debugLog("x rubber banding, right");
                anim::moveQuadOut(&this->vVelocity.x,
                                  (this->vScrollSize.x > this->getSize().x ? -this->vScrollSize.x : 1.0) +
                                      (this->vScrollSize.x > this->getSize().x ? this->getSize().x : 0.0),
                                  0.05, 0.0, true);
                anim::moveQuadOut(&this->vScrollPos.x, this->vVelocity.x, 0.2, 0.0, true);
            } else if(std::round(this->vVelocity.x) != 0 &&
                      std::round(this->vScrollPos.x) != std::round(this->vVelocity.x)) {  // kinetic scrolling
                anim::moveQuadOut(&this->vScrollPos.x, this->vVelocity.x, 0.35, 0.0, true);
                // debugLog("x rubber banding, kinetic scrolling");
            }
        }
    }

    // handle scrollbar scrolling movement
    if(this->bScrollbarScrolling) {
        this->vVelocity.x = this->vVelocity.y = 0;
        if(this->bScrollbarIsVerticalScrolling) {
            // debugLog("scrollbar scrolling movement vertical");
            const f64 percent = std::clamp<f64>((curMousePos.y - this->getPos().y - this->verticalScrollbar.getWidth() -
                                                 this->verticalScrollbar.getHeight() - this->vMouseBackup.y - 1.0) /
                                                    (this->getSize().y - 2.0 * this->verticalScrollbar.getWidth()),
                                                0.0, 1.0);
            this->scrollToYInt(-this->vScrollSize.y * percent, true, false);
        } else {
            // debugLog("scrollbar scrolling movement not vertical scrolling");

            const f64 percent =
                std::clamp<f64>((curMousePos.x - this->getPos().x - this->horizontalScrollbar.getHeight() -
                                 this->horizontalScrollbar.getWidth() - this->vMouseBackup.x - 1.0) /
                                    (this->getSize().x - 2.0 * this->horizontalScrollbar.getHeight()),
                                0.0, 1.0);
            this->scrollToXInt(-this->vScrollSize.x * percent, true, false);
        }
    }

    // position update during scrolling
    const bool animating =
        vec::round(dvec2{this->getPos()} + this->vScrollPos) != vec::round(dvec2{this->container.getPos()}) &&
        (anim::isAnimating(&this->vScrollPos.y) || anim::isAnimating(&this->vScrollPos.x));
    if(animating) {
        this->bClippingDirty = true;
        // debugLog("hit first condition, frame: {}", engine->getFrameCount());
        this->container.setPos(vec::round(dvec2{this->getPos()} + this->vScrollPos));
    }

    // update scrollbars
    if(animating || (this->bScrolling || this->bScrollbarScrolling)) {
        this->bClippingDirty = true;
        // debugLog("hit second condition, frame: {}", engine->getFrameCount());
        this->updateScrollbars();
    }

    // HACKHACK: if an animation was started and ended before any setpos could get fired, manually update the position
    if(const dvec2 roundedPos = vec::round(dvec2{this->getPos()} + this->vScrollPos);
       roundedPos != vec::round(dvec2{this->container.getPos()})) {
        this->container.setPos(roundedPos);
        this->bClippingDirty = true;
        // debugLog("hit third condition, frame: {}", engine->getFrameCount());
        this->updateScrollbars();
    }

    if(this->bClippingDirty) this->updateClipping();
}

void CBaseUIScrollView::onKeyUp(KeyboardEvent &e) { this->container.onKeyUp(e); }

void CBaseUIScrollView::onKeyDown(KeyboardEvent &e) { this->container.onKeyDown(e); }

void CBaseUIScrollView::onChar(KeyboardEvent &e) { this->container.onChar(e); }

void CBaseUIScrollView::scrollY(int delta, bool animated) {
    if(!this->bVerticalScrolling || delta == 0 || this->bScrolling || this->getSize().y >= this->vScrollSize.y ||
       this->container.isBusy())
        return;

    const bool allowOverscrollBounce = cv::ui_scrollview_mousewheel_overscrollbounce.getBool();

    // keep velocity (partially animated/finished scrolls should not get lost, especially multiple scroll() calls in
    // quick succession)
    const f64 remainingVelocity = this->vScrollPos.y - this->vVelocity.y;
    if(animated && this->bAutoScrollingY) delta -= remainingVelocity;

    // calculate new target
    f64 target = this->vScrollPos.y + delta;
    this->bAutoScrollingY = animated;

    // clamp target
    {
        if(target > 1) {
            if(!allowOverscrollBounce) target = 1;

            this->bAutoScrollingY = !allowOverscrollBounce;
        }

        if(std::abs(target) + this->getSize().y > this->vScrollSize.y) {
            if(!allowOverscrollBounce)
                target = (this->vScrollSize.y > this->getSize().y ? -this->vScrollSize.y : this->vScrollSize.y) +
                         (this->vScrollSize.y > this->getSize().y ? this->getSize().y : 0);

            this->bAutoScrollingY = !allowOverscrollBounce;
        }
    }

    // TODO: fix very slow autoscroll when 1 scroll event goes to >= top or >= bottom
    // TODO: fix overscroll dampening user action when direction flips (while rubber banding)

    // apply target
    anim::deleteExistingAnimation(&this->vVelocity.y);
    if(animated) {
        anim::moveQuadOut(&this->vScrollPos.y, target, 0.15, 0.0, true);

        this->vVelocity.y = target;
    } else {
        anim::deleteExistingAnimation(&this->vScrollPos.y);

        this->vScrollPos.y = target;
        this->vVelocity.y = this->vScrollPos.y - remainingVelocity;
    }
}

void CBaseUIScrollView::scrollX(int delta, bool animated) {
    if(!this->bHorizontalScrolling || delta == 0 || this->bScrolling || this->getSize().x >= this->vScrollSize.x ||
       this->container.isBusy())
        return;

    // TODO: fix all of this shit with the code from scrollY() above

    // stop any movement
    if(animated) this->vVelocity.x = 0;

    // keep velocity
    if(this->bAutoScrollingX && animated)
        delta += (delta > 0 ? (this->iPrevScrollDeltaX < 0 ? 0 : std::abs(delta - this->iPrevScrollDeltaX))
                            : (this->iPrevScrollDeltaX > 0 ? 0 : -std::abs(delta - this->iPrevScrollDeltaX)));

    // calculate target respecting the boundaries
    f64 target = this->vScrollPos.x + delta;
    if(target > 1) target = 1;
    if(std::abs(target) + this->getSize().x > this->vScrollSize.x)
        target = (this->vScrollSize.x > this->getSize().x ? -this->vScrollSize.x : this->vScrollSize.x) +
                 (this->vScrollSize.x > this->getSize().x ? this->getSize().x : 0);

    this->bAutoScrollingX = animated;
    this->iPrevScrollDeltaX = delta;

    if(animated)
        anim::moveQuadOut(&this->vScrollPos.x, target, 0.15, 0.0, true);
    else {
        const f64 remainingVelocity = this->vScrollPos.x - this->vVelocity.x;

        this->vScrollPos.x += delta;
        this->vVelocity.x = this->vScrollPos.x - remainingVelocity;

        anim::deleteExistingAnimation(&this->vScrollPos.x);
    }
}

void CBaseUIScrollView::scrollToX(int scrollPosX, bool animated) { this->scrollToXInt(scrollPosX, animated); }

void CBaseUIScrollView::scrollToY(int scrollPosY, bool animated) { this->scrollToYInt(scrollPosY, animated); }

void CBaseUIScrollView::scrollToYInt(int scrollPosY, bool animated, bool slow) {
    if(!this->bVerticalScrolling || this->bScrolling) return;
    this->bClippingDirty = true;

    f64 upperBounds = 1;
    f64 lowerBounds = -this->vScrollSize.y + this->getSize().y;
    if(lowerBounds >= upperBounds) lowerBounds = upperBounds;

    const f64 targetY = std::clamp<f64>(scrollPosY, lowerBounds, upperBounds);

    this->vVelocity.y = targetY;

    if(animated) {
        this->bAutoScrollingY = true;
        anim::moveQuadOut(&this->vScrollPos.y, targetY, (slow ? 0.15 : 0.035), 0.0, true);
    } else {
        anim::deleteExistingAnimation(&this->vVelocity.y);
        anim::deleteExistingAnimation(&this->vScrollPos.y);
        this->vScrollPos.y = targetY;
    }
}

void CBaseUIScrollView::scrollToXInt(int scrollPosX, bool animated, bool slow) {
    if(!this->bHorizontalScrolling || this->bScrolling) return;
    this->bClippingDirty = true;

    f64 upperBounds = 1;
    f64 lowerBounds = -this->vScrollSize.x + this->getSize().x;
    if(lowerBounds >= upperBounds) lowerBounds = upperBounds;

    const f64 targetX = std::clamp<f64>(scrollPosX, lowerBounds, upperBounds);

    this->vVelocity.x = targetX;

    if(animated) {
        this->bAutoScrollingX = true;
        anim::moveQuadOut(&this->vScrollPos.x, targetX, (slow ? 0.15 : 0.035), 0.0, true);
    } else {
        anim::deleteExistingAnimation(&this->vScrollPos.x);
        this->vScrollPos.x = targetX;
    }
}

void CBaseUIScrollView::scrollToElement(CBaseUIElement *element, int /*xOffset*/, int yOffset, bool animated) {
    const std::vector<CBaseUIElement *> &elements = this->container.getElements();
    if(const auto &elemit = std::ranges::find(elements, element); elemit != elements.end()) {
        this->scrollToY(-(*elemit)->getRelPos().y + yOffset, animated);
    }
}

void CBaseUIScrollView::updateClipping() {
    u32 numChangedElements = 0;
    //u32 numVisElements = 0;

    const std::vector<CBaseUIElement *> &allElements = this->container.getElements();
    std::vector<CBaseUIElement *> &visElements = this->container.vVisibleElements;

    // poor man's PVS
    // this makes us a bit less strict about which elements are "visible", but it shouldn't be too significant
    // just prevents cutting off of new elements before old elements have completely scrolled off screen
    // there could be a "smarter" way to handle this "slack" but this is good enough for a noticeable improvement
    McRect expandedMe = this->getRect();
    {
        const vec2 oldPos = expandedMe.getPos();
        const vec2 oldSize = expandedMe.getSize();
        const vec2 newSize = oldSize * 1.15;
        const vec2 newPos = oldPos - ((newSize - oldSize) / 2.f);
        expandedMe.setSize(newSize);
        expandedMe.setPos(newPos);
    }

    const uSz prevVisibleSize = this->previousClippingVisibleElements;
    const uSz prevTotalSize = this->previousClippingTotalElements;
    this->previousClippingTotalElements = allElements.size();

    const bool useCache = this->bVerticalScrolling &&                                              //
                          visElements.size() > 2 &&                                                // sanity checks
                          allElements.size() > 0 &&                                                //
                          prevVisibleSize > 0 &&                                                   //
                          prevVisibleSize == visElements.size() &&                                 //
                          prevTotalSize == allElements.size() &&                                   //
                          !(2.f * this->getSize().y >= -this->vScrollPos.y ||                      // overscroll, top
                            2.f * this->getSize().y >= this->vScrollPos.y + this->vScrollSize.y);  // overscroll, bottom
    // don't use cached clipping near top/bottom bounds because we might have set some elements as invisible without replacing them

    bool foundDifferent = !useCache;

    if(useCache) {
        for(auto *e : visElements) {
            const bool eVisible = e->isVisible();
            if(!eVisible || !expandedMe.intersects(e->getRect())) {
                foundDifferent = true;
                break;
            }
        }
    }

    if(foundDifferent) {
        bool eVisible{false}, nowVisible{false};
        visElements.clear();  // rebuild
        for(auto *e : allElements) {
            eVisible = nowVisible = e->isVisible();
            //numVisElements += eVisible;
            if(expandedMe.intersects(e->getRect())) {
                if(!eVisible) {
                    e->setVisible(true);
                    // need to call isVisible instead of just directly setting "true" because
                    // it may be overridden
                    nowVisible = e->isVisible();
                    ++numChangedElements;
                }
            } else if(eVisible) {
                e->setVisible(false);
                nowVisible = e->isVisible();
                ++numChangedElements;
            }

            if(nowVisible) {
                visElements.push_back(e);
            }
        }
    }

    if(!numChangedElements) {
        // sort final list of elements to draw (in order)
        if(this->container.drawOrderCmp) {
            std::ranges::sort(visElements, this->container.drawOrderCmp);
        }
        this->bClippingDirty = false;
    }

    this->previousClippingVisibleElements = visElements.size();
}

void CBaseUIScrollView::updateScrollbars() {
    // update vertical scrollbar
    if(this->bVerticalScrolling && this->vScrollSize.y > this->getSize().y) {
        const f64 verticalBlockWidth = cv::ui_scrollview_scrollbarwidth.getInt();

        const f64 rawVerticalPercent = (this->vScrollPos.y > 0 ? -this->vScrollPos.y : std::abs(this->vScrollPos.y)) /
                                       (this->vScrollSize.y - this->getSize().y);
        f64 overscroll = 1.0;
        if(rawVerticalPercent > 1.0)
            overscroll = 1.0 - (rawVerticalPercent - 1.0) * 0.95;
        else if(rawVerticalPercent < 0.0)
            overscroll = 1.0 - std::abs(rawVerticalPercent) * 0.95;

        const f64 verticalPercent = std::clamp<f64>(rawVerticalPercent, 0.0, 1.0);

        const f64 verticalHeightPercent = (this->getSize().y - (verticalBlockWidth * 2)) / this->vScrollSize.y;
        const f64 verticalBlockHeight =
            std::clamp<f64>(std::max(verticalHeightPercent * this->getSize().y, verticalBlockWidth) * overscroll,
                            verticalBlockWidth, std::max((f64)this->getSize().y, verticalBlockWidth));

        this->verticalScrollbar =
            McRect(this->getPos().x + this->getSize().x - (verticalBlockWidth * this->fScrollbarSizeMultiplier),
                   this->getPos().y +
                       (verticalPercent * (this->getSize().y - (verticalBlockWidth * 2.0) - verticalBlockHeight) +
                        verticalBlockWidth + 1.0),
                   (verticalBlockWidth * this->fScrollbarSizeMultiplier), verticalBlockHeight);
        if(this->bScrollbarOnLeft) {
            this->verticalScrollbar.setMinX(this->getPos().x);
            this->verticalScrollbar.setMaxX(verticalBlockWidth * this->fScrollbarSizeMultiplier);
        }
    }

    // update horizontal scrollbar
    if(this->bHorizontalScrolling && this->vScrollSize.x > this->getSize().x) {
        const f64 horizontalPercent =
            std::clamp<f64>((this->vScrollPos.x > 0 ? -this->vScrollPos.x : std::abs(this->vScrollPos.x)) /
                                (this->vScrollSize.x - this->getSize().x),
                            0.0, 1.0);
        const f64 horizontalBlockWidth = cv::ui_scrollview_scrollbarwidth.getInt();
        const f64 horizontalHeightPercent = (this->getSize().x - (horizontalBlockWidth * 2.0)) / this->vScrollSize.x;
        const f64 horizontalBlockHeight = std::max(horizontalHeightPercent * this->getSize().x, horizontalBlockWidth);

        this->horizontalScrollbar = McRect(
            this->getPos().x +
                (horizontalPercent * (this->getSize().x - (horizontalBlockWidth * 2.0) - horizontalBlockHeight) +
                 horizontalBlockWidth + 1.0),
            this->getPos().y + this->getSize().y - horizontalBlockWidth, horizontalBlockHeight, horizontalBlockWidth);
    }
}

CBaseUIScrollView *CBaseUIScrollView::setScrollSizeToContent(int border) {
    auto oldScrollPos = this->vScrollPos;
    const bool wasAtBottom = this->isAtBottom();

    this->vScrollSize = {0., 0.};

    const std::vector<CBaseUIElement *> &elements = this->container.getElements();
    for(auto *e : elements) {
        const f64 x = e->getRelPos().x + e->getSize().x;
        const f64 y = e->getRelPos().y + e->getSize().y;

        if(x > this->vScrollSize.x) this->vScrollSize.x = x;
        if(y > this->vScrollSize.y) this->vScrollSize.y = y;
    }

    this->vScrollSize.x += border;
    this->vScrollSize.y += border;

    this->container.setSize(this->vScrollSize);

    // TODO: duplicate code, ref onResized(), but can't call onResized() due to possible endless recursion if
    // setScrollSizeToContent() within onResized() HACKHACK: shit code
    if(this->bVerticalScrolling && this->vScrollSize.y < this->getSize().y && this->vScrollPos.y != 1)
        this->scrollToY(1);
    if(this->bHorizontalScrolling && this->vScrollSize.x < this->getSize().x && this->vScrollPos.x != 1)
        this->scrollToX(1);

    this->updateScrollbars();

    if(wasAtBottom && this->sticky && !this->bFirstScrollSizeToContent) {
        // Scroll to bottom without animation
        // XXX: Correct way to do this would be to keep the animation, but then you have to correct
        //      the existing scrolling animation, AND the possible scroll bounce animation.
        auto target = std::max(oldScrollPos.y, this->vScrollSize.y);
        this->scrollToY(-target, false);
    }

    this->bFirstScrollSizeToContent = false;
    this->bClippingDirty = true;
    return this;
}

void CBaseUIScrollView::scrollToLeft() { this->scrollToX(0); }

void CBaseUIScrollView::scrollToRight() { this->scrollToX(-this->vScrollSize.x); }

void CBaseUIScrollView::scrollToBottom() { this->scrollToY(-this->vScrollSize.y); }

void CBaseUIScrollView::scrollToTop() { this->scrollToY(0); }

void CBaseUIScrollView::onMouseDownOutside(bool /*left*/, bool /*right*/) { this->container.stealFocus(); }

void CBaseUIScrollView::onMouseDownInside(bool /*left*/, bool /*right*/) {
    this->bBusy = true;
    this->vMouseBackup2 = mouse->getPos();  // to avoid spastic movement at scroll start
}

void CBaseUIScrollView::onMouseUpInside(bool /*left*/, bool /*right*/) { this->bBusy = false; }

void CBaseUIScrollView::onMouseUpOutside(bool /*left*/, bool /*right*/) { this->bBusy = false; }

void CBaseUIScrollView::onFocusStolen() {
    this->bActive = false;
    this->bScrolling = false;
    this->bScrollbarScrolling = false;
    this->bBusy = false;

    // forward focus steal to container
    this->container.stealFocus();
}

void CBaseUIScrollView::onEnabled() {
    this->bClippingDirty = true;
    this->container.setEnabled(true);
}

void CBaseUIScrollView::onDisabled() {
    this->bActive = false;
    this->bScrolling = false;
    this->bScrollbarScrolling = false;
    this->bBusy = false;

    this->container.setEnabled(false);
}

void CBaseUIScrollView::onResized() {
    this->bClippingDirty = true;

    this->container.setSize(this->vScrollSize);

    // TODO: duplicate code
    // HACKHACK: shit code
    if(this->bVerticalScrolling && this->vScrollSize.y < this->getSize().y && this->vScrollPos.y != 1)
        this->scrollToY(1);
    if(this->bHorizontalScrolling && this->vScrollSize.x < this->getSize().x && this->vScrollPos.x != 1)
        this->scrollToX(1);

    this->updateScrollbars();
}

void CBaseUIScrollView::onMoved() {
    this->bClippingDirty = true;

    this->container.setPos(dvec2{this->getPos()} + this->vScrollPos);

    this->vMouseBackup2 = mouse->getPos();  // to avoid spastic movement after we are moved

    this->updateScrollbars();
}

bool CBaseUIScrollView::isBusy() {
    return (this->container.isBusy() || this->bScrolling || this->bBusy) && this->isVisible();
}
