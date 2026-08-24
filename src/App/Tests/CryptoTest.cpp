// Copyright (c) 2026, WH, All rights reserved.
#include "CryptoTest.h"

#include "TestMacros.h"
#include "Engine.h"
#include "Environment.h"
#include "crypto.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mc::Tests {

CryptoTest::CryptoTest() { logRaw("CryptoTest created"); }

void CryptoTest::update() {
    if(m_done) return;
    m_done = true;

    namespace hash = crypto::hash;
    namespace conv = crypto::conv;
    namespace rng = crypto::rng;

    const std::string fox = "The quick brown fox jumps over the lazy dog";
    const std::vector<u8> fox_bytes{fox.begin(), fox.end()};

    TEST_SECTION("md5");
    TEST_ASSERT_EQ(hash::md5(""), "d41d8cd98f00b204e9800998ecf8427e", "md5 of empty string");
    TEST_ASSERT_EQ(hash::md5(std::span<const u8>{}), "d41d8cd98f00b204e9800998ecf8427e",
                   "md5 of empty span");
    TEST_ASSERT_EQ(hash::md5("abc"), "900150983cd24fb0d6963f7d28e17f72", "md5 of \"abc\"");
    TEST_ASSERT_EQ(hash::md5(fox), "9e107d9d372bb6826bd81d3542a419d6", "md5 of std::string");
    TEST_ASSERT(hash::md5(fox_bytes) == hash::md5(fox), "md5 of byte span matches string overload");

    TEST_SECTION("sha256");
    TEST_ASSERT_EQ(conv::encodehex(hash::sha256("")),
                   "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 of empty string");
    TEST_ASSERT_EQ(conv::encodehex(hash::sha256("abc")),
                   "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 of \"abc\"");
    TEST_ASSERT_EQ(conv::encodehex(hash::sha256(fox)),
                   "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592", "sha256 of std::string");
    TEST_ASSERT(hash::sha256(fox_bytes) == hash::sha256(fox), "sha256 of byte span matches string overload");

    TEST_SECTION("file hashing");
    {
        // larger than the internal read chunk and not a multiple of it, so chunk boundaries are exercised
        std::vector<u8> contents(100003);
        for(size_t i = 0; i < contents.size(); i++) contents[i] = static_cast<u8>((i * 31 + 7) & 0xFF);

        const std::string path = "crypto_test.tmp";
        {
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(contents.data()), static_cast<std::streamsize>(contents.size()));
        }

        const auto md5 = hash::md5_file(path);
        TEST_ASSERT(md5.has_value(), "md5_file succeeds on readable file");
        if(md5) TEST_ASSERT(*md5 == hash::md5(contents), "md5_file matches in-memory md5");

        const auto sha = hash::sha256_file(path);
        TEST_ASSERT(sha.has_value(), "sha256_file succeeds on readable file");
        if(sha) TEST_ASSERT(*sha == hash::sha256(contents), "sha256_file matches in-memory sha256");

        Environment::deleteFile(path);

        TEST_ASSERT(!hash::md5_file("this_file_does_not_exist.tmp").has_value(), "md5_file fails on missing file");
        TEST_ASSERT(!hash::sha256_file("this_file_does_not_exist.tmp").has_value(),
                    "sha256_file fails on missing file");
    }

    TEST_SECTION("base64");
    {
        // RFC 4648 test vectors
        constexpr std::array<std::pair<std::string_view, std::string_view>, 7> vectors{{
            {"", ""},
            {"f", "Zg=="},
            {"fo", "Zm8="},
            {"foo", "Zm9v"},
            {"foob", "Zm9vYg=="},
            {"fooba", "Zm9vYmE="},
            {"foobar", "Zm9vYmFy"},
        }};
        for(const auto& [plain, encoded] : vectors) {
            const std::vector<u8> plain_bytes{plain.begin(), plain.end()};
            TEST_ASSERT_EQ(conv::encode64(plain_bytes), encoded, "encode64 rfc4648 vector");
            TEST_ASSERT(conv::decode64(encoded) == plain_bytes, "decode64 rfc4648 vector");
        }

        const std::array<u8, 4> binary{0x00, 0xFF, 0x7F, 0x80};
        TEST_ASSERT_EQ(conv::encode64(binary), "AP9/gA==", "encode64 of binary bytes");
        TEST_ASSERT(std::ranges::equal(conv::decode64("AP9/gA=="), binary), "decode64 of binary bytes");

        TEST_ASSERT(conv::decode64("Zm9").empty(), "decode64 rejects length not multiple of 4");
        TEST_ASSERT(conv::decode64("Zm=v").empty(), "decode64 rejects padding in the middle");
        TEST_ASSERT(conv::decode64("Zm9v====").empty(), "decode64 rejects excessive padding");
        TEST_ASSERT(conv::decode64("Zm9v!Zm9").empty(), "decode64 rejects characters outside the alphabet");

        std::vector<u8> random_bytes(1000);
        rng::get_rand(random_bytes);
        TEST_ASSERT(conv::decode64(conv::encode64(random_bytes)) == random_bytes, "base64 round trip of random bytes");
    }

    TEST_SECTION("hex");
    TEST_ASSERT_EQ(conv::encodehex(std::array<u8, 4>{0x00, 0x0F, 0xA5, 0xFF}), "000fa5ff", "encodehex");
    TEST_ASSERT_EQ(conv::encodehex(std::span<const u8>{}), "", "encodehex of empty span");

    TEST_SECTION("rng");
    {
        std::array<u8, 64> a{};
        std::array<u8, 64> b{};
        rng::get_rand(a);
        rng::get_rand(b);
        TEST_ASSERT(std::ranges::any_of(a, [](u8 x) { return x != 0; }), "get_rand fills array with random bytes");
        TEST_ASSERT(a != b, "consecutive get_rand calls differ");

        u32 c_array[8]{};
        rng::get_rand(c_array);
        TEST_ASSERT(std::ranges::any_of(c_array, [](u32 x) { return x != 0; }), "get_rand fills C array");

        std::vector<u8> empty_vec;
        rng::get_rand(empty_vec);
        TEST_ASSERT(empty_vec.empty(), "get_rand on empty vector is a no-op");

        TEST_ASSERT(rng::get_rand<u64>() != rng::get_rand<u64>(), "get_rand<u64> produces different values");
    }

    TEST_SECTION("prng");
    {
        bool in_range = true;
        for(int i = 0; i < 1000; i++) in_range = in_range && prand() >= 0;
        TEST_ASSERT(in_range, "prand is non-negative");
    }

    TEST_PRINT_RESULTS("CryptoTest");
    engine->shutdown();
}

}  // namespace Mc::Tests
