// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIColorButton.h"

#include "CBaseUIBox.h"
#include "CBaseUIColorPicker.h"
#include "CBaseUIPopupLayer.h"
#include "Graphics.h"
#include "UIStyle.h"

#include "fmt/format.h"

#include <utility>

namespace {
// the picker on a popup surface
class Popup final : public CBaseUIBox {
    NOCOPY_NOMOVE(Popup)
   public:
    explicit Popup(std::string name) : CBaseUIBox(Axis::COLUMN, std::move(name)) {
        // a press on its surface stays with it
        this->bClickThroughSelf = false;
        this->setPadding(uiStyle().metrics.padding);
    }
    ~Popup() override = default;

    void draw() override {
        uiStyle().drawPopup(this->getRect());
        CBaseUIBox::draw();
    }
};
}  // namespace

CBaseUIColorButton::CBaseUIColorButton(CBaseUIPopupLayer &layer, std::string name)
    : CBaseUIElement(0, 0, 0, 0, std::move(name)), layer(layer) {}

CBaseUIColorButton::~CBaseUIColorButton() {
    // what its picker would still say goes nowhere
    if(this->isOpen()) this->picker->setChangeCallback({});
    this->close();
}

CBaseUIColorButton *CBaseUIColorButton::setColor(Color c) {
    c.a = 255;
    this->colour = c;
    if(this->isOpen()) this->picker->setColor(c);
    return this;
}

Color CBaseUIColorButton::getColor() const { return this->isOpen() ? this->picker->getColor() : this->colour; }

CBaseUIColorButton *CBaseUIColorButton::setChangeCallback(std::function<void(Color)> callback) {
    this->changeCallback = std::move(callback);
    return this;
}

void CBaseUIColorButton::open() {
    this->close();
    this->picker = new CBaseUIColorPicker(fmt::format("{}_picker", this->getName()));
    this->picker->setColor(this->colour)->setReference(this->colour)->setChangeCallback([this](Color c) {
        this->colour = c;
        if(this->changeCallback) this->changeCallback(c);
    });
    auto *popup = new Popup(fmt::format("{}_popup", this->getName()));
    popup->add(this->picker, BoxItem::fit());
    // a press on the control still reaches it, which closes the picker
    const McRect &rect = this->getRect();
    this->popupId = this->layer.open(popup, {rect.getX(), rect.getY() + rect.getHeight()}, rect);
}

void CBaseUIColorButton::close() {
    if(this->isOpen()) this->layer.close(this->popupId);
}

bool CBaseUIColorButton::isOpen() const { return this->popupId != 0 && this->layer.isOpen(this->popupId); }

bool CBaseUIColorButton::isAdjusting() const { return this->isOpen() && this->picker->isAdjusting(); }

void CBaseUIColorButton::draw() {
    if(!this->isVisible()) return;

    using enum UIStyle::ControlState;
    const UIStyle::ControlState state = !this->isEnabled()   ? DISABLED
                                        : this->isOpen()     ? PRESSED
                                        : this->bMouseInside ? HOVERED
                                                             : NORMAL;
    const UIStyle &style = uiStyle();
    const McRect &rect = this->getRect();
    style.drawControl(rect, state);
    const float inset = style.px(3.f);
    g->setColor(this->getColor());
    g->fillRect(McRect{rect.getX() + inset, rect.getY() + inset, rect.getWidth() - 2.f * inset,
                       rect.getHeight() - 2.f * inset});
}

vec2 CBaseUIColorButton::getNaturalSize() {
    const UIStyle &style = uiStyle();
    return {2.f * style.px(style.metrics.controlHeight), style.px(style.metrics.controlHeight)};
}

void CBaseUIColorButton::onMouseDownInside(bool left, bool right) {
    CBaseUIElement::onMouseDownInside(left, right);
    if(!left) return;
    if(this->isOpen()) {
        this->close();
    } else {
        this->open();
    }
}
