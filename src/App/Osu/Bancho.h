#pragma once
// Copyright (c) 2023, kiwec, All rights reserved.

#include "BanchoProtocol.h"
#include "Hashing.h"

#include <atomic>
#include <vector>
#include <array>

class Image;

enum class ServerPolicy : uint8_t {
    NO,
    YES,
    NO_PREFERENCE,
};

enum class OnlineStatus : u8 { LOGGED_OUT, POLLING, LOGIN_IN_PROGRESS, LOGGED_IN };

struct BanchoState final {
    // entirely static
    BanchoState() = delete;
    ~BanchoState() = delete;

    BanchoState &operator=(const BanchoState &) = delete;
    BanchoState &operator=(BanchoState &&) = delete;
    BanchoState(const BanchoState &) = delete;
    BanchoState(BanchoState &&) = delete;

    static std::string endpoint;
    static MD5String pw_md5;
    static std::array<u8, 32> oauth_challenge;
    static std::array<u8, 32> oauth_verifier;
    static bool is_oauth;
    static bool fully_supports_neosu;

    static bool spectating;
    static i32 spectated_player_id;
    static std::vector<u32> spectators;
    static std::vector<u32> fellow_spectators;

    static std::string server_icon_url;
    static Image *server_icon;

    static ServerPolicy score_submission_policy;

    static std::string neosu_version;
    static std::string cho_token;
    static std::string user_agent;
    static std::string client_hashes;

    static Room room;
    static bool match_started;
    static std::array<Slot, 16> last_scores;

    struct Channel {
        std::string name;
        std::string topic;
        u32 nb_members;
    };

    static Hash::unstable_stringmap<BanchoState::Channel *> chat_channels;

    // utils
    static void handle_packet(Packet &packet);
    static std::string build_login_packet();
    static void update_online_status(OnlineStatus new_status);
    static void initialize_neosu_server_session();
    static void check_and_notify_nonsubmittable();

    // cached uuid
    [[nodiscard]] static const std::string &get_disk_uuid();

    // cached install id (currently unimplemented, just returns disk uuid)
    [[nodiscard]] static inline const std::string &get_install_id() { return get_disk_uuid(); }

    // Room ID can be 0 on private servers! So we check if the room has players instead.
    [[nodiscard]] static bool is_in_a_multi_room();
    [[nodiscard]] static inline bool is_playing_a_multi_map() { return match_started; }
    [[nodiscard]] static bool can_submit_scores();

    [[nodiscard]] static inline OnlineStatus get_online_status() { return online_status; }
    [[nodiscard]] static inline bool is_online() {
        const i32 uid = user_id.load(std::memory_order_acquire);
        return (uid > 0) || (uid < -10000);
    }
    [[nodiscard]] static inline bool is_logging_in() { return online_status == OnlineStatus::LOGIN_IN_PROGRESS; }

    [[nodiscard]] static inline i32 get_uid() { return user_id.load(std::memory_order_acquire); }
    static void set_uid(i32 uid);

    [[nodiscard]] static const std::string &get_username();

    static void poll_login();
    static void disconnect(bool shutdown = false);
    static void reconnect();

   private:
    // internal helpers
    static void update_channel(const std::string &name, const std::string &topic, i32 nb_members, bool join);

    [[nodiscard]] static std::string get_disk_uuid_win32();
    [[nodiscard]] static std::string get_disk_uuid_blkid();
    [[nodiscard]] static std::string get_disk_uuid_wasm();

    static bool print_new_channels;

    // use get_username() to avoid having to check for online status
    static std::string username;

    // cached on first get
    static std::string disk_uuid;
    // static std::string install_id; // TODO?

    static std::atomic<i32> user_id;
    static OnlineStatus online_status;
    // HACK: can't cancel async login (so log out immediately after logging in, if ever)
    static bool async_logout_pending;
    static bool was_in_a_multi_room;
    static bool nonsubmittable_notification_clicked;
};
