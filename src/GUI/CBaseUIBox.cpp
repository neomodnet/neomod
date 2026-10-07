// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"

#include "Hashing.h"
#include "UIStyle.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
// the boxes that exist, by id (see onItemChanged())
Hash::flat::map<u32, CBaseUIBox *> &boxes() {
    static Hash::flat::map<u32, CBaseUIBox *> map;
    return map;
}
u32 nextId{1};

// a vector's component along a box's axis, and across it
float along(vec2 v, CBaseUIBox::Axis axis) { return axis == CBaseUIBox::Axis::ROW ? v.x : v.y; }
float across(vec2 v, CBaseUIBox::Axis axis) { return axis == CBaseUIBox::Axis::ROW ? v.y : v.x; }
vec2 make(float alongAxis, float acrossAxis, CBaseUIBox::Axis axis) {
    return axis == CBaseUIBox::Axis::ROW ? vec2{alongAxis, acrossAxis} : vec2{acrossAxis, alongAxis};
}
}  // namespace

CBaseUIBox::CBaseUIBox(Axis axis, std::string name)
    : CBaseUIContainer(0, 0, 0, 0, std::move(name)), axis(axis), id(nextId++) {
    boxes().emplace(this->id, this);
}

CBaseUIBox::~CBaseUIBox() { boxes().erase(this->id); }

void CBaseUIBox::onItemChanged(u32 boxId) {
    if(const auto it = boxes().find(boxId); it != boxes().end()) it->second->requestOwnLayout();
}

void CBaseUIBox::requestOwnLayout() {
    this->bLayoutDirty = true;
    this->requestLayout();
}

CBaseUIBox *CBaseUIBox::add(CBaseUIElement *child, BoxItem item) {
    child->setBoxItem(item);
    child->boxId = this->id;
    this->addBaseUIElement(child);
    this->requestOwnLayout();
    return this;
}

CBaseUIBox *CBaseUIBox::setPadding(float units) {
    this->padding = units;
    this->requestOwnLayout();
    return this;
}

CBaseUIBox *CBaseUIBox::setGap(float units) {
    this->gap = units;
    this->requestOwnLayout();
    return this;
}

void CBaseUIBox::tick() {
    // (children first: what they change in their tick is laid out in the same frame)
    CBaseUIContainer::tick();
    if(this->isVisible() && (this->bLayoutDirty || this->styleGeneration != uiStyle().getGeneration())) this->layout();
}

void CBaseUIBox::onResized() { this->layout(); }

vec2 CBaseUIBox::getNaturalSize() {
    const UIStyle &style = uiStyle();
    float length = 0.f, thickness = 0.f;
    int count = 0;
    for(auto *child : this->vElements) {
        if(!child->isVisible()) continue;
        const BoxItem &item = child->getBoxItem();
        const vec2 natural = child->getNaturalSize();
        length += item.size == BoxItem::Size::FIXED ? style.px(item.value) : along(natural, this->axis);
        thickness = std::max(thickness, across(natural, this->axis));
        count++;
    }
    const float pad = 2.f * style.px(this->padding);
    return make(length + style.px(this->gap) * static_cast<float>(std::max(count - 1, 0)) + pad, thickness + pad,
                this->axis);
}

void CBaseUIBox::layout() {
    const UIStyle &style = uiStyle();
    const float pad = style.px(this->padding);
    const float gap = style.px(this->gap);
    const float innerLength = std::max(along(this->getSize(), this->axis) - 2.f * pad, 0.f);
    const float innerThickness = std::max(across(this->getSize(), this->axis) - 2.f * pad, 0.f);

    // what the fixed and natural lengths take, and how the rest is shared
    float taken = 0.f, weights = 0.f;
    int count = 0;
    for(auto *child : this->vElements) {
        // (also those added with the container's functions instead of add())
        child->boxId = this->id;
        if(!child->isVisible()) continue;
        const BoxItem &item = child->getBoxItem();
        if(item.size == BoxItem::Size::FIXED) taken += style.px(item.value);
        if(item.size == BoxItem::Size::FIT) taken += along(child->getNaturalSize(), this->axis);
        if(item.size == BoxItem::Size::FLEX) weights += item.value;
        count++;
    }
    const float left = std::max(innerLength - taken - gap * static_cast<float>(std::max(count - 1, 0)), 0.f);

    // (edges are rounded, not lengths, so that neighbours meet without gaps or overlaps)
    float cursor = pad;
    for(auto *child : this->vElements) {
        if(!child->isVisible()) continue;
        const BoxItem &item = child->getBoxItem();
        const vec2 natural = child->getNaturalSize();

        float length = 0.f;
        switch(item.size) {
            case BoxItem::Size::FIXED:
                length = style.px(item.value);
                break;
            case BoxItem::Size::FIT:
                length = along(natural, this->axis);
                break;
            case BoxItem::Size::FLEX:
                length = weights > 0.f ? left * item.value / weights : 0.f;
                break;
        }

        float thickness = innerThickness, offset = 0.f;
        if(item.align != BoxItem::Align::FILL) {
            thickness = std::min(across(natural, this->axis), innerThickness);
            if(item.align == BoxItem::Align::CENTER) offset = (innerThickness - thickness) / 2.f;
            if(item.align == BoxItem::Align::END) offset = innerThickness - thickness;
        }

        const float start = std::round(cursor), end = std::round(cursor + length);
        const float top = std::round(pad + offset), bottom = std::round(pad + offset + thickness);
        child->setRelPos(make(start, top, this->axis));
        child->setSize(make(end - start, bottom - top, this->axis));
        cursor += length + gap;
    }

    this->update_pos();
    this->bLayoutDirty = false;
    this->styleGeneration = style.getGeneration();
}
