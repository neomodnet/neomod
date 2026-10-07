#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIElement.h"

#include <functional>
#include <string>
#include <vector>

class CBaseUIPopupLayer;

// a control showing the chosen one of its options, drawn by the style: a click opens a menu of the options under it in
// a popup layer (which has to outlive it), and the one chosen there becomes the chosen one and goes to the change
// callback. with none chosen it shows nothing (what a mixed selection has, for example)
class CBaseUIDropdown final : public CBaseUIElement {
    NOCOPY_NOMOVE(CBaseUIDropdown)
   public:
    CBaseUIDropdown(CBaseUIPopupLayer &layer, std::vector<std::string> options, std::string name = {});
    ~CBaseUIDropdown() override;

    // -1: none; the change callback isn't called
    CBaseUIDropdown *setChosen(int index);
    [[nodiscard]] int getChosen() const { return this->chosen; }
    // called with an option chosen from the menu
    CBaseUIDropdown *setChangeCallback(std::function<void(int index)> callback);

    // the menu of options (keyboard: a key opened it, so its first item is highlighted)
    void open(bool keyboard);
    void close();
    [[nodiscard]] bool isOpen() const;

    void draw() override;
    [[nodiscard]] vec2 getNaturalSize() override;

   protected:
    void onMouseDownInside(bool left, bool right) override;

   private:
    CBaseUIPopupLayer &layer;
    std::vector<std::string> options;
    std::function<void(int)> changeCallback;
    u32 menuId{0};
    int chosen{-1};
};
