// Copyright (c) 2026, WH, All rights reserved.
#include "Form.h"

#include "CBaseUIColorButton.h"
#include "CBaseUIDropdown.h"
#include "CBaseUINumberField.h"
#include "CBaseUIStyledButton.h"
#include "CBaseUIStyledLabel.h"
#include "CBaseUIStyledTextbox.h"
#include "ContainerRanges.h"
#include "EditorState.h"
#include "UIStyle.h"

#include <algorithm>
#include <utility>

namespace {
// units
constexpr float LABEL_WIDTH{170.f};
}  // namespace

Form::Form(EditorState &state, CBaseUIPopupLayer &popups, std::string name, std::string title)
    : CBaseUIBox(Axis::COLUMN, std::move(name)), state(state), popups(popups) {
    this->setGap(uiStyle().metrics.gap);
    auto *heading = new CBaseUIStyledLabel(std::move(title));
    heading->setDim(true);
    this->add(heading, BoxItem::fit());
}

Form::~Form() = default;

void Form::addRow(std::string label, CBaseUIElement *field, BoxItem item) {
    auto *line = new CBaseUIBox(Axis::ROW);
    line->setGap(uiStyle().metrics.gap);
    line->add(new CBaseUIStyledLabel(std::move(label)), BoxItem::fixed(LABEL_WIDTH))->add(field, item);
    this->add(line, BoxItem::fit());
}

CBaseUIStyledTextbox *Form::addText(const std::string &name, std::string label, Action action,
                                    std::function<std::string(const Document &)> read,
                                    std::function<void(Change &, std::string_view)> write, Enabled enabled) {
    auto *field = new CBaseUIStyledTextbox(name);
    field->setChangeCallback([this, action, write = std::move(write)](std::string_view text) {
        this->state.edit(action, [&](Change &c) { write(c, text); });
    });
    this->fields.push_back(field);
    this->rows.push_back({.field = field,
                          .show =
                              [field, read = std::move(read)](const Document &document) {
                                  if(std::string value = read(document); value != field->getText()) {
                                      field->setText(std::move(value));
                                  }
                              },
                          .used = [field] { return field->isFocused(); },
                          .enabled = std::move(enabled)});
    this->addRow(std::move(label), field, BoxItem::flex());
    return field;
}

CBaseUINumberField *Form::makeNumber(const std::string &name, Action action, std::function<f64(const Document &)> read,
                                     std::function<void(Change &, f64)> write, Enabled enabled) {
    auto *field = new CBaseUINumberField(name);
    field->setChangeCallback([this, action, write = std::move(write)](f64 value) {
        this->state.edit(action, [&](Change &c) { write(c, value); });
    });
    this->fields.push_back(field);
    this->rows.push_back(
        {.field = field,
         .show = [field, read = std::move(read)](const Document &document) { field->setValue(read(document)); },
         .used = [field] { return field->isFocused() || field->isAdjusting(); },
         .enabled = std::move(enabled)});
    return field;
}

CBaseUINumberField *Form::addNumber(const std::string &name, std::string label, Action action,
                                    std::function<f64(const Document &)> read, std::function<void(Change &, f64)> write,
                                    Enabled enabled) {
    CBaseUINumberField *field = this->makeNumber(name, action, std::move(read), std::move(write), std::move(enabled));
    this->addRow(std::move(label), field, BoxItem::flex());
    return field;
}

CBaseUIStyledButton *Form::addToggle(const std::string &name, std::string label, Action action,
                                     std::function<bool(const Document &)> read,
                                     std::function<void(Change &, bool)> write, Enabled enabled) {
    auto *button = new CBaseUIStyledButton(std::move(label), name);
    button->setClickCallback([this, button, action, write = std::move(write)] {
        this->state.edit(action, [&](Change &c) { write(c, !button->isChecked()); });
    });
    this->add(button, BoxItem::fit());
    this->rows.push_back(
        {.field = button,
         .show = [button, read = std::move(read)](const Document &document) { button->setChecked(read(document)); },
         .used = [] { return false; },
         .enabled = std::move(enabled)});
    return button;
}

CBaseUIDropdown *Form::addChoice(const std::string &name, std::string label, std::vector<std::string> options,
                                 Action action, std::function<int(const Document &)> read,
                                 std::function<void(Change &, int)> write, Enabled enabled) {
    auto *dropdown = new CBaseUIDropdown(this->popups, std::move(options), name);
    dropdown->setChangeCallback([this, action, write = std::move(write)](int index) {
        this->state.edit(action, [&](Change &c) { write(c, index); });
    });
    this->rows.push_back(
        {.field = dropdown,
         .show = [dropdown, read = std::move(read)](const Document &document) { dropdown->setChosen(read(document)); },
         .used = [] { return false; },
         .enabled = std::move(enabled)});
    this->addRow(std::move(label), dropdown, BoxItem::flex());
    return dropdown;
}

CBaseUIStyledButton *Form::makeButton(const std::string &name, std::string label, Action action,
                                      std::function<void(Change &)> edit, Enabled enabled) {
    auto *button = new CBaseUIStyledButton(std::move(label), name);
    button->setClickCallback([this, action, edit = std::move(edit)] { this->state.edit(action, edit); });
    this->rows.push_back({.field = button,
                          .show = [](const Document &) {},
                          .used = [] { return false; },
                          .enabled = std::move(enabled)});
    return button;
}

CBaseUIColorButton *Form::makeColour(const std::string &name, Action action,
                                     std::function<Color(const Document &)> read,
                                     std::function<void(Change &, Color)> write, Enabled enabled) {
    auto *button = new CBaseUIColorButton(this->popups, name);
    button->setChangeCallback(
        [this, button, action, write = std::move(write), visit = u32{0}, gesture = u64{0}](Color colour) mutable {
            if(visit != button->getVisit()) {
                visit = button->getVisit();
                gesture = this->state.newGesture();
            }
            this->state.edit(action, [&](Change &c) { write(c, colour); }, gesture);
        });
    this->rows.push_back(
        {.field = button,
         .show = [button, read = std::move(read)](const Document &document) { button->setColor(read(document)); },
         .used = [button] { return button->isAdjusting(); },
         .enabled = std::move(enabled)});
    return button;
}

CBaseUIStyledButton *Form::addButton(const std::string &name, std::string label, Action action,
                                     std::function<void(Change &)> edit, Enabled enabled) {
    CBaseUIStyledButton *button = this->makeButton(name, std::move(label), action, std::move(edit), std::move(enabled));
    this->add(button, BoxItem::fit());
    return button;
}

void Form::refresh() {
    const Document *document = this->state.document.get();
    for(Row &row : this->rows) {
        if(document && !row.used()) row.show(*document);
        row.field->setEnabled(document && (!row.enabled || row.enabled(*document)));
    }
}

void Form::tick() {
    this->refresh();
    CBaseUIBox::tick();
}

void Form::commit() {
    for(CBaseUIStyledTextbox *field : this->fields) field->commit();
}

bool Form::visitNext(std::span<Form *const> forms, bool backwards) {
    // what the focused one has typed may change which of them can be typed in
    for(Form *form : forms) form->commit();
    for(Form *form : forms) form->refresh();

    std::vector<CBaseUIStyledTextbox *> all;
    for(Form *form : forms) {
        if(!form->isVisible()) continue;
        for(CBaseUIStyledTextbox *field : form->fields) {
            if(field->isEnabled() && field->isVisible()) all.push_back(field);
        }
    }
    if(all.empty()) return false;
    if(backwards) std::reverse(all.begin(), all.end());

    const auto focused = Mc::ranges::find_if(all, [](CBaseUIStyledTextbox *field) { return field->isFocused(); });
    all[focused == all.end() ? 0 : (uSz)(focused - all.begin() + 1) % all.size()]->visit();
    return true;
}
