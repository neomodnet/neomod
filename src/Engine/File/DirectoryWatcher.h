#pragma once
// Copyright (c) 2025 kiwec, All rights reserved.

#include "noinclude.h"
#include "types.h"
#include "Registration.h"
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

    // reports changes to the files and direct subdirectories of `path` (not recursive) to `cb`, on the main thread,
    // for as long as the returned Registration lives. any number of watches can share a directory
    Mc::Registration watch_directory(std::string path, FileChangeCallback cb);

   private:
    friend class Engine;

    // Similar to other engine async APIs, let us control when callbacks are fired
    // to avoid race condition issues.
    void update();

    StaticPImpl<DirWatcherImpl, 256> pImpl;
};

extern DirectoryWatcher *directoryWatcher;
