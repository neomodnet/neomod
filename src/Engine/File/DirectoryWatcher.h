#pragma once
// Copyright (c) 2025 kiwec, All rights reserved.

#include "noinclude.h"
#include "types.h"
#include "StaticPImpl.h"

#include <functional>
#include <string>

enum class FileChangeType : u8 {
    CREATED,
    MODIFIED,
    DELETED,
};

struct FileChangeEvent {
    std::string path;
    FileChangeType type;
    bool is_dir{false};  // a direct subdirectory (one level, its mtime moves when entries inside it change)
};

// Consider this API "temporary" until a better solution is implemented

using FileChangeCallback = std::function<void(FileChangeEvent)>;

struct DirWatcherImpl;
class DirectoryWatcher {
    NOCOPY_NOMOVE(DirectoryWatcher);

   public:
    DirectoryWatcher();
    ~DirectoryWatcher();

    // keeps a watch_directory() going until it's destroyed or reset; its callback never runs after that
    class Watch {
       public:
        Watch() = default;
        Watch(Watch &&other) noexcept;
        Watch &operator=(Watch &&other) noexcept;
        Watch(const Watch &) = delete;
        Watch &operator=(const Watch &) = delete;
        ~Watch() { this->reset(); }

        void reset();

       private:
        friend class DirectoryWatcher;
        Watch(DirectoryWatcher *watcher, u32 id) : watcher(watcher), id(id) {}

        DirectoryWatcher *watcher{nullptr};
        u32 id{0};
    };

    // reports changes to the files and direct subdirectories of `path` (not recursive) to `cb`, on the main thread,
    // for as long as the returned Watch lives. any number of watches can share a directory
    [[nodiscard]] Watch watch_directory(std::string path, FileChangeCallback cb);

   private:
    friend class Engine;

    // Similar to other engine async APIs, let us control when callbacks are fired
    // to avoid race condition issues.
    void update();

    void stop_watching(u32 id);

    StaticPImpl<DirWatcherImpl, 256> pImpl;
};

extern DirectoryWatcher *directoryWatcher;
