// shared frozen defaults for the convars the BUILD_TOOLS_ONLY diffcalc build hardcodes.
// the CONVAR() definitions in OsuConVarDefs.h reference these, so the standalone tool and the
// game defaults can never silently drift apart.
// types intentionally mirror the previous CONVAR literal types exactly (int stays int etc.),
// a type change here would change the deduced ConVar type and its serialization.
#pragma once

namespace cv::defaults {

inline constexpr int approachtime_max = 450;
inline constexpr int approachtime_mid = 1200;
inline constexpr int approachtime_min = 1800;
inline constexpr int beatmap_max_num_hitobjects = 100000;
inline constexpr int beatmap_max_num_slider_scoringtimes = 32768;
inline constexpr int hitobject_fade_in_time = 400;
inline constexpr float hitobject_fade_out_time = 0.293f;
inline constexpr float hitobject_fade_out_time_speed_multiplier_min = 0.5f;
inline constexpr bool mod_fps = false;
inline constexpr bool mod_millhioref = false;
inline constexpr float mod_millhioref_multiplier = 2.0f;
inline constexpr float playfield_border_bottom_percent = 0.0834f;
inline constexpr float playfield_border_top_percent = 0.117f;
inline constexpr float slider_curve_max_length = 65536.f / 2.f;
inline constexpr float slider_curve_max_points = 9999.0f;
inline constexpr float slider_curve_points_separation = 2.5f;
inline constexpr int slider_end_inside_check_offset = 36;
inline constexpr int slider_max_repeats = 9000;
inline constexpr int slider_max_ticks = 2048;
inline constexpr bool stars_ignore_clamped_sliders = true;
inline constexpr float stars_slider_curve_points_separation = 20.0f;
inline constexpr bool stars_stacking = true;

}  // namespace cv::defaults
