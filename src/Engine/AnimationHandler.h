// Copyright (c) 2012, PG & 2026, WH, All rights reserved.
#ifndef ANIMATIONHANDLER_H
#define ANIMATIONHANDLER_H

#include "types.h"

#include <glm/vec2.hpp>

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
    AnimHandleT() = default;
    explicit AnimHandleT(T initial);
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

struct AnimVec2 {
    AnimFloat x, y;

    AnimVec2() = default;
    explicit AnimVec2(f32 initial1, f32 initial2 = f32{0}) {
        x = initial1;
        y = initial2;
    }
    explicit AnimVec2(f64 initial1, f64 initial2 = f64{0}) {
        x = static_cast<f32>(initial1);
        y = static_cast<f32>(initial2);
    }
    explicit AnimVec2(glm::vec2 initial) {
        x = initial.x;
        y = initial.y;
    }
    explicit AnimVec2(glm::dvec2 initial) {
        x = static_cast<f32>(initial.x);
        y = static_cast<f32>(initial.y);
    }

    inline void stop() {
        x.stop();
        y.stop();
    }

    inline AnimVec2 &operator=(glm::vec2 value) {
        x = value.x;
        y = value.y;
        return *this;
    }

    [[nodiscard]] constexpr operator glm::vec2() const { return glm::vec2{f32(x), f32(y)}; }
};

struct AnimVec2D {
    AnimDouble x, y;

    AnimVec2D() = default;
    explicit AnimVec2D(f32 initial1, f32 initial2 = f32{0}) {
        x = initial1;
        y = initial2;
    }
    explicit AnimVec2D(f64 initial1, f64 initial2 = f64{0}) {
        x = initial1;
        y = initial2;
    }
    explicit AnimVec2D(glm::vec2 initial) {
        x = initial.x;
        y = initial.y;
    }
    explicit AnimVec2D(glm::dvec2 initial) {
        x = initial.x;
        y = initial.y;
    }

    inline void stop() {
        x.stop();
        y.stop();
    }

    inline AnimVec2D &operator=(glm::dvec2 value) {
        x = value.x;
        y = value.y;
        return *this;
    }

    [[nodiscard]] constexpr operator glm::dvec2() const { return glm::dvec2{f64(x), f64(y)}; }
};

// --- engine-level functions ---

// called by engine once per frame, after updating time
void update();
void clearAll();  // called when shutting down, for safety

[[nodiscard]] uSz getNumActiveAnimations();

}  // namespace AnimationHandler

namespace anim = AnimationHandler;
using anim::AnimDouble;
using anim::AnimFloat;
using anim::AnimVec2;
using anim::AnimVec2D;

#endif
