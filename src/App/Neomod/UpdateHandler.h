#pragma once
// Copyright (c) 2016, PG, All rights reserved.

#include "noinclude.h"

#include <string>
#include <atomic>

class UpdateHandler {
    NOCOPY_NOMOVE(UpdateHandler)
   public:
    enum class STATUS : int8_t {
        STATUS_IDLE,
        STATUS_CHECKING_FOR_UPDATE,
        STATUS_DOWNLOADING_UPDATE,
        STATUS_DOWNLOAD_COMPLETE,
        STATUS_ERROR,
    };

    UpdateHandler();
    ~UpdateHandler() = default;

    void checkForUpdates(bool force_update);
    void installUpdate();

    [[nodiscard]] inline STATUS getStatus() const { return this->status.load(std::memory_order_acquire); }

    // release stream management
    void onBleedingEdgeChanged(float oldVal, float newVal);

    // convar command callback
    void updateCallback();

   private:
    // async operation chain
    void onVersionCheckComplete(const std::string& response, bool success, bool force_update);
    void onDownloadComplete(const std::string& data, bool success, std::string hash);

    // status
    std::atomic<STATUS> status = STATUS::STATUS_IDLE;
};
