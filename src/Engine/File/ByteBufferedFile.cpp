// Copyright (c) 2024, kiwec & 2025, WH, All rights reserved.
#include "ByteBufferedFile.h"

#include "Logging.h"
#include "File.h"
#include "SyncMutex.h"
#include "MD5Hash.h"

#include <system_error>
#include <cassert>
#include <vector>

namespace {
constexpr size_t NUM_FILE_LOCKS = 16;

std::array<Sync::shared_mutex, NUM_FILE_LOCKS> file_locks;
size_t path_to_lock_index(std::string_view path) { return std::hash<std::string_view>{}(path) % NUM_FILE_LOCKS; }
}  // namespace

ByteBufferedFile::Reader::Reader(std::string_view readPath_param)
    : buffer(std::make_unique_for_overwrite<u8[]>(READ_BUFFER_SIZE)), read_path(readPath_param) {
    file_locks[path_to_lock_index(this->read_path)].lock_shared();

    auto path = File::getFsPath(this->read_path);
    this->file.open(path, std::ios::binary);
    if(!this->file.is_open()) {
        this->set_error("Failed to open file for reading: " + std::generic_category().message(errno));
        debugLog("Failed to open '{:s}': {:s}", this->read_path, std::generic_category().message(errno).c_str());
        return;
    }

    this->file.seekg(0, std::ios::end);
    if(this->file.fail()) {
        goto seek_error;
    }

    this->total_size = this->file.tellg();

    this->file.seekg(0, std::ios::beg);
    if(this->file.fail()) {
        goto seek_error;
    }

    return;  // success

seek_error:
    this->set_error("Failed to initialize file reader: " + std::generic_category().message(errno));
    debugLog("Failed to initialize file reader '{:s}': {:s}", this->read_path,
             std::generic_category().message(errno).c_str());
    this->file.close();
    return;
}

ByteBufferedFile::Reader::~Reader() { file_locks[path_to_lock_index(this->read_path)].unlock_shared(); }

void ByteBufferedFile::Reader::set_error(const std::string &error_msg) {
    if(!this->error_flag) {  // only set first error
        this->error_flag = true;
        this->last_error = error_msg;
    }
}

// TODO: error handling is wildly incorrect/dubious
bool ByteBufferedFile::Reader::read_hash_chars(MD5String &inout) {
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

bool ByteBufferedFile::Reader::read_hash_chars(MD5Hash &inout) {
    MD5String temp;
    bool ret = read_hash_chars(temp);
    inout = temp;
    return ret;
}

bool ByteBufferedFile::Reader::read_hash_digest(MD5Hash &inout) {
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

bool ByteBufferedFile::Reader::read_string(std::string &inout) {
    if(this->error_flag) {
        return false;
    }

    u8 empty_check = this->read<u8>();
    if(empty_check == 0) return false;

    u32 len = this->read_uleb128();

    inout.resize_and_overwrite(
        len, [this](char *data, uSz size) -> uSz { return this->read_bytes(reinterpret_cast<u8 *>(data), size); });

    if(inout.size() != len) {
        inout.clear();
        this->set_error("Failed to read " + std::to_string(len) + " bytes for string");
        return false;
    }

    return true;
}

std::string ByteBufferedFile::Reader::read_string() {
    std::string str;
    this->read_string(str);
    return str;
}

bool ByteBufferedFile::Reader::read_cstring(std::unique_ptr<char[]> &inout) {
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
    inout = std::make_unique_for_overwrite<char[]>(len + 1);
    read = this->read_bytes(reinterpret_cast<u8 *>(inout.get()), len);

    inout[len] = '\0';

    if(read != len) {
        this->set_error("Failed to read " + std::to_string(len) + " bytes for string");
        goto out;
    }
    success = true;
out:
    if(!success) {
        inout = std::make_unique<char[]>(1);
    }
    return success;
}

std::unique_ptr<char[]> ByteBufferedFile::Reader::read_cstring() {
    std::unique_ptr<char[]> str;
    this->read_cstring(str);
    return str;
}

u32 ByteBufferedFile::Reader::read_uleb128() {
    if(this->error_flag) {
        return 0;
    }

    u32 result = 0;
    u32 shift = 0;
    u8 byte = 0;

    do {
        byte = this->read<u8>();
        result |= (byte & 0x7f) << shift;
        shift += 7;
    } while(byte & 0x80);

    return result;
}

void ByteBufferedFile::Reader::skip_string() {
    if(this->error_flag) {
        return;
    }

    u8 empty_check = this->read<u8>();
    if(empty_check == 0) return;

    u32 len = this->read_uleb128();
    this->skip_bytes(len);
}

ByteBufferedFile::Writer::Writer(std::string_view writePath_param)
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
        this->set_error("Failed to open file for writing: " + std::generic_category().message(errno));
        debugLog("Failed to open '{:s}': {:s}", this->write_path, std::generic_category().message(errno).c_str());
        return;
    }
}

ByteBufferedFile::Writer::~Writer() {
    if(this->file.is_open()) {
        this->flush();
        this->file.close();

        if(!this->error_flag && this->tmp_file_path != this->file_path) {
            std::error_code ec;
            std::filesystem::remove(this->file_path, ec);  // Windows (the Microsoft docs are LYING)
            std::filesystem::rename(this->tmp_file_path, this->file_path, ec);
            if(ec) {
                // can't set error in destructor, but log it
                debugLog("Failed to rename temporary file: {:s}", ec.message().c_str());
            }
        }
    }

    file_locks[path_to_lock_index(this->write_path)].unlock();
}

void ByteBufferedFile::Writer::set_error(const std::string &error_msg) {
    if(!this->error_flag) {  // only set first error
        this->error_flag = true;
        this->last_error = error_msg;
    }
}

void ByteBufferedFile::Writer::write_hash_chars(const MD5String &hash_str) {
    if(this->error_flag) {
        return;
    }

    this->write<u8>(0x0B);
    this->write<u8>(hash_str.size());
    this->write_bytes(reinterpret_cast<const u8 *>(hash_str.data()), hash_str.size());
}

void ByteBufferedFile::Writer::write_hash_digest(const MD5Hash &hash_digest) {
    if(this->error_flag) {
        return;
    }

    this->write<u8>(0x0B);
    this->write<u8>(hash_digest.size());
    this->write_bytes(reinterpret_cast<const u8 *>(hash_digest.data()), hash_digest.size());
}

void ByteBufferedFile::Writer::flush() {
    if(this->error_flag || !this->file.is_open()) {
        return;
    }

    this->file.write(reinterpret_cast<const char *>(&this->buffer[0]), this->pos);
    if(this->file.fail()) {
        this->set_error("Failed to write to file: " + std::generic_category().message(errno));
        return;
    }
    this->pos = 0;
}

void ByteBufferedFile::Writer::write_bytes(const u8 *bytes, uSz n) {
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
        this->set_error("Attempted to write " + std::to_string(n) + " bytes (exceeding buffer size " +
                        std::to_string(WRITE_BUFFER_SIZE) + ")");
        return;
    }

    memcpy(&this->buffer[this->pos], bytes, n);
    this->pos += n;
}

void ByteBufferedFile::Writer::write_uleb128(u32 num) {
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

bool ByteBufferedFile::Writer::write_string_isnull(const char *str) {
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

void ByteBufferedFile::Writer::write_string_nonnull(const char *str, uSz len) {
    u8 empty_check = 0x0B;
    this->write<u8>(empty_check);

    this->write_uleb128(len);
    this->write_bytes(reinterpret_cast<const u8 *>(str), len);
}
