// Copyright (c) 2016, PG, All rights reserved.
#include "UIContextMenu.h"

#include <utility>

#include "AnimationHandler.h"
#include "CBaseUIContainer.h"
#include "CBaseUIDispatch.h"
#include "CBaseUIEventCtx.h"
#include "CBaseUIScrollView.h"
#include "ContainerRanges.h"
#include "OsuConVars.h"
#include "MakeDelegateWrapper.h"
#include "Engine.h"
#include "KeyBindings.h"
#include "OsuKeyBinds.h"
#include "Mouse.h"
#include "Graphics.h"
#include "Osu.h"
#include "Skin.h"
#include "SoundEngine.h"
#include "TooltipOverlay.h"
#include "UI.h"
#include "SString.h"

UIContextMenuButton::UIContextMenuButton(float xPos, float yPos, float xSize, float ySize, std::string name,
                                         std::string text, int id)
    : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name), std::move(text)) {
    this->iID = id;
}

void UIContextMenuButton::updateInput(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;
    CBaseUIButton::updateInput(c);

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

void UIContextMenuButton::setTooltipText(std::string_view text) {
    this->tooltipTextLines = SString::split_newlines<std::string>(text);
}

UIContextMenuTextbox::UIContextMenuTextbox(float xPos, float yPos, float xSize, float ySize, std::string name, int id)
    : CBaseUITextbox(xPos, yPos, xSize, ySize, std::move(name)) {
    this->iID = id;
}

UIContextMenu::UIContextMenu(float xPos, float yPos, float xSize, float ySize, std::string name,
                             CBaseUIScrollView *parent)
    : CBaseUIScrollView(xPos, yPos, xSize, ySize, std::move(name)) {
    this->parent = parent;

    this->backgroundColor = defaultBGColor;
    this->frameColor = defaultFrameColor;

    this->setPos(xPos, yPos);
    this->setSize(xSize, ySize);

    this->setHorizontalScrolling(false);
    this->setDrawBackground(false);
    this->setDrawFrame(false);
    this->setScrollbarSizeMultiplier(0.5f);

    // a context menu draws on top of its group, so its (sub)tree out-ranks same-group siblings for
    // hover/click/wheel regardless of visit order (the songbrowser menu is visited before the
    // carousel beneath it)
    this->bDrawsOnTop = true;

    // HACHACK: this->bVisible is always true, since we want to be able to put a context menu in a scrollview.
    //          When scrolling, scrollviews call setVisible(false) to clip items, and that breaks the menu.
    this->bVisible = true;

    this->bBigStyle = false;
}

UIContextMenu::~UIContextMenu() {
    if(this->searchList != nullptr) this->searchList->invalidate();
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

    if(this->searchList != nullptr) {
        // a line under the search box
        if(this->searchList->getSize().y > 0) {
            g->drawLine(this->getPos().x, this->searchList->getPos().y, this->getPos().x + this->getSize().x,
                        this->searchList->getPos().y);
        }

        if(this->highlighted != nullptr) {
            g->pushClipRect(this->searchList->getRect());
            g->setColor(Color(0xff404040).setA((f32)this->fAnimation));
            g->fillRect(this->getPos().x + 1, this->highlighted->getPos().y, this->getSize().x - 1,
                        this->highlighted->getSize().y);
            g->popClipRect();
        }
    }

    CBaseUIScrollView::draw();

    if(this->fAnimation > 0.0f && this->fAnimation < 1.0f) g->pop3DScene();
}

void UIContextMenu::updateInput(CBaseUIEventCtx &c) {
    if(!this->bVisible2) return;
    // bDrawsOnTop alone would tie other bDrawsOnTop siblings
    // (e.g. the songbrowser topbar buttons an open dropdown covers)
    // and lose the overlap to later-visited ones, so raise a second level the menu's subtree out-ranks those too
    // TODO: better solution (bDrawsOnTop + this is redundant/competing)
    c.currentHitTier++;
    CBaseUIScrollView::updateInput(c);
    c.currentHitTier--;
}

void UIContextMenu::tick() {
    if(!this->bVisible2) return;
    CBaseUIScrollView::tick();

    if(this->containedTextbox != nullptr) {
        if(this->containedTextbox->hitEnter()) this->onHitEnter(this->containedTextbox);
    }

    if(this->searchTextbox != nullptr) {
        // a press on the menu takes the keyboard focus from the search box
        if(CBaseUIDispatch::getFocus() == nullptr) this->searchTextbox->requestFocus();

        if(this->searchTextbox->getText() != this->sSearch) this->updateSearch();
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

    if(this->searchList != nullptr) {
        const std::vector<CBaseUIElement *> &buttons = this->searchList->container.getElements();
        if(e == KEY_ESCAPE) {
            this->setVisible2(false);
        } else if(e == KEY_DOWN || e == KEY_UP) {
            if(!buttons.empty()) {
                // past the last button it goes on from the first one
                const uSz current = Mc::ranges::find(buttons, this->highlighted) - buttons.begin();
                const uSz step = e == KEY_DOWN ? 1 : buttons.size() - 1;
                this->highlight(static_cast<UIContextMenuButton *>(buttons[(current + step) % buttons.size()]));
            }
        } else if(e == KEY_ENTER || e == KEY_NUMPAD_ENTER) {
            if(this->highlighted != nullptr) this->onClick(this->highlighted);
        } else {
            // the search box takes every other key
            CBaseUIScrollView::onKeyDown(e);
        }

        // nothing beneath gets keys while it is open
        e.consume();
        return;
    }

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
        if(e == KEY_ESCAPE || e == binds::GAME_PAUSE) {
            e.consume();
            this->setVisible2(false);
        }
    }
}

void UIContextMenu::onChar(KeyboardEvent &e) {
    if(!this->bVisible2) return;

    CBaseUIScrollView::onChar(e);

    if(this->searchTextbox != nullptr) e.consume();
}

void UIContextMenu::begin(int minWidth, bool bigStyle, bool searchable) {
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

        // in a searchable menu, searchButtons owns the buttons, so the list must not delete them
        if(this->searchList != nullptr) this->searchList->invalidate();
        for(auto &button : this->searchButtons) {
            this->selfDeletionCrashWorkaroundScheduledElementDeleteHack.push_back(button.release());
        }
        this->searchButtons.clear();
    }

    this->freeElements();

    this->containedTextbox = nullptr;
    this->searchTextbox = nullptr;
    this->searchList = nullptr;
    this->highlighted = nullptr;
    this->sSearch.clear();

    if(searchable) {
        const int buttonHeight = 30 * Osu::getUIScale() * (this->bBigStyle ? 1.27f : 1.0f);
        const int margin = 9 * Osu::getUIScale();

        // the search text lines up with the button text
        this->searchTextbox = new UIContextMenuTextbox(0, margin, 0, buttonHeight, "contextmenu_search", -1);
        this->searchTextbox->setTextOffsetX(margin);
        this->searchTextbox->setDrawFrame(false);
        this->searchTextbox->setDrawBackground(false);
        if(this->bBigStyle) this->searchTextbox->setFont(osu->getSubTitleFont());
        this->container.addBaseUIElement(this->searchTextbox);

        this->searchList = new CBaseUIScrollView(0, buttonHeight + 2 * margin, 0, 0, "");
        this->searchList->setHorizontalScrolling(false);
        this->searchList->setDrawBackground(false);
        this->searchList->setDrawFrame(false);
        this->searchList->setScrollbarSizeMultiplier(0.5f);
        this->container.addBaseUIElement(this->searchList);
    }
}

UIContextMenuButton *UIContextMenu::addButtonJustified(const std::string &text, TEXT_JUSTIFICATION j, int id) {
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
    if(this->searchList != nullptr) {
        this->searchButtons.emplace_back(button);
    } else {
        this->container.addBaseUIElement(button);
    }

    if(button->getSize().x + 2 * margin > this->iWidthCounter) {
        this->iWidthCounter = button->getSize().x + 2 * margin;
        this->setSizeX(this->iWidthCounter);
    }

    this->iYCounter += buttonHeight;
    this->setSizeY(this->iYCounter + 2 * margin);

    return button;
}

UIContextMenuTextbox *UIContextMenu::addTextbox(const std::string &text, int id) {
    const int buttonHeight = 30 * Osu::getUIScale() * (this->bBigStyle ? 1.27f : 1.0f);
    const int margin = 9 * Osu::getUIScale();

    auto *textbox = new UIContextMenuTextbox(margin, this->iYCounter + margin, 0, buttonHeight, text, id);
    {
        textbox->setText(text);

        if(this->bBigStyle) textbox->setFont(osu->getSubTitleFont());

        textbox->requestFocus();
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
    if(this->searchList != nullptr) this->updateSearch();

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
    this->fAnimation.set(1.0f, 0.15f, anim::QuartOut);

    soundEngine->play(osu->getSkin()->s_expand);

    if(enableScrolling && this->parent && this->parent->isVisible()) {
        // steal focus for scroll handling priority
        this->parent->stealFocus();
    }

    if(this->searchTextbox != nullptr) this->searchTextbox->requestFocus();
}

void UIContextMenu::updateSearch() {
    const int buttonHeight = 30 * Osu::getUIScale() * (this->bBigStyle ? 1.27f : 1.0f);
    const int margin = 9 * Osu::getUIScale();

    this->sSearch = this->searchTextbox->getText();

    // searchButtons owns the buttons, the list must not delete them
    this->searchList->invalidate();
    this->searchList->freeElements();

    int listHeight = 0;
    for(const auto &button : this->searchButtons) {
        if(!this->sSearch.empty() && !SString::contains_ncase(button->getText(), this->sSearch)) continue;

        button->setSizeX(this->iWidthCounter - 2 * margin);
        this->searchList->container.addBaseUIElement(button.get(), margin, margin + listHeight);
        listHeight += buttonHeight;
    }
    if(listHeight > 0) listHeight += 2 * margin;

    const int searchHeight = buttonHeight + 2 * margin;
    const int maxListHeight = osu->getVirtScreenHeight() / 2 - searchHeight;
    this->searchList->setVerticalScrolling(listHeight > maxListHeight);
    this->searchList->setSize(this->iWidthCounter, std::min(listHeight, maxListHeight));
    this->searchList->setScrollSizeToContent(margin);

    this->searchTextbox->setSizeX(this->iWidthCounter);
    this->setSizeY(searchHeight + this->searchList->getSize().y);
    this->setScrollSizeToContent();
    if(this->parent && this->parent->isVisible()) {
        this->parent->setScrollSizeToContent();
        this->parent->forceInvalidateClipping();
    }

    // Enter clicks the first match
    const std::vector<CBaseUIElement *> &buttons = this->searchList->container.getElements();
    this->highlight(buttons.empty() ? nullptr : static_cast<UIContextMenuButton *>(buttons.front()));
}

void UIContextMenu::highlight(UIContextMenuButton *button) {
    this->highlighted = button;
    if(button == nullptr) return;

    const int margin = 9 * Osu::getUIScale();
    const float top = button->getRelPos().y - margin;
    const float bottom = button->getRelPos().y + button->getSize().y + margin;
    if(top + this->searchList->getRelPosY() < 0) {
        this->searchList->scrollToY(-top, false);
    } else if(bottom + this->searchList->getRelPosY() > this->searchList->getSize().y) {
        this->searchList->scrollToY(-(bottom - this->searchList->getSize().y), false);
    }
}

void UIContextMenu::setVisible2(bool visible2) {
    this->bVisible2 = visible2;

    if(!this->bVisible2) {
        this->setSize(1, 1);  // reset size
        if(this->searchTextbox != nullptr) this->searchTextbox->stealFocus();
    }

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
