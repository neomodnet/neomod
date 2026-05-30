// Copyright (c) 2015, PG & 2025, WH & 2025, kiwec, All rights reserved.
#include "config.h"

#ifndef MCENGINE_PLATFORM_WASM

#include "NetworkHandler.h"
#include "Engine.h"
#include "Thread.h"
#include "SString.h"
#include "ConVar.h"
#include "Timing.h"
#include "Logging.h"
#include "SyncJthread.h"
#include "SyncCV.h"
#include "SyncStoptoken.h"
#include "ContainerRanges.h"

#include "binary_embed.h"
#include <curl/curl.h>

#include <utility>
#include <queue>
#include <atomic>
#include <optional>

#if defined(MCENGINE_PLATFORM_LINUX) || defined(MCENGINE_PLATFORM_MACOS)
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Mc::Net {

std::string urlEncode(std::string_view unencodedString) noexcept {
    CURL* curl = curl_easy_init();
    if(!curl) {
        return "";
    }

    char* encoded = curl_easy_escape(curl, unencodedString.data(), static_cast<int>(unencodedString.length()));
    if(!encoded) {
        curl_easy_cleanup(curl);
        return "";
    }

    std::string result(encoded);
    curl_free(encoded);
    curl_easy_cleanup(curl);
    return result;
}

WSInstance::~WSInstance() = default;

void WSInstance::write(std::span<const u8> data) {
    {
        Sync::scoped_lock lock{this->io_mutex};
        this->out.insert(this->out.end(), data.begin(), data.end());
    }
    // wake up curl_multi_poll so the network thread sends asap
    if(auto* mh = static_cast<CURLM*>(this->multi_wakeup.load(std::memory_order_acquire))) {
        curl_multi_wakeup(mh);
    }
}

std::vector<u8> WSInstance::read() {
    Sync::scoped_lock lock{this->io_mutex};
    return std::exchange(this->in, {});
}

std::vector<u8> WSInstance::drain_output() {
    Sync::scoped_lock lock{this->io_mutex};
    return std::exchange(this->out, {});
}

// RAII wrappers for curl resources
namespace {

struct CurlEasyDeleter {
    void operator()(CURL* handle) const {
        if(handle) curl_easy_cleanup(handle);
    }
};

struct CurlEasy : public std::unique_ptr<CURL, CurlEasyDeleter> {
    [[nodiscard]] operator CURL*() const { return this->get(); }

    template <typename Opt, typename... Args>
    auto setopt(Opt&& opt, Args&&... args)
        requires(sizeof...(Args) > 0)
    {
        return curl_easy_setopt(this->get(), std::forward<Opt>(opt), std::forward<Args>(args)...);
    }
};

struct CurlMimeDeleter {
    void operator()(curl_mime* mime) const {
        if(mime) curl_mime_free(mime);
    }
};

struct CurlMime : public std::unique_ptr<curl_mime, CurlMimeDeleter> {
    [[nodiscard]] operator curl_mime*() const { return this->get(); }
};

// append() can reallocate, so use a "builder" pattern
class CurlSlist {
    curl_slist* list{nullptr};

   public:
    CurlSlist() = default;
    ~CurlSlist() {
        if(this->list) curl_slist_free_all(this->list);
    }

    CurlSlist(const CurlSlist&) = delete;
    CurlSlist& operator=(const CurlSlist&) = delete;
    CurlSlist(CurlSlist&& other) noexcept : list(other.list) { other.list = nullptr; }
    CurlSlist& operator=(CurlSlist&& other) noexcept {
        if(this != &other) {
            if(this->list) curl_slist_free_all(this->list);
            this->list = other.list;
            other.list = nullptr;
        }
        return *this;
    }

    void append(const char* str) { this->list = curl_slist_append(this->list, str); }
    [[nodiscard]] curl_slist* get() const { return this->list; }
    explicit operator bool() const { return !!this->list; }
};

// stop_callback body: wakes curl_multi_poll so a cancellation request is noticed promptly
// (curl_multi_wakeup is thread-safe, so this can fire from the canceller's thread)
struct CurlMultiWaker {
    CURLM* multi_handle{nullptr};
    void operator()() const noexcept {
        if(multi_handle) curl_multi_wakeup(multi_handle);
    }
};

}  // namespace

struct NetworkImpl {
   private:
    NOCOPY_NOMOVE(NetworkImpl)

   public:
    // internal request structure
    struct Request {
        std::string url;
        RequestOptions options;
        AsyncCallback callback;
        Response response;

        CurlEasy easy_handle;
        CurlSlist headers_list;
        CurlMime mime;

        // for websocket requests
        std::shared_ptr<WSInstance> websocket;

        // for sync requests
        bool is_sync{false};
        void* sync_id{nullptr};

        // registered on the network thread for cancellable requests; wakes the poll when
        // cancellation is requested. the Request's address is stable (owned via unique_ptr),
        // so holding a non-movable stop_callback in-place is safe.
        std::optional<Sync::stop_callback<CurlMultiWaker>> cancel_waker;

        Request(std::string url, RequestOptions opts, AsyncCallback cb = {})
            : url(std::move(url)), options(std::move(opts)), callback(std::move(cb)) {}

        void setupCurlHandle();
    };

    // necessary data for deferred callback execution
    // (the rest of the request is immediately deleted)
    struct CompletedRequest {
        AsyncCallback callback;
        Response response;
        Sync::stop_token cancel_token;  // re-checked at dispatch: a request cancelled after completing isn't delivered
    };

    NetworkImpl() {
        // this needs to be called once to initialize curl on startup
        curl_global_init(CURL_GLOBAL_DEFAULT);

        this->multi_handle = curl_multi_init();
        if(!this->multi_handle) {
            debugLog("ERROR: Failed to initialize curl multi handle!");
            return;
        }

        // start network thread
        this->network_thread = Sync::jthread([this](const Sync::stop_token& stoken) { this->threadLoopFunc(stoken); });

        if(!this->network_thread.joinable()) {
            debugLog("ERROR: Failed to create network thread!");
        }
    }

    ~NetworkImpl() {
        this->network_thread = {};  // shut down the thread now

        // cleanup any remaining requests (including websockets; network thread is stopped)
        for(auto& [handle, request] : this->active_requests) {
            curl_multi_remove_handle(this->multi_handle, handle);
            if(request->websocket) {
                request->websocket->handle = nullptr;
            }
        }
        this->active_requests.clear();

        {
            Sync::scoped_lock completed_lock{this->completed_requests_mutex};
            this->completed_requests.clear();
        }

        if(this->multi_handle) {
            curl_multi_cleanup(this->multi_handle);
        }

        curl_global_cleanup();

#if defined(MCENGINE_PLATFORM_LINUX) || defined(MCENGINE_PLATFORM_MACOS)
        // close IPC socket so restart works (this instance is gone)
        if(int sock = this->ipc_socket_fd.load(std::memory_order_acquire); sock != -1) {
            close(sock);
        }
#endif
    }

    // public interface methods (passthroughs)
    // synchronous requests
    Response httpRequestSynchronous(std::string_view url, RequestOptions options);

    // asynchronous API
    void httpRequestAsync(std::string_view url, RequestOptions options, AsyncCallback callback = {});

    // websockets
    std::shared_ptr<WSInstance> initWebsocket(std::string_view url, const WSOptions& options);

    void update();

    // request queuing
    Sync::mutex request_queue_mutex;
    std::queue<std::unique_ptr<Request>> pending_requests;

    // active requests tracking (only accessed from the network thread)
    std::unordered_map<CURL*, std::unique_ptr<Request>> active_requests;

    // completed requests (callback + response only)
    Sync::mutex completed_requests_mutex;
    std::vector<CompletedRequest> completed_requests;

    // sync request support
    Sync::mutex sync_requests_mutex;
    std::unordered_map<void*, Sync::condition_variable*> sync_request_cvs;
    std::unordered_map<void*, Response> sync_responses;

    // curl_multi implementation
    CURLM* multi_handle{nullptr};
    Sync::jthread network_thread;

    // IPC socket for instance detection (Linux/macOS)
    std::atomic<int> ipc_socket_fd{-1};
    IPCCallback ipc_callback;
    Sync::mutex ipc_mutex;
    std::vector<std::vector<std::string>> pending_ipc_messages;

    void setIPCSocket(int fd, IPCCallback callback);
    void handleIPCConnection(int ipc_fd);

    void processNewRequests();
    void processCancelledRequests();
    void processCompletedRequests();
    void websocketSend();

    static uSz headerCallback(char* buffer, uSz size, uSz nitems, void* userdata);
    static uSz writeCallback(void* contents, uSz size, uSz nmemb, void* userp);
    static i32 progressCallback(void* clientp, i64 dltotal, i64 dlnow, i64 /**/, i64 /**/);

    // main async thread
    void threadLoopFunc(const Sync::stop_token& stopToken);
};

void NetworkImpl::threadLoopFunc(const Sync::stop_token& stopToken) {
    McThread::set_current_thread_name("net_manager");
    McThread::set_current_thread_prio(McThread::Priority::NORMAL);  // reset priority

    Sync::stop_callback stop_cb(stopToken, [this] { curl_multi_wakeup(this->multi_handle); });

#if defined(MCENGINE_PLATFORM_LINUX) || defined(MCENGINE_PLATFORM_MACOS)
    int ipc_socket_local = -1;
#endif

    while(!stopToken.stop_requested()) {
        processNewRequests();
        processCancelledRequests();
        websocketSend();

        i32 running_handles = 0;
        if(!this->active_requests.empty()) {
            CURLMcode mres = curl_multi_perform(this->multi_handle, &running_handles);

            if(mres != CURLM_OK) {
                debugLog("curl_multi_perform error: {}", curl_multi_strerror(mres));
            }

            processCompletedRequests();
        }

        // wait for activity on curl handles (including websockets) and IPC socket;
        // woken by curl_multi_wakeup() when new requests are submitted
        int numfds = 0;
#if defined(MCENGINE_PLATFORM_LINUX) || defined(MCENGINE_PLATFORM_MACOS)
        curl_waitfd ipc_fd{};
        if(ipc_socket_local == -1) {
            ipc_socket_local = this->ipc_socket_fd.load(std::memory_order_acquire);
        }
        if(ipc_socket_local >= 0) {
            ipc_fd = {.fd = (curl_socket_t)ipc_socket_local, .events = CURL_WAIT_POLLIN, .revents = {}};
        }
        const int nfds = (ipc_socket_local >= 0);
        curl_multi_poll(this->multi_handle, nfds ? &ipc_fd : nullptr, nfds, 60000, &numfds);
        if(nfds && (ipc_fd.revents & CURL_WAIT_POLLIN)) {
            handleIPCConnection(ipc_socket_local);
        }
#else
        curl_multi_poll(this->multi_handle, nullptr, 0, 60000, &numfds);
#endif
    }
}

void NetworkImpl::processNewRequests() {
    Sync::scoped_lock requests_lock{this->request_queue_mutex};

    while(!this->pending_requests.empty()) {
        auto request = std::move(this->pending_requests.front());
        this->pending_requests.pop();

        // cancelled before it even started: don't open a connection, don't run the callback.
        // sync requests block their caller and ignore cancellation, so never drop one here (it would
        // strand the waiting caller, who only wakes once a response lands in sync_responses).
        if(!request->is_sync && request->options.cancel_token.stop_requested()) continue;

        request->easy_handle.reset(curl_easy_init());
        if(!request->easy_handle) {
            request->response.success = false;
            if(request->callback) {  // if there's no callback, don't put it in completed_requests
                Sync::scoped_lock completed_lock{this->completed_requests_mutex};
                this->completed_requests.emplace_back(std::move(request->callback), std::move(request->response));
            }
            continue;
        }

        request->setupCurlHandle();

        if(request->websocket) {
            request->websocket->multi_wakeup.store(this->multi_handle, std::memory_order_release);
        }

        // arm prompt cancellation. websockets manage their own teardown (shared_ptr + status), and
        // sync requests block their caller (cancelling would strand it), so neither registers a waker.
        if(!request->websocket && !request->is_sync && request->options.cancel_token.stop_possible()) {
            request->cancel_waker.emplace(request->options.cancel_token, CurlMultiWaker{this->multi_handle});
        }

        CURLMcode mres = curl_multi_add_handle(this->multi_handle, request->easy_handle);
        if(mres != CURLM_OK) {
            request->response.success = false;
            if(request->callback) {
                Sync::scoped_lock completed_lock{this->completed_requests_mutex};
                this->completed_requests.emplace_back(std::move(request->callback), std::move(request->response));
            }
            continue;
        }

        this->active_requests[request->easy_handle] = std::move(request);
    }
}

// drop requests whose caller asked to cancel: abort the transfer and discard the callback without running it.
// runs on the network thread, woken promptly by the request's CurlMultiWaker when stop is requested.
void NetworkImpl::processCancelledRequests() {
    for(auto it = this->active_requests.begin(); it != this->active_requests.end();) {
        auto& request = it->second;
        if(!request->is_sync && request->options.cancel_token.stop_requested()) {
            curl_multi_remove_handle(this->multi_handle, it->first);
            it = this->active_requests.erase(it);  // ~Request cleans up the easy handle and unregisters the waker
        } else {
            ++it;
        }
    }
}

void NetworkImpl::processCompletedRequests() {
    CURLMsg* msg;
    i32 msgs_left;

    // collect completed requests without holding locks during callback execution
    while((msg = curl_multi_info_read(this->multi_handle, &msgs_left))) {
        if(msg->msg != CURLMSG_DONE) continue;
        CURL* raw_handle = msg->easy_handle;

        auto it = this->active_requests.find(raw_handle);
        if(it == this->active_requests.end()) continue;

        auto request = std::move(it->second);
        this->active_requests.erase(it);

        curl_multi_remove_handle(this->multi_handle, raw_handle);

        curl_easy_getinfo(raw_handle, CURLINFO_RESPONSE_CODE, &request->response.response_code);
        request->response.success = (msg->data.result == CURLE_OK);
        if(msg->data.result == CURLE_OK || msg->data.result == CURLE_HTTP_RETURNED_ERROR) {
            request->response.error_msg = "HTTP " + std::to_string(request->response.response_code);
        } else {
            request->response.error_msg = curl_easy_strerror(msg->data.result);
        }

        if(request->websocket) {
            // websocket transfer completed (disconnected or upgrade failed)
            request->websocket->handle = nullptr;
            if(request->websocket->status.load(std::memory_order_relaxed) == WSStatus::CONNECTING) {
                request->websocket->status.store(WSStatus::UNSUPPORTED, std::memory_order_relaxed);
            } else {
                request->websocket->status.store(WSStatus::DISCONNECTED, std::memory_order_relaxed);
            }
        } else if(request->is_sync) {
            // handle sync request immediately
            Sync::scoped_lock sync_lock{this->sync_requests_mutex};
            this->sync_responses[request->sync_id] = request->response;
            auto cv_it = this->sync_request_cvs.find(request->sync_id);
            if(cv_it != this->sync_request_cvs.end()) {
                cv_it->second->notify_one();
            }
        } else if(request->callback) {
            // a cancel may have raced the natural completion: drop without delivering
            if(request->options.cancel_token.stop_requested()) continue;
            // defer async callback execution
            Sync::scoped_lock completed_lock{this->completed_requests_mutex};
            this->completed_requests.emplace_back(std::move(request->callback), std::move(request->response),
                                                  request->options.cancel_token);
        }
    }
}

void NetworkImpl::websocketSend() {
    for(auto& [handle, req] : this->active_requests) {
        if(!req->websocket || req->websocket->status.load(std::memory_order_relaxed) != WSStatus::CONNECTED) continue;
        auto& ws = req->websocket;

        std::vector<u8> to_send;
        {
            Sync::scoped_lock lock{ws->io_mutex};
            to_send = std::exchange(ws->out, {});
        }

        size_t total_sent = 0;
        CURLcode res = CURLE_OK;
        while(res == CURLE_OK && total_sent < to_send.size()) {
            size_t nb_sent = 0;
            res = curl_ws_send(ws->handle, to_send.data() + total_sent, to_send.size() - total_sent, &nb_sent, 0,
                               CURLWS_BINARY);
            total_sent += nb_sent;
        }

        if(total_sent < to_send.size()) {
            Sync::scoped_lock lock{ws->io_mutex};
            ws->out.insert(ws->out.begin(), to_send.begin() + (ssize_t)total_sent, to_send.end());
        }

        if(res != CURLE_OK && res != CURLE_AGAIN) {
            debugLog("Failed to send data on websocket: {}", curl_easy_strerror(res));
            ws->status.store(WSStatus::DISCONNECTED, std::memory_order_relaxed);
        }
    }

    // clean up disconnected websockets
    for(auto it = this->active_requests.begin(); it != this->active_requests.end();) {
        auto& req = it->second;
        if(req->websocket && req->websocket->status.load(std::memory_order_relaxed) != WSStatus::CONNECTED &&
           req->websocket->status.load(std::memory_order_relaxed) != WSStatus::CONNECTING) {
            curl_multi_remove_handle(this->multi_handle, it->first);
            req->websocket->handle = nullptr;
            it = this->active_requests.erase(it);
        } else {
            ++it;
        }
    }
}

i32 NetworkImpl::progressCallback(void* clientp, i64 dltotal, i64 dlnow, i64 /*unused*/, i64 /*unused*/) {
    auto* request = static_cast<Request*>(clientp);
    if(request->options.progress_callback && dltotal > 0) {
        float progress = static_cast<float>(dlnow) / static_cast<float>(dltotal);
        request->options.progress_callback(progress);
    }
    return 0;
}

#ifndef _MSC_VER
namespace {  // static
// this is unnecessary with our MSVC build of curl, since it uses schannel instead of openssl
// curl_ca_embed included from binary_embed.h
struct curl_blob cert_blob{
    .data = (void*)curl_ca_embed, .len = (size_t)curl_ca_embed_size(), .flags = CURL_BLOB_NOCOPY};
}  // namespace
#endif

namespace {
int curlDebugCallback(CURL* /*handle*/, curl_infotype type, char* data, size_t size, void* /*userdata*/) {
    if(!cv::debug_network.getBool()) return 0;  // no thanks

    // skip raw data (binary, potentially large)
    if(type == CURLINFO_DATA_IN || type == CURLINFO_DATA_OUT || type == CURLINFO_SSL_DATA_IN ||
       type == CURLINFO_SSL_DATA_OUT) {
        return 0;
    }
    std::string_view sv{data, size};

    // curl includes trailing newlines but our logger also adds them, so remove these
    while(!sv.empty() && (sv.back() == '\n' || sv.back() == '\r')) {
        sv.remove_suffix(1);
    }

    if(!sv.empty()) {
        logRawChannel(Logger::CHAN_NETWORK, sv);
    }

    return 0;
}
}  // namespace

void NetworkImpl::Request::setupCurlHandle() {
    assert(this->easy_handle.get());

    // always enable verbose, filter it out in the callback if we have debug_network disabled
    // (can't easily update verbose mode for existing handles)
    this->easy_handle.setopt(CURLOPT_VERBOSE, 1L);
    this->easy_handle.setopt(CURLOPT_DEBUGFUNCTION, curlDebugCallback);

    this->easy_handle.setopt(CURLOPT_URL, this->url.c_str());
    this->easy_handle.setopt(CURLOPT_CONNECTTIMEOUT, this->options.connect_timeout);
    this->easy_handle.setopt(CURLOPT_TIMEOUT, this->options.timeout);
    this->easy_handle.setopt(CURLOPT_WRITEFUNCTION, writeCallback);
    this->easy_handle.setopt(CURLOPT_WRITEDATA, this);
    this->easy_handle.setopt(CURLOPT_HEADERFUNCTION, headerCallback);
    this->easy_handle.setopt(CURLOPT_HEADERDATA, this);
    this->easy_handle.setopt(CURLOPT_SSL_VERIFYHOST, cv::ssl_verify.getBool() ? 2L : 0L);
    this->easy_handle.setopt(CURLOPT_SSL_VERIFYPEER, cv::ssl_verify.getBool() ? 1L : 0L);
    this->easy_handle.setopt(CURLOPT_NOSIGNAL, 1L);
    this->easy_handle.setopt(CURLOPT_FAILONERROR, 1L);  // fail on HTTP responses >= 400

    if(!this->options.user_agent.empty()) {
        this->easy_handle.setopt(CURLOPT_USERAGENT, this->options.user_agent.c_str());
    }

    if(this->options.flags & RequestOptions::FOLLOW_REDIRECTS) {
        this->easy_handle.setopt(CURLOPT_FOLLOWLOCATION, 1L);
    }

#ifndef _MSC_VER
    this->easy_handle.setopt(CURLOPT_CAINFO_BLOB, &cert_blob);
#endif

    // setup headers
    if(!this->options.headers.empty()) {
        for(const auto& [key, value] : this->options.headers) {
            std::string header = fmt::format("{}: {}", key, value);
            this->headers_list.append(header.c_str());
        }
        this->easy_handle.setopt(CURLOPT_HTTPHEADER, this->headers_list.get());
    }

    // setup POST data
    if(!this->options.post_data.empty()) {
        this->easy_handle.setopt(CURLOPT_POSTFIELDS, this->options.post_data.c_str());
        this->easy_handle.setopt(CURLOPT_POSTFIELDSIZE, this->options.post_data.length());
    }

    // setup MIME data
    if(!this->options.mime_parts.empty()) {
        this->mime.reset(curl_mime_init(this->easy_handle));

        for(const auto& info : this->options.mime_parts) {
            auto part = curl_mime_addpart(this->mime);
            if(!info.filename.empty()) {
                curl_mime_filename(part, info.filename.c_str());
            }
            curl_mime_name(part, info.name.c_str());
            curl_mime_data(part, reinterpret_cast<const char*>(info.data.data()), info.data.size());
        }

        this->easy_handle.setopt(CURLOPT_MIMEPOST, this->mime.get());
    }

    // setup progress callback if provided
    if(this->options.progress_callback) {
        this->easy_handle.setopt(CURLOPT_NOPROGRESS, 0L);
        this->easy_handle.setopt(CURLOPT_XFERINFOFUNCTION, progressCallback);
        this->easy_handle.setopt(CURLOPT_XFERINFODATA, this);
    }
}

size_t NetworkImpl::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* request = static_cast<Request*>(userp);
    const size_t real_size = size * nmemb;

    auto& ws = request->websocket;
    const struct curl_ws_frame* meta = nullptr;
    if(ws && !!(meta = curl_ws_meta(request->easy_handle))) {  // is websocket and has metadata
        // websocket frame data arrives here via curl_multi_perform
        if(real_size > 0 && (meta->flags & CURLWS_BINARY)) {
            ws->in_partial.insert(ws->in_partial.end(), static_cast<u8*>(contents),
                                  static_cast<u8*>(contents) + real_size);
        }
        if(!ws->in_partial.empty() && meta->bytesleft == 0) {
            Sync::scoped_lock lock{ws->io_mutex};
            Mc::append_range(ws->in, std::move(ws->in_partial));
            ws->in_partial.clear();
        }
        if(meta->flags & CURLWS_CLOSE) {
            debugLog("Websocket connection closed.");
            ws->status.store(WSStatus::DISCONNECTED, std::memory_order_relaxed);
        }
    } else if(!ws) {  // non-websocket path
        request->response.body.append(static_cast<char*>(contents), real_size);
    }

    return real_size;
}

size_t NetworkImpl::headerCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* request = static_cast<Request*>(userdata);
    size_t real_size = size * nitems;

    if(request->websocket) {
        // detect successful websocket upgrade (HTTP 101)
        std::string_view header(buffer, real_size);
        if(header.starts_with("HTTP/") && header.find(" 101 ") != std::string_view::npos) {
            request->websocket->handle = request->easy_handle.get();
            request->websocket->status.store(WSStatus::CONNECTED, std::memory_order_relaxed);
        }
        return real_size;
    }

    std::string header(buffer, real_size);
    size_t colon_pos = header.find(':');
    if(colon_pos != std::string::npos) {
        std::string key = header.substr(0, colon_pos);
        std::string value = header.substr(colon_pos + 1);

        // lowercase the key for consistency between platforms/curl builds
        SString::lower_inplace(key);

        // trim whitespace
        SString::trim_inplace(key);
        SString::trim_inplace(value);

        request->response.headers[key] = value;
    }

    return real_size;
}

// Callbacks will all be run on the main thread, in engine->update()
void NetworkImpl::update() {
    // process IPC messages
    if(this->ipc_callback) {
        std::vector<std::vector<std::string>> messages;
        {
            Sync::scoped_lock lock{this->ipc_mutex};
            messages = std::move(this->pending_ipc_messages);
            this->pending_ipc_messages.clear();
        }
        for(auto& args : messages) {
            this->ipc_callback(std::move(args));
        }
    }

    // process completed HTTP requests
    {
        std::vector<CompletedRequest> responses_to_handle;
        {
            Sync::scoped_lock lock{this->completed_requests_mutex};
            responses_to_handle = std::move(this->completed_requests);
            this->completed_requests.clear();
        }
        for(auto& completed : responses_to_handle) {
            // cancelled after completing but before delivery (both this and request_stop run on the
            // main thread, so this is race-free): drop it without invoking the callback
            if(completed.cancel_token.stop_requested()) continue;
            completed.callback(std::move(completed.response));
        }
    }

    // websocket I/O is handled on the network thread (websocketSend)
}

void NetworkImpl::httpRequestAsync(std::string_view url, RequestOptions options, AsyncCallback callback) {
    const bool schemePrepended = url.starts_with("https://") || url.starts_with("http://") ||
                                 url.starts_with("wss://") ||
                                 url.starts_with("ws://");  // should normally not already be prefixed, but allow it

    std::string urlWithScheme =
        schemePrepended ? std::string{url} : fmt::format("{}{}", cv::use_https.getBool() ? "https://" : "http://", url);

    auto request = std::make_unique<Request>(std::move(urlWithScheme), std::move(options), std::move(callback));

    {
        Sync::scoped_lock lock{this->request_queue_mutex};
        this->pending_requests.push(std::move(request));
    }
    curl_multi_wakeup(this->multi_handle);
}

std::shared_ptr<WSInstance> NetworkImpl::initWebsocket(std::string_view url, const WSOptions& options) {
    assert(!url.starts_with("ws://") && !url.starts_with("wss://") && !url.starts_with("http://") &&
           !url.starts_with("https://"));

    std::string urlWithScheme = fmt::format("{}{}", cv::use_https.getBool() ? "wss://" : "ws://", url);

    auto websocket = std::make_shared<WSInstance>();
    websocket->max_recv = options.max_recv;
    websocket->time_created = engine->getTime();

    RequestOptions httpOptions{.headers = options.headers,
                               .user_agent = options.user_agent,
                               .timeout = 0,  // websockets are long-lived; only connect_timeout applies
                               .connect_timeout = options.connect_timeout,
                               .flags = RequestOptions::WEBSOCKET};

    auto request = std::make_unique<Request>(std::move(urlWithScheme), std::move(httpOptions));
    request->websocket = websocket;

    {
        Sync::scoped_lock lock{this->request_queue_mutex};
        this->pending_requests.push(std::move(request));
    }
    curl_multi_wakeup(this->multi_handle);

    return websocket;
}

// synchronous API (blocking)
Response NetworkImpl::httpRequestSynchronous(std::string_view url, RequestOptions options) {
    const bool schemePrepended = url.starts_with("https://") || url.starts_with("http://") ||
                                 url.starts_with("wss://") ||
                                 url.starts_with("ws://");  // should normally not already be prefixed, but allow it

    std::string urlWithScheme =
        schemePrepended ? std::string{url} : fmt::format("{}{}", cv::use_https.getBool() ? "https://" : "http://", url);

    Response result;
    Sync::condition_variable cv;
    Sync::mutex cv_mutex;

    void* sync_id = &cv;

    // register sync request
    {
        Sync::scoped_lock lock{this->sync_requests_mutex};
        this->sync_request_cvs[sync_id] = &cv;
    }

    // create sync request
    auto request = std::make_unique<Request>(std::move(urlWithScheme), std::move(options));
    request->is_sync = true;
    request->sync_id = sync_id;

    // submit request
    {
        Sync::scoped_lock lock{this->request_queue_mutex};
        this->pending_requests.push(std::move(request));
    }
    curl_multi_wakeup(this->multi_handle);

    // wait for completion
    Sync::unique_lock lock{cv_mutex};
    cv.wait(lock, [&] {
        Sync::scoped_lock sync_lock{this->sync_requests_mutex};
        return this->sync_responses.find(sync_id) != this->sync_responses.end();
    });

    // get result and cleanup
    {
        Sync::scoped_lock sync_lock{this->sync_requests_mutex};
        result = this->sync_responses[sync_id];
        this->sync_responses.erase(sync_id);
        this->sync_request_cvs.erase(sync_id);
    }

    return result;
}

void NetworkImpl::setIPCSocket(int fd, IPCCallback callback) {
    this->ipc_socket_fd.store(fd, std::memory_order_release);
    this->ipc_callback = std::move(callback);
    curl_multi_wakeup(this->multi_handle);
}

void NetworkImpl::handleIPCConnection([[maybe_unused]] int ipc_fd) {
#if defined(MCENGINE_PLATFORM_LINUX) || defined(MCENGINE_PLATFORM_MACOS)
    int client_fd = accept(ipc_fd, nullptr, nullptr);
    if(client_fd < 0) return;

    // read the data: format is [total_size:4 bytes][null-separated strings]
    u32 total_size = 0;
    ssize_t n = recv(client_fd, &total_size, sizeof(total_size), MSG_WAITALL);
    if(n != sizeof(total_size) || total_size == 0 || total_size > 4096) {
        close(client_fd);
        return;
    }

    std::vector<char> buffer(total_size);
    n = recv(client_fd, buffer.data(), total_size, MSG_WAITALL);
    if(n != static_cast<ssize_t>(total_size)) {
        close(client_fd);
        return;
    }

    // send acknowledgment
    char ack = 1;
    send(client_fd, &ack, 1, 0);
    close(client_fd);

    // parse null-separated strings
    std::vector<std::string> args;
    const char* start = buffer.data();
    const char* end = buffer.data() + buffer.size();
    while(start < end) {
        size_t len = strnlen(start, end - start);
        if(len > 0) {
            args.emplace_back(start, len);
        }
        start += len + 1;
    }

    if(!args.empty()) {
        Sync::scoped_lock lock{this->ipc_mutex};
        this->pending_ipc_messages.push_back(std::move(args));
    }
#endif
}

// passthrough to implementation ctor/dtor
NetworkHandler::NetworkHandler() : pImpl() {}
NetworkHandler::~NetworkHandler() = default;

Response NetworkHandler::httpRequestSynchronous(std::string_view url, RequestOptions options) {
    return pImpl->httpRequestSynchronous(url, std::move(options));
}

void NetworkHandler::httpRequestAsync(std::string_view url, RequestOptions options, AsyncCallback callback) {
    return pImpl->httpRequestAsync(url, std::move(options), std::move(callback));
}

std::shared_ptr<WSInstance> NetworkHandler::initWebsocket(std::string_view url, const WSOptions& options) {
    return pImpl->initWebsocket(url, options);
}

void NetworkHandler::setIPCSocket(int fd, IPCCallback callback) { return pImpl->setIPCSocket(fd, std::move(callback)); }

void NetworkHandler::update() { return pImpl->update(); }

}  // namespace Mc::Net

#endif  // !MCENGINE_PLATFORM_WASM
