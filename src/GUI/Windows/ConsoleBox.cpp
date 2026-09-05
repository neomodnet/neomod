// Copyright (c) 2011, PG, All rights reserved.
#include "ConsoleBox.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "AnimationHandler.h"
#include "ConVar.h"
#include "Console.h"
#include "ConsoleWidgets.h"
#include "Engine.h"
#include "Font.h"
#include "Environment.h"
#include "Graphics.h"
#include "KeyBindings.h"
#include "Mouse.h"

ConsoleBox::ConsoleBox() : CBaseUIElement(0, 0, 0, 0, ""), fConsoleDelay(engine->getTime() + 0.2f) {
    const float dpiScale = env->getDPIScale();

    this->logFont = engine->getConsoleFont();

    this->suggestion = std::make_unique<ConsoleSuggestionList>(5.f * dpiScale, (float)engine->getScreenHeight(),
                                                               engine->getScreenWidth() - 10.f * dpiScale,
                                                               90.f * dpiScale, "consoleboxsuggestion");

    this->textbox = std::make_unique<ConsoleTextbox>(5.f * dpiScale, (float)engine->getScreenHeight(),
                                                     engine->getScreenWidth() - 10.f * dpiScale, 26.f,
                                                     "consoleboxtextbox", this->suggestion.get());
    {
        this->textbox->setSizeY(this->textbox->getRelSize().y * dpiScale);
        this->textbox->setFont(engine->getDefaultFont());
        this->textbox->setDrawBackground(true);
        this->textbox->setVisible(false);
        this->textbox->setBusy(true);
    }

    this->children = {this->textbox.get(), this->suggestion.get()};
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
    const float dpiScale = this->getDPIScale();

    const float logScale = Mc::consoleLogScale(dpiScale);

    const int shadowOffset = 1 * logScale;

    // the newest entries since the overlay was last emptied, at most console_overlay_lines of them
    const Console::LogRange range = Console::getLogRange();
    const auto maxLines = static_cast<u64>(std::max(0, cv::console_overlay_lines.getInt()));
    const u64 first = std::max({this->iOverlayFirst, range.first, range.next - std::min(maxLines, range.next)});
    if(first >= range.next) return;

    g->setColor(0xff000000);
    const float alpha =
        1.0f - (this->fLogYPos / (this->logFont->getHeight() * (cv::console_overlay_lines.getInt() + 1)));
    if(this->fLogYPos != 0.0f) g->setAlpha(alpha);

    g->pushTransform();
    {
        g->scale(logScale, logScale);
        g->translate(2 * logScale + shadowOffset, -this->fLogYPos + shadowOffset);
        for(u64 seq = first; seq < range.next; seq++) {
            g->translate(0, (int)((this->logFont->getHeight() + (seq == first ? 0 : 2) + 1) * logScale));
            g->drawString(this->logFont, Console::getLogEntry(seq).text);
        }
    }
    g->popTransform();

    g->setColor(0xffffffff);
    if(this->fLogYPos != 0.0f) g->setAlpha(alpha);

    g->pushTransform();
    {
        g->scale(logScale, logScale);
        g->translate(2 * logScale, -this->fLogYPos);
        for(u64 seq = first; seq < range.next; seq++) {
            const Console::LogEntry &entry = Console::getLogEntry(seq);
            g->translate(0, (int)((this->logFont->getHeight() + (seq == first ? 0 : 2) + 1) * logScale));
            g->setColor(Color(entry.color).setA(alpha));

            g->drawString(this->logFont, entry.text);
        }
    }
    g->popTransform();
}

void ConsoleBox::updateInput(CBaseUIEventCtx &c) {
    // self before children: visit order doubles as hit-candidate priority (latest = top-most)
    CBaseUIElement::updateInput(c);

    CBaseUIEventCtx::HitPathScope scope(c, this);

    this->textbox->updateInput(c);
    if(this->suggestion->isVisible()) this->suggestion->updateInput(c);
}

void ConsoleBox::tick() {
    CBaseUIElement::tick();
    this->textbox->tick();
    this->suggestion->tick();

    // new scrollback entries (re)start the overlay's fade timeout
    if(const Console::LogRange range = Console::getLogRange(); range.next != this->iLogSequence) {
        this->iLogSequence = range.next;
        this->fLogYPos.stop();
        this->fLogYPos = 0.f;
        this->fLogTime = engine->getTime() + cv::console_overlay_timeout.getFloat();
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
            this->textbox->requestFocus();
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

    // the popup sits right above the textbox, as tall as its (up to 4) rows of matches
    if(this->suggestion->isVisible()) {
        const int gap = 10 * this->getDPIScale();
        this->suggestion->setPosY(this->textbox->getPos().y - gap -
                                  std::min(this->suggestion->getCount(), 4) * this->suggestion->getRowHeight());
    }

    // handle overlay animation and timeout
    const bool forceVisible = cv::console_overlay_timeout.getFloat() == 0.f; /* infinite timeout */

    if(!forceVisible && engine->getTime() > this->fLogTime) {
        if(!this->fLogYPos.animating() && this->fLogYPos == 0.0f)
            this->fLogYPos.set(this->logFont->getHeight() * (cv::console_overlay_lines.getFloat() + 1), 0.5f,
                               anim::QuadInOut);

        if(this->fLogYPos >= this->logFont->getHeight() * (cv::console_overlay_lines.getInt() + 1))
            this->iOverlayFirst = this->iLogSequence;  // faded out: nothing shows until the next entry
    }
}

void ConsoleBox::onKeyDown(KeyboardEvent &e) {
    if(this->isOpen() && e == KEY_ESCAPE && this->toggle()) e.consume();

    if(this->bConsoleAnimateOut) return;

    this->textbox->onKeyDown(e);
}

void ConsoleBox::onChar(KeyboardEvent &e) {
    if(this->bConsoleAnimateOut && !this->bConsoleAnimateIn) return;

    this->textbox->onChar(e);
}

void ConsoleBox::onResolutionChange(vec2 newResolution) {
    const float dpiScale = this->getDPIScale();

    this->textbox->setSize(newResolution.x - 10 * dpiScale, this->textbox->getRelSize().y * dpiScale);
    this->textbox->setPos(5 * dpiScale, this->textbox->isVisible()
                                            ? newResolution.y - this->textbox->getSize().y - 6 * dpiScale
                                            : newResolution.y);

    this->suggestion->setPosX(5 * dpiScale);  // its y follows the textbox in tick()
    this->suggestion->setSizeX(newResolution.x - 10 * dpiScale);
}

bool ConsoleBox::isBusy() {
    return (this->textbox->isBusy() || this->suggestion->isBusy()) && this->textbox->isVisible();
}

bool ConsoleBox::isActive() {
    // the box is "active" exactly while it is shown: it is the keyboard focus target whenever open
    // (toggle requestFocus/stealFocus) and its textbox is perma-busy, so visibility is the gate
    return this->textbox->isVisible();
}

bool ConsoleBox::isOpen() const { return this->textbox->isVisible() && !this->bConsoleAnimateOut; }

void ConsoleBox::show() {
    if(!this->textbox->isVisible()) this->toggle();
}

void ConsoleBox::hide() {
    if(this->isOpen()) this->toggle();
}

bool ConsoleBox::toggle() {
    bool toggled = true;

    if(this->textbox->isVisible() && !this->bConsoleAnimateIn) {
        this->bConsoleAnimateOut = true;
        this->fConsoleAnimation.set(0.0f, 0.25f, anim::QuartOut);

        // release the keyboard (the suggestion popup goes with it): the box keeps drawing through the close
        // animation, but it is no longer the focus target (onKeyDown/onChar early-out while animating out anyway)
        this->textbox->stealFocus();
    } else if(!this->bConsoleAnimateOut) {
        this->textbox->setVisible(true);
        this->textbox->requestFocus();
        this->textbox->setBusy(true);
        this->bConsoleAnimateIn = true;

        this->fConsoleAnimation.set(this->getAnimTargetY(), 0.15f, anim::QuartOut);
    } else
        toggled = false;

    // HACKHACK: force layout update
    this->onResolutionChange(engine->getScreenSize());

    return toggled;
}

float ConsoleBox::getAnimTargetY() { return 32.0f * this->getDPIScale(); }

float ConsoleBox::getDPIScale() {
    return ((float)std::max(env->getDPI(), this->textbox->getFont()->getDPI()) / 96.0f);  // NOTE: abusing font dpi
}
