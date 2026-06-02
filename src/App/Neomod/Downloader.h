#pragma once
#include "types.h"

#include <memory>
#include <vector>
#include <string>
#include <span>

class DatabaseBeatmap;
struct MD5Hash;
struct Packet;

namespace Downloader {

struct Request;

// RAII handle for download lifetime management.
// When all handles for a download drop, the queue entry becomes eligible for cleanup.
class DownloadHandle;

struct BeatmapMetadata {
    std::string diffname;
    float star_rating{0.f};
    u8 mode{0};
};

struct BeatmapSetMetadata {
    std::string osz_filename{};
    std::string artist{};
    std::string title{};
    std::string creator{};
    u8 ranking_status{0};
    f32 avg_user_rating{10.0};
    u64 last_update{0};  // TODO: wrong type?
    i32 set_id{0};
    i32 topic_id{0};
    bool has_video{false};
    bool has_storyboard{false};
    u64 osz_filesize{0};
    u64 osz_filesize_novideo{0};

    std::vector<BeatmapMetadata> beatmaps;
};

BeatmapSetMetadata parse_beatmapset_metadata(std::string_view server_response);

void abort_downloads();

// Cancel a single in-flight/queued download: aborts the transfer, drops the queue entry,
// resets the passed handle, and lets the next queued download start.
void abort_download(DownloadHandle &handle);

// Start an HTTP download. Deduplicates by URL.
DownloadHandle download(std::string_view url);

// Downloads and extracts given beatmapset.
// Returns true when files are on disk. Check handle.failed() on failure.
bool download_beatmapset(u32 set_id, DownloadHandle &handle);

// Resolves a beatmap_id to its containing beatmapset_id.
// - set_id_hint > 0: cache and return immediately, skipping the network lookup.
// - returns: positive set_id (resolved), 0 (still resolving / in flight), -1 (resolution failed).
// Main-thread only.
i32 resolve_beatmapset_id_for(i32 beatmap_id, i32 set_id_hint = 0);

void process_beatmapset_info_response(const Packet &packet);

// extract an archive whose beatmapset id is already known, into map_dir.
bool extract_beatmapset(std::span<const u8> data, const std::string &map_dir);

// extract a local .osz: resolves the beatmapset id from the archive (or fallback_id if the .osu files
// don't declare one), extracts into maps/<id>/, and returns the resolved id (-1 if no valid id).
i32 resolve_and_extract_osz(std::span<const u8> data, std::string_view osz_name, i32 fallback_id);

}  // namespace Downloader
