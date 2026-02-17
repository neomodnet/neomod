// Copyright (c) 2023, kiwec, All rights reserved.
#include "BanchoUsers.h"

#include "Bancho.h"
#include "BanchoNetworking.h"
#include "BanchoProtocol.h"
#include "Chat.h"
#include "OsuConVars.h"
#include "Engine.h"
#include "NotificationOverlay.h"
#include "SpectatorScreen.h"
#include "SString.h"
#include "Timing.h"
#include "UI.h"
#include "Logging.h"

#include <algorithm>

namespace BANCHO::User {

Hash::flat::map<i32, const UserInfo*> online_users;  // pointers to all_users elements
Hash::flat::set<i32> friends;
namespace {                                   // static
std::unordered_map<i32, UserInfo> all_users;  // needs pointer stability, so std::unordered is fine
Hash::flat::set<const UserInfo*> presence_requests;
Hash::flat::set<const UserInfo*> stats_requests;
}  // namespace

void dequeue_presence_request(const UserInfo* info) { presence_requests.erase(info); }
void dequeue_stats_request(const UserInfo* info) { stats_requests.erase(info); }

void enqueue_presence_request(const UserInfo* info) {
    if(info->has_presence) return;
    if(presence_requests.contains(info)) return;
    presence_requests.insert(info);
}

void enqueue_stats_request(const UserInfo* info) {
    if(info->irc_user) return;
    if(info->stats_tms + 5000 > Timing::getTicksMS()) return;
    if(stats_requests.contains(info)) return;
    stats_requests.insert(info);
}

void request_presence_batch() {
    Hash::flat::set<i32> actual_requests;
    for(const auto* req : presence_requests) {
        if(req->has_presence) continue;
        actual_requests.insert(req->user_id);
    }

    presence_requests.clear();
    if(actual_requests.empty()) return;

    Packet packet;
    packet.id = OUTP_USER_PRESENCE_REQUEST;
    packet.write<u16>(actual_requests.size());
    for(i32 user_id : actual_requests) {
        packet.write<i32>(user_id);
    }
    BANCHO::Net::send_packet(packet);
}

void request_stats_batch() {
    Hash::flat::set<i32> actual_requests;
    for(const auto* req : stats_requests) {
        if(req->irc_user) continue;
        if(req->stats_tms + 5000 > Timing::getTicksMS()) continue;
        actual_requests.insert(req->user_id);
    }

    stats_requests.clear();
    if(actual_requests.empty()) return;

    Packet packet;
    packet.id = OUTP_USER_STATS_REQUEST;
    packet.write<u16>(actual_requests.size());
    for(i32 user_id : actual_requests) {
        packet.write<i32>(user_id);
    }
    BANCHO::Net::send_packet(packet);
}

void login_user(i32 user_id) {
    // We mark the user as online, but don't request presence data
    // Presence & stats are only requested when the user shows up in UI
    online_users[user_id] = get_user_info(user_id, false);
}

void logout_user(i32 user_id) {
    if(const auto& it = online_users.find(user_id); it != online_users.end()) {
        const auto* user_info = it->second;

        debugLog("{:s} has disconnected.", user_info->name);
        if(user_id == BanchoState::spectated_player_id) {
            Spectating::stop();
        }

        if(user_info->is_friend() && cv::notify_friend_status_change.getBool()) {
            auto text = fmt::format("{} is now offline", user_info->name);
            ui->getNotificationOverlay()->addToast(text, STATUS_TOAST, {}, ToastElement::TYPE::CHAT);
        }

        online_users.erase(it);
        dequeue_presence_request(user_info);
        dequeue_stats_request(user_info);
        ui->getChat()->updateUserList();
    }
}

void logout_all_users() {
    online_users.clear();
    friends.clear();
    presence_requests.clear();
    stats_requests.clear();
    all_users.clear();
}

UserInfo* find_user(std::string_view username) {
    if(const auto& it = std::ranges::find_if(
           all_users, [&username](const auto& info_pair) { return info_pair.second.name == username; });
       it != all_users.end()) {
        return &it->second;
    }

    return nullptr;
}

const UserInfo* find_user_starting_with(std::string_view prefix, std::string_view last_match) {
    if(prefix.empty()) return nullptr;

    std::string prefixLower = SString::to_lower(prefix);

    // cycle through matches
    bool matched = last_match.length() == 0;
    for(auto& [_, user] : online_users) {
        if(!matched) {
            if(user->name == last_match) {
                matched = true;
            }
            continue;
        }
        std::string usernameLower = SString::to_lower(user->name);

        // if it starts with prefix
        if(usernameLower.starts_with(prefixLower)) {
            return user;
        }
    }

    if(last_match.length() == 0) {
        return nullptr;
    } else {
        return find_user_starting_with(prefixLower, "");
    }
}

UserInfo* try_get_user_info(i32 user_id, bool wants_presence) {
    if(const auto& it = all_users.find(user_id); it != all_users.end()) {
        auto* user_info = &it->second;
        if(wants_presence) {
            enqueue_presence_request(user_info);
        }

        return user_info;
    }

    return nullptr;
}

UserInfo* get_user_info(i32 user_id, bool wants_presence) {
    auto* existing_info = try_get_user_info(user_id, wants_presence);
    if(existing_info) {
        return existing_info;
    }

    std::pair<i32, UserInfo> temp_new_info{user_id, UserInfo{}};

    temp_new_info.second.user_id = user_id;
    temp_new_info.second.name = fmt::format("User #{:d}", user_id);
    const auto& [inserted_it, successfully_inserted] = all_users.emplace(std::move(temp_new_info));
    assert(successfully_inserted);
    auto* new_info = &inserted_it->second;

    ui->getChat()->updateUserList();

    if(wants_presence) {
        enqueue_presence_request(new_info);
    }

    return new_info;
}

}  // namespace BANCHO::User

using namespace BANCHO::User;

UserInfo::~UserInfo() {
    // remove from vectors
    dequeue_presence_request(this);
    dequeue_stats_request(this);
}

bool UserInfo::is_friend() const { return friends.contains(this->user_id); }
