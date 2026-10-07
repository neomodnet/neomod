#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"
#include "CBaseUIMenu.h"

#include <functional>
#include <string>
#include <vector>

class CBaseUIPopupLayer;

// a row of menu titles whose menus open in a popup layer, which has to outlive the bar: clicking a title opens its
// menu below it, hovering another title while one is open opens that one instead, clicking the open one closes it, and
// Left and Right in an open menu go to the neighbouring titles' menus
class CBaseUIMenuBar final : public CBaseUIBox {
    NOCOPY_NOMOVE(CBaseUIMenuBar)
   public:
    explicit CBaseUIMenuBar(CBaseUIPopupLayer &layer, std::string name = {});
    ~CBaseUIMenuBar() override;

    // a title (an element named `name`) whose menu `items` makes whenever it opens
    CBaseUIMenuBar *add(std::string name, std::string label, std::function<std::vector<MenuItem>()> items);
    // opens a title's menu instead of the open one (keyboard: a key opened it, so its first item is highlighted)
    void open(int index, bool keyboard);
    void close();
    // the title whose menu is open (-1: none)
    [[nodiscard]] int getOpen() const { return this->openIndex; }

    void tick() override;

   private:
    class Title;

    CBaseUIPopupLayer &layer;
    std::vector<Title *> titles;
    std::vector<std::function<std::vector<MenuItem>()>> menus;
    u32 menuId{0};
    int openIndex{-1};
};
