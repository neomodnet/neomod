// Copyright (c) 2026, WH, All rights reserved.
#include "ToolUITest.h"

#include "CBaseUIBox.h"
#include "CBaseUIMenu.h"
#include "CBaseUIMenuBar.h"
#include "CBaseUIPopupLayer.h"
#include "CBaseUIStyledButton.h"
#include "CBaseUIStyledLabel.h"
#include "Engine.h"
#include "Font.h"
#include "KeyBindings.h"
#include "KeyboardEvent.h"
#include "Logging.h"
#include "Shortcut.h"
#include "TestMacros.h"
#include "UIStyle.h"

#include "fmt/format.h"

#include <string>
#include <vector>

namespace Mc::Tests {

namespace {
// an element whose content needs `natural`
struct Block final : CBaseUIElement {
    explicit Block(vec2 natural) : CBaseUIElement(0, 0, 0, 0, std::string{}), natural(natural) {}
    void draw() override {}
    vec2 getNaturalSize() override { return this->natural; }
    vec2 natural;
};

// a popup that says when it's deleted
struct Probe final : CBaseUIElement {
    Probe(vec2 natural, bool &deleted)
        : CBaseUIElement(0, 0, 0, 0, std::string{}), natural(natural), deleted(deleted) {}
    ~Probe() override { this->deleted = true; }
    void draw() override {}
    vec2 getNaturalSize() override { return this->natural; }
    vec2 natural;
    bool &deleted;
};

std::string rect(const CBaseUIElement *e) {
    return fmt::format("{},{} {}x{}", e->getRelPos().x, e->getRelPos().y, e->getSize().x, e->getSize().y);
}

KeyboardEvent key(SCANCODE scancode, KEYMOD modifiers) { return {scancode, 0, 0, 0, modifiers}; }
}  // namespace

ToolUITest::ToolUITest() { logRaw("ToolUITest created"); }

ToolUITest::~ToolUITest() { uiStyle().setScale(1.f); }

void ToolUITest::update() {
    if(m_done) return;
    m_done = true;

    this->testShortcuts();
    this->testBoxes();
    this->testStyledWidgets();
    this->testMenus();

    TEST_PRINT_RESULTS("ToolUITest");
    engine->shutdown();
}

void ToolUITest::testShortcuts() {
    TEST_SECTION("shortcuts");

    const Shortcut ctrlS{KEY_S, KEYMOD_CONTROL};
    TEST_ASSERT(ctrlS.matches(key(KEY_S, KEYMOD_LCONTROL)), "a shortcut matches its key with its modifier");
    TEST_ASSERT(ctrlS.matches(key(KEY_S, KEYMOD_RCONTROL | KEYMOD_CAPS | KEYMOD_NUM)),
                "...on either side, whatever the lock keys are");
    TEST_ASSERT(!ctrlS.matches(key(KEY_S, KEYMOD_LCONTROL | KEYMOD_LSHIFT)),
                "...but not with another modifier as well");
    TEST_ASSERT(!ctrlS.matches(key(KEY_S, KEYMOD_NONE)), "...nor without its modifier");
    TEST_ASSERT(!ctrlS.matches(key(KEY_A, KEYMOD_LCONTROL)), "...nor another key");

    const Shortcut space{KEY_SPACE};
    TEST_ASSERT(space.matches(key(KEY_SPACE, KEYMOD_NONE)) && !space.matches(key(KEY_SPACE, KEYMOD_LALT)),
                "a shortcut without modifiers wants none held");
}

void ToolUITest::testBoxes() {
    TEST_SECTION("boxes");
    UIStyle &style = uiStyle();
    style.setScale(1.f);

    CBaseUIBox row(CBaseUIBox::Axis::ROW, "row");
    auto *fixed = new Block({0, 0});
    auto *flexOne = new Block({0, 0});
    auto *flexThree = new Block({0, 0});
    auto *fit = new Block({20, 10});
    row.setPadding(4)->setGap(2);
    row.add(fixed, BoxItem::fixed(50))
        ->add(flexOne, BoxItem::flex(1))
        ->add(flexThree, BoxItem::flex(3))
        ->add(fit, BoxItem::fit(BoxItem::Align::CENTER));
    row.setPos(100, 50);
    row.setSize(300, 40);

    // inside: 292x32; the fixed and fitting items take 70, the gaps 6, so the weights share 216
    TEST_ASSERT_EQ(rect(fixed), "4,4 50x32", "a fixed item, filling the box across");
    TEST_ASSERT_EQ(rect(flexOne), "56,4 54x32", "a flexible item gets its share of what's left");
    TEST_ASSERT_EQ(rect(flexThree), "112,4 162x32", "...by its weight");
    TEST_ASSERT_EQ(rect(fit), "276,15 20x10", "a fitting item keeps its natural size, here centered across");
    TEST_ASSERT(fixed->getPos() == vec2(104, 54), "items are placed relative to the box");

    // (by hand, behind the box's back)
    fixed->setSize(1, 1);
    row.tick();
    TEST_ASSERT_EQ(rect(fixed), "4,4 1x1", "a box doesn't lay out again when nothing changed");
    fixed->requestLayout();
    row.tick();
    TEST_ASSERT_EQ(rect(fixed), "4,4 50x32", "...but at its next tick once an item asks for it");

    style.setScale(2.f);
    row.tick();
    // inside: 284x24; fixed 100, the fitting block (natural size in pixels) 20, gaps 12: the weights share 152
    TEST_ASSERT_EQ(rect(fixed), "8,8 100x24", "units follow the scale at the next tick");
    TEST_ASSERT_EQ(rect(flexThree), "154,8 114x24", "...and so do the shares");
    style.setScale(1.f);

    fit->setVisible(false);
    row.tick();
    // (the weights share 238: 59.5 and 178.5, rounded at their edges)
    TEST_ASSERT_EQ(rect(flexThree), "118,4 178x32", "a hidden item takes no space");
    fit->setVisible(true);
    row.tick();

    TEST_ASSERT(row.getNaturalSize() == vec2(4 + 50 + 2 + 0 + 2 + 0 + 2 + 20 + 4, 4 + 10 + 4),
                "a box's natural size: its fixed lengths and natural sizes, gaps and padding");

    CBaseUIBox column(CBaseUIBox::Axis::COLUMN, "column");
    auto *nested = new CBaseUIBox(CBaseUIBox::Axis::ROW, "nested");
    nested->add(new Block({30, 12}), BoxItem::fit())->add(new Block({40, 16}), BoxItem::fit())->setGap(5);
    auto *rest = new Block({0, 0});
    column.add(nested, BoxItem::fit())->add(rest, BoxItem::flex());
    column.setSize(200, 100);
    TEST_ASSERT_EQ(rect(nested), "0,0 200x16", "a nested box fits its own natural size");
    TEST_ASSERT_EQ(rect(nested->getElements()[1]), "35,0 40x16", "...and lays its items out in it");
    TEST_ASSERT_EQ(rect(rest), "0,16 200x84", "the column's flexible item takes the rest");

    // three thirds of 100: the edges are rounded, so neighbours meet exactly
    CBaseUIBox thirds(CBaseUIBox::Axis::ROW);
    auto *first = new Block({0, 0});
    auto *second = new Block({0, 0});
    auto *third = new Block({0, 0});
    thirds.add(first, BoxItem::flex())->add(second, BoxItem::flex())->add(third, BoxItem::flex());
    thirds.setSize(100, 10);
    TEST_ASSERT(first->getRelPos().x + first->getSize().x == second->getRelPos().x &&
                    second->getRelPos().x + second->getSize().x == third->getRelPos().x &&
                    third->getRelPos().x + third->getSize().x == 100.f,
                "fractional shares leave neither gaps nor overlaps");
}

void ToolUITest::testStyledWidgets() {
    TEST_SECTION("styled widgets");
    UIStyle &style = uiStyle();
    const McFont *font = style.font();

    CBaseUIStyledLabel label("Hello");
    TEST_ASSERT(label.getNaturalSize() == vec2(font->getStringWidth("Hello"), font->getHeight()),
                "a label's natural size is its text's");

    CBaseUIBox row(CBaseUIBox::Axis::ROW);
    auto *growing = new CBaseUIStyledLabel("a");
    auto *after = new Block({10, 10});
    row.add(growing, BoxItem::fit())->add(after, BoxItem::fit());
    row.setSize(500, 20);
    growing->setText("a much longer text");
    TEST_ASSERT_EQ(after->getRelPos().x, font->getStringWidth("a"),
                   "a label's new text isn't laid out before its box's tick");
    row.tick();
    TEST_ASSERT_EQ(after->getRelPos().x, font->getStringWidth("a much longer text"),
                   "...where what follows it moves along");

    CBaseUIStyledButton button("Back");
    style.setScale(2.f);
    TEST_ASSERT(button.getNaturalSize() == vec2(font->getStringWidth("Back") + 2.f * 2.f * style.metrics.controlPadding,
                                                2.f * style.metrics.controlHeight),
                "a button's natural size is its text's with the style's padding, at the style's height");
    style.setScale(1.f);

    const vec2 unchecked = button.getNaturalSize();
    button.setChecked(true);
    TEST_ASSERT(button.isChecked() && button.getNaturalSize() == unchecked, "a checked button keeps its size");
}

void ToolUITest::testMenus() {
    TEST_SECTION("menus");
    uiStyle().setScale(1.f);

    TEST_ASSERT_EQ(Shortcut(KEY_S, (KEYMOD)(KEYMOD_CONTROL | KEYMOD_SHIFT)).text(), "Ctrl+Shift+S",
                   "a shortcut's text: its modifiers, then its key");
    TEST_ASSERT_EQ(Shortcut{KEY_F5}.text(), "F5", "...without modifiers just the key");

    CBaseUIPopupLayer layer("layer");
    layer.setSize(300, 200);
    bool firstGone = false;
    bool secondGone = false;
    const u32 first = layer.open(new Probe({100, 50}, firstGone), {250, 180});
    TEST_ASSERT(layer.isVisible() && layer.isOpen(first), "an open popup shows the layer");
    TEST_ASSERT_EQ(rect(layer.getElements()[0]), "200,150 100x50",
                   "a popup gets its natural size, moved as far as needed to be inside the layer");
    const u32 second = layer.open(new Probe({10, 10}, secondGone), {5, 5});
    layer.close(first);
    TEST_ASSERT(!layer.isOpen(first) && !layer.isOpen(second) && !layer.isVisible(),
                "closing a popup closes the ones opened after it, and the layer hides with none open");
    TEST_ASSERT(!firstGone && !secondGone, "closed popups live until the layer's next tick");
    layer.tick();
    TEST_ASSERT(firstGone && secondGone, "...and go then");

    int chosen = -1;
    bool closedWhenRun = false;
    const auto act = [&](int which) {
        return [&, which] {
            chosen = which;
            closedWhenRun = !layer.isOpen();
        };
    };
    const std::vector<MenuItem> items{
        {.name = "a", .label = "A", .shortcut = "Ctrl+A", .action = act(0)},
        MenuItem::line(),
        {.name = "b", .label = "B", .action = act(1), .enabled = false},
        {.name = "c", .label = "C", .action = act(2), .checked = true},
        {.name = "d",
         .label = "D",
         .submenu =
             [&] {
                 return std::vector<MenuItem>{{.name = "e", .label = "E", .action = act(3)},
                                              {.name = "f", .label = "F", .action = act(4)}};
             }},
    };
    const auto press = [&layer](SCANCODE scancode) {
        KeyboardEvent e = key(scancode, KEYMOD_NONE);
        layer.onKeyDown(e);
        return e.isConsumed();
    };

    auto *menu = new CBaseUIMenu(layer, items, "menu");
    TEST_ASSERT_EQ(menu->getNaturalSize().y, 4.f * 24.f + 7.f + 2.f * 4.f,
                   "a menu is as tall as its rows and separators, with its padding");
    menu->open({0, 0});
    TEST_ASSERT_EQ(menu->getHighlighted(), -1, "a menu the mouse opened has nothing highlighted");
    press(KEY_DOWN);
    TEST_ASSERT_EQ(menu->getHighlighted(), 0, "Down highlights the first item");
    press(KEY_DOWN);
    TEST_ASSERT_EQ(menu->getHighlighted(), 3,
                   "...then the next one that can be chosen (no separator, nothing disabled)");
    press(KEY_DOWN);
    press(KEY_DOWN);
    TEST_ASSERT_EQ(menu->getHighlighted(), 0, "...around past the last");
    press(KEY_UP);
    TEST_ASSERT_EQ(menu->getHighlighted(), 4, "Up goes back");
    press(KEY_RIGHT);
    TEST_ASSERT_EQ(layer.getElements().size(), uSz{2}, "Right opens a submenu");
    TEST_ASSERT_EQ(static_cast<CBaseUIMenu *>(layer.getElements().back())->getHighlighted(), 0,
                   "...with its first item highlighted");
    press(KEY_LEFT);
    TEST_ASSERT_EQ(layer.getElements().size(), uSz{1}, "Left closes it");
    press(KEY_RIGHT);
    press(KEY_DOWN);
    press(KEY_ENTER);
    TEST_ASSERT(chosen == 4 && closedWhenRun && !layer.isOpen(), "Enter chooses, after every menu closed");
    layer.tick();

    menu = new CBaseUIMenu(layer, items, "menu");
    menu->open({0, 0});
    TEST_ASSERT(press(KEY_A) && layer.isOpen(), "no key gets past an open popup");
    press(KEY_ESCAPE);
    TEST_ASSERT(!layer.isOpen(), "Escape closes it");
    layer.tick();

    CBaseUIMenuBar bar(layer, "bar");
    const auto build = [&items] { return items; };
    bar.add("one", "One", build)->add("two", "Two", build)->add("three", "Three", build);
    bar.open(0, true);
    TEST_ASSERT(bar.getOpen() == 0 && static_cast<CBaseUIMenu *>(layer.getElements().back())->getHighlighted() == 0,
                "a title's menu opened by a key has its first item highlighted");
    press(KEY_RIGHT);
    TEST_ASSERT_EQ(bar.getOpen(), 1, "Right on an item without a submenu opens the next title's menu");
    press(KEY_LEFT);
    press(KEY_LEFT);
    TEST_ASSERT_EQ(bar.getOpen(), 2, "Left the previous one's, around past the first");
    press(KEY_ESCAPE);
    bar.tick();
    TEST_ASSERT_EQ(bar.getOpen(), -1, "the bar notices its menu closed");
}

}  // namespace Mc::Tests
