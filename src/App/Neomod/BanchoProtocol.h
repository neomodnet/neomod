#pragma once
// Copyright (c) 2023, kiwec, All rights reserved.
#include "BanchoPacket.h"
#include "ModFlags.h"

#include <string>

enum class Action : uint8_t {
    IDLE = 0,
    AFK = 1,
    PLAYING = 2,
    EDITING = 3,
    MODDING = 4,
    MULTIPLAYER = 5,
    WATCHING = 6,
    UNKNOWN = 7,
    TESTING = 8,
    SUBMITTING = 9,
    PAUSED = 10,
    TESTING2 = 11,  // Was LOBBY but shows as "Testing" in-game
    MULTIPLAYING = 12,
    OSU_DIRECT = 13,
    MAX
};

enum class Privileges : uint8_t {
    PLAYER = 1,
    NOMINATOR = 2,
    SUPPORTER = 4,
    OWNER = 8,
    DEVELOPER = 16,
    TOURNAMENT_STAFF = 32,

    // Made-up flag for convenience
    MODERATOR = NOMINATOR | OWNER | DEVELOPER | TOURNAMENT_STAFF,
};
MAKE_FLAG_ENUM(Privileges)

enum class WinCondition : uint8_t {
    SCOREV1 = 0,
    ACCURACY = 1,
    CURRENT_COMBO = 2,
    SCOREV2 = 3,

    // neomod-specific, shouldn't be networked
    MISSES = 4,
    PP = 5,
    MAX_COMBO = 6,
};

enum class GameMode : uint8_t {
    STANDARD = 0,
    TAIKO = 1,
    CATCH = 2,
    MANIA = 3,
};

// NOTE: u8 in case packet headers get shortened, even though packet IDs are currently u16
enum IncomingPackets : uint8_t {
    INP_USER_ID = 5,
    INP_RECV_MESSAGE = 7,
    INP_PONG = 8,
    INP_USER_STATS = 11,
    INP_USER_LOGOUT = 12,
    INP_SPECTATOR_JOINED = 13,
    INP_SPECTATOR_LEFT = 14,
    INP_SPECTATE_FRAMES = 15,
    INP_VERSION_UPDATE = 19,
    INP_SPECTATOR_CANT_SPECTATE = 22,
    INP_GET_ATTENTION = 23,
    INP_NOTIFICATION = 24,
    INP_ROOM_UPDATED = 26,
    INP_ROOM_CREATED = 27,
    INP_ROOM_CLOSED = 28,
    INP_ROOM_JOIN_SUCCESS = 36,
    INP_ROOM_JOIN_FAIL = 37,
    INP_FELLOW_SPECTATOR_JOINED = 42,
    INP_FELLOW_SPECTATOR_LEFT = 43,
    INP_MATCH_STARTED = 46,
    INP_MATCH_SCORE_UPDATED = 48,
    INP_HOST_CHANGED = 50,
    INP_MATCH_ALL_PLAYERS_LOADED = 53,
    INP_MATCH_PLAYER_FAILED = 57,
    INP_MATCH_FINISHED = 58,
    INP_MATCH_SKIP = 61,
    INP_CHANNEL_JOIN_SUCCESS = 64,
    INP_CHANNEL_INFO = 65,
    INP_LEFT_CHANNEL = 66,
    INP_CHANNEL_AUTO_JOIN = 67,
    INP_PRIVILEGES = 71,
    INP_FRIENDS_LIST = 72,
    INP_PROTOCOL_VERSION = 75,
    INP_MAIN_MENU_ICON = 76,
    INP_MATCH_PLAYER_SKIPPED = 81,
    INP_USER_PRESENCE = 83,
    INP_RESTART = 86,
    INP_ROOM_INVITE = 88,
    INP_CHANNEL_INFO_END = 89,
    INP_ROOM_PASSWORD_CHANGED = 91,
    INP_SILENCE_END = 92,
    INP_USER_SILENCED = 94,
    INP_USER_PRESENCE_SINGLE = 95,
    INP_USER_PRESENCE_BUNDLE = 96,
    INP_USER_DM_BLOCKED = 100,
    INP_TARGET_IS_SILENCED = 101,
    INP_VERSION_UPDATE_FORCED = 102,
    INP_SWITCH_SERVER = 103,
    INP_ACCOUNT_RESTRICTED = 104,
    INP_MATCH_ABORT = 106,

    // neomod-specific packets (128 is arbitrary number to start at)
    INP_PROTECT_VARIABLES = 128,
    INP_UNPROTECT_VARIABLES = 129,
    INP_FORCE_VALUES = 130,
    INP_RESET_VALUES = 131,
    INP_REQUEST_MAP = 132,
};

// NOTE: u8 in case packet headers get shortened, even though packet IDs are currently u16
enum OutgoingPackets : uint8_t {
    OUTP_CHANGE_ACTION = 0,
    OUTP_SEND_PUBLIC_MESSAGE = 1,
    OUTP_LOGOUT = 2,
    OUTP_PING = 4,
    OUTP_START_SPECTATING = 16,
    OUTP_STOP_SPECTATING = 17,
    OUTP_SPECTATE_FRAMES = 18,
    OUTP_ERROR_REPORT = 20,
    OUTP_CANT_SPECTATE = 21,
    OUTP_SEND_PRIVATE_MESSAGE = 25,
    OUTP_EXIT_ROOM_LIST = 29,
    OUTP_JOIN_ROOM_LIST = 30,
    OUTP_CREATE_ROOM = 31,
    OUTP_JOIN_ROOM = 32,
    OUTP_EXIT_ROOM = 33,
    OUTP_CHANGE_SLOT = 38,
    OUTP_MATCH_READY = 39,
    OUTP_MATCH_LOCK = 40,
    OUTP_MATCH_CHANGE_SETTINGS = 41,
    OUTP_START_MATCH = 44,
    OUTP_UPDATE_MATCH_SCORE = 47,
    OUTP_FINISH_MATCH = 49,
    OUTP_MATCH_CHANGE_MODS = 51,
    OUTP_MATCH_LOAD_COMPLETE = 52,
    OUTP_MATCH_NO_BEATMAP = 54,
    OUTP_MATCH_NOT_READY = 55,
    OUTP_MATCH_FAILED = 56,
    OUTP_MATCH_HAS_BEATMAP = 59,
    OUTP_MATCH_SKIP_REQUEST = 60,
    OUTP_CHANNEL_JOIN = 63,
    OUTP_BEATMAP_INFO_REQUEST = 68,
    OUTP_TRANSFER_HOST = 70,
    OUTP_FRIEND_ADD = 73,
    OUTP_FRIEND_REMOVE = 74,
    OUTP_MATCH_CHANGE_TEAM = 77,
    OUTP_CHANNEL_PART = 78,
    OUTP_RECEIVE_UPDATES = 79,
    OUTP_SET_AWAY_MESSAGE = 82,
    OUTP_IRC_ONLY = 84,
    OUTP_USER_STATS_REQUEST = 85,
    OUTP_MATCH_INVITE = 88,
    OUTP_CHANGE_ROOM_PASSWORD = 90,
    OUTP_TOURNAMENT_MATCH_INFO_REQUEST = 93,
    OUTP_USER_PRESENCE_REQUEST = 97,
    OUTP_USER_PRESENCE_REQUEST_ALL = 98,
    OUTP_TOGGLE_BLOCK_NON_FRIEND_DMS = 99,
    OUTP_TOURNAMENT_JOIN_MATCH_CHANNEL = 108,
    OUTP_TOURNAMENT_EXIT_MATCH_CHANNEL = 109,
};

struct Slot {
    // From ROOM_CREATED, ROOM_UPDATED
    u8 status = 0;  // bitfield of [quit, complete, playing, no_map, ready, not_ready, locked, open]
    u8 team = 0;
    i32 player_id = 0;
    LegacyFlags mods{};

    // From MATCH_PLAYER_SKIPPED
    bool skipped = false;

    // From MATCH_PLAYER_FAILED
    bool died = false;

    // From MATCH_SCORE_UPDATED
    i32 last_update_tms = 0;
    u16 num300 = 0;
    u16 num100 = 0;
    u16 num50 = 0;
    u16 num_geki = 0;
    u16 num_katu = 0;
    u16 num_miss = 0;
    i32 total_score = 0;
    u16 current_combo = 0;
    u16 max_combo = 0;
    u8 is_perfect = 0;
    u8 current_hp = 0;
    u8 tag = 0;
    double sv2_combo = 0.0;
    double sv2_bonus = 0.0;

    // locked
    [[nodiscard]] inline bool is_locked() const { return (this->status & 0b00000010); }

    // ready
    [[nodiscard]] inline bool is_ready() const { return (this->status & 0b00001000); }

    // no_map
    [[nodiscard]] inline bool no_map() const { return (this->status & 0b00010000); }

    // playing
    [[nodiscard]] inline bool is_player_playing() const { return (this->status & 0b00100000); }
    [[nodiscard]] inline bool has_finished_playing() const { return (this->status & 0b01000000); }

    // no_map
    [[nodiscard]] inline bool is_missing_beatmap() const { return (this->status & 0b00010000); }

    // quit
    [[nodiscard]] inline bool has_quit() const { return (this->status & 0b10000000); }

    // not_ready | ready | no_map | playing | complete
    [[nodiscard]] inline bool has_player() const { return (this->status & 0b01111100); }
};

class Room {
   public:
    Room() = default;  // default-initialized room means we're not in multiplayer at the moment
    Room(Packet &packet);

    MD5Hash map_md5;

    u32 seed = 0;
    i32 map_id = 0;
    i32 host_id = 0;
    u16 id = 0;

    u8 in_progress = 0;
    u8 match_type = 0;
    LegacyFlags mods{};
    bool has_password = false;

    std::string name;
    std::string password;
    std::string map_name;

    std::array<Slot, 16> slots{};

    u8 mode = 0;
    WinCondition win_condition{};
    u8 team_type = 0;
    u8 freemods = 0;

    u8 nb_players = 0;
    u8 nb_open_slots = 0;

    [[nodiscard]] inline bool nb_ready() const {
        u8 nb = 0;
        for(const auto &slot : this->slots) {
            if(slot.has_player() && slot.is_ready()) {
                nb++;
            }
        }
        return nb;
    }

    [[nodiscard]] inline bool all_players_ready() const {
        for(const auto &slot : this->slots) {
            if(slot.has_player() && !slot.is_ready()) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool is_host() const;
    void pack(Packet &packet);
};

#pragma pack(push, 1)
struct ScoreFrame {
    i32 time{0};
    u8 slot_id{0};
    u16 num300{0};
    u16 num100{0};
    u16 num50{0};
    u16 num_geki{0};
    u16 num_katu{0};
    u16 num_miss{0};
    i32 total_score{0};
    u16 max_combo{0};
    u16 current_combo{0};
    u8 is_perfect{0};
    u8 current_hp{0};
    u8 tag{0};
    u8 is_scorev2{0};

    static ScoreFrame get();
};
#pragma pack(pop)

#pragma pack(push, 1)
struct LiveReplayFrame {
    u8 key_flags;
    u8 padding;  // was used in very old versions of the game
    f32 mouse_x;
    f32 mouse_y;
    i32 time;
};
#pragma pack(pop)

enum class LiveReplayAction : uint8_t {
    NONE = 0,
    NEW_SONG = 1,
    SKIP = 2,
    COMPLETION = 3,
    FAIL = 4,
    PAUSE = 5,
    UNPAUSE = 6,
    SONG_SELECT = 7,
    WATCHING_OTHER = 8,
    MAX_ACTION
};

struct LiveReplayBundle {
    LiveReplayAction action{LiveReplayAction::NONE};
    u16 nb_frames{0};
    LiveReplayFrame *frames{nullptr};
    ScoreFrame score;
    u16 sequence{0};
};
