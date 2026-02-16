// Copyright (c) 2024, kiwec, All rights reserved.
#include "Chat.h"

#include "AnimationHandler.h"
#include "Bancho.h"
#include "BanchoApi.h"
#include "BanchoNetworking.h"
#include "BanchoUsers.h"
#include "BeatmapInterface.h"
#include "CBaseUIButton.h"
#include "CBaseUIContainer.h"
#include "CBaseUILabel.h"
#include "CBaseUITextbox.h"
#include "Logging.h"
#include "NetworkHandler.h"
#include "OsuConVars.h"
#include "ChatLink.h"
#include "Engine.h"
#include "Font.h"
#include "Keyboard.h"
#include "Lobby.h"
#include "ModSelector.h"
#include "Mouse.h"
#include "OptionsOverlay.h"
#include "Osu.h"
#include "PauseOverlay.h"
#include "PromptOverlay.h"
#include "RenderTarget.h"
#include "ResourceManager.h"
#include "RoomScreen.h"
#include "Skin.h"
#include "SongBrowser/ScoreButton.h"
#include "SongBrowser/SongBrowser.h"
#include "SoundEngine.h"
#include "SString.h"
#include "SpectatorScreen.h"
#include "UI.h"
#include "UIButton.h"
#include "UIUserContextMenu.h"
#include "UserCard2.h"
#include "Timing.h"
#include "Environment.h"
#include "MakeDelegateWrapper.h"
#include "crypto.h"

#include <algorithm>
#include <cmath>
#include "ctre.hpp"
#include <utility>

using namespace flags::operators;
static McFont *chat_font = nullptr;

ChatChannel::ChatChannel(Chat *chat, UString name_arg) {
    this->chat = chat;
    this->name = std::move(name_arg);

    this->ui = new CBaseUIScrollView(0, 0, 0, 0, "");
    this->ui->setDrawFrame(false);
    this->ui->setDrawBackground(true);
    this->ui->setBackgroundColor(0xdd000000);
    this->ui->setHorizontalScrolling(false);
    this->ui->setDrawScrollbars(true);
    this->ui->setAutoscroll(true);

    if(chat != nullptr) {
        this->btn = new UIButton(0, 0, 0, 0, "button", this->name);
        this->btn->setGrabClicks(true);
        this->btn->setUseDefaultSkin();
        this->btn->setClickCallback(SA::MakeDelegate<&ChatChannel::onChannelButtonClick>(this));
        this->chat->button_container->addBaseUIElement(this->btn);
    }
}

ChatChannel::~ChatChannel() {
    SAFE_DELETE(this->ui);
    if(this->chat != nullptr) {
        this->chat->button_container->deleteBaseUIElement(this->btn);
    }
}

void ChatChannel::onChannelButtonClick(CBaseUIButton * /*btn*/) {
    if(this->chat == nullptr) return;
    soundEngine->play(osu->getSkin()->s_click_button);
    this->chat->switchToChannel(this);
}

void ChatChannel::add_message(ChatMessage msg) {
    const float line_height = 20;
    const Color system_color = 0xffffff00;

    float x = 10;

    const bool is_action = msg.text.startsWith(US_("\001ACTION"));
    if(is_action) {
        msg.text.erase(0, 7);
        if(msg.text.endsWith(US_("\001"))) {
            msg.text.erase(msg.text.length() - 1, 1);
        }
    }

    struct tm tm;
    localtime_x(&msg.tms, &tm);
    UString timestamp_str = fmt::format("{:02d}:{:02d} ", tm.tm_hour, tm.tm_min);
    if(is_action) timestamp_str.append(u'*');
    float time_width = chat_font->getStringWidth(timestamp_str);
    auto *timestamp = new CBaseUILabel(x, this->y_total, time_width, line_height, "", timestamp_str);
    timestamp->setDrawFrame(false);
    timestamp->setDrawBackground(false);
    this->ui->container.addBaseUIElement(timestamp);
    x += time_width;

    bool is_system_message = msg.author_name.length() == 0;
    if(!is_system_message) {
        float name_width = chat_font->getStringWidth(msg.author_name);
        auto user_box = new UIUserLabel(msg.author_id, msg.author_name);
        user_box->setTextColor(0xff2596be);
        user_box->setPos(x, this->y_total);
        user_box->setSize(name_width, line_height);
        this->ui->container.addBaseUIElement(user_box);
        x += name_width;

        if(!is_action) {
            msg.text.insert(0, US_(": "));
        }
    }

    // regex101 format: (\[\[(.+?)\]\])|(\[((\S+):\/\/\S+) (.+?)\])|(https?:\/\/\S+)
    // This matches:
    // - Raw URLs      https://example.com
    // - Labeled URLs  [https://regex101.com useful website]
    // - Lobby invites [osump://0/ join my lobby plz]
    // - Wiki links    [[Chat Console]]
    //
    // Group 1) [[FAQ]]
    // Group 2) FAQ
    // Group 3) [https://regex101.com label]
    // Group 4) https://regex101.com
    // Group 5) https
    // Group 6) label
    // Group 7) https://example.com
    //
    // Groups 1, 2 only exist for wiki links
    // Groups 3, 4, 5, 6 only exist for labeled links
    // Group 7 only exists for raw links
    static constexpr ctll::fixed_string url_pattern{LR"((\[\[(.+?)\]\])|(\[((\S+)://\S+) (.+?)\])|(https?://\S+))"};

    std::wstring msg_text = msg.text.to_wstring();
    std::vector<CBaseUILabel *> temp_text_fragments;
    sSz text_idx = 0;

    for(auto match : ctre::search_all<url_pattern>(msg_text)) {
        sSz match_start = match.begin() - msg_text.cbegin();
        sSz match_len = match.end() - match.begin();

        UString link_url;
        UString link_label;

        if(auto raw_link = match.get<7>(); raw_link) {
            // Raw link
            link_url = raw_link.to_view();
            link_label = raw_link.to_view();
        } else if(auto labeled_link = match.get<3>(); labeled_link) {
            // Labeled link
            link_url = match.get<4>().to_view();
            link_label = match.get<6>().to_view();

            // Normalize invite links to osump://
            UString link_protocol = match.get<5>().to_view();
            if(link_protocol == US_("osu")) {
                // osu:// -> osump://
                link_url.insert(3, US_("mp"));
            } else if(link_protocol == US_("http://osump")) {
                // http://osump:// -> osump://
                link_url.erase(0, 7);
            }
        } else {
            // Wiki link
            auto wiki_name = match.get<2>().to_view();
            link_url = US_("https://osu.ppy.sh/wiki/");
            link_url.append(wiki_name);
            link_label = US_("wiki:");
            link_label.append(wiki_name);
        }

        // Add preceding text
        if(match_start > text_idx) {
            auto preceding_text = msg.text.substr(text_idx, match_start - text_idx);
            temp_text_fragments.push_back(new CBaseUILabel(0, 0, 0, 0, "", preceding_text));
        }

        auto link = new ChatLink(0, 0, 0, 0, link_url, link_label);
        temp_text_fragments.push_back(link);

        text_idx = match_start + match_len;
    }

    // Add remaining text after last match
    if(text_idx < (sSz)msg_text.size()) {
        auto text = msg.text.substr(text_idx);
        temp_text_fragments.push_back(new CBaseUILabel(0, 0, 0, 0, "", text));
    }
    if(is_action) {
        // Only appending now to prevent this character from being included in a link
        msg.text.append(u'*');
        msg_text.push_back(L'*');
    }

    // We're offsetting the first fragment to account for the username + timestamp
    // Since first fragment will always be text, we don't care about the size being wrong
    float line_width = x;

    // We got a bunch of text fragments, now position them, and if we start a new line,
    // possibly divide them into more text fragments.
    for(auto fragment : temp_text_fragments) {
        UString text_str("");
        auto fragment_text = fragment->getText();

        for(int i = 0; i < fragment_text.length(); i++) {
            float char_width = chat_font->getGlyphWidth(fragment_text[i]);
            if(line_width + char_width + 20 >= this->ui->getSize().x) {
                auto *link_fragment = dynamic_cast<ChatLink *>(fragment);
                if(link_fragment == nullptr) {
                    auto *text = new CBaseUILabel(x, this->y_total, line_width - x, line_height, "", text_str);
                    text->setDrawFrame(false);
                    text->setDrawBackground(false);
                    if(is_system_message) {
                        text->setTextColor(system_color);
                    }
                    this->ui->container.addBaseUIElement(text);
                } else {
                    auto *link =
                        new ChatLink(x, this->y_total, line_width - x, line_height, fragment->getName(), text_str);
                    this->ui->container.addBaseUIElement(link);
                }

                x = 10;
                this->y_total += line_height;
                line_width = x;
                text_str.clear();
            }

            text_str.append(fragment_text[i]);
            line_width += char_width;
        }

        auto *link_fragment = dynamic_cast<ChatLink *>(fragment);
        if(link_fragment == nullptr) {
            auto *text = new CBaseUILabel(x, this->y_total, line_width - x, line_height, "", text_str);
            text->setDrawFrame(false);
            text->setDrawBackground(false);
            if(is_system_message) {
                text->setTextColor(system_color);
            }
            this->ui->container.addBaseUIElement(text);
        } else {
            auto *link = new ChatLink(x, this->y_total, line_width - x, line_height, fragment->getName(), text_str);
            this->ui->container.addBaseUIElement(link);
        }

        x = line_width;
    }

    for(auto &fragment : temp_text_fragments) {
        SAFE_DELETE(fragment);
    }

    this->y_total += line_height;
    this->ui->setScrollSizeToContent();
}

void ChatChannel::updateLayout(vec2 pos, vec2 size) {
    this->ui->freeElements();
    this->ui->setPos(pos);
    this->ui->setSize(size);
    this->y_total = 7;

    for(const auto &msg : this->messages) {
        this->add_message(msg);
    }
}

Chat::Chat() : UIScreen() {
    chat_font = engine->getDefaultFont();

    this->ticker = new ChatChannel(nullptr, "");
    this->ticker->ui->setVerticalScrolling(false);
    this->ticker->ui->setDrawScrollbars(false);

    this->button_container = new CBaseUIContainer(0, 0, 0, 0, "");

    this->join_channel_btn = new UIButton(0, 0, 0, 0, "button", "+");
    this->join_channel_btn->setUseDefaultSkin();
    this->join_channel_btn->setColor(0xffd9d948);
    this->join_channel_btn->setSize(this->button_height + 2, this->button_height + 2);
    this->join_channel_btn->setClickCallback(SA::MakeDelegate<&Chat::askWhatChannelToJoin>(this));
    this->button_container->addBaseUIElement(this->join_channel_btn);

    this->input_box = new CBaseUITextbox(0, 0, 0, 0, "");
    this->input_box->setDrawFrame(false);
    this->input_box->setDrawBackground(true);
    this->input_box->setBackgroundColor(0xdd000000);
    this->addBaseUIElement(this->input_box);

    this->user_list = new CBaseUIScrollView(0, 0, 0, 0, "");
    this->user_list->setDrawFrame(false);
    this->user_list->setDrawBackground(true);
    this->user_list->setBackgroundColor(0xcc000000);
    this->user_list->setHorizontalScrolling(false);
    this->user_list->setDrawScrollbars(true);
    this->user_list->setVisible(false);
    this->addBaseUIElement(this->user_list);

    this->updateLayout(osu->getVirtScreenSize());
}

Chat::~Chat() {
    anim::deleteExistingAnimation(&this->fAnimation);

    for(auto &chan : this->channels) {
        SAFE_DELETE(chan);
    }
    SAFE_DELETE(this->button_container);
    SAFE_DELETE(this->ticker);
}

void Chat::draw() {
    this->drawTicker();

    const bool isAnimating = anim::isAnimating(&this->fAnimation);
    if(!this->bVisible && !isAnimating) return;

    if(isAnimating) {
        // XXX: Setting PREMUL_ALPHA is not enough, transparency is still incorrect
        osu->getSliderFrameBuffer()->enable();
        g->setBlendMode(DrawBlendMode::PREMUL_ALPHA);
    }

    g->setColor(argb(100, 0, 10, 50));
    g->fillRect(this->button_container->getPos().x, this->button_container->getPos().y,
                this->button_container->getSize().x, this->button_container->getSize().y);
    this->button_container->draw();

    if(this->selected_channel == nullptr) {
        f32 chat_h = std::round(this->getSize().y * 0.3f);
        f32 chat_y = this->getSize().y - chat_h;
        f32 chat_w = this->isSmallChat() ? std::round(this->getSize().x * 0.6f) : this->getSize().x;
        g->setColor(argb(150, 0, 0, 0));
        g->fillRect(0, chat_y, chat_w, chat_h);
    } else {
        UIScreen::draw();
        this->selected_channel->ui->draw();
    }

    if(isAnimating) {
        osu->getSliderFrameBuffer()->disable();

        g->setBlendMode(DrawBlendMode::ALPHA);
        g->push3DScene(McRect(0, 0, this->getSize().x, this->getSize().y));
        {
            g->rotate3DScene(-(1.0f - this->fAnimation) * 90, 0, 0);
            g->translate3DScene(0, -(1.0f - this->fAnimation) * this->getSize().y * 1.25f,
                                -(1.0f - this->fAnimation) * 700);

            osu->getSliderFrameBuffer()->setColor(argb(this->fAnimation, 1.0f, 1.0f, 1.0f));
            osu->getSliderFrameBuffer()->draw(0, 0);
        }
        g->pop3DScene();
    }
}

void Chat::drawTicker() {
    if(!cv::chat_ticker.getBool()) return;

    f64 time_elapsed = engine->getTime() - this->ticker_tms;
    if(this->ticker_tms == 0.0 || time_elapsed > 6.0) return;

    f32 a = std::clamp(6.0 - time_elapsed, 0.0, 1.0);
    auto ticker_size = this->ticker->ui->getSize();
    if(!anim::isAnimating(&this->fAnimation)) {
        this->fAnimation = 0.f;
        if(this->isVisible()) return;  // don't draw ticker while chat is visible
    }

    // XXX: Setting PREMUL_ALPHA is not enough, transparency is still incorrect
    osu->getSliderFrameBuffer()->enable();
    g->setBlendMode(DrawBlendMode::PREMUL_ALPHA);
    this->ticker->ui->draw();
    osu->getSliderFrameBuffer()->disable();

    g->setBlendMode(DrawBlendMode::ALPHA);
    g->push3DScene(McRect(0, 0, ticker_size.x, ticker_size.y));
    {
        g->rotate3DScene(this->fAnimation * 90, 0, 0);
        osu->getSliderFrameBuffer()->setColor(argb(a * (1.f - this->fAnimation), 1.f, 1.f, 1.f));
        osu->getSliderFrameBuffer()->draw(0, 0);
    }
    g->pop3DScene();
}

void Chat::update(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;

    if(this->user_list->isVisible()) {
        // Request presence & stats for on-screen user cards
        const McRect userlist_rect = this->user_list->getRect();
        for(auto *card : this->user_list->container.getElements<UserCard2>()) {
            if(userlist_rect.intersects(card->getRect())) {
                BANCHO::User::enqueue_presence_request(card->info);
                BANCHO::User::enqueue_stats_request(card->info);
            }
        }
        // update userlist immediately
        // TODO: lag/redundant update prevention
        if(!this->in_userlist_update) {
            // if we had a full layout update scheduled, do that, which includes a userlist update
            if(this->layout_update_scheduled) {
                this->updateLayout(osu->getVirtScreenSize());
                this->layout_update_scheduled = false;
                this->userlist_update_scheduled = false;
            }
            // otherwise only update the userlist
            if(this->userlist_update_scheduled) {
                this->updateUserList();
                this->userlist_update_scheduled = false;
            }
        }
    }

    UIScreen::update(c);

    // XXX: don't let mouse click through the buttons area
    this->button_container->update(c);
    if(this->selected_channel) {
        this->selected_channel->ui->update(c);
    }

    // HACKHACK: MOUSE3 handling
    static bool was_M3_down = false;
    bool is_M3_down = mouse->isMiddleDown();
    if(is_M3_down != was_M3_down) {
        if(is_M3_down) {
            auto mpos = mouse->getPos();

            // Try to close hovered channel
            for(auto &chan : this->channels) {
                if(chan->btn->getRect().contains(mpos)) {
                    this->leave(chan->name);
                    break;
                }
            }
        }

        was_M3_down = is_M3_down;
    }

    // Focus without placing the cursor at the end of the field
    this->input_box->focus(false);
}

void Chat::handle_command(const UString &msg) {
    if(msg == US_("/clear")) {
        this->selected_channel->messages.clear();
        this->updateLayout(osu->getVirtScreenSize());
        return;
    }

    if(msg == US_("/close") || msg == US_("/p") || msg == US_("/part")) {
        this->leave(this->selected_channel->name);
        return;
    }

    if(msg == US_("/help") || msg == US_("/keys")) {
        env->openURLInDefaultBrowser("https://osu.ppy.sh/wiki/en/Client/Interface/Chat_console");
        return;
    }

    if(msg == US_("/np")) {
        auto map = osu->getMapInterface()->getBeatmap();
        if(map == nullptr) {
            this->addSystemMessage("You are not listening to anything.");
            return;
        }

        std::string song_name =
            fmt::format("{:s} - {:s} [{:s}]", map->getArtist(), map->getTitle(), map->getDifficultyName());
        std::string song_link =
            fmt::format("[https://osu.{:s}/beatmaps/{:d} {:s}]", BanchoState::endpoint, map->getID(), song_name);

        std::string np_msg;
        if(osu->isInPlayMode()) {
            np_msg = fmt::format("\001ACTION is playing {:s}", song_link);

            Replay::Mods mods = osu->getScore()->mods;
            if(mods.speed != 1.f) {
                std::string speed_modifier = fmt::format(" x{:.1f}", mods.speed);
                np_msg.append(speed_modifier);
            }
            UString mod_string = ScoreButton::getModsStringForDisplay(mods);
            if(mod_string.length() > 0) {
                np_msg.append(" (+");
                np_msg.append(mod_string.utf8View());
                np_msg.append(")");
            }

            np_msg.append("\001");
        } else {
            np_msg = fmt::format("\001ACTION is listening to {:s}\001", song_link);
        }

        this->send_message(np_msg);
        return;
    }

    if(msg.startsWith(US_("/addfriend "))) {
        auto friend_name = msg.substr<std::string>(11);
        UserInfo *user = BANCHO::User::find_user(friend_name);
        if(!user) {
            this->addSystemMessage(fmt::format("User '{:s}' not found. Are they online?", friend_name));
            return;
        }

        if(user->is_friend()) {
            this->addSystemMessage(fmt::format("You are already friends with {:s}!", friend_name));
        } else {
            Packet packet;
            packet.id = OUTP_FRIEND_ADD;
            packet.write<i32>(user->user_id);
            BANCHO::Net::send_packet(packet);

            BANCHO::User::friends.insert(user->user_id);

            this->addSystemMessage(fmt::format("You are now friends with {:s}.", friend_name));
        }

        return;
    }

    if(msg.startsWith(US_("/bb "))) {
        this->addChannel("BanchoBot", true);
        this->send_message(msg.substr(4));
        return;
    }

    if(msg == US_("/away")) {
        this->away_msg.clear();
        this->addSystemMessage("Away message removed.");
        return;
    }
    if(msg.startsWith(US_("/away "))) {
        this->away_msg = msg.substr(6);
        this->addSystemMessage(fmt::format("Away message set to '{:s}'.", this->away_msg));
        return;
    }

    if(msg.startsWith(US_("/delfriend "))) {
        auto friend_name = msg.substr(11);
        auto *user = BANCHO::User::find_user(friend_name.utf8View());
        if(!user) {
            this->addSystemMessage(fmt::format("User '{:s}' not found. Are they online?", friend_name));
            return;
        }

        if(user->is_friend()) {
            Packet packet;
            packet.id = OUTP_FRIEND_REMOVE;
            packet.write<i32>(user->user_id);
            BANCHO::Net::send_packet(packet);

            auto it = std::ranges::find(BANCHO::User::friends, user->user_id);
            if(it != BANCHO::User::friends.end()) {
                BANCHO::User::friends.erase(it);
            }

            this->addSystemMessage(fmt::format("You are no longer friends with {:s}.", friend_name));
        } else {
            this->addSystemMessage(fmt::format("You aren't friends with {:s}!", friend_name));
        }

        return;
    }

    if(msg.startsWith(US_("/me "))) {
        auto new_text = msg.substr(3);
        new_text.insert(0, "\001ACTION");
        new_text.append("\001");
        this->send_message(new_text);
        return;
    }

    if(msg.startsWith(US_("/chat ")) || msg.startsWith(US_("/msg ")) || msg.startsWith(US_("/query "))) {
        auto username = msg.substr(msg.find(L" "));
        this->addChannel(username, true);
        return;
    }

    if(msg.startsWith(US_("/invite "))) {
        if(!BanchoState::is_in_a_multi_room()) {
            this->addSystemMessage("You are not in a multiplayer room!");
            return;
        }

        auto username = msg.substr(8);
        auto invite_msg = fmt::format("\001ACTION has invited you to join [osump://{:d}/{:s} {:s}]\001",
                                      BanchoState::room.id, BanchoState::room.password, BanchoState::room.name);

        Packet packet;
        packet.id = OUTP_SEND_PRIVATE_MESSAGE;
        packet.write_string(BanchoState::get_username());
        packet.write_string(invite_msg);
        packet.write_string(username.utf8View());
        packet.write<i32>(BanchoState::get_uid());
        BANCHO::Net::send_packet(packet);

        this->addSystemMessage(fmt::format("{:s} has been invited to the game.", username));
        return;
    }

    if(msg.startsWith(US_("/j ")) || msg.startsWith(US_("/join "))) {
        auto channel = msg.substr(msg.find(u' '));
        this->join(channel);
        return;
    }

    if(msg.startsWith(US_("/p ")) || msg.startsWith(US_("/part "))) {
        auto channel = msg.substr(msg.find(u' '));
        this->leave(channel);
        return;
    }

    this->addSystemMessage(US_("This command is not supported."));
}

void Chat::onKeyDown(KeyboardEvent &key) {
    if(!this->bVisible) return;
    SCANCODE sc = key.getScanCode();

    if(keyboard->isAltDown() && sc >= KEY_1 && sc <= KEY_0) {
        static_assert((int)KEY_1 + 9 == (int)KEY_0);

        // KEY_1 => tab_select := 0
        const i32 tab_select = KEY_1 - sc;

        key.consume();
        if(tab_select < this->channels.size()) {
            this->switchToChannel(this->channels[tab_select]);
        }
        return;
    }

    if(sc == KEY_PAGEUP) {
        if(this->selected_channel != nullptr) {
            key.consume();
            this->selected_channel->ui->scrollY(this->getSize().y - this->input_box_height);
            return;
        }
    }

    if(sc == KEY_PAGEDOWN) {
        if(this->selected_channel != nullptr) {
            key.consume();
            this->selected_channel->ui->scrollY(-(this->getSize().y - this->input_box_height));
            return;
        }
    }

    // Escape: close chat
    if(sc == KEY_ESCAPE) {
        if(this->isVisibilityForced()) return;

        key.consume();
        this->user_wants_chat = false;
        this->updateVisibility();
        return;
    }

    // Return: send message
    if(sc == KEY_ENTER || sc == KEY_NUMPAD_ENTER) {
        key.consume();
        if(this->selected_channel != nullptr && this->input_box->getText().length() > 0) {
            if(this->input_box->getText()[0] == L'/') {
                this->handle_command(this->input_box->getText());
            } else {
                this->send_message(this->input_box->getText());
            }

            soundEngine->play(osu->getSkin()->s_message_sent);
            this->input_box->clear();
        }
        this->tab_completion_prefix.clear();
        this->tab_completion_match.clear();
        return;
    }

    // Ctrl+W: Close current channel
    if(keyboard->isControlDown() && sc == KEY_W) {
        key.consume();
        if(this->selected_channel != nullptr) {
            this->leave(this->selected_channel->name);
        }
        return;
    }

    // Ctrl+Tab: Switch channels
    if(keyboard->isControlDown() && (sc == KEY_TAB)) {
        key.consume();
        if(this->selected_channel == nullptr) return;
        int chan_index = this->channels.size();
        for(auto chan : this->channels) {
            if(chan == this->selected_channel) {
                break;
            }
            chan_index++;
        }

        if(keyboard->isShiftDown()) {
            // Ctrl+Shift+Tab: Go to previous channel
            auto new_chan = this->channels[(chan_index - 1) % this->channels.size()];
            this->switchToChannel(new_chan);
        } else {
            // Ctrl+Tab: Go to next channel
            auto new_chan = this->channels[(chan_index + 1) % this->channels.size()];
            this->switchToChannel(new_chan);
        }

        soundEngine->play(osu->getSkin()->s_click_button);

        return;
    }

    // TAB: Complete nickname
    if(sc == KEY_TAB) {
        key.consume();

        auto text = this->input_box->getText();
        i32 username_start_idx = text.findLast(US_(" "), 0, this->input_box->iCaretPosition) + 1;
        i32 username_end_idx = this->input_box->iCaretPosition;
        i32 username_len = username_end_idx - username_start_idx;

        if(this->tab_completion_prefix.length() == 0) {
            this->tab_completion_prefix = text.substr(username_start_idx, username_len);
        } else {
            username_start_idx = this->input_box->iCaretPosition - this->tab_completion_match.length();
            username_len = username_end_idx - username_start_idx;
        }

        const auto *user = BANCHO::User::find_user_starting_with(this->tab_completion_prefix.utf8View(),
                                                                 this->tab_completion_match.utf8View());
        if(user) {
            this->tab_completion_match = user->name;

            // Remove current username, add new username
            // TODO(spec): these should be internal fields, why are we manipulating them directly like this?
            //             makes it so much more difficult to refactor things uniformly...
            this->input_box->sText.erase(this->input_box->iCaretPosition - username_len, username_len);
            this->input_box->iCaretPosition -= username_len;
            this->input_box->sText.insert(this->input_box->iCaretPosition, this->tab_completion_match);
            this->input_box->iCaretPosition += this->tab_completion_match.length();
            this->input_box->setText(this->input_box->sText);
            this->input_box->tickCaret();

            soundEngine->play(osu->getSound((ActionSound)((prand() % 4) + (size_t)ActionSound::TYPING1)));
        }

        return;
    }

    // Typing in chat: capture keypresses
    if(!keyboard->isAltDown()) {
        this->tab_completion_prefix.clear();
        this->tab_completion_match.clear();
        this->input_box->onKeyDown(key);
        key.consume();
        return;
    }
}

void Chat::onKeyUp(KeyboardEvent &key) {
    if(!this->bVisible || key.isConsumed()) return;

    this->input_box->onKeyUp(key);
    key.consume();
}

void Chat::onChar(KeyboardEvent &key) {
    if(!this->bVisible || key.isConsumed()) return;

    this->input_box->onChar(key);
    key.consume();
}

void Chat::mark_as_read(ChatChannel *chan) {
    if(!this->bVisible) return;

    // XXX: Only mark as read after 500ms
    chan->read = true;

    std::string url{"web/osu-markasread.php?"};
    url.append(fmt::format("channel={:s}", Mc::Net::urlEncode(chan->name.utf8View())));
    BANCHO::Api::append_auth_params(url);

    BANCHO::Api::Request request;

    request.type = BANCHO::Api::MARK_AS_READ;
    request.path = url;

    BANCHO::Api::send_request(request);
}

void Chat::switchToChannel(ChatChannel *chan) {
    this->selected_channel = chan;
    if(!chan->read) {
        this->mark_as_read(this->selected_channel);
    }

    // Update button colors
    this->updateButtonLayout(this->getSize());
}

void Chat::openChannel(const UString &channel_name) {
    this->addChannel(channel_name, true);
    this->user_wants_chat = true;
    this->updateVisibility();
}

void Chat::addChannel(const UString &channel_name, bool switch_to) {
    for(auto chan : this->channels) {
        if(chan->name == channel_name) {
            if(switch_to) {
                this->switchToChannel(chan);
            }
            return;
        }
    }

    auto *chan = new ChatChannel(this, channel_name);
    this->channels.push_back(chan);

    if((switch_to) ||                                                          //
       (this->selected_channel == nullptr && this->channels.size() == 1) ||    //
       (channel_name == US_("#multiplayer") || channel_name == US_("#lobby"))  //
    ) {
        this->switchToChannel(chan);
    }

    this->updateLayout(osu->getVirtScreenSize());

    if(this->isVisible()) {
        soundEngine->play(osu->getSkin()->s_expand);
    }
}

void Chat::addMessage(UString channel_name, const ChatMessage &msg, bool mark_unread) {
    const auto *user = BANCHO::User::get_user_info(msg.author_id);
    const bool chatter_is_moderator = (msg.author_id == 0) ||  // system message
                                      (user->privileges & Privileges::MODERATOR);

    auto ignore_list = SString::split(cv::chat_ignore_list.getString(), ' ');
    auto msg_words = msg.text.split(US_(" "));
    if(!chatter_is_moderator) {
        for(auto &word : msg_words) {
            for(const auto ignored : ignore_list) {
                if(ignored.empty()) continue;

                word.lowerCase();
                if(word.utf8View() == SString::to_lower(ignored)) {
                    // Found a word we don't want - don't print the message
                    return;
                }
            }
        }
    }

    bool should_highlight = false;
    auto highlight_list = SString::split(cv::chat_highlight_words.getString(), ' ');
    for(auto &word : msg_words) {
        if(chatter_is_moderator && word == US_("@everyone")) {
            should_highlight = true;
            break;
        }

        for(const auto highlight : highlight_list) {
            if(highlight.empty()) continue;

            word.lowerCase();
            if(word.utf8View() == SString::to_lower(highlight)) {
                should_highlight = true;
                break;
            }
        }
    }
    if(should_highlight) {
        // TODO: highlight message
        auto notif = fmt::format("{} mentioned you in {}", msg.author_name, channel_name);
        ui->getNotificationOverlay()->addToast(
            std::move(notif), CHAT_TOAST, [channel_name] { ui->getChat()->openChannel(channel_name); }, ToastElement::TYPE::CHAT);
    }

    bool is_pm =
        msg.author_id != 0 && channel_name[0] != '#' && msg.author_name.utf8View() != BanchoState::get_username();
    if(is_pm) {
        // If it's a PM, the channel title should be the one who sent the message
        channel_name = msg.author_name;

        if(cv::chat_notify_on_dm.getBool()) {
            auto notif = fmt::format("{} sent you a message", msg.author_name);
            ui->getNotificationOverlay()->addToast(
                std::move(notif), CHAT_TOAST, [channel_name] { ui->getChat()->openChannel(channel_name); },
                ToastElement::TYPE::CHAT);
        }
        if(cv::chat_ping_on_mention.getBool()) {
            // Yes, osu! really does use "match-start.wav" for when you get pinged
            // XXX: Use it as fallback, allow neosu-targeting skins to have custom ping sound
            soundEngine->play(osu->getSkin()->s_match_start);
        }
    }

    const bool mentioned = (msg.author_id != BanchoState::get_uid()) &&
                           SString::contains_ncase(msg.text.utf8View(), BanchoState::get_username());
    if(mentioned && cv::chat_notify_on_mention.getBool()) {
        auto notif = fmt::format("You were mentioned in {:s}", channel_name);
        ui->getNotificationOverlay()->addToast(
            std::move(notif), CHAT_TOAST, [channel_name] { ui->getChat()->openChannel(channel_name); }, ToastElement::TYPE::CHAT);
    }
    if(mentioned && cv::chat_ping_on_mention.getBool()) {
        // Yes, osu! really does use "match-start.wav" for when you get pinged
        // XXX: Use it as fallback, allow neosu-targeting skins to have custom ping sound
        soundEngine->play(osu->getSkin()->s_match_start);
    }

    this->addChannel(channel_name);
    for(auto chan : this->channels) {
        if(chan->name != channel_name) continue;
        chan->messages.push_back(msg);
        chan->add_message(msg);

        if(mark_unread) {
            chan->read = false;
            if(chan == this->selected_channel) {
                this->mark_as_read(chan);

                // Update ticker
                if(msg.author_name.toUtf8() != BanchoState::get_username()) {
                    auto screen = osu->getVirtScreenSize();
                    this->ticker_tms = engine->getTime();
                    this->ticker->messages.clear();
                    this->ticker->messages.push_back(msg);
                    this->ticker->add_message(msg);
                    this->updateTickerLayout(screen);
                }
            } else {
                this->updateButtonLayout(this->getSize());
            }
        }

        if(chan->messages.size() > 100) {
            chan->messages.erase(chan->messages.begin());
        }

        break;
    }

    if(is_pm && this->away_msg.length() > 0) {
        Packet packet;
        packet.id = OUTP_SEND_PRIVATE_MESSAGE;

        packet.write_string(BanchoState::get_username());
        packet.write_string(this->away_msg.utf8View());
        packet.write_string(msg.author_name.utf8View());
        packet.write<i32>(BanchoState::get_uid());
        BANCHO::Net::send_packet(packet);

        // Server doesn't echo the message back
        this->addMessage(channel_name,
                         ChatMessage{
                             .tms = time(nullptr),
                             .author_id = BanchoState::get_uid(),
                             .author_name = BanchoState::get_username(),
                             .text = this->away_msg,
                         },
                         false);
    }
}

void Chat::addSystemMessage(UString msg) {
    this->addMessage(this->selected_channel->name, ChatMessage{
                                                       .tms = time(nullptr),
                                                       .author_id = 0,
                                                       .author_name = {},
                                                       .text = std::move(msg),
                                                   });
}

void Chat::removeChannel(const UString &channel_name) {
    ChatChannel *chan = nullptr;
    for(auto c : this->channels) {
        if(c->name == channel_name) {
            chan = c;
            break;
        }
    }

    if(chan == nullptr) return;

    auto it = std::ranges::find(this->channels, chan);
    this->channels.erase(it);
    if(this->selected_channel == chan) {
        this->selected_channel = nullptr;
        if(!this->channels.empty()) {
            this->switchToChannel(this->channels[0]);
        }
    }

    delete chan;
    this->updateButtonLayout(this->getSize());
}

void Chat::updateLayout(vec2 newResolution) {
    this->input_box_height = 30.f * Osu::getUIScale();
    this->button_height = 26.f * Osu::getUIScale();

    this->updateTickerLayout(newResolution);

    // We don't want to update while the chat is hidden, to avoid lagspikes during gameplay
    if(!this->bVisible) {
        this->layout_update_scheduled = true;
        return;
    }

    // In the lobby and in multi rooms don't take the full horizontal width to allow for cleaner UI designs.
    if(this->isSmallChat()) {
        newResolution.x = std::round(newResolution.x * 0.6f);
    }

    this->setSize(newResolution);

    const float chat_w = newResolution.x;
    const float chat_h = std::round(newResolution.y * 0.3f) - this->input_box_height;
    const float chat_y = newResolution.y - (chat_h + this->input_box_height);
    for(auto chan : this->channels) {
        chan->updateLayout(vec2{0.f, chat_y}, vec2{chat_w, chat_h});
    }

    this->input_box->setPos(vec2{0.f, chat_y + chat_h});
    this->input_box->setSize(vec2{chat_w, this->input_box_height});

    if(this->selected_channel == nullptr && !this->channels.empty()) {
        this->selected_channel = this->channels[0];
        this->selected_channel->read = true;
    }

    this->updateButtonLayout(newResolution);
    this->updateButtonLayout(newResolution);  // idk
    this->layout_update_scheduled = false;
}

void Chat::updateButtonLayout(vec2 screen) {
    const f32 dpiScale = Osu::getUIScale();
    const f32 space = 20.f * dpiScale;
    const f32 border = 2.f * dpiScale;
    const f32 initial_x = border;
    f32 total_x = initial_x;

    std::ranges::sort(this->channels, [](ChatChannel *a, ChatChannel *b) { return a->name < b->name; });

    // Look, I really tried. But for some reason setPos() doesn't work until we change
    // the screen resolution once. So I'll just compute absolute position manually.
    f32 button_container_height = this->button_height + border;
    for(auto chan : this->channels) {
        UIButton *btn = chan->btn;
        const f32 button_width = chat_font->getStringWidth(btn->getText()) + space;

        // Wrap channel buttons
        if(total_x + button_width > screen.x - space) {
            total_x = initial_x;
            button_container_height += this->button_height;
        }

        total_x += button_width;
    }

    const float chat_y = std::round(screen.y * 0.7f);
    float total_y = 0.f;
    total_x = initial_x;
    for(auto chan : this->channels) {
        UIButton *btn = chan->btn;
        float button_width = chat_font->getStringWidth(btn->getText()) + space;

        // Wrap channel buttons
        if(total_x + button_width > screen.x - space) {
            total_x = initial_x;
            total_y += this->button_height;
        }

        btn->setPos(total_x, chat_y - button_container_height + total_y);
        // Buttons are drawn a bit smaller than they should, so we up the size here
        btn->setSize(button_width + border, this->button_height + border);

        if(this->selected_channel->name == btn->getText()) {
            btn->setColor(0xffc9c9c9);
        } else {
            if(chan->read) {
                btn->setColor(0xff20265e);
            } else {
                btn->setColor(0xff677abc);
            }
        }

        total_x += button_width;
    }

    this->join_channel_btn->setPos(total_x, chat_y - button_container_height + total_y);
    this->join_channel_btn->setSize(button_container_height, button_container_height);
    this->button_container->setPos(0, chat_y - button_container_height);
    this->button_container->setSize(screen.x, button_container_height);

    // Update user list here, since we'll size it based on chat console height (including buttons)
    this->updateUserList();
}

void Chat::updateTickerLayout(vec2 screen) {
    this->ticker->updateLayout(vec2{0.f, 0.f}, screen);

    f32 h = this->ticker->ui->getScrollSize().y + 5.f * Osu::getUIScale();
    this->ticker->ui->setPos(vec2{0.f, screen.y - h});
    this->ticker->ui->setSize(vec2{screen.x, h});
}

void Chat::updateUserList() {
    // We don't want to update while the chat is hidden, to avoid lagspikes during gameplay
    if(!this->bVisible ||
       this->in_userlist_update /* FIXME: recursively calling updateUserList when creating UserCard2s */) {
        this->userlist_update_scheduled = true;
        return;
    }

    this->in_userlist_update = true;

    // XXX: don't use SongBrowser::getUIScale
    auto card_size = vec2{SongBrowser::getUIScale(320), SongBrowser::getUIScale(75)};

    auto size = this->getSize();
    size.y = this->button_container->getPos().y;
    this->user_list->setSize(size);

    const f32 MARGIN = 10.f;
    const f32 INITIAL_MARGIN = MARGIN * 1.5;
    f32 total_x = INITIAL_MARGIN;
    f32 total_y = INITIAL_MARGIN;

    // XXX: Optimize so fps doesn't halve when F9 is open
    //      (still not optimal, but not as bad anymore)

    std::vector<const UserInfo *> sorted_users;
    for(const auto &pair : BANCHO::User::online_users) {
        if(pair.second->user_id != 0) {
            sorted_users.push_back(pair.second);
        }
    }

    // Intentionally not calling this->user_list->invalidate(), because that would affect scroll position/animation
    auto old_card_elems_copy = this->user_list->container.getElements<UserCard2>();
    this->user_list->container.invalidate();  // clear scrollview container elements and rebuild

    // FIXME: dumb to sort this every time, can cause pop-in and jarring reshuffling in f9 menu buttons
    std::ranges::sort(sorted_users, SString::alnum_comp, [](const UserInfo *ui) { return ui->name; });

    for(const auto *user : sorted_users) {
        if(total_x + card_size.x + MARGIN > size.x) {
            total_x = INITIAL_MARGIN;
            total_y += card_size.y + MARGIN;
        }

        UserCard2 *card = nullptr;
        // super overkill lazy hack
        if(auto old_card_it =
               std::ranges::find_if(old_card_elems_copy, [user](const UserCard2 *card) { return card->info == user; });
           old_card_it != old_card_elems_copy.end()) {
            card = *old_card_it;

            card->update_userid(user->user_id);

            // remove it so we don't delete it later (moved into container)
            old_card_elems_copy.erase(old_card_it);
        } else {
            card = new UserCard2(user->user_id);
        }

        card->setSize(card_size);
        card->setPos(total_x, total_y);
        card->setVisible(false);

        this->user_list->container.addBaseUIElement(card);

        // Only display the card if we have presence data
        // (presence data is only fetched *after* UserCard2 is initialized)
        if(user->has_presence) {
            card->setVisible(true);
            total_x += card_size.x + MARGIN * 1.5;  // idk why margin is bogged
        }
    }

    this->user_list->setScrollSizeToContent();

    // delete any excess items in the container
    for(auto *card : old_card_elems_copy) {
        SAFE_DELETE(card);
    }

    // recursion prevention
    this->in_userlist_update = false;
}

void Chat::join(const UString &channel_name) {
    // XXX: Open the channel immediately, without letting the user send messages in it.
    //      Would require a way to signal if a channel is joined or not.
    //      Would allow to keep open the tabs of the channels we got kicked out of.
    Packet packet;
    packet.id = OUTP_CHANNEL_JOIN;
    packet.write_string(channel_name.utf8View());
    BANCHO::Net::send_packet(packet);
}

void Chat::leave(const UString &channel_name) {
    const bool send_leave_packet =
        (channel_name[0] == u'#') && !(channel_name == US_("#lobby") || channel_name == US_("#multiplayer"));

    if(send_leave_packet) {
        Packet packet;
        packet.id = OUTP_CHANNEL_PART;
        packet.write_string(channel_name.utf8View());
        BANCHO::Net::send_packet(packet);
    }

    this->removeChannel(channel_name);

    soundEngine->play(osu->getSkin()->s_close_chat_tab);
}

void Chat::send_message(const UString &msg) {
    Packet packet;
    packet.id = this->selected_channel->name[0] == u'#' ? OUTP_SEND_PUBLIC_MESSAGE : OUTP_SEND_PRIVATE_MESSAGE;

    packet.write_string(BanchoState::get_username());
    packet.write_string(msg.utf8View());
    packet.write_string(this->selected_channel->name.utf8View());
    packet.write<i32>(BanchoState::get_uid());
    BANCHO::Net::send_packet(packet);

    // Server doesn't echo the message back
    this->addMessage(this->selected_channel->name,
                     ChatMessage{
                         .tms = time(nullptr),
                         .author_id = BanchoState::get_uid(),
                         .author_name = BanchoState::get_username(),
                         .text = msg,
                     },
                     false);
}

void Chat::onDisconnect() {
    for(auto chan : this->channels) {
        delete chan;
    }
    this->channels.clear();

    for(const auto &chan : BanchoState::chat_channels) {
        delete chan.second;
    }
    BanchoState::chat_channels.clear();

    this->selected_channel = nullptr;
    this->updateLayout(osu->getVirtScreenSize());

    this->updateVisibility();
}

void Chat::onResolutionChange(vec2 newResolution) { this->updateLayout(newResolution); }

bool Chat::isSmallChat() {
    if(ui->getRoom() == nullptr || ui->getLobby() == nullptr || ui->getSongBrowser() == nullptr) return false;
    bool sitting_in_room =
        ui->getRoom()->isVisible() && !ui->getSongBrowser()->isVisible() && !BanchoState::is_playing_a_multi_map();
    bool sitting_in_lobby = ui->getLobby()->isVisible();
    return sitting_in_room || sitting_in_lobby;
}

bool Chat::isVisibilityForced() {
    bool is_forced = this->isSmallChat();
    if(is_forced != this->visibility_was_forced) {
        // Chat width changed: update the layout now
        this->visibility_was_forced = is_forced;
        this->updateLayout(osu->getVirtScreenSize());
    }
    return is_forced;
}

void Chat::updateVisibility() {
    bool can_skip = osu->getMapInterface()->isInSkippableSection();
    bool is_spectating = cv::mod_autoplay.getBool() || (cv::mod_autopilot.getBool() && cv::mod_relax.getBool()) ||
                         osu->getMapInterface()->is_watching || BanchoState::spectating;
    bool is_clicking_circles =
        osu->isInPlayMode() && !can_skip && !is_spectating && !ui->getPauseOverlay()->isVisible();
    if(BanchoState::is_playing_a_multi_map() && !osu->getMapInterface()->all_players_loaded) {
        is_clicking_circles = false;
    }
    is_clicking_circles &= cv::chat_auto_hide.getBool();
    bool force_hide = ui->getOptionsOverlay()->isVisible() || ui->getModSelector()->isVisible() || is_clicking_circles;
    if(!BanchoState::is_online()) force_hide = true;

    if(force_hide) {
        this->setVisible(false);
    } else if(this->isVisibilityForced()) {
        this->setVisible(true);
    } else {
        this->setVisible(this->user_wants_chat);
    }
}

CBaseUIContainer *Chat::setVisible(bool visible) {
    if(visible == this->bVisible) return this;

    soundEngine->play(osu->getSkin()->s_click_button);

    if(visible && !BanchoState::is_online()) {
        ui->getOptionsOverlay()->askForLoginDetails();
        return this;
    }

    this->bVisible = visible;
    if(visible) {
        ui->getOptionsOverlay()->setVisible(false);
        anim::moveQuartOut(&this->fAnimation, 1.0f, 0.25f * (1.0f - this->fAnimation), true);

        if(this->selected_channel != nullptr && !this->selected_channel->read) {
            this->mark_as_read(this->selected_channel);
        }

        if(this->layout_update_scheduled) {
            this->updateLayout(osu->getVirtScreenSize());
        }
    } else {
        anim::moveQuadOut(&this->fAnimation, 0.0f, 0.25f * this->fAnimation, true);
    }

    // HACKHACK for text input listening
    osu->updateWindowsKeyDisable();

    return this;
}

bool Chat::isMouseInChat() {
    if(!this->isVisible()) return false;
    if(this->selected_channel == nullptr) return false;
    return this->input_box->isMouseInside() || this->selected_channel->ui->isMouseInside();
}

void Chat::askWhatChannelToJoin(CBaseUIButton * /*btn*/) {
    // XXX: Could display nicer UI with full channel list (chat_channels in Bancho.cpp)
    ui->getPromptOverlay()->prompt("Type in the channel you want to join (e.g. '#osu'):",
                                   SA::MakeDelegate<&Chat::join>(this));
}
