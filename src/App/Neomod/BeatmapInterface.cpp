// Copyright (c) 2015, PG, All rights reserved.
#include "BeatmapInterface.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

#include "Environment.h"
#include "AnimationHandler.h"
#include "BackgroundImageHandler.h"
#include "Bancho.h"
#include "BanchoNetworking.h"
#include "BanchoProtocol.h"
#include "BanchoSubmitter.h"
#include "BanchoUsers.h"
#include "BeatmapInterface.h"
#include "Chat.h"
#include "OsuConVars.h"
#include "ConVarHandler.h"
#include "Timing.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "DifficultyCalculator.h"
#include "Engine.h"
#include "GameRules.h"
#include "HUD.h"
#include "HitObjects.h"
#include "LegacyReplay.h"
#include "Logging.h"
#include "MainMenu.h"
#include "ModFPoSu.h"
#include "ModSelector.h"
#include "Mouse.h"
#include "NotificationOverlay.h"
#include "OptionsOverlay.h"
#include "Osu.h"
#include "PauseOverlay.h"
#include "ResourceManager.h"
#include "RichPresence.h"
#include "RoomScreen.h"
#include "SimulatedBeatmapInterface.h"
#include "Sound.h"
#include "Font.h"
#include "Shader.h"
#include "RenderTarget.h"
#include "Skin.h"
#include "SkinImage.h"
#include "AsyncPPCalculator.h"
#include "SongBrowser/SongBrowser.h"
#include "SoundEngine.h"
#include "SpectatorScreen.h"
#include "UI.h"
#include "UIModSelectorModButton.h"

using namespace flags::operators;
BeatmapInterface::BeatmapInterface() : AbstractBeatmapInterface(), ppv2_calc(this) {
    // vars
    this->bIsPlaying = false;
    this->bIsPaused = false;
    this->bIsWaiting = false;
    this->bIsRestartScheduled = false;
    this->bIsRestartScheduledQuick = false;

    this->bIsInSkippableSection = false;
    this->bShouldFlashWarningArrows = false;
    this->fShouldFlashSectionPass = 0.0f;
    this->fShouldFlashSectionFail = 0.0f;
    this->bContinueScheduled = false;
    this->iContinueMusicPos = 0;

    this->beatmap = nullptr;

    this->music = nullptr;

    this->fMusicFrequencyBackup = 0.f;
    this->iCurMusicPos = 0;
    this->iCurMusicPosWithOffsets = 0;
    this->bWasSeekFrame = false;
    this->fAfterMusicIsFinishedVirtualAudioTimeStart = -1.0f;
    this->bIsFirstMissSound = true;

    this->bFailed = false;
    this->fFailAnim = 1.0f;
    this->fHealth = 1.0;
    this->fHealth2 = 1.0f;

    this->fDrainRate = 0.0;

    this->fBreakBackgroundFade = 0.0f;
    this->bInBreak = false;
    this->currentHitObject = nullptr;
    this->iNextHitObjectTime = 0;
    this->iPreviousHitObjectTime = 0;
    this->iPreviousSectionPassFailTime = -1;

    this->bClickedContinue = false;
    this->lastPressedKey = 0;
    this->iAllowAnyNextKeyUntilHitObjectIndex = 0;

    this->iNPS = 0;
    this->iND = 0;
    this->iCurrentHitObjectIndex = 0;
    this->iCurrentNumCircles = 0;
    this->iCurrentNumSliders = 0;
    this->iCurrentNumSpinners = 0;

    this->iPreviousFollowPointObjectIndex = -1;

    this->bIsSpinnerActive = false;

    this->fPlayfieldRotation = 0.0f;
    this->fScaleFactor = 1.0f;

    this->fXMultiplier = 1.0f;
    this->fNumberScale = 1.0f;
    this->fHitcircleOverlapScale = 1.0f;
    this->fRawHitcircleDiameter = 27.35f * 2.0f;
    this->fSliderFollowCircleDiameter = 0.0f;

    this->iAutoCursorDanceIndex = 0;

    this->fAimStars = 0.0f;
    this->fAimSliderFactor = 0.0f;
    this->fSpeedStars = 0.0f;
    this->fSpeedNotes = 0.0f;

    this->bWasHREnabled = false;
    this->fPrevHitCircleDiameter = 0.0f;
    this->bWasHorizontalMirrorEnabled = false;
    this->bWasVerticalMirrorEnabled = false;
    this->bWasEZEnabled = false;
    this->bWasMafhamEnabled = false;
    this->fPrevPlayfieldRotationFromConVar = 0.0f;
    this->bIsPreLoading = true;
    this->iPreLoadingIndex = 0;

    this->mafhamActiveRenderTarget = nullptr;
    this->mafhamFinishedRenderTarget = nullptr;
    this->bMafhamRenderScheduled = true;
    this->iMafhamHitObjectRenderIndex = 0;
    this->iMafhamPrevHitObjectIndex = 0;
    this->iMafhamActiveRenderHitObjectIndex = 0;
    this->iMafhamFinishedRenderHitObjectIndex = 0;
    this->bInMafhamRenderChunk = false;
}

BeatmapInterface::~BeatmapInterface() {
    anim::deleteExistingAnimation(&this->fBreakBackgroundFade);
    anim::deleteExistingAnimation(&this->fHealth2);
    anim::deleteExistingAnimation(&this->fFailAnim);

    this->unloadObjects();
}

void BeatmapInterface::drawDebug() {
    static constexpr Color shadowColor = argb(255, 0, 0, 0);
    static constexpr Color textColor = argb(255, 255, 232, 255);

    const auto &alltp = this->beatmap->getTimingpoints();
    if(alltp.empty()) return;

    McFont *debugFont = engine->getConsoleFont();
    const i32 dbgfontheight = (i32)debugFont->getHeight() + 3;

    const i32 totalTextLinesHeight = dbgfontheight * (i32)alltp.size();
    const i32 offscreenPixels = totalTextLinesHeight - osu->getVirtScreenHeight();
    const f32 textYRatio = (f32)osu->getVirtScreenHeight() / (f32)totalTextLinesHeight;

    const i32 yOffset = textYRatio >= 1 ? 0
                                        : -(i32)((f32)(offscreenPixels + dbgfontheight) *
                                                 (this->getMousePos().y / (f32)osu->getVirtScreenHeight()));
    const i32 xOffset = 5;

    g->pushTransform();
    g->translate(xOffset, yOffset + dbgfontheight);

    for(const DatabaseBeatmap::TIMINGPOINT &t : alltp) {
        // TODO: draw current TIMING_INFO in green (not timingpoint)
        // next to (to the right) the closest timingpoint with an offset < (this->iCurMusicPos + cv::timingpoints_offset.getInt())

        const UString curtpString = fmt::format("{},{},{},{},{},{},{}", (i32)t.offset, t.msPerBeat, t.sampleSet,
                                                t.sampleIndex, t.volume, (i32)t.uninherited, (i32)t.kiai);

        g->drawString(debugFont, curtpString,
                      TextShadow{.col_text = textColor, .col_shadow = shadowColor, .offs_px = 1.f});

        // spacing for next
        g->translate(0, dbgfontheight);
    }
    g->popTransform();
}

void BeatmapInterface::drawBackground() {
    if(!this->canDraw()) return;

    // draw beatmap background image
    {
        const Image *backgroundImage = nullptr;
        if(cv::draw_beatmap_background_image.getBool() &&
           (backgroundImage = osu->getBackgroundImageHandler()->getLoadBackgroundImage(
                this->beatmap, false, cv::draw_menu_background.getBool())) &&
           (cv::background_dim.getFloat() < 1.0f || this->fBreakBackgroundFade > 0.0f)) {
            const float scale = Osu::getImageScaleToFillResolution(backgroundImage, osu->getVirtScreenSize());
            const vec2 centerTrans = (osu->getVirtScreenSize() / 2.0f);

            const float backgroundFadeDimMultiplier = 1.0f - (cv::background_dim.getFloat() - 0.3f);
            const auto dim =
                (1.0f - cv::background_dim.getFloat()) + this->fBreakBackgroundFade * backgroundFadeDimMultiplier;
            const auto alpha = cv::mod_fposu.getBool() ? cv::background_alpha.getFloat() : 1.0f;

            g->setColor(argb(alpha, dim, dim, dim));
            g->pushTransform();
            {
                g->scale(scale, scale);
                g->translate((int)centerTrans.x, (int)centerTrans.y);
                g->drawImage(backgroundImage);
            }
            g->popTransform();
        }
    }

    // draw background
    if(cv::background_brightness.getFloat() > 0.0f) {
        const auto brightness = cv::background_brightness.getFloat();

        const Channel red = std::clamp<float>(brightness * cv::background_color_r.getFloat(), 0.0f, 255.0f);
        const Channel green = std::clamp<float>(brightness * cv::background_color_g.getFloat(), 0.0f, 255.0f);
        const Channel blue = std::clamp<float>(brightness * cv::background_color_b.getFloat(), 0.0f, 255.0f);
        const Channel alpha = 255 * (1.0f - this->fBreakBackgroundFade) *
                              (cv::mod_fposu.getBool() ? cv::background_alpha.getFloat() : 1.0f);

        g->setColor(argb(alpha, red, green, blue));
        g->fillRect(0, 0, osu->getVirtScreenWidth(), osu->getVirtScreenHeight());
    }

    // draw scorebar-bg
    if(cv::draw_hud.getBool() && cv::draw_scorebarbg.getBool() &&
       (!cv::mod_fposu.getBool() || (!cv::fposu_draw_scorebarbg_on_top.getBool())))  // NOTE: special case for FPoSu
        ui->getHUD()->drawScorebarBg(
            cv::hud_scorebar_hide_during_breaks.getBool() ? (1.0f - this->fBreakBackgroundFade) : 1.0f,
            ui->getHUD()->getScoreBarBreakAnim());

    if(cv::debug_osu.getBool()) {
        int y = 50;

        if(this->bIsPaused) {
            g->setColor(0xffffffff);
            g->fillRect(50, y, 15, 50);
            g->fillRect(50 + 50 - 15, y, 15, 50);
        }

        y += 100;

        if(this->bIsWaiting) {
            g->setColor(0xff00ff00);
            g->fillRect(50, y, 50, 50);
        }

        y += 100;

        if(this->bIsPlaying) {
            g->setColor(0xff0000ff);
            g->fillRect(50, y, 50, 50);
        }

        y += 100;

        if(this->hitobjectsSortedByEndTime.size() > 0) {
            HitObject *lastHitObject = this->hitobjectsSortedByEndTime.back();
            if(lastHitObject->isFinished() &&
               this->iCurMusicPos > lastHitObject->getEndTime() + (i32)cv::end_skip_time.getInt()) {
                g->setColor(0xff00ffff);
                g->fillRect(50, y, 50, 50);
            }

            y += 100;
        }
    }
}

void BeatmapInterface::skipEmptySection() {
    if(!this->bIsInSkippableSection) return;
    this->bIsInSkippableSection = false;
    ui->getChat()->updateVisibility();

    const f32 offset = 2500.0f;
    f32 offsetMultiplier = this->getSpeedMultiplier();
    {
        // only compensate if not within "normal" osu mod range (would make the game feel too different regarding time
        // from skip until first hitobject)
        if(offsetMultiplier >= 0.74f && offsetMultiplier <= 1.51f) offsetMultiplier = 1.0f;

        // don't compensate speed increases at all actually
        if(offsetMultiplier > 1.0f) offsetMultiplier = 1.0f;

        // and cap slowdowns at sane value (~ spinner fadein start)
        if(offsetMultiplier <= 0.2f) offsetMultiplier = 0.2f;
    }

    const i32 nextHitObjectDelta = this->iNextHitObjectTime - (i32)this->iCurMusicPosWithOffsets;

    if(!cv::end_skip.getBool() && nextHitObjectDelta < 0) {
        this->music->setPositionMS(std::max(this->music->getLengthMS(), (u32)1) - 1);
        this->bWasSeekFrame = true;
    } else {
        this->music->setPositionMS(std::max(this->iNextHitObjectTime - (i32)(offset * offsetMultiplier), (i32)0));
        this->bWasSeekFrame = true;
    }

    soundEngine->play(this->getSkin()->s_menu_hit);

    if(!BanchoState::spectators.empty()) {
        // TODO @kiwec: how does skip work? does client jump to latest time or just skip beginning?
        //              is signaling skip even necessary?
        this->broadcast_spectator_frames();

        Packet packet;
        packet.id = OUTP_SPECTATE_FRAMES;
        packet.write<i32>(0);
        packet.write<u16>(0);
        packet.write<u8>((u8)LiveReplayAction::SKIP);
        packet.write<ScoreFrame>(ScoreFrame::get());
        packet.write<u16>(this->spectator_sequence++);
        BANCHO::Net::send_packet(packet);
    }
}

void BeatmapInterface::onKey(GameplayKeys key_flag, bool down, u64 timestamp) {
    if(this->is_watching || BanchoState::spectating) return;

    if(down) {  // pressed
        // this needs to be immediately updated here, to update state outside of early returns
        this->current_keys |= key_flag;

        // allow held keys to change while paused, but don't animate or anything
        // to reiterate, this needs to happen because we have no separate outside-gameplay-held-keys state (anymore),
        // BeatmapInterface::current_keys is the only source of truth
        if(this->bIsPaused || !osu->isInPlayMode()) return;

        if(this->bContinueScheduled) {
            // don't insta-unpause if we had a held key or doubleclicked or something
            if(engine->getTime() < this->fPrevUnpauseTime + cv::unpause_continue_delay.getFloat()) {
                return;
            }

            this->bClickedContinue =
                !ui->getModSelector()->isMouseInside() &&
                vec::length(this->getCursorPos() - this->vContinueCursorPoint) < (this->fHitcircleDiameter / 2.f);
            if(!this->bClickedContinue) return;
        }

        if(cv::mod_singletap.getBool() && !(this->lastPressedKey & key_flag)) {
            if(this->iCurrentHitObjectIndex > this->iAllowAnyNextKeyUntilHitObjectIndex) {
                soundEngine->play(this->getSkin()->s_combobreak);
                return;
            }
        }

        if(cv::mod_fullalternate.getBool() && (this->lastPressedKey & key_flag)) {
            if(this->iCurrentHitObjectIndex > this->iAllowAnyNextKeyUntilHitObjectIndex) {
                return;
            }
        }

        // key overlay & counter
        ui->getHUD()->animateInputOverlay(key_flag, true);

        if(this->bFailed) return;

        bool hasAnyHitObjects = (likely(!this->hitobjects.empty()));
        bool is_too_early = hasAnyHitObjects && this->iCurMusicPosWithOffsets < this->hitobjects[0]->click_time;
        bool should_count_keypress =
            !is_too_early && !this->bInBreak && !this->bIsInSkippableSection && this->bIsPlaying;
        if(should_count_keypress) osu->getScore()->addKeyCount(key_flag);

        this->lastPressedKey = key_flag;

        if((!osu->getModAuto() && !osu->getModRelax()) || !cv::auto_and_relax_block_user_input.getBool()) {
            // music position to be interped to next update (in update2())
            this->clicks.push_back(
                Click{.timestamp = timestamp, .pos = this->getCursorPos(), .music_pos = this->iCurMusicPosWithOffsets});
        }
    } else {  // released
        // always allow released key to animate
        ui->getHUD()->animateInputOverlay(key_flag, false);
        this->current_keys &= ~key_flag;
    }
}

void BeatmapInterface::selectBeatmap() {
    // sanity
    osu->bIsPlayingASelectedBeatmap = false;

    // if possible, continue playing where we left off
    if(likely(!!this->music) && (this->music->isPlaying())) this->iContinueMusicPos = this->music->getPositionMS();

    this->selectBeatmap(this->beatmap);
}

void BeatmapInterface::selectBeatmap(DatabaseBeatmap *map) {
    AsyncPPC::set_map(map);

    if(map != nullptr) {
        this->beatmap = map;

        this->nb_hitobjects = map->getNumObjects();

        // need to recheck/reload the music here since every difficulty might be using a different sound file
        this->bIsWaitingForPreview = true;
        this->loadMusic(false /*not reload*/, true /*async*/);
    }

    if(cv::beatmap_preview_mods_live.getBool()) {
        this->onModUpdate();
    } else {
        this->invalidateWholeMapPPInfo();  // onModUpdate already calls this
    }
}

void BeatmapInterface::deselectBeatmap() {
    this->iContinueMusicPos = 0;
    this->beatmap = nullptr;
    this->unloadObjects();
}

bool BeatmapInterface::play() {
    this->is_watching = false;

    if(this->start()) {
        this->last_spectator_broadcast = engine->getTime();
        RichPresence::onPlayStart();
        if(!BanchoState::spectators.empty()) {
            Packet packet;
            packet.id = OUTP_SPECTATE_FRAMES;
            packet.write<i32>(0);
            packet.write<u16>(0);  // 0 frames, we're just signaling map start
            packet.write<u8>((u8)LiveReplayAction::NEW_SONG);
            packet.write<ScoreFrame>(ScoreFrame::get());
            packet.write<u16>(this->spectator_sequence++);
            BANCHO::Net::send_packet(packet);

            if(cv::spec_share_map.getBool()) {
                ui->getChat()->addChannel("#spectator", true);
                ui->getChat()->handle_command("/np");
            }
        }
        if(BanchoState::is_online() && BanchoState::can_submit_scores()) {
            BanchoState::check_and_notify_nonsubmittable();
        }

        return true;
    }

    return false;
}

bool BeatmapInterface::watch(const FinishedScore &score, u32 start_ms) {
    this->sim.reset();
    if(score.replay.size() < 3) {
        // Replay is invalid
        return false;
    }

    this->bIsPlaying = false;
    this->bIsPaused = false;
    this->bContinueScheduled = false;
    this->unloadObjects();

    *osu->previous_mods = Replay::Mods::from_cvars();

    osu->watched_user_name = score.playerName;
    osu->watched_user_id = score.player_id;
    this->is_watching = true;

    Replay::Mods::use(score.mods);

    if(!this->start()) {
        // Map failed to load
        return false;
    }

    this->spectated_replay = score.replay;

    // Don't seek to beginning, since that would skip waiting time
    if(start_ms > 0) {
        this->seekMS(start_ms);
    }

    this->sim = std::make_unique<SimulatedBeatmapInterface>(this->beatmap, score.mods);
    this->sim->spectated_replay = score.replay;

    return true;
}

bool BeatmapInterface::spectate() {
    this->bIsPlaying = false;
    this->bIsPaused = false;
    this->bContinueScheduled = false;
    this->unloadObjects();

    const auto *user_info = BANCHO::User::get_user_info(BanchoState::spectated_player_id, true);
    osu->watched_user_id = BanchoState::spectated_player_id;
    osu->watched_user_name = user_info->name;

    *osu->previous_mods = Replay::Mods::from_cvars();

    FinishedScore score;
    score.client = "peppy-unknown";
    score.server = BanchoState::endpoint;
    score.mods = Replay::Mods::from_legacy(user_info->mods);
    Replay::Mods::use(score.mods);

    if(!this->start()) {
        // Map failed to load
        return false;
    }

    this->spectated_replay.clear();
    this->score_frames.clear();

    score.mods.flags |= ModFlags::NoFail;
    this->sim = std::make_unique<SimulatedBeatmapInterface>(this->beatmap, score.mods);
    this->sim->spectated_replay.clear();

    return true;
}

bool BeatmapInterface::start() {
    // set it to false to catch early returns first
    osu->bIsPlayingASelectedBeatmap = false;

    if(unlikely(!this->beatmap)) return false;

    osu->bIsPlayingASelectedBeatmap = true;
    osu->setShouldPauseBGThreads(true);

    soundEngine->play(this->getSkin()->s_menu_hit);

    osu->updateMods();

    // multiplayer
    this->all_players_loaded = false;
    this->all_players_skipped = false;
    this->player_loaded = false;

    // HACKHACK: stuck key quickfix
    const auto keys_held = this->current_keys & ~GameplayKeys::Smoke;
    if(this->is_watching || BanchoState::spectating) {
        this->onKey(GameplayKeys::K1, false, 0);
        this->onKey(GameplayKeys::M1, false, 0);
        this->onKey(GameplayKeys::K2, false, 0);
        this->onKey(GameplayKeys::M2, false, 0);
    } else {
        // only clear potential stuck mouse buttons
        if(cv::disable_mousebuttons.getBool()) {
            if(mouse->isLeftDown()) this->onKey(GameplayKeys::M1, false, 0);
            if(mouse->isRightDown()) this->onKey(GameplayKeys::M2, false, 0);
        }
    }
    if(keys_held != (this->current_keys & ~GameplayKeys::Smoke)) {
        // unexpand cursor if we released
        ui->getHUD()->animateCursorShrink();
    }

    this->flashlight_position = vec2{GameRules::OSU_COORD_WIDTH / 2, GameRules::OSU_COORD_HEIGHT / 2};

    // reset everything, including deleting any previously loaded hitobjects from another diff which we might just have
    // played
    this->unloadObjects();
    this->resetScore();
    this->smoke_trail.clear();

    // some hitobjects already need this information to be up-to-date before their constructor is called
    this->updatePlayfieldMetrics();
    this->updateHitobjectMetrics();
    this->bIsPreLoading = false;

    // actually load the difficulty (and the hitobjects)
    {
        DatabaseBeatmap::LOAD_GAMEPLAY_RESULT result = DatabaseBeatmap::loadGameplay(this->beatmap, this);
        if(result.error.errc) {
            using enum DatabaseBeatmap::LoadError::code;
            UString errorMessage;
            switch(result.error.errc) {
                case METADATA:
                case LOADMETADATA_ON_BEATMAPSET:
                case NON_STD_GAMEMODE:
                case UNKNOWN_VERSION: {
                    errorMessage = "Error: Couldn't load beatmap metadata :(";
                    debugLog("Osu Error: Couldn't load beatmap metadata {:s}", this->beatmap->getFilePath());
                } break;

                case FILE_LOAD: {
                    errorMessage = "Error: Couldn't load beatmap file :(";
                    debugLog("Osu Error: Couldn't load beatmap file {:s}", this->beatmap->getFilePath());
                } break;

                case NO_TIMINGPOINTS: {
                    errorMessage = "Error: No timingpoints in beatmap :(";
                    debugLog("Osu Error: No timingpoints in beatmap {:s}", this->beatmap->getFilePath());
                } break;

                case NO_OBJECTS: {
                    errorMessage = "Error: No hitobjects in beatmap :(";
                    debugLog("Osu Error: No hitobjects in beatmap {:s}", this->beatmap->getFilePath());
                } break;

                case TOOMANY_HITOBJECTS: {
                    errorMessage = "Error: Too many hitobjects in beatmap :(";
                    debugLog("Osu Error: Too many hitobjects in beatmap {:s}", this->beatmap->getFilePath());
                } break;

                default: {
                    if(result.error.errc != LOAD_INTERRUPTED) errorMessage = "Error: Couldn't load beatmap :(";
                    debugLog("Osu Error: Couldn't load beatmap {}: {}", this->beatmap->getFilePath(),
                             result.error.error_string());
                } break;
            }

            if(!errorMessage.isEmpty()) {
                ui->getNotificationOverlay()->addToast(errorMessage, ERROR_TOAST);
            }

            osu->bIsPlayingASelectedBeatmap = false;
            osu->setShouldPauseBGThreads(false);

            return false;
        }

        // move temp result data into beatmap
        this->hitobjects = std::move(result.hitobjects);
        this->breaks = std::move(result.breaks);
        this->getSkinMutable()->setBeatmapComboColors(std::move(result.combocolors));  // update combo colors in skin

        this->cur_timing_info = {};
        this->default_sample_set = result.defaultSampleSet;

        // load beatmap skin
        this->getSkinMutable()->loadBeatmapOverride(this->beatmap->getFolder());
    }

    // the drawing order is different from the playing/input order.
    // for drawing, if multiple hitobjects occupy the exact same time (duration) then they get drawn on top of the
    // active hitobject
    this->hitobjectsSortedByEndTime.clear();
    this->hitobjectsSortedByEndTime.reserve(this->hitobjects.size());
    for(const auto &unq : this->hitobjects) {
        this->hitobjectsSortedByEndTime.push_back(unq.get());
    }

    std::ranges::sort(this->hitobjectsSortedByEndTime, BeatmapInterface::sortHitObjectByEndTimeComp);

    // after the hitobjects have been loaded we can calculate the stacks
    this->calculateStacks();
    this->computeDrainRate();

    // start preloading (delays the play start until it's set to false, see isLoading())
    this->bIsPreLoading = true;
    this->iPreLoadingIndex = 0;

    // live pp/stars
    this->resetLiveStarsTasks();

    // load music
    this->bIsWaitingForPreview = false;  // cancel pending preview music play
    if(cv::restart_sound_engine_before_playing.getBool()) {
        // HACKHACK: Reload sound engine before starting the song, as it starts lagging after a while
        //           (i haven't figured out the root cause yet)
        soundEngine->pause(this->music);
        soundEngine->restart();

        // Restarting sound engine already reloads the music
    } else {
        this->reloadMusicNow();  // need to reload in case of speed/pitch changes (just to be sure)
    }

    this->music->setLoop(false);
    this->spectate_pause = false;
    this->bIsPaused = false;
    this->bContinueScheduled = false;

    this->bInBreak = cv::background_fade_after_load.getBool();
    anim::deleteExistingAnimation(&this->fBreakBackgroundFade);
    this->fBreakBackgroundFade = cv::background_fade_after_load.getBool() ? 1.0f : 0.0f;
    this->iPreviousSectionPassFailTime = -1;
    this->fShouldFlashSectionPass = 0.0f;
    this->fShouldFlashSectionFail = 0.0f;

    this->music->setPositionMS(0);
    this->iCurMusicPos = 0;

    // we are waiting for an asynchronous start of the beatmap in the next update()
    this->bIsPlaying = true;
    this->bIsWaiting = true;
    this->fWaitTime = Timing::getTimeReal<f32>();

    cv::snd_change_check_interval.setValue(0.0f);

    if(this->beatmap->getLocalOffset() != 0)
        ui->getNotificationOverlay()->addNotification(
            fmt::format("Using local beatmap offset ({} ms)", this->beatmap->getLocalOffset()), 0xffffffff, false,
            0.75f);

    osu->iQuickSaveMS = 0;  // reset

    ui->hide();
    osu->updateConfineCursor();
    osu->updateWindowsKeyDisable();

    if(cv::mod_fposu.getBool() && cv::fposu_center_cursor_on_start.getBool() &&
       !(env->isCursorVisible() || !env->isCursorInWindow() || !env->winFocused())) {
        osu->getFPoSu()->resetCamera();
    }

    soundEngine->play(this->getSkin()->s_expand);

    // NOTE: loading failures are handled dynamically in update(), so temporarily assume everything has worked in here
    return true;
}

void BeatmapInterface::restart(bool quick) {
    soundEngine->stop(this->getSkin()->s_fail);

    if(!this->bIsWaiting) {
        this->bIsRestartScheduled = true;
        this->bIsRestartScheduledQuick = quick;
    } else if(this->bIsPaused && !BanchoState::spectating) {
        this->pause(false);
    }
}

void BeatmapInterface::actualRestart() {
    // reset everything
    this->resetScore();
    this->resetHitObjects(-1000);
    this->smoke_trail.clear();
    this->all_clicks.clear();

    // we are waiting for an asynchronous start of the beatmap in the next update()
    this->bIsWaiting = true;
    this->fWaitTime = Timing::getTimeReal<f32>();

    // if the first hitobject starts immediately, add artificial wait time before starting the music
    if(likely(!this->hitobjects.empty())) {
        if(this->hitobjects[0]->click_time < cv::early_note_time.getInt()) {
            this->bIsWaiting = true;
            this->fWaitTime = Timing::getTimeReal<f32>() + cv::early_note_time.getFloat() / 1000.0f;
        }
    }

    // pause temporarily if playing
    if(this->music->isPlaying()) soundEngine->pause(this->music);

    // reset/restore frequency (from potential fail before)
    this->music->setFrequency(0);

    this->music->setLoop(false);
    this->bIsPaused = false;
    this->bContinueScheduled = false;

    this->bInBreak = false;
    anim::deleteExistingAnimation(&this->fBreakBackgroundFade);
    this->fBreakBackgroundFade = 0.0f;
    this->iPreviousSectionPassFailTime = -1;
    this->fShouldFlashSectionPass = 0.0f;
    this->fShouldFlashSectionFail = 0.0f;

    this->onModUpdate();  // sanity

    // reset position
    this->music->setPositionMS(0);
    this->bWasSeekFrame = true;
    this->iCurMusicPos = 0;

    this->bIsPlaying = true;
    this->bTempSeekNF = false;
    this->is_watching = false;
}

void BeatmapInterface::pause(bool quitIfWaiting) {
    if(unlikely(!this->beatmap)) return;
    if(cv::mod_no_pausing.getBool()) return;
    if(BanchoState::spectating) {
        Spectating::stop();
        return;
    }

    const bool isFirstPause = !this->bContinueScheduled;

    // NOTE: this assumes that no beatmap ever goes far beyond the end of the music
    // NOTE: if pure virtual audio time is ever supported (playing without SoundEngine) then this needs to be adapted
    // fix pausing after music ends breaking beatmap state (by just not allowing it to be paused)
    if(this->fAfterMusicIsFinishedVirtualAudioTimeStart >= 0.0f) {
        const f32 delta = Timing::getTimeReal<f32>() - this->fAfterMusicIsFinishedVirtualAudioTimeStart;
        if(delta < 5.0f)  // WARNING: sanity limit, always allow escaping after 5 seconds of overflow time
            return;
    }

    if(this->bIsPlaying) {
        if(this->bIsWaiting && quitIfWaiting) {
            // if we are still m_bIsWaiting, pausing the game via the escape key is the
            // same as stopping playing
            this->stop();
        } else {
            // first time pause pauses the music
            // case 1: the beatmap is already "finished", jump to the ranking screen if some small amount of time past
            //         the last objects endTime
            // case 2: in the middle somewhere, pause as usual
            HitObject *lastHitObject =
                this->hitobjectsSortedByEndTime.size() > 0 ? this->hitobjectsSortedByEndTime.back() : nullptr;
            if(lastHitObject != nullptr && lastHitObject->isFinished() &&
               (this->iCurMusicPos > lastHitObject->getEndTime() + (i32)cv::end_skip_time.getInt()) &&
               cv::end_skip.getBool()) {
                this->stop(false);
            } else {
                soundEngine->pause(this->music);
                this->bIsPlaying = false;
                this->bIsPaused = true;
            }
        }
    } else if(this->bIsPaused && !this->bContinueScheduled) {
        // if this is the first time unpausing
        if(osu->getModAuto() || osu->getModAutopilot() || this->bIsInSkippableSection || this->is_watching) {
            if(!this->bIsWaiting) {
                // only force play() if we were not early waiting
                soundEngine->play(this->music);
            }

            this->bIsPlaying = true;
            this->bIsPaused = false;
        } else {
            // otherwise, schedule a continue (wait for user to click, handled in update())
            // first time unpause schedules a continue
            this->bIsPaused = false;
            this->bContinueScheduled = true;
        }
    } else {
        // if this is not the first time pausing/unpausing, then just toggle the pause state (the visibility of the
        // pause menu is handled in the Osu class, a bit shit)
        this->bIsPaused = !this->bIsPaused;
    }

    if(this->bIsPaused && isFirstPause) {
        if(!cv::submit_after_pause.getBool()) {
            debugLog("Disabling score submission due to pausing");
            this->is_submittable = false;
        }

        this->vContinueCursorPoint = this->getMousePos();
        if(cv::mod_fps.getBool()) {
            this->vContinueCursorPoint = GameRules::getPlayfieldCenter();
        }
    }

    if(!this->bIsPaused) {
        // clear potential held mouse buttons from pause menu, if mousebuttons during gameplay are disabled
        if(cv::disable_mousebuttons.getBool()) {
            const auto keys_held = this->current_keys & ~GameplayKeys::Smoke;

            // only clear if they're actually down, since M1/M2 might be mapped to keyboard keys
            if(mouse->isLeftDown()) this->onKey(GameplayKeys::M1, false, 0);
            if(mouse->isRightDown()) this->onKey(GameplayKeys::M2, false, 0);

            if(keys_held != (this->current_keys & ~GameplayKeys::Smoke)) {
                // unexpand cursor if we released
                ui->getHUD()->animateCursorShrink();
            }
        }

        this->fPrevUnpauseTime = engine->getTime();
    }

    // if we have failed, and the user early exits to the pause menu, stop the failing animation
    if(this->bFailed) anim::deleteExistingAnimation(&this->fFailAnim);
}

void BeatmapInterface::pausePreviewMusic(bool toggle) {
    if(likely(!!this->music)) {
        if(this->music->isPlaying())
            soundEngine->pause(this->music);
        else if(toggle)
            soundEngine->play(this->music);
    }
}

bool BeatmapInterface::isPreviewMusicPlaying() {
    if(likely(!!this->music)) return this->music->isPlaying();

    return false;
}

void BeatmapInterface::stop(bool quit) {
    osu->bIsPlayingASelectedBeatmap = false;

    soundEngine->stop(this->getSkin()->s_fail);

    if(unlikely(!this->beatmap)) {
        osu->setShouldPauseBGThreads(false);
        return;
    }

    this->bIsPlaying = false;
    this->bIsPaused = false;
    this->bContinueScheduled = false;

    // Call this BEFORE unloadObjects()!
    FinishedScore score =
        BanchoState::is_playing_a_multi_map() ? ui->getRoom()->get_approximate_score() : this->saveAndSubmitScore(quit);

    // Auto mod was "temporary" since it was set from Ctrl+Clicking a map, not from the mod selector
    if(osu->bModAutoTemp) {
        auto *btnAuto = ui->getModSelector()->getGridButton(ModSelector::AUTO_POS);
        if(btnAuto && btnAuto->isOn()) {
            btnAuto->click();
        }
        osu->bModAutoTemp = false;
    }

    if(this->is_watching || BanchoState::spectating) {
        Replay::Mods::use(*osu->previous_mods);
    }

    this->is_watching = false;
    this->spectated_replay.clear();
    this->score_frames.clear();
    this->sim.reset();

    this->unloadObjects();

    if(BanchoState::is_playing_a_multi_map()) {
        if(quit) {
            osu->onPlayEnd(score, true);
        } else {
            ui->getRoom()->onClientScoreChange(true);
            Packet packet;
            packet.id = OUTP_FINISH_MATCH;
            BANCHO::Net::send_packet(packet);
        }
    } else {
        osu->onPlayEnd(score, quit);
    }

    osu->setShouldPauseBGThreads(false);
}

void BeatmapInterface::fail(bool force_death) {
    if(this->bFailed) return;
    if((this->is_watching || BanchoState::spectating) && !force_death) return;

    // Change behavior of relax mod when online
    if(BanchoState::is_online() && osu->getModRelax()) return;

    if(!BanchoState::is_playing_a_multi_map() && cv::drain_kill.getBool()) {
        soundEngine->play(this->getSkin()->s_fail);

        // trigger music slowdown and delayed menu, see update()
        this->bFailed = true;
        this->fFailAnim = 1.0f;
        anim::moveLinear(&this->fFailAnim, 0.0f, cv::fail_time.getFloat(), true);
    } else if(!osu->getScore()->isDead()) {
        debugLog("Disabling score submission due to death");
        this->is_submittable = false;

        anim::deleteExistingAnimation(&this->fHealth2);
        this->fHealth = 0.0;
        this->fHealth2 = 0.0f;

        // Send a score update with health = 0 so server knows we died
        ui->getRoom()->onClientScoreChange(true);

        if(cv::drain_kill_notification_duration.getFloat() > 0.0f) {
            if(!osu->getScore()->hasDied())
                ui->getNotificationOverlay()->addNotification("You have failed, but you can keep playing!", 0xffffffff,
                                                              false, cv::drain_kill_notification_duration.getFloat());
        }
    }

    if(!osu->getScore()->isDead()) osu->getScore()->setDead(true);
}

void BeatmapInterface::cancelFailing() {
    if(!this->bFailed || this->fFailAnim <= 0.0f) return;

    this->bFailed = false;

    anim::deleteExistingAnimation(&this->fFailAnim);
    this->fFailAnim = 1.0f;

    if(likely(!!this->music)) this->music->setFrequency(0.0f);

    soundEngine->stop(this->getSkin()->s_fail);
}

f32 BeatmapInterface::getIdealVolume() {
    if(unlikely(!this->music)) return 1.f;

    f32 volume = cv::volume_music.getFloat();
    f32 modifier = 1.f;

    if(cv::normalize_loudness.getBool()) {
        if(unlikely(!this->beatmap)) return volume;
        if(this->beatmap->loudness != 0.f) {
            modifier = std::pow(10, (cv::loudness_target.getFloat() - this->beatmap->loudness) / 20);
        }
    }

    return volume * modifier;
}

void BeatmapInterface::setMusicSpeed(f32 speed) {
    if(likely(!!this->music)) {
        if((osu->isInPlayMode() || cv::beatmap_preview_mods_live.getBool())) {
            this->music->setSpeed(speed);
        } else {  // reset playback speed
            this->music->setSpeed(1.f);
        }
    }

    // also update music pitch
    this->setMusicPitch(this->getPitchMultiplier());
}

void BeatmapInterface::setMusicPitch(f32 pitch) {
    if(likely(!!this->music)) {
        if((osu->isInPlayMode() || cv::beatmap_preview_mods_live.getBool())) {
            this->music->setPitch(pitch);
        } else {  // reset playback pitch
            this->music->setPitch(1.f);
        }
    }
}

void BeatmapInterface::seekMS(u32 ms) {
    this->bTempSeekNF = false;
    if(unlikely(!this->beatmap) || unlikely(!this->music) || this->bFailed) return;

    // this->resetScore() resets this->is_submittable
    bool was_submittable = this->is_submittable;

    this->bWasSeekFrame = true;
    this->fWaitTime = 0.0f;

    this->music->setPositionMS(ms);
    this->music->setBaseVolume(this->getIdealVolume());
    this->setMusicSpeed(this->getSpeedMultiplier());

    this->resetHitObjects(ms);
    this->resetScore();
    this->is_submittable = false;

    this->iPreviousSectionPassFailTime = -1;

    if(this->bIsWaiting) {
        this->bIsWaiting = false;
        this->bIsPlaying = true;
        this->bIsRestartScheduledQuick = false;

        soundEngine->play(this->music);

        // if there are calculations in there that need the hitobjects to be loaded, also applies speed/pitch
        this->onModUpdate(false, false);
    }

    if(BanchoState::spectating) {
        debugLog("After seeking, we are now {:d}ms behind the player.", this->last_frame_ms - (i32)ms);
    }

    if(this->is_watching) {
        // When seeking backwards, restart simulation from beginning
        if(std::cmp_less(ms, this->iCurMusicPos)) {
            this->sim = std::make_unique<SimulatedBeatmapInterface>(this->beatmap, osu->getScore()->mods);
            this->sim->spectated_replay = this->spectated_replay;
            osu->getScore()->reset();
        }
    }

    if(!this->is_watching && !BanchoState::spectating) {  // score submission already disabled when watching replay
        if(was_submittable && BanchoState::can_submit_scores()) {
            ui->getNotificationOverlay()->addToast(US_("Score will not submit due to seeking"), ERROR_TOAST);
        }
        this->bTempSeekNF = true;
    }
}

u32 BeatmapInterface::getTime() const {
    if(likely(!!this->music) && this->music->isAsyncReady())
        return this->music->getPositionMS();
    else
        return 0;
}

u32 BeatmapInterface::getStartTimePlayable() const {
    if(likely(!this->hitobjects.empty()))
        return (u32)this->hitobjects[0]->click_time;
    else
        return 0;
}

u32 BeatmapInterface::getLength() const {
    if(likely(!!this->music) && this->music->isAsyncReady())
        return this->music->getLengthMS();
    else if(likely(!!this->beatmap))
        return this->beatmap->getLengthMS();
    else
        return 0;
}

u32 BeatmapInterface::getLengthPlayable() const {
    if(likely(!this->hitobjects.empty()))
        return (u32)((this->hitobjects.back()->click_time + this->hitobjects.back()->duration) -
                     this->hitobjects[0]->click_time);
    else
        return this->getLength();
}

f32 BeatmapInterface::getPercentFinished() const {
    if(f64 length = this->getLength(); length > 0.)
        return (f64)this->getTime() / length;
    else
        return 0.f;
}

f32 BeatmapInterface::getPercentFinishedPlayable() const {
    if(this->bIsWaiting) {
        // this->fWaitTime is set to the time when the wait time ENDS
        f32 wait_duration = (cv::early_note_time.getFloat() / 1000.f);
        if(wait_duration <= 0.f) return 0.f;

        f32 wait_start = this->fWaitTime - wait_duration;
        f32 wait_percent = (Timing::getTimeReal<f32>() - wait_start) / wait_duration;
        return std::clamp(wait_percent, 0.f, 1.f);
    } else {
        f32 length_playable = this->getLengthPlayable();
        if(length_playable <= 0.f) return 0.f;

        f32 time_since_first_object = (f32)this->getTime() - (f32)this->getStartTimePlayable();
        return std::clamp(time_since_first_object / length_playable, 0.f, 1.f);
    }
}

int BeatmapInterface::getMostCommonBPM() const {
    if(likely(!!this->beatmap)) {
        if(likely(!!this->music))
            return (int)(this->beatmap->getMostCommonBPM() * this->music->getSpeed());
        else
            return (int)(this->beatmap->getMostCommonBPM() * this->getSpeedMultiplier());
    } else
        return 0;
}

f32 BeatmapInterface::getSpeedMultiplier() const {
    if(f32 speed_override = osu->getScore()->mods.speed; speed_override >= 0.05f) {
        return std::max(speed_override, 0.05f);
    } else {
        return 1.f;
    }
}

f32 BeatmapInterface::getPitchMultiplier() const {
    // if pitch compensation is off, keep pitch constant (do not apply daycore/nightcore pitch)
    if(!cv::snd_speed_compensate_pitch.getBool()) return 1.f;

    float pitchMultiplier = 1.0f;

    // TODO: re-add actual nightcore/daycore mods
    const float speedMult = this->getSpeedMultiplier();
    if(cv::nightcore_enjoyer.getBool() && speedMult != 1.f) {
        pitchMultiplier = speedMult < 1.f ? 0.92f /* daycore */ : 1.1166f /* nightcore */;
    }

    return pitchMultiplier;
}

// currently just a passthrough for the main skin, might return beatmap skins in the future
const Skin *BeatmapInterface::getSkin() const { return osu->getSkin(); }
Skin *BeatmapInterface::getSkinMutable() { return osu->getSkinMutable(); }

f32 BeatmapInterface::getRawAR() const {
    if(unlikely(!this->beatmap)) return 5.0f;

    return std::clamp<f32>(this->beatmap->getAR() * Osu::getDifficultyMultiplier(), 0.0f, 10.0f);
}

f32 BeatmapInterface::getAR() const {
    if(unlikely(!this->beatmap)) return 5.0f;

    f32 AR;
    if(f32 ar_overridenegative = cv::ar_overridenegative.getFloat(); ar_overridenegative < 0.0f)
        AR = ar_overridenegative;
    else if(f32 ar_override = cv::ar_override.getFloat(); ar_override >= 0.0f)
        AR = ar_override;
    else
        AR = this->getRawAR();

    if(cv::ar_override_lock.getBool()) AR = GameRules::arWithSpeed(AR, 1.f / this->getSpeedMultiplier());

    if(cv::mod_artimewarp.getBool() && likely(!this->hitobjects.empty())) {
        const f32 percent = 1.0f - ((f64)(this->iCurMusicPos - this->hitobjects[0]->click_time) /
                                    (f64)(this->hitobjects.back()->click_time + this->hitobjects.back()->duration -
                                          this->hitobjects[0]->click_time)) *
                                       (1.0f - cv::mod_artimewarp_multiplier.getFloat());
        AR *= percent;
    }

    if(cv::mod_arwobble.getBool())
        AR += std::sin((this->iCurMusicPos / 1000.0f) * cv::mod_arwobble_interval.getFloat()) *
              cv::mod_arwobble_strength.getFloat();

    return AR;
}

f32 BeatmapInterface::getCS() const {
    if(unlikely(!this->beatmap)) return 5.0f;

    f32 CS;
    if(f32 cs_overridenegative = cv::cs_overridenegative.getFloat(); cs_overridenegative < 0.0f)
        CS = cs_overridenegative;
    else if(f32 cs_override = cv::cs_override.getFloat(); cs_override >= 0.0f)
        CS = cs_override;
    else
        CS = std::clamp<f32>(this->beatmap->getCS() * Osu::getCSDifficultyMultiplier(), 0.0f, 10.0f);

    if(cv::mod_minimize.getBool() && likely(!this->hitobjects.empty())) {
        if(likely(!this->hitobjects.empty())) {
            const f32 percent = 1.0f + ((f64)(this->iCurMusicPos - this->hitobjects[0]->click_time) /
                                        (f64)(this->hitobjects.back()->click_time + this->hitobjects.back()->duration -
                                              this->hitobjects[0]->click_time)) *
                                           cv::mod_minimize_multiplier.getFloat();
            CS *= percent;
        }
    }

    if(cv::cs_cap_sanity.getBool()) CS = std::min(CS, 12.1429f);

    return CS;
}

f32 BeatmapInterface::getHP() const {
    if(unlikely(!this->beatmap)) return 5.0f;

    f32 HP;
    if(f32 hp_override = cv::hp_override.getFloat(); hp_override >= 0.0f)
        HP = hp_override;
    else
        HP = std::clamp<f32>(this->beatmap->getHP() * Osu::getDifficultyMultiplier(), 0.0f, 10.0f);

    return HP;
}

f32 BeatmapInterface::getRawOD() const {
    if(unlikely(!this->beatmap)) return 5.0f;

    return std::clamp<f32>(this->beatmap->getOD() * Osu::getDifficultyMultiplier(), 0.0f, 10.0f);
}

f32 BeatmapInterface::getOD() const {
    f32 OD;
    if(f32 od_override = cv::od_override.getFloat(); od_override >= 0.0f)
        OD = od_override;
    else
        OD = this->getRawOD();

    if(cv::od_override_lock.getBool()) OD = GameRules::odWithSpeed(OD, 1.f / this->getSpeedMultiplier());

    return OD;
}

u32 BeatmapInterface::getBreakDurationTotal() const {
    if(unlikely(!this->beatmap && this->breaks.empty())) return 0;

    u32 breakDurationTotal = 0;
    for(auto i : this->breaks) {
        breakDurationTotal += (u32)(i.endTime - i.startTime);
    }

    return breakDurationTotal;
}

DatabaseBeatmap::BREAK BeatmapInterface::getBreakForTimeRange(i64 startMS, i64 positionMS, i64 endMS) const {
    DatabaseBeatmap::BREAK curBreak;

    curBreak.startTime = -1;
    curBreak.endTime = -1;

    for(const auto &i : this->breaks) {
        if(i.startTime >= startMS && i.endTime <= endMS) {
            if(positionMS >= curBreak.startTime) curBreak = i;
        }
    }

    return curBreak;
}

LiveScore::HIT BeatmapInterface::addHitResult(HitObject *hitObject, LiveScore::HIT hit, i32 delta, bool isEndOfCombo,
                                              bool ignoreOnHitErrorBar, bool hitErrorBarOnly, bool ignoreCombo,
                                              bool ignoreScore, bool ignoreHealth) {
    using enum LiveScore::HIT;

    // Frames are already written on every keypress/release.
    // For some edge cases, we need to write extra frames to avoid replaybugs.
    if(!hitErrorBarOnly) {
        const bool should_write_frame =
            // Slider interactions
            // Surely buzz sliders won't be an issue... Clueless
            (hit == HIT_SLIDER10)             //
            || (hit == HIT_SLIDER30)          //
            || (hit == HIT_MISS_SLIDERBREAK)  //
            // Relax: no keypresses, instead we write on every hitresult
            || (osu->getModRelax() && ((hit == HIT_50)        //
                                       || (hit == HIT_100)    //
                                       || (hit == HIT_300)    //
                                       || (hit == HIT_MISS))  //
               );

        if(should_write_frame) {
            this->write_frame();
        }
    }

    // handle perfect & sudden death
    if(osu->getModSS()) {
        if(!hitErrorBarOnly && hit != HIT_300 && hit != HIT_300G && hit != HIT_SLIDER10 && hit != HIT_SLIDER30 &&
           hit != HIT_SPINNERSPIN && hit != HIT_SPINNERBONUS) {
            this->restart();
            return HIT_MISS;
        }
    } else if(osu->getModSD()) {
        if(hit == HIT_MISS) {
            if(cv::mod_suddendeath_restart.getBool())
                this->restart();
            else
                this->fail();

            return HIT_MISS;
        }
    }

    // miss sound
    if(hit == HIT_MISS) this->playMissSound();

    // score
    osu->getScore()->addHitResult(this, hitObject, hit, delta, ignoreOnHitErrorBar, hitErrorBarOnly, ignoreCombo,
                                  ignoreScore);

    // health
    LiveScore::HIT returnedHit = HIT_MISS;
    if(!ignoreHealth) {
        this->addHealth(osu->getScore()->getHealthIncrease(this, hit), true);

        // geki/katu handling
        if(isEndOfCombo) {
            const int comboEndBitmask = osu->getScore()->getComboEndBitmask();

            if(comboEndBitmask == 0) {
                returnedHit = HIT_300G;
                this->addHealth(osu->getScore()->getHealthIncrease(this, returnedHit), true);
                osu->getScore()->addHitResultComboEnd(returnedHit);
            } else if((comboEndBitmask & 2) == 0) {
                if(hit == HIT_100) {
                    returnedHit = HIT_100K;
                    this->addHealth(osu->getScore()->getHealthIncrease(this, returnedHit), true);
                    osu->getScore()->addHitResultComboEnd(returnedHit);
                } else if(hit == HIT_300) {
                    returnedHit = HIT_300K;
                    this->addHealth(osu->getScore()->getHealthIncrease(this, returnedHit), true);
                    osu->getScore()->addHitResultComboEnd(returnedHit);
                }
            } else if(hit != HIT_MISS)
                this->addHealth(osu->getScore()->getHealthIncrease(this, HIT_MU), true);

            osu->getScore()->setComboEndBitmask(0);
        }
    }

    return returnedHit;
}

void BeatmapInterface::addSliderBreak() {
    // handle perfect & sudden death
    if(osu->getModSS()) {
        this->restart();
        return;
    } else if(osu->getModSD()) {
        if(cv::mod_suddendeath_restart.getBool())
            this->restart();
        else
            this->fail();

        return;
    }

    // miss sound
    this->playMissSound();

    // score
    osu->getScore()->addSliderBreak();
}

void BeatmapInterface::addScorePoints(int points, bool isSpinner) { osu->getScore()->addPoints(points, isSpinner); }

void BeatmapInterface::addHealth(f64 percent, bool isFromHitResult) {
    // never drain before first hitobject (or if drain is disabled)
    if(this->bTempSeekNF || cv::drain_disabled.getBool() || osu->getScore()->mods.has(ModFlags::NoHP) ||
       (likely(!this->hitobjects.empty()) && this->iCurMusicPosWithOffsets < this->hitobjects[0]->click_time))
        return;

    // never drain after last hitobject
    if(this->hitobjectsSortedByEndTime.size() > 0 &&
       this->iCurMusicPosWithOffsets >
           (this->hitobjectsSortedByEndTime.back()->click_time + this->hitobjectsSortedByEndTime.back()->duration))
        return;

    if(this->bFailed) {
        anim::deleteExistingAnimation(&this->fHealth2);

        this->fHealth = 0.0;
        this->fHealth2 = 0.0f;

        return;
    }

    if(isFromHitResult && percent > 0.0) {
        ui->getHUD()->animateKiBulge();

        if(this->fHealth > 0.9) ui->getHUD()->animateKiExplode();
    }

    this->fHealth = std::clamp<f64>(this->fHealth + percent, 0.0, 1.0);

    // handle generic fail state (2)
    const bool isDead = this->fHealth < 0.001;
    if(isDead && !osu->getModNF()) {
        if(osu->getModEZ() && osu->getScore()->getNumEZRetries() > 0)  // retries with ez
        {
            osu->getScore()->setNumEZRetries(osu->getScore()->getNumEZRetries() - 1);

            // special case: set health to 160/200 (osu!stable behavior, seems fine for all drains)
            this->fHealth = 160.0f / 200.f;
            this->fHealth2 = (f32)this->fHealth;

            anim::deleteExistingAnimation(&this->fHealth2);
        } else if(isFromHitResult && percent < 0.0)  // judgement fail
        {
            this->fail();
        }
    }
}

bool BeatmapInterface::sortHitObjectByStartTimeComp(HitObject const *a, HitObject const *b) {
    if(a == b) return false;

    if((a->click_time) != (b->click_time)) return (a->click_time) < (b->click_time);

    if(a->type != b->type) return static_cast<int>(a->type) < static_cast<int>(b->type);
    if(a->combo_number != b->combo_number) return a->combo_number < b->combo_number;

    auto aPosAtStartTime = a->getRawPosAt(a->click_time), bPosAtClickTime = b->getRawPosAt(b->click_time);
    if(aPosAtStartTime != bPosAtClickTime) return vec::all(vec::lessThan(aPosAtStartTime, bPosAtClickTime));

    return false;  // equivalent
}

bool BeatmapInterface::sortHitObjectByEndTimeComp(HitObject const *a, HitObject const *b) {
    if(a == b) return false;

    if((a->getEndTime()) != (b->getEndTime())) return (a->getEndTime()) < (b->getEndTime());

    if(a->type != b->type) return static_cast<int>(a->type) < static_cast<int>(b->type);
    if(a->combo_number != b->combo_number) return a->combo_number < b->combo_number;

    auto aPosAtEndTime = a->getRawPosAt(a->getEndTime()), bPosAtClickTime = b->getRawPosAt(b->getEndTime());
    if(aPosAtEndTime != bPosAtClickTime) return vec::all(vec::lessThan(aPosAtEndTime, bPosAtClickTime));

    return false;  // equivalent
}

bool BeatmapInterface::canDraw() {
    if(!this->bIsPlaying && !this->bIsPaused && !this->bContinueScheduled && !this->bIsWaiting) return false;
    if(unlikely(!this->beatmap) || unlikely(!this->music))  // sanity check
        return false;

    return true;
}

void BeatmapInterface::handlePreviewPlay() {
    if(unlikely(!this->music)) return;

    if(!ui->getMainMenu()->isVisible() && loading_reselect_map != MD5Hash{}) {
        // if we are waiting to reselect a main menu beatmap after loading song browser, don't seek at all
        this->music->setLoop(cv::beatmap_preview_music_loop.getBool());
        if(this->music->isPlaying()) {
            return;
        }
    }

    bool almost_finished = false;
    if((!this->music->isPlaying() || (almost_finished = this->music->getPositionPct() > 0.95f)) &&
       likely(!!this->beatmap)) {
        // soundEngine->stop(this->music);

        if(soundEngine->play(this->music)) {
            // this is an assumption, but should be good enough for most songs
            // reset playback position when the song has nearly reached the end (when the user switches back to the results
            // screen or the songbrowser after playing)
            // (check again after restarting due to async)
            if(almost_finished || this->music->getPositionPct() > 0.95f) this->iContinueMusicPos = 0;

            if(this->music->getFrequency() < this->fMusicFrequencyBackup)  // player has died, reset frequency
                this->music->setFrequency(this->fMusicFrequencyBackup);

            // When neomod is initialized, it starts playing a random song in the main menu.
            // Users can set a convar to make it start at its preview point instead.
            // The next songs will start at the beginning regardless.
            static bool should_start_song_at_preview_point = cv::start_first_main_menu_song_at_preview_point.getBool();
            const bool start_at_song_beginning = ui->getMainMenu()->isVisible() && !should_start_song_at_preview_point;
            should_start_song_at_preview_point = false;

            if(start_at_song_beginning) {
                this->iContinueMusicPos = 0;
            }

            const u32 position_to_set =
                (this->iContinueMusicPos != 0 || start_at_song_beginning)
                    ? this->iContinueMusicPos
                    : (this->beatmap->getPreviewTime() < 0 ? (u32)(this->music->getLengthMS() * 0.40f)
                                                           : this->beatmap->getPreviewTime());

            this->music->setPositionMS(position_to_set);
            this->bWasSeekFrame = true;

            this->music->setBaseVolume(this->getIdealVolume());
            this->setMusicSpeed(this->getSpeedMultiplier());
        }
    }

    // always loop during preview
    this->music->setLoop(cv::beatmap_preview_music_loop.getBool());
}

void BeatmapInterface::loadMusic(bool reload, bool async) {
    const std::string beatmapSoundPath = this->beatmap ? this->beatmap->getFullSoundFilePath() : "";
    if(beatmapSoundPath.empty()) {
        if(this->beatmap) {
            debugLog("no music file for {}!", this->beatmap->getFilePath());
        }
        // pause previously playing music, if any
        // only if we are not waiting for reload
        if(loading_reselect_map.empty()) {
            soundEngine->pause(this->music);
        }
        return;
    }

    // try getting existing sound resource first and rebuilding with a new path
    if(!this->music) {
        this->music = resourceManager->getSound("BEATMAP_MUSIC");
    }

    const std::string oldPath = this->music ? this->music->getFilePath() : "";
    const std::string &newPath = beatmapSoundPath;

    const bool pathChanged = newPath != oldPath;
    const bool haveExistingMusic = !!this->music;
    const bool musicAlreadyLoadedSuccessfully = haveExistingMusic && this->music->isReady();

    // we can skip if the path didn't change and we already loaded
    // also skip if we have async music being loaded and we are async
    const bool skipLoading = !reload &&                          //
                             !pathChanged &&                     //
                             (musicAlreadyLoadedSuccessfully ||  //
                              (async && haveExistingMusic && resourceManager->isLoadingResource(this->music)));

    logIf(cv::debug_osu.getBool() || cv::debug_snd.getBool(),
          "reload: {} async: {} path changed: {} existing music: {} existing music loaded successfully: {} skipping: "
          "{}",
          reload, async, pathChanged, haveExistingMusic, musicAlreadyLoadedSuccessfully, skipLoading);

    if(skipLoading) {
        if(this->bIsWaitingForPreview && !resourceManager->isLoadingResource(this->music)) {
            // manually handle preview play from selectBeatmap, since the callback won't be fired (was already)
            this->bIsWaitingForPreview = false;
            this->handlePreviewPlay();
            if(!ui->getMainMenu()->isVisible() && db->isFinished()) {
                loading_reselect_map.clear();
            }
        }
        return;
    }

    // load the song (again)
    if(haveExistingMusic) {
        // rebuild with new path
        this->music->rebuild(newPath, async);
    } else {
        // fresh load
        if(async) resourceManager->requestNextLoadAsync();
        this->music = resourceManager->loadSoundAbs(newPath, "BEATMAP_MUSIC", true /* stream */, false, false);

        // for sync load it should be ready now (one-time manual call for a fresh load, then the callback should handle it after)
        if(!async) {
            BeatmapInterface::onMusicLoadingFinished(this->music, this);
        }

        // set the callback after we call it once manually
        this->music->setOnInitCB(
            Resource::SyncLoadCB{.userdata = nullptr, .callback = &BeatmapInterface::onMusicLoadingFinished});
    }

    // TODO: load custom hitsounds
    // TODO: load custom skin elements
}

void BeatmapInterface::onMusicLoadingFinished(Resource *rs, void * /*userdata*/) {
    const auto &map_iface = osu->getMapInterface();
    if(!map_iface) return;

    auto *music = static_cast<Sound *>(rs);
    if(!music || music != map_iface->getMusic()) {
        // impossible?
        if(music) {
            debugLog("loaded music was different from currently playing!?");
        }
        return;
    }

    if(!music->isReady() || !soundEngine->enqueue(music)) {
        logIf(cv::debug_osu.getBool() || cv::debug_snd.getBool(), "failed to enqueue music at {}",
              music->getFilePath());
    } else {
        // ready and enqueued
        music->setBaseVolume(map_iface->getIdealVolume());
        map_iface->fMusicFrequencyBackup = music->getFrequency();
        map_iface->setMusicSpeed(map_iface->getSpeedMultiplier());
        if(map_iface->bIsWaitingForPreview) {
            map_iface->bIsWaitingForPreview = false;
            map_iface->handlePreviewPlay();
            if(!ui->getMainMenu()->isVisible() && db->isFinished()) {
                loading_reselect_map.clear();
            }
        }
    }
}

void BeatmapInterface::unloadMusic() {
    if(this->music) {
        resourceManager->destroyResource(this->music);
        this->music = nullptr;
    }

    // TODO: unload custom hitsounds
    // TODO: unload custom skin elements
}

void BeatmapInterface::unloadObjects() {
    this->currentHitObject = nullptr;
    this->hitobjects.clear();
    this->hitobjectsSortedByEndTime.clear();
    this->misaimObjects.clear();
    this->breaks.clear();
    this->clicks.clear();
    this->all_clicks.clear();
}

void BeatmapInterface::resetHitObjects(i32 curPos) {
    for(auto &hitobject : this->hitobjects) {
        hitobject->onReset(curPos);
        hitobject->update(curPos, engine->getFrameTime());
        hitobject->onReset(curPos);
    }
    ui->getHUD()->resetHitErrorBar();
}

void BeatmapInterface::resetScore() {
    this->is_submittable = cvars().areAllCvarsSubmittable();

    this->live_replay.clear();
    this->live_replay.push_back(LegacyReplay::Frame{
        .cur_music_pos = -1,
        .milliseconds_since_last_frame = 0,
        .x = 256,
        .y = -500,
        .key_flags = 0,
    });
    this->live_replay.push_back(LegacyReplay::Frame{
        .cur_music_pos = -1,
        .milliseconds_since_last_frame = -1,
        .x = 256,
        .y = -500,
        .key_flags = 0,
    });

    this->last_event_time = Timing::getTimeReal();
    this->last_event_ms = 0;
    this->current_keys = 0;
    this->last_keys = 0;
    this->current_frame_idx = 0;
    this->iCurMusicPos = 0;
    this->iCurMusicPosWithOffsets = 0;

    this->fHealth = 1.0;
    this->fHealth2 = 1.0f;
    this->bFailed = false;
    this->fFailAnim = 1.0f;
    this->bTempSeekNF = false;
    anim::deleteExistingAnimation(&this->fFailAnim);

    osu->getScore()->reset();
    ui->getHUD()->resetScoreboard();

    this->holding_slider = false;
    this->bIsFirstMissSound = true;
}

void BeatmapInterface::playMissSound() {
    if((this->bIsFirstMissSound && osu->getScore()->getCombo() > 0) ||
       osu->getScore()->getCombo() > cv::combobreak_sound_combo.getInt()) {
        this->bIsFirstMissSound = false;
        soundEngine->play(this->getSkin()->s_combobreak);
    }
}

void BeatmapInterface::draw() {
    if(!this->canDraw()) return;

    // draw background
    this->drawBackground();

    // draw loading circle
    if(this->isLoading()) {
        if(this->isBuffering()) {
            f32 leeway = std::clamp<i32>(this->last_frame_ms - this->iCurMusicPos, 0, cv::spec_buffer.getInt());
            f32 pct = leeway / (cv::spec_buffer.getFloat()) * 100.f;
            auto loadingMessage = fmt::format("Buffering ... ({:.2f}%)", pct);
            ui->getHUD()->drawLoadingSmall(loadingMessage);

            // draw the rest of the playfield while buffering/paused
        } else if(BanchoState::is_playing_a_multi_map() && !this->all_players_loaded) {
            ui->getHUD()->drawLoadingSmall("Waiting for players ...");

            // only start drawing the rest of the playfield if everything has loaded
            return;
        } else {
            ui->getHUD()->drawLoadingSmall("Loading ...");

            // only start drawing the rest of the playfield if everything has loaded
            return;
        }
    }

    if(this->is_watching && cv::simulate_replays.getBool()) {
        // XXX: while this fixes HUD desyncing, it's not perfect replay playback
        this->sim->simulate_to(this->iCurMusicPosWithOffsets);
        *osu->getScore() = this->sim->live_score;
        osu->getScore()->simulating = false;
        osu->getScore()->setCheated();
    }

    // draw playfield border
    if(cv::draw_playfield_border.getBool() && !cv::mod_fps.getBool()) {
        ui->getHUD()->drawPlayfieldBorder(this->vPlayfieldCenter, this->vPlayfieldSize, this->fHitcircleDiameter);
    }

    // draw hiterrorbar
    if(!cv::mod_fposu.getBool()) ui->getHUD()->drawHitErrorBar(this);

    // draw first person crosshair
    if(cv::mod_fps.getBool()) {
        const int length = 15;
        vec2 center = this->osuCoords2Pixels(vec2(GameRules::OSU_COORD_WIDTH / 2, GameRules::OSU_COORD_HEIGHT / 2));
        g->setColor(0xff777777);
        g->drawLine(center.x, (int)(center.y - length), center.x, (int)(center.y + length + 1));
        g->drawLine((int)(center.x - length), center.y, (int)(center.x + length + 1), center.y);
    }

    // draw followpoints
    if(cv::draw_followpoints.getBool() && !cv::mod_mafham.getBool()) this->drawFollowPoints();

    // draw all hitobjects in reverse
    if(cv::draw_hitobjects.getBool()) this->drawHitObjects();

    // draw smoke
    if(cv::draw_smoke.getBool()) this->drawSmoke();

    // draw flashlight overlay
    {
        const bool flashlight_enabled = cv::mod_flashlight.getBool();
        const bool actual_flashlight_enabled = cv::mod_actual_flashlight.getBool();
        if(flashlight_enabled || actual_flashlight_enabled)
            this->drawFlashlight(flashlight_enabled ? FLType::NORMAL_FL : FLType::ACTUAL_FL);
    }

    // draw continue overlay (moved from HUD to draw properly in FPoSu)
    if(this->isContinueScheduled() && cv::draw_continue.getBool()) this->drawContinue();

    // draw spectator pause message
    if(this->spectate_pause) {
        auto info = BANCHO::User::get_user_info(BanchoState::spectated_player_id);
        auto pause_msg = fmt::format("{} has paused", info->name.c_str());
        ui->getHUD()->drawLoadingSmall(pause_msg);
    }

    // debug stuff
    if(cv::debug_hiterrorbar_misaims.getBool()) {
        for(auto &misaimObject : this->misaimObjects) {
            g->setColor(0xbb00ff00);
            vec2 pos = this->osuCoords2Pixels(misaimObject->getRawPosAt(0));
            g->fillRect(pos.x - 50, pos.y - 50, 100, 100);
        }
    }

    // this is barely even useful for debugging in its current state, don't use it
    if(cv::debug_draw_gameplay_clicks.getBool()) {
        for(auto &click : this->all_clicks) {
            g->setColor(0xbb0000ff);
            g->fillRect(click.pos.x - 2, click.pos.y - 2, 4, 4);
        }
    }
}

void BeatmapInterface::drawFlashlight(FLType type) {
    // Convert screen mouse -> osu mouse pos
    const vec2 cursorPos = this->getCursorPos();
    const vec2 mouse_position = (cursorPos - GameRules::getPlayfieldOffset())  //
                                / GameRules::getPlayfieldScaleFactor();

    // Update flashlight position
    const double follow_delay = cv::flashlight_follow_delay.getFloat();
    const double frame_time = std::min(engine->getFrameTime(), follow_delay);
    float t = frame_time / follow_delay;
    t = t * (2.f - t);
    this->flashlight_position += t * (mouse_position - this->flashlight_position);
    const vec2 flashlightPos =
        this->flashlight_position * GameRules::getPlayfieldScaleFactor() + GameRules::getPlayfieldOffset();

    const float base_fl_radius = cv::flashlight_radius.getFloat() * GameRules::getPlayfieldScaleFactor();
    float anti_fl_radius = base_fl_radius * 0.625f;
    float fl_radius = base_fl_radius;

    if(osu->getScore()->getCombo() >= 200 || cv::flashlight_always_hard.getBool()) {
        anti_fl_radius = base_fl_radius;
        fl_radius *= 0.625f;
    } else if(osu->getScore()->getCombo() >= 100) {
        anti_fl_radius = base_fl_radius * 0.8125f;
        fl_radius *= 0.8125f;
    }

    if(type == FLType::NORMAL_FL) {
        // Lazy-load shader
        if(!this->flashlight_shader) {
            this->flashlight_shader = resourceManager->createShaderAuto("flashlight");
        }

        // Dim screen when holding a slider
        float opacity = 1.f;
        if(this->holding_slider && !cv::avoid_flashes.getBool()) {
            opacity = 0.2f;
        }

        this->flashlight_shader->enable();
        this->flashlight_shader->setUniform1f("max_opacity", opacity);
        this->flashlight_shader->setUniform1f("flashlight_radius", fl_radius);

        if(!env->usingGL()) {  // don't flip Y position for DX11
            this->flashlight_shader->setUniform2f("flashlight_center", flashlightPos.x, flashlightPos.y);
        } else {
            this->flashlight_shader->setUniform2f("flashlight_center", flashlightPos.x,
                                                  osu->getVirtScreenSize().y - flashlightPos.y);
        }

        g->setColor(argb(255, 0, 0, 0));
        g->fillRect(0, 0, osu->getVirtScreenWidth(), osu->getVirtScreenHeight());

        this->flashlight_shader->disable();
    }

    if(type == FLType::ACTUAL_FL) {
        // Lazy-load shader
        if(!this->actual_flashlight_shader) {
            this->actual_flashlight_shader = resourceManager->createShaderAuto("actual_flashlight");
        }

        // Brighten screen when holding a slider
        float opacity = 1.f;
        if(this->holding_slider && !cv::avoid_flashes.getBool()) {
            opacity = 0.8f;
        }

        this->actual_flashlight_shader->enable();
        this->actual_flashlight_shader->setUniform1f("max_opacity", opacity);
        this->actual_flashlight_shader->setUniform1f("flashlight_radius", anti_fl_radius);

        if(!env->usingGL()) {  // don't flip Y position for DX11
            this->actual_flashlight_shader->setUniform2f("flashlight_center", flashlightPos.x, flashlightPos.y);
        } else {
            this->actual_flashlight_shader->setUniform2f("flashlight_center", flashlightPos.x,
                                                         osu->getVirtScreenSize().y - flashlightPos.y);
        }

        g->setColor(argb(255, 0, 0, 0));
        g->fillRect(0, 0, osu->getVirtScreenWidth(), osu->getVirtScreenHeight());

        this->actual_flashlight_shader->disable();
    }
}

void BeatmapInterface::drawSmoke() {
    const BasicSkinImage &smoke_img = this->getSkin()->i_cursor_smoke;
    if(smoke_img == MISSING_TEXTURE) return;

    // We're not using this->iCurMusicPos, because we want the user to be able
    // to draw while the music is loading / before the map starts.
    const u64 current_time = Timing::getTicksMS();

    // Add new smoke particles if unpaused & smoke key pressed
    if(!this->bIsPaused && (this->current_keys & GameplayKeys::Smoke)) {
        SMOKETRAIL sm;
        sm.pos = this->pixels2OsuCoords(this->getCursorPos());
        sm.time = current_time;

        while(this->smoke_trail.size() > cv::smoke_trail_max_size.getInt()) {
            this->smoke_trail.erase(this->smoke_trail.begin());
        }

        // Only add smoke particle if 5ms have passed since we added the last one
        // XXX: This is not how stable does it, at all. Instead of *only* relying on time,
        //      when you move the cursor, stable fills the path with smoke particles
        //      so that there is no 'gap' in between particles.
        //      (similar to HUD::addCursorTrailPosition...)
        //      Also our smoke_trail_spacing is too low, stable probably has it over 15ms.
        const i64 last_trail_tms = !this->smoke_trail.empty() ? this->smoke_trail.back().time : 0;
        if(sm.time - last_trail_tms > cv::smoke_trail_spacing.getInt()) {
            this->smoke_trail.push_back(sm);
        }
    }

    const f32 scale = (ui->getHUD()->getCursorScaleFactor() / smoke_img.scale())  //
                      * cv::cursor_scale.getFloat()                               //
                      * cv::smoke_scale.getFloat();

    const u64 time_visible = cv::smoke_trail_duration.getFloat() * 1000.f;
    const u64 time_fully_visible = cv::smoke_trail_opaque_duration.getFloat() * 1000.f;
    const u64 fade_time = time_visible - time_fully_visible;

    for(const auto &sm : this->smoke_trail) {
        const u64 active_for = current_time - sm.time;
        if(active_for >= time_visible) continue;  // avoids division by 0 when (time_visible == time_fully_visible)

        // Start fading out when time_fully_visible has passed
        const f32 alpha = (f32)std::min(fade_time, time_fully_visible - active_for) / (f32)fade_time;
        if(alpha <= 0.f) continue;

        g->setColor(argb(alpha, 1.f, 1.f, 1.f));
        g->pushTransform();
        {
            const auto pos = this->osuCoords2Pixels(sm.pos);
            g->scale(scale, scale);
            g->translate(pos.x, pos.y);
            g->drawImage(smoke_img);
        }
        g->popTransform();
    }

    // trail cleanup
    while(!this->smoke_trail.empty() && std::cmp_greater(current_time, this->smoke_trail[0].time + time_visible)) {
        this->smoke_trail.erase(this->smoke_trail.begin());
    }
}

void BeatmapInterface::drawContinue() {
    vec2 cursor = this->vContinueCursorPoint;
    const float hitcircleDiameter = this->fHitcircleDiameter;

    const auto &unpause = this->getSkin()->i_unpause;
    const float unpauseScale = Osu::getImageScale(unpause, 80);

    const auto &cursorImage = this->getSkin()->i_cursor_default;
    const float cursorScale =
        Osu::getImageScaleToFitResolution(cursorImage, vec2(hitcircleDiameter, hitcircleDiameter));

    // bleh
    if(cursor.x < cursorImage->getWidth() || cursor.y < cursorImage->getHeight() ||
       cursor.x > osu->getVirtScreenWidth() - cursorImage->getWidth() ||
       cursor.y > osu->getVirtScreenHeight() - cursorImage->getHeight())
        cursor = osu->getVirtScreenSize() / 2.f;

    // base
    g->setColor(argb(255, 255, 153, 51));
    g->pushTransform();
    {
        g->scale(cursorScale, cursorScale);
        g->translate(cursor.x, cursor.y);
        g->drawImage(cursorImage);
    }
    g->popTransform();

    // pulse animation
    const float cursorAnimPulsePercent = std::clamp<float>(fmod(engine->getTime(), 1.35f), 0.0f, 1.0f);
    g->setColor(argb((short)(255.0f * (1.0f - cursorAnimPulsePercent)), 255, 153, 51));
    g->pushTransform();
    {
        g->scale(cursorScale * (1.0f + cursorAnimPulsePercent), cursorScale * (1.0f + cursorAnimPulsePercent));
        g->translate(cursor.x, cursor.y);
        g->drawImage(cursorImage);
    }
    g->popTransform();

    // unpause click message
    g->setColor(0xffffffff);
    g->pushTransform();
    {
        g->scale(unpauseScale, unpauseScale);
        g->translate(cursor.x + 20 + (unpause->getWidth() / 2) * unpauseScale,
                     cursor.y + 20 + (unpause->getHeight() / 2) * unpauseScale);
        g->drawImage(unpause);
    }
    g->popTransform();
}

void BeatmapInterface::drawFollowPoints() {
    const auto &skin = this->getSkin();

    const i32 curPos = this->iCurMusicPosWithOffsets;

    // I absolutely hate this, followpoints can be abused for cheesing high AR reading since they always fade in with a
    // fixed 800 ms custom approach time. Capping it at the current approach rate seems sensible, but unfortunately
    // that's not what osu is doing. It was non-osu-compliant-clamped since this client existed, but let's see how many
    // people notice a change after all this time (26.02.2020)

    // 0.7x means animation lasts only 0.7 of it's time
    const f64 animationMultiplier = this->getSpeedAdjustedAnimationSpeed();
    const i32 followPointApproachTime =
        animationMultiplier *
        (cv::followpoints_clamp.getBool()
             ? std::min((i32)this->fCachedApproachTimeForUpdate, (i32)cv::followpoints_approachtime.getFloat())
             : (i32)cv::followpoints_approachtime.getFloat());
    const bool followPointsConnectCombos = cv::followpoints_connect_combos.getBool();
    const bool followPointsConnectSpinners = cv::followpoints_connect_spinners.getBool();
    const f32 followPointSeparationMultiplier = std::max(cv::followpoints_separation_multiplier.getFloat(), 0.1f);
    const f32 followPointPrevFadeTime = animationMultiplier * cv::followpoints_prevfadetime.getFloat();
    const f32 followPointScaleMultiplier = cv::followpoints_scale_multiplier.getFloat();

    // include previous object in followpoints
    int lastObjectIndex = -1;

    for(int index = this->iPreviousFollowPointObjectIndex; index < this->hitobjects.size(); index++) {
        lastObjectIndex = index - 1;

        // ignore future spinners
        auto *spinnerPointer = this->hitobjects[index] && this->hitobjects[index]->type == HitObjectType::SPINNER
                                   ? static_cast<Spinner *>(this->hitobjects[index].get())
                                   : nullptr;
        if(spinnerPointer != nullptr && !followPointsConnectSpinners)  // if this is a spinner
        {
            lastObjectIndex = -1;
            continue;
        }

        const bool isCurrentHitObjectNewCombo =
            (lastObjectIndex >= 0 ? this->hitobjects[lastObjectIndex]->is_end_of_combo : false);
        const bool isCurrentHitObjectSpinner =
            (lastObjectIndex >= 0 && followPointsConnectSpinners
                 ? this->hitobjects[lastObjectIndex] &&
                       this->hitobjects[lastObjectIndex]->type == HitObjectType::SPINNER
                 : false);
        if(lastObjectIndex >= 0 && (!isCurrentHitObjectNewCombo || followPointsConnectCombos ||
                                    (isCurrentHitObjectSpinner && followPointsConnectSpinners))) {
            // ignore previous spinners
            spinnerPointer =
                this->hitobjects[lastObjectIndex] && this->hitobjects[lastObjectIndex]->type == HitObjectType::SPINNER
                    ? static_cast<Spinner *>(this->hitobjects[lastObjectIndex].get())
                    : nullptr;
            if(spinnerPointer != nullptr && !followPointsConnectSpinners)  // if this is a spinner
            {
                lastObjectIndex = -1;
                continue;
            }

            // get time & pos of the last and current object
            const i32 lastObjectEndTime =
                this->hitobjects[lastObjectIndex]->click_time + this->hitobjects[lastObjectIndex]->duration + 1;
            const i32 objectStartTime = this->hitobjects[index]->click_time;
            const i32 timeDiff = objectStartTime - lastObjectEndTime;

            const vec2 startPoint =
                this->osuCoords2Pixels(this->hitobjects[lastObjectIndex]->getRawPosAt(lastObjectEndTime));
            const vec2 endPoint = this->osuCoords2Pixels(this->hitobjects[index]->getRawPosAt(objectStartTime));

            const f32 xDiff = endPoint.x - startPoint.x;
            const f32 yDiff = endPoint.y - startPoint.y;
            const vec2 diff = endPoint - startPoint;
            const f32 dist =
                std::round(vec::length(diff) * 100.0f) / 100.0f;  // rounded to avoid flicker with playfield rotations

            // draw all points between the two objects
            const int followPointSeparation = Osu::getUIScale(32) * followPointSeparationMultiplier;
            for(int j = (int)(followPointSeparation * 1.5f); j < (dist - followPointSeparation);
                j += followPointSeparation) {
                const f32 animRatio = ((f32)j / dist);

                const vec2 animPosStart = startPoint + (animRatio - 0.1f) * diff;
                const vec2 finalPos = startPoint + animRatio * diff;

                const i32 fadeInTime = (i32)(lastObjectEndTime + animRatio * timeDiff) - followPointApproachTime;
                const i32 fadeOutTime = (i32)(lastObjectEndTime + animRatio * timeDiff);

                // draw
                f32 alpha = 1.0f;
                f32 followAnimPercent =
                    std::clamp<f32>((f32)(curPos - fadeInTime) / (f32)followPointPrevFadeTime, 0.0f, 1.0f);
                followAnimPercent = -followAnimPercent * (followAnimPercent - 2.0f);  // quad out

                // NOTE: only internal osu default skin uses scale + move transforms here, it is impossible to achieve
                // this effect with user skins
                const f32 scale = cv::followpoints_anim.getBool() ? 1.5f - 0.5f * followAnimPercent : 1.0f;
                const vec2 followPos = cv::followpoints_anim.getBool()
                                           ? animPosStart + (finalPos - animPosStart) * followAnimPercent
                                           : finalPos;

                // bullshit performance optimization: only draw followpoints if within screen bounds (plus a bit of a
                // margin) there is only one beatmap where this matters currently: https://osu.ppy.sh/b/1145513
                if(followPos.x < -osu->getVirtScreenWidth() || followPos.x > osu->getVirtScreenWidth() * 2 ||
                   followPos.y < -osu->getVirtScreenHeight() || followPos.y > osu->getVirtScreenHeight() * 2)
                    continue;

                // calculate trail alpha
                if(curPos >= fadeInTime && curPos < fadeOutTime) {
                    // future trail
                    const f32 delta = curPos - fadeInTime;
                    alpha = (f32)delta / (f32)followPointApproachTime;
                } else if(curPos >= fadeOutTime && curPos < (fadeOutTime + (i32)followPointPrevFadeTime)) {
                    // previous trail
                    const i32 delta = curPos - fadeOutTime;
                    alpha = 1.0f - (f32)delta / (f32)(followPointPrevFadeTime);
                } else
                    alpha = 0.0f;

                // draw it
                g->setColor(Color(0xffffffff).setA(alpha));

                g->pushTransform();
                {
                    g->rotate(glm::degrees(std::atan2(yDiff, xDiff)));

                    skin->i_followpoint->setAnimationTimeOffset(fadeInTime);

                    // NOTE: getSizeBaseRaw() depends on the current animation time being set correctly beforehand!
                    // (otherwise you get incorrect scales, e.g. for animated elements with inconsistent @2x mixed in)
                    // the followpoints are scaled by one eighth of the hitcirclediameter (not the raw diameter, but the
                    // scaled diameter)
                    const f32 followPointImageScale =
                        ((this->fHitcircleDiameter / 8.0f) / skin->i_followpoint->getSizeBaseRaw().x) *
                        followPointScaleMultiplier;

                    skin->i_followpoint->drawRaw(followPos, followPointImageScale * scale);
                }
                g->popTransform();
            }
        }

        // store current index as previous index
        lastObjectIndex = index;

        // iterate up until the "nextest" element
        if(this->hitobjects[index]->click_time >= curPos + followPointApproachTime) break;
    }
}

void BeatmapInterface::drawHitObjects() {
    const i32 curPos = this->iCurMusicPosWithOffsets;
    const i32 pvs = this->getPVS();
    const bool usePVS = cv::pvs.getBool();

    if(!cv::mod_mafham.getBool()) {
        this->nonSpinnerObjectsToDraw.clear();
        i32 mostDistantEndTimeDrawn = 0;

        for(sSz i = static_cast<sSz>(this->hitobjectsSortedByEndTime.size()) - 1; i >= 0; i--) {
            // PVS optimization (reversed)
            HitObject *obj = this->hitobjectsSortedByEndTime[i];
            if(usePVS) {
                const i32 endTime = obj->getEndTime();

                if(obj->isFinished() && (curPos - pvs > endTime))  // past objects
                    break;
                if(obj->click_time > curPos + pvs)  // future objects
                    continue;

                if(endTime > mostDistantEndTimeDrawn) mostDistantEndTimeDrawn = endTime;
            }

            // in order to avoid covering circles/sliders with spinner skin elements, draw spinners first
            // and overlay circles/sliders on top
            // this logic could be embedded in the sort order itself but i'm lazy to check if that would mess anything else up
            if(obj->type == HitObjectType::SPINNER) {
                obj->draw();
            } else {
                this->nonSpinnerObjectsToDraw.push_back(obj);
            }
        }

        // draw non-spinners after
        for(auto *obj : this->nonSpinnerObjectsToDraw) {
            obj->draw();
        }

        // this avoids PVS culling objects which are overlapped before the end of other objects, like circles appearing before
        // sliders are finished causing the initial slider approach circle to not be drawn
        // e.g.: the first object of https://osu.ppy.sh/beatmapsets/613791#osu/1294898
        const i32 mostDistantFutureObjectPVS = std::max(mostDistantEndTimeDrawn, curPos + pvs);

        // this doesn't need the spinner front-to-back thing because spinners have no draw2
        for(auto *obj : this->hitobjectsSortedByEndTime) {
            // NOTE: to fix mayday simultaneous sliders with increasing endtime getting culled here, would have to
            // switch from m_hitobjectsSortedByEndTime to m_hitobjects PVS optimization
            if(usePVS) {
                if(obj->isFinished() && (curPos - pvs > obj->getEndTime()))  // past objects
                    continue;

                if((obj->click_time > mostDistantFutureObjectPVS))  // future objects
                    break;
            }

            obj->draw2();
        }

        this->nonSpinnerObjectsToDraw.clear();
    } else {
        const int mafhamRenderLiveSize = cv::mod_mafham_render_livesize.getInt();

        if(this->mafhamActiveRenderTarget == nullptr) this->mafhamActiveRenderTarget = osu->getFrameBuffer();

        if(this->mafhamFinishedRenderTarget == nullptr) this->mafhamFinishedRenderTarget = osu->getFrameBuffer2();

        // if we have a chunk to render into the scene buffer
        const bool shouldDrawBuffer =
            (this->hitobjectsSortedByEndTime.size() - this->iCurrentHitObjectIndex) > mafhamRenderLiveSize;
        bool shouldRenderChunk =
            this->iMafhamHitObjectRenderIndex < this->hitobjectsSortedByEndTime.size() && shouldDrawBuffer;
        if(shouldRenderChunk) {
            this->bInMafhamRenderChunk = true;

            this->mafhamActiveRenderTarget->setClearColorOnDraw(this->iMafhamHitObjectRenderIndex == 0);
            this->mafhamActiveRenderTarget->setClearDepthOnDraw(this->iMafhamHitObjectRenderIndex == 0);

            this->mafhamActiveRenderTarget->enable();
            {
                g->setBlendMode(DrawBlendMode::PREMUL_ALPHA);
                {
                    int chunkCounter = 0;
                    for(int i = this->hitobjectsSortedByEndTime.size() - 1 - this->iMafhamHitObjectRenderIndex; i >= 0;
                        i--, this->iMafhamHitObjectRenderIndex++) {
                        chunkCounter++;
                        if(chunkCounter > cv::mod_mafham_render_chunksize.getInt())
                            break;  // continue chunk render in next frame

                        if(i <= this->iCurrentHitObjectIndex + mafhamRenderLiveSize)  // skip live objects
                        {
                            this->iMafhamHitObjectRenderIndex =
                                this->hitobjectsSortedByEndTime.size();  // stop chunk render
                            break;
                        }

                        // PVS optimization (reversed)
                        if(usePVS) {
                            if(this->hitobjectsSortedByEndTime[i]->isFinished() &&
                               (curPos - pvs > this->hitobjectsSortedByEndTime[i]->click_time +
                                                   this->hitobjectsSortedByEndTime[i]->duration))  // past objects
                            {
                                this->iMafhamHitObjectRenderIndex =
                                    this->hitobjectsSortedByEndTime.size();  // stop chunk render
                                break;
                            }
                            if(this->hitobjectsSortedByEndTime[i]->click_time > curPos + pvs)  // future objects
                                continue;
                        }

                        this->hitobjectsSortedByEndTime[i]->draw();

                        this->iMafhamActiveRenderHitObjectIndex = i;
                    }
                }
                g->setBlendMode(DrawBlendMode::ALPHA);
            }
            this->mafhamActiveRenderTarget->disable();

            this->bInMafhamRenderChunk = false;
        }
        shouldRenderChunk =
            this->iMafhamHitObjectRenderIndex < this->hitobjectsSortedByEndTime.size() && shouldDrawBuffer;
        if(!shouldRenderChunk && this->bMafhamRenderScheduled) {
            // finished, we can now swap the active framebuffer with the one we just finished
            this->bMafhamRenderScheduled = false;

            RenderTarget *temp = this->mafhamFinishedRenderTarget;
            this->mafhamFinishedRenderTarget = this->mafhamActiveRenderTarget;
            this->mafhamActiveRenderTarget = temp;

            this->iMafhamFinishedRenderHitObjectIndex = this->iMafhamActiveRenderHitObjectIndex;
            this->iMafhamActiveRenderHitObjectIndex = this->hitobjectsSortedByEndTime.size();  // reset
        }

        // draw scene buffer
        if(shouldDrawBuffer) {
            g->setBlendMode(DrawBlendMode::PREMUL_COLOR);
            {
                this->mafhamFinishedRenderTarget->draw(0, 0);
            }
            g->setBlendMode(DrawBlendMode::ALPHA);
        }

        // draw followpoints
        if(cv::draw_followpoints.getBool()) this->drawFollowPoints();

        // draw live hitobjects (also, code duplication yay)
        {
            for(int i = this->hitobjectsSortedByEndTime.size() - 1; i >= 0; i--) {
                // PVS optimization (reversed)
                if(usePVS) {
                    if(this->hitobjectsSortedByEndTime[i]->isFinished() &&
                       (curPos - pvs > this->hitobjectsSortedByEndTime[i]->click_time +
                                           this->hitobjectsSortedByEndTime[i]->duration))  // past objects
                        break;
                    if(this->hitobjectsSortedByEndTime[i]->click_time > curPos + pvs)  // future objects
                        continue;
                }

                if(i > this->iCurrentHitObjectIndex + mafhamRenderLiveSize ||
                   (i > this->iMafhamFinishedRenderHitObjectIndex - 1 && shouldDrawBuffer))  // skip non-live objects
                    continue;

                this->hitobjectsSortedByEndTime[i]->draw();
            }

            for(int i = 0; i < this->hitobjectsSortedByEndTime.size(); i++) {
                // PVS optimization
                if(usePVS) {
                    if(this->hitobjectsSortedByEndTime[i]->isFinished() &&
                       (curPos - pvs > this->hitobjectsSortedByEndTime[i]->click_time +
                                           this->hitobjectsSortedByEndTime[i]->duration))  // past objects
                        continue;
                    if(this->hitobjectsSortedByEndTime[i]->click_time > curPos + pvs)  // future objects
                        break;
                }

                if(i >= this->iCurrentHitObjectIndex + mafhamRenderLiveSize ||
                   (i >= this->iMafhamFinishedRenderHitObjectIndex - 1 && shouldDrawBuffer))  // skip non-live objects
                    break;

                this->hitobjectsSortedByEndTime[i]->draw2();
            }
        }
    }
}

void BeatmapInterface::update() {
    if(!this->bIsPlaying && !this->bIsPaused && !this->bContinueScheduled) return;

    // some things need to be updated before loading has finished, so control flow is a bit weird here.

    // live update hitobject and playfield metrics
    this->updateHitobjectMetrics();
    this->updatePlayfieldMetrics();

    // wobble mod
    if(cv::mod_wobble.getBool()) {
        const f32 speedMultiplierCompensation = 1.0f / this->getSpeedMultiplier();
        this->fPlayfieldRotation = (this->iCurMusicPos / 1000.0f) * 30.0f * speedMultiplierCompensation *
                                   cv::mod_wobble_rotation_speed.getFloat();
        this->fPlayfieldRotation = std::fmod(this->fPlayfieldRotation, 360.0f);
    } else {
        this->fPlayfieldRotation = 0.0f;
    }

    // do hitobject updates among other things
    // yes, this needs to happen after updating metrics and playfield rotation
    this->update2();

    // handle preloading (only for distributed slider vertexbuffer generation atm)
    const bool was_preloading = this->bIsPreLoading;
    if(this->bIsPreLoading) {
        if(cv::debug_osu.getBool() && this->iPreLoadingIndex == 0)
            debugLog("Beatmap: Preloading slider vertexbuffers ...");

        f64 startTime = Timing::getTimeReal();
        f64 delta = 0.0;

        // hardcoded deadline of 10 ms, will temporarily bring us down to 45fps on average (better than freezing)
        while(delta < 0.010 && this->bIsPreLoading) {
            if(this->iPreLoadingIndex >= this->hitobjects.size()) {
                this->bIsPreLoading = false;
                debugLog("Beatmap: Preloading done.");
                break;
            } else {
                auto *ho = this->hitobjects[this->iPreLoadingIndex].get();
                auto *sliderPointer = ho && ho->type == HitObjectType::SLIDER ? static_cast<Slider *>(ho) : nullptr;
                if(sliderPointer != nullptr) sliderPointer->rebuildVertexBuffer();
            }

            this->iPreLoadingIndex++;
            delta = Timing::getTimeReal() - startTime;
        }
    }

    // notify server once we've finished loading
    if(!this->player_loaded && BanchoState::is_playing_a_multi_map() && !this->isActuallyLoading()) {
        this->player_loaded = true;

        Packet packet;
        packet.id = OUTP_MATCH_LOAD_COMPLETE;
        BANCHO::Net::send_packet(packet);
    }

    if(this->isLoading() || was_preloading) {
        // Only continue if we have loaded everything.
        // We also return if we just finished preloading, since we need an extra update loop
        // to correctly initialize the beatmap.
        return;
    }

    // @PPV3: also calculate live ppv3
    if(ui->getHUD()->getScoringMetric() == WinCondition::PP || cv::draw_statistics_pp.getBool() ||
       cv::draw_statistics_livestars.getBool()) {
        this->ppv2_calc.update(*osu->getScore());
    }

    // update auto (after having updated the hitobjects)
    if(osu->getModAuto() || osu->getModAutopilot()) this->updateAutoCursorPos();

    // spinner detection (used by osu!stable drain, and by HUD for not drawing the hiterrorbar)
    if(this->currentHitObject != nullptr) {
        this->bIsSpinnerActive = this->currentHitObject->type == HitObjectType::SPINNER;
        this->bIsSpinnerActive &= this->iCurMusicPosWithOffsets > this->currentHitObject->click_time;
        this->bIsSpinnerActive &=
            this->iCurMusicPosWithOffsets < this->currentHitObject->click_time + this->currentHitObject->duration;
    }

    // scene buffering logic
    if(cv::mod_mafham.getBool()) {
        if(!this->bMafhamRenderScheduled &&
           this->iCurrentHitObjectIndex !=
               this->iMafhamPrevHitObjectIndex)  // if we are not already rendering and the index changed
        {
            this->iMafhamPrevHitObjectIndex = this->iCurrentHitObjectIndex;
            this->iMafhamHitObjectRenderIndex = 0;
            this->bMafhamRenderScheduled = true;
        }
    }

    // singletap/fullalternate mod lenience
    if(this->bInBreak || this->bIsInSkippableSection || this->bIsSpinnerActive || this->iCurrentHitObjectIndex < 1) {
        this->iAllowAnyNextKeyUntilHitObjectIndex = this->iCurrentHitObjectIndex + 1;
    }

    if(this->last_keys != this->current_keys || (this->last_event_time + 0.01666666666 <= Timing::getTimeReal())) {
        this->write_frame();
    }
}

i32 BeatmapInterface::getInterpedMusicPos() const {
    const auto currentTime = Timing::getTimeReal<f64>();

    const int interpCV = cv::interpolate_music_pos.getInt();
    const bool useMcOsuInterp = interpCV == 2;
    const bool useLazerInterp = !useMcOsuInterp && interpCV == 3;

    // lazy switch on convar change
    if(useMcOsuInterp && (!this->musicInterp || this->musicInterp->getType() != 2)) {
        this->musicInterp = std::make_unique<McOsuInterpolator>();
    } else if(useLazerInterp && (!this->musicInterp || this->musicInterp->getType() != 3)) {
        this->musicInterp = std::make_unique<TachyonInterpolator>();
    }

    i64 realMusicPos = -1000;
    i64 returnPos = -1000;
    if(this->isActuallyLoading()) {
        // fake negative start
        if(useMcOsuInterp || useLazerInterp) {
            this->musicInterp->update(0.0, currentTime, 0.0, false, 0.0, false);
        }
        // otherwise don't do anything (default interpolator is embedded in stream playback position)
    } else {
        if(useMcOsuInterp || useLazerInterp) {
            returnPos = (i32)this->musicInterp->update(
                (f64)(realMusicPos = (i64)this->music->getPositionMS()), currentTime, this->music->getSpeed(), false,
                this->music->getLengthMS(), this->music->isPlaying() && !this->bWasSeekFrame);
        } else {
            returnPos = (i32)(realMusicPos = (i64)this->music->getPositionMS());
        }

        if(this->music->getSpeed() < 1.0f && cv::compensate_music_speed.getBool() &&
           cv::snd_speed_compensate_pitch.getBool())
            returnPos += (i64)(((1.0f - this->music->getSpeed()) / 0.75f) * 5);  // osu (new)
    }

    if(cv::debug_snd.getInt() > 1) {
        const std::string logString = fmt::format(
            R"(==== MUSIC POSITION DEBUG ====
real time: {}
interpolator type: {}
music->getPositionMS(): {}
iCurMusicPos: {}
==== END MUSIC POSITION DEBUG ====)",
            currentTime, cv::interpolate_music_pos.getInt(), realMusicPos, returnPos);

        logRaw(logString);
    }

    return (i32)returnPos;
}

void BeatmapInterface::update2() {
    if(this->bContinueScheduled) {
        // If we paused while m_bIsWaiting (green progressbar), then we have to let the 'if (this->bIsWaiting)' block
        // handle the sound play() call
        bool isEarlyNoteContinue = (!this->bIsPaused && this->bIsWaiting);
        if(this->bClickedContinue || isEarlyNoteContinue) {
            this->bClickedContinue = false;
            this->bContinueScheduled = false;
            this->bIsPaused = false;

            // annoying place to put this, but update text input state after pressing continue
            // (for allowing consolebox optionsmenu chat etc. text input in the continue screen)
            osu->updateWindowsKeyDisable();

            if(!isEarlyNoteContinue) {
                soundEngine->play(this->music);
            }

            this->bIsPlaying = true;  // usually this should be checked with the result of the above play() call, but
                                      // since we are continuing we can assume that everything works

            // for nightmare mod, to avoid a miss because of the continue click
            this->clicks.clear();
        }
    }

    // handle restarts
    if(this->bIsRestartScheduled) {
        this->bIsRestartScheduled = false;
        this->actualRestart();
        return;
    }

    const bool isIdlePaused = this->isActuallyPausedAndNotSpectating();

    // update current music position (this variable does not include any offsets!)
    this->iCurMusicPos = this->getInterpedMusicPos();
    this->iContinueMusicPos = this->iCurMusicPos < 0 ? 0 : this->iCurMusicPos;

    const bool wasSeekFrame = this->bWasSeekFrame;
    this->bWasSeekFrame = false;

    // handle timewarp
    if(cv::mod_timewarp.getBool()) {
        if(likely(!this->hitobjects.empty()) && this->iCurMusicPos > this->hitobjects[0]->click_time) {
            const f32 percentFinished = ((f64)(this->iCurMusicPos - this->hitobjects[0]->click_time) /
                                         (f64)(this->hitobjects.back()->click_time + this->hitobjects.back()->duration -
                                               this->hitobjects[0]->click_time));
            f32 warp_multiplier = std::max(cv::mod_timewarp_multiplier.getFloat(), 1.f);
            const f32 speed =
                this->getSpeedMultiplier() + percentFinished * this->getSpeedMultiplier() * (warp_multiplier - 1.0f);
            this->setMusicSpeed(speed);
        }
    }

    // hoist this call out (it's constant throughout an update iteration)
    const f64 current_frametime = engine->getFrameTime();

    // HACKHACK: clean this mess up
    // waiting to start (file loading, retry)
    // NOTE: this is dependent on being here AFTER m_iCurMusicPos has been set above, because it modifies it to fake a
    // negative start (else everything would just freeze for the waiting period)
    if(this->bIsWaiting) {
        if(this->isLoading()) {
            this->fWaitTime = Timing::getTimeReal<f32>();

            // if the first hitobject starts immediately, add artificial wait time before starting the music
            if(!this->bIsRestartScheduledQuick && likely(!this->hitobjects.empty())) {
                if(this->hitobjects[0]->click_time < cv::early_note_time.getInt()) {
                    this->fWaitTime = Timing::getTimeReal<f32>() + cv::early_note_time.getFloat() / 1000.0f;
                }
            }
        } else {
            if(Timing::getTimeReal<f32>() > this->fWaitTime) {
                if(!this->bIsPaused) {
                    this->bIsWaiting = false;
                    this->bIsPlaying = true;

                    i64 start_ms = 0;

                    // if we are quick restarting, jump just before the first hitobject (even if there is a long waiting
                    // period at the beginning with nothing etc.)
                    if(this->bIsRestartScheduledQuick) {
                        if(likely(!this->hitobjects.empty())) {
                            i64 retry_time = std::max(0, cv::quick_retry_time.getInt());
                            start_ms = this->hitobjects[0]->click_time - retry_time;
                            if(start_ms < 0) start_ms = 0;
                        }
                        this->bIsRestartScheduledQuick = false;
                    }

                    soundEngine->play(this->music);
                    this->music->setLoop(false);
                    this->music->setPositionMS(start_ms);
                    this->bWasSeekFrame = true;
                    this->music->setBaseVolume(this->getIdealVolume());

                    // if there are calculations in there that need the hitobjects to be loaded, also applies
                    // speed/pitch
                    this->onModUpdate(false, false);
                }
            } else {
                this->iCurMusicPos =
                    (Timing::getTimeReal<f32>() - this->fWaitTime) * 1000.0f * this->getSpeedMultiplier();
            }
        }

        // ugh. force update all hitobjects while waiting (necessary because of pvs optimization)
        i32 curPos = this->iCurMusicPos +
                     (i32)((cv::universal_offset.getFloat() + cv::universal_offset_hardcoded_blamepeppy.getFloat()) *
                           this->getSpeedMultiplier()) +
                     cv::universal_offset_norate.getInt() - this->music->getRateBasedStreamDelayMS() -
                     this->beatmap->getLocalOffset() - this->beatmap->getOnlineOffset() -
                     (this->beatmap->getVersion() < 5 ? cv::old_beatmap_offset.getInt() : 0);
        if(curPos > -1)  // otherwise auto would already click elements that start at exactly 0 (while the map has not
                         // even started)
            curPos = -1;

        for(auto &hitobject : this->hitobjects) {
            hitobject->update(curPos, current_frametime);
        }
    }

    if(BanchoState::spectating) {
        if(this->spectate_pause && !this->bFailed && this->music->isPlaying()) {
            soundEngine->pause(this->music);
            this->bIsPlaying = false;
            this->bIsPaused = true;
        }
        if(!this->spectate_pause && this->bIsPaused) {
            soundEngine->play(this->music);
            this->bIsPlaying = true;
            this->bIsPaused = false;
        }
    }

    // only continue updating hitobjects etc. if we have loaded everything
    if(this->isLoading()) return;

    // handle music loading fail
    if(!this->music->isReady()) {
        ui->getNotificationOverlay()->addToast(US_("Couldn't load music file :("), ERROR_TOAST);
        this->stop(true);
        return;
    }

    // detect and handle music end
    if(!this->bIsWaiting && this->music->isReady()) {
        const bool isMusicFinished = this->music->isFinished();

        // trigger virtual audio time after music finishes
        if(!isMusicFinished)
            this->fAfterMusicIsFinishedVirtualAudioTimeStart = -1.0f;
        else if(this->fAfterMusicIsFinishedVirtualAudioTimeStart < 0.0f)
            this->fAfterMusicIsFinishedVirtualAudioTimeStart = Timing::getTimeReal<f32>();

        if(isMusicFinished) {
            // continue with virtual audio time until the last hitobject is done (plus sanity offset given via
            // osu_end_delay_time) because some beatmaps have hitobjects going until >= the exact end of the music ffs
            // NOTE: this overwrites m_iCurMusicPos for the rest of the update loop
            this->iCurMusicPos =
                (i32)this->music->getLengthMS() +
                (i32)((Timing::getTimeReal<f32>() - this->fAfterMusicIsFinishedVirtualAudioTimeStart) * 1000.0f);
        }

        const bool hasAnyHitObjects = (likely(!this->hitobjects.empty()));
        const bool isTimePastLastHitObjectPlusLenience =
            !this->hitobjectsSortedByEndTime.empty() &&
            (this->iCurMusicPos >
             (this->hitobjectsSortedByEndTime.back()->click_time + this->hitobjectsSortedByEndTime.back()->duration +
              (i32)cv::end_delay_time.getInt()));
        if(!hasAnyHitObjects || (cv::end_skip.getBool() && isTimePastLastHitObjectPlusLenience) ||
           (!cv::end_skip.getBool() && isMusicFinished)) {
            if(!this->bFailed) {
                this->stop(false);
                return;
            }
        }
    }

    // update timing (with offsets)
    this->iCurMusicPosWithOffsets = this->iCurMusicPos;
    this->iCurMusicPosWithOffsets +=
        (i32)((cv::universal_offset.getFloat() + cv::universal_offset_hardcoded_blamepeppy.getFloat()) *
              this->getSpeedMultiplier());
    this->iCurMusicPosWithOffsets += cv::universal_offset_norate.getInt();
    this->iCurMusicPosWithOffsets -= this->music->getRateBasedStreamDelayMS();
    this->iCurMusicPosWithOffsets -= this->beatmap->getLocalOffset();
    this->iCurMusicPosWithOffsets -= this->beatmap->getOnlineOffset();
    if(this->beatmap->getVersion() < 5) {
        this->iCurMusicPosWithOffsets -= cv::old_beatmap_offset.getInt();
    }

    // get timestamp from the previous update cycle
    const u64 lastUpdateTime = this->iLastMusicPosUpdateTime;

    // update the update timestamp
    const u64 currentUpdateTime = Timing::getTicksNS();
    this->iLastMusicPosUpdateTime = currentUpdateTime;

    // update current timingpoint
    if(this->iCurMusicPosWithOffsets >= 0) {
        this->cur_timing_info =
            this->beatmap->getTimingInfoForTime(this->iCurMusicPosWithOffsets + cv::timingpoints_offset.getInt());
    }

    // Make sure we're not too far behind the liveplay
    if(BanchoState::spectating) {
        if(this->iCurMusicPos + (2 * cv::spec_buffer.getInt()) < this->last_frame_ms) {
            i32 target = this->last_frame_ms - cv::spec_buffer.getInt();
            debugLog("We're {:d}ms behind, seeking to catch up to player...", this->last_frame_ms - this->iCurMusicPos);
            this->seekMS(std::max(0, target));
            return;
        }
    }

    // interpolate clicks that occurred between the last update and now
    // (except if we are paused)
    if(!isIdlePaused && !this->is_watching && !BanchoState::spectating && !this->clicks.empty()) {
        const u64 timeSinceLastUpdate = currentUpdateTime - lastUpdateTime;

        if(timeSinceLastUpdate > 0) {
            for(auto &click : this->clicks) {
                // how long after the last music update did this click occur?
                const u64 clickDeltaSinceLastUpdate = click.timestamp - lastUpdateTime;
                const f64 percent = std::clamp((f64)clickDeltaSinceLastUpdate / (f64)timeSinceLastUpdate, 0.0, 1.0);

                // interpolate between the music position when click was captured and current music position
                // TODO: aim-between-frames
                click.music_pos = static_cast<i32>(
                    std::round(std::lerp((f64)click.music_pos, (f64)this->iCurMusicPosWithOffsets, percent)));
            }
        }
    }

    // don't advance replay frames if we are paused unless this was a seek
    if((!isIdlePaused || wasSeekFrame) && (this->is_watching || BanchoState::spectating) &&
       this->spectated_replay.size() >= 2) {
        LegacyReplay::Frame current_frame = this->spectated_replay[this->current_frame_idx];
        LegacyReplay::Frame next_frame = this->spectated_replay[this->current_frame_idx + 1];

        while(next_frame.cur_music_pos <= this->iCurMusicPosWithOffsets) {
            if(this->current_frame_idx + 2 >= this->spectated_replay.size()) break;

            this->last_keys = this->current_keys;

            this->current_frame_idx++;
            current_frame = this->spectated_replay[this->current_frame_idx];
            next_frame = this->spectated_replay[this->current_frame_idx + 1];

            // There is a big gap in the replay, it is safe to assume it was made from a neomod client
            // and that the player skipped an empty section.
            // We'll "schedule" a skip, which is the same as clicking the skip button, so nothing
            // would get skipped if it isn't actually a skippable section.
            if(next_frame.milliseconds_since_last_frame >= cv::skip_time.getInt()) {
                osu->bSkipScheduled = true;
            }

            this->current_keys = current_frame.key_flags;

            // Replays have both K1 and M1 set when K1 is pressed, fix it now
            const auto &mods = osu->getScore()->mods;
            if(!mods.has(ModFlags::NoKeylock)) {
                if(this->current_keys & LegacyReplay::K1) this->current_keys &= ~LegacyReplay::M1;
                if(this->current_keys & LegacyReplay::K2) this->current_keys &= ~LegacyReplay::M2;
            }

            Click click{
                .timestamp = Timing::getTicksNS(),
                .pos = (vec2{current_frame.x, current_frame.y} * GameRules::getPlayfieldScaleFactor()) +
                       GameRules::getPlayfieldOffset(),
                .music_pos = current_frame.cur_music_pos,
            };

            bool hasAnyHitObjects = (likely(!this->hitobjects.empty()));
            bool is_too_early = hasAnyHitObjects && this->iCurMusicPosWithOffsets < this->hitobjects[0]->click_time;
            bool should_count_keypress = !is_too_early && !this->bInBreak && !this->bIsInSkippableSection;

            // Key presses
            for(auto key : {GameplayKeys::K1, GameplayKeys::K2, GameplayKeys::M1, GameplayKeys::M2}) {
                if(!(this->last_keys & key) && (this->current_keys & key)) {
                    this->lastPressedKey = key;
                    this->clicks.push_back(click);
                    ui->getHUD()->animateInputOverlay(key, true);
                    if(should_count_keypress) osu->getScore()->addKeyCount(key);
                }
            }

            // Key releases
            for(auto key : {GameplayKeys::K1, GameplayKeys::K2, GameplayKeys::M1, GameplayKeys::M2}) {
                if((this->last_keys & key) && !(this->current_keys & key)) {
                    ui->getHUD()->animateInputOverlay(key, false);
                }
            }
        }

        f32 percent = 0.f;
        if(next_frame.milliseconds_since_last_frame > 0) {
            i32 ms_since_last_frame = this->iCurMusicPosWithOffsets - current_frame.cur_music_pos;
            percent = (f32)ms_since_last_frame / (f32)next_frame.milliseconds_since_last_frame;
        }

        this->interpolatedMousePos =
            vec2{std::lerp(current_frame.x, next_frame.x, percent), std::lerp(current_frame.y, next_frame.y, percent)};

        if(cv::playfield_mirror_horizontal.getBool())
            this->interpolatedMousePos.y = GameRules::OSU_COORD_HEIGHT - this->interpolatedMousePos.y;
        if(cv::playfield_mirror_vertical.getBool())
            this->interpolatedMousePos.x = GameRules::OSU_COORD_WIDTH - this->interpolatedMousePos.x;

        if(cv::playfield_rotation.getFloat() != 0.0f) {
            this->interpolatedMousePos.x -= GameRules::OSU_COORD_WIDTH / 2;
            this->interpolatedMousePos.y -= GameRules::OSU_COORD_HEIGHT / 2;
            vec3 coords3 = vec3(this->interpolatedMousePos.x, this->interpolatedMousePos.y, 0);
            Matrix4 rot;
            rot.rotateZ(cv::playfield_rotation.getFloat());
            coords3 = coords3 * rot;
            coords3.x += GameRules::OSU_COORD_WIDTH / 2;
            coords3.y += GameRules::OSU_COORD_HEIGHT / 2;
            this->interpolatedMousePos.x = coords3.x;
            this->interpolatedMousePos.y = coords3.y;
        }

        this->interpolatedMousePos *= GameRules::getPlayfieldScaleFactor();
        this->interpolatedMousePos += GameRules::getPlayfieldOffset();
    }

    // add current clicks to all_clicks for debug
    if(cv::debug_draw_gameplay_clicks.getBool()) {
        this->all_clicks.insert(this->all_clicks.end(), this->clicks.cbegin(), this->clicks.cend());
    }

    // for performance reasons, a lot of operations are crammed into 1 loop over all hitobjects:
    // update all hitobjects,
    // handle click events,
    // also get the time of the next/previous hitobject and their indices for later,
    // and get the current hitobject,
    // also handle miss hiterrorbar slots,
    // also calculate nps and nd,
    // also handle note blocking
    this->currentHitObject = nullptr;
    this->iNextHitObjectTime = 0;
    this->iPreviousHitObjectTime = 0;
    this->iPreviousFollowPointObjectIndex = 0;
    this->iNPS = 0;
    this->iND = 0;
    this->iCurrentNumCircles = 0;
    this->iCurrentNumSliders = 0;
    this->iCurrentNumSpinners = 0;
    {
        bool blockNextNotes = false;
        bool spinner_active = false;

        // to avoid recalculating in HitObject::update every call
        this->fCachedApproachTimeForUpdate = this->getApproachTime();
        this->fBaseAnimationSpeedFactor = osu->getAnimationSpeedMultiplier();
        this->fSpeedAdjustedAnimationSpeedFactor = this->getSpeedMultiplier() / this->fBaseAnimationSpeedFactor;

        const i32 pvs = [&]() -> i32 {
            if(likely(!cv::mod_mafham.getBool()) || unlikely(this->hitobjects.empty())) {
                return this->getPVS();
            }
            // mafham PVS
            const i32 objIdx =
                std::clamp<i32>(this->iCurrentHitObjectIndex + cv::mod_mafham_render_livesize.getInt() + 1, 0,
                                (i32)this->hitobjects.size() - 1);
            return this->hitobjects[objIdx]->click_time - this->iCurMusicPosWithOffsets + 1500;
        }();
        const bool usePVS = cv::pvs.getBool();

        const int notelockType = cv::notelock_type.getInt();
        const i32 tolerance2B = (i32)cv::notelock_stable_tolerance2b.getInt();

        this->iCurrentHitObjectIndex = 0;  // reset below here, since it's needed for mafham pvs

        for(int i = -1; const auto &hobjptr : this->hitobjects) {
            ++i;
            HitObject *curHobj = hobjptr.get();
            // the order must be like this:
            // 0) miscellaneous stuff (minimal performance impact)
            // 1) prev + next time vars
            // 2) PVS optimization
            // 3) main hitobject update
            // 4) note blocking
            // 5) click events
            //
            // (because the hitobjects need to know about note blocking before handling the click events)

            // ************ live pp block start ************ //
            const bool isCircle = curHobj->type == HitObjectType::CIRCLE;
            const bool isSlider = curHobj->type == HitObjectType::SLIDER;
            const bool isSpinner = curHobj->type == HitObjectType::SPINNER;
            // ************ live pp block end ************** //

            // determine previous & next object time, used for auto + followpoints + warning arrows + empty section
            // skipping
            if(this->iNextHitObjectTime == 0) {
                if(curHobj->click_time > this->iCurMusicPosWithOffsets)
                    this->iNextHitObjectTime = curHobj->click_time;
                else {
                    this->currentHitObject = curHobj;
                    const i32 actualPrevHitObjectTime = curHobj->getEndTime();
                    this->iPreviousHitObjectTime = actualPrevHitObjectTime;

                    if(this->iCurMusicPosWithOffsets >
                       actualPrevHitObjectTime + (i32)cv::followpoints_prevfadetime.getFloat())
                        this->iPreviousFollowPointObjectIndex = i;
                }
            }

            // PVS optimization
            if(usePVS) {
                if(curHobj->isFinished() &&
                   (this->iCurMusicPosWithOffsets - pvs > curHobj->getEndTime()))  // past objects
                {
                    // ************ live pp block start ************ //
                    if(isCircle) this->iCurrentNumCircles++;
                    if(isSlider) this->iCurrentNumSliders++;
                    if(isSpinner) this->iCurrentNumSpinners++;

                    this->iCurrentHitObjectIndex = i;
                    // ************ live pp block end ************** //

                    continue;
                }
                if(curHobj->click_time > this->iCurMusicPosWithOffsets + pvs)  // future objects
                    break;
            }

            // ************ live pp block start ************ //
            if(this->iCurMusicPosWithOffsets >= curHobj->getEndTime()) this->iCurrentHitObjectIndex = i;
            // ************ live pp block end ************** //

            // main hitobject update
            curHobj->update(this->iCurMusicPosWithOffsets, current_frametime);

            // spinner visibility detection
            // XXX: there might be a "better" way to do it?
            if(isSpinner) {
                bool spinner_started = this->iCurMusicPosWithOffsets >= curHobj->click_time;
                bool spinner_ended = this->iCurMusicPosWithOffsets > curHobj->getEndTime();
                spinner_active |= (spinner_started && !spinner_ended);
            }

            // note blocking / notelock (1)
            const Slider *currentSliderPointer = isSlider ? static_cast<Slider *>(curHobj) : nullptr;
            if(notelockType > 0) {
                curHobj->setBlocked(blockNextNotes);

                if(notelockType == 1)  // mcosu
                {
                    // (nothing, handled in (2) block)
                } else if(notelockType == 2)  // osu!stable
                {
                    if(!curHobj->isFinished()) {
                        blockNextNotes = true;

                        // Sliders are "finished" after they end
                        // Extra handling for simultaneous/2b hitobjects, as these would otherwise get blocked
                        // NOTE: this will still unlock some simultaneous/2b patterns too early
                        //       (slider slider circle [circle]), but nobody from that niche has complained so far
                        {
                            const bool isSlider = (currentSliderPointer != nullptr);
                            const bool isSpinner = (!isSlider && !isCircle);

                            if(isSlider || isSpinner) {
                                if((i + 1) < this->hitobjects.size()) {
                                    if((isSpinner || currentSliderPointer->isStartCircleFinished()) &&
                                       (this->hitobjects[i + 1]->click_time <= (curHobj->getEndTime() + tolerance2B)))
                                        blockNextNotes = false;
                                }
                            }
                        }
                    }
                } else if(notelockType == 3)  // osu!lazer 2020
                {
                    if(!curHobj->isFinished()) {
                        const bool isSlider = (currentSliderPointer != nullptr);
                        const bool isSpinner = (!isSlider && !isCircle);

                        if(!isSpinner)  // spinners are completely ignored (transparent)
                        {
                            blockNextNotes = (this->iCurMusicPosWithOffsets <= curHobj->click_time);

                            // sliders are "finished" after their startcircle
                            {
                                // sliders with finished startcircles do not block
                                if(currentSliderPointer != nullptr && currentSliderPointer->isStartCircleFinished())
                                    blockNextNotes = false;
                            }
                        }
                    }
                }
            } else
                curHobj->setBlocked(false);

            // click events (this also handles hitsounds!)
            const bool isCurrentHitObjectASliderAndHasItsStartCircleFinishedBeforeClickEvents =
                (currentSliderPointer != nullptr && currentSliderPointer->isStartCircleFinished());
            const bool isCurrentHitObjectFinishedBeforeClickEvents = curHobj->isFinished();
            {
                if(this->clicks.size() > 0) curHobj->onClickEvent(this->clicks);
            }
            const bool isCurrentHitObjectFinishedAfterClickEvents = curHobj->isFinished();
            const bool isCurrentHitObjectASliderAndHasItsStartCircleFinishedAfterClickEvents =
                (currentSliderPointer != nullptr && currentSliderPointer->isStartCircleFinished());

            // note blocking / notelock (2.1)
            if(!isCurrentHitObjectASliderAndHasItsStartCircleFinishedBeforeClickEvents &&
               isCurrentHitObjectASliderAndHasItsStartCircleFinishedAfterClickEvents) {
                // in here if a slider had its startcircle clicked successfully in this update iteration

                if(notelockType == 2)  // osu!stable
                {
                    // edge case: frame perfect double tapping on overlapping sliders would incorrectly eat the second
                    // input, because the isStartCircleFinished() 2b edge case check handling happens before
                    // m_hitobjects[i]->onClickEvent(this->clicks); so, we check if the currentSliderPointer got its
                    // isStartCircleFinished() within this m_hitobjects[i]->onClickEvent(this->clicks); and unlock
                    // blockNextNotes if that is the case note that we still only unlock within duration + tolerance2B
                    // (same as in (1))
                    if((i + 1) < this->hitobjects.size()) {
                        if((this->hitobjects[i + 1]->click_time <= (curHobj->getEndTime() + tolerance2B)))
                            blockNextNotes = false;
                    }
                }
            }

            // note blocking / notelock (2.2)
            if(!isCurrentHitObjectFinishedBeforeClickEvents && isCurrentHitObjectFinishedAfterClickEvents) {
                // in here if a hitobject has been clicked (and finished completely) successfully in this update
                // iteration

                blockNextNotes = false;

                if(notelockType == 1)  // mcosu
                {
                    // auto miss all previous unfinished hitobjects, always
                    // (can stop reverse iteration once we get to the first finished hitobject)

                    for(int m = i - 1; m >= 0; m--) {
                        if(!this->hitobjects[m]->isFinished()) {
                            const bool isSlider = this->hitobjects[m]->type == HitObjectType::SLIDER;
                            const bool isSpinner = (!isSlider && !isCircle);

                            if(!isSpinner)  // spinners are completely ignored (transparent)
                            {
                                if(curHobj->click_time >
                                   (this->hitobjects[m]->click_time +
                                    this->hitobjects[m]->duration))  // NOTE: 2b exception. only force miss if objects
                                                                     // are not overlapping.
                                    this->hitobjects[m]->miss(this->iCurMusicPosWithOffsets);
                            }
                        } else
                            break;
                    }
                } else if(notelockType == 2)  // osu!stable
                {
                    // (nothing, handled in (1) and (2.1) blocks)
                } else if(notelockType == 3)  // osu!lazer 2020
                {
                    // auto miss all previous unfinished hitobjects if the current music time is > their time (center)
                    // (can stop reverse iteration once we get to the first finished hitobject)

                    for(int m = i - 1; m >= 0; m--) {
                        if(!this->hitobjects[m]->isFinished()) {
                            const bool isSlider = this->hitobjects[m]->type == HitObjectType::SLIDER;
                            const bool isSpinner = (!isSlider && !isCircle);

                            if(!isSpinner)  // spinners are completely ignored (transparent)
                            {
                                if(this->iCurMusicPosWithOffsets > this->hitobjects[m]->click_time) {
                                    if(curHobj->click_time >
                                       this->hitobjects[m]->getEndTime())  // NOTE: 2b exception. only force miss if
                                                                           // objects are not overlapping.
                                        this->hitobjects[m]->miss(this->iCurMusicPosWithOffsets);
                                }
                            }
                        } else
                            break;
                    }
                }
            }

            // ************ live pp block start ************ //
            if(isCircle && curHobj->isFinished()) this->iCurrentNumCircles++;
            if(isSlider && curHobj->isFinished()) this->iCurrentNumSliders++;
            if(isSpinner && curHobj->isFinished()) this->iCurrentNumSpinners++;

            if(curHobj->isFinished()) this->iCurrentHitObjectIndex = i;
            // ************ live pp block end ************** //

            // notes per second
            const i32 npsHalfGateSizeMS = (i32)(500.0f * this->getSpeedMultiplier());
            if(curHobj->click_time > this->iCurMusicPosWithOffsets - npsHalfGateSizeMS &&
               curHobj->click_time < this->iCurMusicPosWithOffsets + npsHalfGateSizeMS)
                this->iNPS++;

            // note density
            if(curHobj->isVisible()) this->iND++;
        }

        // miss hiterrorbar slots
        // this gets the closest previous unfinished hitobject, as well as all following hitobjects which are in 50
        // range and could be clicked
        if(cv::hiterrorbar_misaims.getBool()) {
            this->misaimObjects.clear();
            HitObject *lastUnfinishedHitObject = nullptr;
            const i32 hitWindow50 = (i32)this->getHitWindow50();
            for(const auto &hitobject : this->hitobjects)  // this shouldn't hurt performance too much, since no
                                                           // expensive operations are happening within the loop
            {
                if(!hitobject->isFinished()) {
                    if(this->iCurMusicPosWithOffsets >= hitobject->click_time)
                        lastUnfinishedHitObject = hitobject.get();
                    else if(std::abs(hitobject->click_time - this->iCurMusicPosWithOffsets) < hitWindow50)
                        this->misaimObjects.push_back(hitobject.get());
                    else
                        break;
                }
            }
            if(lastUnfinishedHitObject != nullptr &&
               std::abs(lastUnfinishedHitObject->click_time - this->iCurMusicPosWithOffsets) < hitWindow50)
                this->misaimObjects.insert(this->misaimObjects.begin(), lastUnfinishedHitObject);

            // now, go through the remaining clicks, and go through the unfinished hitobjects.
            // handle misaim clicks sequentially (setting the misaim flag on the hitobjects to only allow 1 entry in the
            // hiterrorbar for misses per object) clicks don't have to be consumed here, as they are deleted below
            // anyway
            for(const auto &click : this->clicks) {
                for(auto *misaimObject : this->misaimObjects) {
                    if(misaimObject->hasMisAimed())  // only 1 slot per object!
                        continue;

                    misaimObject->misAimed();
                    const i32 delta = click.music_pos - (i32)misaimObject->click_time;
                    ui->getHUD()->addHitError(delta, false, true);

                    break;  // the current click has been dealt with (and the hitobject has been misaimed)
                }
            }
        }

        // all remaining clicks which have not been consumed by any hitobjects can safely be deleted
        if(this->clicks.size() > 0) {
            // nightmare mod: extra clicks = sliderbreak
            bool break_on_extra_click = (osu->getModNightmare() || cv::mod_jigsaw1.getBool());
            break_on_extra_click &= !this->bIsInSkippableSection && !this->bInBreak && !spinner_active;
            break_on_extra_click &= this->iCurrentHitObjectIndex > 0;
            if(break_on_extra_click) {
                this->addSliderBreak();
                this->addHitResult(nullptr, LiveScore::HIT::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                                   false);  // only decrease health
            }

            this->clicks.clear();
        }
    }

    // empty section detection & skipping
    if(likely(!this->hitobjects.empty())) {
        const bool wasInSkippableSection = this->bIsInSkippableSection;

        const i32 legacyOffset = (this->iPreviousHitObjectTime < this->hitobjects[0]->click_time ? 0 : 1000);  // Mc
        const i32 nextHitObjectDelta = this->iNextHitObjectTime - (i32)this->iCurMusicPosWithOffsets;
        if(nextHitObjectDelta > 0 && nextHitObjectDelta > (i32)cv::skip_time.getInt() &&
           this->iCurMusicPosWithOffsets > (this->iPreviousHitObjectTime + legacyOffset))
            this->bIsInSkippableSection = true;
        else if(!cv::end_skip.getBool() && nextHitObjectDelta < 0)
            this->bIsInSkippableSection = true;
        else
            this->bIsInSkippableSection = false;

        if((wasInSkippableSection != this->bIsInSkippableSection) || (BanchoState::is_playing_a_multi_map())) {
            // FIXME: why the FUCK is this here?
            ui->getChat()->updateVisibility();
        }

        // While we want to allow the chat to pop up during breaks, we don't
        // want to be able to skip after the start in multiplayer rooms
        if(BanchoState::is_playing_a_multi_map() && this->iCurrentHitObjectIndex > 0) {
            this->bIsInSkippableSection = false;
        }
    }

    // warning arrow logic
    if(likely(!this->hitobjects.empty())) {
        const i32 legacyOffset = (this->iPreviousHitObjectTime < this->hitobjects[0]->click_time ? 0 : 1000);  // Mc
        const i32 minGapSize = 1000;
        const i32 lastVisibleMin = 400;
        const i32 blinkDelta = 100;

        const i32 gapSize = this->iNextHitObjectTime - (this->iPreviousHitObjectTime + legacyOffset);
        const i32 nextDelta = (this->iNextHitObjectTime - this->iCurMusicPosWithOffsets);
        const bool drawWarningArrows = gapSize > minGapSize && nextDelta > 0;
        if(drawWarningArrows &&
           ((nextDelta <= lastVisibleMin + blinkDelta * 13 && nextDelta > lastVisibleMin + blinkDelta * 12) ||
            (nextDelta <= lastVisibleMin + blinkDelta * 11 && nextDelta > lastVisibleMin + blinkDelta * 10) ||
            (nextDelta <= lastVisibleMin + blinkDelta * 9 && nextDelta > lastVisibleMin + blinkDelta * 8) ||
            (nextDelta <= lastVisibleMin + blinkDelta * 7 && nextDelta > lastVisibleMin + blinkDelta * 6) ||
            (nextDelta <= lastVisibleMin + blinkDelta * 5 && nextDelta > lastVisibleMin + blinkDelta * 4) ||
            (nextDelta <= lastVisibleMin + blinkDelta * 3 && nextDelta > lastVisibleMin + blinkDelta * 2) ||
            (nextDelta <= lastVisibleMin + blinkDelta * 1 && nextDelta > lastVisibleMin)))
            this->bShouldFlashWarningArrows = true;
        else
            this->bShouldFlashWarningArrows = false;
    }

    // break time detection, and background fade during breaks
    const DatabaseBeatmap::BREAK breakEvent = this->getBreakForTimeRange(
        this->iPreviousHitObjectTime, this->iCurMusicPosWithOffsets, this->iNextHitObjectTime);
    const bool isInBreak = ((int)this->iCurMusicPosWithOffsets >= breakEvent.startTime &&
                            (int)this->iCurMusicPosWithOffsets <= breakEvent.endTime);
    if(isInBreak != this->bInBreak) {
        this->bInBreak = !this->bInBreak;

        if(!cv::background_dont_fade_during_breaks.getBool() || this->fBreakBackgroundFade != 0.0f) {
            if(this->bInBreak && !cv::background_dont_fade_during_breaks.getBool()) {
                const int breakDuration = breakEvent.endTime - breakEvent.startTime;
                if(breakDuration > (int)(cv::background_fade_min_duration.getFloat() * 1000.0f))
                    anim::moveLinear(&this->fBreakBackgroundFade, 1.0f, cv::background_fade_in_duration.getFloat(),
                                     true);
            } else
                anim::moveLinear(&this->fBreakBackgroundFade, 0.0f, cv::background_fade_out_duration.getFloat(), true);
        }
    }

    // section pass/fail logic
    if(likely(!this->hitobjects.empty())) {
        const i32 minGapSize = 2880;
        const i32 fadeStart = 1280;
        const i32 fadeEnd = 1480;

        const i32 gapSize = this->iNextHitObjectTime - this->iPreviousHitObjectTime;
        const i32 start = (gapSize / 2 > minGapSize ? this->iPreviousHitObjectTime + (gapSize / 2)
                                                    : this->iNextHitObjectTime - minGapSize);
        const i32 nextDelta = this->iCurMusicPosWithOffsets - start;
        const bool inSectionPassFail =
            (gapSize > minGapSize && nextDelta > 0) &&
            this->iCurMusicPosWithOffsets > this->hitobjects[0]->click_time &&
            this->iCurMusicPosWithOffsets < (this->hitobjectsSortedByEndTime.back()->click_time +
                                             this->hitobjectsSortedByEndTime.back()->duration) &&
            !this->bFailed && this->bInBreak && (breakEvent.endTime - breakEvent.startTime) > minGapSize;

        const bool passing = (this->fHealth >= 0.5);

        // draw logic
        if(passing) {
            if(inSectionPassFail && ((nextDelta <= fadeEnd && nextDelta >= 280) ||
                                     (nextDelta <= 230 && nextDelta >= 160) || (nextDelta <= 100 && nextDelta >= 20))) {
                const f32 fadeAlpha = 1.0f - (f32)(nextDelta - fadeStart) / (f32)(fadeEnd - fadeStart);
                this->fShouldFlashSectionPass = (nextDelta > fadeStart ? fadeAlpha : 1.0f);
            } else
                this->fShouldFlashSectionPass = 0.0f;
        } else {
            if(inSectionPassFail &&
               ((nextDelta <= fadeEnd && nextDelta >= 280) || (nextDelta <= 230 && nextDelta >= 130))) {
                const f32 fadeAlpha = 1.0f - (f32)(nextDelta - fadeStart) / (f32)(fadeEnd - fadeStart);
                this->fShouldFlashSectionFail = (nextDelta > fadeStart ? fadeAlpha : 1.0f);
            } else
                this->fShouldFlashSectionFail = 0.0f;
        }

        // sound logic
        if(inSectionPassFail) {
            if(this->iPreviousSectionPassFailTime != start &&
               ((passing && nextDelta >= 20) || (!passing && nextDelta >= 130))) {
                this->iPreviousSectionPassFailTime = start;

                if(!wasSeekFrame) {
                    if(passing)
                        soundEngine->play(this->getSkin()->s_section_pass);
                    else
                        soundEngine->play(this->getSkin()->s_section_fail);
                }
            }
        }
    }

    // hp drain & failing
    // handle constant drain
    {
        if(this->fDrainRate > 0.0) {
            if(this->bIsPlaying                  // not paused
               && !this->bInBreak                // not in a break
               && !this->bIsInSkippableSection)  // not in a skippable section
            {
                // special case: break drain edge cases
                bool drainAfterLastHitobjectBeforeBreakStart = (this->beatmap->getVersion() < 8);

                const bool isBetweenHitobjectsAndBreak = (int)this->iPreviousHitObjectTime <= breakEvent.startTime &&
                                                         (int)this->iNextHitObjectTime >= breakEvent.endTime &&
                                                         this->iCurMusicPosWithOffsets > this->iPreviousHitObjectTime;
                const bool isLastHitobjectBeforeBreakStart =
                    isBetweenHitobjectsAndBreak && (int)this->iCurMusicPosWithOffsets <= breakEvent.startTime;

                if(!isBetweenHitobjectsAndBreak ||
                   (drainAfterLastHitobjectBeforeBreakStart && isLastHitobjectBeforeBreakStart)) {
                    // special case: spinner nerf
                    f64 spinnerDrainNerf = this->isSpinnerActive() ? 0.25 : 1.0;
                    this->addHealth(
                        -this->fDrainRate * current_frametime * (f64)this->getSpeedMultiplier() * spinnerDrainNerf,
                        false);
                }
            }
        }
    }

    // revive in mp
    if(this->fHealth > 0.999 && osu->getScore()->isDead()) osu->getScore()->setDead(false);

    // handle fail animation
    if(this->bFailed) {
        if(this->fFailAnim <= 0.0f) {
            if(this->music->isPlaying() || !ui->getPauseOverlay()->isVisible()) {
                soundEngine->pause(this->music);
                this->bIsPaused = true;

                if(BanchoState::spectating) {
                    osu->bIsPlayingASelectedBeatmap = false;
                } else {
                    ui->getPauseOverlay()->setVisible(true);
                    osu->updateConfineCursor();
                }
            }
        } else {
            this->music->setFrequency(this->fMusicFrequencyBackup * this->fFailAnim > 100
                                          ? this->fMusicFrequencyBackup * this->fFailAnim
                                          : 100);
        }
    }

    // spectator score correction
    if(BanchoState::spectating && this->spectated_replay.size() >= 2) {
        const auto &current_frame = this->spectated_replay[this->current_frame_idx];

        i32 score_frame_idx = -1;
        for(i32 i = 0; i < this->score_frames.size(); i++) {
            if(this->score_frames[i].time == current_frame.cur_music_pos) {
                score_frame_idx = i;
                break;
            }
        }

        if(score_frame_idx != -1) {
            const auto &score = osu->getScore();
            auto fixed_score = this->score_frames[score_frame_idx];
            score->iNum300s = fixed_score.num300;
            score->iNum100s = fixed_score.num100;
            score->iNum50s = fixed_score.num50;
            score->iNum300gs = fixed_score.num_geki;
            score->iNum100ks = fixed_score.num_katu;
            score->iNumMisses = fixed_score.num_miss;
            score->iScoreV1 = fixed_score.total_score;
            score->iScoreV2 = fixed_score.total_score;
            score->iComboMax = fixed_score.max_combo;
            score->iCombo = fixed_score.current_combo;

            // XXX: instead naively of setting it, we should simulate time decay
            this->fHealth = ((f32)fixed_score.current_hp) / 255.f;

            // fixed_score.is_perfect is unused
            // fixed_score.tag is unused (XXX: i think scorev2 uses this?)
            // fixed_score.is_scorev2 is unused
        }
    }
}

void BeatmapInterface::broadcast_spectator_frames() {
    if(BanchoState::spectators.empty()) return;

    Packet packet;
    packet.id = OUTP_SPECTATE_FRAMES;
    packet.write<i32>(0);
    packet.write<u16>(this->frame_batch.size());
    for(auto batch : this->frame_batch) {
        packet.write<LiveReplayFrame>(batch);
    }
    packet.write<u8>((u8)LiveReplayAction::NONE);
    packet.write<ScoreFrame>(ScoreFrame::get());
    packet.write<u16>(this->spectator_sequence++);
    BANCHO::Net::send_packet(packet);

    this->frame_batch.clear();
    this->last_spectator_broadcast = engine->getTime();
}

void BeatmapInterface::write_frame() {
    if(!this->bIsPlaying || this->bFailed || this->is_watching || BanchoState::spectating) return;

    i32 delta = this->iCurMusicPosWithOffsets - this->last_event_ms;
    if(delta < 0) return;
    if(delta == 0 && this->last_keys == this->current_keys) return;

    vec2 pos = this->pixels2OsuCoords(this->getCursorPos());
    if(cv::playfield_mirror_horizontal.getBool()) pos.y = GameRules::OSU_COORD_HEIGHT - pos.y;
    if(cv::playfield_mirror_vertical.getBool()) pos.x = GameRules::OSU_COORD_WIDTH - pos.x;
    if(cv::playfield_rotation.getFloat() != 0.0f) {
        pos.x -= GameRules::OSU_COORD_WIDTH / 2;
        pos.y -= GameRules::OSU_COORD_HEIGHT / 2;
        vec3 coords3 = vec3(pos.x, pos.y, 0);
        Matrix4 rot;
        rot.rotateZ(-cv::playfield_rotation.getFloat());
        coords3 = coords3 * rot;
        coords3.x += GameRules::OSU_COORD_WIDTH / 2;
        coords3.y += GameRules::OSU_COORD_HEIGHT / 2;
        pos.x = coords3.x;
        pos.y = coords3.y;
    }

    // TODO: not CBF-friendly, since it's called in the update loop and doesn't use click timestamps

    // In replays, "K1" is always stored as "K1+M1"
    // (unless we have the "no keylock" mod on, which uses 4 keys instead of only 2)
    u8 replay_keys = this->current_keys;
    if(!cv::mod_no_keylock.getBool()) {
        if(this->current_keys & LegacyReplay::KeyFlags::K1) replay_keys |= LegacyReplay::KeyFlags::M1;
        if(this->current_keys & LegacyReplay::KeyFlags::K2) replay_keys |= LegacyReplay::KeyFlags::M2;
    }

    this->live_replay.push_back(LegacyReplay::Frame{
        .cur_music_pos = this->iCurMusicPosWithOffsets,
        .milliseconds_since_last_frame = delta,
        .x = pos.x,
        .y = pos.y,
        .key_flags = replay_keys,
    });

    this->frame_batch.push_back(LiveReplayFrame{
        .key_flags = replay_keys,
        .padding = 0,
        .mouse_x = pos.x,
        .mouse_y = pos.y,
        .time = (i32)this->iCurMusicPos,  // NOTE: might be incorrect
    });

    this->last_event_time = engine->getTime();
    this->last_event_ms = this->iCurMusicPosWithOffsets;
    this->last_keys = this->current_keys;

    if(!BanchoState::spectators.empty() && last_event_time > this->last_spectator_broadcast + 1.0) {
        this->broadcast_spectator_frames();
    }
}

void BeatmapInterface::onModUpdate(bool rebuildSliderVertexBuffers, bool recomputeDrainRate) {
    logIfCV(debug_osu, "onModUpdate() @ {:f}", engine->getTime());

    this->updatePlayfieldMetrics();
    this->updateHitobjectMetrics();

    if(recomputeDrainRate) this->computeDrainRate();

    // Updates not just speed but also nightcore state
    this->setMusicSpeed(this->getSpeedMultiplier());

    // recalculate slider vertexbuffers
    if(osu->getModHR() != this->bWasHREnabled ||
       cv::playfield_mirror_horizontal.getBool() != this->bWasHorizontalMirrorEnabled ||
       cv::playfield_mirror_vertical.getBool() != this->bWasVerticalMirrorEnabled) {
        this->bWasHREnabled = osu->getModHR();
        this->bWasHorizontalMirrorEnabled = cv::playfield_mirror_horizontal.getBool();
        this->bWasVerticalMirrorEnabled = cv::playfield_mirror_vertical.getBool();

        this->calculateStacks();

        if(rebuildSliderVertexBuffers) this->updateSliderVertexBuffers();
    }
    if(osu->getModEZ() != this->bWasEZEnabled) {
        this->calculateStacks();

        this->bWasEZEnabled = osu->getModEZ();
        if(rebuildSliderVertexBuffers) this->updateSliderVertexBuffers();
    }
    if(this->fHitcircleDiameter != this->fPrevHitCircleDiameter && likely(!this->hitobjects.empty())) {
        this->calculateStacks();

        this->fPrevHitCircleDiameter = this->fHitcircleDiameter;
        if(rebuildSliderVertexBuffers) this->updateSliderVertexBuffers();
    }
    if(cv::playfield_rotation.getFloat() != this->fPrevPlayfieldRotationFromConVar) {
        this->fPrevPlayfieldRotationFromConVar = cv::playfield_rotation.getFloat();
        if(rebuildSliderVertexBuffers) this->updateSliderVertexBuffers();
    }
    if(cv::mod_mafham.getBool() != this->bWasMafhamEnabled) {
        this->bWasMafhamEnabled = cv::mod_mafham.getBool();
        for(auto &hitobject : this->hitobjects) {
            hitobject->update(this->iCurMusicPosWithOffsets, engine->getFrameTime());
        }
    }

    this->resetLiveStarsTasks();
    this->invalidateWholeMapPPInfo();
}

void BeatmapInterface::resetLiveStarsTasks() {
    logIfCV(debug_osu, "called");

    this->ppv2_calc.invalidate();
}

void BeatmapInterface::invalidateWholeMapPPInfo() {
    this->full_calc_req_params.numMisses = -1;  // invalidate (sentinel)
    this->full_ppinfo.pp = -1.0;                // invalidate (sentinel)
    this->getWholeMapPPInfo();                  // make new request immediately
}

const AsyncPPC::pp_res &BeatmapInterface::getWholeMapPPInfo() {
    auto map = this->beatmap;
    if(!map) return this->full_ppinfo;

    const bool new_request = this->full_calc_req_params.numMisses == -1;
    if(!new_request && this->full_ppinfo.pp != -1.0) {
        return this->full_ppinfo;  // fast path exit
    } else if(new_request) {
        AsyncPPC::set_map(map);  // just in case...

        // full-length pp calc for currently selected mods
        // (the fact that this is duplicated 50000 times means this interface is terrible)
        const auto &mods = osu->getScore()->mods;
        this->full_calc_req_params = {.modFlags = mods.flags,
                                      .speedOverride = mods.speed,
                                      .AR = mods.get_naive_ar(map),
                                      .HP = mods.get_naive_hp(map),
                                      .CS = mods.get_naive_cs(map),
                                      .OD = mods.get_naive_od(map),
                                      .comboMax = -1,
                                      .numMisses = 0,  // unset sentinel for request (bad api)
                                      .num300s = map->getNumObjects(),
                                      .num100s = 0,
                                      .num50s = 0,
                                      .legacyTotalScore = 0,
                                      .scoreFromMcOsu = false};
    }

    this->full_ppinfo = AsyncPPC::query_result(this->full_calc_req_params,
                                               true /* ignore gameplay background thread freeze, we need this asap */);
    // we'll take the fast path once it's done anyways

    return this->full_ppinfo;
}

// HACK: Updates buffering state and pauses/unpauses the music!
bool BeatmapInterface::isBuffering() {
    if(!BanchoState::spectating) return false;

    i32 leeway = this->last_frame_ms - this->iCurMusicPos;
    if(this->is_buffering) {
        // Make sure music is actually paused
        if(this->music->isPlaying()) {
            soundEngine->pause(this->music);
            this->bIsPlaying = false;
            this->bIsPaused = true;
        }

        if(leeway >= cv::spec_buffer.getInt()) {
            debugLog("UNPAUSING: leeway: {:d}, last_event: {:d}, last_frame: {:d}", leeway, this->iCurMusicPos,
                     this->last_frame_ms);
            soundEngine->play(this->music);
            this->bIsPlaying = true;
            this->bIsPaused = false;
            this->is_buffering = false;
        }
    } else {
        HitObject *lastHitObject =
            this->hitobjectsSortedByEndTime.size() > 0 ? this->hitobjectsSortedByEndTime.back() : nullptr;
        bool is_finished = lastHitObject != nullptr && lastHitObject->isFinished();

        if(leeway < 0 && !is_finished) {
            debugLog("PAUSING: leeway: {:d}, last_event: {:d}, last_frame: {:d}", leeway, this->iCurMusicPos,
                     this->last_frame_ms);
            soundEngine->pause(this->music);
            this->bIsPlaying = false;
            this->bIsPaused = true;
            this->is_buffering = true;
        }
    }

    return this->is_buffering;
}

bool BeatmapInterface::isLoading() {
    return (this->isActuallyLoading() || this->isBuffering() ||
            (BanchoState::is_playing_a_multi_map() && !this->all_players_loaded));
}

bool BeatmapInterface::isActuallyLoading() const {
    return (!soundEngine->isReady() || !this->music->isAsyncReady() || this->bIsPreLoading);
}

vec2 BeatmapInterface::pixels2OsuCoords(vec2 pixelCoords) const {
    // un-first-person
    if(cv::mod_fps.getBool()) {
        // HACKHACK: this is the worst hack possible (engine->isDrawing()), but it works
        // the problem is that this same function is called while draw()ing and update()ing
        if(!((engine->isDrawing() && (osu->getModAuto() || osu->getModAutopilot())) ||
             !(osu->getModAuto() || osu->getModAutopilot())))
            pixelCoords += this->getFirstPersonCursorDelta();
    }

    // un-offset and un-scale, reverse order
    pixelCoords -= this->vPlayfieldOffset;
    pixelCoords /= this->fScaleFactor;

    return pixelCoords;
}

vec2 BeatmapInterface::osuCoords2Pixels(vec2 coords) const {
    if(osu->getModHR()) coords.y = GameRules::OSU_COORD_HEIGHT - coords.y;
    if(cv::playfield_mirror_horizontal.getBool()) coords.y = GameRules::OSU_COORD_HEIGHT - coords.y;
    if(cv::playfield_mirror_vertical.getBool()) coords.x = GameRules::OSU_COORD_WIDTH - coords.x;

    // wobble
    if(cv::mod_wobble.getBool()) {
        const f32 speedMultiplierCompensation = 1.0f / this->getSpeedMultiplier();
        coords.x += std::sin((this->iCurMusicPos / 1000.0f) * 5 * speedMultiplierCompensation *
                             cv::mod_wobble_frequency.getFloat()) *
                    cv::mod_wobble_strength.getFloat();
        coords.y += std::sin((this->iCurMusicPos / 1000.0f) * 4 * speedMultiplierCompensation *
                             cv::mod_wobble_frequency.getFloat()) *
                    cv::mod_wobble_strength.getFloat();
    }

    // wobble2
    if(cv::mod_wobble2.getBool()) {
        const f32 speedMultiplierCompensation = 1.0f / this->getSpeedMultiplier();
        vec2 centerDelta = coords - vec2(GameRules::OSU_COORD_WIDTH, GameRules::OSU_COORD_HEIGHT) / 2.f;
        coords.x += centerDelta.x * 0.25f *
                    std::sin((this->iCurMusicPos / 1000.0f) * 5 * speedMultiplierCompensation *
                             cv::mod_wobble_frequency.getFloat()) *
                    cv::mod_wobble_strength.getFloat();
        coords.y += centerDelta.y * 0.25f *
                    std::sin((this->iCurMusicPos / 1000.0f) * 3 * speedMultiplierCompensation *
                             cv::mod_wobble_frequency.getFloat()) *
                    cv::mod_wobble_strength.getFloat();
    }

    // rotation
    if(this->fPlayfieldRotation + cv::playfield_rotation.getFloat() != 0.0f) {
        coords.x -= GameRules::OSU_COORD_WIDTH / 2;
        coords.y -= GameRules::OSU_COORD_HEIGHT / 2;

        vec3 coords3 = vec3(coords.x, coords.y, 0);
        Matrix4 rot;
        rot.rotateZ(this->fPlayfieldRotation + cv::playfield_rotation.getFloat());

        coords3 = coords3 * rot;
        coords3.x += GameRules::OSU_COORD_WIDTH / 2;
        coords3.y += GameRules::OSU_COORD_HEIGHT / 2;

        coords.x = coords3.x;
        coords.y = coords3.y;
    }

    // if wobble, clamp coordinates
    if(cv::mod_wobble.getBool() || cv::mod_wobble2.getBool()) {
        coords.x = std::clamp<f32>(coords.x, 0.0f, GameRules::OSU_COORD_WIDTH);
        coords.y = std::clamp<f32>(coords.y, 0.0f, GameRules::OSU_COORD_HEIGHT);
    }

    if(this->bFailed) {
        f32 failTimePercentInv = 1.0f - this->fFailAnim;  // goes from 0 to 1 over the duration of osu_fail_time
        failTimePercentInv *= failTimePercentInv;

        coords.x -= GameRules::OSU_COORD_WIDTH / 2;
        coords.y -= GameRules::OSU_COORD_HEIGHT / 2;

        vec3 coords3 = vec3(coords.x, coords.y, 0);
        Matrix4 rot;
        rot.rotateZ(failTimePercentInv * 60.0f);

        coords3 = coords3 * rot;
        coords3.x += GameRules::OSU_COORD_WIDTH / 2;
        coords3.y += GameRules::OSU_COORD_HEIGHT / 2;

        coords.x = coords3.x + failTimePercentInv * GameRules::OSU_COORD_WIDTH * 0.25f;
        coords.y = coords3.y + failTimePercentInv * GameRules::OSU_COORD_HEIGHT * 1.25f;
    }

    // scale and offset
    coords *= this->fScaleFactor;
    coords += this->vPlayfieldOffset;  // the offset is already scaled, just add it

    // first person mod, centered cursor
    if(cv::mod_fps.getBool()) {
        // this is the worst hack possible (engine->isDrawing()), but it works
        // the problem is that this same function is called while draw()ing and update()ing
        if((engine->isDrawing() && (osu->getModAuto() || osu->getModAutopilot())) ||
           !(osu->getModAuto() || osu->getModAutopilot()))
            coords += this->getFirstPersonCursorDelta();
    }

    return coords;
}

vec2 BeatmapInterface::osuCoords2RawPixels(vec2 coords) const {
    // scale and offset
    coords *= this->fScaleFactor;
    coords += this->vPlayfieldOffset;  // the offset is already scaled, just add it

    return coords;
}

vec2 BeatmapInterface::osuCoords2LegacyPixels(vec2 coords) const {
    if(osu->getModHR()) coords.y = GameRules::OSU_COORD_HEIGHT - coords.y;
    if(cv::playfield_mirror_horizontal.getBool()) coords.y = GameRules::OSU_COORD_HEIGHT - coords.y;
    if(cv::playfield_mirror_vertical.getBool()) coords.x = GameRules::OSU_COORD_WIDTH - coords.x;

    // rotation
    if(this->fPlayfieldRotation + cv::playfield_rotation.getFloat() != 0.0f) {
        coords.x -= GameRules::OSU_COORD_WIDTH / 2;
        coords.y -= GameRules::OSU_COORD_HEIGHT / 2;

        vec3 coords3 = vec3(coords.x, coords.y, 0);
        Matrix4 rot;
        rot.rotateZ(this->fPlayfieldRotation + cv::playfield_rotation.getFloat());

        coords3 = coords3 * rot;
        coords3.x += GameRules::OSU_COORD_WIDTH / 2;
        coords3.y += GameRules::OSU_COORD_HEIGHT / 2;

        coords.x = coords3.x;
        coords.y = coords3.y;
    }

    // VR center
    coords.x -= GameRules::OSU_COORD_WIDTH / 2;
    coords.y -= GameRules::OSU_COORD_HEIGHT / 2;

    return coords;
}

vec2 BeatmapInterface::getMousePos() const {
    if((this->is_watching && !this->bIsPaused) || BanchoState::spectating) {
        return this->interpolatedMousePos;
    } else {
        return mouse->getPos();
    }
}

vec2 BeatmapInterface::getCursorPos() const {
    if(cv::mod_fps.getBool() && !this->bIsPaused) {
        if(osu->getModAuto() || osu->getModAutopilot()) {
            return this->vAutoCursorPos;
        } else {
            return this->vPlayfieldCenter;
        }
    } else if(osu->getModAuto() || osu->getModAutopilot()) {
        return this->vAutoCursorPos;
    } else {
        vec2 pos = this->getMousePos();
        if(cv::mod_shirone.getBool() && osu->getScore()->getCombo() > 0) {
            return pos + vec2(std::sin((this->iCurMusicPos / 20.0f) * 1.15f) *
                                  ((f32)osu->getScore()->getCombo() / cv::mod_shirone_combo.getFloat()),
                              std::cos((this->iCurMusicPos / 20.0f) * 1.3f) *
                                  ((f32)osu->getScore()->getCombo() / cv::mod_shirone_combo.getFloat()));
        } else {
            return pos;
        }
    }
}

vec2 BeatmapInterface::getFirstPersonCursorDelta() const {
    return this->vPlayfieldCenter -
           (osu->getModAuto() || osu->getModAutopilot() ? this->vAutoCursorPos : this->getMousePos());
}

FinishedScore BeatmapInterface::saveAndSubmitScore(bool quit) {
    // calculate stars
    const std::string &osuFilePath = this->beatmap->getFilePath();
    const f32 AR = this->getAR();
    const f32 HP = this->getHP();
    const f32 CS = this->getCS();
    const f32 OD = this->getOD();
    const f32 speedMultiplier =
        this->getSpeedMultiplier();  // NOTE: not this->getSpeedMultiplier()! (outdated comment ?)
    const bool relax = osu->getModRelax();
    const bool hidden = osu->getModHD();
    const bool touchDevice = osu->getModTD();
    const bool autopilot = osu->getModAutopilot();

    const u32 breakDuration = this->getBreakDurationTotal();
    const u32 playableLength = this->getLengthPlayable();

    auto diffres = DatabaseBeatmap::loadDifficultyHitObjects(osuFilePath, AR, CS, speedMultiplier);

    DifficultyCalculator::BeatmapDiffcalcData diffcalcData{.sortedHitObjects = diffres.diffobjects,
                                                           .CS = CS,
                                                           .HP = HP,
                                                           .AR = AR,
                                                           .OD = OD,
                                                           .hidden = hidden,
                                                           .relax = relax,
                                                           .autopilot = autopilot,
                                                           .touchDevice = touchDevice,
                                                           .speedMultiplier = speedMultiplier,
                                                           .breakDuration = breakDuration,
                                                           .playableLength = playableLength};

    DifficultyCalculator::DifficultyAttributes diffAttributesOut{};

    DifficultyCalculator::StarCalcParams params{.cachedDiffObjects = {},
                                                .outAttributes = diffAttributesOut,
                                                .beatmapData = diffcalcData,
                                                .outAimStrains = nullptr,
                                                .outSpeedStrains = nullptr,
                                                .incremental = nullptr,
                                                .upToObjectIndex = -1,
                                                .cancelCheck = {}};

    const f64 totalStars = DifficultyCalculator::calculateStarDiffForHitObjects(params);

    this->fAimStars = (f32)diffAttributesOut.AimDifficulty;
    this->fSpeedStars = (f32)diffAttributesOut.SpeedDifficulty;

    const auto &liveScore = osu->getScore();

    // calculate final pp
    const int numHitObjects = this->hitobjects.size();
    const int numCircles = this->beatmap->getNumCircles();
    const int numSliders = this->beatmap->getNumSliders();
    const int numSpinners = this->beatmap->getNumSpinners();
    const int highestCombo = liveScore->getComboMax();
    const int numMisses = liveScore->getNumMisses();
    const int num300s = liveScore->getNum300s();
    const int num100s = liveScore->getNum100s();
    const int num50s = liveScore->getNum50s();
    const u32 legacyTotalScore = liveScore->getScore();

    DifficultyCalculator::PPv2CalcParams ppv2calcparams{.attributes = diffAttributesOut,
                                                        .modFlags = liveScore->mods.flags,
                                                        .timescale = speedMultiplier,
                                                        .ar = AR,
                                                        .od = OD,
                                                        .numHitObjects = numHitObjects,
                                                        .numCircles = numCircles,
                                                        .numSliders = numSliders,
                                                        .numSpinners = numSpinners,
                                                        .maxPossibleCombo = this->iMaxPossibleCombo,
                                                        .combo = highestCombo,
                                                        .misses = numMisses,
                                                        .c300 = num300s,
                                                        .c100 = num100s,
                                                        .c50 = num50s,
                                                        .legacyTotalScore = legacyTotalScore,
                                                        .isMcOsuImported = false};

    const f32 pp = DifficultyCalculator::calculatePPv2(ppv2calcparams);

    liveScore->setStarsTomTotal(totalStars);
    liveScore->setStarsTomAim(this->fAimStars);
    liveScore->setStarsTomSpeed(this->fSpeedStars);
    liveScore->setPPv2(pp);

    // save local score, but only under certain conditions
    bool isComplete = (num300s + num100s + num50s + numMisses >= numHitObjects);
    bool isZero = (liveScore->getScore() < 1);
    bool isCheated = (osu->getModAuto() || (osu->getModAutopilot() && osu->getModRelax())) || liveScore->isUnranked() ||
                     this->is_watching || BanchoState::spectating;

    FinishedScore score;

    score.client = fmt::format(PACKAGE_NAME "-" OS_NAME "-{:s}", BanchoState::neomod_version);

    score.unixTimestamp =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    if(BanchoState::is_online()) {
        score.player_id = BanchoState::get_uid();
        score.server = BanchoState::endpoint;
    }
    score.playerName = BanchoState::get_username();
    score.passed = isComplete && !isZero && !liveScore->hasDied();
    score.grade = score.passed ? liveScore->getGrade() : ScoreGrade::F;
    score.map = this->beatmap;
    score.ragequit = quit;
    // iCurMusicPos < 0 means "did not start"
    score.play_time_ms = (this->iCurMusicPos > 0 ? this->iCurMusicPos / this->getSpeedMultiplier() : 0);

    // osu!stable doesn't submit scores of less than 7 seconds
    isZero |= (score.play_time_ms < 7000);

    score.num300s = liveScore->getNum300s();
    score.num100s = liveScore->getNum100s();
    score.num50s = liveScore->getNum50s();
    score.numGekis = liveScore->getNum300gs();
    score.numKatus = liveScore->getNum100ks();
    score.numMisses = liveScore->getNumMisses();
    score.score = liveScore->getScore();
    score.comboMax = liveScore->getComboMax();
    score.perfect = (this->iMaxPossibleCombo > 0 && score.comboMax > 0 && score.comboMax >= this->iMaxPossibleCombo);
    score.numSliderBreaks = liveScore->getNumSliderBreaks();
    score.unstableRate = liveScore->getUnstableRate();
    score.hitErrorAvgMin = liveScore->getHitErrorAvgMin();
    score.hitErrorAvgMax = liveScore->getHitErrorAvgMax();
    score.maxPossibleCombo = this->iMaxPossibleCombo;
    score.numHitObjects = numHitObjects;
    score.numCircles = numCircles;
    score.mods = liveScore->mods;
    score.beatmap_hash = this->beatmap->getMD5();  // NOTE: necessary for "Use Mods"
    score.replay = this->live_replay;

    // @PPV3: store ppv3 data if not already done. also double check replay is marked correctly
    score.ppv2_version = DiffCalc::PP_ALGORITHM_VERSION;
    score.ppv2_score = pp;
    score.ppv2_total_stars = totalStars;
    score.ppv2_aim_stars = this->fAimStars;
    score.ppv2_speed_stars = this->fSpeedStars;

    if(!isCheated) {
        RichPresence::onPlayEnd(quit);

        if(BanchoState::can_submit_scores() && !isZero && this->is_submittable) {
            score.server = BanchoState::endpoint;
            BANCHO::Net::submit_score(score);
            // XXX: Save bancho_score_id after getting submission result
        }

        if(score.passed || cv::save_failed_scores.getBool()) {
            if(!db->addScore(score)) {
                ui->getNotificationOverlay()->addToast(US_("Failed saving score!"), ERROR_TOAST);
            }
        }
    }

    if(!BanchoState::spectators.empty()) {
        this->broadcast_spectator_frames();

        Packet packet;
        packet.id = OUTP_SPECTATE_FRAMES;
        packet.write<i32>(0);
        packet.write<u16>(0);
        packet.write<u8>((u8)(isComplete ? LiveReplayAction::COMPLETION : LiveReplayAction::FAIL));
        packet.write<ScoreFrame>(ScoreFrame::get());
        packet.write<u16>(this->spectator_sequence++);
        BANCHO::Net::send_packet(packet);
    }

    osu->getScore()->setComboFull(this->iMaxPossibleCombo);  // used in RankingScreen/UIRankingScreenRankingPanel

    // special case: incomplete scores should NEVER show pp, even if auto
    if(!isComplete) {
        liveScore->setPPv2(0.0f);
    }

    return score;
}

void BeatmapInterface::updateAutoCursorPos() {
    this->vAutoCursorPos = this->vPlayfieldCenter;
    this->vAutoCursorPos.y *= 2.5f;  // start moving in offscreen from bottom

    if(!this->bIsPlaying && !this->bIsPaused) {
        this->vAutoCursorPos = this->vPlayfieldCenter;
        return;
    }
    if(unlikely(this->hitobjects.empty())) {
        this->vAutoCursorPos = this->vPlayfieldCenter;
        return;
    }

    const i32 curMusicPos = this->iCurMusicPosWithOffsets;

    // general
    i32 prevTime = 0;
    i32 nextTime = this->hitobjects[0]->click_time;
    vec2 prevPos = this->vAutoCursorPos;
    vec2 curPos = this->vAutoCursorPos;
    vec2 nextPos = this->vAutoCursorPos;
    bool haveCurPos = false;

    // dance
    int nextPosIndex = 0;

    if(this->hitobjects[0]->click_time < cv::early_note_time.getInt())
        prevTime = (int)std::roundf((float)-cv::early_note_time.getInt() * this->getSpeedMultiplier());

    if(osu->getModAuto()) {
        bool autoDanceOverride = false;
        for(int i = 0; i < this->hitobjects.size(); i++) {
            HitObject *o = this->hitobjects[i].get();

            // get previous object
            if(o->click_time <= curMusicPos) {
                prevTime = o->getEndTime();
                prevPos = o->getAutoCursorPos(curMusicPos);
                if(o->duration > 0 && curMusicPos - o->click_time <= o->duration) {
                    if(cv::auto_cursordance.getBool()) {
                        auto *sliderPointer = o->type == HitObjectType::SLIDER ? static_cast<Slider *>(o) : nullptr;
                        if(sliderPointer != nullptr) {
                            const std::vector<Slider::SLIDERCLICK> &clicks = sliderPointer->getClicks();

                            // start
                            prevTime = o->click_time;
                            prevPos = this->osuCoords2Pixels(o->getRawPosAt(prevTime));

                            i32 biggestPrevious = 0;
                            i32 smallestNext = (std::numeric_limits<i32>::max)();
                            bool allFinished = true;
                            i32 endTime = 0;

                            // middle clicks
                            for(const auto &click : clicks) {
                                // get previous click
                                if(click.time <= curMusicPos && click.time > biggestPrevious) {
                                    biggestPrevious = click.time;
                                    prevTime = click.time;
                                    prevPos = this->osuCoords2Pixels(o->getRawPosAt(prevTime));
                                }

                                // get next click
                                if(click.time > curMusicPos && click.time < smallestNext) {
                                    smallestNext = click.time;
                                    nextTime = click.time;
                                    nextPos = this->osuCoords2Pixels(o->getRawPosAt(nextTime));
                                }

                                // end hack
                                if(!click.finished)
                                    allFinished = false;
                                else if(click.time > endTime)
                                    endTime = click.time;
                            }

                            // end
                            if(allFinished) {
                                // hack for slider without middle clicks
                                if(endTime == 0) endTime = o->click_time;

                                prevTime = endTime;
                                prevPos = this->osuCoords2Pixels(o->getRawPosAt(prevTime));
                                nextTime = o->getEndTime();
                                nextPos = this->osuCoords2Pixels(o->getRawPosAt(nextTime));
                            }

                            haveCurPos = false;
                            autoDanceOverride = true;
                            break;
                        }
                    }

                    haveCurPos = true;
                    curPos = prevPos;
                    break;
                }
            }

            // get next object
            if(o->click_time > curMusicPos) {
                nextPosIndex = i;
                if(!autoDanceOverride) {
                    nextPos = o->getAutoCursorPos(curMusicPos);
                    nextTime = o->click_time;
                }
                break;
            }
        }
    } else if(osu->getModAutopilot()) {
        for(int i = 0; i < this->hitobjects.size(); i++) {
            HitObject *o = this->hitobjects[i].get();

            // get previous object
            if(o->isFinished() ||
               (curMusicPos > o->getEndTime() + (i32)(this->getHitWindow50() * cv::autopilot_lenience.getFloat()))) {
                prevTime = o->getEndTime() + o->getAutopilotDelta();
                prevPos = o->getAutoCursorPos(curMusicPos);
            } else if(!o->isFinished())  // get next object
            {
                nextPosIndex = i;
                nextPos = o->getAutoCursorPos(curMusicPos);
                nextTime = o->click_time;

                // wait for the user to click
                if(curMusicPos >= nextTime + o->duration) {
                    haveCurPos = true;
                    curPos = nextPos;

                    // i32 delta = curMusicPos - (nextTime + o->duration);
                    o->setAutopilotDelta(curMusicPos - (nextTime + o->duration));
                } else if(o->duration > 0 && curMusicPos >= nextTime)  // handle objects with duration
                {
                    haveCurPos = true;
                    curPos = nextPos;
                    o->setAutopilotDelta(0);
                }

                break;
            }
        }
    }

    if(haveCurPos)  // in active hitObject
        this->vAutoCursorPos = curPos;
    else {
        // interpolation
        f32 percent = 1.0f;
        if((nextTime == 0 && prevTime == 0) || (nextTime - prevTime) == 0)
            percent = 1.0f;
        else
            percent = (f32)((i32)curMusicPos - prevTime) / (f32)(nextTime - prevTime);

        percent = std::clamp<f32>(percent, 0.0f, 1.0f);

        // scaled distance (not osucoords)
        f32 distance = vec::length((nextPos - prevPos));
        if(distance > this->fHitcircleDiameter * 1.05f)  // snap only if not in a stream (heuristic)
        {
            int numIterations = std::clamp<int>(
                osu->getModAutopilot() ? cv::autopilot_snapping_strength.getInt() : cv::auto_snapping_strength.getInt(),
                0, 42);
            for(int i = 0; i < numIterations; i++) {
                percent = (-percent) * (percent - 2.0f);
            }
        } else  // in a stream
        {
            this->iAutoCursorDanceIndex = nextPosIndex;
        }

        this->vAutoCursorPos = prevPos + (nextPos - prevPos) * percent;

        if(cv::auto_cursordance.getBool() && !osu->getModAutopilot()) {
            vec3 dir = vec3(nextPos.x, nextPos.y, 0) - vec3(prevPos.x, prevPos.y, 0);
            vec3 center = dir * 0.5f;
            Matrix4 worldMatrix;
            worldMatrix.translate(center);
            worldMatrix.rotate((1.0f - percent) * 180.0f * (this->iAutoCursorDanceIndex % 2 == 0 ? 1 : -1), 0, 0, 1);
            vec3 fancyAutoCursorPos = worldMatrix * center;
            this->vAutoCursorPos =
                prevPos + (nextPos - prevPos) * 0.5f + vec2(fancyAutoCursorPos.x, fancyAutoCursorPos.y);
        }
    }
}

void BeatmapInterface::updatePlayfieldMetrics() {
    this->fScaleFactor = GameRules::getPlayfieldScaleFactor();
    this->vPlayfieldSize = GameRules::getPlayfieldSize();
    this->vPlayfieldOffset = GameRules::getPlayfieldOffset();
    this->vPlayfieldCenter = GameRules::getPlayfieldCenter();
}

void BeatmapInterface::updateHitobjectMetrics() {
    const auto &skin = this->getSkin();

    this->fRawHitcircleDiameter = GameRules::getRawHitCircleDiameter(this->getCS());
    this->fXMultiplier = GameRules::getHitCircleXMultiplier();
    this->fHitcircleDiameter = GameRules::getRawHitCircleDiameter(this->getCS()) * GameRules::getHitCircleXMultiplier();

    const f32 osuCoordScaleMultiplier = (this->fHitcircleDiameter / this->fRawHitcircleDiameter);
    this->fNumberScale = (this->fRawHitcircleDiameter / (160.0f * (skin->i_defaults[1].scale()))) *
                         osuCoordScaleMultiplier * cv::number_scale_multiplier.getFloat();
    this->fHitcircleOverlapScale =
        (this->fRawHitcircleDiameter / (160.0f)) * osuCoordScaleMultiplier * cv::number_scale_multiplier.getFloat();

    const f32 followcircle_size_multiplier = 2.4f;
    const f32 sliderFollowCircleDiameterMultiplier =
        (osu->getModNightmare() || cv::mod_jigsaw2.getBool()
             ? (1.0f * (1.0f - cv::mod_jigsaw_followcircle_radius_factor.getFloat()) +
                cv::mod_jigsaw_followcircle_radius_factor.getFloat() * followcircle_size_multiplier)
             : followcircle_size_multiplier);
    this->fSliderFollowCircleDiameter = this->fHitcircleDiameter * sliderFollowCircleDiameterMultiplier;
}

void BeatmapInterface::updateSliderVertexBuffers() {
    this->updatePlayfieldMetrics();
    this->updateHitobjectMetrics();

    this->bWasEZEnabled = osu->getModEZ();                    // to avoid useless double updates in onModUpdate()
    this->fPrevHitCircleDiameter = this->fHitcircleDiameter;  // same here
    this->fPrevPlayfieldRotationFromConVar = cv::playfield_rotation.getFloat();  // same here

    debugLog("rebuilding for {:d} hitobjects ...", this->hitobjects.size());

    for(auto &hitobject : this->hitobjects) {
        auto *sliderPointer =
            hitobject && hitobject->type == HitObjectType::SLIDER ? static_cast<Slider *>(hitobject.get()) : nullptr;
        if(sliderPointer != nullptr) sliderPointer->rebuildVertexBuffer();
    }
}

void BeatmapInterface::calculateStacks() {
    if(!this->beatmap) return;
    this->updateHitobjectMetrics();

    debugLog("Beatmap: Calculating stacks ...");

    // reset
    for(auto &hitobject : this->hitobjects) {
        hitobject->setStack(0);
    }

    const f32 STACK_LENIENCE = 3.0f;
    const f32 STACK_OFFSET = 0.05f;

    const f32 approachTime =
        GameRules::mapDifficultyRange(this->getAR(), GameRules::getMinApproachTime(), GameRules::getMidApproachTime(),
                                      GameRules::getMaxApproachTime());
    const f32 stackLeniency = this->beatmap->getStackLeniency();

    if(this->beatmap->getVersion() > 5) {
        // peppy's algorithm
        // https://gist.github.com/peppy/1167470

        for(int i = this->hitobjects.size() - 1; i >= 0; i--) {
            int n = i;

            HitObject *objectI = this->hitobjects[i].get();

            bool isSpinner = objectI->type == HitObjectType::SPINNER;

            if(objectI->getStack() != 0 || isSpinner) continue;

            bool isHitCircle = objectI->type == HitObjectType::CIRCLE;
            bool isSlider = objectI->type == HitObjectType::SLIDER;

            if(isHitCircle) {
                while(--n >= 0) {
                    HitObject *objectN = this->hitobjects[n].get();

                    bool isSpinnerN = objectN->type == HitObjectType::SPINNER;

                    if(isSpinnerN) continue;

                    if(objectI->click_time - (approachTime * stackLeniency) > (objectN->getEndTime())) break;

                    vec2 objectNEndPosition = objectN->getOriginalRawPosAt(objectN->getEndTime());
                    if(objectN->duration != 0 &&
                       vec::length(objectNEndPosition - objectI->getOriginalRawPosAt(objectI->click_time)) <
                           STACK_LENIENCE) {
                        int offset = objectI->getStack() - objectN->getStack() + 1;
                        for(int j = n + 1; j <= i; j++) {
                            if(vec::length((objectNEndPosition - this->hitobjects[j]->getOriginalRawPosAt(
                                                                     this->hitobjects[j]->click_time))) <
                               STACK_LENIENCE)
                                this->hitobjects[j]->setStack(this->hitobjects[j]->getStack() - offset);
                        }

                        break;
                    }

                    if(vec::length((objectN->getOriginalRawPosAt(objectN->click_time) -
                                    objectI->getOriginalRawPosAt(objectI->click_time))) < STACK_LENIENCE) {
                        objectN->setStack(objectI->getStack() + 1);
                        objectI = objectN;
                    }
                }
            } else if(isSlider) {
                while(--n >= 0) {
                    HitObject *objectN = this->hitobjects[n].get();

                    bool isSpinnerN = objectN->type == HitObjectType::SPINNER;

                    if(isSpinnerN) continue;

                    if(objectI->click_time - (approachTime * stackLeniency) > objectN->click_time) break;

                    if(vec::length(((objectN->duration != 0 ? objectN->getOriginalRawPosAt(objectN->getEndTime())
                                                            : objectN->getOriginalRawPosAt(objectN->click_time)) -
                                    objectI->getOriginalRawPosAt(objectI->click_time))) < STACK_LENIENCE) {
                        objectN->setStack(objectI->getStack() + 1);
                        objectI = objectN;
                    }
                }
            }
        }
    } else  // getSelectedDifficulty()->version < 6
    {
        // old stacking algorithm for old beatmaps
        // https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Beatmaps/BeatmapProcessor.cs

        for(int i = 0; i < this->hitobjects.size(); i++) {
            HitObject *currHitObject = this->hitobjects[i].get();
            auto *sliderPointer = currHitObject && currHitObject->type == HitObjectType::SLIDER
                                      ? static_cast<Slider *>(currHitObject)
                                      : nullptr;

            const bool isSlider = (sliderPointer != nullptr);

            if(currHitObject->getStack() != 0 && !isSlider) continue;

            i32 startTime = currHitObject->getEndTime();
            int sliderStack = 0;

            for(int j = i + 1; j < this->hitobjects.size(); j++) {
                HitObject *objectJ = this->hitobjects[j].get();

                if(objectJ->click_time - (approachTime * stackLeniency) > startTime) break;

                // "The start position of the hitobject, or the position at the end of the path if the hitobject is a
                // slider"
                vec2 position2 = isSlider ? sliderPointer->getOriginalRawPosAt(sliderPointer->getEndTime())
                                          : currHitObject->getOriginalRawPosAt(currHitObject->click_time);

                if(vec::length((objectJ->getOriginalRawPosAt(objectJ->click_time) -
                                currHitObject->getOriginalRawPosAt(currHitObject->click_time))) < 3) {
                    currHitObject->setStack(currHitObject->getStack() + 1);
                    startTime = objectJ->getEndTime();
                } else if(vec::length((objectJ->getOriginalRawPosAt(objectJ->click_time) - position2)) < 3) {
                    // "Case for sliders - bump notes down and right, rather than up and left."
                    sliderStack++;
                    objectJ->setStack(objectJ->getStack() - sliderStack);
                    startTime = objectJ->getEndTime();
                }
            }
        }
    }

    // update hitobject positions
    f32 stackOffset = this->fRawHitcircleDiameter * STACK_OFFSET;
    for(auto &hitobject : this->hitobjects) {
        if(hitobject->getStack() != 0) hitobject->updateStackPosition(stackOffset);
    }
}

void BeatmapInterface::computeDrainRate() {
    this->fDrainRate = 0.0;
    this->fHpMultiplierNormal = 1.0;
    this->fHpMultiplierComboEnd = 1.0;

    if(unlikely(this->hitobjects.empty()) || unlikely(!this->beatmap)) return;

    debugLog("Beatmap: Calculating drain ...");

    {
        // see https://github.com/ppy/osu-iPhone/blob/master/Classes/OsuPlayer.m
        // see calcHPDropRate() @ https://github.com/ppy/osu-iPhone/blob/master/Classes/OsuFiletype.m#L661

        // NOTE: all drain changes between 2014 and today have been fixed here (the link points to an old version of the
        // algorithm!) these changes include: passive spinner nerf (drain * 0.25 while spinner is active), and clamping
        // the object length drain to 0 + an extra check for that (see maxLongObjectDrop) see
        // https://osu.ppy.sh/home/changelog/stable40/20190513.2

        struct TestPlayer {
            TestPlayer(f64 hpBarMaximum) {
                this->hpBarMaximum = hpBarMaximum;

                this->hpMultiplierNormal = 1.0;
                this->hpMultiplierComboEnd = 1.0;

                this->resetHealth();
            }

            void resetHealth() {
                this->health = this->hpBarMaximum;
                this->healthUncapped = this->hpBarMaximum;
            }

            void increaseHealth(f64 amount) {
                this->healthUncapped += amount;
                this->health += amount;

                if(this->health > this->hpBarMaximum) this->health = this->hpBarMaximum;

                if(this->health < 0.0) this->health = 0.0;

                if(this->healthUncapped < 0.0) this->healthUncapped = 0.0;
            }

            void decreaseHealth(f64 amount) {
                this->health -= amount;

                if(this->health < 0.0) this->health = 0.0;

                if(this->health > this->hpBarMaximum) this->health = this->hpBarMaximum;

                this->healthUncapped -= amount;

                if(this->healthUncapped < 0.0) this->healthUncapped = 0.0;
            }

            f64 hpBarMaximum;

            f64 health;
            f64 healthUncapped;

            f64 hpMultiplierNormal;
            f64 hpMultiplierComboEnd;
        };
        TestPlayer testPlayer(200.0);

        const f64 HP = this->getHP();
        const int version = this->beatmap->getVersion();

        f64 testDrop = 0.05;

        const f64 lowestHpEver = GameRules::mapDifficultyRange(HP, 195.0, 160.0, 60.0);
        const f64 lowestHpComboEnd = GameRules::mapDifficultyRange(HP, 198.0, 170.0, 80.0);
        const f64 lowestHpEnd = GameRules::mapDifficultyRange(HP, 198.0, 180.0, 80.0);
        const f64 HpRecoveryAvailable = GameRules::mapDifficultyRange(HP, 8.0, 4.0, 0.0);

        bool fail = false;

        do {
            testPlayer.resetHealth();

            f64 lowestHp = testPlayer.health;
            int lastTime = (int)(this->hitobjects[0]->click_time - (i32)this->getApproachTime());
            fail = false;

            const int breakCount = this->breaks.size();
            int breakNumber = 0;

            int comboTooLowCount = 0;

            for(int i = 0; i < this->hitobjects.size(); i++) {
                const HitObject *h = this->hitobjects[i].get();
                const auto *sliderPointer = h->type == HitObjectType::SLIDER ? static_cast<const Slider *>(h) : nullptr;
                const auto *spinnerPointer =
                    h->type == HitObjectType::SPINNER ? static_cast<const Spinner *>(h) : nullptr;

                const int localLastTime = lastTime;

                int breakTime = 0;
                if(breakCount > 0 && breakNumber < breakCount) {
                    const DatabaseBeatmap::BREAK &e = this->breaks[breakNumber];
                    if(e.startTime >= localLastTime && e.endTime <= h->click_time) {
                        // consider break start equal to object end time for version 8+ since drain stops during this
                        // time
                        breakTime = (version < 8) ? (e.endTime - e.startTime) : (e.endTime - localLastTime);
                        breakNumber++;
                    }
                }

                testPlayer.decreaseHealth(testDrop * (h->click_time - lastTime - breakTime));

                lastTime = (int)(h->getEndTime());

                if(testPlayer.health < lowestHp) lowestHp = testPlayer.health;

                if(testPlayer.health > lowestHpEver) {
                    const f64 LongObjectDrop = testDrop * (f64)h->duration;
                    const f64 maxLongObjectDrop = std::max(0.0, LongObjectDrop - testPlayer.health);

                    testPlayer.decreaseHealth(LongObjectDrop);

                    // nested hitobjects
                    if(sliderPointer != nullptr) {
                        // startcircle
                        testPlayer.increaseHealth(LiveScore::getHealthIncrease(
                            LiveScore::HIT::HIT_SLIDER30, HP, testPlayer.hpMultiplierNormal,
                            testPlayer.hpMultiplierComboEnd, 1.0));  // slider30

                        // ticks + repeats + repeat ticks
                        const std::vector<Slider::SLIDERCLICK> &clicks = sliderPointer->getClicks();
                        for(const auto &click : clicks) {
                            switch(click.type) {
                                case 0:  // repeat
                                    testPlayer.increaseHealth(LiveScore::getHealthIncrease(
                                        LiveScore::HIT::HIT_SLIDER30, HP, testPlayer.hpMultiplierNormal,
                                        testPlayer.hpMultiplierComboEnd, 1.0));  // slider30
                                    break;
                                case 1:  // tick
                                    testPlayer.increaseHealth(LiveScore::getHealthIncrease(
                                        LiveScore::HIT::HIT_SLIDER10, HP, testPlayer.hpMultiplierNormal,
                                        testPlayer.hpMultiplierComboEnd, 1.0));  // slider10
                                    break;
                            }
                        }

                        // endcircle
                        testPlayer.increaseHealth(LiveScore::getHealthIncrease(
                            LiveScore::HIT::HIT_SLIDER30, HP, testPlayer.hpMultiplierNormal,
                            testPlayer.hpMultiplierComboEnd, 1.0));  // slider30
                    } else if(spinnerPointer != nullptr) {
                        const int rotationsNeeded =
                            (int)((f32)spinnerPointer->duration / 1000.0f * GameRules::getSpinnerSpinsPerSecond(this));
                        for(int r = 0; r < rotationsNeeded; r++) {
                            testPlayer.increaseHealth(LiveScore::getHealthIncrease(
                                LiveScore::HIT::HIT_SPINNERSPIN, HP, testPlayer.hpMultiplierNormal,
                                testPlayer.hpMultiplierComboEnd, 1.0));  // spinnerspin
                        }
                    }

                    if(!(maxLongObjectDrop > 0.0) || (testPlayer.health - maxLongObjectDrop) > lowestHpEver) {
                        // regular hit (for every hitobject)
                        testPlayer.increaseHealth(
                            LiveScore::getHealthIncrease(LiveScore::HIT::HIT_300, HP, testPlayer.hpMultiplierNormal,
                                                         testPlayer.hpMultiplierComboEnd, 1.0));  // 300

                        // end of combo (new combo starts at next hitobject)
                        if((i == this->hitobjects.size() - 1) || this->hitobjects[i]->is_end_of_combo) {
                            testPlayer.increaseHealth(LiveScore::getHealthIncrease(
                                LiveScore::HIT::HIT_300G, HP, testPlayer.hpMultiplierNormal,
                                testPlayer.hpMultiplierComboEnd, 1.0));  // geki

                            if(testPlayer.health < lowestHpComboEnd) {
                                if(++comboTooLowCount > 2) {
                                    testPlayer.hpMultiplierComboEnd *= 1.07;
                                    testPlayer.hpMultiplierNormal *= 1.03;
                                    fail = true;
                                    break;
                                }
                            }
                        }

                        continue;
                    }

                    fail = true;
                    testDrop *= 0.96;
                    break;
                }

                fail = true;
                testDrop *= 0.96;
                break;
            }

            if(!fail && testPlayer.health < lowestHpEnd) {
                fail = true;
                testDrop *= 0.94;
                testPlayer.hpMultiplierComboEnd *= 1.01;
                testPlayer.hpMultiplierNormal *= 1.01;
            }

            const f64 recovery = (testPlayer.healthUncapped - testPlayer.hpBarMaximum) / (f64)this->hitobjects.size();
            if(!fail && recovery < HpRecoveryAvailable) {
                fail = true;
                testDrop *= 0.96;
                testPlayer.hpMultiplierComboEnd *= 1.02;
                testPlayer.hpMultiplierNormal *= 1.01;
            }
        } while(fail);

        this->fDrainRate =
            (testDrop / testPlayer.hpBarMaximum) * 1000.0;  // from [0, 200] to [0, 1], and from ms to seconds
        this->fHpMultiplierComboEnd = testPlayer.hpMultiplierComboEnd;
        this->fHpMultiplierNormal = testPlayer.hpMultiplierNormal;
    }
}

f32 BeatmapInterface::getApproachTime() const {
    return cv::mod_mafham.getBool()
               ? this->getLength() * 2
               : GameRules::mapDifficultyRange(this->getAR(), GameRules::getMinApproachTime(),
                                               GameRules::getMidApproachTime(), GameRules::getMaxApproachTime());
}

f32 BeatmapInterface::getRawApproachTime() const {
    return cv::mod_mafham.getBool()
               ? this->getLength() * 2
               : GameRules::mapDifficultyRange(this->getRawAR(), GameRules::getMinApproachTime(),
                                               GameRules::getMidApproachTime(), GameRules::getMaxApproachTime());
}

bool BeatmapInterface::isActuallyPausedAndNotSpectating() const {
    // TODO(spec): need to actually test spectating to make sure i dont break something, so not including that here for now
    // this function is mainly to avoid running through replay frames while we are paused
    // (see while(next_frame.cur_music_pos <= this->iCurMusicPosWithOffsets) ... in update2())
    if(BanchoState::spectating) return false;

    return (this->isPaused() && ui->getPauseOverlay()->isVisible())  //
           && (this->music && !this->music->isPlaying())             //
           && !(this->bIsWaiting || this->isActuallyLoading());
}
