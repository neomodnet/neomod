#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.

#include "CBaseUILabel.h"
#include "UIScreen.h"

class UIContextMenu;

class UIUserContextMenuScreen final : public UIScreen {
   public:
    UIUserContextMenuScreen();

    void onResolutionChange(vec2 newResolution) override;
    void stealFocus() override;

    void open(i32 user_id, bool is_song_browser_button = false);
    void close();
    void on_action(std::string_view text, int user_action);

    i32 user_id{0};
    bool from_user_button{false};
    UIContextMenu* menu{nullptr};
};

class UIUserLabel final : public CBaseUILabel {
   public:
    UIUserLabel(i32 user_id, std::string username);

    void onMouseUpInside(bool left = true, bool right = false) override;

    i32 user_id;
};
