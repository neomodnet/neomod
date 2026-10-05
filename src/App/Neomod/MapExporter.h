// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "types.h"

#include "AsyncCancellable.h"
#include "AsyncChannel.h"
#include "noinclude.h"

#include <compare>
#include <functional>
#include <vector>
#include <string>
#include <set>

namespace MapExporter {

// the folder exports land in: cv::export_folder if set, otherwise <data dir>/exports (always ends with a slash)
std::string export_root();

struct ExportContext {
    [[nodiscard]] bool operator==(const ExportContext& o) const;
    [[nodiscard]] std::strong_ordering operator<=>(const ExportContext& o) const;

    // the main path(s) to export
    std::vector<std::string> beatmap_folder_paths;
    // if toplevel_archive_bundle is non-empty, then nest all compressed folders in a top-level archive with that name
    // (+ .zip extension)
    std::string toplevel_archive_bundle{""};

    using UpdateProgressCallback = std::function<void(float progress, std::string entry_being_processed)>;
    UpdateProgressCallback cb{nullptr};
};

struct Notification {
    std::string msg;
    bool success;
    std::function<void()> click_cb;
};

// submits export work on Lane::Background. pushes Notifications into `out` as work completes.
// caller owns the returned handle and the channel; channel must outlive the handle.
MC_UNREVOCABLE Async::CancellableHandle<void> submit_export(std::set<ExportContext> contexts,
                                                            Async::Channel<Notification>& out);

}  // namespace MapExporter
