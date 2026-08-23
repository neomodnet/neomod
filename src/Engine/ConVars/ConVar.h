// Copyright (c) 2011, PG & 2025, WH & 2025, kiwec, All rights reserved.
#ifndef CONVAR_H
#define CONVAR_H

#include "BaseEnvironment.h"

#include "Delegate.h"

#include <atomic>
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
        : sName(name), sHelpString(""), sDefaultValue(name) {
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

    template <typename T>
    void setValue(T &&value, bool doCallback = true, CvarEditor editor = CvarEditor::CLIENT) {
        using D = std::decay_t<T>;
        // bool is convertible to double, so it flows through the numeric path and is stored as
        // "1"/"0" like ints/floats; the std::string overload parses/normalizes "true"/"false" back
        if constexpr(std::is_convertible_v<D, double>)
            this->setValueImpl(static_cast<double>(value), doCallback, editor);
        else
            this->setValueImpl(std::string{std::forward<T>(value)}, doCallback, editor);
    }

    // generic callback setter that auto-detects callback type
    template <typename Callback>
    void setCallback(Callback &&callback)
        requires cv::detail::CallbackAny<Callback>
    {
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
    void reset();

    // get
    template <typename T = int>
    [[nodiscard]] inline T getDefaultVal() const {
        return static_cast<T>(this->dDefaultValue);
    }
    [[nodiscard]] inline float getDefaultFloat() const { return static_cast<float>(this->dDefaultValue); }
    [[nodiscard]] inline double getDefaultDouble() const { return this->dDefaultValue; }
    [[nodiscard]] inline const std::string &getDefaultString() const { return this->sDefaultValue; }

    void setDefaultDouble(double defaultValue);
    void setDefaultString(std::string_view defaultValue);

    std::string getFancyDefaultValue() const;

    [[nodiscard]] inline double getDouble() const {
        if(likely(this->bUseCachedDouble.load(std::memory_order_relaxed))) {
            return this->dCachedReturnedDouble.load(std::memory_order_relaxed);
        }
        return this->getDoubleInt();
    }
    [[nodiscard]] inline const std::string &getString() const {
        if(likely(this->bUseCachedString.load(std::memory_order_relaxed))) {
            return *(this->sCachedReturnedString.load(std::memory_order_relaxed));
        }
        return this->getStringInt();
    }

    template <typename T = int>
    [[nodiscard]] forceinline T getVal() const {
        return static_cast<T>(this->getDouble());
    }

    [[nodiscard]] forceinline int getInt() const { return static_cast<int>(this->getDouble()); }
    [[nodiscard]] forceinline bool getBool() const { return !!static_cast<int>(this->getDouble()); }
    [[nodiscard]] forceinline bool get() const { return !!static_cast<int>(this->getDouble()); }
    [[nodiscard]] forceinline float getFloat() const { return static_cast<float>(this->getDouble()); }

    [[nodiscard]] forceinline std::string_view getHelpstring() const { return this->sHelpString; }
    [[nodiscard]] forceinline std::string_view getName() const { return this->sName; }
    [[nodiscard]] forceinline CONVAR_TYPE getType() const { return this->type; }
    [[nodiscard]] forceinline uint8_t getFlags() const { return this->iFlags; }

    [[nodiscard]] CvarEditor getMaster() const;
    [[nodiscard]] forceinline bool canHaveValue() const { return this->bCanHaveValue; }

    [[nodiscard]] bool hasAnyNonVoidCallback() const;
    [[nodiscard]] bool hasSingleArgCallback() const;

    [[nodiscard]] inline bool isFlagSet(uint8_t flag) const { return ((this->iFlags & flag) == flag); }
    [[nodiscard]] inline bool isDefault() const {
        // a convar carries two representations (double + string), but (usually?) only one is authoritative per
        // type: the double for numeric convars, the string for STRING convars.
        // the other is derived and can diverge (e.g. an unparseable/"true"/"false" string set on a numeric convar
        // resets the double to default but leaves the raw text behind).
        if(this->type == CONVAR_TYPE::STRING) return this->getString() == this->getDefaultString();
        return this->getDouble() == this->getDefaultDouble();
    }

    void setServerProtected(CvarProtection policy) {
        this->serverProtectionPolicy.store(policy, std::memory_order_release);
        this->invalidateCache();
    }

    [[nodiscard]] inline bool isProtected() const {
        switch(this->serverProtectionPolicy.load(std::memory_order_acquire)) {
            case CvarProtection::DEFAULT:
                return this->isFlagSet(cv::PROTECTED);
            case CvarProtection::PROTECTED:
                return true;
            case CvarProtection::UNPROTECTED:
            default:
                return false;
        }
    }

    // shared callbacks, app-defined
    static void setOnSetValueProtectedCallback(const VoidCB &callback);

    using ProtectedCVGetCB = bool (*)(std::string_view cvarname);
    static void setOnGetValueProtectedCallback(ProtectedCVGetCB func);

    using GameplayCVChangeCB = bool (*)(std::string_view cvarname, CvarEditor setterkind);
    static void setOnSetValueGameplayCallback(GameplayCVChangeCB func);

   private:
    // typed setValue impls — public setValue<T> dispatches into these based on T category
    // (bool routes through the double overload; there's no dedicated bool string form)
    void setValueImpl(double newDouble, bool doCallback, CvarEditor editor);
    void setValueImpl(std::string newString, bool doCallback, CvarEditor editor);

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

    void initCmdCallbackImpl(uint8_t flags, VoidCB cb);
    void initCmdCallbackImpl(uint8_t flags, StringCB cb);
    void initCmdCallbackImpl(uint8_t flags, FloatCB cb);
    void initCmdCallbackImpl(uint8_t flags, DoubleCB cb);

    // central store-and-dispatch routine called by all 3 setValueImpl overloads
    void setValueInt(double newDouble, std::string newString, bool doCallback, CvarEditor editor);

    [[nodiscard]] double getDoubleInt() const;
    [[nodiscard]] const std::string &getStringInt() const;

    inline void invalidateCache() {
        // invalidate cache, after we stored new values
        this->bUseCachedDouble.store(false, std::memory_order_release);
        this->bUseCachedString.store(false, std::memory_order_release);
    }

   private:
    // static callbacks are shared across all convars
    // to call when a convar with PROTECTED flag has been changed
    static VoidCB onSetValueProtectedCallback;

    // to call when a PROTECTED convar has getString or getValue called on it
    // if the callback returns FALSE, the default value will be returned instead
    // TODO/LOOK INTO: this is only called if it doesn't have a skin or server value, is that cheeseable?
    static ProtectedCVGetCB onGetValueProtectedCallback;

    // to call when a GAMEPLAY convar is being changed
    // if the callback returns FALSE, the convar won't be changed
    static GameplayCVChangeCB onSetValueGameplayCallback;

    std::string_view sName;
    std::string_view sHelpString;

    std::string sDefaultValue{};
    double dDefaultValue{0.0};

    std::atomic<double> dClientValue{0.0};
    std::string sClientValue{};

    std::atomic<double> dSkinValue{0.0};
    std::string sSkinValue{};

    std::atomic<double> dServerValue{0.0};
    std::string sServerValue{};

    // just return cached values to avoid checking flags, unless something changed
    mutable std::atomic<const std::string *> sCachedReturnedString{&sDefaultValue};
    mutable std::atomic<double> dCachedReturnedDouble{0.};

    // callback storage (allow having 1 "change" callback and 1 single value (or void) callback)
    CallbackSlot callback;
    CallbackSlot changeCallback;

    std::atomic<CvarProtection> serverProtectionPolicy{CvarProtection::DEFAULT};

    CONVAR_TYPE type{CONVAR_TYPE::FLOAT};
    uint8_t iFlags{0};

    bool bCanHaveValue{false};
    std::atomic<bool> hasServerValue{false};
    std::atomic<bool> hasSkinValue{false};

    mutable std::atomic<bool> bUseCachedDouble{false};
    mutable std::atomic<bool> bUseCachedString{false};
};

#endif
