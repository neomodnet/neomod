#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIContainer.h"

#include <functional>
#include <string>
#include <vector>

class CBaseUIPopupLayer;

// one entry of a menu: something to do, a submenu, or a separator line
struct MenuItem {
    std::string name{};  // its row's element name (for scripts)
    std::string label{};
    std::string shortcut{};                            // shown at its right (see Shortcut::text())
    std::function<void()> action{};                    // run when it's chosen, once the menus are closed
    std::function<std::vector<MenuItem>()> submenu{};  // its items, made when it opens (instead of an action)
    bool checked{false};
    bool enabled{true};
    bool separator{false};

    [[nodiscard]] static MenuItem line() { return {.separator = true}; }
};

// a list of items shown in a popup layer: the mouse highlights a row and a click chooses it, Up and Down move the
// highlight, Right and Left open and close submenus, Enter or Space choose (Escape closes it, through the layer).
// choosing an item closes every menu before its action runs
class CBaseUIMenu final : public CBaseUIContainer {
    NOCOPY_NOMOVE(CBaseUIMenu)
   public:
    CBaseUIMenu(CBaseUIPopupLayer &layer, std::vector<MenuItem> items, std::string name = {});
    ~CBaseUIMenu() override;

    // shows it in the layer at `pos` (the layer owns it from here on; see CBaseUIPopupLayer::open()), its id there.
    // keyboard: opened by a key, so its first item is highlighted
    u32 open(vec2 pos, const McRect &passThrough = {}, bool keyboard = false);
    // Left on a menu without a parent and Right on an item without a submenu go here (a menu bar's neighbours)
    CBaseUIMenu *setSideways(std::function<void(int direction)> sideways);

    // the row the mouse or the keyboard points at (-1: none)
    [[nodiscard]] int getHighlighted() const { return this->highlighted; }
    [[nodiscard]] const std::vector<MenuItem> &getItems() const { return this->items; }

    void draw() override;
    void onKeyDown(KeyboardEvent &e) override;
    [[nodiscard]] vec2 getNaturalSize() override;

   private:
    class Row;

    // the next row that can be chosen from `from` in `direction` (wrapping), -1 without one
    [[nodiscard]] int nextChoosable(int from, int direction) const;
    void highlight(int index);
    // keyboard: a submenu it opens gets its first item highlighted
    void choose(int index, bool keyboard);
    void openSubmenu(int index, bool keyboard);
    void closeSubmenu();

    CBaseUIPopupLayer &layer;
    std::vector<MenuItem> items;
    std::vector<Row *> rows;
    std::function<void(int)> sideways;
    CBaseUIMenu *parent{nullptr};
    u32 id{0};
    // the open submenu, while the layer has its id open (0: none)
    CBaseUIMenu *submenu{nullptr};
    u32 submenuId{0};
    int submenuIndex{-1};
    int highlighted{-1};
};
