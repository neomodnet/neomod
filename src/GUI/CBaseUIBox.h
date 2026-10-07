#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIContainer.h"

// a row or a column: lays its visible children out one after another along its axis, each as its BoxItem says, with
// padding around them and gaps between them (in units, see UIStyle). it lays out again right away when its size changes,
// and at its next tick when a child came or went, changed its natural size, BoxItem or visibility (requestLayout()) or
// the style's scale changed: nothing else ever has to tell it
class CBaseUIBox : public CBaseUIContainer {
    NOCOPY_NOMOVE(CBaseUIBox)
   public:
    enum class Axis : u8 { ROW, COLUMN };

    explicit CBaseUIBox(Axis axis, std::string name = {});
    ~CBaseUIBox() override;

    // adds `child` (the box owns it, like any container), laid out as `item`
    CBaseUIBox *add(CBaseUIElement *child, BoxItem item);
    CBaseUIBox *setPadding(float units);
    CBaseUIBox *setGap(float units);

    void tick() override;
    void onResized() override;

    // one of its items asked for a layout (CBaseUIElement::requestLayout()). an item knows its box by id, which is still
    // safe to use when the box is gone: elements can be taken out of a container and outlive it
    static void onItemChanged(u32 boxId);

    // what its children's fixed lengths and natural sizes add up to, with padding and gaps
    [[nodiscard]] vec2 getNaturalSize() override;

   private:
    void layout();
    // at its next tick, and its own box with it (its natural size may change too)
    void requestOwnLayout();

    Axis axis;
    float padding{0.f};
    float gap{0.f};

    u32 id;
    bool bLayoutDirty{true};
    u32 styleGeneration{0};  // the style's when it last laid out
};
