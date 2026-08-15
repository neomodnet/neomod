// Copyright (c) 2016, PG, All rights reserved.

#include "GameRules.h"

#ifndef BUILD_TOOLS_ONLY
#include "Osu.h"
#include "AbstractBeatmapInterface.h"
#include "ConVar.h"

namespace cv {
extern ConVar approachtime_max;
extern ConVar approachtime_mid;
extern ConVar approachtime_min;
extern ConVar hitobject_fade_in_time;
extern ConVar hitobject_fade_out_time_speed_multiplier_min;
extern ConVar hitobject_fade_out_time;
extern ConVar mod_fps;
extern ConVar mod_millhioref_multiplier;
extern ConVar mod_millhioref;
extern ConVar playfield_border_bottom_percent;
extern ConVar playfield_border_top_percent;
}  // namespace cv

#define APPROACHTIME_MAX cv::approachtime_max.getFloat()
#define APPROACHTIME_MID cv::approachtime_mid.getFloat()
#define APPROACHTIME_MIN cv::approachtime_min.getFloat()
#define HITOBJECT_FADE_IN_TIME cv::hitobject_fade_in_time.getInt()
#define HITOBJECT_FADE_OUT_TIME_SPEED_MULTIPLIER_MIN cv::hitobject_fade_out_time_speed_multiplier_min.getFloat()
#define HITOBJECT_FADE_OUT_TIME cv::hitobject_fade_out_time.getFloat()
#define MOD_FPS cv::mod_fps.getBool()
#define MOD_MILLHIOREF_MULTIPLIER cv::mod_millhioref_multiplier.getFloat()
#define MOD_MILLHIOREF cv::mod_millhioref.getBool()
#define PLAYFIELD_BORDER_BOTTOM_PERCENT cv::playfield_border_bottom_percent.getFloat()
#define PLAYFIELD_BORDER_TOP_PERCENT cv::playfield_border_top_percent.getFloat()

#define OSU_RES likely(osu) ? osu->getVirtScreenSize() : Osu::osuBaseResolution

#define BEATMAP_SPEED(map__) map__->getSpeedMultiplier()
#define BEATMAP_OD(map__) map__->getOD()

#else

#include "OsuConVars/DiffCalcDefaults.h"

#define APPROACHTIME_MAX (float)cv::defaults::approachtime_max
#define APPROACHTIME_MID (float)cv::defaults::approachtime_mid
#define APPROACHTIME_MIN (float)cv::defaults::approachtime_min
#define HITOBJECT_FADE_IN_TIME cv::defaults::hitobject_fade_in_time
#define HITOBJECT_FADE_OUT_TIME_SPEED_MULTIPLIER_MIN cv::defaults::hitobject_fade_out_time_speed_multiplier_min
#define HITOBJECT_FADE_OUT_TIME cv::defaults::hitobject_fade_out_time
#define MOD_FPS cv::defaults::mod_fps
#define MOD_MILLHIOREF_MULTIPLIER cv::defaults::mod_millhioref_multiplier
#define MOD_MILLHIOREF cv::defaults::mod_millhioref
#define PLAYFIELD_BORDER_BOTTOM_PERCENT cv::defaults::playfield_border_bottom_percent
#define PLAYFIELD_BORDER_TOP_PERCENT cv::defaults::playfield_border_top_percent

#define OSU_RES vec2{640.0f, 480.0f}

#define BEATMAP_SPEED(map__) 1.f
#define BEATMAP_OD(map__) 5.f

#endif

namespace GameRules {

float getFadeOutTime(float animationSpeedMultiplier) {
    const float fade_out_time = HITOBJECT_FADE_OUT_TIME;
    const float multiplier_min = HITOBJECT_FADE_OUT_TIME_SPEED_MULTIPLIER_MIN;
    return fade_out_time * (1.0f / std::max(animationSpeedMultiplier, multiplier_min));
}

i32 getFadeInTime() { return (i32)HITOBJECT_FADE_IN_TIME; }

float getMinApproachTime() { return APPROACHTIME_MIN * (MOD_MILLHIOREF ? MOD_MILLHIOREF_MULTIPLIER : 1.0f); }

float getMidApproachTime() { return APPROACHTIME_MID * (MOD_MILLHIOREF ? MOD_MILLHIOREF_MULTIPLIER : 1.0f); }

float getMaxApproachTime() { return APPROACHTIME_MAX * (MOD_MILLHIOREF ? MOD_MILLHIOREF_MULTIPLIER : 1.0f); }

float arToMilliseconds(float AR) {
    return mapDifficultyRange(AR, APPROACHTIME_MIN, APPROACHTIME_MID, APPROACHTIME_MAX);
}

float arWithSpeed(float AR, float speed) {
    float approachTime = arToMilliseconds(AR);
    return mapDifficultyRangeInv(approachTime / speed, APPROACHTIME_MIN, APPROACHTIME_MID, APPROACHTIME_MAX);
}

// raw spins required per second
float getSpinnerSpinsPerSecond(const AbstractBeatmapInterface *beatmap) {
    (void)beatmap;
    return mapDifficultyRange(BEATMAP_OD(beatmap), 3.0f, 5.0f, 7.5f);
}

// spinner length compensated rotations
// respect all mods and overrides
float getSpinnerRotationsForSpeedMultiplier(const AbstractBeatmapInterface *beatmap, i32 spinnerDuration) {
    return getSpinnerRotationsForSpeedMultiplier(beatmap, spinnerDuration, BEATMAP_SPEED(beatmap));
}

vec2 getPlayfieldOffset() {
    const vec2 res = OSU_RES;

    const float osu_screen_width = res.x;
    const float osu_screen_height = res.y;
    const vec2 playfield_size = getPlayfieldSize();
    const float bottom_border_size = PLAYFIELD_BORDER_BOTTOM_PERCENT * osu_screen_height;

    // first person mode doesn't need any offsets, cursor/crosshair should be centered on screen
    const float playfield_y_offset =
        MOD_FPS ? 0.f : (osu_screen_height / 2.0f - (playfield_size.y / 2.0f)) - bottom_border_size;

    return {(osu_screen_width - playfield_size.x) / 2.0f,
            (osu_screen_height - playfield_size.y) / 2.0f + playfield_y_offset};
}

float getPlayfieldScaleFactor() {
    const vec2 res = OSU_RES;

    const float osu_screen_width = res.x;
    const float osu_screen_height = res.y;
    const float top_border_size = PLAYFIELD_BORDER_TOP_PERCENT * osu_screen_height;
    const float bottom_border_size = PLAYFIELD_BORDER_BOTTOM_PERCENT * osu_screen_height;

    const float adjusted_playfield_height = osu_screen_height - bottom_border_size - top_border_size;

    return (osu_screen_width / (float)OSU_COORD_WIDTH) > (adjusted_playfield_height / (float)OSU_COORD_HEIGHT)
               ? (adjusted_playfield_height / (float)OSU_COORD_HEIGHT)
               : (osu_screen_width / (float)OSU_COORD_WIDTH);
}
}  // namespace GameRules
