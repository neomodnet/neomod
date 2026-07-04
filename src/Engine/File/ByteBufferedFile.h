// Copyright (c) 2024, kiwec & 2025, WH, All rights reserved.
#pragma once
#include "config.h"

#include <cassert>
#include <cstring>
#include <string>
#include <string_view>
#include <algorithm>
#include <filesystem>
#include <fstream>

#include "noinclude.h"
#include "types.h"

struct MD5String;
struct MD5Hash;

// don't do something stupid like:
// Writer("pathA");
// Reader("pathA");
// because that would deadlock.

// clang annoyingly inlines this stuff too eagerly and slows down compilation
// (even without __attribute__((always_inline)) )
#if defined(_DEBUG) && (defined(__GNUC__) || defined(__clang__))
#define default_inline_attr inline __attribute__((__noinline__))
#else
#define default_inline_attr forceinline
#endif

namespace ByteBufferedFile {
namespace detail {
inline constexpr const uSz READ_BUFFER_SIZE{4ULL * 1024 * 1024};
inline constexpr const uSz WRITE_BUFFER_SIZE{4ULL * 1024 * 1024};
inline constexpr const uSz DIRECT_READ_THRESHOLD{16ULL * 1024};

class FastIFStream final {
   public:
    FastIFStream() noexcept = default;
    ~FastIFStream() noexcept;

    FastIFStream(const FastIFStream &) = delete;
    FastIFStream &operator=(const FastIFStream &) = delete;
    FastIFStream(FastIFStream &&) noexcept = default;
    FastIFStream &operator=(FastIFStream &&) noexcept = default;

    // open + size the file, and read `n` bytes at an absolute `offset` into `out`,
    // returning the count actually read (0 = EOF).
    uSz read_at(uSz offset, u8 *out, uSz n) noexcept;

    // open the file and stat its size, success returns filesize > 0
    std::pair<uSz /*filesize*/, std::string /*error if filesize == 0*/> open_file(
        const std::filesystem::path &read_path) noexcept;

   private:
#ifdef MCENGINE_PLATFORM_WINDOWS
    void *handle{nullptr};
#else
    int handle{-1};
#endif
};
}  // namespace detail

class Reader {
    NOCOPY_NOMOVE(Reader)
   public:
    Reader() = delete;
    Reader(std::string_view readPath) noexcept;
    ~Reader() noexcept;

    [[nodiscard]] default_inline_attr uSz read_bytes(u8 *out, uSz len) noexcept {
        using namespace detail;
        assert(!!out);

        if(this->error_flag) {
            return 0;
        }

        if(len > READ_BUFFER_SIZE) {
            this->set_oversized_error(len);
            return 0;
        }

        // the ring can't satisfy the request: either refill it, or (for a bulk read) bypass it.
        if(this->buffered_bytes < len) {
            // faster to do this draining the ring remainder and reading the rest
            // straight into the caller's buffer (instead of double-memcpying)
            if(len >= DIRECT_READ_THRESHOLD) {
                return this->read_bytes_direct(out, len);
            }
            // the next unread file byte:
            // bytes consumed (total_pos) + bytes still buffered = bytes pulled from the file so far.
            uSz head = this->total_pos + this->buffered_bytes;
            // calculate available space for reading more data
            uSz available_space = READ_BUFFER_SIZE - this->buffered_bytes;
            uSz bytes_to_read = available_space;

            if(this->write_pos + bytes_to_read <= READ_BUFFER_SIZE) {
                // no wrap needed, read directly
                uSz bytes_read = this->file.read_at(head, &this->buffer[this->write_pos], bytes_to_read);
                this->write_pos = (this->write_pos + bytes_read) % READ_BUFFER_SIZE;
                this->buffered_bytes += bytes_read;
            } else {
                // wrap needed, read in two parts
                uSz first_part = READ_BUFFER_SIZE - this->write_pos;
                uSz bytes_read = this->file.read_at(head, &this->buffer[this->write_pos], first_part);

                if(bytes_read == first_part && bytes_to_read > first_part) {
                    uSz second_part = bytes_to_read - first_part;
                    uSz second_read = this->file.read_at(head + first_part, &this->buffer[0], second_part);
                    bytes_read += second_read;
                    this->write_pos = second_read;
                } else {
                    this->write_pos = (this->write_pos + bytes_read) % READ_BUFFER_SIZE;
                }

                this->buffered_bytes += bytes_read;
            }
        }

        // reached EOF
        if(this->buffered_bytes == 0) {
            memset(out, 0, len);
            return 0;
        }

        // truncated
        if(this->buffered_bytes < len) {
            len = this->buffered_bytes;
        }

        // read from ring buffer
        if(this->read_pos + len <= READ_BUFFER_SIZE) {
            // no wrap needed
            memcpy(out, &this->buffer[this->read_pos], len);
        } else {
            // wrap needed
            uSz first_part = std::min(len, READ_BUFFER_SIZE - this->read_pos);
            uSz second_part = len - first_part;

            memcpy(out, &this->buffer[this->read_pos], first_part);
            memcpy(out + first_part, &this->buffer[0], second_part);
        }

        this->read_pos = (this->read_pos + len) % READ_BUFFER_SIZE;
        this->buffered_bytes -= len;
        this->total_pos += len;

        return len;
    }

    template <typename T>
    [[nodiscard]] forceinline T read() noexcept {
        using namespace detail;
        static_assert(sizeof(T) < READ_BUFFER_SIZE);

        T result;
        // faster to do the simple case directly here
        if(!this->error_flag && this->buffered_bytes >= sizeof(T) && this->read_pos + sizeof(T) < READ_BUFFER_SIZE) {
            memcpy(&result, &this->buffer[this->read_pos], sizeof(T));
            this->read_pos += sizeof(T);
            this->buffered_bytes -= sizeof(T);
            this->total_pos += sizeof(T);
            return result;
        }
        this->read_fill(reinterpret_cast<u8 *>(&result), sizeof(T));
        return result;
    }

    inline void skip_bytes(u32 n) noexcept {
        using namespace detail;
        if(this->error_flag) {
            return;
        }

        // if we can skip entirely within the buffered data
        if(n <= this->buffered_bytes) {
            this->read_pos = (this->read_pos + n) % READ_BUFFER_SIZE;
            this->buffered_bytes -= n;
            this->total_pos += n;
            return;
        }

        // skipping past the ring: advance the logical position and drop the buffered data.
        // the next refill derives its file offset from total_pos + buffered_bytes
        this->total_pos += n;
        this->read_pos = 0;
        this->write_pos = 0;
        this->buffered_bytes = 0;
    }

    template <typename T>
    void skip() noexcept {
        using namespace detail;
        static_assert(sizeof(T) < READ_BUFFER_SIZE);
        this->skip_bytes(sizeof(T));
    }

    [[nodiscard]] constexpr bool good() const noexcept { return !this->error_flag; }
    [[nodiscard]] constexpr std::string_view error() const noexcept { return this->last_error; }

    [[nodiscard]] bool read_hash_chars(MD5String &hash_str_inout) noexcept;  // read into a given buffer directly
    [[nodiscard]] bool read_hash_chars(MD5Hash &hash_digest_inout) noexcept;
    [[nodiscard]] bool read_hash_digest(MD5Hash &hash_digest_inout) noexcept;

    bool read_string(std::string &inout) noexcept;
    [[nodiscard]] std::string read_string() noexcept;

    bool read_cstring(std::unique_ptr<char[]> &inout) noexcept;
    [[nodiscard]] std::unique_ptr<char[]> read_cstring() noexcept;

    [[nodiscard]] u32 read_uleb128() noexcept;

    void skip_string() noexcept;

    // underlying file state getters
    [[nodiscard]] uSz get_total_size() const { return total_size; }
    [[nodiscard]] uSz get_total_pos() const { return total_pos; }

   private:
    uSz total_size{0};
    uSz total_pos{0};

    // out-of-line bulk path for read_bytes
    uSz read_bytes_direct(u8 *out, uSz len) noexcept;

    // out-of-line cold path for read<T>
    void read_fill(u8 *out, uSz len) noexcept;

    void set_error(const std::string &error_msg) noexcept;
    void set_oversized_error(uSz attempted) noexcept;

    std::unique_ptr<u8[]> buffer;
    detail::FastIFStream file;
    std::string read_path;

    uSz read_pos{0};        // current read position in ring buffer
    uSz write_pos{0};       // current write position in ring buffer
    uSz buffered_bytes{0};  // amount of data currently buffered

    bool error_flag{false};
    std::string last_error;
};

class Writer {
    NOCOPY_NOMOVE(Writer)
   public:
    Writer() = delete;
    Writer(std::string_view writePath);
    ~Writer();

    [[nodiscard]] constexpr bool good() const noexcept { return !this->error_flag; }
    [[nodiscard]] constexpr std::string_view error() const noexcept { return this->last_error; }

    void flush() noexcept;
    void write_bytes(const u8 *bytes, uSz n) noexcept;
    void write_hash_chars(const MD5String &hash_str) noexcept;
    void write_hash_digest(const MD5Hash &hash_digest) noexcept;
    void write_uleb128(u32 num) noexcept;
    void write_string(const char *str) noexcept {
        if(this->write_string_isnull(str)) return;
        this->write_string_nonnull(str, strlen(str));
    }
    void write_string(std::string_view strview) noexcept {
        if(strview.empty()) {
            const u8 zero = 0;
            this->write<u8>(zero);
            return;
        }
        this->write_string_nonnull(strview.data(), strview.length());
    }

    template <typename T>
    void write(T t) noexcept {
        this->write_bytes(reinterpret_cast<const u8 *>(&t), sizeof(T));
    }

   private:
    // check for null, if it is, write 0 and return true
    bool write_string_isnull(const char *str) noexcept;
    // unchecked
    void write_string_nonnull(const char *str, uSz len) noexcept;

    void set_error(const std::string &error_msg) noexcept;

    std::unique_ptr<u8[]> buffer;

    std::string write_path;
    std::filesystem::path file_path;
    std::filesystem::path tmp_file_path;
    std::ofstream file;

    uSz pos{0};
    bool error_flag{false};
    std::string last_error;
};
}  // namespace ByteBufferedFile

#undef default_inline_attr
