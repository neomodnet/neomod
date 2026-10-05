// Copyright (c) 2025, WH, All rights reserved.
#include "PlaybackInterpolator.h"
#include <algorithm>
#include <cmath>

#include "ConVar.h"

u64 PlaybackInterpolator::update(f64 rawPositionS, f64 currentTime, f64 playbackSpeed, bool isLooped, u32 lengthMS,
                                 bool isPlaying) {
    if(cv::interpolate_music_pos.getInt() == 0) return static_cast<u64>(std::round(rawPositionS * 1000. * 1000.));

    // reset state if not initialized or not playing
    if(this->dLastPositionTime <= 0.0 || !isPlaying) {
        reset(rawPositionS, currentTime, playbackSpeed);
        return this->iLastInterpolatedPositionUS;
    }

    // time since last update() call (for interpolation)
    const f64 timeDelta = currentTime - this->dLastPositionTime;

    // always advance from last interpolated position
    f64 currentInterpolatedS = static_cast<f64>(this->iLastInterpolatedPositionUS) / (1000.0 * 1000.0);
    f64 interpolatedPositionS = currentInterpolatedS + (timeDelta * this->dEstimatedRate);

    // update rate estimate and apply drift correction when raw position changes
    if(this->dLastRawPosition != rawPositionS) {
        // calculate time between raw position samples
        const f64 rawPositionTimeDelta = currentTime - this->dLastRawPositionChangeTime;

        // only update rate if enough time has passed (5ms minimum)
        if(rawPositionTimeDelta > 0.005) {
            f64 newRate;

            if(rawPositionS >= this->dLastRawPosition) {
                // normal forward movement
                newRate = (rawPositionS - this->dLastRawPosition) / rawPositionTimeDelta;
            } else if(isLooped && lengthMS > 0) {
                // handle loop wraparound
                const f64 lengthS = lengthMS / 1000.;
                const f64 wrappedChange = (lengthS - this->dLastRawPosition) + rawPositionS;
                newRate = wrappedChange / rawPositionTimeDelta;
            } else {
                // backward movement (seeking), keep current rate
                newRate = this->dEstimatedRate;
            }

            // sanity check against expected rate (allow 20% deviation)
            const f64 expectedRate = playbackSpeed;
            if(newRate < expectedRate * 0.8 || newRate > expectedRate * 1.2) {
                newRate = expectedRate * 0.7 + newRate * 0.3;  // blend back toward expected
            }

            // smooth the rate estimate
            this->dEstimatedRate = this->dEstimatedRate * 0.6 + newRate * 0.4;
        }

        // apply drift correction to sync with raw position
        const f64 error = rawPositionS - interpolatedPositionS;
        interpolatedPositionS += error * 0.3;

        this->dLastRawPosition = rawPositionS;
        this->dLastRawPositionChangeTime = currentTime;  // track when raw position changed
    } else {
        // gradual adjustment when raw position hasn't changed for a while
        const f64 timeSinceLastChange = currentTime - this->dLastRawPositionChangeTime;
        if(timeSinceLastChange > 0.1) {
            const f64 expectedRate = playbackSpeed;
            this->dEstimatedRate = this->dEstimatedRate * 0.95 + expectedRate * 0.05;
        }
    }

    // update time basis for next call
    this->dLastPositionTime = currentTime;

    // handle looping
    if(isLooped && lengthMS > 0) {
        const f64 lengthS = lengthMS / 1000.;
        if(interpolatedPositionS >= lengthS) {
            this->iLastInterpolatedPositionUS =
                static_cast<u64>(std::round(std::fmod(interpolatedPositionS, lengthS) * 1000. * 1000.));
            return this->iLastInterpolatedPositionUS;
        }
    }

    this->iLastInterpolatedPositionUS =
        static_cast<u64>(std::round(std::max(0.0, interpolatedPositionS) * 1000. * 1000.));
    return this->iLastInterpolatedPositionUS;
}

// Playback interpolator used by McOsu
u32 McOsuInterpolator::update(f64 rawPositionMS, f64 currentTime, f64 playbackSpeed, bool /*isLooped*/,
                              u64 /*lengthMS*/, bool isPlaying) {
    if(!isPlaying) {
        // no interpolation
        this->fInterpolatedMusicPos = rawPositionMS;
        this->fLastAudioTimeAccurateSet = currentTime;
        this->fLastRealTimeForInterpolationDelta = currentTime;
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

f64 TachyonInterpolator::Lerp(f64 start, f64 final, f64 amount) { return start + (final - start) * amount; }

f64 TachyonInterpolator::Damp(f64 start, f64 final, f64 base, f64 exponent) {
    return Lerp(start, final, 1 - std::pow(std::clamp(base, 0.0, 1.0), exponent));
}

f64 TachyonInterpolator::DampContinuously(f64 current, f64 target, f64 halfTime, f64 elapsedTime) {
    f64 exponent = elapsedTime / halfTime;
    return Damp(current, target, 0.5, exponent);
}
