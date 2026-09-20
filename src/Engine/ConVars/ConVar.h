// Copyright (c) 2011, PG & 2025, WH & 2025, kiwec, All rights reserved.
#ifndef CONVAR_H
#define CONVAR_H

#include "BaseEnvironment.h"

#include "Delegate.h"
#include "Thread.h"

#include <atomic>
#include <cassert>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

#ifndef DEFINE_CONVARS
#include "ConVarDefs.h"
#endif

using namespace std::string_view_literals;
using namespace std::string_literals;

namespace cv {
enum CvarFlags : uint8_t {
    // Modifiable by clients
    CLIENT = (1 << 0),

    // Modifiable by servers, OR by offline clients
    SERVER = (1 << 1),

    // Modifiable by skins
    // TODO: assert() CLIENT is set
    SKINS = (1 << 2),

    // Scores won't submit if modified
    PROTECTED = (1 << 3),

    // Scores won't submit if modified during gameplay
    // (nothing the engine looks at: what it means is up to the app's ConVarHandler::Policy)
    GAMEPLAY = (1 << 4),

    // Hidden from console suggestions (e.g. for passwords or deprecated cvars)
    HIDDEN = (1 << 5),

    // Don't save this cvar to configs
    NOSAVE = (1 << 6),

    // Don't load this cvar from configs
    NOLOAD = (1 << 7),

    // Mark the variable as intended for use only inside engine code
    // NOTE: This is intended to be used without any other flags
    CONSTANT = HIDDEN | NOLOAD | NOSAVE,
};

// the values a numeric convar can take: whatever gets set (by anyone) ends up inside of it
struct Range {
    double min;
    double max;
};

namespace detail {
// command-only convars accept the 4 single-arg "exec" signatures
template <typename C>
concept CallbackCmd = std::is_invocable_v<C> || std::is_invocable_v<C, std::string_view> ||
                      std::is_invocable_v<C, float> || std::is_invocable_v<C, double>;

// value convars also accept the 3 two-arg "change" signatures
template <typename C>
concept CallbackAny = CallbackCmd<C> || std::is_invocable_v<C, std::string_view, std::string_view> ||
                      std::is_invocable_v<C, float, float> || std::is_invocable_v<C, double, double>;
}  // namespace detail
}  // namespace cv

enum class CvarEditor : uint8_t { CLIENT, SERVER, SKIN };
enum class CvarProtection : uint8_t { DEFAULT, PROTECTED, UNPROTECTED };

// what became of a setValue()
enum class CvarSetResult : uint8_t {
    APPLIED,  // it is the convar's value now
    MASKED,   // kept for later: a skin's/the server's value or the protection lock decides the value right now
    DENIED,   // this editor isn't allowed to set the convar (see CvarFlags)
    VETOED,   // the app refused it (see ConVarHandler::Policy)
    INVALID,  // not something the convar can be set to (see setValue())
};

class ConVar {
    // convenience for "tricking" clangd/intellisense into allowing us to use a namespace for ConVarHandler in ConVarDefs.h
#ifndef DEFINE_CONVARS
    friend class ConVarHandler;
#endif

   public:
    enum class CONVAR_TYPE : uint8_t { BOOL, INT, FLOAT, STRING };

    // callback typedefs using Kryukov delegates
    using VoidCB = SA::delegate<void()>;
    using StringCB = SA::delegate<void(std::string_view)>;
    using StringChangeCB = SA::delegate<void(std::string_view, std::string_view)>;
    using FloatCB = SA::delegate<void(float)>;
    using DoubleCB = SA::delegate<void(double)>;
    using FloatChangeCB = SA::delegate<void(float, float)>;
    using DoubleChangeCB = SA::delegate<void(double, double)>;

    template <typename... Args>
    static inline constexpr bool cb_invocable = std::is_invocable_v<Args...>;

    template <typename C>
    static inline constexpr bool is_cb_delegate =
        std::is_same_v<C, VoidCB> || std::is_same_v<C, StringCB> || std::is_same_v<C, FloatCB> ||
        std::is_same_v<C, DoubleCB> || std::is_same_v<C, StringChangeCB> || std::is_same_v<C, FloatChangeCB> ||
        std::is_same_v<C, DoubleChangeCB>;

   private:
    // discriminated opaque storage for any callback delegate. all SA::delegate<R(Args...)>
    // share the same 2-pointer layout (object + stub), so a single sized/aligned buffer holds
    // any of them.
    enum class CallbackKind : uint8_t {
        None,
        Void,
        String,
        Float,
        Double,
        StringChange,
        FloatChange,
        DoubleChange,
    };
    struct CallbackSlot final {
        CallbackKind kind{CallbackKind::None};
        alignas(VoidCB) unsigned char storage[sizeof(VoidCB)]{};
    };

    // a value in both of its representations
    struct Value final {
        double d{0.0};
        std::string s{};
    };

    // ctor helper
    void addConVar();

   public:
    // utils
    static std::string_view typeToString(CONVAR_TYPE type);
    static std::string flagsToString(uint8_t flags);

   public:
    // ctors are neverinline due to causing giant compile-time blowups on TUs with many convars defined
    // they only ever run once at startup anyway, so there's no measurable runtime cost (besides maybe some extra milliseconds to boot)

    // command-only constructor
    neverinline explicit ConVar(const char *name, uint8_t flags = cv::CLIENT)
        : sName(name), sHelpString(""), defaultValue{.d = 0.0, .s = name} {
        this->type = CONVAR_TYPE::STRING;
        this->iFlags = cv::NOSAVE | flags;
        this->addConVar();
    };

    // callback-only constructors (no value)
    template <typename Callback>
    neverinline explicit ConVar(const char *name, uint8_t flags, Callback &&callback)
        requires cv::detail::CallbackCmd<Callback>
        : sName(name), sHelpString("") {
        this->setupCmdCallback(flags, std::forward<Callback>(callback));
        this->addConVar();
    }

    template <typename Callback>
    neverinline explicit ConVar(const char *name, uint8_t flags, const char *helpString, Callback &&callback)
        requires cv::detail::CallbackCmd<Callback>
        : sName(name), sHelpString(helpString) {
        this->setupCmdCallback(flags, std::forward<Callback>(callback));
        this->addConVar();
    }

    // value constructors handle all types uniformly
    template <typename T>
    neverinline explicit ConVar(const char *name, T &&defaultValue, uint8_t flags, const char *helpString = "")
        requires(!std::is_same_v<std::decay_t<T>, const char *>)
        : sName(name), sHelpString(helpString) {
        this->setupValue(std::forward<T>(defaultValue), flags);
        this->addConVar();
    }

    template <typename T, typename Callback>
    neverinline explicit ConVar(const char *name, T &&defaultValue, uint8_t flags, const char *helpString,
                                Callback &&callback)
        requires(!std::is_same_v<std::decay_t<T>, const char *>) && cv::detail::CallbackAny<Callback>
        : sName(name), sHelpString(helpString) {
        this->setupValue(std::forward<T>(defaultValue), flags);
        this->setCallback(std::forward<Callback>(callback));
        this->addConVar();
    }

    template <typename T>
    neverinline explicit ConVar(const char *name, T &&defaultValue, uint8_t flags, const char *helpString,
                                cv::Range range)
        requires std::is_arithmetic_v<std::decay_t<T>>
        : sName(name), sHelpString(helpString), range(range) {
        this->setupValue(std::forward<T>(defaultValue), flags);
        this->addConVar();
    }

    template <typename T, typename Callback>
    neverinline explicit ConVar(const char *name, T &&defaultValue, uint8_t flags, const char *helpString,
                                cv::Range range, Callback &&callback)
        requires std::is_arithmetic_v<std::decay_t<T>> && cv::detail::CallbackAny<Callback>
        : sName(name), sHelpString(helpString), range(range) {
        this->setupValue(std::forward<T>(defaultValue), flags);
        this->setCallback(std::forward<Callback>(callback));
        this->addConVar();
    }

    template <typename T, typename Callback>
    neverinline explicit ConVar(const char *name, T &&defaultValue, uint8_t flags, Callback &&callback)
        requires(!std::is_same_v<std::decay_t<T>, const char *>) && cv::detail::CallbackAny<Callback>
        : sName(name), sHelpString("") {
        this->setupValue(std::forward<T>(defaultValue), flags);
        this->setCallback(std::forward<Callback>(callback));
        this->addConVar();
    }

    // const char* specializations for string convars
    neverinline explicit ConVar(const char *name, std::string_view defaultValue, uint8_t flags,
                                const char *helpString = "")
        : sName(name), sHelpString(helpString) {
        this->initValueImpl(defaultValue, flags);
        this->addConVar();
    }

    template <typename Callback>
    neverinline explicit ConVar(const char *name, std::string_view defaultValue, uint8_t flags, const char *helpString,
                                Callback &&callback)
        requires cv::detail::CallbackAny<Callback>
        : sName(name), sHelpString(helpString) {
        this->initValueImpl(defaultValue, flags);
        this->setCallback(std::forward<Callback>(callback));
        this->addConVar();
    }

    template <typename Callback>
    neverinline explicit ConVar(const char *name, std::string_view defaultValue, uint8_t flags, Callback &&callback)
        requires cv::detail::CallbackAny<Callback>
        : sName(name), sHelpString("") {
        this->initValueImpl(defaultValue, flags);
        this->setCallback(std::forward<Callback>(callback));
        this->addConVar();
    }

    // callbacks
    void exec();
    void execArgs(std::string_view args);
    void execFloat(float args);
    void execDouble(double args);

    // every editor has a value of its own: the server's beats the skin's, which beats the client's (see resolve()).
    // not every text is something a convar can be set to: numeric ones only take numbers (and bool ones
    // "true"/"false"). anything else leaves the convar alone and is INVALID, which is for whoever lets text in from
    // outside to look at, since nobody else is able to tell anyone about it (debug_cv logs it as well)
    template <typename T>
    CvarSetResult setValue(const T &value, bool doCallback = true, CvarEditor editor = CvarEditor::CLIENT) {
        using D = std::decay_t<T>;
        // bool is convertible to double, so it flows through the numeric path and is stored as
        // "1"/"0" like ints/floats; the string overload parses/normalizes "true"/"false" back
        if constexpr(std::is_convertible_v<D, double>)
            return this->setValueImpl(static_cast<double>(value), doCallback, editor);
        else
            return this->setValueImpl(std::string_view{value}, doCallback, editor);
    }

    // takes the skin's/the server's value away again (the client's can't go away: it gets set back to the default)
    void clearValue(CvarEditor editor);

    // generic callback setter that auto-detects callback type
    template <typename Callback>
    void setCallback(Callback &&callback)
        requires cv::detail::CallbackAny<Callback>
    {
        assert(McThread::is_main_thread() && "convars belong to the main thread");
        using D = std::decay_t<Callback>;
        if constexpr(is_cb_delegate<D>)
            this->setCallbackImpl(std::forward<Callback>(callback));
        else if constexpr(cb_invocable<D>)
            this->setCallbackImpl(VoidCB(std::forward<Callback>(callback)));
        else if constexpr(cb_invocable<D, std::string_view>)
            this->setCallbackImpl(StringCB(std::forward<Callback>(callback)));
        else if constexpr(cb_invocable<D, float>)
            this->setCallbackImpl(FloatCB(std::forward<Callback>(callback)));
        else if constexpr(cb_invocable<D, double>)
            this->setCallbackImpl(DoubleCB(std::forward<Callback>(callback)));
        else if constexpr(cb_invocable<D, std::string_view, std::string_view>)
            this->setCallbackImpl(StringChangeCB(std::forward<Callback>(callback)));
        else if constexpr(cb_invocable<D, float, float>)
            this->setCallbackImpl(FloatChangeCB(std::forward<Callback>(callback)));
        else if constexpr(cb_invocable<D, double, double>)
            this->setCallbackImpl(DoubleChangeCB(std::forward<Callback>(callback)));
        else
            static_assert(Env::always_false_v<D>, "Unsupported callback signature");
    }

    void removeCallback();
    void removeChangeCallback();
    void removeAllCallbacks();

    // get
    template <typename T = int>
    [[nodiscard]] inline T getDefaultVal() const {
        return static_cast<T>(this->defaultValue.d);
    }
    [[nodiscard]] inline float getDefaultFloat() const { return static_cast<float>(this->defaultValue.d); }
    [[nodiscard]] inline double getDefaultDouble() const { return this->defaultValue.d; }
    [[nodiscard]] inline const std::string &getDefaultString() const { return this->defaultValue.s; }

    void setDefaultDouble(double newDefault);
    void setDefaultString(std::string_view newDefault);

    std::string getFancyDefaultValue() const;

    // a convar belongs to the main thread, with one exception: its value as a number can be read from anywhere.
    // (not as a string: that would be a reference to something the main thread may change or free at any time, so
    // whatever else needs one has to be handed a copy)
    [[nodiscard]] forceinline double getDouble() const { return this->dValue.load(std::memory_order_relaxed); }
    [[nodiscard]] forceinline const std::string &getString() const {
        assert(McThread::is_main_thread() && "string convars can only be read on the main thread");
        return *this->sValue;
    }

    template <typename T = int>
    [[nodiscard]] forceinline T getVal() const {
        return static_cast<T>(this->getDouble());
    }

    [[nodiscard]] forceinline int getInt() const { return static_cast<int>(this->getDouble()); }
    [[nodiscard]] forceinline bool getBool() const { return !!static_cast<int>(this->getDouble()); }
    [[nodiscard]] forceinline bool get() const { return !!static_cast<int>(this->getDouble()); }
    [[nodiscard]] forceinline float getFloat() const { return static_cast<float>(this->getDouble()); }

    [[nodiscard]] forceinline cv::Range getRange() const { return this->range; }
    [[nodiscard]] forceinline std::string_view getHelpstring() const { return this->sHelpString; }
    [[nodiscard]] forceinline std::string_view getName() const { return this->sName; }
    [[nodiscard]] forceinline CONVAR_TYPE getType() const { return this->type; }
    [[nodiscard]] forceinline uint8_t getFlags() const { return this->iFlags; }

    // who the current value comes from (SERVER also while the protection lock is what decides it)
    [[nodiscard]] forceinline CvarEditor getMaster() const { return this->master; }
    [[nodiscard]] forceinline bool canHaveValue() const { return this->bCanHaveValue; }

    [[nodiscard]] bool hasAnyNonVoidCallback() const;
    [[nodiscard]] bool hasSingleArgCallback() const;

    [[nodiscard]] inline bool isFlagSet(uint8_t flag) const { return ((this->iFlags & flag) == flag); }
    [[nodiscard]] inline bool isDefault() const {
        // a convar carries two representations (double + string), but (usually?) only one is authoritative per
        // type: the double for numeric convars, the string for STRING convars.
        // the other is derived and can diverge (e.g. a numeric convar set from text keeps the text as it was typed,
        // "1.50" next to a default string of "1.5").
        if(this->type == CONVAR_TYPE::STRING) return this->getString() == this->getDefaultString();
        return this->getDouble() == this->getDefaultDouble();
    }

    // the client's own value, no matter what is overriding it at the moment: this (and not what the getters above
    // return) is what belongs into the client's config
    [[nodiscard]] inline const std::string &getClientString() const {
        assert(McThread::is_main_thread() && "string convars can only be read on the main thread");
        return this->clientValue.s;
    }
    [[nodiscard]] inline bool isClientDefault() const {
        if(this->type == CONVAR_TYPE::STRING) return this->clientValue.s == this->defaultValue.s;
        return this->clientValue.d == this->defaultValue.d;
    }

    void setServerProtected(CvarProtection policy);

    [[nodiscard]] inline bool isProtected() const {
        switch(this->serverProtectionPolicy) {
            case CvarProtection::DEFAULT:
                return this->isFlagSet(cv::PROTECTED);
            case CvarProtection::PROTECTED:
                return true;
            case CvarProtection::UNPROTECTED:
            default:
                return false;
        }
    }

   private:
    // typed setValue impls — public setValue<T> dispatches into these based on T category
    // (bool routes through the double overload; there's no dedicated bool string form)
    CvarSetResult setValueImpl(double newDouble, bool doCallback, CvarEditor editor);
    CvarSetResult setValueImpl(std::string_view newString, bool doCallback, CvarEditor editor);

    // typed setCallback impls — public setCallback<C> dispatches into these
    void setCallbackImpl(VoidCB cb);
    void setCallbackImpl(StringCB cb);
    void setCallbackImpl(FloatCB cb);
    void setCallbackImpl(DoubleCB cb);
    void setCallbackImpl(StringChangeCB cb);
    void setCallbackImpl(FloatChangeCB cb);
    void setCallbackImpl(DoubleChangeCB cb);

    // constructor helpers — compile-time dispatch into typed init impls
    template <typename T>
    void setupValue(T &&v, uint8_t flags) {
        using D = std::decay_t<T>;
        if constexpr(std::is_same_v<D, bool>)
            this->initValueImpl(static_cast<bool>(v), flags);
        else if constexpr(std::is_integral_v<D>)
            this->initValueImpl(static_cast<int>(v), flags);
        else if constexpr(std::is_floating_point_v<D>)
            this->initValueImpl(static_cast<double>(v), flags);
        else
            this->initValueImpl(std::string_view{std::forward<T>(v)}, flags);
    }

    template <typename Callback>
    void setupCmdCallback(uint8_t flags, Callback &&cb) {
        using D = std::decay_t<Callback>;
        if constexpr(is_cb_delegate<D>)
            this->initCmdCallbackImpl(flags, std::forward<Callback>(cb));
        else if constexpr(cb_invocable<D>)
            this->initCmdCallbackImpl(flags, VoidCB(std::forward<Callback>(cb)));
        else if constexpr(cb_invocable<D, std::string_view>)
            this->initCmdCallbackImpl(flags, StringCB(std::forward<Callback>(cb)));
        else if constexpr(cb_invocable<D, float>)
            this->initCmdCallbackImpl(flags, FloatCB(std::forward<Callback>(cb)));
        else if constexpr(cb_invocable<D, double>)
            this->initCmdCallbackImpl(flags, DoubleCB(std::forward<Callback>(cb)));
        else
            static_assert(Env::always_false_v<D>, "Unsupported command callback signature");
    }

    void initValueImpl(bool v, uint8_t flags);
    void initValueImpl(int v, uint8_t flags);
    void initValueImpl(double v, uint8_t flags);
    void initValueImpl(std::string_view v, uint8_t flags);
    void initValueInt(Value value, uint8_t flags);

    void initCmdCallbackImpl(uint8_t flags, VoidCB cb);
    void initCmdCallbackImpl(uint8_t flags, StringCB cb);
    void initCmdCallbackImpl(uint8_t flags, FloatCB cb);
    void initCmdCallbackImpl(uint8_t flags, DoubleCB cb);

    // numeric view of text (which gets normalized for bool convars), false if this convar can't take it (see setValue())
    [[nodiscard]] bool parseValue(std::string_view &text, double &dbl) const;

    // what a number (that parseValue() may have gotten out of text) is as a value of this convar: inside of its range
    [[nodiscard]] Value makeValue(double dbl) const;
    [[nodiscard]] Value makeValue(double dbl, std::string_view text) const;

    // whether an editor gets to write at all, asked before anything changes (APPLIED: nothing against it)
    [[nodiscard]] CvarSetResult checkWrite(CvarEditor editor) const;

    // puts a value where that editor's go, for the next resolve() to pick up
    void store(CvarEditor editor, Value value);

    // central store-and-dispatch routine called by both setValueImpl overloads
    CvarSetResult setValueInt(Value newValue, bool doCallback, CvarEditor editor);

    // recomputes what the getters return: the only place that picks between the default/client/skin/server values.
    // has to run after every change to something it looks at (setValueInt does for writes, everything else
    // goes through snapshot() -> change -> resolve() -> notifyIfChanged())
    void resolve();

    [[nodiscard]] Value snapshot() const { return {.d = this->getDouble(), .s = this->getString()}; }
    void notifyIfChanged(const Value &old);

    // the value is a different one now: the app's policy hears about it (before the convar's own callbacks)
    void valueChanged() const;
    void runCallbacks(double oldDouble, std::string_view oldString);

   private:
    // what the getters return, published by resolve() (first, so that a read only touches the start of the object)
    // dValue is the only member other threads get to look at
    std::atomic<double> dValue{0.0};
    const std::string *sValue{nullptr};

    std::string_view sName;
    std::string_view sHelpString;

    cv::Range range{.min = -std::numeric_limits<double>::infinity(), .max = std::numeric_limits<double>::infinity()};

    Value defaultValue{};
    Value clientValue{};
    std::unique_ptr<Value> skinValue{nullptr};    // null if the skin doesn't set this convar
    std::unique_ptr<Value> serverValue{nullptr};  // ditto for the server

    // callback storage (allow having 1 "change" callback and 1 single value (or void) callback)
    CallbackSlot callback;
    CallbackSlot changeCallback;

    CvarProtection serverProtectionPolicy{CvarProtection::DEFAULT};
    CvarEditor master{CvarEditor::CLIENT};

    CONVAR_TYPE type{CONVAR_TYPE::FLOAT};
    uint8_t iFlags{0};

    bool bCanHaveValue{false};
    bool bProtectedNonDefault{false};  // what ConVarHandler keeps count of (kept up to date by resolve())
};

#endif
