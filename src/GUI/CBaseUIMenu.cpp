// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIMenu.h"

#include "CBaseUIPopupLayer.h"
#include "Font.h"
#include "KeyBindings.h"
#include "KeyboardEvent.h"
#include "UIStyle.h"

#include <algorithm>
#include <utility>

// one item: highlighted while the mouse is over it, chosen when clicked
class CBaseUIMenu::Row final : public CBaseUIElement {
    NOCOPY_NOMOVE(Row)
   public:
    Row(CBaseUIMenu &menu, int index, std::string name)
        : CBaseUIElement(0, 0, 0, 0, std::move(name)), menu(menu), index(index) {}
    ~Row() override = default;

    void draw() override {
        const MenuItem &item = this->menu.items[this->index];
        const UIStyle &style = uiStyle();
        const McRect &r = this->getRect();
        if(item.separator) {
            style.drawSeparator(r);
            return;
        }

        using enum UIStyle::ControlState;
        const UIStyle::ControlState state = !item.enabled                           ? DISABLED
                                            : this->menu.highlighted == this->index ? HOVERED
                                                                                    : NORMAL;
        style.drawRow(r, state);
        const float mark = style.px(style.metrics.markWidth);
        if(item.checked) style.drawMark({r.getX(), r.getY(), mark, r.getHeight()}, UIStyle::Mark::CHECK, state);
        const McRect text{r.getX() + mark, r.getY(), r.getWidth() - 2.f * mark, r.getHeight()};
        style.drawText(text, item.label, TEXT_JUSTIFICATION::LEFT, state);
        if(!item.shortcut.empty()) style.drawText(text, item.shortcut, TEXT_JUSTIFICATION::RIGHT, state, true);
        if(item.submenu) {
            style.drawMark({r.getX() + r.getWidth() - mark, r.getY(), mark, r.getHeight()}, UIStyle::Mark::SUBMENU,
                           state);
        }
    }

   protected:
    void onMouseInside() override {
        const MenuItem &item = this->menu.items[this->index];
        if(item.separator || !item.enabled) {
            this->menu.highlighted = -1;
            return;
        }
        this->menu.highlight(this->index);
        if(item.submenu) {
            this->menu.openSubmenu(this->index, false);
        } else {
            this->menu.closeSubmenu();
        }
    }
    void onMouseOutside() override {
        // (an open submenu keeps its row highlighted)
        if(this->menu.highlighted == this->index && this->menu.submenuIndex != this->index) {
            this->menu.highlighted = -1;
        }
    }
    void onMouseUpInside(bool left, bool /*right*/) override {
        if(left) this->menu.choose(this->index, false);
    }

   private:
    CBaseUIMenu &menu;
    int index;
};

CBaseUIMenu::CBaseUIMenu(CBaseUIPopupLayer &layer, std::vector<MenuItem> items, std::string name)
    : CBaseUIContainer(0, 0, 0, 0, std::move(name)), layer(layer), items(std::move(items)) {
    // (a click between its rows ends here, not on the layer, which would close it)
    this->bClickThroughSelf = false;
    for(int i = 0; i < (int)this->items.size(); i++) {
        auto *row = new Row(*this, i, this->items[i].name);
        this->rows.push_back(row);
        this->addBaseUIElement(row);
    }
}

CBaseUIMenu::~CBaseUIMenu() = default;

u32 CBaseUIMenu::open(vec2 pos, const McRect &passThrough, bool keyboard) {
    // the rows, at the natural size the layer gives the menu
    const UIStyle &style = uiStyle();
    const float pad = style.px(style.metrics.popupPadding);
    const float width = this->getNaturalSize().x - 2.f * pad;
    float y = pad;
    for(int i = 0; i < (int)this->rows.size(); i++) {
        const float height =
            style.px(this->items[i].separator ? style.metrics.separatorHeight : style.metrics.rowHeight);
        this->rows[i]->setRelPos(pad, y);
        this->rows[i]->setSize(width, height);
        y += height;
    }
    if(keyboard) this->highlight(this->nextChoosable(-1, 1));
    this->id = this->layer.open(this, pos, passThrough);
    return this->id;
}

CBaseUIMenu *CBaseUIMenu::setSideways(std::function<void(int)> sideways) {
    this->sideways = std::move(sideways);
    return this;
}

vec2 CBaseUIMenu::getNaturalSize() {
    const UIStyle &style = uiStyle();
    const McFont *font = style.font();
    float width = style.px(140.f);
    float height = 0.f;
    for(const MenuItem &item : this->items) {
        height += style.px(item.separator ? style.metrics.separatorHeight : style.metrics.rowHeight);
        if(item.separator) continue;
        const float shortcut =
            item.shortcut.empty() ? 0.f : style.px(3.f * style.metrics.gap) + font->getStringWidth(item.shortcut);
        width = std::max(width, 2.f * style.px(style.metrics.markWidth) + font->getStringWidth(item.label) + shortcut);
    }
    const float pad = style.px(style.metrics.popupPadding);
    return {std::round(width + 2.f * pad), std::round(height + 2.f * pad)};
}

void CBaseUIMenu::draw() {
    if(!this->isVisible()) return;
    uiStyle().drawPopup(this->getRect());
    CBaseUIContainer::draw();
}

void CBaseUIMenu::onKeyDown(KeyboardEvent &e) {
    const bool onSubmenu = this->highlighted >= 0 && this->items[this->highlighted].submenu;
    switch(e.getScanCode()) {
        case KEY_UP:
        case KEY_DOWN:
            if(const int next = this->nextChoosable(this->highlighted, e == KEY_DOWN ? 1 : -1); next >= 0) {
                this->highlight(next);
            }
            break;
        case KEY_RIGHT:
            if(onSubmenu) {
                this->openSubmenu(this->highlighted, true);
            } else if(this->sideways) {
                this->sideways(1);
            }
            break;
        case KEY_LEFT:
            if(this->parent) {
                this->parent->closeSubmenu();
            } else if(this->sideways) {
                this->sideways(-1);
            }
            break;
        case KEY_ENTER:
        case KEY_NUMPAD_ENTER:
        case KEY_SPACE:
            this->choose(this->highlighted, true);
            break;
        default:
            return;
    }
    e.consume();
}

int CBaseUIMenu::nextChoosable(int from, int direction) const {
    const int count = (int)this->items.size();
    for(int step = 1; step <= count; step++) {
        const int i =
            from < 0 ? (direction > 0 ? step - 1 : count - step) : ((from + direction * step) % count + count) % count;
        if(!this->items[i].separator && this->items[i].enabled) return i;
    }
    return -1;
}

void CBaseUIMenu::highlight(int index) { this->highlighted = index; }

void CBaseUIMenu::choose(int index, bool keyboard) {
    if(index < 0 || index >= (int)this->items.size()) return;
    const MenuItem &item = this->items[index];
    if(item.separator || !item.enabled) return;
    if(item.submenu) {
        this->openSubmenu(index, keyboard);
        return;
    }
    // (the menus stay alive until the layer's next tick, so this one can still be touched afterwards)
    const std::function<void()> action = item.action;
    this->layer.closeAll();
    if(action) action();
}

void CBaseUIMenu::openSubmenu(int index, bool keyboard) {
    if(this->submenuIndex != index || !this->layer.isOpen(this->submenuId)) {
        this->closeSubmenu();
        const MenuItem &item = this->items[index];
        this->submenu = new CBaseUIMenu(this->layer, item.submenu(), item.name + "_menu");
        this->submenu->parent = this;
        const float pad = uiStyle().px(uiStyle().metrics.popupPadding);
        this->submenuId = this->submenu->open(
            {this->getPos().x + this->getSize().x, this->rows[index]->getPos().y - pad}, {}, keyboard);
        this->submenuIndex = index;
    } else if(keyboard) {
        this->submenu->highlight(this->submenu->nextChoosable(-1, 1));
    }
}

void CBaseUIMenu::closeSubmenu() {
    if(this->submenuId != 0) this->layer.close(this->submenuId);
    this->submenuId = 0;
    this->submenuIndex = -1;
}
