// Copyright (c) 2026, WH, All rights reserved.
#include "SkinArchive.h"

#include "Archival.h"
#include "AsyncPool.h"
#include "ConVar.h"
#include "Environment.h"
#include "File.h"
#include "Hashing.h"
#include "Logging.h"
#include "Paths.h"
#include "SString.h"
#include "Skin.h"
#include "SyncMutex.h"

#include "fmt/chrono.h"

#include <algorithm>
#include <atomic>
#include <ctime>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace SkinArchive {
namespace {

// .osk entry names are Shift-JIS, like .osz ones
constexpr std::string_view ARCHIVE_CHARSET{"CP932"};

// deeper than any skin goes, and an end to symlink loops
constexpr int MAX_FOLDER_DEPTH = 16;

// for the names of archives that are still being written
std::atomic<u32> part_serial{0};

// (exports pick a free name one at a time, so that two of the same skin can't take the same one)
Sync::mutex naming_mutex;

// where a file comes from, in the order the skin loader looks
enum class Tier : u8 { Skin, Fallback, Default };

// a folder that files get taken from
struct Source {
    std::string dir;
    std::string name;
    Tier tier;
    std::vector<std::string> packed;  // (for the log)
};

struct Entry {
    std::string name;  // in the archive
    std::string disk_path;
    size_t source;
};

// what submit_export() takes from the skin and the convars for the background thread
struct Job {
    std::string name;  // of the export, empty to make one up
    std::string skin_name;
    std::string skin_dir;
    std::string fallback_dir;  // empty without a fallback skin
    std::string fallback_name;
    std::string default_dir;                 // empty if that's the skin being exported
    std::vector<Skin::ExportFile> resolved;  // the files the loader found the elements in
    std::string out_dir;
    std::string version;
    bool mixed;  // from random elements, which don't come from the skin's own folder
    bool include_default;
};

// the element a file is to the skin loader: an image's @2x and plain variants are one, and so are the formats a
// sound can come in
std::string element_key(std::string_view path) {
    std::string key = SString::to_lower(path);
    const size_t dot = key.rfind('.');
    if(dot == std::string::npos || key.find('/', dot) != std::string::npos) return key;

    if(std::ranges::contains(Skin::SOUND_EXTENSIONS, std::string_view{key}.substr(dot))) {
        key.replace(dot, std::string::npos, ".<sound>");
    } else if(std::string_view{key}.substr(0, dot).ends_with("@2x")) {
        key.erase(dot - 3, 3);
    }
    return key;
}

// operating system clutter, not part of a skin
bool is_junk(std::string_view name) {
    const std::string lower = SString::to_lower(name);
    return lower.starts_with('.') || lower == "__macosx" || lower == "thumbs.db" || lower == "desktop.ini";
}

// every file below dir + rel, relative to dir
void list_files(const std::string &dir, const std::string &rel, int depth, std::vector<std::string> &files,
                std::vector<std::string> &skipped) {
    std::vector<File::DirEntry> entries;
    if(depth > MAX_FOLDER_DEPTH || !File::getDirectoryEntries(dir + rel, File::DirContents::ALL, entries, false)) {
        skipped.push_back(fmt::format("{} (couldn't be read)", rel));
        return;
    }

    for(auto &entry : entries) {
        std::string path = rel + entry.name;
        if(is_junk(entry.name)) {
            skipped.push_back(fmt::format("{} (system file)", path));
        } else if(entry.type == File::FILETYPE::FOLDER) {
            list_files(dir, path + '/', depth + 1, files, skipped);
        } else if(entry.type == File::FILETYPE::FILE) {
            files.push_back(std::move(path));
        }
    }
}

std::string folder_name(std::string dir) {
    while(dir.ends_with('/')) dir.pop_back();
    return Environment::getFileNameFromFilePath(dir);
}

// one that works on every system, and that makes a skin folder that can be picked (unlike one called "default")
std::string file_name(std::string name) {
    std::ranges::replace_if(
        name, [](char c) -> bool { return (static_cast<u8>(c) < 0x20) || R"(<>:"/\|?*)"sv.contains(c); }, '_');
    if(name.starts_with('.')) name[0] = '_';
    if(SString::to_lower(name) == "default") name = PACKAGE_NAME " default";
    return name;
}

void append_list(std::string &log, std::string_view heading, const std::vector<std::string> &items) {
    if(items.empty()) return;
    log += fmt::format("\n{}:\n", heading);
    for(const auto &item : items) log += fmt::format("  {}\n", item);
}

std::string file_count(size_t n) { return fmt::format("{} file{}", n, n == 1 ? "" : "s"); }

ExportResult run(const Job &job) {
    using enum ExportResult::Status;
    const std::time_t now = std::time(nullptr);
    const auto out_base = [&job](std::string name) { return job.out_dir + file_name(std::move(name)); };

    std::vector<Source> sources;
    const auto source_of = [&](const std::string &dir) -> size_t {
        if(const auto it = std::ranges::find(sources, dir, &Source::dir); it != sources.end()) {
            return it - sources.begin();
        }
        if(dir == job.default_dir) {
            sources.push_back({.dir = dir, .name = "the default skin", .tier = Tier::Default, .packed = {}});
        } else if(dir == job.fallback_dir) {
            sources.push_back({.dir = dir, .name = job.fallback_name, .tier = Tier::Fallback, .packed = {}});
        } else {
            // (the skin's own, or a random one in a mix)
            sources.push_back({.dir = dir,
                               .name = dir == job.skin_dir ? job.skin_name : folder_name(dir),
                               .tier = Tier::Skin,
                               .packed = {}});
        }
        return sources.size() - 1;
    };

    std::vector<Entry> entries;
    std::vector<std::string> skipped;
    Hash::unstable_ncase_set<std::string> names;  // (case-insensitive, like the file systems of most players)
    Hash::unstable_stringmap<size_t> provided;    // element key -> the source it's taken from

    // the skin's own folder, all of it (including what the skin loader doesn't know about)
    if(!job.mixed) {
        if(!Environment::directoryExists(job.skin_dir)) {
            debugLog("skin folder {} is gone, can't export it", job.skin_dir);
            return {.status = Failed, .path = out_base(job.name.empty() ? job.skin_name : job.name) + ".osk"};
        }

        const size_t own = source_of(job.skin_dir);
        std::vector<std::string> files;
        list_files(job.skin_dir, "", 0, files, skipped);
        std::ranges::sort(files);
        for(auto &path : files) {
            if(SString::to_lower(path) == SString::to_lower(LOG_NAME)) {
                skipped.push_back(fmt::format("{} (log of an earlier export)", path));
            } else if(!names.insert(path).second) {
                skipped.push_back(
                    fmt::format("{} (another file has the same name, apart from upper/lower case)", path));
            } else {
                provided.try_emplace(element_key(path), own);
                entries.push_back({.name = path, .disk_path = job.skin_dir + path, .source = own});
            }
        }
    }

    // the files the loader found the elements in, by the order it looks: the first folder with an element supplies it
    std::vector<std::string> left_to_default;
    {
        std::vector<Entry> found;
        found.reserve(job.resolved.size());
        for(const auto &[dir, path, name] : job.resolved) {
            found.push_back({.name = name, .disk_path = path, .source = source_of(dir)});
        }
        std::ranges::stable_sort(found, {}, [&sources](const Entry &e) { return sources[e.source].tier; });

        for(auto &file : found) {
            const std::string key = element_key(file.name);
            if(const auto it = provided.find(key); it != provided.end() && it->second != file.source) {
                // (in a mix, two random skins can supply a file of the same name, e.g. score and combo digits)
                if(job.mixed) {
                    skipped.push_back(fmt::format("{} from {} (the one from {} is in)", file.name,
                                                  sources[file.source].name, sources[it->second].name));
                }
                continue;
            }

            if(sources[file.source].tier == Tier::Default && !job.include_default) {
                left_to_default.push_back(std::move(file.name));
            } else if(names.insert(file.name).second) {
                provided.try_emplace(key, file.source);
                entries.push_back(std::move(file));
            }
        }

        std::ranges::sort(left_to_default);
        const auto [first, last] = std::ranges::unique(left_to_default);
        left_to_default.erase(first, last);
    }

    Archive::Writer ar(ARCHIVE_CHARSET);
    for(const auto &entry : entries) {
        if(!ar.addFile(entry.disk_path, entry.name)) {
            skipped.push_back(fmt::format("{} (couldn't be read)", entry.name));
            continue;
        }
        Source &source = sources[entry.source];
        const auto from = std::string_view{entry.disk_path}.substr(source.dir.size());
        source.packed.push_back(from == entry.name ? entry.name : fmt::format("{} (from {})", entry.name, from));
    }

    std::string name = job.name;
    if(name.empty() && job.mixed) {
        name = fmt::format("random mix {:%F %H-%M-%S}", fmt::gmtime(now));
    } else if(name.empty()) {
        const bool filled_in = std::ranges::any_of(
            sources, [](const Source &source) { return source.tier == Tier::Fallback && !source.packed.empty(); });
        name = filled_in ? fmt::format("{} + {}", job.skin_name, job.fallback_name) : job.skin_name;
    }
    const std::string base = out_base(std::move(name));
    ExportResult res{.status = Failed, .path = base + ".osk"};

    {
        std::string log = fmt::format("{} skin export\n\n", PACKAGE_NAME);
        if(job.mixed) {
            const auto skins = std::ranges::count_if(
                sources, [](const Source &source) { return source.tier != Tier::Default && !source.packed.empty(); });
            log += fmt::format("skin: a mix of random elements from {} skins (skin_random_elements)\n", skins);
        } else {
            log += fmt::format("skin: {}\n", job.skin_name);
        }
        if(!job.fallback_dir.empty()) log += fmt::format("fallback skin: {}\n", job.fallback_name);
        log += fmt::format("exported: {:%F %T} UTC with {}\n", fmt::gmtime(now), job.version);

        for(const Tier tier : {Tier::Skin, Tier::Fallback, Tier::Default}) {
            for(const auto &source : sources) {
                if(source.tier != tier || source.packed.empty()) continue;
                const std::string count = file_count(source.packed.size());
                if(tier == Tier::Skin && !job.mixed) {
                    // (all of its folder, no need to list it)
                    log += fmt::format("\n{} from {}\n", count, source.name);
                } else {
                    append_list(
                        log,
                        fmt::format("{} {} {}", count, tier == Tier::Skin ? "from" : "filled in from", source.name),
                        source.packed);
                }
            }
        }
        append_list(log, "left to the default skin (set skin_export_include_default 1 to include)", left_to_default);
        append_list(log, "skipped", skipped);
        ar.addData(LOG_NAME, std::span{reinterpret_cast<const u8 *>(log.data()), log.size()});
    }

    if(!Environment::directoryExists(job.out_dir) && !Environment::createDirectory(job.out_dir)) {
        debugLog("couldn't create {} to export the skin into", job.out_dir);
        return res;
    }

    // (written under another name first, then renamed to the first free one of <name>.osk, <name> (1).osk, ...)
    const std::string part_path = fmt::format("{}.{}.part", res.path, part_serial++);
    bool written = ar.writeToFile(part_path, false);
    if(written) {
        Sync::scoped_lock lock(naming_mutex);
        for(int n = 1; Environment::fileExists(std::string_view{res.path}); n++) {
            res.path = fmt::format("{} ({}).osk", base, n);
        }
        written = Environment::renameFile(part_path, res.path);
    }
    if(!written) {
        debugLog("couldn't write {}", res.path);
        Environment::deleteFile(part_path);
        return res;
    }

    debugLog("exported skin {} to {} ({} files)", job.skin_name, res.path, ar.getEntryCount());
    res.status = Exported;
    return res;
}

}  // namespace

Async::Future<ExportResult> submit_export(const Skin &skin, std::string dir, std::string name, bool include_default) {
    if(skin.is_default && !include_default) {
        return Async::make_ready_future(ExportResult{.status = ExportResult::Status::DefaultSkin, .path = {}});
    }

    if(!dir.ends_with('/')) dir.push_back('/');
    SString::trim_inplace(name);
    if(SString::to_lower(name).ends_with(".osk")) name.erase(name.size() - 4);

    std::string fallback_dir, fallback_name;
    if(skin.search_dirs.size() > 1 && skin.search_dirs[1] == skin.fallback_dir) {
        fallback_dir = skin.fallback_dir;
        fallback_name = folder_name(fallback_dir);
    }

    Job job{
        .name = std::move(name),
        .skin_name = skin.name,
        .skin_dir = skin.skin_dir,
        .fallback_dir = std::move(fallback_dir),
        .fallback_name = std::move(fallback_name),
        .default_dir = skin.is_default ? std::string{} : Mc::Paths::materials() + "/default/",
        .resolved = skin.files_for_export,
        .out_dir = std::move(dir),
        .version = fmt::format("{} {:.2f} ({})", PACKAGE_NAME, cv::version.getFloat(), cv::build_timestamp.getString()),
        .mixed = skin.hasRandomElements(),
        .include_default = include_default,
    };

    return Async::submit([job = std::move(job)]() -> ExportResult { return run(job); }, Lane::Background);
}

bool unpack(std::string_view osk_path, std::string_view skins_dir) {
    auto skin_name = Environment::getFileNameFromFilePath(osk_path);
    debugLog("Extracting {:s}...", skin_name.c_str());
    skin_name.erase(skin_name.size() - 4);  // remove .osk extension

    auto skin_root = fmt::format("{}/{}/", skins_dir, skin_name);

    std::unique_ptr<u8[]> fileBuffer;
    size_t fileSize{0};
    {
        File file(osk_path);
        if(!file.canRead() || !(fileSize = file.getFileSize())) {
            debugLog("Failed to read skin file {:s}", osk_path);
            return false;
        }
        fileBuffer = file.takeFileBuffer();
        // close the file here
    }

    Archive::Reader archive({fileBuffer.get(), fileSize}, ARCHIVE_CHARSET);
    if(!archive.isValid()) {
        debugLog("Failed to open .osk file");
        return false;
    }

    auto entries = archive.getAllEntries();
    if(entries.empty()) {
        debugLog(".osk file is empty!");
        return false;
    }

    // skins zipped as a folder instead of its contents would extract to skins/name/name/,
    // so if every file lives under the same top-level folder, strip that folder while extracting
    std::string common_root;
    for(const auto &entry : entries) {
        if(entry.isDirectory()) continue;

        std::string filename = entry.getFilename();
        File::normalizeSlashes(filename, '\\', '/');

        const auto slash = filename.find('/');
        const auto root =
            (slash == std::string::npos) ? std::string_view{} : std::string_view{filename}.substr(0, slash);
        if(root.empty() || (!common_root.empty() && common_root != root)) {
            common_root.clear();
            break;
        }
        common_root = root;
    }

    if(!Environment::directoryExists(skin_root)) {
        Environment::createDirectory(skin_root);
    }

    for(const auto &entry : entries) {
        if(entry.isDirectory()) continue;

        std::string filename = entry.getFilename();
        File::normalizeSlashes(filename, '\\', '/');
        if(!common_root.empty()) filename.erase(0, common_root.size() + 1);

        const auto folders = SString::split(filename, '/');
        // security check: skip files with path traversal attempts
        if(std::ranges::contains(folders, std::string_view{".."})) continue;

        std::string file_path = skin_root;
        for(const auto &folder : folders) {
            if(!Environment::directoryExists(file_path)) {
                Environment::createDirectory(file_path);
            }
            file_path.push_back('/');
            file_path.append(folder);
        }

        // when a file can't be extracted we just ignore it (as long as the archive is valid)
        if(!entry.extractToFile(file_path)) {
            debugLog("Failed to extract skin file {:s}", filename.c_str());
        }
    }

    return true;
}

}  // namespace SkinArchive
