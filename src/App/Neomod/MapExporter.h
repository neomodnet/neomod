// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "types.h"

#include "AsyncCancellable.h"
#include "AsyncChannel.h"
#include "UString.h"

#include <functional>
#include <vector>
#include <string>
#include <set>

namespace MapExporter {

struct ExportContext {
    [[nodiscard]] bool operator==(const ExportContext& o) const {
        return std::operator==(beatmap_folder_paths, o.beatmap_folder_paths) &&
               std::operator==(toplevel_archive_bundle, o.toplevel_archive_bundle);
    }

    [[nodiscard]] auto operator<=>(const ExportContext& o) const {
        if(beatmap_folder_paths == o.beatmap_folder_paths) {
            return std::operator<=>(toplevel_archive_bundle, o.toplevel_archive_bundle);
        } else {
            return std::operator<=>(beatmap_folder_paths, o.beatmap_folder_paths);
        }
    }

    // the main path(s) to export
    std::vector<std::string> beatmap_folder_paths;
    // if toplevel_archive_bundle is non-empty, then nest all compressed folders in a top-level archive with that name
    // (+ .zip extension)
    std::string toplevel_archive_bundle{""};

    using UpdateProgressCallback = std::function<void(float progress, std::string entry_being_processed)>;
    UpdateProgressCallback cb{nullptr};
};

struct Notification {
    UString msg;
    bool success;
    std::function<void()> click_cb;
};

// submits export work on Lane::Background. pushes Notifications into `out` as work completes.
// caller owns the returned handle and the channel; channel must outlive the handle.
Async::CancellableHandle<void> submit_export(
    std::set<ExportContext> contexts,
    Async::Channel<Notification> &out);

}  // namespace MapExporter
