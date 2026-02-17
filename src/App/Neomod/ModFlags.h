#pragma once

#include "noinclude.h"
#include "types.h"

enum class ModFlags : u64 {
    None = 0,  // NOTE: not "nomod" (unless ModFlags == None, but not ModFlags & None)

    // Green mods
    NoFail = 1ULL << 0,
    Easy = 1ULL << 1,
    Autopilot = 1ULL << 2,
    Relax = 1ULL << 3,

    // Red mods
    Hidden = 1ULL << 4,
    HardRock = 1ULL << 5,
    Flashlight = 1ULL << 6,
    SuddenDeath = 1ULL << 7,
    Perfect = SuddenDeath | (1ULL << 8),
    Nightmare = 1ULL << 9,

    // Special mods
    NoPitchCorrection = 1ULL << 10,  // should rename to Nightcore?
    TouchDevice = 1ULL << 11,
    SpunOut = 1ULL << 12,
    ScoreV2 = 1ULL << 13,
    FPoSu = 1ULL << 14,
    Target = 1ULL << 15,

    // Experimental mods
    AROverrideLock = 1ULL << 16,
    ODOverrideLock = 1ULL << 17,
    Timewarp = 1ULL << 18,
    ARTimewarp = 1ULL << 19,
    Minimize = 1ULL << 20,
    Jigsaw1 = 1ULL << 21,
    Jigsaw2 = 1ULL << 22,
    Wobble1 = 1ULL << 23,
    Wobble2 = 1ULL << 24,
    ARWobble = 1ULL << 25,
    FullAlternate = 1ULL << 26,
    Shirone = 1ULL << 27,
    Mafham = 1ULL << 28,
    HalfWindow = 1ULL << 29,
    HalfWindowAllow300s = 1ULL << 30,
    Ming3012 = 1ULL << 31,
    No100s = 1ULL << 32,
    No50s = 1ULL << 33,
    MirrorHorizontal = 1ULL << 34,
    MirrorVertical = 1ULL << 35,
    FPoSu_Strafing = 1ULL << 36,
    FadingCursor = 1ULL << 37,
    FPS = 1ULL << 38,
    ReverseSliders = 1ULL << 39,
    Millhioref = 1ULL << 40,
    StrictTracking = 1ULL << 41,
    ApproachDifferent = 1ULL << 42,
    Singletap = 1ULL << 43,
    NoKeylock = 1ULL << 44,
    NoPausing = 1ULL << 45,

    // Reserved
    SliderHeadAccuracy = 1ULL << 46,
    SliderTailAccuracy = 1ULL << 47,
    DKS = 1ULL << 48,

    // Non-submittable
    NoHP = 1ULL << 62,
    Autoplay = 1ULL << 63
};
MAKE_FLAG_ENUM(ModFlags);

enum class LegacyFlags : u32 {
    None = 0,

    NoFail = 1U << 0,
    Easy = 1U << 1,
    TouchDevice = 1U << 2,
    Hidden = 1U << 3,
    HardRock = 1U << 4,
    SuddenDeath = 1U << 5,
    DoubleTime = 1U << 6,
    Relax = 1U << 7,
    HalfTime = 1U << 8,
    Nightcore = DoubleTime | (1U << 9),
    Flashlight = 1U << 10,
    Autoplay = 1U << 11,
    SpunOut = 1U << 12,
    Autopilot = 1U << 13,
    Perfect = SuddenDeath | (1U << 14),
    Key4 = 1U << 15,
    Key5 = 1U << 16,
    Key6 = 1U << 17,
    Key7 = 1U << 18,
    Key8 = 1U << 19,
    FadeIn = 1U << 20,
    Random = 1U << 21,
    Cinema = 1U << 22,
    Target = 1U << 23,
    Key9 = 1U << 24,
    KeyCoop = 1U << 25,
    Key1 = 1U << 26,
    Key3 = 1U << 27,
    Key2 = 1U << 28,
    ScoreV2 = 1U << 29,
    Mirror = 1U << 30,

    Nightmare = Cinema,
    FPoSu = Key1,
};

MAKE_FLAG_ENUM(LegacyFlags);
