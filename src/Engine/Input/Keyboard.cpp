// Copyright (c) 2015, PG, 2026, WH, All rights reserved.
#include "Keyboard.h"

#include "Engine.h"
#include "Environment.h"
#include "KeyboardEvent.h"
#include "KeyboardListener.h"
#include "TextInput.h"
#include "Timing.h"
#include "UniString.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {
struct OSEvent {
    enum class Type : u8 { KEY_DOWN, KEY_UP, TEXT, EDITING };

    Type type;
    bool isRepeat{false};
    SCANCODE scancode{0};
    KEYMOD mods{0};
    KEYCODE keycode{0};
    i32 editStart{0};
    i32 editLength{0};
    u64 timestamp{0};
    std::string text{};
};
}  // namespace

struct Keyboard::KeyboardImpl {
    void dispatchKeyDown(KeyboardEvent event);
    void dispatchKeyUp(KeyboardEvent event);
    void deliverText(OSEvent &ev);
    void setComposition(const OSEvent &ev);

    std::vector<KeyboardListener *> listeners;

    std::vector<OSEvent> eventQueue;

    // global keyboard modifier state as of the last keydown/up event
    KEYMOD modstate{};

    // the target offered this frame
    TextInputClient *offer{nullptr};
    TextInputArea offerArea{};
    bool offerAmbient{false};

    // the text input session: the target the os text input was started for, and how
    TextInputClient *target{nullptr};
    TextInputType targetType{TextInputType::TEXT};
    bool targetAmbient{false};
    bool active{false};
    // the IME still holds a composition that was committed here, or the target died: start over
    bool restartPending{false};
    // text stamped earlier was typed for an older session
    u64 sessionStart{0};
    TextInputArea area{};

    TextComposition composition;
    // the IME composed since the last key reached the app: what it commits is text even with Ctrl held
    bool fromIme{false};
    // keys pressed while the IME composed: their releases go to it as well
    std::vector<SCANCODE> imeKeys;
};

Keyboard::Keyboard() : m_impl() { m_impl->eventQueue.reserve(8); }
Keyboard::~Keyboard() = default;

void Keyboard::draw() {}

void Keyboard::update() {
    auto &impl = *m_impl;
    for(auto &event : impl.eventQueue) {
        switch(event.type) {
            using enum OSEvent::Type;
            case KEY_DOWN:
                impl.modstate = event.mods;
                // while the IME composes, the keys are its own (a repeat belongs to whoever got the press)
                if(!impl.composition.text.empty()) {
                    if(!event.isRepeat) impl.imeKeys.push_back(event.scancode);
                    break;
                }
                std::erase(impl.imeKeys, event.scancode);
                impl.fromIme = false;
                impl.dispatchKeyDown({event.scancode, event.keycode, event.timestamp, event.mods, event.isRepeat});
                break;

            case KEY_UP:
                impl.modstate = event.mods;
                if(std::erase(impl.imeKeys, event.scancode) > 0) break;
                impl.dispatchKeyUp({event.scancode, event.keycode, event.timestamp, event.mods, event.isRepeat});
                break;

            case TEXT:
                impl.deliverText(event);
                break;

            case EDITING:
                impl.setComposition(event);
                break;
        }
    }
    impl.eventQueue.clear();
}

void Keyboard::onKeyEvent(u64 timestamp, u32 /*keyboardID*/, KEYCODE layoutDependentKeycode,
                          KEYMOD heldModifiersAsOfEvent, SCANCODE layoutIndependentScancode, bool isKeyDown,
                          bool isRepeatEvent) {
    m_impl->eventQueue.push_back({.type = isKeyDown ? OSEvent::Type::KEY_DOWN : OSEvent::Type::KEY_UP,
                                  .isRepeat = isRepeatEvent,
                                  .scancode = layoutIndependentScancode,
                                  .mods = heldModifiersAsOfEvent,
                                  .keycode = layoutDependentKeycode,
                                  .timestamp = timestamp});
}

void Keyboard::onTextEvent(u64 timestamp, const char *text) {
    // should not normally happen, but we can just ignore events with literally no text at all
    if(likely(text && text[0] != '\0')) {
        m_impl->eventQueue.push_back({.type = OSEvent::Type::TEXT, .timestamp = timestamp, .text = text});
    }
}

void Keyboard::onTextEditingEvent(u64 timestamp, const char *text, i32 start, i32 length) {
    m_impl->eventQueue.push_back({.type = OSEvent::Type::EDITING,
                                  .editStart = start,
                                  .editLength = length,
                                  .timestamp = timestamp,
                                  .text = text ? text : ""});
}

// actually, don't throw away queued events (otherwise we might not dispatch a queued key up for a key down we got)
void Keyboard::reset() {
    m_impl->modstate = env->getCurrentlyHeldKeyModifiers();
    // their releases may never come
    m_impl->imeKeys.clear();
}

void Keyboard::addListener(KeyboardListener *keyboardListener, bool insertOnTop) {
    if(keyboardListener == nullptr) {
        engine->showMessageError("Keyboard Error", "addListener(NULL)!");
        return;
    }

    if(insertOnTop)
        m_impl->listeners.insert(m_impl->listeners.begin(), keyboardListener);
    else
        m_impl->listeners.push_back(keyboardListener);
}

void Keyboard::removeListener(KeyboardListener *keyboardListener) { std::erase(m_impl->listeners, keyboardListener); }

bool Keyboard::areModsHeld(KEYMOD keymodMask) const { return (m_impl->modstate & keymodMask) == keymodMask; }

bool Keyboard::isControlDown() const { return !!(m_impl->modstate & KEYMOD_CONTROL); }
bool Keyboard::isAltDown() const { return !!(m_impl->modstate & KEYMOD_ALT); }
bool Keyboard::isShiftDown() const { return !!(m_impl->modstate & KEYMOD_SHIFT); }
bool Keyboard::isSuperDown() const { return !!(m_impl->modstate & KEYMOD_SUPER); }

void Keyboard::offerTextTarget(TextInputClient *target, const TextInputArea &area, bool ambient) {
    auto &impl = *m_impl;
    if(impl.offer != nullptr) return;
    impl.offer = target;
    impl.offerArea = area;
    impl.offerAmbient = ambient;
}

void Keyboard::applyTextTarget() {
    auto &impl = *m_impl;
    // nothing is typed into a window in the background
    if(!env->winFocused()) impl.offer = nullptr;

    const TextInputType type = impl.offer ? impl.offer->getTextInputType() : TextInputType::TEXT;
    const bool ambient = impl.offer != nullptr && impl.offerAmbient;
    if(impl.offer != impl.target || type != impl.targetType || ambient != impl.targetAmbient || impl.restartPending) {
        // a session per target: what was being composed goes to the one it was composed for. its handlers run, so the
        // offer is only read after
        this->commitComposition(impl.target);
        if(impl.active) env->stopTextInput();

        impl.target = impl.offer;
        impl.targetType = type;
        impl.targetAmbient = ambient;
        impl.restartPending = false;
        impl.active = impl.target != nullptr;
        impl.area = {};
        if(impl.active) {
            env->startTextInput(type, !ambient);
            impl.sessionStart = Timing::getTicksNS();
        }
    }

    if(impl.active && impl.offerArea != impl.area) {
        impl.area = impl.offerArea;
        env->setTextInputArea(impl.area.rect, impl.area.caretX);
    }
    impl.offer = nullptr;
}

TextInputClient *Keyboard::getTextTarget() const { return m_impl->target; }

const TextComposition *Keyboard::getComposition(const TextInputClient *client) const {
    return client == m_impl->target && !m_impl->composition.text.empty() ? &m_impl->composition : nullptr;
}

void Keyboard::commitComposition(const TextInputClient *client) {
    auto &impl = *m_impl;
    if(client == nullptr || client != impl.target || impl.composition.text.empty()) return;

    const std::string text = std::exchange(impl.composition, {}).text;
    // the IME still has it; it starts over with the next session
    impl.restartPending = true;
    impl.target->onTextComposition(impl.composition);
    impl.target->onTextInput(text);
}

void Keyboard::forgetTextClient(const TextInputClient *client) {
    auto &impl = *m_impl;
    if(impl.offer == client) impl.offer = nullptr;
    if(impl.target != client) return;
    impl.target = nullptr;
    impl.composition = {};
    impl.restartPending = true;
}

void Keyboard::KeyboardImpl::dispatchKeyDown(KeyboardEvent event) {
    for(auto *listener : this->listeners) {
        listener->onKeyDown(event);
        if(event.isConsumed()) {
            break;
        }
    }
}

void Keyboard::KeyboardImpl::dispatchKeyUp(KeyboardEvent event) {
    for(auto *listener : this->listeners) {
        listener->onKeyUp(event);
        if(event.isConsumed()) {
            break;
        }
    }
}

void Keyboard::KeyboardImpl::deliverText(OSEvent &ev) {
    if(!this->target || ev.timestamp < this->sessionStart) return;

    // what's typed with Ctrl (not AltGr) or Super held is a shortcut (macOS sends text for those)
    if(!this->fromIme &&
       ((this->modstate & KEYMOD_SUPER) || ((this->modstate & KEYMOD_CONTROL) && !(this->modstate & KEYMOD_ALT))))
        return;

    // control characters aren't text (some platforms send one for Ctrl+Backspace or Delete)
    std::erase_if(ev.text, [](char c) { return static_cast<unsigned char>(c) < 32 || c == 127; });
    if(!ev.text.empty()) this->target->onTextInput(ev.text);
}

void Keyboard::KeyboardImpl::setComposition(const OSEvent &ev) {
    if(!this->target || ev.timestamp < this->sessionStart) return;
    // wayland reports "nothing composed" with every update
    if(ev.text.empty() && this->composition.text.empty()) return;

    // codepoints to bytes; a caret that isn't given is at the end
    const auto bytes = [&ev](i32 codepoints) {
        uSz pos = 0;
        for(i32 i = 0; i < codepoints && pos < ev.text.size(); i++) pos = UniString::next(ev.text, pos);
        return (i32)pos;
    };
    const i32 cursor = ev.editStart < 0 ? (i32)ev.text.size() : bytes(ev.editStart);
    const i32 clauseEnd = ev.editStart < 0 || ev.editLength <= 0 ? cursor : bytes(ev.editStart + ev.editLength);
    this->composition = {.text = ev.text, .cursor = cursor, .clauseLength = clauseEnd - cursor};
    if(!ev.text.empty()) this->fromIme = true;
    this->target->onTextComposition(this->composition);
}

TextInputClient::~TextInputClient() {
    if(keyboard) keyboard->forgetTextClient(this);
}
