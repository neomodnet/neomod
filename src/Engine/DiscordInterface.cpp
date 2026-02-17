// Copyright (c) 2018, PG, All rights reserved.
#include "DiscordInterface.h"

#ifndef MCENGINE_FEATURE_DISCORD
namespace DiscRPC {
void init() {}
void deinit() {}
void tick() {}
void destroy() {}
void clear_activity() {}
void set_activity(struct DiscordActivity* /*activity*/) {}
}  // namespace DiscRPC

#else

#include <csignal>

#include "ConVar.h"
#include "Engine.h"
#include "Logging.h"
#include "Timing.h"
#include "dynutils.h"

#define DISCORD_CLIENT_ID 1288141291686989846

namespace DiscRPC {
namespace  // static
{

static bool initialized{false};

#ifdef SIGPIPE
static bool sigpipe_handler_installed{false};

static volatile sig_atomic_t s_in_discord_call{0};
static volatile sig_atomic_t s_sigpipe{0};

// only attribute SIGPIPE to discord if we're currently inside an SDK call
static void sigpipe_handler(int val) {
    if(s_in_discord_call) {
        s_sigpipe = 1;
    } else {
        debugLog("got unknown sigpipe {}", val);
    }
}

#define DISCCALL(...)          \
    do {                       \
        s_in_discord_call = 1; \
        __VA_ARGS__;           \
        s_in_discord_call = 0; \
    } while(false);
#else
#define DISCCALL(...) __VA_ARGS__;
#endif

static struct Application {
    struct IDiscordCore *core;
    struct IDiscordUserManager *users;
    struct IDiscordAchievementManager *achievements;
    struct IDiscordActivityManager *activities;
    struct IDiscordRelationshipManager *relationships;
    struct IDiscordApplicationManager *application;
    struct IDiscordLobbyManager *lobbies;
    DiscordUserId user_id;
} dapp{};

static struct IDiscordActivityEvents activities_events{};
static struct IDiscordRelationshipEvents relationships_events{};
static struct IDiscordUserEvents users_events{};

#if !(defined(MCENGINE_PLATFORM_WINDOWS) && defined(MC_ARCH32))  // doesn't work on winx32
static void on_discord_log(void * /*cdata*/, enum EDiscordLogLevel level, const char *message) {
    //(void)cdata;
    if(level == DiscordLogLevel_Error) {
        logRaw("[Discord] ERROR: {:s}", message);
    } else {
        logRaw("[Discord] {:s}", message);
    }
}
#endif

static dynutils::lib_obj *discord_handle{nullptr};
static decltype(DiscordCreate) *pDiscordCreate{nullptr};
static bool broken{false};  // if we couldn't load discord_handle or pDiscordCreate after trying once

static constexpr int MAX_RECONNECT_ATTEMPTS = 3;
static constexpr u64 RECONNECT_INTERVAL_NS = 5'000'000'000ULL;  // 5s
static int reconnect_attempts{MAX_RECONNECT_ATTEMPTS};  // exhausted by default so we don't retry on initial failure
static u64 last_reconnect_attempt{0};

// establish (or re-establish) a connection to discord
static bool connect() {
    dapp = {};

    struct DiscordCreateParams params{};
    params.client_id = DISCORD_CLIENT_ID;
    params.flags = DiscordCreateFlags_NoRequireDiscord;
    params.event_data = &dapp;
    params.activity_events = &activities_events;
    params.relationship_events = &relationships_events;
    params.user_events = &users_events;

    int res = pDiscordCreate(DISCORD_VERSION, &params, &dapp.core);
    if(res != DiscordResult_Ok) {
        logRaw("[Discord] failed to connect (error {:d})", res);
        return false;
    }

#if !(defined(MCENGINE_PLATFORM_WINDOWS) && defined(MC_ARCH32))
    DISCCALL(dapp.core->set_log_hook(dapp.core, DiscordLogLevel_Warn, nullptr, on_discord_log));
#endif
    DISCCALL(dapp.activities = dapp.core->get_activity_manager(dapp.core));
    DISCCALL(dapp.activities->register_command(dapp.activities, PACKAGE_NAME "://run"));

    initialized = true;
    return true;
}

}  // namespace

void init() {
    if(initialized || broken) return;

    if(!discord_handle && !(discord_handle = dynutils::load_lib("discord_game_sdk"))) {
        broken = true;
        logRaw("[Discord] Failed to load Discord SDK! (error {:s})", dynutils::get_error());
        return;
    }

    if(!pDiscordCreate &&
       !(pDiscordCreate = dynutils::load_func<decltype(DiscordCreate)>(discord_handle, "DiscordCreate"))) {
        broken = true;
        logRaw("[Discord] Failed to load DiscordCreate from discord_game_sdk.dll! (error {:s})", dynutils::get_error());
        return;
    }

#ifdef SIGPIPE
    if(!sigpipe_handler_installed) {
        sigpipe_handler_installed = true;
        // the discord SDK writes to a unix socket that can break if discord restarts;
        // catch SIGPIPE so we can detect the disconnect and reconnect
        struct sigaction sa{};
        sa.sa_handler = sigpipe_handler;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGPIPE, &sa, nullptr);
    }
#endif

    connect();

    // there's an issue where if the game starts with discord closed, then the SDK fails to initialize, and there's
    // no way to try reinitializing it (without restarting the game)
    // so allow "turning it off and on again" to try reinitializing
    // (DiscRPC::init() does nothing if already initialized)
    if(!cv::rich_presence.hasSingleArgCallback()) {
        cv::rich_presence.setCallback(
            [](float newValue) -> void { return !!static_cast<int>(newValue) ? DiscRPC::init() : DiscRPC::deinit(); });
    }
}

void tick() {
#ifdef SIGPIPE
    if(s_sigpipe) {
        s_sigpipe = 0;
        logRaw("[Discord] connection lost (SIGPIPE), will try to reconnect");
        // can't safely destroy the old core when discord is gone, just abandon it
        dapp = {};
        initialized = false;
        reconnect_attempts = 0;
        last_reconnect_attempt = Timing::getTicksNS();
    }
#endif

    if(!initialized) {
        if(!pDiscordCreate || reconnect_attempts >= MAX_RECONNECT_ATTEMPTS) return;

        u64 now = Timing::getTicksNS();
        if(now - last_reconnect_attempt < RECONNECT_INTERVAL_NS) return;
        last_reconnect_attempt = now;
        reconnect_attempts++;

        if(connect())
            logRaw("[Discord] reconnected successfully");
        else
            logRaw("[Discord] reconnect attempt {:d}/{:d} failed", reconnect_attempts, MAX_RECONNECT_ATTEMPTS);
        return;
    }

    DISCCALL(dapp.core->run_callbacks(dapp.core));
}

void deinit() {
    if(initialized) {
        DISCCALL(dapp.core->destroy(dapp.core));
#ifdef SIGPIPE
        // ignore
        s_sigpipe = 0;
#endif
        reconnect_attempts = MAX_RECONNECT_ATTEMPTS;  // don't attempt to automatically reconnect
        dapp = {};
        initialized = false;
    }
}

void destroy() {
    deinit();
    if(discord_handle) {
        dynutils::unload_lib(discord_handle);
    }
}

void clear_activity() {
    if(!initialized) return;

    DISCCALL(dapp.activities->clear_activity(dapp.activities, nullptr, nullptr));
}

void set_activity(struct DiscordActivity *activity) {
    if(!initialized) return;

    if(!cv::rich_presence.getBool()) return;

    // activity->type: int
    //     DiscordActivityType_Playing,
    //     DiscordActivityType_Streaming,
    //     DiscordActivityType_Listening,
    //     DiscordActivityType_Watching,

    // activity->details: char[128]; // what the player is doing
    // activity->state:   char[128]; // party status

    // Only set "end" if in multiplayer lobby, else it doesn't make sense since user can pause
    // Keep "start" across retries
    // activity->timestamps->start: int64_t
    // activity->timestamps->end:   int64_t

    // activity->party: DiscordActivityParty
    // activity->secrets: DiscordActivitySecrets
    // currently unused. should be lobby id, etc

    activity->application_id = DISCORD_CLIENT_ID;
    strcpy(&activity->name[0], PACKAGE_NAME);
    strcpy(&activity->assets.large_image[0], PACKAGE_NAME "_icon");
    activity->assets.large_text[0] = '\0';
    strcpy(&activity->assets.small_image[0], "None");
    activity->assets.small_text[0] = '\0';

    DISCCALL(dapp.activities->update_activity(dapp.activities, activity, nullptr, nullptr));
}

// void (DISCORD_API *send_request_reply)(struct IDiscordActivityManager* manager, DiscordUserId user_id, enum
//     EDiscordActivityJoinRequestReply reply, void* callback_data, void (DISCORD_API *callback)(void* callback_data,
//     enum EDiscordResult result));

// void (DISCORD_API *send_invite)(struct IDiscordActivityManager* manager, DiscordUserId user_id, enum
//     EDiscordActivityActionType type, const char* content, void* callback_data, void (DISCORD_API *callback)(void*
//     callback_data, enum EDiscordResult result));

// void (DISCORD_API *accept_invite)(struct IDiscordActivityManager* manager, DiscordUserId user_id, void*
//     callback_data, void (DISCORD_API *callback)(void* callback_data, enum EDiscordResult result));
}  // namespace DiscRPC

#endif
