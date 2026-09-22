#pragma once
// Copyright (c) 2023, kiwec, All rights reserved.
#include "BanchoPacket.h"
#include "ModFlags.h"
#include "EnumString.h"

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

#define INP_ENUM_VALS(X)                                                \
    X(INP_USER_ID, = 5)                                                 \
    X(INP_RECV_MESSAGE, = 7)                                            \
    X(INP_PONG, = 8)                                                    \
    X(INP_USER_STATS, = 11)                                             \
    X(INP_USER_LOGOUT, = 12)                                            \
    X(INP_SPECTATOR_JOINED, = 13)                                       \
    X(INP_SPECTATOR_LEFT, = 14)                                         \
    X(INP_SPECTATE_FRAMES, = 15)                                        \
    X(INP_VERSION_UPDATE, = 19)                                         \
    X(INP_SPECTATOR_CANT_SPECTATE, = 22)                                \
    X(INP_GET_ATTENTION, = 23)                                          \
    X(INP_NOTIFICATION, = 24)                                           \
    X(INP_ROOM_UPDATED, = 26)                                           \
    X(INP_ROOM_CREATED, = 27)                                           \
    X(INP_ROOM_CLOSED, = 28)                                            \
    X(INP_ROOM_JOIN_SUCCESS, = 36)                                      \
    X(INP_ROOM_JOIN_FAIL, = 37)                                         \
    X(INP_FELLOW_SPECTATOR_JOINED, = 42)                                \
    X(INP_FELLOW_SPECTATOR_LEFT, = 43)                                  \
    X(INP_MATCH_STARTED, = 46)                                          \
    X(INP_MATCH_SCORE_UPDATED, = 48)                                    \
    X(INP_HOST_CHANGED, = 50)                                           \
    X(INP_MATCH_ALL_PLAYERS_LOADED, = 53)                               \
    X(INP_MATCH_PLAYER_FAILED, = 57)                                    \
    X(INP_MATCH_FINISHED, = 58)                                         \
    X(INP_MATCH_SKIP, = 61)                                             \
    X(INP_CHANNEL_JOIN_SUCCESS, = 64)                                   \
    X(INP_CHANNEL_INFO, = 65)                                           \
    X(INP_LEFT_CHANNEL, = 66)                                           \
    X(INP_CHANNEL_AUTO_JOIN, = 67)                                      \
    X(INP_PRIVILEGES, = 71)                                             \
    X(INP_FRIENDS_LIST, = 72)                                           \
    X(INP_PROTOCOL_VERSION, = 75)                                       \
    X(INP_MAIN_MENU_ICON, = 76)                                         \
    X(INP_MATCH_PLAYER_SKIPPED, = 81)                                   \
    X(INP_USER_PRESENCE, = 83)                                          \
    X(INP_RESTART, = 86)                                                \
    X(INP_ROOM_INVITE, = 88)                                            \
    X(INP_CHANNEL_INFO_END, = 89)                                       \
    X(INP_ROOM_PASSWORD_CHANGED, = 91)                                  \
    X(INP_SILENCE_END, = 92)                                            \
    X(INP_USER_SILENCED, = 94)                                          \
    X(INP_USER_PRESENCE_SINGLE, = 95)                                   \
    X(INP_USER_PRESENCE_BUNDLE, = 96)                                   \
    X(INP_USER_DM_BLOCKED, = 100)                                       \
    X(INP_TARGET_IS_SILENCED, = 101)                                    \
    X(INP_VERSION_UPDATE_FORCED, = 102)                                 \
    X(INP_SWITCH_SERVER, = 103)                                         \
    X(INP_ACCOUNT_RESTRICTED, = 104)                                    \
    X(INP_MATCH_ABORT, = 106)                                           \
    X(INP_SWITCH_TOURNAMENT_SERVER, = 107)                              \
    /* neomod-specific packets (128 is arbitrary number to start at) */ \
    X(INP_PROTECT_VARIABLES, = 128)                                     \
    X(INP_UNPROTECT_VARIABLES, = 129)                                   \
    X(INP_FORCE_VALUES, = 130)                                          \
    X(INP_RESET_VALUES, = 131)                                          \
    X(INP_REQUEST_MAP, = 132)

// NOTE: u8 in case packet headers get shortened, even though packet IDs are currently u16
enum MC_DEFINE_ENUM(IncomingPackets, uint8_t, INP_ENUM_VALS, IncomingPackets_to_string);
#undef INP_ENUM_VALS

#define OUTP_ENUM_VALS(X)                        \
    X(OUTP_CHANGE_ACTION, = 0)                   \
    X(OUTP_SEND_PUBLIC_MESSAGE, = 1)             \
    X(OUTP_LOGOUT, = 2)                          \
    X(OUTP_PING, = 4)                            \
    X(OUTP_START_SPECTATING, = 16)               \
    X(OUTP_STOP_SPECTATING, = 17)                \
    X(OUTP_SPECTATE_FRAMES, = 18)                \
    X(OUTP_ERROR_REPORT, = 20)                   \
    X(OUTP_CANT_SPECTATE, = 21)                  \
    X(OUTP_SEND_PRIVATE_MESSAGE, = 25)           \
    X(OUTP_EXIT_ROOM_LIST, = 29)                 \
    X(OUTP_JOIN_ROOM_LIST, = 30)                 \
    X(OUTP_CREATE_ROOM, = 31)                    \
    X(OUTP_JOIN_ROOM, = 32)                      \
    X(OUTP_EXIT_ROOM, = 33)                      \
    X(OUTP_CHANGE_SLOT, = 38)                    \
    X(OUTP_MATCH_READY, = 39)                    \
    X(OUTP_MATCH_LOCK, = 40)                     \
    X(OUTP_MATCH_CHANGE_SETTINGS, = 41)          \
    X(OUTP_START_MATCH, = 44)                    \
    X(OUTP_UPDATE_MATCH_SCORE, = 47)             \
    X(OUTP_FINISH_MATCH, = 49)                   \
    X(OUTP_MATCH_CHANGE_MODS, = 51)              \
    X(OUTP_MATCH_LOAD_COMPLETE, = 52)            \
    X(OUTP_MATCH_NO_BEATMAP, = 54)               \
    X(OUTP_MATCH_NOT_READY, = 55)                \
    X(OUTP_MATCH_FAILED, = 56)                   \
    X(OUTP_MATCH_HAS_BEATMAP, = 59)              \
    X(OUTP_MATCH_SKIP_REQUEST, = 60)             \
    X(OUTP_CHANNEL_JOIN, = 63)                   \
    X(OUTP_BEATMAP_INFO_REQUEST, = 68)           \
    X(OUTP_TRANSFER_HOST, = 70)                  \
    X(OUTP_FRIEND_ADD, = 73)                     \
    X(OUTP_FRIEND_REMOVE, = 74)                  \
    X(OUTP_MATCH_CHANGE_TEAM, = 77)              \
    X(OUTP_CHANNEL_PART, = 78)                   \
    X(OUTP_RECEIVE_UPDATES, = 79)                \
    X(OUTP_SET_AWAY_MESSAGE, = 82)               \
    X(OUTP_IRC_ONLY, = 84)                       \
    X(OUTP_USER_STATS_REQUEST, = 85)             \
    X(OUTP_MATCH_INVITE, = 88)                   \
    X(OUTP_CHANGE_ROOM_PASSWORD, = 90)           \
    X(OUTP_TOURNAMENT_MATCH_INFO_REQUEST, = 93)  \
    X(OUTP_USER_PRESENCE_REQUEST, = 97)          \
    X(OUTP_USER_PRESENCE_REQUEST_ALL, = 98)      \
    X(OUTP_TOGGLE_BLOCK_NON_FRIEND_DMS, = 99)    \
    X(OUTP_TOURNAMENT_JOIN_MATCH_CHANNEL, = 108) \
    X(OUTP_TOURNAMENT_EXIT_MATCH_CHANNEL, = 109)

// NOTE: u8 in case packet headers get shortened, even though packet IDs are currently u16
enum MC_DEFINE_ENUM(OutgoingPackets, uint8_t, OUTP_ENUM_VALS, OutgoingPackets_to_string);
#undef OUTP_ENUM_VALS

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

    // quit
    [[nodiscard]] inline bool has_quit() const { return (this->status & 0b10000000); }

    // not_ready | ready | no_map | playing | complete
    [[nodiscard]] inline bool has_player() const { return (this->status & 0b01111100); }
};

class Room {
   public:
    Room() = default;  // default-initialized room means we're not in multiplayer at the moment
    Room(PacketReader &packet);

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
