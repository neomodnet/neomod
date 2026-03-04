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

bool Skin::unpack(const char *filepath) {
    auto skin_name = Environment::getFileNameFromFilePath(filepath);
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

    if(!Environment::directoryExists(skin_root)) {
        Environment::createDirectory(skin_root);
    }

    for(const auto &entry : entries) {
        if(entry.isDirectory()) continue;

        std::string filename = entry.getFilename();
        const auto folders = SString::split(filename, '/');
        std::string file_path = skin_root;

        for(const auto &folder : folders) {
            if(!Environment::directoryExists(file_path)) {
                Environment::createDirectory(file_path);
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

void Skin::destroy(bool everything) {
    const auto destroyFlags = everything ? ResourceDestroyFlags::RDF_FORCE_BLOCKING : ResourceDestroyFlags::RDF_DEFAULT;

    for(auto &bimg : this->basic_images) {
        // don't destroy named/cached default skin images unless we are destroying everything
        if(bimg->img && bimg->img != MISSING_TEXTURE && (everything || !bimg->isFromDefault())) {
            resourceManager->destroyResource(bimg->img, destroyFlags);
        }
    }
    this->basic_images.clear();

    for(auto &simg : this->skin_images) {
        simg->destroy(everything);
    }
    this->skin_images.clear();

    // sounds are managed by resourcemanager, not unloaded here (unless we are destroying everything)
    if(everything) {
        for(auto *sound : this->sounds) {
            resourceManager->destroyResource(sound, destroyFlags);
            sound = nullptr;
        }
        this->sounds.clear();
    }
}

void Skin::update(bool isInPlayMode, bool isPlaying, i32 curMusicPos) {
    // tasks which have to be run after async loading finishes
    if(!this->is_ready && this->isReady()) {
        this->is_ready = true;
    }

    // shitty check to not animate while paused with hitobjects in background
    if(isInPlayMode && !isPlaying && !cv::skin_animation_force.getBool()) return;

    const bool useEngineTimeForAnimations = !isInPlayMode;
    for(auto *image : this->skin_images) {
        image->update(this->anim_speed, useEngineTimeForAnimations, curMusicPos);
    }
}

bool Skin::isReady() const {
    if(this->is_ready) return true;

    // default skin sounds aren't added to the resources vector... so check explicitly for that
    for(const auto *sound : this->sounds) {
        if(resourceManager->isLoadingResource(sound)) return false;
    }

    for(const auto *image : this->basic_images) {
        if(resourceManager->isLoadingResource(image->img)) return false;
    }

    for(const auto *image : this->skin_images) {
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
                std::vector<std::string> skinFolders = Environment::getFoldersInFolder(skinFolder);

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
        this->loadUnsizedImage(this->i_loading_spinner, "loading-spinner", "SK_I_LOADING_SPINNER");
    }

    // and the cursor comes right after that
    this->randomizeFilePath();
    {
        this->loadUnsizedImage(this->i_cursor, "cursor", "SK_I_CURSOR");
        this->loadUnsizedImage(this->i_cursor_middle, "cursormiddle", "SK_I_CURSORMIDDLE", true);
        this->loadUnsizedImage(this->i_cursor_trail, "cursortrail", "SK_I_CURSORTRAIL");
        this->loadUnsizedImage(this->i_cursor_ripple, "cursor-ripple", "SK_I_CURSORRIPPLE");
        this->loadUnsizedImage(this->i_cursor_smoke, "cursor-smoke", "SK_I_CURSORSMOKE");

        // special case: if fallback to default cursor, do load cursorMiddle
        if(this->i_cursor.img == resourceManager->getImage("SK_I_CURSOR_DEFAULT"))
            this->loadUnsizedImage(this->i_cursor_middle, "cursormiddle", "SK_I_CURSORMIDDLE");
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
    this->loadUnsizedImage(this->i_hitcircle, "hitcircle", "SK_I_HITCIRCLE");
    this->createSkinImage(this->i_hitcircleoverlay, "hitcircleoverlay", vec2(128, 128), 64);
    this->i_hitcircleoverlay.setAnimationFramerate(2);

    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_approachcircle, "approachcircle", "SK_I_APPROACHCIRCLE");
    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_reversearrow, "reversearrow", "SK_I_REVERSEARROW");

    this->randomizeFilePath();
    this->createSkinImage(this->i_followpoint, "followpoint", vec2(16, 22), 64);

    this->randomizeFilePath();
    {
        const std::string hitCirclePrefix = this->hitcircle_prefix.empty() ? "default" : this->hitcircle_prefix;
        const std::string &fbHitCirclePrefix = this->fallback_hitcircle_prefix;
        for(int i = 0; i < 10; i++) {
            const std::string resName = fmt::format("SK_I_DEFAULT{}", i);
            this->loadUnsizedImage(this->i_defaults[i], fmt::format("{}-{}", hitCirclePrefix, i), resName);
            // try fallback skin's prefix if it differs from primary
            if(this->i_defaults[i].img == MISSING_TEXTURE && !fbHitCirclePrefix.empty() &&
               fbHitCirclePrefix != hitCirclePrefix)
                this->loadUnsizedImage(this->i_defaults[i], fmt::format("{}-{}", fbHitCirclePrefix, i), resName);
            // special cases: fallback to default skin hitcircle numbers if the
            // defined prefix doesn't point to any valid files
            if(this->i_defaults[i].img == MISSING_TEXTURE)
                this->loadUnsizedImage(this->i_defaults[i], fmt::format("default-{}", i), resName);
        }
    }

    this->randomizeFilePath();
    {
        const std::string scorePrefix = this->score_prefix.empty() ? "score" : this->score_prefix;
        const std::string &fbScorePrefix = this->fallback_score_prefix;
        for(int i = 0; i < 10; i++) {
            const std::string resName = fmt::format("SK_I_SCORE{}", i);
            this->loadUnsizedImage(this->i_scores[i], fmt::format("{}-{}", scorePrefix, i), resName);
            // try fallback skin's prefix if it differs from primary
            if(this->i_scores[i].img == MISSING_TEXTURE && !fbScorePrefix.empty() && fbScorePrefix != scorePrefix)
                this->loadUnsizedImage(this->i_scores[i], fmt::format("{}-{}", fbScorePrefix, i), resName);
            // fallback logic
            if(this->i_scores[i].img == MISSING_TEXTURE)
                this->loadUnsizedImage(this->i_scores[i], fmt::format("score-{}", i), resName);
        }

        this->loadUnsizedImage(this->i_score_x, fmt::format("{}-x", scorePrefix), "SK_I_SCOREX");
        // if (this->scoreX == MISSING_TEXTURE) checkLoadImage(m_scoreX, "score-x", "SK_I_SCOREX"); // special
        // case: ScorePrefix'd skins don't get default fallbacks, instead missing extraneous things like the X are
        // simply not drawn
        this->loadUnsizedImage(this->i_score_percent, fmt::format("{}-percent", scorePrefix), "SK_I_SCOREPERCENT");
        this->loadUnsizedImage(this->i_score_dot, fmt::format("{}-dot", scorePrefix), "SK_I_SCOREDOT");
    }

    this->randomizeFilePath();
    {
        // yes, "score" is the default value for the combo prefix
        const std::string comboPrefix = this->combo_prefix.empty() ? "score" : this->combo_prefix;
        const std::string &fbComboPrefix = this->fallback_combo_prefix;
        for(int i = 0; i < 10; i++) {
            const std::string resName = fmt::format("SK_I_COMBO{}", i);
            this->loadUnsizedImage(this->i_combos[i], fmt::format("{}-{}", comboPrefix, i), resName);
            // try fallback skin's prefix if it differs from primary
            if(this->i_combos[i].img == MISSING_TEXTURE && !fbComboPrefix.empty() && fbComboPrefix != comboPrefix)
                this->loadUnsizedImage(this->i_combos[i], fmt::format("{}-{}", fbComboPrefix, i), resName);
            // fallback logic
            if(this->i_combos[i].img == MISSING_TEXTURE)
                this->loadUnsizedImage(this->i_combos[i], fmt::format("score-{}", i), resName);
        }

        // same special case as above for extras
        this->loadUnsizedImage(this->i_combo_x, fmt::format("{}-x", comboPrefix), "SK_I_COMBOX");
    }

    this->randomizeFilePath();
    this->createSkinImage(this->i_play_skip, "play-skip", vec2(193, 147), 94);
    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_play_warning_arrow, "play-warningarrow", "SK_I_PLAYWARNINGARROW");
    this->createSkinImage(this->i_play_warning_arrow2, "play-warningarrow", vec2(167, 129), 128);
    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_circular_metre, "circularmetre", "SK_I_CIRCULARMETRE");
    this->randomizeFilePath();
    this->createSkinImage(this->i_scorebar_bg, "scorebar-bg", vec2(695, 44), 27.5f);
    this->createSkinImage(this->i_scorebar_colour, "scorebar-colour", vec2(645, 10), 6.25f);
    this->createSkinImage(this->i_scorebar_marker, "scorebar-marker", vec2(24, 24), 15.0f, true);
    this->createSkinImage(this->i_scorebar_ki, "scorebar-ki", vec2(116, 116), 72.0f);
    this->createSkinImage(this->i_scorebad_ki_danger, "scorebar-kidanger", vec2(116, 116), 72.0f);
    this->createSkinImage(this->i_scorebar_ki_danger2, "scorebar-kidanger2", vec2(116, 116), 72.0f);
    this->randomizeFilePath();
    this->createSkinImage(this->i_section_pass, "section-pass", vec2(650, 650), 400.0f);
    this->randomizeFilePath();
    this->createSkinImage(this->i_section_fail, "section-fail", vec2(650, 650), 400.0f);
    this->randomizeFilePath();
    this->createSkinImage(this->i_input_overlay_bg, "inputoverlay-background", vec2(193, 55), 34.25f);
    this->createSkinImage(this->i_input_overlay_key, "inputoverlay-key", vec2(43, 46), 26.75f);

    this->randomizeFilePath();
    // clang-format off
#define MKPAIR_(hitimgname) std::pair<SkinImage *, std::string>{&this->i_##hitimgname, #hitimgname ""sv}
    for(const auto &[imgptr, str] : std::array{
            MKPAIR_(hit0),
            MKPAIR_(hit50),  MKPAIR_(hit50g),  MKPAIR_(hit50k),
            MKPAIR_(hit100), MKPAIR_(hit100g), MKPAIR_(hit100k),
            MKPAIR_(hit300), MKPAIR_(hit300g), MKPAIR_(hit300k),
        }) {
        this->createSkinImage(*imgptr, str, vec2(128, 128), 42);
        imgptr->setAnimationFramerate(60);
    }
#undef MKPAIR_
    // clang-format on

    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_particle50, "particle50", "SK_I_PARTICLE50", true);
    this->loadUnsizedImage(this->i_particle100, "particle100", "SK_I_PARTICLE100", true);
    this->loadUnsizedImage(this->i_particle300, "particle300", "SK_I_PARTICLE300", true);

    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_slider_gradient, "slidergradient", "SK_I_SLIDERGRADIENT");
    this->randomizeFilePath();
    this->createSkinImage(this->i_sliderb, "sliderb", vec2(128, 128), 64, false, "");
    this->i_sliderb.setAnimationFramerate(/*45.0f*/ 50.0f);
    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_slider_score_point, "sliderscorepoint", "SK_I_SLIDERSCOREPOINT");
    this->randomizeFilePath();
    this->createSkinImage(this->i_slider_follow_circle, "sliderfollowcircle", vec2(259, 259), 64);
    this->randomizeFilePath();
    this->loadUnsizedImage(
        this->i_slider_start_circle, "sliderstartcircle", "SK_I_SLIDERSTARTCIRCLE",
        !this->is_default);  // !m_bIsDefaultSkin ensures that default doesn't override user, in these special cases
    this->createSkinImage(this->i_slider_start_circle2, "sliderstartcircle", vec2(128, 128), 64, !this->is_default);
    this->loadUnsizedImage(this->i_slider_start_circle_overlay, "sliderstartcircleoverlay",
                           "SK_I_SLIDERSTARTCIRCLEOVERLAY", !this->is_default);
    this->createSkinImage(this->i_slider_start_circle_overlay2, "sliderstartcircleoverlay", vec2(128, 128), 64,
                          !this->is_default);
    this->i_slider_start_circle_overlay2.setAnimationFramerate(2);
    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_slider_end_circle, "sliderendcircle", "SK_I_SLIDERENDCIRCLE", !this->is_default);
    this->createSkinImage(this->i_slider_end_circle2, "sliderendcircle", vec2(128, 128), 64, !this->is_default);
    this->loadUnsizedImage(this->i_slider_end_circle_overlay, "sliderendcircleoverlay", "SK_I_SLIDERENDCIRCLEOVERLAY",
                           !this->is_default);
    this->createSkinImage(this->i_slider_end_circle_overlay2, "sliderendcircleoverlay", vec2(128, 128), 64,
                          !this->is_default);
    this->i_slider_end_circle_overlay2.setAnimationFramerate(2);

    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_spinner_bg, "spinner-background", "SK_I_SPINNERBACKGROUND");
    this->loadUnsizedImage(this->i_spinner_circle, "spinner-circle", "SK_I_SPINNERCIRCLE");
    this->loadUnsizedImage(this->i_spinner_approach_circle, "spinner-approachcircle", "SK_I_SPINNERAPPROACHCIRCLE");
    this->loadUnsizedImage(this->i_spinner_bottom, "spinner-bottom", "SK_I_SPINNERBOTTOM");
    this->loadUnsizedImage(this->i_spinner_middle, "spinner-middle", "SK_I_SPINNERMIDDLE");
    this->loadUnsizedImage(this->i_spinner_middle2, "spinner-middle2", "SK_I_SPINNERMIDDLE2");
    this->loadUnsizedImage(this->i_spinner_top, "spinner-top", "SK_I_SPINNERTOP");
    this->loadUnsizedImage(this->i_spinner_spin, "spinner-spin", "SK_I_SPINNERSPIN");
    this->loadUnsizedImage(this->i_spinner_clear, "spinner-clear", "SK_I_SPINNERCLEAR");
    this->loadUnsizedImage(this->i_spinner_metre, "spinner-metre", "SK_I_SPINNERMETRE");
    this->loadUnsizedImage(this->i_spinner_glow, "spinner-glow", "SK_I_SPINNERGLOW");  // TODO: use
    this->loadUnsizedImage(this->i_spinner_osu, "spinner-osu", "SK_I_SPINNEROSU");     // TODO: use
    this->loadUnsizedImage(this->i_spinner_rpm, "spinner-rpm", "SK_I_SPINNERRPM");     // TODO: use

    this->randomizeFilePath();
    this->createSkinImage(this->i_modselect_ez, "selection-mod-easy", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_nf, "selection-mod-nofail", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_ht, "selection-mod-halftime", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_hr, "selection-mod-hardrock", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_sd, "selection-mod-suddendeath", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_pf, "selection-mod-perfect", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_dt, "selection-mod-doubletime", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_nc, "selection-mod-nightcore", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_dc, "selection-mod-daycore", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_hd, "selection-mod-hidden", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_fl, "selection-mod-flashlight", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_rx, "selection-mod-relax", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_ap, "selection-mod-relax2", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_so, "selection-mod-spunout", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_auto, "selection-mod-autoplay", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_nightmare, "selection-mod-nightmare", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_target, "selection-mod-target", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_sv2, "selection-mod-scorev2", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_td, "selection-mod-touchdevice", vec2(68, 66), 38);
    this->createSkinImage(this->i_modselect_cinema, "selection-mod-cinema", vec2(68, 66), 38);

    this->createSkinImage(this->i_mode_osu, "mode-osu", vec2(32, 32), 32);
    this->createSkinImage(this->i_mode_osu_small, "mode-osu-small", vec2(32, 32), 32);

    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_pause_continue, "pause-continue", "SK_I_PAUSE_CONTINUE");
    this->loadUnsizedImage(this->i_pause_replay, "pause-replay", "SK_I_PAUSE_REPLAY");
    this->loadUnsizedImage(this->i_pause_retry, "pause-retry", "SK_I_PAUSE_RETRY");
    this->loadUnsizedImage(this->i_pause_back, "pause-back", "SK_I_PAUSE_BACK");
    this->loadUnsizedImage(this->i_pause_overlay, "pause-overlay", "SK_I_PAUSE_OVERLAY");
    if(this->i_pause_overlay.img == MISSING_TEXTURE)
        this->loadUnsizedImage(this->i_pause_overlay, "pause-overlay", "SK_I_PAUSE_OVERLAY", true, "jpg");
    this->loadUnsizedImage(this->i_fail_bg, "fail-background", "SK_I_FAIL_BACKGROUND");
    if(this->i_fail_bg.img == MISSING_TEXTURE)
        this->loadUnsizedImage(this->i_fail_bg, "fail-background", "SK_I_FAIL_BACKGROUND", true, "jpg");
    this->loadUnsizedImage(this->i_unpause, "unpause", "SK_I_UNPAUSE");

    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_button_left, "button-left", "SK_I_BUTTON_LEFT");
    this->loadUnsizedImage(this->i_button_mid, "button-middle", "SK_I_BUTTON_MIDDLE");
    this->loadUnsizedImage(this->i_button_right, "button-right", "SK_I_BUTTON_RIGHT");
    this->randomizeFilePath();
    // always load default skin menu-back (to show in options menu)
    {
        std::string origdir = this->search_dirs[0];
        this->search_dirs[0] = MCENGINE_IMAGES_PATH "/default/";
        this->createSkinImage(this->i_menu_back2_DEFAULTSKIN, "menu-back", vec2(225, 87), 54);
        this->search_dirs[0] = std::move(origdir);
    }
    this->createSkinImage(this->i_menu_back2, "menu-back", vec2(225, 87), 54);

    this->randomizeFilePath();

    // NOTE: scaling is ignored when drawing this specific element
    this->createSkinImage(this->i_sel_mode, "selection-mode", vec2(90, 90), 38);

    this->createSkinImage(this->i_sel_mode_over, "selection-mode-over", vec2(88, 90), 38);
    this->createSkinImage(this->i_sel_mods, "selection-mods", vec2(74, 90), 38);
    this->createSkinImage(this->i_sel_mods_over, "selection-mods-over", vec2(74, 90), 38);
    this->createSkinImage(this->i_sel_random, "selection-random", vec2(74, 90), 38);
    this->createSkinImage(this->i_sel_random_over, "selection-random-over", vec2(74, 90), 38);
    this->createSkinImage(this->i_sel_options, "selection-options", vec2(74, 90), 38);
    this->createSkinImage(this->i_sel_options_over, "selection-options-over", vec2(74, 90), 38);

    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_songselect_top, "songselect-top", "SK_I_SONGSELECT_TOP");
    this->loadUnsizedImage(this->i_songselect_bot, "songselect-bottom", "SK_I_SONGSELECT_BOTTOM");
    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_menu_button_bg, "menu-button-background", "SK_I_MENU_BUTTON_BACKGROUND");
    this->createSkinImage(this->i_menu_button_bg2, "menu-button-background", vec2(699, 103), 64.0f);
    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_star, "star", "SK_I_STAR");

    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_ranking_panel, "ranking-panel", "SK_I_RANKING_PANEL");
    this->loadUnsizedImage(this->i_ranking_graph, "ranking-graph", "SK_I_RANKING_GRAPH");
    this->loadUnsizedImage(this->i_ranking_title, "ranking-title", "SK_I_RANKING_TITLE");
    this->loadUnsizedImage(this->i_ranking_max_combo, "ranking-maxcombo", "SK_I_RANKING_MAXCOMBO");
    this->loadUnsizedImage(this->i_ranking_accuracy, "ranking-accuracy", "SK_I_RANKING_ACCURACY");

    this->loadUnsizedImage(this->i_ranking_a, "ranking-A", "SK_I_RANKING_A");
    this->loadUnsizedImage(this->i_ranking_b, "ranking-B", "SK_I_RANKING_B");
    this->loadUnsizedImage(this->i_ranking_c, "ranking-C", "SK_I_RANKING_C");
    this->loadUnsizedImage(this->i_ranking_d, "ranking-D", "SK_I_RANKING_D");
    this->loadUnsizedImage(this->i_ranking_s, "ranking-S", "SK_I_RANKING_S");
    this->loadUnsizedImage(this->i_ranking_sh, "ranking-SH", "SK_I_RANKING_SH");
    this->loadUnsizedImage(this->i_ranking_x, "ranking-X", "SK_I_RANKING_X");
    this->loadUnsizedImage(this->i_ranking_xh, "ranking-XH", "SK_I_RANKING_XH");

    this->createSkinImage(this->i_ranking_a_small, "ranking-A-small", vec2(34, 40), 128);
    this->createSkinImage(this->i_ranking_b_small, "ranking-B-small", vec2(34, 40), 128);
    this->createSkinImage(this->i_ranking_c_small, "ranking-C-small", vec2(34, 40), 128);
    this->createSkinImage(this->i_ranking_d_small, "ranking-D-small", vec2(34, 40), 128);
    this->createSkinImage(this->i_ranking_s_small, "ranking-S-small", vec2(34, 40), 128);
    this->createSkinImage(this->i_ranking_sh_small, "ranking-SH-small", vec2(34, 40), 128);
    this->createSkinImage(this->i_ranking_x_small, "ranking-X-small", vec2(34, 40), 128);
    this->createSkinImage(this->i_ranking_xh_small, "ranking-XH-small", vec2(34, 40), 128);

    this->createSkinImage(this->i_ranking_perfect, "ranking-perfect", vec2(478, 150), 128);

    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_beatmap_import_spinner, "beatmapimport-spinner", "SK_I_BEATMAP_IMPORT_SPINNER");
    this->loadUnsizedImage(this->i_circle_empty, "circle-empty", "SK_I_CIRCLE_EMPTY");
    this->loadUnsizedImage(this->i_circle_full, "circle-full", "SK_I_CIRCLE_FULL");
    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_seek_triangle, "seektriangle", "SK_I_SEEKTRIANGLE");
    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_user_icon, "user-icon", "SK_I_USER_ICON");
    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_background_cube, "backgroundcube", "SK_I_FPOSU_BACKGROUNDCUBE", false, "png",
                           true);  // force mipmaps
    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_menu_bg, "menu-background", "SK_I_MENU_BACKGROUND", false, "jpg");
    this->randomizeFilePath();
    this->loadUnsizedImage(this->i_skybox, "skybox", "SK_I_FPOSU_3D_SKYBOX");

    // slider ticks
    this->loadSound(this->s_normal_slidertick, "normal-slidertick", "SK_S_NORMALSLIDERTICK",  //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                        //
    this->loadSound(this->s_soft_slidertick, "soft-slidertick", "SK_S_SOFTSLIDERTICK",        //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                        //
    this->loadSound(this->s_drum_slidertick, "drum-slidertick", "SK_S_DRUMSLIDERTICK",        //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                        //

    // silder slides
    this->loadSound(this->s_normal_sliderslide, "normal-sliderslide", "SK_S_NORMALSLIDERSLIDE",  //
                    NOT_OVERLAYABLE, SAMPLE, LOOPING);                                           //
    this->loadSound(this->s_soft_sliderslide, "soft-sliderslide", "SK_S_SOFTSLIDERSLIDE",        //
                    NOT_OVERLAYABLE, SAMPLE, LOOPING);                                           //
    this->loadSound(this->s_drum_sliderslide, "drum-sliderslide", "SK_S_DRUMSLIDERSLIDE",        //
                    NOT_OVERLAYABLE, SAMPLE, LOOPING);                                           //

    // slider whistles
    this->loadSound(this->s_normal_sliderwhistle, "normal-sliderwhistle", "SK_S_NORMALSLIDERWHISTLE",  //
                    NOT_OVERLAYABLE, SAMPLE, LOOPING);                                                 //
    this->loadSound(this->s_soft_sliderwhistle, "soft-sliderwhistle", "SK_S_SOFTSLIDERWHISTLE",        //
                    NOT_OVERLAYABLE, SAMPLE, LOOPING);                                                 //
    this->loadSound(this->s_drum_sliderwhistle, "drum-sliderwhistle", "SK_S_DRUMSLIDERWHISTLE",        //
                    NOT_OVERLAYABLE, SAMPLE, LOOPING);                                                 //

    // hitcircle
    this->loadSound(this->s_normal_hitnormal, "normal-hitnormal", "SK_S_NORMALHITNORMAL",     //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                        //
    this->loadSound(this->s_soft_hitnormal, "soft-hitnormal", "SK_S_SOFTHITNORMAL",           //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                        //
    this->loadSound(this->s_drum_hitnormal, "drum-hitnormal", "SK_S_DRUMHITNORMAL",           //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                        //
    this->loadSound(this->s_normal_hitwhistle, "normal-hitwhistle", "SK_S_NORMALHITWHISTLE",  //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                        //
    this->loadSound(this->s_soft_hitwhistle, "soft-hitwhistle", "SK_S_SOFTHITWHISTLE",        //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                        //
    this->loadSound(this->s_drum_hitwhistle, "drum-hitwhistle", "SK_S_DRUMHITWHISTLE",        //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                        //
    this->loadSound(this->s_normal_hitfinish, "normal-hitfinish", "SK_S_NORMALHITFINISH",     //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                        //
    this->loadSound(this->s_soft_hitfinish, "soft-hitfinish", "SK_S_SOFTHITFINISH",           //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                        //
    this->loadSound(this->s_drum_hitfinish, "drum-hitfinish", "SK_S_DRUMHITFINISH",           //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                        //
    this->loadSound(this->s_normal_hitclap, "normal-hitclap", "SK_S_NORMALHITCLAP",           //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                        //
    this->loadSound(this->s_soft_hitclap, "soft-hitclap", "SK_S_SOFTHITCLAP",                 //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                        //
    this->loadSound(this->s_drum_hitclap, "drum-hitclap", "SK_S_DRUMHITCLAP",                 //
                    OVERLAYABLE, SAMPLE, NOT_LOOPING);                                        //

    // spinner
    this->loadSound(this->s_spinner_bonus, "spinnerbonus", "SK_S_SPINNERBONUS", OVERLAYABLE, SAMPLE, NOT_LOOPING);
    this->loadSound(this->s_spinner_spin, "spinnerspin", "SK_S_SPINNERSPIN", NOT_OVERLAYABLE, SAMPLE, LOOPING);

    // others
    this->loadSound(this->s_combobreak, "combobreak", "SK_S_COMBOBREAK", true, true);
    this->loadSound(this->s_fail, "failsound", "SK_S_FAILSOUND");
    this->loadSound(this->s_applause, "applause", "SK_S_APPLAUSE");
    this->loadSound(this->s_menu_hit, "menuhit", "SK_S_MENUHIT", true, true);
    this->loadSound(this->s_menu_hover, "menuclick", "SK_S_MENUCLICK", true, true);
    this->loadSound(this->s_check_on, "check-on", "SK_S_CHECKON", true, true);
    this->loadSound(this->s_check_off, "check-off", "SK_S_CHECKOFF", true, true);
    this->loadSound(this->s_shutter, "shutter", "SK_S_SHUTTER", true, true);
    this->loadSound(this->s_section_pass, "sectionpass", "SK_S_SECTIONPASS");
    this->loadSound(this->s_section_fail, "sectionfail", "SK_S_SECTIONFAIL");

    // UI feedback
    this->loadSound(this->s_message_sent, "key-confirm", "SK_S_MESSAGE_SENT", true, true, false);
    this->loadSound(this->s_deleting_text, "key-delete", "SK_S_DELETING_TEXT", true, true, false);
    this->loadSound(this->s_moving_text_cursor, "key-movement", "SK_S_MOVING_TEXT_CURSOR", true, true, false);
    this->loadSound(this->s_typing1, "key-press-1", "SK_S_TYPING_1", true, true, false);
    this->loadSound(this->s_typing2, "key-press-2", "SK_S_TYPING_2", true, true, false, false);
    this->loadSound(this->s_typing3, "key-press-3", "SK_S_TYPING_3", true, true, false, false);
    this->loadSound(this->s_typing4, "key-press-4", "SK_S_TYPING_4", true, true, false, false);
    this->loadSound(this->s_menu_back, "menuback", "SK_S_MENU_BACK", true, true, false, false);
    this->loadSound(this->s_close_chat_tab, "click-close", "SK_S_CLOSE_CHAT_TAB", true, true, false, false);
    this->loadSound(this->s_click_button, "click-short-confirm", "SK_S_CLICK_BUTTON", true, true, false, false);
    this->loadSound(this->s_hover_button, "click-short", "SK_S_HOVER_BUTTON", true, true, false, false);
    this->loadSound(this->s_click_back_button, "back-button-click", "SK_S_BACK_BUTTON_CLICK", true, true, false, false);
    this->loadSound(this->s_hover_back_button, "back-button-hover", "SK_S_BACK_BUTTON_HOVER", true, true, false, false);
    this->loadSound(this->s_click_main_menu_cube, "menu-play-click", "SK_S_CLICK_MAIN_MENU_CUBE", true, true, false,
                    false);
    this->loadSound(this->s_hover_main_menu_cube, "menu-play-hover", "SK_S_HOVER_MAIN_MENU_CUBE", true, true, false,
                    false);
    this->loadSound(this->s_click_sp, "menu-freeplay-click", "SK_S_CLICK_SINGLEPLAYER", true, true, false, false);
    this->loadSound(this->s_hover_sp, "menu-freeplay-hover", "SK_S_HOVER_SINGLEPLAYER", true, true, false, false);
    this->loadSound(this->s_click_mp, "menu-multiplayer-click", "SK_S_CLICK_MULTIPLAYER", true, true, false, false);
    this->loadSound(this->s_hover_mp, "menu-multiplayer-hover", "SK_S_HOVER_MULTIPLAYER", true, true, false, false);
    this->loadSound(this->s_click_options, "menu-options-click", "SK_S_CLICK_OPTIONS", true, true, false, false);
    this->loadSound(this->s_hover_options, "menu-options-hover", "SK_S_HOVER_OPTIONS", true, true, false, false);
    this->loadSound(this->s_click_exit, "menu-exit-click", "SK_S_CLICK_EXIT", true, true, false, false);
    this->loadSound(this->s_hover_exit, "menu-exit-hover", "SK_S_HOVER_EXIT", true, true, false, false);
    this->loadSound(this->s_expand, "select-expand", "SK_S_EXPAND", true, true, false);
    this->loadSound(this->s_select_difficulty, "select-difficulty", "SK_S_SELECT_DIFFICULTY", true, true, false, false);
    this->loadSound(this->s_sliderbar, "sliderbar", "SK_S_DRAG_SLIDER", true, true, false);
    this->loadSound(this->s_match_confirm, "match-confirm", "SK_S_ALL_PLAYERS_READY", true, true, false);
    this->loadSound(this->s_room_joined, "match-join", "SK_S_ROOM_JOINED", true, true, false);
    this->loadSound(this->s_room_quit, "match-leave", "SK_S_ROOM_QUIT", true, true, false);
    this->loadSound(this->s_room_not_ready, "match-notready", "SK_S_ROOM_NOT_READY", true, true, false);
    this->loadSound(this->s_room_ready, "match-ready", "SK_S_ROOM_READY", true, true, false);
    this->loadSound(this->s_match_start, "match-start", "SK_S_MATCH_START", true, true, false);

    this->loadSound(this->s_pause_loop, "pause-loop", "SK_S_PAUSE_LOOP", NOT_OVERLAYABLE, STREAM, LOOPING, true);
    this->loadSound(this->s_pause_hover, "pause-hover", "SK_S_PAUSE_HOVER", OVERLAYABLE, SAMPLE, NOT_LOOPING, false);
    this->loadSound(this->s_click_pause_back, "pause-back-click", "SK_S_CLICK_QUIT_SONG", true, true, false, false);
    this->loadSound(this->s_hover_pause_back, "pause-back-hover", "SK_S_HOVER_QUIT_SONG", true, true, false, false);
    this->loadSound(this->s_click_pause_continue, "pause-continue-click", "SK_S_CLICK_RESUME_SONG", true, true, false,
                    false);
    this->loadSound(this->s_hover_pause_continue, "pause-continue-hover", "SK_S_HOVER_RESUME_SONG", true, true, false,
                    false);
    this->loadSound(this->s_click_pause_retry, "pause-retry-click", "SK_S_CLICK_RETRY_SONG", true, true, false, false);
    this->loadSound(this->s_hover_pause_retry, "pause-retry-hover", "SK_S_HOVER_RETRY_SONG", true, true, false, false);

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
        this->loadUnsizedImage(this->i_cursor_default, "cursor", "SK_I_CURSOR", false, "png", false, default_dir);
        this->loadUnsizedImage(this->i_button_left_default, "button-left", "SK_I_BUTTON_LEFT", false, "png", false,
                               default_dir);
        this->loadUnsizedImage(this->i_button_mid_default, "button-middle", "SK_I_BUTTON_MIDDLE", false, "png", false,
                               default_dir);
        this->loadUnsizedImage(this->i_button_right_default, "button-right", "SK_I_BUTTON_RIGHT", false, "png", false,
                               default_dir);
    }

    // print some debug info
    debugLog("Skin: Version {:.2f}", this->version);
    debugLog("Skin: HitCircleOverlap = {:d}", this->hitcircle_overlap_amt);

    // delayed error notifications due to resource loading potentially blocking engine time
    if(auto *notifOverlay = ui && ui->getNotificationOverlay() ? ui->getNotificationOverlay() : nullptr) {
        if(!parseSkinIni1Status && parseSkinIni2Status && cv::skin.getString() != "default")
            notifOverlay->addNotification("Error: Couldn't load skin.ini!", 0xffff0000);
        else if(!parseSkinIni2Status)
            notifOverlay->addNotification("Error: Couldn't load DEFAULT skin.ini!!!", 0xffff0000);
    }
}

void Skin::loadBeatmapOverride(std::string_view /*filepath*/) {
    // debugLog("Skin::loadBeatmapOverride( {:s} )", filepath.c_str());
    //  TODO: beatmap skin support
}

void Skin::reloadSounds() {
    resourceManager->reloadResources(reinterpret_cast<const std::vector<Resource *> &>(this->sounds),
                                     cv::skin_async.getBool());
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

void Skin::createSkinImage(SkinImage &ref, const std::string &skinElementName, vec2 baseSizeForScaling2x, float osuSize,
                           bool ignoreDefaultSkin, const std::string &animationSeparator) {
    assert(!ref.skin);

    this->skin_images.push_back(&ref);
    auto exportFiles =
        ref.init(this, skinElementName, baseSizeForScaling2x, osuSize, animationSeparator, ignoreDefaultSkin);
    Mc::append_range(this->filepaths_for_export, std::move(exportFiles));
}

void Skin::loadUnsizedImage(BasicSkinImage &ref, const std::string &skinElementName, const std::string &resourceName,
                            bool ignoreDefaultSkin, const std::string &fileExtension, bool forceLoadMipmaps,
                            const std::string &overrideDir) {
    assert(ref == MISSING_TEXTURE);
    bool loaded = false;

    const bool use_mipmaps = cv::skin_mipmaps.getBool() || forceLoadMipmaps;
    const size_t n_dirs = overrideDir.empty() ? (ignoreDefaultSkin ? 1 : this->search_dirs.size()) : 1;

    const bool load_hd = cv::skin_hd.getBool();
    const bool load_async = cv::skin_async.getBool();

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
        const bool exists_2x = Environment::fileExists(path_2x);
        const bool exists_1x = Environment::fileExists(path_1x);

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

        if(load_hd && exists_2x) {
            if(load_async) resourceManager->requestNextLoadAsync();
            ref.img = resourceManager->loadImageAbs(path_2x, res_name, use_mipmaps);
            ref.scale_mul = 2;
            loaded = true;
        } else if(exists_1x) {
            if(load_async) resourceManager->requestNextLoadAsync();
            ref.img = resourceManager->loadImageAbs(path_1x, res_name, use_mipmaps);
            ref.scale_mul = 1;
            loaded = true;
        }

        if(loaded) {
            if(exists_2x) this->filepaths_for_export.push_back(std::move(path_2x));
            if(exists_1x) this->filepaths_for_export.push_back(std::move(path_1x));
            ref.is_default = is_cached_default;

            break;
        }
    }

    if(loaded && ref.img != MISSING_TEXTURE) {
        this->basic_images.push_back(&ref);
    }

    return;
}

void Skin::loadSound(Sound *&ref, const std::string &skinElementName, const std::string &resourceName,
                     bool isOverlayable, bool isSample, bool loop, bool fallback_to_default) {
    assert(!ref);

    this->randomizeFilePath();

    // find first existing file with any supported audio extension
    auto find_sound_file = [](const std::string &dir, const std::string &name) -> std::string {
        for(auto ext : {".wav", ".mp3", ".ogg", ".flac"}) {
            std::string path = dir;
            path.append(name);
            path.append(ext);
            if(Environment::fileExists(path)) return path;
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
            ref = resourceManager->loadSoundAbs(std::move(path), default_name, !isSample, isOverlayable, loop);
        } else if(is_primary) {
            // primary dir: reuse existing Sound object if available, rebuild with new path
            Sound *existing = resourceManager->getSound(resourceName);
            if(existing) {
                existing->rebuild(path, cv::skin_async.getBool());
                ref = existing;
            } else {
                if(cv::skin_async.getBool()) resourceManager->requestNextLoadAsync();
                ref = resourceManager->loadSoundAbs(std::move(path), resourceName, !isSample, isOverlayable, loop);
            }
        } else {
            // fallback dir: rebuild existing or create new, with _FALLBACK suffix
            std::string fallback_name = resourceName;
            fallback_name.append("_FALLBACK");
            Sound *existing = resourceManager->getSound(fallback_name);
            if(existing) {
                existing->rebuild(path, cv::skin_async.getBool());
                ref = existing;
            } else {
                if(cv::skin_async.getBool()) resourceManager->requestNextLoadAsync();
                ref = resourceManager->loadSoundAbs(std::move(path), fallback_name, !isSample, isOverlayable, loop);
            }
        }
        break;
    }

    if(ref == nullptr) {
        debugLog("Skin Warning: NULL sound {:s}!", skinElementName.c_str());
    } else {
        this->sounds.push_back(ref);
        this->filepaths_for_export.push_back(ref->getFilePath());
    }

    return;
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

const SkinImage &Skin::getGradeImageSmall(ScoreGrade grade) const {
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

void Skin::getModImagesForMods(std::vector<SkinImage Skin::*> &outVec, const Replay::Mods &mods) {
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

void Skin::getModImagesForMods(std::vector<SkinImage Skin::*> &outVec, LegacyFlags flags) {
    return Skin::getModImagesForMods(outVec, Replay::Mods::from_legacy(flags));
}
