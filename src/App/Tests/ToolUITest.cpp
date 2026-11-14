// Copyright (c) 2026, WH, All rights reserved.
#include "ToolUITest.h"

#include "CBaseUIBox.h"
#include "CBaseUIColorButton.h"
#include "CBaseUIColorPicker.h"
#include "CBaseUIDropdown.h"
#include "CBaseUIList.h"
#include "CBaseUIMenu.h"
#include "CBaseUIMenuBar.h"
#include "CBaseUINumberField.h"
#include "CBaseUIPopupLayer.h"
#include "CBaseUIStyledButton.h"
#include "CBaseUIStyledLabel.h"
#include "CBaseUIStyledTextbox.h"
#include "CBaseUITabBar.h"
#include "CBaseUITextbox.h"
#include "ContainerRanges.h"
#include "Engine.h"
#include "Environment.h"
#include "Font.h"
#include "KeyBindings.h"
#include "KeyboardEvent.h"
#include "Logging.h"
#include "Mouse.h"
#include "Shortcut.h"
#include "TestMacros.h"
#include "UIStyle.h"

#include "fmt/format.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
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

// a number field its test presses, drags and releases
struct PressedNumberField final : CBaseUINumberField {
    using CBaseUINumberField::CBaseUINumberField;
    using CBaseUINumberField::onCapturedMouseMove;
    using CBaseUINumberField::onMouseCancel;
    using CBaseUINumberField::onMouseDownInside;
    using CBaseUINumberField::onMouseUpInside;
    using CBaseUINumberField::onMouseUpOutside;
};

// a list its test presses, with rows its test makes (a row's key is its text)
struct PressedList final : CBaseUIList {
    using CBaseUIList::CBaseUIList;
    using CBaseUIList::onMouseDownInside;
    using CBaseUIList::onMouseUpInside;
    using CBaseUIList::onWheel;
};
struct Rows final : CBaseUIList::Model {
    [[nodiscard]] uSz getRowCount() const override { return this->keys.size(); }
    [[nodiscard]] u64 getRowKey(uSz row) const override { return this->keys[row]; }
    [[nodiscard]] std::string getCellText(uSz row, uSz column) const override {
        this->asked.push_back(row);
        return fmt::format("{}:{}", this->keys[row], column);
    }
    std::vector<u64> keys;
    mutable std::vector<uSz> asked;
};

std::string rect(const CBaseUIElement *e) {
    return fmt::format("{},{} {}x{}", e->getRelPos().x, e->getRelPos().y, e->getSize().x, e->getSize().y);
}

KeyboardEvent key(SCANCODE scancode, KEYMOD modifiers) { return {scancode, 0, 0, 0, modifiers}; }

// the element called `name` in `root` or under it
CBaseUIElement *find(CBaseUIElement *root, std::string_view name) {
    if(root->getName() == name) return root;
    for(CBaseUIElement *child : root->getAllChildren()) {
        if(CBaseUIElement *found = find(child, name)) return found;
    }
    return nullptr;
}

void type(CBaseUIElement *field, std::u32string_view characters) {
    for(const char32_t character : characters) {
        KeyboardEvent e{0, 0, character, 0, KEYMOD_NONE};
        field->onChar(e);
    }
}

constexpr KEYMOD CTRL_SHIFT{(KEYMOD)(KEYMOD_LCONTROL | KEYMOD_LSHIFT)};
}  // namespace

ToolUITest::ToolUITest() { logRaw("ToolUITest created"); }

ToolUITest::~ToolUITest() { uiStyle().setScale(1.f); }

void ToolUITest::update() {
    if(m_done) return;
    m_done = true;

    this->testShortcuts();
    this->testBoxes();
    this->testStyledWidgets();
    this->testTextboxes();
    this->testTextFields();
    this->testNumberFields();
    this->testLists();
    this->testMenus();
    this->testColourPickers();

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
    TEST_ASSERT(ctrlS.matches(key(KEY_S, KEYMOD_LCONTROL | KEYMOD_LALT), KEYMOD_ALT) &&
                    !ctrlS.matches(key(KEY_S, KEYMOD_LCONTROL | KEYMOD_LSHIFT), KEYMOD_ALT),
                "an ignored modifier counts as not held");

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

    // a label given less room than its text
    CBaseUIBox narrow(CBaseUIBox::Axis::ROW);
    auto *squeezed = new CBaseUIStyledLabel("a much longer text");
    narrow.add(squeezed, BoxItem::flex())->add(new Block({10, 10}), BoxItem::fit());
    narrow.setSize(font->getStringWidth("a much longer text") + 10.f, 20);
    TEST_ASSERT(squeezed->getShownText() == "a much longer text", "a label with room for its text shows all of it");
    narrow.setSize(font->getStringWidth("a much lon..."), 20);
    TEST_ASSERT(squeezed->getShownText().ends_with("...") && squeezed->getText() == "a much longer text" &&
                    font->getStringWidth(squeezed->getShownText()) <= squeezed->getSize().x + 1.f,
                "...and one without cuts it short with an ellipsis, its text being what it was");
    squeezed->setCutInMiddle(true);
    TEST_ASSERT(squeezed->getShownText().starts_with("a m") && squeezed->getShownText().ends_with("ext") &&
                    squeezed->getShownText().contains("...") &&
                    font->getStringWidth(squeezed->getShownText()) <= squeezed->getSize().x + 1.f,
                "...or, cut in the middle, keeps both ends");
    squeezed->setText("short");
    TEST_ASSERT(squeezed->getShownText() == "short", "...until a text that fits is set");

    CBaseUIStyledButton button("Back");
    style.setScale(2.f);
    TEST_ASSERT(button.getNaturalSize() == vec2(font->getStringWidth("Back") + 2.f * 2.f * style.metrics.controlPadding,
                                                2.f * style.metrics.controlHeight),
                "a button's natural size is its text's with the style's padding, at the style's height");
    style.setScale(1.f);

    const vec2 unchecked = button.getNaturalSize();
    button.setChecked(true);
    TEST_ASSERT(button.isChecked() && button.getNaturalSize() == unchecked, "a checked button keeps its size");

    CBaseUITabBar bar("tabs");
    bar.add("tab_first", "First")->add("tab_second", "Second");
    auto *first = static_cast<CBaseUIStyledButton *>(bar.getElements()[0]);
    auto *second = static_cast<CBaseUIStyledButton *>(bar.getElements()[1]);
    int heard = -1;
    bar.setChangeCallback([&](int index) { heard = index; });
    TEST_ASSERT(bar.getChosen() == 0 && first->isChecked() && !second->isChecked(), "a tab bar's first tab is chosen");
    second->click();
    TEST_ASSERT(heard == 1 && bar.getChosen() == 1 && !first->isChecked() && second->isChecked(),
                "a click on another tab chooses it and tells the bar's user");
    heard = -1;
    second->click();
    bar.setChosen(0);
    TEST_ASSERT(heard == -1 && bar.getChosen() == 0 && first->isChecked(),
                "...which a click on the chosen tab or a tab set doesn't");
}

void ToolUITest::testTextboxes() {
    TEST_SECTION("textboxes");

    CBaseUITextbox box(0, 0, 400, 30, "box");
    const auto press = [&](SCANCODE scancode, KEYMOD modifiers = KEYMOD_NONE) {
        KeyboardEvent e = key(scancode, modifiers);
        box.onKeyDown(e);
    };
    const auto type = [&](char32_t character) {
        KeyboardEvent e{0, 0, character, 0, KEYMOD_NONE};
        box.onChar(e);
    };

    box.setText("one two  three");
    box.focus();
    TEST_ASSERT(box.isFocused() && box.caretPosition == 14, "a focused textbox has its caret after its text");

    press(KEY_LEFT, KEYMOD_LCONTROL);
    TEST_ASSERT_EQ(box.caretPosition, 9, "Ctrl+Left goes to the start of the word before the caret");
    press(KEY_LEFT, KEYMOD_LCONTROL);
    TEST_ASSERT_EQ(box.caretPosition, 4, "...over the spaces between two words");
    press(KEY_RIGHT, KEYMOD_LCONTROL);
    TEST_ASSERT_EQ(box.caretPosition, 9, "Ctrl+Right goes to the start of the next word");
    press(KEY_RIGHT, KEYMOD_LCONTROL);
    TEST_ASSERT_EQ(box.caretPosition, 14, "...and from the last word to the end");

    press(KEY_HOME);
    press(KEY_RIGHT, KEYMOD_LSHIFT);
    press(KEY_RIGHT, KEYMOD_LSHIFT);
    TEST_ASSERT(box.hasSelectedText() && box.caretPosition == 2, "Shift+Right selects from the caret on");
    press(KEY_LEFT, KEYMOD_LSHIFT);
    press(KEY_DELETE);
    TEST_ASSERT_EQ(box.getText(), "ne two  three", "...Shift+Left gives a character back, Delete removes the rest");

    press(KEY_END, KEYMOD_LSHIFT);
    TEST_ASSERT(box.hasSelectedText() && box.caretPosition == 13, "Shift+End selects to the end");
    press(KEY_LEFT);
    TEST_ASSERT(!box.hasSelectedText() && box.caretPosition == 0,
                "Left alone puts the caret where the selection began");
    press(KEY_RIGHT, CTRL_SHIFT);
    press(KEY_BACKSPACE);
    TEST_ASSERT_EQ(box.getText(), "two  three", "Ctrl+Shift+Right selects a word with the spaces after it");

    press(KEY_END);
    press(KEY_BACKSPACE, KEYMOD_LCONTROL);
    TEST_ASSERT_EQ(box.getText(), "two  ", "Ctrl+Backspace removes the word before the caret");
    press(KEY_BACKSPACE, KEYMOD_LCONTROL);
    TEST_ASSERT_EQ(box.getText(), "", "...with the spaces after it, when the caret is behind them");

    // where Ctrl+Left/Right stop, until the caret stays where it is
    const auto jumps = [&](SCANCODE scancode) {
        std::vector<int> stops;
        do {
            press(scancode, KEYMOD_LCONTROL);
            stops.push_back(box.caretPosition);
        } while(stops.size() < 2 || stops[stops.size() - 2] != stops.back());
        return stops;
    };
    box.setText("foo.bar(baz) 1.5");
    press(KEY_HOME);
    TEST_ASSERT(jumps(KEY_RIGHT) == std::vector<int>({7, 8, 11, 13, 16, 16}),
                "Ctrl+Right stops at punctuation, a '.' between letters is part of the word");
    TEST_ASSERT(jumps(KEY_LEFT) == std::vector<int>({13, 11, 8, 7, 0, 0}), "...and so does Ctrl+Left");
    box.setText("\u6b8b\u9177\u306a\u5929\u4f7f");
    press(KEY_END);
    TEST_ASSERT(jumps(KEY_LEFT) == std::vector<int>({9, 6, 0, 0}), "...and between kanji and kana");
    box.setText("foo.bar(baz)");
    press(KEY_END);
    press(KEY_BACKSPACE, KEYMOD_LCONTROL);
    press(KEY_BACKSPACE, KEYMOD_LCONTROL);
    TEST_ASSERT_EQ(box.getText(), "foo.bar(", "Ctrl+Backspace removes the punctuation and the word before the caret");

    box.setText("abc");
    press(KEY_A, KEYMOD_LCONTROL);
    TEST_ASSERT(box.hasSelectedText() && box.caretPosition == 3, "Ctrl+A selects everything");
    type(U'x');
    TEST_ASSERT_EQ(box.getText(), "x", "...which what's typed replaces");

    box.setText("a\u00f1b");
    press(KEY_END);
    press(KEY_LEFT);
    press(KEY_LEFT, KEYMOD_LSHIFT);
    press(KEY_DELETE);
    TEST_ASSERT_EQ(box.getText(), "ab", "the caret and the selection step by characters, not bytes");

    box.stealFocus();
    TEST_ASSERT(!box.isFocused(), "a textbox gives the focus back");
}

void ToolUITest::testTextFields() {
    TEST_SECTION("text fields");
    uiStyle().setScale(1.f);

    CBaseUIStyledTextbox field("field");
    field.setNaturalWidth(100.f);
    TEST_ASSERT(field.getNaturalSize() == vec2(100.f, uiStyle().metrics.controlHeight),
                "a text field is as wide as its user says and as high as a control");

    std::vector<std::string> heard;
    field.setChangeCallback([&](std::string_view text) { heard.emplace_back(text); });
    const auto press = [&](SCANCODE scancode, KEYMOD modifiers = KEYMOD_NONE) {
        KeyboardEvent e = key(scancode, modifiers);
        field.onKeyDown(e);
        return e.isConsumed();
    };
    const auto type = [&](char32_t character) {
        KeyboardEvent e{0, 0, character, 0, KEYMOD_NONE};
        field.onChar(e);
        return e.isConsumed();
    };

    field.setText("ab");
    TEST_ASSERT(heard.empty(), "a text set isn't an edit");
    TEST_ASSERT(!press(KEY_A) && !type(U'a'), "a field without the focus takes no key");

    field.focus();
    type(U'c');
    type(U'd');
    press(KEY_BACKSPACE);
    press(KEY_LEFT);
    TEST_ASSERT(field.getText() == "abc" && heard.empty(), "what's typed during a visit isn't told");
    field.commit();
    TEST_ASSERT(heard == std::vector<std::string>({"abc"}) && field.isFocused(),
                "...before the visit is committed, which keeps the focus");
    type(U'd');
    press(KEY_BACKSPACE);
    field.commit();
    TEST_ASSERT_EQ(heard.size(), 1u, "a visit that ends with the text it started with is no change");

    TEST_ASSERT(
        press(KEY_S) && press(KEY_SPACE) && press(KEY_UP) && press(KEY_E, (KEYMOD)(KEYMOD_LCONTROL | KEYMOD_RALT)),
        "a focused field takes the keys that type or edit");
    TEST_ASSERT(!press(KEY_S, KEYMOD_LCONTROL) && !press(KEY_F5) && !press(KEY_TAB) && !press(KEY_A, KEYMOD_LSUPER),
                "...but not the function keys, Tab, or what's pressed with Ctrl or Cmd");
    TEST_ASSERT(press(KEY_A, KEYMOD_LCONTROL) && field.hasSelectedText(), "...unless it has a use for it");

    type(U'x');
    TEST_ASSERT_EQ(field.getText(), "x", "what's typed replaces the selection");
    press(KEY_ESCAPE);
    TEST_ASSERT(field.getText() == "abc" && heard.size() == 1 && !field.isFocused(),
                "Escape ends the visit with the text it started with, untold");

    field.focus();
    type(U'!');
    TEST_ASSERT(press(KEY_Z) && field.isFocused(), "a z is a key that types");
    press(KEY_Z, KEYMOD_LCONTROL);
    TEST_ASSERT(field.getText() == "abc" && heard.size() == 1 && !field.isFocused(), "...and Ctrl+Z is Escape");

    field.focus();
    type(U'z');
    press(KEY_ENTER);
    TEST_ASSERT(field.getText() == "abcz" && heard.back() == "abcz" && !field.isFocused(),
                "Enter ends the visit with the text as it is");
    field.focus();
    press(KEY_ESCAPE);
    TEST_ASSERT_EQ(field.getText(), "abcz", "...which the next visit starts with");

    field.setFilter([](char32_t character) { return character < 127; });
    field.focus();
    TEST_ASSERT(type(U'\u00f1') && field.getText() == "abcz", "a character its filter refuses isn't typed");
    env->setClipBoardText("1\n2\u00f13");
    press(KEY_V, KEYMOD_LCONTROL);
    TEST_ASSERT_EQ(field.getText(), "abcz123", "...nor pasted, and neither are control characters");
    field.stealFocus();
    TEST_ASSERT(heard == std::vector<std::string>({"abc", "abcz", "abcz123"}), "the focus leaving ends a visit too");
}

void ToolUITest::testNumberFields() {
    TEST_SECTION("number fields");
    uiStyle().setScale(1.f);

    PressedNumberField field("number");
    field.setRange(0.0, 10.0)->setStep(0.1, 0.01)->setDecimals(2);
    std::vector<f64> heard;
    field.setChangeCallback([&](f64 value) { heard.push_back(value); });
    const auto press = [&](SCANCODE scancode, KEYMOD modifiers = KEYMOD_NONE) {
        KeyboardEvent e = key(scancode, modifiers);
        field.onKeyDown(e);
    };
    const auto type = [&](std::u32string_view characters) {
        for(const char32_t character : characters) {
            KeyboardEvent e{0, 0, character, 0, KEYMOD_NONE};
            field.onChar(e);
        }
    };

    field.setValue(5.0);
    TEST_ASSERT(field.getText() == "5" && heard.empty(),
                "a number field shows its value without zeros after it, and a value set isn't a change");
    field.setSuffix("x");
    field.setValue(4.125);
    TEST_ASSERT_EQ(field.getText(), "4.125x", "...with its suffix, and with all the decimals it has");

    field.setNamedValue(0.0, "Auto");
    field.setValue(0.0);
    TEST_ASSERT_EQ(field.getText(), "Auto", "a named value shows as its name");
    field.visit();
    TEST_ASSERT_EQ(field.getText(), "0", "...and as its number while it's typed in");
    field.stealFocus();
    TEST_ASSERT(field.getText() == "Auto" && heard.empty(), "...which changes nothing");

    field.setValue(5.0);
    field.visit();
    TEST_ASSERT(field.isFocused() && field.getText() == "5" && field.hasSelectedText(),
                "visited, it has the number alone, selected");
    type(U"7,5");
    TEST_ASSERT(heard.empty(), "what's typed isn't the value yet");
    press(KEY_ENTER);
    TEST_ASSERT(
        field.getValue() == 7.5 && heard == std::vector<f64>({7.5}) && field.getText() == "7.5x" && !field.isFocused(),
        "Enter takes it");
    field.visit();
    type(U"99");
    field.stealFocus();
    TEST_ASSERT(field.getValue() == 10.0 && field.getText() == "10x",
                "...and so does the focus leaving, within the range");
    field.visit();
    type(U"-");
    press(KEY_ENTER);
    field.visit();
    type(U"3");
    press(KEY_ESCAPE);
    TEST_ASSERT(field.getValue() == 10.0 && heard.size() == 2 && field.getText() == "10x",
                "what's no number changes nothing, nor does Escape");
    field.setValue(4.125);
    field.visit();
    press(KEY_ENTER);
    TEST_ASSERT(field.getValue() == 4.125 && heard.size() == 2, "a visit without typing leaves the value as it is");

    field.setValue(5.0);
    field.visit();
    press(KEY_UP);
    press(KEY_DOWN, KEYMOD_LSHIFT);
    TEST_ASSERT(field.getValue() == 5.09 && field.getText() == "5.09" && field.isFocused() && heard.size() == 2,
                "Up and Down step it while it's visited, with Shift by the fine step, untold");
    field.commit();
    TEST_ASSERT(heard.back() == 5.09 && heard.size() == 3 && field.isFocused(),
                "...before the visit is committed, which keeps the focus");
    press(KEY_UP);
    press(KEY_ESCAPE);
    TEST_ASSERT(field.getValue() == 5.09 && heard.size() == 3 && field.getText() == "5.09x",
                "...and Escape takes back what wasn't");
    field.setValue(4.125);
    field.visit();
    press(KEY_UP);
    press(KEY_ESCAPE);
    TEST_ASSERT(field.getValue() == 4.125 && heard.size() == 3, "...to the value as it was, off its decimals or not");

    // (100 steps in the range: 4 units each)
    field.setSize(72.f, 28.f);
    field.setValue(5.0);
    heard.clear();
    mouse->setPos({10.f, 10.f});
    field.onMouseDownInside(true, false);
    mouse->setPos({12.f, 10.f});
    field.onCapturedMouseMove();
    TEST_ASSERT(!field.isAdjusting() && heard.empty(), "a press isn't a drag before the cursor went some way");
    mouse->setPos({30.f, 10.f});
    field.onCapturedMouseMove();
    TEST_ASSERT(field.isAdjusting() && field.getValue() == 5.5 && field.getText() == "5.5x",
                "a drag to the right raises the value by steps");
    mouse->setPos({-70.f, 10.f});
    field.onCapturedMouseMove();
    TEST_ASSERT(field.getValue() == 3.0 && heard.empty(), "...one to the left lowers it, untold while it lasts");
    mouse->setPos({10.f, 10.f});
    field.onCapturedMouseMove();
    TEST_ASSERT_EQ(field.getValue(), 5.0, "back where it started it's the value it was");
    mouse->setPos({1000.f, 10.f});
    field.onCapturedMouseMove();
    field.onMouseUpOutside(true, false);
    TEST_ASSERT(heard == std::vector<f64>({10.0}) && !field.isAdjusting() && !field.isFocused(),
                "the value it ends at is told, within the range");

    mouse->setPos({10.f, 10.f});
    field.onMouseDownInside(true, false);
    mouse->setPos({-30.f, 10.f});
    field.onCapturedMouseMove();
    mouse->setPos({10.f, 10.f});
    field.onCapturedMouseMove();
    field.onMouseUpInside(true, false);
    TEST_ASSERT(heard.size() == 1 && !field.isFocused(), "a drag that ends where it started is no change");
    mouse->setPos({10.f, 10.f});
    field.onMouseDownInside(true, false);
    mouse->setPos({-30.f, 10.f});
    field.onCapturedMouseMove();
    field.onMouseCancel();
    TEST_ASSERT(field.getValue() == 10.0 && heard.size() == 1 && !field.isAdjusting(),
                "...and neither is one that's cancelled");

    mouse->setPos({10.f, 10.f});
    field.onMouseDownInside(true, false);
    field.onMouseUpInside(true, false);
    TEST_ASSERT(field.isFocused() && field.getText() == "10" && field.hasSelectedText(), "a click visits it");
    field.stealFocus();

    // no value
    const uSz told = heard.size();
    field.setValue(std::numeric_limits<f64>::quiet_NaN());
    TEST_ASSERT(field.getText().empty() && heard.size() == told, "a value that's no number shows as nothing");
    field.onMouseDownInside(true, false);
    mouse->setPos({60.f, 10.f});
    field.onCapturedMouseMove();
    field.onMouseUpInside(true, false);
    TEST_ASSERT(!field.isAdjusting() && field.isFocused() && field.getText().empty(),
                "...which a drag doesn't change: it's a click");
    press(KEY_UP);
    press(KEY_ENTER);
    TEST_ASSERT(std::isnan(field.getValue()) && heard.size() == told, "...nor does a step, or a visit without typing");
    field.visit();
    type(U"3");
    press(KEY_ENTER);
    TEST_ASSERT(field.getValue() == 3.0 && heard.back() == 3.0 && field.getText() == "3x",
                "...but a number typed is its value");
}

void ToolUITest::testLists() {
    TEST_SECTION("lists");
    uiStyle().setScale(1.f);
    const float rowHeight = uiStyle().metrics.rowHeight;

    Rows rows;
    for(u64 key = 100; key < 200; key++) rows.keys.push_back(key);
    PressedList list(rows, {{"a", 50.f}, {"b", 0.f}}, "list");
    // a header and five rows
    list.setPos(0.f, 0.f);
    list.setSize(200.f, rowHeight * 6.f);
    int changes = 0;
    std::vector<uSz> activated;
    list.setSelectionCallback([&] { changes++; });
    list.setActivateCallback([&](uSz row) { activated.push_back(row); });
    const auto selected = [&] {
        std::vector<u64> keys{list.getSelection().begin(), list.getSelection().end()};
        Mc::ranges::sort(keys);
        return keys;
    };
    const auto click = [&](uSz rowInView) {
        mouse->setPos({20.f, rowHeight * ((float)rowInView + 1.5f)});
        list.onMouseDownInside(true, false);
        list.onMouseUpInside(true, false);
    };
    const auto press = [&](SCANCODE scancode, KEYMOD modifiers = KEYMOD_NONE) {
        KeyboardEvent e = key(scancode, modifiers);
        list.onKeyDown(e);
        return e.isConsumed();
    };
    const auto inView = [&](uSz first, uSz last) { return list.getRowsInView() == std::pair{first, last}; };

    TEST_ASSERT(inView(0, 5), "a list shows the rows that fit under its header");
    TEST_ASSERT(!press(KEY_DOWN), "...and takes no key without the focus");
    click(1);
    TEST_ASSERT(selected() == std::vector<u64>({101}) && changes == 1 && list.isFocused(),
                "a click selects a row and gives the list the focus");
    press(KEY_DOWN);
    press(KEY_DOWN, KEYMOD_LSHIFT);
    TEST_ASSERT(selected() == std::vector<u64>({102, 103}) && changes == 3,
                "Down picks the next row, with Shift all from the one picked without it");
    press(KEY_END);
    TEST_ASSERT(selected() == std::vector<u64>({199}) && inView(95, 100), "End picks the last row, scrolled into view");
    press(KEY_PAGEUP);
    TEST_ASSERT(selected() == std::vector<u64>({195}), "a page is a row less than what fits");
    TEST_ASSERT(press(KEY_ENTER) && activated == std::vector<uSz>({95}), "Enter activates the row picked");
    TEST_ASSERT(press(KEY_A, KEYMOD_LCONTROL) && list.getSelection().size() == 100, "Ctrl+A selects every row");
    TEST_ASSERT(!press(KEY_S, KEYMOD_LCONTROL) && !press(KEY_SPACE) && !press(KEY_DELETE),
                "the keys it has no use for go on");

    // rows going and coming
    press(KEY_HOME);
    press(KEY_DOWN, KEYMOD_LSHIFT);
    rows.keys.erase(rows.keys.begin());
    rows.keys.insert(rows.keys.begin() + 3, 500);
    TEST_ASSERT(selected() == std::vector<u64>({100, 101}), "the selection is the keys', whatever the rows do");
    press(KEY_DOWN);
    TEST_ASSERT(selected() == std::vector<u64>({102}), "...and the keys go on from where the row picked is now");
    list.setSelection(std::vector<u64>{500, 103});
    TEST_ASSERT(selected() == std::vector<u64>({103, 500}) && changes == 9, "a selection set isn't the user's change");
    list.setSelection(std::vector<u64>{105});
    press(KEY_UP);
    TEST_ASSERT(selected() == std::vector<u64>({104}), "a row selected alone is where the keys go on from");
    list.setSelection(std::vector<u64>{500, 103});

    // only what shows is asked for
    list.scrollTo(50);
    rows.asked.clear();
    list.draw();
    Mc::ranges::sort(rows.asked);
    rows.asked.erase(std::unique(rows.asked.begin(), rows.asked.end()), rows.asked.end());
    TEST_ASSERT(rows.asked == std::vector<uSz>({46, 47, 48, 49, 50}), "only the rows in view are asked for their text");
    list.onWheel({.delta = 120, .ticks = 1}, {});
    TEST_ASSERT_EQ(list.getRowsInView().first, 43u, "the wheel scrolls by three rows");
    click(0);
    TEST_ASSERT(selected() == std::vector<u64>({rows.keys[43]}), "a click picks the row under it, scrolled or not");

    rows.keys.resize(2);
    click(3);
    TEST_ASSERT(list.getSelection().empty() && inView(0, 2), "a click under the last row drops the selection");
    list.stealFocus();

    Rows more;
    for(u64 key = 0; key < 20; key++) more.keys.push_back(key);
    PressedList late(more, {{"a", 0.f}}, "late");
    late.scrollTo(0);
    late.setSize(200.f, rowHeight * 6.f);
    TEST_ASSERT_EQ(late.getRowsInView().first, 0u,
                   "a row scrolled to before the list has a size shows once it has one");
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
    auto *field = new CBaseUIStyledTextbox("field");
    layer.open(field, {0, 0});
    field->visit();
    TEST_ASSERT(field->isFocused(), "a popup's field takes the focus from the layer, which took it as it opened");
    field->stealFocus();
    layer.tick();
    TEST_ASSERT(layer.isFocused(), "...and the layer has it again once the field lets it go");
    layer.closeAll();
    layer.tick();

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
    layer.tick();

    int changed = -1;
    CBaseUIDropdown dropdown(layer, {"Auto", "Normal", "Soft", "Drum"}, "dropdown");
    dropdown.setChangeCallback([&changed](int index) { changed = index; });
    dropdown.setPos(10, 20);
    dropdown.setSize(120, 28);
    TEST_ASSERT_EQ(dropdown.getChosen(), -1, "a dropdown starts with nothing chosen");
    dropdown.setChosen(2);
    TEST_ASSERT(dropdown.getChosen() == 2 && changed == -1, "choosing one in code doesn't call back");
    TEST_ASSERT_EQ(dropdown.getNaturalSize().y, 28.f, "a dropdown is as tall as a control");
    dropdown.open(false);
    const auto *list = static_cast<const CBaseUIMenu *>(layer.getElements().back());
    TEST_ASSERT(dropdown.isOpen() && list->getItems().size() == 4 && list->getItems()[2].checked &&
                    !list->getItems()[1].checked,
                "its menu lists the options, the chosen one checked");
    TEST_ASSERT(list->getPos() == vec2(10, 48), "...under it");
    press(KEY_DOWN);
    press(KEY_ENTER);
    TEST_ASSERT(changed == 0 && dropdown.getChosen() == 0 && !dropdown.isOpen(),
                "an option chosen from the menu is the chosen one and goes to the callback");
    layer.tick();
    dropdown.open(true);
    press(KEY_ESCAPE);
    TEST_ASSERT(!dropdown.isOpen() && changed == 0, "Escape closes it without a change");
    layer.tick();
    dropdown.setChosen(7);
    TEST_ASSERT_EQ(dropdown.getChosen(), -1, "an option it doesn't have is none");
}

void ToolUITest::testColourPickers() {
    TEST_SECTION("colour pickers");
    uiStyle().setScale(1.f);

    {
        CBaseUIColorPicker picker("picker");
        picker.setSize(picker.getNaturalSize());
        std::vector<Color> heard;
        picker.setChangeCallback([&](Color c) { heard.push_back(c); });
        const auto channel = [&](std::string_view name) {
            return static_cast<CBaseUINumberField *>(find(&picker, name));
        };
        auto *hex = static_cast<CBaseUIStyledTextbox *>(find(&picker, "picker_hex"));
        const auto press = [&](SCANCODE scancode, KEYMOD modifiers = KEYMOD_NONE) {
            KeyboardEvent e = key(scancode, modifiers);
            picker.onKeyDown(e);
            return e.isConsumed();
        };

        picker.setColor(rgb(0, 128, 255));
        TEST_ASSERT(channel("picker_r")->getValue() == 0.0 && channel("picker_g")->getValue() == 128.0 &&
                        channel("picker_b")->getValue() == 255.0 && hex->getText() == "#0080FF" && heard.empty(),
                    "a colour picker shows its colour's channels and hex code, and a colour set isn't a change");
        TEST_ASSERT(!picker.isAdjusting(), "...nor is it being set");

        channel("picker_r")->visit();
        type(channel("picker_r"), U"64");
        TEST_ASSERT(picker.isAdjusting() && heard.empty(), "what's typed into a channel isn't the colour yet");
        press(KEY_ENTER);
        TEST_ASSERT(picker.getColor() == rgb(64, 128, 255) && heard == std::vector<Color>({rgb(64, 128, 255)}) &&
                        hex->getText() == "#4080FF",
                    "Enter takes it: its user hears of the colour, and the hex code follows");
        hex->visit();
        type(hex, U"#fc0");
        press(KEY_ENTER);
        TEST_ASSERT(picker.getColor() == rgb(255, 204, 0) && hex->getText() == "#FFCC00" &&
                        channel("picker_g")->getValue() == 204.0 && heard.size() == 2,
                    "a hex code typed is the colour, a short one's digits doubled");
        hex->visit();
        type(hex, U"12zz");
        press(KEY_ENTER);
        TEST_ASSERT(picker.getColor() == rgb(255, 204, 0) && hex->getText() == "#FFCC00" && heard.size() == 2,
                    "what's no hex code changes nothing");
        picker.setColor(rgb(255, 204, 0));
        hex->visit();
        type(hex, U"000000");
        press(KEY_ENTER);
        TEST_ASSERT(picker.getColor() == rgb(0, 0, 0) && heard.size() == 3, "black is a colour");

        TEST_ASSERT(press(KEY_TAB) && channel("picker_r")->isFocused(), "Tab goes to the first of its fields");
        type(channel("picker_r"), U"9");
        press(KEY_TAB);
        TEST_ASSERT(channel("picker_g")->isFocused() && picker.getColor() == rgb(9, 0, 0),
                    "...and on to the next, taking what was typed");
        press(KEY_TAB, KEYMOD_LSHIFT);
        press(KEY_TAB, KEYMOD_LSHIFT);
        TEST_ASSERT(hex->isFocused(), "Shift+Tab goes back, from the first to the last");
        hex->stealFocus();
    }

    CBaseUIPopupLayer layer("layer");
    layer.setSize(600, 400);
    const auto press = [&layer](SCANCODE scancode) {
        KeyboardEvent e = key(scancode, KEYMOD_NONE);
        layer.onKeyDown(e);
    };
    {
        CBaseUIColorButton button(layer, "colour");
        std::vector<Color> heard;
        button.setChangeCallback([&](Color c) { heard.push_back(c); });
        button.setPos(10, 20);
        button.setSize(56, 28);
        button.setColor(rgb(10, 20, 30));
        TEST_ASSERT(button.getColor() == rgb(10, 20, 30) && !button.isOpen() && heard.empty() && button.getVisit() == 0,
                    "a colour button shows the colour it was given, closed");

        button.open();
        TEST_ASSERT(button.isOpen() && layer.getElements().back()->getPos() == vec2(10, 48),
                    "its picker opens under it");
        auto *red = static_cast<CBaseUINumberField *>(find(&layer, "colour_picker_r"));
        TEST_ASSERT(red && red->getValue() == 10.0, "...showing its colour");
        red->visit();
        type(red, U"200");
        TEST_ASSERT(button.isAdjusting(), "a field of its picker typed in, it's being set");
        press(KEY_ENTER);
        TEST_ASSERT(button.getColor() == rgb(200, 20, 30) && heard == std::vector<Color>({rgb(200, 20, 30)}),
                    "a colour set in its picker is its colour and goes to the callback");
        TEST_ASSERT(button.isOpen(), "...and the picker stays");
        button.setColor(rgb(1, 2, 3));
        TEST_ASSERT(red->getValue() == 1.0 && heard.size() == 1, "a colour given while it's open shows there too");
        const u32 visit = button.getVisit();
        layer.closeAll();
        TEST_ASSERT(!button.isOpen() && button.getColor() == rgb(1, 2, 3), "closed by the layer, it's closed");
        layer.tick();
        button.open();
        TEST_ASSERT(button.isOpen() && button.getVisit() != visit, "each opening is a visit of its own");
    }
    TEST_ASSERT(!layer.isOpen(), "a button that goes takes its picker along");
    layer.tick();
}

}  // namespace Mc::Tests
