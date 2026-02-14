// Copyright (c) 2025, WH & 2025, kiwec, All rights reserved.
#include "crypto.h"
#include "sha256.h"            // vendored library
#include "base64.h"            // vendored library
#include "MD5.h"               // vendored library
#include "ByteBufferedFile.h"  // for file hashing functions
#include "MD5Hash.h"
#include "BaseEnvironment.h"

#include <vector>
#include <cstring>
#include <cerrno>
#include <random>
#include <algorithm>

#ifdef USE_OPENSSL
#include <openssl/rand.h>
#include <openssl/evp.h>
#endif

#ifdef _WIN32
#include "WinDebloatDefs.h"
#include <windows.h>
#include <wincrypt.h>
#elif defined(__EMSCRIPTEN__)
#include <unistd.h>  // for getentropy
#else
#include <sys/random.h>
#endif

namespace crypto {
namespace {
// we will call init() in the Engine ctor which will seed it properly
// NOLINTNEXTLINE(cert-msc51-cpp, cert-msc32-c)
std::mt19937_64 rngalg;

std::uniform_int_distribution<i64> rngdist;
}  // namespace

void init() noexcept {
    // initialize uniform int distribution range
    rngdist.param(std::uniform_int_distribution<i64>::param_type{0, crypto::prng::PRAND_MAX});

    // seed with true random (seed C rand() here as well)
    srand(crypto::rng::get_rand<u32>());
    rngalg.seed(crypto::rng::get_rand<u64>());
}

namespace prng {
i64 prand() noexcept { return rngdist(rngalg); }
}  // namespace prng

namespace rng {

void get_bytes(u8* out, std::size_t s_out) {
#ifdef USE_OPENSSL
    if(RAND_bytes(out, static_cast<i32>(s_out)) == 1) {
        return;
    }
#endif

#ifdef _WIN32
    HCRYPTPROV hCryptProv;
    if(!CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        // failed to acquire crypto context, nope out
        fubar_abort();
    }

    if(!CryptGenRandom(hCryptProv, s_out, out)) {
        CryptReleaseContext(hCryptProv, 0);
        // failed to generate random bytes, nope out
        fubar_abort();
    }

    CryptReleaseContext(hCryptProv, 0);
#elif __APPLE__
    arc4random_buf(out, s_out);
#elif defined(__EMSCRIPTEN__)
    // emscripten provides getentropy (max 256 bytes per call)
    size_t offset = 0;
    while(offset < s_out) {
        size_t chunk = std::min(s_out - offset, size_t{256});
        if(getentropy(out + offset, chunk) != 0) {
            fubar_abort();
        }
        offset += chunk;
    }
#else
    size_t offset = 0;
    while(offset < s_out) {
        ssize_t ret = getrandom(out + offset, s_out - offset, 0);
        if(ret < 0) {
            if(errno == EINTR) {
                continue;  // interrupted by signal, retry
            }
            // failed, nope out
            fubar_abort();
        }
        offset += static_cast<size_t>(ret);
    }
#endif
}

}  // namespace rng

namespace hash {
void sha256(const void* data, size_t size, u8* hash) {
#ifdef USE_OPENSSL
    unsigned int hash_len;
    if(EVP_Digest(data, size, hash, &hash_len, EVP_sha256(), nullptr) == 1) {
        return;
    }
#endif

    // fallback to vendored library
    sha256_easy_hash(data, size, hash);
}

void md5(const void* data, size_t size, u8* hash) {
#ifdef USE_OPENSSL
    unsigned int hash_len;
    if(EVP_Digest(data, size, hash, &hash_len, EVP_md5(), nullptr) == 1) {
        return;
    }
#endif

    // fallback to vendored library
    MD5 hasher;
    hasher.update(static_cast<const unsigned char*>(data), size);
    hasher.finalize();
    std::memcpy(hash, hasher.getDigest(), 16);
}

void sha256_f(std::string_view file_path, u8* hash) {
    constexpr size_t CHUNK_SIZE{32768};
    std::array<u8, CHUNK_SIZE> buffer{};
    size_t bytes_read{0};

#ifdef USE_OPENSSL
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if(ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1) {
        ByteBufferedFile::Reader reader(file_path);
        if(reader.good()) {
            bool success = true;

            while(reader.good() && (bytes_read = reader.read_bytes(buffer.data(), CHUNK_SIZE)) > 0) {
                if(EVP_DigestUpdate(ctx, buffer.data(), bytes_read) != 1) {
                    success = false;
                    break;
                }
            }

            if(success && reader.good()) {
                unsigned int hash_len = 0;
                if(EVP_DigestFinal_ex(ctx, hash, &hash_len) == 1) {
                    EVP_MD_CTX_free(ctx);
                    return;
                }
            }
        }
        EVP_MD_CTX_free(ctx);
    }
#endif

    // fallback to vendored library
    struct sha256_buff buff;
    sha256_init(&buff);

    ByteBufferedFile::Reader reader(file_path);
    if(reader.good()) {
        while(reader.good() && (bytes_read = reader.read_bytes(buffer.data(), CHUNK_SIZE)) > 0) {
            sha256_update(&buff, buffer.data(), bytes_read);
        }
    }

    sha256_finalize(&buff);
    sha256_read(&buff, hash);
}

void md5_f(std::string_view file_path, u8* hash) {
    constexpr size_t CHUNK_SIZE{32768};
    std::array<u8, CHUNK_SIZE> buffer{};
    size_t bytes_read{0};

#ifdef USE_OPENSSL
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if(ctx && EVP_DigestInit_ex(ctx, EVP_md5(), nullptr) == 1) {
        ByteBufferedFile::Reader reader(file_path);
        if(reader.good()) {
            bool success = true;

            while(reader.good() && (bytes_read = reader.read_bytes(buffer.data(), CHUNK_SIZE)) > 0) {
                if(EVP_DigestUpdate(ctx, buffer.data(), bytes_read) != 1) {
                    success = false;
                    break;
                }
            }

            if(success && reader.good()) {
                unsigned int hash_len;
                if(EVP_DigestFinal_ex(ctx, hash, &hash_len) == 1) {
                    EVP_MD_CTX_free(ctx);
                    return;
                }
            }
        }
        EVP_MD_CTX_free(ctx);
    }
#endif

    // fallback to vendored library
    MD5 hasher;
    ByteBufferedFile::Reader reader(file_path);
    if(reader.good()) {
        while(reader.good() && (bytes_read = reader.read_bytes(buffer.data(), CHUNK_SIZE)) > 0) {
            hasher.update(buffer.data(), bytes_read);
        }
    }

    hasher.finalize();
    std::memcpy(hash, hasher.getDigest(), 16);
}

MD5String md5_hex(const u8* msg, size_t msg_len) {
    MD5Hash hash_bytes;
    crypto::hash::md5(msg, msg_len, hash_bytes.data());

    return MD5String{hash_bytes};
}

}  // namespace hash

namespace conv {
std::string encode64(const u8* src, size_t len) {
#ifdef USE_OPENSSL
    // calculate output size: base64 encoding produces 4 chars for every 3 input bytes
    size_t encoded_len = 4 * ((len + 2) / 3);
    std::vector<u8> temp(encoded_len + 1);

    size_t actual_len = EVP_EncodeBlock(temp.data(), src, static_cast<int>(len));
    if(actual_len > 0) {
        return std::string{reinterpret_cast<const char*>(temp.data()), actual_len};
    }
#endif

    // fallback to vendored library
    size_t out_len;
    u8* result = base64_encode(src, len, &out_len);
    if(result) {
        std::string res{reinterpret_cast<const char*>(result), out_len};
        delete[] result;
        return res;
    }

    return "";
}

std::vector<u8> decode64(std::string_view srcParam) {
    const u8* src = (u8*)srcParam.data();
    size_t len = srcParam.length();

#ifdef USE_OPENSSL
    // calculate maximum output size
    size_t max_decoded_len = (len * 3) / 4 + 1;
    std::vector<u8> temp(max_decoded_len);

    int actual_len = EVP_DecodeBlock(temp.data(), src, len);
    if(actual_len >= 0) {
        // EVP_DecodeBlock doesn't account for padding, need to adjust
        int padding = 0;
        if(len >= 2) {
            if(src[len - 1] == '=') padding++;
            if(src[len - 2] == '=') padding++;
        }
        actual_len -= padding;

        temp.resize(static_cast<size_t>(actual_len));
        return temp;
    }
#endif

    // fallback to vendored library
    size_t out_len;
    u8* result = base64_decode(src, len, &out_len);
    if(result) {
        std::vector<u8> vec(result, result + out_len);
        delete[] result;
        return vec;
    }

    return {};
}
}  // namespace conv

}  // namespace crypto
