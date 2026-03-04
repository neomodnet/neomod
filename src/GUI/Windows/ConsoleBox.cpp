// Copyright (c) 2011, PG, All rights reserved.
#include "ConsoleBox.h"

#include <utility>

#include "AnimationHandler.h"
#include "CBaseUIButton.h"
#include "CBaseUIContainer.h"
#include "CBaseUIScrollView.h"
#include "CBaseUITextbox.h"
#include "ConVar.h"
#include "ConVarHandler.h"
#include "Console.h"
#include "Engine.h"
#include "Font.h"
#include "Logging.h"
#include "Environment.h"
#include "Keyboard.h"
#include "Graphics.h"
#include "MakeDelegateWrapper.h"
#include "Mouse.h"
#include "Timing.h"

class ConsoleBoxTextbox : public CBaseUITextbox {
   public:
    ConsoleBoxTextbox(float xPos, float yPos, float xSize, float ySize, UString name)
        : CBaseUITextbox(xPos, yPos, xSize, ySize, std::move(name)) {}

    void setSuggestion(UString suggestion) { this->sSuggestion = std::move(suggestion); }

   protected:
    void drawText() override {
        if(cv::consolebox_draw_preview.getBool()) {
            if(this->sSuggestion.length() > 0 && this->sSuggestion.startsWith(this->sText)) {
                g->setColor(0xff444444);
                g->pushTransform();
                {
                    g->translate((int)(this->getPos().x + this->iTextAddX + this->fTextScrollAddX),
                                 (int)(this->getPos().y + this->iTextAddY));
                    g->drawString(this->font, this->sSuggestion);
                }
                g->popTransform();
            }
        }

        CBaseUITextbox::drawText();
    }

   private:
    UString sSuggestion;
};

class ConsoleBoxSuggestionButton : public CBaseUIButton {
   public:
    ConsoleBoxSuggestionButton(float xPos, float yPos, float xSize, float ySize, UString name, UString text,
                               UString helpText, const ConsoleBoxTextbox *const textbox)
        : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name), std::move(text)), cboxtbox(textbox) {
        this->sHelpText = std::move(helpText);
    }

   protected:
    void drawText() override {
        if(this->font == nullptr || this->getText().length() < 1) return;

        if(cv::consolebox_draw_helptext.getBool()) {
            if(this->sHelpText.length() > 0) {
                const UString helpTextSeparator = "-";
                const int helpTextOffset = std::round(2.0f * this->font->getStringWidth(helpTextSeparator) *
                                                      ((float)this->font->getDPI() / 96.0f));  // NOTE: abusing font dpi
                const int helpTextSeparatorStringWidth =
                    std::max(1, (int)this->font->getStringWidth(helpTextSeparator));
                const int helpTextStringWidth = std::max(1, (int)this->font->getStringWidth(this->sHelpText));

                g->pushTransform();
                {
                    const float scale = std::min(
                        1.0f, (std::max(1.0f, this->cboxtbox->getSize().x - this->fStringWidth - helpTextOffset * 1.5f -
                                                  helpTextSeparatorStringWidth * 1.5f)) /
                                  (float)helpTextStringWidth);

                    g->scale(scale, scale);
                    g->translate((int)(this->getPos().x + this->fStringWidth + helpTextOffset * scale / 2 +
                                       helpTextSeparatorStringWidth * scale),
                                 (int)(this->getPos().y + this->getSize().y / 2.0f + this->fStringHeight / 2.0f -
                                       this->font->getHeight() * (1.0f - scale) / 2.0f));
                    g->setColor(0xff444444);
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
    const ConsoleBoxTextbox *const cboxtbox;
    UString sHelpText;
};

ConsoleBox::ConsoleBox() : CBaseUIElement(0, 0, 0, 0, ""), fConsoleDelay(engine->getTime() + 0.2f) {
    const float dpiScale = env->getDPIScale();

    this->logFont = engine->getConsoleFont();

    this->textbox =
        std::make_unique<ConsoleBoxTextbox>(5.f * dpiScale, (float)engine->getScreenHeight(),
                                            engine->getScreenWidth() - 10.f * dpiScale, 26.f, "consoleboxtextbox");
    {
        this->textbox->setSizeY(this->textbox->getRelSize().y * dpiScale);
        this->textbox->setFont(engine->getDefaultFont());
        this->textbox->setDrawBackground(true);
        this->textbox->setVisible(false);
        this->textbox->setBusy(true);
    }

    this->suggestion = std::make_unique<CBaseUIScrollView>(5.f * dpiScale, (float)engine->getScreenHeight(),
                                                           engine->getScreenWidth() - 10.f * dpiScale, 90.f * dpiScale,
                                                           "consoleboxsuggestion");
    {
        this->suggestion->setDrawBackground(true);
        this->suggestion->setBackgroundColor(argb(255, 0, 0, 0));
        this->suggestion->setFrameColor(argb(255, 255, 255, 255));
        this->suggestion->setHorizontalScrolling(false);
        this->suggestion->setVerticalScrolling(true);
        this->suggestion->setVisible(false);
    }

    this->clearSuggestions();

    // setup convar callbacks
    cv::cmd::exec.setCallback(CFUNC(Console::execConfigFile));
    cv::cmd::showconsolebox.setCallback(SA::MakeDelegate<&ConsoleBox::show>(this));
    cv::cmd::clear.setCallback(SA::MakeDelegate<&ConsoleBox::clear>(this));

    if constexpr(Env::cfg(BUILD::DEBUG)) {
        // don't allow this to run in release builds, because it happens before client protection mechanisms are set up
        Console::execConfigFile("autoexec.cfg");
    }
}

ConsoleBox::~ConsoleBox() = default;

void ConsoleBox::draw() {
    // HACKHACK: legacy OpenGL fix
    g->setAntialiasing(false);

    g->pushTransform();
    {
        if(mouse->isMiddleDown()) g->translate(0, mouse->getPos().y - engine->getScreenHeight());

        if(cv::console_overlay.getBool() || this->textbox->isVisible()) this->drawLogOverlay();

        if(this->fConsoleAnimation.animating()) {
            g->push3DScene(McRect(this->textbox->getPos().x, this->textbox->getPos().y, this->textbox->getSize().x,
                                  this->textbox->getSize().y));
            {
                g->rotate3DScene(((this->fConsoleAnimation / this->getAnimTargetY()) * 130 - 130), 0, 0);
                g->translate3DScene(0, 0, ((this->fConsoleAnimation / this->getAnimTargetY()) * 500 - 500));
                this->textbox->draw();
                this->suggestion->draw();
            }
            g->pop3DScene();
        } else {
            this->suggestion->draw();
            this->textbox->draw();
        }
    }
    g->popTransform();
}

void ConsoleBox::drawLogOverlay() {
    Sync::scoped_lock logGuard(this->logMutex);

    const float dpiScale = this->getDPIScale();

    const float logScale = std::round(dpiScale + 0.255f) * cv::console_overlay_scale.getFloat();

    const int shadowOffset = 1 * logScale;

    g->setColor(0xff000000);
    const float alpha =
        1.0f - (this->fLogYPos / (this->logFont->getHeight() * (cv::console_overlay_lines.getInt() + 1)));
    if(this->fLogYPos != 0.0f) g->setAlpha(alpha);

    g->pushTransform();
    {
        g->scale(logScale, logScale);
        g->translate(2 * logScale + shadowOffset, -this->fLogYPos + shadowOffset);
        for(size_t i = 0; i < this->log_entries.size(); i++) {
            g->translate(0, (int)((this->logFont->getHeight() + (i == 0 ? 0 : 2) + 1) * logScale));
            g->drawString(this->logFont, this->log_entries[i].text);
        }
    }
    g->popTransform();

    g->setColor(0xffffffff);
    if(this->fLogYPos != 0.0f) g->setAlpha(alpha);

    g->pushTransform();
    {
        g->scale(logScale, logScale);
        g->translate(2 * logScale, -this->fLogYPos);
        for(size_t i = 0; i < this->log_entries.size(); i++) {
            g->translate(0, (int)((this->logFont->getHeight() + (i == 0 ? 0 : 2) + 1) * logScale));
            g->setColor(Color(this->log_entries[i].textColor).setA(alpha));

            g->drawString(this->logFont, this->log_entries[i].text);
        }
    }
    g->popTransform();
}

void ConsoleBox::processPendingLogAnimations() {
    // check if we have pending animation reset from logging thread
    if(this->bLogAnimationResetPending.exchange(false)) {
        // execute animation operations on main thread only
        this->fLogYPos.stop();
        this->fLogYPos = 0.f;
        this->fLogTime = this->fPendingLogTime.load(std::memory_order_acquire);
    }
}

void ConsoleBox::update(CBaseUIEventCtx &c) {
    // handle textbox focus first
    this->textbox->update(c);

    CBaseUIElement::update(c);

    // handle pending animation operations from logging threads
    processPendingLogAnimations();

    const bool mleft = mouse->isLeftDown();

    if(this->textbox->hitEnter()) {
        this->processCommand(this->textbox->getText().toUtf8());
        Logger::flush();  // make sure it's output immediately
        this->textbox->clear();
        this->textbox->setSuggestion("");
    }

    if(this->bConsoleAnimateOnce) {
        if(engine->getTime() > this->fConsoleDelay) {
            this->bConsoleAnimateIn = true;
            this->bConsoleAnimateOnce = false;
            this->textbox->setVisible(true);
        }
    }

    if(this->bConsoleAnimateIn) {
        if(this->fConsoleAnimation < this->getAnimTargetY() &&
           std::round((this->fConsoleAnimation / this->getAnimTargetY()) * 500) < 500.0f)
            this->textbox->setPosY(engine->getScreenHeight() - this->fConsoleAnimation);
        else {
            this->bConsoleAnimateIn = false;
            this->fConsoleAnimation.stop();
            this->fConsoleAnimation = this->getAnimTargetY();
            this->textbox->setPosY(engine->getScreenHeight() - this->fConsoleAnimation);
            this->textbox->setActive(true);
        }
    }

    if(this->bConsoleAnimateOut) {
        if(this->fConsoleAnimation > 0.0f &&
           std::round((this->fConsoleAnimation / this->getAnimTargetY()) * 500) > 0.0f)
            this->textbox->setPosY(engine->getScreenHeight() - this->fConsoleAnimation);
        else {
            this->bConsoleAnimateOut = false;
            this->textbox->setVisible(false);
            this->fConsoleAnimation.stop();
            this->fConsoleAnimation = 0.0f;
            this->textbox->setPosY(engine->getScreenHeight());
        }
    }

    // handle suggestions
    if(this->suggestion->isVisible()) this->suggestion->update(c);

    if(this->bSuggestionAnimateOut) {
        if(this->fSuggestionAnimation <= this->fSuggestionY) {
            this->suggestion->setPosY(engine->getScreenHeight() - (this->fSuggestionY - this->fSuggestionAnimation));
            this->fSuggestionAnimation += cv::consolebox_animspeed.getFloat();
        } else {
            this->bSuggestionAnimateOut = false;
            this->fSuggestionAnimation = this->fSuggestionY;
            this->suggestion->setVisible(false);
            this->suggestion->setPosY(engine->getScreenHeight());
        }
    }

    if(this->bSuggestionAnimateIn) {
        if(this->fSuggestionAnimation >= 0) {
            this->suggestion->setPosY(engine->getScreenHeight() - (this->fSuggestionY - this->fSuggestionAnimation));
            this->fSuggestionAnimation -= cv::consolebox_animspeed.getFloat();
        } else {
            this->bSuggestionAnimateIn = false;
            this->fSuggestionAnimation = 0.0f;
            this->suggestion->setPosY(engine->getScreenHeight() - this->fSuggestionY);
        }
    }

    if(mleft) {
        if(!this->suggestion->isMouseInside() && !this->textbox->isActive() && !this->suggestion->isBusy())
            this->suggestion->setVisible(false);

        if(this->textbox->isActive() && this->textbox->isMouseInside() && this->iSuggestionCount > 0)
            this->suggestion->setVisible(true);
    }

    // handle overlay animation and timeout
    // theres probably a better way to do it than yet another atomic boolean, but eh
    const bool forceVisible =
        cv::console_overlay_timeout.getFloat() == 0.f /* infinite timeout */ || this->bForceLogVisible.exchange(false);

    if(!forceVisible && engine->getTime() > this->fLogTime) {
        if(!this->fLogYPos.animating() && this->fLogYPos == 0.0f)
            this->fLogYPos.set(this->logFont->getHeight() * (cv::console_overlay_lines.getFloat() + 1), 0.5f,
                               anim::QuadInOut);

        if(!this->bClearPending &&
           this->fLogYPos >= this->logFont->getHeight() * (cv::console_overlay_lines.getInt() + 1)) {
            this->bClearPending = true;
        }
    }

    if(this->bClearPending) {
        Sync::unique_lock lk(this->logMutex, Sync::try_to_lock);
        if(!lk.owns_lock()) return;  // we'll wait until we can acquire it without blocking (it's not crucial)

        this->bClearPending = false;
        this->log_entries.clear();
    }
}

void ConsoleBox::onSuggestionClicked(CBaseUIButton *suggestion) {
    UString text = suggestion->getName();

    ConVar *temp = cvars().getConVarByName(text.toUtf8(), false);
    if(temp != nullptr && (temp->canHaveValue() || temp->hasAnyNonVoidCallback())) text.append(" ");

    this->textbox->setSuggestion("");
    this->textbox->setText(text);
    this->textbox->setCursorPosRight();
    this->textbox->setActive(true);
}

void ConsoleBox::onKeyDown(KeyboardEvent &e) {
    // toggle visibility
    if((e == KEY_F1 && (this->textbox->isActive() && this->textbox->isVisible() && !this->bConsoleAnimateOut
                            ? true
                            : keyboard->isShiftDown())) ||
       (this->textbox->isActive() && this->textbox->isVisible() && !this->bConsoleAnimateOut && e == KEY_ESCAPE))
        this->toggle(e);

    if(this->bConsoleAnimateOut) return;

    // textbox
    this->textbox->onKeyDown(e);

    // suggestion + command history hotkey handling
    if(this->iSuggestionCount > 0 && this->textbox->isActive() && this->textbox->isVisible()) {
        // handle suggestion up/down buttons

        if(e == KEY_DOWN || (e == KEY_TAB && !keyboard->isShiftDown())) {
            if(this->iSelectedSuggestion < 1)
                this->iSelectedSuggestion = this->iSuggestionCount - 1;
            else
                this->iSelectedSuggestion--;

            if(this->iSelectedSuggestion > -1 && this->iSelectedSuggestion < this->vSuggestionButtons.size()) {
                UString command = this->vSuggestionButtons[this->iSelectedSuggestion]->getName();

                ConVar *temp = cvars().getConVarByName(command.toUtf8(), false);
                if(temp != nullptr && (temp->canHaveValue() || temp->hasAnyNonVoidCallback())) command.append(" ");

                this->textbox->setSuggestion("");
                this->textbox->setText(command);
                this->textbox->setCursorPosRight();
                this->suggestion->scrollToElement(this->vSuggestionButtons[this->iSelectedSuggestion]);

                for(size_t i = 0; i < this->vSuggestionButtons.size(); i++) {
                    if(i == this->iSelectedSuggestion) {
                        this->vSuggestionButtons[i]->setTextColor(0xff00ff00);
                        this->vSuggestionButtons[i]->setTextDarkColor(0xff000000);
                    } else
                        this->vSuggestionButtons[i]->setTextColor(0xffffffff);
                }
            }

            e.consume();
        } else if(e == KEY_UP || (e == KEY_TAB && keyboard->isShiftDown())) {
            if(this->iSelectedSuggestion > this->iSuggestionCount - 2)
                this->iSelectedSuggestion = 0;
            else
                this->iSelectedSuggestion++;

            if(this->iSelectedSuggestion > -1 && this->iSelectedSuggestion < this->vSuggestionButtons.size()) {
                UString command = this->vSuggestionButtons[this->iSelectedSuggestion]->getName();

                ConVar *temp = cvars().getConVarByName(command.toUtf8(), false);
                if(temp != nullptr && (temp->canHaveValue() || temp->hasAnyNonVoidCallback())) command.append(" ");

                this->textbox->setSuggestion("");
                this->textbox->setText(command);
                this->textbox->setCursorPosRight();
                this->suggestion->scrollToElement(this->vSuggestionButtons[this->iSelectedSuggestion]);

                for(size_t i = 0; i < this->vSuggestionButtons.size(); i++) {
                    if(i == this->iSelectedSuggestion) {
                        this->vSuggestionButtons[i]->setTextColor(0xff00ff00);
                        this->vSuggestionButtons[i]->setTextDarkColor(0xff000000);
                    } else
                        this->vSuggestionButtons[i]->setTextColor(0xffffffff);
                }
            }

            e.consume();
        }
    } else if(this->commandHistory.size() > 0 && this->textbox->isActive() && this->textbox->isVisible()) {
        // handle command history up/down buttons

        if(e == KEY_DOWN) {
            if(this->iSelectedHistory > this->commandHistory.size() - 2)
                this->iSelectedHistory = 0;
            else
                this->iSelectedHistory++;

            if(this->iSelectedHistory > -1 && this->iSelectedHistory < this->commandHistory.size()) {
                UString text{this->commandHistory[this->iSelectedHistory]};
                this->textbox->setSuggestion("");
                this->textbox->setText(text);
                this->textbox->setCursorPosRight();
            }

            e.consume();
        } else if(e == KEY_UP) {
            if(this->iSelectedHistory < 1)
                this->iSelectedHistory = this->commandHistory.size() - 1;
            else
                this->iSelectedHistory--;

            if(this->iSelectedHistory > -1 && this->iSelectedHistory < this->commandHistory.size()) {
                UString text{this->commandHistory[this->iSelectedHistory]};
                this->textbox->setSuggestion("");
                this->textbox->setText(text);
                this->textbox->setCursorPosRight();
            }

            e.consume();
        }
    }
}

void ConsoleBox::onChar(KeyboardEvent &e) {
    if(this->bConsoleAnimateOut && !this->bConsoleAnimateIn) return;
    if(e == KEY_TAB) return;

    this->textbox->onChar(e);

    if(this->textbox->isActive() && this->textbox->isVisible()) {
        // rebuild suggestion list
        this->clearSuggestions();

        std::vector<ConVar *> suggestions = cvars().getConVarByLetter(this->textbox->getText().toUtf8());
        for(const auto *suggestion : suggestions) {
            UString suggestionText{suggestion->getName()};

            if(suggestion->canHaveValue()) {
                switch(suggestion->getType()) {
                    case ConVar::CONVAR_TYPE::BOOL:
                        suggestionText.append(fmt::format(" {}", (int)suggestion->getBool()));
                        // suggestionText.append(fmt::format(" ( def. \"{}\" )",
                        // (int)(suggestions[i]->getDefaultFloat() > 0)));
                        break;
                    case ConVar::CONVAR_TYPE::INT:
                        suggestionText.append(fmt::format(" {}", suggestion->getInt()));
                        // suggestionText.append(fmt::format(" ( def. \"{}\" )",
                        // (int)suggestions[i]->getDefaultFloat()));
                        break;
                    case ConVar::CONVAR_TYPE::FLOAT:
                        suggestionText.append(fmt::format(" {:g}", suggestion->getFloat()));
                        // suggestionText.append(fmt::format(" ( def. \"{:g}\" )",
                        // suggestions[i]->getDefaultFloat()));
                        break;
                    case ConVar::CONVAR_TYPE::STRING:
                        suggestionText.append(" ");
                        suggestionText.append(suggestion->getString());
                        // suggestionText.append(" ( def. \"");
                        // suggestionText.append(suggestions[i]->getDefaultString());
                        // suggestionText.append("\" )");
                        break;
                }
            }
            this->addSuggestion(suggestionText, suggestion->getHelpstring(), suggestion->getName());
        }
        this->suggestion->setVisible(suggestions.size() > 0);

        if(suggestions.size() > 0) {
            this->suggestion->scrollToElement(this->suggestion->container.getElements()[0]);
            this->textbox->setSuggestion(suggestions[0]->getName());
        } else
            this->textbox->setSuggestion(u"");

        this->iSelectedSuggestion = -1;
    }
}

void ConsoleBox::onResolutionChange(vec2 newResolution) {
    const float dpiScale = this->getDPIScale();

    this->textbox->setSize(newResolution.x - 10 * dpiScale, this->textbox->getRelSize().y * dpiScale);
    this->textbox->setPos(5 * dpiScale, this->textbox->isVisible()
                                            ? newResolution.y - this->textbox->getSize().y - 6 * dpiScale
                                            : newResolution.y);

    this->suggestion->setPos(5 * dpiScale, newResolution.y - this->fSuggestionY);
    this->suggestion->setSizeX(newResolution.x - 10 * dpiScale);
}

void ConsoleBox::processCommand(const UString &command) {
    this->clearSuggestions();
    this->iSelectedHistory = -1;

    if(command.length() > 0) {
        this->commandHistory.push_back(command);

        Console::processCommand(command.utf8View());
    }
}

bool ConsoleBox::isBusy() {
    return (this->textbox->isBusy() || this->suggestion->isBusy()) && this->textbox->isVisible();
}

bool ConsoleBox::isActive() {
    return (this->textbox->isActive() || this->suggestion->isActive()) && this->textbox->isVisible();
}

void ConsoleBox::addSuggestion(const UString &text, const UString &helpText, const UString &command) {
    const float dpiScale = this->getDPIScale();

    const int vsize = this->vSuggestionButtons.size() + 1;
    const int bottomAdd = 3 * dpiScale;
    const int buttonheight = 22 * dpiScale;
    const int addheight = (17 + 8) * dpiScale;

    // create button and add it
    CBaseUIButton *button = new ConsoleBoxSuggestionButton(3 * dpiScale, (vsize - 1) * buttonheight + 2 * dpiScale, 100,
                                                           addheight, command, text, helpText, this->textbox.get());
    {
        button->setDrawFrame(false);
        button->setSizeX(button->getFont()->getStringWidth(text));
        button->setClickCallback(SA::MakeDelegate<&ConsoleBox::onSuggestionClicked>(this));
        button->setDrawBackground(false);
    }
    this->suggestion->container.addBaseUIElement(button);
    this->vSuggestionButtons.insert(this->vSuggestionButtons.begin(), button);

    // update suggestion size
    const int gap = 10 * dpiScale;
    this->fSuggestionY = std::clamp<float>(buttonheight * vsize, 0, buttonheight * 4) +
                         (engine->getScreenHeight() - this->textbox->getPos().y) + gap;

    if(buttonheight * vsize > buttonheight * 4) {
        this->suggestion->setSizeY(buttonheight * 4 + bottomAdd);
        this->suggestion->setScrollSizeToContent();
    } else {
        this->suggestion->setSizeY(buttonheight * vsize + bottomAdd);
        this->suggestion->setScrollSizeToContent();
    }

    this->suggestion->setPosY(engine->getScreenHeight() - this->fSuggestionY);

    this->iSuggestionCount++;
}

void ConsoleBox::clearSuggestions() {
    this->iSuggestionCount = 0;
    this->suggestion->container.freeElements();
    this->vSuggestionButtons = std::vector<CBaseUIButton *>();
    this->suggestion->setVisible(false);
}

void ConsoleBox::show() {
    if(!this->textbox->isVisible()) {
        KeyboardEvent fakeEvent(KEY_F1, 0, Timing::getTicksNS());
        this->toggle(fakeEvent);
    }
}

void ConsoleBox::toggle(KeyboardEvent &e) {
    if(this->textbox->isVisible() && !this->bConsoleAnimateIn && !this->bSuggestionAnimateIn) {
        this->bConsoleAnimateOut = true;
        this->fConsoleAnimation.set(0.0f, 0.25f, anim::QuartOut);

        if(this->suggestion->container.getElements().size() > 0) this->bSuggestionAnimateOut = true;

        e.consume();
    } else if(!this->bConsoleAnimateOut && !this->bSuggestionAnimateOut) {
        this->textbox->setVisible(true);
        this->textbox->setActive(true);
        this->textbox->setBusy(true);
        this->bConsoleAnimateIn = true;

        this->fConsoleAnimation.set(this->getAnimTargetY(), 0.15f, anim::QuartOut);

        if(this->suggestion->container.getElements().size() > 0) {
            this->bSuggestionAnimateIn = true;
            this->suggestion->setVisible(true);
        }

        e.consume();
    }

    // HACKHACK: force layout update
    this->onResolutionChange(engine->getScreenSize());
}

void ConsoleBox::log(const UString &text, Color textColor) {
    // lock is held by Logger::ConsoleBoxSink::flush_buffer_to_console

    // newlines must be stripped before being sent here (see Logging.cpp)
    assert(!text.endsWith(u'\n') && !text.endsWith(u'\r') && "Console log strings can't end with a newline.");

    // add log entry(ies, split on any newlines inside the string)
    if(text.find(u'\n') != -1) {
        auto stringVec = text.split(US_("\n"));
        this->log_entries.reserve(this->log_entries.size() + stringVec.size());
        for(const auto &entry : stringVec) {
            auto trimmed = entry.trim();
            if(trimmed.isEmpty()) {
                continue;
            }
            this->log_entries.push_back({trimmed, textColor});
        }
    } else {
        this->log_entries.push_back({text, textColor});
    }

    const auto maxLines = cv::console_overlay_lines.getVal<size_t>();
    while(this->log_entries.size() > maxLines) {
        this->log_entries.erase(this->log_entries.begin());
    }

    // defer animation operations to main thread to avoid data races
    // use force visibility flag to prevent immediate timeout on same frame (this is so dumb)
    const float timeout = cv::console_overlay_timeout.getFloat();
    this->fPendingLogTime.store(Timing::getTimeReal<float>() + timeout, std::memory_order_release);
    this->bForceLogVisible.store(true, std::memory_order_release);
    this->bLogAnimationResetPending.store(true, std::memory_order_release);
}

float ConsoleBox::getAnimTargetY() { return 32.0f * this->getDPIScale(); }

float ConsoleBox::getDPIScale() {
    return ((float)std::max(env->getDPI(), this->textbox->getFont()->getDPI()) / 96.0f);  // NOTE: abusing font dpi
}
