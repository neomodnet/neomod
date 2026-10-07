#pragma once
// Copyright (c) 2026, WH, All rights reserved.

#include "MouseListener.h"
#include "types.h"

// ============================================================================================
//  CBaseUI input model (read this first)
// ============================================================================================
//  Two ROOTS dispatch every frame in a fixed order: the engine gui root (guiContainer = console/
//  debug, drawn on top) then the app UI root. They share one consumed-state buffer, so the engine
//  root gets first claim on each event and the app root sees only what is left.
//
//  Each root runs in TWO PASSES per frame:
//
//    Pass A - the updateInput walk (CBaseUIElement::updateInput, driven top-most-first by the
//             caller, e.g. UI::update over LAYER_ORDER). It does NOT deliver clicks: each visible+
//             enabled element under the cursor registers as a HIT CANDIDATE on the ctx
//             (CBaseUIEventCtx), snapshots its ancestor path, resolves hover LOSS locally, and
//             broadcasts outside-downs. Transparent wrappers (bClickThroughSelf: containers,
//             screens) register no candidacy of their own.
//
//    Pass B - dispatchEvents (this object). The frame's buffered button/wheel events (collected
//             off the Mouse relay) are routed to those candidates, hover GAIN is resolved, and
//             capture is advanced. One delivery per event, to one element.
//
//  ONE candidate set feeds hover, clicks AND wheel, ranked by ONE function (topCandidate):
//    1. group  - layer/screen order (beginHitGroup per layer; first group walked = top-most)
//    2. tier   - bDrawsOnTop raises an element's (sub)tree above later-visited same-group siblings
//    3. visit  - latest visited within a tier wins (= later sibling draws on top)
//  so the three pickers (hover, click-down, wheel) can never disagree on "what is on top".
//
//  Four routing concerns live in this one object, all keyed off that candidate set:
//    - hover   : the single top-most candidate + its ancestors gain hover (resolveHover)
//    - capture : a down implicitly captures its target; all further buttons + the matching up go
//                to the captor until release (lock/steal/cancel serve the drag widgets)
//    - wheel   : top-most wheel-accepting candidate, else the fall-through sink (VolumeOverlay)
//    - focus   : a single focused element across both roots (the keyboard target)
// ============================================================================================

// pass-B mouse routing + capture state for the CBaseUI layer. one instance, owned by Engine
// alongside the engine gui root; it listens on the same Mouse relay as every other consumer
// (Mouse itself knows nothing about UI routing; consumption state lives in the buffer here).

class CBaseUIElement;
struct CBaseUIEventCtx;

namespace CBaseUIDispatch {

// per-frame mouse sink
class MouseSink final : public MouseListener {
    NOCOPY_NOMOVE(MouseSink)
   public:
    MouseSink() = default;
    ~MouseSink() override = default;

    // buffers the frame's button events off the regular Mouse listener relay (events arrive
    // during the input-device update, but routing must wait until the updateInput walk has
    // collected the hit candidates). self-cleans: the first event of a new frame drops the
    // previous frame's.
    void onButtonChange(ButtonEvent &ev) override;

    // wheel deltas off the same relay, accumulated into per-frame totals (same self-cleaning
    // model); routed in dispatchEvents to the top-most wheel-accepting hit candidate, or to
    // the captor while a capture is held. consumed-state is shared across both roots like the
    // button events (the engine root dispatches first).
    void onWheelVertical(WheelDelta wheel) override;
    void onWheelHorizontal(WheelDelta wheel) override;
};

// which UI root a dispatch call serves; the engine root (guiContainer) dispatches before the
// app root each frame and consumes the events it delivers
enum class Root : uint8_t { ENGINE, APP };

// routes this frame's buffered mouse button events to the candidates collected during the
// updateInput walk. down -> best candidate + implicit capture, up -> whoever received the
// down. call once per root, directly after its updateInput pass.
void dispatchEvents(CBaseUIEventCtx &c, Root root);

// element dtors report here: bumps the mutation generation and releases a dead captor
void onElementDestroyed(CBaseUIElement *elem);

// the fall-through wheel sink: offered the frame's totals when no hit candidate in either
// root consumed them (VolumeOverlay; replaces its raw-delta poll, whose exclusivity used
// to depend on the canChangeVolume screen enumeration instead of actual consumption)
void setWheelSink(CBaseUIElement *sink);

// the single focused element across BOTH roots (the keyboard-target authority; replaces
// the engine->stealUIFocus() cascade). setFocus(x) relinquishes the previous holder via
// its stealFocus(); clearFocusIf is the non-recursive drop stealFocus() itself calls.
void setFocus(CBaseUIElement *elem);
[[nodiscard]] CBaseUIElement *getFocus();
void clearFocusIf(const CBaseUIElement *elem);

// a locked capture cannot be stolen (slider grab, scrollbar drag, window drag/resize);
// only the current captor may lock
void lockCapture(const CBaseUIElement *who);

// an ancestor on the captured hit path takes an unlocked descendant capture (scrollview past
// drag resistance): the old captor gets onMouseCancel (its press dies, no click), ancestors
// below the thief get onCapturedEndThrough, the thief becomes the locked captor
// returns true if capture steal succeeded
bool stealCapture(CBaseUIElement *thief);

[[nodiscard]] CBaseUIElement *getCaptor();
[[nodiscard]] bool isCaptureLocked();

// which buttons the current capture holds (for captured-move/observe handlers; reconciled
// against the device state every dispatch)
[[nodiscard]] MouseButtonFlags getCaptorButtons();

struct State;  // internal per-frame state (for CBaseUIElement friend access)
void clear();  // reset all state (sanity cleanup on engine shutdown)

}  // namespace CBaseUIDispatch
