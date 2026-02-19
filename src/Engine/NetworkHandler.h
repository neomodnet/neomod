#pragma once
// Copyright (c) 2015, PG & 2025, WH & 2025, kiwec, All rights reserved.
#include "config.h"

#include "noinclude.h"
#include "types.h"
#include "StaticPImpl.h"
#include "Hashing.h"

#include <string>
#include <string_view>
#include <functional>
#include <memory>
#include <atomic>
#include <vector>

// forward defs
#ifndef MCENGINE_PLATFORM_WASM
typedef void CURL;
#endif
class Engine;

// generic networking things, not BANCHO::Net
namespace Mc::Net {
struct NetworkImpl;

// standalone helper
// Fully URL-encodes a string, including slashes
[[nodiscard]] std::string urlEncode(std::string_view unencodedString) noexcept;

// public defs
enum class WSStatus : u8 {
    CONNECTING,
    CONNECTED,
    DISCONNECTED,
    UNSUPPORTED,
};

struct WSOptions {
    Hash::unstable_stringmap<std::string> headers;
    std::string user_agent;
    long timeout{5};
    long connect_timeout{5};
    u64 max_recv{10ULL * 1024 * 1024};  // limit "in" buffer to 10Mb
};

struct WSInstance {
   private:
    NOCOPY_NOMOVE(WSInstance)
    friend class NetworkHandler;
    friend struct NetworkImpl;

   public:
    WSInstance() = default;
    ~WSInstance();

    std::atomic<WSStatus> status{WSStatus::CONNECTING};
    std::vector<u8> in;
    std::vector<u8> out;
    f64 time_created{0.f};

   private:
#ifdef MCENGINE_PLATFORM_WASM
    int handle{0};
#else
    CURL* handle{nullptr};

    // Servers can send fragmented packets, we want to only append them
    // to "in" once the packets are complete.
    std::vector<u8> in_partial;
    u64 max_recv{0};  // in bytes
#endif
};

// async request options
struct RequestOptions {
    friend class NetworkHandler;
    friend struct NetworkImpl;

    struct MimePart {
        std::string filename{};
        std::string name{};
        std::vector<u8> data{};
    };

    Hash::unstable_stringmap<std::string> headers{};
    std::string post_data{};
    std::string user_agent{};
    std::vector<MimePart> mime_parts{};
    std::function<void(float)> progress_callback{nullptr};  // progress callback for downloads
    long timeout{5};
    long connect_timeout{5};

    // NOTE: not all flags are supported by all APIs/implementations
    static constexpr u8 FOLLOW_REDIRECTS = 1 << 0;
    static constexpr u8 WEBSOCKET = 1 << 1;  // TODO: remove this
    // KEEPALIVE: for httpRequestSynchronous, use a non-blocking fire-and-forget request
    // that survives page unload. on WASM this uses fetch(keepalive); on native it's a no-op
    // (the synchronous request already completes reliably).
    static constexpr u8 KEEPALIVE = 1 << 2;
    u8 flags{0};
};

// async response data
struct Response {
   private:
    friend class NetworkHandler;
    friend struct NetworkImpl;
#ifndef MCENGINE_PLATFORM_WASM
    // HACK for passing websocket handle
    CURL* easy_handle{nullptr};
#endif

   public:
    long response_code{0};
    std::string body;
    std::string error_msg;
    Hash::unstable_stringmap<std::string> headers;
    bool success{false};
};

using AsyncCallback = std::function<void(Response response)>;
using IPCCallback = std::function<void(std::vector<std::string>)>;

// NOTE: do not prepend url with https:// or http:// (or wss:// ws:// for initWebsocket), this will be auto-prepended depending on the use_https ConVar
class NetworkHandler {
    NOCOPY_NOMOVE(NetworkHandler)
   public:
    NetworkHandler();
    ~NetworkHandler();

    // synchronous requests
    Response httpRequestSynchronous(std::string_view url, RequestOptions options);

    // asynchronous API
    void httpRequestAsync(std::string_view url, RequestOptions options, AsyncCallback callback = {});

    // websockets
    // TODO: consolidate websocket/http to avoid needing this entirely
    // (should be able to just choose the implementation based off of http(s):// or ws(s):// protocol prefix)
    std::shared_ptr<WSInstance> initWebsocket(std::string_view url, const WSOptions& options);

    // IPC socket for instance detection (Linux)
    void setIPCSocket(int fd, IPCCallback callback);

   private:
    // callback update tick
    friend class ::Engine;
    void update();

    // implementation details
    friend struct NetworkImpl;
#ifdef MCENGINE_PLATFORM_WASM
    StaticPImpl<NetworkImpl, 128> pImpl;
#else
    StaticPImpl<NetworkImpl, 704> pImpl;
#endif
};

}  // namespace Mc::Net

using Mc::Net::NetworkHandler;
extern std::unique_ptr<NetworkHandler> networkHandler;
