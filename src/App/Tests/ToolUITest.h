// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "App.h"

namespace Mc::Tests {

// the framework's pieces for tool screens: shortcuts, boxes, the style's widgets, popups, menus and dropdowns
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
    void testMenus();

    int m_passes{0};
    int m_failures{0};
    bool m_done{false};
};

}  // namespace Mc::Tests
