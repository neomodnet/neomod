#ifndef CONVARDEFS_H
#define CONVARDEFS_H

// DO NOT put osu-related convars in this file (put them in OsuConVarDefs.h)

// ########################################################################################################################
// # this first part is just to allow for proper code completion when editing this file
// ########################################################################################################################

// NOLINTBEGIN(misc-definitions-in-headers)

#if !defined(CONVAR_H) && \
    (defined(_CLANGD) || defined(Q_CREATOR_RUN) || defined(__INTELLISENSE__) || defined(__CDT_PARSER__))
#define DEFINE_CONVARS
#include "ConVar.h"

struct dummyEngine {
    inline void shutdown() { ; }
    inline void toggleFullscreen() { ; }
};
dummyEngine *engine;

struct dummyGraphics {
    inline void takeScreenshot(std::string_view /*args*/) { ; }
};
dummyGraphics *g;

namespace ConVarHandler::ConVarBuiltins {
extern void find(std::string_view args);
extern void help(std::string_view args);
extern void listcommands(void);
extern void dumpcommands(void);
extern void echo(std::string_view args);
}  // namespace ConVarHandler::ConVarBuiltins

extern void _borderless();
extern void _center();
extern void _dpiinfo();
extern void _errortest();
extern void _focus();
extern void _maximize();
extern void _minimize();
extern void _printsize();
extern void _toggleresizable();
extern void _restart();
extern void _update();

#endif

// ########################################################################################################################
// # actual declarations/definitions below
// ########################################################################################################################

#define _CV(name) name
// helper to create an SA delegate from a freestanding/static function
#define CFUNC(func) SA::delegate<decltype(func)>::template create<func>()

// defined and included at the end of ConVar.cpp
#if defined(DEFINE_CONVARS)
#undef CONVAR
#define CONVAR(name, ...) ConVar _CV(name)(#name __VA_OPT__(, ) __VA_ARGS__)

#include "BaseEnvironment.h"

namespace Profiling {
extern void vprofToggleCB(float);
}
namespace McThread {
enum Priority : unsigned char;
extern void set_current_thread_prio(Priority /**/);
}  // namespace McThread

namespace CBaseUIDebug {
extern void onDumpElemsChangeCallback(float newvalue);
}

namespace AnimationHandler {
extern void onDebugAnimChange(float newVal);
}

#else
#define CONVAR(name, ...) extern ConVar _CV(name)
#endif

class ConVar;
namespace cv {
// special cased to improve dependency tracked rebuilds
extern ConVar build_timestamp;
namespace cmd {

// Generic commands
CONVAR(crash, CLIENT | HIDDEN | NOLOAD | NOSAVE, SA::delegate<void()>::template create<fubar_abort_>());  // debug
CONVAR(borderless, CLIENT, CFUNC(_borderless));
CONVAR(center, CLIENT, CFUNC(_center));
CONVAR(clear, NOLOAD);
CONVAR(dpiinfo, CLIENT, CFUNC(_dpiinfo));
CONVAR(dumpcommands, CLIENT, CFUNC(ConVarHandler::ConVarBuiltins::dumpcommands));
CONVAR(errortest, CLIENT, CFUNC(_errortest));
CONVAR(exec, CLIENT | NOLOAD);  // set in ConsoleBox
CONVAR(find, CLIENT, CFUNC(ConVarHandler::ConVarBuiltins::find));
CONVAR(focus, CLIENT, CFUNC(_focus));
CONVAR(help, CLIENT, CFUNC(ConVarHandler::ConVarBuiltins::help));
CONVAR(listcommands, CLIENT, CFUNC(ConVarHandler::ConVarBuiltins::listcommands));
CONVAR(maximize, CLIENT, CFUNC(_maximize));
CONVAR(minimize, CLIENT, CFUNC(_minimize));
CONVAR(printsize, CLIENT, CFUNC(_printsize));
CONVAR(resizable_toggle, CLIENT, CFUNC(_toggleresizable));
CONVAR(restart, CLIENT, CFUNC(_restart));
CONVAR(save);  // database save, set in Database
CONVAR(showconsolebox);
CONVAR(snd_restart);
CONVAR(take_screenshot, CLIENT | NOLOAD | NOSAVE,
       [](std::string_view args) -> void { g ? g->takeScreenshot(args) : (void)0; });
CONVAR(update);

// Server-callable commands
CONVAR(exit, CLIENT | SERVER, []() -> void { engine ? engine->shutdown() : (void)0; });
CONVAR(shutdown, CLIENT | SERVER, []() -> void { engine ? engine->shutdown() : (void)0; });

// Server and skin-callable commands
CONVAR(echo, CLIENT | SKINS | SERVER, CFUNC(ConVarHandler::ConVarBuiltins::echo));

}  // namespace cmd

// Audio
CONVAR(asio_buffer_size, -1, CLIENT, "buffer size in samples (usually 44100 samples per second)");
CONVAR(asio_freq, 0, CLIENT, "preferred sample rate (0 means let the device decide)");
CONVAR(snd_async_buffer, 65536, CLIENT, "BASS_CONFIG_ASYNCFILE_BUFFER length in bytes. Set to 0 to disable.");
CONVAR(snd_change_check_interval, 0.5f, CLIENT,
       "check for output device changes every this many seconds. 0 = disabled");
CONVAR(snd_dev_buffer, 30, CLIENT, "BASS_CONFIG_DEV_BUFFER length in milliseconds");
CONVAR(snd_dev_period, 10, CLIENT, "BASS_CONFIG_DEV_PERIOD length in milliseconds, or if negative then in samples");
CONVAR(snd_output_device, "Default"sv, CLIENT);
CONVAR(snd_ready_delay, 0.f, CLIENT, "after a sound engine restart, wait this many seconds before marking it as ready");
CONVAR(snd_restrict_play_frame, true, CLIENT,
       "only allow one new channel per frame for overlayable sounds (prevents lag and earrape)");
CONVAR(snd_updateperiod, 10, CLIENT, "BASS_CONFIG_UPDATEPERIOD length in milliseconds");
CONVAR(snd_file_min_size, 51, CLIENT,
       "minimum file size in bytes for WAV files to be considered valid (everything below will "
       "fail to load), this is a workaround for BASS crashes");
CONVAR(snd_force_load_unknown, false, CLIENT, "force loading of assumed invalid audio files");
CONVAR(snd_freq, 44100, CLIENT | NOSAVE, "output sampling rate in Hz");
CONVAR(snd_soloud_buffer, 0, CLIENT | NOSAVE, "SoLoud audio device buffer size (recommended to leave this on 0/auto)");
CONVAR(snd_soloud_backend, "MiniAudio"sv, CLIENT, R"(SoLoud backend, "MiniAudio" or "SDL3" (MiniAudio is default))");
CONVAR(snd_soloud_offset_compensation_strategy, 1, CLIENT,
       R"(For debugging: 0 = naive (no auto offset), 1 and 2 are slightly different WSOLA pipeline model variants)");

CONVAR(snd_sanity_simultaneous_limit, 128, CLIENT | NOSAVE,
       "The maximum number of overlayable sounds that are allowed to be active at once");
CONVAR(snd_soloud_resampler, "linear", CLIENT,
       "resampler to use. \"point\", \"linear\", or \"catmull-rom\" (in order of increasing quality/cpu usage)");
CONVAR(snd_soloud_prefer_ffmpeg, 0, CLIENT,
       "(0=no, 1=streams, 2=streams+samples) prioritize using ffmpeg as a decoder (if available) over other decoder "
       "backends");
CONVAR(snd_soloud_prefer_exclusive, false, CLIENT, "try initializing in exclusive mode first for MiniAudio on Windows");
CONVAR(snd_rate_transpose_algorithm, "cubic", CLIENT,
       "rate changing algorithm to use. \"linear\", \"cubic\", or \"shannon\" (in order of increasing "
       "quality/cpu usage)");
CONVAR(snd_disable_exclusive_unfocused, true, CLIENT,
       "disable WASAPI exclusive mode when losing focus (currently SoLoud+MiniAudio only)");
CONVAR(snd_speed_compensate_pitch, true, CLIENT, "automatically keep pitch constant if speed changes");
CONVAR(volume_change_interval, 0.05f, CLIENT | SKINS | SERVER);
CONVAR(volume_effects, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(volume_master, 0.7f, CLIENT | SKINS | SERVER);
CONVAR(volume_master_inactive, 0.25f, CLIENT | SKINS | SERVER);
CONVAR(volume_music, 0.4f, CLIENT | SKINS | SERVER);
CONVAR(win_snd_wasapi_buffer_size, 0.011f, CLIENT,
       "buffer size/length in seconds (e.g. 0.011 = 11 ms), directly responsible for audio delay and crackling");
CONVAR(win_snd_wasapi_exclusive, true, CLIENT);
CONVAR(win_snd_wasapi_period_size, 0.0f, CLIENT,
       "interval between OutputWasapiProc calls in seconds (e.g. 0.016 = 16 ms) (0 = use default)");
CONVAR(win_snd_wasapi_event_callbacks, false, CLIENT,
       "wait for WASAPI to ask for data instead of filling a buffer, potentially lower latency (ignores period/buffer "
       "size)");  // convar for testing

// Debug
CONVAR(debug_cv, false, CLIENT);
CONVAR(debug_network, false, CLIENT);
CONVAR(debug_anim, false, CLIENT, CFUNC(AnimationHandler::onDebugAnimChange));
CONVAR(debug_box_shadows, false, CLIENT);
CONVAR(debug_engine, false, CLIENT);
CONVAR(debug_ui, false, CLIENT, CFUNC(CBaseUIDebug::onDumpElemsChangeCallback));
CONVAR(debug_env, false, CLIENT);
CONVAR(debug_font, false, CLIENT);
CONVAR(debug_file, false, CLIENT);
CONVAR(debug_image, false, CLIENT | NOSAVE);
CONVAR(debug_mouse, false, CLIENT | SERVER | PROTECTED | GAMEPLAY);
CONVAR(debug_draw_hardware_cursor, false, CLIENT | NOSAVE);
CONVAR(debug_rm, false, CLIENT);
CONVAR(debug_rt, false, CLIENT | SERVER | PROTECTED | GAMEPLAY,
       "draws all rendertargets with a translucent green background");
CONVAR(debug_shaders, false, CLIENT | PROTECTED | GAMEPLAY);
CONVAR(debug_vprof, false, CLIENT | SERVER);
CONVAR(debug_opengl, false, CLIENT | PROTECTED | GAMEPLAY);
CONVAR(debug_snd, false, CLIENT | NOSAVE);
CONVAR(r_3dscene_zf, 5000.0f, CLIENT | PROTECTED | GAMEPLAY);
CONVAR(r_3dscene_zn, 5.0f, CLIENT | PROTECTED | GAMEPLAY);
CONVAR(r_debug_disable_3dscene, false, CLIENT | PROTECTED | GAMEPLAY);
CONVAR(r_debug_disable_cliprect, false, CLIENT | PROTECTED | GAMEPLAY);
CONVAR(r_debug_drawimage, false, CLIENT | PROTECTED | GAMEPLAY);
CONVAR(r_debug_flush_drawstring, false, CLIENT);
CONVAR(r_debug_drawstring_unbind, false, CLIENT);
CONVAR(r_debug_font_unicode, false, CLIENT, "debug messages for unicode/fallback font related stuff");
CONVAR(r_sync_timeout, 5000000, CLIENT, "timeout in microseconds for GPU synchronization operations");
CONVAR(r_sync_enabled, true, CLIENT, "enable explicit GPU synchronization for OpenGL");
CONVAR(r_disable_driver_threaded_opts, true, CLIENT,
       "force disable driver-enforced threaded optimizations (they increase framerate, but also increase latency)");
CONVAR(r_opengl_legacy_vao_use_vertex_array, Env::cfg(REND::GLES32) ? true : false, CLIENT,
       "dramatically reduces per-vao draw calls, but completely breaks legacy ffp draw calls (vertices work, but "
       "texcoords/normals/etc. are NOT in gl_MultiTexCoord0 -> requiring a shader with attributes)");
CONVAR(font_load_system, true, CLIENT, "try to load a similar system font if a glyph is missing in the bundled fonts");
CONVAR(r_gl_image_unbind, false, CLIENT);
CONVAR(r_gles_orphan_buffers, Env::cfg(OS::WASM) ? false : true, CLIENT,  // destroys WASM perf for some reason
       "reduce cpu/gpu synchronization by freeing buffer objects before modifying them");
CONVAR(r_gl_rt_unbind, false, CLIENT);
CONVAR(r_globaloffset_x, 0.0f, CLIENT | PROTECTED | GAMEPLAY);
CONVAR(r_globaloffset_y, 0.0f, CLIENT | PROTECTED | GAMEPLAY);
CONVAR(r_sync_debug, false, CLIENT | HIDDEN, "print debug information about sync objects");
CONVAR(vprof, false, CLIENT | SERVER, "enables/disables the visual profiler", CFUNC(Profiling::vprofToggleCB));
CONVAR(vprof_display_mode, 0, CLIENT | SERVER,
       "which info blade to show on the top right (gpu/engine/app/etc. info), use CTRL + TAB to "
       "cycle through, 0 = disabled");
CONVAR(vprof_graph, true, CLIENT | SERVER, "whether to draw the graph when the overlay is enabled");
CONVAR(vprof_graph_alpha, 0.9f, CLIENT | SERVER, "line opacity");
CONVAR(vprof_graph_draw_overhead, false, CLIENT | SERVER,
       "whether to draw the profiling overhead time in white (usually negligible)");
CONVAR(vprof_graph_height, 250.0f, CLIENT | SERVER);
CONVAR(vprof_graph_margin, 40.0f, CLIENT | SERVER);
CONVAR(vprof_graph_range_max, 16.666666f, CLIENT | SERVER, "max value of the y-axis in milliseconds");
CONVAR(vprof_graph_width, 800.0f, CLIENT | SERVER);
CONVAR(vprof_spike, 0, CLIENT | SERVER,
       "measure and display largest spike details (1 = small info, 2 = extended info)");

// Display settings
CONVAR(fps_max, 1000.0f, CLIENT, "framerate limiter, gameplay");
CONVAR(fps_max_menu, 420.f, CLIENT, "framerate limiter, menus");
CONVAR(fps_max_background, 30.0f, CLIENT, "framerate limiter, background");
CONVAR(fps_max_yield, false, CLIENT, "always release rest of timeslice once per frame (call scheduler via sleep(0))");
CONVAR(fps_limiter_nobusywait, false, CLIENT, "only use 1ms sleeps to reach the FPS target, without busywaiting");
// fps_unlimited: Unused since v39.01. Instead we just check if fps_max <= 0 (see MainMenu.cpp for migration)
CONVAR(fps_unlimited, false, CLIENT | HIDDEN | NOSAVE);
CONVAR(
    fps_unlimited_yield, true, CLIENT,
    "always release rest of timeslice once per frame (call scheduler via sleep(0)), even if unlimited fps are enabled");
CONVAR(fullscreen_windowed_borderless, false, CLIENT);
CONVAR(fullscreen, false, CLIENT, [](float /*newValue*/) -> void { engine ? engine->toggleFullscreen() : (void)0; });
CONVAR(monitor, 0, CLIENT, "monitor/display device to switch to, 0 = primary monitor");
CONVAR(r_sync_max_frames, 1, CLIENT,
       "maximum pre-rendered frames allowed in rendering pipeline");  // (a la "Max Prerendered Frames")
CONVAR(alt_sleep, 1, CLIENT,
       "use an alternative sleep implementation (on Windows) for potentially more accurate frame limiting");

// Constants
CONVAR(version, PACKAGE_VERSION, CONSTANT);

// Sanity checks
CONVAR(r_drawstring_max_string_length, 16384, CLIENT,
       "maximum number of characters per call, sanity/memory buffer limit");

// Not sorted
CONVAR(console_logging, true, CLIENT | SKINS | SERVER);
CONVAR(console_overlay, false, CLIENT | SKINS | SERVER,
       "should the log overlay always be visible (or only if the console is out)");
CONVAR(console_overlay_lines, 12, CLIENT | SKINS | SERVER, "max number of lines of text");
CONVAR(console_overlay_scale, 1.0f, CLIENT | SKINS | SERVER, "log text size multiplier");
CONVAR(console_overlay_timeout, 8.0f, CLIENT | SKINS | SERVER,
       "how long to wait before fading out visible console log lines (0 = never fade out)");
CONVAR(consolebox_animspeed, 12.0f, CLIENT | SKINS | SERVER);
CONVAR(consolebox_draw_helptext, true, CLIENT | SKINS | SERVER, "whether convar suggestions also draw their helptext");
CONVAR(consolebox_draw_preview, true, CLIENT | SKINS | SERVER,
       "whether the textbox shows the topmost suggestion while typing");
CONVAR(engine_throttle, true, CLIENT | SKINS | SERVER,
       "limit some engine component updates to improve performance (non-gameplay-related, only turn this off if you "
       "like lower performance for no reason)");
CONVAR(file_size_max, 1024, CLIENT | SKINS | SERVER,
       "maximum filesize sanity limit in MB, all files bigger than this are not allowed to load");
CONVAR(interpolate_music_pos, 2L, CLIENT | SKINS | SERVER,
       "interpolate song position with engine time (0 = none, 1 = new method, 2 = McOsu, 3 = \"lazer\" (broken?))");
CONVAR(minimize_on_focus_lost_if_borderless_windowed_fullscreen, false, CLIENT | SKINS | SERVER);
CONVAR(minimize_on_focus_lost_if_fullscreen, true, CLIENT | SKINS | SERVER);
CONVAR(mouse_raw_input, false, CLIENT | SKINS | SERVER);
CONVAR(keyboard_raw_input, false, CLIENT | SKINS | SERVER,
       "listen to keyboard input on a separate thread (Windows only)");
CONVAR(mouse_sensitivity, 1.0f, CLIENT | SKINS | SERVER);
CONVAR(pen_input, true, CLIENT | SKINS | SERVER, "support OTD Artist Mode and native tablet drivers' pen events");
CONVAR(rich_presence, true, CLIENT | SKINS | SERVER);  // callback set in DiscordInterface
CONVAR(ssl_verify, true, CLIENT);
CONVAR(use_https, true, CLIENT);
CONVAR(ui_scrollview_kinetic_approach_time, 0.075f, CLIENT | SKINS | SERVER,
       "approach target afterscroll delta over this duration");
CONVAR(ui_scrollview_kinetic_energy_multiplier, 24.0f, CLIENT, "afterscroll delta multiplier");
CONVAR(ui_scrollview_mousewheel_multiplier, 3.5f, CLIENT | SKINS | SERVER);
CONVAR(ui_scrollview_mousewheel_overscrollbounce, true, CLIENT);
CONVAR(ui_scrollview_resistance, 5.0f, CLIENT | SKINS | SERVER,
       "how many pixels you have to pull before you start scrolling");
CONVAR(ui_scrollview_scrollbarwidth, 15.0f, CLIENT | SKINS | SERVER);
CONVAR(ui_textbox_caret_blink_time, 0.5f, CLIENT | SKINS | SERVER);
CONVAR(ui_textbox_text_offset_x, 3, CLIENT | SKINS | SERVER);
CONVAR(use_ime, true, CLIENT, "enable the use of the OS IME window for editing text");
CONVAR(ui_window_animspeed, 0.29f, CLIENT | SKINS | SERVER);
CONVAR(vsync, false, CLIENT);  // callback set in Graphics.cpp
CONVAR(archive_threads, 1, CLIENT, "default number of threads to use for compressing archives");
// this is not windows-only anymore, just keeping it with the "win_" prefix to not break old configs
CONVAR(win_processpriority, (McThread::Priority)1, CLIENT, "sets the main process priority (0 = normal, 1 = high)",
       [](float newFloat) -> void { McThread::set_current_thread_prio((McThread::Priority)(int)newFloat); });

// NOLINTEND(misc-definitions-in-headers)

}  // namespace cv

#endif
