#pragma once
// Copyright (c) 2015, PG, 2024-2025, kiwec, 2025-2026, WH, All rights reserved.
#include "noinclude.h"
#include "Color.h"
#include "Vectors.h"
#include "Image.h"

#include <array>
#include <vector>

class Image;
class Sound;
class Resource;
class ConVar;
class UString;

class SkinImage;

enum class ScoreGrade : uint8_t;

extern Image *MISSING_TEXTURE;

namespace Replay {
struct Mods;
}
enum class LegacyFlags : u32;

// for lazy-loading "is @2x" checks, for non-animated skin images (which belong to the SkinImage class)
struct BasicSkinImage {
    BasicSkinImage() = default;
    BasicSkinImage(Image *img) : img(img) {}

    Image *img{MISSING_TEXTURE};

    [[nodiscard]] float scale() const;

    inline Image *operator->() const noexcept { return img; }
    inline operator Image *() const noexcept { return img; }
    inline explicit operator bool() const noexcept { return !!img; }
    inline bool operator==(Image *other) const noexcept { return img == other; }

   private:
    mutable i8 scale_mul{-1};
};

struct SkinIniOptions {};

struct Skin final {
   private:
    NOCOPY_NOMOVE(Skin)

    // custom
    void randomizeFilePath();

    bool parseSkinINI(std::string filepath);
    void parseFallbackPrefixes(const std::string &iniPath);
    void fixupPrefix(std::string &prefix, const std::string &baseDir);

    SkinImage *createSkinImage(const std::string &skinElementName, vec2 baseSizeForScaling2x, f32 osuSize,
                               bool ignoreDefaultSkin = false, const std::string &animationSeparator = "-");
    void checkLoadImage(BasicSkinImage &imgRef, const std::string &skinElementName, const std::string &resourceName,
                        bool ignoreDefaultSkin = false, const std::string &fileExtension = "png",
                        bool forceLoadMipmaps = false, const std::string &overrideDir = {});

    void loadSound(Sound *&sndRef, const std::string &skinElementName, const std::string &resourceName,
                   bool isOverlayable = false, bool isSample = false, bool loop = false,
                   bool fallback_to_default = true);

    void load();

   public:
    static bool unpack(const char *filepath);

    Skin(std::string name, std::string filepath, std::string fallbackDir = "");
    ~Skin();

    void update(bool isInPlayMode, bool isPlaying, i32 curMusicPos);

    [[nodiscard]] bool isReady() const;

    void loadBeatmapOverride(const std::string &filepath);
    void reloadSounds();

    // drawable helpers
    Color getComboColorForCounter(int i, int offset) const;
    inline void setBeatmapComboColors(std::vector<Color> colors) { this->c_beatmap_combo_colors = std::move(colors); }

    // these theoretically "should" match osu!stable mod image stacking order (by increasing bit position)
    static void getModImagesForMods(std::vector<SkinImage * Skin::*> &outVec, LegacyFlags flags);
    static void getModImagesForMods(std::vector<SkinImage * Skin::*> &outVec, const Replay::Mods &mods);

    inline static std::vector<SkinImage * Skin::*> getModImagesForMods(LegacyFlags flags) {
        std::vector<SkinImage * Skin::*> ret;
        getModImagesForMods(ret, flags);
        return ret;
    }
    inline static std::vector<SkinImage * Skin::*> getModImagesForMods(const Replay::Mods &mods) {
        std::vector<SkinImage * Skin::*> ret;
        getModImagesForMods(ret, mods);
        return ret;
    }

    [[nodiscard]] const BasicSkinImage &getGradeImageLarge(ScoreGrade grade) const;
    [[nodiscard]] const SkinImage *getGradeImageSmall(ScoreGrade grade) const;

    [[nodiscard]] inline bool useSmoothCursorTrail() const { return this->i_cursor_middle.img != MISSING_TEXTURE; }

    std::string name;           // the skin name by itself
    std::string skin_dir;       // fully qualified skin directory (e.g. /path/to/skins_directory/<name>/)
    std::string fallback_dir;   // optional fallback skin directory (between primary and default)
    std::string skin_ini_path;  // fully qualified skin.ini path (e.g. /path/to/skins_directory/<name>/skin.ini)

    // ordered list of directories to search for skin elements (first match wins)
    // typically: [user_skin_dir, default_skin_dir] or [user_skin_dir, fallback_skin_dir, default_skin_dir]
    // the default skin itself only searches its own dir
    std::vector<std::string> search_dirs;

    std::vector<Resource *> resources;
    std::vector<Sound *> sounds;
    std::vector<SkinImage *> images;

    // images
    BasicSkinImage i_hitcircle{};
    SkinImage *i_hitcircleoverlay{nullptr};
    BasicSkinImage i_approachcircle{};
    BasicSkinImage i_reversearrow{};
    SkinImage *i_followpoint{nullptr};

    std::array<BasicSkinImage, 10> i_defaults{};

    std::array<BasicSkinImage, 10> i_scores{};
    BasicSkinImage i_score_x{};
    BasicSkinImage i_score_percent{};
    BasicSkinImage i_score_dot{};

    std::array<BasicSkinImage, 10> i_combos{};
    BasicSkinImage i_combo_x{};

    SkinImage *i_play_skip{nullptr};
    BasicSkinImage i_play_warning_arrow{};
    SkinImage *i_play_warning_arrow2{nullptr};
    BasicSkinImage i_circular_metre{};
    SkinImage *i_scorebar_bg{nullptr};
    SkinImage *i_scorebar_colour{nullptr};
    SkinImage *i_scorebar_marker{nullptr};
    SkinImage *i_scorebar_ki{nullptr};
    SkinImage *i_scorebad_ki_danger{nullptr};
    SkinImage *i_scorebar_ki_danger2{nullptr};
    SkinImage *i_section_pass{nullptr};
    SkinImage *i_section_fail{nullptr};
    SkinImage *i_input_overlay_bg{nullptr};
    SkinImage *i_input_overlay_key{nullptr};

    SkinImage *i_hit0{nullptr};
    SkinImage *i_hit50{nullptr};
    SkinImage *i_hit50g{nullptr};
    SkinImage *i_hit50k{nullptr};
    SkinImage *i_hit100{nullptr};
    SkinImage *i_hit100g{nullptr};
    SkinImage *i_hit100k{nullptr};
    SkinImage *i_hit300{nullptr};
    SkinImage *i_hit300g{nullptr};
    SkinImage *i_hit300k{nullptr};

    BasicSkinImage i_particle50{};
    BasicSkinImage i_particle100{};
    BasicSkinImage i_particle300{};

    BasicSkinImage i_slider_gradient{};
    SkinImage *i_sliderb{nullptr};
    SkinImage *i_slider_follow_circle{nullptr};
    BasicSkinImage i_slider_score_point{};
    BasicSkinImage i_slider_start_circle{};
    SkinImage *i_slider_start_circle2{nullptr};
    BasicSkinImage i_slider_start_circle_overlay{};
    SkinImage *i_slider_start_circle_overlay2{nullptr};
    BasicSkinImage i_slider_end_circle{};
    SkinImage *i_slider_end_circle2{nullptr};
    BasicSkinImage i_slider_end_circle_overlay{};
    SkinImage *i_slider_end_circle_overlay2{nullptr};

    BasicSkinImage i_spinner_approach_circle{};
    BasicSkinImage i_spinner_bg{};
    BasicSkinImage i_spinner_circle{};
    BasicSkinImage i_spinner_clear{};
    BasicSkinImage i_spinner_bottom{};
    BasicSkinImage i_spinner_glow{};
    BasicSkinImage i_spinner_metre{};
    BasicSkinImage i_spinner_middle{};
    BasicSkinImage i_spinner_middle2{};
    BasicSkinImage i_spinner_osu{};
    BasicSkinImage i_spinner_top{};
    BasicSkinImage i_spinner_rpm{};
    BasicSkinImage i_spinner_spin{};

    BasicSkinImage i_cursor{};
    BasicSkinImage i_cursor_default{};
    BasicSkinImage i_cursor_middle{};
    BasicSkinImage i_cursor_trail{};
    BasicSkinImage i_cursor_ripple{};
    BasicSkinImage i_cursor_smoke{};

    SkinImage *i_modselect_ez{nullptr};
    SkinImage *i_modselect_nf{nullptr};
    SkinImage *i_modselect_ht{nullptr};
    SkinImage *i_modselect_dc{nullptr};
    SkinImage *i_modselect_hr{nullptr};
    SkinImage *i_modselect_sd{nullptr};
    SkinImage *i_modselect_pf{nullptr};
    SkinImage *i_modselect_dt{nullptr};
    SkinImage *i_modselect_nc{nullptr};
    SkinImage *i_modselect_hd{nullptr};
    SkinImage *i_modselect_fl{nullptr};
    SkinImage *i_modselect_rx{nullptr};
    SkinImage *i_modselect_ap{nullptr};
    SkinImage *i_modselect_so{nullptr};
    SkinImage *i_modselect_auto{nullptr};
    SkinImage *i_modselect_nightmare{nullptr};
    SkinImage *i_modselect_target{nullptr};
    SkinImage *i_modselect_sv2{nullptr};
    SkinImage *i_modselect_td{nullptr};
    SkinImage *i_modselect_cinema{nullptr};

    SkinImage *i_mode_osu{nullptr};
    SkinImage *i_mode_osu_small{nullptr};

    BasicSkinImage i_pause_continue{};
    BasicSkinImage i_pause_replay{};
    BasicSkinImage i_pause_retry{};
    BasicSkinImage i_pause_back{};
    BasicSkinImage i_pause_overlay{};
    BasicSkinImage i_fail_bg{};
    BasicSkinImage i_unpause{};

    BasicSkinImage i_button_left{};
    BasicSkinImage i_button_mid{};
    BasicSkinImage i_button_right{};
    BasicSkinImage i_button_left_default{};
    BasicSkinImage i_button_mid_default{};
    BasicSkinImage i_button_right_default{};
    SkinImage *i_menu_back2_DEFAULTSKIN{nullptr};
    SkinImage *i_menu_back2{nullptr};
    SkinImage *i_sel_mode{nullptr};
    SkinImage *i_sel_mode_over{nullptr};
    SkinImage *i_sel_mods{nullptr};
    SkinImage *i_sel_mods_over{nullptr};
    SkinImage *i_sel_random{nullptr};
    SkinImage *i_sel_random_over{nullptr};
    SkinImage *i_sel_options{nullptr};
    SkinImage *i_sel_options_over{nullptr};

    BasicSkinImage i_songselect_top{};
    BasicSkinImage i_songselect_bot{};
    BasicSkinImage i_menu_button_bg{};
    SkinImage *i_menu_button_bg2{nullptr};
    BasicSkinImage i_star{};
    BasicSkinImage i_ranking_panel{};
    BasicSkinImage i_ranking_graph{};
    BasicSkinImage i_ranking_title{};
    BasicSkinImage i_ranking_max_combo{};
    BasicSkinImage i_ranking_accuracy{};
    BasicSkinImage i_ranking_a{};
    BasicSkinImage i_ranking_b{};
    BasicSkinImage i_ranking_c{};
    BasicSkinImage i_ranking_d{};
    BasicSkinImage i_ranking_s{};
    BasicSkinImage i_ranking_sh{};
    BasicSkinImage i_ranking_x{};
    BasicSkinImage i_ranking_xh{};
    SkinImage *i_ranking_a_small{nullptr};
    SkinImage *i_ranking_b_small{nullptr};
    SkinImage *i_ranking_c_small{nullptr};
    SkinImage *i_ranking_d_small{nullptr};
    SkinImage *i_ranking_s_small{nullptr};
    SkinImage *i_ranking_sh_small{nullptr};
    SkinImage *i_ranking_x_small{nullptr};
    SkinImage *i_ranking_xh_small{nullptr};
    SkinImage *i_ranking_perfect{nullptr};

    BasicSkinImage i_beatmap_import_spinner{};
    BasicSkinImage i_loading_spinner{};
    BasicSkinImage i_circle_empty{};
    BasicSkinImage i_circle_full{};
    BasicSkinImage i_seek_triangle{};
    BasicSkinImage i_user_icon{};
    BasicSkinImage i_background_cube{};
    BasicSkinImage i_menu_bg{};
    BasicSkinImage i_skybox{};

    // sounds
    Sound *s_normal_hitnormal{nullptr};
    Sound *s_normal_hitwhistle{nullptr};
    Sound *s_normal_hitfinish{nullptr};
    Sound *s_normal_hitclap{nullptr};

    Sound *s_normal_slidertick{nullptr};
    Sound *s_normal_sliderslide{nullptr};
    Sound *s_normal_sliderwhistle{nullptr};

    Sound *s_soft_hitnormal{nullptr};
    Sound *s_soft_hitwhistle{nullptr};
    Sound *s_soft_hitfinish{nullptr};
    Sound *s_soft_hitclap{nullptr};

    Sound *s_soft_slidertick{nullptr};
    Sound *s_soft_sliderslide{nullptr};
    Sound *s_soft_sliderwhistle{nullptr};

    Sound *s_drum_hitnormal{nullptr};
    Sound *s_drum_hitwhistle{nullptr};
    Sound *s_drum_hitfinish{nullptr};
    Sound *s_drum_hitclap{nullptr};

    Sound *s_drum_slidertick{nullptr};
    Sound *s_drum_sliderslide{nullptr};
    Sound *s_drum_sliderwhistle{nullptr};

    Sound *s_spinner_bonus{nullptr};
    Sound *s_spinner_spin{nullptr};

    // Plays when sending a message in chat
    Sound *s_message_sent{nullptr};

    // Plays when deleting text in a message in chat
    Sound *s_deleting_text{nullptr};

    // Plays when changing the text cursor position
    Sound *s_moving_text_cursor{nullptr};

    // Plays when pressing a key for chat, search, edit, etc
    Sound *s_typing1{nullptr};
    Sound *s_typing2{nullptr};
    Sound *s_typing3{nullptr};
    Sound *s_typing4{nullptr};

    // Plays when returning to the previous screen
    Sound *s_menu_back{nullptr};

    // Plays when closing a chat tab
    Sound *s_close_chat_tab{nullptr};

    // Plays when hovering above all selectable boxes except beatmaps or main screen buttons
    Sound *s_hover_button{nullptr};

    // Plays when clicking to confirm a button or dropdown option, opening or
    // closing chat, switching between chat tabs, or switching groups
    Sound *s_click_button{nullptr};

    // Main menu sounds
    Sound *s_click_main_menu_cube{nullptr};
    Sound *s_hover_main_menu_cube{nullptr};
    Sound *s_click_sp{nullptr};
    Sound *s_hover_sp{nullptr};
    Sound *s_click_mp{nullptr};
    Sound *s_hover_mp{nullptr};
    Sound *s_click_options{nullptr};
    Sound *s_hover_options{nullptr};
    Sound *s_click_exit{nullptr};
    Sound *s_hover_exit{nullptr};

    // Pause menu sounds
    Sound *s_pause_loop{nullptr};
    Sound *s_pause_hover{nullptr};
    Sound *s_click_pause_back{nullptr};
    Sound *s_hover_pause_back{nullptr};
    Sound *s_click_pause_continue{nullptr};
    Sound *s_hover_pause_continue{nullptr};
    Sound *s_click_pause_retry{nullptr};
    Sound *s_hover_pause_retry{nullptr};

    // Back button sounds
    Sound *s_click_back_button{nullptr};
    Sound *s_hover_back_button{nullptr};

    // Plays when switching into song selection, selecting a beatmap, opening dropdown boxes, opening chat tabs
    Sound *s_expand{nullptr};

    // Plays when selecting a difficulty of a beatmap
    Sound *s_select_difficulty{nullptr};

    // Plays when changing the options via a slider
    Sound *s_sliderbar{nullptr};

    // Multiplayer sounds
    Sound *s_match_confirm{nullptr};   // all players are ready
    Sound *s_room_joined{nullptr};     // a player joined
    Sound *s_room_quit{nullptr};       // a player left
    Sound *s_room_not_ready{nullptr};  // a player is no longer ready
    Sound *s_room_ready{nullptr};      // a player is now ready
    Sound *s_match_start{nullptr};     // match started

    Sound *s_combobreak{nullptr};
    Sound *s_fail{nullptr};
    Sound *s_applause{nullptr};
    Sound *s_menu_hit{nullptr};
    Sound *s_menu_hover{nullptr};
    Sound *s_check_on{nullptr};
    Sound *s_check_off{nullptr};
    Sound *s_shutter{nullptr};
    Sound *s_section_pass{nullptr};
    Sound *s_section_fail{nullptr};

    // colors
    Color c_spinner_approach_circle;
    Color c_spinner_bg;
    Color c_slider_border;
    Color c_slider_track_override;
    Color c_slider_ball;

    Color c_song_select_inactive_text;
    Color c_song_select_active_text;

    Color c_input_overlay_text;

    std::vector<Color> c_combo_colors;
    std::vector<Color> c_beatmap_combo_colors;

    // custom
    std::vector<std::string> filepaths_for_random_skin;
    std::vector<std::string> filepaths_for_export;

    std::string combo_prefix;
    std::string score_prefix;
    std::string hitcircle_prefix;

    // prefixes from the fallback skin's skin.ini (for per-directory prefix resolution)
    std::string fallback_combo_prefix;
    std::string fallback_score_prefix;
    std::string fallback_hitcircle_prefix;
    f32 anim_speed{1.f};

    // skin.ini
    f32 version{1.f};
    f32 anim_framerate{0.f};
    i32 slider_style{2};
    i32 combo_overlap_amt{0};
    i32 score_overlap_amt{0};
    i32 hitcircle_overlap_amt{0};

    bool o_cursor_centered{true};
    bool o_cursor_rotate{true};
    bool o_cursor_expand{true};
    bool o_layered_hitsounds{
        true};  // when true, hitnormal sounds must always be played regardless of map hitsound flags
    bool o_spinner_fade_playfield{false};     // Should the spinner add black bars during spins
    bool o_spinner_frequency_modulate{true};  // Should the spinnerspin sound pitch up the longer the spinner goes
    bool o_spinner_no_blink{false};           // Should the highest bar of the metre stay visible all the time

    bool o_sliderball_flip{true};
    bool o_allow_sliderball_tint{false};
    bool o_hitcircle_overlay_above_number{true};
    bool o_slider_track_overridden{false};

    bool o_random;
    bool o_random_elements;

    bool is_ready{false};
    bool is_default;
};
