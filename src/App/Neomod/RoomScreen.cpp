// Copyright (c) 2024, kiwec, All rights reserved.
#include "RoomScreen.h"

#include <sstream>
#include <utility>

#include "BackgroundImageHandler.h"
#include "Bancho.h"
#include "BanchoNetworking.h"
#include "BanchoUsers.h"
#include "BeatmapInterface.h"
#include "CBaseUIContainer.h"
#include "CBaseUILabel.h"
#include "CBaseUITextbox.h"
#include "Changelog.h"
#include "Chat.h"
#include "OsuConVars.h"
#include "Database.h"
#include "MakeDelegateWrapper.h"
#include "Downloader.h"
#include "Engine.h"
#include "HUD.h"
#include "Keyboard.h"
#include "LegacyReplay.h"
#include "Lobby.h"
#include "MainMenu.h"
#include "ModSelector.h"
#include "Mouse.h"
#include "NotificationOverlay.h"
#include "OptionsOverlay.h"
#include "Osu.h"
#include "OsuDirectScreen.h"
#include "PromptOverlay.h"
#include "RankingScreen.h"
#include "ResourceManager.h"
#include "RichPresence.h"
#include "Skin.h"
#include "SkinImage.h"
#include "SongBrowser/SongBrowser.h"
#include "SongBrowser/SongButton.h"
#include "SoundEngine.h"
#include "SpectatorScreen.h"
#include "UI.h"
#include "UIAvatar.h"
#include "UIButton.h"
#include "UICheckbox.h"
#include "UIContextMenu.h"
#include "UIUserContextMenu.h"
#include "Logging.h"

using namespace flags::operators;

void UIModList::draw() {
    // TODO: when can this->flags actually change? why can't we just store the flags ourselves instead of using a pointer
    if(this->last_flags != *this->flags) {
        this->last_flags = *this->flags;

        Replay::Mods temp = Replay::Mods::from_legacy(*this->flags);
        // clientside nightcore
        if(cv::nightcore_enjoyer.getBool() && (temp.speed == 1.5f || temp.speed == 0.75f)) {
            temp.flags |= ModFlags::NoPitchCorrection;
        } else {
            temp.flags &= ~ModFlags::NoPitchCorrection;
        }

        this->mod_images.clear();
        Skin::getModImagesForMods(this->mod_images, temp);
    }

    g->setColor(0xffffffff);
    vec2 mod_pos = this->getPos();

    const auto *skin = osu->getSkin();
    for(const auto memb : this->mod_images) {
        const SkinImage *mod_icon = skin->*memb;
        float target_height = this->getSize().y;
        float scaling_factor = target_height / mod_icon->getSize().y;
        float target_width = mod_icon->getSize().x * scaling_factor;

        vec2 fixed_pos = mod_pos;
        fixed_pos.x += (target_width / 2);
        fixed_pos.y += (target_height / 2);
        mod_icon->draw(fixed_pos, scaling_factor);

        // Overlap mods a bit, just like peppy's client does
        // TODO: check if this is right... should probably re-use the logic in RankingScreen?
        mod_pos.x += target_width * 0.6f;
    }
}

bool UIModList::isVisible() { return !!*this->flags; }

#define INIT_LABEL(label_name, default_text, is_big)                        \
    do {                                                                    \
        (label_name) = new CBaseUILabel(0, 0, 0, 0, "label", default_text); \
        (label_name)->setFont((is_big) ? lfont : font);                     \
        (label_name)->setSizeToContent(0, 0);                               \
        (label_name)->setDrawFrame(false);                                  \
        (label_name)->setDrawBackground(false);                             \
    } while(0)

#define ADD_ELEMENT_WITH_PADDING(element, x_padding, y_padding)  \
    do {                                                         \
        (element)->onResized();                                  \
        (element)->setSizeToContent(x_padding, y_padding);       \
        (element)->setPos(10.f * Osu::getUIScale(), settings_y); \
        this->settings->container.addBaseUIElement(element);     \
        settings_y += (element)->getSize().y;                    \
    } while(0)

#define ADD_ELEMENT(element) ADD_ELEMENT_WITH_PADDING(element, button_padding, button_padding)

#define ADD_BUTTON(button, label)                                                                     \
    do {                                                                                              \
        (label)->onResized();                                                                         \
        (label)->setSizeToContent(0, 0);                                                              \
        (button)->onResized();                                                                        \
        (button)->setSizeToContent(button_padding, button_padding);                                   \
        (button)->setPos((label)->getSize().x + 20.f * Osu::getUIScale(),                             \
                         (label)->getPos().y + ((label)->getSize().y - (button)->getSize().y) / 2.f); \
        this->settings->container.addBaseUIElement(button);                                           \
    } while(0)

#define PAD(x)                               \
    do {                                     \
        settings_y += x * Osu::getUIScale(); \
    } while(0)

RoomScreen::RoomScreen() : UIScreen() {
    this->font = engine->getDefaultFont();
    this->lfont = osu->getSubTitleFont();

    this->pauseButton = new PauseButton(0, 0, 0, 0, "pause_btn", "");
    this->pauseButton->setClickCallback([]() { ui->getMainMenu()->onPausePressed(); });
    this->addBaseUIElement(this->pauseButton);

    this->settings = new CBaseUIScrollView(0, 0, 0, 0, "room_settings");
    this->settings->setDrawFrame(false);  // it's off by 1 pixel, turn it OFF
    this->settings->setDrawBackground(true);
    this->settings->setBackgroundColor(0xdd000000);
    this->settings->setHorizontalScrolling(false);
    this->addBaseUIElement(this->settings);

    INIT_LABEL(this->room_name, "Multiplayer room", true);
    INIT_LABEL(this->host, "Host: None", false);  // XXX: make it an UIUserLabel

    INIT_LABEL(this->room_name_iptl, "Room name", false);
    this->room_name_ipt = new CBaseUITextbox(0, 0, this->settings->getSize().x, 40, "");
    this->room_name_ipt->setText("Multiplayer room");

    this->change_password_btn = new UIButton(0, 0, 0, 0, "change_password_btn", "Change password");
    this->change_password_btn->setColor(0xff0c7c99);
    this->change_password_btn->setUseDefaultSkin();
    this->change_password_btn->setClickCallback(SA::MakeDelegate<&RoomScreen::onChangePasswordClicked>(this));

    INIT_LABEL(this->win_condition, "Win condition: Score", false);
    this->change_win_condition_btn = new UIButton(0, 0, 0, 0, "change_win_condition_btn", "Win condition: Score");
    this->change_win_condition_btn->setColor(0xff00d900);
    this->change_win_condition_btn->setUseDefaultSkin();
    this->change_win_condition_btn->setClickCallback(SA::MakeDelegate<&RoomScreen::onChangeWinConditionClicked>(this));

    INIT_LABEL(map_label, "Beatmap", true);
    this->select_map_btn = new UIButton(0, 0, 0, 0, "select_map_btn", "Select map");
    this->select_map_btn->setColor(0xff0c7c99);
    this->select_map_btn->setUseDefaultSkin();
    this->select_map_btn->setClickCallback(SA::MakeDelegate<&RoomScreen::onSelectMapClicked>(this));
    this->online_maps_btn = new UIButton(0, 0, 0, 0, "online_maps_btn", "Download maps");
    this->online_maps_btn->setColor(0xff0c7c99);
    this->online_maps_btn->setUseDefaultSkin();
    this->online_maps_btn->setClickCallback(SA::MakeDelegate<&RoomScreen::onDownloadMapsClicked>(this));

    INIT_LABEL(this->map_title, "(no map selected)", false);
    INIT_LABEL(this->map_stars, "", false);
    INIT_LABEL(this->map_attributes, "", false);
    INIT_LABEL(this->map_attributes2, "", false);

    INIT_LABEL(mods_label, "Mods", true);
    this->select_mods_btn = new UIButton(0, 0, 0, 0, "select_mods_btn", "Select mods [F1]");
    this->select_mods_btn->setColor(0xff0c7c99);
    this->select_mods_btn->setUseDefaultSkin();
    this->select_mods_btn->setClickCallback(SA::MakeDelegate<&RoomScreen::onSelectModsClicked>(this));
    this->freemod = new UICheckbox(0, 0, 0, 0, "allow_freemod", "Freemod");
    this->freemod->setDrawFrame(false);
    this->freemod->setDrawBackground(false);
    this->freemod->setChangeCallback(SA::MakeDelegate<&RoomScreen::onFreemodCheckboxChanged>(this));
    this->mods = new UIModList(&BanchoState::room.mods);
    INIT_LABEL(this->no_mods_selected, "No mods selected.", false);

    this->ready_btn = new UIButton(0, 0, 0, 0, "start_game_btn", "Start game");
    this->ready_btn->setColor(0xff00d900);
    this->ready_btn->setUseDefaultSkin();
    this->ready_btn->setClickCallback(SA::MakeDelegate<&RoomScreen::onReadyButtonClick>(this));
    this->ready_btn->is_loading = true;

    this->player_list_label = new CBaseUILabel(50, 50, 0, 0, "label", "Player list");
    this->player_list_label->setFont(this->lfont);
    this->player_list_label->setSizeToContent(0, 0);
    this->player_list_label->setDrawFrame(false);
    this->player_list_label->setDrawBackground(false);
    this->addBaseUIElement(this->player_list_label);

    this->slotlist = new CBaseUIScrollView(50, 90, 0, 0, "slot_list");
    this->slotlist->setDrawFrame(true);
    this->slotlist->setDrawBackground(true);
    this->slotlist->setBackgroundColor(0xdd000000);
    this->slotlist->setHorizontalScrolling(false);
    this->addBaseUIElement(this->slotlist);

    this->contextMenu = new UIContextMenu(50, 50, 150, 0, "", this->settings);
    this->addBaseUIElement(this->contextMenu);

    this->updateLayout(osu->getVirtScreenSize());
}

RoomScreen::~RoomScreen() {
    this->settings->invalidate();
    SAFE_DELETE(this->room_name);
    SAFE_DELETE(this->change_password_btn);
    SAFE_DELETE(this->host);
    SAFE_DELETE(this->room_name_iptl);
    SAFE_DELETE(this->room_name_ipt);
    SAFE_DELETE(this->select_map_btn);
    SAFE_DELETE(this->online_maps_btn);
    SAFE_DELETE(this->select_mods_btn);
    SAFE_DELETE(this->change_win_condition_btn);
    SAFE_DELETE(this->win_condition);
    SAFE_DELETE(this->map_label);
    SAFE_DELETE(this->map_title);
    SAFE_DELETE(this->map_stars);
    SAFE_DELETE(this->map_attributes);
    SAFE_DELETE(this->map_attributes2);
    SAFE_DELETE(this->mods_label);
    SAFE_DELETE(this->freemod);
    SAFE_DELETE(this->no_mods_selected);
    SAFE_DELETE(this->mods);
    SAFE_DELETE(this->ready_btn);
}

void RoomScreen::draw() {
    if(!BanchoState::is_in_a_multi_room() || osu->isInPlayMode()) return;

    // TODO: don't download things in draw(), or do any heavy state changing logic like on_map_change

    static i32 current_map_id = -1;
    if(BanchoState::room.map_id == -1 || BanchoState::room.map_id == 0) {
        this->map_title->setText("Host is selecting a map...");
        this->map_title->setSizeToContent(0, 0);
        this->ready_btn->is_loading = true;
    } else if(BanchoState::room.map_id != current_map_id) {
        auto beatmap = Downloader::download_beatmap(BanchoState::room.map_id, BanchoState::room.map_md5, this->map_dl);
        if(this->map_dl.failed()) {
            std::string error_str = fmt::format("Failed to download Beatmap #{:d} :(", BanchoState::room.map_id);
            this->map_title->setText(error_str);
            this->map_title->setSizeToContent(0, 0);
            this->ready_btn->is_loading = true;
        } else if(beatmap != nullptr) {
            current_map_id = BanchoState::room.map_id;
            this->map_dl.reset();
            this->on_map_change();
        } else {
            std::string text = fmt::format("Downloading... {:.2f}%", this->map_dl.progress() * 100.f);
            this->map_title->setText(text);
            this->map_title->setSizeToContent(0, 0);
            this->ready_btn->is_loading = true;
        }
    }

    // XXX: Add convar for toggling room backgrounds
    osu->getBackgroundImageHandler()->draw(osu->getMapInterface()->getBeatmap());
    UIScreen::draw();
}

void RoomScreen::update(CBaseUIEventCtx &c) {
    if(!BanchoState::is_in_a_multi_room() || osu->isInPlayMode()) return;

    const bool room_name_changed = this->room_name_ipt->getText() != BanchoState::room.name;
    if(BanchoState::room.is_host() && room_name_changed) {
        // XXX: should only update 500ms after last input
        BanchoState::room.name = this->room_name_ipt->getText().toUtf8();

        Packet packet;
        packet.id = OUTP_MATCH_CHANGE_SETTINGS;
        BanchoState::room.pack(packet);
        BANCHO::Net::send_packet(packet);

        // Update room name in rich presence info
        RichPresence::onMultiplayerLobby();
    }

    this->pauseButton->setPaused(!osu->getMapInterface()->isPreviewMusicPlaying());

    this->contextMenu->update(c);
    if(c.mouse_consumed()) return;

    // HACK: disable "slotlist" scrollview when options menu is open, because it somehow takes priority
    if(ui->getOptionsOverlay()->isVisible()) {
        this->settings->update(c);
    } else {
        UIScreen::update(c);
    }
}

void RoomScreen::onKeyDown(KeyboardEvent &key) {
    if(!this->bVisible || ui->getOptionsOverlay()->isVisible()) return;

    if(key.getScanCode() == KEY_ESCAPE) {
        key.consume();

        static f64 last_escape_press = 0.0;
        if(last_escape_press + 1.0 < engine->getTime()) {
            last_escape_press = engine->getTime();
            ui->getNotificationOverlay()->addNotification("Hit 'Escape' once more to exit this multiplayer match.",
                                                          0xffffffff, false, 0.75f);
        } else {
            this->ragequit();
        }

        return;
    }

    if(key.getScanCode() == KEY_F1) {
        key.consume();
        if(BanchoState::room.freemods || BanchoState::room.is_host()) {
            ui->setScreen(ui->getModSelector());
        }
        return;
    }

    UIScreen::onKeyDown(key);
}

void RoomScreen::onKeyUp(KeyboardEvent &key) {
    if(!this->bVisible) return;
    UIScreen::onKeyUp(key);
}

void RoomScreen::onChar(KeyboardEvent &key) {
    if(!this->bVisible) return;
    UIScreen::onChar(key);
}

void RoomScreen::onResolutionChange(vec2 newResolution) { this->updateLayout(newResolution); }

CBaseUIContainer *RoomScreen::setVisible(bool visible) {
    if(this->bVisible == visible) return this;

    // NOTE: Calling setVisible(false) does not quit the room! Call ragequit() instead.
    this->bVisible = visible;

    if(visible) {
        soundEngine->play(osu->getSkin()->s_menu_back);
    }

    ui->getChat()->updateVisibility();
    return this;
}

void RoomScreen::updateSettingsLayout(vec2 newResolution) {
    const f32 button_padding = 10.f * Osu::getUIScale();
    const bool is_host = BanchoState::room.is_host();
    int settings_y = 10.f * Osu::getUIScale();

    this->settings->invalidate();
    this->settings->setPos(std::round(newResolution.x * 0.6f), 0);
    this->settings->setSize(std::round(newResolution.x * 0.4f), newResolution.y);

    // Room name (title)
    this->room_name->setText(BanchoState::room.name);
    this->room_name->setSizeToContent();
    ADD_ELEMENT(this->room_name);
    if(is_host) {
        ADD_BUTTON(this->change_password_btn, this->room_name);
    }

    // Host name
    if(!is_host) {
        UString host_str = "Host: None";
        if(BanchoState::room.host_id != 0) {
            const auto *host = BANCHO::User::get_user_info(BanchoState::room.host_id, true);
            host_str = fmt::format("Host: {}", host->name.c_str());
        }
        this->host->setText(host_str);
        ADD_ELEMENT(this->host);
    }

    if(is_host) {
        // Room name (input)
        ADD_ELEMENT(this->room_name_iptl);
        this->room_name_ipt->setSize(this->settings->getSize().x - 20.f * Osu::getUIScale(), 40.f * Osu::getUIScale());
        ADD_ELEMENT(this->room_name_ipt);
        PAD(10.f);
    }

    // Win condition
    if(BanchoState::room.win_condition == WinCondition::SCOREV1) {
        this->win_condition->setText("Win condition: Score");
    } else if(BanchoState::room.win_condition == WinCondition::ACCURACY) {
        this->win_condition->setText("Win condition: Accuracy");
    } else if(BanchoState::room.win_condition == WinCondition::CURRENT_COMBO) {
        this->win_condition->setText("Win condition: Combo");
    } else if(BanchoState::room.win_condition == WinCondition::SCOREV2) {
        this->win_condition->setText("Win condition: ScoreV2");
    } else {
        this->win_condition->setText("Win condition: ???");
    }
    if(is_host) {
        this->change_win_condition_btn->setText(this->win_condition->getText());
        this->change_win_condition_btn->setSizeToContent(button_padding, button_padding);
        ADD_ELEMENT(this->change_win_condition_btn);
    } else {
        ADD_ELEMENT(this->win_condition);
    }

    // Beatmap
    PAD(20.f);
    ADD_ELEMENT(map_label);
    if(is_host) {
        ADD_BUTTON(this->select_map_btn, map_label);

        // Add "Download maps" button to the right of the "Select map" button
        // Jank code because the obvious setPos() wasn't working and I gave up
        // after 1h of debugging this piece of shit UI framework.
        this->online_maps_btn->onResized();
        this->online_maps_btn->setSizeToContent(button_padding, button_padding);
        this->online_maps_btn->setPos(
            map_label->getSize().x + this->select_map_btn->getSize().x + 30.f * Osu::getUIScale(),
            map_label->getPos().y + (map_label->getSize().y - this->online_maps_btn->getSize().y) / 2.f);
        this->settings->container.addBaseUIElement(this->online_maps_btn);
    }
    ADD_ELEMENT(this->map_title);
    if(!this->ready_btn->is_loading) {
        ADD_ELEMENT(this->map_stars);
        ADD_ELEMENT(this->map_attributes);
        ADD_ELEMENT(this->map_attributes2);
    }

    // Mods
    PAD(20.f);
    ADD_ELEMENT(mods_label);
    if(is_host || BanchoState::room.freemods) {
        ADD_BUTTON(this->select_mods_btn, mods_label);
    }
    if(is_host) {
        this->freemod->setChecked(BanchoState::room.freemods);
        ADD_ELEMENT(this->freemod);
    }
    if(!BanchoState::room.mods) {
        ADD_ELEMENT(this->no_mods_selected);
    } else {
        this->mods->flags = &BanchoState::room.mods;
        this->mods->setSize(300.f * Osu::getUIScale(), 90.f * Osu::getUIScale());
        ADD_ELEMENT(this->mods);
    }

    // Ready button
    int nb_ready = 0;
    bool is_ready = false;
    for(auto &slot : BanchoState::room.slots) {
        if(slot.has_player() && slot.is_ready()) {
            nb_ready++;
            if(slot.player_id == BanchoState::get_uid()) {
                is_ready = true;
            }
        }
    }
    if(is_host && is_ready && nb_ready > 1) {
        const std::string force_start_str =
            BanchoState::room.all_players_ready()
                ? "Start game"
                : fmt::format("Force start ({:d}/{:d})", nb_ready, BanchoState::room.nb_players);
        this->ready_btn->setText(force_start_str);
        this->ready_btn->setColor(0xff00d900);
    } else {
        this->ready_btn->setText(is_ready ? "Not ready" : "Ready!");
        this->ready_btn->setColor(is_ready ? 0xffd90000 : 0xff00d900);
    }
    PAD(20.f);
    ADD_ELEMENT_WITH_PADDING(this->ready_btn, button_padding * 10, button_padding * 1.5);

    this->settings->setScrollSizeToContent();
}

void RoomScreen::updateLayout(vec2 newResolution) {
    this->setSize(newResolution);
    this->updateSettingsLayout(newResolution);

    const float dpiScale = Osu::getUIScale();
    this->pauseButton->setSize(30 * dpiScale, 30 * dpiScale);
    this->pauseButton->setPos(std::round(newResolution.x * 0.6f) - this->pauseButton->getSize().x * 2 - 10 * dpiScale,
                              this->pauseButton->getSize().y + 10 * dpiScale);

    this->player_list_label->onResized();
    this->player_list_label->setSizeToContent(0, 0);

    // XXX: Display detailed user presence
    this->slotlist->setPosY(100.f * dpiScale);
    this->slotlist->setSize(newResolution.x * 0.6f - 200.f * dpiScale, newResolution.y * 0.6f - 110.f * dpiScale);
    this->slotlist->freeElements();
    i32 y_total = 10.f * dpiScale;
    for(auto &slot : BanchoState::room.slots) {
        if(slot.has_player()) {
            const auto *user_info = BANCHO::User::get_user_info(slot.player_id, true);

            auto color = 0xffffffff;
            std::string username = user_info->name;
            if(slot.is_player_playing()) {
                username = fmt::format("[playing] {}", user_info->name);
            } else if(slot.no_map()) {
                username = fmt::format("[no map] {}", user_info->name);
            } else if(slot.is_ready()) {
                color = 0xff00ff00;
            }

            const f32 SLOT_HEIGHT = 40.f * dpiScale;
            auto avatar =
                new UIAvatar(this->slotlist, slot.player_id, 10.f * dpiScale, y_total, SLOT_HEIGHT, SLOT_HEIGHT);
            this->slotlist->container.addBaseUIElement(avatar);

            auto user_box = new UIUserLabel(slot.player_id, username);
            user_box->setFont(this->lfont);
            user_box->setPos(avatar->getRelPos().x + avatar->getSize().x + 10, y_total);
            user_box->setTextColor(color);
            user_box->setSizeToContent();
            user_box->setSize(user_box->getSize().x, SLOT_HEIGHT);
            this->slotlist->container.addBaseUIElement(user_box);

            auto user_mods = new UIModList(&slot.mods);
            user_mods->setPos(user_box->getPos().x + user_box->getSize().x + 30.f * dpiScale, y_total);
            user_mods->setSize(350.f * dpiScale, SLOT_HEIGHT);
            this->slotlist->container.addBaseUIElement(user_mods);

            y_total += SLOT_HEIGHT + 5.f * dpiScale;
        }
    }
    this->slotlist->setScrollSizeToContent();
}

// Exit to main menu
void RoomScreen::ragequit(bool play_sound) {
    BanchoState::match_started = false;
    ui->getHUD()->updateScoringMetric();

    Packet packet;
    packet.id = OUTP_EXIT_ROOM;
    BANCHO::Net::send_packet(packet);

    ui->getModSelector()->resetMods();
    ui->getModSelector()->updateButtons();

    BanchoState::room = Room();
    ui->setScreen(ui->getLobby());
    ui->getChat()->removeChannel("#multiplayer");
    ui->getChat()->updateVisibility();

    Replay::Mods::use(*osu->previous_mods);

    if(play_sound) {
        soundEngine->play(osu->getSkin()->s_menu_back);
    }
}

void RoomScreen::on_map_change() {
    if(BanchoState::is_playing_a_multi_map()) return;

    debugLog("Map changed to ID {:d}, MD5 {:s}: {:s}", BanchoState::room.map_id, BanchoState::room.map_md5,
             BanchoState::room.map_name);
    this->ready_btn->is_loading = true;

    // Deselect current map
    this->pauseButton->setPaused(true);
    osu->getMapInterface()->deselectBeatmap();

    if(BanchoState::room.map_id == 0) {
        this->map_title->setText("(no map selected)");
        this->map_title->setSizeToContent(0, 0);
        this->ready_btn->is_loading = true;
    } else {
        auto beatmap = db->getBeatmapDifficulty(BanchoState::room.map_md5);
        if(beatmap != nullptr) {
            ui->getSongBrowser()->onDifficultySelected(beatmap, false);
            this->map_title->setText(BanchoState::room.map_name);
            this->map_title->setSizeToContent(0, 0);
            auto attributes = fmt::format("AR: {:.1f}, CS: {:.1f}, HP: {:.1f}, OD: {:.1f}", beatmap->getAR(),
                                          beatmap->getCS(), beatmap->getHP(), beatmap->getOD());
            this->map_attributes->setText(attributes);
            this->map_attributes->setSizeToContent(0, 0);
            auto attributes2 = fmt::format("Length: {} seconds, BPM: {} ({} - {})", beatmap->getLengthMS() / 1000,
                                           beatmap->getMostCommonBPM(), beatmap->getMinBPM(), beatmap->getMaxBPM());
            this->map_attributes2->setText(attributes2);
            this->map_attributes2->setSizeToContent(0, 0);

            auto stars = fmt::format("Star rating: {:.2f}*", beatmap->getStarRating(StarPrecalc::active_idx));
            this->map_stars->setText(stars);
            this->map_stars->setSizeToContent(0, 0);
            this->ready_btn->is_loading = false;

            Packet packet;
            packet.id = OUTP_MATCH_HAS_BEATMAP;
            BANCHO::Net::send_packet(packet);
        } else {
            Packet packet;
            packet.id = OUTP_MATCH_NO_BEATMAP;
            BANCHO::Net::send_packet(packet);
        }
    }

    this->updateLayout(osu->getVirtScreenSize());
}

void RoomScreen::on_room_joined(const Room &room) {
    BanchoState::room = room;
    debugLog("Joined room #{:d}\nPlayers:", room.id);
    for(auto &slot : room.slots) {
        if(slot.has_player()) {
            const UserInfo *user_info = BANCHO::User::get_user_info(slot.player_id, true);
            debugLog("- {:s}", user_info->name);
        }
    }

    this->on_map_change();

    // Close all screens and stop any activity the player is in
    Spectating::stop();
    if(osu->isInPlayMode()) {
        osu->getMapInterface()->stop(true);
    }

    this->updateLayout(osu->getVirtScreenSize());
    ui->setScreen(ui->getRoom());

    RichPresence::setBanchoStatus(room.name.c_str(), Action::MULTIPLAYER);
    RichPresence::onMultiplayerLobby();
    ui->getChat()->openChannel("#multiplayer");

    *osu->previous_mods = Replay::Mods::from_cvars();

    ui->getModSelector()->resetMods();
    ui->getModSelector()->enableModsFromFlags(BanchoState::room.mods);
    cv::mod_no_pausing.setValue(true);
}

void RoomScreen::on_room_updated(const Room &room) {
    if(BanchoState::is_playing_a_multi_map() || !BanchoState::is_in_a_multi_room()) return;

    if(BanchoState::room.nb_players < room.nb_players) {
        soundEngine->play(osu->getSkin()->s_room_joined);
    } else if(BanchoState::room.nb_players > room.nb_players) {
        soundEngine->play(osu->getSkin()->s_room_quit);
    }
    if(BanchoState::room.nb_ready() < room.nb_ready()) {
        soundEngine->play(osu->getSkin()->s_room_ready);
    } else if(BanchoState::room.nb_ready() > room.nb_ready()) {
        soundEngine->play(osu->getSkin()->s_room_not_ready);
    }
    if(!BanchoState::room.all_players_ready() && room.all_players_ready()) {
        soundEngine->play(osu->getSkin()->s_match_confirm);
    }

    bool was_host = BanchoState::room.is_host();
    bool map_changed = BanchoState::room.map_id != room.map_id;
    BanchoState::room = room;

    Slot *player_slot = nullptr;
    for(auto &slot : BanchoState::room.slots) {
        if(slot.player_id == BanchoState::get_uid()) {
            player_slot = &slot;
            break;
        }
    }
    if(player_slot == nullptr) {
        // Player got kicked
        this->ragequit();
        return;
    }

    if(map_changed) {
        this->on_map_change();
    }

    if(!was_host && BanchoState::room.is_host()) {
        if(this->room_name_ipt->getText() != BanchoState::room.name) {
            this->room_name_ipt->setText(BanchoState::room.name);

            // Update room name in rich presence info
            RichPresence::onMultiplayerLobby();
        }
    }

    if(ui->getModSelector()->isVisible() && !BanchoState::room.is_host() && !BanchoState::room.freemods) {
        // Force close mod selector if host disabled freemods
        ui->setScreen(ui->getRoom());
    }
    ui->getModSelector()->updateButtons();
    ui->getModSelector()->resetMods();
    ui->getModSelector()->enableModsFromFlags(BanchoState::room.mods | player_slot->mods);
    cv::mod_no_pausing.setValue(true);

    this->updateLayout(osu->getVirtScreenSize());
}

void RoomScreen::on_match_started(const Room &room) {
    BanchoState::room = room;
    if(osu->getMapInterface()->getBeatmap() == nullptr) {
        debugLog("We received MATCH_STARTED without being ready, wtf!");
        return;
    }

    this->last_packet_tms = time(nullptr);

    if(osu->getMapInterface()->play()) {
        BanchoState::match_started = true;
        ui->getHUD()->updateScoringMetric();
        ui->getChat()->updateVisibility();

        soundEngine->play(osu->getSkin()->s_match_start);
    } else {
        ui->getNotificationOverlay()->addToast(US_("Failed to load map"), ERROR_TOAST);
        this->ragequit();  // map failed to load
    }
}

void RoomScreen::on_match_score_updated(Packet &packet) {
    auto frame = packet.read<ScoreFrame>();
    if(frame.slot_id > 15) return;

    auto slot = &BanchoState::room.slots[frame.slot_id];
    slot->last_update_tms = frame.time;
    slot->num300 = frame.num300;
    slot->num100 = frame.num100;
    slot->num50 = frame.num50;
    slot->num_geki = frame.num_geki;
    slot->num_katu = frame.num_katu;
    slot->num_miss = frame.num_miss;
    slot->total_score = frame.total_score;
    slot->max_combo = frame.max_combo;
    slot->current_combo = frame.current_combo;
    slot->is_perfect = frame.is_perfect;
    slot->current_hp = frame.current_hp;
    slot->tag = frame.tag;

    bool is_scorev2 = packet.read<u8>();
    if(is_scorev2) {
        slot->sv2_combo = packet.read<f64>();
        slot->sv2_bonus = packet.read<f64>();
    }

    ui->getHUD()->updateScoreboard(true);
}

void RoomScreen::on_player_failed(i32 slot_id) {
    if(slot_id < 0 || slot_id > 15) return;
    BanchoState::room.slots[slot_id].died = true;
}

FinishedScore RoomScreen::get_approximate_score() {
    FinishedScore score;

    score.player_id = BanchoState::get_uid();
    score.playerName = BanchoState::get_username();

    score.map = osu->getMapInterface()->getBeatmap();

    for(const auto &slot : BanchoState::room.slots) {
        if(slot.player_id != BanchoState::get_uid()) continue;

        score.mods = Replay::Mods::from_legacy(slot.mods);
        score.passed = !slot.died;
        score.unixTimestamp = slot.last_update_tms;
        score.num300s = slot.num300;
        score.num100s = slot.num100;
        score.num50s = slot.num50;
        score.numGekis = slot.num_geki;
        score.numKatus = slot.num_katu;
        score.numMisses = slot.num_miss;
        score.score = slot.total_score;
        score.comboMax = slot.max_combo;
        score.perfect = slot.is_perfect;
    }

    score.grade = score.calculate_grade();

    return score;
}

// All players have finished.
void RoomScreen::on_match_finished() {
    if(!BanchoState::is_playing_a_multi_map()) return;
    BanchoState::last_scores = BanchoState::room.slots;

    osu->onPlayEnd(this->get_approximate_score(), false);

    BanchoState::match_started = false;
    ui->getHUD()->updateScoringMetric();

    // Display room presence instead of map again
    RichPresence::onMultiplayerLobby();
}

void RoomScreen::on_player_skip(i32 user_id) {
    for(auto &slot : BanchoState::room.slots) {
        if(slot.player_id == user_id) {
            slot.skipped = true;
            break;
        }
    }
}

void RoomScreen::on_match_aborted() {
    if(!BanchoState::is_playing_a_multi_map()) return;
    if(osu->isInPlayMode()) {
        osu->getMapInterface()->stop(true);
    }
    BanchoState::match_started = false;
    ui->getHUD()->updateScoringMetric();
    ui->setScreen(ui->getRoom());

    // Display room presence instead of map again
    RichPresence::onMultiplayerLobby();
}

void RoomScreen::onClientScoreChange(bool force) {
    if(!BanchoState::is_playing_a_multi_map()) return;

    // Update at most once every 250ms
    bool should_update = difftime(time(nullptr), this->last_packet_tms) > 0.25;
    if(!should_update && !force) return;

    Packet packet;
    packet.id = OUTP_UPDATE_MATCH_SCORE;
    packet.write<ScoreFrame>(ScoreFrame::get());
    BANCHO::Net::send_packet(packet);

    this->last_packet_tms = time(nullptr);
}

void RoomScreen::onReadyButtonClick() {
    if(this->ready_btn->is_loading) return;
    soundEngine->play(osu->getSkin()->s_menu_hit);

    int nb_ready = 0;
    bool is_ready = false;
    for(auto &slot : BanchoState::room.slots) {
        if(slot.has_player() && slot.is_ready()) {
            nb_ready++;
            if(slot.player_id == BanchoState::get_uid()) {
                is_ready = true;
            }
        }
    }
    if(BanchoState::room.is_host() && is_ready && nb_ready > 1) {
        Packet packet;
        packet.id = OUTP_START_MATCH;
        BANCHO::Net::send_packet(packet);
    } else {
        Packet packet;
        packet.id = is_ready ? OUTP_MATCH_NOT_READY : OUTP_MATCH_READY;
        BANCHO::Net::send_packet(packet);
    }
}

void RoomScreen::onSelectModsClicked() {
    ui->setScreen(ui->getModSelector());
    soundEngine->play(osu->getSkin()->s_menu_hit);
}

void RoomScreen::onSelectMapClicked() {
    if(!BanchoState::room.is_host()) return;

    ui->setScreen(ui->getSongBrowser());
    soundEngine->play(osu->getSkin()->s_menu_hit);

    Packet packet;
    packet.id = OUTP_MATCH_CHANGE_SETTINGS;
    BanchoState::room.map_id = -1;
    BanchoState::room.map_name = "";
    BanchoState::room.map_md5 = {};
    BanchoState::room.pack(packet);
    BANCHO::Net::send_packet(packet);
}

void RoomScreen::onDownloadMapsClicked() {
    if(!BanchoState::room.is_host()) return;

    ui->setScreen(ui->getOsuDirectScreen());
    soundEngine->play(osu->getSkin()->s_menu_hit);

    Packet packet;
    packet.id = OUTP_MATCH_CHANGE_SETTINGS;
    BanchoState::room.map_id = -1;
    BanchoState::room.map_name = "";
    BanchoState::room.map_md5 = {};
    BanchoState::room.pack(packet);
    BANCHO::Net::send_packet(packet);
}

void RoomScreen::onChangePasswordClicked() {
    ui->getPromptOverlay()->prompt("New password:", SA::MakeDelegate<&RoomScreen::set_new_password>(this));
}

void RoomScreen::onChangeWinConditionClicked() {
    this->contextMenu->setVisible(false);
    this->contextMenu->begin();
    this->contextMenu->addButton("Score V1", (int)WinCondition::SCOREV1);
    this->contextMenu->addButton("Score V2", (int)WinCondition::SCOREV2);
    this->contextMenu->addButton("Accuracy", (int)WinCondition::ACCURACY);
    this->contextMenu->addButton("Combo", (int)WinCondition::CURRENT_COMBO);
    this->contextMenu->end(false, false);
    this->contextMenu->setPos(mouse->getPos());
    this->contextMenu->setClickCallback(SA::MakeDelegate<&RoomScreen::onWinConditionSelected>(this));
    this->contextMenu->setVisible(true);
}

void RoomScreen::onWinConditionSelected(const UString & /*win_condition_str*/, int win_condition) {
    assert(win_condition >= 0 && win_condition <= 255);

    BanchoState::room.win_condition = (WinCondition)win_condition;

    Packet packet;
    packet.id = OUTP_MATCH_CHANGE_SETTINGS;
    BanchoState::room.pack(packet);
    BANCHO::Net::send_packet(packet);

    this->updateLayout(osu->getVirtScreenSize());
}

void RoomScreen::set_new_password(const UString &new_password) {
    BanchoState::room.has_password = new_password.lengthUtf8() > 0;
    BanchoState::room.password = new_password.toUtf8();

    Packet packet;
    packet.id = OUTP_CHANGE_ROOM_PASSWORD;
    BanchoState::room.pack(packet);
    BANCHO::Net::send_packet(packet);
}

void RoomScreen::set_current_map(DatabaseBeatmap *map) {
    if(!BanchoState::room.is_host()) return;
    if(!map) return;

    BanchoState::room.map_name =
        fmt::format("{:s} - {:s} [{:s}]", map->getArtistLatin(), map->getTitleLatin(), map->getDifficultyName());
    BanchoState::room.map_md5 = map->getMD5();
    BanchoState::room.map_id = map->getID();

    Packet packet;
    packet.id = OUTP_MATCH_CHANGE_SETTINGS;
    BanchoState::room.pack(packet);
    BANCHO::Net::send_packet(packet);

    this->on_map_change();
}

void RoomScreen::onFreemodCheckboxChanged(CBaseUICheckbox *checkbox) {
    if(!BanchoState::room.is_host()) return;

    BanchoState::room.freemods = checkbox->isChecked();

    Packet packet;
    packet.id = OUTP_MATCH_CHANGE_SETTINGS;
    BanchoState::room.pack(packet);
    BANCHO::Net::send_packet(packet);

    this->on_room_updated(BanchoState::room);
}
