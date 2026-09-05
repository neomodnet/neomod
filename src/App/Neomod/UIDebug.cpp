// Copyright (c)  2026, WH, All rights reserved.
#include "UIDebug.h"
#include "UI.h"
#include "UIScreen.h"

#include "ConVar.h"
#include "ConVarHandler.h"
#include "MakeDelegateWrapper.h"

#include "CBaseUIScrollView.h"
#include "CBaseUITextbox.h"
#include "PromptOverlay.h"
#include "Parsing.h"
#include "SString.h"
#include "Bancho.h"
#include "Logging.h"
#include "Mouse.h"
#include "Engine.h"
#include "Environment.h"
#include "Osu.h"
#include "Graphics.h"

#include <array>
#include <cmath>

namespace cv {
// callbacks only set after initialized
// for debugging/testing
static ConVar set_active_ui_screen("set_active_ui_screen", CLIENT | NOLOAD | NOSAVE);
static ConVar ui_screens_cmd("ui_screens", CLIENT | NOLOAD | NOSAVE);
static ConVar ui_dump_cmd("ui_dump", CLIENT | NOLOAD | NOSAVE);
static ConVar ui_assert_cmd("ui_assert", CLIENT | NOLOAD | NOSAVE);
static ConVar ui_prompt_cmd("ui_prompt", CLIENT | NOLOAD | NOSAVE);
static ConVar debug_fake_online_cmd("debug_fake_online", CLIENT | NOLOAD | NOSAVE);
static ConVar debug_fake_room_cmd("debug_fake_room", CLIENT | NOLOAD | NOSAVE);
}  // namespace cv

UIScreen *UIDebug::findScreenByName(std::string_view lowerName) const {
    if(lowerName == "changelog"sv) lowerName = "aboutscreen";  // alias (old name)
    for(size_t i = 0; i < UI::SCREEN_NAMES.size(); ++i) {
        if(UI::SCREEN_NAMES[i] == lowerName) return m_ui->screens[i];
    }
    return nullptr;
}

void UIDebug::setScreenByName(std::string_view screenGetterNameWithoutGet) {
    const std::string lowerName = SString::to_lower(screenGetterNameWithoutGet);
    UIScreen *toSet = (lowerName == "dummy"sv) ? nullptr : this->findScreenByName(lowerName);

    if(toSet) {
        m_ui->setScreen(toSet);
    } else {
        debugLog("Invalid screen {}", screenGetterNameWithoutGet);
    }
}

namespace {
void dumpElementTree(CBaseUIElement *e, int depth) {
    const McRect &r = e->getRect();
    logRaw("{:{}}{} rect=({},{},{},{}) vis={:d} act={:d} busy={:d} en={:d} hover={:d}", "", depth * 2,
           CBaseUIDebug::elemName(e), (int)r.getX(), (int)r.getY(), (int)r.getWidth(), (int)r.getHeight(),
           e->isVisible(), e->isActive(), e->isBusy(), e->isEnabled(), e->isMouseInside());
    for(auto *c : e->getAllChildren()) dumpElementTree(c, depth + 1);
}

CBaseUIElement *findElementByName(CBaseUIElement *e, std::string_view name) {
    if(CBaseUIDebug::elemName(e) == name) return e;
    for(auto *c : e->getAllChildren()) {
        if(auto *found = findElementByName(c, name)) return found;
    }
    return nullptr;
}
}  // namespace

void UIDebug::debugDumpScreens() {
    logRaw("==== UI SCREENS frame={} ====", engine->getFrameCount());
    logRaw("engineScreen={} gResolution={} virtScreen={} mousePos={} mouseViewport={}", engine->getScreenSize(),
           g->getResolution(), osu->getVirtScreenSize(), mouse->getPos(), mouse->getAppViewport());
    // flags: M = modal, C = closeOnScreenSwitch
    for(size_t i = 0; i < m_ui->screens.size(); ++i) {
        auto *s = m_ui->screens[i];
        logRaw("[{:2}] {:<22} visible={:d} active_screen={:d} flags={}{}", i, UI::SCREEN_NAMES[i],
               s ? s->isVisible() : false, s == m_ui->active_screen, (s && s->isModal()) ? 'M' : '-',
               (s && s->closesOnScreenSwitch()) ? 'C' : '-');
    }
    for(size_t i = 0; i < m_ui->extra_overlays.size(); ++i) {
        auto *o = m_ui->extra_overlays[i];
        logRaw("[ov{}] {} visible={:d} flags={}{}", i, CBaseUIDebug::elemName(o), o->isVisible(),
               o->isModal() ? 'M' : '-', o->closesOnScreenSwitch() ? 'C' : '-');
    }
    std::string order;
    for(size_t li = 0; li < UI::NUM_SCREENS; ++li) {
        if(li == UI::EXTRAS_SPLICE) order += "[extra_overlays] < ";
        order += UI::SCREEN_NAMES[UI::LAYER_ORDER[li]];
        if(li + 1 < UI::NUM_SCREENS) order += " < ";
    }
    logRaw("layer order (bottom->top): {}", order);
    logRaw("==== END UI SCREENS ====");
}

void UIDebug::debugDumpElements(std::string_view screenName) {
    std::string lowerName = SString::to_lower(screenName);
    SString::trim_inplace(lowerName);
    if(lowerName == "engine"sv) {
        // the engine gui root (console, profiler) lives outside the screen table
        logRaw("==== UI DUMP engine frame={} ====", engine->getFrameCount());
        dumpElementTree(engine->getGUI(), 0);
        logRaw("==== END UI DUMP ====");
        return;
    }
    UIScreen *screen = lowerName.empty() ? m_ui->active_screen : this->findScreenByName(lowerName);
    if(!screen) {
        logRaw("ui_dump: unknown screen '{}'", screenName);
        return;
    }
    logRaw("==== UI DUMP {} frame={} ====", lowerName.empty() ? "<active>"sv : std::string_view{lowerName},
           engine->getFrameCount());
    dumpElementTree(screen, 0);
    logRaw("==== END UI DUMP ====");
}

void UIDebug::debugAssert(std::string_view args) {
    // ui_assert <exists|visible|hovered|active|screen_active> <name> <0|1>
    // ui_assert text <name> <expected>
    // ui_assert convar <name> <expected>
    // ui_assert mouse_at <x> <y> <tolerance>
    // ui_assert os_cursor <visible|clipped> <0|1>
    auto parts = SString::split(args, ' ');
    std::erase_if(parts, [](std::string_view p) { return p.empty(); });

    if(parts.size() < 3) {
        logRaw("UITEST FAIL ui_assert '{}' (usage: ui_assert <pred> <name> <0|1> | ui_assert mouse_at <x> <y> <tol>)",
               args);
        return;
    }

    const std::string pred = SString::to_lower(parts[0]);

    if(pred == "mouse_at"sv) {
        if(parts.size() < 4) {
            logRaw("UITEST FAIL mouse_at '{}' (usage: ui_assert mouse_at <x> <y> <tol>)", args);
            return;
        }
        const auto x = Parsing::strto<float>(parts[1]);
        const auto y = Parsing::strto<float>(parts[2]);
        const auto tol = Parsing::strto<float>(parts[3]);
        const vec2 mp = mouse->getPos();
        if(std::abs(mp.x - x) <= tol && std::abs(mp.y - y) <= tol)
            logRaw("UITEST OK mouse_at ({}, {}) actual={}", x, y, mp);
        else
            logRaw("UITEST FAIL mouse_at expected=({}, {}) tol={} actual={}", x, y, tol, mp);
        return;
    }

    if(pred == "os_cursor"sv) {
        // the os cursor state the engine settled on (headless keeps the state without an actual cursor)
        const bool expected = Parsing::strto<int>(parts[2]) != 0;
        bool actual{};
        if(parts[1] == "visible"sv)
            actual = env->isCursorVisible();
        else if(parts[1] == "clipped"sv)
            actual = env->isCursorClipped();
        else {
            logRaw("UITEST FAIL os_cursor {} (unknown state, expected visible|clipped)", parts[1]);
            return;
        }
        logRaw("UITEST {} os_cursor {} expected={:d} actual={:d}", actual == expected ? "OK" : "FAIL", parts[1],
               expected, actual);
        return;
    }

    if(pred == "convar"sv) {
        ConVar *var = cvars().getConVarByName(parts[1], false);
        if(!var) {
            logRaw("UITEST FAIL convar {} (not found)", parts[1]);
            return;
        }
        const auto &actualVal = var->getString();
        logRaw("UITEST {} convar {} expected='{}' actual='{}'", actualVal == parts[2] ? "OK" : "FAIL", parts[1],
               parts[2], actualVal);
        return;
    }

    const std::string lowerName = SString::to_lower(parts[1]);
    const bool expected = Parsing::strto<int>(parts[2]) != 0;

    if(pred == "screen_active"sv) {
        UIScreen *screen = this->findScreenByName(lowerName);
        if(!screen) {
            logRaw("UITEST FAIL screen_active {} (unknown screen)", parts[1]);
            return;
        }
        const bool actual = (screen == m_ui->active_screen);
        logRaw("UITEST {} screen_active {} expected={:d} actual={:d}", actual == expected ? "OK" : "FAIL", parts[1],
               expected, actual);
        return;
    }

    // screens can be addressed by table name, everything else by element name (case-sensitive)
    CBaseUIElement *elem = this->findScreenByName(lowerName);
    if(!elem) {
        for(auto *screen : m_ui->screens) {
            if((elem = findElementByName(screen, parts[1])) != nullptr) break;
        }
    }
    if(!elem) {
        for(auto *overlay : m_ui->extra_overlays) {
            if((elem = findElementByName(overlay, parts[1])) != nullptr) break;
        }
    }
    if(!elem) elem = findElementByName(engine->getGUI(), parts[1]);

    if(pred == "exists"sv) {
        const bool actual = (elem != nullptr);
        logRaw("UITEST {} exists {} expected={:d} actual={:d}", actual == expected ? "OK" : "FAIL", parts[1], expected,
               actual);
        return;
    }

    if(!elem) {
        logRaw("UITEST FAIL {} {} (element not found)", pred, parts[1]);
        return;
    }

    if(pred == "text"sv) {
        // ui_assert text <name> <expected> (expected may not contain spaces)
        auto *tb = dynamic_cast<CBaseUITextbox *>(elem);
        if(!tb) {
            logRaw("UITEST FAIL text {} (not a textbox)", parts[1]);
            return;
        }
        const std::string_view actualText = tb->getText();
        logRaw("UITEST {} text {} expected='{}' actual='{}'", actualText == parts[2] ? "OK" : "FAIL", parts[1],
               parts[2], actualText);
        return;
    }

    bool actual{};
    if(pred == "visible"sv)
        actual = elem->isVisible();
    else if(pred == "hovered"sv)
        actual = elem->isMouseInside();
    else if(pred == "active"sv)
        actual = elem->isActive();
    else {
        logRaw("UITEST FAIL {} {} (unknown predicate)", pred, parts[1]);
        return;
    }

    logRaw("UITEST {} {} {} expected={:d} actual={:d}", actual == expected ? "OK" : "FAIL", pred, parts[1], expected,
           actual);
}

void UIDebug::debugPrompt(std::string_view msg) {
    // scripted stand-in for the (online-only) real prompt() callers; logs the response for trace asserts
    m_ui->promptoverlay->prompt(std::string{msg}, SA::MakeDelegate([](std::string_view response) -> void {
                                    logRaw("uiprompt response='{}'", response);
                                }));
}

void UIDebug::debugFakeOnline(std::string_view arg) {
    // headless stand-in for the (server-dependent) login flow, so online-gated UI is testable.
    // no arg or non-"0" enables; "0" disables.
    std::string a{arg};
    SString::trim_inplace(a);
    BanchoState::set_fake_online(a.empty() || a != "0");
}

void UIDebug::debugFakeRoom() { BanchoState::fake_join_room(); }

UIDebug::UIDebug(UI *ui_parent) : m_ui(ui_parent) {
    cv::set_active_ui_screen.setCallback(SA::MakeDelegate<&UIDebug::setScreenByName>(this));
    cv::ui_screens_cmd.setCallback(SA::MakeDelegate<&UIDebug::debugDumpScreens>(this));
    cv::ui_dump_cmd.setCallback(SA::MakeDelegate<&UIDebug::debugDumpElements>(this));
    cv::ui_assert_cmd.setCallback(SA::MakeDelegate<&UIDebug::debugAssert>(this));
    cv::ui_prompt_cmd.setCallback(SA::MakeDelegate<&UIDebug::debugPrompt>(this));
    cv::debug_fake_online_cmd.setCallback(SA::MakeDelegate<&UIDebug::debugFakeOnline>(this));
    cv::debug_fake_room_cmd.setCallback(SA::MakeDelegate<&UIDebug::debugFakeRoom>(this));
}

UIDebug::~UIDebug() {
    cv::set_active_ui_screen.removeAllCallbacks();
    cv::ui_screens_cmd.removeAllCallbacks();
    cv::ui_dump_cmd.removeAllCallbacks();
    cv::ui_assert_cmd.removeAllCallbacks();
    cv::ui_prompt_cmd.removeAllCallbacks();
    cv::debug_fake_online_cmd.removeAllCallbacks();
    cv::debug_fake_room_cmd.removeAllCallbacks();
}