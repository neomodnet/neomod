// Copyright (c) 2025, kiwec, All rights reserved.
#include "UIButtonWithIcon.h"
#include "CBaseUILabel.h"
#include "Graphics.h"
#include "Osu.h"

UIButtonWithIcon::UIButtonWithIcon(const UString& text, char16_t icon) : CBaseUIContainer(0, 0, 0, 0, "") {
    UString iconString;
    iconString.insert(0, icon);
    this->icon = new CBaseUILabel(0, 0, 0, 0, "", iconString);
    this->icon->setDrawBackground(false);
    this->icon->setDrawFrame(false);
    this->icon->setDrawTextShadow(true);
    this->addBaseUIElement(this->icon);

    this->text = new CBaseUILabel(0, 0, 0, 0, "", text);
    this->text->setDrawBackground(false);
    this->text->setDrawFrame(false);
    this->text->setDrawTextShadow(true);
    this->addBaseUIElement(this->text);

    this->onResized();
}

void UIButtonWithIcon::draw() {
    CBaseUIContainer::draw();

    // draw frame when hovered
    if(this->isMouseInside()) {
        g->drawRect(this->getPos(), this->getSize());
    }
}

void UIButtonWithIcon::onResized() {
    this->icon->setFont(osu->getFontIcons());  // calls onResized()
    this->icon->setSizeToContent();
    this->text->onResized();
    this->text->setSizeToContent();

    const f32 dpiScale = Osu::getUIScale();
    const f32 inner_margin = 4.f * dpiScale;
    const f32 btn_width = this->icon->getSize().x + inner_margin + this->text->getSize().x;
    const f32 btn_height = std::max(this->icon->getSize().y, this->text->getSize().y);

    this->text->setRelPos(this->icon->getSize().x + inner_margin, btn_height / 2 - this->text->getSize().y / 2);

    this->setSize(btn_width, btn_height);
}
