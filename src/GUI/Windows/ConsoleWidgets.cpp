// Copyright (c) 2011, PG & 2026, WH, All rights reserved.
#include "ConsoleWidgets.h"

#include "CBaseUIButton.h"
#include "ConVar.h"
#include "Console.h"
#include "Engine.h"
#include "Environment.h"
#include "Font.h"
#include "Graphics.h"
#include "Keyboard.h"
#include "Mouse.h"

#include "fmt/format.h"

#include <algorithm>
#include <cmath>

namespace Mc {
float consoleLogScale(float dpiScale) { return std::round(dpiScale + 0.255f) * cv::console_overlay_scale.getFloat(); }
}  // namespace Mc

ConsoleTextbox::ConsoleTextbox(float xPos, float yPos, float xSize, float ySize, std::string name,
                               ConsoleSuggestionList *suggestions)
    : CBaseUITextbox(xPos, yPos, xSize, ySize, std::move(name)), suggestions(suggestions) {
    suggestions->setCommandCallback(SA::MakeDelegate<&ConsoleTextbox::onSuggestionPicked>(this));
}

void ConsoleTextbox::drawText() {
    // the top match in grey behind the text, as long as the text is still a prefix of it
    if(cv::consolebox_draw_preview.getBool() && this->suggestions->isVisible() && !this->text.empty()) {
        const std::string_view top = this->suggestions->getTopCommand();
        if(top.starts_with(this->text)) {
            g->setColor(0xff666666);
            g->pushTransform();
            {
                g->translate((int)(this->getPos().x + this->iTextAddX + this->fTextScrollAddX),
                             (int)(this->getPos().y + this->iTextAddY));
                g->drawString(this->font, top);
            }
            g->popTransform();
        }
    }

    CBaseUITextbox::drawText();
}

void ConsoleTextbox::onKeyDown(KeyboardEvent &e) {
    if(!this->isFocused() || !this->bVisible) return;

    // consumes every key; an edit ends up in setText, which syncs the suggestions
    CBaseUITextbox::onKeyDown(e);

    if(this->hitEnter()) {
        this->submit();
    } else if(this->suggestions->isVisible()) {
        if(e == KEY_DOWN || (e == KEY_TAB && !keyboard->isShiftDown()))
            this->complete(this->suggestions->cycle(1));
        else if(e == KEY_UP || (e == KEY_TAB && keyboard->isShiftDown()))
            this->complete(this->suggestions->cycle(-1));
    } else if(e == KEY_DOWN || e == KEY_UP) {
        if(const std::string_view entry = Console::cycleHistory(e == KEY_DOWN ? 1 : -1); !entry.empty())
            this->complete(std::string{entry});
    }
}

void ConsoleTextbox::onFocusStolen() {
    CBaseUITextbox::onFocusStolen();

    // the popup goes with the keyboard, as the source engine's does (a press elsewhere, escape, the console closing)
    this->suggestions->clear();
}

void ConsoleTextbox::onMouseDownOutside(bool left, bool right) {
    // a press on the popup keeps the keyboard here, the click on a suggestion needs the popup to stay
    if(this->suggestions->isVisible() && this->suggestions->getRect().contains(mouse->getPos())) return;

    CBaseUITextbox::onMouseDownOutside(left, right);
}

CBaseUITextbox *ConsoleTextbox::setText(std::string text) {
    CBaseUITextbox::setText(std::move(text));

    // every way the text changes (typing, deleting, pasting, clearing) goes through here, complete() excepted
    if(this->text != this->sSuggestionsInput) {
        this->sSuggestionsInput = this->text;
        this->suggestions->rebuild(this->text);
    }
    return this;
}

void ConsoleTextbox::submit() {
    // the box is cleared before the command runs: it may swap the console style, i.e. close this view
    const std::string command{this->text};
    this->clear();
    Console::submit(command);
}

void ConsoleTextbox::complete(std::string text) {
    this->sSuggestionsInput = text;
    this->setText(std::move(text));
    this->setCursorPosRight();
}

void ConsoleTextbox::onSuggestionPicked(std::string command) {
    // unlike a keyboard cycle, a pick is an edit: the suggestions follow the completed text (nothing matches a name
    // plus a space, so the popup closes, as the source engine's does)
    this->setText(std::move(command));
    this->setCursorPosRight();
    this->requestFocus();  // the click on the popup took it
}

namespace {
class ConsoleSuggestionButton final : public CBaseUIButton {
    NOCOPY_NOMOVE(ConsoleSuggestionButton)
   public:
    ConsoleSuggestionButton(float xPos, float yPos, float xSize, float ySize, std::string name, std::string text,
                            std::string helpText, const CBaseUIElement *const list)
        : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name), std::move(text)),
          list(list),
          sHelpText(std::move(helpText)) {}
    ~ConsoleSuggestionButton() override = default;

   protected:
    void drawText() override {
        if(this->font == nullptr || this->getText().length() < 1) return;

        if(cv::consolebox_draw_helptext.getBool()) {
            if(this->sHelpText.length() > 0) {
                constexpr std::string_view helpTextSeparator = "-";
                const int helpTextOffset = std::round(2.0f * this->font->getStringWidth(helpTextSeparator) *
                                                      ((float)this->font->getDPI() / 96.0f));  // NOTE: abusing font dpi
                const int helpTextSeparatorStringWidth =
                    std::max(1, (int)this->font->getStringWidth(helpTextSeparator));
                const int helpTextStringWidth = std::max(1, (int)this->font->getStringWidth(this->sHelpText));

                g->pushTransform();
                {
                    const float scale = std::min(
                        1.0f, (std::max(1.0f, this->list->getSize().x - this->fStringWidth - helpTextOffset * 1.5f -
                                                  helpTextSeparatorStringWidth * 1.5f)) /
                                  (float)helpTextStringWidth);

                    g->scale(scale, scale);
                    g->translate((int)(this->getPos().x + this->fStringWidth + helpTextOffset * scale / 2 +
                                       helpTextSeparatorStringWidth * scale),
                                 (int)(this->getPos().y + this->getSize().y / 2.0f + this->fStringHeight / 2.0f -
                                       this->font->getHeight() * (1.0f - scale) / 2.0f));
                    g->setColor(0xff666666);
                    g->drawString(this->font, helpTextSeparator);
                    g->translate(helpTextOffset * scale, 0);
                    g->drawString(this->font, this->sHelpText);
                }
                g->popTransform();
            }
        }

        CBaseUIButton::drawText();
    }

   private:
    const CBaseUIElement *const list;
    std::string sHelpText;
};
}  // namespace

ConsoleSuggestionList::ConsoleSuggestionList(float xPos, float yPos, float xSize, float ySize, std::string name)
    : CBaseUIScrollView(xPos, yPos, xSize, ySize, std::move(name)) {
    this->setDrawBackground(true);
    this->setBackgroundColor(argb(255, 0, 0, 0));
    this->setFrameColor(argb(255, 255, 255, 255));
    this->setHorizontalScrolling(false);
    this->setVerticalScrolling(true);
    this->setVisible(false);
}

float ConsoleSuggestionList::getDPIScale() const {
    return ((float)std::max(env->getDPI(), engine->getDefaultFont()->getDPI()) / 96.0f);  // NOTE: abusing font dpi
}

float ConsoleSuggestionList::getRowHeight() const {
    return static_cast<float>(static_cast<int>(22 * this->getDPIScale()));
}

void ConsoleSuggestionList::rebuild(std::string_view input) {
    this->clear();

    const float dpiScale = this->getDPIScale();
    const float rowHeight = this->getRowHeight();
    const int bottomAdd = 3 * dpiScale;
    const int buttonHeight = (17 + 8) * dpiScale;

    std::vector<Console::Suggestion> suggestions = Console::getSuggestions(input);
    for(size_t i = 0; i < suggestions.size(); i++) {
        auto &suggestion = suggestions[i];
        auto *button = new ConsoleSuggestionButton(3 * dpiScale, i * rowHeight + 2 * dpiScale, 100, buttonHeight,
                                                   std::string{suggestion.command}, std::move(suggestion.display),
                                                   std::string{suggestion.help}, this);
        {
            button->setDrawFrame(false);
            button->setSizeX(button->getFont()->getStringWidth(button->getText()));
            button->setClickCallback(SA::MakeDelegate<&ConsoleSuggestionList::onSuggestionClicked>(this));
            button->setDrawBackground(false);
        }
        this->container.addBaseUIElement(button);
    }

    const int count = static_cast<int>(suggestions.size());
    if(count > 0) {
        this->setSizeY(std::min(count, 4) * rowHeight + bottomAdd);
        this->setScrollSizeToContent();
        this->scrollToElement(this->container.getElements()[0], 0, 0, false);  // new content, no scroll-in from the old
    }
    this->setVisible(count > 0);
}

void ConsoleSuggestionList::clear() {
    this->iSelected = -1;
    this->container.freeElements();
    this->setVisible(false);
}

std::string ConsoleSuggestionList::cycle(int dir) {
    const auto &buttons = this->container.getElements();
    const int count = static_cast<int>(buttons.size());
    if(count < 1) return {};

    if(dir > 0)
        this->iSelected = (this->iSelected < 0 || this->iSelected >= count - 1) ? 0 : this->iSelected + 1;
    else
        this->iSelected = (this->iSelected <= 0) ? count - 1 : this->iSelected - 1;

    for(int i = 0; i < count; i++) {
        auto *button = static_cast<CBaseUIButton *>(buttons[i]);
        if(i == this->iSelected) {
            button->setTextColor(0xff00ff00);
            button->setTextDarkColor(0xff000000);
        } else
            button->setTextColor(0xffffffff);
    }
    this->scrollToElement(buttons[this->iSelected]);

    return fmt::format("{:s} ", buttons[this->iSelected]->getName());
}

std::string_view ConsoleSuggestionList::getTopCommand() const {
    const auto &buttons = this->container.getElements();
    return buttons.empty() ? std::string_view{} : buttons[0]->getName();
}

void ConsoleSuggestionList::onSuggestionClicked(CBaseUIButton *button) {
    if(this->commandCallback) this->commandCallback(fmt::format("{:s} ", button->getName()));
}
