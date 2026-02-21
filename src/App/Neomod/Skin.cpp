// Copyright (c) 2015, PG, 2024-2025, kiwec, 2025-2026, WH, All rights reserved.
#include "Skin.h"

#include "Archival.h"
#include "OsuConVars.h"
#include "Font.h"
#include "Sound.h"
#include "ConVarHandler.h"
#include "Engine.h"
#include "Environment.h"
#include "File.h"
#include "Database.h"
#include "NotificationOverlay.h"
#include "Parsing.h"
#include "ResourceManager.h"
#include "SString.h"
#include "SkinImage.h"
#include "SoundEngine.h"
#include "UI.h"
#include "Hashing.h"
#include "Logging.h"
#include "crypto.h"
#include "ContainerRanges.h"

#include <cstring>
#include <utility>

// Readability
// XXX: change loadSound() interface to use flags instead
#define NOT_OVERLAYABLE false
#define OVERLAYABLE true
#define STREAM false
#define SAMPLE true
#define NOT_LOOPING false
#define LOOPING true

float BasicSkinImage::scale() const {
    if(unlikely(this->scale_mul == -1)) {
        this->scale_mul = 1;
        std::string_view path;
        if(this->img && this->img != MISSING_TEXTURE &&
           // i don't think @2x jpeg are even supported in osu stable, but doesn't hurt to check here anyways...
           (path = this->img->getFilePath()).length() > 8 /* @2x.jpeg == 8 */) {
            path = path.substr(path.length() - 8);

            if(path.contains("@2x")) {
                this->scale_mul = 2;
            }
        }
    }

    return this->scale_mul;
}

bool Skin::unpack(const char *filepath) {
    auto skin_name = env->getFileNameFromFilePath(filepath);
    debugLog("Extracting {:s}...", skin_name.c_str());
    skin_name.erase(skin_name.size() - 4);  // remove .osk extension

    auto skin_root = fmt::format(NEOMOD_SKINS_PATH "/{}/", skin_name);

    std::unique_ptr<u8[]> fileBuffer;
    size_t fileSize{0};
    {
        File file(filepath);
        if(!file.canRead() || !(fileSize = file.getFileSize())) {
            debugLog("Failed to read skin file {:s}", filepath);
            return false;
        }
        fileBuffer = file.takeFileBuffer();
        // close the file here
    }

    Archive::Reader archive(fileBuffer.get(), fileSize);
    if(!archive.isValid()) {
        debugLog("Failed to open .osk file");
        return false;
    }

    auto entries = archive.getAllEntries();
    if(entries.empty()) {
        debugLog(".osk file is empty!");
        return false;
    }

    if(!env->directoryExists(skin_root)) {
        env->createDirectory(skin_root);
    }

    for(const auto &entry : entries) {
        if(entry.isDirectory()) continue;

        std::string filename = entry.getFilename();
        const auto folders = SString::split(filename, '/');
        std::string file_path = skin_root;

        for(const auto &folder : folders) {
            if(!env->directoryExists(file_path)) {
                env->createDirectory(file_path);
            }

            if(folder == "..") {
                // security check: skip files with path traversal attempts
                goto skip_file;
            } else {
                file_path.push_back('/');
                file_path.append(folder);
            }
        }

        if(!entry.extractToFile(file_path)) {
            debugLog("Failed to extract skin file {:s}", filename.c_str());
        }

    skip_file:;
        // when a file can't be extracted we just ignore it (as long as the archive is valid)
    }

    return true;
}

Skin::Skin(std::string name, std::string filepath, std::string fallbackDir) {
    this->name = std::move(name);
    this->skin_dir = std::move(filepath);
    this->fallback_dir = std::move(fallbackDir);
    this->is_default = (this->skin_dir == MCENGINE_IMAGES_PATH "/default/");

    // vars
    this->c_spinner_approach_circle = 0xffffffff;
    this->c_spinner_bg = rgb(100, 100, 100);  // https://osu.ppy.sh/wiki/en/Skinning/skin.ini#[colours]
    this->c_slider_border = 0xffffffff;
    this->c_slider_ball = 0xffffffff;  // NOTE: 0xff02aaff is a hardcoded special case for osu!'s default skin, but it
                                       // does not apply to user skins

    this->c_song_select_active_text = 0xff000000;
    this->c_song_select_inactive_text = 0xffffffff;
    this->c_input_overlay_text = 0xff000000;

    // custom
    this->o_random = cv::skin_random.getBool();
    this->o_random_elements = cv::skin_random_elements.getBool();

    // load all files
    this->load();
}

Skin::~Skin() {
    for(auto &resource : this->resources) {
        if(resource && resource != (Resource *)MISSING_TEXTURE) resourceManager->destroyResource(resource);
    }
    this->resources.clear();

    for(auto &image : this->images) {
        delete image;
    }
    this->images.clear();

    this->filepaths_for_export.clear();
    // sounds are managed by resourcemanager, not unloaded here
}

void Skin::update(bool isInPlayMode, bool isPlaying, i32 curMusicPos) {
    // tasks which have to be run after async loading finishes
    if(!this->is_ready && this->isReady()) {
        this->is_ready = true;
    }

    // shitty check to not animate while paused with hitobjects in background
    if(isInPlayMode && !isPlaying && !cv::skin_animation_force.getBool()) return;

    const bool useEngineTimeForAnimations = !isInPlayMode;
    for(auto *image : this->images) {
        image->update(this->anim_speed, useEngineTimeForAnimations, curMusicPos);
    }
}

bool Skin::isReady() const {
    if(this->is_ready) return true;

    // default skin sounds aren't added to the resources vector... so check explicitly for that
    for(const auto *sound : this->sounds) {
        if(resourceManager->isLoadingResource(sound)) return false;
    }

    for(const auto *resource : this->resources) {
        if(resourceManager->isLoadingResource(resource)) return false;
    }

    for(const auto *image : this->images) {
        if(!image->isReady()) return false;
    }

    // (ready is set in update())
    return true;
}

void Skin::load() {
    const std::string default_dir{MCENGINE_IMAGES_PATH "/default/"};

    // random skins
    {
        this->filepaths_for_random_skin.clear();
        if(this->o_random || this->o_random_elements) {
            std::vector<std::string> skinNames;

            // regular skins
            {
                std::string skinFolder = cv::osu_folder.getString();
                skinFolder.append(cv::osu_folder_sub_skins.getString());
                std::vector<std::string> skinFolders = env->getFoldersInFolder(skinFolder);

                for(const auto &i : skinFolders) {
                    std::string randomSkinFolder = skinFolder;
                    randomSkinFolder.append(i);
                    randomSkinFolder.append("/");

                    this->filepaths_for_random_skin.push_back(randomSkinFolder);
                    skinNames.push_back(i);
                }
            }

            if(this->o_random && this->filepaths_for_random_skin.size() > 0) {
                const int randomIndex =
                    (int)(prand() % std::min(this->filepaths_for_random_skin.size(), skinNames.size()));

                this->name = skinNames[randomIndex];
                this->skin_dir = this->filepaths_for_random_skin[randomIndex];
            }
        }
    }

    // build the search directory list: [primary, fallback?, default?]
    this->search_dirs.clear();
    this->search_dirs.push_back(this->skin_dir);
    if(!this->fallback_dir.empty() && this->fallback_dir != this->skin_dir && this->fallback_dir != default_dir) {
        this->search_dirs.push_back(this->fallback_dir);
    }
    // if we are not the neomod-provided default skin, add it as the third-tier fallback level
    if(!this->is_default) {
        this->search_dirs.push_back(default_dir);
    }

    // spinner loading has top priority in async
    this->randomizeFilePath();
    {
        this->checkLoadImage(this->i_loading_spinner, "loading-spinner", "SKIN_LOADING_SPINNER");
    }

    // and the cursor comes right after that
    this->randomizeFilePath();
    {
        this->checkLoadImage(this->i_cursor, "cursor", "SKIN_CURSOR");
        this->checkLoadImage(this->i_cursor_middle, "cursormiddle", "SKIN_CURSORMIDDLE", true);
        this->checkLoadImage(this->i_cursor_trail, "cursortrail", "SKIN_CURSORTRAIL");
        this->checkLoadImage(this->i_cursor_ripple, "cursor-ripple", "SKIN_CURSORRIPPLE");
        this->checkLoadImage(this->i_cursor_smoke, "cursor-smoke", "SKIN_CURSORSMOKE");

        // special case: if fallback to default cursor, do load cursorMiddle
        if(this->i_cursor.img == resourceManager->getImage("SKIN_CURSOR_DEFAULT"))
            this->checkLoadImage(this->i_cursor_middle, "cursormiddle", "SKIN_CURSORMIDDLE");
    }

    // skin ini
    this->randomizeFilePath();
    this->skin_ini_path = this->skin_dir + "skin.ini";

    bool parseSkinIni1Status = true;
    bool parseSkinIni2Status = true;
    cvars().resetSkinCvars();
    if(!this->parseSkinINI(this->skin_ini_path)) {
        parseSkinIni1Status = false;
        this->skin_ini_path = MCENGINE_IMAGES_PATH "/default/skin.ini";
        cvars().resetSkinCvars();
        parseSkinIni2Status = this->parseSkinINI(this->skin_ini_path);
    }

    // parse fallback skin's skin.ini for prefix settings
    if(!this->fallback_dir.empty()) {
        this->parseFallbackPrefixes(this->fallback_dir + "skin.ini");
    }

    // default values, if none were loaded
    if(this->c_combo_colors.size() == 0) {
        this->c_combo_colors.push_back(argb(255, 255, 192, 0));
        this->c_combo_colors.push_back(argb(255, 0, 202, 0));
        this->c_combo_colors.push_back(argb(255, 18, 124, 255));
        this->c_combo_colors.push_back(argb(255, 242, 24, 57));
    }

    // images
    this->randomizeFilePath();
    this->checkLoadImage(this->i_hitcircle, "hitcircle", "SKIN_HITCIRCLE");
    this->i_hitcircleoverlay = this->createSkinImage("hitcircleoverlay", vec2(128, 128), 64);
    this->i_hitcircleoverlay->setAnimationFramerate(2);

    this->randomizeFilePath();
    this->checkLoadImage(this->i_approachcircle, "approachcircle", "SKIN_APPROACHCIRCLE");
    this->randomizeFilePath();
    this->checkLoadImage(this->i_reversearrow, "reversearrow", "SKIN_REVERSEARROW");

    this->randomizeFilePath();
    this->i_followpoint = this->createSkinImage("followpoint", vec2(16, 22), 64);

    this->randomizeFilePath();
    {
        const std::string hitCirclePrefix = this->hitcircle_prefix.empty() ? "default" : this->hitcircle_prefix;
        const std::string &fbHitCirclePrefix = this->fallback_hitcircle_prefix;
        for(int i = 0; i < 10; i++) {
            const std::string resName = fmt::format("SKIN_DEFAULT{}", i);
            this->checkLoadImage(this->i_defaults[i], fmt::format("{}-{}", hitCirclePrefix, i), resName);
            // try fallback skin's prefix if it differs from primary
            if(this->i_defaults[i].img == MISSING_TEXTURE && !fbHitCirclePrefix.empty() &&
               fbHitCirclePrefix != hitCirclePrefix)
                this->checkLoadImage(this->i_defaults[i], fmt::format("{}-{}", fbHitCirclePrefix, i), resName);
            // special cases: fallback to default skin hitcircle numbers if the
            // defined prefix doesn't point to any valid files
            if(this->i_defaults[i].img == MISSING_TEXTURE)
                this->checkLoadImage(this->i_defaults[i], fmt::format("default-{}", i), resName);
        }
    }

    this->randomizeFilePath();
    {
        const std::string scorePrefix = this->score_prefix.empty() ? "score" : this->score_prefix;
        const std::string &fbScorePrefix = this->fallback_score_prefix;
        for(int i = 0; i < 10; i++) {
            const std::string resName = fmt::format("SKIN_SCORE{}", i);
            this->checkLoadImage(this->i_scores[i], fmt::format("{}-{}", scorePrefix, i), resName);
            // try fallback skin's prefix if it differs from primary
            if(this->i_scores[i].img == MISSING_TEXTURE && !fbScorePrefix.empty() && fbScorePrefix != scorePrefix)
                this->checkLoadImage(this->i_scores[i], fmt::format("{}-{}", fbScorePrefix, i), resName);
            // fallback logic
            if(this->i_scores[i].img == MISSING_TEXTURE)
                this->checkLoadImage(this->i_scores[i], fmt::format("score-{}", i), resName);
        }

        this->checkLoadImage(this->i_score_x, fmt::format("{}-x", scorePrefix), "SKIN_SCOREX");
        // if (this->scoreX == MISSING_TEXTURE) checkLoadImage(m_scoreX, "score-x", "SKIN_SCOREX"); // special
        // case: ScorePrefix'd skins don't get default fallbacks, instead missing extraneous things like the X are
        // simply not drawn
        this->checkLoadImage(this->i_score_percent, fmt::format("{}-percent", scorePrefix), "SKIN_SCOREPERCENT");
        this->checkLoadImage(this->i_score_dot, fmt::format("{}-dot", scorePrefix), "SKIN_SCOREDOT");
    }

    this->randomizeFilePath();
    {
        // yes, "score" is the default value for the combo prefix
        const std::string comboPrefix = this->combo_prefix.empty() ? "score" : this->combo_prefix;
        const std::string &fbComboPrefix = this->fallback_combo_prefix;
        for(int i = 0; i < 10; i++) {
            const std::string resName = fmt::format("SKIN_COMBO{}", i);
            this->checkLoadImage(this->i_combos[i], fmt::format("{}-{}", comboPrefix, i), resName);
            // try fallback skin's prefix if it differs from primary
            if(this->i_combos[i].img == MISSING_TEXTURE && !fbComboPrefix.empty() && fbComboPrefix != comboPrefix)
                this->checkLoadImage(this->i_combos[i], fmt::format("{}-{}", fbComboPrefix, i), resName);
            // fallback logic
            if(this->i_combos[i].img == MISSING_TEXTURE)
                this->checkLoadImage(this->i_combos[i], fmt::format("score-{}", i), resName);
        }

        // same special case as above for extras
        this->checkLoadImage(this->i_combo_x, fmt::format("{}-x", comboPrefix), "SKIN_COMBOX");
    }

    this->randomizeFilePath();
    this->i_play_skip = this->createSkinImage("play-skip", vec2(193, 147), 94);
    this->randomizeFilePath();
    this->checkLoadImage(this->i_play_warning_arrow, "play-warningarrow", "SKIN_PLAYWARNINGARROW");
    this->i_play_warning_arrow2 = this->createSkinImage("play-warningarrow", vec2(167, 129), 128);
    this->randomizeFilePath();
    this->checkLoadImage(this->i_circular_metre, "circularmetre", "SKIN_CIRCULARMETRE");
    this->randomizeFilePath();
    this->i_scorebar_bg = this->createSkinImage("scorebar-bg", vec2(695, 44), 27.5f);
    this->i_scorebar_colour = this->createSkinImage("scorebar-colour", vec2(645, 10), 6.25f);
    this->i_scorebar_marker = this->createSkinImage("scorebar-marker", vec2(24, 24), 15.0f, true);
    this->i_scorebar_ki = this->createSkinImage("scorebar-ki", vec2(116, 116), 72.0f);
    this->i_scorebad_ki_danger = this->createSkinImage("scorebar-kidanger", vec2(116, 116), 72.0f);
    this->i_scorebar_ki_danger2 = this->createSkinImage("scorebar-kidanger2", vec2(116, 116), 72.0f);
    this->randomizeFilePath();
    this->i_section_pass = this->createSkinImage("section-pass", vec2(650, 650), 400.0f);
    this->randomizeFilePath();
    this->i_section_fail = this->createSkinImage("section-fail", vec2(650, 650), 400.0f);
    this->randomizeFilePath();
    this->i_input_overlay_bg = this->createSkinImage("inputoverlay-background", vec2(193, 55), 34.25f);
    this->i_input_overlay_key = this->createSkinImage("inputoverlay-key", vec2(43, 46), 26.75f);

    this->randomizeFilePath();
    this->i_hit0 = this->createSkinImage("hit0", vec2(128, 128), 42);
    this->i_hit0->setAnimationFramerate(60);
    this->i_hit50 = this->createSkinImage("hit50", vec2(128, 128), 42);
    this->i_hit50->setAnimationFramerate(60);
    this->i_hit50g = this->createSkinImage("hit50g", vec2(128, 128), 42);
    this->i_hit50g->setAnimationFramerate(60);
    this->i_hit50k = this->createSkinImage("hit50k", vec2(128, 128), 42);
    this->i_hit50k->setAnimationFramerate(60);
    this->i_hit100 = this->createSkinImage("hit100", vec2(128, 128), 42);
    this->i_hit100->setAnimationFramerate(60);
    this->i_hit100g = this->createSkinImage("hit100g", vec2(128, 128), 42);
    this->i_hit100g->setAnimationFramerate(60);
    this->i_hit100k = this->createSkinImage("hit100k", vec2(128, 128), 42);
    this->i_hit100k->setAnimationFramerate(60);
    this->i_hit300 = this->createSkinImage("hit300", vec2(128, 128), 42);
    this->i_hit300->setAnimationFramerate(60);
    this->i_hit300g = this->createSkinImage("hit300g", vec2(128, 128), 42);
    this->i_hit300g->setAnimationFramerate(60);
    this->i_hit300k = this->createSkinImage("hit300k", vec2(128, 128), 42);
    this->i_hit300k->setAnimationFramerate(60);

    this->randomizeFilePath();
    this->checkLoadImage(this->i_particle50, "particle50", "SKIN_PARTICLE50", true);
    this->checkLoadImage(this->i_particle100, "particle100", "SKIN_PARTICLE100", true);
    this->checkLoadImage(this->i_particle300, "particle300", "SKIN_PARTICLE300", true);

    this->randomizeFilePath();
    this->checkLoadImage(this->i_slider_gradient, "slidergradient", "SKIN_SLIDERGRADIENT");
    this->randomizeFilePath();
    this->i_sliderb = this->createSkinImage("sliderb", vec2(128, 128), 64, false, "");
    this->i_sliderb->setAnimationFramerate(/*45.0f*/ 50.0f);
    this->randomizeFilePath();
    this->checkLoadImage(this->i_slider_score_point, "sliderscorepoint", "SKIN_SLIDERSCOREPOINT");
    this->randomizeFilePath();
    this->i_slider_follow_circle = this->createSkinImage("sliderfollowcircle", vec2(259, 259), 64);
    this->randomizeFilePath();
    this->checkLoadImage(
        this->i_slider_start_circle, "sliderstartcircle", "SKIN_SLIDERSTARTCIRCLE",
        !this->is_default);  // !m_bIsDefaultSkin ensures that default doesn't override user, in these special cases
    this->i_slider_start_circle2 = this->createSkinImage("sliderstartcircle", vec2(128, 128), 64, !this->is_default);
    this->checkLoadImage(this->i_slider_start_circle_overlay, "sliderstartcircleoverlay",
                         "SKIN_SLIDERSTARTCIRCLEOVERLAY", !this->is_default);
    this->i_slider_start_circle_overlay2 =
        this->createSkinImage("sliderstartcircleoverlay", vec2(128, 128), 64, !this->is_default);
    this->i_slider_start_circle_overlay2->setAnimationFramerate(2);
    this->randomizeFilePath();
    this->checkLoadImage(this->i_slider_end_circle, "sliderendcircle", "SKIN_SLIDERENDCIRCLE", !this->is_default);
    this->i_slider_end_circle2 = this->createSkinImage("sliderendcircle", vec2(128, 128), 64, !this->is_default);
    this->checkLoadImage(this->i_slider_end_circle_overlay, "sliderendcircleoverlay", "SKIN_SLIDERENDCIRCLEOVERLAY",
                         !this->is_default);
    this->i_slider_end_circle_overlay2 =
        this->createSkinImage("sliderendcircleoverlay", vec2(128, 128), 64, !this->is_default);
    this->i_slider_end_circle_overlay2->setAnimationFramerate(2);

    this->randomizeFilePath();
    this->checkLoadImage(this->i_spinner_bg, "spinner-background", "SKIN_SPINNERBACKGROUND");
    this->checkLoadImage(this->i_spinner_circle, "spinner-circle", "SKIN_SPINNERCIRCLE");
    this->checkLoadImage(this->i_spinner_approach_circle, "spinner-approachcircle", "SKIN_SPINNERAPPROACHCIRCLE");
    this->checkLoadImage(this->i_spinner_bottom, "spinner-bottom", "SKIN_SPINNERBOTTOM");
    this->checkLoadImage(this->i_spinner_middle, "spinner-middle", "SKIN_SPINNERMIDDLE");
    this->checkLoadImage(this->i_spinner_middle2, "spinner-middle2", "SKIN_SPINNERMIDDLE2");
    this->checkLoadImage(this->i_spinner_top, "spinner-top", "SKIN_SPINNERTOP");
    this->checkLoadImage(this->i_spinner_spin, "spinner-spin", "SKIN_SPINNERSPIN");
    this->checkLoadImage(this->i_spinner_clear, "spinner-clear", "SKIN_SPINNERCLEAR");
    this->checkLoadImage(this->i_spinner_metre, "spinner-metre", "SKIN_SPINNERMETRE");
    this->checkLoadImage(this->i_spinner_glow, "spinner-glow", "SKIN_SPINNERGLOW");  // TODO: use
    this->checkLoadImage(this->i_spinner_osu, "spinner-osu", "SKIN_SPINNEROSU");     // TODO: use
    this->checkLoadImage(this->i_spinner_rpm, "spinner-rpm", "SKIN_SPINNERRPM");     // TODO: use

    this->randomizeFilePath();
    this->i_modselect_ez = this->createSkinImage("selection-mod-easy", vec2(68, 66), 38);
    this->i_modselect_nf = this->createSkinImage("selection-mod-nofail", vec2(68, 66), 38);
    this->i_modselect_ht = this->createSkinImage("selection-mod-halftime", vec2(68, 66), 38);
    this->i_modselect_hr = this->createSkinImage("selection-mod-hardrock", vec2(68, 66), 38);
    this->i_modselect_sd = this->createSkinImage("selection-mod-suddendeath", vec2(68, 66), 38);
    this->i_modselect_pf = this->createSkinImage("selection-mod-perfect", vec2(68, 66), 38);
    this->i_modselect_dt = this->createSkinImage("selection-mod-doubletime", vec2(68, 66), 38);
    this->i_modselect_nc = this->createSkinImage("selection-mod-nightcore", vec2(68, 66), 38);
    this->i_modselect_dc = this->createSkinImage("selection-mod-daycore", vec2(68, 66), 38);
    this->i_modselect_hd = this->createSkinImage("selection-mod-hidden", vec2(68, 66), 38);
    this->i_modselect_fl = this->createSkinImage("selection-mod-flashlight", vec2(68, 66), 38);
    this->i_modselect_rx = this->createSkinImage("selection-mod-relax", vec2(68, 66), 38);
    this->i_modselect_ap = this->createSkinImage("selection-mod-relax2", vec2(68, 66), 38);
    this->i_modselect_so = this->createSkinImage("selection-mod-spunout", vec2(68, 66), 38);
    this->i_modselect_auto = this->createSkinImage("selection-mod-autoplay", vec2(68, 66), 38);
    this->i_modselect_nightmare = this->createSkinImage("selection-mod-nightmare", vec2(68, 66), 38);
    this->i_modselect_target = this->createSkinImage("selection-mod-target", vec2(68, 66), 38);
    this->i_modselect_sv2 = this->createSkinImage("selection-mod-scorev2", vec2(68, 66), 38);
    this->i_modselect_td = this->createSkinImage("selection-mod-touchdevice", vec2(68, 66), 38);
    this->i_modselect_cinema = this->createSkinImage("selection-mod-cinema", vec2(68, 66), 38);

    this->i_mode_osu = this->createSkinImage("mode-osu", vec2(32, 32), 32);
    this->i_mode_osu_small = this->createSkinImage("mode-osu-small", vec2(32, 32), 32);

    this->randomizeFilePath();
    this->checkLoadImage(this->i_pause_continue, "pause-continue", "SKIN_PAUSE_CONTINUE");
    this->checkLoadImage(this->i_pause_replay, "pause-replay", "SKIN_PAUSE_REPLAY");
    this->checkLoadImage(this->i_pause_retry, "pause-retry", "SKIN_PAUSE_RETRY");
    this->checkLoadImage(this->i_pause_back, "pause-back", "SKIN_PAUSE_BACK");
    this->checkLoadImage(this->i_pause_overlay, "pause-overlay", "SKIN_PAUSE_OVERLAY");
    if(this->i_pause_overlay.img == MISSING_TEXTURE)
        this->checkLoadImage(this->i_pause_overlay, "pause-overlay", "SKIN_PAUSE_OVERLAY", true, "jpg");
    this->checkLoadImage(this->i_fail_bg, "fail-background", "SKIN_FAIL_BACKGROUND");
    if(this->i_fail_bg.img == MISSING_TEXTURE)
        this->checkLoadImage(this->i_fail_bg, "fail-background", "SKIN_FAIL_BACKGROUND", true, "jpg");
    this->checkLoadImage(this->i_unpause, "unpause", "SKIN_UNPAUSE");

    this->randomizeFilePath();
    this->checkLoadImage(this->i_button_left, "button-left", "SKIN_BUTTON_LEFT");
    this->checkLoadImage(this->i_button_mid, "button-middle", "SKIN_BUTTON_MIDDLE");
    this->checkLoadImage(this->i_button_right, "button-right", "SKIN_BUTTON_RIGHT");
    this->randomizeFilePath();
    // always load default skin menu-back (to show in options menu)
    {
        std::string origdir = this->search_dirs[0];
        this->search_dirs[0] = MCENGINE_IMAGES_PATH "/default/";
        this->i_menu_back2_DEFAULTSKIN = this->createSkinImage("menu-back", vec2(225, 87), 54);
        this->search_dirs[0] = std::move(origdir);
    }
    this->i_menu_back2 = this->createSkinImage("menu-back", vec2(225, 87), 54);

    this->randomizeFilePath();

    // NOTE: scaling is ignored when drawing this specific element
    this->i_sel_mode = this->createSkinImage("selection-mode", vec2(90, 90), 38);

    this->i_sel_mode_over = this->createSkinImage("selection-mode-over", vec2(88, 90), 38);
    this->i_sel_mods = this->createSkinImage("selection-mods", vec2(74, 90), 38);
    this->i_sel_mods_over = this->createSkinImage("selection-mods-over", vec2(74, 90), 38);
    this->i_sel_random = this->createSkinImage("selection-random", vec2(74, 90), 38);
    this->i_sel_random_over = this->createSkinImage("selection-random-over", vec2(74, 90), 38);
    this->i_sel_options = this->createSkinImage("selection-options", vec2(74, 90), 38);
    this->i_sel_options_over = this->createSkinImage("selection-options-over", vec2(74, 90), 38);

    this->randomizeFilePath();
    this->checkLoadImage(this->i_songselect_top, "songselect-top", "SKIN_SONGSELECT_TOP");
    this->checkLoadImage(this->i_songselect_bot, "songselect-bottom", "SKIN_SONGSELECT_BOTTOM");
    this->randomizeFilePath();
    this->checkLoadImage(this->i_menu_button_bg, "menu-button-background", "SKIN_MENU_BUTTON_BACKGROUND");
    this->i_menu_button_bg2 = this->createSkinImage("menu-button-background", vec2(699, 103), 64.0f);
    this->randomizeFilePath();
    this->checkLoadImage(this->i_star, "star", "SKIN_STAR");

    this->randomizeFilePath();
    this->checkLoadImage(this->i_ranking_panel, "ranking-panel", "SKIN_RANKING_PANEL");
    this->checkLoadImage(this->i_ranking_graph, "ranking-graph", "SKIN_RANKING_GRAPH");
    this->checkLoadImage(this->i_ranking_title, "ranking-title", "SKIN_RANKING_TITLE");
    this->checkLoadImage(this->i_ranking_max_combo, "ranking-maxcombo", "SKIN_RANKING_MAXCOMBO");
    this->checkLoadImage(this->i_ranking_accuracy, "ranking-accuracy", "SKIN_RANKING_ACCURACY");

    this->checkLoadImage(this->i_ranking_a, "ranking-A", "SKIN_RANKING_A");
    this->checkLoadImage(this->i_ranking_b, "ranking-B", "SKIN_RANKING_B");
    this->checkLoadImage(this->i_ranking_c, "ranking-C", "SKIN_RANKING_C");
    this->checkLoadImage(this->i_ranking_d, "ranking-D", "SKIN_RANKING_D");
    this->checkLoadImage(this->i_ranking_s, "ranking-S", "SKIN_RANKING_S");
    this->checkLoadImage(this->i_ranking_sh, "ranking-SH", "SKIN_RANKING_SH");
    this->checkLoadImage(this->i_ranking_x, "ranking-X", "SKIN_RANKING_X");
    this->checkLoadImage(this->i_ranking_xh, "ranking-XH", "SKIN_RANKING_XH");

    this->i_ranking_a_small = this->createSkinImage("ranking-A-small", vec2(34, 40), 128);
    this->i_ranking_b_small = this->createSkinImage("ranking-B-small", vec2(34, 40), 128);
    this->i_ranking_c_small = this->createSkinImage("ranking-C-small", vec2(34, 40), 128);
    this->i_ranking_d_small = this->createSkinImage("ranking-D-small", vec2(34, 40), 128);
    this->i_ranking_s_small = this->createSkinImage("ranking-S-small", vec2(34, 40), 128);
    this->i_ranking_sh_small = this->createSkinImage("ranking-SH-small", vec2(34, 40), 128);
    this->i_ranking_x_small = this->createSkinImage("ranking-X-small", vec2(34, 40), 128);
    this->i_ranking_xh_small = this->createSkinImage("ranking-XH-small", vec2(34, 40), 128);

    this->i_ranking_perfect = this->createSkinImage("ranking-perfect", vec2(478, 150), 128);

    this->randomizeFilePath();
    this->checkLoadImage(this->i_beatmap_import_spinner, "beatmapimport-spinner", "SKIN_BEATMAP_IMPORT_SPINNER");
    this->checkLoadImage(this->i_circle_empty, "circle-empty", "SKIN_CIRCLE_EMPTY");
    this->checkLoadImage(this->i_circle_full, "circle-full", "SKIN_CIRCLE_FULL");
    this->randomizeFilePath();
    this->checkLoadImage(this->i_seek_triangle, "seektriangle", "SKIN_SEEKTRIANGLE");
    this->randomizeFilePath();
    this->checkLoadImage(this->i_user_icon, "user-icon", "SKIN_USER_ICON");
    this->randomizeFilePath();
    this->checkLoadImage(this->i_background_cube, "backgroundcube", "SKIN_FPOSU_BACKGROUNDCUBE", false, "png",
                         true);  // force mipmaps
    this->randomizeFilePath();
    this->checkLoadImage(this->i_menu_bg, "menu-background", "SKIN_MENU_BACKGROUND", false, "jpg");
    this->randomizeFilePath();
    this->checkLoadImage(this->i_skybox, "skybox", "SKIN_FPOSU_3D_SKYBOX");

    // slider ticks
    this->loadSound(this->s_normal_slidertick, "normal-slidertick", "SKIN_NORMALSLIDERTICK_SND",  //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                            //
    this->loadSound(this->s_soft_slidertick, "soft-slidertick", "SKIN_SOFTSLIDERTICK_SND",        //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                            //
    this->loadSound(this->s_drum_slidertick, "drum-slidertick", "SKIN_DRUMSLIDERTICK_SND",        //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                            //

    // silder slides
    this->loadSound(this->s_normal_sliderslide, "normal-sliderslide", "SKIN_NORMALSLIDERSLIDE_SND",  //
                    NOT_OVERLAYABLE, SAMPLE, LOOPING);                                               //
    this->loadSound(this->s_soft_sliderslide, "soft-sliderslide", "SKIN_SOFTSLIDERSLIDE_SND",        //
                    NOT_OVERLAYABLE, SAMPLE, LOOPING);                                               //
    this->loadSound(this->s_drum_sliderslide, "drum-sliderslide", "SKIN_DRUMSLIDERSLIDE_SND",        //
                    NOT_OVERLAYABLE, SAMPLE, LOOPING);                                               //

    // slider whistles
    this->loadSound(this->s_normal_sliderwhistle, "normal-sliderwhistle", "SKIN_NORMALSLIDERWHISTLE_SND",  //
                    NOT_OVERLAYABLE, SAMPLE, LOOPING);                                                     //
    this->loadSound(this->s_soft_sliderwhistle, "soft-sliderwhistle", "SKIN_SOFTSLIDERWHISTLE_SND",        //
                    NOT_OVERLAYABLE, SAMPLE, LOOPING);                                                     //
    this->loadSound(this->s_drum_sliderwhistle, "drum-sliderwhistle", "SKIN_DRUMSLIDERWHISTLE_SND",        //
                    NOT_OVERLAYABLE, SAMPLE, LOOPING);                                                     //

    // hitcircle
    this->loadSound(this->s_normal_hitnormal, "normal-hitnormal", "SKIN_NORMALHITNORMAL_SND",     //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                            //
    this->loadSound(this->s_soft_hitnormal, "soft-hitnormal", "SKIN_SOFTHITNORMAL_SND",           //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                            //
    this->loadSound(this->s_drum_hitnormal, "drum-hitnormal", "SKIN_DRUMHITNORMAL_SND",           //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                            //
    this->loadSound(this->s_normal_hitwhistle, "normal-hitwhistle", "SKIN_NORMALHITWHISTLE_SND",  //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                            //
    this->loadSound(this->s_soft_hitwhistle, "soft-hitwhistle", "SKIN_SOFTHITWHISTLE_SND",        //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                            //
    this->loadSound(this->s_drum_hitwhistle, "drum-hitwhistle", "SKIN_DRUMHITWHISTLE_SND",        //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                            //
    this->loadSound(this->s_normal_hitfinish, "normal-hitfinish", "SKIN_NORMALHITFINISH_SND",     //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                            //
    this->loadSound(this->s_soft_hitfinish, "soft-hitfinish", "SKIN_SOFTHITFINISH_SND",           //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                            //
    this->loadSound(this->s_drum_hitfinish, "drum-hitfinish", "SKIN_DRUMHITFINISH_SND",           //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                            //
    this->loadSound(this->s_normal_hitclap, "normal-hitclap", "SKIN_NORMALHITCLAP_SND",           //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                            //
    this->loadSound(this->s_soft_hitclap, "soft-hitclap", "SKIN_SOFTHITCLAP_SND",                 //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                            //
    this->loadSound(this->s_drum_hitclap, "drum-hitclap", "SKIN_DRUMHITCLAP_SND",                 //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                            //

    // spinner
    this->loadSound(this->s_spinner_bonus, "spinnerbonus", "SKIN_SPINNERBONUS_SND", OVERLAYABLE, SAMPLE, NOT_LOOPING);
    this->loadSound(this->s_spinner_spin, "spinnerspin", "SKIN_SPINNERSPIN_SND", NOT_OVERLAYABLE, SAMPLE, LOOPING);

    // others
    this->loadSound(this->s_combobreak, "combobreak", "SKIN_COMBOBREAK_SND", true, true);
    this->loadSound(this->s_fail, "failsound", "SKIN_FAILSOUND_SND");
    this->loadSound(this->s_applause, "applause", "SKIN_APPLAUSE_SND");
    this->loadSound(this->s_menu_hit, "menuhit", "SKIN_MENUHIT_SND", true, true);
    this->loadSound(this->s_menu_hover, "menuclick", "SKIN_MENUCLICK_SND", true, true);
    this->loadSound(this->s_check_on, "check-on", "SKIN_CHECKON_SND", true, true);
    this->loadSound(this->s_check_off, "check-off", "SKIN_CHECKOFF_SND", true, true);
    this->loadSound(this->s_shutter, "shutter", "SKIN_SHUTTER_SND", true, true);
    this->loadSound(this->s_section_pass, "sectionpass", "SKIN_SECTIONPASS_SND");
    this->loadSound(this->s_section_fail, "sectionfail", "SKIN_SECTIONFAIL_SND");

    // UI feedback
    this->loadSound(this->s_message_sent, "key-confirm", "SKIN_MESSAGE_SENT_SND", true, true, false);
    this->loadSound(this->s_deleting_text, "key-delete", "SKIN_DELETING_TEXT_SND", true, true, false);
    this->loadSound(this->s_moving_text_cursor, "key-movement", "MOVING_TEXT_CURSOR_SND", true, true, false);
    this->loadSound(this->s_typing1, "key-press-1", "TYPING_1_SND", true, true, false);
    this->loadSound(this->s_typing2, "key-press-2", "TYPING_2_SND", true, true, false, false);
    this->loadSound(this->s_typing3, "key-press-3", "TYPING_3_SND", true, true, false, false);
    this->loadSound(this->s_typing4, "key-press-4", "TYPING_4_SND", true, true, false, false);
    this->loadSound(this->s_menu_back, "menuback", "MENU_BACK_SND", true, true, false, false);
    this->loadSound(this->s_close_chat_tab, "click-close", "CLOSE_CHAT_TAB_SND", true, true, false, false);
    this->loadSound(this->s_click_button, "click-short-confirm", "CLICK_BUTTON_SND", true, true, false, false);
    this->loadSound(this->s_hover_button, "click-short", "HOVER_BUTTON_SND", true, true, false, false);
    this->loadSound(this->s_click_back_button, "back-button-click", "BACK_BUTTON_CLICK_SND", true, true, false, false);
    this->loadSound(this->s_hover_back_button, "back-button-hover", "BACK_BUTTON_HOVER_SND", true, true, false, false);
    this->loadSound(this->s_click_main_menu_cube, "menu-play-click", "CLICK_MAIN_MENU_CUBE_SND", true, true, false,
                    false);
    this->loadSound(this->s_hover_main_menu_cube, "menu-play-hover", "HOVER_MAIN_MENU_CUBE_SND", true, true, false,
                    false);
    this->loadSound(this->s_click_sp, "menu-freeplay-click", "CLICK_SINGLEPLAYER_SND", true, true, false, false);
    this->loadSound(this->s_hover_sp, "menu-freeplay-hover", "HOVER_SINGLEPLAYER_SND", true, true, false, false);
    this->loadSound(this->s_click_mp, "menu-multiplayer-click", "CLICK_MULTIPLAYER_SND", true, true, false, false);
    this->loadSound(this->s_hover_mp, "menu-multiplayer-hover", "HOVER_MULTIPLAYER_SND", true, true, false, false);
    this->loadSound(this->s_click_options, "menu-options-click", "CLICK_OPTIONS_SND", true, true, false, false);
    this->loadSound(this->s_hover_options, "menu-options-hover", "HOVER_OPTIONS_SND", true, true, false, false);
    this->loadSound(this->s_click_exit, "menu-exit-click", "CLICK_EXIT_SND", true, true, false, false);
    this->loadSound(this->s_hover_exit, "menu-exit-hover", "HOVER_EXIT_SND", true, true, false, false);
    this->loadSound(this->s_expand, "select-expand", "EXPAND_SND", true, true, false);
    this->loadSound(this->s_select_difficulty, "select-difficulty", "SELECT_DIFFICULTY_SND", true, true, false, false);
    this->loadSound(this->s_sliderbar, "sliderbar", "DRAG_SLIDER_SND", true, true, false);
    this->loadSound(this->s_match_confirm, "match-confirm", "ALL_PLAYERS_READY_SND", true, true, false);
    this->loadSound(this->s_room_joined, "match-join", "ROOM_JOINED_SND", true, true, false);
    this->loadSound(this->s_room_quit, "match-leave", "ROOM_QUIT_SND", true, true, false);
    this->loadSound(this->s_room_not_ready, "match-notready", "ROOM_NOT_READY_SND", true, true, false);
    this->loadSound(this->s_room_ready, "match-ready", "ROOM_READY_SND", true, true, false);
    this->loadSound(this->s_match_start, "match-start", "MATCH_START_SND", true, true, false);

    this->loadSound(this->s_pause_loop, "pause-loop", "PAUSE_LOOP_SND", NOT_OVERLAYABLE, STREAM, LOOPING, true);
    this->loadSound(this->s_pause_hover, "pause-hover", "PAUSE_HOVER_SND", OVERLAYABLE, SAMPLE, NOT_LOOPING, false);
    this->loadSound(this->s_click_pause_back, "pause-back-click", "CLICK_QUIT_SONG_SND", true, true, false, false);
    this->loadSound(this->s_hover_pause_back, "pause-back-hover", "HOVER_QUIT_SONG_SND", true, true, false, false);
    this->loadSound(this->s_click_pause_continue, "pause-continue-click", "CLICK_RESUME_SONG_SND", true, true, false,
                    false);
    this->loadSound(this->s_hover_pause_continue, "pause-continue-hover", "HOVER_RESUME_SONG_SND", true, true, false,
                    false);
    this->loadSound(this->s_click_pause_retry, "pause-retry-click", "CLICK_RETRY_SONG_SND", true, true, false, false);
    this->loadSound(this->s_hover_pause_retry, "pause-retry-hover", "HOVER_RETRY_SONG_SND", true, true, false, false);

    if(!this->s_click_button) this->s_click_button = this->s_menu_hit;
    if(!this->s_hover_button) this->s_hover_button = this->s_menu_hover;
    if(!this->s_pause_hover) this->s_pause_hover = this->s_hover_button;
    if(!this->s_select_difficulty) this->s_select_difficulty = this->s_click_button;
    if(!this->s_typing2) this->s_typing2 = this->s_typing1;
    if(!this->s_typing3) this->s_typing3 = this->s_typing2;
    if(!this->s_typing4) this->s_typing4 = this->s_typing3;
    if(!this->s_click_back_button) this->s_click_back_button = this->s_click_button;
    if(!this->s_hover_back_button) this->s_hover_back_button = this->s_hover_button;
    if(!this->s_menu_back) this->s_menu_back = this->s_click_button;
    if(!this->s_close_chat_tab) this->s_close_chat_tab = this->s_click_button;
    if(!this->s_click_main_menu_cube) this->s_click_main_menu_cube = this->s_click_button;
    if(!this->s_hover_main_menu_cube) this->s_hover_main_menu_cube = this->s_menu_hover;
    if(!this->s_click_sp) this->s_click_sp = this->s_click_button;
    if(!this->s_hover_sp) this->s_hover_sp = this->s_menu_hover;
    if(!this->s_click_mp) this->s_click_mp = this->s_click_button;
    if(!this->s_hover_mp) this->s_hover_mp = this->s_menu_hover;
    if(!this->s_click_options) this->s_click_options = this->s_click_button;
    if(!this->s_hover_options) this->s_hover_options = this->s_menu_hover;
    if(!this->s_click_exit) this->s_click_exit = this->s_click_button;
    if(!this->s_hover_exit) this->s_hover_exit = this->s_menu_hover;
    if(!this->s_click_pause_back) this->s_click_pause_back = this->s_click_button;
    if(!this->s_hover_pause_back) this->s_hover_pause_back = this->s_pause_hover;
    if(!this->s_click_pause_continue) this->s_click_pause_continue = this->s_click_button;
    if(!this->s_hover_pause_continue) this->s_hover_pause_continue = this->s_pause_hover;
    if(!this->s_click_pause_retry) this->s_click_pause_retry = this->s_click_button;
    if(!this->s_hover_pause_retry) this->s_hover_pause_retry = this->s_pause_hover;

    // always load these from the bundled default skin for consistent UI appearance (e.g. options menu buttons).
    // can't rely on the _DEFAULT resource cache from checkLoadImage, since a user fallback skin may have
    // provided the element before we ever reach the default dir.
    if(this->is_default) {
        this->i_cursor_default = this->i_cursor;
        this->i_button_left_default = this->i_button_left;
        this->i_button_mid_default = this->i_button_mid;
        this->i_button_right_default = this->i_button_right;
    } else {
        this->checkLoadImage(this->i_cursor_default, "cursor", "SKIN_CURSOR", false, "png", false, default_dir);
        this->checkLoadImage(this->i_button_left_default, "button-left", "SKIN_BUTTON_LEFT", false, "png", false,
                             default_dir);
        this->checkLoadImage(this->i_button_mid_default, "button-middle", "SKIN_BUTTON_MIDDLE", false, "png", false,
                             default_dir);
        this->checkLoadImage(this->i_button_right_default, "button-right", "SKIN_BUTTON_RIGHT", false, "png", false,
                             default_dir);
    }

    // print some debug info
    debugLog("Skin: Version {:f}", this->version);
    debugLog("Skin: HitCircleOverlap = {:d}", this->hitcircle_overlap_amt);

    // delayed error notifications due to resource loading potentially blocking engine time
    if(auto *notifOverlay = ui && ui->getNotificationOverlay() ? ui->getNotificationOverlay() : nullptr) {
        if(!parseSkinIni1Status && parseSkinIni2Status && cv::skin.getString() != "default")
            notifOverlay->addNotification("Error: Couldn't load skin.ini!", 0xffff0000);
        else if(!parseSkinIni2Status)
            notifOverlay->addNotification("Error: Couldn't load DEFAULT skin.ini!!!", 0xffff0000);
    }
}

void Skin::loadBeatmapOverride(const std::string & /*filepath*/) {
    // debugLog("Skin::loadBeatmapOverride( {:s} )", filepath.c_str());
    //  TODO: beatmap skin support
}

void Skin::reloadSounds() {
    std::vector<Resource *> soundResources;
    soundResources.reserve(this->sounds.size());

    for(auto &sound : this->sounds) {
        soundResources.push_back(sound);
    }

    resourceManager->reloadResources(soundResources, cv::skin_async.getBool());
}

bool Skin::parseSkinINI(std::string filepath) {
    UString fileContent;

    size_t fileSize{0};
    {
        File file(filepath);
        if(!file.canRead() || !(fileSize = file.getFileSize())) {
            debugLog("OsuSkin Error: Couldn't load {:s}", filepath);
            return false;
        }
        // convert possible non-UTF8 file to UTF8
        fileContent = {file.readToString().c_str(), static_cast<int>(fileSize)};
        // close the file here
    }

    enum class SkinSection : u8 {
        GENERAL,
        COLOURS,
        FONTS,
        NEOMOD,
    };

    bool hasNonEmptyLines = false;

    std::array<std::optional<Color>, 8> tempColors;

    // osu! defaults to [General] and loads properties even before the actual section start
    SkinSection curBlock = SkinSection::GENERAL;
    using enum SkinSection;

    for(const auto curLine : SString::split_newlines(fileContent.utf8View())) {
        // ignore comments, but only if at the beginning of a line
        if(curLine.empty() || SString::is_comment(curLine)) continue;
        hasNonEmptyLines = true;

        // section detection
        if(curLine.find("[General]") != std::string::npos)
            curBlock = GENERAL;
        else if(curLine.find("[Colours]") != std::string::npos || curLine.find("[Colors]") != std::string::npos)
            curBlock = COLOURS;
        else if(curLine.find("[Fonts]") != std::string::npos)
            curBlock = FONTS;
        else if(curLine.find("[" PACKAGE_NAME "]") != std::string::npos || curLine.find("[neosu]") != std::string::npos)
            curBlock = NEOMOD;

        switch(curBlock) {
// to go to the next line after we successfully parse a line
#define PARSE_LINE(...) \
    if(!!(Parsing::parse(curLine, __VA_ARGS__))) break;

            case GENERAL: {
                std::string version;
                if(Parsing::parse(curLine, "Version", ':', &version)) {
                    if((version.find("latest") != std::string::npos) || (version.find("User") != std::string::npos)) {
                        this->version = 2.5f;
                    } else {
                        PARSE_LINE("Version", ':', &this->version);
                    }
                }

                PARSE_LINE("CursorRotate", ':', &this->o_cursor_rotate);
                PARSE_LINE("CursorCentre", ':', &this->o_cursor_centered);
                PARSE_LINE("CursorExpand", ':', &this->o_cursor_expand);
                PARSE_LINE("LayeredHitSounds", ':', &this->o_layered_hitsounds);
                PARSE_LINE("SliderBallFlip", ':', &this->o_sliderball_flip);
                PARSE_LINE("AllowSliderBallTint", ':', &this->o_allow_sliderball_tint);
                PARSE_LINE("HitCircleOverlayAboveNumber", ':', &this->o_hitcircle_overlay_above_number);
                PARSE_LINE("SpinnerFadePlayfield", ':', &this->o_spinner_fade_playfield);
                PARSE_LINE("SpinnerFrequencyModulate", ':', &this->o_spinner_frequency_modulate);
                PARSE_LINE("SpinnerNoBlink", ':', &this->o_spinner_no_blink);

                // https://osu.ppy.sh/community/forums/topics/314209
                PARSE_LINE("HitCircleOverlayAboveNumer", ':', &this->o_hitcircle_overlay_above_number);

                if(Parsing::parse(curLine, "SliderStyle", ':', &this->slider_style)) {
                    if(this->slider_style != 1 && this->slider_style != 2) this->slider_style = 2;
                }

                if(Parsing::parse(curLine, "AnimationFramerate", ':', &this->anim_framerate)) {
                    if(this->anim_framerate < 0.f) this->anim_framerate = 0.f;
                }

                break;
            }

            case COLOURS: {
                u8 comboNum;
                u8 r, g, b;

                if(Parsing::parse(curLine, "Combo", &comboNum, ':', &r, ',', &g, ',', &b)) {
                    if(comboNum >= 1 && comboNum <= 8) {
                        tempColors[comboNum - 1] = rgb(r, g, b);
                    }
                } else if(Parsing::parse(curLine, "SpinnerApproachCircle", ':', &r, ',', &g, ',', &b))
                    this->c_spinner_approach_circle = rgb(r, g, b);
                else if(Parsing::parse(curLine, "SpinnerBackground", ':', &r, ',', &g, ',', &b))
                    this->c_spinner_bg = rgb(r, g, b);
                else if(Parsing::parse(curLine, "SliderBall", ':', &r, ',', &g, ',', &b))
                    this->c_slider_ball = rgb(r, g, b);
                else if(Parsing::parse(curLine, "SliderBorder", ':', &r, ',', &g, ',', &b))
                    this->c_slider_border = rgb(r, g, b);
                else if(Parsing::parse(curLine, "SliderTrackOverride", ':', &r, ',', &g, ',', &b)) {
                    this->c_slider_track_override = rgb(r, g, b);
                    this->o_slider_track_overridden = true;
                } else if(Parsing::parse(curLine, "SongSelectActiveText", ':', &r, ',', &g, ',', &b))
                    this->c_song_select_active_text = rgb(r, g, b);
                else if(Parsing::parse(curLine, "SongSelectInactiveText", ':', &r, ',', &g, ',', &b))
                    this->c_song_select_inactive_text = rgb(r, g, b);
                else if(Parsing::parse(curLine, "InputOverlayText", ':', &r, ',', &g, ',', &b))
                    this->c_input_overlay_text = rgb(r, g, b);

                break;
            }

            case FONTS: {
                PARSE_LINE("ComboOverlap", ':', &this->combo_overlap_amt);
                PARSE_LINE("ScoreOverlap", ':', &this->score_overlap_amt);
                PARSE_LINE("HitCircleOverlap", ':', &this->hitcircle_overlap_amt);

                PARSE_LINE("ComboPrefix", ':', &this->combo_prefix);
                PARSE_LINE("ScorePrefix", ':', &this->score_prefix);
                PARSE_LINE("HitCirclePrefix", ':', &this->hitcircle_prefix);
                break;
            }
#undef PARSE_LINE

            case NEOMOD: {
                size_t pos = curLine.find(':');
                if(pos == std::string::npos) break;

                bool shouldParse = true;
                std::string name, value;
                shouldParse &= !!Parsing::parse(curLine.substr(0, pos), &name);
                shouldParse &= !!Parsing::parse(curLine.substr(pos + 1), &value);

                // XXX: shouldn't be setting cvars directly in parsing method
                if(shouldParse) {
                    auto cvar = cvars().getConVarByName(name, false);
                    if(cvar) {
                        cvar->setValue(value, true, CvarEditor::SKIN);
                    } else {
                        debugLog("Skin wanted to set cvar '{}' to '{}', but it doesn't exist!", name, value);
                    }
                }

                break;
            }
        }
    }

    if(!hasNonEmptyLines) return false;

    for(const auto &tempCol : tempColors) {
        if(tempCol.has_value()) {
            this->c_combo_colors.push_back(tempCol.value());
        }
    }

    for(std::string *prefix_ref : {&this->combo_prefix, &this->score_prefix, &this->hitcircle_prefix}) {
        this->fixupPrefix(*prefix_ref, this->skin_dir);
    }

    return true;
}

void Skin::parseFallbackPrefixes(const std::string &iniPath) {
    File file(iniPath);
    if(!file.canRead() || !file.getFileSize()) return;

    UString content{file.readToString().c_str(), static_cast<int>(file.getFileSize())};

    bool inFonts = false;
    for(const auto curLine : SString::split_newlines(content.utf8View())) {
        if(curLine.empty() || SString::is_comment(curLine)) continue;
        if(curLine.find("[Fonts]") != std::string::npos) {
            inFonts = true;
            continue;
        }
        if(curLine.starts_with('[')) {
            inFonts = false;
            continue;
        }
        if(!inFonts) continue;

        Parsing::parse(curLine, "ComboPrefix", ':', &this->fallback_combo_prefix);
        Parsing::parse(curLine, "ScorePrefix", ':', &this->fallback_score_prefix);
        Parsing::parse(curLine, "HitCirclePrefix", ':', &this->fallback_hitcircle_prefix);
    }

    for(std::string *prefix_ref :
        {&this->fallback_combo_prefix, &this->fallback_score_prefix, &this->fallback_hitcircle_prefix}) {
        this->fixupPrefix(*prefix_ref, this->fallback_dir);
    }
}

// fixup incorrectly-cased subfolder prefixes for compatibility with case-sensitive filesystems
// e.g. "Images\\Main\\score" where on disk it's "images/main/"
// TODO: have a check to determine whether the current filesystem is case-insensitive already
void Skin::fixupPrefix(std::string &prefix, const std::string &baseDir) {
    if(prefix.empty()) return;

    File::normalizeSlashes(prefix, '\\', '/');
    if(!prefix.contains('/')) return;  // no subdirectory, nothing to fix

    const bool debug = cv::debug_osu.getBool() || cv::debug_file.getBool();

    // split into directory components + filename prefix (last element)
    auto parts = SString::split<std::string>(prefix, '/');
    const auto filename_prefix = std::move(parts.back());
    parts.pop_back();

    // walk directory components, fixing case against what's actually on disk
    std::string cur_path = baseDir;
    for(auto &dir_part : parts) {
        auto folders = Environment::getFoldersInFolder(cur_path);
        Hash::unstable_ncase_set<std::string> folders_nocase(folders.begin(), folders.end());

        if(auto it = folders_nocase.find(dir_part); it != folders_nocase.end()) {
            logIf(debug, "prefix fixup: matched '{}' -> '{}' in {}", dir_part, *it, cur_path);
            dir_part = *it;
            if(!cur_path.ends_with('/')) cur_path += '/';
            cur_path += *it;
        } else {
            logIf(debug, "prefix fixup: '{}' not found in {}, leaving as-is", dir_part, cur_path);
            break;
        }
    }

    // reassemble: dir1/dir2/.../filenameprefix
    prefix.clear();
    for(const auto &dir_part : parts) {
        prefix += dir_part;
        prefix += '/';
    }
    prefix += filename_prefix;
    logIf(debug, "prefix fixup result: {}", prefix);
}

Color Skin::getComboColorForCounter(int i, int offset) const {
    i += cv::skin_color_index_add.getInt();
    i = std::max(i, 0);

    if(this->c_beatmap_combo_colors.size() > 0 && !cv::ignore_beatmap_combo_colors.getBool())
        return this->c_beatmap_combo_colors[(i + offset) % this->c_beatmap_combo_colors.size()];
    else if(this->c_combo_colors.size() > 0)
        return this->c_combo_colors[i % this->c_combo_colors.size()];
    else
        return argb(255, 0, 255, 0);
}

void Skin::randomizeFilePath() {
    if(this->o_random_elements && this->filepaths_for_random_skin.size() > 0)
        this->search_dirs[0] = this->filepaths_for_random_skin[prand() % this->filepaths_for_random_skin.size()];
}

SkinImage *Skin::createSkinImage(const std::string &skinElementName, vec2 baseSizeForScaling2x, float osuSize,
                                 bool ignoreDefaultSkin, const std::string &animationSeparator) {
    auto *skinImage =
        new SkinImage(this, skinElementName, baseSizeForScaling2x, osuSize, animationSeparator, ignoreDefaultSkin);
    this->images.push_back(skinImage);

    Mc::append_range(this->filepaths_for_export, skinImage->getFilepathsForExport());

    return skinImage;
}

void Skin::checkLoadImage(BasicSkinImage &imgRef, const std::string &skinElementName, const std::string &resourceName,
                          bool ignoreDefaultSkin, const std::string &fileExtension, bool forceLoadMipmaps,
                          const std::string &overrideDir) {
    if(imgRef.img != MISSING_TEXTURE) return;  // already loaded

    const bool use_mipmaps = cv::skin_mipmaps.getBool() || forceLoadMipmaps;
    const size_t n_dirs = overrideDir.empty() ? (ignoreDefaultSkin ? 1 : this->search_dirs.size()) : 1;

    // forward iteration: first match wins
    for(size_t i = 0; i < n_dirs; i++) {
        const auto &dir = overrideDir.empty() ? this->search_dirs[i] : overrideDir;

        std::string base = dir;
        base.append(skinElementName);

        std::string path_2x = base;
        path_2x.append("@2x.");
        path_2x.append(fileExtension);

        std::string path_1x = base;
        path_1x.append(".");
        path_1x.append(fileExtension);
        const bool exists_2x = env->fileExists(path_2x);
        const bool exists_1x = env->fileExists(path_1x);

        if(!exists_2x && !exists_1x) continue;

        // only the built-in default dir (last entry for non-default skins) uses _DEFAULT naming;
        // primary and fallback dirs use unnamed resources tracked in this->resources.
        // compare against full search_dirs size, not n_dirs, since ignoreDefaultSkin truncates n_dirs.
        // overrideDir loads are also cached (they explicitly target the default dir).
        const bool is_cached_default = !this->is_default && (!overrideDir.empty() || i == this->search_dirs.size() - 1);

        std::string res_name;
        if(is_cached_default) {
            res_name = resourceName;
            res_name.append("_DEFAULT");
        }

        bool loaded = false;
        if(cv::skin_hd.getBool() && exists_2x) {
            if(cv::skin_async.getBool()) resourceManager->requestNextLoadAsync();
            imgRef = {resourceManager->loadImageAbs(path_2x, res_name, use_mipmaps)};
            loaded = true;
        } else if(exists_1x) {
            if(cv::skin_async.getBool()) resourceManager->requestNextLoadAsync();
            imgRef = {resourceManager->loadImageAbs(path_1x, res_name, use_mipmaps)};
            loaded = true;
        }

        if(loaded) {
            if(!is_cached_default) {
                this->resources.push_back(imgRef.img);
            }
            if(exists_2x) this->filepaths_for_export.push_back(std::move(path_2x));
            if(exists_1x) this->filepaths_for_export.push_back(std::move(path_1x));
            break;
        }
    }
}

void Skin::loadSound(Sound *&sndRef, const std::string &skinElementName, const std::string &resourceName,
                     bool isOverlayable, bool isSample, bool loop, bool fallback_to_default) {
    if(sndRef != nullptr) return;

    this->randomizeFilePath();

    // find first existing file with any supported audio extension
    auto find_sound_file = [](const std::string &dir, const std::string &name) -> std::string {
        for(auto ext : {".wav", ".mp3", ".ogg", ".flac"}) {
            std::string path = dir;
            path.append(name);
            path.append(ext);
            if(env->fileExists(path)) return path;
        }
        return {};
    };

    const size_t n_dirs = fallback_to_default ? this->search_dirs.size() : 1;

    for(size_t i = 0; i < n_dirs; i++) {
        if(i == 0 && isSample && !cv::skin_use_skin_hitsounds.getBool()) continue;

        std::string path = find_sound_file(this->search_dirs[i], skinElementName);
        if(path.empty()) continue;

        // only the built-in default dir (last entry for non-default skins) uses _DEFAULT naming.
        // compare against full search_dirs size, not n_dirs, since ignoreDefaultSkin truncates n_dirs
        const bool is_default_dir = !this->is_default && (i == this->search_dirs.size() - 1);
        const bool is_primary = (i == 0);

        if(is_default_dir) {
            // default dir: use _DEFAULT name, cached forever by ResourceManager
            std::string default_name = resourceName;
            default_name.append("_DEFAULT");
            if(cv::skin_async.getBool()) resourceManager->requestNextLoadAsync();
            sndRef = resourceManager->loadSoundAbs(std::move(path), default_name, !isSample, isOverlayable, loop);
        } else if(is_primary) {
            // primary dir: reuse existing Sound object if available, rebuild with new path
            Sound *existing = resourceManager->getSound(resourceName);
            if(existing) {
                existing->rebuild(path, cv::skin_async.getBool());
                sndRef = existing;
            } else {
                if(cv::skin_async.getBool()) resourceManager->requestNextLoadAsync();
                sndRef = resourceManager->loadSoundAbs(std::move(path), resourceName, !isSample, isOverlayable, loop);
            }
        } else {
            // fallback dir: rebuild existing or create new, with _FALLBACK suffix
            std::string fallback_name = resourceName;
            fallback_name.append("_FALLBACK");
            Sound *existing = resourceManager->getSound(fallback_name);
            if(existing) {
                existing->rebuild(path, cv::skin_async.getBool());
                sndRef = existing;
            } else {
                if(cv::skin_async.getBool()) resourceManager->requestNextLoadAsync();
                sndRef = resourceManager->loadSoundAbs(std::move(path), fallback_name, !isSample, isOverlayable, loop);
            }
        }
        break;
    }

    if(sndRef == nullptr) {
        debugLog("Skin Warning: NULL sound {:s}!", skinElementName.c_str());
        return;
    }

    this->sounds.push_back(sndRef);
    this->filepaths_for_export.push_back(sndRef->getFilePath());
}

const BasicSkinImage &Skin::getGradeImageLarge(ScoreGrade grade) const {
    using enum ScoreGrade;
    switch(grade) {
        case XH:
            return this->i_ranking_xh;
        case SH:
            return this->i_ranking_sh;
        case X:
            return this->i_ranking_x;
        case S:
            return this->i_ranking_s;
        case A:
            return this->i_ranking_a;
        case B:
            return this->i_ranking_b;
        case C:
            return this->i_ranking_c;
        default:
            return this->i_ranking_d;
    }
}

const SkinImage *Skin::getGradeImageSmall(ScoreGrade grade) const {
    using enum ScoreGrade;
    switch(grade) {
        case XH:
            return this->i_ranking_xh_small;
        case SH:
            return this->i_ranking_sh_small;
        case X:
            return this->i_ranking_x_small;
        case S:
            return this->i_ranking_s_small;
        case A:
            return this->i_ranking_a_small;
        case B:
            return this->i_ranking_b_small;
        case C:
            return this->i_ranking_c_small;
        default:
            return this->i_ranking_d_small;
    }
}

void Skin::getModImagesForMods(std::vector<SkinImage * Skin::*> &outVec, const Replay::Mods &mods) {
    using enum ModFlags;

    const bool modSS = flags::has<Perfect>(mods.flags);
    const bool modSD = flags::has<SuddenDeath>(mods.flags);

    // only for exact values
    const bool pitchCore = flags::has<NoPitchCorrection>(mods.flags);
    const bool modNC = mods.speed == 1.5f && pitchCore;
    const bool modDT = mods.speed == 1.5f && !modNC;  // only show dt/nc, not both

    const bool modDC = mods.speed == 0.75f && pitchCore;
    const bool modHT = mods.speed == 0.75f && !modDC;

    if(flags::has<NoFail>(mods.flags)) outVec.push_back(&Skin::i_modselect_nf);
    if(flags::has<Easy>(mods.flags)) outVec.push_back(&Skin::i_modselect_ez);
    if(flags::has<TouchDevice>(mods.flags)) outVec.push_back(&Skin::i_modselect_td);
    if(flags::has<Hidden>(mods.flags)) outVec.push_back(&Skin::i_modselect_hd);
    if(flags::has<HardRock>(mods.flags)) outVec.push_back(&Skin::i_modselect_hr);
    if(modSD && !modSS) outVec.push_back(&Skin::i_modselect_sd);
    if(modDT) outVec.push_back(&Skin::i_modselect_dt);
    if(flags::has<Relax>(mods.flags)) outVec.push_back(&Skin::i_modselect_rx);
    if(modHT)
        outVec.push_back(&Skin::i_modselect_ht);
    else if(modDC)
        outVec.push_back(&Skin::i_modselect_dc);  // idk where this should actually go since osu doesn't have it
    if(modNC) outVec.push_back(&Skin::i_modselect_nc);
    if(flags::has<Autoplay>(mods.flags)) outVec.push_back(&Skin::i_modselect_auto);
    if(flags::has<SpunOut>(mods.flags)) outVec.push_back(&Skin::i_modselect_so);
    if(flags::has<Autopilot>(mods.flags)) outVec.push_back(&Skin::i_modselect_ap);
    if(modSS) outVec.push_back(&Skin::i_modselect_pf);
    if(flags::has<Target>(mods.flags)) outVec.push_back(&Skin::i_modselect_target);
    if(flags::has<Nightmare>(mods.flags)) outVec.push_back(&Skin::i_modselect_nightmare);
    if(flags::has<ScoreV2>(mods.flags)) outVec.push_back(&Skin::i_modselect_sv2);

    return;
}

void Skin::getModImagesForMods(std::vector<SkinImage * Skin::*> &outVec, LegacyFlags flags) {
    return Skin::getModImagesForMods(outVec, Replay::Mods::from_legacy(flags));
}
