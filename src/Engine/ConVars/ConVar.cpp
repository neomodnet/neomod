// Copyright (c) 2011, PG & 2025, WH & 2025, kiwec, All rights reserved.
#include "ConVar.h"
#include "ConVarHandler.h"

#include "Logging.h"
#include "Parsing.h"
#include "SString.h"

#include "build_timestamp.h"

#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <new>
#include <utility>

// SA::delegate<R(Args...)> has a fixed 2-pointer layout regardless of signature, so a single
// sized/aligned buffer (CallbackSlot::storage) can hold any of them.
static_assert(sizeof(ConVar::VoidCB) == sizeof(ConVar::StringCB));
static_assert(sizeof(ConVar::VoidCB) == sizeof(ConVar::FloatCB));
static_assert(sizeof(ConVar::VoidCB) == sizeof(ConVar::DoubleCB));
static_assert(sizeof(ConVar::VoidCB) == sizeof(ConVar::StringChangeCB));
static_assert(sizeof(ConVar::VoidCB) == sizeof(ConVar::FloatChangeCB));
static_assert(sizeof(ConVar::VoidCB) == sizeof(ConVar::DoubleChangeCB));
static_assert(alignof(ConVar::VoidCB) == alignof(ConVar::StringCB));
static_assert(alignof(ConVar::VoidCB) == alignof(ConVar::FloatCB));
static_assert(alignof(ConVar::VoidCB) == alignof(ConVar::DoubleCB));
static_assert(alignof(ConVar::VoidCB) == alignof(ConVar::StringChangeCB));
static_assert(alignof(ConVar::VoidCB) == alignof(ConVar::FloatChangeCB));
static_assert(alignof(ConVar::VoidCB) == alignof(ConVar::DoubleChangeCB));
// Trivial destructor lets us skip explicit dtor calls when overwriting a slot or when the
// ConVar dies (its CallbackSlot has only POD members, so its defaulted dtor doesn't reach
// into the stored delegate). Per [basic.life], storage of a trivially-destructible object
// can be reused without ending lifetime explicitly.
static_assert(std::is_trivially_destructible_v<ConVar::VoidCB>);
static_assert(std::is_trivially_destructible_v<ConVar::StringCB>);
static_assert(std::is_trivially_destructible_v<ConVar::FloatCB>);
static_assert(std::is_trivially_destructible_v<ConVar::DoubleCB>);
static_assert(std::is_trivially_destructible_v<ConVar::StringChangeCB>);
static_assert(std::is_trivially_destructible_v<ConVar::FloatChangeCB>);
static_assert(std::is_trivially_destructible_v<ConVar::DoubleChangeCB>);

namespace cv {
// special-cased to improve rebuild times (only declared as extern in ConVarDefs.h)
ConVar build_timestamp("build_timestamp", BUILD_TIMESTAMP, CONSTANT);
ConVar version("version", PACKAGE_VERSION_UNCACHED, CONSTANT);
}  // namespace cv

void ConVar::addConVar() {
    // every ctor ends up here with its values in place: publish them for the getters
    this->resolve();

    std::string_view name = this->getName();

    // osu_ prefix is deprecated.
    // If you really need it, you'll also need to edit Console::execConfigFile to whitelist it there.
    assert(!(name.starts_with("osu_") && !name.starts_with("osu_folder")) && "osu_ ConVar prefix is deprecated.");

    auto &convar_map = cvars().vConVarMap;

    // No duplicate ConVar names allowed
    assert(!convar_map.contains(name) && "no duplicate ConVar names allowed.");

    convar_map.emplace(name, this);
    cvars().vConVarArray.push_back(this);
}

std::string ConVar::getFancyDefaultValue() const {
    switch(this->getType()) {
        using enum CONVAR_TYPE;
        case BOOL:
            return this->defaultValue.d == 0 ? "false" : "true";
        case INT:
            return fmt::format("{:d}", (int)this->defaultValue.d);
        case FLOAT:
            return fmt::format("{:g}", this->defaultValue.d);
        case STRING: {
            return fmt::format(R"("{:s}")", this->defaultValue.s);
        }
    }

    std::unreachable();
    return "unreachable";
}

std::string_view ConVar::typeToString(CONVAR_TYPE type) {
    switch(type) {
        using enum CONVAR_TYPE;
        case BOOL:
            return "bool"sv;
        case INT:
            return "int"sv;
        case FLOAT:
            return "float"sv;
        case STRING:
            return "string"sv;
    }

    std::unreachable();
    return ""sv;
}

std::string_view ConVar::editorToString(CvarEditor editor) {
    switch(editor) {
        using enum CvarEditor;
        case CLIENT:
            return "client"sv;
        case SERVER:
            return "server"sv;
        case SKIN:
            return "skin"sv;
    }

    std::unreachable();
    return ""sv;
}

std::string ConVar::flagsToString(uint8_t flags) {
    if(flags == 0) {
        return "no flags";
    }

    static constexpr const auto flagStringPairArray = std::array{
        std::pair{cv::CLIENT, "client"},       std::pair{cv::SERVER, "server"},     std::pair{cv::SKINS, "skins"},
        std::pair{cv::PROTECTED, "protected"}, std::pair{cv::GAMEPLAY, "gameplay"}, std::pair{cv::HIDDEN, "hidden"},
        std::pair{cv::NOSAVE, "nosave"},       std::pair{cv::NOLOAD, "noload"}};

    std::string string;
    for(bool first = true; const auto &[flag, str] : flagStringPairArray) {
        if((flags & flag) == flag) {
            if(!first) {
                string.push_back(' ');
            }
            first = false;
            string.append(str);
        }
    }

    return string;
}

void ConVar::resolve() {
    // (every change to a convar's value ends up here)
    assert(McThread::is_main_thread() && "convars can only be changed on the main thread");

    // server > protection lock > skin > client (whose value has a stand-in during a session) > default
    const Value *value = &this->defaultValue;
    if(const auto &client = this->layer(CvarEditor::CLIENT); client) value = client.get();
    this->master = CvarEditor::CLIENT;
    if(this->serverValue) {
        value = this->serverValue.get();
        this->master = CvarEditor::SERVER;
    } else if(this->isProtected() && cvars().isProtectionEnforced()) {
        // nobody but the server gets a say about protected convars while the lock is on
        value = &this->defaultValue;
        this->master = CvarEditor::SERVER;
    } else if(this->skinValue) {
        value = this->skinValue.get();
        this->master = CvarEditor::SKIN;
    }

    this->effectiveValue = value;
    this->dValue.store(value->d, std::memory_order_release);

    // keep count for ConVarHandler::areProtectedCvarsDefault()
    if(const bool protectedNonDefault = this->isProtected() && !this->isDefault();
       protectedNonDefault != this->bProtectedNonDefault) {
        this->bProtectedNonDefault = protectedNonDefault;
        cvars().iNumProtectedNonDefault += protectedNonDefault ? 1 : -1;
    }
}

void ConVar::valueChanged() const {
    logIfCV(debug_cv, "{:s} = {:s} ({:s})", this->sName, this->isFlagSet(cv::HIDDEN) ? "..." : this->getString(),
            this->isLocked() ? "lock"sv : ConVar::editorToString(this->master));

    if(const auto onValueChanged = cvars().policy.onValueChanged; onValueChanged) onValueChanged(*this);
}

template <typename Mutation>
void ConVar::change(const Mutation &mutate) {
    cvars().change([&] {
        cvars().remember(*this, true);
        mutate();
        this->resolve();
    });
}

void ConVar::reresolve() {
    this->change([] {});
}

bool ConVar::setInSession(bool inSession) {
    if(inSession == this->bInSession || !this->bCanHaveValue) return false;

    this->change([&] {
        this->bInSession = inSession;

        // (the stand-in starts out as what the client's value is: a session that begins changes nothing)
        if(inSession && this->clientValue) this->sessionValue = std::make_unique<Value>(*this->clientValue);
        if(!inSession) this->sessionValue.reset();
    });
    return true;
}

void ConVar::notifyIfChanged(const Value &old, bool callbacks) {
    if(this->sameValue(old, *this->effectiveValue)) return;

    this->valueChanged();
    if(callbacks) this->runCallbacks(old.d, old.s);
}

void ConVar::setServerProtected(CvarProtection policy) {
    // (commands have no value that could be protected: the lock would keep the client from running them)
    if(!this->bCanHaveValue || policy == this->serverProtectionPolicy) return;

    this->change([&] { this->serverProtectionPolicy = policy; });
}

// (the default is what a locked protected convar reads as)

void ConVar::setDefaultDouble(double newDefault) {
    this->change([&] { this->defaultValue = this->makeValue(newDefault); });
}

void ConVar::setDefaultString(std::string_view newDefault) {
    auto value = this->makeValue(newDefault);
    if(!value) return;

    this->change([&] { this->defaultValue = std::move(*value); });
}

ConVar::Value ConVar::makeValue(double dbl) const {
    dbl = std::clamp(dbl, this->range.min, this->range.max);
    return {.d = dbl, .s = fmt::format("{:g}", dbl)};
}

// typed setValue impls — header dispatcher (setValue<T>) routes here based on T category.
// Each just computes the (double, std::string) representation and hands off to setValueInt.

CvarSetResult ConVar::setValueImpl(double newDouble, bool doCallback, CvarEditor editor) {
    return this->setValueInt(this->makeValue(newDouble), doCallback, editor);
}

std::optional<ConVar::Value> ConVar::makeValue(std::string_view text) const {
    double dbl{this->defaultValue.d};
    const auto [ptr, err] = Parsing::from_chars(text.data(), text.data() + text.size(), dbl);
    (void)ptr;
    if(err != std::errc()) {
        // older builds saved bool convars as "true"/"false", accept those too, but normalize the
        // stored string back to the canonical "1"/"0". otherwise a default-valued bool keeps the
        // textual "false" while its default string is "0", so isDefault() ("incorrectly") reports non-default
        if(this->type == CONVAR_TYPE::BOOL && SString::strcase_equal(text, "true")) {
            dbl = 1.0;
            text = "1";
        } else if(this->type == CONVAR_TYPE::BOOL && SString::strcase_equal(text, "false")) {
            dbl = 0.0;
            text = "0";
        } else if(this->type == CONVAR_TYPE::STRING) {
            // only numeric convars need their text to be a number
            dbl = this->defaultValue.d;
        } else {
            logIfCV(debug_cv, "{:s}: \"{:s}\" is not a valid {:s} value", this->sName, text,
                    ConVar::typeToString(this->type));
            return std::nullopt;
        }
    }

    // (text stays as it was typed, unless it isn't what the value is)
    if(dbl < this->range.min || dbl > this->range.max) return this->makeValue(dbl);
    return Value{.d = dbl, .s = std::string{text}};
}

CvarSetResult ConVar::setValueImpl(std::string_view newString, bool doCallback, CvarEditor editor) {
    auto value = this->makeValue(newString);
    if(!value) return CvarSetResult::INVALID;

    return this->setValueInt(std::move(*value), doCallback, editor);
}

CvarSetResult ConVar::checkWrite(CvarEditor editor) const {
    // editor must match a flag we accept
    if(editor == CvarEditor::CLIENT && !this->isFlagSet(cv::CLIENT)) return CvarSetResult::DENIED;
    if(editor == CvarEditor::SKIN && !this->isFlagSet(cv::SKINS)) return CvarSetResult::DENIED;
    if(editor == CvarEditor::SERVER && !this->isFlagSet(cv::SERVER)) return CvarSetResult::DENIED;

    // the app may have something against it
    if(const auto allowWrite = cvars().policy.allowWrite; allowWrite && unlikely(!allowWrite(*this, editor))) {
        return CvarSetResult::VETOED;
    }

    return CvarSetResult::APPLIED;
}

void ConVar::store(CvarEditor editor, Value value) {
    if(auto &layer = this->layer(editor); layer) {
        *layer = std::move(value);
    } else {
        layer = std::make_unique<Value>(std::move(value));
    }
}

// central store-and-dispatch. handles flag gating, the app's policy, value store, exec/change callbacks.
CvarSetResult ConVar::setValueInt(Value newValue, bool doCallback, CvarEditor editor) {
    if(const CvarSetResult refused = this->checkWrite(editor); refused != CvarSetResult::APPLIED) return refused;

    // commands have no value that could be overridden: whoever is allowed to call them just runs them
    // (nor one that could change: they run right away, also during a ConVarHandler::change())
    if(!this->bCanHaveValue) editor = CvarEditor::CLIENT;

    // as part of a change, a write is one more thing that happens to the convar: what came of it is for the end of that
    const bool deferred = this->bCanHaveValue && cvars().remember(*this, doCallback);

    // backup old values for callbacks
    const double oldDouble{this->getDouble()};
    std::string oldString;
    if(!deferred && doCallback && this->changeCallback.kind == CallbackKind::StringChange) {
        oldString = this->getString();
    }

    const bool unchanged = this->sameValue(*this->effectiveValue, newValue);

    this->store(editor, std::move(newValue));
    this->resolve();

    // a write below whatever decides the value right now (a skin/server value, the protection lock) is kept for
    // later, but it changes nothing anyone could see: callbacks hear about it if and when it becomes the value
    if(this->master != editor) return CvarSetResult::MASKED;
    if(deferred) return CvarSetResult::APPLIED;

    // (the convar's own callbacks also get to hear about a write that went through without changing the value)
    if(!unchanged && this->bCanHaveValue) this->valueChanged();

    if(doCallback) this->runCallbacks(oldDouble, oldString);
    return CvarSetResult::APPLIED;
}

void ConVar::clearValue(CvarEditor editor) {
    if(!this->bCanHaveValue) return;

    // (a skin's/the server's value going away is nothing to ask anyone about: they are gone, or done with it)
    if(editor == CvarEditor::CLIENT && this->checkWrite(editor) != CvarSetResult::APPLIED) return;

    auto &layer = this->layer(editor);
    if(!layer) return;

    this->change([&] { layer.reset(); });
}

void ConVar::runCallbacks(double oldDouble, std::string_view oldString) {
    const double newDouble{this->getDouble()};

    // dispatch exec callback (kind=None just falls through)
    switch(this->callback.kind) {
        using enum CallbackKind;
        case Void:
            (*std::launder(reinterpret_cast<VoidCB *>(&this->callback.storage[0])))();
            break;
        case String:
            (*std::launder(reinterpret_cast<StringCB *>(&this->callback.storage[0])))(this->getString());
            break;
        case Float:
            (*std::launder(reinterpret_cast<FloatCB *>(&this->callback.storage[0])))(static_cast<float>(newDouble));
            break;
        case Double:
            (*std::launder(reinterpret_cast<DoubleCB *>(&this->callback.storage[0])))(newDouble);
            break;
        default:
            break;
    }

    // dispatch change callback
    switch(this->changeCallback.kind) {
        using enum CallbackKind;
        case StringChange:
            (*std::launder(reinterpret_cast<StringChangeCB *>(&this->changeCallback.storage[0])))(oldString,
                                                                                                  this->getString());
            break;
        case FloatChange:
            (*std::launder(reinterpret_cast<FloatChangeCB *>(&this->changeCallback.storage[0])))(
                static_cast<float>(oldDouble), static_cast<float>(newDouble));
            break;
        case DoubleChange:
            (*std::launder(reinterpret_cast<DoubleChangeCB *>(&this->changeCallback.storage[0])))(oldDouble, newDouble);
            break;
        default:
            break;
    }
}

// typed setCallback impls — installed into the callback / changeCallback slot via placement
// new. delegate dtor is trivial (asserted above), so we don't need to end the old object's
// lifetime explicitly before reusing the storage.

void ConVar::setCallbackImpl(VoidCB cb) {
    ::new(&this->callback.storage[0]) VoidCB(std::move(cb));
    this->callback.kind = CallbackKind::Void;
}
void ConVar::setCallbackImpl(StringCB cb) {
    ::new(&this->callback.storage[0]) StringCB(std::move(cb));
    this->callback.kind = CallbackKind::String;
}
void ConVar::setCallbackImpl(FloatCB cb) {
    ::new(&this->callback.storage[0]) FloatCB(std::move(cb));
    this->callback.kind = CallbackKind::Float;
}
void ConVar::setCallbackImpl(DoubleCB cb) {
    ::new(&this->callback.storage[0]) DoubleCB(std::move(cb));
    this->callback.kind = CallbackKind::Double;
}
void ConVar::setCallbackImpl(StringChangeCB cb) {
    ::new(&this->changeCallback.storage[0]) StringChangeCB(std::move(cb));
    this->changeCallback.kind = CallbackKind::StringChange;
}
void ConVar::setCallbackImpl(FloatChangeCB cb) {
    ::new(&this->changeCallback.storage[0]) FloatChangeCB(std::move(cb));
    this->changeCallback.kind = CallbackKind::FloatChange;
}
void ConVar::setCallbackImpl(DoubleChangeCB cb) {
    ::new(&this->changeCallback.storage[0]) DoubleChangeCB(std::move(cb));
    this->changeCallback.kind = CallbackKind::DoubleChange;
}

// typed init impls used by value ctors. each sets type/flags + default value, which is what the convar is until the
// client (or anyone else) sets it.

void ConVar::initValueImpl(bool v, uint8_t flags) {
    this->type = CONVAR_TYPE::BOOL;
    this->initValueInt({.d = v ? 1.0 : 0.0, .s = v ? "1" : "0"}, flags);
}

void ConVar::initValueImpl(int v, uint8_t flags) {
    this->type = CONVAR_TYPE::INT;
    this->initValueInt({.d = static_cast<double>(v), .s = fmt::format("{:g}", static_cast<double>(v))}, flags);
}

void ConVar::initValueImpl(double v, uint8_t flags) {
    this->type = CONVAR_TYPE::FLOAT;
    this->initValueInt({.d = v, .s = fmt::format("{:g}", v)}, flags);
}

void ConVar::initValueImpl(std::string_view v, uint8_t flags) {
    this->type = CONVAR_TYPE::STRING;

    // (string convars take any text, with a numeric view of it if it happens to be a number)
    this->initValueInt(*this->makeValue(v), flags);
}

void ConVar::initValueInt(Value value, uint8_t flags) {
    assert(value.d >= this->range.min && value.d <= this->range.max &&
           "a convar's default has to be inside of its range");
    this->bCanHaveValue = true;
    this->iFlags = flags;
    this->defaultValue = std::move(value);
}

// typed init impls used by callback-only ctors. flags get NOSAVE forced on, and type is
// determined by the callback signature (preserves pre-refactor semantics: float/double cb → INT).

void ConVar::initCmdCallbackImpl(uint8_t flags, VoidCB cb) {
    this->iFlags = flags | cv::NOSAVE;
    this->type = CONVAR_TYPE::STRING;
    ::new(&this->callback.storage[0]) VoidCB(std::move(cb));
    this->callback.kind = CallbackKind::Void;
}
void ConVar::initCmdCallbackImpl(uint8_t flags, StringCB cb) {
    this->iFlags = flags | cv::NOSAVE;
    this->type = CONVAR_TYPE::STRING;
    ::new(&this->callback.storage[0]) StringCB(std::move(cb));
    this->callback.kind = CallbackKind::String;
}
void ConVar::initCmdCallbackImpl(uint8_t flags, FloatCB cb) {
    this->iFlags = flags | cv::NOSAVE;
    this->type = CONVAR_TYPE::INT;
    ::new(&this->callback.storage[0]) FloatCB(std::move(cb));
    this->callback.kind = CallbackKind::Float;
}
void ConVar::initCmdCallbackImpl(uint8_t flags, DoubleCB cb) {
    this->iFlags = flags | cv::NOSAVE;
    this->type = CONVAR_TYPE::INT;
    ::new(&this->callback.storage[0]) DoubleCB(std::move(cb));
    this->callback.kind = CallbackKind::Double;
}

void ConVar::removeCallback() {
    assert(McThread::is_main_thread() && "convars belong to the main thread");
    // delegate dtor is trivial; just clear the tag
    this->callback.kind = CallbackKind::None;
}
void ConVar::removeChangeCallback() {
    assert(McThread::is_main_thread() && "convars belong to the main thread");
    this->changeCallback.kind = CallbackKind::None;
}
void ConVar::removeAllCallbacks() {
    this->removeCallback();
    this->removeChangeCallback();
}

bool ConVar::hasSingleArgCallback() const {
    using enum CallbackKind;
    auto kind = this->callback.kind;
    return kind == String || kind == Float || kind == Double;
}
