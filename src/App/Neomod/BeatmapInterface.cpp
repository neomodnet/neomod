// Copyright (c) 2015, PG, All rights reserved.
#include "BeatmapInterface.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <span>
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
#include "ConVarHandler.h"
#include "OsuConVars.h"
#include "Timing.h"
#include "BeatmapInstaller.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "DifficultyCalculator.h"
#include "Engine.h"
#include "GameRules.h"
#include "HUD.h"
#include "HitSounds.h"
#include "HitObjects.h"
#include "i18n.h"
#include "LegacyReplay.h"
#include "Logging.h"
#include "MapSettingsOverlay.h"
#include "ModFPoSu.h"
#include "ModSelector.h"
#include "Mouse.h"
#include "MusicTrack.h"
#include "NotificationOverlay.h"
#include "OptionsOverlay.h"
#include "Osu.h"
#include "PauseOverlay.h"
#include "ResourceManager.h"
#include "RichPresence.h"
#include "RoomScreen.h"
#include "GameplayJudge.h"
#include "Sound.h"
#include "Font.h"
#include "Shader.h"
#include "RenderTarget.h"
#include "Skin.h"
#include "SkinImage.h"
#include "SliderRenderer.h"
#include "AsyncPPCalculator.h"
#include "SoundEngine.h"
#include "SpectatorScreen.h"
#include "Storyboard.h"
#include "UI.h"
#include "VolumeOverlay.h"
#include "Matrices.h"
#include "UIModSelectorModButton.h"
#include "Graphics.h"
#include "URLHistory.h"

using namespace flags::operators;
using namespace neomod;

// the mods that came with what was being watched (a replay, a spectated player) go away with it: the player's own ones
// are back, or the room's if it was watched from inside of one
static void end_watched_mod_session() {
    if(BanchoState::is_in_a_multi_room()) {
        ui->getRoomScreen()->useRoomMods();
    } else {
        Replay::Mods::end_session();
    }
}

BeatmapInterface::BeatmapInterface(MusicTrack &music) : music(music), ppv2_calc(this) {
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

    this->beatmap = nullptr;

    this->iCurMusicPos = 0;
    this->iCurMusicPosWithOffsets = 0;
    this->bWasSeekFrame = false;
    this->bIsFirstMissSound = true;

    this->fFailAnim = 1.0f;

    this->fBreakBackgroundFade = 0.0f;
    this->bInBreak = false;
    this->iPreviousSectionPassFailTime = -1;

    this->bClickedContinue = false;
    this->iAllowAnyNextKeyUntilHitObjectIndex = 0;

    this->fScaleFactor = 1.0f;

    this->fXMultiplier = 1.0f;
    this->fNumberScale = 1.0f;
    this->fHitcircleOverlapScale = 1.0f;
    this->fRawHitcircleDiameter = 27.35f * 2.0f;
    this->fSliderFollowCircleDiameter = 0.0f;

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

BeatmapInterface::~BeatmapInterface() { this->unloadObjects(); }

void BeatmapInterface::drawDebug() {
    if(!this->beatmap) return;
    static constexpr Color shadowColor = argb(255, 0, 0, 0);
    static constexpr Color textColor = argb(255, 255, 232, 255);

    if(!this->judge) return;
    const auto &alltp = this->judge->getMapTimingPoints();
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

    for(const DBType::TIMINGPOINT &t : alltp) {
        // TODO: draw current TIMING_INFO in green (not timingpoint)
        // next to (to the right) the closest timingpoint with an offset < (this->iCurMusicPos + cv::timingpoints_offset.getInt())

        const std::string curtpString = fmt::format("{},{},{},{},{},{},{}", (i32)t.offset, t.msPerBeat, t.sampleSet,
                                                    t.sampleIndex, t.volume, (i32)t.uninherited, (i32)t.kiai);

        g->drawString(debugFont, curtpString, TextFX{.col_text = textColor, .col_shadow = shadowColor, .offs_px = 1.f});

        // spacing for next
        g->translate(0, dbgfontheight);
    }
    g->popTransform();
}

void BeatmapInterface::drawBackground() {
    if(!this->canDraw()) return;

    const Storyboard::View view = this->getStoryboardView();
    const f32 brightness = view.brightness;

    // draw background image
    {
        const Storyboard *storyboard = this->bIsPreLoading ? nullptr : this->storyboard.get();

        const Image *backgroundImage = osu->getBackgroundImageHandler()->getLoadBackgroundImage(
            this->beatmap, false, cv::draw_menu_background.getBool());

        bool drawBackgroundImage = backgroundImage && backgroundImage->isReady();
        if(brightness == 0.f) drawBackgroundImage = false;
        if(storyboard && storyboard->replacesBackground()) drawBackgroundImage = false;
        if(!storyboard && !cv::draw_beatmap_background_image.getBool()) drawBackgroundImage = false;

        vec2 offset{0.f};
        if(storyboard) offset = storyboard->getBackgroundOffset() * (view.screen.getHeight() / 480.f);

        if(drawBackgroundImage) {
            const float scale = Osu::getImageScaleToFillResolution(backgroundImage, osu->getVirtScreenSize());
            const vec2 centerTrans = (osu->getVirtScreenSize() / 2.0f) + offset;
            const auto alpha = cv::mod_fposu.getBool() ? cv::background_alpha.getFloat() : 1.0f;

            g->setColor(argb(alpha, brightness, brightness, brightness));
            g->pushTransform();
            {
                g->scale(scale, scale);
                g->translate((int)centerTrans.x, (int)centerTrans.y);
                if(storyboard) view.transform();
                g->drawImage(backgroundImage);
            }
            g->popTransform();
        }

        if(cv::background_brightness.getFloat() > 0.0f) {
            const f32 brightness = cv::background_brightness.getFloat();

            const Channel red = std::clamp<float>(brightness * cv::background_color_r.getFloat(), 0.0f, 255.0f);
            const Channel green = std::clamp<float>(brightness * cv::background_color_g.getFloat(), 0.0f, 255.0f);
            const Channel blue = std::clamp<float>(brightness * cv::background_color_b.getFloat(), 0.0f, 255.0f);
            const Channel alpha = 255 * (1.0f - this->fBreakBackgroundFade) *
                                  (cv::mod_fposu.getBool() ? cv::background_alpha.getFloat() : 1.0f);

            g->setColor(argb(alpha, red, green, blue));
            g->fillRect(0, 0, osu->getVirtScreenWidth(), osu->getVirtScreenHeight());
        }
    }

    if(this->storyboard) {
        this->storyboard->draw(Storyboard::Layer::BACKGROUND, Storyboard::Layer::FOREGROUND, view);
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

        if(this->judge && !this->judge->getHitObjectsSortedByEndTime().empty()) {
            const HitObject *lastHitObject = this->judge->getHitObjectsSortedByEndTime().back();
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

    const i32 nextHitObjectTime = this->judge->getNextHitObjectTime();
    const i32 nextHitObjectDelta = nextHitObjectTime - (i32)this->iCurMusicPosWithOffsets;

    if(this->isInOutro()) {
        // (past its end, the play ends)
        this->music.setPosition(*this->getStoryboardEnd() + 1);
        this->bWasSeekFrame = true;
    } else if(!cv::end_skip.getBool() && nextHitObjectDelta < 0) {
        this->music.setPosition(std::max(this->music.getLengthMS(), (u32)1) - 1);
        this->bWasSeekFrame = true;
    } else {
        this->music.setPosition(std::max(nextHitObjectTime - (i32)(offset * offsetMultiplier), (i32)0));
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

    bool should_click = (!osu->getModAuto() && !osu->getModRelax()) || !cv::auto_and_relax_block_user_input.getBool();

    // music position to be interped to next update (in update2())
    Click click{
        .timestampNS = timestamp, .cursorPos = this->getCursorPos(), .musicPosMS = this->iCurMusicPosWithOffsets};

    if(down) {  // pressed
        // this needs to be immediately updated here, to update state outside of early returns
        this->current_keys |= key_flag;

        // allow held keys to change while paused, but don't animate or anything
        // to reiterate, this needs to happen because we have no separate outside-gameplay-held-keys state (anymore),
        // BeatmapInterface::current_keys is the only source of truth
        if(!osu->isInPlayMode() || this->bIsPaused) return;

        if(this->bContinueScheduled) {
            // don't insta-unpause if we had a held key or doubleclicked or something
            if(engine->getTime() < this->fPrevUnpauseTime + cv::unpause_continue_delay.getFloat()) {
                return;
            }

            // sanity check: if we changed resolutions after pausing, the old continue cursor point might be offscreen
            // so re-clamp it
            this->vContinueCursorPoint =
                vec::clamp(this->vContinueCursorPoint, vec2{10.f, 10.f}, osu->getVirtScreenSize() - vec2{10.f, 10.f});

            this->bClickedContinue = !ui->getModSelector()->isMouseInside() && this->isCursorOnContinuePoint();
            if(!this->bClickedContinue) {
                return;
            }
        }

        if(cv::mod_singletap.getBool() && !(this->lastPressedKey & key_flag)) {
            if(this->getCurrentHitObjectIndex() > this->iAllowAnyNextKeyUntilHitObjectIndex) {
                soundEngine->play(this->getSkin()->s_combobreak);
                return;
            }
        }

        if(cv::mod_fullalternate.getBool() && (this->lastPressedKey & key_flag)) {
            if(this->getCurrentHitObjectIndex() > this->iAllowAnyNextKeyUntilHitObjectIndex) {
                return;
            }
        }

        // key overlay & counter
        ui->getHUD()->animateInputOverlay(key_flag, true);

        if(this->judge && this->judge->hasFailed()) return;

        this->lastPressedKey = key_flag;
        this->key_events.push_back(
            KeyEvent{.click = click, .keys = this->current_keys, .clickKey = should_click ? (u8)key_flag : (u8)0});
    } else {  // released
        bool clicks = false;
        if(osu->getModDKS()) {
            this->lastPressedKey = key_flag;
            clicks = should_click;
        }

        // always allow released key to animate
        ui->getHUD()->animateInputOverlay(key_flag, false);
        this->current_keys &= ~key_flag;
        if(osu->isInPlayMode()) {
            this->key_events.push_back(
                KeyEvent{.click = click, .keys = this->current_keys, .clickKey = clicks ? (u8)key_flag : (u8)0});
        }
    }
}

void BeatmapInterface::selectBeatmap() {
    // sanity
    osu->bIsPlayingASelectedBeatmap = false;

    this->selectBeatmap(this->beatmap);
}

void BeatmapInterface::selectBeatmap(DatabaseBeatmap *map) {
    AsyncPPC::set_map(map);

    if(map != nullptr) {
        this->beatmap = map;

        // (the judge plays with the score's mods, so it also answers for the map outside of a play)
        if(!this->judge || this->judge->getBeatmap() != map)
            this->judge = std::make_unique<GameplayJudge>(map, *osu->getScore(), this, this);
    }

    if(cv::beatmap_preview_mods_live.getBool()) {
        this->onModUpdate();
    } else {
        this->invalidateWholeMapPPInfo();  // onModUpdate already calls this
        this->setMusicSpeed(this->getSpeedMultiplier());
    }
}

void BeatmapInterface::deselectBeatmap() {
    this->beatmap = nullptr;
    this->unloadObjects();
    this->storyboardMap = nullptr;
    this->storyboardHandle = {};
    this->storyboard.reset();
    this->judge.reset();
}

bool BeatmapInterface::play() {
    this->is_watching = false;

    if(this->start()) {
        ui->getMapSettingsOverlay()->onPlayStart();
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
    if(score.replay.size() < 3) {
        // Replay is invalid
        return false;
    }

    this->bIsPlaying = false;
    this->bIsPaused = false;
    this->bContinueScheduled = false;
    this->unloadObjects();

    osu->watched_user_name = score.playerName;
    osu->watched_user_id = score.player_id;
    this->replay_data = score;
    this->is_watching = true;

    // the replay's mods are only for watching it (see stop())
    Replay::Mods::begin_session(true);
    Replay::Mods::use(score.mods, true);

    if(!this->start()) {
        // Map failed to load
        end_watched_mod_session();
        this->is_watching = false;
        return false;
    }

    this->judge->replay = score.replay;

    // judge the whole replay up front, without sounds or effects
    {
        LiveScore simScore(true);
        simScore.mods = osu->getScore()->mods;
        GameplayJudge simJudge(this->beatmap, simScore);
        this->replay_hitresults.clear();
        if(const auto err = simJudge.load()) {
            debugLog("failed to judge replay ahead: {}", err.error_string());
        } else {
            simJudge.replay = score.replay;
            simJudge.simulate_to(std::numeric_limits<i32>::max());
            this->replay_hitresults = std::move(simScore.hitresults);
        }
    }

    // Don't seek to beginning, since that would skip waiting time
    if(start_ms > 0) {
        this->seekMS(start_ms);
    }

    // update url history
    if(i64 score_id = score.bancho_score_id; score_id != 0) {
        Mc::URLHistory::replaceState(fmt::format("/scores/osu/{:d}", score_id).c_str());
    } else {
        Mc::URLHistory::replaceState(Osu::isBleedingEdge() ? "/online/bleedingedge/" : "/online/");
    }

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

    FinishedScore score;
    score.client = "peppy-unknown";
    score.server = BanchoState::endpoint;
    score.mods = Replay::Mods::from_legacy(user_info->mods);

    // the spectated player's mods are only for watching them play (see stop())
    Replay::Mods::begin_session(true);
    Replay::Mods::use(score.mods, true);

    if(!this->start()) {
        // Map failed to load
        end_watched_mod_session();
        return false;
    }

    this->judge->replay.clear();
    this->score_frames.clear();

    return true;
}

bool BeatmapInterface::start() {
    // set it to false to catch early returns first
    osu->bIsPlayingASelectedBeatmap = false;

    if(unlikely(!this->beatmap || !this->judge)) return false;

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

    // the map's settings first: some of them decide what gets loaded (its skin, its storyboard)
    this->applyMapSettings();

    // actually load the difficulty (and the hitobjects)
    {
        const Primitives::LoadError error = this->judge->load();
        if(error.errc) {
            using enum Primitives::LoadError::code;
            std::string errorMessage;
            switch(error.errc) {
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
                    if(error.errc != LOAD_INTERRUPTED) errorMessage = "Error: Couldn't load beatmap :(";
                    debugLog("Osu Error: Couldn't load beatmap {}: {}", this->beatmap->getFilePath(),
                             error.error_string());
                } break;
            }

            if(!errorMessage.empty()) {
                ui->getNotificationOverlay()->addToast(errorMessage, ERROR_TOAST);
            }

            cvars().clearLayer(CvarEditor::CONTEXT);
            osu->bIsPlayingASelectedBeatmap = false;
            osu->setShouldPauseBGThreads(false);

            return false;
        }

        // the file changed since the database read it: the database catches up after the play (osu!.db's maps once
        // osu!stable rescans, a raw-loaded songs folder's at the next F5)
        if(this->judge->getMapData()->md5 != this->beatmap->getMD5() &&
           this->beatmap->type == DatabaseBeatmap::BeatmapType::NEOMOD_DIFFICULTY) {
            osu->getBeatmapInstaller()->enqueue_folder(db->locate(*this->beatmap).folder, /*changed=*/true);
        }

        this->cur_timing_info = {};

        this->loadBeatmapSkin();
        this->loadStoryboard();
    }

    // start preloading (delays the play start until it's set to false, see isLoading())
    this->bIsPreLoading = true;
    this->iPreLoadingIndex = 0;

    // live pp/stars
    this->resetLiveStarsTasks();

    // load music
    if(cv::restart_sound_engine_before_playing.getBool()) {
        // HACKHACK: Reload sound engine before starting the song, as it starts lagging after a while
        //           (i haven't figured out the root cause yet)
        this->music.pause();
        soundEngine->restart();

        // Restarting sound engine already reloads the music
    } else {
        // need to reload in case of speed/pitch changes (just to be sure)
        this->music.load(this->beatmap, false, true);
    }

    // the lead-in before the song and the time after its end
    this->music.setVirtualTime(true);
    this->music.setLoop(false);
    this->spectate_pause = false;
    this->bIsPaused = false;
    this->bContinueScheduled = false;

    this->bInBreak = cv::background_fade_after_load.getBool();
    this->fBreakBackgroundFade.stop();
    this->fBreakBackgroundFade = cv::background_fade_after_load.getBool() ? 1.0f : 0.0f;
    this->iPreviousSectionPassFailTime = -1;
    this->fShouldFlashSectionPass = 0.0f;
    this->fShouldFlashSectionFail = 0.0f;

    this->seekToStart(false);

    // we are waiting for an asynchronous start of the beatmap in the next update()
    // (from the beginning, even if the last play was quit while waiting for a quick restart)
    this->bIsPlaying = true;
    this->bIsWaiting = true;
    this->bLeadInStarted = false;
    this->iLeadInMS = 0;
    this->bIsRestartScheduled = false;
    this->bIsRestartScheduledQuick = false;
    this->bIsInSkippableSection = false;
    this->bSkipScheduled = false;
    this->bClickedSkipButton = false;

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

    // (nothing has started yet while loading, so there is nothing to restart)
    if(!this->bIsWaiting || !this->isLoading()) {
        this->bIsRestartScheduled = true;
        this->bIsRestartScheduledQuick = quick;
    } else if(this->bIsPaused && !BanchoState::spectating) {
        this->pause(false);
    }
}

void BeatmapInterface::actualRestart() {
    // reset everything
    this->resetScore();
    this->judge->reset(-1000);
    ui->getHUD()->resetHitErrorBar();
    this->smoke_trail.clear();
    this->all_clicks.clear();

    // we are waiting for an asynchronous start of the beatmap in the next update()
    this->bIsWaiting = true;
    this->bLeadInStarted = false;
    this->iLeadInMS = 0;

    // pause temporarily if playing
    if(this->music.isPlaying()) this->music.pause();

    // reset/restore frequency (from potential fail before)
    this->music.endSlowdown();

    this->music.setLoop(false);
    this->bIsPaused = false;
    this->bContinueScheduled = false;

    this->bInBreak = false;
    this->fBreakBackgroundFade.stop();
    this->fBreakBackgroundFade = 0.0f;
    this->iPreviousSectionPassFailTime = -1;
    this->fShouldFlashSectionPass = 0.0f;
    this->fShouldFlashSectionFail = 0.0f;

    this->onModUpdate();  // sanity

    // reset position
    this->seekToStart(std::exchange(this->bIsRestartScheduledQuick, false));
    this->bWasSeekFrame = true;

    this->bIsPlaying = true;
}

void BeatmapInterface::seekToStart(bool quick) {
    // a quick restart starts just before the first hitobject (even if there is a long waiting period at the beginning
    // with nothing etc.); from the song's start, a first hitobject that starts immediately gets early_note_time of
    // lead-in
    const auto hitobjects = this->judge->getHitObjects();
    i32 start_ms = 0;
    if(quick && likely(!hitobjects.empty())) {
        const i32 retry_time = std::max(0, cv::quick_retry_time.getInt());
        start_ms = std::max(hitobjects[0]->getClickTime() - retry_time, 0);
    }
    if(start_ms == 0 && likely(!hitobjects.empty()) && hitobjects[0]->getClickTime() < cv::early_note_time.getInt()) {
        start_ms = -(i32)(cv::early_note_time.getFloat() * this->getSpeedMultiplier());
    }
    // a storyboard starting before that gets its lead-in (not on quick restarts)
    if(const auto span = this->storyboard ? this->storyboard->getTimeSpan() : std::nullopt; span && !quick) {
        const i32 offset = this->music.getOffset(this->beatmap, this->judge->getMapVersion());
        start_ms = std::min(start_ms, (i32)std::floor(span->first) - offset);
    }
    this->music.setPosition(start_ms);
    this->iCurMusicPos = start_ms;
}

std::optional<i32> BeatmapInterface::getStoryboardEnd() const {
    if(!this->storyboard || this->judge->hasFailed() || BanchoState::is_playing_a_multi_map()) return std::nullopt;
    const auto span = this->storyboard->getTimeSpan();
    if(!span) return std::nullopt;
    return (i32)std::ceil(span->second) - this->music.getOffset(this->beatmap, this->judge->getMapVersion());
}

bool BeatmapInterface::isInOutro() const {
    const auto end = this->getStoryboardEnd();
    const auto sortedByEndTime = this->judge->getHitObjectsSortedByEndTime();
    return end && !sortedByEndTime.empty() && this->iCurMusicPos > sortedByEndTime.back()->getEndTime() &&
           *end - this->iCurMusicPos > 3000;
}

void BeatmapInterface::pause(bool quitIfWaiting) {
    if(unlikely(!this->beatmap)) return;
    if(cv::mod_no_pausing.getBool()) return;
    if(BanchoState::spectating) {
        Spectating::stop();
        return;
    }

    const bool isFirstPause = !this->bContinueScheduled;

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
            const auto sortedByEndTime = this->judge->getHitObjectsSortedByEndTime();
            const HitObject *lastHitObject = !sortedByEndTime.empty() ? sortedByEndTime.back() : nullptr;
            if(lastHitObject != nullptr && lastHitObject->isFinished() &&
               (this->iCurMusicPos > lastHitObject->getEndTime() + (i32)cv::end_skip_time.getInt()) &&
               cv::end_skip.getBool()) {
                this->stop(false);
            } else {
                this->music.pause();
                this->bIsPlaying = false;
                this->bIsPaused = true;
            }
        }
    } else if(this->bIsPaused && !this->bContinueScheduled) {
        // if this is the first time unpausing
        if(osu->getModAuto() || osu->getModAutopilot() || this->bIsInSkippableSection || this->is_watching) {
            // (also during the lead-in, which runs on the music's virtual time, but before it the music waits for it)
            if(this->bLeadInStarted || !this->bIsWaiting) this->music.play();

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

        // clamp to actual game rect (but a bit smaller)
        this->vContinueCursorPoint =
            vec::clamp(this->getMousePos(), vec2{10.f, 10.f}, osu->getVirtScreenSize() - vec2{10.f, 10.f});
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
    if(this->judge->hasFailed()) this->fFailAnim.stop();
}

void BeatmapInterface::stop(bool quit) {
    osu->bIsPlayingASelectedBeatmap = false;
    this->music.setVirtualTime(false);

    soundEngine->stop(this->getSkin()->s_fail);

    if(unlikely(!this->beatmap)) {
        osu->setShouldPauseBGThreads(false);
        return;
    }

    this->bIsPlaying = false;
    this->bIsPaused = false;
    this->bContinueScheduled = false;

    FinishedScore score;
    if(this->is_watching) {
        score = this->replay_data;
    } else if(BanchoState::is_playing_a_multi_map()) {
        score = ui->getRoomScreen()->get_approximate_score();
    } else {
        // Call this BEFORE unloadObjects()!
        score = this->saveAndSubmitScore(quit);
    }

    // Auto mod was "temporary" since it was set from Ctrl+Clicking a map, not from the mod selector
    if(osu->bModAutoTemp) {
        auto *btnAuto = ui->getModSelector()->getGridButton(ModSelector::AUTO_POS);
        if(btnAuto && btnAuto->isOn()) {
            btnAuto->click();
        }
        osu->bModAutoTemp = false;
    }

    // (outside of a room there is nothing else that a session could be for, so whatever is left of one ends here as
    // well: a disconnect while spectating doesn't get here as "spectating" anymore)
    if(this->is_watching || BanchoState::spectating || !BanchoState::is_in_a_multi_room()) {
        end_watched_mod_session();
    }

    this->is_watching = false;
    this->is_buffering = false;
    this->judge->replay.clear();
    this->score_frames.clear();

    if(this->judge->hasFailed()) this->music.endSlowdown();

    this->unloadObjects();
    cvars().clearLayer(CvarEditor::CONTEXT);

    if(BanchoState::is_playing_a_multi_map()) {
        if(quit && !Osu::isKioskMode()) {
            osu->onPlayEnd(score, true);
            ui->getRoomScreen()->ragequit();
        } else {
            ui->getRoomScreen()->onClientScoreChange(true);
            Packet packet;
            packet.id = OUTP_FINISH_MATCH;
            BANCHO::Net::send_packet(packet);
        }
    } else {
        osu->onPlayEnd(score, quit);
    }

    osu->setShouldPauseBGThreads(false);

    // reset url to default
    Mc::URLHistory::replaceState(Osu::isBleedingEdge() ? "/online/bleedingedge/" : "/online/");
}

void BeatmapInterface::fail(bool force_death) {
    if(!this->judge || this->judge->hasFailed()) return;
    if(this->failPlay(force_death)) this->judge->markFailed();
}

bool BeatmapInterface::onFail(bool byMods) {
    // perfect restarts instead of failing, and so can sudden death
    if(byMods && !this->is_watching && !BanchoState::spectating &&
       (osu->getModSS() || cv::mod_suddendeath_restart.getBool())) {
        this->restart();
        return false;
    }

    return this->failPlay(false);
}

bool BeatmapInterface::failPlay(bool force_death) {
    if((this->is_watching || BanchoState::spectating) && !force_death) return false;

    // Change behavior of relax mod when online
    if(BanchoState::is_online() && osu->getModRelax()) return false;

    bool failed = false;
    if(!BanchoState::is_playing_a_multi_map() && cv::drain_kill.getBool()) {
        soundEngine->play(this->getSkin()->s_fail);

        // trigger music slowdown and delayed menu, see update()
        failed = true;
        this->fFailAnim = 1.0f;
        this->fFailAnim.set(0.0f, cv::fail_time.getFloat(), anim::Linear);
    } else if(!osu->getScore()->isDead()) {
        debugLog("Disabling score submission due to death");
        this->is_submittable = false;

        this->judge->setHealth(0.0);

        // Send a score update with health = 0 so server knows we died
        ui->getRoomScreen()->onClientScoreChange(true);

        // Only sent on first death, since there is no packet for reviving
        if(BanchoState::is_playing_a_multi_map() && !osu->getScore()->hasDied()) {
            Packet packet;
            packet.id = OUTP_MATCH_FAILED;
            BANCHO::Net::send_packet(packet);
        }

        if(cv::drain_kill_notification_duration.getFloat() > 0.0f) {
            if(!osu->getScore()->hasDied())
                ui->getNotificationOverlay()->addNotification("You have failed, but you can keep playing!", 0xffffffff,
                                                              false, cv::drain_kill_notification_duration.getFloat());
        }
    }

    if(!osu->getScore()->isDead()) osu->getScore()->setDead(true);

    return failed;
}

void BeatmapInterface::cancelFailing() {
    if(!this->judge || !this->judge->hasFailed() || this->fFailAnim <= 0.0f) return;

    this->judge->cancelFailing();

    this->fFailAnim.stop();
    this->fFailAnim = 1.0f;

    this->music.endSlowdown();

    soundEngine->stop(this->getSkin()->s_fail);
}

void BeatmapInterface::setMusicSpeed(f32 speed) {
    // (outside of play only with beatmap_preview_mods_live)
    const bool modded = osu->isInPlayMode() || cv::beatmap_preview_mods_live.getBool();
    this->music.setRate(modded ? speed : 1.f, modded ? this->getPitchMultiplier() : 1.f,
                        cv::snd_speed_compensate_pitch.getBool());
}

void BeatmapInterface::seekMS(u32 ms) {
    if(unlikely(!this->beatmap) || this->music.isEmpty() || this->judge->hasFailed()) return;

    // this->resetScore() resets this->is_submittable
    bool was_submittable = this->is_submittable;

    this->bWasSeekFrame = true;

    this->music.setPosition(ms);
    this->setMusicSpeed(this->getSpeedMultiplier());

    const bool backwards = std::cmp_less(ms, this->iCurMusicPos);
    this->resetScore();
    this->is_submittable = false;
    ui->getHUD()->resetHitErrorBar();

    this->iPreviousSectionPassFailTime = -1;

    if(this->bIsWaiting) {
        this->bIsWaiting = false;
        this->bIsPlaying = true;

        this->music.play();

        // if there are calculations in there that need the hitobjects to be loaded, also applies speed/pitch
        this->onModUpdate(false, false);
    }

    if(this->is_watching || BanchoState::spectating) {
        // a replay is judged up to where it was seeked to (from the start again when seeking backwards), without
        // showing every hit on the way
        if(backwards) this->judge->reset();
        osu->getScore()->simulating = true;
        this->judge->simulate_to((i32)ms);
        osu->getScore()->simulating = false;
    } else {
        // the play goes on from there, with everything before it done
        this->judge->reset((i32)ms);
    }

    if(!this->is_watching && !BanchoState::spectating) {  // score submission already disabled when watching replay
        if(was_submittable && BanchoState::can_submit_scores()) {
            ui->getNotificationOverlay()->addToast("Score will not submit due to seeking", ERROR_TOAST);
        }
        this->judge->bTempSeekNF = true;
    }

    // clear outgoing spectator frames so that neomod clients are able to tell when we seek backwards
    // (since frame order isn't kept within a frame bundle, but packet order should remain the same)
    if(!this->frame_batch.empty()) {
        this->broadcast_spectator_frames();
    }
}

const std::vector<LiveScore::TimedHitResult> &BeatmapInterface::getTimelineHitresults() const {
    return this->is_watching ? this->replay_hitresults : osu->getScore()->hitresults;
}

u32 BeatmapInterface::getTime() const { return (u32)std::max(this->music.getTime(), 0); }

u32 BeatmapInterface::getStartTimePlayable() const {
    if(this->judge && !this->judge->getHitObjects().empty())
        return (u32)this->judge->getHitObjects()[0]->getClickTime();
    else
        return 0;
}

u32 BeatmapInterface::getLength() const {
    if(this->music.isReady())
        return this->music.getLengthMS();
    else if(likely(!!this->beatmap))
        return this->beatmap->getLengthMS();
    else
        return 0;
}

u32 BeatmapInterface::getLengthPlayable() const {
    if(this->judge && !this->judge->getHitObjects().empty())
        return this->judge->getLengthPlayable();
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
        // (through the lead-in)
        if(this->iLeadInMS <= 0) return 0.f;
        return std::clamp(1.f + (f32)this->iCurMusicPos / (f32)this->iLeadInMS, 0.f, 1.f);
    } else {
        f32 length_playable = this->getLengthPlayable();
        if(length_playable <= 0.f) return 0.f;

        f32 time_since_first_object = (f32)this->getTime() - (f32)this->getStartTimePlayable();
        return std::clamp(time_since_first_object / length_playable, 0.f, 1.f);
    }
}

int BeatmapInterface::getMostCommonBPM() const {
    if(likely(!!this->beatmap)) {
        if(!this->music.isEmpty())
            return (int)(this->beatmap->getMostCommonBPM() * this->music.getSpeed());
        else
            return (int)(this->beatmap->getMostCommonBPM() * this->getSpeedMultiplier());
    } else
        return 0;
}

f32 BeatmapInterface::getSpeedMultiplier() const { return this->judge ? this->judge->getSpeedMultiplier() : 1.f; }
f32 BeatmapInterface::getPitchMultiplier() const { return this->judge ? this->judge->getPitchMultiplier() : 1.f; }

// currently just a passthrough for the main skin, might return beatmap skins in the future
const Skin *BeatmapInterface::getSkin() const { return osu->getSkin(); }

const Replay::Mods &BeatmapInterface::getMods() const { return osu->getScore()->mods; }
Skin *BeatmapInterface::getSkinMutable() { return osu->getSkinMutable(); }

ModFlags BeatmapInterface::getModFlags() const {
    // the experimental mods that change the look are only convars, not part of the play's mods
    ModFlags flags = this->getMods().flags;
    if(cv::mod_strict_tracking.getBool()) flags |= ModFlags::StrictTracking;
    if(cv::mod_approach_different.getBool()) flags |= ModFlags::ApproachDifferent;
    if(cv::mod_fps.getBool()) flags |= ModFlags::FPS;
    return flags;
}

Color BeatmapInterface::getComboColor(i32 colorCounter, i32 colorOffset) const {
    static const std::vector<Color> none;
    return this->getSkin()->getComboColorForCounter(colorCounter, colorOffset,
                                                    this->judge ? this->judge->getComboColors() : none);
}

bool BeatmapInterface::slidersRenderDynamically() const { return osu->slidersRenderDynamically(); }

vec2 BeatmapInterface::getScreenSize() const { return osu->getVirtScreenSize(); }

f32 BeatmapInterface::getRawAR() const { return this->judge ? this->judge->getRawAR() : 5.f; }
f32 BeatmapInterface::getAR() const { return this->judge ? this->judge->getAR() : 5.f; }
f32 BeatmapInterface::getCS() const { return this->judge ? this->judge->getCS() : 5.f; }
f32 BeatmapInterface::getHP() const { return this->judge ? this->judge->getHP() : 5.f; }
f32 BeatmapInterface::getRawOD() const { return this->judge ? this->judge->getRawOD() : 5.f; }
f32 BeatmapInterface::getOD() const { return this->judge ? this->judge->getOD() : 5.f; }
f32 BeatmapInterface::getApproachTime() const { return this->judge ? this->judge->getApproachTime() : 0.f; }
f64 BeatmapInterface::getHitWindow300() const { return this->judge ? this->judge->getHitWindow300() : 0.0; }
f64 BeatmapInterface::getHitWindow100() const { return this->judge ? this->judge->getHitWindow100() : 0.0; }
f64 BeatmapInterface::getHitWindow50() const { return this->judge ? this->judge->getHitWindow50() : 0.0; }
f32 BeatmapInterface::getApproachRateForSpeedMultiplier() const {
    return this->judge ? this->judge->getApproachRateForSpeedMultiplier() : 5.f;
}
f32 BeatmapInterface::getRawARForSpeedMultiplier() const {
    return this->judge ? this->judge->getRawARForSpeedMultiplier() : 5.f;
}
f32 BeatmapInterface::getRawODForSpeedMultiplier() const {
    return this->judge ? this->judge->getRawODForSpeedMultiplier() : 5.f;
}
f32 BeatmapInterface::getOverallDifficultyForSpeedMultiplier() const {
    return this->judge ? this->judge->getOverallDifficultyForSpeedMultiplier() : 5.f;
}
i32 BeatmapInterface::getCurrentHitObjectIndex() const {
    return this->judge ? this->judge->getCurrentHitObjectIndex() : 0;
}
bool BeatmapInterface::isSpinnerActive() const { return this->judge && this->judge->isSpinnerActive(); }
f64 BeatmapInterface::getHealth() const { return this->judge ? this->judge->getHealth() : 1.0; }
bool BeatmapInterface::hasFailed() const { return this->judge && this->judge->hasFailed(); }

void BeatmapInterface::onComboBreak() {
    if((this->bIsFirstMissSound && osu->getScore()->getCombo() > 0) ||
       osu->getScore()->getCombo() > cv::combobreak_sound_combo.getInt()) {
        this->bIsFirstMissSound = false;
        soundEngine->play(this->getSkin()->s_combobreak);
    }
}

void BeatmapInterface::onHealthGain(f64 healthBefore) {
    ui->getHUD()->animateKiBulge();
    if(healthBefore > 0.9) ui->getHUD()->animateKiExplode();
}

namespace {
// a sound's panning from where it happens on the playfield as drawn (mirrors, rotation, mods), as played
f32 soundPanAt(const BeatmapInterface &play, vec2 rawPos) {
    return HitSoundUtils::playedPan(GameRules::osuCoords2Pan(play.pixels2OsuCoords(play.osuCoords2Pixels(rawPos)).x));
}

// the context of a hitsound at `timeMS` (-1: now)
HitSoundUtils::HitSoundContext hitSoundContext(const BeatmapInterface &play, const Skin &skin, i32 timeMS) {
    const GameplayJudge *judge = play.getJudge();
    return HitSoundUtils::makeContext(
        (timeMS != -1 && judge) ? HitSoundUtils::samplesAt(judge->getMapTimingPoints(), timeMS)
                                : play.getCurrentTimingInfo(),
        judge ? judge->getDefaultSampleSet() : 1, skin.o_layered_hitsounds, play.getStoryboard() != nullptr);
}

// spinnerspin and spinnerbonus follow the sample volume (or the current timing point volume), without hitsound boost
f32 spinnerSoundVolume(const BeatmapInterface &play, const Skin &skin, DatabaseBeatmapTypes::HITSAMPLE_BITS samples) {
    const auto ctx = hitSoundContext(play, skin, -1);
    if(ctx.ignoreSampleVolume) return 1.f;
    return (f32)(samples.volume > 0 ? samples.volume : ctx.timingPointVolume) / 100.f;
}
}  // namespace

void BeatmapInterface::playHitSound(DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos, i32 delta, i32 timeMS) {
    // Don't play hitsounds when seeking
    const Skin *skin = this->getSkin();
    if(this->bWasSeekFrame || !skin) return;

    f32 pitch = 0.f;
    if(cv::snd_pitch_hitsounds.getBool()) {
        // don't change pitch for 300s if delta is within 300 hitwindow
        // (see AbstractBeatmapInterface.cpp for weird math justification)
        if(!cv::snd_pitch_hitsounds_ignore_300s.getBool() ||
           (f32)std::abs(delta) >= (std::floor(this->getHitWindow300()) - 0.5f)) {
            pitch = (f32)delta / this->getHitWindow100() * cv::snd_pitch_hitsounds_factor.getFloat();
        }
    }

    const auto sounds = HitSoundUtils::resolve(samples, hitSoundContext(*this, *skin, timeMS), false);
    HitSoundUtils::play(*skin, sounds, soundPanAt(*this, rawPos), pitch);

    if(this->storyboard && !sounds.empty()) {
        Storyboard::HitSound played{
            .normalSet = sounds[0].set, .additionSet = sounds[0].set, .additions = 0, .index = sounds[0].index};
        for(const auto &sound : sounds) {
            if(sound.hit == 0) {
                played.normalSet = sound.set;
            } else {
                played.additionSet = sound.set;
                played.additions |= (u8)(1 << (sound.hit - 1));
            }
        }
        this->storyboard->onHitSound(played, this->iCurMusicPosWithOffsets);
        this->storyboard->onHitObjectHit(this->iCurMusicPosWithOffsets);
    }
}

void BeatmapInterface::playSliderTickSound(DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos, i32 timeMS) {
    const Skin *skin = this->getSkin();
    if(!skin) return;
    HitSoundUtils::playSliderTick(*skin,
                                  HitSoundUtils::resolveSliderTick(samples, hitSoundContext(*this, *skin, timeMS)),
                                  soundPanAt(*this, rawPos));
}

std::vector<HitSoundUtils::Set_Slider_Hit> BeatmapInterface::updateSliderSlideSounds(
    bool sliding, DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos,
    const std::vector<HitSoundUtils::Set_Slider_Hit> &started) {
    const Skin *skin = this->getSkin();
    if(!skin) return {};
    if(sliding && !this->bWasSeekFrame) {
        return HitSoundUtils::play(*skin, HitSoundUtils::resolve(samples, hitSoundContext(*this, *skin, -1), true),
                                   soundPanAt(*this, rawPos), 0.f);
    }

    // debugLog("not sliding, stopping");
    if(!started.empty()) HitSoundUtils::stopSliderSounds(*skin, started);
    return {};
}

void BeatmapInterface::stopSliderSounds(const std::vector<HitSoundUtils::Set_Slider_Hit> &started) {
    if(const Skin *skin = this->getSkin()) HitSoundUtils::stopSliderSounds(*skin, started);
}

void BeatmapInterface::playSpinnerSpinSound(f32 ratio, DatabaseBeatmapTypes::HITSAMPLE_BITS samples) {
    if(this->bWasSeekFrame) return;

    const Skin *skin = this->getSkin();
    Sound *spinner_spinsound = skin ? skin->s_spinner_spin : nullptr;
    if(spinner_spinsound) {
        if(!spinner_spinsound->isPlaying()) {
            soundEngine->play(spinner_spinsound, 0.f, 0.f, spinnerSoundVolume(*this, *skin, samples));
        }
        if(skin->o_spinner_frequency_modulate) {
            // same as stable: 20 kHz at the start, +40 kHz per required spin speed, capped at 100 kHz
            const f32 frequency = std::min(100000.0f, 20000.0f + (i32)(std::max(ratio, 0.0f) * 40000.0f));
            spinner_spinsound->setFrequency(frequency);
        } else {
            // sanity reset
            spinner_spinsound->setFrequency(0);
        }
    }
}

void BeatmapInterface::stopSpinnerSpinSound() {
    const Skin *skin = this->getSkin();
    Sound *spinner_spinsound = skin ? skin->s_spinner_spin : nullptr;
    if(spinner_spinsound && spinner_spinsound->isPlaying()) {
        soundEngine->stop(spinner_spinsound);
    }
}

void BeatmapInterface::playSpinnerBonusSound(DatabaseBeatmapTypes::HITSAMPLE_BITS samples) {
    const Skin *skin = this->getSkin();
    if(!this->bWasSeekFrame && skin && skin->s_spinner_bonus)
        soundEngine->play(skin->s_spinner_bonus, 0.f, 0.f, spinnerSoundVolume(*this, *skin, samples));
}

void BeatmapInterface::addTargetHit(f32 delta, f32 angle) { ui->getHUD()->addTarget(delta, angle); }

bool BeatmapInterface::canDraw() {
    if(!this->bIsPlaying && !this->bIsPaused && !this->bContinueScheduled && !this->bIsWaiting) return false;
    if(unlikely(!this->beatmap) || this->music.isEmpty())  // sanity check
        return false;

    return true;
}

void BeatmapInterface::loadBeatmapSkin() {
    Skin *skin = this->getSkinMutable();
    const PlayedMap *map = this->judge ? this->judge->getMapData() : nullptr;
    if(!skin || !map || !this->beatmap) return;

    skin->loadBeatmapOverride(this->beatmap->getFolder(), map->primitives.sampleFiles, map->primitives.sliderBorder,
                              map->primitives.sliderTrackOverride);
}

Storyboard::View BeatmapInterface::getStoryboardView() const {
    // the storyboard follows playfield scale and position
    // osu! centers the playfield in the 640x480 storyboard area at (64, 48), then shifts it 8px down
    const f32 scale = this->fScaleFactor;
    return {
        .screen = McRect(this->vPlayfieldOffset - vec2{64.f, 56.f} * scale, vec2{640.f, 480.f} * scale),
        .pivot = this->vPlayfieldCenter,
        .flipX = cv::playfield_mirror_vertical.getBool(),
        .flipY = cv::playfield_mirror_horizontal.getBool(),
        .rotation = cv::playfield_rotation.getFloat(),
        .brightness = std::clamp(1.f - cv::background_dim.getFloat() + this->fBreakBackgroundFade * 0.3f, 0.f, 1.f),
    };
}

void BeatmapInterface::loadStoryboard() {
    const PlayedMap *map = this->judge->getMapData();

    DatabaseBeatmap *sbmap = nullptr;
    if(map && map->data && cv::draw_storyboard.getBool()) {
        sbmap = this->beatmap;
    }

    if(sbmap == this->storyboardMap) return;

    this->storyboardMap = sbmap;
    this->storyboard.reset();
    this->storyboardHandle = {};
    if(!sbmap) return;

    const Skin *skin = this->getSkin();
    this->storyboardHandle = Storyboard::parseAsync(
        *sbmap, map->data, skin ? skin->search_dirs : std::vector<std::string>{}, Lane::Foreground);
}

void BeatmapInterface::applyMapSettings() {
    std::vector<std::pair<ConVar *, std::string>> values;
    if(this->beatmap) {
        for(const auto &[name, value] : this->beatmap->getSettings().convars) {
            if(ConVar *cvar = cvars().getConVarByName(name)) values.emplace_back(cvar, value);
        }
    }
    cvars().setLayer(CvarEditor::CONTEXT, values);

    // nothing has been played while the play waits to start (paused or not), so it starts over with these (and so does
    // the score, whose mods some of them are part of)
    if((this->bIsPlaying || this->bIsPaused) && this->bIsWaiting && !this->bLeadInStarted) this->judge->reset(-1000);
}

void BeatmapInterface::unloadObjects() {
    this->resetLiveStarsTasks();
    if(this->judge) this->judge->unload();
    if(Skin *skin = this->getSkinMutable()) skin->unloadBeatmapOverride();
    this->key_events.clear();
    this->all_clicks.clear();
}

void BeatmapInterface::resetScore() {
    this->is_submittable = BanchoState::are_settings_submittable();

    // don't clear the replay when we're watching/spectating
    if(!this->is_watching && !BanchoState::spectating) {
        this->judge->replay = {
            LegacyReplay::Frame{
                .cur_music_pos = -1,
                .milliseconds_since_last_frame = 0,
                .x = 256,
                .y = -500,
                .key_flags = 0,
            },
            LegacyReplay::Frame{
                .cur_music_pos = -1,
                .milliseconds_since_last_frame = -1,
                .x = 256,
                .y = -500,
                .key_flags = 0,
            },
        };
    }

    this->last_event_time = Timing::getTimeReal();
    this->last_event_ms = -1;
    this->key_events.clear();
    this->current_keys = 0;
    this->last_keys = 0;
    this->raw_gameplay_keys = 0;
    this->current_frame_idx = 0;
    this->iCurMusicPos = 0;
    this->iCurMusicPosWithOffsets = 0;

    this->fFailAnim.stop();
    this->fFailAnim = 1.0f;

    ui->getHUD()->resetScoreboard();
    ui->getHUD()->resetInputOverlayTrail();

    this->bIsFirstMissSound = true;
}

void BeatmapInterface::draw() {
    if(!this->canDraw()) return;

    // draw background
    this->drawBackground();

    // draw loading circle
    if(this->isLoading()) {
        if(this->is_buffering) {
            i32 leeway = std::min(this->getSpectatingLeeway(), cv::spec_buffer.getInt());
            f32 pct = (f32)leeway / (cv::spec_buffer.getFloat()) * 100.f;
            std::string loadingMessage = _("Buffering ...");
            if(pct > 0.f) {
                loadingMessage.append(fmt::format(" ({:.2f}%)", pct));
            }

            // show "paused" rather than "buffering" when applicable.
            // note that depending on server implementation, there are cases where we can miss the unpause packet.
            // we would then show "paused" while buffering even though they aren't actually paused, but it is what it is...
            if(this->spectate_pause) {
                auto info = BANCHO::User::get_user_info(BanchoState::spectated_player_id);
                loadingMessage = tformat("{} has paused", info->name.c_str());
            }

            ui->getHUD()->drawLoadingSmall(loadingMessage);

            // draw the rest of the playfield while buffering/paused
        } else if(BanchoState::is_playing_a_multi_map() && !this->all_players_loaded) {
            ui->getHUD()->drawLoadingSmall(_("Waiting for players ..."));

            // only start drawing the rest of the playfield if everything has loaded
            return;
        } else {
            ui->getHUD()->drawLoadingSmall(_("Loading ..."));

            // only start drawing the rest of the playfield if everything has loaded
            return;
        }
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
    if(cv::draw_followpoints.getBool() && !cv::mod_mafham.getBool())
        HitObjects::drawFollowPoints(*this, this->judge->getHitObjects(),
                                     (uSz)std::max(0, this->judge->getPreviousFollowPointObjectIndex()));

    // draw all hitobjects in reverse
    if(cv::draw_hitobjects.getBool()) this->drawHitObjects();

    if(this->storyboard) {
        this->storyboard->draw(Storyboard::Layer::OVERLAY, Storyboard::Layer::OVERLAY, this->getStoryboardView());
    }

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
    if(this->isContinueScheduled() && !this->isPaused() && cv::draw_continue.getBool()) this->drawContinue();

    // debug stuff
    if(cv::debug_hiterrorbar_misaims.getBool()) {
        for(const auto *misaimObject : this->judge->getMisaimObjects()) {
            g->setColor(0xbb00ff00);
            vec2 pos = this->osuCoords2Pixels(misaimObject->getRawPosAt(0));
            g->fillRect(pos.x - 50, pos.y - 50, 100, 100);
        }
    }

    // this is barely even useful for debugging in its current state, don't use it
    if(cv::debug_draw_gameplay_clicks.getBool()) {
        for(auto &click : this->all_clicks) {
            g->setColor(0xbb0000ff);
            g->fillRect(click.cursorPos.x - 2, click.cursorPos.y - 2, 4, 4);
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
        if(this->judge->holding_slider && !cv::avoid_flashes.getBool()) {
            opacity = 0.2f;
        }

        this->flashlight_shader->enable();
        this->flashlight_shader->setUniform1f("max_opacity", opacity);
        this->flashlight_shader->setUniform1f("flashlight_radius", fl_radius);

        if(!g->hasFlippedTextureOrigin()) {  // top-left origin backends don't need a Y flip
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
        if(this->judge->holding_slider && !cv::avoid_flashes.getBool()) {
            opacity = 0.8f;
        }

        this->actual_flashlight_shader->enable();
        this->actual_flashlight_shader->setUniform1f("max_opacity", opacity);
        this->actual_flashlight_shader->setUniform1f("flashlight_radius", anti_fl_radius);

        if(!g->hasFlippedTextureOrigin()) {  // top-left origin backends don't need a Y flip
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
    const u64 current_time = std::lround(engine->getTime() * 1000);

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
    const float cursorAnimPulsePercent =
        std::clamp<float>(static_cast<float>(std::fmod(engine->getTime(), 1.35)), 0.0f, 1.0f);
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

void BeatmapInterface::drawHitObjects() {
    const i32 curPos = this->iCurMusicPosWithOffsets;
    const i32 pvs = this->judge->getPVS();
    const auto hitobjectsSortedByEndTime = this->judge->getHitObjectsSortedByEndTime();
    const i32 currentHitObjectIndex = this->judge->getCurrentHitObjectIndex();
    const bool usePVS = cv::pvs.getBool();

    if(!cv::mod_mafham.getBool()) {
        this->objectsToDraw.clear();
        i32 mostDistantEndTimeDrawn = 0;

        for(sSz i = static_cast<sSz>(hitobjectsSortedByEndTime.size()) - 1; i >= 0; i--) {
            // PVS optimization (reversed)
            HitObject *obj = hitobjectsSortedByEndTime[i];
            if(likely(usePVS)) {
                if(obj->getClickTime() > curPos + pvs)  // future objects
                    continue;

                const i32 endTime = obj->getEndTime();
                if(obj->isFinished() && (curPos - pvs > endTime))  // past objects
                    break;

                if(endTime > mostDistantEndTimeDrawn) mostDistantEndTimeDrawn = endTime;
            }

            this->objectsToDraw.push_back(obj);
        }

        HitObjects::draw(this->objectsToDraw, osu->getSliderFrameBuffer());

        // this avoids PVS culling objects which are overlapped before the end of other objects, like circles appearing before
        // sliders are finished causing the initial slider approach circle to not be drawn
        // e.g.: the first object of https://osu.ppy.sh/beatmapsets/613791#osu/1294898
        const i32 mostDistantFutureObjectPVS = std::max(mostDistantEndTimeDrawn, curPos + pvs);

        // this doesn't need the spinner front-to-back thing because spinners have no draw2
        for(auto *obj : hitobjectsSortedByEndTime) {
            // NOTE: to fix mayday simultaneous sliders with increasing endtime getting culled here, would have to
            // switch from m_hitobjectsSortedByEndTime to m_hitobjects PVS optimization
            if(likely(usePVS)) {
                if(obj->isFinished() && (curPos - pvs > obj->getEndTime()))  // past objects
                    continue;

                if((obj->getClickTime() > mostDistantFutureObjectPVS))  // future objects
                    break;
            }

            obj->draw2();
        }
    } else {
        // the slider bodies are queued before any of the objects is drawn, so their fields render together
        const auto drawObjects = [](std::span<HitObject *const> objects) {
            SliderRenderer::Batch sliderBodies{osu->getSliderFrameBuffer()};
            for(const HitObject *obj : objects)
                if(obj->isSlider()) sliderBodies.queue(*static_cast<const Slider *>(obj));
            for(HitObject *obj : objects) obj->draw();
        };

        const int mafhamRenderLiveSize = cv::mod_mafham_render_livesize.getInt();

        if(this->mafhamActiveRenderTarget == nullptr) this->mafhamActiveRenderTarget = osu->getFrameBuffer();

        if(this->mafhamFinishedRenderTarget == nullptr) this->mafhamFinishedRenderTarget = osu->getFrameBuffer2();

        // if we have a chunk to render into the scene buffer
        const bool shouldDrawBuffer = (hitobjectsSortedByEndTime.size() - currentHitObjectIndex) > mafhamRenderLiveSize;
        bool shouldRenderChunk =
            this->iMafhamHitObjectRenderIndex < hitobjectsSortedByEndTime.size() && shouldDrawBuffer;
        if(shouldRenderChunk) {
            const bool firstChunk = this->iMafhamHitObjectRenderIndex == 0;

            this->objectsToDraw.clear();
            int chunkCounter = 0;
            for(int i = hitobjectsSortedByEndTime.size() - 1 - this->iMafhamHitObjectRenderIndex; i >= 0;
                i--, this->iMafhamHitObjectRenderIndex++) {
                chunkCounter++;
                if(chunkCounter > cv::mod_mafham_render_chunksize.getInt())
                    break;  // continue chunk render in next frame

                if(i <= currentHitObjectIndex + mafhamRenderLiveSize)  // skip live objects
                {
                    this->iMafhamHitObjectRenderIndex = hitobjectsSortedByEndTime.size();  // stop chunk render
                    break;
                }

                // PVS optimization (reversed)
                if(likely(usePVS)) {
                    if(hitobjectsSortedByEndTime[i]->isFinished() &&
                       (curPos - pvs > hitobjectsSortedByEndTime[i]->getClickTime() +
                                           hitobjectsSortedByEndTime[i]->getDuration()))  // past objects
                    {
                        this->iMafhamHitObjectRenderIndex = hitobjectsSortedByEndTime.size();  // stop chunk render
                        break;
                    }
                    if(hitobjectsSortedByEndTime[i]->getClickTime() > curPos + pvs)  // future objects
                        continue;
                }

                this->objectsToDraw.push_back(hitobjectsSortedByEndTime[i]);

                this->iMafhamActiveRenderHitObjectIndex = i;
            }

            this->bInMafhamRenderChunk = true;

            this->mafhamActiveRenderTarget->enable(/*clear=*/firstChunk);
            {
                g->setBlendMode(DrawBlendMode::PREMUL_ALPHA);
                drawObjects(this->objectsToDraw);
                g->setBlendMode(DrawBlendMode::ALPHA);
            }
            this->mafhamActiveRenderTarget->disable();

            this->bInMafhamRenderChunk = false;
        }
        shouldRenderChunk = this->iMafhamHitObjectRenderIndex < hitobjectsSortedByEndTime.size() && shouldDrawBuffer;
        if(!shouldRenderChunk && this->bMafhamRenderScheduled) {
            // finished, we can now swap the active framebuffer with the one we just finished
            this->bMafhamRenderScheduled = false;

            RenderTarget *temp = this->mafhamFinishedRenderTarget;
            this->mafhamFinishedRenderTarget = this->mafhamActiveRenderTarget;
            this->mafhamActiveRenderTarget = temp;

            this->iMafhamFinishedRenderHitObjectIndex = this->iMafhamActiveRenderHitObjectIndex;
            this->iMafhamActiveRenderHitObjectIndex = hitobjectsSortedByEndTime.size();  // reset
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
        if(cv::draw_followpoints.getBool())
            HitObjects::drawFollowPoints(*this, this->judge->getHitObjects(),
                                         (uSz)std::max(0, this->judge->getPreviousFollowPointObjectIndex()));

        // draw live hitobjects (also, code duplication yay)
        {
            this->objectsToDraw.clear();
            for(int i = hitobjectsSortedByEndTime.size() - 1; i >= 0; i--) {
                // PVS optimization (reversed)
                if(likely(usePVS)) {
                    if(hitobjectsSortedByEndTime[i]->getClickTime() > curPos + pvs)  // future objects
                        continue;
                    if(hitobjectsSortedByEndTime[i]->isFinished() &&
                       (curPos - pvs > hitobjectsSortedByEndTime[i]->getClickTime() +
                                           hitobjectsSortedByEndTime[i]->getDuration()))  // past objects
                        break;
                }

                if(i > currentHitObjectIndex + mafhamRenderLiveSize ||
                   (i > this->iMafhamFinishedRenderHitObjectIndex - 1 && shouldDrawBuffer))  // skip non-live objects
                    continue;

                this->objectsToDraw.push_back(hitobjectsSortedByEndTime[i]);
            }
            drawObjects(this->objectsToDraw);

            for(int i = 0; i < hitobjectsSortedByEndTime.size(); i++) {
                // PVS optimization
                if(likely(usePVS)) {
                    if(hitobjectsSortedByEndTime[i]->isFinished() &&
                       (curPos - pvs > hitobjectsSortedByEndTime[i]->getClickTime() +
                                           hitobjectsSortedByEndTime[i]->getDuration()))  // past objects
                        continue;
                    if(hitobjectsSortedByEndTime[i]->getClickTime() > curPos + pvs)  // future objects
                        break;
                }

                if(i >= currentHitObjectIndex + mafhamRenderLiveSize ||
                   (i >= this->iMafhamFinishedRenderHitObjectIndex - 1 && shouldDrawBuffer))  // skip non-live objects
                    break;

                hitobjectsSortedByEndTime[i]->draw2();
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

    // do hitobject updates among other things
    // yes, this needs to happen after updating metrics
    this->update2();

    // adopt the storyboard once parsed (at the start, or after it got turned on mid-play)
    if(this->storyboardHandle.valid() && this->storyboardHandle.is_ready()) {
        this->storyboard = this->storyboardHandle.get();
        if(this->storyboard) {
            this->storyboard->load(false);
            // (for its lead-in, while the play still waits to start; quick restarts keep their storyboard)
            if(this->bIsWaiting && !this->bLeadInStarted) this->seekToStart(false);
        }
    }

    // handle preloading (only for distributed slider vertexbuffer generation atm)
    const bool was_preloading = this->bIsPreLoading;
    if(this->bIsPreLoading) {
        if(cv::debug_osu.getBool() && this->iPreLoadingIndex == 0)
            debugLog("Beatmap: Preloading slider vertexbuffers ...");

        f64 startTime = Timing::getTimeReal();
        f64 delta = 0.0;

        // hardcoded deadline of 10 ms, will temporarily bring us down to 45fps on average (better than freezing)
        const auto hitobjects = this->judge->getHitObjects();
        while(delta < 0.010 && this->bIsPreLoading) {
            if(this->iPreLoadingIndex >= hitobjects.size()) {
                // (and for the map's skin elements to load, see loadBeatmapSkin())
                if(const Skin *skin = this->getSkin(); skin && !skin->isReady()) break;
                if(this->storyboardHandle.valid()) break;
                if(this->storyboard && !this->storyboard->isReady()) break;
                this->bIsPreLoading = false;
                debugLog("Beatmap: Preloading done.");
                break;
            } else {
                hitobjects[this->iPreLoadingIndex]->rebuildVertexBuffer();
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

    // scene buffering logic
    if(cv::mod_mafham.getBool()) {
        if(!this->bMafhamRenderScheduled &&
           this->judge->getCurrentHitObjectIndex() !=
               this->iMafhamPrevHitObjectIndex)  // if we are not already rendering and the index changed
        {
            this->iMafhamPrevHitObjectIndex = this->judge->getCurrentHitObjectIndex();
            this->iMafhamHitObjectRenderIndex = 0;
            this->bMafhamRenderScheduled = true;
        }
    }

    // singletap/fullalternate mod lenience
    if(this->bInBreak || this->bIsInSkippableSection || this->judge->isSpinnerActive() ||
       this->judge->getCurrentHitObjectIndex() < 1) {
        this->iAllowAnyNextKeyUntilHitObjectIndex = this->judge->getCurrentHitObjectIndex() + 1;
    }
}

void BeatmapInterface::update2() {
    if(this->bContinueScheduled) {
        // (a pause while waiting (green progressbar) continues without the continue click)
        bool isEarlyNoteContinue = (!this->bIsPaused && this->bIsWaiting);
        if(this->bClickedContinue || isEarlyNoteContinue) {
            this->bClickedContinue = false;
            this->bContinueScheduled = false;
            this->bIsPaused = false;

            // annoying place to put this, but update text input state after pressing continue
            // (for allowing consolebox optionsmenu chat etc. text input in the continue screen)
            osu->updateWindowsKeyDisable();

            // (before the lead-in, the music waits for it, see below)
            if(this->bLeadInStarted || !this->bIsWaiting) this->music.play();

            this->bIsPlaying = true;  // usually this should be checked with the result of the above play() call, but
                                      // since we are continuing we can assume that everything works

            // for nightmare mod, to avoid a miss because of the continue click
            this->key_events.clear();
        }
    }

    // handle restarts
    if(this->bIsRestartScheduled) {
        this->bIsRestartScheduled = false;
        this->actualRestart();
        return;
    }

    const bool isIdlePaused = this->isActuallyPausedAndNotSpectating();

    // update current music position (this variable does not include any offsets!), with a fake negative start while
    // loading
    this->iCurMusicPos = this->isActuallyLoading() ? -1000 : this->music.getTime();

    const bool wasSeekFrame = this->bWasSeekFrame;
    this->bWasSeekFrame = false;

    const auto hitobjects = this->judge->getHitObjects();

    // handle timewarp
    if(cv::mod_timewarp.getBool()) {
        if(likely(!hitobjects.empty()) && this->iCurMusicPos > hitobjects[0]->getClickTime()) {
            const f32 percentFinished = ((f64)(this->iCurMusicPos - hitobjects[0]->getClickTime()) /
                                         (f64)(hitobjects.back()->getEndTime() - hitobjects[0]->getClickTime()));
            f32 warp_multiplier = std::max(cv::mod_timewarp_multiplier.getFloat(), 1.f);
            const f32 speed =
                this->getSpeedMultiplier() + percentFinished * this->getSpeedMultiplier() * (warp_multiplier - 1.0f);
            this->setMusicSpeed(speed);
        }
    }

    // hoist this call out (it's constant throughout an update iteration)
    const f64 current_frametime = engine->getFrameTime();

    // waiting to start: the load, then the lead-in, which is the music track's virtual time before the song
    if(this->bIsWaiting) {
        if(!this->isLoading() && !this->bLeadInStarted && !this->bIsPaused &&
           !ui->getMapSettingsOverlay()->isHoldingStart()) {
            this->bLeadInStarted = true;
            this->bIsPlaying = true;

            // (from where the play waited, see seekToStart())
            this->iLeadInMS = -std::min(this->iCurMusicPos, 0);
            // (without a lead-in the waiting ends with the start)
            this->bIsWaiting = this->iLeadInMS > 0;

            this->music.setLoop(false);
            this->music.play();

            // if there are calculations in there that need the hitobjects to be loaded, also applies speed/pitch
            this->onModUpdate(false, false);
        } else if(this->bLeadInStarted && this->iCurMusicPos >= 0) {
            this->bIsWaiting = false;
        }

        // ugh. force update all hitobjects while waiting (necessary because of pvs optimization)
        i32 curPos = this->iCurMusicPos + this->music.getOffset(this->beatmap, this->judge->getMapVersion());
        if(curPos > -1)  // otherwise auto would already click elements that start at exactly 0 (while the map has not
                         // even started)
            curPos = -1;

        this->judge->updateAll(curPos, current_frametime);
    }

    // only continue updating hitobjects etc. if we have loaded everything
    if(this->isLoading()) return;

    // handle music loading fail
    if(!this->music.isReady()) {
        ui->getNotificationOverlay()->addToast("Couldn't load music file :(", ERROR_TOAST);
        this->stop(true);
        return;
    }

    // detect and handle music end (the music track's virtual time goes on past it until the last hitobject is done, plus
    // end_delay_time, because some beatmaps have hitobjects going until >= the exact end of the music ffs)
    if(!this->bIsWaiting && this->music.isReady()) {
        const bool isMusicFinished = this->music.isFinished();

        const bool hasAnyHitObjects = (likely(!hitobjects.empty()));
        const auto sortedByEndTime = this->judge->getHitObjectsSortedByEndTime();
        // (a storyboard going on after the last hitobject holds the play until its end)
        const i32 storyboardEnd = this->getStoryboardEnd().value_or(std::numeric_limits<i32>::min());
        const bool isTimePastLastHitObjectPlusLenience =
            !sortedByEndTime.empty() &&
            (this->iCurMusicPos >
             std::max(sortedByEndTime.back()->getEndTime() + (i32)cv::end_delay_time.getInt(), storyboardEnd));
        if(!hasAnyHitObjects || (cv::end_skip.getBool() && isTimePastLastHitObjectPlusLenience) ||
           (!cv::end_skip.getBool() && isMusicFinished && this->iCurMusicPos > storyboardEnd)) {
            if(!this->judge->hasFailed()) {
                this->stop(false);
                return;
            }
        }
    }

    // update timing (with offsets)
    this->iCurMusicPosWithOffsets =
        this->iCurMusicPos + this->music.getOffset(this->beatmap, this->judge->getMapVersion());

    // get timestamp from the previous update cycle
    const u64 lastUpdateTime = this->iLastMusicPosUpdateTime;

    // update the update timestamp
    const u64 currentUpdateTime = Timing::getTicksNS();
    this->iLastMusicPosUpdateTime = currentUpdateTime;

    // update current timingpoint
    if(this->iCurMusicPosWithOffsets >= 0) {
        this->cur_timing_info =
            HitSoundUtils::samplesAt(this->judge->getMapTimingPoints(), this->iCurMusicPosWithOffsets);
    }

    // interpolate the key changes that occurred between the last update and now
    // (except if we are paused)
    if(!isIdlePaused && !this->is_watching && !BanchoState::spectating && !this->key_events.empty()) {
        if(const u64 timeSinceLastUpdate = currentUpdateTime - lastUpdateTime; timeSinceLastUpdate > 0) {
            for(auto &event : this->key_events) {
                Click &click = event.click;

                // how long after the last music update did this click occur?
                const u64 clickDeltaSinceLastUpdate = click.timestampNS - lastUpdateTime;
                const f64 percent = std::clamp((f64)clickDeltaSinceLastUpdate / (f64)timeSinceLastUpdate, 0.0, 1.0);

                // interpolate between the music position when click was captured and current music position
                // TODO: aim-between-frames
                click.musicPosMS = static_cast<i32>(
                    std::round(std::lerp((f64)click.musicPosMS, (f64)this->iCurMusicPosWithOffsets, percent)));
            }
        }
    }

    // the cursor and keys shown while watching (the judge judges the frames, below)
    // don't advance replay frames if we are paused unless this was a seek
    const auto &replay = this->judge->replay;
    if((!isIdlePaused || wasSeekFrame) && (this->is_watching || BanchoState::spectating) && replay.size() >= 2) {
        LegacyReplay::Frame current_frame = replay[this->current_frame_idx];
        LegacyReplay::Frame next_frame = replay[this->current_frame_idx + 1];

        while(next_frame.cur_music_pos <= this->iCurMusicPosWithOffsets) {
            if(this->current_frame_idx + 2 >= replay.size()) break;

            this->last_keys = this->current_keys;

            this->current_frame_idx++;
            current_frame = replay[this->current_frame_idx];
            next_frame = replay[this->current_frame_idx + 1];

            // There is a big gap in the replay, it is safe to assume it was made from a neomod client
            // and that the player skipped an empty section.
            // We'll "schedule" a skip, which is the same as clicking the skip button, so nothing
            // would get skipped if it isn't actually a skippable section.
            if(next_frame.milliseconds_since_last_frame >= cv::skip_time.getInt()) {
                this->bSkipScheduled = true;
            }

            this->current_keys = current_frame.key_flags;

            // Replays have both K1 and M1 set when K1 is pressed, fix it now
            if(!this->getMods().has(ModFlags::NoKeylock)) {
                if(this->current_keys & LegacyReplay::K1) this->current_keys &= ~LegacyReplay::M1;
                if(this->current_keys & LegacyReplay::K2) this->current_keys &= ~LegacyReplay::M2;
            }

            for(auto key : {GameplayKeys::K1, GameplayKeys::K2, GameplayKeys::M1, GameplayKeys::M2}) {
                const bool wasDown = this->last_keys & key;
                const bool isDown = this->current_keys & key;
                if(wasDown != isDown) ui->getHUD()->animateInputOverlay(key, isDown, current_frame.cur_music_pos);
            }
        }

        f32 percent = 0.f;
        if(next_frame.milliseconds_since_last_frame > 0) {
            i32 ms_since_last_frame = this->iCurMusicPosWithOffsets - current_frame.cur_music_pos;
            percent = (f32)ms_since_last_frame / (f32)next_frame.milliseconds_since_last_frame;
        }

        this->interpolatedMousePos = this->replayCoords2Pixels(
            vec2{std::lerp(current_frame.x, next_frame.x, percent), std::lerp(current_frame.y, next_frame.y, percent)});
    }

    // add current clicks to all_clicks for debug
    if(cv::debug_draw_gameplay_clicks.getBool()) {
        for(const auto &event : this->key_events) {
            if(event.clickKey) this->all_clicks.push_back(event.click);
        }
    }

    // judging
    if(this->is_watching || BanchoState::spectating) {
        if(!isIdlePaused || wasSeekFrame) this->judge->simulate_to(this->iCurMusicPosWithOffsets);
        this->judge->animate(this->iCurMusicPosWithOffsets);
        if(this->bIsPaused) this->stopObjectSounds();
        if(this->is_watching) osu->getScore()->setCheated();
    } else if(this->bIsPlaying && !this->judge->hasFailed()) {
        this->step(current_frametime);
    } else {
        // (paused, waiting for the continue click, or failing: nothing is judged)
        this->judge->animate(this->iCurMusicPosWithOffsets);
        this->stopObjectSounds();
    }
    this->key_events.clear();
    this->vAutoCursorPos = this->replayCoords2Pixels(this->judge->getAutoCursorPos());

    if(this->storyboard) {
        this->storyboard->update(this->iCurMusicPosWithOffsets, this->judge->isPassing(), wasSeekFrame);
    }

    const i32 previousHitObjectTime = this->judge->getPreviousHitObjectTime();
    const i32 nextHitObjectTime = this->judge->getNextHitObjectTime();

    // empty section detection & skipping
    if(likely(!hitobjects.empty())) {
        const bool wasInSkippableSection = this->bIsInSkippableSection;
        this->bIsInSkippableSection = this->judge->isInSkippableSection() || this->isInOutro();

        if((wasInSkippableSection != this->bIsInSkippableSection) || (BanchoState::is_playing_a_multi_map())) {
            // FIXME: why the FUCK is this here?
            ui->getChat()->updateVisibility();
        }
    }

    // skip button clicking
    if(
        // skipping is not already scheduled
        (!this->bSkipScheduled && !this->bClickedSkipButton) &&
        // skipping is possible, and
        (this->bIsInSkippableSection && !this->bIsPaused && !ui->getVolumeOverlay()->isBusy()) &&
        // any gameplay-relevant key is down, and
        (this->isClickHeld() || mouse->isLeftDown()) &&
        // mouse is inside skip click rect
        (ui->getHUD()->getSkipClickRect().contains(mouse->getPos()))  //
    ) {
        // schedule skip
        this->bSkipScheduled = true;
        this->bClickedSkipButton = true;

        if(BanchoState::is_playing_a_multi_map()) {
            Packet packet;
            packet.id = OUTP_MATCH_SKIP_REQUEST;
            BANCHO::Net::send_packet(packet);
        }
    }

    // skipping
    if(this->bSkipScheduled) {
        if(this->bIsInSkippableSection && !this->bIsPaused) {
            bool can_skip_intro = (cv::skip_intro_enabled.getBool() && this->getCurrentHitObjectIndex() < 1);
            bool can_skip_break = (cv::skip_breaks_enabled.getBool() && this->getCurrentHitObjectIndex() > 0);
            if(BanchoState::is_playing_a_multi_map()) {
                can_skip_intro = this->all_players_skipped;
                can_skip_break = false;
            }

            if(can_skip_intro || can_skip_break) {
                this->skipEmptySection();
            }
        }

        this->bSkipScheduled = false;
    }

    // Reset bClickedSkipButton on mouse up
    // We only use bClickedSkipButton to prevent seeking when clicking the skip button
    if(this->bClickedSkipButton && !this->bIsInSkippableSection) {
        if(!mouse->isLeftDown()) {
            this->bClickedSkipButton = false;
        }
    }

    // warning arrow logic
    if(likely(!hitobjects.empty())) {
        const i32 legacyOffset = (previousHitObjectTime < hitobjects[0]->getClickTime() ? 0 : 1000);  // Mc
        const i32 minGapSize = 1000;
        const i32 lastVisibleMin = 400;
        const i32 blinkDelta = 100;

        const i32 gapSize = nextHitObjectTime - (previousHitObjectTime + legacyOffset);
        const i32 nextDelta = (nextHitObjectTime - this->iCurMusicPosWithOffsets);
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

    // background fade during breaks
    const DBType::BREAK breakEvent =
        this->judge->getBreakForTimeRange(previousHitObjectTime, this->iCurMusicPosWithOffsets, nextHitObjectTime);
    if(this->judge->isInBreak() != this->bInBreak) {
        this->bInBreak = !this->bInBreak;

        if(!cv::background_dont_fade_during_breaks.getBool() || this->fBreakBackgroundFade != 0.0f) {
            if(this->bInBreak && !cv::background_dont_fade_during_breaks.getBool()) {
                const int breakDuration = breakEvent.endTime - breakEvent.startTime;
                if(breakDuration > (int)(cv::background_fade_min_duration.getFloat() * 1000.0f))
                    this->fBreakBackgroundFade.set(1.0f, cv::background_fade_in_duration.getFloat(), anim::Linear);
            } else
                this->fBreakBackgroundFade.set(0.0f, cv::background_fade_out_duration.getFloat(), anim::Linear);
        }
    }

    // section pass/fail logic
    if(likely(!hitobjects.empty())) {
        const i32 minGapSize = 2880;
        const i32 fadeStart = 1280;
        const i32 fadeEnd = 1480;

        const i32 gapSize = nextHitObjectTime - previousHitObjectTime;
        const i32 start =
            (gapSize / 2 > minGapSize ? previousHitObjectTime + (gapSize / 2) : nextHitObjectTime - minGapSize);
        const i32 nextDelta = this->iCurMusicPosWithOffsets - start;
        const bool inSectionPassFail =
            (gapSize > minGapSize && nextDelta > 0) && this->iCurMusicPosWithOffsets > hitobjects[0]->getClickTime() &&
            this->iCurMusicPosWithOffsets < this->judge->getHitObjectsSortedByEndTime().back()->getEndTime() &&
            !this->judge->hasFailed() && this->bInBreak && (breakEvent.endTime - breakEvent.startTime) > minGapSize;

        const bool passing = (this->judge->getHealth() >= 0.5);

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

    // nightcore beats
    // TODO: is this the right place to put this?
    this->playNightcoreBeats(wasSeekFrame);

    // handle fail animation
    if(this->judge->hasFailed()) {
        if(this->fFailAnim <= 0.0f) {
            if(this->music.isPlaying() || !ui->getPauseOverlay()->isVisible()) {
                this->music.pause();
                this->bIsPaused = true;

                if(BanchoState::spectating) {
                    osu->bIsPlayingASelectedBeatmap = false;
                } else {
                    ui->getPauseOverlay()->setVisible(true);
                    ui->getModSelector()->setVisible(false);  // can be open mid-play (live mod changing)
                    osu->updateConfineCursor();
                }
            }
        } else {
            this->music.setSlowdown(this->fFailAnim);
        }
    }

    // spectator score correction
    if(BanchoState::spectating && replay.size() >= 2) {
        const auto &current_frame = replay[this->current_frame_idx];

        i32 score_frame_idx = -1;
        for(i32 i = 0; i < this->score_frames.size(); i++) {
            if(this->score_frames[i].time == current_frame.cur_music_pos) {
                score_frame_idx = i;
                break;
            }
        }

        if(score_frame_idx != -1) {
            auto *score = osu->getScore();
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
            this->judge->setHealth(((f32)fixed_score.current_hp) / 255.f);

            // fixed_score.is_perfect is unused
            // fixed_score.tag is unused (XXX: i think scorev2 uses this?)
            // fixed_score.is_scorev2 is unused
        }
    }
}

void BeatmapInterface::step(f64 frametime) {
    const bool auto_cursor = osu->getModAuto() || osu->getModAutopilot();
    const auto replayCursor = [this, auto_cursor](vec2 cursorPos) -> vec2 {
        if(auto_cursor) return this->judge->getAutoCursorPos();
        return this->pixels2ReplayCoords(cv::mod_fps.getBool() ? this->getMousePos() : cursorPos);
    };

    // judge clicks since last frame
    for(const auto &event : this->key_events) {
        const vec2 pos = replayCursor(event.click.cursorPos);
        const LegacyReplay::Frame frame{.cur_music_pos = event.click.musicPosMS,
                                        .milliseconds_since_last_frame = 0,
                                        .x = pos.x,
                                        .y = pos.y,
                                        .key_flags = event.keys};
        this->judge->step(frame, 0.0, event.clickKey);
        this->write_frame(frame, true);
    }

    // judge cursor movement (or lack thereof)
    const vec2 pos = replayCursor(this->getCursorPos());
    const LegacyReplay::Frame frame{.cur_music_pos = this->iCurMusicPosWithOffsets,
                                    .milliseconds_since_last_frame = 0,
                                    .x = pos.x,
                                    .y = pos.y,
                                    .key_flags = this->current_keys};
    this->judge->step(frame, frametime, 0);
    this->write_frame(frame, this->judge->needsReplayFrame());
}

void BeatmapInterface::stopObjectSounds() {
    this->stopSliderSounds({});
    this->stopSpinnerSpinSound();
}

bool BeatmapInterface::clickableHitobjectAt(vec2 cursor_pos) const {
    // FIXME: this doesn't use PVS and iterates over every hit object
    const vec2 replayCursorPos = this->pixels2ReplayCoords(cursor_pos);
    for(const auto &h : this->judge->getHitObjects()) {
        if(!h->isClickableFrom(this->iCurMusicPosWithOffsets, replayCursorPos)) continue;
        return true;
    }

    return false;
}

// played at the end of update2()
void BeatmapInterface::playNightcoreBeats(bool wasSeekFrame) {
    const int nightcoreBehavior = cv::nightcore_beats_behavior.getInt();
    const bool playBeats = nightcoreBehavior != 2 /* never */ &&
                           (nightcoreBehavior == 1 /*always*/ ||
                            (this->getMods().has(ModFlags::NoPitchCorrection) && this->getSpeedMultiplier() > 1.f));
    if(!playBeats) return;

    // no timingpoints (is this even reachable here?)
    const auto &timingPoints = this->judge->getMapTimingPoints();
    if(timingPoints.empty()) return;

    // a kick and a clap on alternating beats with hats between them, and a finish every 4 measures.
    // hats only play if the slider tick rate gives a regular offbeat.
    // meters other than 3/4 and 4/4 only get the finish.
    const auto &timing = timingPoints.getUninherited(this->iCurMusicPosWithOffsets);
    if(timing.msPerBeat <= 0.0) return;  // need >0 msPerBeat

    f64 halfBeatLength = timing.msPerBeat / 2.0;
    while(halfBeatLength < 50.0) halfBeatLength *= 2.0;

    const i64 halfBeat = (i64)std::floor((this->iCurMusicPosWithOffsets - timing.offset) / halfBeatLength);
    const i32 halfBeatTime = (i32)(timing.offset + (f64)halfBeat * halfBeatLength);
    if(halfBeatTime != this->iLastNightcoreBeatTime) {
        this->iLastNightcoreBeatTime = halfBeatTime;

        if(halfBeat >= 0 && !wasSeekFrame && this->music.isPlaying() && !this->judge->hasFailed()) {
            const Skin *skin = this->getSkin();
            if(halfBeat % (timing.meter * 2 * 4) == 0 && (halfBeat > 0 || !timing.omitFirstBarline)) {
                soundEngine->play(skin->s_nightcore_finish);
            }

            if(timing.meter == 3 || timing.meter == 4) {
                const i64 inBar = halfBeat % (timing.meter == 3 ? 6 : 4);
                if(inBar == 0) {
                    soundEngine->play(skin->s_nightcore_kick);
                } else if(inBar == (timing.meter == 3 ? 3 : 2)) {
                    soundEngine->play(skin->s_nightcore_clap);
                } else if(std::fmod(this->beatmap->getSliderTickRate(), 2.f) == 0.f) {
                    soundEngine->play(skin->s_nightcore_hat);
                }
            }
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

void BeatmapInterface::write_frame(const LegacyReplay::Frame &frame, bool force) {
    if(!this->bIsPlaying || this->judge->hasFailed() || this->is_watching || BanchoState::spectating) return;

    const i32 delta = frame.cur_music_pos - this->last_event_ms;
    if(delta < 0) return;
    const bool keysChanged = frame.key_flags != this->last_keys;
    if(!force && !keysChanged && this->last_event_time + 0.01666666666 > Timing::getTimeReal()) return;
    if(delta == 0 && !keysChanged) return;

    // In replays, "K1" is always stored as "K1+M1"
    // (unless we have the "no keylock" mod on, which uses 4 keys instead of only 2)
    u8 replay_keys = frame.key_flags;
    if(!this->getMods().has(ModFlags::NoKeylock)) {
        if(frame.key_flags & LegacyReplay::KeyFlags::K1) replay_keys |= LegacyReplay::KeyFlags::M1;
        if(frame.key_flags & LegacyReplay::KeyFlags::K2) replay_keys |= LegacyReplay::KeyFlags::M2;
    }

    this->judge->replay.push_back(LegacyReplay::Frame{
        .cur_music_pos = frame.cur_music_pos,
        .milliseconds_since_last_frame = delta,
        .x = frame.x,
        .y = frame.y,
        .key_flags = replay_keys,
    });

    this->frame_batch.push_back(LiveReplayFrame{
        .key_flags = replay_keys,
        .padding = 0,
        .mouse_x = frame.x,
        .mouse_y = frame.y,
        .time = frame.cur_music_pos,
    });

    this->last_event_time = engine->getTime();
    this->last_event_ms = frame.cur_music_pos;
    this->last_keys = frame.key_flags;

    if(!BanchoState::spectators.empty() && last_event_time > this->last_spectator_broadcast + 1.0) {
        this->broadcast_spectator_frames();
    }
}

void BeatmapInterface::onModUpdate(bool rebuildSliderVertexBuffers, bool recomputeDrainRate) {
    logIfCV(debug_osu, "onModUpdate() @ {:f}", engine->getTime());

    this->updatePlayfieldMetrics();
    this->updateHitobjectMetrics();
    if(this->judge) this->judge->updateHitobjectMetrics();

    if(recomputeDrainRate && this->judge) this->judge->computeDrainRate();

    // Updates not just speed but also nightcore state
    this->setMusicSpeed(this->getSpeedMultiplier());

    // recalculate slider vertexbuffers
    if(osu->getModHR() != this->bWasHREnabled ||
       cv::playfield_mirror_horizontal.getBool() != this->bWasHorizontalMirrorEnabled ||
       cv::playfield_mirror_vertical.getBool() != this->bWasVerticalMirrorEnabled) {
        this->bWasHREnabled = osu->getModHR();
        this->bWasHorizontalMirrorEnabled = cv::playfield_mirror_horizontal.getBool();
        this->bWasVerticalMirrorEnabled = cv::playfield_mirror_vertical.getBool();

        if(this->judge) this->judge->calculateStacks();

        if(rebuildSliderVertexBuffers) this->updateSliderVertexBuffers();
    }
    if(osu->getModEZ() != this->bWasEZEnabled) {
        if(this->judge) this->judge->calculateStacks();

        this->bWasEZEnabled = osu->getModEZ();
        if(rebuildSliderVertexBuffers) this->updateSliderVertexBuffers();
    }
    if(this->fHitcircleDiameter != this->fPrevHitCircleDiameter && this->judge &&
       !this->judge->getHitObjects().empty()) {
        this->judge->calculateStacks();

        this->fPrevHitCircleDiameter = this->fHitcircleDiameter;
        if(rebuildSliderVertexBuffers) this->updateSliderVertexBuffers();
    }
    if(cv::playfield_rotation.getFloat() != this->fPrevPlayfieldRotationFromConVar) {
        this->fPrevPlayfieldRotationFromConVar = cv::playfield_rotation.getFloat();
        if(rebuildSliderVertexBuffers) this->updateSliderVertexBuffers();
    }
    if(cv::mod_mafham.getBool() != this->bWasMafhamEnabled) {
        this->bWasMafhamEnabled = cv::mod_mafham.getBool();
        if(this->judge) this->judge->updateAll(this->iCurMusicPosWithOffsets, engine->getFrameTime());
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

const AsyncPPC::pp_res &BeatmapInterface::getWholeMapPPInfo() const {
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

i32 BeatmapInterface::getSpectatingLeeway() const {
    if(!this->judge || this->judge->replay.empty()) return 0;
    return std::max(0, this->judge->replay.back().cur_music_pos - this->iCurMusicPos);
}

bool BeatmapInterface::isLoading() {
    return (this->isActuallyLoading() || this->is_buffering ||
            (BanchoState::is_playing_a_multi_map() && !this->all_players_loaded));
}

bool BeatmapInterface::isActuallyLoading() const {
    // (the track can still be waiting for the map's loudness, see MusicTrack::load())
    return !soundEngine->isReady() || this->music.isLoading() || this->bIsPreLoading;
}

vec2 BeatmapInterface::legacyPixels2RawPixels(vec2 coords) const {
    // just scale
    coords *= this->fScaleFactor;
    return coords;
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
    // the mods' flips and wobble, as judged
    if(this->judge) coords = this->judge->osuCoords2Pixels(coords);

    // the playfield's flips and rotation (only how it's shown, see pixels2ReplayCoords())
    if(cv::playfield_mirror_horizontal.getBool()) coords.y = GameRules::OSU_COORD_HEIGHT - coords.y;
    if(cv::playfield_mirror_vertical.getBool()) coords.x = GameRules::OSU_COORD_WIDTH - coords.x;
    if(cv::playfield_rotation.getFloat() != 0.0f) {
        coords.x -= GameRules::OSU_COORD_WIDTH / 2;
        coords.y -= GameRules::OSU_COORD_HEIGHT / 2;

        vec3 coords3 = vec3(coords.x, coords.y, 0);
        Matrix4 rot;
        rot.rotateZ(cv::playfield_rotation.getFloat());

        coords3 = coords3 * rot;
        coords.x = coords3.x + GameRules::OSU_COORD_WIDTH / 2;
        coords.y = coords3.y + GameRules::OSU_COORD_HEIGHT / 2;
    }

    if(this->hasFailed()) {
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

vec2 BeatmapInterface::pixels2ReplayCoords(vec2 pixelCoords) const {
    vec2 pos = this->pixels2OsuCoords(pixelCoords);

    // undo the playfield's rotation and flips (in reverse order of osuCoords2Pixels())
    if(cv::playfield_rotation.getFloat() != 0.0f) {
        pos.x -= GameRules::OSU_COORD_WIDTH / 2;
        pos.y -= GameRules::OSU_COORD_HEIGHT / 2;
        vec3 coords3 = vec3(pos.x, pos.y, 0);
        Matrix4 rot;
        rot.rotateZ(-cv::playfield_rotation.getFloat());
        coords3 = coords3 * rot;
        pos.x = coords3.x + GameRules::OSU_COORD_WIDTH / 2;
        pos.y = coords3.y + GameRules::OSU_COORD_HEIGHT / 2;
    }
    if(cv::playfield_mirror_vertical.getBool()) pos.x = GameRules::OSU_COORD_WIDTH - pos.x;
    if(cv::playfield_mirror_horizontal.getBool()) pos.y = GameRules::OSU_COORD_HEIGHT - pos.y;

    return pos;
}

vec2 BeatmapInterface::replayCoords2Pixels(vec2 coords) const {
    if(cv::playfield_mirror_horizontal.getBool()) coords.y = GameRules::OSU_COORD_HEIGHT - coords.y;
    if(cv::playfield_mirror_vertical.getBool()) coords.x = GameRules::OSU_COORD_WIDTH - coords.x;
    if(cv::playfield_rotation.getFloat() != 0.0f) {
        coords.x -= GameRules::OSU_COORD_WIDTH / 2;
        coords.y -= GameRules::OSU_COORD_HEIGHT / 2;
        vec3 coords3 = vec3(coords.x, coords.y, 0);
        Matrix4 rot;
        rot.rotateZ(cv::playfield_rotation.getFloat());
        coords3 = coords3 * rot;
        coords.x = coords3.x + GameRules::OSU_COORD_WIDTH / 2;
        coords.y = coords3.y + GameRules::OSU_COORD_HEIGHT / 2;
    }

    return coords * this->fScaleFactor + this->vPlayfieldOffset;
}

vec2 BeatmapInterface::osuCoords2LegacyPixels(vec2 coords) const {
    if(osu->getModHR()) coords.y = GameRules::OSU_COORD_HEIGHT - coords.y;
    if(cv::playfield_mirror_horizontal.getBool()) coords.y = GameRules::OSU_COORD_HEIGHT - coords.y;
    if(cv::playfield_mirror_vertical.getBool()) coords.x = GameRules::OSU_COORD_WIDTH - coords.x;

    // rotation (getPlayfieldRotation() is for mods like wobble, cv::playfield_rotation is user preference)
    const f32 rotation = (this->judge ? this->judge->getPlayfieldRotation() : 0.f) + cv::playfield_rotation.getFloat();
    if(rotation != 0.0f) {
        coords.x -= GameRules::OSU_COORD_WIDTH / 2;
        coords.y -= GameRules::OSU_COORD_HEIGHT / 2;

        vec3 coords3 = vec3(coords.x, coords.y, 0);
        Matrix4 rot;
        rot.rotateZ(rotation);

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

f32 BeatmapInterface::osuAngle2PixelAngle(f32 degrees) const {
    f32 rotation =
        degrees - cv::playfield_rotation.getFloat() - (this->judge ? this->judge->getPlayfieldRotation() : 0.f);
    if(flags::has<ModFlags::HardRock>(this->getMods().flags)) rotation = 360.0f - rotation;
    if(cv::playfield_mirror_horizontal.getBool()) rotation = 360.0f - rotation;
    if(cv::playfield_mirror_vertical.getBool()) rotation = 180.0f - rotation;
    return rotation;
}

vec2 BeatmapInterface::getMousePos() const {
    if((this->is_watching && !this->bIsPaused) || BanchoState::spectating) {
        return this->interpolatedMousePos;
    } else {
        return mouse->getPos();
    }
}

bool BeatmapInterface::isCursorOnContinuePoint() const {
    return this->bContinueScheduled && !this->bIsPaused &&
           vec::length(this->getMousePos() - this->vContinueCursorPoint) < (this->fHitcircleDiameter / 2.f);
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
    // calculate stars (of what was played, which the file may not be anymore)
    const PlayedMap &played = *this->judge->getMapData();
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
    const bool flashlight = osu->getModFlashlight();
    const bool hardRock = osu->getModHR();

    const u32 breakDuration = this->judge->getBreakDurationTotal();

    auto diffres = DatabaseBeatmap::loadDifficultyHitObjects(*played.data, AR, CS, speedMultiplier, hardRock);

    DiffCalc::BeatmapDiffcalcData diffcalcData{.sortedHitObjects = diffres.diffobjects,
                                               .CS = CS,
                                               .HP = HP,
                                               .AR = AR,
                                               .OD = OD,
                                               .fileCS = diffres.fileCS,
                                               .fileHP = diffres.fileHP,
                                               .fileOD = diffres.fileOD,
                                               .hidden = hidden,
                                               .relax = relax,
                                               .autopilot = autopilot,
                                               .touchDevice = touchDevice,
                                               .flashlight = flashlight,
                                               .speedMultiplier = speedMultiplier,
                                               .breakDuration = breakDuration};

    DiffCalc::DifficultyAttributes diffAttributesOut{};

    DiffCalc::StarCalcParams params{.outAttributes = diffAttributesOut,
                                    .beatmapData = diffcalcData,
                                    .outAimStrains = nullptr,
                                    .outSpeedStrains = nullptr,
                                    .upToObjectIndex = -1,
                                    .cancelCheck = {},
                                    .strainState = &diffres.strainState};

    const f64 totalStars = DiffCalc::calculateStarDiffForHitObjects(params);

    this->fAimStars = (f32)diffAttributesOut.AimDifficulty;
    this->fSpeedStars = (f32)diffAttributesOut.SpeedDifficulty;

    auto *liveScore = osu->getScore();

    // calculate final pp
    const int numHitObjects = (int)this->judge->getHitObjects().size();
    const i32 maxPossibleCombo = this->judge->iMaxPossibleCombo;
    const int numCircles = static_cast<int>(played.primitives.getNumObjects<DBType::HITCIRCLE>());
    const int numSliders = static_cast<int>(played.primitives.getNumObjects<DBType::SLIDER>());
    const int numSpinners = static_cast<int>(played.primitives.getNumObjects<DBType::SPINNER>());
    const int highestCombo = liveScore->getComboMax();
    const int numMisses = liveScore->getNumMisses();
    const int num300s = liveScore->getNum300s();
    const int num100s = liveScore->getNum100s();
    const int num50s = liveScore->getNum50s();
    const u32 legacyTotalScore = liveScore->getScore();

    DiffCalc::PPv2CalcParams ppv2calcparams{.attributes = diffAttributesOut,
                                            .modFlags = liveScore->mods.flags,
                                            .timescale = speedMultiplier,
                                            .ar = AR,
                                            .od = OD,
                                            .numHitObjects = numHitObjects,
                                            .numCircles = numCircles,
                                            .numSliders = numSliders,
                                            .numSpinners = numSpinners,
                                            .maxPossibleCombo = maxPossibleCombo,
                                            .combo = highestCombo,
                                            .misses = numMisses,
                                            .c300 = num300s,
                                            .c100 = num100s,
                                            .c50 = num50s,
                                            .legacyTotalScore = legacyTotalScore,
                                            .isMcOsuImported = false};

    const f32 pp = DiffCalc::calculatePPv2(ppv2calcparams);

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

    score.client = fmt::format(PACKAGE_NAME "-{}", BanchoState::neomod_version);

    score.unix_timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    if(BanchoState::is_online()) {
        score.player_id = BanchoState::get_uid();
        score.server = BanchoState::endpoint;
    }
    score.playerName = BanchoState::get_username();
    score.passed = isComplete && !isZero && !liveScore->hasDied();
    score.grade = score.passed ? liveScore->getGrade() : ScoreGrade::F;
    // (the score is of what was played: the record only names the same content when the file didn't change)
    score.map = this->beatmap->getMD5() == played.md5 ? this->beatmap : nullptr;
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
    score.perfect = (maxPossibleCombo > 0 && score.comboMax > 0 && score.comboMax >= maxPossibleCombo);
    score.numSliderBreaks = liveScore->getNumSliderBreaks();
    score.unstableRate = liveScore->getUnstableRate();
    score.hitErrorAvgMin = liveScore->getHitErrorAvgMin();
    score.hitErrorAvgMax = liveScore->getHitErrorAvgMax();
    score.maxPossibleCombo = maxPossibleCombo;
    score.numHitObjects = numHitObjects;
    score.numCircles = numCircles;
    score.mods = liveScore->mods;
    if(numSpinners == 0) score.mods.flags &= ~ModFlags::SpunOut;
    if(this->storyboard) score.storyboard_hash = this->storyboard->getHash();
    score.beatmap_hash = played.md5;  // NOTE: necessary for "Use Mods"
    score.replay = this->judge->replay;

    // @PPV3: store ppv3 data if not already done. also double check replay is marked correctly
    score.ppv2_version = DiffCalc::PP_ALGORITHM_VERSION;
    score.ppv2_score = pp;
    score.ppv2_total_stars = totalStars;
    score.ppv2_aim_stars = this->fAimStars;
    score.ppv2_speed_stars = this->fSpeedStars;

    if(isComplete) {
        RichPresence::onPlayEnd(quit);
    }

    if(!isCheated) {
        if(BanchoState::can_submit_scores() && !isZero && this->is_submittable) {
            score.server = BanchoState::endpoint;
            BANCHO::Net::submit_score(score);
            // XXX: Save bancho_score_id after getting submission result
        }

        if(score.passed || cv::save_failed_scores.getBool()) {
            if(!db->addScore(score)) {
                ui->getNotificationOverlay()->addToast("Failed saving score!", ERROR_TOAST);
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

    osu->getScore()->setComboFull(maxPossibleCombo);  // used in RankingScreen/UIRankingScreenRankingPanel

    // special case: incomplete scores should NEVER show pp, even if auto
    if(!isComplete) {
        liveScore->setPPv2(0.0f);
    }

    return score;
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

    this->fNumberScale = PlayfieldView::numberScale(skin, this->fRawHitcircleDiameter, this->fHitcircleDiameter);
    this->fHitcircleOverlapScale =
        PlayfieldView::hitcircleOverlapScale(this->fRawHitcircleDiameter, this->fHitcircleDiameter);

    const f32 sliderFollowCircleDiameterMultiplier =
        cv::mod_jigsaw2.getBool()
            ? (1.0f * (1.0f - cv::mod_jigsaw_followcircle_radius_factor.getFloat()) +
               cv::mod_jigsaw_followcircle_radius_factor.getFloat() * GameRules::SLIDER_FOLLOW_CIRCLE_MULTIPLIER)
            : GameRules::SLIDER_FOLLOW_CIRCLE_MULTIPLIER;
    this->fSliderFollowCircleDiameter = this->fHitcircleDiameter * sliderFollowCircleDiameterMultiplier;
}

void BeatmapInterface::updateSliderVertexBuffers() {
    this->updatePlayfieldMetrics();
    this->updateHitobjectMetrics();

    this->bWasEZEnabled = osu->getModEZ();                    // to avoid useless double updates in onModUpdate()
    this->fPrevHitCircleDiameter = this->fHitcircleDiameter;  // same here
    this->fPrevPlayfieldRotationFromConVar = cv::playfield_rotation.getFloat();  // same here

    if(!this->judge) return;

    debugLog("rebuilding for {:d} hitobjects ...", this->judge->getHitObjects().size());

    for(const auto &hitobject : this->judge->getHitObjects()) {
        hitobject->rebuildVertexBuffer();
    }
}

f32 BeatmapInterface::getBaseAnimationSpeed() const { return osu->getAnimationSpeedMultiplier(); }

f32 BeatmapInterface::getSpeedAdjustedAnimationSpeed() const {
    return this->getSpeedMultiplier() / this->getBaseAnimationSpeed();
}

bool BeatmapInterface::isActuallyPausedAndNotSpectating() const {
    // TODO(spec): need to actually test spectating to make sure i dont break something, so not including that here for now
    // this function is mainly to avoid running through replay frames while we are paused
    // (see while(next_frame.cur_music_pos <= this->iCurMusicPosWithOffsets) ... in update2())
    if(BanchoState::spectating) return false;

    return (this->isPaused() && ui->getPauseOverlay()->isVisible())  //
           && !this->music.isPlaying()                               //
           && !(this->bIsWaiting || this->isActuallyLoading());
}
