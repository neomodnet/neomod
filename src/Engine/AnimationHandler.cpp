// Copyright (c) 2012, PG & 2026, WH, All rights reserved.
#include "AnimationHandler.h"

#include "noinclude.h"

#include "ConVar.h"
#include "Logging.h"
#include "Vectors.h"

#include <algorithm>

namespace AnimationHandler {
static bool s_doLogging{false};

void onDebugAnimChange(float newVal) { s_doLogging = !!static_cast<int>(newVal); }

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

    // --- easing math ---

    static forceinline T applyEasing(Ease type, T percent) noexcept {
        constexpr T half{0.5};
        constexpr T one{1};
        constexpr T two{2};

        using enum Ease;
        switch(type) {
            case QuadIn:
                return percent * percent;
            case QuadOut:
                return -percent * (percent - two);
            case QuadInOut:
                if((percent *= two) < one) return half * percent * percent;
                percent -= one;
                return -half * (percent * (percent - two) - one);
            case CubicIn:
                return percent * percent * percent;
            case CubicOut:
                percent -= one;
                return percent * percent * percent + one;
            case QuartIn:
                return percent * percent * percent * percent;
            case QuartOut:
                percent -= one;
                return one - percent * percent * percent * percent;
            default:
                return percent;
        }
    }

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
        const T absMax = std::max(std::abs(value), std::abs(target));
        const T threshold = std::max(T{1e-4}, absMax * T{1e-7});

        if(diff <= threshold) {
            value = target;
            logIf(s_doLogging, "slot {:d}: removing anim {:d} (epsilon completion), elapsed = {:f}", slot, idx,
                  elapsedTime);
            return true;
        }

        T percent = std::clamp(elapsedTime / duration, zero, one);

        logIf(s_doLogging, "slot {:d}: anim {:d}, percent = {:f}", slot, idx, percent);

        if(percent >= one) {
            value = target;
            logIf(s_doLogging, "slot {:d}: removing anim {:d}, elapsed = {:f}", slot, idx, elapsedTime);
            return true;
        }

        percent = applyEasing(animType, percent);
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
    return std::max(T{0}, a.duration - a.elapsedTime);
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
