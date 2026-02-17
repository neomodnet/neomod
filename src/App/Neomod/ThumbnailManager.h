// Copyright (c) 2025-2026, WH, All rights reserved.
#pragma once

#include "Image.h"
#include "DownloadHandle.h"
#include "Hashing.h"

#include <unordered_set>
#include <utility>

struct ThumbIdentifier {
    std::string save_path;
    std::string download_url;  // url without scheme prepended
    i32 id{0};

    bool operator==(const ThumbIdentifier&) const = default;
};

namespace ankerl::unordered_dense {
template <>
struct hash<::ThumbIdentifier> {
    using is_avalanching = void;

    u64 operator()(const ::ThumbIdentifier& thumb) const noexcept {
        u64 h = hash<std::string_view>{}(thumb.save_path);
        h ^= hash<std::string_view>{}(thumb.download_url) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
}  // namespace ankerl::unordered_dense

namespace std {
template <>
struct hash<ThumbIdentifier> : ::ankerl::unordered_dense::hash<ThumbIdentifier> {};
}  // namespace std

class ThumbnailManager final {
    NOCOPY_NOMOVE(ThumbnailManager)
   public:
    ThumbnailManager() { this->load_queue.reserve(128); };
    ~ThumbnailManager() { this->clear(); }

    // this is run during Osu::update(), while not in unpaused gameplay
    void update();

    // call this when you want to have some images ready soon
    // e.g. called by UIAvatar ctor to add new user id/folder avatar pairs to the loading queue (and tracking)
    void request_image(const ThumbIdentifier& identifier);

    // call this when you no longer care about some image you requested
    // e.g. called ~UIAvatar dtor (removes it from pending queue, to not load/download images we don't need)
    void discard_image(const ThumbIdentifier& identifier);

    // may return null if image is still loading
    [[nodiscard]] Image* try_get_image(const ThumbIdentifier& identifier);

   private:
    // only keep this many thumbnail Image resources loaded in VRAM at once
    static constexpr size_t MAX_LOADED_IMAGES{256};

    // entries are created by request_image and remain alive forever, but the actual Image resource
    // will be unloaded (by priority of access time) to keep VRAM/RAM usage sustainable
    struct ThumbEntry {
        u32 refcount{0};
        double last_access_time{
            0.0};               // timestamp of last try_get_image call; used for queue priority and VRAM eviction
        std::string file_path;  // empty until downloaded/found on disk
        Image* image{nullptr};  // null if not loaded in memory
        Downloader::DownloadHandle dl_handle;
    };
    static Image* load_image(const ThumbEntry& entry);

    void prune_oldest_entries();
    bool download_image(const ThumbIdentifier& identifier);
    void clear();

    Hash::flat::map<ThumbIdentifier, ThumbEntry> images;
    std::vector<ThumbIdentifier> load_queue;
    Hash::flat::set<ThumbIdentifier> id_blacklist;
    std::vector<u8> temp_img_download_data;  // if it has something in it, we just downloaded something
};
