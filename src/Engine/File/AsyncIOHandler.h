// Copyright (c) 2025-2026, WH, All rights reserved.
#pragma once

#include "config.h"
#include "noinclude.h"
#include "Registration.h"
#include "StaticPImpl.h"
#include "types.h"

#include <functional>
#include <string_view>
#include <string>
#include <vector>
#include <memory>

class AsyncIOHandler final {
    NOCOPY_NOMOVE(AsyncIOHandler)
   public:
    AsyncIOHandler();
    ~AsyncIOHandler();

    // read entire file asynchronously
    // callback receives data vector (empty on failure, e.g. if the file already has a pending operation) on the main
    // thread, in a later update(), as long as the Registration lives; a detached one also gets called for an
    // operation that finishes during engine shutdown, after the app is gone
    using ReadCallback = std::function<void(std::vector<u8>)>;
    Mc::Registration read(std::string_view path, ReadCallback callback);

    // write data to file asynchronously
    // callback receives success status after write completes, like read()'s
    using WriteCallback = std::function<void(bool)>;
    Mc::Registration write(std::string_view path, std::vector<u8> data, WriteCallback callback);
    Mc::Registration write(std::string_view path, std::string data, WriteCallback callback);

   private:
    friend class Engine;  // only to be used by engine

    // returns true if initialization succeeded
    [[nodiscard]] bool succeeded() const;

    // clean up all i/o before shutdown
    void cleanup();

    // must be called regularly (e.g., once per frame) to process completed I/O tasks
    void update();

   private:
    class InternalIOContext;
#ifdef MCENGINE_PLATFORM_WASM
    StaticPImpl<InternalIOContext, 40> m_impl;  // basically a passthrough to synchronous I/O
#else
    StaticPImpl<InternalIOContext, 152> m_impl;
#endif
};

extern AsyncIOHandler *io;
