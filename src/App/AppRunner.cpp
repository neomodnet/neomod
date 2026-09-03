// Copyright (c) 2026, WH, All rights reserved.
#include "AppRunner.h"
#ifdef MCENGINE_TESTS

#include "AppDescriptor.h"

#include "Engine.h"
#include "Mouse.h"
#include "Graphics.h"
#include "Font.h"
#include "Logging.h"
#include "KeyBindings.h"

#include <cstring>

using namespace Mc;

AppRunner::AppRunner(bool testMode, std::string_view appName) : m_bTestMode(testMode) {
    mouse->addListener(this);

    if(!m_bTestMode) {
        // normal mode: auto-launch default app
        launchApp(getDefaultAppDescriptor().name);
        return;
    }

    // test mode
    for(const auto &entry : getAllAppDescriptors()) {
        if(appName == entry.name) {
            launchApp(entry.name);
            return;
        }
    }
    debugLog("unknown app '{:s}', showing selection screen", appName);
}

AppRunner::~AppRunner() {
    m_activeApp.reset();
    mouse->removeListener(this);
}

void AppRunner::launchApp(std::string_view name) {
    m_activeApp.reset();

    for(const auto &entry : getAllAppDescriptors()) {
        if(name == entry.name) {
            if(Env::cfg(BUILD::DEBUG)) {
                debugLog("launching app: {:s}", name);
            }
            m_activeApp.reset(entry.create());
            return;
        }
    }
}

void AppRunner::returnToMenu() {
    // allow vetoing shutdown (this can also run some pre-destruction things, or a "confirm to exit" thing)
    if(m_activeApp->onShutdown()) {
        m_activeApp.reset();
        m_iHoveredIndex = -1;
    }
}

void AppRunner::draw() {
    if(m_activeApp) {
        m_activeApp->draw();
        return;
    }

    // selection screen
    McFont *font = engine->getDefaultFont();
    if(!font) return;

    const auto allApps = getAllAppDescriptors();
    const float lineHeight = font->getHeight() * 1.5f;
    const float startX = 50.f;
    const float startY = 50.f + font->getHeight();

    // title
    g->setColor(0xffffffff);
    g->pushTransform();
    g->translate(startX, startY);
    g->drawString(font, "apps (ESC to return here)");
    g->popTransform();

    // list entries
    for(size_t i = 0; i < allApps.size(); i++) {
        const float y = startY + lineHeight * (float)(i + 1);

        if((int)i == m_iHoveredIndex) {
            g->setColor(0x40ffffff);
            g->fillRect((int)(startX - 5.f), (int)(y - font->getHeight()),
                        (int)(font->getStringWidth(allApps[i].name) + 10.f), (int)(font->getHeight() + 4.f));
        }

        g->setColor((int)i == m_iHoveredIndex ? 0xff00ffff : 0xffcccccc);
        g->pushTransform();
        g->translate(startX, y);
        g->drawString(font, allApps[i].name);
        g->popTransform();
    }
}

void AppRunner::update() {
    if(m_activeApp) {
        m_activeApp->update();
        return;
    }

    // hit test for selection screen
    McFont *font = engine->getDefaultFont();
    if(!font) return;

    const auto allApps = getAllAppDescriptors();
    const float lineHeight = font->getHeight() * 1.5f;
    const float startX = 50.f;
    const float startY = 50.f + font->getHeight();

    vec2 mousePos = mouse->getPos();
    m_iHoveredIndex = -1;

    for(size_t i = 0; i < allApps.size(); i++) {
        const float y = startY + lineHeight * (float)(i + 1);
        const float left = startX - 5.f;
        const float top = y - font->getHeight();
        const float width = font->getStringWidth(allApps[i].name) + 10.f;
        const float height = font->getHeight() + 4.f;

        if(mousePos.x >= left && mousePos.x <= left + width && mousePos.y >= top && mousePos.y <= top + height) {
            m_iHoveredIndex = (int)i;
            break;
        }
    }
}

void AppRunner::onKeyDown(KeyboardEvent &e) {
    if(m_activeApp) {
        m_activeApp->onKeyDown(e);
        if(m_bTestMode && !e.isConsumed() && e.getScanCode() == KEY_ESCAPE) {
            returnToMenu();
            e.consume();
        }
    }
}

void AppRunner::onKeyUp(KeyboardEvent &e) {
    if(m_activeApp) {
        m_activeApp->onKeyUp(e);
    }
}

void AppRunner::onChar(KeyboardEvent &e) {
    if(m_activeApp) {
        m_activeApp->onChar(e);
    }
}

void AppRunner::onButtonChange(ButtonEvent &event) {
    // this inconsistency should be fixed, App should just inherit from both MouseListener and KeyboardListener so
    // we're not implicitly expecting the active app to register itself (and let the engine register the apprunner instead)
    // (addMouse/KeyboardListener should be only accessible through Engine TBH...)
    if(m_activeApp) return;

    const auto allApps = getAllAppDescriptors();

    // selection screen click
    if(flags::has<MouseButtonFlags::MF_LEFT>(event.btn) && m_iHoveredIndex >= 0 &&
       m_iHoveredIndex < (int)allApps.size()) {
        if(event.down) {
            m_iMouseDownIndex = m_iHoveredIndex;
        } else if(m_iMouseDownIndex == m_iHoveredIndex) {
            launchApp(allApps[m_iMouseDownIndex].name);
        }
    }
}

void AppRunner::onWheelVertical(int delta) {
    if(m_activeApp) return;
    (void)delta;
}

void AppRunner::onWheelHorizontal(int delta) {
    if(m_activeApp) return;
    (void)delta;
}

void AppRunner::onResolutionChanged(vec2 newResolution) {
    if(m_activeApp) m_activeApp->onResolutionChanged(newResolution);
}

void AppRunner::onDPIChanged() {
    if(m_activeApp) m_activeApp->onDPIChanged();
}

void AppRunner::onFocusGained() {
    if(m_activeApp) m_activeApp->onFocusGained();
}

void AppRunner::onFocusLost() {
    if(m_activeApp) m_activeApp->onFocusLost();
}

void AppRunner::onMinimized() {
    if(m_activeApp) m_activeApp->onMinimized();
}

void AppRunner::onRestored() {
    if(m_activeApp) m_activeApp->onRestored();
}

bool AppRunner::onShutdown() {
    if(m_activeApp) return m_activeApp->onShutdown();
    return true;
}

bool AppRunner::isInGameplay() const {
    if(m_activeApp) return m_activeApp->isInGameplay();
    return false;
}

bool AppRunner::isInUnpausedGameplay() const {
    if(m_activeApp) return m_activeApp->isInUnpausedGameplay();
    return false;
}

Sound *AppRunner::getSound(ActionSound action) const {
    if(m_activeApp) return m_activeApp->getSound(action);
    return nullptr;
}

void AppRunner::showNotification(const NotificationInfo &info) {
    if(m_activeApp) m_activeApp->showNotification(info);
}

#endif  // MCENGINE_TESTS
