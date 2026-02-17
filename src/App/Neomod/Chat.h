#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.

#include "CBaseUIScrollView.h"
#include "CBaseUITextbox.h"
#include "UIScreen.h"

class CBaseUIButton;
class McFont;
class Chat;
class UIButton;
class UserCard2;

struct ChatMessage final {
    time_t tms;
    i32 author_id;
    UString author_name;
    UString text;
};

struct ChatChannel final {
    NOCOPY_NOMOVE(ChatChannel)
   public:
    ChatChannel(Chat *chat, UString name_arg);
    ~ChatChannel();

    Chat *chat;
    CBaseUIScrollView *ui;
    UIButton *btn;
    UString name;
    std::vector<ChatMessage> messages;
    float y_total{.0f};
    bool read = true;

    void add_message(ChatMessage msg);
    void updateLayout(vec2 pos, vec2 size);
    void onChannelButtonClick(CBaseUIButton *btn);
};

class Chat final : public UIScreen {
    NOCOPY_NOMOVE(Chat)
   public:
    Chat();
    ~Chat() override;

    void draw() override;
    void drawTicker();
    void update(CBaseUIEventCtx &c) override;
    void onKeyDown(KeyboardEvent &e) override;
    void onKeyUp(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;
    void onResolutionChange(vec2 newResolution) override;

    void mark_as_read(ChatChannel *chan);
    void switchToChannel(ChatChannel *chan);
    void addChannel(const UString &channel_name, bool switch_to = false);
    void openChannel(const UString &channel_name);
    void addMessage(UString channel_name, const ChatMessage &msg, bool mark_unread = true);
    void addSystemMessage(UString msg);
    void removeChannel(const UString &channel_name);
    void updateLayout(vec2 newResolution);
    void updateButtonLayout(vec2 screen);
    void updateTickerLayout(vec2 screen);
    void updateUserList();

    void join(const UString &channel_name);
    void leave(const UString &channel_name);
    void handle_command(const UString &msg);
    void send_message(const UString &msg);
    void onDisconnect();

    CBaseUIContainer *setVisible(bool visible) override;
    bool isSmallChat();
    bool isVisibilityForced();
    void updateVisibility();
    bool isMouseInChat();

    void askWhatChannelToJoin(CBaseUIButton *btn);
    UIButton *join_channel_btn;

    ChatChannel *selected_channel = nullptr;
    std::vector<ChatChannel *> channels;
    CBaseUIContainer *button_container;
    CBaseUITextbox *input_box;

    CBaseUIScrollView *user_list;

    float fAnimation = 0.f;
    bool user_wants_chat = false;
    bool visibility_was_forced = false;
    bool layout_update_scheduled = false;
    bool userlist_update_scheduled = false;
    bool in_userlist_update = false;

    f32 input_box_height = 30.f;
    f32 button_height = 26.f;

    UString away_msg;
    UString tab_completion_prefix;
    UString tab_completion_match;

    ChatChannel *ticker = nullptr;
    f64 ticker_tms = 0.0;
    f64 last_sort_time = 0.0;
};
