#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.

#include "CBaseUIScrollView.h"
#include "DownloadHandle.h"
#include "UIScreen.h"
#include "score.h"

class Room;
struct Packet;

class CBaseUICheckbox;
class CBaseUILabel;
class CBaseUITextbox;
class DatabaseBeatmap;
class PauseButton;
class UIButton;
class UICheckbox;
class UIContextMenu;
class McFont;

class UIModList final : public CBaseUIContainer {
   public:
    UIModList() = delete;
    UIModList(LegacyFlags* flags) : CBaseUIContainer(0, 0, 0, 0, "mod_list"), flags(flags) {}

    LegacyFlags* flags;

    void draw() override;
    bool isVisible() override;

    // Dummy function for RoomScreen generic macro
    void setSizeToContent(int /*a*/, int /*b*/) {}
};

class RoomScreen final : public UIScreen {
    NOCOPY_NOMOVE(RoomScreen)
   public:
    RoomScreen();
    ~RoomScreen() override;

    void draw() override;
    void update(CBaseUIEventCtx& c) override;
    void onKeyDown(KeyboardEvent& e) override;
    void onKeyUp(KeyboardEvent& e) override;
    void onChar(KeyboardEvent& e) override;
    void onResolutionChange(vec2 newResolution) override;
    CBaseUIContainer* setVisible(bool visible) override;  // does nothing

    void updateLayout(vec2 newResolution);
    void updateSettingsLayout(vec2 newResolution);
    void ragequit(bool play_sound = true);

    void on_map_change();
    void on_room_joined(const Room& room);
    void on_room_updated(const Room& room);
    void on_match_started(const Room& room);
    void on_match_score_updated(Packet& packet);
    void on_player_failed(i32 slot_id);
    void on_match_finished();
    void on_player_skip(i32 user_id);
    void on_match_aborted();
    void onClientScoreChange(bool force = false);
    void onReadyButtonClick();

    FinishedScore get_approximate_score();

    // Host only
    void onStartGameClicked();
    void onSelectModsClicked();
    void onSelectMapClicked();
    void onChangePasswordClicked();
    void onChangeWinConditionClicked();
    void onWinConditionSelected(const UString& win_condition_str, int win_condition);
    void set_new_password(const UString& new_password);
    void onFreemodCheckboxChanged(CBaseUICheckbox* checkbox);

    CBaseUILabel* map_label{nullptr};
    CBaseUILabel* mods_label{nullptr};
    CBaseUIScrollView* settings{nullptr};
    CBaseUIScrollView* slotlist{nullptr};
    CBaseUIScrollView* map{nullptr};
    CBaseUILabel* host{nullptr};
    CBaseUILabel* room_name{nullptr};
    CBaseUILabel* room_name_iptl{nullptr};
    CBaseUITextbox* room_name_ipt{nullptr};
    UIButton* change_password_btn{nullptr};
    CBaseUILabel* win_condition{nullptr};
    UIButton* change_win_condition_btn{nullptr};
    CBaseUILabel* map_title{nullptr};
    CBaseUILabel* map_attributes{nullptr};
    CBaseUILabel* map_attributes2{nullptr};
    CBaseUILabel* map_stars{nullptr};
    UIButton* select_map_btn{nullptr};
    UIButton* select_mods_btn{nullptr};
    UICheckbox* freemod{nullptr};
    UIModList* mods{nullptr};
    CBaseUILabel* no_mods_selected{nullptr};
    UIButton* ready_btn{nullptr};
    UIContextMenu* contextMenu{nullptr};

    CBaseUILabel* player_list_label{nullptr};
    PauseButton* pauseButton{nullptr};

    McFont* font{nullptr};
    McFont* lfont{nullptr};

    time_t last_packet_tms = {0};
    Downloader::DownloadHandle map_dl;
};
