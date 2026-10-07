// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIMenuBar.h"

#include "CBaseUIPopupLayer.h"
#include "Font.h"
#include "UIStyle.h"

#include <utility>

// a menu's title: opens and closes it
class CBaseUIMenuBar::Title final : public CBaseUIElement {
    NOCOPY_NOMOVE(Title)
   public:
    Title(CBaseUIMenuBar &bar, int index, std::string name, std::string label)
        : CBaseUIElement(0, 0, 0, 0, std::move(name)), bar(bar), index(index), label(std::move(label)) {}
    ~Title() override = default;

    void draw() override {
        using enum UIStyle::ControlState;
        const UIStyle::ControlState state = this->bar.openIndex == this->index ? PRESSED
                                            : this->bMouseInside               ? HOVERED
                                                                               : NORMAL;
        const UIStyle &style = uiStyle();
        style.drawRow(this->getRect(), state);
        style.drawText(this->getRect(), this->label, TEXT_JUSTIFICATION::CENTERED, state);
    }
    [[nodiscard]] vec2 getNaturalSize() override {
        const UIStyle &style = uiStyle();
        return {style.font()->getStringWidth(this->label) + 2.f * style.px(style.metrics.controlPadding),
                style.px(style.metrics.controlHeight)};
    }

   protected:
    void onMouseDownInside(bool left, bool /*right*/) override {
        if(!left) return;
        if(this->bar.openIndex == this->index) {
            this->bar.close();
        } else {
            this->bar.open(this->index, false);
        }
    }
    void onMouseInside() override {
        if(this->bar.openIndex >= 0 && this->bar.openIndex != this->index) this->bar.open(this->index, false);
    }

   private:
    CBaseUIMenuBar &bar;
    int index;
    std::string label;
};

CBaseUIMenuBar::CBaseUIMenuBar(CBaseUIPopupLayer &layer, std::string name)
    : CBaseUIBox(Axis::ROW, std::move(name)), layer(layer) {}

CBaseUIMenuBar::~CBaseUIMenuBar() { this->close(); }

CBaseUIMenuBar *CBaseUIMenuBar::add(std::string name, std::string label, std::function<std::vector<MenuItem>()> items) {
    auto *title = new Title(*this, (int)this->titles.size(), std::move(name), std::move(label));
    this->titles.push_back(title);
    this->menus.push_back(std::move(items));
    CBaseUIBox::add(title, BoxItem::fit());
    return this;
}

void CBaseUIMenuBar::tick() {
    // (closed by a choice, a click beside it or Escape)
    if(this->openIndex >= 0 && !this->layer.isOpen(this->menuId)) this->openIndex = -1;
    CBaseUIBox::tick();
}

void CBaseUIMenuBar::open(int index, bool keyboard) {
    this->close();
    const int count = (int)this->titles.size();
    auto *menu =
        new CBaseUIMenu(this->layer, this->menus[index](), std::string{this->titles[index]->getName()} + "_menu");
    menu->setSideways([this, index, count](int direction) { this->open((index + direction + count) % count, true); });

    // the titles still take presses and hover while it's open, so that another one can be opened
    McRect titles = this->titles.front()->getRect();
    titles = titles.Union(this->titles.back()->getRect());
    const McRect &title = this->titles[index]->getRect();
    this->menuId = menu->open({title.getX(), title.getY() + title.getHeight()}, titles, keyboard);
    this->openIndex = index;
}

void CBaseUIMenuBar::close() {
    if(this->openIndex < 0) return;
    this->layer.close(this->menuId);
    this->openIndex = -1;
}
