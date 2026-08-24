// Copyright (c) 2025, WH & 2025, kiwec, All rights reserved.
#include "crypto.h"
#include "sha256.h"            // vendored library
#include "MD5.h"               // vendored library
#include "ByteBufferedFile.h"  // for file hashing functions
#include "BaseEnvironment.h"
#include "noinclude.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>

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
// NOLINTNEXTLINE(bugprone-random-generator-seed, cert-msc51-cpp, cert-msc32-c)
std::mt19937_64 rngalg;

std::uniform_int_distribution<i64> rngdist{0, prng::PRAND_MAX};
}  // namespace

void init() noexcept {
    // seed with true random (seed C rand() here as well)
    srand(rng::get_rand<u32>());
    rngalg.seed(rng::get_rand<u64>());
}

namespace prng {
i64 prand() noexcept { return rngdist(rngalg); }
}  // namespace prng

namespace rng {

void get_bytes(std::span<u8> out) {
    if(out.empty()) return;

#ifdef USE_OPENSSL
    if(out.size() <= static_cast<size_t>(std::numeric_limits<int>::max()) &&
       RAND_bytes(out.data(), static_cast<int>(out.size())) == 1) {
        return;
    }
#endif

#ifdef _WIN32
    HCRYPTPROV hCryptProv;
    if(!CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        // failed to acquire crypto context, nope out
        fubar_abort();
    }

    if(!CryptGenRandom(hCryptProv, static_cast<DWORD>(out.size()), out.data())) {
        CryptReleaseContext(hCryptProv, 0);
        // failed to generate random bytes, nope out
        fubar_abort();
    }

    CryptReleaseContext(hCryptProv, 0);
#elif __APPLE__
    arc4random_buf(out.data(), out.size());
#elif defined(__EMSCRIPTEN__)
    // emscripten provides getentropy (max 256 bytes per call)
    while(!out.empty()) {
        const size_t chunk = std::min(out.size(), size_t{256});
        if(getentropy(out.data(), chunk) != 0) {
            fubar_abort();
        }
        out = out.subspan(chunk);
    }
#else
    while(!out.empty()) {
        const ssize_t ret = getrandom(out.data(), out.size(), 0);
        if(ret < 0) {
            if(errno == EINTR) {
                continue;  // interrupted by signal, retry
            }
            // failed, nope out
            fubar_abort();
        }
        out = out.subspan(static_cast<size_t>(ret));
    }
#endif
}

}  // namespace rng

namespace hash {
namespace {
constexpr size_t FILE_CHUNK_SIZE{32768};

// streaming digest contexts sharing one update()/finalize() interface,
// so that the one-shot and file hashing paths are only written once

struct MD5Vendored {
    MD5 hasher;

    void update(std::span<const u8> data) {
        // the vendored implementation takes 32-bit lengths
        while(!data.empty()) {
            const auto chunk =
                static_cast<MD5::size_type>(std::min<size_t>(data.size(), std::numeric_limits<MD5::size_type>::max()));
            hasher.update(data.data(), chunk);
            data = data.subspan(chunk);
        }
    }

    MD5Hash finalize() {
        MD5Hash out;
        hasher.finalize();
        std::memcpy(out.data(), hasher.getDigest(), out.size());
        return out;
    }
};

struct SHA256Vendored {
    sha256_buff buff{};

    SHA256Vendored() { sha256_init(&buff); }

    void update(std::span<const u8> data) {
        if(!data.empty()) sha256_update(&buff, data.data(), data.size());
    }

    std::array<u8, 32> finalize() {
        std::array<u8, 32> out{};
        sha256_finalize(&buff);
        sha256_read(&buff, out.data());
        return out;
    }
};

struct MD5Algo {
    using Digest = MD5Hash;
    using Vendored = MD5Vendored;
#ifdef USE_OPENSSL
    static const EVP_MD* evp() { return EVP_md5(); }
#endif
};

struct SHA256Algo {
    using Digest = std::array<u8, 32>;
    using Vendored = SHA256Vendored;
#ifdef USE_OPENSSL
    static const EVP_MD* evp() { return EVP_sha256(); }
#endif
};

#ifdef USE_OPENSSL
class EVPDigest {
    NOCOPY_NOMOVE(EVPDigest)
   public:
    explicit EVPDigest(const EVP_MD* md) : ctx(EVP_MD_CTX_new()), ok(ctx && EVP_DigestInit_ex(ctx, md, nullptr) == 1) {}
    ~EVPDigest() { EVP_MD_CTX_free(ctx); }

    [[nodiscard]] bool good() const { return ok; }

    void update(std::span<const u8> data) {
        if(data.empty()) return;
        ok = ok && EVP_DigestUpdate(ctx, data.data(), data.size()) == 1;
    }

    [[nodiscard]] bool finalize(std::span<u8> out) {
        unsigned int len{0};
        return ok && EVP_DigestFinal_ex(ctx, out.data(), &len) == 1 && len == out.size();
    }

   private:
    EVP_MD_CTX* ctx;
    bool ok;
};
#endif

// feed(digest) pushes the entire input through the given digest context, returning false if it couldn't be read
template <typename Algo, typename Feed>
std::optional<typename Algo::Digest> compute(const Feed& feed) {
#ifdef USE_OPENSSL
    {
        EVPDigest evp{Algo::evp()};
        if(evp.good()) {
            if(!feed(evp)) return std::nullopt;
            typename Algo::Digest digest{};
            if(evp.finalize(digest)) return digest;
        }
        // openssl failed somewhere, retry with the vendored implementation
    }
#endif

    typename Algo::Vendored vendored;
    if(!feed(vendored)) return std::nullopt;
    return vendored.finalize();
}

template <typename Algo>
typename Algo::Digest hash_bytes(std::span<const u8> data) {
    // can't fail: in-memory input has no read errors and the vendored fallback always succeeds
    return *compute<Algo>([data](auto& digest) {
        digest.update(data);
        return true;
    });
}

template <typename Algo>
std::optional<typename Algo::Digest> hash_file(std::string_view file_path) {
    return compute<Algo>([file_path](auto& digest) {
        ByteBufferedFile::Reader reader(file_path);
        if(!reader.good()) return false;

        std::array<u8, FILE_CHUNK_SIZE> buffer{};
        while(reader.good()) {
            const size_t bytes_read = reader.read_bytes(buffer.data(), buffer.size());
            if(bytes_read == 0) break;
            digest.update(std::span{buffer}.first(bytes_read));
        }
        return reader.good();
    });
}

std::span<const u8> as_bytes(std::string_view str) { return {reinterpret_cast<const u8*>(str.data()), str.size()}; }
}  // namespace

MD5Hash md5(std::span<const u8> data) { return hash_bytes<MD5Algo>(data); }
MD5Hash md5(std::string_view data) { return md5(as_bytes(data)); }

std::array<u8, 32> sha256(std::span<const u8> data) { return hash_bytes<SHA256Algo>(data); }
std::array<u8, 32> sha256(std::string_view data) { return sha256(as_bytes(data)); }

std::optional<MD5Hash> md5_file(std::string_view file_path) { return hash_file<MD5Algo>(file_path); }
std::optional<std::array<u8, 32>> sha256_file(std::string_view file_path) { return hash_file<SHA256Algo>(file_path); }

}  // namespace hash

namespace conv {
namespace {
constexpr std::string_view BASE64_ALPHABET{"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};
constexpr std::string_view HEX_ALPHABET{"0123456789abcdef"};
}  // namespace

std::string encode64(std::span<const u8> src) {
    std::string out;
    out.reserve(((src.size() + 2) / 3) * 4);

    size_t i = 0;
    for(; i + 3 <= src.size(); i += 3) {
        const u32 triple = (u32{src[i]} << 16) | (u32{src[i + 1]} << 8) | u32{src[i + 2]};
        out.push_back(BASE64_ALPHABET[(triple >> 18) & 0x3F]);
        out.push_back(BASE64_ALPHABET[(triple >> 12) & 0x3F]);
        out.push_back(BASE64_ALPHABET[(triple >> 6) & 0x3F]);
        out.push_back(BASE64_ALPHABET[triple & 0x3F]);
    }

    // 1 or 2 trailing bytes, padded with '='
    if(const size_t remaining = src.size() - i; remaining > 0) {
        const u32 triple = (u32{src[i]} << 16) | (remaining == 2 ? u32{src[i + 1]} << 8 : 0u);
        out.push_back(BASE64_ALPHABET[(triple >> 18) & 0x3F]);
        out.push_back(BASE64_ALPHABET[(triple >> 12) & 0x3F]);
        out.push_back(remaining == 2 ? BASE64_ALPHABET[(triple >> 6) & 0x3F] : '=');
        out.push_back('=');
    }

    return out;
}

std::vector<u8> decode64(std::string_view src) {
    static constexpr auto lookup = [] {
        std::array<u8, 256> table{};
        table.fill(0xFF);
        for(size_t i = 0; i < BASE64_ALPHABET.size(); i++) {
            table[static_cast<u8>(BASE64_ALPHABET[i])] = static_cast<u8>(i);
        }
        return table;
    }();

    if(src.empty() || src.size() % 4 != 0) return {};

    // '=' is only valid as (up to two) trailing padding characters
    size_t padding = 0;
    while(padding < 2 && src[src.size() - 1 - padding] == '=') padding++;
    src.remove_suffix(padding);

    std::vector<u8> out;
    out.reserve(src.size() * 6 / 8);

    u32 acc = 0;
    u32 bits = 0;
    for(const char c : src) {
        const u8 sextet = lookup[static_cast<u8>(c)];
        if(sextet == 0xFF) return {};

        acc = (acc << 6) | sextet;
        bits += 6;
        if(bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<u8>(acc >> bits));
            acc &= (1u << bits) - 1;
        }
    }

    return out;
}

std::string encodehex(std::span<const u8> src) {
    std::string out;
    out.reserve(src.size() * 2);

    for(const u8 byte : src) {
        out.push_back(HEX_ALPHABET[byte >> 4]);
        out.push_back(HEX_ALPHABET[byte & 0x0F]);
    }

    return out;
}
}  // namespace conv

}  // namespace crypto
