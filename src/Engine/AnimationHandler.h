// Copyright (c) 2012, PG & 2026, WH, All rights reserved.
#ifndef ANIMATIONHANDLER_H
#define ANIMATIONHANDLER_H

#include "types.h"
#include "Vectors_fwd.h"

#include <concepts>

namespace AnimationHandler {

// cv::debug_anim change callback
void onDebugAnimChange(float newVal);

template <typename T>
concept AnimatableType = std::same_as<T, f32> || std::same_as<T, f64>;

// --- handle-based animation system ---

enum class Ease : u8 {
    Linear,
    QuadIn,
    QuadOut,
    QuadInOut,
    CubicIn,
    CubicOut,
    QuartIn,
    QuartOut,
};

using enum Ease;

// handle to an animated value. value is stored inline; a pool slot is only
// allocated while an animation is active (lazy allocation).
// non-copyable, movable. destructor cancels active animations and frees the slot.
template <AnimatableType T>
class AnimHandleT {
   public:
    constexpr AnimHandleT() = default;
    explicit constexpr AnimHandleT(T initial) : m_value{initial} {}
    ~AnimHandleT();

    AnimHandleT(AnimHandleT &&o) noexcept;
    AnimHandleT &operator=(AnimHandleT &&o) noexcept;

    AnimHandleT(const AnimHandleT &) = delete;
    AnimHandleT &operator=(const AnimHandleT &) = delete;

    // read the current value
    [[nodiscard]] constexpr operator T() const { return m_value; }

    // set the value directly (does not cancel active animations)
    AnimHandleT &operator=(T value);

    // start an animation, canceling any existing ones on this handle (overrideExisting=true equivalent)
    void set(T target, T duration, Ease ease);
    void set(T target, T duration, Ease ease, T delay);

    // start an animation without canceling existing ones (overrideExisting=false equivalent)
    void append(T target, T duration, Ease ease);
    void append(T target, T duration, Ease ease, T delay);

    [[nodiscard]] bool animating() const;
    [[nodiscard]] T remaining() const;
    void stop();  // cancel animations, keep current value

    static constexpr u16 NULL_SLOT = u16{0xFFFF};

   private:
    mutable T m_value{0};
    mutable u16 m_slot{NULL_SLOT};
};

using AnimFloat = AnimHandleT<f32>;
using AnimDouble = AnimHandleT<f64>;

// instantiated explicitly in AnimationHandler.cpp
extern template class AnimHandleT<f32>;
extern template class AnimHandleT<f64>;

struct AnimVec2 {
    AnimFloat x, y;

    constexpr AnimVec2() = default;
    explicit constexpr AnimVec2(f32 initial1, f32 initial2) : x(initial1), y(initial2) {}
    explicit constexpr AnimVec2(f64 initial1, f64 initial2)
        : x(static_cast<f32>(initial1)), y(static_cast<f32>(initial2)) {}
    explicit AnimVec2(vec2 initial);
    explicit AnimVec2(dvec2 initial);

    void stop();

    AnimVec2 &operator=(vec2 value);
    [[nodiscard]] operator vec2() const;
};

struct AnimVec2D {
    AnimDouble x, y;

    constexpr AnimVec2D() = default;
    explicit constexpr AnimVec2D(f32 initial1, f32 initial2) : x(initial1), y(initial2) {}
    explicit constexpr AnimVec2D(f64 initial1, f64 initial2) : x(initial1), y(initial2) {}
    explicit AnimVec2D(vec2 initial);
    explicit AnimVec2D(dvec2 initial);

    void stop();

    AnimVec2D &operator=(dvec2 value);
    [[nodiscard]] operator dvec2() const;
};

// --- engine-level functions ---

// called by engine once per frame, after updating time
void update(f64 frameTime);
void clearAll();  // called when shutting down, for safety

[[nodiscard]] uSz getNumActiveAnimations();

}  // namespace AnimationHandler

namespace anim = AnimationHandler;
using anim::AnimDouble;
using anim::AnimFloat;
using anim::AnimVec2;
using anim::AnimVec2D;

#endif
