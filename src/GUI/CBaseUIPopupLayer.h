#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIContainer.h"

#include <string>
#include <vector>

// hosts popups (menus) over the content it lies on, as the last child of the container holding that content, sized to
// it. while one is open the layer covers its whole area: a press outside the popups closes them and goes no further, the
// wheel ends there, and every key goes to the topmost popup first (Escape closes it); it is hidden otherwise. a popup
// is deleted after the frame that closed it, so it can close itself from its own handlers
class CBaseUIPopupLayer final : public CBaseUIContainer {
    NOCOPY_NOMOVE(CBaseUIPopupLayer)
   public:
    explicit CBaseUIPopupLayer(std::string name = {});
    ~CBaseUIPopupLayer() override;

    // shows `popup` (the layer owns it) on top of the others, at its natural size, at `pos` moved as far as needed to
    // be inside the layer. presses inside `passThrough` still reach what's beneath (a menu bar's titles while one of
    // its menus is open). returns its id
    u32 open(CBaseUIElement *popup, vec2 pos, const McRect &passThrough = {});
    // closes the popup with this id and every popup opened after it
    void close(u32 id);
    void closeAll();
    [[nodiscard]] bool isOpen() const { return !this->popups.empty(); }
    [[nodiscard]] bool isOpen(u32 id) const;

    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;
    void onKeyDown(KeyboardEvent &e) override;
    void onKeyUp(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;

   protected:
    void onMouseDownInside(bool left, bool right) override;
    bool onWheel(WheelDelta vertical, WheelDelta horizontal) override;

   private:
    struct Popup {
        u32 id;
        McRect passThrough;
    };
    std::vector<Popup> popups;  // the open ones, bottom first, as vElements
    std::vector<CBaseUIElement *> closed;
    u32 nextId{1};
};
