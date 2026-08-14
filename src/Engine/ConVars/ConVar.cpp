// Copyright (c) 2011, PG & 2025, WH & 2025, kiwec, All rights reserved.
#include "ConVar.h"
#include "ConVarHandler.h"

#include "Parsing.h"
#include "SString.h"

#include "build_timestamp.h"

#include "fmt/format.h"

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

// set by app, shared across all convars, called when a protected convar changes
ConVar::VoidCB ConVar::onSetValueProtectedCallback{};
void ConVar::setOnSetValueProtectedCallback(const VoidCB &callback) { ConVar::onSetValueProtectedCallback = callback; }

// ditto
ConVar::ProtectedCVGetCB ConVar::onGetValueProtectedCallback{nullptr};
void ConVar::setOnGetValueProtectedCallback(ProtectedCVGetCB func) { ConVar::onGetValueProtectedCallback = func; }

// ditto
ConVar::GameplayCVChangeCB ConVar::onSetValueGameplayCallback{nullptr};
void ConVar::setOnSetValueGameplayCallback(GameplayCVChangeCB func) { ConVar::onSetValueGameplayCallback = func; }

void ConVar::addConVar() {
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
            return this->dDefaultValue == 0 ? "false" : "true";
        case INT:
            return fmt::format("{:d}", (int)this->dDefaultValue);
        case FLOAT:
            return fmt::format("{:g}", this->dDefaultValue);
        case STRING: {
            return fmt::format(R"("{:s}")", this->sDefaultValue);
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

void ConVar::exec() {
    if(this->callback.kind == CallbackKind::Void) {
        (*std::launder(reinterpret_cast<VoidCB *>(&this->callback.storage[0])))();
    }
}

void ConVar::execArgs(std::string_view args) {
    if(this->callback.kind == CallbackKind::String) {
        (*std::launder(reinterpret_cast<StringCB *>(&this->callback.storage[0])))(args);
    }
}

void ConVar::execFloat(float args) {
    if(this->callback.kind == CallbackKind::Float) {
        (*std::launder(reinterpret_cast<FloatCB *>(&this->callback.storage[0])))(args);
    }
}

void ConVar::execDouble(double args) {
    if(this->callback.kind == CallbackKind::Double) {
        (*std::launder(reinterpret_cast<DoubleCB *>(&this->callback.storage[0])))(args);
    }
}

CvarEditor ConVar::getMaster() const {
    if((this->isFlagSet(cv::SERVER) && this->hasServerValue.load(std::memory_order_acquire)) ||
       // (only for multiplayer rooms, see note on invalidateAllProtectedCaches below)
       (this->isProtected() &&
        (likely(!!ConVar::onGetValueProtectedCallback) && !ConVar::onGetValueProtectedCallback(this->sName)))) {
        return CvarEditor::SERVER;
    } else if(this->isFlagSet(cv::SKINS) && this->hasSkinValue.load(std::memory_order_acquire)) {
        return CvarEditor::SKIN;
    } else {
        return CvarEditor::CLIENT;
    }
}

double ConVar::getDoubleInt() const {
    if(this->isFlagSet(cv::SERVER) && this->hasServerValue.load(std::memory_order_acquire)) {
        this->dCachedReturnedDouble.store(this->dServerValue.load(std::memory_order_acquire),
                                          std::memory_order_release);
    } else if(this->isFlagSet(cv::SKINS) && this->hasSkinValue.load(std::memory_order_acquire)) {
        this->dCachedReturnedDouble.store(this->dSkinValue.load(std::memory_order_acquire), std::memory_order_release);
    } else if(this->isProtected() &&
              (likely(!!ConVar::onGetValueProtectedCallback) && !ConVar::onGetValueProtectedCallback(this->sName))) {
        // FIXME: this is unreliable since onGetValueProtectedCallback might change arbitrarily,
        // need to invalidate cached state when that happens
        // currently relying on a cvars().invalidateAllProtectedCaches "backdoor" (see Bancho.cpp),
        // so the API user needs to know the implementation details or else they'll keep getting default values :)
        this->dCachedReturnedDouble.store(this->dDefaultValue, std::memory_order_release);
    } else {
        this->dCachedReturnedDouble.store(this->dClientValue.load(std::memory_order_acquire),
                                          std::memory_order_release);
    }

    this->bUseCachedDouble.store(true, std::memory_order_release);
    return this->dCachedReturnedDouble.load(std::memory_order_acquire);
}

const std::string &ConVar::getStringInt() const {
    if(this->isFlagSet(cv::SERVER) && this->hasServerValue.load(std::memory_order_acquire)) {
        this->sCachedReturnedString.store(&this->sServerValue, std::memory_order_release);
    } else if(this->isFlagSet(cv::SKINS) && this->hasSkinValue.load(std::memory_order_acquire)) {
        this->sCachedReturnedString.store(&this->sSkinValue, std::memory_order_release);
    } else if(this->isProtected() &&
              (likely(!!ConVar::onGetValueProtectedCallback) && !ConVar::onGetValueProtectedCallback(this->sName))) {
        this->sCachedReturnedString.store(&this->sDefaultValue, std::memory_order_release);
    } else {
        this->sCachedReturnedString.store(&this->sClientValue, std::memory_order_release);
    }

    this->bUseCachedString.store(true, std::memory_order_release);
    return *(this->sCachedReturnedString.load(std::memory_order_acquire));
}

void ConVar::setDefaultDouble(double defaultValue) {
    this->dDefaultValue = defaultValue;
    this->sDefaultValue = fmt::format("{:g}", defaultValue);

    // FIXME: continued hacks from the protected value returning default value issue
    if(this->isFlagSet(cv::PROTECTED)) {
        this->invalidateCache();
    }
}

void ConVar::setDefaultString(std::string_view defaultValue) {
    this->sDefaultValue = defaultValue;

    // also try to parse default float from the default string
    double dbl{};
    const auto [ptr, err] = Parsing::from_chars(defaultValue.data(), defaultValue.data() + defaultValue.size(), dbl);
    if(err == std::errc()) this->dDefaultValue = dbl;

    if(this->isFlagSet(cv::PROTECTED)) {
        this->invalidateCache();
    }
}

// typed setValue impls — header dispatcher (setValue<T>) routes here based on T category.
// Each just computes the (double, std::string) representation and hands off to setValueInt.

void ConVar::setValueImpl(double newDouble, bool doCallback, CvarEditor editor) {
    this->setValueInt(newDouble, fmt::format("{:g}", newDouble), doCallback, editor);
}

void ConVar::setValueImpl(std::string newString, bool doCallback, CvarEditor editor) {
    double dbl{this->dDefaultValue};
    const auto [ptr, err] = Parsing::from_chars(newString.data(), newString.data() + newString.size(), dbl);
    (void)ptr;
    if(err != std::errc()) {
        // older builds saved bool convars as "true"/"false", accept those too, but normalize the
        // stored string back to the canonical "1"/"0". otherwise a default-valued bool keeps the
        // textual "false" while its default string is "0", so isDefault() ("incorrectly") reports non-default
        dbl = this->dDefaultValue;
        if(this->type == CONVAR_TYPE::BOOL) {
            if(SString::strcase_equal(newString, "true")) {
                dbl = 1.0;
                newString = "1";
            } else if(SString::strcase_equal(newString, "false")) {
                dbl = 0.0;
                newString = "0";
            }
        }
    }
    this->setValueInt(dbl, std::move(newString), doCallback, editor);
}

// central store-and-dispatch. handles flag gating, value store, protected/exec/change callbacks.
void ConVar::setValueInt(double newDouble, std::string newString, bool doCallback, CvarEditor editor) {
    // editor must match a flag we accept
    if(editor == CvarEditor::CLIENT && !this->isFlagSet(cv::CLIENT)) return;
    if(editor == CvarEditor::SKIN && !this->isFlagSet(cv::SKINS)) return;
    if(editor == CvarEditor::SERVER && !this->isFlagSet(cv::SERVER)) return;

    // gameplay gate: if flag set AND callback exists AND callback denies, skip
    if(this->isFlagSet(cv::GAMEPLAY) && likely(!!ConVar::onSetValueGameplayCallback) &&
       unlikely(!ConVar::onSetValueGameplayCallback(this->sName, editor))) {
        return;
    }

    // backup old values for callbacks
    double oldDouble{this->getDoubleInt()};
    std::string oldString;
    std::string_view newStringStored;  // minor optimization to avoid copying (points to field we moved into)
    if(doCallback) {
        oldString = this->getStringInt();
    }

    // store new values
    switch(editor) {
        using enum CvarEditor;
        case CLIENT: {
            this->dClientValue.store(newDouble, std::memory_order_release);
            newStringStored = (this->sClientValue = std::move(newString));
            break;
        }
        case SKIN: {
            this->dSkinValue.store(newDouble, std::memory_order_release);
            newStringStored = (this->sSkinValue = std::move(newString));
            this->hasSkinValue.store(true, std::memory_order_release);
            break;
        }
        case SERVER: {
            this->dServerValue.store(newDouble, std::memory_order_release);
            newStringStored = (this->sServerValue = std::move(newString));
            this->hasServerValue.store(true, std::memory_order_release);
            break;
        }
    }

    this->invalidateCache();

    // run protected value change cb
    if(this->isProtected() && oldDouble != newDouble && likely(!!ConVar::onSetValueProtectedCallback)) {
        ConVar::onSetValueProtectedCallback();
    }

    if(!doCallback) return;

    // dispatch exec callback (kind=None just falls through)
    switch(this->callback.kind) {
        using enum CallbackKind;
        case Void:
            (*std::launder(reinterpret_cast<VoidCB *>(&this->callback.storage[0])))();
            break;
        case String:
            (*std::launder(reinterpret_cast<StringCB *>(&this->callback.storage[0])))(newStringStored);
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
                                                                                                  newStringStored);
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

// typed init impls used by value ctors. each sets type/flags + default value, then copies
// defaults to all 3 editor slots (client/skin/server).

void ConVar::initValueImpl(bool v, uint8_t flags) {
    this->bCanHaveValue = true;
    this->iFlags = flags;
    this->type = CONVAR_TYPE::BOOL;
    this->setDefaultDouble(v ? 1.0 : 0.0);
    this->sClientValue = this->sDefaultValue;
    this->sSkinValue = this->sDefaultValue;
    this->sServerValue = this->sDefaultValue;
    this->dClientValue.store(this->dDefaultValue, std::memory_order_relaxed);
    this->dSkinValue.store(this->dDefaultValue, std::memory_order_relaxed);
    this->dServerValue.store(this->dDefaultValue, std::memory_order_relaxed);
}

void ConVar::initValueImpl(int v, uint8_t flags) {
    this->bCanHaveValue = true;
    this->iFlags = flags;
    this->type = CONVAR_TYPE::INT;
    this->setDefaultDouble(static_cast<double>(v));
    this->sClientValue = this->sDefaultValue;
    this->sSkinValue = this->sDefaultValue;
    this->sServerValue = this->sDefaultValue;
    this->dClientValue.store(this->dDefaultValue, std::memory_order_relaxed);
    this->dSkinValue.store(this->dDefaultValue, std::memory_order_relaxed);
    this->dServerValue.store(this->dDefaultValue, std::memory_order_relaxed);
}

void ConVar::initValueImpl(double v, uint8_t flags) {
    this->bCanHaveValue = true;
    this->iFlags = flags;
    this->type = CONVAR_TYPE::FLOAT;
    this->setDefaultDouble(v);
    this->sClientValue = this->sDefaultValue;
    this->sSkinValue = this->sDefaultValue;
    this->sServerValue = this->sDefaultValue;
    this->dClientValue.store(this->dDefaultValue, std::memory_order_relaxed);
    this->dSkinValue.store(this->dDefaultValue, std::memory_order_relaxed);
    this->dServerValue.store(this->dDefaultValue, std::memory_order_relaxed);
}

void ConVar::initValueImpl(std::string_view v, uint8_t flags) {
    this->bCanHaveValue = true;
    this->iFlags = flags;
    this->type = CONVAR_TYPE::STRING;
    this->setDefaultString(v);
    this->sClientValue = this->sDefaultValue;
    this->sSkinValue = this->sDefaultValue;
    this->sServerValue = this->sDefaultValue;
    this->dClientValue.store(this->dDefaultValue, std::memory_order_relaxed);
    this->dSkinValue.store(this->dDefaultValue, std::memory_order_relaxed);
    this->dServerValue.store(this->dDefaultValue, std::memory_order_relaxed);
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
    // delegate dtor is trivial; just clear the tag
    this->callback.kind = CallbackKind::None;
}
void ConVar::removeChangeCallback() { this->changeCallback.kind = CallbackKind::None; }
void ConVar::removeAllCallbacks() {
    this->removeCallback();
    this->removeChangeCallback();
}

void ConVar::reset() {
    this->removeAllCallbacks();
    this->invalidateCache();
    this->hasServerValue = false;
    this->hasSkinValue = false;
    this->setServerProtected(CvarProtection::DEFAULT);
}

bool ConVar::hasAnyNonVoidCallback() const {
    using enum CallbackKind;
    auto kind = this->callback.kind;
    return kind != None && kind != Void;
}

bool ConVar::hasSingleArgCallback() const {
    using enum CallbackKind;
    auto kind = this->callback.kind;
    return kind == String || kind == Float || kind == Double;
}
