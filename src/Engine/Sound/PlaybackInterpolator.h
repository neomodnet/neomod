#pragma once
#include "types.h"
#include <cmath>

/**
 * PlaybackInterpolator provides smooth interpolated position tracking for audio playback.
 *
 * This class implements rate-based interpolation to smooth out position reporting
 * from audio backends that may have irregular or jittery position updates. It
 * estimates the actual playback rate based on position changes and interpolates
 * between updates to provide smooth, predictable position values.
 */
class PlaybackInterpolator {
   public:
    PlaybackInterpolator() = default;

    /**
     * Update with current raw position and get interpolated position.
     * Call this regularly (every frame) with the raw position from your audio backend.
     *
     * @param rawPositionS Raw position from audio backend in seconds
     * @param currentTime Current engine time in seconds
     * @param playbackSpeed Current playback speed multiplier (1.0 = normal)
     * @param isLooped Whether the audio is looped
     * @param lengthMS Total length of audio in milliseconds (required for loop handling)
     * @param isPlaying Whether the audio is currently playing
     * @return Interpolated position in microseconds
     */
    u64 update(f64 rawPositionS, f64 currentTime, f64 playbackSpeed, bool isLooped = false, u32 lengthMS = 0,
               bool isPlaying = true);

    /**
     * Reset interpolation state.
     * Call this when seeking or starting playback to reset the interpolation.
     *
     * @param rawPositionUS Current raw position in microseconds
     * @param currentTime Current engine time in seconds
     * @param playbackSpeed Current playback speed multiplier
     */
    inline void reset(f64 rawPositionS, f64 currentTime, f64 playbackSpeed) {
        this->dLastRawPosition = rawPositionS;
        this->dLastRawPositionChangeTime = currentTime;
        this->dLastPositionTime = currentTime;
        this->dEstimatedRate = playbackSpeed;
        this->iLastInterpolatedPositionUS = static_cast<u64>(std::round(rawPositionS * 1000. * 1000.));
    }

    /**
     * Get the last calculated interpolated position (microseconds) without updating.
     * @return Last interpolated position in microseconds
     */
    [[nodiscard]] u64 getLastInterpolatedPositionUS() const { return this->iLastInterpolatedPositionUS; }

    /**
     * Get the last calculated interpolated position (seconds) without updating.
     * @return Last interpolated position in seconds
     */
    [[nodiscard]] f64 getLastInterpolatedPositionS() const {
        return static_cast<f64>(this->iLastInterpolatedPositionUS) / (1000. * 1000.);
    }

   private:
    f64 dLastRawPosition{0.0};            // last raw position in seconds
    f64 dLastPositionTime{0.0};           // engine time when last position was obtained
    f64 dEstimatedRate{1.};               // estimated playback rate (seconds per second)
    f64 dLastRawPositionChangeTime{0.0};  // track when raw position last changed
    u64 iLastInterpolatedPositionUS{0};   // last calculated interpolated position (seconds)
};

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

    // types correspond to cv::interpolate_music_pos
    [[nodiscard]] virtual inline int getType() const { return -1; }
};

// Playback interpolator used by McOsu
class McOsuInterpolator : public GameplayInterpolator {
   public:
    McOsuInterpolator() = default;
    u32 update(f64 rawPositionMS, f64 currentTime, f64 playbackSpeed, bool isLooped = false, u64 lengthMS = 0,
               bool isPlaying = true) override;

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
