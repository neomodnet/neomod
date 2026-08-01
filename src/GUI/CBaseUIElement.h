#pragma once
// Copyright (c) 2013, PG, All rights reserved.

#include "KeyboardListener.h"
#include "Vectors.h"
#include "Rect.h"
#include "CBaseUIEventCtx.h"

#include <string>
#include <string_view>
#include <span>
#include <vector>

class CBaseUIElement;

// see CBaseUIDispatch.h
namespace CBaseUIDispatch {
struct State;
}

// convar callbacks to avoid hammering atomic convar reads
namespace CBaseUIDebug {
void onTraceChangeCallback(float newvalue);

// element name for debug output: sName if set, demangled type name otherwise
std::string elemName(const CBaseUIElement *elem);

// ui_trace level (0 = off), see the ui_trace convar
[[nodiscard]] int traceLevel();

// logs "uitrace evt=<evt> elem=<name>" for scripted-test golden diffing
void traceEvent(const CBaseUIElement *elem, std::string_view evt);

// helper to trace if traceLevel is > given debug_level
#define UI_TRACE_EVENT(debug_level, elem_to_trace, evt_txt) \
    if(unlikely(CBaseUIDebug::traceLevel() > (debug_level))) CBaseUIDebug::traceEvent((elem_to_trace), (evt_txt))
}  // namespace CBaseUIDebug

// Guidelines for avoiding hair pulling:
// - Don't use m_vmSize
// - When an element is standalone, use getPos/setPos
// - In a container or in a scrollview, use getRelPos/setRelPos and call update_pos() on the container

enum class TEXT_JUSTIFICATION : u8 { LEFT, CENTERED, RIGHT };

class CBaseUIContainer;

// ============================================================================================
//  Authoring a UI element (the part you actually touch)
// ============================================================================================
//  Most subclasses override draw() + tick() and one or two on*() handlers; the flags and the rest
//  of the virtuals are framework-managed. Recipes:
//
//   - Plain widget (button/label/icon): override draw(); handle a click in onMouseUpInside() (it
//     fires only if the cursor is still inside on release). Hover (onMouseInside/onMouseOutside)
//     is automatic - the dispatcher grants it to the single top-most element under the cursor.
//   - Pure container (no surface of its own): derive from CBaseUIContainer; it sets
//     bClickThroughSelf, so it is never a hit candidate itself, only its children are. Put a child
//     widget where you want a click rather than overriding the container's own onMouse*.
//   - Draws over a sibling visited later in the same screen (logo over the menu buttons, back
//     button over the body): set bDrawsOnTop. Otherwise the later sibling out-ranks you for hits.
//     Do not bump the hit tier by hand.
//   - Scrollable surface: derive from CBaseUIScrollView (sets bWheelSurface so it owns the wheel
//     over its area); drag / kinetic / scrollbar come for free via mouse capture.
//   - Keyboard: override onKeyDown/onKeyUp/onChar. To be THE keyboard target (textbox), call
//     requestFocus() on press and gate the handlers on isFocused() - the dispatcher keeps one
//     focused element across both UI roots.
//
//  Read but don't set: bMouseInside (hover), bActive (pressed/held while a button is down on us).
//  Do set: bVisible, bEnabled, and bBusy for "mid-gesture" (queried via isBusy()). The keyboard
//  target is the dispatcher focus pointer (requestFocus/isFocused), NOT bActive. How the walk
//  routes all of this is documented atop CBaseUIDispatch.h.
// ============================================================================================

class CBaseUIElement : public KeyboardListener {
    NOCOPY_NOMOVE(CBaseUIElement)
   public:
    CBaseUIElement(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, std::nullptr_t = {});
    CBaseUIElement(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, std::string name = {});
    ~CBaseUIElement() override;

    // main
    virtual void draw() = 0;

    // logic/animations/async polling; always runs, regardless of visibility or input consumption
    // NOTE: invisible CBaseUIContainers do NOT tick() their children, for performance reasons
    inline virtual void tick() {}

    // mouse input pass: hover + hit-candidate registration; gated and priority-ordered by the caller
    virtual void updateInput(CBaseUIEventCtx &c);

    // keyboard input (nothing by default)
    void onKeyUp(KeyboardEvent &e) override;
    void onKeyDown(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;

    // getters
    [[nodiscard]] std::string_view getName() const;

    [[nodiscard]] const McRect &getRect() const;

    [[nodiscard]] const vec2 &getPos() const;
    [[nodiscard]] const vec2 &getSize() const;

    [[nodiscard]] const McRect &getRelRect() const;

    [[nodiscard]] const vec2 &getRelPos() const;
    [[nodiscard]] const vec2 &getRelSize() const;

    virtual bool isActive();
    virtual bool isVisible();

    // engine rectangle contains rect
    static bool isVisibleOnScreen(const McRect &rect);
    [[nodiscard]] static bool isVisibleOnScreen(CBaseUIElement *elem);
    [[nodiscard]] bool isVisibleOnScreen() const;

    virtual bool isEnabled();
    virtual bool isBusy();
    virtual bool isMouseInside();

    CBaseUIElement *setPos(vec2 newPos);

    CBaseUIElement *setPos(float xPos, float yPos);
    CBaseUIElement *setPosX(float xPos);
    CBaseUIElement *setPosY(float yPos);
    CBaseUIElement *setRelPos(vec2 newRelPos);
    CBaseUIElement *setRelPos(float xPos, float yPos);
    CBaseUIElement *setRelPosX(float xPos);
    CBaseUIElement *setRelPosY(float yPos);

    CBaseUIElement *setSize(vec2 newSize);
    CBaseUIElement *setSize(float xSize, float ySize);
    CBaseUIElement *setSizeX(float xSize);
    CBaseUIElement *setSizeY(float ySize);

    CBaseUIElement *setRect(McRect rect);

    CBaseUIElement *setRelRect(McRect rect);

    virtual CBaseUIElement *setVisible(bool visible);
    virtual CBaseUIElement *setActive(bool active);
    virtual CBaseUIElement *setEnabled(bool enabled);
    virtual CBaseUIElement *setBusy(bool busy);
    virtual CBaseUIElement *setName(std::string name);
    virtual CBaseUIElement *setHandleLeftMouse(bool handle);
    virtual CBaseUIElement *setHandleRightMouse(bool handle);

    // declare that this element's (sub)tree draws above later-visited same-group siblings, so it
    // out-ranks them for hover/click/wheel hit-testing (the declarative form of raising the hit tier;
    // see bDrawsOnTop). dedicated widget subclasses set bDrawsOnTop in their ctor instead.
    CBaseUIElement *setDrawsOnTop(bool drawsOnTop);

    // actions
    // focus: requestFocus() makes this the single keyboard target across both roots
    // (relinquishing the previous holder); stealFocus() gives it up; isFocused() queries.
    // backed by the CBaseUIDispatch focus pointer.
    virtual void stealFocus();
    void requestFocus();
    bool isFocused();

    void dumpElem() const;  // debug
    [[nodiscard]] virtual std::span<CBaseUIElement *const> getAllChildren() const;

   protected:
    friend class CBaseUIContainer;
    friend CBaseUIDispatch::State;

    // events (default implementation does nothing for all of these)
    virtual void onResized();
    virtual void onMoved();

    virtual void onFocusStolen();
    virtual void onEnabled();
    virtual void onDisabled();

    virtual void onMouseInside();
    virtual void onMouseOutside();
    virtual void onMouseDownInside(bool /*left*/ = true, bool /*right*/ = false);
    virtual void onMouseDownOutside(bool /*left*/ = true, bool /*right*/ = false);
    virtual void onMouseUpInside(bool /*left*/ = true, bool /*right*/ = false);
    virtual void onMouseUpOutside(bool /*left*/ = true, bool /*right*/ = false);

    // wheel routing (dispatch-driven, see CBaseUIDispatch): per-frame wheel totals, offered
    // top-most-first to the hovered hit candidates; return true to consume (stops the
    // fall-through to elements beneath)
    virtual bool onWheel(int deltaVertical, int deltaHorizontal);

    // mouse capture lifecycle (dispatch-driven, see CBaseUIDispatch). a pressed element whose press is
    // taken away (capture steal, hidden/disabled/input-blocked mid-hold) gets onMouseCancel
    // instead of an up: discard pressed state, no click follows.
    virtual void onMouseCancel();
    // once per frame to the captor while it holds mouse capture (cursor position is polled)
    virtual void onCapturedMouseMove();
    // once per frame to each ancestor on the captor's hit path (innermost first) while a
    // descendant holds capture; the end hook fires when that capture ends or is stolen away
    virtual void onCapturedMoveThrough();
    virtual void onCapturedEndThrough();

    // capture control for self-dragging widgets (forward to CBaseUIDispatch): lockCapture makes the
    // current capture unstealable (slider grab, scrollbar drag); stealCapture takes a descendant's
    // unlocked capture (scrollview past drag resistance), cancelling the descendant's press
    void lockCapture();
    bool stealCapture();

    // vars
    std::string sName;

    // position and size
    McRect rect;
    McRect relRect;

    // vec2 &vPos;    // reference to rect.vMin
    // vec2 &vSize;   // reference to rect.vSize
    // vec2 &vmPos;   // reference to relRect.vMin
    // vec2 &vmSize;  // reference to relRect.vSize

    // attributes

   private:
    // stamped by every updateInput pass; an element is input-eligible for dispatch only if its
    // stamp is current (encodes all procedural screen gating without a declarative visibility model)
    u64 lastInputFrame{0};

   protected:
    // transparent wrappers (containers) don't hit-candidate their own rect; real widgets with a
    // self-rect surface (scrollview, window) opt back in
    bool bClickThroughSelf : 1 {false};
    // this (sub)tree draws above later-visited same-group siblings, so rank its hit candidacy above
    // them regardless of visit order (cube over the menu buttons, back button / context menu / user
    // card over the screen body beneath). applied as a balanced += around the walk, see updateInput.
    bool bDrawsOnTop : 1 {false};
    // scroll surfaces (scrollviews) floor the wheel scan: if the hit group of a hovered one
    // declines the frame's wheel, the layers beneath never see it - a wheel aimed at an opaque
    // surface must not act on occluded surfaces below (only the fall-through sink may take it).
    // small non-surface widgets (buttons, toasts, labels) stay wheel-transparent.
    bool bWheelSurface : 1 {false};
    bool bVisible : 1 {true};
    bool bActive : 1 {false};  // pressed/held: set while a mouse button is down on us, cleared on release
    bool bBusy : 1 {false};    // we demand the focus to be kept on us, e.g. click-drag scrolling in a scrollview
    bool bEnabled : 1 {true};

    bool bMouseInside : 1 {false};

    bool bHandleLeftMouse : 1 {true};
    bool bHandleRightMouse : 1 {false};
};
