// Copyright (c) 2024, kiwec, All rights reserved.
#include "SpectatorScreen.h"

#include "Osu.h"
#include "BackgroundImageHandler.h"
#include "Bancho.h"
#include "BanchoNetworking.h"
#include "BanchoUsers.h"
#include "BeatmapInterface.h"
#include "Database.h"
#include "CBaseUILabel.h"
#include "OsuConVars.h"
#include "MapFetcher.h"
#include "ModSelector.h"
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
#include "SoundEngine.h"
#include "UI.h"
#include "UIButton.h"
#include "UserCard.h"
#include "Engine.h"

using namespace Spectating;

namespace Spectating {

static i32 current_map_id = 0;
static CONSTINIT MapFetcher map_fetcher;

// TODO @kiwec: buglist
// - when starting to spectate mid-play, we get chainmiss for a second
// - actions such as death are instant when they should happen relative to the scoreframe time (eg spec_buffer later)
// - sometimes if guy switches to non-downloaded map, spectating still starts, but on wrong map
// - spectator screen ui is laggy
// - lagspikes during gameplay
// - spec_buffer should be dynamic instead of a cvar

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
    current_map_id = 0;
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

    BanchoState::spectating = false;
    BanchoState::spectated_player_id = 0;
    current_map_id = 0;
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

// NOTE: We use this to control client state, even when the spectator screen isn't visible.
void SpectatorScreen::tick() {
    UIScreen::tick();

    if(!BanchoState::spectating) return;

    const bool can_load_maps = db->isFinished() && !db->isCancelled();

    // Control client state
    // XXX: should use map_md5 instead of map_id
    const UserInfo *user_info = BANCHO::User::get_user_info(BanchoState::spectated_player_id, true);
    if(user_info->map_id == -1 || user_info->map_id == 0) {
        current_map_id = 0;
        if(osu->isInPlayMode()) {
            osu->getMapInterface()->stop(true);
        }
    } else if(user_info->mode == GameMode::STANDARD && user_info->map_id != current_map_id && can_load_maps) {
        // drive the spectated user's map through the fetcher (retargeting is implicit when they
        // change maps under us); start spectating once it lands.
        map_fetcher.target_map(user_info->map_id, user_info->map_md5);
        if(map_fetcher.tick().status == MapFetcher::Status::Found) {
            auto *diff = map_fetcher.result();
            current_map_id = user_info->map_id;
            map_fetcher.clear();
            ui->setScreen(ui->getSpectatorScreen());
            ui->getSongBrowser()->onDifficultySelected(diff, false);
            osu->getMapInterface()->spectate();
        }
        // failure is handled by the status text below
    }

    // Update spectator screen UI
    static i32 last_player_id = 0;
    if(BanchoState::spectated_player_id != last_player_id) {
        this->userCard->setID(BanchoState::spectated_player_id);
        last_player_id = BanchoState::spectated_player_id;
    }

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
        if(user_info->map_id != current_map_id) {
            const auto &fs = map_fetcher.state();
            if(!can_load_maps) {
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
            } else {
                this->status->setText(tformat("Downloading map... {:.2f}%", fs.progress * 100.f));
            }
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
    // bVisible is computed in tick(), which ran earlier this frame
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
