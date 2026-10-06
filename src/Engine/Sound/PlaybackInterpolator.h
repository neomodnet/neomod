#pragma once
#include "types.h"
#include <cmath>

class GameplayInterpolator {
   public:
    GameplayInterpolator() = default;
    virtual ~GameplayInterpolator() = default;

    GameplayInterpolator(const GameplayInterpolator &) = default;
    GameplayInterpolator &operator=(const GameplayInterpolator &) = default;
    GameplayInterpolator(GameplayInterpolator &&) noexcept = default;
    GameplayInterpolator &operator=(GameplayInterpolator &&) noexcept = default;

    virtual u32 update(f64 rawPositionMS, f64 currentTime, f64 playbackSpeed, bool isLooped = false, u64 lengthMS = 0,
                       bool isPlaying = true) = 0;
    // starts over from this position, for a jump the source made (a seek, a loop, a new voice)
    virtual void reset(f64 rawPositionMS, f64 currentTime) = 0;

    // types correspond to cv::interpolate_music_pos
    [[nodiscard]] virtual inline int getType() const { return -1; }
};

// Playback interpolator used by McOsu
class McOsuInterpolator : public GameplayInterpolator {
   public:
    McOsuInterpolator() = default;
    u32 update(f64 rawPositionMS, f64 currentTime, f64 playbackSpeed, bool isLooped = false, u64 lengthMS = 0,
               bool isPlaying = true) override;
    void reset(f64 rawPositionMS, f64 currentTime) override;

    [[nodiscard]] inline int getType() const override { return 2; }

   private:
    f64 fInterpolatedMusicPos{0.0};
    f64 fLastAudioTimeAccurateSet{0.0};
    f64 fLastRealTimeForInterpolationDelta{0.0};
};

// Playback interpolator used by osu-framework (LLM'd to C++)
class TachyonInterpolator : public GameplayInterpolator {
   public:
    TachyonInterpolator() = default;

    u32 update(f64 rawPositionMS, f64 currentTime, f64 playbackSpeed, bool isLooped = false, u64 lengthMS = 0,
               bool isPlaying = true) override;
    void reset(f64 rawPositionMS, f64 currentTime) override;

    [[nodiscard]] inline int getType() const override { return 3; }

   private:
    f64 Lerp(f64 start, f64 final, f64 amount);
    f64 Damp(f64 start, f64 final, f64 base, f64 exponent);
    f64 DampContinuously(f64 current, f64 target, f64 halfTime, f64 elapsedTime);

    f64 fAllowableErrorMilliseconds{1000.0 / 60 * 2};
    f64 fDriftRecoveryHalfLife{50.0};
    bool fIsInterpolating{false};
    f64 fInterpolatedMusicPos{0.0};
    f64 fLastAudioTimeAccurateSet{0.0};
    f64 fLastRealTimeForInterpolationDelta{0.0};
};
