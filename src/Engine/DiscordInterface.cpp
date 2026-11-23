// Copyright (c) 2026, kiwec, All rights reserved.
#include "DiscordInterface.h"

#include "config.h"

// the web build has no discord client to talk to
#ifdef MCENGINE_PLATFORM_WASM
namespace Discord {
void set_activity(const DiscordActivity& /*activity*/) {}
void tick() {}
void shutdown() {}
}  // namespace Discord

#else

#include "ConVar.h"
#include "Environment.h"
#include "Logging.h"
#include "Timing.h"
#include "UniString.h"

#include "fmt/format.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

#ifdef MCENGINE_PLATFORM_WINDOWS
#include "WinDebloatDefs.h"
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace Discord {
namespace {

constexpr const char *CLIENT_ID{"1474141308183380181"};

// each frame is the opcode and the payload size as little-endian u32s, followed by a json payload
enum class Op : u32 { HANDSHAKE = 0, FRAME = 1, CLOSE = 2, PING = 3, PONG = 4 };
constexpr size_t HEADER_SIZE{2 * sizeof(u32)};
constexpr u32 MAX_FRAME_SIZE{64 * 1024};
constexpr size_t MAX_BUFFERED{HEADER_SIZE + MAX_FRAME_SIZE};

// discord drops activity updates past 5 per 20 seconds
constexpr u64 SEND_INTERVAL_MS{4000};
constexpr u64 HANDSHAKE_TIMEOUT_MS{10000};
// the delay before reconnecting doubles with each failure, up to the max
constexpr u64 MIN_RETRY_MS{2000};
constexpr u64 MAX_RETRY_MS{60000};

enum class State : u8 { DISCONNECTED, HANDSHAKING, READY };

State state{State::DISCONNECTED};
std::vector<char> recv_buf;
std::optional<DiscordActivity> wanted;
std::optional<DiscordActivity> sent;
u64 next_connect_ms{0};
u64 retry_ms{MIN_RETRY_MS};
u64 next_send_ms{0};
u64 handshake_deadline_ms{0};
u64 nonce{0};

#ifdef MCENGINE_PLATFORM_WINDOWS

// overlapped i/o, so a stuck discord client can't hang the game
HANDLE pipe{INVALID_HANDLE_VALUE};
HANDLE read_event{nullptr};
OVERLAPPED write_ov{};
std::string write_buf;
bool write_pending{false};

i64 pid() { return GetCurrentProcessId(); }

void ipc_close() {
    if(pipe != INVALID_HANDLE_VALUE) {
        if(write_pending) {
            DWORD written = 0;
#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x600
            CancelIoEx(pipe, &write_ov);
#else
            CancelIo(pipe);
#endif
            GetOverlappedResult(pipe, &write_ov, &written, TRUE);
        }
        CloseHandle(pipe);
    }
    if(read_event) CloseHandle(read_event);
    if(write_ov.hEvent) CloseHandle(write_ov.hEvent);
    pipe = INVALID_HANDLE_VALUE;
    read_event = nullptr;
    write_ov = {};
    write_pending = false;
}

bool ipc_open() {
    for(int i = 0; i < 10; i++) {
        const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
        pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                           nullptr);
        if(pipe != INVALID_HANDLE_VALUE) break;
    }
    if(pipe == INVALID_HANDLE_VALUE) return false;

    read_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    write_ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if(!read_event || !write_ov.hEvent) {
        ipc_close();
        return false;
    }
    return true;
}

bool ipc_write(const char *data, size_t size) {
    if(write_pending) {
        DWORD written = 0;
        if(!GetOverlappedResult(pipe, &write_ov, &written, FALSE)) return false;
        write_pending = false;
    }

    write_buf.assign(data, size);
    const HANDLE event = write_ov.hEvent;
    write_ov = {};
    write_ov.hEvent = event;
    if(WriteFile(pipe, write_buf.data(), static_cast<DWORD>(size), nullptr, &write_ov)) return true;
    write_pending = GetLastError() == ERROR_IO_PENDING;
    return write_pending;
}

// appends whatever arrived without waiting for more. false once the pipe broke
bool ipc_read(std::vector<char> &buf) {
    DWORD avail = 0;
    if(!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr)) return false;
    avail = std::min<DWORD>(avail, MAX_BUFFERED - buf.size());
    if(avail == 0) return true;

    const size_t old_size = buf.size();
    buf.resize(old_size + avail);
    OVERLAPPED ov{};
    ov.hEvent = read_event;
    DWORD got = 0;
    if(!ReadFile(pipe, buf.data() + old_size, avail, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) return false;
    if(!GetOverlappedResult(pipe, &ov, &got, TRUE)) return false;
    buf.resize(old_size + got);
    return true;
}

#else

#ifdef MSG_NOSIGNAL
constexpr int SEND_FLAGS{MSG_NOSIGNAL};
#else
constexpr int SEND_FLAGS{0};
#endif

int sock{-1};

i64 pid() { return getpid(); }

void ipc_close() {
    if(sock >= 0) close(sock);
    sock = -1;
}

bool try_connect(const std::string &path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if(path.size() >= sizeof(addr.sun_path)) return false;
    std::memcpy(addr.sun_path, path.c_str(), path.size());

    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd < 0) return false;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    const int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    // a hung discord client fills its backlog, and connecting to a full backlog blocks
    if(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) != 0 ||
       connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return false;
    }
    sock = fd;
    return true;
}

bool ipc_open() {
    std::string dir{"/tmp"};
    for(const char *var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        if(std::string value = Environment::getEnvVariable(var); !value.empty()) {
            dir = std::move(value);
            break;
        }
    }

    // flatpak and snap installs of discord put the socket in their own subdirectory
    for(const char *subdir : {"", "app/com.discordapp.Discord/", "snap.discord/"}) {
        for(int i = 0; i < 10; i++) {
            if(try_connect(fmt::format("{}/{}discord-ipc-{}", dir, subdir, i))) return true;
        }
    }
    return false;
}

bool ipc_write(const char *data, size_t size) {
    // a full socket buffer means discord stopped reading, so a partial write is a failure too
    while(size > 0) {
        const ssize_t n = send(sock, data, size, SEND_FLAGS);
        if(n > 0) {
            data += n;
            size -= static_cast<size_t>(n);
        } else if(n == 0 || errno != EINTR) {
            return false;
        }
    }
    return true;
}

// appends whatever arrived without waiting for more. false once the socket closed or broke
bool ipc_read(std::vector<char> &buf) {
    char chunk[4096];
    while(buf.size() < MAX_BUFFERED) {
        const ssize_t n = recv(sock, chunk, std::min(sizeof(chunk), MAX_BUFFERED - buf.size()), 0);
        if(n > 0) {
            buf.insert(buf.end(), chunk, chunk + n);
        } else if(n == 0) {
            return false;
        } else if(errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        } else if(errno != EINTR) {
            return false;
        }
    }
    return true;
}

#endif

bool send_frame(Op op, std::string_view payload) {
    std::string frame(HEADER_SIZE + payload.size(), '\0');
    const std::array<u32, 2> header{static_cast<u32>(op), static_cast<u32>(payload.size())};
    std::memcpy(frame.data(), header.data(), HEADER_SIZE);
    std::memcpy(frame.data() + HEADER_SIZE, payload.data(), payload.size());
    return ipc_write(frame.data(), frame.size());
}

void disconnect() {
    ipc_close();
    state = State::DISCONNECTED;
    recv_buf.clear();
    sent.reset();
}

void fail(u64 now) {
    if(state == State::READY) logRaw("[Discord] disconnected");
    disconnect();
    next_connect_ms = now + retry_ms;
    retry_ms = std::min(retry_ms * 2, MAX_RETRY_MS);
}

const rapidjson::Value &member(const rapidjson::Value &obj, const char *key) {
    static const rapidjson::Value null;
    if(!obj.IsObject()) return null;
    const auto it = obj.FindMember(key);
    return it != obj.MemberEnd() ? it->value : null;
}

std::string_view str(const rapidjson::Value &value) {
    return value.IsString() ? std::string_view{value.GetString(), value.GetStringLength()} : std::string_view{};
}

i32 num(const rapidjson::Value &value) { return value.IsInt() ? value.GetInt() : 0; }

// false if the connection has to be dropped
bool handle_frame(Op op, std::string_view payload) {
    if(op == Op::PING) return send_frame(Op::PONG, payload);
    if(op != Op::FRAME && op != Op::CLOSE) return true;

    rapidjson::Document doc;
    doc.Parse<rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag>(payload.data(), payload.size());

    if(op == Op::CLOSE) {
        logRaw("[Discord] connection closed ({:d}): {:s}", num(member(doc, "code")), str(member(doc, "message")));
        return false;
    }

    const std::string_view evt = str(member(doc, "evt"));
    const auto &data = member(doc, "data");
    if(evt == "READY") {
        state = State::READY;
        retry_ms = MIN_RETRY_MS;
        logRaw("[Discord] connected as {:s}", str(member(member(data, "user"), "username")));
    } else if(evt == "ERROR") {
        logRaw("[Discord] ERROR ({:d}): {:s}", num(member(data, "code")), str(member(data, "message")));
    }
    return true;
}

// false if the connection has to be dropped
bool handle_frames() {
    size_t pos = 0;
    while(recv_buf.size() - pos >= HEADER_SIZE) {
        std::array<u32, 2> header{};
        std::memcpy(header.data(), recv_buf.data() + pos, HEADER_SIZE);
        if(header[1] > MAX_FRAME_SIZE) return false;
        if(recv_buf.size() - pos - HEADER_SIZE < header[1]) break;
        if(!handle_frame(static_cast<Op>(header[0]), {recv_buf.data() + pos + HEADER_SIZE, header[1]})) return false;
        pos += HEADER_SIZE + header[1];
    }
    recv_buf.erase(recv_buf.begin(), recv_buf.begin() + static_cast<ptrdiff_t>(pos));
    return true;
}

// cuts `text` to `max` code points, ending it with "..." if it was cut
std::string crop(std::string_view text, size_t max) {
    size_t count = 0;
    size_t cut = 0;
    for(size_t i = 0; i < text.size(); i++) {
        if((static_cast<u8>(text[i]) & 0xC0) == 0x80) continue;
        if(count == max - 3) cut = i;
        if(++count > max) return fmt::format("{:s}...", text.substr(0, cut));
    }
    return std::string{text};
}

std::string activity_command(const DiscordActivity *activity) {
    using Writer = rapidjson::Writer<rapidjson::StringBuffer>;
    rapidjson::StringBuffer buf;
    Writer w(buf);
    const auto string = [&w](std::string_view s) {
        const std::string valid = UniString::sanitize_utf8(std::string{s});
        w.String(valid.data(), static_cast<rapidjson::SizeType>(valid.size()));
    };
    const auto text = [&](const char *key, std::string_view value, size_t max) {
        if(value.empty()) return;
        w.Key(key);
        string(crop(value, max));
    };
    // a cropped url would be broken, so drop it if it's too long
    const auto image = [&](const char *key, std::string_view value) {
        if(value.empty() || value.size() > 256) return;
        w.Key(key);
        string(value);
    };

    w.StartObject();
    w.Key("cmd");
    w.String("SET_ACTIVITY");
    w.Key("nonce");
    string(std::to_string(++nonce));
    w.Key("args");
    w.StartObject();
    w.Key("pid");
    w.Int64(pid());
    if(activity) {
        w.Key("activity");
        w.StartObject();
        text("details", activity->details, 128);
        text("state", activity->state, 128);
        if(activity->start_timestamp > 0) {
            w.Key("timestamps");
            w.StartObject();
            w.Key("start");
            w.Int64(activity->start_timestamp);
            w.EndObject();
        }
        w.Key("assets");
        w.StartObject();
        image("large_image", activity->large_image);
        text("large_text", activity->large_text, 128);
        image("small_image", activity->small_image);
        text("small_text", activity->small_text, 128);
        w.EndObject();
        if(activity->party_size > 0 && activity->party_max >= activity->party_size) {
            w.Key("party");
            w.StartObject();
            w.Key("size");
            w.StartArray();
            w.Int(activity->party_size);
            w.Int(activity->party_max);
            w.EndArray();
            w.EndObject();
        }
        const auto valid = [](const DiscordActivity::Button &b) {
            return !b.label.empty() && !b.url.empty() && b.url.size() <= 512;
        };
        if(std::ranges::any_of(activity->buttons, valid)) {
            w.Key("buttons");
            w.StartArray();
            for(const auto &button : activity->buttons) {
                if(!valid(button)) continue;
                w.StartObject();
                text("label", button.label, 32);
                w.Key("url");
                string(button.url);
                w.EndObject();
            }
            w.EndArray();
        }
        w.EndObject();
    }
    w.EndObject();
    w.EndObject();
    return {buf.GetString(), buf.GetSize()};
}

}  // namespace

void set_activity(const DiscordActivity &activity) { wanted = activity; }

void tick() {
    if(!wanted || !cv::rich_presence.getBool()) {
        shutdown();
        return;
    }

    const u64 now = Timing::getTicksMS();
    if(state == State::DISCONNECTED) {
        if(now < next_connect_ms) return;
        if(!ipc_open()) {
            logIf(cv::debug_discord_rpc.getBool(), "no discord client found, retrying in {}ms", retry_ms);
            fail(now);
            return;
        }
        state = State::HANDSHAKING;
        handshake_deadline_ms = now + HANDSHAKE_TIMEOUT_MS;
        if(!send_frame(Op::HANDSHAKE, fmt::format(R"({{"v":1,"client_id":"{:s}"}})", CLIENT_ID))) {
            fail(now);
            return;
        }
    }

    if(!ipc_read(recv_buf) || !handle_frames()) {
        fail(now);
        return;
    }

    if(state == State::HANDSHAKING) {
        if(now >= handshake_deadline_ms) {
            logRaw("[Discord] handshake timed out");
            fail(now);
        }
        return;
    }

    if(sent == wanted || now < next_send_ms) return;
    const std::string command = activity_command(&*wanted);
    logIf(cv::debug_discord_rpc.getBool(), "{:s}", command);
    if(!send_frame(Op::FRAME, command)) {
        fail(now);
        return;
    }
    sent = wanted;
    next_send_ms = now + SEND_INTERVAL_MS;
}

void shutdown() {
    // closing the connection already removes the status, clearing it first doesn't rely on that
    if(state == State::READY && sent) send_frame(Op::FRAME, activity_command(nullptr));
    disconnect();
    // turning it back on connects right away
    next_connect_ms = 0;
    retry_ms = MIN_RETRY_MS;
}

}  // namespace Discord

#endif
