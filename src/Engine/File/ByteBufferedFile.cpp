// Copyright (c) 2024, kiwec & 2025, WH, All rights reserved.
#include "ByteBufferedFile.h"

#include "Logging.h"
#include "File.h"
#include "SyncMutex.h"
#include "MD5Hash.h"

#include "fmt/format.h"

#include <system_error>
#include <cassert>
#include <vector>

#ifdef MCENGINE_PLATFORM_WINDOWS
#include "WinDebloatDefs.h"
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace ByteBufferedFile {
namespace {

namespace fs = std::filesystem;
constexpr uSz NUM_FILE_LOCKS = 16;

std::array<Sync::shared_mutex, NUM_FILE_LOCKS> file_locks;
uSz path_to_lock_index(std::string_view path) noexcept { return std::hash<std::string_view>{}(path) % NUM_FILE_LOCKS; }
}  // namespace

namespace detail {

FastIFStream::~FastIFStream() noexcept {
#ifdef MCENGINE_PLATFORM_WINDOWS
    if(!!this->handle) ::CloseHandle(this->handle);
#else
    if(this->handle >= 0) ::close(this->handle);
#endif
}

// open the file and stat its size.
// success returns filesize > 0
std::pair<uSz, std::string> FastIFStream::open_file(const fs::path &read_path) noexcept {
    std::pair<uSz, std::string> ret{0, ""};

#ifdef MCENGINE_PLATFORM_WINDOWS
    HANDLE h = CreateFileW(read_path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);

    if(h == INVALID_HANDLE_VALUE) {
        ret.second = fmt::format("open failed: {:#x}", GetLastError());
        return ret;
    }
    LARGE_INTEGER sz;
    if(!GetFileSizeEx(h, &sz)) {
        ret.second = fmt::format("cannot obtain size: {:#x}", GetLastError());
        CloseHandle(h);
        return ret;
    }
    this->handle = h;
    ret.first = static_cast<uSz>(sz.QuadPart);
    return ret;
#else
    int fd = open(read_path.c_str(), O_RDONLY | O_CLOEXEC);
    if(fd < 0) {
        ret.second = fmt::format("open failed: {:s}", std::generic_category().message(errno));
        return ret;
    }
    struct stat st{};
    if(fstat(fd, &st) != 0) {
        ret.second = fmt::format("cannot obtain size: {:s}", std::generic_category().message(errno));
        close(fd);
        return ret;
    }
    this->handle = fd;
    ret.first = static_cast<uSz>(st.st_size);
    return ret;
#endif
}

uSz FastIFStream::read_at(uSz offset, u8 *out, uSz n) noexcept {
#ifdef MCENGINE_PLATFORM_WINDOWS
    assert(!!this->handle);
#else
    assert(this->handle >= 0);
#endif

    uSz total = 0;
#ifdef MCENGINE_PLATFORM_WINDOWS
    while(total < n) {
        OVERLAPPED ov{};
        u64 off = static_cast<u64>(offset) + total;
        ov.Offset = static_cast<DWORD>(off & 0xFFFFFFFFu);
        ov.OffsetHigh = static_cast<DWORD>(off >> 32);
        DWORD want = static_cast<DWORD>(std::min<uSz>(n - total, 0xFFFFFFFFu));
        DWORD got = 0;
        if(!ReadFile(this->handle, out + total, want, &got, &ov)) break;  // incl. ERROR_HANDLE_EOF
        if(got == 0) break;                                               // EOF
        total += got;
    }
#else
    while(total < n) {
        sSz r = pread(this->handle, out + total, n - total, static_cast<off_t>(offset) + static_cast<off_t>(total));
        if(r < 0) {
            if(errno == EINTR) continue;
            break;  // hard error: treat as a short read
        }
        if(r == 0) break;  // EOF
        total += static_cast<uSz>(r);
    }
#endif
    return total;
}

}  // namespace detail

using namespace detail;

Reader::Reader(std::string_view readPath_param) noexcept
    : buffer(std::make_unique_for_overwrite<u8[]>(READ_BUFFER_SIZE)), read_path(readPath_param) {
    file_locks[path_to_lock_index(this->read_path)].lock_shared();
    auto [size, err] = this->file.open_file(File::getFsPath(this->read_path));
    if(size == 0 && !err.empty()) {
        this->set_error(err);
        debugLog("File {:s} error: {:s}", this->read_path, err);
    }
    this->total_size = size;
}

Reader::~Reader() noexcept { file_locks[path_to_lock_index(this->read_path)].unlock_shared(); }

// need >= DIRECT_READ_THRESHOLD bytes and the ring can't satisfy it.
// drain whatever the ring holds, then read the remainder straight into the caller's buffer.
neverinline uSz Reader::read_bytes_direct(u8 *out, uSz len) noexcept {
    uSz from_ring = this->buffered_bytes;
    if(from_ring > 0) {
        if(this->read_pos + from_ring <= READ_BUFFER_SIZE) {
            memcpy(out, &this->buffer[this->read_pos], from_ring);
        } else {
            uSz first_part = READ_BUFFER_SIZE - this->read_pos;
            memcpy(out, &this->buffer[this->read_pos], first_part);
            memcpy(out + first_part, &this->buffer[0], from_ring - first_part);
        }
    }

    uSz head = this->total_pos + from_ring;  // next unread file byte (the ring held [total_pos, head))
    uSz total = from_ring + this->file.read_at(head, out + from_ring, len - from_ring);

    // the ring is now fully consumed
    this->read_pos = 0;
    this->write_pos = 0;
    this->buffered_bytes = 0;

    if(total == 0) {
        memset(out, 0, len);  // EOF: keep the documented zero-fill-on-EOF contract
        return 0;
    }
    this->total_pos += total;
    return total;
}

// noinline + out-of-line so read<T>'s fast path stays small enough to inline into the parse call sites.
neverinline void Reader::read_fill(u8 *out, uSz len) noexcept {
    if(this->read_bytes(out, len) != len) {
        memset(out, 0, len);
    }
}

void Reader::set_error(const std::string &error_msg) noexcept {
    if(!this->error_flag) {  // only set first error
        this->error_flag = true;
        this->last_error = error_msg;
    }
}

void Reader::set_oversized_error(uSz attempted) noexcept {
    this->set_error(
        fmt::format("Attempted to read {:d} bytes (exceeding buffer size {:d})", attempted, READ_BUFFER_SIZE));
}

// TODO: error handling is wildly incorrect/dubious
bool Reader::read_hash_chars(MD5String &inout) noexcept {
    if(this->error_flag) {
        return false;
    }

    u8 empty_check = this->read<u8>();
    if(empty_check == 0) {
        return false;
    }

    bool success = true;

    u32 len = this->read_uleb128();
    u32 extra = 0;
    if(len > 32) {
        // just continue, don't set error flag
        debugLog("WARNING: Expected 32 bytes for hash string, got {}!", len);
        extra = len - 32;
        len = 32;

        success = false;
    }

    assert(len <= 32);  // shut up gcc PLEASE
    if(this->read_bytes(reinterpret_cast<u8 *>(inout.data()), len) != len) {
        // just continue, don't set error flag
        debugLog("WARNING: failed to read {} bytes to obtain hash string.", len);
        extra = len;

        success = false;
    }

    this->skip_bytes(extra);

    return success;
}

bool Reader::read_hash_chars(MD5Hash &inout) noexcept {
    MD5String temp;
    bool ret = read_hash_chars(temp);
    inout = temp;
    return ret;
}

bool Reader::read_hash_digest(MD5Hash &inout) noexcept {
    if(this->error_flag) {
        return false;
    }

    u8 empty_check = this->read<u8>();
    if(empty_check == 0) {
        return false;
    }

    bool success = true;

    u32 len = this->read_uleb128();
    u32 extra = 0;
    if(len > 16) {
        // just continue, don't set error flag
        debugLog("WARNING: Expected 16 bytes for hash digest, got {}!", len);
        extra = len - 16;
        len = 16;

        success = false;
    }

    assert(len <= 16);  // shut up gcc PLEASE
    if(this->read_bytes(reinterpret_cast<u8 *>(inout.data()), len) != len) {
        // just continue, don't set error flag
        debugLog("WARNING: failed to read {} bytes to obtain hash digest.", len);
        extra = len;

        success = false;
    }

    this->skip_bytes(extra);

    return success;
}

bool Reader::read_string(std::string &inout) noexcept {
    if(this->error_flag) {
        return false;
    }

    u8 empty_check = this->read<u8>();
    if(empty_check == 0) return false;

    u32 len = this->read_uleb128();
    if(len > READ_BUFFER_SIZE) {
        // a corrupt length would otherwise request a multi-GiB allocation (and under
        // -fno-exceptions a failing new is std::terminate, not a catchable error); read_bytes can't
        // return more than the ring buffer anyway, so this rejects nothing legitimate.
        inout.clear();
        this->set_oversized_error(len);
        return false;
    }

    inout.resize_and_overwrite(
        len, [this](char *data, uSz size) -> uSz { return this->read_bytes(reinterpret_cast<u8 *>(data), size); });

    if(inout.size() != len) {
        inout.clear();
        this->set_error(fmt::format("Failed to read {:d} bytes for string", len));
        return false;
    }

    return true;
}

std::string Reader::read_string() noexcept {
    std::string str;
    this->read_string(str);
    return str;
}

bool Reader::read_cstring(std::unique_ptr<char[]> &inout) noexcept {
    bool success = false;
    u8 empty_check = 0;
    u32 len = 0;
    uSz read = 0;
    if(this->error_flag) {
        goto out;
    }

    empty_check = this->read<u8>();
    if(empty_check == 0) goto out;

    len = this->read_uleb128();
    if(len > READ_BUFFER_SIZE) {
        // reject before allocating: len+1 would wrap a u32 at UINT32_MAX (0-byte alloc, then
        // an oversized read_bytes overruns it), and read_bytes can't return more than the buffer.
        this->set_oversized_error(len);
        goto out;
    }
    inout = std::make_unique_for_overwrite<char[]>(static_cast<uSz>(len) + 1);
    read = this->read_bytes(reinterpret_cast<u8 *>(inout.get()), len);

    inout[len] = '\0';

    if(read != len) {
        this->set_error(fmt::format("Failed to read {:d} bytes for string", len));
        goto out;
    }
    success = true;
out:
    if(!success) {
        inout = std::make_unique<char[]>(1);
    }
    return success;
}

std::unique_ptr<char[]> Reader::read_cstring() noexcept {
    std::unique_ptr<char[]> str;
    this->read_cstring(str);
    return str;
}

u32 Reader::read_uleb128() noexcept {
    if(this->error_flag) {
        return 0;
    }

    u32 result = 0;
    u32 shift = 0;
    u8 byte = 0;

    do {
        byte = this->read<u8>();
        result |= static_cast<u32>(byte & 0x7f) << shift;
        shift += 7;
    } while((byte & 0x80) && shift < 32);  // stop after 5 bytes; shift >= 32 would be UB

    return result;
}

void Reader::skip_string() noexcept {
    if(this->error_flag) {
        return;
    }

    u8 empty_check = this->read<u8>();
    if(empty_check == 0) return;

    u32 len = this->read_uleb128();
    this->skip_bytes(len);
}

Writer::Writer(std::string_view writePath_param)
    : buffer(std::make_unique_for_overwrite<u8[]>(WRITE_BUFFER_SIZE)), write_path(writePath_param) {
    file_locks[path_to_lock_index(this->write_path)].lock();

    auto path = File::getFsPath(this->write_path);
    this->file_path = path;

    this->tmp_file_path = this->file_path;
    if constexpr(!Env::cfg(OS::WASM)) {
        // on WASM/IDBFS, just write directly to the final path.
        // the .tmp+rename pattern doesn't trigger IDBFS autoPersist, so the rename
        // never gets synced to IndexedDB and the file is lost on next page load.
        this->tmp_file_path += ".tmp";
    }

    this->file.open(this->tmp_file_path, std::ios::binary);
    if(!this->file.is_open()) {
        this->set_error(fmt::format("Failed to open file for writing: {:s}", std::generic_category().message(errno)));
        debugLog("Failed to open '{:s}': {:s}", this->write_path, std::generic_category().message(errno).c_str());
        return;
    }
}

Writer::~Writer() {
    if(this->file.is_open()) {
        this->flush();
        this->file.close();

        if(!this->error_flag && this->tmp_file_path != this->file_path) {
            std::error_code ec;
            fs::remove(this->file_path, ec);  // Windows (the Microsoft docs are LYING)
            fs::rename(this->tmp_file_path, this->file_path, ec);
            if(ec) {
                // can't set error in destructor, but log it
                debugLog("Failed to rename temporary file: {:s}", ec.message().c_str());
            }
        }
    }

    file_locks[path_to_lock_index(this->write_path)].unlock();
}

void Writer::set_error(const std::string &error_msg) noexcept {
    if(!this->error_flag) {  // only set first error
        this->error_flag = true;
        this->last_error = error_msg;
    }
}

void Writer::write_hash_chars(const MD5String &hash_str) noexcept {
    if(this->error_flag) {
        return;
    }

    this->write<u8>(0x0B);
    this->write<u8>(hash_str.size());
    this->write_bytes(reinterpret_cast<const u8 *>(hash_str.data()), hash_str.size());
}

void Writer::write_hash_digest(const MD5Hash &hash_digest) noexcept {
    if(this->error_flag) {
        return;
    }

    this->write<u8>(0x0B);
    this->write<u8>(hash_digest.size());
    this->write_bytes(reinterpret_cast<const u8 *>(hash_digest.data()), hash_digest.size());
}

void Writer::flush() noexcept {
    if(this->error_flag || !this->file.is_open()) {
        return;
    }

    this->file.write(reinterpret_cast<const char *>(&this->buffer[0]), static_cast<std::streamsize>(this->pos));
    if(this->file.fail()) {
        this->set_error(fmt::format("Failed to write to file: {:s}", std::generic_category().message(errno)));
        return;
    }
    this->pos = 0;
}

void Writer::write_bytes(const u8 *bytes, uSz n) noexcept {
    if(this->error_flag || !this->file.is_open()) {
        return;
    }

    if(this->pos + n > WRITE_BUFFER_SIZE) {
        this->flush();
        if(this->error_flag) {
            return;
        }
    }

    if(this->pos + n > WRITE_BUFFER_SIZE) {
        this->set_error(
            fmt::format("Attempted to write {:d} bytes (exceeding buffer size {:d})", n, WRITE_BUFFER_SIZE));
        return;
    }

    memcpy(&this->buffer[this->pos], bytes, n);
    this->pos += n;
}

void Writer::write_uleb128(u32 num) noexcept {
    if(this->error_flag) {
        return;
    }

    if(num == 0) {
        u8 zero = 0;
        this->write<u8>(zero);
        return;
    }

    while(num != 0) {
        u8 next = num & 0x7F;
        num >>= 7;
        if(num != 0) {
            next |= 0x80;
        }
        this->write<u8>(next);
    }
}

bool Writer::write_string_isnull(const char *str) noexcept {
    if(this->error_flag) {
        return true;
    }

    if(!str || str[0] == '\0') {
        const u8 zero = 0;
        this->write<u8>(zero);
        return true;
    }

    return false;
}

void Writer::write_string_nonnull(const char *str, uSz len) noexcept {
    u8 empty_check = 0x0B;
    this->write<u8>(empty_check);

    this->write_uleb128(len);
    this->write_bytes(reinterpret_cast<const u8 *>(str), len);
}
}  // namespace ByteBufferedFile
