// Copyright (c) 2024, kiwec, All rights reserved.
#include "SpectatorScreen.h"

#include "Osu.h"
#include "BackgroundImageHandler.h"
#include "Bancho.h"
#include "BanchoNetworking.h"
#include "BanchoUsers.h"
#include "BeatmapInterface.h"
#include "CBaseUILabel.h"
#include "Changelog.h"
#include "OsuConVars.h"
#include "Downloader.h"
#include "ModSelector.h"
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

static i32 current_map_id = 0;

using namespace Spectating;

namespace Spectating {

// TODO @kiwec: test that those bugs have been fixed

// TODO @kiwec: buglist
// - lagspike every second DURING GAMEPLAY!
// - fps dumping to 0 when spectating
//   - during map download
//   - at end of a map?
// - retrying after score doesn't work
// - retrying after death doesn't work
// - teleporting to ranking screen when user is not playing or just started map
// - cursor "times out" randomly during gameplay
// - pause -> retry results in buffering, then chainmiss for like 10 seconds
//   - with buffer 5000, we start 12 seconds behind, and skip the start of the map :(
// - when seeking or initially spectating, all hitobjects before first replay frame should be ignored

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
    auto notif = fmt::format("Started spectating {:s}", user_info->name);
    ui->getNotificationOverlay()->addToast(notif, SUCCESS_TOAST);

    BanchoState::spectating = true;
    BanchoState::spectated_player_id = user_id;
    current_map_id = 0;

    ui->setScreen(ui->getSpectatorScreen());
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
    auto notif = fmt::format("Stopped spectating {:s}", user_info->name);
    ui->getNotificationOverlay()->addToast(notif, INFO_TOAST);

    BanchoState::spectating = false;
    BanchoState::spectated_player_id = 0;
    current_map_id = 0;

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

    INIT_LABEL(this->spectating, "Spectating", true);
    this->background->container.addBaseUIElement(this->spectating);

    this->userCard = new UserCard(0);
    this->background->container.addBaseUIElement(this->userCard);

    INIT_LABEL(this->status, "...", false);
    this->background->container.addBaseUIElement(this->status);

    this->stop_btn = new UIButton(0, 0, 190, 40, "stop_spec_btn", "Stop spectating");
    this->stop_btn->setGrabClicks(true);
    this->stop_btn->setColor(0xff00d900);
    this->stop_btn->setUseDefaultSkin();
    this->stop_btn->setClickCallback(SA::MakeDelegate<&SpectatorScreen::onStopSpectatingClicked>(this));
    this->addBaseUIElement(this->stop_btn);
}

// NOTE: We use this to control client state, even when the spectator screen isn't visible.
void SpectatorScreen::update(CBaseUIEventCtx &c) {
    // HACK: "spectator screen" is just an overlay with higher priority than most screens
    this->bVisible = BanchoState::spectating && !osu->isInPlayMode() && !ui->getRankingScreen()->isVisible();

    if(!BanchoState::spectating) return;

    // Control client state
    // XXX: should use map_md5 instead of map_id
    const UserInfo *user_info = BANCHO::User::get_user_info(BanchoState::spectated_player_id, true);
    if(user_info->map_id == -1 || user_info->map_id == 0) {
        if(osu->isInPlayMode()) {
            osu->getMapInterface()->stop(true);
        }
    } else if(user_info->mode == GameMode::STANDARD && user_info->map_id != current_map_id) {
        auto beatmap = Downloader::download_beatmap(user_info->map_id, user_info->map_md5, this->map_dl);
        if(beatmap != nullptr) {
            current_map_id = user_info->map_id;
            this->map_dl.reset();
            ui->setScreen(ui->getSpectatorScreen());
            ui->getSongBrowser()->onDifficultySelected(beatmap, false);
            osu->getMapInterface()->spectate();
        }
    }

    // Update spectator screen UI
    static i32 last_player_id = 0;
    if(BanchoState::spectated_player_id != last_player_id) {
        this->userCard->setID(BanchoState::spectated_player_id);
        last_player_id = BanchoState::spectated_player_id;
    }

    this->spectating->setText(fmt::format("Spectating {:s}", user_info->name));

    {
        using enum LiveReplayAction;
        if(LiveReplayAction action = user_info->spec_action;
           action == NONE || action == SONG_SELECT || action == WATCHING_OTHER) {
            std::string_view action_str = action == NONE          ? "AFK"sv
                                          : action == SONG_SELECT ? "picking a map..."sv
                                                                  : "spectating someone else"sv;
            this->status->setText(fmt::format("{:s} is {}", user_info->name, action_str));
        }
    }

    if(user_info->mode != GameMode::STANDARD) {
        this->status->setText(fmt::format("{:s} is playing minigames", user_info->name));
    } else if(user_info->map_id != -1 && user_info->map_id != 0) {
        if(user_info->map_id != current_map_id) {
            if(this->map_dl.failed()) {
                auto error_str = fmt::format("Failed to download Beatmap #{:d} :(", user_info->map_id);
                this->status->setText(error_str);

                static i32 last_failed_map = 0;
                if(user_info->map_id != last_failed_map) {
                    Packet packet;
                    packet.id = OUTP_CANT_SPECTATE;
                    BANCHO::Net::send_packet(packet);

                    last_failed_map = user_info->map_id;
                }
            } else {
                auto text = fmt::format("Downloading map... {:.2f}%", this->map_dl.progress() * 100.f);
                this->status->setText(text);
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

    // Handle spectator screen UI input
    if(this->isVisible()) {
        UIScreen::update(c);
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
