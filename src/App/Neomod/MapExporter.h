// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "types.h"

#include "AsyncCancellable.h"
#include "AsyncChannel.h"
#include "Registration.h"
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

// what one ExportContext came to
struct Result {
    // the archives written: an .osz per map folder, or the bundle (none if it failed)
    std::vector<std::string> files;
    // the folder the archives went to, or the bundle
    std::string destination;
    // how many map folders the bundle holds (0 without one)
    size_t bundled{0};
};

// submits export work on Lane::Background. pushes a Result into `out` per context as work completes.
// caller owns the returned handle and the channel; channel must outlive the handle.
MC_UNREVOCABLE Async::CancellableHandle<void> submit_export(std::set<ExportContext> contexts,
                                                            Async::Channel<Result>& out);

// exports one batch after another and shows their results as toasts. the app owns one and updates it every frame,
// so exports go on whatever screen shows. on WASM it also downloads every archive, the only way it reaches the user
class Queue {
    NOCOPY_NOMOVE(Queue)
   public:
    Queue() = default;
    ~Queue();

    // starts `ctx` now, or with the next batch once the running one is done (an export already waiting isn't added
    // twice)
    void start(ExportContext ctx);
    // starts the next batch once the running one is done, and shows the results that came in
    void update();

   private:
    std::set<ExportContext> pending;
    // (before the handle, whose task writes to it)
    Async::Channel<Result> results;
    Async::CancellableHandle<void> running;
};

}  // namespace MapExporter
