// Copyright (c) 2026, kiwec, All rights reserved.
#include "Migrations.h"

#include <optional>
#include <string>

#include "fmt/format.h"

#include "Accounts.h"
#include "BanchoNetworking.h"
#include "BaseEnvironment.h"
#include "ConVarHandler.h"
#include "Database.h"
#include "Environment.h"
#include "File.h"
#include "KeyBindings.h"
#include "Logging.h"
#include "OptionsOverlay.h"
#include "OsuConVars.h"
#include "OsuKeyBinds.h"
#include "Parsing.h"
#include "Paths.h"
#include "UI.h"
#include "crypto.h"

namespace Migrations {
namespace {
struct PreviousVersion {
    double version{-1.};
    u64 buildstamp{0};
    bool gotLegitBuildstamp{true};
};

std::optional<PreviousVersion> read_version_file() {
    if(!Environment::fileExists(Mc::Paths::data() + "/version.txt")) {
        // no version.txt exists, we are not updating
        return std::nullopt;
    }

    PreviousVersion previous;
    File versionFile(Mc::Paths::data() + "/version.txt");
    std::string linebuf{};
    // get version number
    if(!(versionFile.canRead() && ((linebuf = versionFile.readLine()) != "") &&
         ((previous.version = Parsing::strto<f64>(linebuf)) > 0.))) {
        previous.version = -1.;
        return previous;
    }

    // get build timestamp
    if(versionFile.canRead() && ((linebuf = versionFile.readLine()) != "") &&
       ((previous.buildstamp = Parsing::strto<u64>(linebuf)) > 0)) {
        // ignore bogus build timestamps
        if(previous.buildstamp > 4000000000 || previous.buildstamp < 2000000000) {
            previous.gotLegitBuildstamp = false;
            previous.buildstamp = cv::build_timestamp.getVal<u64>();
        }
    }
    previous.gotLegitBuildstamp &= previous.buildstamp > 0;

    // debugLog("versionFile version: {} our version: {}{}", previous.version, cv::version.getFloat(),
    //           previous.buildstamp > 0.0f ? fmt::format(" build timestamp: {}", previous.buildstamp) : "");
    return previous;
}
}  // namespace

bool is_updating_from_old_version() {
    const auto previous = read_version_file();
    if(!previous) return false;

    // assume empty/invalid version.txt means we updated
    if(previous->version <= 0.) return true;

    if(Env::cfg(BUILD::DEBUG)) return false;
    return previous->version < cv::version.getDouble() || previous->buildstamp < cv::build_timestamp.getVal<u64>();
}

void run() {
    const auto previous = read_version_file();
    if(!previous || previous->version <= 0.) return;
    const double version = previous->version;
    const u64 buildstamp = previous->buildstamp;

    const bool makeBackup = !Env::cfg(OS::WASM) &&                 // too hard to even access on wasm
                            (version < cv::version.getDouble() ||  // always backup release version bumps
                             // don't spam backups for debug builds with build timestamp updates
                             (!Env::cfg(BUILD::DEBUG) && buildstamp < cv::build_timestamp.getVal<u64>()));

    bool shouldSave = false;
    if(version < 35.06) {
        // SoundEngine choking issues have been fixed, option has been removed from settings menu
        // We leave the cvar available as it could still be useful for some players
        cv::restart_sound_engine_before_playing.setValue(false);

        // Match osu!stable value
        if(cv::relax_offset.getFloat() == 0.f) {
            cv::relax_offset.setValue(-12.f);
        }

        shouldSave = true;
    }
    if(version < 39.01) {
        if(cv::fps_unlimited.getBool()) {
            cv::fps_max.setValue(0);
            shouldSave = true;
        }
    }
    if(version < 40.00) {
        for(auto *bind : OsuKeyBinds::getAll()) {
            if(bind->isDefault()) continue;
            bind->set(KeyBindings::old_keycode_to_sdl_scancode(bind->get()));
        }
        shouldSave = true;
    }
    if(version < 40.06) {
        cv::letterboxed_resolution.setValue(cv::resolution.getString());
        shouldSave = true;
    }
    if(version < 43.02) {
        if(cv::mp_server.getString() == "neosu.net"sv) {
            cv::mp_server.setValue(cv::mp_server.getDefaultString());
            shouldSave = true;
        }
        if(Database::migrate_neosu_to_neomod()) {
            debugLog("Migrated old neosu databases to neomod.");
        }
    }
    if(version < 43.04 || buildstamp <= 2602190926) {
        cv::prefer_websockets.setValue(true);
        shouldSave = true;
    }
    if(version < 45.01 || buildstamp <= 2611240000) {
        // accounts.cfg
        std::optional<std::string> password_md5 = cvars().takeKeptValue("mp_password_md5");
        if(const auto password = cvars().takeKeptValue("mp_password"); password && !password->empty()) {
            password_md5 = std::string{crypto::hash::md5(*password).to_chars().string()};
        }
        const auto oauth_token = cvars().takeKeptValue("mp_oauth_token");
        const auto supporter_token = cvars().takeKeptValue("supporter_token");

        const bool has_password = password_md5 && !password_md5->empty();
        const bool has_oauth_token = oauth_token && !oauth_token->empty();
        if(has_password || has_oauth_token) {
            Account &account = Accounts::get(cv::mp_server.getString());
            if(has_password) {
                account.username = cv::name.getString();
                account.password_md5 = *password_md5;
            }
            if(has_oauth_token) account.oauth_token = *oauth_token;
        }
        if(supporter_token && !supporter_token->empty()) {
            Accounts::get(NEOMOD_DOMAIN).supporter_token = *supporter_token;
        }
        if(password_md5 || oauth_token || supporter_token) {
            Accounts::save();
            shouldSave = true;
        }

        // confine cursor setting
        const auto never = cvars().takeKeptValue("confine_cursor_never");
        const auto windowed = cvars().takeKeptValue("confine_cursor_windowed");
        const auto fullscreen = cvars().takeKeptValue("confine_cursor_fullscreen");
        if(never || windowed || fullscreen) {
            const auto isOn = [](const std::optional<std::string> &text, bool def) {
                return text ? Parsing::strto<bool>(*text) : def;
            };
            const bool inWindow = isOn(windowed, false);
            const bool inFullscreen = isOn(fullscreen, true);
            if(isOn(never, false)) {
                cv::confine_cursor.setValue("never");
            } else if(inWindow) {
                cv::confine_cursor.setValue(inFullscreen ? "always" : "windowed");
            } else if(!inFullscreen) {
                cv::confine_cursor.setValue("gameplay");
            }
            shouldSave = true;
        }
    }

    if(makeBackup && shouldSave) {
        // back up synchronously
        const std::string cfg_path = Mc::Paths::cfg() + "/osu.cfg";
        const std::string backup_name =
            fmt::format("{}.{:.2f}{:s}.bak", cfg_path, version,
                        previous->gotLegitBuildstamp ? fmt::format("-{:d}", buildstamp) : "");
        debugLog("Backing up config {} -> {}", cfg_path, backup_name);
        if(!File::copy(cfg_path, backup_name)) {
            debugLog("WARNING: failed to back up {} -> {:s}!", cfg_path, backup_name);
        }
    }
    if(shouldSave) {
        ui->getOptionsOverlay()->save();
    }
}

}  // namespace Migrations
