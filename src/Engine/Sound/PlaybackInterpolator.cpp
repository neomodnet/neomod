// Copyright (c) 2025, WH, All rights reserved.
#include "PlaybackInterpolator.h"
#include <algorithm>
#include <cmath>

// Playback interpolator used by McOsu
u32 McOsuInterpolator::update(f64 rawPositionMS, f64 currentTime, f64 playbackSpeed, bool /*isLooped*/,
                              u64 /*lengthMS*/, bool isPlaying) {
    if(!isPlaying) {
        // no interpolation
        this->reset(rawPositionMS, currentTime);
        return (u32)std::round(this->fInterpolatedMusicPos);
    }

    // not reinventing the wheel, the interpolation magic numbers here are (c) peppy
    // TODO: fix snapping at beginning for maps with instant start

    f64 realTimeDelta = currentTime - this->fLastRealTimeForInterpolationDelta;
    this->fLastRealTimeForInterpolationDelta = currentTime;

    const f64 interpolationDelta = realTimeDelta * 1000.0 * playbackSpeed;
    const f64 interpolationDeltaLimit =
        ((currentTime - this->fLastAudioTimeAccurateSet) * 1000.0 < 1500 || playbackSpeed < 1.0f ? 11 : 33);

    f64 newInterpolatedPos = this->fInterpolatedMusicPos + interpolationDelta;
    f64 delta = newInterpolatedPos - rawPositionMS;

    // approach and recalculate delta
    newInterpolatedPos -= delta / 8.0;
    delta = newInterpolatedPos - rawPositionMS;

    if(std::abs(delta) > interpolationDeltaLimit * 2) {
        // we're fucked, snap back to rawPositionMS
        this->fInterpolatedMusicPos = rawPositionMS;
    } else if(delta < -interpolationDeltaLimit) {
        // undershot
        this->fInterpolatedMusicPos += interpolationDelta * 2;
        this->fLastAudioTimeAccurateSet = currentTime;
    } else if(delta < interpolationDeltaLimit) {
        // normal
        this->fInterpolatedMusicPos = newInterpolatedPos;
    } else {
        // overshot
        this->fInterpolatedMusicPos += interpolationDelta / 2;
        this->fLastAudioTimeAccurateSet = currentTime;
    }

    return (u32)std::round(this->fInterpolatedMusicPos);
}

void McOsuInterpolator::reset(f64 rawPositionMS, f64 currentTime) {
    this->fInterpolatedMusicPos = rawPositionMS;
    this->fLastAudioTimeAccurateSet = currentTime;
    this->fLastRealTimeForInterpolationDelta = currentTime;
}

// Playback interpolator used by osu-framework (LLM'd to C++)
u32 TachyonInterpolator::update(f64 rawPositionMS, f64 currentTime, f64 playbackSpeed, bool /*isLooped*/,
                                u64 /*lengthMS*/, bool isPlaying) {
    f64 lastTime = this->fInterpolatedMusicPos;
    // (in ms, like the positions)
    f64 realTimeDelta = (currentTime - this->fLastRealTimeForInterpolationDelta) * 1000.0;
    this->fLastRealTimeForInterpolationDelta = currentTime;

    if(!isPlaying) {
        // While the source isn't playing, we remain in the current interpolation mode unless there's a seek.
        if(rawPositionMS != this->fLastAudioTimeAccurateSet) {
            this->fIsInterpolating = false;
            this->fInterpolatedMusicPos = rawPositionMS;
            this->fLastAudioTimeAccurateSet = rawPositionMS;
        }
        return (u32)std::round(this->fInterpolatedMusicPos);
    }

    if(this->fIsInterpolating) {
        // Apply time increase from interpolation.
        this->fInterpolatedMusicPos += realTimeDelta * playbackSpeed;
        // Then check the post-interpolated time.
        // If we differ from the current time of the source, gradually approach the ground truth.
        this->fInterpolatedMusicPos =
            DampContinuously(this->fInterpolatedMusicPos, rawPositionMS, this->fDriftRecoveryHalfLife, realTimeDelta);
        bool withinAllowableError = std::abs(rawPositionMS - this->fInterpolatedMusicPos) <=
                                    this->fAllowableErrorMilliseconds * std::abs(playbackSpeed);
        if(!withinAllowableError) {
            // if we've exceeded the allowable error, use the source clock's time value.
            this->fIsInterpolating = false;
            this->fInterpolatedMusicPos = rawPositionMS;
            this->fLastAudioTimeAccurateSet = rawPositionMS;
        }
    } else {
        this->fInterpolatedMusicPos = rawPositionMS;
        // Only start interpolating from the next frame.
        if(realTimeDelta != 0) {
            this->fIsInterpolating = true;
        }
        this->fLastAudioTimeAccurateSet = rawPositionMS;
    }

    // seeking backwards should only be allowed if the source is explicitly doing that.
    bool elapsedInOpposingDirection = (realTimeDelta != 0) && ((realTimeDelta > 0) != (playbackSpeed > 0));
    if(!elapsedInOpposingDirection) {
        this->fInterpolatedMusicPos = (playbackSpeed >= 0) ? std::max(lastTime, this->fInterpolatedMusicPos)
                                                           : std::min(lastTime, this->fInterpolatedMusicPos);
    }

    return (u32)std::round(this->fInterpolatedMusicPos);
}

void TachyonInterpolator::reset(f64 rawPositionMS, f64 currentTime) {
    this->fIsInterpolating = false;
    this->fInterpolatedMusicPos = rawPositionMS;
    this->fLastAudioTimeAccurateSet = rawPositionMS;
    this->fLastRealTimeForInterpolationDelta = currentTime;
}

f64 TachyonInterpolator::Lerp(f64 start, f64 final, f64 amount) { return start + (final - start) * amount; }

f64 TachyonInterpolator::Damp(f64 start, f64 final, f64 base, f64 exponent) {
    return Lerp(start, final, 1 - std::pow(std::clamp(base, 0.0, 1.0), exponent));
}

f64 TachyonInterpolator::DampContinuously(f64 current, f64 target, f64 halfTime, f64 elapsedTime) {
    f64 exponent = elapsedTime / halfTime;
    return Damp(current, target, 0.5, exponent);
}
