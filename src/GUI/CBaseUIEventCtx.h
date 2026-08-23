#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "noinclude.h"

#include <vector>

class CBaseUIElement;

struct CBaseUIEventCtx {
    void clear();

    // candidacy suppression carried along the walk: the bottom bar clears this on a click so the
    // carousel (a full-height surface visited after it) doesn't also register as a hit candidate.
    // this is NOT consumption - the walk keeps going; see bConsumed below.
    // TODO: this is still dirty (also CBaseUIEventCtx still holds a lot of possibly redundant state with CBaseUIDispatch)
    bool propagate_clicks{true};

    // the walk floor: a visible modal layer calls consume_mouse() and the LAYER_ORDER walk stops below it.
    bool bConsumed{false};

    void consume_mouse();

    [[nodiscard]] bool mouse_consumed() const;

    // pass-A hit-candidate collection: elements under the cursor that may receive this frame's
    // button events; single-target delivery happens in CBaseUIDispatch::dispatchEvents after the
    // walk. groups = top-level screens/overlays in input-priority order; within a group the
    // best (tier, latest visit) candidate wins. each candidate snapshots the ancestor chain that led
    // to it (outermost first): if it captures, those ancestors observe the drag and may steal (scrollview drag resistance).
    // TODO: approximating top-most draw order until we have a real layer stack (providing Z-order)
    struct HitCandidate {
        CBaseUIElement *elem;
        int tier;
        bool wheelOnly{false};
        // hit-clipped by an ancestor surface: the dispatcher skips clipped candidates when
        // ranking the top one but still retracts any stale hover they hold
        bool clipped{false};
        std::vector<CBaseUIElement *> path;
    };
    std::vector<HitCandidate> hitCandidates;
    std::vector<size_t> hitGroupStarts;
    std::vector<CBaseUIElement *> hitPath;  // ancestor stack during the walk
    // within a hit group candidacy ranks by (tier, then latest visit); an element raises this for its
    // own subtree by declaring bDrawsOnTop (see CBaseUIElement), so "draws on top" beats "visited
    // later" automatically instead of each call site bracketing the walk by hand
    int currentHitTier{0};

    void beginHitGroup();
    void addHitCandidate(CBaseUIElement *elem);
    // wheel-only candidate, skipped by button targeting: a hover-independent wheel claim
    // (screen rects are 0x0, so a screen-wide claim cannot come from bMouseInside candidacy);
    // register it FIRST in the group so every hovered candidate gets first refusal
    void addWheelClaim(CBaseUIElement *elem);

    // RAII: containers wrap their child walk in a scope so candidates registered inside know
    // their ancestor chain
    struct HitPathScope {
        NOCOPY_NOMOVE(HitPathScope)
       public:
        HitPathScope(CBaseUIEventCtx &ctx, CBaseUIElement *elem);
        ~HitPathScope();

       private:
        CBaseUIEventCtx &c;
    };
};
