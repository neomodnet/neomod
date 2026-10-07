// Copyright (c) 2026, WH, All rights reserved.
#include "FileTest.h"

#include "TestMacros.h"
#include "ByteBufferedFile.h"
#include "Engine.h"
#include "Environment.h"
#include "File.h"
#include "Paths.h"
#include "SyncJthread.h"

#include "fmt/format.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace Mc::Tests {

namespace {
namespace fs = std::filesystem;

std::vector<u8> readAll(const std::string &path) {
    std::vector<u8> bytes;
    if(File file(path); file.canRead()) file.readToVector(bytes);
    return bytes;
}

std::vector<u8> pattern(uSz size, u8 seed) {
    std::vector<u8> bytes(size);
    for(uSz i = 0; i < size; i++) bytes[i] = (u8)(i * 31 + seed);
    return bytes;
}
}  // namespace

FileTest::FileTest() { logRaw("FileTest created"); }

void FileTest::update() {
    if(m_done) return;
    m_done = true;

    const std::string dir = Mc::Paths::cache() + "/filetest";
    Environment::createDirectory(dir);

    TEST_SECTION("replace");
    {
        // someone looking at the path while it's replaced, over and over, always finds a file there
        const std::string path = dir + "/replaced";
        {
            ByteBufferedFile::Writer out(path);
            out.write<u32>(0);
        }
        std::atomic<u32> missing{0};
        std::atomic<bool> started{false};
        {
            Sync::jthread watcher([&](const Sync::stop_token &stop) {
                started.store(true, std::memory_order_release);
                while(!stop.stop_requested()) {
                    std::error_code ec;
                    if(!fs::exists(fs::path{path}, ec)) missing.fetch_add(1, std::memory_order_relaxed);
                }
            });
            while(!started.load(std::memory_order_acquire)) {
            }
            for(u32 i = 1; i <= 2000; i++) {
                ByteBufferedFile::Writer out(path);
                out.write<u32>(i);
            }
        }
        TEST_ASSERT_EQ(missing.load(), 0u, "a replaced file is never missing");
        const std::vector<u8> last = readAll(path);
        TEST_ASSERT(last.size() == 4 && last[0] == (2000 & 0xff) && last[1] == (2000 >> 8), "the last write is there");
        TEST_ASSERT(!Environment::fileExists(path + ".tmp"), "no .tmp left");
    }

    TEST_SECTION("large writes");
    {
        // more than the writer's buffer in one go, then more after it
        const std::string path = dir + "/large";
        const std::vector<u8> big = pattern(5 * 1024 * 1024 + 123, 7);
        const std::vector<u8> tail = pattern(1000, 3);
        bool good = false;
        {
            ByteBufferedFile::Writer out(path);
            out.write_bytes(big.data(), big.size());
            out.write_bytes(tail.data(), tail.size());
            good = out.good();
        }
        TEST_ASSERT(good, "a write larger than the buffer succeeds");
        std::vector<u8> expected = big;
        expected.insert(expected.end(), tail.begin(), tail.end());
        TEST_ASSERT(readAll(path) == expected, "and is all there, in order");
    }

    TEST_SECTION("commit");
    {
        const std::string path = dir + "/committed";
        {
            ByteBufferedFile::Writer out(path);
            out.write<u32>(7);
            TEST_ASSERT(out.commit(), "a commit says it worked");
            TEST_ASSERT(Environment::fileExists(path) && !Environment::fileExists(path + ".tmp"),
                        "the file is there when commit() returns, without its .tmp");
            out.write<u32>(8);
            TEST_ASSERT(out.commit(), "committing again changes nothing");
        }
        TEST_ASSERT_EQ(readAll(path).size(), uSz{4}, "what's written after a commit goes nowhere");

        // the path is a directory, so the replace fails
        const std::string blocked = dir + "/blocked";
        Environment::createDirectory(blocked);
        {
            ByteBufferedFile::Writer out(blocked);
            out.write<u32>(1);
            TEST_ASSERT(!out.commit() && !out.error().empty(), "a failed replace says so, and why");
        }
        TEST_ASSERT(Environment::directoryExists(blocked) && !Environment::fileExists(blocked + ".tmp"),
                    "and changes nothing: what was there stays, the .tmp goes");

        ByteBufferedFile::Writer nowhere(dir + "/missing/file");
        nowhere.write<u32>(1);
        TEST_ASSERT(!nowhere.commit() && !nowhere.error().empty(), "a file that can't be opened fails its commit");
    }

    Environment::deletePathsRecursive(dir);
    TEST_PRINT_RESULTS("FileTest");
    engine->shutdown();
}

}  // namespace Mc::Tests
