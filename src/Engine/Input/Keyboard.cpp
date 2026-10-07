// Copyright (c) 2015, PG, 2026, WH, All rights reserved.
#include "Keyboard.h"

#include "Engine.h"
#include "Environment.h"
#include "KeyboardEvent.h"
#include "UniString.h"

#include <cstring>
#include <utility>

// NOLINTBEGIN(cppcoreguidelines-pro-type-const-cast,cppcoreguidelines-pro-type-member-init,hicpp-member-init)

namespace {
struct OSEvent final {
    enum EvType : u8 { KEYB_KEYDOWN, KEYB_KEYUP, KEYB_CHAR };

    OSEvent() = delete;
    OSEvent(u64 timestamp, u32 keyboardID, KEYCODE layoutDependentKeycode, KEYMOD heldModifiersAsOfEvent,
            SCANCODE layoutIndependentScancode, bool isKeyDown, bool isRepeatEvent) {
        keyev.type = isKeyDown ? EvType::KEYB_KEYDOWN : EvType::KEYB_KEYUP;
        keyev.timestamp = timestamp;
        keyev.keyboardID = keyboardID;
        keyev.layoutDependentKeycode = layoutDependentKeycode;
        keyev.heldModifiersAsOfEvent = heldModifiersAsOfEvent;
        keyev.layoutIndependentScancode = layoutIndependentScancode;
        keyev.isRepeat = isRepeatEvent;

        charev.allocatedFlag = false;
    }
    OSEvent(u64 timestamp, const char* text) {
        assert(!!text && text[0] != '\0');

        charev.type = EvType::KEYB_CHAR;
        charev.timestamp = timestamp;

        // make a deep copy of the text
        charev.textLen = static_cast<u32>(std::strlen(text));
        if(charev.textLen <= sizeof(charev.staticText)) {
            // the text can fit inside staticText, just copy it in there
            charev.allocatedFlag = false;

            std::memcpy(static_cast<void*>(&charev.staticText[0]), static_cast<const void*>(text), charev.textLen);
            charev.text = &charev.staticText[0];
        } else {
            // too long to fit inside a full event, allocate
            charev.allocatedFlag = true;

            charev.text = static_cast<char*>(std::malloc(charev.textLen));
            std::memcpy(static_cast<void*>(charev.text), static_cast<const void*>(text), charev.textLen);
        }
    }

    ~OSEvent() {
        if(charev.allocatedFlag) {
            std::free(static_cast<void*>(charev.text));
        }
    }
    OSEvent(OSEvent&& o) noexcept {
        std::memcpy(static_cast<void*>(this), static_cast<const void*>(&o), sizeof(OSEvent));
        if(type == KEYB_CHAR && !charev.allocatedFlag) charev.text = &charev.staticText[0];
        std::memset(static_cast<void*>(&o), 0, sizeof(OSEvent));
    }
    OSEvent& operator=(OSEvent&& o) noexcept {
        if(this != &o) {
            // clean up existing state
            if(type == KEYB_CHAR && charev.allocatedFlag && charev.text) {
                std::free(static_cast<void*>(charev.text));
            }

            std::memcpy(static_cast<void*>(this), static_cast<const void*>(&o), sizeof(OSEvent));
            if(type == KEYB_CHAR && !charev.allocatedFlag) charev.text = &charev.staticText[0];
            std::memset(static_cast<void*>(&o), 0, sizeof(OSEvent));
        }
        return *this;
    }

    OSEvent(const OSEvent&) = delete;
    OSEvent& operator=(const OSEvent&) = delete;

    struct KeyEvent {
        u64 timestamp;
        u32 keyboardID;
        KEYCODE layoutDependentKeycode;
        KEYMOD heldModifiersAsOfEvent;
        SCANCODE layoutIndependentScancode;
        EvType type;
        bool isRepeat;
    };

    struct CharEvent {
        u64 timestamp;
        u32 textLen;
        bool allocatedFlag;
        u8 _pad[offsetof(KeyEvent, type) - sizeof(timestamp) - sizeof(textLen) - sizeof(allocatedFlag)];
        EvType type;
        char staticText[(64 - sizeof(char*)) - (offsetof(KeyEvent, type) + sizeof(type))];
        char* text;  // NOTE: not null-terminated
    };

    union {
        KeyEvent keyev;
        CharEvent charev;
        struct {
            u8 _pad[offsetof(KeyEvent, type)];
            EvType type;
            u8 _pad2[sizeof(CharEvent) - (sizeof(_pad) + sizeof(type))];
        };
    };
};

static_assert(offsetof(OSEvent::KeyEvent, type) == offsetof(OSEvent::CharEvent, type));
static_assert(offsetof(OSEvent, type) == offsetof(OSEvent::CharEvent, type) &&
              sizeof(OSEvent) == sizeof(OSEvent::CharEvent));

}  // namespace

// NOLINTEND(cppcoreguidelines-pro-type-const-cast,cppcoreguidelines-pro-type-member-init,hicpp-member-init)

struct Keyboard::KeyboardImpl {
    void dispatchKeyDown(KeyboardEvent event);
    void dispatchKeyUp(KeyboardEvent event);
    void dispatchChar(KeyboardEvent event);
    std::vector<KeyboardListener*> listeners;

    std::vector<OSEvent> eventQueue;

    // global keyboard modifier state as of the last keydown/up event
    KEYMOD modstate{};
};

Keyboard::Keyboard() : m_impl() { m_impl->eventQueue.reserve(8); }
Keyboard::~Keyboard() = default;

void Keyboard::draw() {}

void Keyboard::update() {
    for(auto& event : m_impl->eventQueue) {
        switch(event.type) {
            using enum OSEvent::EvType;
            case KEYB_KEYDOWN: {
                const auto& kevent{event.keyev};
                m_impl->modstate = kevent.heldModifiersAsOfEvent;
                m_impl->dispatchKeyDown({kevent.layoutIndependentScancode, kevent.layoutDependentKeycode,
                                         static_cast<char32_t>(kevent.layoutDependentKeycode), kevent.timestamp,
                                         kevent.heldModifiersAsOfEvent, kevent.isRepeat});
                break;
            }

            case KEYB_KEYUP: {
                const auto& kevent{event.keyev};
                m_impl->modstate = kevent.heldModifiersAsOfEvent;
                m_impl->dispatchKeyUp({kevent.layoutIndependentScancode, kevent.layoutDependentKeycode,
                                       static_cast<char32_t>(kevent.layoutDependentKeycode), kevent.timestamp,
                                       kevent.heldModifiersAsOfEvent, kevent.isRepeat});
                break;
            }

            case KEYB_CHAR: {
                const auto& tevent{event.charev};
                assert(tevent.text && tevent.text[0] != '\0');

                const KEYMOD modifiers = m_impl->modstate;
                if(likely(tevent.textLen == 1)) {
                    m_impl->dispatchChar({0, 0, static_cast<char32_t>(tevent.text[0]), tevent.timestamp, modifiers});
                } else {
                    for(char32_t chr : UniString::codepoints(std::string_view{tevent.text, tevent.textLen}))
                        m_impl->dispatchChar({0, 0, chr, tevent.timestamp, modifiers});
                }
                break;
            }

            default: {
                assert(false && "unknown event in the queue");
                std::unreachable();
                break;
            }
        }
    }
    m_impl->eventQueue.clear();
}

void Keyboard::onKeyEvent(u64 timestamp, u32 keyboardID, KEYCODE layoutDependentKeycode, KEYMOD heldModifiersAsOfEvent,
                          SCANCODE layoutIndependentScancode, bool isKeyDown, bool isRepeatEvent) {
    m_impl->eventQueue.emplace_back(timestamp, keyboardID, layoutDependentKeycode, heldModifiersAsOfEvent,
                                    layoutIndependentScancode, isKeyDown, isRepeatEvent);
}

void Keyboard::onCharEvent(u64 timestamp, const char* text) {
    // should not normally happen, but we can just ignore events with literally no text at all
    if(likely(text && text[0] != '\0')) {
        m_impl->eventQueue.emplace_back(timestamp, text);
    }
}

// actually, don't throw away queued events (otherwise we might not dispatch a queued key up for a key down we got)
void Keyboard::reset() { m_impl->modstate = env->getCurrentlyHeldKeyModifiers(); }

void Keyboard::addListener(KeyboardListener* keyboardListener, bool insertOnTop) {
    if(keyboardListener == nullptr) {
        engine->showMessageError("Keyboard Error", "addListener(NULL)!");
        return;
    }

    if(insertOnTop)
        m_impl->listeners.insert(m_impl->listeners.begin(), keyboardListener);
    else
        m_impl->listeners.push_back(keyboardListener);
}

void Keyboard::removeListener(KeyboardListener* keyboardListener) { std::erase(m_impl->listeners, keyboardListener); }

bool Keyboard::areModsHeld(KEYMOD keymodMask) const { return (m_impl->modstate & keymodMask) == keymodMask; }

bool Keyboard::isControlDown() const { return !!(m_impl->modstate & KEYMOD_CONTROL); }
bool Keyboard::isAltDown() const { return !!(m_impl->modstate & KEYMOD_ALT); }
bool Keyboard::isShiftDown() const { return !!(m_impl->modstate & KEYMOD_SHIFT); }
bool Keyboard::isSuperDown() const { return !!(m_impl->modstate & KEYMOD_SUPER); }

void Keyboard::KeyboardImpl::dispatchKeyDown(KeyboardEvent event) {
    for(auto* listener : this->listeners) {
        listener->onKeyDown(event);
        if(event.isConsumed()) {
            break;
        }
    }
}

void Keyboard::KeyboardImpl::dispatchKeyUp(KeyboardEvent event) {
    for(auto* listener : this->listeners) {
        listener->onKeyUp(event);
        if(event.isConsumed()) {
            break;
        }
    }
}

void Keyboard::KeyboardImpl::dispatchChar(KeyboardEvent event) {
    for(auto* listener : this->listeners) {
        listener->onChar(event);
        if(event.isConsumed()) {
            break;
        }
    }
}
