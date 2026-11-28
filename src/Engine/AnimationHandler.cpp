// Copyright (c) 2012, PG & 2026, WH, All rights reserved.
#include "AnimationHandler.h"

#include "noinclude.h"

#include "ConVar.h"
#include "Logging.h"
#include "Vectors.h"
#include "Algorithms.h"

#include <cmath>
#include <numbers>

namespace AnimationHandler {
static bool s_doLogging{false};

void onDebugAnimChange(float newVal) { s_doLogging = !!static_cast<int>(newVal); }

namespace {

template <AnimatableType T>
T easeIn(Ease type, T x) {
    constexpr T one{1};
    switch(type) {
        case Ease::QuadIn:
            return x * x;
        case Ease::CubicIn:
            return x * x * x;
        case Ease::QuartIn:
            return x * x * x * x;
        case Ease::QuintIn:
            return x * x * x * x * x;
        case Ease::SineIn:
            return one - std::cos(x * std::numbers::pi_v<T> / T{2});
        case Ease::ExpoIn:
            return std::pow(T{2}, T{10} * x - T{10});
        case Ease::CircIn:
            return one - std::sqrt(one - x * x);
        case Ease::ElasticIn:
            return one - (std::pow(T{2}, T{-10} * (one - x)) *
                              std::sin(((one - x) - T{0.075}) * T{2} * std::numbers::pi_v<T> / T{0.3}) +
                          one);
        case Ease::BackIn:
            return x * x * ((T{1.70158} + one) * x - T{1.70158});
        case Ease::BounceIn: {
            x = one - x;
            T out;
            if(x < T{1 / 2.75}) {
                out = T{7.5625} * x * x;
            } else if(x < T{2 / 2.75}) {
                x -= T{1.5 / 2.75};
                out = T{7.5625} * x * x + T{0.75};
            } else if(x < T{2.5 / 2.75}) {
                x -= T{2.25 / 2.75};
                out = T{7.5625} * x * x + T{0.9375};
            } else {
                x -= T{2.625 / 2.75};
                out = T{7.5625} * x * x + T{0.984375};
            }
            return one - out;
        }
        default:
            return x;
    }
}

}  // namespace

template <AnimatableType T>
T ease(Ease type, T x) {
    constexpr T half{0.5};
    constexpr T one{1};
    constexpr T two{2};

    const auto out = [x](Ease in) -> T { return one - easeIn(in, one - x); };
    const auto inOut = [x](Ease in) -> T {
        return half * (x < half ? easeIn(in, two * x) : two - easeIn(in, two - two * x));
    };
    const auto elasticOut = [x](T periods) -> T {
        return std::pow(two, T{-10} * x) * std::sin((periods * x - T{0.075}) * two * std::numbers::pi_v<T> / T{0.3}) +
               one;
    };

    switch(type) {
        case Ease::LegacyIn:
            return easeIn(Ease::QuadIn, x);
        case Ease::LegacyOut:
            return out(Ease::QuadIn);
        case Ease::QuadIn:
        case Ease::CubicIn:
        case Ease::QuartIn:
        case Ease::QuintIn:
        case Ease::SineIn:
        case Ease::ExpoIn:
        case Ease::CircIn:
        case Ease::ElasticIn:
        case Ease::BackIn:
        case Ease::BounceIn:
            return easeIn(type, x);
        case Ease::QuadOut:
            return out(Ease::QuadIn);
        case Ease::CubicOut:
            return out(Ease::CubicIn);
        case Ease::QuartOut:
            return out(Ease::QuartIn);
        case Ease::QuintOut:
            return out(Ease::QuintIn);
        case Ease::SineOut:
            return out(Ease::SineIn);
        case Ease::ExpoOut:
            return out(Ease::ExpoIn);
        case Ease::CircOut:
            return out(Ease::CircIn);
        case Ease::ElasticOut:
            return elasticOut(one);
        case Ease::ElasticHalfOut:
            return elasticOut(half);
        case Ease::ElasticQuarterOut:
            return elasticOut(T{0.25});
        case Ease::BackOut:
            return out(Ease::BackIn);
        case Ease::BounceOut:
            return out(Ease::BounceIn);
        case Ease::QuadInOut:
            return inOut(Ease::QuadIn);
        case Ease::CubicInOut:
            return inOut(Ease::CubicIn);
        case Ease::QuartInOut:
            return inOut(Ease::QuartIn);
        case Ease::QuintInOut:
            return inOut(Ease::QuintIn);
        case Ease::SineInOut:
            return inOut(Ease::SineIn);
        case Ease::ExpoInOut:
            return inOut(Ease::ExpoIn);
        case Ease::CircInOut:
            return inOut(Ease::CircIn);
        case Ease::BounceInOut:
            return inOut(Ease::BounceIn);
        case Ease::ElasticInOut: {
            constexpr T c = two * std::numbers::pi_v<T> / T{0.3} / T{1.5};
            const T t = x * two;
            if(t < one) return -half * std::pow(two, T{-10} + T{10} * t) * std::sin((one - t - T{0.075 * 1.5}) * c);
            return half * std::pow(two, T{-10} * (t - one)) * std::sin((t - one - T{0.075 * 1.5}) * c) + one;
        }
        case Ease::BackInOut: {
            constexpr T s = T{1.70158 * 1.525};
            const T t = x < half ? two * x : two - two * x;
            const T in = t * t * ((s + one) * t - s);
            return half * (x < half ? in : two - in);
        }
        case Ease::Linear:
        case Ease::COUNT:
            break;
    }
    return x;
}

template f32 ease<f32>(Ease type, f32 percent);
template f64 ease<f64>(Ease type, f64 percent);

namespace {

// --- slot pool: all animation state for one float type ---

static constexpr uSz MAX_SLOTS_F32 = 2048;
static constexpr uSz MAX_SLOTS_F64 = 1024;

// per-animation entry
template <AnimatableType T>
struct Anim final {
    T target;
    T duration;
    T startValue;
    T delay;
    T elapsedTime;
    Ease animType;
    bool started;

    // --- per-animation tick (returns true if animation finished) ---

    forceinline INLINE_BODY bool tick(T &value, T frameTime, u16 slot, u16 idx) noexcept {
        constexpr T zero{0};
        constexpr T one{1};

        if(!started) {
            elapsedTime += frameTime;
            if(elapsedTime < delay) return false;

            startValue = value;
            started = true;
            elapsedTime = zero;
        }

        elapsedTime += frameTime;

        const T diff = std::abs(value - target);
        const T absMax = Mc::max(std::abs(value), std::abs(target));
        const T threshold = Mc::max(T{1e-4}, absMax * T{1e-7});

        if(diff <= threshold) {
            value = target;
            logIf(s_doLogging, "slot {:d}: removing anim {:d} (epsilon completion), elapsed = {:f}", slot, idx,
                  elapsedTime);
            return true;
        }

        T percent = Mc::clamp(elapsedTime / duration, zero, one);

        logIf(s_doLogging, "slot {:d}: anim {:d}, percent = {:f}", slot, idx, percent);

        if(percent >= one) {
            value = target;
            logIf(s_doLogging, "slot {:d}: removing anim {:d}, elapsed = {:f}", slot, idx, elapsedTime);
            return true;
        }

        percent = ease(animType, percent);
        value = startValue * (one - percent) + target * percent;
        return false;
    }
};

template <AnimatableType T>
struct SlotPool final {
    static constexpr u16 NULL_SLOT = AnimHandleT<T>::NULL_SLOT;
    static constexpr uSz MAX_SLOTS = std::is_same_v<T, f32> ? MAX_SLOTS_F32 : MAX_SLOTS_F64;
    static constexpr uSz MAX_PER_SLOT = 4;

    // per-slot data
    T values[MAX_SLOTS];
    u16 animCount[MAX_SLOTS];   // number of active anims for each slot
    T *ownerValue[MAX_SLOTS];   // back-pointer to handle's m_value
    u16 *ownerSlot[MAX_SLOTS];  // back-pointer to handle's m_slot

    // slot freelist
    u16 freelist[MAX_SLOTS];
    u16 freeCount;
    u16 highWaterMark;

    // per-slot animation storage
    Anim<T> anims[MAX_SLOTS][MAX_PER_SLOT];
    u16 animHigh;    // upper bound for update() iteration
    u32 totalAnims;  // total active animations across all slots

    // --- slot management ---

    u16 allocSlot(T initial) {
        u16 slot{0};
        if(freeCount > 0) {
            slot = freelist[--freeCount];
        } else {
            if(highWaterMark >= MAX_SLOTS) {
                debugLog("ERROR: AnimHandleT slot pool exhausted ({:d} slots)!", MAX_SLOTS);
                return NULL_SLOT;
            }
            slot = highWaterMark++;
        }
        values[slot] = initial;
        animCount[slot] = 0;
        return slot;
    }

    void freeSlot(u16 slot) {
        if(slot == NULL_SLOT) return;
        freelist[freeCount++] = slot;
    }

    // --- animation management ---

    void addAnim(u16 slot, T target, T duration, T delay, bool overrideExisting, Ease type) noexcept {
        if(overrideExisting) deleteAnims(slot);

        if(animCount[slot] >= MAX_PER_SLOT) {
            debugLog("ERROR: per-slot animation limit reached ({:d} per slot)!", MAX_PER_SLOT);
            return;
        }

        anims[slot][animCount[slot]++] = Anim<T>{
            .target = target,
            .duration = duration,
            .startValue = values[slot],
            .delay = delay,
            .elapsedTime = T{0},
            .animType = type,
            .started = (delay == T{0}),
        };
        totalAnims++;
        if(slot >= animHigh) animHigh = slot + 1;
    }

    void deleteAnims(u16 slot) noexcept {
        totalAnims -= animCount[slot];
        animCount[slot] = 0;
    }

    void clearAnims() noexcept {
        for(u16 s = 0; s < animHigh; s++) animCount[s] = 0;
        animHigh = 0;
        totalAnims = 0;
    }

    void update(T frameTime) {
        u16 newAnimHigh = 0;
        for(u16 s = 0; s < animHigh; s++) {
            if(animCount[s] == 0) continue;

            T &curValue = values[s];
            u16 count = animCount[s];

            for(u16 i = 0; i < count;) {
                if(anims[s][i].tick(curValue, frameTime, s, i)) {
                    totalAnims--;
                    anims[s][i] = anims[s][--count];
                } else {
                    ++i;
                }
            }
            animCount[s] = count;

            if(count == 0) {
                // reclaim slot when all animations on it have finished
                *ownerValue[s] = curValue;
                *ownerSlot[s] = NULL_SLOT;
                freeSlot(s);
            } else {
                *ownerValue[s] = curValue;
                newAnimHigh = s + 1;
            }
        }
        animHigh = newAnimHigh;
    }
};

template <AnimatableType T>
inline CONSTINIT SlotPool<T> pool{};

}  // namespace

// --- AnimHandleT implementation ---

template <AnimatableType T>
AnimHandleT<T>::~AnimHandleT() {
    if(m_slot != NULL_SLOT) {
        pool<T>.deleteAnims(m_slot);
        pool<T>.freeSlot(m_slot);
    }
}

template <AnimatableType T>
AnimHandleT<T>::AnimHandleT(AnimHandleT &&o) noexcept : m_value(o.m_value), m_slot(o.m_slot) {
    if(m_slot != NULL_SLOT) {
        pool<T>.ownerValue[m_slot] = &m_value;
        pool<T>.ownerSlot[m_slot] = &m_slot;
    }
    o.m_slot = NULL_SLOT;
}

template <AnimatableType T>
AnimHandleT<T> &AnimHandleT<T>::operator=(AnimHandleT &&o) noexcept {
    if(this != &o) {
        if(m_slot != NULL_SLOT) {
            pool<T>.deleteAnims(m_slot);
            pool<T>.freeSlot(m_slot);
        }
        m_value = o.m_value;
        m_slot = o.m_slot;
        if(m_slot != NULL_SLOT) {
            pool<T>.ownerValue[m_slot] = &m_value;
            pool<T>.ownerSlot[m_slot] = &m_slot;
        }
        o.m_slot = NULL_SLOT;
    }
    return *this;
}

template <AnimatableType T>
AnimHandleT<T> &AnimHandleT<T>::operator=(T value) {
    if(m_slot != NULL_SLOT) {
        pool<T>.values[m_slot] = value;
    }
    m_value = value;
    return *this;
}

template <AnimatableType T>
void AnimHandleT<T>::set(T target, T duration, Ease ease) {
    set(target, duration, ease, T{0});
}

template <AnimatableType T>
void AnimHandleT<T>::set(T target, T duration, Ease ease, T delay) {
    if(m_slot == NULL_SLOT) {
        m_slot = pool<T>.allocSlot(m_value);
        if(m_slot == NULL_SLOT) return;
        pool<T>.ownerValue[m_slot] = &m_value;
        pool<T>.ownerSlot[m_slot] = &m_slot;
    }
    pool<T>.addAnim(m_slot, target, duration, delay, true, ease);
}

template <AnimatableType T>
void AnimHandleT<T>::append(T target, T duration, Ease ease) {
    append(target, duration, ease, T{0});
}

template <AnimatableType T>
void AnimHandleT<T>::append(T target, T duration, Ease ease, T delay) {
    if(m_slot == NULL_SLOT) {
        m_slot = pool<T>.allocSlot(m_value);
        if(m_slot == NULL_SLOT) return;
        pool<T>.ownerValue[m_slot] = &m_value;
        pool<T>.ownerSlot[m_slot] = &m_slot;
    }
    pool<T>.addAnim(m_slot, target, duration, delay, false, ease);
}

template <AnimatableType T>
bool AnimHandleT<T>::animating() const {
    return m_slot != NULL_SLOT && pool<T>.animCount[m_slot] > 0;
}

template <AnimatableType T>
T AnimHandleT<T>::remaining() const {
    if(m_slot == NULL_SLOT) return T{0};

    if(pool<T>.animCount[m_slot] == 0) return T{0};
    auto &a = pool<T>.anims[m_slot][0];
    if(!a.started) return (a.delay - a.elapsedTime) + a.duration;
    return Mc::max(T{0}, a.duration - a.elapsedTime);
}

template <AnimatableType T>
void AnimHandleT<T>::stop() {
    if(m_slot == NULL_SLOT) return;

    m_value = pool<T>.values[m_slot];
    pool<T>.deleteAnims(m_slot);
    pool<T>.freeSlot(m_slot);
    m_slot = NULL_SLOT;
}

// explicit instantiations
template class AnimHandleT<f32>;
template class AnimHandleT<f64>;

AnimVec2::AnimVec2(vec2 initial) : x(initial.x), y(initial.y) {}
AnimVec2::AnimVec2(dvec2 initial) : x(static_cast<f32>(initial.x)), y(static_cast<f32>(initial.y)) {}

AnimVec2 &AnimVec2::operator=(vec2 value) {
    x = value.x;
    y = value.y;
    return *this;
}

AnimVec2::operator vec2() const { return vec2{static_cast<f32>(x), static_cast<f32>(y)}; }

void AnimVec2::stop() {
    x.stop();
    y.stop();
}

AnimVec2D::AnimVec2D(vec2 initial) : x(initial.x), y(initial.y) {}
AnimVec2D::AnimVec2D(dvec2 initial) : x(initial.x), y(initial.y) {}

void AnimVec2D::stop() {
    x.stop();
    y.stop();
}

AnimVec2D &AnimVec2D::operator=(dvec2 value) {
    x = value.x;
    y = value.y;
    return *this;
}

AnimVec2D::operator dvec2() const { return dvec2{static_cast<f64>(x), static_cast<f64>(y)}; }

// --- engine functions ---

void clearAll() {
    pool<f32>.clearAnims();
    pool<f64>.clearAnims();
}

void update(f64 frameTime) {
    pool<f32>.update(static_cast<f32>(frameTime));
    pool<f64>.update(frameTime);

    const uSz totalAnims = pool<f32>.totalAnims + pool<f64>.totalAnims;
    if(totalAnims > 512) {
        debugLog("WARNING: AnimationHandler has {:d} animations!", totalAnims);
    }
}

uSz getNumActiveAnimations() { return pool<f32>.totalAnims + pool<f64>.totalAnims; }

}  // namespace AnimationHandler
