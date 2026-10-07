// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIDropdown.h"

#include "CBaseUIMenu.h"
#include "CBaseUIPopupLayer.h"
#include "Font.h"
#include "UIStyle.h"

#include "fmt/format.h"

#include <algorithm>
#include <utility>

CBaseUIDropdown::CBaseUIDropdown(CBaseUIPopupLayer &layer, std::vector<std::string> options, std::string name)
    : CBaseUIElement(0, 0, 0, 0, std::move(name)), layer(layer), options(std::move(options)) {}

CBaseUIDropdown::~CBaseUIDropdown() { this->close(); }

CBaseUIDropdown *CBaseUIDropdown::setChosen(int index) {
    this->chosen = index >= 0 && index < (int)this->options.size() ? index : -1;
    return this;
}

CBaseUIDropdown *CBaseUIDropdown::setChangeCallback(std::function<void(int)> callback) {
    this->changeCallback = std::move(callback);
    return this;
}

void CBaseUIDropdown::open(bool keyboard) {
    this->close();
    std::vector<MenuItem> items;
    for(int i = 0; i < (int)this->options.size(); i++) {
        items.push_back({.name = fmt::format("{}_option_{}", this->getName(), i),
                         .label = this->options[i],
                         .action =
                             [this, i] {
                                 this->chosen = i;
                                 if(this->changeCallback) this->changeCallback(i);
                             },
                         .checked = i == this->chosen});
    }
    auto *menu = new CBaseUIMenu(this->layer, std::move(items), fmt::format("{}_menu", this->getName()));
    // (a press on the control still reaches it, which closes the menu)
    const McRect &rect = this->getRect();
    this->menuId = menu->open({rect.getX(), rect.getY() + rect.getHeight()}, rect, keyboard);
}

void CBaseUIDropdown::close() {
    if(this->isOpen()) this->layer.close(this->menuId);
}

bool CBaseUIDropdown::isOpen() const { return this->menuId != 0 && this->layer.isOpen(this->menuId); }

void CBaseUIDropdown::draw() {
    if(!this->isVisible()) return;

    using enum UIStyle::ControlState;
    const UIStyle::ControlState state = !this->isEnabled()   ? DISABLED
                                        : this->isOpen()     ? PRESSED
                                        : this->bMouseInside ? HOVERED
                                                             : NORMAL;
    const UIStyle &style = uiStyle();
    const McRect &rect = this->getRect();
    const float padding = style.px(style.metrics.controlPadding);
    const float mark = style.px(style.metrics.markWidth);
    style.drawControl(rect, state);
    if(this->chosen >= 0) {
        style.drawText({rect.getX() + padding, rect.getY(), rect.getWidth() - padding - mark, rect.getHeight()},
                       this->options[this->chosen], TEXT_JUSTIFICATION::LEFT, state);
    }
    style.drawMark({rect.getX() + rect.getWidth() - mark, rect.getY(), mark, rect.getHeight()}, UIStyle::Mark::DROPDOWN,
                   state);
}

vec2 CBaseUIDropdown::getNaturalSize() {
    const UIStyle &style = uiStyle();
    float widest = 0.f;
    for(const std::string &option : this->options) widest = std::max(widest, style.font()->getStringWidth(option));
    return {widest + style.px(style.metrics.controlPadding) + style.px(style.metrics.markWidth),
            style.px(style.metrics.controlHeight)};
}

void CBaseUIDropdown::onMouseDownInside(bool left, bool /*right*/) {
    if(!left) return;
    if(this->isOpen()) {
        this->close();
    } else {
        this->open(false);
    }
}
