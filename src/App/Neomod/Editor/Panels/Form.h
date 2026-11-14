#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"
#include "Color.h"
#include "Document.h"
#include "noinclude.h"
#include "types.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class CBaseUIColorButton;
class CBaseUIDropdown;
class CBaseUINumberField;
class CBaseUIPopupLayer;
class CBaseUIStyledButton;
class CBaseUIStyledTextbox;
struct EditorState;

// a titled column of labelled fields that show values from the shown map and write them as steps: what's typed into a
// field or dragged to is one step once the visit or the drag ends, and none if it ends where it started. every field
// has a function that reads its value from the document (asked each tick while the field isn't being used) and one
// that makes the edit for a new value; `enabled`, when given, says whether it can be set at all
class Form final : public CBaseUIBox {
    NOCOPY_NOMOVE(Form)
   public:
    using Action = Document::Action;
    using Change = Document::Change;
    using Enabled = std::function<bool(const Document &)>;

    // the choice menus open in `popups`, which outlives it
    Form(EditorState &state, CBaseUIPopupLayer &popups, std::string name, std::string title);
    ~Form() override;

    CBaseUIStyledTextbox *addText(const std::string &name, std::string label, Action action,
                                  std::function<std::string(const Document &)> read,
                                  std::function<void(Change &, std::string_view)> write, Enabled enabled = {});
    CBaseUINumberField *addNumber(const std::string &name, std::string label, Action action,
                                  std::function<f64(const Document &)> read, std::function<void(Change &, f64)> write,
                                  Enabled enabled = {});
    // a button that's checked while it's on
    CBaseUIStyledButton *addToggle(const std::string &name, std::string label, Action action,
                                   std::function<bool(const Document &)> read,
                                   std::function<void(Change &, bool)> write, Enabled enabled = {});
    // `read` gives the option index (-1: none of them)
    CBaseUIDropdown *addChoice(const std::string &name, std::string label, std::vector<std::string> options,
                               Action action, std::function<int(const Document &)> read,
                               std::function<void(Change &, int)> write, Enabled enabled = {});

    // a button that makes `edit` a step
    CBaseUIStyledButton *addButton(const std::string &name, std::string label, Action action,
                                   std::function<void(Change &)> edit, Enabled enabled = {});
    // the same field and button without a row of their own, for the caller to put somewhere in the form (addRow() makes
    // a labelled row of anything)
    CBaseUINumberField *makeNumber(const std::string &name, Action action, std::function<f64(const Document &)> read,
                                   std::function<void(Change &, f64)> write, Enabled enabled = {});
    CBaseUIStyledButton *makeButton(const std::string &name, std::string label, Action action,
                                    std::function<void(Change &)> edit, Enabled enabled = {});
    // a colour to set in a picker: one visit of the picker is one step
    CBaseUIColorButton *makeColour(const std::string &name, Action action, std::function<Color(const Document &)> read,
                                   std::function<void(Change &, Color)> write, Enabled enabled = {});
    void addRow(std::string label, CBaseUIElement *field, BoxItem item);

    void tick() override;
    // what's being typed into one of its fields becomes a step now (the field keeps the focus)
    void commit();
    // the document values into the fields that aren't being used, and which of them can be set
    void refresh();

    // goes to the field after the focused one in these forms (backwards: before it) that can be typed in, to the first
    // (last) one when none has the focus; false without any
    static bool visitNext(std::span<Form *const> forms, bool backwards);

   private:
    struct Row {
        CBaseUIElement *field;
        std::function<void(const Document &)> show;  // the document value into the field
        std::function<bool()> used;                  // typed in or dragged
        Enabled enabled;
    };

    EditorState &state;
    CBaseUIPopupLayer &popups;
    std::vector<Row> rows;
    std::vector<CBaseUIStyledTextbox *> fields;  // the ones to type in, in order
};
