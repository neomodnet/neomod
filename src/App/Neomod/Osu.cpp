// Copyright (c) 2015, PG, All rights reserved.
#include "Osu.h"

#include "ThumbnailManager.h"
#include "BackgroundImageHandler.h"
#include "Bancho.h"
#include "BanchoNetworking.h"
#include "BeatmapInterface.h"
#include "CBaseUIScrollView.h"
#include "CBaseUISlider.h"
#include "CBaseUITextbox.h"
#include "Changelog.h"
#include "Chat.h"
#include "OsuConVars.h"
#include "ConVarHandler.h"
#include "MakeDelegateWrapper.h"
#include "Console.h"
#include "ConsoleBox.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "DirectoryWatcher.h"
#include "DiscordInterface.h"
#include "Downloader.h"
#include "Engine.h"
#include "File.h"
#include "Font.h"
#include "RuntimePlatform.h"
#include "Sound.h"
#include "Environment.h"
#include "GameRules.h"
#include "HUD.h"
#include "HitObjects.h"
#include "Icons.h"
#include "KeyBindings.h"
#include "Keyboard.h"
#include "LegacyReplay.h"
#include "Lobby.h"
#include "MainMenu.h"
#include "ModFPoSu.h"
#include "ModSelector.h"
#include "Mouse.h"
#include "NotificationOverlay.h"
#include "OptionsOverlay.h"
#include "OsuDirectScreen.h"
#include "Parsing.h"
#include "PauseOverlay.h"
#include "SettingsImporter.h"
#include "Profiler.h"
#include "PromptOverlay.h"
#include "RankingScreen.h"
#include "RenderTarget.h"
#include "ResourceManager.h"
#include "RoomScreen.h"
#include "Shader.h"
#include "Skin.h"
#include "AsyncPPCalculator.h"
#include "SongBrowser/LoudnessCalcThread.h"
#include "DiffCalc/BatchDiffCalc.h"
#include "SongBrowser/SongBrowser.h"
#include "SoundEngine.h"
#include "SpectatorScreen.h"
#include "TooltipOverlay.h"
#include "UI.h"
#include "UIContextMenu.h"
#include "UIModSelectorModButton.h"
#include "UIUserContextMenu.h"
#include "UpdateHandler.h"
#include "UserCard.h"
#include "UserStatsScreen.h"
#include "VolumeOverlay.h"
#include "Logging.h"

#include "score.h"

#include <algorithm>

using namespace flags::operators;

Osu *osu{nullptr};

// prevents score submission when/if a protected convar is changed during gameplay
void Osu::globalOnSetValueProtectedCallback() {
    if(likely(this->map_iface)) {
        this->map_iface->is_submittable = false;
    }
}

// prevents getting changed protected convars while in a multi lobby
bool Osu::globalOnGetValueProtectedCallback(const char *cvarname) {
    if(BanchoState::is_in_a_multi_room()) {
        logIfCV(debug_cv, "Returning default value for {}, currently in a multi room.", cvarname);
        return false;
    }
    return true;
}

// prevents changing gameplay convars while playing multi and disables score submission
bool Osu::globalOnSetValueGameplayCallback(const char *cvarname, CvarEditor setterkind) {
    // Only SERVER can edit GAMEPLAY cvars during multiplayer matches
    if(BanchoState::is_playing_a_multi_map() && setterkind != CvarEditor::SERVER) {
        debugLog("Can't edit {} while in a multiplayer match.", cvarname);
        return false;
    }

    // Regardless of the editor, changing GAMEPLAY cvars in the middle of a map
    // will result in an invalid replay. Set it as cheated so the score isn't saved.
    if(osu->isInPlayMode()) {
        debugLog("{} affects gameplay: won't submit score.", cvarname);
    }
    // maybe an impossible scenario for this to be NULL here but just checking anyways
    if(const auto &liveScore = osu->getScore(); !!liveScore) {
        liveScore->setCheated();
    }

    return true;
}

bool Osu::globalOnAreAllCvarsSubmittableCallback() {
    // Also check for non-vanilla mod combinations here while we're at it
    // We don't want to submit target scores, even though it's allowed in multiplayer
    if(osu->getModTarget()) return false;

    if(osu->getModEZ() && osu->getModHR()) return false;

    if(!cv::sv_allow_speed_override.getBool()) {
        f32 speed = cv::speed_override.getFloat();
        if(speed != -1.f && speed != 0.75 && speed != 1.0 && speed != 1.5) return false;
    }
    return true;
}

Osu::GlobalOsuCtorDtorThing::GlobalOsuCtorDtorThing(Osu *optr) { osu = optr; }
Osu::GlobalOsuCtorDtorThing::~GlobalOsuCtorDtorThing() { osu = nullptr; }

Osu::Osu()
    : App(),
      MouseListener(),
      global_osu_(this),
      previous_mods(std::make_unique<Replay::Mods>()),
      map_iface(std::make_unique<BeatmapInterface>()),
      score(std::make_unique<LiveScore>(false)) {
    // global cvar callbacks will be removed in destructor
    ConVar::setOnSetValueProtectedCallback(SA::MakeDelegate<&Osu::globalOnSetValueProtectedCallback>(this));

    ConVar::setOnGetValueProtectedCallback(Osu::globalOnGetValueProtectedCallback);

    ConVar::setOnSetValueGameplayCallback(Osu::globalOnSetValueGameplayCallback);

    cvars().setCVSubmittableCheckFunc(Osu::globalOnAreAllCvarsSubmittableCallback);

    // create cache dir, with migration for old versions
    {
        const std::string &cacheDir = env->getCacheDir();
        Environment::createDirectory(cacheDir);
        if(Environment::directoryExists(NEOMOD_DATA_DIR "avatars")) {
            Environment::renameFile(NEOMOD_DATA_DIR "avatars", cacheDir + "/avatars");
        }
        Environment::createDirectory(cacheDir + "/avatars");
        Environment::createDirectory(cacheDir + "/thumbs");
    }

    // create directories we will assume already exist later on
    Environment::createDirectory(NEOMOD_CFG_PATH);
    Environment::createDirectory(NEOMOD_MAPS_PATH);
    Environment::createDirectory(NEOMOD_REPLAYS_PATH);
    Environment::createDirectory(NEOMOD_SCREENSHOTS_PATH);
    Environment::createDirectory(NEOMOD_SKINS_PATH);

    engine->getConsoleBox()->setRequireShiftToActivate(true);
    mouse->addListener(this);

    // set default fullscreen/letterboxed/windowed resolutions to match reality
    {
        const auto def_res = env->getNativeScreenSize();
        std::string def_res_str = fmt::format("{:.0f}x{:.0f}", def_res.x, def_res.y);
        cv::resolution.setValue(def_res_str);
        cv::resolution.setDefaultString(def_res_str);
        cv::letterboxed_resolution.setValue(def_res_str);
        cv::letterboxed_resolution.setDefaultString(def_res_str);

        const auto def_windowed_res = env->getWindowSize();
        std::string def_windowed_res_str = fmt::format("{:.0f}x{:.0f}", def_windowed_res.x, def_windowed_res.y);
        cv::windowed_resolution.setValue(def_windowed_res_str);
        cv::windowed_resolution.setDefaultString(def_windowed_res_str);
    }

    // convar callbacks
    cv::resolution.setCallback(SA::MakeDelegate<&Osu::onFSResChanged>(this));
    cv::letterboxed_resolution.setCallback(SA::MakeDelegate<&Osu::onFSLetterboxedResChanged>(this));
    cv::windowed_resolution.setCallback(SA::MakeDelegate<&Osu::onWindowedResolutionChanged>(this));
    cv::animation_speed_override.setCallback(SA::MakeDelegate<&Osu::onAnimationSpeedChange>(this));
    cv::ui_scale.setCallback(SA::MakeDelegate<&Osu::onUIScaleChange>(this));
    cv::ui_scale_to_dpi.setCallback(SA::MakeDelegate<&Osu::onUIScaleToDPIChange>(this));
    cv::letterboxing.setCallback(SA::MakeDelegate<&Osu::onLetterboxingChange>(this));
    cv::letterboxing_offset_x.setCallback(SA::MakeDelegate<&Osu::onLetterboxingOffsetChange>(this));
    cv::letterboxing_offset_y.setCallback(SA::MakeDelegate<&Osu::onLetterboxingOffsetChange>(this));
    cv::confine_cursor_windowed.setCallback(SA::MakeDelegate<&Osu::updateConfineCursor>(this));
    cv::confine_cursor_fullscreen.setCallback(SA::MakeDelegate<&Osu::updateConfineCursor>(this));
    cv::confine_cursor_never.setCallback(SA::MakeDelegate<&Osu::updateConfineCursor>(this));
    cv::osu_folder.setCallback([](std::string_view oldString, std::string_view newString) -> void {
        std::string normalized = Environment::normalizeDirectory(std::string{newString});
        if(normalized.empty()) {
            if(!oldString.empty()) {
                // don't allow making it empty if it wasn't already empty
                normalized = oldString;
            } else {
                // if it was empty, reset it to some sane default
                normalized = Osu::getDefaultFallbackOsuFolder();
            }
        }
        cv::osu_folder.setValue(normalized, false);
        if(osu && osu->UIReady()) ui->getOptionsOverlay()->updateOsuFolderTextbox(normalized);
    });

    // clamp to sane range
    cv::slider_curve_points_separation.setCallback([](float /*oldValue*/, float newValue) -> void {
        newValue = std::clamp(newValue, 1.0f, 2.5f);
        cv::slider_curve_points_separation.setValue(newValue, false);
    });

    // renderer
    this->internalRect = engine->getScreenRect();

    this->backBuffer =
        resourceManager->createRenderTarget(0, 0, this->getVirtScreenWidth(), this->getVirtScreenHeight());
    this->playfieldBuffer = resourceManager->createRenderTarget(0, 0, 64, 64);
    this->sliderFrameBuffer =
        resourceManager->createRenderTarget(0, 0, this->getVirtScreenWidth(), this->getVirtScreenHeight());
    this->AAFrameBuffer = resourceManager->createRenderTarget(0, 0, this->getVirtScreenWidth(),
                                                              this->getVirtScreenHeight(), MultisampleType::X4);
    this->frameBuffer = resourceManager->createRenderTarget(0, 0, 64, 64);
    this->frameBuffer2 = resourceManager->createRenderTarget(0, 0, 64, 64);

    // load a few select subsystems very early
    db = std::make_unique<Database>();  // global database instance
    this->ui_memb = std::make_unique<UI>();
    this->updateHandler = std::make_unique<UpdateHandler>();
    this->thumbnailManager = std::make_unique<ThumbnailManager>();
    this->backgroundImageHandler = std::make_unique<BGImageHandler>();
    this->fposu = std::make_unique<ModFPoSu>();

    // load main menu icon before skin
    resourceManager->requestNextLoadAsync();
    resourceManager->loadImage(PACKAGE_NAME ".png", "NEOMOD_LOGO", true /* mipmapped */);

    // exec the main config file (this must be right here!)
    Console::execConfigFile("underride");  // same as override, but for defaults
    Console::execConfigFile("osu");
    Console::execConfigFile("override");  // used for quickfixing live builds without redeploying/recompiling

    // if we don't have an osu.cfg, import
    if(!Environment::fileExists(NEOMOD_CFG_PATH "/osu.cfg")) {
        SettingsImporter::import_from_mcosu();
        SettingsImporter::import_from_osu_stable();
    }

    // don't allow empty osu_folder if it's still empty at this point
    if(cv::osu_folder.getString().empty()) {
        const std::string fallback = this->getDefaultFallbackOsuFolder();
        debugLog("using fallback/default osu! folder: {}", fallback);
        cv::osu_folder.setValue(fallback, false);
    }

    // Initialize sound here so we can load the preferred device from config
    // Avoids initializing the sound device twice, which can take a while depending on the driver
    this->setupAudio();

    cv::skin.setCallback(SA::MakeDelegate<&Osu::onSkinChange>(this));
    // no callback for skin_fallback: it's read on-demand by onSkinChange.
    // to apply a new fallback, change skin or use skin_reload.
    cv::skin_reload.setCallback(SA::MakeDelegate<&Osu::onSkinReload>(this));
    // Initialize skin after sound engine has started, or else sounds won't load properly
    this->onSkinChange(cv::skin.getString());

    // Init neomod_version after loading config for correct bleedingedge detection
    if(Env::cfg(BUILD::DEBUG)) {
        BanchoState::neomod_version = fmt::format("dev-{}", cv::build_timestamp.getVal<u64>());
    } else if(Osu::isBleedingEdge()) {
        BanchoState::neomod_version = fmt::format("bleedingedge-{}", cv::build_timestamp.getVal<u64>());
    } else {
        BanchoState::neomod_version = fmt::format("release-{:.2f}", cv::version.getFloat());
    }

    BanchoState::user_agent = "Mozilla/5.0 (compatible; " PACKAGE_NAME "/";
    BanchoState::user_agent.append(BanchoState::neomod_version);
    BanchoState::user_agent.append("; " OS_NAME "; +https://" NEOMOD_DOMAIN "/)");

    // Convar callbacks that should be set after loading the config
    cv::mod_mafham.setCallback(SA::MakeDelegate<&Osu::rebuildRenderTargets>(this));
    cv::mod_fposu.setCallback(SA::MakeDelegate<&Osu::rebuildRenderTargets>(this));
    cv::playfield_mirror_horizontal.setCallback(SA::MakeDelegate<&Osu::updateModsForConVarTemplate>(this));
    cv::playfield_mirror_vertical.setCallback(SA::MakeDelegate<&Osu::updateModsForConVarTemplate>(this));
    cv::playfield_rotation.setCallback(SA::MakeDelegate<&Osu::onPlayfieldChange>(this));
    cv::speed_override.setCallback(SA::MakeDelegate<&Osu::onSpeedChange>(this));
    cv::mod_doubletime_dummy.setCallback(
        [] { cv::speed_override.setValue(cv::mod_doubletime_dummy.getBool() ? "1.5" : "-1.0"); });
    cv::mod_halftime_dummy.setCallback(
        [] { cv::speed_override.setValue(cv::mod_halftime_dummy.getBool() ? "0.75" : "-1.0"); });
    cv::draw_songbrowser_thumbnails.setCallback(SA::MakeDelegate<&Osu::onThumbnailsToggle>(this));
    cv::bleedingedge.setCallback(SA::MakeDelegate<&UpdateHandler::onBleedingEdgeChanged>(this->updateHandler.get()));

    // These mods conflict with each other, prevent them from being enabled at the same time
    // TODO: allow fullalternate, needs extra logic to detect whether player is using 2K/3K/4K
    cv::mod_fullalternate.setCallback([] {
        if(!cv::mod_fullalternate.getBool()) return;
        cv::mod_no_keylock.setValue(false);
        cv::mod_singletap.setValue(false);
        osu && osu->UIReady() ? ui->getModSelector()->updateExperimentalButtons() : (void)0;
    });
    cv::mod_singletap.setCallback([] {
        if(!cv::mod_singletap.getBool()) return;
        cv::mod_fullalternate.setValue(false);
        cv::mod_no_keylock.setValue(false);
        osu && osu->UIReady() ? ui->getModSelector()->updateExperimentalButtons() : (void)0;
    });
    cv::mod_no_keylock.setCallback([] {
        if(!cv::mod_no_keylock.getBool()) return;
        cv::mod_fullalternate.setValue(false);
        cv::mod_singletap.setValue(false);
        osu && osu->UIReady() ? ui->getModSelector()->updateExperimentalButtons() : (void)0;
    });

    this->prevUIScale = Osu::getUIScale();

    // load global resources
    const int baseDPI = 96;
    const int newDPI = Osu::getUIScale() * baseDPI;

    McFont *defaultFont = resourceManager->loadFont("weblysleekuisb.ttf", "FONT_DEFAULT", 15, true, newDPI);
    this->titleFont = resourceManager->loadFont("SourceSansPro-Semibold.otf", "FONT_OSU_TITLE", 60, true, newDPI);
    this->subTitleFont = resourceManager->loadFont("SourceSansPro-Semibold.otf", "FONT_OSU_SUBTITLE", 21, true, newDPI);
    this->songBrowserFont =
        resourceManager->loadFont("SourceSansPro-Regular.otf", "FONT_OSU_SONGBROWSER", 35, true, newDPI);
    this->songBrowserFontBold =
        resourceManager->loadFont("SourceSansPro-Bold.otf", "FONT_OSU_SONGBROWSER_BOLD", 30, true, newDPI);

    {
        const std::string newIconFontPath = MCENGINE_FONTS_PATH "/forkawesome.ttf";
        const std::string oldIconFontPath = MCENGINE_FONTS_PATH "/forkawesome-webfont.ttf";
        if(!Environment::fileExists(newIconFontPath) && Environment::fileExists(oldIconFontPath)) {
            Environment::renameFile(oldIconFontPath, newIconFontPath);
        }
    }
    this->fontIcons = resourceManager->loadFont("forkawesome.ttf", "FONT_OSU_ICONS", Icons::icons, 26, true, newDPI);

    this->fonts.push_back(defaultFont);
    this->fonts.push_back(this->titleFont);
    this->fonts.push_back(this->subTitleFont);
    this->fonts.push_back(this->songBrowserFont);
    this->fonts.push_back(this->songBrowserFontBold);
    this->fonts.push_back(this->fontIcons);

    float averageIconHeight = 0.0f;
    for(char16_t icon : Icons::icons) {
        const float height = this->fontIcons->getGlyphHeight(icon);
        if(height > averageIconHeight) averageIconHeight = height;
    }
    this->fontIcons->setHeight(averageIconHeight);

    if(defaultFont->getDPI() != newDPI) {
        this->bFontReloadScheduled = true;
        this->last_res_change_req_src |= R_MISC_MANUAL;
    }

    // load ui
    this->userButton = std::make_unique<UserCard>(BanchoState::get_uid());

    this->bUILoaded = ui->init();
}

void Osu::doDeferredInitTasks() {
    // do this after reading configs if we wanted to set a windowed resolution
    if(this->last_res_change_req_src & R_CV_WINDOWED_RESOLUTION) {
        this->onWindowedResolutionChanged(cv::windowed_resolution.getString());
    }

    // show screen
    ui->show();

    // update mod settings
    this->updateMods();

    // Init online functionality (multiplayer/leaderboards/etc)
    if(cv::mp_autologin.getBool()) {
        BanchoState::reconnect();
    }

    if constexpr(!Env::cfg(BUILD::DEBUG) && !Env::cfg(OS::WASM)) {  // don't auto-update debug/web builds
        // don't auto update if this env var is set to anything other than 0 or empty (if it is set)
        std::string extUpdater = Environment::getEnvVariable("NEOMOD_EXTERNAL_UPDATE_PROVIDER");
        if(extUpdater.empty()) {
            extUpdater = Environment::getEnvVariable("NEOSU_EXTERNAL_UPDATE_PROVIDER");
        }
        if(cv::auto_update.getBool() &&
           (extUpdater.empty() || extUpdater == "0" || SString::to_lower(extUpdater) == "false")) {
            bool force_update = cv::bleedingedge.getBool() != cv::is_bleedingedge.getBool();
            this->updateHandler->checkForUpdates(force_update);
        }
    }

    // now handle commandline arguments after we have loaded everything
    env->getEnvInterop().handle_cmdline_args();

    // extract osks & watch for osks to extract
    {
        const auto osks = env->getFilesInFolder(NEOMOD_SKINS_PATH "/");
        for(const auto &file : osks) {
            if(env->getFileExtensionFromFilePath(file) != "osk") continue;
            auto path = NEOMOD_SKINS_PATH "/" + file;
            const bool extracted = env->getEnvInterop().handle_osk(path.c_str());
            if(extracted) env->deleteFile(path);
        }

        directoryWatcher->watch_directory(NEOMOD_SKINS_PATH "/", [](const FileChangeEvent &ev) -> void {
            if(ev.type != FileChangeType::CREATED) return;
            logRaw("[DirectoryWatcher] Importing new skin {}: type {}", ev.path, static_cast<u32>(ev.type));
            if(env->getFileExtensionFromFilePath(ev.path) != "osk") return;
            const bool extracted = env->getEnvInterop().handle_osk(ev.path.c_str());
            if(extracted) env->deleteFile(ev.path);
        });
    }

    env->setCursorVisible(!this->internalRect.contains(mouse->getPos()));
}

Osu::~Osu() {
    // remove mouse listener status (TODO: don't register ourselves explicitly, let apprunner do that)
    {
        mouse->removeListener(this);
        // another piece of global state, don't forget!
        // this entire mouse offset/scale handling being done in-app instead of in-engine causes more trouble than it's worth
        mouse->setOffset({0.f, 0.f});
        mouse->setScale({1.f, 1.f});
    }

    // remove soundengine callbacks (so it doesnt try to call them after we are destroyed)
    soundEngine->setDeviceChangeBeforeCallback({});
    soundEngine->setDeviceChangeAfterCallback({});

    BatchDiffCalc::abort_calc();
    AsyncPPC::set_map(nullptr);
    VolNormalization::shutdown();
    BANCHO::Net::cleanup_networking();

    // destroy playing music
    if(this->map_iface && this->map_iface->getMusic()) {
        resourceManager->destroyResource(this->map_iface->getMusic(), ResourceDestroyFlags::RDF_FORCE_BLOCKING);
        this->map_iface.reset();
    }

    // clear main menu maps early, just in case
    if(this->UIReady()) {
        this->ui_memb->getMainMenu()->clearPreloadedMaps();
    }

    this->bUILoaded = false;
    this->ui_memb.reset();  // destroy ui layers
    ui = nullptr;
    db.reset();  // shutdown db

    // remove the static callbacks
    cvars().setCVSubmittableCheckFunc({});
    ConVar::setOnSetValueGameplayCallback({});
    ConVar::setOnGetValueProtectedCallback({});
    ConVar::setOnSetValueProtectedCallback({});

    // destroy all skin sounds (and potentially loading skin), then skin
    if(this->skinScheduledToLoad && this->skinScheduledToLoad != this->skin.get()) {
        for(auto *sound : this->skinScheduledToLoad->sounds) {
            resourceManager->destroyResource(sound, ResourceDestroyFlags::RDF_FORCE_BLOCKING);
        }
        this->skinScheduledToLoad->sounds.clear();
        SAFE_DELETE(this->skinScheduledToLoad);
    }
    if(this->skin) {
        for(auto *sound : this->skin->sounds) {
            resourceManager->destroyResource(sound, ResourceDestroyFlags::RDF_FORCE_BLOCKING);
        }
        this->skin->sounds.clear();
        this->skin.reset();
    }

    // destroy rendertargets
    resourceManager->destroyResource(this->backBuffer, ResourceDestroyFlags::RDF_FORCE_BLOCKING);
    this->backBuffer = nullptr;
    resourceManager->destroyResource(this->playfieldBuffer, ResourceDestroyFlags::RDF_FORCE_BLOCKING);
    this->playfieldBuffer = nullptr;
    resourceManager->destroyResource(this->sliderFrameBuffer, ResourceDestroyFlags::RDF_FORCE_BLOCKING);
    this->sliderFrameBuffer = nullptr;
    resourceManager->destroyResource(this->AAFrameBuffer, ResourceDestroyFlags::RDF_FORCE_BLOCKING);
    this->AAFrameBuffer = nullptr;
    resourceManager->destroyResource(this->frameBuffer, ResourceDestroyFlags::RDF_FORCE_BLOCKING);
    this->frameBuffer = nullptr;
    resourceManager->destroyResource(this->frameBuffer2, ResourceDestroyFlags::RDF_FORCE_BLOCKING);
    this->frameBuffer2 = nullptr;

    // release cursor
    env->setCursorClip(false, {});
    env->setCursorVisible(true);

    // TODO: this is disgusting and completely manual, but required if we want to support re-entrancy
    {
        cv::background_image_cache_size.reset();
        cv::background_image_eviction_delay_frames.reset();
        cv::background_image_loading_delay.reset();
        cv::load_beatmap_background_images.reset();
        cv::cmd::save.reset();
        cv::cursor_trail_max_size.reset();
        cv::skin_use_skin_hitsounds.reset();
        cv::options_slider_quality.reset();
        cv::options_high_quality_sliders.reset();
        cv::rich_presence.reset();
        cv::rich_presence_map_backgrounds.reset();
        cv::snd_soloud_backend.reset();
        cv::loudness_calc_threads.reset();
        cv::songbrowser_search_hardcoded_filter.reset();
        cv::volume_master.reset();
        cv::volume_effects.reset();
        cv::volume_music.reset();
        cv::hud_volume_size_multiplier.reset();
        cv::resolution.reset();
        cv::letterboxed_resolution.reset();
        cv::windowed_resolution.reset();
        cv::animation_speed_override.reset();
        cv::ui_scale.reset();
        cv::ui_scale_to_dpi.reset();
        cv::letterboxing.reset();
        cv::letterboxing_offset_x.reset();
        cv::letterboxing_offset_y.reset();
        cv::confine_cursor_windowed.reset();
        cv::confine_cursor_fullscreen.reset();
        cv::confine_cursor_never.reset();
        cv::osu_folder.reset();
        cv::slider_curve_points_separation.reset();
        cv::skin.reset();
        cv::skin_reload.reset();
        cv::mod_mafham.reset();
        cv::mod_fposu.reset();
        cv::playfield_mirror_horizontal.reset();
        cv::playfield_mirror_vertical.reset();
        cv::playfield_rotation.reset();
        cv::speed_override.reset();
        cv::mod_doubletime_dummy.reset();
        cv::mod_halftime_dummy.reset();
        cv::draw_songbrowser_thumbnails.reset();
        cv::bleedingedge.reset();
        cv::mod_fullalternate.reset();
        cv::mod_singletap.reset();
        cv::mod_no_keylock.reset();
        cv::snd_freq.reset();
        cv::win_snd_wasapi_exclusive.reset();
        cv::win_snd_wasapi_buffer_size.reset();
        cv::win_snd_wasapi_period_size.reset();
        cv::win_snd_wasapi_event_callbacks.reset();
        cv::asio_buffer_size.reset();
        cv::snd_output_device.reset();
        cv::win_global_media_hotkeys.reset();
    }
    // "osu" will be set to null when global_osu_ is deleted (at the end of all automatically deleted members)
}

void Osu::draw() {
    if(!this->skin.get())  // sanity check
    {
        g->setColor(0xff000000);
        g->fillRect(0, 0, this->getVirtScreenWidth(), this->getVirtScreenHeight());
        if(ui && ui->getMainMenu() && this->backgroundImageHandler && this->map_iface->getBeatmap()) {
            // try at least drawing background image during early loading
            ui->getMainMenu()->draw();
        }
        return;
    }

    ui->draw();
}

void Osu::update() {
    if(unlikely(!this->UIReady())) return;  // TODO: guarantee that this can't happen
    if(unlikely(!this->bFirstUpdateTasksDone)) {
        this->bFirstUpdateTasksDone = true;
        this->doDeferredInitTasks();
    }

    if(this->skin.get()) {
        this->skin->update(this->isInPlayMode(), this->map_iface->isPlaying(),
                           this->map_iface->getCurMusicPosWithOffsets());
    }

    this->fposu->update();

    // only update if not playing (and online)
    // TODO: offline/local avatars?
    if(BanchoState::is_online() &&
       // XXX: there are too many flags/states to individually check whether or not we're in a "low-latency session" or not
       (!this->isInPlayModeAndNotPaused() || this->map_iface->isInBreak() || this->map_iface->isInSkippableSection())) {
        this->thumbnailManager->update();
    }

    ui->update();

    if(this->music_unpause_scheduled && soundEngine->isReady()) {
        if(Sound *music = this->map_iface->getMusic(); music && !music->isPlaying()) {
            soundEngine->play(music);
        }
        this->music_unpause_scheduled = false;
    }

    // main playfield update
    this->bSeeking = false;
    if(this->isInPlayMode()) {
        this->map_iface->update();

        // NOTE: force keep loaded background images while playing
        this->backgroundImageHandler->scheduleFreezeCache();

        // skip button clicking
        bool can_skip = this->map_iface->isInSkippableSection() && !this->bClickedSkipButton;
        can_skip &= !this->map_iface->isPaused() && !ui->getVolumeOverlay()->isBusy();
        if(can_skip) {
            const bool isAnyKeyDown = (this->map_iface->isClickHeld() || mouse->isLeftDown());
            if(isAnyKeyDown) {
                if(ui->getHUD()->getSkipClickRect().contains(mouse->getPos())) {
                    if(!this->bSkipScheduled) {
                        this->bSkipScheduled = true;
                        this->bClickedSkipButton = true;

                        if(BanchoState::is_playing_a_multi_map()) {
                            Packet packet;
                            packet.id = OUTP_MATCH_SKIP_REQUEST;
                            BANCHO::Net::send_packet(packet);
                        }
                    }
                }
            }
        }

        // skipping
        if(this->bSkipScheduled) {
            const bool isLoading = this->map_iface->isLoading();

            if(this->map_iface->isInSkippableSection() && !this->map_iface->isPaused() && !isLoading) {
                bool can_skip_intro = (cv::skip_intro_enabled.getBool() && this->map_iface->iCurrentHitObjectIndex < 1);
                bool can_skip_break =
                    (cv::skip_breaks_enabled.getBool() && this->map_iface->iCurrentHitObjectIndex > 0);
                if(BanchoState::is_playing_a_multi_map()) {
                    can_skip_intro = this->map_iface->all_players_skipped;
                    can_skip_break = false;
                }

                if(can_skip_intro || can_skip_break) {
                    this->map_iface->skipEmptySection();
                }
            }

            if(!isLoading) this->bSkipScheduled = false;
        }

        // Reset m_bClickedSkipButton on mouse up
        // We only use m_bClickedSkipButton to prevent seeking when clicking the skip button
        if(this->bClickedSkipButton && !this->map_iface->isInSkippableSection()) {
            if(!mouse->isLeftDown()) {
                this->bClickedSkipButton = false;
            }
        }

        // scrubbing/seeking
        this->bSeeking = (this->bSeekKey ||
                          // only auto-seek if not paused (or we immediately seek when unpausing)
                          (this->map_iface->is_watching && !this->map_iface->isActuallyPausedAndNotSpectating()))  //
                         && (!ui->getVolumeOverlay()->isBusy())                                                    //
                         && (!BanchoState::is_playing_a_multi_map() && !this->bClickedSkipButton)                  //
                         && (!BanchoState::spectating);                                                            //
        if(this->bSeeking) {
            f32 mousePosX = std::round(mouse->getPos().x);
            f64 percent = std::clamp<f64>(mousePosX / (f64)this->internalRect.getWidth(), 0., 1.);
            u32 seek_to_ms = static_cast<u32>(std::round(
                percent * (f64)(this->map_iface->getStartTimePlayable() + this->map_iface->getLengthPlayable())));

            if(mouse->isLeftDown()) {
                if(mousePosX != this->fPrevSeekMousePosX || !cv::scrubbing_smooth.getBool()) {
                    this->fPrevSeekMousePosX = mousePosX;

                    // special case: allow cancelling the failing animation here
                    if(this->map_iface->hasFailed()) this->map_iface->cancelFailing();

                    this->map_iface->seekMS(seek_to_ms);
                }
            } else {
                this->fPrevSeekMousePosX = -1.0f;
            }

            if(mouse->isRightDown()) {
                this->iQuickSaveMS = seek_to_ms;
            }
        }

        // quick retry timer
        if(this->bQuickRetryDown && this->fQuickRetryTime != 0.0f && engine->getTime() > this->fQuickRetryTime) {
            this->fQuickRetryTime = 0.0f;

            if(!BanchoState::is_playing_a_multi_map()) {
                this->map_iface->restart(true);
                this->map_iface->update();
                ui->getPauseOverlay()->setVisible(false);
            }
        }
    }

    // background image cache tick
    // NOTE: must be before the asynchronous ui toggles due to potential 1-frame unloads after invisible songbrowser
    this->backgroundImageHandler->update(ui->getSongBrowser()->isVisible());

    this->updateCursorVisibility();

    // endless mod
    if(this->bScheduleEndlessModNextBeatmap) {
        this->bScheduleEndlessModNextBeatmap = false;
        ui->getSongBrowser()->playNextRandomBeatmap();
    }

    // multiplayer/networking update
    {
        VPROF_BUDGET_DBG("Bancho::update", VPROF_BUDGETGROUP_UPDATE);
        BANCHO::Net::update_networking();
    }

    // skin async loading
    if(this->bSkinLoadScheduled) {
        if((!this->skin.get() || this->skin->isReady()) && this->skinScheduledToLoad != nullptr &&
           this->skinScheduledToLoad->isReady()) {
            this->bSkinLoadScheduled = false;

            if(this->skin.get() != this->skinScheduledToLoad) {
                this->skin.reset(this->skinScheduledToLoad);
            }

            this->skinScheduledToLoad = nullptr;

            // force effect volume update now that the new skin's sounds are loaded
            ui->getVolumeOverlay()->updateEffectVolume(this->skin.get());

            // force layout update after all skin elements have been loaded
            this->last_res_change_req_src |= R_MISC_MANUAL;

            // notify if done after reload
            if(this->bSkinLoadWasReload) {
                this->bSkinLoadWasReload = false;

                ui->getNotificationOverlay()->addNotification(
                    this->skin->name.length() > 0 ? fmt::format("Skin reloaded! ({})", this->skin->name.c_str())
                                                  : US_("Skin reloaded!"),
                    0xffffffff, false, 0.75f);
            }
        }
    }

    // (must be before m_bFontReloadScheduled and m_bFireResolutionChangedScheduled are handled!)
    if(this->last_res_change_req_src & R_DELAYED_DESYNC_FIX) {
        this->last_res_change_req_src = (this->last_res_change_req_src & ~R_DELAYED_DESYNC_FIX) | R_MISC_MANUAL;
        this->bFontReloadScheduled = true;
    }

    // delayed font reloads (must be before layout updates!)
    if(this->bFontReloadScheduled) {
        this->bFontReloadScheduled = false;
        this->reloadFonts();
    }

    // delayed layout updates
    // ignore CV_WINDOWED_RESOLUTION since that will come from the window resize
    if(this->last_res_change_req_src & ~(R_ENGINE | R_NOT_PENDING | R_CV_WINDOWED_RESOLUTION)) {
        this->onResolutionChanged(this->getVirtScreenSize(), this->last_res_change_req_src);
    }
}

bool Osu::isInPlayModeAndNotPaused() const {
    // very elegant
    return this->isInPlayMode() && !(this->map_iface->isPaused() ||   //
                                     this->map_iface->isLoading() ||  //
                                     this->ui_memb->getPauseOverlay()->isVisible());
}

void Osu::updateMods() {
    this->score->mods = Replay::Mods::from_cvars();
    this->score->setCheated();

    // this is called 3 times in succession with the same mods
    // when enabling a mod selector button and 2 times when disabling them
    // debugLog("updating mods: {}", this->score->getModsStringForRichPresence());
    // MC_DO_BACKTRACE;

    {
        auto idx = StarPrecalc::index_of(this->score->mods.flags, this->score->mods.speed);
        StarPrecalc::active_idx = (idx != StarPrecalc::INVALID_MODCOMBO) ? (u8)idx : StarPrecalc::NOMOD_1X_INDEX;
    }

    if(this->isInPlayMode()) {
        // notify the possibly running playfield of mod changes
        // e.g. recalculating stacks dynamically if HR is toggled
        this->map_iface->onModUpdate();
    } else {
        // onModUpdate already updates speed
        // do this to update nightcore/dt things outside gameplay
        this->map_iface->setMusicSpeed(this->map_iface->getSpeedMultiplier());
    }

    // handle windows key disable/enable
    this->updateWindowsKeyDisable();
}

void Osu::onKeyDown(KeyboardEvent &key) {
    // global hotkeys

    // global hotkey
    if(key == KEY_O && keyboard->isControlDown()) {
        ui->getOptionsOverlay()->setVisible(!ui->getOptionsOverlay()->isVisible());
        key.consume();
        return;
    }

    // special hotkeys
    // reload & recompile shaders
    if(keyboard->isAltDown() && keyboard->isControlDown() && key == KEY_R) {
        // only non-repeat keypresses
        if(!key.isRepeat()) {
            Shader *sliderShader = resourceManager->getShader("slider");
            Shader *cursorTrailShader = resourceManager->getShader("cursortrail");

            if(sliderShader != nullptr) sliderShader->reload();
            if(cursorTrailShader != nullptr) cursorTrailShader->reload();
        }
        key.consume();
    }

    // reload skin (alt)
    if(keyboard->isAltDown() && keyboard->isControlDown() && key == KEY_S) {
        // only non-repeat keypresses
        if(!key.isRepeat()) {
            this->onSkinReload();
        }
        key.consume();
    }

    if(key == cv::OPEN_SKIN_SELECT_MENU.getVal<SCANCODE>()) {
        ui->getOptionsOverlay()->onSkinSelect();
        key.consume();
        return;
    }

    // disable mouse buttons hotkey
    if(key == cv::DISABLE_MOUSE_BUTTONS.getVal<SCANCODE>()) {
        if(cv::disable_mousebuttons.getBool()) {
            cv::disable_mousebuttons.setValue(0.0f);
            ui->getNotificationOverlay()->addNotification("Mouse buttons are enabled.");
        } else {
            cv::disable_mousebuttons.setValue(1.0f);
            ui->getNotificationOverlay()->addNotification("Mouse buttons are disabled.");
        }
    }

    if(key == cv::TOGGLE_MAP_BACKGROUND.getVal<SCANCODE>()) {
        auto diff = this->map_iface->getBeatmap();
        if(!diff) {
            ui->getNotificationOverlay()->addNotification("No beatmap is currently selected.");
        } else {
            diff->draw_background = !diff->draw_background;
            db->update_overrides(diff);
            DiscRPC::clear_activity();
        }
        key.consume();
        return;
    }

    // F8 toggle chat
    if(key == cv::TOGGLE_CHAT.getVal<SCANCODE>()) {
        if(!BanchoState::is_online()) {
            ui->getOptionsOverlay()->askForLoginDetails();
        } else if(ui->getOptionsOverlay()->isVisible()) {
            // When options menu is open, instead of toggling chat, always open chat
            ui->getOptionsOverlay()->setVisible(false);
            ui->getChat()->user_wants_chat = true;
            ui->getChat()->updateVisibility();
        } else {
            ui->getChat()->user_wants_chat = !ui->getChat()->user_wants_chat;
            ui->getChat()->updateVisibility();
        }
    }

    // F9 toggle extended chat
    if(key == cv::TOGGLE_EXTENDED_CHAT.getVal<SCANCODE>()) {
        if(!BanchoState::is_online()) {
            ui->getOptionsOverlay()->askForLoginDetails();
        } else if(ui->getOptionsOverlay()->isVisible()) {
            // When options menu is open, instead of toggling extended chat, always enable it
            ui->getOptionsOverlay()->setVisible(false);
            ui->getChat()->user_wants_chat = true;
            ui->getChat()->user_list->setVisible(true);
            ui->getChat()->updateVisibility();
        } else {
            if(ui->getChat()->user_wants_chat) {
                ui->getChat()->user_list->setVisible(!ui->getChat()->user_list->isVisible());
                ui->getChat()->updateVisibility();
            } else {
                ui->getChat()->user_wants_chat = true;
                ui->getChat()->user_list->setVisible(true);
                ui->getChat()->updateVisibility();
            }
        }
    }

    // screenshots
    if(key == cv::SAVE_SCREENSHOT.getVal<SCANCODE>() && cv::enable_screenshots.getBool()) {
        if(!key.isRepeat()) {
            this->saveScreenshot();
        }
        key.consume();
    }

    // boss key (minimize + mute)
    if(key == cv::BOSS_KEY.getVal<SCANCODE>()) {
        if(env->minimizeWindow()) {
            this->bWasBossKeyPaused = this->map_iface->isPreviewMusicPlaying();
            this->map_iface->pausePreviewMusic(false);
        }
    }

    // local hotkeys (and gameplay keys)

    // while playing (and not in options)
    if(this->isInPlayMode() && !ui->getOptionsOverlay()->isVisible() && !ui->getChat()->isVisible()) {
        // instant replay
        if((this->map_iface->isPaused() || this->map_iface->hasFailed())) {
            if(!key.isConsumed() && key == cv::INSTANT_REPLAY.getVal<SCANCODE>()) {
                if(!this->map_iface->is_watching && !BanchoState::spectating) {
                    FinishedScore score;
                    score.replay = this->map_iface->live_replay;
                    score.beatmap_hash = this->map_iface->getBeatmap()->getMD5();
                    score.mods = this->score->mods;

                    score.playerName = BanchoState::get_username();
                    score.player_id = std::max(0, BanchoState::get_uid());

                    f64 pos_seconds = this->map_iface->getTime() - cv::instant_replay_duration.getFloat();
                    u32 pos_ms = (u32)(std::max(0.0, pos_seconds) * 1000.0);
                    this->map_iface->cancelFailing();
                    this->map_iface->watch(score, pos_ms);
                    return;
                }
            }
        }

        // while playing and not paused
        if(!this->map_iface->isPaused()) {
            // gameplay keys + smoke
            {
                GameplayKeys gameplayKeyPressed{0};

                if(key == cv::LEFT_CLICK.getVal<SCANCODE>()) {
                    gameplayKeyPressed = GameplayKeys::K1;
                } else if(key == cv::LEFT_CLICK_2.getVal<SCANCODE>()) {
                    gameplayKeyPressed = GameplayKeys::M1;
                } else if(key == cv::RIGHT_CLICK.getVal<SCANCODE>()) {
                    gameplayKeyPressed = GameplayKeys::K2;
                } else if(key == cv::RIGHT_CLICK_2.getVal<SCANCODE>()) {
                    gameplayKeyPressed = GameplayKeys::M2;
                } else if(key == cv::SMOKE.getVal<SCANCODE>()) {
                    gameplayKeyPressed = GameplayKeys::Smoke;
                }

                if(gameplayKeyPressed > 0) {
                    this->onGameplayKey(gameplayKeyPressed, true, key.getTimestamp());
                    // consume if not failed
                    if(!this->map_iface->hasFailed()) {
                        key.consume();
                    }
                }
            }

            // handle skipping
            if(key == KEY_ENTER || key == KEY_NUMPAD_ENTER || key == cv::SKIP_CUTSCENE.getVal<SCANCODE>())
                this->bSkipScheduled = true;

            // toggle ui
            if(!key.isConsumed() && key == cv::TOGGLE_SCOREBOARD.getVal<SCANCODE>() && !this->bScoreboardToggleCheck) {
                this->bScoreboardToggleCheck = true;

                if(keyboard->isShiftDown()) {
                    if(!this->bUIToggleCheck) {
                        this->bUIToggleCheck = true;
                        cv::draw_hud.setValue(!cv::draw_hud.getBool());
                        ui->getNotificationOverlay()->addNotification(cv::draw_hud.getBool()
                                                                          ? "In-game interface has been enabled."
                                                                          : "In-game interface has been disabled.",
                                                                      0xffffffff, false, 0.1f);

                        key.consume();
                    }
                } else {
                    if(BanchoState::is_playing_a_multi_map()) {
                        cv::draw_scoreboard_mp.setValue(!cv::draw_scoreboard_mp.getBool());
                        ui->getNotificationOverlay()->addNotification(
                            cv::draw_scoreboard_mp.getBool() ? "Scoreboard is shown." : "Scoreboard is hidden.",
                            0xffffffff, false, 0.1f);
                    } else {
                        cv::draw_scoreboard.setValue(!cv::draw_scoreboard.getBool());
                        ui->getNotificationOverlay()->addNotification(
                            cv::draw_scoreboard.getBool() ? "Scoreboard is shown." : "Scoreboard is hidden.",
                            0xffffffff, false, 0.1f);
                    }

                    key.consume();
                }
            }

            // allow live mod changing while playing
            if(!key.isConsumed() && (key == KEY_F1 || key == cv::TOGGLE_MODSELECT.getVal<SCANCODE>()) &&
               ((KEY_F1 != cv::LEFT_CLICK.getVal<SCANCODE>() && KEY_F1 != cv::LEFT_CLICK_2.getVal<SCANCODE>()) ||
                !(this->map_iface->getKeys() & (GameplayKeys::K1 | GameplayKeys::M1))) &&
               ((KEY_F1 != cv::RIGHT_CLICK.getVal<SCANCODE>() && KEY_F1 != cv::RIGHT_CLICK_2.getVal<SCANCODE>()) ||
                !(this->map_iface->getKeys() & (GameplayKeys::K2 | GameplayKeys::M2))) &&
               !this->bF1 && !this->map_iface->hasFailed() &&
               !BanchoState::is_playing_a_multi_map())  // only if not failed though
            {
                this->bF1 = true;
                ui->getModSelector()->setVisible(!ui->getModSelector()->isVisible());
            }

            // quick save/load
            if(!BanchoState::is_playing_a_multi_map()) {
                if(key == cv::QUICK_SAVE.getVal<SCANCODE>()) this->iQuickSaveMS = this->map_iface->getTime();

                if(key == cv::QUICK_LOAD.getVal<SCANCODE>()) {
                    // special case: allow cancelling the failing animation here
                    if(this->map_iface->hasFailed()) this->map_iface->cancelFailing();

                    this->map_iface->seekMS(this->iQuickSaveMS);
                }
            }

            // quick seek
            if(!BanchoState::is_playing_a_multi_map()) {
                const bool backward = (key == cv::SEEK_TIME_BACKWARD.getVal<SCANCODE>());
                const bool forward = (key == cv::SEEK_TIME_FORWARD.getVal<SCANCODE>());
                if(backward || forward) {
                    i32 diff = 0;
                    if(backward) diff -= cv::seek_delta.getInt();
                    if(forward) diff += cv::seek_delta.getInt();
                    if(diff != 0) {
                        // special case: allow cancelling the failing animation here
                        if(this->map_iface->hasFailed()) this->map_iface->cancelFailing();

                        this->map_iface->seekMS(this->map_iface->getTime() + diff);
                    }
                }
            }
        }

        // while paused or maybe not paused

        // handle quick restart
        if(((key == cv::QUICK_RETRY.getVal<SCANCODE>() ||
             (keyboard->isControlDown() && !keyboard->isAltDown() && key == KEY_R)) &&
            !this->bQuickRetryDown)) {
            this->bQuickRetryDown = true;
            this->fQuickRetryTime = engine->getTime() + cv::quick_retry_delay.getFloat();
        }

        // handle seeking
        if(key == cv::SEEK_TIME.getVal<SCANCODE>()) this->bSeekKey = true;

        // handle fposu key handling
        this->fposu->onKeyDown(key);
    }

    ui->onKeyDown(key);

    // special handling, after subsystems, if still not consumed, if playing
    bool handle = !key.isConsumed() && this->isInPlayMode();
    while(handle) {
        handle = false;
        // toggle pause menu
        // ignore repeat events when key is held down
        const bool pressed_pause =
            ((key == cv::GAME_PAUSE.getVal<SCANCODE>()) || (key == KEY_ESCAPE)) && !key.isRepeat();
        if(pressed_pause) {
            key.consume();

            // bit of a misnomer, this pauses OR unpauses the music OR stops if it was still loading/waiting
            this->map_iface->pause();
            if(!this->isInPlayMode()) break;  // if we exit due to the "pause", don't do anything else

            if(ui->getPauseOverlay()->isVisible() && this->map_iface->hasFailed()) {
                // quit if we try to 'escape' the pause menu when dead (satisfying ragequit mechanic)
                this->map_iface->stop(true);
            } else {
                // else just toggle the pause menu
                ui->getPauseOverlay()->setVisible(!ui->getPauseOverlay()->isVisible());
            }
        }

        // local offset
        if(key == cv::INCREASE_LOCAL_OFFSET.getVal<SCANCODE>()) {
            DatabaseBeatmap *curMap = this->map_iface->getBeatmap();

            i32 offsetAdd = keyboard->isAltDown() ? 1 : 5;
            curMap->setLocalOffset(curMap->getLocalOffset() + offsetAdd);
            db->update_overrides(curMap);
            ui->getNotificationOverlay()->addNotification(
                fmt::format("Local beatmap offset set to {} ms", curMap->getLocalOffset()));
        }
        if(key == cv::DECREASE_LOCAL_OFFSET.getVal<SCANCODE>()) {
            DatabaseBeatmap *curMap = this->map_iface->getBeatmap();

            i32 offsetAdd = -(keyboard->isAltDown() ? 1 : 5);
            curMap->setLocalOffset(curMap->getLocalOffset() + offsetAdd);
            db->update_overrides(curMap);
            ui->getNotificationOverlay()->addNotification(
                fmt::format("Local beatmap offset set to {} ms", curMap->getLocalOffset()));
        }
    }
}

void Osu::onKeyUp(KeyboardEvent &key) {
    // clicks + smoke
    {
        GameplayKeys gameplayKeyReleased{0};

        if(key == cv::LEFT_CLICK.getVal<SCANCODE>()) {
            gameplayKeyReleased = GameplayKeys::K1;
        } else if(key == cv::LEFT_CLICK_2.getVal<SCANCODE>()) {
            gameplayKeyReleased = GameplayKeys::M1;
        } else if(key == cv::RIGHT_CLICK.getVal<SCANCODE>()) {
            gameplayKeyReleased = GameplayKeys::K2;
        } else if(key == cv::RIGHT_CLICK_2.getVal<SCANCODE>()) {
            gameplayKeyReleased = GameplayKeys::M2;
        } else if(key == cv::SMOKE.getVal<SCANCODE>()) {
            gameplayKeyReleased = GameplayKeys::Smoke;
        }

        // even if not playing, update currently held keys
        if(gameplayKeyReleased > 0) {
            this->onGameplayKey(gameplayKeyReleased, false, key.getTimestamp());
            // don't consume keyup
        }
    }

    ui->onKeyUp(key);

    // misc hotkeys release
    // XXX: handle keypresses in the engine, instead of doing this hacky mess
    if(key == KEY_F1 || key == cv::TOGGLE_MODSELECT.getVal<SCANCODE>()) this->bF1 = false;
    if(key == KEY_LSHIFT || key == KEY_RSHIFT) this->bUIToggleCheck = false;
    if(key == cv::TOGGLE_SCOREBOARD.getVal<SCANCODE>()) {
        this->bScoreboardToggleCheck = false;
        this->bUIToggleCheck = false;
    }
    if(key == cv::QUICK_RETRY.getVal<SCANCODE>() || key == KEY_R) this->bQuickRetryDown = false;
    if(key == cv::SEEK_TIME.getVal<SCANCODE>()) this->bSeekKey = false;

    // handle fposu key handling
    this->fposu->onKeyUp(key);
}

void Osu::stealFocus() { this->UIReady() ? ui->stealFocus() : (void)0; }

void Osu::onChar(KeyboardEvent &e) { this->UIReady() ? ui->onChar(e) : (void)0; }

void Osu::onButtonChange(ButtonEvent ev) {
    using enum MouseButtonFlags;
    if(!(ev.btn & (MF_LEFT | MF_RIGHT)) ||
       (cv::disable_mousebuttons.getBool() && (this->isInPlayMode() && !this->map_iface->isPaused())))
        return;

    this->onGameplayKey(!!(ev.btn & MF_LEFT) ? GameplayKeys::M1 : GameplayKeys::M2, ev.down, ev.timestamp, true);
}

Sound *Osu::getSound(ActionSound action) const {
    const Skin *curSkin = this->skin.get();
    if(!curSkin) return nullptr;
    switch(action) {
        case ActionSound::DELETING_TEXT:
            return curSkin->s_deleting_text;
        case ActionSound::MOVE_TEXT_CURSOR:
            return curSkin->s_moving_text_cursor;
        case ActionSound::ADJUST_SLIDER:
            return curSkin->s_sliderbar;
        case ActionSound::TYPING1:
            return curSkin->s_typing1;
        case ActionSound::TYPING2:
            return curSkin->s_typing2;
        case ActionSound::TYPING3:
            return curSkin->s_typing3;
        case ActionSound::TYPING4:
            return curSkin->s_typing4;
    }
    std::unreachable();
}

void Osu::showNotification(const NotificationInfo &info) {
    NotificationOverlay *noverlay = ui ? ui->getNotificationOverlay() : nullptr;
    if(!noverlay) {
        debugLog(info.text);
        return;
    }

    if(info.nclass == NotificationClass::TOAST) {
        using enum NotificationPreset;
        switch(info.preset) {
            case CUSTOM:
                noverlay->addToast(info.text, info.custom_color, info.callback);
                return;
            case INFO:
                noverlay->addToast(info.text, INFO_TOAST, info.callback);
                return;
            case ERROR:
                noverlay->addToast(info.text, ERROR_TOAST, info.callback);
                return;
            case SUCCESS:
                noverlay->addToast(info.text, SUCCESS_TOAST, info.callback);
                return;
            case STATUS:
                noverlay->addToast(info.text, STATUS_TOAST, info.callback);
                return;
        }
        std::unreachable();
    } else if(info.nclass == NotificationClass::BANNER) {
        using enum NotificationPreset;
        // NOTE: currently abusing toast colors
        switch(info.preset) {
            case CUSTOM:
                noverlay->addNotification(info.text, info.custom_color, false, info.duration);
                return;
            case INFO:
                noverlay->addNotification(info.text, INFO_TOAST, false, info.duration);
                return;
            case ERROR:
                noverlay->addNotification(info.text, ERROR_TOAST, false, info.duration);
                return;
            case SUCCESS:
                noverlay->addNotification(info.text, SUCCESS_TOAST, false, info.duration);
                return;
            case STATUS:
                noverlay->addNotification(info.text, STATUS_TOAST, false, info.duration);
                return;
        }
        std::unreachable();
    }

    // "CUSTOM" class does nothing
    debugLog(info.text);
    return;
}

void Osu::reloadMapInterface() { this->map_iface = std::make_unique<BeatmapInterface>(); }

void Osu::saveScreenshot() {
    static i32 screenshotNumber = 0;

    if(!cv::enable_screenshots.getBool()) return;

    if(!env->directoryExists(NEOMOD_SCREENSHOTS_PATH) && !env->createDirectory(NEOMOD_SCREENSHOTS_PATH)) {
        ui->getNotificationOverlay()->addNotification("Error: Couldn't create screenshots folder.", 0xffff0000, false,
                                                      3.0f);
        return;
    }

    while(env->fileExists(fmt::format(NEOMOD_SCREENSHOTS_PATH "/screenshot{}.png", screenshotNumber)))
        screenshotNumber++;

    auto screenshotFilename = fmt::format(NEOMOD_SCREENSHOTS_PATH "/screenshot{}.png", screenshotNumber);
    constexpr u8 screenshotChannels{3};

    const auto saveFunc = [currentRes = this->internalRect, &skin = this->skin,
                           filename = std::move(screenshotFilename)](std::vector<u8> pixels) -> void {
        if(!osu) return;  // paranoia
        if(pixels.empty()) {
            static uint8_t once = 0;
            if(!once++)
                ui->getNotificationOverlay()->addNotification("Error: Couldn't grab a screenshot :(", 0xffff0000, false,
                                                              3.0f);
            debugLog("failed to get pixel data for screenshot");
            return;
        }

        const f32 outerWidth = g->getResolution().x;
        const f32 outerHeight = g->getResolution().y;
        const f32 innerWidth = currentRes.getWidth();
        const f32 innerHeight = currentRes.getHeight();

        if(skin) {
            soundEngine->play(skin->s_shutter);
        }
        ui->getNotificationOverlay()->addToast(fmt::format("Saved screenshot to {}", filename), CHAT_TOAST,
                                               [filename] { env->openFileBrowser(filename); });

        // don't need cropping
        if(!cv::crop_screenshots.getBool() || (g->getResolution() == currentRes.getSize())) {
            Image::saveToImage(pixels.data(), static_cast<i32>(outerWidth), static_cast<i32>(outerHeight),
                               screenshotChannels, filename);
            return;
        }

        // need cropping
        f32 offsetXpct = 0, offsetYpct = 0;
        if((g->getResolution() != currentRes.getSize()) && cv::letterboxing.getBool()) {
            offsetXpct = cv::letterboxing_offset_x.getFloat();
            offsetYpct = cv::letterboxing_offset_y.getFloat();
        }

        const i32 startX = std::clamp<i32>(static_cast<i32>((outerWidth - innerWidth) * (1 + offsetXpct) / 2), 0,
                                           static_cast<i32>(outerWidth - innerWidth));
        const i32 startY = std::clamp<i32>(static_cast<i32>((outerHeight - innerHeight) * (1 + offsetYpct) / 2), 0,
                                           static_cast<i32>(outerHeight - innerHeight));

        std::vector<u8> croppedPixels(static_cast<size_t>(innerWidth * innerHeight * screenshotChannels));

        for(sSz y = 0; y < static_cast<sSz>(innerHeight); ++y) {
            auto srcRowStart =
                pixels.begin() + ((startY + y) * static_cast<sSz>(outerWidth) + startX) * screenshotChannels;
            auto destRowStart = croppedPixels.begin() + (y * static_cast<sSz>(innerWidth)) * screenshotChannels;
            // copy the entire row
            std::ranges::copy_n(srcRowStart, static_cast<sSz>(innerWidth) * screenshotChannels, destRowStart);
        }

        Image::saveToImage(croppedPixels.data(), static_cast<i32>(innerWidth), static_cast<i32>(innerHeight),
                           screenshotChannels, filename);
    };

    g->takeScreenshot({.savePath = {}, .dataCB = std::move(saveFunc), .withAlpha = screenshotChannels > 3});
}

void Osu::onPlayEnd(const FinishedScore &score, bool quit) {
    cv::snd_change_check_interval.setValue(cv::snd_change_check_interval.getDefaultFloat());

    if(!quit && cv::mod_endless.getBool()) {
        this->bScheduleEndlessModNextBeatmap = true;
        return;  // nothing more to do here
    }

    if(quit) {
        ui->setScreen(ui->getSongBrowser());
    } else {
        ui->getRankingScreen()->setScore(score);
        ui->setScreen(ui->getRankingScreen());
        soundEngine->play(this->skin->s_applause);
    }

    ui->getSongBrowser()->onPlayEnd(quit);

    this->updateConfineCursor();
    this->updateWindowsKeyDisable();
}

float Osu::getDifficultyMultiplier() {
    if(cv::mod_easy.getBool())
        return .5f;
    else if(cv::mod_hardrock.getBool())
        return 1.4f;
    else
        return 1.f;
}

float Osu::getCSDifficultyMultiplier() {
    if(cv::mod_easy.getBool())
        return .5f;
    else if(cv::mod_hardrock.getBool())
        return 1.3f;  // different!
    else
        return 1.f;
}

float Osu::getScoreMultiplier() const { return this->score->getScoreMultiplier(); }

float Osu::getAnimationSpeedMultiplier() const {
    float animationSpeedMultiplier = this->map_iface->getSpeedMultiplier();

    if(cv::animation_speed_override.getFloat() >= 0.0f) return std::max(cv::animation_speed_override.getFloat(), 0.05f);

    return animationSpeedMultiplier;
}

bool Osu::shouldFallBackToLegacySliderRenderer() const {
    return cv::force_legacy_slider_renderer.getBool() || cv::mod_wobble.getBool() || cv::mod_wobble2.getBool() ||
           cv::mod_minimize.getBool() || ui->getModSelector()->isCSOverrideSliderActive()
        /* || (this->osu_playfield_rotation->getFloat() < -0.01f || m_osu_playfield_rotation->getFloat() > 0.01f)*/;
}

void Osu::onResolutionChanged(vec2 newResolution, ResolutionRequestFlags src) {
    if(src == R_ENGINE && this->last_res_change_req_src != R_NOT_PENDING) {
        // since cv::windowed_resolution does env->setWindowSize, it goes through the engine first
        src |= this->last_res_change_req_src;
    }

    this->last_res_change_req_src = R_NOT_PENDING;  // reset to default

    std::string req_srcstr;
    if(src & R_ENGINE) req_srcstr += "engine/external;";
    if(src & R_CV_RESOLUTION) req_srcstr += "convar (resolution);";
    if(src & R_CV_LETTERBOXED_RES) req_srcstr += "convar (letterboxed_res);";
    if(src & R_CV_LETTERBOXING) req_srcstr += "convar (letterboxing);";
    if(src & R_CV_WINDOWED_RESOLUTION) req_srcstr += "convar (windowed_resolution);";
    if(src & R_DELAYED_DESYNC_FIX) req_srcstr += "delayed desync fix;";
    if(src & R_MISC_MANUAL) req_srcstr += "misc/manual;";
    req_srcstr.pop_back();

    debugLog("{:.0f}x{:.0f}, minimized: {} request source: {}", newResolution.x, newResolution.y, env->winMinimized(),
             req_srcstr);

    const bool manual_request = src != R_ENGINE;

    if(env->winMinimized() && !manual_request) return;  // ignore if minimized and not a manual req

    const bool fs = env->winFullscreened();
    const bool fs_letterboxed = fs && cv::letterboxing.getBool();

    // ignore engine resolution size request and find it from cvars, if we are in fullscreen/letterboxed
    const bool res_from_cvars =
        (fs || fs_letterboxed) && (src & (R_ENGINE | R_CV_LETTERBOXING | R_CV_RESOLUTION | R_CV_LETTERBOXED_RES));

    if(res_from_cvars) {
        const std::string &res_cv_str =
            fs_letterboxed ? cv::letterboxed_resolution.getString() : cv::resolution.getString() /* fullscreen */;

        if(!res_cv_str.empty()) {
            newResolution = Parsing::parse_resolution(res_cv_str).value_or(this->internalRect.getSize());
        }
    }

    {
        const vec2 graphicsRes = g->getResolution();
        const vec2 nativeScreenSize = env->getNativeScreenSize();

        const vec2 maxRes = vec::max(graphicsRes, nativeScreenSize);
        if(vec::any(vec::greaterThan(newResolution, maxRes))) {
            // clamp it to desktop/graphics rect
            newResolution = maxRes;
        }
    }

    auto res_str = fmt::format("{:d}x{:d}", (i32)newResolution.x, (i32)newResolution.y);

    const bool dbgcond = cv::debug_env.getBool() || cv::debug_osu.getBool();
    logIf(dbgcond, "Actual ({}): {}", res_str, fs_letterboxed ? "FS letterboxed" : fs ? "FS" : "windowed");

    // save setting depending on request source
    if(fs_letterboxed && (src & R_CV_LETTERBOXED_RES)) {
        logIf(dbgcond, "FS letterboxed: updating from {} to {}", cv::letterboxed_resolution.getString(), res_str);
        cv::letterboxed_resolution.setValue(res_str, false);
    } else if(fs && (src & R_CV_RESOLUTION)) {
        logIf(dbgcond, "FS: updating from {} to {}", cv::resolution.getString(), res_str);
        cv::resolution.setValue(res_str, false);
    } else if((!fs && !fs_letterboxed) && (src & R_CV_WINDOWED_RESOLUTION)) {
        logIf(dbgcond, "windowed: updating from {} to {}", cv::windowed_resolution.getString(), res_str);
        cv::windowed_resolution.setValue(res_str, false);
    }

    const bool resolution_changed = (this->getSliderFrameBuffer()->getSize() != newResolution);  // HACK
    this->internalRect = {vec2{}, newResolution};

    // update dpi specific engine globals
    cv::ui_scrollview_scrollbarwidth.setValue(15.0f * Osu::getUIScale());  // not happy with this as a convar

    // always call onResolutionChange, since DPI changes cause layout changes
    ui->onResolutionChange(this->getVirtScreenSize());

    // update fposu projection matrix
    this->fposu->onResolutionChange(this->getVirtScreenSize());

    // skip rebuilding rendertargets if we didn't change resolution
    if(resolution_changed) {
        this->rebuildRenderTargets();
    }

    // mouse scale/offset
    this->updateMouseSettings();

    // cursor clipping
    this->updateConfineCursor();

    // see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=323
    struct LossyComparisonToFixExcessFPUPrecisionBugBecauseFuckYou {
        static bool equalEpsilon(float f1, float f2) { return std::abs(f1 - f2) < 0.00001f; }
    };

    // a bit hacky, but detect resolution-specific-dpi-scaling changes and force a font and layout reload after a 1
    // frame delay (1/2)
    if(!LossyComparisonToFixExcessFPUPrecisionBugBecauseFuckYou::equalEpsilon(Osu::getUIScale(), this->prevUIScale)) {
        this->prevUIScale = Osu::getUIScale();
        this->last_res_change_req_src = R_DELAYED_DESYNC_FIX;
    }
}

void Osu::onDPIChanged() {
    // delay
    this->bFontReloadScheduled = true;
    this->last_res_change_req_src |= R_MISC_MANUAL;
}

void Osu::rebuildRenderTargets() {
    debugLog("{}x{}", this->internalRect.getWidth(), this->internalRect.getHeight());

    this->backBuffer->rebuild(0, 0, this->internalRect.getWidth(), this->internalRect.getHeight());

    if(cv::mod_fposu.getBool())
        this->playfieldBuffer->rebuild(0, 0, this->internalRect.getWidth(), this->internalRect.getHeight());
    else
        this->playfieldBuffer->rebuild(0, 0, 64, 64);

    this->sliderFrameBuffer->rebuild(0, 0, this->internalRect.getWidth(), this->internalRect.getHeight(),
                                     MultisampleType::X0);

    this->AAFrameBuffer->rebuild(0, 0, this->internalRect.getWidth(), this->internalRect.getHeight());

    if(cv::mod_mafham.getBool()) {
        this->frameBuffer->rebuild(0, 0, this->internalRect.getWidth(), this->internalRect.getHeight());
        this->frameBuffer2->rebuild(0, 0, this->internalRect.getWidth(), this->internalRect.getHeight());
    } else {
        this->frameBuffer->rebuild(0, 0, 64, 64);
        this->frameBuffer2->rebuild(0, 0, 64, 64);
    }
}

void Osu::reloadFonts() {
    const int baseDPI = 96;
    const int newDPI = Osu::getUIScale() * baseDPI;

    for(McFont *font : this->fonts) {
        if(font->getDPI() != newDPI) {
            font->setDPI(newDPI);
            resourceManager->reloadResource(font);
            if(font == this->fontIcons) {
                float averageIconHeight = 0.0f;
                for(char16_t icon : Icons::icons) {
                    const float height = font->getGlyphHeight(icon);
                    if(height > averageIconHeight) averageIconHeight = height;
                }
                font->setHeight(averageIconHeight);
            }
        }
    }
}

void Osu::updateMouseSettings() {
    // mouse scaling & offset
    vec2 offset = vec2(0, 0);
    vec2 scale = vec2(1, 1);
    if((g->getResolution() != this->getVirtScreenSize()) && cv::letterboxing.getBool()) {
        offset = -vec2((engine->getScreenWidth() / 2.f - this->internalRect.getWidth() / 2.f) *
                           (1.0f + cv::letterboxing_offset_x.getFloat()),
                       (engine->getScreenHeight() / 2.f - this->internalRect.getHeight() / 2.f) *
                           (1.0f + cv::letterboxing_offset_y.getFloat()));

        scale = this->internalRect.getSize() / engine->getScreenSize();
    }

    mouse->setOffset(offset);
    mouse->setScale(scale);
    logIf(cv::debug_mouse.getBool() || cv::debug_osu.getBool(), "offset {} scale {}", offset, scale);
}

void Osu::updateWindowsKeyDisable() {
    const bool isPlayerPlaying =
        (env->winFocused() && !env->winMinimized()) && this->isInPlayMode() &&
        (!(this->map_iface->isPaused() || this->map_iface->isContinueScheduled()) ||
         this->map_iface->isRestartScheduled()) &&
        !(cv::mod_autoplay.getBool() || this->map_iface->is_watching || BanchoState::spectating);

    const bool disable = cv::win_disable_windows_key_while_playing.getBool() && isPlayerPlaying;
    logIfCV(debug_osu, "{} windows key, {} to text input", disable ? "disabling" : "enabling",
            isPlayerPlaying ? "not listening" : "listening");

    env->setWindowsKeyDisabled(disable);

    // this is kind of a weird place to put this, but we don't care about text input when in gameplay
    // on some platforms, text input being enabled might result in an on-screen keyboard showing up
    // ultra hack: we have no way of knowing whether there are any char event consumers currently listening
    // special case chat to allow chat while playing (chat->setVisible will call this function too)
    env->listenToTextInput(!isPlayerPlaying || ui->getChat()->isVisible());
}

void Osu::onWindowedResolutionChanged(std::string_view args) {
    // ignore if we're still loading or not in fullscreen
    this->last_res_change_req_src |= R_CV_WINDOWED_RESOLUTION;

    if(env->winFullscreened() || !this->UIReady()) return;

    auto parsed = Parsing::parse_resolution(args);
    if(!parsed.has_value()) {
        debugLog(
            "Error: Invalid arguments {} for command 'windowed_resolution'! (Usage: e.g. \"windowed_resolution "
            "1280x720\")",
            args);
        return;
    }

    i32 width{parsed->x}, height{parsed->y};
    debugLog("{}x{}", width, height);

    env->setWindowSize(width, height);
    env->centerWindow();
}

void Osu::onFSResChanged(std::string_view args) {
    auto parsed = Parsing::parse_resolution(args);
    if(!parsed.has_value()) {
        debugLog("Error: Invalid arguments {} for command 'resolution'! (Usage: e.g. \"resolution 1280x720\")", args);
        return;
    }

    vec2 newRes = parsed.value();
    debugLog("{:.0f}x{:.0f}", newRes.x, newRes.y);

    // clamp requested internal resolution to current renderer resolution
    // however, this could happen while we are transitioning into fullscreen. therefore only clamp when not in
    // fullscreen or not in fullscreen transition
    if(this->UIReady()) {
        bool isTransitioningIntoFullscreenHack =
            g->getResolution().x < env->getNativeScreenSize().x || g->getResolution().y < env->getNativeScreenSize().y;
        if(!env->winFullscreened() || !isTransitioningIntoFullscreenHack) {
            if(newRes.x > g->getResolution().x) newRes.x = g->getResolution().x;
            if(newRes.y > g->getResolution().y) newRes.y = g->getResolution().y;
        }
    }

    std::string res_str = fmt::format("{:.0f}x{:.0f}", newRes.x, newRes.y);
    cv::resolution.setValue(res_str, false);  // set it to the cleaned up value

    // delay
    this->last_res_change_req_src |= R_CV_RESOLUTION;
}

void Osu::onFSLetterboxedResChanged(std::string_view args) {
    auto parsed = Parsing::parse_resolution(args);
    if(!parsed.has_value()) {
        debugLog(
            "Error: Invalid arguments {} for command 'letterboxed_resolution'! (Usage: e.g. \"letterboxed_resolution "
            "1280x720\")",
            args);
        return;
    }

    vec2 newRes = parsed.value();
    debugLog("{:.0f}x{:.0f}", newRes.x, newRes.y);
    if(this->UIReady()) {
        bool isTransitioningIntoFullscreenHack =
            g->getResolution().x < env->getNativeScreenSize().x || g->getResolution().y < env->getNativeScreenSize().y;
        if(!env->winFullscreened() || !isTransitioningIntoFullscreenHack) {
            if(newRes.x > g->getResolution().x) newRes.x = g->getResolution().x;
            if(newRes.y > g->getResolution().y) newRes.y = g->getResolution().y;
        }
    }

    std::string res_str = fmt::format("{:.0f}x{:.0f}", newRes.x, newRes.y);
    cv::letterboxed_resolution.setValue(res_str, false);  // set it to the cleaned up value

    this->last_res_change_req_src |= R_CV_LETTERBOXED_RES;
}

void Osu::onFocusGained() {
    if(!this->UIReady()) return;

    // cursor clipping
    this->updateConfineCursor();

    if(this->bWasBossKeyPaused) {
        this->bWasBossKeyPaused = false;

        // make sure playfield is fully constructed before accessing it
        this->map_iface->pausePreviewMusic();
    }

    this->updateWindowsKeyDisable();
    ui->getVolumeOverlay()->gainFocus();
}

void Osu::onFocusLost() {
    if(!this->UIReady()) return;
    if(this->isInPlayMode() && !this->map_iface->isPaused() && cv::pause_on_focus_loss.getBool()) {
        if(!BanchoState::is_playing_a_multi_map() && !this->map_iface->is_watching && !BanchoState::spectating) {
            this->map_iface->pause(false);
            ui->getPauseOverlay()->setVisible(true);
            ui->getModSelector()->setVisible(false);
        }
    }

    this->updateWindowsKeyDisable();
    ui->getVolumeOverlay()->loseFocus();

    // release cursor clip
    this->updateConfineCursor();
}

void Osu::onMinimized() {
    if(this->UIReady()) ui->getVolumeOverlay()->loseFocus();
}

void Osu::saveEverything() {
    if(this->UIReady()) {
        this->ui_memb->getOptionsOverlay()->save();
    }
    if(db) {
        db->save();
    }

    File::flushToDisk();
}

bool Osu::onShutdown() {
    debugLog("Osu::onShutdown()");

    if(!Env::cfg(OS::WASM) && this->map_iface && !cv::alt_f4_quits_even_while_playing.getBool() &&
       this->isInPlayMode()) {
        this->map_iface->stop();
        return false;
    }

    this->saveEverything();
    BanchoState::disconnect(true);

    return true;
}

void Osu::onSkinReload() {
    this->bSkinLoadWasReload = true;
    this->onSkinChange(cv::skin.getString());
}

// resolve a skin name to its directory path
// tries neomod skins folder first, then osu! skins folder
// returns empty string for "default" or empty name
static std::string resolveSkinPath(std::string_view skinName) {
    if(skinName.empty() || skinName == "default") return {};

    std::string neomodFolder = fmt::format(NEOMOD_SKINS_PATH "/{}/", skinName);
    if(env->directoryExists(neomodFolder)) return neomodFolder;

    std::string ppyFolder{
        fmt::format("{}/{}/{}/", cv::osu_folder.getString(), cv::osu_folder_sub_skins.getString(), skinName)};
    File::normalizeSlashes(ppyFolder, '\\', '/');
    return ppyFolder;
}

void Osu::onSkinChange(std::string_view newSkinName) {
    if(this->skin) {
        if(this->bSkinLoadScheduled || this->skinScheduledToLoad != nullptr) return;
        if(newSkinName.length() < 1) return;
    }

    // resolve fallback skin path
    std::string fallbackDir;
    const auto &fallbackName = cv::skin_fallback.getString();
    if(!fallbackName.empty() && fallbackName != newSkinName && fallbackName != "default") {
        fallbackDir = resolveSkinPath(fallbackName);
    }

    if(newSkinName == "default") {
        this->skinScheduledToLoad =
            new Skin(std::string{newSkinName}, MCENGINE_IMAGES_PATH "/default/", std::move(fallbackDir));
        if(!this->skin) this->skin.reset(this->skinScheduledToLoad);
        this->bSkinLoadScheduled = true;
        return;
    }

    std::string skinDir = resolveSkinPath(newSkinName);
    this->skinScheduledToLoad = new Skin(std::string{newSkinName}, std::move(skinDir), std::move(fallbackDir));

    // initial load
    if(!this->skin) this->skin.reset(this->skinScheduledToLoad);

    this->bSkinLoadScheduled = true;
}

void Osu::updateAnimationSpeed() {
    if(this->skin) {
        float speed = this->getAnimationSpeedMultiplier() / this->map_iface->getSpeedMultiplier();
        this->skin->anim_speed = (speed >= 0.01f ? speed : 0.0f);
    }
}

void Osu::onAnimationSpeedChange() { this->updateAnimationSpeed(); }

void Osu::onSpeedChange(float speed) {
    this->map_iface->setMusicSpeed(speed >= 0.01f ? speed : this->map_iface->getSpeedMultiplier());
    this->updateAnimationSpeed();

    // Update mod menu UI
    {
        // DT/HT buttons
        auto *modSelector = this->ui_memb->getModSelector();
        cv::mod_doubletime_dummy.setValue(speed == 1.5f, false);
        modSelector->getGridButton(ModSelector::DT_POS)->setOn(speed == 1.5f, true);
        cv::mod_halftime_dummy.setValue(speed == 0.75f, false);
        modSelector->getGridButton(ModSelector::HT_POS)->setOn(speed == 0.75f, true);
        modSelector->updateButtons(true);

        // Speed slider ('+1' to compensate for turn-off area of the override sliders)
        modSelector->speedSlider->setValue(speed + 1.f, true, false);
        modSelector->updateOverrideSliderLabels();

        // Score multiplier
        modSelector->updateScoreMultiplierLabelText();
    }
}

void Osu::onThumbnailsToggle() {
    ui->getSongBrowser()->thumbnailYRatio = cv::draw_songbrowser_thumbnails.getBool() ? 1.333333f : 0.f;
}

void Osu::onPlayfieldChange() { this->map_iface->onModUpdate(); }

void Osu::onUIScaleChange(float oldValue, float newValue) {
    if(oldValue != newValue) {
        // delay
        this->bFontReloadScheduled = true;
        this->last_res_change_req_src |= R_MISC_MANUAL;
    }
    Osu::rawUIScale = newValue;
}

void Osu::onUIScaleToDPIChange(float oldValue, float newValue) {
    if((oldValue > 0) != (newValue > 0)) {
        // delay
        this->bFontReloadScheduled = true;
        this->last_res_change_req_src |= R_MISC_MANUAL;
    }
}

void Osu::onLetterboxingChange(float oldValue, float newValue) {
    if((oldValue > 0) != (newValue > 0)) {
        // delay
        this->last_res_change_req_src |= R_CV_LETTERBOXING;
    }
}

// Here, "cursor" is the Windows mouse cursor, not the game cursor
void Osu::updateCursorVisibility() {
    if(!env->isCursorInWindow()) {
        return;  // don't do anything
    }

    const bool currently_visible = env->isCursorVisible();

    bool forced_visible = this->isInPlayMode() && !this->map_iface->isPaused() &&
                          (cv::mod_autoplay.getBool() || cv::mod_autopilot.getBool() || this->map_iface->is_watching ||
                           BanchoState::spectating);
    bool desired_vis = forced_visible;

    // if it's not forced visible, check whether it's inside the internal window
    if(!forced_visible) {
        const bool internal_contains_mouse = this->internalRect.contains(mouse->getPos());
        if(internal_contains_mouse) {
            desired_vis = false;
        } else {
            desired_vis = true;
        }
    }

    // only change if it's different from the current mouse state
    if(desired_vis != currently_visible) {
        logIfCV(debug_mouse, "current: {} desired: {}", currently_visible, desired_vis);
        env->setCursorVisible(desired_vis);
    }
}

void Osu::updateConfineCursor() {
    McRect clip{};
    const bool is_fullscreen = env->winFullscreened();
    const bool playing = this->isInPlayMode();
    // we need relative mode (rawinput) for fposu without absolute mode
    const bool playing_fposu_nonabs = (playing && cv::mod_fposu.getBool() && !cv::fposu_absolute_mode.getBool());

    const bool might_confine = (playing_fposu_nonabs) ||                                                     //
                               (is_fullscreen && cv::confine_cursor_fullscreen.getBool()) ||                 //
                               (!is_fullscreen && cv::confine_cursor_windowed.getBool()) ||                  //
                               (playing && !(ui->getPauseOverlay() && ui->getPauseOverlay()->isVisible()));  //

    const bool force_no_confine = !env->winFocused() ||                                             //
                                  (!playing_fposu_nonabs && cv::confine_cursor_never.getBool()) ||  //
                                  this->getModAuto() ||                                             //
                                  this->getModAutopilot() ||                                        //
                                  (this->map_iface && this->map_iface->is_watching) ||              //
                                  BanchoState::spectating;                                          //

    const bool confine_cursor = might_confine && !force_no_confine;
    if(confine_cursor) {
        clip = McRect{-mouse->getOffset(), this->getVirtScreenSize()};
    }

    logIfCV(debug_mouse, "confined: {}, cliprect: {}", confine_cursor, clip);

    env->setCursorClip(confine_cursor, clip);
}

// needs a separate fromMouse parameter, since M1/M2 might be bound to keyboard keys too
void Osu::onGameplayKey(GameplayKeys key_flag, bool down, u64 timestamp, bool fromMouse) {
    auto held_now = this->map_iface->getKeys();

    const bool changed = !(held_now & key_flag) == down;
    if(!changed) return;

    if(key_flag & GameplayKeys::Smoke) {
        // just add/remove smoke
        this->map_iface->current_keys = down ? (held_now | GameplayKeys::Smoke) : (held_now & ~GameplayKeys::Smoke);
        return;
    }

    // remove smoke from consideration
    held_now &= ~GameplayKeys::Smoke;

    // always allow keyup
    bool can_press = !down || cv::mod_no_keylock.getBool();
    if(!can_press) {
        auto k1m1 = (GameplayKeys::K1 | GameplayKeys::M1);
        auto k2m2 = (GameplayKeys::K2 | GameplayKeys::M2);
        bool is_k1m1 = !!(key_flag & k1m1);
        can_press = !(held_now & (is_k1m1 ? k1m1 : k2m2));
    }

    // NOTE: allow events even while beatmap is paused, to correctly not-continue immediately due to pressed keys
    // debugLog("got key{} {:04b} fromMouse {} held_now {:04b} can_press {}", down ? "down" : "up",
    //          static_cast<u8>(key_flag), fromMouse, held_now, can_press);
    if(can_press) {
        this->map_iface->onKey(key_flag, down, timestamp);
    }

    // only skip animating if it's a mouse event and we are in unpaused gameplay, otherwise animate
    const bool do_animate =
        fromMouse ? !(cv::disable_mousebuttons.getBool() && this->isInPlayMode() && !this->map_iface->isPaused())
                  : true;

    // cursor anim + ripples
    if(do_animate) {
        if(down && can_press) {
            ui->getHUD()->animateCursorExpand();
            ui->getHUD()->addCursorRipple(mouse->getPos());
        } else if(!this->map_iface->isClickHeld()) {
            ui->getHUD()->animateCursorShrink();
        }
    }
}

void Osu::onLetterboxingOffsetChange() {
    this->updateMouseSettings();
    this->updateConfineCursor();
}

void Osu::onUserCardChange(std::string_view new_username) {
    // NOTE: force update options textbox to avoid shutdown inconsistency
    ui->getOptionsOverlay()->setUsername(UString{new_username.data(), static_cast<int>(new_username.length())});
    this->userButton->setID(BanchoState::get_uid());
}

float Osu::getRectScaleToFitResolution(vec2 size, vec2 resolution) {
    if(resolution.x / size.x > resolution.y / size.y) {
        return resolution.y / size.y;
    } else {
        return resolution.x / size.x;
    }
}

float Osu::getImageScaleToFitResolution(const Image *img, vec2 resolution) {
    return getRectScaleToFitResolution(vec2(img->getWidth(), img->getHeight()), resolution);
}

float Osu::getRectScaleToFillResolution(vec2 size, vec2 resolution) {
    if(resolution.x / size.x < resolution.y / size.y) {
        return resolution.y / size.y;
    } else {
        return resolution.x / size.x;
    }
}

float Osu::getImageScaleToFillResolution(const Image *img, vec2 resolution) {
    return getRectScaleToFillResolution(vec2(img->getWidth(), img->getHeight()), resolution);
}

float Osu::getRectScale(vec2 size, float osuSize) {
    auto screen = osu ? osu->getVirtScreenSize() : engine->getScreenSize();
    if(screen.x * 3 > screen.y * 4) {
        // Reduce width to fit 4:3
        screen.x = screen.y * 4 / 3;
    } else {
        // Reduce height to fit 4:3
        screen.y = screen.x * 3 / 4;
    }

    f32 x = screen.x / Osu::osuBaseResolution.x / size.x;
    f32 y = screen.y / Osu::osuBaseResolution.y / size.y;
    return osuSize * std::max(x, y);
}

float Osu::getImageScale(const Image *img, float osuSize) {
    return getRectScale(vec2(img->getWidth(), img->getHeight()), osuSize);
}

float Osu::getUIScale(float osuSize) {
    // return osuSize * Osu::getImageScaleToFitResolution(Osu::osuBaseResolution, osu->getVirtScreenSize());

    auto screen = osu ? osu->getVirtScreenSize() : engine->getScreenSize();
    if(screen.x * 3 > screen.y * 4) {
        return osuSize * screen.y / Osu::osuBaseResolution.y;
    } else {
        return osuSize * screen.x / Osu::osuBaseResolution.x;
    }
}

float Osu::getUIScale() {
    f32 scale = Osu::getRawUIScale();

    if(cv::ui_scale_to_dpi.getBool()) {
        if(env->getPixelDensity() >= 1.4f /* maybe make it configurable? idk what retina  */) {
            scale *= env->getDPIScale();
        } else {
            f32 w = osu ? osu->getVirtScreenWidth() : engine->getScreenWidth();
            f32 h = osu ? osu->getVirtScreenHeight() : engine->getScreenHeight();
            if(w >= cv::ui_scale_to_dpi_minimum_width.getInt() && h >= cv::ui_scale_to_dpi_minimum_height.getInt()) {
                scale *= env->getDPIScale();
            }
        }
    }

    return scale;
}

bool Osu::getModAuto() const { return flags::has<ModFlags::Autoplay>(this->score->mods.flags); }
bool Osu::getModAutopilot() const { return flags::has<ModFlags::Autopilot>(this->score->mods.flags); }
bool Osu::getModRelax() const { return flags::has<ModFlags::Relax>(this->score->mods.flags); }
bool Osu::getModSpunout() const { return flags::has<ModFlags::SpunOut>(this->score->mods.flags); }
bool Osu::getModTarget() const { return flags::has<ModFlags::Target>(this->score->mods.flags); }
bool Osu::getModScorev2() const { return flags::has<ModFlags::ScoreV2>(this->score->mods.flags); }
bool Osu::getModFlashlight() const { return flags::has<ModFlags::Flashlight>(this->score->mods.flags); }
bool Osu::getModNF() const { return flags::has<ModFlags::NoFail>(this->score->mods.flags); }
bool Osu::getModHD() const { return flags::has<ModFlags::Hidden>(this->score->mods.flags); }
bool Osu::getModHR() const { return flags::has<ModFlags::HardRock>(this->score->mods.flags); }
bool Osu::getModEZ() const { return flags::has<ModFlags::Easy>(this->score->mods.flags); }
bool Osu::getModSD() const { return flags::has<ModFlags::SuddenDeath>(this->score->mods.flags); }
bool Osu::getModSS() const { return flags::has<ModFlags::Perfect>(this->score->mods.flags); }
bool Osu::getModNightmare() const { return flags::has<ModFlags::Nightmare>(this->score->mods.flags); }
bool Osu::getModTD() const {
    return flags::has<ModFlags::TouchDevice>(this->score->mods.flags) || cv::mod_touchdevice_always.getBool();
}
bool Osu::getModDT() const { return this->score->mods.speed == 1.5f; }
bool Osu::getModNC() const {
    return this->score->mods.speed == 1.5f && flags::has<ModFlags::NoPitchCorrection>(this->score->mods.flags);
}
bool Osu::getModHT() const { return this->score->mods.speed == 0.75f; }

bool Osu::isBleedingEdge() {
    if constexpr(Env::cfg(OS::WASM)) {
        return Environment::getEnvVariable("IS_BLEEDINGEDGE") == "1";
    } else {
        return cv::is_bleedingedge.getBool();
    }
}

// part 1 of callback
void Osu::audioRestartCallbackBefore() {
    // abort loudness calc
    VolNormalization::abort();

    Sound *map_music = nullptr;
    if(this->map_iface && (map_music = this->map_iface->getMusic())) {
        this->music_was_playing = map_music->isPlaying();
        this->music_prev_position_ms = map_music->getPositionMS();
    } else {
        this->music_was_playing = false;
        this->music_prev_position_ms = 0;
    }
}

// the actual reset will be sandwiched between these during restart
// part 2 of callback
void Osu::audioRestartCallbackAfter() {
    if(this->UIReady()) {
        auto *options = this->ui_memb->getOptionsOverlay();
        options->onOutputDeviceChange();
        if(this->skin) {
            this->skin->reloadSounds();
        }

        // start playing music again after audio device changed
        Sound *map_music = nullptr;
        if(this->map_iface && (map_music = this->map_iface->getMusic())) {
            // TODO(spec): is this even right? why do we only unload music after already destroying/restarting soundengine
            this->map_iface->unloadMusic();
            this->map_iface->loadMusic();
            if((map_music = this->map_iface->getMusic())) {  // need to get new music after loading
                map_music->setLoop(!this->isInPlayMode());
                map_music->setPositionMS(this->music_prev_position_ms);
            }
        }

        if(this->music_was_playing) {
            this->music_unpause_scheduled = true;
        }
        options->scheduleLayoutUpdate();
    }

    // resume loudness calc
    if(db) {
        VolNormalization::start_calc(db->loudness_to_calc);
    }
}

void Osu::setupAudio() {
    if(Env::cfg(AUD::BASS) && soundEngine->getTypeId() == SoundEngine::BASS) {
        soundEngine->updateOutputDevices(true);
        soundEngine->initializeOutputDevice(soundEngine->getWantedDevice());
        cv::snd_output_device.setValue(soundEngine->getOutputDeviceName());
        cv::snd_freq.setCallback(SA::MakeDelegate<&SoundEngine::onFreqChanged>(soundEngine.get()));
        cv::cmd::snd_restart.setCallback(SA::MakeDelegate<&SoundEngine::restart>(soundEngine.get()));
        cv::win_snd_wasapi_exclusive.setCallback(SA::MakeDelegate<&SoundEngine::onParamChanged>(soundEngine.get()));
        cv::win_snd_wasapi_buffer_size.setCallback(SA::MakeDelegate<&SoundEngine::onParamChanged>(soundEngine.get()));
        cv::win_snd_wasapi_period_size.setCallback(SA::MakeDelegate<&SoundEngine::onParamChanged>(soundEngine.get()));
        cv::win_snd_wasapi_event_callbacks.setCallback(
            SA::MakeDelegate<&SoundEngine::onParamChanged>(soundEngine.get()));
        cv::asio_buffer_size.setCallback(SA::MakeDelegate<&SoundEngine::onParamChanged>(soundEngine.get()));
        cv::snd_output_device.setCallback(
            []() -> void { osu && osu->UIReady() ? ui->getOptionsOverlay()->scheduleLayoutUpdate() : (void)0; });
    }

    soundEngine->setDeviceChangeBeforeCallback(SA::MakeDelegate<&Osu::audioRestartCallbackBefore>(this));
    soundEngine->setDeviceChangeAfterCallback(SA::MakeDelegate<&Osu::audioRestartCallbackAfter>(this));

    if(Env::cfg(AUD::SOLOUD) && soundEngine->getTypeId() == SoundEngine::SOLOUD) {  // bass works differently
        // this sets convar callbacks for things that require a soundengine reinit, do it
        // only after init so config files don't restart it over and over again
        soundEngine->allowInternalCallbacks();
    }
}

// guaranteed to never return an empty string
std::string Osu::getDefaultFallbackOsuFolder() {
    // default fallback return value
    std::string folderRet = env->getUserDataPath();
    // directory to look for osu in
    std::string toplevelDir;

    // non-windows, or windows in wine (try to find default osu-winello install folder)
    bool isWine = false;
    if(!Env::cfg(OS::WINDOWS) || (isWine = (RuntimePlatform::current() & RuntimePlatform::WIN_WINE))) {
        // try $XDG_DATA_HOME/
        toplevelDir = Environment::getEnvVariable("XDG_DATA_HOME");
        if(toplevelDir.empty() && isWine) {
            toplevelDir = Environment::getEnvVariable("WINE_HOST_XDG_DATA_HOME");
        }
        if(toplevelDir.empty()) {
            // try ~/.local/share/
            if(isWine) {
                toplevelDir = Environment::getEnvVariable("HOME");
                if(toplevelDir.empty()) {
                    toplevelDir = Environment::getEnvVariable("WINE_HOST_HOME");
                }
                if(!toplevelDir.empty()) {
                    toplevelDir += "/.local/share/";
                }
            } else {
                // this should be equivalent to ~/.local/share
                toplevelDir = env->getUserDataPath();
            }
        }
    } else {  // windows, try to find osu install folder manually in the default location (since we didn't find it in the registry)
        std::string appDataFolder = env->getUserDataPath();
        if(size_t roamPos = appDataFolder.rfind("Roaming"); roamPos != std::string::npos) {
            appDataFolder = appDataFolder.substr(0, roamPos);
            appDataFolder += "Local";
        }
        toplevelDir = appDataFolder;
    }

    if(!toplevelDir.empty()) {
        File::normalizeSlashes(toplevelDir, '\\', '/');
        if(!toplevelDir.empty() && !toplevelDir.ends_with('/')) {
            toplevelDir.push_back('/');
        }
        for(std::string_view subdir : {"osu!/", "osu-wine/", "osu-wine/osu!/", "osu/", "osu/osu!/"}) {
            const std::string checkDir = fmt::format("{}{}", toplevelDir, subdir);
            if(Environment::directoryExists(checkDir)) {
                // check for osu!.exe
                const std::string checkFile = fmt::format("{}{}", checkDir, "osu!.exe");
                if(Environment::fileExists(checkFile)) {
                    // found
                    folderRet = checkDir;
                    break;
                }
            }
        }
    }

    return folderRet;
}
