#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIElement.h"
#include "Color.h"
#include "types.h"

#include <functional>
#include <string>

class CBaseUIColorPicker;
class CBaseUIPopupLayer;

// a control showing a colour, drawn by the style: a click opens a colour picker under it in a popup layer (which has to
// outlive it), and a colour set there becomes its colour and goes to the change callback. the picker stays until a
// press beside it or Escape, with the colour it opened with next to the one being set
class CBaseUIColorButton final : public CBaseUIElement {
    NOCOPY_NOMOVE(CBaseUIColorButton)
   public:
    explicit CBaseUIColorButton(CBaseUIPopupLayer &layer, std::string name = {});
    ~CBaseUIColorButton() override;

    // shows it, opaque (the change callback isn't called)
    CBaseUIColorButton *setColor(Color colour);
    // what it shows: while its picker is dragged in, not yet what its user was told
    [[nodiscard]] Color getColor() const;
    // called with a colour set in its picker
    CBaseUIColorButton *setChangeCallback(std::function<void(Color)> callback);

    void open();
    void close();
    [[nodiscard]] bool isOpen() const;
    // which picker opening this is (0: it never opened), to tell the changes in one visit from those in the next
    [[nodiscard]] u32 getVisit() const { return this->popupId; }
    // its picker is being dragged or typed in
    [[nodiscard]] bool isAdjusting() const;

    void draw() override;
    [[nodiscard]] vec2 getNaturalSize() override;

   protected:
    void onMouseDownInside(bool left, bool right) override;

   private:
    CBaseUIPopupLayer &layer;
    std::function<void(Color)> changeCallback;
    Color colour{0xff000000};
    CBaseUIColorPicker *picker{nullptr};  // owned by the layer, there while isOpen()
    u32 popupId{0};
};
