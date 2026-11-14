// Copyright (c) 2026, WH, All rights reserved.
#include "EditorHistory.h"

#include "ByteBufferedFile.h"
#include "ContainerRanges.h"
#include "Deflate.h"
#include "Environment.h"
#include "Hashing.h"
#include "Logging.h"
#include "Paths.h"
#include "crypto.h"

#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace EditorHistory {
namespace {
constexpr u8 FILE_VERSION{1};

enum class Part : u8 { OBJECTS, TIMING, OTHER };

// every line in the map text that says something, with the map section it's in ([Editor] left out)
template <typename F>
void eachLine(std::span<const u8> bytes, F &&fn) {
    std::string_view text{reinterpret_cast<const char *>(bytes.data()), bytes.size()};
    std::optional<Part> part{Part::OTHER};
    while(!text.empty()) {
        const uSz end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        if(!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if(line.empty()) continue;
        if(line.front() == '[' && line.back() == ']') {
            part = line == "[HitObjects]"     ? std::optional{Part::OBJECTS}
                   : line == "[TimingPoints]" ? std::optional{Part::TIMING}
                   : line == "[Editor]"       ? std::nullopt
                                              : std::optional{Part::OTHER};
            continue;
        }
        if(part) fn(*part, line);
    }
}

u32 objectsIn(std::span<const u8> bytes) {
    u32 objects = 0;
    eachLine(bytes, [&](Part part, std::string_view) { objects += part == Part::OBJECTS; });
    return objects;
}

// <editor>/history/<the map folder in maps/>: one directory per difficulty in it
std::string folderOf(const Database::MapLocation &location) {
    return fmt::format("{}/history/{}", Mc::Paths::editor(), Database::standIn(location).folder);
}

// the first bytes in a file: listing versions reads no content
std::optional<Version> readHeader(ByteBufferedFile::Reader &in, std::string path) {
    Version v;
    v.path = std::move(path);
    if(in.read<u8>() != FILE_VERSION) return std::nullopt;
    v.time = in.read<i64>();
    v.saved = in.read<u8>() != 0;
    if(!in.read_hash_digest(v.md5)) return std::nullopt;
    v.file = in.read_string();
    v.objects = in.read<u32>();
    v.changes = {.objectsAdded = in.read<u32>(),
                 .objectsRemoved = in.read<u32>(),
                 .timingAdded = in.read<u32>(),
                 .timingRemoved = in.read<u32>(),
                 .otherLines = in.read<u32>()};
    return in.good() ? std::optional{std::move(v)} : std::nullopt;
}

// the versions a difficulty has, the latest first
std::vector<Version> list(const std::string &dir) {
    std::vector<Version> versions;
    for(const std::string &name : Environment::getFilesInFolder(dir)) {
        // a write that was cut short leaves its .tmp
        if(!name.ends_with(".snap")) continue;
        std::string path = fmt::format("{}/{}", dir, name);
        ByteBufferedFile::Reader in(path);
        if(auto version = readHeader(in, std::move(path))) versions.push_back(std::move(*version));
    }
    Mc::ranges::stable_sort(versions, [](const Version &a, const Version &b) { return a.time > b.time; });
    return versions;
}

// the difficulty a file belongs to: the one whose latest version has this content, else the one whose latest version
// was this file (its directory and versions; none: it has none yet). only the latest counts: difficulties made from
// the same content share their first version
std::optional<std::pair<std::string, std::vector<Version>>> find(const std::string &folder, const MD5Hash &md5,
                                                                 std::string_view file) {
    std::optional<std::pair<std::string, std::vector<Version>>> byName;
    for(const std::string &name : Environment::getFoldersInFolder(folder)) {
        std::string dir = fmt::format("{}/{}", folder, name);
        std::vector<Version> versions = list(dir);
        if(versions.empty()) continue;
        if(versions.front().md5 == md5) return std::pair{std::move(dir), std::move(versions)};
        if(!byName && versions.front().file == file) byName = std::pair{std::move(dir), std::move(versions)};
    }
    return byName;
}

bool write(const std::string &dir, const Version &v, std::span<const u8> content) {
    const std::vector<u8> packed = Mc::Deflate::pack(content);
    ByteBufferedFile::Writer file(fmt::format("{}/{:020}-{}.snap", dir, v.time, v.md5));
    file.write<u8>(FILE_VERSION);
    file.write<i64>(v.time);
    file.write<u8>(v.saved);
    file.write_hash_digest(v.md5);
    file.write_string(v.file);
    file.write<u32>(v.objects);
    for(const u32 count : {v.changes.objectsAdded, v.changes.objectsRemoved, v.changes.timingAdded,
                           v.changes.timingRemoved, v.changes.otherLines}) {
        file.write<u32>(count);
    }
    file.write<u32>((u32)content.size());
    file.write<u32>((u32)packed.size());
    file.write_bytes(packed);
    if(file.commit()) return true;
    debugLog("couldn't keep a version of {}: {}", v.file, file.error());
    return false;
}
}  // namespace

Changes compare(std::span<const u8> from, std::span<const u8> to) {
    // what `from` has of each line, taken off by the lines of `to`: what's left is gone, what wasn't there is new
    std::array<Hash::flat::map<std::string_view, i32>, 3> lines;
    eachLine(from, [&](Part part, std::string_view line) { lines[(uSz)part][line]++; });
    std::array<u32, 3> added{};
    eachLine(to, [&](Part part, std::string_view line) {
        if(const auto it = lines[(uSz)part].find(line); it != lines[(uSz)part].end() && it->second > 0) {
            it->second--;
        } else {
            added[(uSz)part]++;
        }
    });
    std::array<u32, 3> removed{};
    for(uSz part = 0; part < lines.size(); part++) {
        for(const auto &[line, count] : lines[part]) removed[part] += (u32)count;
    }
    return {.objectsAdded = added[(uSz)Part::OBJECTS],
            .objectsRemoved = removed[(uSz)Part::OBJECTS],
            .timingAdded = added[(uSz)Part::TIMING],
            .timingRemoved = removed[(uSz)Part::TIMING],
            .otherLines = added[(uSz)Part::OTHER] + removed[(uSz)Part::OTHER]};
}

void record(std::string_view was, const MD5Hash &held, std::span<const u8> before, const Database::MapLocation &now,
            std::span<const u8> after, i64 time) {
    const std::string folder = folderOf(now);
    auto found = find(folder, held, was);
    const std::string dir = found ? found->first : fmt::format("{}/{}-{}", folder, time, held);
    std::vector<Version> versions = found ? std::move(found->second) : std::vector<Version>{};
    if(!Environment::createDirectory(dir)) {
        debugLog("couldn't make {}", dir);
        return;
    }

    // what the save overwrote, unless that's the latest version
    if(versions.empty() || versions.front().md5 != held) {
        Version foundVersion{.path = {},
                             .time = time - 1,
                             .saved = false,
                             .md5 = held,
                             .file = std::string{was},
                             .objects = objectsIn(before),
                             .changes = {}};
        if(!versions.empty()) {
            if(const auto latest = read(versions.front())) foundVersion.changes = compare(*latest, before);
        }
        write(dir, foundVersion, before);
    }
    write(dir,
          Version{.path = {},
                  .time = time,
                  .saved = true,
                  .md5 = crypto::hash::md5(after),
                  .file = now.file,
                  .objects = objectsIn(after),
                  .changes = compare(before, after)},
          after);

    // no more than MAX_VERSIONS: the oldest saved ones go, the first one found stays
    versions = list(dir);
    for(uSz i = versions.size(); versions.size() > MAX_VERSIONS && i-- > 0;) {
        if(i == versions.size() - 1 && !versions[i].saved) continue;
        Environment::deleteFile(versions[i].path);
        versions.erase(versions.begin() + (sSz)i);
    }
}

std::vector<Version> versionsOf(const Database::MapLocation &location, const MD5Hash &md5) {
    auto found = find(folderOf(location), md5, location.file);
    return found ? std::move(found->second) : std::vector<Version>{};
}

std::optional<std::vector<u8>> read(const Version &version) {
    ByteBufferedFile::Reader in(version.path);
    if(!readHeader(in, version.path)) return std::nullopt;
    const u32 size = in.read<u32>();
    const u32 packedSize = in.read<u32>();
    if(!in.good() || packedSize > in.get_total_size() - in.get_total_pos()) return std::nullopt;
    std::vector<u8> packed(packedSize);
    if(in.read_bytes(packed.data(), packed.size()) != packed.size()) return std::nullopt;
    return Mc::Deflate::unpack(packed, size);
}
}  // namespace EditorHistory
