// Copyright (c) 2024, kiwec, All rights reserved.
#include "SpectatorScreen.h"

#include "Osu.h"
#include "BackgroundImageHandler.h"
#include "Bancho.h"
#include "BanchoNetworking.h"
#include "BanchoProtocol.h"
#include "BanchoUsers.h"
#include "BeatmapInterface.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "CBaseUILabel.h"
#include "OsuConVars.h"
#include "MapFetcher.h"
#include "HitObjects.h"
#include "i18n.h"
#include "KeyBindings.h"
#include "Lobby.h"
#include "Logging.h"
#include "MakeDelegateWrapper.h"
#include "MainMenu.h"
#include "NotificationOverlay.h"
#include "PromptOverlay.h"
#include "RankingScreen.h"
#include "RoomScreen.h"
#include "Skin.h"
#include "SongBrowser.h"
#include "Sound.h"
#include "SoundEngine.h"
#include "UI.h"
#include "UIButton.h"
#include "UserCard.h"
#include "Engine.h"

using namespace Spectating;

namespace Spectating {

static CONSTINIT MapFetcher map_fetcher;

#define INIT_LABEL(label_name, default_text, is_big)                      \
    do {                                                                  \
        label_name = new CBaseUILabel(0, 0, 0, 0, "label", default_text); \
        label_name->setFont(is_big ? lfont : font);                       \
        label_name->setSizeToContent(0, 0);                               \
        label_name->setDrawFrame(false);                                  \
        label_name->setDrawBackground(false);                             \
    } while(0)

void start(int user_id) {
    Spectating::stop();

    Packet packet;
    packet.id = OUTP_START_SPECTATING;
    packet.write<i32>(user_id);
    BANCHO::Net::send_packet(packet);

    const UserInfo *user_info = BANCHO::User::get_user_info(user_id, true);
    auto notif = tformat("Started spectating {:s}", user_info->name);
    ui->getNotificationOverlay()->addToast(notif, SUCCESS_TOAST);

    BanchoState::spectating = true;
    BanchoState::spectated_player_id = user_id;
    map_fetcher.clear();

    if(!db->isFinished() || db->isCancelled()) {
        // TODO: what happens if user cancels db load? probably nothing good...
        ui->getSongBrowser()->refreshBeatmaps(/*next_screen=*/ui->getSpectatorScreen());
    } else {
        ui->setScreen(ui->getSpectatorScreen());
    }

    soundEngine->play(osu->getSkin()->s_menu_hit);
}

void start_by_username(std::string_view username) {
    auto *user = BANCHO::User::find_user(username);
    if(user == nullptr) {
        debugLog("Couldn't find user \"{:s}\"!", username);
        return;
    }

    debugLog("Spectating {:s} (user {:d})...", username, user->user_id);
    Spectating::start(user->user_id);
}

void stop() {
    if(!BanchoState::spectating) return;

    if(osu->isInPlayMode()) {
        osu->getMapInterface()->stop(true);
    }

    const UserInfo *user_info = BANCHO::User::get_user_info(BanchoState::spectated_player_id, true);
    auto notif = tformat("Stopped spectating {:s}", user_info->name);
    ui->getNotificationOverlay()->addToast(notif, INFO_TOAST);

    BanchoState::fellow_spectators.clear();
    BanchoState::spectating = false;
    BanchoState::spectated_player_id = 0;
    map_fetcher.clear();

    Packet packet;
    packet.id = OUTP_STOP_SPECTATING;
    BANCHO::Net::send_packet(packet);

    ui->setScreen(ui->getMainMenu());
    soundEngine->play(osu->getSkin()->s_menu_back);
}

}  // namespace Spectating

SpectatorScreen::SpectatorScreen() {
    this->font = engine->getDefaultFont();
    this->lfont = osu->getSubTitleFont();

    this->pauseButton = new PauseButton(0, 0, 0, 0, "pause_btn", "");
    this->pauseButton->setClickCallback([]() { ui->getMainMenu()->onPausePressed(); });
    this->addBaseUIElement(this->pauseButton);

    this->background = new CBaseUIScrollView(0, 0, 0, 0, "spectator_bg");
    this->background->setDrawFrame(true);
    this->background->setDrawBackground(true);
    this->background->setBackgroundColor(0xdd000000);
    this->background->setHorizontalScrolling(false);
    this->background->setVerticalScrolling(false);
    this->addBaseUIElement(this->background);

    INIT_LABEL(this->spectating, _("Spectating"), true);
    this->background->container.addBaseUIElement(this->spectating);

    this->userCard = new UserCard(0);
    this->background->container.addBaseUIElement(this->userCard);

    INIT_LABEL(this->status, _("..."), false);
    this->background->container.addBaseUIElement(this->status);

    this->stop_btn = new UIButton(0, 0, 190, 40, "stop_spec_btn", _("Stop spectating"));
    this->stop_btn->setDrawsOnTop(true);
    this->stop_btn->setColor(0xff00d900);
    this->stop_btn->setUseDefaultSkin();
    this->stop_btn->setClickCallback(SA::MakeDelegate<&SpectatorScreen::onStopSpectatingClicked>(this));
    this->addBaseUIElement(this->stop_btn);
}

void SpectatorScreen::controlClientState() {
    if(!BanchoState::spectating) return;

    const bool can_load_maps = db->isFinished() && !db->isCancelled();
    auto *map_iface = osu->getMapInterface();

    // while in play mode, push all gameplay frames to map_iface
    // a "gameplay" frame is any frame that doesn't require us to do an action (such as seeking or dying)
    if(osu->isInPlayMode()) {
        i32 last_music_pos = -1000;
        if(!map_iface->spectated_replay.empty()) last_music_pos = map_iface->spectated_replay.back().cur_music_pos;

        auto it =
            std::find_if(this->player_updates.begin(), this->player_updates.end(), [&](const RemotePlayerUpdate &u) {
                return u.music_pos < last_music_pos || u.map_id != map_iface->beatmap->getID() ||
                       u.action != LiveReplayAction::NONE;
            });
        for(auto u = this->player_updates.begin(); u != it; u++) {
            for(auto frame : u->replay_frames) {
                map_iface->spectated_replay.push_back(frame);
            }
            map_iface->score_frames.push_back(u->score);
        }
        this->player_updates.erase(this->player_updates.begin(), it);
    }

    // gameplay buffer management & auto-seek (forwards only)
    // TODO: instead of using cv::spec_buffer, we should dynamically change the buffer size
    //       based on network ping & jitter (or even simply just expanding it when buffering)
    if(osu->isInPlayMode()) {
        i32 leeway = map_iface->getSpectatingLeeway();
        if(map_iface->is_buffering) {
            // make sure music is actually paused
            if(map_iface->music->isPlaying()) {
                soundEngine->pause(map_iface->music);
                map_iface->bIsPlaying = false;
                map_iface->bIsPaused = true;
            }

            if(leeway >= cv::spec_buffer.getInt()) {
                debugLog("UNPAUSING: leeway: {:d}, iCurMusicPos: {:d}", leeway, map_iface->iCurMusicPos);
                soundEngine->play(map_iface->music);
                map_iface->bIsPlaying = true;
                map_iface->bIsPaused = false;
                map_iface->is_buffering = false;
            }
        } else {
            HitObject *lastHitObject =
                map_iface->hitobjectsSortedByEndTime.size() > 0 ? map_iface->hitobjectsSortedByEndTime.back() : nullptr;
            bool is_finished = lastHitObject != nullptr && lastHitObject->isFinished();

            if(leeway <= 0 && !is_finished) {
                debugLog("PAUSING: leeway: {:d}, iCurMusicPos: {:d}", leeway, map_iface->iCurMusicPos);
                soundEngine->pause(map_iface->music);
                map_iface->bIsPlaying = false;
                map_iface->bIsPaused = true;
                map_iface->is_buffering = true;
            }
        }

        // make sure we're not too far behind the liveplay
        if(!map_iface->spectated_replay.empty()) {
            if(leeway > 2 * cv::spec_buffer.getInt()) {
                i32 target = std::max(map_iface->spectated_replay.front().cur_music_pos,
                                      map_iface->spectated_replay.back().cur_music_pos - cv::spec_buffer.getInt());
                debugLog("We're {:d}ms behind, seeking to catch up to player...", target - map_iface->iCurMusicPos);
                map_iface->bTempSeekNF = true;
                map_iface->seekMS(std::max(0, target));
                return;
            }
        }
    }

    // wait until we empty the replay buffer before doing anything
    // ...unless we are buffering, or else we could get stuck waiting for frames which will never come
    if(map_iface->getSpectatingLeeway() > 0 && !map_iface->is_buffering) return;

    // clear all frames that aren't from the remote player's current map
    // (so we can be sure that user_info->map_id is our target)
    UserInfo *user_info = BANCHO::User::get_user_info(BanchoState::spectated_player_id, true);
    auto it = std::find_if(this->player_updates.begin(), this->player_updates.end(),
                           [&](const RemotePlayerUpdate &u) { return u.map_id == user_info->map_id; });
    this->player_updates.erase(this->player_updates.begin(), it);

    // check if we need to switch map
    if(can_load_maps && (map_iface->beatmap == nullptr || map_iface->beatmap->getID() != user_info->map_id)) {
        if(osu->isInPlayMode()) {
            // this will also send us back to the spectator screen, if user quits the map
            map_iface->stop(true);
        }

        // drive the spectated user's map through the fetcher (retargeting is implicit when they
        // change maps under us); start spectating once it lands.
        map_fetcher.target_map(user_info->map_id, user_info->map_md5);
        if(map_fetcher.tick().status == MapFetcher::Status::Found) {
            auto *diff = map_fetcher.result();
            map_fetcher.clear();
            ui->getSongBrowser()->onDifficultySelected(diff, false);
            osu->getMapInterface()->spectate();
        }
    }

    // we'll assume we are in gameplay and only process 1 non-standard frame per tick for sanity
    if(!osu->isInPlayMode()) return;
    if(this->player_updates.empty()) return;
    const auto &update = this->player_updates[0];

    // check if we need to seek backwards
    i32 last_music_pos = -1000;
    if(!map_iface->spectated_replay.empty()) last_music_pos = map_iface->spectated_replay.back().cur_music_pos;
    if(update.music_pos < last_music_pos) {
        i32 target = update.music_pos - cv::spec_buffer.getInt();
        if(target <= 0) {
            debugLog("Remote player seeked to {:d}ms, restarting map", update.music_pos);
            map_iface->spectated_replay.clear();
            map_iface->score_frames.clear();
            map_iface->is_buffering = true;
            map_iface->restart(true);
        } else {
            debugLog("Remote player seeked backwards, seeking to {:d}ms", target);
            map_iface->bTempSeekNF = true;
            map_iface->seekMS(target);
        }

        this->player_updates.erase(this->player_updates.begin());
        return;
    }

    // process gameplay actions
    switch(update.action) {
        using enum LiveReplayAction;
        case NONE: {
            // don't delete this frame!
            return;
        }
        case SKIP: {
            map_iface->skipEmptySection();
            break;
        }
        case FAIL: {
            map_iface->fail(true);
            break;
        }
        case PAUSE: {
            map_iface->spectate_pause = true;
            break;
        }
        case UNPAUSE: {
            map_iface->spectate_pause = false;
            break;
        }
        case WATCHING_OTHER:  // fallthrough
        case SONG_SELECT: {
            // some servers do a little too much caching, let's kick ourselves off the map
            user_info->map_id = 0;
            user_info->map_md5.clear();
            break;
        }
        default:
            break;
    }
    this->player_updates.erase(this->player_updates.begin());
}

void SpectatorScreen::tick() {
    UIScreen::tick();
    this->controlClientState();
    if(!this->isVisible()) return;

    static i32 last_player_id = 0;
    if(BanchoState::spectated_player_id != last_player_id) {
        this->userCard->setID(BanchoState::spectated_player_id);
        last_player_id = BanchoState::spectated_player_id;
    }

    const UserInfo *user_info = BANCHO::User::get_user_info(BanchoState::spectated_player_id, true);
    this->spectating->setText(tformat("Spectating {:s}", user_info->name));

    {
        using enum LiveReplayAction;
        if(LiveReplayAction action = user_info->spec_action;
           action == NONE || action == SONG_SELECT || action == WATCHING_OTHER) {
            std::string_view action_str = action == NONE          ? _("AFK")
                                          : action == SONG_SELECT ? _("picking a map...")
                                                                  : _("spectating someone else");
            this->status->setText(tformat("{:s} is {}", user_info->name, action_str));
        }
    }

    if(user_info->mode != GameMode::STANDARD) {
        this->status->setText(tformat("{:s} is playing minigames", user_info->name));
    } else if(user_info->map_id != -1 && user_info->map_id != 0) {
        const auto &fs = map_fetcher.state();
        if(!db->isFinished() || db->isCancelled()) {
            // this text shouldn't be visible, it's a failsafe in case we fucked up the complex db loading logic
            this->status->setText(tformat("Database not loaded, cannot install map"));
        } else if(fs.status == MapFetcher::Status::NotFound) {
            // TODO: more detailed error message
            this->status->setText(tformat("Failed to download Beatmap #{:d} :(", user_info->map_id));

            if(user_info->map_id != this->last_failed_map) {
                Packet packet;
                packet.id = OUTP_CANT_SPECTATE;
                BANCHO::Net::send_packet(packet);
                this->last_failed_map = user_info->map_id;
            }
        } else if(fs.status == MapFetcher::Status::Working) {
            // map download overlay already shows download progress
            this->status->setText(_("Downloading map..."));
        }
    }

    const float dpiScale = Osu::getUIScale();
    auto resolution = osu->getVirtScreenSize();
    this->setPos(0, 0);
    this->setSize(resolution);

    this->pauseButton->setSize(30 * dpiScale, 30 * dpiScale);
    this->pauseButton->setPos(resolution.x - this->pauseButton->getSize().x * 2 - 10 * dpiScale,
                              this->pauseButton->getSize().y + 10 * dpiScale);
    this->pauseButton->setPaused(!osu->getMapInterface()->isPreviewMusicPlaying());

    this->background->setSize(resolution.x * 0.6, resolution.y * 0.6 - 110 * dpiScale);
    auto bgsize = this->background->getSize();
    this->background->setPos(resolution.x / 2.0 - bgsize.x / 2.0, resolution.y / 2.0 - bgsize.y / 2.0);

    {
        this->spectating->setSizeToContent();
        this->spectating->setRelPos(bgsize.x / 2.f - this->spectating->getSize().x / 2.f,
                                    bgsize.y / 2.f - 100 * dpiScale);

        // XXX: don't use SongBrowser::getUIScale
        this->userCard->setSize(SongBrowser::getUIScale(320), SongBrowser::getUIScale(75));
        auto cardsize = this->userCard->getSize();
        this->userCard->setRelPos(bgsize.x / 2.f - cardsize.x / 2.f, bgsize.y / 2.f - cardsize.y / 2.f);

        this->status->setTextJustification(TEXT_JUSTIFICATION::CENTERED);
        this->status->setRelPos(bgsize.x / 2.f, bgsize.y / 2.f + 100 * dpiScale);
    }
    this->background->setScrollSizeToContent();

    auto stop_pos = this->background->getPos();
    stop_pos.x += bgsize.x / 2.f - this->stop_btn->getSize().x / 2.f;
    stop_pos.y += bgsize.y + 20 * dpiScale;
    this->stop_btn->setPos(stop_pos);
}

void SpectatorScreen::updateInput(CBaseUIEventCtx &c) {
    if(this->isVisible()) {
        UIScreen::updateInput(c);
    }
}

void SpectatorScreen::draw() {
    if(!this->isVisible()) return;

    if(cv::draw_spectator_background_image.getBool()) {
        osu->getBackgroundImageHandler()->draw(osu->getMapInterface()->getBeatmap());
    }

    UIScreen::draw();
}

void SpectatorScreen::onKeyDown(KeyboardEvent &key) {
    if(!this->isVisible()) return;

    if(key.getScanCode() == KEY_ESCAPE) {
        key.consume();
        this->onStopSpectatingClicked();
        return;
    }

    UIScreen::onKeyDown(key);
}

void SpectatorScreen::onStopSpectatingClicked() { Spectating::stop(); }

void SpectatorScreen::handleFrameBundle(Packet &packet) {
    if(!BanchoState::spectating) return;

    UserInfo *info = BANCHO::User::get_user_info(BanchoState::spectated_player_id, true);
    auto *map_iface = osu->getMapInterface();

    RemotePlayerUpdate update;
    update.map_id = info->map_id;
    update.map_md5 = info->map_md5;

    update.music_pos = -1000;
    if(map_iface->beatmap && map_iface->beatmap->getID() == update.map_id && !map_iface->spectated_replay.empty()) {
        update.music_pos = map_iface->spectated_replay.back().cur_music_pos;
    }

    i32 extra = packet.read<i32>();
    (void)extra;  // this is mania seed or something we can't use

    u16 nb_frames = packet.read<u16>();
    for(u16 i = 0; i < nb_frames; i++) {
        auto frame = packet.read<LiveReplayFrame>();

        // stable sometimes sends frames with a huge negative time value. probably some magic number,
        // but i have no idea how to parse it, so let's ignore it just like the sequence number.
        if(frame.time < -1000) continue;

        update.replay_frames.push_back(LegacyReplay::Frame{
            .cur_music_pos = frame.time,
            .milliseconds_since_last_frame = 0,  // set below
            .x = frame.mouse_x,
            .y = frame.mouse_y,
            .key_flags = frame.key_flags,
        });
    }

    auto action = (LiveReplayAction)packet.read<u8>();
    info->spec_action = action;
    update.action = action;

    update.score = packet.read<ScoreFrame>();

    auto sequence = packet.read<u16>();
    (void)sequence;  // don't know how to use this

    // some clients send frames in the wrong order, so we're correcting it here.
    std::ranges::sort(update.replay_frames, [](const LegacyReplay::Frame &a, const LegacyReplay::Frame &b) {
        return a.cur_music_pos < b.cur_music_pos;
    });

    // fix milliseconds_since_last_frame and update.music_pos
    for(auto &frame : update.replay_frames) {
        frame.milliseconds_since_last_frame = std::max(0, frame.cur_music_pos - update.music_pos);
        update.music_pos = frame.cur_music_pos;
    }

    this->player_updates.push_back(update);
}
