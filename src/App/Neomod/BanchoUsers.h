#pragma once
// Copyright (c) 2023, kiwec, All rights reserved.

#include "BanchoProtocol.h"
#include "ModFlags.h"

#include <unordered_map>
#include <string>
#include <vector>

// The fields are slightly strangely ordered/packed for struct layout purposes.
struct UserInfo {
    UserInfo() noexcept = default;
    ~UserInfo() noexcept;

    UserInfo(const UserInfo &) noexcept = default;
    UserInfo &operator=(const UserInfo &) noexcept = default;
    UserInfo(UserInfo &&) noexcept = default;
    UserInfo &operator=(UserInfo &&) noexcept = default;

    MD5Hash map_md5;

    std::string name{};  // From presence
    std::string info_text{"Loading..."};

    i32 user_id{0};

    // Stats (via USER_STATS_REQUEST)
    i32 map_id{0};
    u64 stats_tms{0};
    i64 total_score{0};
    i64 ranked_score{0};
    i32 plays{0};
    f32 accuracy{0.f};
    LegacyFlags mods{};
    u16 pp{0};

    Action action{Action::UNKNOWN};
    GameMode mode{GameMode::STANDARD};

    // Presence (via USER_PRESENCE_REQUEST or USER_PRESENCE_REQUEST_ALL)
    f32 longitude{0.f};
    f32 latitude{0.f};
    i32 global_rank{0};
    u8 utc_offset{0};
    u8 country{0};
    u8 privileges{0};

    bool has_presence : 1 {false};
    bool irc_user : 1 {false};

    LiveReplayAction spec_action : 6 {LiveReplayAction::NONE};  // Received when spectating

    [[nodiscard]] bool is_friend() const;
};

namespace BANCHO::User {

static inline bool is_online_id(i32 id) { return id > 0 || id < -10000; }

extern Hash::flat::map<i32, const UserInfo *> online_users;
extern Hash::flat::set<i32> friends;

void login_user(i32 user_id);
void logout_user(i32 user_id);
void logout_all_users();

UserInfo *find_user(std::string_view username);
const UserInfo *find_user_starting_with(std::string_view prefix, std::string_view last_match);
UserInfo *try_get_user_info(i32 user_id, bool wants_presence = false);
UserInfo *get_user_info(i32 user_id, bool wants_presence = false);

void dequeue_presence_request(const UserInfo *info);
void dequeue_stats_request(const UserInfo *info);
void enqueue_presence_request(const UserInfo *info);
void enqueue_stats_request(const UserInfo *info);
void request_presence_batch();
void request_stats_batch();
}  // namespace BANCHO::User
