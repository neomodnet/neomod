// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIDispatch.h"

#include "CBaseUIElement.h"
#include "Engine.h"
#include "Mouse.h"

#include <algorithm>
#include <array>
#include <span>
#include <cassert>

namespace CBaseUIDispatch {

struct State final {
    // documentation for these first few are in the header
    void dispatchEvents(CBaseUIEventCtx &c, Root root);
    void onElementDestroyed(CBaseUIElement *elem);
    void setWheelSink(CBaseUIElement *sink) { this->wheelSink = sink; }
    void setFocus(CBaseUIElement *elem);
    [[nodiscard]] CBaseUIElement *getFocus() const { return this->focused; }
    void clearFocusIf(const CBaseUIElement *elem) {
        if(this->focused == elem) this->focused = nullptr;
    }
    void lockCapture(const CBaseUIElement *who);
    bool stealCapture(CBaseUIElement *thief);

    [[nodiscard]] CBaseUIElement *getCaptor() const { return this->captor; }
    [[nodiscard]] bool isCaptureLocked() const { return this->captorLocked; }
    [[nodiscard]] MouseButtonFlags getCaptorButtons() const { return this->captorButtons; }

    // hover resolved once per frame from the walk's hit candidates: the single top-most candidate
    // (+ its ancestor path) GAINS hover, everything beneath it is retracted - one resolution that
    // reuses the click-targeting ranking, so occlusion is automatic instead of hand-rolled per
    // widget. runs for both roots every frame regardless of events (the engine console draws on top:
    // if it hovers something it suppresses the app root). hover LOSS on rect-leave stays
    // element-local (CBaseUIElement::updateInput); a held capture freezes gains for its own root.
    void resolveHover(CBaseUIEventCtx &c, Root root);

    // capture ends without an up: onMouseCancel to the captor + observation end (cancelCapture),
    // or observation end alone (endObservation, captor handled separately by the caller)
    void cancelCapture();
    void endObservation();

    // one captured frame: move to the captor, then observation through its ancestors
    // (innermost first; an observer may steal or mutate the UI mid-loop)
    void observeCapturedFrame();

    static constexpr const uSz MAX_QUEUED_BUTTONS{1024};
    std::array<ButtonEvent, MAX_QUEUED_BUTTONS> queue{};
    u64 qSize{0};
    u64 lastPushFrame{0};

    // per-frame wheel totals
    int wheelVertical{0};
    int wheelHorizontal{0};
    u64 lastWheelFrame{0};
    bool wheelConsumed{false};
    // a hit group containing a hovered scroll surface (bWheelSurface) declined the wheel:
    // the scan stops there for the rest of the frame, across both roots - surfaces occluded
    // by that group never see it; only the fall-through sink may still take it
    bool wheelFloored{false};
    CBaseUIElement *wheelSink{nullptr};

    // the single focused element across both roots; element dtors and stealFocus() clear it
    CBaseUIElement *focused{nullptr};

    // per-frame hover resolution: the first root that hovers something claims the pointer for the
    // frame (engine before app), so a lower root suppresses its hover when an upper root already has it
    u64 lastHoverFrame{0};
    bool hoverClaimed{false};

    // mouse capture: whichever element receives a down receives the matching up(s). one captor
    // globally (there is one pointer device), tagged with the UI root that owns it so the other
    // root's dispatch leaves its events alone
    CBaseUIElement *captor{nullptr};
    Root captorRoot{Root::APP};
    MouseButtonFlags captorButtons{};
    bool captorLocked{false};

    // the captor's ancestor chain at down time (outermost first): observes the drag once per
    // frame and may steal; element dtors prune themselves out
    static constexpr const uSz MAX_CAPTURE_PATH_DEPTH{128};
    std::vector<CBaseUIElement *> capturePath{};

    // last cursor position delivered as a captured move (for move-edge trace gating)
    vec2 lastCaptureMovePos{0.f};

    // bumped on every element destruction; dispatch abandons the frame's remaining deliveries
    // when it changes mid-loop (a handler deleted/rebuilt parts of the UI under the candidates)
    u64 elemGeneration{0};
};

namespace {

// per-frame buffer lives here
inline CONSTINIT State state{};

// the top-most hit candidate matching pred: groups in input-priority order, within a group the best
// (tier, then latest visit), the first group with a match winning (= the top-most layer). the single
// ranking shared by hover resolution and button-down targeting (wheel walks the same order with its
// own per-group floor), so the three pickers cannot disagree on what is "on top".
template <typename Pred>
const CBaseUIEventCtx::HitCandidate *topCandidate(const CBaseUIEventCtx &c, Pred pred) {
    for(uSz g = 0; g < c.hitGroupStarts.size(); ++g) {
        const uSz begin = c.hitGroupStarts[g];
        const uSz end = (g + 1 < c.hitGroupStarts.size()) ? c.hitGroupStarts[g + 1] : c.hitCandidates.size();
        const CBaseUIEventCtx::HitCandidate *best = nullptr;
        int bestTier = 0;
        for(uSz i = begin; i < end; ++i) {
            const auto &cand = c.hitCandidates[i];
            if(cand.clipped) continue;  // hit-clipped by an ancestor surface: never the top (but still retracted)
            if(!pred(cand)) continue;
            if(best == nullptr || cand.tier >= bestTier) {
                best = &cand;
                bestTier = cand.tier;
            }
        }
        if(best != nullptr) return best;
    }
    return nullptr;
}
}  // namespace

void clear() { state = {}; }

// MouseSink implementation
void MouseSink::onButtonChange(ButtonEvent &ev) {
    const u64 frame = engine->getFrameCount();
    if(frame != state.lastPushFrame) {
        state.qSize = 0;
        state.lastPushFrame = frame;
    }
    assert(state.qSize < state.queue.size());
    state.queue[state.qSize++] = ev;
}

void MouseSink::onWheelVertical(int delta) {
    const u64 frame = engine->getFrameCount();
    if(frame != state.lastWheelFrame) {
        state.wheelVertical = state.wheelHorizontal = 0;
        state.wheelConsumed = state.wheelFloored = false;
        state.lastWheelFrame = frame;
    }
    state.wheelVertical += delta;
}

void MouseSink::onWheelHorizontal(int delta) {
    const u64 frame = engine->getFrameCount();
    if(frame != state.lastWheelFrame) {
        state.wheelVertical = state.wheelHorizontal = 0;
        state.wheelConsumed = state.wheelFloored = false;
        state.lastWheelFrame = frame;
    }
    state.wheelHorizontal += delta;
}

void State::setFocus(CBaseUIElement *elem) {
    if(this->focused == elem) return;
    CBaseUIElement *old = this->focused;
    this->focused = elem;
    // the old holder loses focus (bActive=false + onFocusStolen); its own clearFocusIf is a
    // no-op now that focused already moved, so this does not recurse
    if(old != nullptr) old->stealFocus();
}

void State::onElementDestroyed(CBaseUIElement *elem) {
    assert(elem != nullptr);

    ++this->elemGeneration;
    if(this->wheelSink == elem) this->wheelSink = nullptr;
    if(this->focused == elem) this->focused = nullptr;
    if(this->captor == elem) {
        // the captor is mid-destruction: no calls into it, but observers still disarm
        this->captor = nullptr;
        this->captorButtons = {};
        this->captorLocked = false;
        this->endObservation();
    } else {
        std::erase(this->capturePath, elem);
    }
}

void State::lockCapture(const CBaseUIElement *who) {
    if(this->captor == who) this->captorLocked = true;
}

bool State::stealCapture(CBaseUIElement *thief) {
    if(this->captor == nullptr || this->captorLocked) return false;
    if(this->captor == thief) return true;

    const auto thiefIt = std::ranges::find(this->capturePath, thief);
    if(thiefIt == this->capturePath.end()) return false;  // only observing ancestors may steal

    // detach the in-between ancestors before any handler runs (they may mutate the path);
    // the thief's own ancestors keep observing the new capture
    const std::vector<CBaseUIElement *> detached(thiefIt + 1, this->capturePath.end());
    this->capturePath.erase(thiefIt, this->capturePath.end());

    CBaseUIElement *old = this->captor;
    this->captor = thief;
    this->captorLocked = true;
    thief->bActive = true;
    UI_TRACE_EVENT(0, thief, "captureSteal");

    old->bActive = false;
    UI_TRACE_EVENT(0, old, "mouseCancel");
    old->onMouseCancel();
    for(auto it = detached.rbegin(); it != detached.rend(); ++it) (*it)->onCapturedEndThrough();
    return true;
}

void State::cancelCapture() {
    if(this->captor == nullptr) return;

    CBaseUIElement *old = this->captor;
    this->captor = nullptr;
    this->captorButtons = {};
    this->captorLocked = false;

    old->bActive = false;
    UI_TRACE_EVENT(0, old, "mouseCancel");
    old->onMouseCancel();
    this->endObservation();
}

void State::endObservation() {
    // move out: end handlers may start new captures or destroy elements
    const std::vector<CBaseUIElement *> path = std::move(this->capturePath);
    this->capturePath.clear();
    for(auto it = path.rbegin(); it != path.rend(); ++it) (*it)->onCapturedEndThrough();
}

void State::observeCapturedFrame() {
    const vec2 pos = mouse->getPos();
    if(pos != this->lastCaptureMovePos) {
        this->lastCaptureMovePos = pos;
        UI_TRACE_EVENT(0, this->captor, "captureMove");
    }
    this->captor->onCapturedMouseMove();

    // innermost first; an observer may steal mid-loop (the path shrinks to its own ancestors,
    // which are exactly the entries not yet visited) or mutate the UI (generation guard)
    const u64 observeGeneration = this->elemGeneration;
    const std::vector<CBaseUIElement *> path = this->capturePath;
    for(auto it = path.rbegin(); it != path.rend(); ++it) {
        if(this->captor == nullptr || this->elemGeneration != observeGeneration) break;
        if(std::ranges::find(this->capturePath, *it) == this->capturePath.end()) continue;
        (*it)->onCapturedMoveThrough();
    }
}

void State::resolveHover(CBaseUIEventCtx &c, Root root) {
    const u64 frame = engine->getFrameCount();
    if(frame != this->lastHoverFrame) {
        this->hoverClaimed = false;
        this->lastHoverFrame = frame;
    }

    // a held capture freezes hover GAINS for its own root: nothing beneath the captor gains (the
    // captor keeps the hover it had at the down; losing-on-rect-leave is handled in updateInput).
    // leaving the candidates untouched keeps the gesture's hover state from churning. the engine
    // root's capture still claims the pointer, so the app beneath a dragged window retracts
    if(this->captor != nullptr && this->captorRoot == root) {
        if(root == Root::ENGINE) this->hoverClaimed = true;
        return;
    }

    // the hovered set = the top-most candidate (+ its ancestor path), the same ranking the button
    // targeting uses. an upper root that already claimed hover (the engine console draws on top)
    // suppresses this root entirely (empty set -> everything beneath retracts).
    std::vector<CBaseUIElement *> set;
    if(!(root == Root::APP && this->hoverClaimed)) {
        const CBaseUIEventCtx::HitCandidate *top =
            topCandidate(c, [](const CBaseUIEventCtx::HitCandidate &cand) { return !cand.wheelOnly; });
        if(top != nullptr) {
            set = top->path;
            set.push_back(top->elem);
            this->hoverClaimed = true;
        }
    }

    // GAIN the set members not already hovered (a candidate's hover GAIN happens ONLY here, so an
    // occluded one never briefly gains it during the walk and plays a spurious hover sound), and
    // RETRACT candidates that fell out of the set (occluded by a higher candidate, or this root
    // suppressed by an upper one).
    for(const auto &cand : c.hitCandidates) {
        if(cand.wheelOnly) continue;
        CBaseUIElement *e = cand.elem;
        const bool want = std::ranges::find(set, e) != set.end();
        if(want && !e->bMouseInside) {
            e->bMouseInside = true;
            UI_TRACE_EVENT(0, e, "hoverIn");
            e->onMouseInside();
        } else if(!want && e->bMouseInside) {
            e->bMouseInside = false;
            UI_TRACE_EVENT(0, e, "hoverOut");
            e->onMouseOutside();
        }
    }
}

void State::dispatchEvents(CBaseUIEventCtx &c, Root root) {
    using namespace flags::operators;

    const u64 frame = engine->getFrameCount();
    const bool hasEvents = (this->lastPushFrame == frame && this->qSize != 0);

    if(this->captor != nullptr && this->captorRoot == root) {
        // self-heal: a captured button can be released without an up event ever reaching us (e.g.
        // released while the window was unfocused; Mouse reconciles buttonsHeldMask on reset).
        // pending same-frame ups count as held so they still deliver below.
        MouseButtonFlags pendingUps{};
        if(hasEvents) {
            for(uSz i = 0; i < this->qSize; ++i) {
                auto &qe = this->queue[i];
                if(!qe.down) pendingUps |= qe.btn;
            }
        }
        this->captorButtons &= (mouse->getHeldButtons() | pendingUps);
        if(!this->captorButtons) this->cancelCapture();
    }

    // captured phase: cancel-on-ineligible + the per-frame captured move and ancestor
    // observation. runs BEFORE the event loop so a release this frame acts on state that
    // already includes this frame's motion (e.g. scrollview kinetic launch).
    if(this->captor != nullptr && this->captorRoot == root) {
        // input-eligible this frame = the updateInput walk reached it (covers hidden, disabled
        // and procedurally input-blocked, none of which stamp)
        if(this->captor->lastInputFrame != frame) {
            this->cancelCapture();
        } else {
            this->observeCapturedFrame();
        }
    }

    // hover is resolved every frame, independent of button/wheel events (the cursor moves without
    // them); must run before the early-out below
    this->resolveHover(c, root);

    const bool hasWheel = (this->lastWheelFrame == frame && !this->wheelConsumed &&
                           (this->wheelVertical != 0 || this->wheelHorizontal != 0));
    if(!hasEvents && !hasWheel) return;

    const u64 startGeneration = this->elemGeneration;

    // wheel before buttons:
    // the frame's totals go to ONE consumer, with decliners falling through (scroll chaining:
    // a button or a can't-scroll dropdown above its scrollview passes the wheel on). while a
    // capture is held the chain is the captor then its observing ancestors innermost-first
    // (mirrors the drag-steal chain); otherwise the top-most wheel-accepting candidate wins -
    // groups in input-priority order, within a group by (tier, latest visit) like the button
    // targeting
    if(hasWheel) {
        if(this->captor != nullptr) {
            if(this->captorRoot == root) {
                this->wheelConsumed = true;
                CBaseUIElement *acceptor = nullptr;
                if(this->captor->onWheel(this->wheelVertical, this->wheelHorizontal)) {
                    acceptor = this->captor;
                } else {
                    const std::vector<CBaseUIElement *> path = this->capturePath;
                    for(auto it = path.rbegin(); it != path.rend() && acceptor == nullptr; ++it) {
                        if(this->elemGeneration != startGeneration) break;
                        if((*it)->onWheel(this->wheelVertical, this->wheelHorizontal)) acceptor = *it;
                    }
                }
                if(acceptor != nullptr) {
                    UI_TRACE_EVENT(0, acceptor, "wheel");
                }
            }
        } else {
            for(uSz g = 0; g < c.hitGroupStarts.size() && !this->wheelConsumed && !this->wheelFloored; ++g) {
                const uSz begin = c.hitGroupStarts[g];
                const uSz end = (g + 1 < c.hitGroupStarts.size()) ? c.hitGroupStarts[g + 1] : c.hitCandidates.size();
                if(begin == end) continue;

                std::vector<uSz> order;
                order.reserve(end - begin);
                for(uSz i = begin; i < end; ++i) order.push_back(i);
                std::ranges::sort(order, [&c](uSz a, uSz b) {
                    return c.hitCandidates[a].tier != c.hitCandidates[b].tier
                               ? c.hitCandidates[a].tier > c.hitCandidates[b].tier
                               : a > b;
                });

                bool groupHasScrollSurface = false;
                for(const uSz i : order) {
                    if(this->elemGeneration != startGeneration) return;  // a handler mutated the UI
                    if(c.hitCandidates[i].clipped) continue;  // clipped by an ancestor surface: not under the cursor
                    CBaseUIElement *elem = c.hitCandidates[i].elem;
                    groupHasScrollSurface |= elem->bWheelSurface;
                    if(elem->onWheel(this->wheelVertical, this->wheelHorizontal)) {
                        UI_TRACE_EVENT(0, elem, "wheel");
                        this->wheelConsumed = true;
                        break;
                    }
                }

                // wheel floor: a hovered scroll surface that declined (fits-content, scrolling
                // disabled, alt) still visually OWNS the point - the gesture must not act on
                // surfaces occluded by its layer. in-group chaining above already happened (an
                // anchored dropdown falls to the scrollview it rides), and the fall-through
                // sink below stays reachable (a dead wheel is still the global volume gesture).
                if(groupHasScrollSurface) this->wheelFloored = true;
            }

            // nothing wanted it: offer the totals to the fall-through sink. the app root
            // dispatches last each frame, so the sink sees only deltas neither root's
            // candidates consumed
            if(root == Root::APP && !this->wheelConsumed && this->wheelSink != nullptr) {
                if(this->wheelSink->onWheel(this->wheelVertical, this->wheelHorizontal)) {
                    UI_TRACE_EVENT(0, this->wheelSink, "wheel");
                    this->wheelConsumed = true;
                }
            }
        }
    }

    if(!hasEvents) return;

    for(uSz qi = 0; qi < this->qSize; ++qi) {
        auto &qe = this->queue[qi];
        if(qe.consumed) continue;

        const MouseButtonFlags btn = qe.btn;
        const bool left = (btn == MouseButtonFlags::MF_LEFT);
        const bool right = (btn == MouseButtonFlags::MF_RIGHT);
        if(!left && !right) continue;  // middle/X buttons have no UI semantics (app code polls them)

        if(this->elemGeneration != startGeneration) break;  // a handler mutated the UI, candidates are stale

        if(this->captor != nullptr) {
            if(this->captorRoot != root) continue;  // the owning root's dispatch handles these
            qe.consumed = true;

            CBaseUIElement *elem = this->captor;
            // input-eligible this frame = the updateInput walk reached it (procedural gating)
            const bool eligible = (elem->lastInputFrame == frame);
            if(qe.down) {
                this->captorButtons |= btn;
                if(eligible) {
                    if(elem->bMouseInside) {
                        UI_TRACE_EVENT(0, elem, "downInside");
                        elem->onMouseDownInside(left, right);
                        elem->bActive = true;
                    } else {
                        UI_TRACE_EVENT(1, elem, "downOutside");
                        elem->onMouseDownOutside(left, right);
                    }
                }
            } else {
                this->captorButtons &= ~btn;
                // bActive gate: a capture steal during the hold cancelled the press already
                if(eligible && elem->bActive) {
                    if(elem->bMouseInside) {
                        UI_TRACE_EVENT(0, elem, "upInside");
                        elem->onMouseUpInside(left, right);
                    } else {
                        UI_TRACE_EVENT(1, elem, "upOutside");
                        elem->onMouseUpOutside(left, right);
                    }
                    elem->bActive = false;
                } else if(!eligible) {
                    // captor went hidden/blocked between the captured-phase check and this event
                    // (mid-event-loop mutation): same cancel semantics, no click
                    this->captorButtons = {};
                    this->cancelCapture();
                    continue;
                }
                if(!this->captorButtons) {
                    this->captor = nullptr;
                    this->captorLocked = false;
                    this->endObservation();
                }
            }
        } else if(qe.down) {
            // route to the best candidate (the shared top-most ranking, filtered to handlers of
            // this button): groups in input-priority order, within a group by (tier, latest visit)
            const CBaseUIEventCtx::HitCandidate *target =
                topCandidate(c, [left, right](const CBaseUIEventCtx::HitCandidate &cand) {
                    if(cand.wheelOnly) return false;
                    if(left && !cand.elem->bHandleLeftMouse) return false;
                    if(right && !cand.elem->bHandleRightMouse) return false;
                    return true;
                });
            if(target == nullptr) continue;  // nothing under the cursor in this root; the other may deliver

            qe.consumed = true;
            this->captor = target->elem;
            this->captorRoot = root;
            this->captorButtons = btn;
            this->captorLocked = false;
            this->capturePath = target->path;
            this->lastCaptureMovePos = mouse->getPos();
            UI_TRACE_EVENT(0, target->elem, "downInside");
            target->elem->onMouseDownInside(left, right);
            target->elem->bActive = true;
            // ancestors observe from the press frame (a scrollview arms its drag gesture here)
            if(this->captor == target->elem) this->observeCapturedFrame();
        }
        // up with no captor: nobody received the down (or the captor died); leave it alone
    }
}

// passthroughs for public functionality
void dispatchEvents(CBaseUIEventCtx &c, Root root) { return state.dispatchEvents(c, root); }
void onElementDestroyed(CBaseUIElement *elem) { return state.onElementDestroyed(elem); }
void setWheelSink(CBaseUIElement *sink) { return state.setWheelSink(sink); }
void setFocus(CBaseUIElement *elem) { return state.setFocus(elem); }
CBaseUIElement *getFocus() { return state.getFocus(); }
void clearFocusIf(const CBaseUIElement *elem) { return state.clearFocusIf(elem); }
void lockCapture(const CBaseUIElement *who) { return state.lockCapture(who); }
bool stealCapture(CBaseUIElement *thief) { return state.stealCapture(thief); }
CBaseUIElement *getCaptor() { return state.getCaptor(); }
bool isCaptureLocked() { return state.isCaptureLocked(); }
MouseButtonFlags getCaptorButtons() { return state.getCaptorButtons(); }

}  // namespace CBaseUIDispatch
