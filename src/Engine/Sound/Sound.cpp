// Copyright (c) 2014, PG, All rights reserved.
#include "Sound.h"

#include "BassSound.h"
#include "SoLoudSound.h"

#include "ConVar.h"
#include "Environment.h"
#include "File.h"
#include "ResourceManager.h"
#include "SoundEngine.h"
#include "SString.h"
#include "Logging.h"

#include <memory>
#include <utility>
#include <algorithm>

void Sound::initAsync() {
    std::string toLoad;
    const bool isRebuild = !this->sRebuildFilePath.empty();
    if(isRebuild) {
        toLoad = std::move(this->sRebuildFilePath);
        this->sRebuildFilePath.clear();
    } else {
        toLoad = this->sFilePath;
    }

    this->doPathFixup(toLoad);

    logIfCV(debug_rm, "Resource Manager: Loading {:s}", toLoad);

    // sanity check for malformed audio files
    const std::string fileExtensionLowerCase{SString::to_lower(env->getFileExtensionFromFilePath(toLoad))};

    if(toLoad.empty() || fileExtensionLowerCase.empty()) {
        this->bIgnored = true;
    } else if(!this->isValidAudioFile(toLoad, fileExtensionLowerCase)) {
        if(!cv::snd_force_load_unknown.getBool()) {
            debugLog("Sound: Ignoring malformed/corrupt .{:s} file {:s}", fileExtensionLowerCase, toLoad);
            this->bIgnored = true;
        } else {
            logIfCV(debug_snd,
                    "Sound: snd_force_load_unknown=true, loading what seems to be a malformed/corrupt .{:s} file "
                    "{:s}",
                    fileExtensionLowerCase, toLoad);
            this->bIgnored = false;
        }
    } else {
        this->bIgnored = false;
    }

    // this is technically racy, since sFilePath is not synchronized, and we are probably running async
    this->sFilePath = toLoad;
}

void Sound::rebuild(std::string_view newFilePath, bool async) {
    if(!newFilePath.empty()) {
        this->sRebuildFilePath = newFilePath;
    }

    resourceManager->reloadResource(this, async);
}

// quick heuristic to check if it's going to be worth loading the audio
bool Sound::isValidAudioFile(std::string_view filePath, std::string_view fileExt) {
    File testFile(filePath);

    if(!testFile.canRead()) return false;

    size_t fileSize = testFile.getFileSize();

    // account for larger flac header
    size_t minSize = fileExt == "flac" ? std::max<size_t>(cv::snd_file_min_size.getVal<size_t>(), 96)
                                       : cv::snd_file_min_size.getVal<size_t>();

    if(fileExt == "wav" || fileExt == "mp3" || fileExt == "ogg" || fileExt == "flac") {
        return fileSize >= minSize;
    }

    return false;  // don't let unsupported formats be read
}

const std::unordered_map<SOUNDHANDLE, PlaybackParams>& Sound::getActiveHandles() {
    // update cache with actual validity from backend
    std::erase_if(this->activeHandleCache,
                  [this](const auto& handleInstance) { return !this->isHandleValid(handleInstance.first); });
    return this->activeHandleCache;
}

void Sound::addActiveInstance(SOUNDHANDLE handle, PlaybackParams instance) {
    this->activeHandleCache[handle] = instance;
}

void Sound::setBaseVolume(float volume) {
    this->fBaseVolume = std::clamp<float>(volume, 0.0f, 2.0f);

    // propagate the changed volume to the active voice handles
    for(const auto& [handle, instance] : this->getActiveHandles()) {
        const auto& vol = instance.volume;
        this->setHandleVolume(handle, this->fBaseVolume * vol);
    }
}
