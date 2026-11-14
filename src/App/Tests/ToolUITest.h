// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "App.h"

namespace Mc::Tests {

// the framework pieces for tool screens: shortcuts, boxes, the style widgets, textboxes and fields, popups, menus,
// dropdowns and colour pickers
class ToolUITest : public App {
    NOCOPY_NOMOVE(ToolUITest)
   public:
    ToolUITest();
    ~ToolUITest() override;

    void update() override;

   private:
    void testShortcuts();
    void testBoxes();
    void testStyledWidgets();
    void testTextboxes();
    void testTextFields();
    void testNumberFields();
    void testLists();
    void testMenus();
    void testColourPickers();

    int m_passes{0};
    int m_failures{0};
    bool m_done{false};
};

}  // namespace Mc::Tests
