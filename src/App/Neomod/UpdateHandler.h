#pragma once
// Copyright (c) 2016, PG, All rights reserved.

#include "noinclude.h"
#include "types.h"
#include "SyncStoptoken.h"

#include <string>
#include <string_view>
#include <span>
#include <atomic>

class UpdateHandler {
    NOCOPY_NOMOVE(UpdateHandler)
   public:
    enum class STATUS : int8_t {
        STATUS_IDLE,
        STATUS_CHECKING_FOR_UPDATE,
        STATUS_DOWNLOADING_UPDATE,
        STATUS_DOWNLOAD_COMPLETE,
        STATUS_MANUAL_UPDATE,  // newer version available, but this platform can't self-update (see installUpdate)
        STATUS_ERROR,
    };

    UpdateHandler();
    ~UpdateHandler();

    void checkForUpdates(bool force_update);
    void installUpdate();

    [[nodiscard]] inline STATUS getStatus() const { return this->status.load(std::memory_order_acquire); }

    // release stream management
    void onBleedingEdgeChanged(float oldVal, float newVal);
    void onSupporterToken();

    // convar command callback
    void updateCallback();

   private:
    // async operation chain
    void onVersionCheckComplete(std::string_view response, bool success, long response_code, bool force_update);
    void onDownloadComplete(std::span<const u8> data, bool success, std::string_view hash);

    // status
    std::atomic<STATUS> status = STATUS::STATUS_IDLE;

    // armed per checkForUpdates(); request_stop() aborts an in-flight version check or download
    Sync::stop_source cancel_src;

    // the stream that the latest check used, which installUpdate() installs
    bool checking_bleedingedge{false};

    // bleeding edge checks wait for the supporter token, which the server sends on login
    bool waiting_for_token{false};
    bool force_after_token{false};
};
