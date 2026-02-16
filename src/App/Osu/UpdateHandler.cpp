// Copyright (c) 2016, PG, All rights reserved.
#include "UpdateHandler.h"

#include "Archival.h"
#include "BanchoNetworking.h"
#include "Bancho.h"
#include "OsuConVars.h"
#include "Parsing.h"
#include "crypto.h"
#include "Engine.h"
#include "File.h"
#include "NetworkHandler.h"
#include "SString.h"
#include "UI.h"
#include "OptionsOverlay.h"
#include "Logging.h"
#include "Environment.h"
#include "MakeDelegateWrapper.h"

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include <fstream>

using enum UpdateHandler::STATUS;

void UpdateHandler::updateCallback() { this->checkForUpdates(true); }

UpdateHandler::UpdateHandler() { cv::cmd::update.setCallback(SA::MakeDelegate<&UpdateHandler::updateCallback>(this)); }

void UpdateHandler::onBleedingEdgeChanged(float oldVal, float newVal) {
    if(this->getStatus() != STATUS_IDLE && this->getStatus() != STATUS_ERROR) {
        debugLog("Can't change release stream while an update is in progress!");
        cv::bleedingedge.setValue(oldVal, false);
    }

    const bool oldState = !!static_cast<int>(oldVal);
    const bool newState = !!static_cast<int>(newVal);
    if(oldState == newState) return;

    this->checkForUpdates(true);
}

void UpdateHandler::checkForUpdates(bool force_update) {
    if(this->getStatus() != STATUS_IDLE && this->getStatus() != STATUS_ERROR) {
        debugLog("We're already updating!");
        return;
    }

    std::string versionUrl = "https://" NEOSU_DOMAIN;
    if(cv::bleedingedge.getBool()) {
        versionUrl.append("/bleedingedge/" OS_NAME ".txt");
    } else {
        versionUrl.append("/update/" OS_NAME "/latest-version.txt");
    }

    debugLog("UpdateHandler: Checking for a newer version from {}", versionUrl);

    Mc::Net::RequestOptions options{
        .user_agent = BanchoState::user_agent,
        .timeout = 10,
        .connect_timeout = 5,
    };

    this->status = STATUS_CHECKING_FOR_UPDATE;
    networkHandler->httpRequestAsync(versionUrl, std::move(options),
                                     [this, force_update](const Mc::Net::Response &response) {
                                         this->onVersionCheckComplete(response.body, response.success, force_update);
                                     });
}

void UpdateHandler::onVersionCheckComplete(const std::string &response, bool success, bool force_update) {
    if(!success || response.empty()) {
        // Avoid setting STATUS_ERROR, since we don't want a big red button to show up for offline players
        // We DO want it to show up if the update check was requested manually.
        this->status = force_update ? STATUS_ERROR : STATUS_IDLE;
        debugLog("UpdateHandler ERROR: Failed to check for updates :/");
        return;
    }

    const std::vector<std::string_view> lines = SString::split_newlines(response);
    f32 latest_version = Parsing::strto<f32>(lines[0]);
    u64 latest_build_tms = 0;
    std::string online_update_hash;
    if(lines.size() > 1) latest_build_tms = Parsing::strto<u64>(lines[1]);
    if(lines.size() > 2) online_update_hash = lines[2];
    if(latest_version == 0.f && latest_build_tms == 0) {
        this->status = force_update ? STATUS_ERROR : STATUS_IDLE;
        debugLog("UpdateHandler ERROR: Failed to parse version number");
        return;
    }

    u64 current_build_tms = cv::build_timestamp.getVal<u64>();
    bool should_update =
        force_update || (cv::version.getFloat() < latest_version) || (current_build_tms < latest_build_tms);
    if(!should_update) {
        // We're already up to date
        this->status = STATUS_IDLE;
        debugLog("UpdateHandler: We're already up to date (current v{:.2f} ({:d}), latest v{:.2f} ({:d}))",
                 cv::version.getFloat(), current_build_tms, latest_version, latest_build_tms);
        return;
    }

    // XXX: Blocking file read
    if(!online_update_hash.empty() && Environment::fileExists(MCENGINE_DATA_DIR "update.zip")) {
        std::array<u8, 32> file_hash{};
        crypto::hash::sha256_f(MCENGINE_DATA_DIR "update.zip", file_hash.data());
        auto downloaded_update_hash = crypto::conv::encodehex(file_hash);
        if(downloaded_update_hash == online_update_hash) {
            debugLog("UpdateHandler: Update already downloaded (hash = {})", downloaded_update_hash);
            this->status = STATUS_DOWNLOAD_COMPLETE;
            return;
        }
    }

    std::string update_url;
    if(cv::bleedingedge.getBool()) {
        update_url = "https://" NEOSU_DOMAIN "/bleedingedge/" OS_NAME ".zip";
    } else {
        update_url = fmt::format("https://" NEOSU_DOMAIN "/update/" OS_NAME "/v{:.2f}.zip", latest_version);
    }
    update_url.append(fmt::format("?hash={:s}", online_update_hash));

    debugLog("UpdateHandler: Downloading latest update... (current v{:.2f} ({:d}), latest v{:.2f} ({:d}))",
             cv::version.getFloat(), current_build_tms, latest_version, latest_build_tms);
    debugLog("UpdateHandler: Downloading {:s}", update_url);

    Mc::Net::RequestOptions options{
        .user_agent = BanchoState::user_agent,
        .timeout = 300,  // 5 minutes for large downloads
        .connect_timeout = 10,
        .flags = Mc::Net::RequestOptions::FOLLOW_REDIRECTS,
    };

    this->status = STATUS_DOWNLOADING_UPDATE;
    networkHandler->httpRequestAsync(update_url, std::move(options),
                                     [this, online_update_hash](const Mc::Net::Response &response) {
                                         this->onDownloadComplete(response.body, response.success, online_update_hash);
                                     });
}

void UpdateHandler::onDownloadComplete(const std::string &data, bool success, std::string hash) {
    if(!success || data.size() < 2) {
        debugLog("UpdateHandler ERROR: downloaded file is too small or failed ({:d} bytes)!", data.size());
        this->status = STATUS_ERROR;
        return;
    }

    std::array<u8, 32> file_hash{};
    crypto::hash::sha256(data.data(), data.size(), file_hash.data());
    auto downloaded_update_hash = crypto::conv::encodehex(file_hash);
    if(!hash.empty() && downloaded_update_hash != hash) {
        debugLog("UpdateHandler ERROR: downloaded file hash does not match! {} != {}", downloaded_update_hash, hash);
        this->status = STATUS_ERROR;
        return;
    }

    // write to disk
    debugLog("UpdateHandler: Downloaded file has {:d} bytes, writing ...", data.size());
    std::ofstream file(MCENGINE_DATA_DIR "update.zip", std::ios::out | std::ios::binary);
    if(!file.good()) {
        debugLog("UpdateHandler ERROR: Can't write file!");
        this->status = STATUS_ERROR;
        return;
    }

    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    file.close();

    debugLog("UpdateHandler: Update finished successfully.");
    this->status = STATUS_DOWNLOAD_COMPLETE;
}

void UpdateHandler::installUpdate() {
    debugLog("UpdateHandler: installing");
    Archive::Reader archive(MCENGINE_DATA_DIR "update.zip");
    if(!archive.isValid()) {
        debugLog("UpdateHandler ERROR: couldn't open archive!");
        this->status = STATUS_ERROR;
        return;
    }

    auto entries = archive.getAllEntries();
    if(entries.empty()) {
        debugLog("UpdateHandler ERROR: archive is empty!");
        this->status = STATUS_ERROR;
        return;
    }

    // separate raw dirs and files
    std::string mainDirectory = "neosu/";
    std::vector<Archive::Entry> files, dirs;
    for(const auto &entry : entries) {
        auto fileName = entry.getFilename();
        if(!fileName.starts_with(mainDirectory)) {
            debugLog("UpdateHandler WARNING: Ignoring \"{:s}\" because it's not in the main dir!", fileName.c_str());
            continue;
        }

        if(entry.isDirectory()) {
            dirs.push_back(entry);
        } else {
            files.push_back(entry);
        }

        debugLog("UpdateHandler: Filename: \"{:s}\", isDir: {}, uncompressed size: {}", entry.getFilename().c_str(),
                 entry.isDirectory(), entry.getUncompressedSize());
    }

    // repair/create missing/new dirs
    for(const auto &dir : dirs) {
        std::string newDir = dir.getFilename().substr(mainDirectory.length());
        if(newDir.length() == 0) continue;
        if(Environment::directoryExists(newDir)) continue;

        debugLog("UpdateHandler: Creating directory {:s}", newDir.c_str());
        Environment::createDirectory(newDir);
    }

    // extract and overwrite almost everything
    for(const auto &file : files) {
        std::string outFilePath = file.getFilename().substr(mainDirectory.length());
        if(outFilePath.length() == 0) continue;

        // Bypass Windows write protection for .exe, .dll, .ttf and possibly others
        std::string old_path{outFilePath};
        old_path.append(".old");
        Environment::deleteFile(old_path);
        Environment::renameFile(outFilePath, old_path);

        debugLog("UpdateHandler: Writing {:s}", outFilePath.c_str());
        if(!file.extractToFile(outFilePath)) {
            debugLog("UpdateHandler: Failed to extract file {:s}", outFilePath.c_str());
            Environment::deleteFile(outFilePath);
            Environment::renameFile(old_path, outFilePath);
            this->status = STATUS_ERROR;
            return;
        }
    }

    cv::is_bleedingedge.setValue(cv::bleedingedge.getBool());
    ui->getOptionsOverlay()->save();

    // we're done updating; restart the game, since the user already clicked to update
    engine->restart();
}
