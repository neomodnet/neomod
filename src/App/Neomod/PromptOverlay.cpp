// Copyright (c) 2024, kiwec, All rights reserved.
#include "PromptOverlay.h"

#include "CBaseUILabel.h"
#include "CBaseUITextbox.h"
#include "KeyBindings.h"
#include "Engine.h"
#include "Osu.h"
#include "Graphics.h"
#include "i18n.h"
#include "UIButton.h"
#include "MakeDelegateWrapper.h"

#include <utility>

PromptOverlay::PromptOverlay() : UIScreen() {
    // modal + closeOnScreenSwitch are declared in UI.h's screen registry
    this->prompt_label = new CBaseUILabel(0, 0, 0, 0, "", "");
    this->prompt_label->setDrawFrame(false);
    this->prompt_label->setDrawBackground(false);
    this->addBaseUIElement(this->prompt_label);

    this->prompt_input = new CBaseUITextbox(0, 0, 400, 40, "prompt_input");
    this->addBaseUIElement(this->prompt_input);

    this->ok_btn = new UIButton(0, 0, 110, 35, "ok_btn", _("OK"));
    this->ok_btn->setColor(0xff00d900);
    this->ok_btn->setUseDefaultSkin();
    this->ok_btn->setClickCallback(SA::MakeDelegate<&PromptOverlay::on_ok>(this));
    this->addBaseUIElement(this->ok_btn);

    this->cancel_btn = new UIButton(0, 0, 110, 35, "cancel_btn", _("Cancel"));
    this->cancel_btn->setColor(0xff0c7c99);
    this->cancel_btn->setUseDefaultSkin();
    this->cancel_btn->setClickCallback(SA::MakeDelegate<&PromptOverlay::close>(this));
    this->addBaseUIElement(this->cancel_btn);
}

void PromptOverlay::onResolutionChange(vec2 newResolution) {
    const float xmiddle = newResolution.x / 2;
    const float ymiddle = newResolution.y / 2;

    this->setSize(newResolution);

    this->prompt_label->setSizeToContent();
    this->prompt_label->setPos(xmiddle - 200, ymiddle - 30);

    this->prompt_input->setPos(xmiddle - 200, ymiddle);

    this->ok_btn->setPos(xmiddle - 120, ymiddle + 50);
    this->cancel_btn->setPos(xmiddle + 10, ymiddle + 50);
}

void PromptOverlay::draw() {
    if(!this->bVisible) return;

    g->setColor(argb(200, 0, 0, 0));
    g->fillRect(0, 0, this->getSize().x, this->getSize().y);

    UIScreen::draw();
}

void PromptOverlay::onKeyDown(KeyboardEvent &e) {
    if(!this->bVisible) return;

    if(e == KEY_ENTER || e == KEY_NUMPAD_ENTER) {
        this->on_ok();
        e.consume();
        return;
    }

    if(e == KEY_ESCAPE) {
        this->close();
        e.consume();
        return;
    }

    this->prompt_input->onKeyDown(e);
    e.consume();
}

void PromptOverlay::onKeyUp(KeyboardEvent &e) {
    if(!this->bVisible) return;
    this->prompt_input->onKeyUp(e);
    e.consume();
}

void PromptOverlay::onChar(KeyboardEvent &e) {
    if(!this->bVisible) return;
    this->prompt_input->onChar(e);
    e.consume();
}

Mc::Registration PromptOverlay::prompt(std::string msg, const PromptResponseCallback &callback) {
    this->prompt_label->setText(std::move(msg));
    this->prompt_input->setText("");
    this->prompt_input->focus();
    this->callback = callback;
    this->bVisible = true;

    this->onResolutionChange(osu->getVirtScreenSize());

    return {[](void *self, u64 id, Mc::Registration::End how) {
                auto *overlay = static_cast<PromptOverlay *>(self);
                if(how == Mc::Registration::End::REVOKE && id == overlay->promptId) overlay->close();
            },
            this, ++this->promptId};
}

void PromptOverlay::on_ok() {
    // (the callback may open the next prompt)
    const PromptResponseCallback cb = std::exchange(this->callback, {});
    this->bVisible = false;
    if(cb) cb(this->prompt_input->getText());
}

void PromptOverlay::close() {
    this->callback = {};
    this->bVisible = false;
}
