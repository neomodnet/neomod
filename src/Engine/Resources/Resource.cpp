// Copyright (c) 2012, PG, All rights reserved.
#include "Resource.h"

#include "File.h"
#include "Logging.h"

#include <utility>

Resource::Resource(Type resType, std::string filepath, bool doFilesystemExistenceCheck) : resType(resType) {
    this->sFilePath = std::move(filepath);

    int exists = -1;
    if(doFilesystemExistenceCheck) {
        exists = this->doPathFixup(this->sFilePath);
    }

    // give it a more descriptive debug identifier
    this->sDebugIdentifier = fmt::format("{:8p}:{:s}:name=<none>:postinit=false:filepath={:s}:found={}"_cf,
                                         fmt::ptr(this), this->typeToString(), this->sFilePath,
                                         exists == -1  ? "unknown"
                                         : exists == 1 ? "true"
                                                       : "false");
}

void Resource::setName(std::string_view name) {
    this->sName.assign(name);

    // update debug identifier with new name
    // don't re-check filesystem status, for performance, just check if the last debug identifier had found=y
    // (the debug string doesn't have to be 100% accurate, use a proper debugger for that)
    if(!this->sFilePath.empty()) {
        this->sDebugIdentifier = fmt::format("{:8p}:{:s}:name={:s}:postinit=true:filepath={:s}:{}"_cf, fmt::ptr(this),
                                             this->typeToString(), this->sName, this->sFilePath,
                                             this->sDebugIdentifier.substr(this->sDebugIdentifier.find("found=")));
    } else {
        this->sDebugIdentifier = fmt::format("{:8p}:{:s}:name={:s}:postinit=true:filepath=<none>"_cf, fmt::ptr(this),
                                             this->typeToString(), this->sName);
    }
}

// separate helper for possible reload with new path
bool Resource::doPathFixup(std::string& input) {
    bool file_found = true;
    if(File::existsCaseInsensitive(input) != File::FILETYPE::FILE) {  // modifies the input string if found
        debugLog("Resource Warning: File {:s} does not exist!", input);
        file_found = false;
    }

    return file_found;
}

void Resource::load() {
    this->init();
    if(this->onInit.has_value()) {
        this->onInit->callback(this, this->onInit->userdata);
    }
}

void Resource::loadAsync() {
    this->bInterrupted.store(false, std::memory_order_release);
    this->initAsync();
}

void Resource::reload() {
    this->release();
    this->loadAsync();
    this->load();
}

void Resource::release() {
    this->bInterrupted.store(true, std::memory_order_release);
    this->destroy();

    // NOTE: these are set afterwards on purpose
    this->bReady.store(false, std::memory_order_release);
    this->bAsyncReady.store(false, std::memory_order_release);
    this->bInterrupted.store(false, std::memory_order_release);
}

void Resource::interruptLoad() { this->bInterrupted.store(true, std::memory_order_release); }
