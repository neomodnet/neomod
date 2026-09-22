// Copyright (c) 2026, WH, All rights reserved.
#include "PacketTest.h"

#include "TestMacros.h"
#include "BanchoPacket.h"
#include "BanchoProtocol.h"
#include "Engine.h"
#include "MD5Hash.h"
#include "ModFlags.h"
#include "Replay.h"

#include <array>
#include <span>
#include <string>
#include <vector>

namespace Mc::Tests {

PacketTest::PacketTest() { logRaw("PacketTest created"); }

void PacketTest::update() {
    if(m_done) return;
    m_done = true;

    TEST_SECTION("scalars");
    {
        Packet packet{42};
        packet.write<u8>(0xAB);
        packet.write<u16>(0x1234);
        packet.write<i32>(-7);
        packet.write<u64>(0x0102030405060708ULL);
        packet.write<f32>(1.5f);
        packet.write<f64>(-2.25);
        TEST_ASSERT_EQ(packet.data.size(), uSz{1 + 2 + 4 + 8 + 4 + 8}, "packed size");
        TEST_ASSERT(packet.data[1] == 0x34 && packet.data[2] == 0x12, "little-endian");

        PacketReader reader{packet.data, packet.id};
        TEST_ASSERT_EQ(reader.id, 42, "reader id");
        TEST_ASSERT_EQ(reader.size(), packet.data.size(), "reader size");
        const u8 a = reader.read<u8>();
        const u16 b = reader.read<u16>();
        const i32 c = reader.read<i32>();
        const u64 d = reader.read<u64>();
        const f32 e = reader.read<f32>();
        const f64 f = reader.read<f64>();
        TEST_ASSERT_EQ(a, 0xAB, "u8");
        TEST_ASSERT_EQ(b, 0x1234, "u16");
        TEST_ASSERT_EQ(c, -7, "i32");
        TEST_ASSERT_EQ(d, 0x0102030405060708ULL, "u64");
        TEST_ASSERT_EQ(e, 1.5f, "f32");
        TEST_ASSERT_EQ(f, -2.25, "f64");
        TEST_ASSERT_EQ(reader.remaining(), uSz{0}, "everything consumed");
        TEST_ASSERT(reader.good(), "reader good after exact consumption");
    }

    TEST_SECTION("uleb128");
    {
        constexpr std::array<u32, 8> values{0, 1, 127, 128, 16383, 16384, 0x0FFFFFFF, 0xFFFFFFFF};
        constexpr std::array<uSz, 8> lengths{1, 1, 1, 2, 2, 3, 4, 5};
        for(size_t i = 0; i < values.size(); i++) {
            Packet packet;
            packet.write_uleb128(values[i]);
            TEST_ASSERT_EQ(packet.data.size(), lengths[i], "uleb128 encoded length");
            PacketReader reader{packet.data};
            const u32 decoded = reader.read_uleb128();
            TEST_ASSERT_EQ(decoded, values[i], "uleb128 round trip");
            TEST_ASSERT(reader.good() && reader.remaining() == 0, "uleb128 consumed exactly");
        }

        // a run of continuation bytes has to stop decoding after 5 bytes instead of shifting past 32 bits
        const std::vector<u8> endless(16, 0xFF);
        PacketReader reader{endless};
        (void)reader.read_uleb128();
        TEST_ASSERT_EQ(reader.remaining(), uSz{11}, "uleb128 decoding stops after 5 bytes");
        TEST_ASSERT(reader.good(), "a capped uleb128 doesn't fail the reader");
    }

    TEST_SECTION("strings");
    {
        const std::string long_string(200, 'x');  // 2-byte uleb128 length
        Packet packet;
        packet.write_string("");
        packet.write_string("hello");
        packet.write_string(long_string);
        packet.write_string("日本語");
        packet.write_string("bad \xff utf8");
        TEST_ASSERT_EQ(packet.data[0], u8{0}, "an empty string is a single null marker");
        TEST_ASSERT_EQ(packet.data[1], u8{0x0B}, "a non-empty string starts with the presence marker");
        TEST_ASSERT_EQ(packet.data[2], u8{5}, "then its uleb128 length");
        TEST_ASSERT_EQ(packet.data.size(), uSz{1 + (2 + 5) + (3 + 200) + (2 + 9) + (2 + 10)}, "encoded size");

        PacketReader reader{packet.data};
        const std::string empty = reader.read_string();
        const std::string hello = reader.read_string();
        const std::string longer = reader.read_string();
        const std::string japanese = reader.read_string();
        const std::string sanitized = reader.read_string();
        TEST_ASSERT_EQ(empty, "", "empty string");
        TEST_ASSERT_EQ(hello, "hello", "ascii string");
        TEST_ASSERT_EQ(longer, long_string, "string with a 2-byte length");
        TEST_ASSERT_EQ(japanese, "日本語", "utf-8 string");
        TEST_ASSERT(sanitized != "bad \xff utf8" && sanitized.starts_with("bad ") && sanitized.ends_with(" utf8"),
                    "invalid utf-8 is sanitized");
        TEST_ASSERT(reader.good() && reader.remaining() == 0, "strings consumed exactly");

        PacketReader skipper{packet.data};
        skipper.skip_string();
        skipper.skip_string();
        skipper.skip_string();
        const std::string fourth = skipper.read_string();
        TEST_ASSERT_EQ(fourth, "日本語", "skip_string skips whole strings");

        // peeking at the presence marker is how Room reads its has_password flag
        PacketReader peeker{packet.data};
        TEST_ASSERT_EQ(peeker.peek<u8>(), u8{0}, "peek sees the null marker");
        peeker.skip_string();
        TEST_ASSERT_EQ(peeker.peek<u8>(), u8{0x0B}, "peek sees the presence marker");
        TEST_ASSERT_EQ(peeker.remaining(), packet.data.size() - 1, "peek doesn't advance");
    }

    TEST_SECTION("hashes");
    {
        const MD5String chars{"0123456789abcdef0123456789abcdef"};
        const MD5Hash digest{chars};
        Packet packet;
        packet.write_hash_chars(chars);
        packet.write_hash_digest(digest);
        packet.write_string("");
        TEST_ASSERT_EQ(packet.data.size(), uSz{(2 + 32) + (2 + 16) + 1}, "encoded hash sizes");

        PacketReader reader{packet.data};
        const MD5String read_chars = reader.read_hash_chars();
        const MD5Hash read_digest = reader.read_hash_digest();
        const MD5String null_hash = reader.read_hash_chars();
        TEST_ASSERT(read_chars == chars, "hash chars round trip");
        TEST_ASSERT(read_digest == digest, "hash digest round trip");
        TEST_ASSERT(null_hash.empty(), "a null string reads as an empty hash");
        TEST_ASSERT(reader.good() && reader.remaining() == 0, "hashes consumed exactly");

        // a hash string of the wrong length is skipped whole (the cursor stays in sync) and copied as far as it goes
        Packet odd;
        odd.write_string("abc");
        odd.write<u8>(9);
        PacketReader odd_reader{odd.data};
        const MD5String short_hash = odd_reader.read_hash_chars();
        const u8 next = odd_reader.read<u8>();
        TEST_ASSERT(short_hash[0] == 'a' && short_hash[2] == 'c' && short_hash[3] == 0,
                    "a short hash string copies what's there");
        TEST_ASSERT(next == 9 && odd_reader.good(), "and the cursor is past the whole string");
    }

    TEST_SECTION("bounds");
    {
        Packet packet;
        packet.write<u32>(0xDEADBEEF);
        packet.write<u8>(7);

        PacketReader reader{packet.data};
        const u32 first = reader.read<u32>();
        TEST_ASSERT_EQ(first, 0xDEADBEEFu, "u32 before the end");
        TEST_ASSERT_EQ(reader.remaining(), uSz{1}, "one byte left");
        const u32 past = reader.read<u32>();
        TEST_ASSERT_EQ(past, 0u, "a read past the end yields zero");
        TEST_ASSERT(!reader.good(), "and fails the reader");
        TEST_ASSERT_EQ(reader.remaining(), uSz{1}, "a failed read consumes nothing");
        const u8 after = reader.read<u8>();
        TEST_ASSERT_EQ(after, u8{0}, "reads after a failure yield zero even if the bytes are there");
        TEST_ASSERT(reader.read_span(1).empty(), "read_span after a failure is empty");
        const std::string no_string = reader.read_string();
        TEST_ASSERT(no_string.empty(), "read_string after a failure is empty");

        PacketReader skipper{packet.data};
        skipper.skip_bytes(5);
        TEST_ASSERT(skipper.good() && skipper.remaining() == 0, "skip_bytes to the exact end");
        skipper.skip_bytes(1);
        TEST_ASSERT(!skipper.good(), "skip_bytes past the end fails");

        PacketReader spanner{packet.data};
        const auto head = spanner.read_span(4);
        TEST_ASSERT(head.size() == 4 && head[0] == 0xEF && spanner.remaining() == 1, "read_span returns the bytes");
        TEST_ASSERT(spanner.read_span(2).empty() && !spanner.good(), "read_span past the end is empty and fails");

        // a string length far beyond the payload is neither trusted nor allocated for
        Packet lying;
        lying.write<u8>(0x0B);
        lying.write_uleb128(0xFFFFFFFF);
        lying.write<u32>(0);
        PacketReader liar{lying.data};
        const std::string nothing = liar.read_string();
        TEST_ASSERT(nothing.empty() && !liar.good(), "an oversized string length reads as empty and fails");
        PacketReader liar_skipper{lying.data};
        liar_skipper.skip_string();
        TEST_ASSERT(!liar_skipper.good(), "skipping an oversized string fails too");

        PacketReader empty{std::span<const u8>{}};
        TEST_ASSERT(empty.read<u8>() == 0 && !empty.good() && empty.remaining() == 0,
                    "reading an empty blob fails cleanly");
    }

    TEST_SECTION("room round trip");
    {
        Room room;
        room.id = 1234;
        room.in_progress = 1;
        room.match_type = 0;
        room.mods = static_cast<LegacyFlags>(72u);  // hidden + doubletime
        room.name = "Test Room";
        room.password = "hunter2";
        room.map_name = "artist - title [diff]";
        room.map_id = 987;
        room.map_md5 = MD5String{"0123456789abcdef0123456789abcdef"};
        room.host_id = 2;
        room.mode = 0;
        room.win_condition = WinCondition::SCOREV2;
        room.team_type = 1;
        room.freemods = 1;
        room.seed = 0xC0FFEE;
        for(auto &slot : room.slots) slot.status = 1;  // open
        room.slots[0].status = 4;                      // not ready (occupied by us)
        room.slots[0].player_id = 2;
        room.slots[0].team = 1;
        room.slots[0].mods = static_cast<LegacyFlags>(8u);  // hidden
        room.slots[3].status = 2;                           // locked
        room.slots[5].status = 8;                           // ready
        room.slots[5].player_id = 77;

        Packet packet;
        room.pack(packet);
        PacketReader reader{packet.data};
        const Room parsed(reader);
        TEST_ASSERT(reader.good() && reader.remaining() == 0, "Room::pack and Room(PacketReader) agree on the layout");
        TEST_ASSERT_EQ(parsed.id, room.id, "room id");
        TEST_ASSERT_EQ(parsed.in_progress, room.in_progress, "in progress");
        TEST_ASSERT(parsed.mods == room.mods, "room mods");
        TEST_ASSERT(parsed.has_password, "has_password comes from the password string's presence marker");
        TEST_ASSERT(parsed.password.empty(), "the password itself is discarded");
        TEST_ASSERT_EQ(parsed.name, room.name, "room name");
        TEST_ASSERT_EQ(parsed.map_name, room.map_name, "map name");
        TEST_ASSERT_EQ(parsed.map_id, room.map_id, "map id");
        TEST_ASSERT(parsed.map_md5 == room.map_md5, "map md5");
        TEST_ASSERT_EQ(parsed.host_id, room.host_id, "host id");
        TEST_ASSERT(parsed.win_condition == WinCondition::SCOREV2, "win condition");
        TEST_ASSERT_EQ(parsed.team_type, room.team_type, "team type");
        TEST_ASSERT_EQ(parsed.freemods, room.freemods, "freemods");
        TEST_ASSERT_EQ(parsed.seed, room.seed, "seed");
        TEST_ASSERT_EQ(parsed.nb_players, 2, "player count from the slot statuses");
        TEST_ASSERT_EQ(parsed.nb_open_slots, 15, "open slot count");
        TEST_ASSERT(
            parsed.slots[0].player_id == 2 && parsed.slots[0].team == 1 && parsed.slots[0].mods == room.slots[0].mods,
            "occupied slot");
        TEST_ASSERT(parsed.slots[5].player_id == 77 && parsed.slots[5].is_ready(), "ready slot");
        TEST_ASSERT(parsed.slots[3].is_locked() && !parsed.slots[3].has_player(), "locked slot");
    }

    TEST_SECTION("mods round trip");
    {
        using namespace flags::operators;
        Replay::Mods mods;
        mods.flags = ModFlags::Hidden | ModFlags::Autopilot | ModFlags::ARWobble;
        mods.speed = 1.25f;
        mods.notelock_type = 1;
        mods.ar_override = 9.5f;
        mods.autopilot_lenience = 0.7f;
        mods.arwobble_strength = 2.f;
        mods.arwobble_interval = 3.f;

        Packet packet;
        Replay::Mods::pack_and_write(packet, mods);
        PacketReader reader{packet.data};
        const Replay::Mods parsed = Replay::Mods::unpack(reader);
        TEST_ASSERT(reader.good() && reader.remaining() == 0, "Mods::pack_and_write and unpack agree on the layout");
        TEST_ASSERT(parsed == mods, "mods round trip");

        // the flag-dependent tail is what a truncated blob loses
        PacketReader truncated{std::span{packet.data}.first(packet.data.size() - 1)};
        (void)Replay::Mods::unpack(truncated);
        TEST_ASSERT(!truncated.good(), "a truncated mods blob fails the reader");
    }

    TEST_PRINT_RESULTS("PacketTest");
    engine->shutdown();
}

}  // namespace Mc::Tests
