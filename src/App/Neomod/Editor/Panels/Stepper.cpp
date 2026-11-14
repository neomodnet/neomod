// Copyright (c) 2026, WH, All rights reserved.
#include "Stepper.h"

#include "CBaseUIStyledButton.h"
#include "CBaseUIStyledLabel.h"
#include "UIStyle.h"

#include <utility>

Stepper::Stepper(const std::string &name, std::function<void(i32)> step, std::function<std::string()> text)
    : CBaseUIBox(Axis::ROW, name), text(std::move(text)) {
    this->setGap(uiStyle().metrics.gap);

    auto *previous = new CBaseUIStyledButton("<", name + "_previous");
    previous->setClickCallback([step] { step(-1); });
    auto *next = new CBaseUIStyledButton(">", name + "_next");
    next->setClickCallback([step] { step(1); });
    this->label = new CBaseUIStyledLabel({}, name + "_value");
    this->label->setJustification(TEXT_JUSTIFICATION::CENTERED);
    this->add(previous, BoxItem::fit())->add(this->label, BoxItem::flex())->add(next, BoxItem::fit());
}

Stepper::~Stepper() = default;

void Stepper::tick() {
    this->label->setText(this->text());
    CBaseUIBox::tick();
}
