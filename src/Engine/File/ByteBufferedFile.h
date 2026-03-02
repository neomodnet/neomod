// Copyright (c) 2024, kiwec & 2025, WH, All rights reserved.
#pragma once

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
#define never_inline_attr inline __attribute__((__noinline__))
#else
#define default_inline_attr forceinline
#define never_inline_attr inline
#endif

class ByteBufferedFile {
   private:
    static constexpr const uSz READ_BUFFER_SIZE{4ULL * 1024 * 1024};
    static constexpr const uSz WRITE_BUFFER_SIZE{4ULL * 1024 * 1024};

   public:
    class Reader {
        NOCOPY_NOMOVE(Reader)
       public:
        Reader() = delete;
        Reader(std::string_view readPath);
        ~Reader();

        // always_inline is a 2x speedup here
        [[nodiscard]] default_inline_attr uSz read_bytes(u8 *out, uSz len) {
            if(this->error_flag) {
                if(out != nullptr) {
                    memset(out, 0, len);
                }
                return 0;
            }

            if(len > READ_BUFFER_SIZE) {
                this->set_error("Attempted to read " + std::to_string(len) + " bytes (exceeding buffer size " +
                                std::to_string(READ_BUFFER_SIZE) + ")");
                if(out != nullptr) {
                    memset(out, 0, len);
                }
                return 0;
            }

            // make sure the ring buffer has enough data
            if(this->buffered_bytes < len) {
                // calculate available space for reading more data
                uSz available_space = READ_BUFFER_SIZE - this->buffered_bytes;
                uSz bytes_to_read = available_space;

                if(this->write_pos + bytes_to_read <= READ_BUFFER_SIZE) {
                    // no wrap needed, read directly
                    this->file.read(reinterpret_cast<char *>(&this->buffer[this->write_pos]),
                                    static_cast<std::streamsize>(bytes_to_read));
                    uSz bytes_read = this->file.gcount();
                    this->write_pos = (this->write_pos + bytes_read) % READ_BUFFER_SIZE;
                    this->buffered_bytes += bytes_read;
                } else {
                    // wrap needed, read in two parts
                    uSz first_part = READ_BUFFER_SIZE - this->write_pos;
                    this->file.read(reinterpret_cast<char *>(&this->buffer[this->write_pos]),
                                    static_cast<std::streamsize>(first_part));
                    uSz bytes_read = this->file.gcount();

                    if(bytes_read == first_part && bytes_to_read > first_part) {
                        uSz second_part = bytes_to_read - first_part;
                        this->file.read(reinterpret_cast<char *>(&this->buffer[0]),
                                        static_cast<std::streamsize>(second_part));
                        uSz second_read = this->file.gcount();
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
                if(out != nullptr) {
                    memset(out, 0, len);
                }
                return 0;
            }

            // truncated
            if(this->buffered_bytes < len) {
                len = this->buffered_bytes;
            }

            // read from ring buffer
            if(out != nullptr) {
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
            }

            this->read_pos = (this->read_pos + len) % READ_BUFFER_SIZE;
            this->buffered_bytes -= len;
            this->total_pos += len;

            return len;
        }

        template <typename T>
        [[nodiscard]] never_inline_attr T read() {
            static_assert(sizeof(T) < READ_BUFFER_SIZE);

            T result;
            if((this->read_bytes(reinterpret_cast<u8 *>(&result), sizeof(T))) != sizeof(T)) {
                memset(&result, 0, sizeof(T));
            }
            return result;
        }

        default_inline_attr void skip_bytes(u32 n) {
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

            // we need to skip more than what's buffered
            u32 skip_from_buffer = this->buffered_bytes;
            u32 skip_from_file = n - skip_from_buffer;

            // skip what's in the buffer
            this->total_pos += skip_from_buffer;

            // seek in the file to skip the rest
            this->file.seekg(skip_from_file, std::ios::cur);
            if(this->file.fail()) {
                this->set_error("Failed to seek " + std::to_string(skip_from_file) + " bytes");
                return;
            }

            this->total_pos += skip_from_file;

            // since we've moved past buffered data, reset buffer state
            this->read_pos = 0;
            this->write_pos = 0;
            this->buffered_bytes = 0;
        }

        template <typename T>
        never_inline_attr void skip() {
            static_assert(sizeof(T) < READ_BUFFER_SIZE);
            this->skip_bytes(sizeof(T));
        }

        [[nodiscard]] constexpr bool good() const { return !this->error_flag; }
        [[nodiscard]] constexpr std::string_view error() const { return this->last_error; }

        [[nodiscard]] bool read_hash_chars(MD5String &hash_str_inout);  // read into a given buffer directly
        [[nodiscard]] bool read_hash_chars(MD5Hash &hash_digest_inout);
        [[nodiscard]] bool read_hash_digest(MD5Hash &hash_digest_inout);

        bool read_string(std::string &inout);
        [[nodiscard]] std::string read_string();

        bool read_cstring(std::unique_ptr<char[]> &inout);
        [[nodiscard]] std::unique_ptr<char[]> read_cstring();

        [[nodiscard]] u32 read_uleb128();

        void skip_string();

        uSz total_size{0};
        uSz total_pos{0};

       private:
        void set_error(const std::string &error_msg);

        std::unique_ptr<u8[]> buffer;

        std::ifstream file;
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

        [[nodiscard]] constexpr bool good() const { return !this->error_flag; }
        [[nodiscard]] constexpr std::string_view error() const { return this->last_error; }

        void flush();
        void write_bytes(const u8 *bytes, uSz n);
        void write_hash_chars(const MD5String &hash_str);
        void write_hash_digest(const MD5Hash &hash_digest);
        void write_uleb128(u32 num);
        never_inline_attr void write_string(const char *str) {
            if(this->write_string_isnull(str)) return;
            this->write_string_nonnull(str, strlen(str));
        }
        never_inline_attr void write_string(std::string_view strview) {
            if(strview.empty()) {
                const u8 zero = 0;
                this->write<u8>(zero);
                return;
            }
            this->write_string_nonnull(strview.data(), strview.length());
        }

        template <typename T>
        never_inline_attr void write(T t) {
            this->write_bytes(reinterpret_cast<const u8 *>(&t), sizeof(T));
        }

       private:
        // check for null, if it is, write 0 and return true
        bool write_string_isnull(const char *str);
        // unchecked
        void write_string_nonnull(const char *str, uSz len);

        void set_error(const std::string &error_msg);

        std::unique_ptr<u8[]> buffer;

        std::string write_path;
        std::filesystem::path file_path;
        std::filesystem::path tmp_file_path;
        std::ofstream file;

        uSz pos{0};
        bool error_flag{false};
        std::string last_error;
    };
};

#undef default_inline_attr
