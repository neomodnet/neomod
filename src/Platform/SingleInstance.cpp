// Copyright (c) 2026, WH, All rights reserved.
#include "SingleInstance.h"

#include "config.h"

#ifdef MCENGINE_PLATFORM_WASM

namespace Mc::SingleInstance {
Claim claim(std::string_view /*name*/, std::span<const std::string> /*args*/, bool /*forward*/) noexcept {
    return Claim::ALONE;
}
std::vector<std::vector<std::string>> take_forwarded() noexcept { return {}; }
void release() noexcept {}
}  // namespace Mc::SingleInstance

#else

#include "BaseEnvironment.h"
#include "Logging.h"
#include "noinclude.h"
#include "SyncJthread.h"
#include "SyncMutex.h"
#include "SyncStoptoken.h"
#include "Thread.h"
#include "Timing.h"

#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <utility>

#ifdef MCENGINE_PLATFORM_WINDOWS
#include "RuntimePlatform.h"
#include "UniString.h"

#include "WinDebloatDefs.h"
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#endif

namespace Mc::SingleInstance {
namespace {

// a launch on the wire: the payload size as a u32, then each argument followed by a null. on windows the instance first
// greets each launch with its process id, which the launch needs to let it come to the front
constexpr u32 MAX_PAYLOAD_SIZE{1u << 20};
// how long a launch keeps trying to reach the instance, and how long the instance waits for one launch's data
constexpr u64 FORWARD_TIMEOUT_MS{5000};
constexpr u32 RECEIVE_TIMEOUT_MS{2000};

Sync::mutex s_forwarded_mutex;
std::vector<std::vector<std::string>> s_forwarded;

enum class Attempt : u8 { OWNER, TAKEN, FAILED };
enum class Delivery : u8 { DELIVERED, RETRY, FAILED };

std::vector<char> encode(std::span<const std::string> args) {
    std::vector<char> msg(sizeof(u32));
    for(const auto &arg : args) {
        msg.insert(msg.end(), arg.c_str(), arg.c_str() + arg.size() + 1);
    }
    const auto size = static_cast<u32>(msg.size() - sizeof(u32));
    std::memcpy(msg.data(), &size, sizeof(size));
    return msg;
}

void receive(std::span<const char> payload) {
    std::vector<std::string> args;
    for(auto it = payload.begin(); it != payload.end();) {
        const auto end = std::find(it, payload.end(), '\0');
        if(end != it) args.emplace_back(it, end);
        it = end == payload.end() ? end : end + 1;
    }

    Sync::scoped_lock lock{s_forwarded_mutex};
    s_forwarded.push_back(std::move(args));
}

u64 remaining_ms(u64 deadline) {
    const u64 now = Timing::getTicksMS();
    return now < deadline ? deadline - now : 0;
}

#ifdef MCENGINE_PLATFORM_WINDOWS

using Endpoint = std::wstring;

// pipe names are machine-wide, so one per session (other users' processes can't write to our pipe anyway)
Endpoint endpoint(std::string_view name) {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    return UniString::to_wide(fmt::format(R"(\\.\pipe\{}-{})", name, session));
}

// finishes an overlapped operation that returned `started`. false if it failed, took longer than `timeout_ms` or
// `stop_event` got set first (it's cancelled then)
bool finish_io(HANDLE h, OVERLAPPED &ov, BOOL started, DWORD timeout_ms, HANDLE stop_event, DWORD *transferred) {
    if(!started && GetLastError() != ERROR_IO_PENDING) return false;
    const std::array waits{ov.hEvent, stop_event};
    if(WaitForMultipleObjects(stop_event ? 2 : 1, waits.data(), FALSE, timeout_ms) != WAIT_OBJECT_0) {
        // all of a handle's i/o comes from one thread here, so CancelIoEx (vista+) isn't needed
        CancelIo(h);
        GetOverlappedResult(h, &ov, transferred, TRUE);
        return false;
    }
    return GetOverlappedResult(h, &ov, transferred, FALSE);
}

bool read_exact(HANDLE h, HANDLE event, char *buf, DWORD size, DWORD timeout_ms, HANDLE stop_event) {
    for(DWORD done = 0, n = 0; done < size; done += n) {
        OVERLAPPED ov{};
        ov.hEvent = event;
        if(!finish_io(h, ov, ReadFile(h, buf + done, size - done, nullptr, &ov), timeout_ms, stop_event, &n) || !n) {
            return false;
        }
    }
    return true;
}

bool write_all(HANDLE h, HANDLE event, const char *buf, DWORD size, DWORD timeout_ms, HANDLE stop_event) {
    for(DWORD done = 0, n = 0; done < size; done += n) {
        OVERLAPPED ov{};
        ov.hEvent = event;
        if(!finish_io(h, ov, WriteFile(h, buf + done, size - done, nullptr, &ov), timeout_ms, stop_event, &n) || !n) {
            return false;
        }
    }
    return true;
}

struct Listener {
    NOCOPY_NOMOVE(Listener)
   public:
    explicit Listener(HANDLE pipe) : pipe(pipe) {
        this->thread = Sync::jthread{[this](const Sync::stop_token &stop) { this->serve(stop); }};
    }
    ~Listener() {
        if(this->thread.joinable()) {
            this->thread.request_stop();
            this->thread.join();
        }
        CloseHandle(this->io_event);
        CloseHandle(this->stop_event);
        CloseHandle(this->pipe);
    }

   private:
    void serve(const Sync::stop_token &stop) {
        McThread::set_current_thread_name("instance_ipc");
        McThread::set_current_thread_prio(McThread::Priority::LOW);
        Sync::stop_callback wake(stop, [this] { SetEvent(this->stop_event); });

        const DWORD pid = GetCurrentProcessId();
        std::vector<char> payload;
        while(!stop.stop_requested()) {
            OVERLAPPED ov{};
            ov.hEvent = this->io_event;
            DWORD unused = 0;
            const BOOL connecting = ConnectNamedPipe(this->pipe, &ov);
            // a launch that connected between two ConnectNamedPipe calls is already there
            if(connecting || GetLastError() != ERROR_PIPE_CONNECTED) {
                if(!finish_io(this->pipe, ov, connecting, INFINITE, this->stop_event, &unused)) {
                    if(stop.stop_requested()) break;
                    // e.g. the launch already closed its end again
                    DisconnectNamedPipe(this->pipe);
                    continue;
                }
            }

            // the launch reads the greeting before it sends anything, so disconnecting after the payload throws
            // nothing unread away
            u32 size = 0;
            if(write_all(this->pipe, this->io_event, reinterpret_cast<const char *>(&pid), sizeof(pid),
                         RECEIVE_TIMEOUT_MS, this->stop_event) &&
               read_exact(this->pipe, this->io_event, reinterpret_cast<char *>(&size), sizeof(size), RECEIVE_TIMEOUT_MS,
                          this->stop_event) &&
               size <= MAX_PAYLOAD_SIZE) {
                payload.resize(size);
                if(read_exact(this->pipe, this->io_event, payload.data(), size, RECEIVE_TIMEOUT_MS, this->stop_event)) {
                    receive(payload);
                }
            }
            DisconnectNamedPipe(this->pipe);
        }
    }

    HANDLE pipe;
    HANDLE io_event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HANDLE stop_event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Sync::jthread thread;
};

Listener *s_listener{nullptr};

Attempt try_own(const Endpoint &pipe_name) {
    // windows xp doesn't support PIPE_REJECT_REMOTE_CLIENTS yet
    const DWORD reject_remote = (RuntimePlatform::current() & RuntimePlatform::WIN_XP) ? 0 : PIPE_REJECT_REMOTE_CLIENTS;
    // creating the first instance of a pipe name only succeeds for one process, until all its handles are closed
    HANDLE pipe =
        CreateNamedPipeW(pipe_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | reject_remote, 1, 4096, 4096, 0, nullptr);
    if(pipe == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        if(err == ERROR_ACCESS_DENIED || err == ERROR_PIPE_BUSY) return Attempt::TAKEN;
        debugLog("can't create the instance pipe: error {}", err);
        return Attempt::FAILED;
    }
    s_listener = new Listener(pipe);
    return Attempt::OWNER;
}

Delivery deliver(const Endpoint &pipe_name, std::span<const char> msg, u64 deadline) {
    HANDLE pipe = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED, nullptr);
    if(pipe == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        if(err == ERROR_PIPE_BUSY) {
            // the instance is busy with another launch
            WaitNamedPipeW(pipe_name.c_str(), static_cast<DWORD>(std::max<u64>(remaining_ms(deadline), 1)));
            return Delivery::RETRY;
        }
        // the instance just quit
        if(err == ERROR_FILE_NOT_FOUND) return Delivery::RETRY;
        debugLog("can't connect to the running instance: error {}", err);
        return Delivery::FAILED;
    }

    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Delivery result = Delivery::RETRY;
    if(DWORD pid = 0; read_exact(pipe, event, reinterpret_cast<char *>(&pid), sizeof(pid),
                                 static_cast<DWORD>(remaining_ms(deadline)), nullptr)) {
        // the instance should come to the front, and only a process the user just started may allow that
        AllowSetForegroundWindow(pid);
        if(write_all(pipe, event, msg.data(), static_cast<DWORD>(msg.size()),
                     static_cast<DWORD>(remaining_ms(deadline)), nullptr)) {
            result = Delivery::DELIVERED;
        }
    }
    CloseHandle(event);
    CloseHandle(pipe);
    return result;
}

#else  // linux, macos

constexpr bool ABSTRACT_SOCKETS{Env::cfg(OS::LINUX)};

#ifdef MSG_NOSIGNAL
constexpr int SEND_FLAGS{MSG_NOSIGNAL};
#else
constexpr int SEND_FLAGS{0};
#endif

struct Endpoint {
    sockaddr_un addr{};
    socklen_t len{0};
    // pathname sockets outlive a crashed owner, so ownership is a lock on this file instead of the bind
    std::string lock_path;
};

// linux: abstract socket (leading null in sun_path), per uid since that namespace is shared by all users.
// macos: pathname socket in $TMPDIR (or /tmp), per uid.
Endpoint endpoint(std::string_view name) {
    Endpoint ep{};
    ep.addr.sun_family = AF_UNIX;

    std::string path;
    if constexpr(ABSTRACT_SOCKETS) {
        path = fmt::format("{}{}-{}", '\0', name, getuid());
    } else {
        std::string_view tmpdir = std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "";
        if(tmpdir.empty()) tmpdir = "/tmp";
        while(tmpdir.size() > 1 && tmpdir.back() == '/') tmpdir.remove_suffix(1);

        path = fmt::format("{}/{}-{}.sock", tmpdir, name, getuid());
        ep.lock_path = fmt::format("{}/{}-{}.lock", tmpdir, name, getuid());
        path.push_back('\0');
    }

    if(path.size() > sizeof(ep.addr.sun_path)) {
        debugLog("instance socket path too long: {}", path);
        return {};
    }
    std::memcpy(&ep.addr.sun_path[0], path.data(), path.size());
    ep.len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size());
    return ep;
}

// a socket that doesn't leak into child processes and can't raise SIGPIPE
int make_socket() {
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd < 0) return fd;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    const int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    return fd;
}

void set_timeouts(int fd, u64 ms) {
    const timeval tv{.tv_sec = static_cast<time_t>(ms / 1000), .tv_usec = static_cast<suseconds_t>((ms % 1000) * 1000)};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

bool recv_exact(int fd, char *buf, size_t size) {
    for(size_t done = 0; done < size;) {
        const ssize_t n = recv(fd, buf + done, size - done, 0);
        if(n > 0) {
            done += static_cast<size_t>(n);
        } else if(n == 0 || errno != EINTR) {
            return false;
        }
    }
    return true;
}

bool send_all(int fd, const char *buf, size_t size) {
    for(size_t done = 0; done < size;) {
        const ssize_t n = send(fd, buf + done, size - done, SEND_FLAGS);
        if(n > 0) {
            done += static_cast<size_t>(n);
        } else if(n == 0 || errno != EINTR) {
            return false;
        }
    }
    return true;
}

struct Listener {
    NOCOPY_NOMOVE(Listener)
   public:
    Listener(int sock, int lock_fd, std::string sock_path, std::array<int, 2> wake)
        : sock(sock), lock_fd(lock_fd), sock_path(std::move(sock_path)), wake(wake) {
        this->thread = Sync::jthread{[this](const Sync::stop_token &stop) { this->serve(stop); }};
    }
    ~Listener() {
        if(this->thread.joinable()) {
            this->thread.request_stop();
            this->thread.join();
        }
        // remove the socket before giving up the lock, the next owner unlinks whatever it finds there
        if(!this->sock_path.empty()) unlink(this->sock_path.c_str());
        close(this->sock);
        if(this->lock_fd >= 0) close(this->lock_fd);
        close(this->wake[0]);
        close(this->wake[1]);
    }

   private:
    void serve(const Sync::stop_token &stop) {
        McThread::set_current_thread_name("instance_ipc");
        McThread::set_current_thread_prio(McThread::Priority::LOW);
        Sync::stop_callback wake_cb(stop, [this] {
            const char b = 0;
            (void)!write(this->wake[1], &b, 1);
        });

        std::vector<char> payload;
        while(!stop.stop_requested()) {
            std::array fds{pollfd{.fd = this->sock, .events = POLLIN, .revents = 0},
                           pollfd{.fd = this->wake[0], .events = POLLIN, .revents = 0}};
            if(poll(fds.data(), fds.size(), -1) < 0) {
                if(errno == EINTR) continue;
                debugLog("instance socket poll failed: {}", strerror(errno));
                break;
            }
            if(fds[1].revents) break;
            if(fds[0].revents & ~POLLIN) {
                debugLog("instance socket error: {:#x}", fds[0].revents);
                break;
            }

            // the listening socket is non-blocking, so a launch that already gave up doesn't block us here
            const int client = accept(this->sock, nullptr, nullptr);
            if(client < 0) continue;
            fcntl(client, F_SETFD, FD_CLOEXEC);
            // macos hands out accepted sockets non-blocking like the listening one
            fcntl(client, F_SETFL, fcntl(client, F_GETFL) & ~O_NONBLOCK);
            set_timeouts(client, RECEIVE_TIMEOUT_MS);

            u32 size = 0;
            if(recv_exact(client, reinterpret_cast<char *>(&size), sizeof(size)) && size <= MAX_PAYLOAD_SIZE) {
                payload.resize(size);
                if(recv_exact(client, payload.data(), size)) {
                    receive(payload);
                }
            }
            close(client);
        }
    }

    int sock;
    int lock_fd;
    std::string sock_path;
    std::array<int, 2> wake;
    Sync::jthread thread;
};

Listener *s_listener{nullptr};

Attempt try_own(const Endpoint &ep) {
    if(!ep.len) return Attempt::FAILED;

    int lock_fd = -1;
    if constexpr(!ABSTRACT_SOCKETS) {
        lock_fd = open(ep.lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if(lock_fd < 0) {
            debugLog("can't open {}: {}", ep.lock_path, strerror(errno));
            return Attempt::FAILED;
        }
        if(flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
            const int err = errno;
            close(lock_fd);
            if(err == EWOULDBLOCK) return Attempt::TAKEN;
            debugLog("can't lock {}: {}", ep.lock_path, strerror(err));
            return Attempt::FAILED;
        }
        // whoever held the lock before is gone, a socket file it left behind is stale
        unlink(&ep.addr.sun_path[0]);
    }

    const int sock = make_socket();
    std::array<int, 2> wake{-1, -1};
    const auto fail = [&](Attempt result) {
        if(sock >= 0) close(sock);
        if(lock_fd >= 0) close(lock_fd);
        return result;
    };
    if(sock < 0) {
        debugLog("can't create the instance socket: {}", strerror(errno));
        return fail(Attempt::FAILED);
    }
    if(bind(sock, reinterpret_cast<const sockaddr *>(&ep.addr), ep.len) != 0) {
        const int err = errno;
        if(err == EADDRINUSE) return fail(Attempt::TAKEN);
        debugLog("can't bind the instance socket: {}", strerror(err));
        return fail(Attempt::FAILED);
    }
    const std::string sock_path = ABSTRACT_SOCKETS ? std::string{} : std::string{&ep.addr.sun_path[0]};
    if(fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK) != 0 || listen(sock, SOMAXCONN) != 0 ||
       pipe(wake.data()) != 0) {
        debugLog("can't listen on the instance socket: {}", strerror(errno));
        if(!sock_path.empty()) unlink(sock_path.c_str());
        return fail(Attempt::FAILED);
    }
    fcntl(wake[0], F_SETFD, FD_CLOEXEC);
    fcntl(wake[1], F_SETFD, FD_CLOEXEC);

    s_listener = new Listener(sock, lock_fd, sock_path, wake);
    return Attempt::OWNER;
}

Delivery deliver(const Endpoint &ep, std::span<const char> msg, u64 deadline) {
    const int sock = make_socket();
    if(sock < 0) {
        debugLog("can't create a socket: {}", strerror(errno));
        return Delivery::FAILED;
    }
    set_timeouts(sock, std::max<u64>(remaining_ms(deadline), 1));

    Delivery result = Delivery::RETRY;
    if(connect(sock, reinterpret_cast<const sockaddr *>(&ep.addr), ep.len) != 0) {
        const int err = errno;
        // ENOENT/ECONNREFUSED: the instance quit, or hasn't started listening yet. EAGAIN: its backlog is full
        if(err != ENOENT && err != ECONNREFUSED && err != EAGAIN && err != EINTR) {
            debugLog("can't connect to the running instance: {}", strerror(err));
            result = Delivery::FAILED;
        }
    } else if(send_all(sock, msg.data(), msg.size())) {
        result = Delivery::DELIVERED;
    }
    close(sock);
    return result;
}

#endif

}  // namespace

Claim claim(std::string_view name, std::span<const std::string> args, bool forward) noexcept {
    assert(!s_listener);
    const Endpoint ep = endpoint(name);
    const std::vector<char> msg = encode(args);
    if(msg.size() - sizeof(u32) > MAX_PAYLOAD_SIZE) {
        debugLog("launch arguments too long to hand to another instance");
        forward = false;
    }

    const u64 deadline = Timing::getTicksMS() + FORWARD_TIMEOUT_MS;
    while(true) {
        switch(try_own(ep)) {
            case Attempt::OWNER:
                return Claim::OWNER;
            case Attempt::FAILED:
                return Claim::ALONE;
            case Attempt::TAKEN:
                break;
        }
        if(!forward) return Claim::ALONE;

        switch(deliver(ep, msg, deadline)) {
            case Delivery::DELIVERED:
                return Claim::FORWARDED;
            case Delivery::FAILED:
                return Claim::ALONE;
            case Delivery::RETRY:
                break;
        }
        if(remaining_ms(deadline) == 0) {
            debugLog("the running instance didn't take the launch, starting another one");
            return Claim::ALONE;
        }
        Timing::sleepMS(5);
    }
}

std::vector<std::vector<std::string>> take_forwarded() noexcept {
    Sync::unique_lock lock{s_forwarded_mutex, Sync::try_to_lock};
    if(!lock.owns_lock()) return {};
    return std::exchange(s_forwarded, {});
}

void release() noexcept {
    delete s_listener;
    s_listener = nullptr;
}

}  // namespace Mc::SingleInstance

#endif
