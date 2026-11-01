#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.

#include "AnimationHandler.h"
#include "CBaseUILabel.h"
#include "UIScreen.h"

#include <optional>

class UIContextMenu;
struct UserInfo;

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

    // twitch purple, for users who are live
    static constexpr Color LIVE_COLOR{0xff9146ff};
    static constexpr Color SUPPORTER_COLOR{0xffffcc22};

    // the name color implied by the user privileges, if any
    static std::optional<Color> getUserColor(const UserInfo* user);

    void draw() override;
    void onMouseUpInside(bool left = true, bool right = false) override;
    void onMouseInside() override;
    void onMouseOutside() override;

    i32 user_id;

    // when false, the text color stays as set regardless of getUserColor()
    bool show_user_color{true};

   private:
    AnimFloat hover_anim{0.f};
};
