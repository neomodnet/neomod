#pragma once
#include "types.h"

#include <memory>
#include <vector>
#include <string>

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

// Start an HTTP download. Deduplicates by URL.
DownloadHandle download(std::string_view url);

// Downloads and extracts given beatmapset.
// Returns true when files are on disk. Check handle.failed() on failure.
bool download_beatmapset(u32 set_id, DownloadHandle &handle);

// Downloads given beatmap (unless it already exists).
// Returns the beatmap when ready, nullptr while in-progress or on failure.
// Check handle.failed() to distinguish.
DatabaseBeatmap *download_beatmap(i32 beatmap_id, MD5Hash beatmap_md5, DownloadHandle &handle);
DatabaseBeatmap *download_beatmap(i32 beatmap_id, i32 beatmapset_id, DownloadHandle &handle);
void process_beatmapset_info_response(const Packet &packet);

i32 extract_beatmapset_id(const u8 *data, size_t data_s);
bool extract_beatmapset(const u8 *data, size_t data_s, std::string &map_dir);

}  // namespace Downloader
