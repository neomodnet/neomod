// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIEventCtx.h"

void CBaseUIEventCtx::clear() {
    this->bConsumed = false;
    this->propagate_clicks = true;
    this->currentHitTier = 0;
    this->hitCandidates.clear();
    this->hitGroupStarts.clear();
    this->hitPath.clear();
}

void CBaseUIEventCtx::consume_mouse() { this->bConsumed = true; }

bool CBaseUIEventCtx::mouse_consumed() const { return this->bConsumed; }

void CBaseUIEventCtx::beginHitGroup() { this->hitGroupStarts.push_back(this->hitCandidates.size()); }

void CBaseUIEventCtx::addHitCandidate(CBaseUIElement *elem) {
    if(this->hitGroupStarts.empty()) this->hitGroupStarts.push_back(0);  // implicit single group
    this->hitCandidates.push_back({.elem = elem, .tier = this->currentHitTier, .path = this->hitPath});
}

void CBaseUIEventCtx::addWheelClaim(CBaseUIElement *elem) {
    if(this->hitGroupStarts.empty()) this->hitGroupStarts.push_back(0);
    // no ancestor path: claims never receive buttons, so they never capture
    this->hitCandidates.push_back({.elem = elem, .tier = this->currentHitTier, .wheelOnly = true, .path = {}});
}

CBaseUIEventCtx::HitPathScope::HitPathScope(CBaseUIEventCtx &ctx, CBaseUIElement *elem) : c(ctx) {
    this->c.hitPath.push_back(elem);
}

CBaseUIEventCtx::HitPathScope::~HitPathScope() { this->c.hitPath.pop_back(); }
