// Copyright (c) 2025, WH, All rights reserved.
// miscellaneous utilities/macros which don't require transitive includes
#pragma once

#if defined(_CLANGD) || defined(Q_CREATOR_RUN) || defined(__INTELLISENSE__) || defined(__CDT_PARSER__) || \
    defined(__clang_analyzer__)
#define MC_IS_IDE
#endif

#define SAFE_DELETE(p)  \
    {                   \
        if(p) {         \
            delete(p);  \
            (p) = NULL; \
        }               \
    }

#ifdef PI
#undef PI
#endif

#ifdef PIOVER180
#undef PIOVER180
#endif

#define PI 3.141592653589793238462643383279502884
#define PIOVER180 0.017453292519943295769236907684886128

#define PI_F static_cast<float>(PI)
#define PIOVER180_F static_cast<float>(PIOVER180)

inline bool isInt(float f) { return (f == static_cast<float>(static_cast<int>(f))); }

// not copy or move constructable/assignable
// purely for clarifying intent
#define NOCOPY_NOMOVE(classname__)                        \
   public:                                                \
    classname__(const classname__ &) = delete;            \
    classname__ &operator=(const classname__ &) = delete; \
    classname__(classname__ &&) = delete;                 \
    classname__ &operator=(classname__ &&) = delete;      \
                                                          \
   private:

// only move constructable, nothing else
#define MOVECONSTRUCTONLY(classname__)                        \
   public:                                                    \
    classname__(const classname__ &) = delete;                \
    classname__ &operator=(const classname__ &) = delete;     \
    classname__(classname__ &&) noexcept = default;           \
    classname__ &operator=(classname__ &&) noexcept = delete; \
                                                              \
   private:

// create string view literal
#define MC_SV(string__) \
    std::string_view_literals::operator""sv(string__, (sizeof(string__) / sizeof((string__)[0]) - 1))

#if defined(__GNUC__) || defined(__clang__)
#define likely(x) __builtin_expect(bool(x), 1)
#define unlikely(x) __builtin_expect(bool(x), 0)
#define really_forceinline __attribute__((always_inline)) inline
#define neverinline __attribute__((noinline))

// force all functions in the function body to be inlined into it
// different from "really_forceinline", because the function itself won't necessarily be inlined at all call sites
#define REALLY_INLINE_BODY __attribute__((flatten))

#define CONSTINIT constinit

#else
#define likely(x) (x)
#define unlikely(x) (x)
#ifdef _MSC_VER

// HACK: ignore "constinit" keyword since it basically doesn't work on MSVC
#define CONSTINIT

#define really_forceinline __forceinline
#define neverinline __declspec(noinline)
#else
#define really_forceinline inline
#define neverinline
#endif
#define REALLY_INLINE_BODY
#endif

// avoid always_inline for debug builds because it 1. slows down compilation and 2. makes debugging harder
#ifdef _DEBUG
#define forceinline inline
#define INLINE_BODY
#else
#define forceinline really_forceinline
#define INLINE_BODY REALLY_INLINE_BODY
#endif

#if defined(__clang__)
#define MC_ASSUME(expr) __builtin_assume(expr)
#elif defined(__GNUC__)
#if defined(__has_attribute) && __has_attribute(assume)
#define MC_ASSUME(expr) __attribute__((assume(expr)))
#else
#define MC_ASSUME(expr)              \
    do {                             \
        if(expr) {                   \
        } else {                     \
            __builtin_unreachable(); \
        }                            \
    } while(false)
#endif
#elif defined(_MSC_VER)
#define MC_ASSUME(expr) __assume(expr)
#endif

#ifdef __AVX512F__
#define OPTIMAL_UNROLL 10
#elif defined(__AVX__)
#define OPTIMAL_UNROLL 8
#elif defined(__SSE__)
#define OPTIMAL_UNROLL 6
#else
#define OPTIMAL_UNROLL 4
#endif

#define MC_QUOTE(s) #s
#define MC_STRINGIZE(s) MC_QUOTE(s)

#define MC_DO_PRAGMA(x) _Pragma(MC_STRINGIZE(x))
#define MC_MESSAGE(msg) MC_DO_PRAGMA(message(msg))

#if defined(__GNUC__) || defined(__clang__)
#ifdef __clang__
#define MC_VECTORIZE_LOOP MC_DO_PRAGMA(clang loop vectorize(enable))
#define MC_UNR_cnt(num) MC_DO_PRAGMA(clang loop unroll_count(num))
#define NULL_PUSH
#define NULL_POP
#else
#define MC_VECTORIZE_LOOP MC_DO_PRAGMA(GCC ivdep)
#define MC_UNR_cnt(num) MC_DO_PRAGMA(GCC unroll num)
#define NULL_PUSH MC_DO_PRAGMA(GCC diagnostic ignored "-Wformat") MC_DO_PRAGMA(GCC diagnostic push)
#define NULL_POP MC_DO_PRAGMA(GCC diagnostic pop)
#endif

#define MC_VEC_UNR_cnt(num) MC_VECTORIZE_LOOP MC_UNR_cnt(num)
#define MC_UNROLL_VECTOR MC_VEC_UNR_cnt(OPTIMAL_UNROLL)
#define MC_UNROLL MC_UNR_cnt(OPTIMAL_UNROLL)

#ifdef _OPENMP
#define ACCUMULATE(op, var) MC_DO_PRAGMA(omp simd reduction(op : var))  // use openmp if available, otherwise unroll
#else
#define ACCUMULATE(op, var) MC_UNR_cnt(OPTIMAL_UNROLL)
#endif

#else

#define MC_VECTORIZE_LOOP
#define MC_UNR_cnt(num)
#define MC_VEC_UNR_cnt(num)
#define MC_UNROLL_VECTOR
#define MC_UNROLL
#define NULL_PUSH
#define NULL_POP
#define ACCUMULATE(op, var)
#endif  // defined(__GNUC__) || defined(__clang__)

#ifdef _WIN32
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")  // msvc wrangling
#endif
// fix including win32 headers without Windows.h
#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_AMD64_) && \
    !defined(_ARM_) && !defined(_ARM64_) && !defined(_ARM64EC_) && defined(_M_IX86)
#define _X86_
#if !defined(_CHPE_X86_ARM64_) && defined(_M_HYBRID)
#define _CHPE_X86_ARM64_
#endif
#endif

#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_AMD64_) && \
    !defined(_ARM_) && !defined(_ARM64_) && (defined(_M_AMD64) || defined(_M_ARM64EC))
#define _AMD64_
#endif

#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_AMD64_) && \
    !defined(_ARM_) && !defined(_ARM64_) && !defined(_ARM64EC_) && defined(_M_ARM)
#define _ARM_
#endif

#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_AMD64_) && \
    !defined(_ARM_) && !defined(_ARM64_) && !defined(_ARM64EC_) && defined(_M_ARM64)
#define _ARM64_
#endif

#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_ARM_) && \
    !defined(_ARM64_) && !defined(_ARM64EC_) && defined(_M_ARM64EC)
#define _ARM64EC_
#endif

#endif

namespace Env {
template <typename T>
constexpr bool always_false_v = false;
}

#define MAKE_FLAG_ENUM(Enum_name__) \
    inline constexpr bool is_flag(Enum_name__) { return true; }

namespace flags {
namespace detail {
// minimal enable_if implementation
template <bool B, class T = void>
struct enable_if {};

template <class T>
struct enable_if<true, T> {
    typedef T type;
};

// check if unsigned using the property that unsigned(-1) > 0
template <typename T>
struct is_unsigned {
    static constexpr inline bool value = T(-1) > T(0);
};

// check if type is an enum using compiler intrinsic
template <typename T>
struct is_enum {
    static constexpr inline bool value = __is_enum(T);
};

// get underlying type using compiler intrinsic (safe for non-enums)
template <typename T, bool is_enum>
struct underlying_type_impl {
    typedef T type;  // fallback for non-enums
};

template <typename T>
struct underlying_type_impl<T, true> {
    typedef __underlying_type(T) type;
};

template <typename T>
struct underlying_type {
    typedef typename underlying_type_impl<T, is_enum<T>::value>::type type;
};

// helper to check if underlying type is unsigned (only valid for enums)
template <typename T, bool is_enum>
struct is_unsigned_enum_impl {
    static const bool value = false;
};

template <typename T>
struct is_unsigned_enum_impl<T, true> {
    static const bool value = is_unsigned<typename underlying_type<T>::type>::value;
};

// check if type is an unsigned enum
template <typename T>
struct is_unsigned_enum {
    static const bool value = is_unsigned_enum_impl<T, is_enum<T>::value>::value;
};

// SFINAE check if is_flag(T{}) exists and returns true
template <typename T>
constexpr auto check_is_flag(int /**/) -> decltype(is_flag(T{}), bool()) {
    return is_flag(T{});
}

template <typename T>
// NOLINTNEXTLINE(cert-dcl50-cpp)
constexpr bool check_is_flag(...) {
    return false;
}

// check if type is a flag enum (unsigned enum with is_flag() returning true)
template <typename T>
struct is_flag_enum {
    static const bool value = is_unsigned_enum<T>::value && check_is_flag<T>(0);
};

template <typename T>
constexpr typename underlying_type<T>::type underlying_value(T enum_value) {
    return static_cast<typename underlying_type<T>::type>(enum_value);
}

}  // namespace detail

namespace operators {
// NOLINTBEGIN(modernize-use-constraints)
template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, T>::type operator~(T value) {
    return static_cast<T>(~detail::underlying_value(value));
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, bool>::type operator!(T value) {
    return detail::underlying_value(value) == 0;
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, T>::type operator|(T lhs, T rhs) {
    return static_cast<T>(detail::underlying_value(lhs) | detail::underlying_value(rhs));
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, typename detail::underlying_type<T>::type>::type
operator|(typename detail::underlying_type<T>::type lhs, T rhs) {
    return static_cast<typename detail::underlying_type<T>::type>(lhs | detail::underlying_value(rhs));
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, typename detail::underlying_type<T>::type>::type
operator|(T lhs, typename detail::underlying_type<T>::type rhs) {
    return static_cast<typename detail::underlying_type<T>::type>(detail::underlying_value(lhs) | rhs);
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, typename detail::underlying_type<T>::type>::type &
operator|=(typename detail::underlying_type<T>::type &value, T const flag) {
    return value = value | flag;
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, T>::type &operator|=(T &lhs, T const rhs) {
    return lhs = static_cast<T>(lhs | rhs);
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, T>::type &operator|=(
    T &lhs, typename detail::underlying_type<T>::type const rhs) {
    return lhs = static_cast<T>(lhs | rhs);
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, T>::type operator&(T lhs, T rhs) {
    return static_cast<T>(detail::underlying_value(lhs) & detail::underlying_value(rhs));
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, typename detail::underlying_type<T>::type>::type
operator&(typename detail::underlying_type<T>::type lhs, T rhs) {
    return static_cast<typename detail::underlying_type<T>::type>(lhs & detail::underlying_value(rhs));
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, typename detail::underlying_type<T>::type>::type
operator&(T lhs, typename detail::underlying_type<T>::type rhs) {
    return static_cast<typename detail::underlying_type<T>::type>(detail::underlying_value(lhs) & rhs);
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, typename detail::underlying_type<T>::type>::type &
operator&=(typename detail::underlying_type<T>::type &value, T const flag) {
    return value = value & flag;
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, T>::type &operator&=(T &lhs, T const rhs) {
    return lhs = static_cast<T>(lhs & rhs);
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, T>::type &operator&=(
    T &lhs, typename detail::underlying_type<T>::type const rhs) {
    return lhs = static_cast<T>(lhs & rhs);
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, T>::type operator^(T lhs, T rhs) {
    return static_cast<T>(detail::underlying_value(lhs) ^ detail::underlying_value(rhs));
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, typename detail::underlying_type<T>::type>::type
operator^(typename detail::underlying_type<T>::type lhs, T rhs) {
    return static_cast<typename detail::underlying_type<T>::type>(lhs ^ detail::underlying_value(rhs));
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, typename detail::underlying_type<T>::type>::type
operator^(T lhs, typename detail::underlying_type<T>::type rhs) {
    return static_cast<typename detail::underlying_type<T>::type>(detail::underlying_value(lhs) ^ rhs);
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, typename detail::underlying_type<T>::type>::type &
operator^=(typename detail::underlying_type<T>::type &value, T const flag) {
    return value = value ^ flag;
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, T>::type &operator^=(T &lhs, T const rhs) {
    return lhs = static_cast<T>(lhs ^ rhs);
}

template <typename T>
constexpr typename detail::enable_if<detail::is_flag_enum<T>::value, T>::type &operator^=(
    T &lhs, typename detail::underlying_type<T>::type const rhs) {
    return lhs = static_cast<T>(lhs ^ rhs);
}
}  // namespace operators

template <auto mask>
constexpr typename detail::enable_if<detail::is_flag_enum<decltype(mask)>::value, bool>::type has(
    typename detail::underlying_type<decltype(mask)>::type value) {
    using namespace operators;
    return (value & mask) == detail::underlying_value(mask);
}

template <auto mask>
constexpr typename detail::enable_if<detail::is_flag_enum<decltype(mask)>::value, bool>::type has(
    decltype(mask) value) {
    using namespace operators;
    return (value & mask) == mask;
}

template <auto mask>
constexpr typename detail::enable_if<detail::is_flag_enum<decltype(mask)>::value, bool>::type any(
    decltype(mask) value) {
    using namespace operators;
    return !!(value & mask);
}

// NOLINTEND(modernize-use-constraints)

}  // namespace flags
