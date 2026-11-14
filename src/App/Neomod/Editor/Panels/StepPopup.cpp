// Copyright (c) 2026, WH, All rights reserved.
#include "StepPopup.h"

#include "CBaseUINumberField.h"
#include "CBaseUIPopupLayer.h"
#include "CBaseUIStyledButton.h"
#include "CBaseUIStyledLabel.h"
#include "EditorState.h"
#include "KeyBindings.h"
#include "KeyboardEvent.h"
#include "UIStyle.h"
#include "i18n.h"

#include "fmt/format.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

StepPopup::StepPopup(EditorState &state, CBaseUIPopupLayer &popups, std::string name, std::string title, Show show)
    : CBaseUIBox(Axis::COLUMN, std::move(name)), state(state), popups(popups), show(std::move(show)) {
    const UIStyle &style = uiStyle();
    // a press on its surface stays with it
    this->bClickThroughSelf = false;
    this->setPadding(style.metrics.padding * 2.f);
    this->setGap(style.metrics.gap);
    auto *heading = new CBaseUIStyledLabel(std::move(title));
    heading->setDim(true);
    this->rows = new CBaseUIBox(Axis::COLUMN);
    this->rows->setGap(style.metrics.gap);
    this->apply = new CBaseUIStyledButton(_("Apply"), std::string{this->getName()} + "_apply");
    this->apply->setClickCallback([this] { this->close(true); });
    auto *cancel = new CBaseUIStyledButton(_("Cancel"), std::string{this->getName()} + "_cancel");
    cancel->setClickCallback([this] { this->close(false); });
    auto *buttons = new CBaseUIBox(Axis::ROW);
    buttons->setGap(style.metrics.gap);
    buttons->add(this->apply, BoxItem::flex())->add(cancel, BoxItem::flex());
    this->add(heading, BoxItem::fit())->add(this->rows, BoxItem::fit())->add(buttons, BoxItem::fit());
}

StepPopup::~StepPopup() = default;

void StepPopup::addRow(std::string label, CBaseUIElement *control) {
    auto *line = new CBaseUIBox(Axis::ROW);
    line->setGap(uiStyle().metrics.gap);
    this->labels.push_back(new CBaseUIStyledLabel(std::move(label)));
    line->add(this->labels.back(), BoxItem::fit())->add(control, BoxItem::flex());
    this->rows->add(line, BoxItem::fit());
}

CBaseUINumberField *StepPopup::addNumber(const std::string &name, std::string label, f64 value, f64 min, f64 max,
                                         f64 step, int decimals) {
    auto *field = new CBaseUINumberField(name);
    field->setRange(min, max)->setStep(step, step)->setDecimals(decimals)->setValue(value);
    this->numbers.push_back(field);
    this->addRow(std::move(label), field);
    return field;
}

void StepPopup::addChoice(const std::string &name, std::string label, std::vector<std::string> options, i32 chosen) {
    const uSz index = this->choices.size();
    Choice &choice = this->choices.emplace_back(Choice{.buttons = {}, .chosen = chosen});
    auto *line = new CBaseUIBox(Axis::ROW);
    line->setGap(uiStyle().metrics.gap);
    for(uSz i = 0; i < options.size(); i++) {
        auto *button = new CBaseUIStyledButton(std::move(options[i]), fmt::format("{}_{}", name, i));
        button->setChecked((i32)i == chosen);
        button->setClickCallback([this, index, i] {
            Choice &c = this->choices[index];
            c.chosen = (i32)i;
            for(uSz j = 0; j < c.buttons.size(); j++) c.buttons[j]->setChecked(j == i);
        });
        choice.buttons.push_back(button);
        line->add(button, BoxItem::flex());
    }
    this->addRow(std::move(label), line);
}

void StepPopup::addToggle(const std::string &name, std::string label, bool on) {
    auto *button = new CBaseUIStyledButton(std::move(label), name);
    button->setChecked(on);
    button->setClickCallback([button] { button->setChecked(!button->isChecked()); });
    this->toggles.push_back(button);
    this->addRow({}, button);
}

void StepPopup::open(Document::Action action, vec2 pos) {
    // one column as wide as the widest label
    f32 width = 0.f;
    for(CBaseUIStyledLabel *label : this->labels) width = std::max(width, label->getNaturalSize().x);
    for(CBaseUIStyledLabel *label : this->labels) label->setBoxItem(BoxItem::fixed(width / uiStyle().getScale()));
    this->state.compose.startPreview(action);
    this->update();
    this->id = this->popups.open(this, pos);
    if(!this->numbers.empty()) this->numbers.front()->visit();
}

f64 StepPopup::number(uSz i) const { return this->numbers[i]->getValue(); }
i32 StepPopup::choice(uSz i) const { return this->choices[i].chosen; }
bool StepPopup::toggle(uSz i) const { return this->toggles[i]->isChecked(); }

void StepPopup::update() {
    std::vector<f64> values;
    values.reserve(this->numbers.size() + this->choices.size() + this->toggles.size());
    for(const CBaseUINumberField *field : this->numbers) values.push_back(field->getValue());
    for(const Choice &choice : this->choices) values.push_back(choice.chosen);
    for(const CBaseUIStyledButton *toggle : this->toggles) values.push_back(toggle->isChecked() ? 1.0 : 0.0);
    if(values == this->shown && this->id != 0) return;
    this->shown = std::move(values);
    this->apply->setEnabled(this->show(*this));
}

void StepPopup::close(bool keep) {
    if(!this->popups.isOpen(this->id)) return;
    this->state.compose.endPreview(keep);
    this->popups.close(this->id);
}

void StepPopup::tick() {
    CBaseUIBox::tick();
    if(this->popups.isOpen(this->id)) this->update();
}

void StepPopup::draw() {
    uiStyle().drawPopup(this->getRect());
    CBaseUIBox::draw();
}

void StepPopup::onKeyDown(KeyboardEvent &e) {
    if(e == KEY_ESCAPE) {
        e.consume();
        this->close(false);
        return;
    }
    if(e == KEY_ENTER || e == KEY_NUMPAD_ENTER) {
        e.consume();
        // what's typed counts
        for(CBaseUINumberField *field : this->numbers) {
            if(field->isFocused()) field->commit();
        }
        this->update();
        if(this->apply->isEnabled()) this->close(true);
        return;
    }
    CBaseUIBox::onKeyDown(e);
}
