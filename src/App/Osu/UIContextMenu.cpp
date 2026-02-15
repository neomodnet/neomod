// Copyright (c) 2016, PG, All rights reserved.
#include "UIContextMenu.h"

#include <utility>

#include "AnimationHandler.h"
#include "CBaseUIContainer.h"
#include "CBaseUIScrollView.h"
#include "OsuConVars.h"
#include "MakeDelegateWrapper.h"
#include "Engine.h"
#include "KeyBindings.h"
#include "Mouse.h"
#include "Graphics.h"
#include "Osu.h"
#include "Skin.h"
#include "SoundEngine.h"
#include "TooltipOverlay.h"
#include "UI.h"

UIContextMenuButton::UIContextMenuButton(float xPos, float yPos, float xSize, float ySize, UString name, UString text,
                                         int id)
    : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name), std::move(text)) {
    this->iID = id;
}

void UIContextMenuButton::update(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;
    CBaseUIButton::update(c);

    if(this->isMouseInside() && this->tooltipTextLines.size() > 0) {
        ui->getTooltipOverlay()->begin();
        {
            for(const auto &tooltipTextLine : this->tooltipTextLines) {
                ui->getTooltipOverlay()->addLine(tooltipTextLine);
            }
        }
        ui->getTooltipOverlay()->end();
    }
}

static float button_sound_cooldown = 0.f;
void UIContextMenuButton::onMouseInside() {
    CBaseUIButton::onMouseInside();

    if(button_sound_cooldown + 0.05f < engine->getTime()) {
        button_sound_cooldown = engine->getTime();
        soundEngine->play(osu->getSkin()->s_hover_button);
    }
}

void UIContextMenuButton::onMouseDownInside(bool /*left*/, bool /*right*/) {
    soundEngine->play(osu->getSkin()->s_click_button);
}

void UIContextMenuButton::setTooltipText(const UString &text) { this->tooltipTextLines = text.split(US_("\n")); }

UIContextMenuTextbox::UIContextMenuTextbox(float xPos, float yPos, float xSize, float ySize, UString name, int id)
    : CBaseUITextbox(xPos, yPos, xSize, ySize, std::move(name)) {
    this->iID = id;
}

UIContextMenu::UIContextMenu(float xPos, float yPos, float xSize, float ySize, const UString &name,
                             CBaseUIScrollView *parent)
    : CBaseUIScrollView(xPos, yPos, xSize, ySize, name) {
    this->parent = parent;

    this->backgroundColor = defaultBGColor;
    this->frameColor = defaultFrameColor;

    this->setPos(xPos, yPos);
    this->setSize(xSize, ySize);
    this->setName(name);

    this->setHorizontalScrolling(false);
    this->setDrawBackground(false);
    this->setDrawFrame(false);
    this->setScrollbarSizeMultiplier(0.5f);

    // HACHACK: this->bVisible is always true, since we want to be able to put a context menu in a scrollview.
    //          When scrolling, scrollviews call setVisible(false) to clip items, and that breaks the menu.
    this->bVisible = true;

    this->bBigStyle = false;
}

void UIContextMenu::draw() {
    if(!this->bVisible2) return;

    if(this->fAnimation > 0.0f && this->fAnimation < 1.0f) {
        g->push3DScene(McRect(this->getPos().x,
                              this->getPos().y + ((this->getSize().y / 2.0f) * (this->bInvertAnimation ? 1.0f : -1.0f)),
                              this->getSize().x, this->getSize().y));
        g->rotate3DScene((1.0f - this->fAnimation) * 90.0f * (this->bInvertAnimation ? 1.0f : -1.0f), 0, 0);
    }

    // draw background
    const Color bgColor = Color(this->backgroundColor).setA(this->backgroundColor.Af() * this->fAnimation);
    g->setColor(bgColor);

    g->fillRect(this->getPos().x + 1, this->getPos().y + 1, this->getSize().x - 1, this->getSize().y - 1);

    // draw frame
    g->setColor(Color(this->frameColor).setA(this->frameColor.Af() * (this->fAnimation * this->fAnimation)));

    g->drawRect(this->getPos(), this->getSize());

    CBaseUIScrollView::draw();

    if(this->fAnimation > 0.0f && this->fAnimation < 1.0f) g->pop3DScene();
}

void UIContextMenu::update(CBaseUIEventCtx &c) {
    if(!this->bVisible2) return;
    CBaseUIScrollView::update(c);

    if(this->containedTextbox != nullptr) {
        if(this->containedTextbox->hitEnter()) this->onHitEnter(this->containedTextbox);
    }

    if(this->selfDeletionCrashWorkaroundScheduledElementDeleteHack.size() > 0) {
        for(auto &i : this->selfDeletionCrashWorkaroundScheduledElementDeleteHack) {
            delete i;
        }

        this->selfDeletionCrashWorkaroundScheduledElementDeleteHack.clear();
    }
}

void UIContextMenu::onKeyUp(KeyboardEvent &e) {
    if(!this->bVisible2) return;

    CBaseUIScrollView::onKeyUp(e);
}

void UIContextMenu::onKeyDown(KeyboardEvent &e) {
    if(!this->bVisible2) return;

    CBaseUIScrollView::onKeyDown(e);

    // also force ENTER event if context menu textbox has lost focus (but context menu is still visible, e.g. if the
    // user clicks inside the context menu but outside the textbox)
    if(this->containedTextbox != nullptr) {
        if(e == KEY_ENTER || e == KEY_NUMPAD_ENTER) {
            e.consume();
            this->onHitEnter(this->containedTextbox);
        }
    }

    // hide on ESC
    if(!e.isConsumed()) {
        if(e == KEY_ESCAPE || e == cv::GAME_PAUSE.getVal<SCANCODE>()) {
            e.consume();
            this->setVisible2(false);
        }
    }
}

void UIContextMenu::onChar(KeyboardEvent &e) {
    if(!this->bVisible2) return;

    CBaseUIScrollView::onChar(e);
}

void UIContextMenu::begin(int minWidth, bool bigStyle) {
    this->iWidthCounter = minWidth;
    this->bBigStyle = bigStyle;

    this->iYCounter = 0;
    this->clickCallback = {};

    this->setSizeX(this->iWidthCounter);

    // HACKHACK: bad design workaround.
    // - callbacks from the same context menu which call begin() to create a new context menu may crash because
    //   begin() deletes the object the callback is currently being called from
    // - so, instead, we just keep a list of things to delete whenever we get to the next update() tick
    {
        const std::vector<CBaseUIElement *> &oldElementsWeCanNotDeleteYet = this->container.getElements();
        this->selfDeletionCrashWorkaroundScheduledElementDeleteHack.insert(
            this->selfDeletionCrashWorkaroundScheduledElementDeleteHack.end(), oldElementsWeCanNotDeleteYet.begin(),
            oldElementsWeCanNotDeleteYet.end());
        this->invalidate();  // ensure nothing is deleted yet
    }

    this->freeElements();

    this->containedTextbox = nullptr;
}

UIContextMenuButton *UIContextMenu::addButtonJustified(const UString &text, TEXT_JUSTIFICATION j, int id) {
    const int buttonHeight = 30 * Osu::getUIScale() * (this->bBigStyle ? 1.27f : 1.0f);
    const int margin = 9 * Osu::getUIScale();

    auto *button = new UIContextMenuButton(margin, this->iYCounter + margin, 0, buttonHeight, text, text, id);
    {
        if(this->bBigStyle) button->setFont(osu->getSubTitleFont());

        button->setClickCallback(SA::MakeDelegate<&UIContextMenu::onClick>(this));
        button->setWidthToContent(3 * Osu::getUIScale());
        button->setTextJustification(j);
        button->setDrawFrame(false);
        button->setDrawBackground(false);
    }
    this->container.addBaseUIElement(button);

    if(button->getSize().x + 2 * margin > this->iWidthCounter) {
        this->iWidthCounter = button->getSize().x + 2 * margin;
        this->setSizeX(this->iWidthCounter);
    }

    this->iYCounter += buttonHeight;
    this->setSizeY(this->iYCounter + 2 * margin);

    return button;
}

UIContextMenuTextbox *UIContextMenu::addTextbox(const UString &text, int id) {
    const int buttonHeight = 30 * Osu::getUIScale() * (this->bBigStyle ? 1.27f : 1.0f);
    const int margin = 9 * Osu::getUIScale();

    auto *textbox = new UIContextMenuTextbox(margin, this->iYCounter + margin, 0, buttonHeight, text, id);
    {
        textbox->setText(text);

        if(this->bBigStyle) textbox->setFont(osu->getSubTitleFont());

        textbox->setActive(true);
    }
    this->container.addBaseUIElement(textbox);

    this->iYCounter += buttonHeight;
    this->setSizeY(this->iYCounter + 2 * margin);

    // NOTE: only one single textbox is supported currently
    this->containedTextbox = textbox;

    return textbox;
}

void UIContextMenu::end(bool invertAnimation, EndStyle style) {
    using namespace flags::operators;

    this->bInvertAnimation = invertAnimation;

    const bool clampTop = flags::has<EndStyle::CLAMP_TOP>(style);
    const bool clampBot = flags::has<EndStyle::CLAMP_BOT>(style);
    const bool clampLeft = flags::has<EndStyle::CLAMP_LEFT>(style);
    const bool clampRight = flags::has<EndStyle::CLAMP_RIGHT>(style);

    const bool fit = flags::has<EndStyle::REPOSITION_ONSCREEN>(style);
    bool enableScrolling = flags::has<EndStyle::STANDALONE_SCROLL>(style);

    const int margin = 9 * Osu::getUIScale();

    const std::vector<CBaseUIElement *> &elements = this->container.getElements();
    if(elements.empty()) return;

    for(auto *element : elements) {
        element->setSizeX(this->iWidthCounter - 2 * margin);
    }

    this->setVerticalScrolling(false);
    if(fit) {
        if(this->getPos().y < 0) {
            const float underflow = std::abs(this->getPos().y);

            this->setRelPosY(this->getPos().y + underflow);
            this->setPosY(this->getPos().y + underflow);
        }

        if(this->getPos().y + this->getSize().y > osu->getVirtScreenHeight()) {
            const float overflow = std::abs(this->getPos().y + this->getSize().y - osu->getVirtScreenHeight());

            this->setSizeY(this->getSize().y - overflow - 1);
        }

        if(this->getPos().x < 0) {
            const float underflow = std::abs(this->getPos().x);

            this->setRelPosX(this->getPos().x + underflow);
            this->setPosX(this->getPos().x + underflow);
        }
        if(this->getPos().x + this->getSize().x > osu->getVirtScreenWidth()) {
            const float overflow = std::abs(this->getPos().x + this->getSize().x - osu->getVirtScreenWidth());

            this->setRelPosX(this->getPos().x - overflow - 1);
            this->setPosX(this->getPos().x - overflow - 1);
        }
    }
    // scrollview handling and edge cases

    {
        if(clampTop && this->getPos().y < 0) {
            enableScrolling = true;

            const float underflow = std::abs(this->getPos().y);

            this->setRelPosY(this->getPos().y + underflow);
            this->setPosY(this->getPos().y + underflow);
            this->setSizeY(this->getSize().y - underflow);
        }

        if(clampBot && this->getPos().y + this->getSize().y > osu->getVirtScreenHeight()) {
            enableScrolling = true;

            const float overflow = std::abs(this->getPos().y + this->getSize().y - osu->getVirtScreenHeight());

            this->setSizeY(this->getSize().y - overflow - 1);
        }

        // horizontal clamp
        if(clampLeft && this->getPos().x < 0) {
            const float underflow = std::abs(this->getPos().x);

            this->setRelPosX(this->getPos().x + underflow);
            this->setPosX(this->getPos().x + underflow);
            this->setSizeX(this->getSize().x - underflow);
        }
        if(clampRight && this->getPos().x + this->getSize().x > osu->getVirtScreenWidth()) {
            const float overflow = std::abs(this->getPos().x + this->getSize().x - osu->getVirtScreenWidth());

            this->setSizeX(this->getSize().x - overflow - 1);
        }
    }

    if(enableScrolling) {
        this->setVerticalScrolling(true);
    }

    this->setScrollSizeToContent();

    this->setVisible2(true);

    this->fAnimation = 0.001f;
    anim::moveQuartOut(&this->fAnimation, 1.0f, 0.15f, true);

    soundEngine->play(osu->getSkin()->s_expand);

    if(enableScrolling && this->parent && this->parent->isVisible()) {
        // steal focus for scroll handling priority
        this->parent->stealFocus();
    }
}

void UIContextMenu::setVisible2(bool visible2) {
    this->bVisible2 = visible2;

    if(!this->bVisible2) this->setSize(1, 1);  // reset size

    if(this->parent && this->parent->isVisible()) {
        this->parent->setScrollSizeToContent();   // and update parent scroll size
        this->parent->forceInvalidateClipping();  // self-inflicted pain
    }
}

void UIContextMenu::onMouseDownOutside(bool /*left*/, bool /*right*/) { this->setVisible2(false); }

void UIContextMenu::onClick(UIContextMenuButton *button) {
    this->setVisible2(false);
    if(this->clickCallback == nullptr) return;

    // special case: if text input exists, then override with its text
    if(this->containedTextbox != nullptr)
        this->clickCallback(this->containedTextbox->getText(), button->getID());
    else
        this->clickCallback(button->getName(), button->getID());
}

void UIContextMenu::onHitEnter(UIContextMenuTextbox *textbox) {
    this->setVisible2(false);

    if(this->clickCallback != nullptr) this->clickCallback(textbox->getText(), textbox->getID());
}

void UIContextMenu::clampToBottomScreenEdge() {
    if(this->getRelPos().y + this->getSize().y > osu->getVirtScreenHeight()) {
        int newRelPosY = osu->getVirtScreenHeight() - this->getSize().y - 1;
        this->setRelPosY(newRelPosY);
        this->setPosY(newRelPosY);
    }
}

void UIContextMenu::clampToRightScreenEdge() {
    if(this->getRelPos().x + this->getSize().x > osu->getVirtScreenWidth()) {
        const int newRelPosX = osu->getVirtScreenWidth() - this->getSize().x - 1;
        this->setRelPosX(newRelPosX);
        this->setPosX(newRelPosX);
    }
}
