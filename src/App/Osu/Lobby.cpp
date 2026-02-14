// Copyright (c) 2024, kiwec, All rights reserved.
#include "Lobby.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Bancho.h"
#include "BanchoNetworking.h"
#include "BanchoUsers.h"
#include "BeatmapInterface.h"
#include "CBaseUIButton.h"
#include "CBaseUIContainer.h"
#include "CBaseUILabel.h"
#include "Chat.h"
#include "Database.h"
#include "Engine.h"
#include "Font.h"
#include "KeyBindings.h"
#include "MainMenu.h"
#include "NotificationOverlay.h"
#include "Osu.h"
#include "PromptOverlay.h"
#include "ResourceManager.h"
#include "RichPresence.h"
#include "Skin.h"
#include "SongBrowser/SongBrowser.h"
#include "SoundEngine.h"
#include "UI.h"
#include "UIButton.h"
#include "Logging.h"
#include "Environment.h"
#include "MakeDelegateWrapper.h"

RoomUIElement::RoomUIElement(Lobby* multi, const Room& room, float x, float y, float width, float height)
    : CBaseUIScrollView(x, y, width, height, "") {
    // NOTE: We can't store the room pointer, since it might expire later
    this->multi = multi;
    this->room_id = room.id;
    this->has_password = room.has_password;

    this->setBlockScrolling(true);
    this->setDrawFrame(true);

    float title_width = multi->font->getStringWidth(room.name) + 20;
    auto title_ui = new CBaseUILabel(10, 5, title_width, 30, "", room.name);
    title_ui->setDrawFrame(false);
    title_ui->setDrawBackground(false);
    this->container.addBaseUIElement(title_ui);

    char player_count_str[256] = {0};
    snprintf(player_count_str, 255, "Players: %d/%d", room.nb_players, room.nb_open_slots);
    float player_count_width = multi->font->getStringWidth(player_count_str) + 20;
    auto slots_ui = new CBaseUILabel(10, 33, player_count_width, 30, "", UString(player_count_str));
    slots_ui->setDrawFrame(false);
    slots_ui->setDrawBackground(false);
    this->container.addBaseUIElement(slots_ui);

    this->join_btn = new UIButton(10, 65, 120, 30, "", "Join room");
    this->join_btn->setUseDefaultSkin();
    this->join_btn->setColor(0xff00d900);
    this->join_btn->setClickCallback(SA::MakeDelegate<&RoomUIElement::onRoomJoinButtonClick>(this));
    this->container.addBaseUIElement(this->join_btn);

    if(room.has_password) {
        auto pwlabel = new CBaseUILabel(135, 64, 150, 30, "", "(password required)");
        pwlabel->setDrawFrame(false);
        pwlabel->setDrawBackground(false);
        this->container.addBaseUIElement(pwlabel);
    }
}

void RoomUIElement::onRoomJoinButtonClick(CBaseUIButton* /*btn*/) {
    if(this->has_password) {
        this->multi->room_to_join = this->room_id;
        ui->getPromptOverlay()->prompt("Room password:",
                                       SA::MakeDelegate<&Lobby::on_room_join_with_password>(this->multi));
    } else {
        this->multi->joinRoom(this->room_id, "");
    }
}

Lobby::Lobby() : UIScreen() {
    this->font = engine->getDefaultFont();

    auto heading = new CBaseUILabel(50, 30, 300, 40, "", "Multiplayer rooms");
    heading->setFont(osu->getTitleFont());
    heading->setSizeToContent(0, 0);
    heading->setDrawFrame(false);
    heading->setDrawBackground(false);
    this->addBaseUIElement(heading);

    this->create_room_btn = new UIButton(0, 0, 200, 50, "", "Create new room");
    this->create_room_btn->setUseDefaultSkin();
    this->create_room_btn->setColor(0xff00d900);
    this->create_room_btn->setClickCallback(SA::MakeDelegate<&Lobby::on_create_room_clicked>(this));
    this->addBaseUIElement(this->create_room_btn);

    this->list = new CBaseUIScrollView(0, 0, 0, 0, "");
    this->list->setDrawFrame(false);
    this->list->setDrawBackground(true);
    this->list->setBackgroundColor(0xdd000000);
    this->list->setHorizontalScrolling(false);
    this->addBaseUIElement(this->list);

    this->updateLayout(osu->getVirtScreenSize());
}

void Lobby::onKeyDown(KeyboardEvent& key) {
    if(!this->bVisible) return;

    if(key.getScanCode() == KEY_ESCAPE) {
        key.consume();
        ui->setScreen(ui->getMainMenu());
        soundEngine->play(osu->getSkin()->s_menu_back);
        return;
    }

    // XXX: search bar
}

void Lobby::onKeyUp(KeyboardEvent& /*key*/) {
    if(!this->bVisible) return;

    // XXX: search bar
}

void Lobby::onChar(KeyboardEvent& /*key*/) {
    if(!this->bVisible) return;

    // XXX: search bar
}

void Lobby::onResolutionChange(vec2 newResolution) { this->updateLayout(newResolution); }

CBaseUIContainer* Lobby::setVisible(bool visible) {
    if(visible == this->bVisible) return this;
    this->bVisible = visible;

    if(visible) {
        Packet packet;
        packet.id = OUTP_JOIN_ROOM_LIST;
        BANCHO::Net::send_packet(packet);

        packet = Packet();
        packet.id = OUTP_CHANNEL_JOIN;
        packet.write_string("#lobby");
        BANCHO::Net::send_packet(packet);

        // LOBBY presence is broken so we send MULTIPLAYER
        RichPresence::setBanchoStatus("Looking to play", Action::MULTIPLAYER);

        if(!db->isFinished()) {
            // Not having a loaded database causes a bunch of issues in multi
            ui->getSongBrowser()->refreshBeatmaps(this);
            return this;
        }
    } else {
        Packet packet;
        packet.id = OUTP_EXIT_ROOM_LIST;
        BANCHO::Net::send_packet(packet);

        packet = Packet();
        packet.id = OUTP_CHANNEL_PART;
        packet.write_string("#lobby");
        BANCHO::Net::send_packet(packet);
        this->rooms.clear();
    }

    ui->getChat()->updateVisibility();
    return this;
}

void Lobby::updateLayout(vec2 newResolution) {
    this->setSize(newResolution);

    this->list->freeElements();
    this->list->setPos(round(newResolution.x * 0.6), 0);
    this->list->setSize(round(newResolution.x * 0.4), newResolution.y);

    const f32 padding = 20.f * Osu::getUIScale();

    if(this->rooms.empty()) {
        auto noRoomsOpenElement = new CBaseUILabel(0, 0, 0, 0, "", "There are no matches available.");
        noRoomsOpenElement->setTextJustification(TEXT_JUSTIFICATION::CENTERED);
        noRoomsOpenElement->setSizeToContent(padding, padding);
        noRoomsOpenElement->setPos(this->list->getSize().x / 2 - noRoomsOpenElement->getSize().x / 2,
                                   this->list->getSize().y / 2 - noRoomsOpenElement->getSize().y / 2);
        this->list->container.addBaseUIElement(noRoomsOpenElement);
    }

    float heading_ratio = 70 / newResolution.y;
    float chat_ratio = 0.3;
    float free_ratio = 1.f - (heading_ratio + chat_ratio);
    this->create_room_btn->onResized();
    this->create_room_btn->setSizeToContent(padding, padding);
    this->create_room_btn->setPos(
        round(newResolution.x * 0.3) - this->create_room_btn->getSize().x / 2,
        70 + std::round(newResolution.y * free_ratio / 2) - this->create_room_btn->getSize().y / 2);

    const f32 room_margin = 20.f * Osu::getUIScale();
    const f32 room_height = 105.f * Osu::getUIScale();
    f32 y = room_margin / 2.f;
    for(auto& room : this->rooms) {
        const f32 x = 10.f * Osu::getUIScale();
        const f32 room_width = this->list->getSize().x - room_margin;
        auto room_ui = new RoomUIElement(this, *room, x, y, room_width, room_height);
        this->list->container.addBaseUIElement(room_ui);
        y += room_height + room_margin;
    }

    this->list->setScrollSizeToContent();
}

void Lobby::addRoom(std::unique_ptr<Room> room) {
    this->rooms.push_back(std::move(room));
    this->updateLayout(this->getSize());
}

void Lobby::joinRoom(u32 id, const UString& password) {
    Packet packet;
    packet.id = OUTP_JOIN_ROOM;
    packet.write<u32>(id);
    packet.write_string(password.utf8View());
    BANCHO::Net::send_packet(packet);

    for(CBaseUIElement* elm : this->list->container.getElements()) {
        auto* room = dynamic_cast<RoomUIElement*>(elm);
        if(room == nullptr) continue;
        if(std::cmp_not_equal(room->room_id, id)) continue;
        room->join_btn->is_loading = true;
        break;
    }

    debugLog("Joining room #{:d} with password '{:s}'", id, password.toUtf8());
    ui->getNotificationOverlay()->addNotification("Joining room...");
}

void Lobby::updateRoom(const Room& room) {
    if(auto old_room = std::ranges::find(this->rooms, room.id, [](const auto& room_) -> u32 { return room_->id; });
       old_room != this->rooms.end()) {
        *(old_room->get()) = room;
        this->updateLayout(this->getSize());
        return;
    }

    // On bancho.py, if a player creates a room when we're already in the lobby,
    // we won't receive a ROOM_CREATED but only a ROOM_UPDATED packet.
    this->addRoom(std::make_unique<Room>(room));
}

void Lobby::removeRoom(u32 room_id) {
    if(std::erase_if(this->rooms, [room_id](const auto& room) -> bool { return room->id == room_id; }) > 0) {
        this->updateLayout(this->getSize());
    }
}

void Lobby::on_create_room_clicked() {
    BanchoState::room = Room();
    BanchoState::room.name = "New room";  // XXX: doesn't work
    BanchoState::room.host_id = BanchoState::get_uid();
    for(auto& slot : BanchoState::room.slots) {
        slot.status = 1;  // open slot
    }
    BanchoState::room.slots[0].status = 4;  // not ready
    BanchoState::room.slots[0].player_id = BanchoState::get_uid();

    if(osu->getMapInterface() && osu->getMapInterface()->getBeatmap()) {
        auto map = osu->getMapInterface()->getBeatmap();
        BanchoState::room.map_name =
            fmt::format("{:s} - {:s} [{:s}]", map->getArtist(), map->getTitle(), map->getDifficultyName());
        BanchoState::room.map_md5 = map->getMD5();
        BanchoState::room.map_id = map->getID();
    }

    Packet packet = {0};
    packet.id = OUTP_CREATE_ROOM;
    BanchoState::room.pack(packet);
    BANCHO::Net::send_packet(packet);

    ui->getNotificationOverlay()->addNotification("Creating room...");
}

void Lobby::on_room_join_with_password(const UString& password) { this->joinRoom(this->room_to_join, password); }

void Lobby::on_room_join_failed() {
    // Updating layout will reset is_loading to false
    this->updateLayout(this->getSize());
}
