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
#include "CBaseUIDispatch.h"

#include <unordered_set>

// #include "ResourceManager.h"
// #include "Logging.h"

namespace {
using ScrollContainer = CBaseUIScrollView::CBaseUIScrollView::CBaseUIScrollViewContainer;
}  // namespace

ScrollContainer::CBaseUIScrollViewContainer(float Xpos, float Ypos, float Xsize, float Ysize, std::string name)
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
    return CBaseUIContainer::deleteBaseUIElement(element);
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

void ScrollContainer::tick() {
    // intentionally not calling parent: tick only the clipped-visible subset,
    // carousels hold thousands of elements
    CBaseUIElement::tick();

    // propagate container visibility to children
    if(!this->isVisible()) return;

    this->invalidateUpdate = false;

    for(auto *e : this->vVisibleElements) {
        e->tick();
        if(this->invalidateUpdate) {
            // iterators have been invalidated!
            // try again next time.
            break;
        }
    }

    this->invalidateUpdate = false;
}

void ScrollContainer::updateInput(CBaseUIEventCtx &c) {
    // intentionally not calling parent
    CBaseUIElement::updateInput(c);
    if(!this->isVisible()) return;

    this->invalidateUpdate = false;

    CBaseUIEventCtx::HitPathScope scope(c, this);
    for(auto *e : this->vVisibleElements) {
        e->updateInput(c);
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

CBaseUIScrollView::CBaseUIScrollView(f32 xPos, f32 yPos, f32 xSize, f32 ySize, std::string name)
    : CBaseUIElement(xPos, yPos, xSize, ySize, name), container(xPos, yPos, xSize, ySize, std::move(name)) {
    this->bWheelSurface = true;  // scroll surfaces floor the wheel scan (see CBaseUIDispatch)

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

    this->vKineticAverage.stop();

    this->vScrollSize.x = this->vScrollSize.y = 0;
    this->vScrollPos.stop();
    this->vVelocity.stop();
    this->vScrollPos = dvec2{};
    this->vVelocity = dvec2{};

    this->container.setPos(this->getPos());  // TODO: wtf is this doing here
}

void CBaseUIScrollView::draw() {
    if(!this->isVisible()) return;

    // really ugly place to put this, but this needs to happen if elements were changed after we have already tick()ed
    // otherwise, 1 frame of flicker (i.e. invisible elements) may occur until we update the next frame
    if(this->bClippingDirty && !(this->bScrolling || this->bScrollbarScrolling)) this->updateClipping();

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
        g->pushClipRect(this->getClipRect());
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

void CBaseUIScrollView::tick() {
    CBaseUIElement::tick();
    this->container.tick();

    // kinetic scrolling + rubber banding continue regardless of visibility or input gating;
    // while drag-scrolling, updateInput() follows the mouse instead
    if(!(this->bScrolling && this->bActive)) {
        this->vKineticAverage = dvec2{0., 0.};

        // rubber banding + kinetic scrolling

        // TODO: fix amount being dependent on fps due to double animation time indirection

        // y axis
        if(!this->bAutoScrollingY && this->bVerticalScrolling) {
            if(std::round(this->vScrollPos.y) > 1)  // rubber banding, top
            {
                // debugLog("y rubber banding, top");
                this->vVelocity.y.set(1.0, 0.05, anim::QuadOut);
                this->vScrollPos.y.set(this->vVelocity.y, 0.2, anim::QuadOut);
            } else if(std::round(std::abs(this->vScrollPos.y) + this->getSize().y) > this->vScrollSize.y &&
                      std::round(this->vScrollPos.y) < 1)  // rubber banding, bottom
            {
                // debugLog("y rubber banding, bottom");
                this->vVelocity.y.set((this->vScrollSize.y > this->getSize().y ? -this->vScrollSize.y : 1.0) +
                                          (this->vScrollSize.y > this->getSize().y ? this->getSize().y : 0),
                                      0.05, anim::QuadOut);

                this->vScrollPos.y.set(this->vVelocity.y, 0.2, anim::QuadOut);
            } else if(std::round(this->vVelocity.y) != 0 &&
                      std::round(this->vScrollPos.y) != std::round(this->vVelocity.y)) {  // kinetic scrolling
                // debugLog("y kinetic scrolling, velocity: {} scrollpos: {} thispos: {}", this->vVelocity.y, this->vScrollPos.y, this->getPos().y);
                this->vScrollPos.y.set(this->vVelocity.y, 0.35, anim::QuadOut);
            }
        }

        // x axis
        if(!this->bAutoScrollingX && this->bHorizontalScrolling) {
            if(std::round(this->vScrollPos.x) > 1)  // rubber banding, left
            {
                // debugLog("x rubber banding, left");
                this->vVelocity.x.set(1.0, 0.05, anim::QuadOut);
                this->vScrollPos.x.set(this->vVelocity.x, 0.2, anim::QuadOut);
            } else if(std::round(std::abs(this->vScrollPos.x) + this->getSize().x) > this->vScrollSize.x &&
                      std::round(this->vScrollPos.x) < 1)  // rubber banding, right
            {
                // debugLog("x rubber banding, right");
                this->vVelocity.x.set((this->vScrollSize.x > this->getSize().x ? -this->vScrollSize.x : 1.0) +
                                          (this->vScrollSize.x > this->getSize().x ? this->getSize().x : 0.0),
                                      0.05, anim::QuadOut);
                this->vScrollPos.x.set(this->vVelocity.x, 0.2, anim::QuadOut);
            } else if(std::round(this->vVelocity.x) != 0 &&
                      std::round(this->vScrollPos.x) != std::round(this->vVelocity.x)) {  // kinetic scrolling
                this->vScrollPos.x.set(this->vVelocity.x, 0.35, anim::QuadOut);
                // debugLog("x rubber banding, kinetic scrolling");
            }
        }
    }

    // position update during scrolling
    const dvec2 scrollPos{this->vScrollPos};
    const bool animating =
        vec::round(dvec2{this->getPos()} + scrollPos) != vec::round(dvec2{this->container.getPos()}) &&
        (this->vScrollPos.y.animating() || this->vScrollPos.x.animating());
    if(animating) {
        this->bClippingDirty = true;
        // debugLog("hit first condition, frame: {}", engine->getFrameCount());
        this->container.setPos(vec::round(dvec2{this->getPos()} + scrollPos));
    }

    // update scrollbars (anim-driven movement only; input-driven movement does its own
    // maintenance at the end of updateInput())
    if(animating) {
        this->bClippingDirty = true;
        // debugLog("hit second condition, frame: {}", engine->getFrameCount());
        this->updateScrollbars();
    }

    // HACKHACK: if an animation was started and ended before any setpos could get fired, manually update the position
    if(const dvec2 roundedPos = vec::round(dvec2{this->getPos()} + dvec2{this->vScrollPos});
       roundedPos != vec::round(dvec2{this->container.getPos()})) {
        this->container.setPos(roundedPos);
        this->bClippingDirty = true;
        // debugLog("hit third condition, frame: {}", engine->getFrameCount());
        this->updateScrollbars();
    }

    // while drag-/scrollbar-scrolling, onCapturedMouseMove() applies the move and rebuilds clipping
    // later this frame (in dispatch, after updateInput), don't redundantly update clipping here
    if(this->bClippingDirty && !(this->bScrolling || this->bScrollbarScrolling)) this->updateClipping();
}

void CBaseUIScrollView::updateInput(CBaseUIEventCtx &c) {
    if(!this->isVisible()) return;

    // self before children: visit order doubles as hit-candidate priority (latest = top-most),
    // and the children draw above the scrollview surface.
    CBaseUIElement::updateInput(c);
    // our content inherits our draws-on-top bias (e.g. UIContextMenu items out-rank the carousel)
    c.currentHitTier += this->bDrawsOnTop;
    const uSz firstChildCandidate = c.hitCandidates.size();
    {
        CBaseUIEventCtx::HitPathScope scope(c, this);
        this->container.updateInput(c);
    }
    c.currentHitTier -= this->bDrawsOnTop;

    // updateClipping()'s PVS slack keeps just-off elements in the walk so they don't pop in late;
    // they're clipped out of draw(), so clip them out of hit-testing too.
    if(!this->getClipRect().contains(mouse->getPos()))
        for(uSz i = firstChildCandidate; i < c.hitCandidates.size(); i++) c.hitCandidates[i].clipped = true;
}

bool CBaseUIScrollView::onWheel(int deltaVertical, int deltaHorizontal) {
    // alt-wheel belongs to the app-level volume gesture
    // HACK: this doesn't belong here???
    if(this->bBlockScrolling || keyboard->isAltDown()) return false;

    bool consumed = false;
    if(this->bVerticalScrolling && deltaVertical != 0 && this->getSize().y < this->vScrollSize.y) {
        this->scrollY(deltaVertical * this->fScrollMouseWheelMultiplier *
                      cv::ui_scrollview_mousewheel_multiplier.getDouble());
        consumed = true;
    }
    if(this->bHorizontalScrolling && deltaHorizontal != 0 && this->getSize().x < this->vScrollSize.x) {
        this->scrollX(-deltaHorizontal * this->fScrollMouseWheelMultiplier *
                      cv::ui_scrollview_mousewheel_multiplier.getDouble());
        consumed = true;
    }

    // deltas no enabled axis can take are DECLINED so the wheel chains to the scroll surface
    // beneath (ultimately to the volume sink): an anchored dropdown whose content fits
    // (UIContextMenu disables scrolling then) or a fits-content surface (empty carousel) must
    // not dead-end the gesture - the space gates mirror scrollY/scrollX's own no-op guards
    return consumed;
}

void CBaseUIScrollView::beginDragScroll(dvec2 pos) {
    this->bScrollbarIsVerticalScrolling = false;

    this->vMouseBackup = pos;
    this->vScrollPosBackup = dvec2{this->vScrollPos};
    this->bScrolling = true;
    this->bAutoScrollingX = false;
    this->bAutoScrollingY = false;

    this->vScrollPos.stop();
    this->vVelocity.stop();
}

bool CBaseUIScrollView::tryBeginScrollbarDrag(dvec2 pos) {
    if(this->verticalScrollbar.contains(pos)) {
        this->vMouseBackup.y = pos.y - this->verticalScrollbar.getMaxY();
        this->bScrollbarScrolling = true;
        this->bScrollbarIsVerticalScrolling = true;
        return true;
    }
    if(this->horizontalScrollbar.contains(pos)) {
        this->vMouseBackup.x = pos.x - this->horizontalScrollbar.getMaxX();
        this->bScrollbarScrolling = true;
        this->bScrollbarIsVerticalScrolling = false;
        return true;
    }
    return false;
}

void CBaseUIScrollView::endDragScroll(bool launchKinetic) {
    if(this->bScrolling || this->bScrollbarScrolling) {
        this->bScrolling = false;

        // calculate remaining kinetic energy
        if(launchKinetic && !this->bScrollbarScrolling) {
            const dvec2 delta{this->vKineticAverage};
            const dvec2 vel = cv::ui_scrollview_kinetic_energy_multiplier.getDouble() * delta *
                                  (engine->getFrameTime() != 0.0 ? 1.0 / engine->getFrameTime() : 60.0) / 60.0 +
                              dvec2{this->vScrollPos};
            this->vVelocity = vel;
        }

        // debugLog("kinetic = ({}), velocity = ({:}), frametime = {:f}", delta, this->vVelocity, engine->getFrameTime());

        this->bScrollbarScrolling = false;
    }
    this->bBusy = false;
}

void CBaseUIScrollView::onCapturedMouseMove() {
    if(!this->bActive) return;  // stealFocus() mid-drag freezes the gesture (cleared press state)

    const dvec2 curMousePos = mouse->getPos();

    // kinetic average tracks the drag motion (evaluated at release for the fling velocity)
    if(this->bBusy) {
        const dvec2 deltaToAdd = (curMousePos - this->vMouseBackup2);
        // debugLog("+ ({})", deltaToAdd);

        this->vKineticAverage.x.set(deltaToAdd.x, cv::ui_scrollview_kinetic_approach_time.getDouble(), anim::QuadOut);
        this->vKineticAverage.y.set(deltaToAdd.y, cv::ui_scrollview_kinetic_approach_time.getDouble(), anim::QuadOut);

        this->vMouseBackup2 = curMousePos;
    }

    // handle drag scrolling
    if(this->bScrolling) {
        if(this->bVerticalScrolling)
            this->vScrollPos.y = this->vScrollPosBackup.y + (curMousePos.y - this->vMouseBackup.y);
        if(this->bHorizontalScrolling)
            this->vScrollPos.x = this->vScrollPosBackup.x + (curMousePos.x - this->vMouseBackup.x);

        this->container.setPos(vec::round(dvec2{this->getPos()} + dvec2{this->vScrollPos}));
    }

    // handle scrollbar scrolling movement
    if(this->bScrollbarScrolling) {
        this->vVelocity.x = 0.;
        this->vVelocity.y = 0.;
        if(this->bScrollbarIsVerticalScrolling) {
            const f64 percent = std::clamp<f64>((curMousePos.y - this->getPos().y - this->verticalScrollbar.getWidth() -
                                                 this->verticalScrollbar.getHeight() - this->vMouseBackup.y - 1.0) /
                                                    (this->getSize().y - 2.0 * this->verticalScrollbar.getWidth()),
                                                0.0, 1.0);
            this->scrollToYInt(-this->vScrollSize.y * percent, true, false);
        } else {
            const f64 percent =
                std::clamp<f64>((curMousePos.x - this->getPos().x - this->horizontalScrollbar.getHeight() -
                                 this->horizontalScrollbar.getWidth() - this->vMouseBackup.x - 1.0) /
                                    (this->getSize().x - 2.0 * this->horizontalScrollbar.getHeight()),
                                0.0, 1.0);
            this->scrollToXInt(-this->vScrollSize.x * percent, true, false);
        }
    }

    // input-driven movement (drag-follow / scrollbar drag) must refresh scrollbars + clipping
    // here: tick() already ran this frame, so deferring to the next tick would draw this frame
    // with a clip list that is one frame of drag delta stale (elements scrolled into view would
    // pop in one frame late)
    if(this->bScrolling || this->bScrollbarScrolling) {
        this->bClippingDirty = true;
        this->updateScrollbars();
    }
    if(this->bClippingDirty) this->updateClipping();
}

void CBaseUIScrollView::onCapturedMoveThrough() {
    // the drag-scroll gesture is a left-button gesture
    if(!flags::has<MouseButtonFlags::MF_LEFT>(CBaseUIDispatch::getCaptorButtons())) return;

    const dvec2 curMousePos = mouse->getPos();

    if(!this->bBusy) {
        // first observed frame: a descendant took a press inside us; arm gesture tracking
        if(!this->bHandleLeftMouse || !this->bMouseInside || !this->isEnabled()) return;

        this->bBusy = true;
        this->vMouseBackup2 = curMousePos;  // to avoid spastic movement at scroll start
        this->vMouseBackup3 = curMousePos;  // resistance origin

        // a press on the scrollbar force-steals even from a child underneath it (locked
        // captures - slider grabs, textbox selections - decline the steal)
        if(!this->bBlockScrolling && (this->bVerticalScrolling || this->bHorizontalScrolling) &&
           (this->verticalScrollbar.contains(curMousePos) || this->horizontalScrollbar.contains(curMousePos))) {
            if(this->stealCapture()) this->tryBeginScrollbarDrag(curMousePos);
            return;
        }
    }

    // kinetic average keeps tracking the held child's motion (a steal mid-gesture must launch
    // with the full drag history at release)
    const dvec2 deltaToAdd = (curMousePos - this->vMouseBackup2);
    this->vKineticAverage.x.set(deltaToAdd.x, cv::ui_scrollview_kinetic_approach_time.getDouble(), anim::QuadOut);
    this->vKineticAverage.y.set(deltaToAdd.y, cv::ui_scrollview_kinetic_approach_time.getDouble(), anim::QuadOut);
    this->vMouseBackup2 = curMousePos;

    // past the pull resistance, take the capture away from the child and drag-scroll
    if(!this->bScrolling && !this->bBlockScrolling && (this->bVerticalScrolling || this->bHorizontalScrolling) &&
       this->isEnabled()) {
        int diff = std::abs(curMousePos.x - this->vMouseBackup3.x);
        if(std::abs(curMousePos.y - this->vMouseBackup3.y) > diff)
            diff = std::abs(curMousePos.y - this->vMouseBackup3.y);

        if(diff > this->iScrollResistance) {
            if(this->stealCapture()) this->beginDragScroll(curMousePos);
        }
    }
}

void CBaseUIScrollView::onCapturedEndThrough() {
    // the observed capture ended on the child (plain click or cancel): disarm gesture tracking
    this->bBusy = false;
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
    this->vVelocity.y.stop();
    if(animated) {
        this->vScrollPos.y.set(target, 0.15, anim::QuadOut);

        this->vVelocity.y = target;
    } else {
        this->vScrollPos.y.stop();
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
    if(animated) this->vVelocity.x = 0.;

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
        this->vScrollPos.x.set(target, 0.15, anim::QuadOut);
    else {
        const f64 remainingVelocity = this->vScrollPos.x - this->vVelocity.x;

        this->vScrollPos.x.stop();
        this->vScrollPos.x = this->vScrollPos.x + delta;
        this->vVelocity.x = this->vScrollPos.x - remainingVelocity;
    }
}

void CBaseUIScrollView::scrollToX(int scrollPosX, bool animated) { this->scrollToXInt(scrollPosX, animated); }

void CBaseUIScrollView::scrollToY(int scrollPosY, bool animated) { this->scrollToYInt(scrollPosY, animated); }

void CBaseUIScrollView::scrollToYInt(int scrollPosY, bool animated, bool slow) {
    if(!this->bVerticalScrolling || this->bScrolling) return;

    f64 upperBounds = 1;
    f64 lowerBounds = -this->vScrollSize.y + this->getSize().y;
    if(lowerBounds >= upperBounds) lowerBounds = upperBounds;

    const f64 targetY = std::clamp<f64>(scrollPosY, lowerBounds, upperBounds);

    // a scroll to where we already sit (and aren't animating away from) is a no-op: don't re-dirty
    // clipping. the songbrowser's left-edge auto-recenter calls this every frame, and a full O(n)
    // clip rebuild over thousands of carousel elements that changes nothing is pure waste
    if(!this->vScrollPos.y.animating() && std::round(this->vScrollPos.y) == std::round(targetY)) {
        // still pin the velocity to the resting position, a scrollbar drag zeroes vVelocity every
        // frame and relies on us to restore it
        this->vVelocity.y = targetY;
        return;
    }

    this->bClippingDirty = true;

    this->vVelocity.y.stop();
    this->vVelocity.y = targetY;

    if(animated) {
        this->bAutoScrollingY = true;
        this->vScrollPos.y.set(targetY, (slow ? 0.15 : 0.035), anim::QuadOut);
    } else {
        this->vScrollPos.y.stop();
        this->vScrollPos.y = targetY;
    }
}

void CBaseUIScrollView::scrollToXInt(int scrollPosX, bool animated, bool slow) {
    if(!this->bHorizontalScrolling || this->bScrolling) return;

    f64 upperBounds = 1;
    f64 lowerBounds = -this->vScrollSize.x + this->getSize().x;
    if(lowerBounds >= upperBounds) lowerBounds = upperBounds;

    const f64 targetX = std::clamp<f64>(scrollPosX, lowerBounds, upperBounds);

    // no-op scroll (already at target, not animating): don't re-dirty clipping (see scrollToYInt)
    if(!this->vScrollPos.x.animating() && std::round(this->vScrollPos.x) == std::round(targetX)) {
        this->vVelocity.x = targetX;  // keep velocity consistent on no-op (see scrollToYInt)
        return;
    }

    this->bClippingDirty = true;

    this->vVelocity.x = targetX;

    if(animated) {
        this->bAutoScrollingX = true;
        this->vScrollPos.x.set(targetX, (slow ? 0.15 : 0.035), anim::QuadOut);
    } else {
        this->vScrollPos.x.stop();
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
        const vec2 newSize = oldSize * 1.15f;
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
                }
            } else if(eVisible) {
                e->setVisible(false);
                nowVisible = e->isVisible();
            }

            if(nowVisible != eVisible) ++numChangedElements;

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

McRect CBaseUIScrollView::getClipRect() const {
    return {this->bHorizontalClipping ? this->getPos().x : 0, this->bVerticalClipping ? this->getPos().y : 0,
            this->bHorizontalClipping ? this->getSize().x : (f32)engine->getScreenWidth(),
            this->bVerticalClipping ? this->getSize().y : (f32)engine->getScreenHeight()};
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
    const dvec2 oldScrollPos{this->vScrollPos};
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
        const auto target = std::max(oldScrollPos.y, this->vScrollSize.y);
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

void CBaseUIScrollView::onMouseDownInside(bool left, bool /*right*/) {
    if(!left) return;

    // a press on our own surface (scrollbar or empty content space); children get their own
    // captures and we observe them through onCapturedMoveThrough instead
    this->bBusy = true;
    this->vMouseBackup2 = mouse->getPos();  // to avoid spastic movement at scroll start

    if(this->bBlockScrolling || (!this->bVerticalScrolling && !this->bHorizontalScrolling)) return;

    const dvec2 curMousePos = mouse->getPos();
    if(!this->tryBeginScrollbarDrag(curMousePos)) this->beginDragScroll(curMousePos);
    this->lockCapture();
}

void CBaseUIScrollView::onMouseUpInside(bool /*left*/, bool /*right*/) { this->endDragScroll(true); }

void CBaseUIScrollView::onMouseUpOutside(bool /*left*/, bool /*right*/) { this->endDragScroll(true); }

void CBaseUIScrollView::onMouseCancel() { this->endDragScroll(false); }

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

    this->container.setPos(dvec2{this->getPos()} + dvec2{this->vScrollPos});

    this->vMouseBackup2 = mouse->getPos();  // to avoid spastic movement after we are moved

    this->updateScrollbars();
}

bool CBaseUIScrollView::isBusy() {
    return (this->container.isBusy() || this->bScrolling || this->bBusy) && this->isVisible();
}
