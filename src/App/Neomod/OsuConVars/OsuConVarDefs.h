#ifndef OSU_CONVARDEFS_H
#define OSU_CONVARDEFS_H

// put osu-related convars in this file (NOT ConVarDefs.h)

// ########################################################################################################################
// # this first part is just to allow for proper code completion when editing this file
// ########################################################################################################################

// NOLINTBEGIN(misc-definitions-in-headers)

#if !defined(OSU_CONVARS_H) && \
    (defined(_CLANGD) || defined(Q_CREATOR_RUN) || defined(__INTELLISENSE__) || defined(__CDT_PARSER__))
#define DEFINE_OSU_CONVARS
#include "OsuConVars.h"

#endif

// ########################################################################################################################
// # actual declarations/definitions below
// ########################################################################################################################

#define _CV(name) name
// helper to create an SA delegate from a freestanding/static function
#define CFUNC(func) SA::delegate<decltype(func)>::template create<func>()

// defined and included at the end of ConVar.cpp
#if defined(DEFINE_OSU_CONVARS)
#undef CONVAR
#undef KEYVAR
#define CONVAR(name, ...) ConVar _CV(name)(#name __VA_OPT__(, ) __VA_ARGS__)
#define KEYVAR(name, ...) ConVar _CV(name)(__VA_ARGS__)

#include "BaseEnvironment.h"
#include "KeyBindings.h"
#include "BanchoNetworking.h"  // defines some things we need like OSU_VERSION_DATEONLY
#include "OsuConfig.h"
namespace SliderRenderer {
extern void onUniformConfigChanged();
}
namespace Spectating {
extern void start_by_username(std::string_view username);
}

#else
#define CONVAR(name, ...) extern ConVar _CV(name)
#define KEYVAR(name, ...) extern ConVar _CV(name)
#endif

class ConVar;
namespace cv {
namespace cmd {

// Server-callable commands
CONVAR(spectate, CLIENT | SERVER, CFUNC(Spectating::start_by_username));

}  // namespace cmd

// Audio
CONVAR(loudness_calc_threads, 0.f, CLIENT, "0 = autodetect. do not use too many threads or your PC will explode");
CONVAR(loudness_fallback, -12.f, CLIENT);
CONVAR(loudness_target, -14.f, CLIENT);
CONVAR(sound_panning, true, CLIENT | SKINS | SERVER, "positional hitsound audio depending on the playfield position");
CONVAR(sound_panning_multiplier, 1.0f, CLIENT | SKINS | SERVER,
       "the final panning value is multiplied with this, e.g. if you want to reduce or "
       "increase the effect strength by a percentage");
CONVAR(snd_boost_hitsound_volume, false, CLIENT | SKINS | SERVER, "slightly increase non-sliderslide hitsound volume");

// Audio (mods)
CONVAR(snd_pitch_hitsounds, false, CLIENT | SKINS | SERVER, "change hitsound pitch based on accuracy");
CONVAR(snd_pitch_hitsounds_factor, -0.5f, CLIENT | SKINS | SERVER, "how much to change the pitch");

// Debug
CONVAR(debug_osu, false, CLIENT);
CONVAR(debug_db, false, CLIENT);
CONVAR(debug_async_db, false, CLIENT);
CONVAR(debug_draw_timingpoints, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(debug_draw_gameplay_clicks, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(debug_hiterrorbar_misaims, false, CLIENT);
CONVAR(debug_pp, false, CLIENT);
CONVAR(debug_bg_loader, false, CLIENT);
CONVAR(debug_thumbs, false, CLIENT);
CONVAR(slider_debug_draw, false, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "draw hitcircle at every curve point and nothing else (no vao, no rt, no shader, nothing) "
       "(requires enabling legacy slider renderer)");
CONVAR(slider_debug_draw_square_vao, false, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "generate square vaos and nothing else (no rt, no shader) (requires disabling legacy slider renderer)");
CONVAR(slider_debug_wireframe, false, CLIENT | SERVER | PROTECTED | GAMEPLAY, "unused");

// Keybinds
KEYVAR(BOSS_KEY, "key_boss", (int)KEY_INSERT, CLIENT);
KEYVAR(DECREASE_LOCAL_OFFSET, "key_decrease_local_offset", (int)KEY_SUBTRACT, CLIENT);
KEYVAR(DECREASE_VOLUME, "key_decrease_volume", (int)KEY_DOWN, CLIENT);
KEYVAR(DISABLE_MOUSE_BUTTONS, "key_disable_mouse_buttons", (int)KEY_F10, CLIENT);
KEYVAR(FPOSU_ZOOM, "key_fposu_zoom", 0, CLIENT);
KEYVAR(GAME_PAUSE, "key_game_pause", (int)KEY_ESCAPE, CLIENT);
KEYVAR(INCREASE_LOCAL_OFFSET, "key_increase_local_offset", (int)KEY_ADD, CLIENT);
KEYVAR(INCREASE_VOLUME, "key_increase_volume", (int)KEY_UP, CLIENT);
KEYVAR(INSTANT_REPLAY, "key_instant_replay", (int)KEY_F2, CLIENT);
KEYVAR(LEFT_CLICK, "key_left_click", (int)KEY_Z, CLIENT);
KEYVAR(LEFT_CLICK_2, "key_left_click_2", 0, CLIENT);
KEYVAR(MOD_AUTO, "key_mod_auto", (int)KEY_V, CLIENT);
KEYVAR(MOD_AUTOPILOT, "key_mod_autopilot", (int)KEY_X, CLIENT);
KEYVAR(MOD_DOUBLETIME, "key_mod_doubletime", (int)KEY_D, CLIENT);
KEYVAR(MOD_EASY, "key_mod_easy", (int)KEY_Q, CLIENT);
KEYVAR(MOD_FLASHLIGHT, "key_mod_flashlight", (int)KEY_G, CLIENT);
KEYVAR(MOD_HALFTIME, "key_mod_halftime", (int)KEY_E, CLIENT);
KEYVAR(MOD_HARDROCK, "key_mod_hardrock", (int)KEY_A, CLIENT);
KEYVAR(MOD_HIDDEN, "key_mod_hidden", (int)KEY_F, CLIENT);
KEYVAR(MOD_NOFAIL, "key_mod_nofail", (int)KEY_W, CLIENT);
KEYVAR(MOD_RELAX, "key_mod_relax", (int)KEY_Z, CLIENT);
KEYVAR(MOD_SCOREV2, "key_mod_scorev2", (int)KEY_B, CLIENT);
KEYVAR(MOD_SPUNOUT, "key_mod_spunout", (int)KEY_C, CLIENT);
KEYVAR(MOD_SUDDENDEATH, "key_mod_suddendeath", (int)KEY_S, CLIENT);
KEYVAR(OPEN_SKIN_SELECT_MENU, "key_open_skin_select_menu", 0, CLIENT);
KEYVAR(QUICK_LOAD, "key_quick_load", (int)KEY_F7, CLIENT);
KEYVAR(QUICK_RETRY, "key_quick_retry", (int)KEY_BACKSPACE, CLIENT);
KEYVAR(QUICK_SAVE, "key_quick_save", (int)KEY_F6, CLIENT);
KEYVAR(RANDOM_BEATMAP, "key_random_beatmap", (int)KEY_F2, CLIENT);
KEYVAR(RIGHT_CLICK, "key_right_click", (int)KEY_X, CLIENT);
KEYVAR(RIGHT_CLICK_2, "key_right_click_2", 0, CLIENT);
KEYVAR(SAVE_SCREENSHOT, "key_save_screenshot", (int)KEY_F12, CLIENT);
KEYVAR(SEEK_TIME, "key_seek_time", (int)KEY_RSHIFT, CLIENT);
KEYVAR(SEEK_TIME_BACKWARD, "key_seek_time_backward", (int)KEY_LEFT, CLIENT);
KEYVAR(SEEK_TIME_FORWARD, "key_seek_time_forward", (int)KEY_RIGHT, CLIENT);
KEYVAR(SMOKE, "key_smoke", 0, CLIENT);
KEYVAR(SKIP_CUTSCENE, "key_skip_cutscene", (int)KEY_SPACE, CLIENT);
KEYVAR(TOGGLE_CHAT, "key_toggle_chat", (int)KEY_F8, CLIENT);
KEYVAR(TOGGLE_EXTENDED_CHAT, "key_toggle_extended_chat", (int)KEY_F9, CLIENT);
KEYVAR(TOGGLE_MAP_BACKGROUND, "key_toggle_map_background", 0, CLIENT);
KEYVAR(TOGGLE_MODSELECT, "key_toggle_modselect", (int)KEY_F1, CLIENT);
KEYVAR(TOGGLE_SCOREBOARD, "key_toggle_scoreboard", (int)KEY_TAB, CLIENT);

// Input behavior
CONVAR(win_global_media_hotkeys, true, CLIENT,
       "Watch for play/pause/next/previous media keys globally for main menu music (Windows only)");
CONVAR(alt_f4_quits_even_while_playing, true, CLIENT);
CONVAR(auto_and_relax_block_user_input, true, CLIENT);
CONVAR(mod_suddendeath_restart, false, CLIENT,
       "osu! has this set to false (i.e. you fail after missing). if set to true, then "
       "behave like SS/PF, instantly restarting the map");
CONVAR(hud_shift_tab_toggles_everything, true, CLIENT);
CONVAR(win_disable_windows_key_while_playing, true, CLIENT);

// Files
CONVAR(database_enabled, true, CLIENT);
CONVAR(database_ignore_version, true, CLIENT, "ignore upper version limit and force load the db file (may crash)");
CONVAR(database_version, OSU_VERSION_DATEONLY, CLIENT | NOLOAD | NOSAVE,
       "maximum supported osu!.db version, above this will use fallback loader");
CONVAR(osu_folder, ""sv, CLIENT);
CONVAR(osu_folder_sub_skins, "Skins/"sv, CLIENT);
CONVAR(songs_folder, "Songs/"sv, CLIENT);
CONVAR(export_folder, NEOMOD_DATA_DIR "exports/"sv, CLIENT, "path to export files to (like beatmaps, skins)");
CONVAR(maps_save_immediately, (Env::cfg(OS::WASM) ? true : false), CLIENT,
       "write " PACKAGE_NAME "_maps.db as soon as a new beatmap is added (will NOT save on override/sr calc changes)");

// Looks
CONVAR(always_render_cursor_trail, true, CLIENT | SKINS,
       "always render the cursor trail, even when not moving the cursor");
CONVAR(automatic_cursor_size, false, CLIENT | SKINS);
CONVAR(mod_hd_circle_fadeout_end_percent, 0.3f, CLIENT | SKINS | SERVER | GAMEPLAY,
       "hiddenFadeOutEndTime = circleTime - approachTime * mod_hd_circle_fadeout_end_percent");
CONVAR(mod_hd_circle_fadeout_start_percent, 0.6f, CLIENT | SKINS | SERVER | GAMEPLAY,
       "hiddenFadeOutStartTime = circleTime - approachTime * mod_hd_circle_fadeout_start_percent");
CONVAR(mod_hd_slider_fade_percent, 1.0f, CLIENT | SKINS | SERVER | GAMEPLAY);
CONVAR(mod_hd_slider_fast_fade, false, CLIENT | SKINS | SERVER | GAMEPLAY);

// Song browser
CONVAR(draw_songbrowser_background_image, true, CLIENT | SKINS | SERVER);
CONVAR(draw_songbrowser_menu_background_image, true, CLIENT | SKINS | SERVER);
CONVAR(draw_songbrowser_strain_graph, false, CLIENT | SKINS | SERVER);
CONVAR(draw_songbrowser_thumbnails, true, CLIENT | SKINS | SERVER);
CONVAR(songbrowser_button_anim_x_push, false, CLIENT | SKINS,
       "whether to push songbuttons to the right depending on the vertical scrolling velocity (set this to 0 for "
       "osu!lazer style carousel)");
CONVAR(songbrowser_button_anim_y_curve, true, CLIENT | SKINS,
       "whether to move songbuttons slightly to the right depending on their vertical position, on a vertically "
       "centered curve");
CONVAR(songbrowser_background_fade_in_duration, 0.1f, CLIENT | SKINS);
CONVAR(songbrowser_button_active_color_a, 230, CLIENT | SKINS);
CONVAR(songbrowser_button_active_color_b, 255, CLIENT | SKINS);
CONVAR(songbrowser_button_active_color_g, 255, CLIENT | SKINS);
CONVAR(songbrowser_button_active_color_r, 255, CLIENT | SKINS);
CONVAR(songbrowser_button_collection_active_color_a, 255, CLIENT | SKINS);
CONVAR(songbrowser_button_collection_active_color_b, 44, CLIENT | SKINS);
CONVAR(songbrowser_button_collection_active_color_g, 240, CLIENT | SKINS);
CONVAR(songbrowser_button_collection_active_color_r, 163, CLIENT | SKINS);
CONVAR(songbrowser_button_collection_inactive_color_a, 255, CLIENT | SKINS);
CONVAR(songbrowser_button_collection_inactive_color_b, 143, CLIENT | SKINS);
CONVAR(songbrowser_button_collection_inactive_color_g, 50, CLIENT | SKINS);
CONVAR(songbrowser_button_collection_inactive_color_r, 35, CLIENT | SKINS);
CONVAR(songbrowser_button_difficulty_inactive_color_a, 255, CLIENT | SKINS);
CONVAR(songbrowser_button_difficulty_inactive_color_b, 236, CLIENT | SKINS);
CONVAR(songbrowser_button_difficulty_inactive_color_g, 150, CLIENT | SKINS);
CONVAR(songbrowser_button_difficulty_inactive_color_r, 0, CLIENT | SKINS);
CONVAR(songbrowser_button_inactive_color_a, 240, CLIENT | SKINS);
CONVAR(songbrowser_button_inactive_color_b, 153, CLIENT | SKINS);
CONVAR(songbrowser_button_inactive_color_g, 73, CLIENT | SKINS);
CONVAR(songbrowser_button_inactive_color_r, 235, CLIENT | SKINS);
CONVAR(songbrowser_thumbnail_delay, 0.1f, CLIENT | SKINS);
CONVAR(songbrowser_thumbnail_fade_in_duration, 0.1f, CLIENT | SKINS);

// Song browser (client only)
CONVAR(prefer_cjk, false, CLIENT, "prefer metadata in original language");
CONVAR(songbrowser_search_delay, 0.2f, CLIENT, "delay until search update when entering text");
CONVAR(songbrowser_search_hardcoded_filter, ""sv, CLIENT,
       "allows forcing the specified search filter to be active all the time");

// Song browser (maybe useful to servers)
CONVAR(songbrowser_scorebrowser_enabled, true, CLIENT | SKINS | SERVER);
CONVAR(songbrowser_scores_filteringtype, "Local"sv, CLIENT | SKINS | SERVER);
CONVAR(songbrowser_scores_filteringtype_manual, "unset"sv, CLIENT | SKINS | SERVER);
CONVAR(songbrowser_scores_sortingtype, "By pp"sv, CLIENT | SKINS | SERVER);
CONVAR(songbrowser_sortingtype, "By Date Added"sv, CLIENT | SKINS | SERVER);

// Song browser (the online kind)
CONVAR(direct_ranking_status_filter, 0, CLIENT | SKINS | SERVER);

// Playfield
CONVAR(background_alpha, 1.0f, CLIENT | SKINS | SERVER,
       "transparency of all background layers at once, only useful for FPoSu");
CONVAR(background_brightness, 0.0f, CLIENT | SKINS | SERVER,
       "0 to 1, if this is larger than 0 then it will replace the entire beatmap background "
       "image with a solid color (see background_color_r/g/b)");
CONVAR(background_color_b, 255.0f, CLIENT | SKINS | SERVER,
       "0 to 255, only relevant if background_brightness is larger than 0");
CONVAR(background_color_g, 255.0f, CLIENT | SKINS | SERVER,
       "0 to 255, only relevant if background_brightness is larger than 0");
CONVAR(background_color_r, 255.0f, CLIENT | SKINS | SERVER,
       "0 to 255, only relevant if background_brightness is larger than 0");
CONVAR(background_dim, 0.9f, CLIENT | SKINS | SERVER);
CONVAR(background_dont_fade_during_breaks, false, CLIENT | SKINS | SERVER);
CONVAR(background_fade_after_load, true, CLIENT | SKINS | SERVER);
CONVAR(background_fade_in_duration, 0.85f, CLIENT | SKINS | SERVER);
CONVAR(background_fade_min_duration, 1.4f, CLIENT | SKINS | SERVER,
       "Only fade if the break is longer than this (in seconds)");
CONVAR(background_fade_out_duration, 0.25f, CLIENT | SKINS | SERVER);
CONVAR(draw_accuracy, true, CLIENT | SKINS | SERVER);
CONVAR(draw_approach_circles, true, CLIENT | SKINS | SERVER);
CONVAR(draw_beatmap_background_image, true, CLIENT | SKINS | SERVER);
CONVAR(draw_circles, true, CLIENT | SKINS | SERVER);
CONVAR(draw_combo, true, CLIENT | SKINS | SERVER);
CONVAR(draw_continue, true, CLIENT | SKINS | SERVER);
CONVAR(draw_cursor_ripples, false, CLIENT | SKINS | SERVER);
CONVAR(draw_cursor_trail, true, CLIENT | SKINS | SERVER);
CONVAR(draw_followpoints, true, CLIENT | SKINS | SERVER);
CONVAR(draw_fps, true, CLIENT | SKINS | SERVER);
CONVAR(draw_hiterrorbar, true, CLIENT | SKINS | SERVER);
CONVAR(draw_hiterrorbar_bottom, true, CLIENT | SKINS | SERVER);
CONVAR(draw_hiterrorbar_left, false, CLIENT | SKINS | SERVER);
CONVAR(draw_hiterrorbar_right, false, CLIENT | SKINS | SERVER);
CONVAR(draw_hiterrorbar_top, false, CLIENT | SKINS | SERVER);
CONVAR(draw_hiterrorbar_ur, true, CLIENT | SKINS | SERVER);
CONVAR(draw_hitobjects, true, CLIENT | SKINS | SERVER);
CONVAR(draw_hud, true, CLIENT | SKINS | SERVER);
CONVAR(draw_inputoverlay, true, CLIENT | SKINS | SERVER);
CONVAR(draw_numbers, true, CLIENT | SKINS | SERVER);
CONVAR(draw_playfield_border, true, CLIENT | SKINS | SERVER);
CONVAR(draw_progressbar, true, CLIENT | SKINS | SERVER);
CONVAR(draw_rankingscreen_background_image, true, CLIENT | SKINS | SERVER);
CONVAR(draw_score, true, CLIENT | SKINS | SERVER);
CONVAR(draw_scorebar, true, CLIENT | SKINS | SERVER);
CONVAR(draw_scorebarbg, true, CLIENT | SKINS | SERVER);
CONVAR(draw_scoreboard, true, CLIENT | SKINS | SERVER);
CONVAR(draw_scoreboard_mp, true, CLIENT | SKINS | SERVER);
CONVAR(draw_scrubbing_timeline, true, CLIENT | SKINS | SERVER);
CONVAR(draw_scrubbing_timeline_breaks, true, CLIENT | SKINS | SERVER);
CONVAR(draw_scrubbing_timeline_strain_graph, false, CLIENT | SKINS | SERVER);
CONVAR(draw_smoke, true, CLIENT | SKINS | SERVER);
CONVAR(draw_spectator_background_image, true, CLIENT | SKINS | SERVER);
CONVAR(draw_spectator_list, true, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_ar, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_bpm, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_cs, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_hitdelta, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_hitwindow300, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_hp, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_livestars, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_maxpossiblecombo, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_misses, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_nd, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_nps, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_od, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_perfectpp, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_pp, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_sliderbreaks, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_totalstars, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_ur, false, CLIENT | SKINS | SERVER);
CONVAR(draw_statistics_audio_offset, false, CLIENT);  // DEBUG
CONVAR(draw_target_heatmap, true, CLIENT | SKINS | SERVER);
CONVAR(hud_accuracy_scale, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_combo_scale, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_fps_smoothing, true, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_alpha, 1.0f, CLIENT | SKINS | SERVER, "opacity multiplier for entire hiterrorbar");
CONVAR(hud_hiterrorbar_bar_alpha, 1.0f, CLIENT | SKINS | SERVER, "opacity multiplier for background color bar");
CONVAR(hud_hiterrorbar_bar_height_scale, 3.4f, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_bar_width_scale, 0.6f, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_centerline_alpha, 1.0f, CLIENT | SKINS | SERVER, "opacity multiplier for center line");
CONVAR(hud_hiterrorbar_centerline_b, 255, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_centerline_g, 255, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_centerline_r, 255, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_entry_100_b, 19, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_entry_100_g, 227, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_entry_100_r, 87, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_entry_300_b, 231, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_entry_300_g, 188, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_entry_300_r, 50, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_entry_50_b, 70, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_entry_50_g, 174, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_entry_50_r, 218, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_entry_additive, true, CLIENT | SKINS | SERVER,
       "whether to use additive blending for all hit error entries/lines");
CONVAR(hud_hiterrorbar_entry_alpha, 0.75f, CLIENT | SKINS | SERVER,
       "opacity multiplier for all hit error entries/lines");
CONVAR(hud_hiterrorbar_entry_hit_fade_time, 6.0f, CLIENT | SKINS | SERVER,
       "fade duration of 50/100/300 hit entries/lines in seconds");
CONVAR(hud_hiterrorbar_entry_miss_b, 0, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_entry_miss_fade_time, 4.0f, CLIENT | SKINS | SERVER,
       "fade duration of miss entries/lines in seconds");
CONVAR(hud_hiterrorbar_entry_miss_g, 0, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_entry_miss_r, 205, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_height_percent, 0.007f, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_hide_during_spinner, true, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_max_entries, 32, CLIENT | SKINS | SERVER, "maximum number of entries/lines");
CONVAR(hud_hiterrorbar_offset_bottom_percent, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_offset_left_percent, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_offset_percent, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_offset_right_percent, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_offset_top_percent, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_scale, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_showmisswindow, false, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_ur_alpha, 0.5f, CLIENT | SKINS | SERVER,
       "opacity multiplier for unstable rate text above hiterrorbar");
CONVAR(hud_hiterrorbar_ur_offset_x_percent, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_ur_offset_y_percent, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_ur_scale, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_width_percent, 0.15f, CLIENT | SKINS | SERVER);
CONVAR(hud_hiterrorbar_width_percent_with_misswindow, 0.4f, CLIENT | SKINS | SERVER);
CONVAR(hud_inputoverlay_anim_color_duration, 0.1f, CLIENT | SKINS | SERVER);
CONVAR(hud_inputoverlay_anim_scale_duration, 0.16f, CLIENT | SKINS | SERVER);
CONVAR(hud_inputoverlay_anim_scale_multiplier, 0.8f, CLIENT | SKINS | SERVER);
CONVAR(hud_inputoverlay_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_inputoverlay_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_inputoverlay_scale, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_playfield_border_size, 5.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_progressbar_scale, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_scale, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_score_scale, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_scorebar_hide_anim_duration, 0.5f, CLIENT | SKINS | SERVER);
CONVAR(hud_scorebar_hide_during_breaks, true, CLIENT | SKINS | SERVER);
CONVAR(hud_scorebar_scale, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_scoreboard_offset_y_percent, 0.11f, CLIENT | SKINS | SERVER);
CONVAR(hud_scoreboard_scale, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_scoreboard_use_menubuttonbackground, true, CLIENT | SKINS | SERVER);
CONVAR(hud_scrubbing_timeline_hover_tooltip_offset_multiplier, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_scrubbing_timeline_strains_aim_color_b, 0, CLIENT | SKINS | SERVER);
CONVAR(hud_scrubbing_timeline_strains_aim_color_g, 255, CLIENT | SKINS | SERVER);
CONVAR(hud_scrubbing_timeline_strains_aim_color_r, 0, CLIENT | SKINS | SERVER);
CONVAR(hud_scrubbing_timeline_strains_alpha, 0.4f, CLIENT | SKINS | SERVER);
CONVAR(hud_scrubbing_timeline_strains_height, 200.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_scrubbing_timeline_strains_speed_color_b, 0, CLIENT | SKINS | SERVER);
CONVAR(hud_scrubbing_timeline_strains_speed_color_g, 0, CLIENT | SKINS | SERVER);
CONVAR(hud_scrubbing_timeline_strains_speed_color_r, 255, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_ar_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_ar_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_bpm_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_bpm_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_cs_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_cs_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_hitdelta_chunksize, 30, CLIENT | SKINS | SERVER,
       "how many recent hit deltas to average (-1 = all)");
CONVAR(hud_statistics_hitdelta_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_hitdelta_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_hitwindow300_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_hitwindow300_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_hp_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_hp_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_livestars_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_livestars_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_maxpossiblecombo_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_maxpossiblecombo_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_misses_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_misses_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_nd_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_nd_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_nps_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_nps_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_od_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_od_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_offset_x, 5.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_offset_y, 50.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_perfectpp_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_perfectpp_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_pp_decimal_places, 0, CLIENT | SKINS | SERVER,
       "number of decimal places for the live pp counter (min = 0, max = 2)");
CONVAR(hud_statistics_pp_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_pp_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_scale, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_sliderbreaks_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_sliderbreaks_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_spacing_scale, 1.1f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_totalstars_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_totalstars_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_ur_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_statistics_ur_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_volume_duration, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(hud_volume_size_multiplier, 1.5f, CLIENT | SKINS | SERVER);
CONVAR(playfield_border_bottom_percent, 0.0834f, CLIENT | SERVER | GAMEPLAY);
CONVAR(playfield_border_top_percent, 0.117f, CLIENT | SERVER | GAMEPLAY);
CONVAR(playfield_mirror_horizontal, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(playfield_mirror_vertical, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(playfield_rotation, 0.f, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "rotates the entire playfield by this many degrees");
CONVAR(smoke_scale, 1.f, CLIENT | SKINS | SERVER);
CONVAR(smoke_trail_duration, 10.f, CLIENT | SKINS | SERVER,
       "how long smoke trails should last before being completely gone, in seconds");
CONVAR(smoke_trail_max_size, 2048, CLIENT | SKINS | SERVER,
       "maximum number of rendered smoke trail images, array size limit");
CONVAR(smoke_trail_opaque_duration, 7.f, CLIENT | SKINS | SERVER,
       "how long smoke trails should last before starting to fade out, in seconds");
CONVAR(smoke_trail_spacing, 5.f, CLIENT | SKINS | SERVER,
       "how big the gap between smoke particles should be, in milliseconds");

// Hitobjects
CONVAR(approach_circle_alpha_multiplier, 0.9f, CLIENT | SKINS | SERVER | GAMEPLAY);
CONVAR(approach_scale_multiplier, 3.0f, CLIENT | SKINS | SERVER | GAMEPLAY);

// Vanilla mods
CONVAR(mod_hidden, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_autoplay, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_autopilot, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_relax, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_spunout, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_target, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_scorev2, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_flashlight, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_doubletime, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_nofail, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_hardrock, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_easy, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_suddendeath, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_perfect, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_touchdevice, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_touchdevice_always, false, CLIENT | SERVER | GAMEPLAY, "always enable touchdevice mod");
// speed_override: Even though it isn't PROTECTED, only (0.75, 1.0, 1.5) are allowed on bancho servers.
CONVAR(speed_override, -1.0f, CLIENT | SERVER | GAMEPLAY);
// mod_*time_dummy: These don't affect gameplay, but edit speed_override.
CONVAR(mod_doubletime_dummy, false, CLIENT | SKINS | SERVER);
CONVAR(mod_halftime_dummy, false, CLIENT | SKINS | SERVER);

// Non-vanilla mods
CONVAR(ar_override, -1.0f, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "use this to override between AR 0 and AR 12.5+. active if value is more than or equal to 0.");
CONVAR(ar_override_lock, false, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "always force constant approach time even through speed changes");
CONVAR(ar_overridenegative, 0.0f, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "use this to override below AR 0. active if value is less than 0, disabled otherwise. "
       "this override always overrides the other override.");
CONVAR(autopilot_lenience, 0.75f, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(autopilot_snapping_strength, 2.0f, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "How many iterations of quadratic interpolation to use, more = snappier, 0 = linear");
CONVAR(cs_override, -1.0f, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "use this to override between CS 0 and CS 12.1429. active if value is more than or equal to 0.");
CONVAR(cs_overridenegative, 0.0f, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "use this to override below CS 0. active if value is less than 0, disabled otherwise. "
       "this override always overrides the other override.");
CONVAR(hp_override, -1.0f, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_actual_flashlight, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_approach_different, false, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "replicates osu!lazer's \"Approach Different\" mod");
CONVAR(mod_approach_different_initial_size, 4.0f, CLIENT | SERVER | GAMEPLAY,
       "initial size of the approach circles, relative to hit circles (as a multiplier)");
CONVAR(mod_approach_different_style, 1, CLIENT | SERVER | GAMEPLAY,
       "0 = linear, 1 = gravity, 2 = InOut1, 3 = InOut2, 4 = Accelerate1, 5 = Accelerate2, 6 = Accelerate3, 7 = "
       "Decelerate1, 8 = Decelerate2, 9 = Decelerate3");
CONVAR(mod_artimewarp, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_artimewarp_multiplier, 0.5f, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_arwobble, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_arwobble_interval, 7.0f, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_arwobble_strength, 1.0f, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_endless, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_fadingcursor, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_fadingcursor_combo, 50.0f, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_fposu, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_fposu_sound_panning, false, CLIENT, "see sound_panning");
CONVAR(mod_fps, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_fps_sound_panning, false, CLIENT, "see sound_panning");
CONVAR(mod_fullalternate, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_halfwindow, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_halfwindow_allow_300s, true, CLIENT | SERVER | GAMEPLAY,
       "should positive hit deltas be allowed within 300 range");
CONVAR(mod_hd_circle_fadein_end_percent, 0.6f, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "hiddenFadeInEndTime = circleTime - approachTime * mod_hd_circle_fadein_end_percent");
CONVAR(mod_hd_circle_fadein_start_percent, 1.0f, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "hiddenFadeInStartTime = circleTime - approachTime * mod_hd_circle_fadein_start_percent");
CONVAR(mod_jigsaw1, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_jigsaw2, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_jigsaw_followcircle_radius_factor, 0.0f, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_mafham, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_mafham_ignore_hittable_dim, true, CLIENT | SERVER | GAMEPLAY,
       "having hittable dim enabled makes it possible to \"read\" the beatmap by "
       "looking at the un-dim animations (thus making it a lot easier)");
CONVAR(mod_mafham_render_chunksize, 15, CLIENT | SERVER | GAMEPLAY,
       "render this many hitobjects per frame chunk into the scene buffer (spreads "
       "rendering across many frames to minimize lag)");
CONVAR(mod_mafham_render_livesize, 25, CLIENT | SERVER | GAMEPLAY,
       "render this many hitobjects without any scene buffering, higher = more lag but more up-to-date scene");
CONVAR(mod_millhioref, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_millhioref_multiplier, 2.0f, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_ming3012, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_minimize, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_minimize_multiplier, 0.5f, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_no_keylock, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_no100s, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_no50s, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_nightmare, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_reverse_sliders, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_shirone, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_shirone_combo, 20.0f, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_singletap, false, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_strict_tracking, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_strict_tracking_remove_slider_ticks, false, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "whether the strict tracking mod should remove slider ticks or not, "
       "this changed after its initial implementation in lazer");
CONVAR(mod_target_100_percent, 0.7f, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_target_300_percent, 0.5f, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_target_50_percent, 0.95f, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_timewarp, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_timewarp_multiplier, 1.5f, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_wobble, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_wobble2, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(mod_wobble_frequency, 1.0f, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_wobble_rotation_speed, 1.0f, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_wobble_strength, 25.0f, CLIENT | SERVER | GAMEPLAY);
CONVAR(mod_no_pausing, false, CLIENT | SERVER | GAMEPLAY);

// Important gameplay values
CONVAR(animation_speed_override, -1.0f, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(approachtime_max, 450, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(approachtime_mid, 1200, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(approachtime_min, 1800, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(cs_cap_sanity, true, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(skip_time, 5000.0f, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "Timeframe in ms within a beatmap which allows skipping if it doesn't contain any hitobjects");

// Accessibility
CONVAR(avoid_flashes, false, CLIENT, "disable flashing elements (like FL dimming on sliders)");

// Auto-updater
CONVAR(auto_update, true, CLIENT);
CONVAR(bleedingedge, false, CLIENT);
CONVAR(is_bleedingedge, false, CLIENT | HIDDEN,
       "used by the updater to tell if it should nag the user to 'update' to the correct release stream");

// Privacy settings
CONVAR(beatmap_mirror_override, ""sv, CLIENT, "URL of custom beatmap download mirror");
CONVAR(chat_auto_hide, true, CLIENT, "automatically hide chat during gameplay");
CONVAR(chat_highlight_words, ""sv, CLIENT, "space-separated list of words to treat as a mention");
CONVAR(chat_ignore_list, ""sv, CLIENT, "space-separated list of words to ignore");
CONVAR(chat_notify_on_dm, true, CLIENT);
CONVAR(chat_notify_on_mention, true, CLIENT, "get notified when someone says your name");
CONVAR(chat_ping_on_mention, true, CLIENT, "play a sound when someone says your name");
CONVAR(chat_ticker, true, CLIENT);

// Memes
CONVAR(auto_cursordance, false, CLIENT | SERVER);
CONVAR(auto_snapping_strength, 1.0f, CLIENT | SERVER,
       "How many iterations of quadratic interpolation to use, more = snappier, 0 = linear");

// Performance tweaks
CONVAR(background_image_cache_size, 32, CLIENT, "how many images can stay loaded in parallel");
CONVAR(background_image_eviction_delay_frames, 60, CLIENT,
       "how many vsync frames to keep stale background images in the cache before deleting them");
CONVAR(background_image_loading_delay, 0.075f, CLIENT,
       "how many seconds to wait until loading background images for visible beatmaps starts");
CONVAR(slider_curve_points_separation, 2.5f, CLIENT,  // NOTE: adjusted by options_slider_quality
       "slider body curve approximation step width in osu!pixels, don't set this lower than around 1.5");

// Sanity checks/limits
CONVAR(
    beatmap_max_num_hitobjects, 40000, CLIENT | PROTECTED | GAMEPLAY,
    "maximum number of total allowed hitobjects per beatmap (prevent crashing on deliberate game-breaking beatmaps)");
CONVAR(beatmap_max_num_slider_scoringtimes, 32768, CLIENT | PROTECTED | GAMEPLAY,
       "maximum number of slider score increase events allowed per slider "
       "(prevent crashing on deliberate game-breaking beatmaps)");
CONVAR(slider_curve_max_length, 65536.f / 2.f, CLIENT | PROTECTED | GAMEPLAY,
       "maximum slider length in osu!pixels (i.e. pixelLength). also used to clamp all "
       "(control-)point coordinates to sane values.");
CONVAR(slider_curve_max_points, 9999.0f, CLIENT | PROTECTED | GAMEPLAY,
       "maximum number of allowed interpolated curve points. quality will be forced to go "
       "down if a slider has more steps than this");
CONVAR(slider_end_inside_check_offset, 36, CLIENT | PROTECTED | GAMEPLAY,
       "offset in milliseconds going backwards from the end point, at which \"being "
       "inside the slider\" is checked. (osu bullshit behavior)");
CONVAR(slider_max_repeats, 9000, CLIENT | PROTECTED | GAMEPLAY,
       "maximum number of repeats allowed per slider (clamp range)");
CONVAR(slider_max_ticks, 2048, CLIENT | PROTECTED | GAMEPLAY,
       "maximum number of ticks allowed per slider (clamp range)");
CONVAR(beatmap_version, 128, CLIENT,
       "maximum supported .osu file version, above this will simply not load (this was 14 but got "
       "bumped to 128 due to lazer backports)");

// Online
CONVAR(mp_autologin, false, CLIENT);
CONVAR(mp_oauth_token, ""sv, CLIENT | HIDDEN);
CONVAR(mp_password, ""sv, CLIENT | HIDDEN | NOSAVE);
CONVAR(mp_password_md5, ""sv, CLIENT | HIDDEN);
CONVAR(mp_server, NEOMOD_DOMAIN ""sv, CLIENT);
CONVAR(name, "Guest"sv, CLIENT);
CONVAR(prefer_websockets, true, CLIENT, "prefer websocket connections over http polling");

// Server settings
CONVAR(sv_allow_speed_override, false, SERVER,
       "let clients submit scores with non-vanilla speeds (e.g. not only HT/DT speed)");
CONVAR(sv_has_irc_users, true, SERVER, "players with negative IDs will show up as IRC users");

// Main menu
CONVAR(adblock, false, CLIENT | SKINS | SERVER);
CONVAR(draw_menu_background, true, CLIENT | SKINS | SERVER);
CONVAR(main_menu_alpha, 0.8f, CLIENT | SKINS | SERVER);
CONVAR(main_menu_friend, true, CLIENT | SKINS | SERVER);
CONVAR(main_menu_background_fade_duration, 0.25f, CLIENT | SKINS | SERVER);
CONVAR(main_menu_startup_anim_duration, 0.25f, CLIENT | SKINS | SERVER);
CONVAR(main_menu_use_server_logo, true, CLIENT | SKINS | SERVER);

// Not sorted
CONVAR(diffcalc_threads, 0.f, CLIENT, "0 = autodetect");
CONVAR(beatmap_preview_mods_live, false, CLIENT | SKINS | SERVER,
       "whether to immediately apply all currently selected mods while browsing beatmaps (e.g. speed/pitch)");
CONVAR(beatmap_preview_music_loop, true, CLIENT | SKINS | SERVER);
CONVAR(bug_flicker_log, false, CLIENT | SKINS | SERVER);
CONVAR(notify_during_gameplay, false, CLIENT | SERVER, "show notification popups instantly during gameplay");
CONVAR(circle_color_saturation, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(circle_fade_out_scale, 0.4f, CLIENT | SKINS | SERVER);
CONVAR(circle_number_rainbow, false, CLIENT | SKINS | SERVER);
CONVAR(circle_rainbow, false, CLIENT | SKINS | SERVER);
CONVAR(circle_shake_duration, 0.120f, CLIENT | SKINS | SERVER);
CONVAR(circle_shake_strength, 8.0f, CLIENT | SKINS | SERVER);
CONVAR(collections_custom_enabled, true, CLIENT | SKINS | SERVER, "load custom collections.db");
CONVAR(collections_custom_version, 20220110, CLIENT | SKINS | SERVER,
       "maximum supported custom collections.db version");
CONVAR(collections_legacy_enabled, true, CLIENT | SKINS | SERVER, "load osu!'s collection.db");
CONVAR(collections_save_immediately, true, CLIENT | SKINS | SERVER,
       "write collections.db as soon as anything is changed");
CONVAR(combo_anim1_duration, 0.15f, CLIENT | SKINS | SERVER);
CONVAR(combo_anim1_size, 0.15f, CLIENT | SKINS | SERVER);
CONVAR(combo_anim2_duration, 0.4f, CLIENT | SKINS | SERVER);
CONVAR(combo_anim2_size, 0.5f, CLIENT | SKINS | SERVER);
CONVAR(combobreak_sound_combo, 20, CLIENT | SKINS | SERVER,
       "Only play the combobreak sound if the combo is higher than this");
CONVAR(compensate_music_speed, true, CLIENT | SKINS | SERVER,
       "compensates speeds slower than 1x a little bit, by adding an offset depending on the slowness");
CONVAR(confine_cursor_fullscreen, true, CLIENT | SKINS | SERVER);
CONVAR(confine_cursor_windowed, false, CLIENT | SKINS | SERVER);
CONVAR(confine_cursor_never, false, CLIENT | SKINS | SERVER);
CONVAR(crop_screenshots, true, CLIENT, "whether to crop screenshots to the letterboxed resolution");
CONVAR(cursor_alpha, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(cursor_expand_duration, 0.1f, CLIENT | SKINS | SERVER);
CONVAR(cursor_expand_scale_multiplier, 1.3f, CLIENT | SKINS | SERVER);
CONVAR(cursor_ripple_additive, true, CLIENT | SKINS | SERVER, "use additive blending");
CONVAR(cursor_ripple_alpha, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(cursor_ripple_anim_end_scale, 0.5f, CLIENT | SKINS | SERVER, "end size multiplier");
CONVAR(cursor_ripple_anim_start_fadeout_delay, 0.0f, CLIENT | SKINS | SERVER,
       "delay in seconds after which to start fading out (limited by cursor_ripple_duration of course)");
CONVAR(cursor_ripple_anim_start_scale, 0.05f, CLIENT | SKINS | SERVER, "start size multiplier");
CONVAR(cursor_ripple_duration, 0.7f, CLIENT | SKINS | SERVER, "time in seconds each cursor ripple is visible");
CONVAR(cursor_ripple_tint_b, 255, CLIENT | SKINS | SERVER, "from 0 to 255");
CONVAR(cursor_ripple_tint_g, 255, CLIENT | SKINS | SERVER, "from 0 to 255");
CONVAR(cursor_ripple_tint_r, 255, CLIENT | SKINS | SERVER, "from 0 to 255");
CONVAR(cursor_scale, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(cursor_trail_alpha, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(cursor_trail_expand, true, CLIENT | SKINS | SERVER,
       "if \"CursorExpand: 1\" in your skin.ini, whether the trail should then also expand or not");
CONVAR(cursor_trail_length, 0.17f, CLIENT | SKINS | SERVER, "how long unsmooth cursortrails should be, in seconds");
CONVAR(cursor_trail_max_size, 2048, CLIENT | SKINS | SERVER,
       "maximum number of rendered trail images, array size limit");
CONVAR(cursor_trail_scale, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(cursor_trail_smooth_div, 4.0f, CLIENT | SKINS | SERVER,
       "divide the cursortrail.png image size by this much, for determining the distance to the next trail image");
CONVAR(cursor_trail_smooth_force, false, CLIENT | SKINS | SERVER);
CONVAR(cursor_trail_smooth_length, 0.5f, CLIENT | SKINS | SERVER, "how long smooth cursortrails should be, in seconds");
CONVAR(cursor_trail_spacing, 15.f, CLIENT | SKINS | SERVER,
       "how big the gap between consecutive unsmooth cursortrail images should be, in milliseconds");
CONVAR(disable_mousebuttons, Env::cfg(OS::WASM) ? false : true, CLIENT | SKINS | SERVER);
CONVAR(disable_mousewheel, true, CLIENT | SKINS | SERVER);
CONVAR(drain_kill, true, CLIENT | SERVER | PROTECTED | GAMEPLAY, "whether to kill the player upon failing");
CONVAR(drain_disabled, false, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "determines if HP drain should be disabled entirely");
CONVAR(drain_kill_notification_duration, 1.0f, CLIENT | SKINS | SERVER,
       "how long to display the \"You have failed, but you can keep playing!\" notification (0 = disabled)");
CONVAR(draw_runtime_info, true, CLIENT | SERVER, "draw version identifier in the bottom right");
CONVAR(early_note_time, 1500.0f, CLIENT | SKINS | SERVER | GAMEPLAY,
       "Timeframe in ms at the beginning of a beatmap which triggers a starting delay for easier reading");
CONVAR(end_delay_time, 750.0f, CLIENT | SKINS | SERVER,
       "Duration in ms which is added at the end of a beatmap after the last hitobject is finished "
       "but before the ranking screen is automatically shown");
CONVAR(end_skip, true, CLIENT | SKINS | SERVER,
       "whether the beatmap jumps to the ranking screen as soon as the last hitobject plus lenience has passed");
CONVAR(end_skip_time, 400.0f, CLIENT | SKINS | SERVER,
       "Duration in ms which is added to the endTime of the last hitobject, after which pausing the "
       "game will immediately jump to the ranking screen");
CONVAR(fail_time, 2.25f, CLIENT | SKINS | SERVER,
       "Timeframe in s for the slowdown effect after failing, before the pause menu is shown");
CONVAR(flashlight_always_hard, false, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "always use 200+ combo flashlight radius");
CONVAR(flashlight_follow_delay, 0.120f, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(flashlight_radius, 100.f, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(followpoints_anim, false, CLIENT | SKINS | SERVER,
       "scale + move animation while fading in followpoints (osu only does this when its "
       "internal default skin is being used)");
CONVAR(followpoints_approachtime, 800.0f, CLIENT | SERVER | GAMEPLAY);
CONVAR(followpoints_clamp, false, CLIENT | SERVER | GAMEPLAY,
       "clamp followpoint approach time to current circle approach time (instead of using the "
       "hardcoded default 800 ms raw)");
CONVAR(followpoints_connect_combos, false, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "connect followpoints even if a new combo has started");
CONVAR(followpoints_connect_spinners, false, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "connect followpoints even through spinners");
CONVAR(followpoints_prevfadetime, 400.0f, CLIENT | SERVER | GAMEPLAY);
CONVAR(followpoints_scale_multiplier, 1.0f, CLIENT | SERVER | GAMEPLAY);
CONVAR(followpoints_separation_multiplier, 1.0f, CLIENT | SERVER | GAMEPLAY);
CONVAR(force_oauth, false, CLIENT, "always display oauth login button instead of password field");
CONVAR(force_legacy_slider_renderer, false, CLIENT | SKINS | SERVER,
       "on some older machines, this may be faster than vertexbuffers");
CONVAR(fposu_3d_skybox, true, CLIENT | SKINS | SERVER);
CONVAR(fposu_3d_skybox_size, 450.0f, CLIENT | SKINS | SERVER);
CONVAR(fposu_absolute_mode, false, CLIENT | SKINS | SERVER);
CONVAR(fposu_center_cursor_on_start, true, CLIENT,
       "snap cursor to the center of the screen when starting a beatmap in fposu mode");
CONVAR(fposu_cube, true, CLIENT | SKINS | SERVER);
CONVAR(fposu_cube_size, 500.0f, CLIENT | SKINS | SERVER);
CONVAR(fposu_cube_tint_b, 255, CLIENT | SKINS | SERVER, "from 0 to 255");
CONVAR(fposu_cube_tint_g, 255, CLIENT | SKINS | SERVER, "from 0 to 255");
CONVAR(fposu_cube_tint_r, 255, CLIENT | SKINS | SERVER, "from 0 to 255");
CONVAR(fposu_curved, true, CLIENT | SKINS | SERVER);
CONVAR(fposu_distance, 0.5f, CLIENT | SKINS | SERVER);
CONVAR(fposu_draw_cursor_trail, true, CLIENT | SKINS | SERVER);
CONVAR(fposu_draw_scorebarbg_on_top, false, CLIENT | SKINS | SERVER);
CONVAR(fposu_fov, 103.0f, CLIENT | SKINS | SERVER);
CONVAR(fposu_invert_horizontal, false, CLIENT | SKINS | SERVER);
CONVAR(fposu_invert_vertical, false, CLIENT | SKINS | SERVER);
CONVAR(fposu_mod_strafing, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(fposu_mod_strafing_frequency_x, 0.1f, CLIENT | SERVER | GAMEPLAY);
CONVAR(fposu_mod_strafing_frequency_y, 0.2f, CLIENT | SERVER | GAMEPLAY);
CONVAR(fposu_mod_strafing_frequency_z, 0.15f, CLIENT | SERVER | GAMEPLAY);
CONVAR(fposu_mod_strafing_strength_x, 0.3f, CLIENT | SERVER | GAMEPLAY);
CONVAR(fposu_mod_strafing_strength_y, 0.1f, CLIENT | SERVER | GAMEPLAY);
CONVAR(fposu_mod_strafing_strength_z, 0.15f, CLIENT | SERVER | GAMEPLAY);
CONVAR(fposu_mouse_cm_360, 30.0f, CLIENT | SKINS | SERVER);
CONVAR(fposu_mouse_dpi, 400, CLIENT | SKINS | SERVER);
CONVAR(fposu_noclip, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(fposu_noclipaccelerate, 20.0f, CLIENT | SKINS | SERVER);
CONVAR(fposu_noclipfriction, 10.0f, CLIENT | SKINS | SERVER);
CONVAR(fposu_noclipspeed, 2.0f, CLIENT | SKINS | SERVER);
CONVAR(fposu_playfield_position_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(fposu_playfield_position_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(fposu_playfield_position_z, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(fposu_playfield_rotation_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(fposu_playfield_rotation_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(fposu_playfield_rotation_z, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(fposu_skybox, true, CLIENT | SKINS | SERVER);
CONVAR(fposu_transparent_playfield, false, CLIENT | SKINS | SERVER,
       "only works if background dim is 100% and background brightness is 0%");
CONVAR(fposu_vertical_fov, false, CLIENT | SKINS | SERVER);
CONVAR(fposu_zoom_anim_duration, 0.065f, CLIENT | SKINS | SERVER, "time in seconds for the zoom/unzoom animation");
CONVAR(fposu_zoom_fov, 45.0f, CLIENT | SKINS | SERVER);
CONVAR(fposu_zoom_sensitivity_ratio, 1.0f, CLIENT | SKINS | SERVER,
       "replicates zoom_sensitivity_ratio behavior on css/csgo/tf2/etc.");
CONVAR(fposu_zoom_toggle, false, CLIENT | SKINS | SERVER, "whether the zoom key acts as a toggle");
CONVAR(hiterrorbar_misaims, true, CLIENT | SKINS | SERVER);
CONVAR(hiterrorbar_misses, true, CLIENT | SKINS | SERVER);
CONVAR(hitobject_fade_in_time, 400, CLIENT | SERVER | PROTECTED | GAMEPLAY, "in milliseconds (!)");
CONVAR(hitobject_fade_out_time, 0.293f, CLIENT | SERVER | PROTECTED | GAMEPLAY, "in seconds (!)");
CONVAR(hitobject_fade_out_time_speed_multiplier_min, 0.5f, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "The minimum multiplication factor allowed for the speed multiplier influencing the fadeout duration");
CONVAR(hitobject_hittable_dim, true, CLIENT | SKINS | SERVER,
       "whether to dim objects not yet within the miss-range (when they can't even be missed yet)");
CONVAR(hitobject_hittable_dim_duration, 100, CLIENT | SKINS | SERVER, "in milliseconds (!)");
CONVAR(hitobject_hittable_dim_start_percent, 0.7647f, CLIENT | SKINS | SERVER,
       "dimmed objects start at this brightness value before becoming fullbright (only RGB, this does not affect "
       "alpha/transparency)");
CONVAR(hitresult_animated, true, CLIENT | SKINS | SERVER,
       "whether to animate hitresult scales (depending on particle<SCORE>.png, either scale wobble or smooth scale)");
CONVAR(hitresult_delta_colorize, false, CLIENT | SKINS | SERVER,
       "whether to colorize hitresults depending on how early/late the hit (delta) was");
CONVAR(hitresult_delta_colorize_early_b, 0, CLIENT | SKINS | SERVER, "from 0 to 255");
CONVAR(hitresult_delta_colorize_early_g, 0, CLIENT | SKINS | SERVER, "from 0 to 255");
CONVAR(hitresult_delta_colorize_early_r, 255, CLIENT | SKINS | SERVER, "from 0 to 255");
CONVAR(hitresult_delta_colorize_interpolate, true, CLIENT | SKINS | SERVER,
       "whether colorized hitresults should smoothly interpolate between "
       "early/late colors depending on the hit delta amount");
CONVAR(hitresult_delta_colorize_late_b, 255, CLIENT | SKINS | SERVER, "from 0 to 255");
CONVAR(hitresult_delta_colorize_late_g, 0, CLIENT | SKINS | SERVER, "from 0 to 255");
CONVAR(hitresult_delta_colorize_late_r, 0, CLIENT | SKINS | SERVER, "from 0 to 255");
CONVAR(
    hitresult_delta_colorize_multiplier, 2.0f, CLIENT | SKINS | SERVER,
    "early/late colors are multiplied by this (assuming interpolation is enabled, increasing this will make early/late "
    "colors appear fully earlier)");
CONVAR(hitresult_draw_300s, false, CLIENT | SKINS | SERVER);
CONVAR(hitresult_duration, 1.100f, CLIENT | SKINS | SERVER,
       "max duration of the entire hitresult in seconds (this limits all other values, except for animated skins!)");
CONVAR(hitresult_duration_max, 5.0f, CLIENT | SKINS | SERVER,
       "absolute hard limit in seconds, even for animated skins");
CONVAR(hitresult_fadein_duration, 0.120f, CLIENT | SKINS | SERVER);
CONVAR(hitresult_fadeout_duration, 0.600f, CLIENT | SKINS | SERVER);
CONVAR(hitresult_fadeout_start_time, 0.500f, CLIENT | SKINS | SERVER);
CONVAR(hitresult_miss_fadein_scale, 2.0f, CLIENT | SKINS | SERVER);
CONVAR(hitresult_scale, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(ignore_beatmap_combo_colors, true, CLIENT | SKINS | SERVER);
CONVAR(ignore_beatmap_combo_numbers, false, CLIENT | SKINS | SERVER, "may be used in conjunction with number_max");
CONVAR(ignore_beatmap_sample_volume, false, CLIENT | SKINS | SERVER);
CONVAR(instafade, false, CLIENT | SKINS | SERVER, "don't draw hitcircle fadeout animations");
CONVAR(instafade_sliders, false, CLIENT | SKINS | SERVER, "don't draw slider fadeout animations");
CONVAR(instant_replay_duration, 15.f, CLIENT | SKINS | SERVER, "instant replay (F2) duration, in seconds");
CONVAR(letterboxing, true, CLIENT | SKINS | SERVER);
CONVAR(letterboxing_offset_x, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(letterboxing_offset_y, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(load_beatmap_background_images, true, CLIENT | SKINS | SERVER);
CONVAR(nightcore_enjoyer, false, CLIENT | SKINS | SERVER);
CONVAR(normalize_loudness, true, CLIENT | SKINS | SERVER, "normalize loudness across songs");
CONVAR(notelock_stable_tolerance2b, 3, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "time tolerance in milliseconds to allow hitting simultaneous objects close "
       "together (e.g. circle at end of slider)");
CONVAR(notelock_type, 2, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "which notelock algorithm to use (0 = None, 1 = " PACKAGE_NAME ", 2 = osu!stable, 3 = osu!lazer 2020)");
CONVAR(notification_duration, 1.25f, CLIENT | SKINS | SERVER);
CONVAR(notify_friend_status_change, true, CLIENT, "notify when friends change status");
CONVAR(number_max, 0, CLIENT | SKINS | SERVER,
       "0 = disabled, 1/2/3/4/etc. limits visual circle numbers to this number");
CONVAR(number_scale_multiplier, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(od_override, -1.0f, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(od_override_lock, false, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "always force constant 300 hit window even through speed changes");
CONVAR(old_beatmap_offset, 24.0f, CLIENT | SKINS | SERVER,
       "offset in ms which is added to beatmap versions < 5 (default value is hardcoded 24 ms in stable)");
CONVAR(timingpoints_offset, 0.0f, CLIENT,
       "offset in ms which is added before determining the active timingpoint for the sample type and sample volume "
       "(hitsounds) of the current frame");
CONVAR(options_high_quality_sliders, false, CLIENT | SKINS | SERVER);
CONVAR(options_save_on_back, true, CLIENT | SKINS | SERVER);
CONVAR(options_slider_preview_use_legacy_renderer, false, CLIENT,
       "apparently newer AMD drivers with old gpus are crashing here with the legacy renderer? was just me being lazy "
       "anyway, so now there is a vao render path as it should be");
CONVAR(options_slider_quality, 0.0f, CLIENT | SKINS | SERVER);
CONVAR(pause_anim_duration, 0.15f, CLIENT | SKINS | SERVER);
CONVAR(pause_dim_alpha, 0.58f, CLIENT | SKINS | SERVER);
CONVAR(pause_dim_background, true, CLIENT | SKINS | SERVER);
CONVAR(pause_on_focus_loss, true, CLIENT | SKINS | SERVER);
CONVAR(unpause_continue_delay, 0.15f, CLIENT,
       "when unpausing, wait for this many seconds before allowing \"click to continue\" to be actually clicked (to "
       "avoid instantly triggering accidentally)");
CONVAR(pvs, true, CLIENT | SKINS | SERVER,
       "optimizes all loops over all hitobjects by clamping the range to the Potentially Visible Set");
CONVAR(quick_retry_delay, 0.27f, CLIENT | SKINS | SERVER);
CONVAR(quick_retry_time, 2000.0f, CLIENT | SKINS | SERVER,
       "Timeframe in ms subtracted from the first hitobject when quick retrying (not regular retry)");
CONVAR(rankingscreen_pp, true, CLIENT | SKINS | SERVER);
CONVAR(rankingscreen_topbar_height_percent, 0.785f, CLIENT | SKINS | SERVER);
CONVAR(relax_offset, -12, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "osu!relax always hits -12 ms too early, so set this to -12 (note the negative) if you want it to be the same");
CONVAR(resolution, "1x1"sv, CLIENT | SKINS | SERVER);
CONVAR(letterboxed_resolution, "1x1"sv, CLIENT | SKINS | SERVER);
CONVAR(windowed_resolution, "1280x720"sv, CLIENT | SKINS | SERVER | (Env::cfg(OS::WASM) ? NOLOAD : 0));
CONVAR(resolution_keep_aspect_ratio, false, CLIENT | SKINS | SERVER);
CONVAR(restart_sound_engine_before_playing, false, CLIENT | SKINS | SERVER,
       "jank fix for users who experience sound issues after playing for a while");
CONVAR(rich_presence_map_backgrounds, true, CLIENT | SKINS | SERVER);
CONVAR(scoreboard_animations, true, CLIENT | SKINS | SERVER, "animate in-game scoreboard");
CONVAR(scores_bonus_pp, true, CLIENT | SKINS | SERVER, "whether to add bonus pp to total (real) pp or not");
CONVAR(scores_enabled, true, CLIENT | SKINS | SERVER);
CONVAR(scores_save_immediately, true, CLIENT | SKINS | SERVER, "write scores.db as soon as a new score is added");
CONVAR(scores_sort_by_pp, true, CLIENT | SKINS | SERVER, "display pp in score browser instead of score");
CONVAR(scrubbing_smooth, true, CLIENT | SKINS | SERVER);
CONVAR(seek_delta, 5, CLIENT | SKINS | SERVER, "how many seconds to skip backward/forward when quick seeking");
CONVAR(show_approach_circle_on_first_hidden_object, true, CLIENT | SKINS | SERVER);
CONVAR(simulate_replays, false, CLIENT | SKINS | SERVER, "experimental \"improved\" replay playback");
CONVAR(skin, "default"sv, CLIENT | SKINS | SERVER);
CONVAR(skin_fallback, ""sv, CLIENT | SKINS | SERVER, "fallback skin for missing elements");
CONVAR(skin_animation_force, false, CLIENT | SKINS | SERVER);
CONVAR(skin_animation_fps_override, -1.0f, CLIENT | SKINS | SERVER);
CONVAR(skin_async, true, CLIENT | SKINS | SERVER, "load in background without blocking");
CONVAR(skin_color_index_add, 0, CLIENT | SKINS | SERVER);
CONVAR(skin_force_hitsound_sample_set, 0, CLIENT | SKINS | SERVER,
       "force a specific hitsound sample set to always be used regardless of what "
       "the beatmap says. 0 = disabled, 1 = normal, 2 = soft, 3 = drum.");
CONVAR(skin_hd, true, CLIENT | SKINS | SERVER, "load and use @2x versions of skin images, if available");
CONVAR(skin_mipmaps, false, CLIENT | SKINS | SERVER,
       "generate mipmaps for every skin image (only useful on lower game resolutions, requires more vram)");
CONVAR(skin_random, false, CLIENT | SKINS | SERVER, "select random skin from list on every skin load/reload");
CONVAR(skin_random_elements, false, CLIENT | SKINS | SERVER, "sElECt RanDOM sKIn eLemENTs FRoM ranDom SkINs");
CONVAR(skin_reload);
CONVAR(skin_use_skin_hitsounds, true, CLIENT | SKINS | SERVER,
       "If enabled: Use skin's sound samples. If disabled: Use default skin's sound samples. For hitsounds only.");
CONVAR(
    skin_use_spinner_metre, false, CLIENT | SKINS | SERVER,
    "enable the spinner-metre graphic, which fills up as the spinner completes.");  // temporary until spinner-metre isn't ugly as hell
CONVAR(skip_breaks_enabled, true, CLIENT | SKINS | SERVER,
       "enables/disables skip button for breaks in the middle of beatmaps");
CONVAR(skip_intro_enabled, true, CLIENT | SKINS | SERVER,
       "enables/disables skip button for intro until first hitobject");
CONVAR(slider_alpha_multiplier, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(slider_ball_tint_combo_color, true, CLIENT | SKINS | SERVER);
CONVAR(slider_body_alpha_multiplier, 1.0f, CLIENT | SKINS | SERVER, CFUNC(SliderRenderer::onUniformConfigChanged));
CONVAR(slider_body_color_saturation, 1.0f, CLIENT | SKINS | SERVER, CFUNC(SliderRenderer::onUniformConfigChanged));
CONVAR(slider_body_fade_out_time_multiplier, 1.0f, CLIENT | SKINS | SERVER, "multiplies hitobject_fade_out_time");
CONVAR(slider_body_lazer_fadeout_style, true, CLIENT | SKINS | SERVER,
       "if snaking out sliders are enabled (aka shrinking sliders), smoothly fade "
       "out the last remaining part of the body (instead of vanishing instantly)");
CONVAR(slider_body_smoothsnake, true, CLIENT | SKINS | SERVER,
       "draw 1 extra interpolated circle mesh at the start & end of every slider for extra smooth snaking/shrinking");
CONVAR(slider_body_unit_circle_subdivisions, 42, CLIENT | SKINS | SERVER);
CONVAR(slider_border_feather, 0.0f, CLIENT | SKINS | SERVER, CFUNC(SliderRenderer::onUniformConfigChanged));
CONVAR(slider_border_size_multiplier, 1.0f, CLIENT | SKINS | SERVER, CFUNC(SliderRenderer::onUniformConfigChanged));
CONVAR(slider_border_tint_combo_color, false, CLIENT | SKINS | SERVER);
CONVAR(slider_draw_body, true, CLIENT | SKINS | SERVER);
CONVAR(slider_draw_endcircle, true, CLIENT | SKINS | SERVER);
CONVAR(slider_end_miss_breaks_combo, false, CLIENT | SKINS | SERVER,
       "should a missed sliderend break combo (aka cause a regular sliderbreak)");
CONVAR(slider_followcircle_fadein_fade_time, 0.06f, CLIENT | SKINS | SERVER);
CONVAR(slider_followcircle_fadein_scale, 0.5f, CLIENT | SKINS | SERVER);
CONVAR(slider_followcircle_fadein_scale_time, 0.18f, CLIENT | SKINS | SERVER);
CONVAR(slider_followcircle_fadeout_fade_time, 0.25f, CLIENT | SKINS | SERVER);
CONVAR(slider_followcircle_fadeout_scale, 0.8f, CLIENT | SKINS | SERVER);
CONVAR(slider_followcircle_fadeout_scale_time, 0.25f, CLIENT | SKINS | SERVER);
CONVAR(slider_followcircle_tick_pulse_scale, 0.1f, CLIENT | SKINS | SERVER);
CONVAR(slider_followcircle_tick_pulse_time, 0.2f, CLIENT | SKINS | SERVER);
CONVAR(
    slider_legacy_use_baked_vao, false, CLIENT | SKINS | SERVER,
    "use baked cone mesh instead of raw mesh for legacy slider renderer (disabled by default because usually slower on "
    "very old gpus even though it should not be)");
CONVAR(slider_osu_next_style, false, CLIENT | SKINS | SERVER, CFUNC(SliderRenderer::onUniformConfigChanged));
CONVAR(slider_rainbow, false, CLIENT | SKINS | SERVER);
CONVAR(slider_reverse_arrow_alpha_multiplier, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(slider_reverse_arrow_animated, true, CLIENT | SKINS | SERVER, "pulse animation on reverse arrows");
CONVAR(slider_reverse_arrow_black_threshold, 1.0f, CLIENT | SKINS | SERVER,
       "Blacken reverse arrows if the average color brightness percentage is above this value");
CONVAR(slider_reverse_arrow_fadein_duration, 150, CLIENT | SKINS | SERVER,
       "duration in ms of the reverse arrow fadein animation after it starts");
CONVAR(slider_shrink, false, CLIENT | SKINS | SERVER);
CONVAR(slider_sliderhead_fadeout, true, CLIENT | SKINS | SERVER);
CONVAR(slider_snake_duration_multiplier, 1.0f, CLIENT | SKINS | SERVER,
       "the default snaking duration is multiplied with this (max sensible value "
       "is 3, anything above that will take longer than the approachtime)");
CONVAR(slider_use_gradient_image, false, CLIENT | SKINS | SERVER);
CONVAR(snaking_sliders, true, CLIENT | SKINS | SERVER);
CONVAR(sort_skins_cleaned, false, CLIENT | SKINS | SERVER,
       "set to true to sort skins alphabetically, ignoring special characters at the start (not like stable)");

CONVAR(spec_buffer, 2500, CLIENT, "size of spectator buffer in milliseconds");
CONVAR(spec_share_map, true, CLIENT | SKINS | SERVER, "automatically send currently-playing beatmap to #spectator");

CONVAR(spinner_fade_out_time_multiplier, 0.7f, CLIENT | SKINS | SERVER);
CONVAR(spinner_use_ar_fadein, false, CLIENT | SKINS | SERVER,
       "whether spinners should fade in with AR (same as circles), or with hardcoded 400 ms fadein time (osu!default)");
CONVAR(stars_ignore_clamped_sliders, true, CLIENT | SKINS | SERVER,
       "skips processing sliders limited by slider_curve_max_length");
CONVAR(stars_slider_curve_points_separation, 20.0f, CLIENT | SKINS | SERVER,
       "massively reduce curve accuracy for star calculations to save memory/performance");
CONVAR(stars_stacking, true, CLIENT | SKINS | SERVER, "respect hitobject stacking before calculating stars/pp");
CONVAR(start_first_main_menu_song_at_preview_point, false, CLIENT);
CONVAR(submit_after_pause, true, CLIENT | SERVER);
CONVAR(submit_scores, false, CLIENT | SERVER);
CONVAR(tooltip_anim_duration, 0.4f, CLIENT | SKINS | SERVER);
CONVAR(ui_scale, 1.0f, CLIENT | SKINS | SERVER, "multiplier");
CONVAR(ui_scale_to_dpi, true, CLIENT | SKINS | SERVER,
       "whether the game should scale its UI based on the DPI reported by your operating system");
CONVAR(ui_scale_to_dpi_minimum_height, 1300, CLIENT | SKINS | SERVER,
       "any in-game resolutions below this will have ui_scale_to_dpi force disabled");
CONVAR(ui_scale_to_dpi_minimum_width, 2200, CLIENT | SKINS | SERVER,
       "any in-game resolutions below this will have ui_scale_to_dpi force disabled");
CONVAR(ui_top_ranks_max, 200, CLIENT | SKINS | SERVER,
       "maximum number of displayed scores, to keep the ui/scrollbar manageable");
CONVAR(ui_window_shadow_radius, 13.0f, CLIENT | SKINS | SERVER);
CONVAR(universal_offset, 0.0f, CLIENT, "rate-dependent offset for music (multiplied by playback speed)");
CONVAR(universal_offset_norate, 0.0f, CLIENT, "rate-independent offset for music");
CONVAR(universal_offset_hardcoded_blamepeppy, 0.0f, CLIENT, "this is in lazer");
CONVAR(use_ppv3, false, CLIENT | SKINS | SERVER, "use ppv3 instead of ppv2 (experimental)");
CONVAR(user_draw_accuracy, true, CLIENT | SKINS | SERVER);
CONVAR(user_draw_level, true, CLIENT | SKINS | SERVER);
CONVAR(user_draw_level_bar, true, CLIENT | SKINS | SERVER);
CONVAR(user_draw_pp, true, CLIENT | SKINS | SERVER);
CONVAR(user_include_relax_and_autopilot_for_stats, false, CLIENT | SKINS | SERVER);

// Unfinished features
CONVAR(enable_spectating, false, CLIENT);
CONVAR(allow_mp_invites, true, CLIENT, "allow multiplayer game invites from all users");
CONVAR(allow_stranger_dms, true, CLIENT, "allow private messages from non-friends");
CONVAR(ignore_beatmap_samples, false, CLIENT | SERVER, "ignore beatmap hitsounds");
CONVAR(ignore_beatmap_skins, false, CLIENT | SERVER, "ignore beatmap skins");
CONVAR(language, "en"sv, CLIENT | SERVER);
CONVAR(draw_storyboard, true, CLIENT | SERVER);
CONVAR(draw_video, true, CLIENT | SERVER);
CONVAR(save_failed_scores, false, CLIENT | HIDDEN, "save scores locally, even if there was a fail");
CONVAR(enable_screenshots, Env::cfg(OS::WASM) ? false : true, CLIENT | SKINS | SERVER);

// NOLINTEND(misc-definitions-in-headers)

}  // namespace cv

#endif
