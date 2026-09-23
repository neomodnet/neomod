// Copyright (c) 2025-26, WH, All rights reserved.

#include "ThumbnailManager.h"
#include "AsyncIOHandler.h"
#include "AsyncPool.h"
#include "Bancho.h"

#include "Downloader.h"
#include "DownloadHandle.h"
#include "Engine.h"
#include "Environment.h"
#include "File.h"
#include "Hashing.h"
#include "Image.h"
#include "Logging.h"
#include "OsuConVars.h"
#include "ResourceManager.h"
#include "Thread.h"

#include <algorithm>
#include <ctime>
#include <memory>
#include <string_view>
#include <vector>

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

struct ThumbnailManager::Impl final {
    NOCOPY_NOMOVE(Impl)
   public:
    Impl() = default;
    ~Impl();

    // only keep this many thumbnail Image resources loaded in VRAM at once
    static constexpr size_t MAX_LOADED_IMAGES{256};

    // cached files older than this are deleted (and downloaded again if still wanted)
    static constexpr i64 CACHE_MAX_AGE_SECONDS{7Z * 24 * 60 * 60};

    // entries are created by request_image and remain alive forever, but the actual Image resource
    // will be unloaded (by priority of access time) to keep VRAM/RAM usage sustainable
    struct ThumbEntry {
        enum class State : u8 {
            Unchecked,    // waiting for its cache directory's listing to say whether the file is on disk
            Downloading,  // not on disk; the download starts once it's among the most recently accessed pending entries
            Writing,      // downloaded, the file is being written
            Resolved,     // on disk; the Image is loaded lazily by try_get_image
            Blacklisted,  // gave up on it for this session
        };
        State state{State::Unchecked};
        bool retried{false};  // whether the download was already redone once after the file failed to write or decode
        u32 refcount{0};
        double last_access_time{
            0.0};               // timestamp of last try_get_image call; used for queue priority and VRAM eviction
        Image* image{nullptr};  // null if not loaded in memory
        Downloader::DownloadHandle dl_handle;
    };

    // a cache directory (avatars/<endpoint>, thumbs/<endpoint>) is listed once, off the main thread: files past
    // CACHE_MAX_AGE_SECONDS get deleted, the names of the rest tell which entries are on disk
    struct CacheDir {
        Async::Future<std::vector<std::string>> scan;
        Hash::flat::set<std::string, Hash::UnstableStringHash, std::equal_to<>> fresh_files;
        bool scanned{false};
    };

    void update();
    void request_image(const ThumbIdentifier& identifier);
    void discard_image(const ThumbIdentifier& identifier);
    const Image* try_get_image(const ThumbIdentifier& identifier);

    void load_image(const ThumbIdentifier& identifier, ThumbEntry& entry);
    void unload_image(ThumbEntry& entry);
    void prune_oldest_entries();
    // returns the image bytes once the download finished, empty while it's in flight or once the entry got blacklisted
    std::vector<u8> download_image(const ThumbIdentifier& identifier, ThumbEntry& entry);
    CacheDir& cache_dir_for(std::string_view dir);

    Hash::flat::map<ThumbIdentifier, ThumbEntry> images;
    Hash::flat::set<ThumbIdentifier> pending;  // the UNCHECKED/DOWNLOADING entries with refcount > 0
    Hash::unstable_stringmap<CacheDir> cache_dirs;
    size_t loaded_count{0};  // entries with an image

    // temporary buffers to avoid reallocations on each update
    std::vector<ThumbEntry*> loaded_entry_buf;
    std::vector<decltype(Impl::images)::value_type*> load_candidate_buf;

    // the file write callbacks handed to the async io handler run on the main thread, but the io queue is drained
    // after the app is destroyed, so they hold a weak ref to this instead of touching the manager unconditionally
    std::shared_ptr<bool> alive{std::make_shared<bool>(true)};
};

ThumbnailManager::Impl::~Impl() {
    for(auto& [identifier, entry] : this->images) {
        Downloader::abort_download(entry.dl_handle);
        if(entry.image) {
            resourceManager->destroyResource(entry.image);
        }
    }
}

const Image* ThumbnailManager::Impl::try_get_image(const ThumbIdentifier& identifier) {
    assert(McThread::is_main_thread());

    auto it = this->images.find(identifier);
    if(it == this->images.end()) {
        return nullptr;
    }

    const ThumbIdentifier& id = it->first;
    ThumbEntry& entry = it->second;
    entry.last_access_time = engine->getTime();

    // not yet downloaded/found on disk
    if(entry.state != ThumbEntry::State::Resolved) {
        return nullptr;
    }

    // lazy load if not in memory (won't block)
    if(!entry.image) {
        this->load_image(id, entry);
    }

    // return only if ready (async loading complete)
    if(entry.image->isReady()) {
        return entry.image;
    }

    if(entry.image->failedLoad()) {
        // the cached file is unreadable (e.g. truncated by a crash mid-write): throw it away and download it again, once
        this->unload_image(entry);
        Environment::deleteFile(id.save_path);
        if(entry.retried) {
            logIfCV(debug_thumbs, "blacklisting {}, the downloaded file doesn't decode either", id.id);
            entry.state = ThumbEntry::State::Blacklisted;
        } else {
            logIfCV(debug_thumbs, "cached file for {} failed to load, downloading it again", id.id);
            entry.retried = true;
            entry.state = ThumbEntry::State::Downloading;
            if(entry.refcount > 0) this->pending.insert(id);
        }
    }
    return nullptr;
}

void ThumbnailManager::Impl::update() {
    if(this->loaded_count > MAX_LOADED_IMAGES) {
        this->prune_oldest_entries();
    }

    // nothing to do
    if(this->pending.empty()) {
        return;
    }

    // pick up finished directory listings
    for(auto& [dir, cache_dir] : this->cache_dirs) {
        if(!cache_dir.scanned && cache_dir.scan.is_ready()) {
            for(auto& name : cache_dir.scan.get()) {
                cache_dir.fresh_files.insert(std::move(name));
            }
            cache_dir.scanned = true;
        }
    }

    // one pass over the pending entries: everything the listings say is on disk is resolved right away
    // (avoid i/o), the rest are download candidates
    auto& candidates = this->load_candidate_buf;
    candidates.clear();
    for(auto it = this->pending.begin(); it != this->pending.end();) {
        auto entry_it = this->images.find(*it);
        assert(entry_it != this->images.end());
        const ThumbIdentifier& id = entry_it->first;
        ThumbEntry& entry = entry_it->second;

        if(entry.state == ThumbEntry::State::Unchecked) {
            const std::string_view save_path{id.save_path};
            const uSz slash = save_path.rfind('/');
            const CacheDir& cache_dir = this->cache_dir_for(save_path.substr(0, slash));
            if(!cache_dir.scanned) {
                ++it;
                continue;
            }
            if(cache_dir.fresh_files.contains(save_path.substr(slash + 1))) {
                logIfCV(debug_thumbs, "{} is cached on disk", id.id);
                entry.state = ThumbEntry::State::Resolved;
                it = this->pending.erase(it);
                continue;
            }
            entry.state = ThumbEntry::State::Downloading;
        }

        candidates.push_back(&*entry_it);
        ++it;
    }

    // start/poll the downloads of the most recently accessed candidates only (a few per frame): the downloader itself
    // has no priorities, so this is what gets the thumbnails that are visible right now served first
    static constexpr uSz ELEMS_TO_CHECK{4};
    const uSz num_to_check = std::min(ELEMS_TO_CHECK, candidates.size());
    std::ranges::partial_sort(candidates, candidates.begin() + (sSz)num_to_check, std::ranges::greater{},
                              [](const auto* candidate) { return candidate->second.last_access_time; });

    for(uSz i = 0; i < num_to_check; ++i) {
        const ThumbIdentifier& id = candidates[i]->first;
        ThumbEntry& entry = candidates[i]->second;

        std::vector<u8> data = this->download_image(id, entry);
        if(entry.state == ThumbEntry::State::Blacklisted) {
            this->pending.erase(id);
        } else if(!data.empty()) {
            logIfCV(debug_thumbs, "downloaded {}, writing {}", id.id, id.save_path);
            entry.state = ThumbEntry::State::Writing;
            this->pending.erase(id);

            // write async
            io->write(id.save_path, std::move(data),
                      [alive = std::weak_ptr{this->alive}, this, key = id](bool success) {
                          if(alive.expired()) return;

                          auto written_it = this->images.find(key);
                          assert(written_it != this->images.end());
                          ThumbEntry& written = written_it->second;
                          if(success) {
                              written.state = ThumbEntry::State::Resolved;
                          } else if(written.retried) {
                              logIfCV(debug_thumbs, "blacklisting {}, writing it failed again", key.id);
                              written.state = ThumbEntry::State::Blacklisted;
                          } else {
                              // download it again (once), e.g. the cache directory might not have been there yet
                              logIfCV(debug_thumbs, "writing {} failed, downloading it again", key.id);
                              written.retried = true;
                              written.state = ThumbEntry::State::Downloading;
                              if(written.refcount > 0) this->pending.insert(key);
                          }
                      });
        }
    }
}

void ThumbnailManager::Impl::request_image(const ThumbIdentifier& identifier) {
    assert(McThread::is_main_thread());

    auto& entry = this->images[identifier];
    const u32 current_refcount = ++entry.refcount;
    logIfCV(debug_thumbs, "requested {}, refcount now {}", identifier.id, current_refcount);

    // the first live reference to an entry that isn't known to be on disk starts (or resumes) pursuing it
    if(current_refcount == 1 &&
       (entry.state == ThumbEntry::State::Unchecked || entry.state == ThumbEntry::State::Downloading)) {
        this->pending.insert(identifier);
    }
}

void ThumbnailManager::Impl::discard_image(const ThumbIdentifier& identifier) {
    assert(McThread::is_main_thread());
    auto it = this->images.find(identifier);
    assert(it != this->images.end());
    ThumbEntry& entry = it->second;
    assert(entry.refcount > 0);

    const u32 current_refcount = --entry.refcount;
    logIfCV(debug_thumbs, "discarded {}, refcount now {}", identifier.id, current_refcount);

    // nobody wants it anymore: stop pursuing it (a later request picks it up where it left off)
    if(current_refcount == 0 && this->pending.erase(identifier) > 0 && entry.dl_handle) {
        logIfCV(debug_thumbs, "cancelled in-progress download for {}", identifier.id);
        Downloader::abort_download(entry.dl_handle);
    }
}

void ThumbnailManager::Impl::load_image(const ThumbIdentifier& identifier, ThumbEntry& entry) {
    assert(!entry.image && entry.state == ThumbEntry::State::Resolved);

    resourceManager->requestNextLoadAsync();
    // the path *is* the resource name
    entry.image = resourceManager->loadImageAbs(identifier.save_path, identifier.save_path);
    assert(entry.image && "ThumbnailManager::load_image: malloc failed");
    ++this->loaded_count;
}

void ThumbnailManager::Impl::unload_image(ThumbEntry& entry) {
    assert(entry.image);
    resourceManager->destroyResource(entry.image);
    entry.image = nullptr;
    --this->loaded_count;
}

void ThumbnailManager::Impl::prune_oldest_entries() {
    // collect all loaded entries (images still being loaded can't be unloaded yet)
    auto& loaded_entries = this->loaded_entry_buf;
    loaded_entries.clear();
    for(auto& [identifier, entry] : this->images) {
        const Image* image = entry.image;
        if(image && (image->isReady() || image->failedLoad() || image->isInterrupted())) {
            loaded_entries.push_back(&entry);
        }
    }

    if(loaded_entries.size() <= MAX_LOADED_IMAGES) {
        return;
    }

    std::ranges::sort(loaded_entries, {}, &ThumbEntry::last_access_time);

    // unload oldest images (a bit more, to not constantly be unloading images for each new image added after we hit the limit once)
    const uSz to_unload = std::clamp<uSz>((uSz)(MAX_LOADED_IMAGES / 4.f), 0, loaded_entries.size() / 2);
    for(uSz i = 0; i < to_unload; ++i) {
        logIfCV(debug_thumbs, "unloading {} from memory due to age", loaded_entries[i]->image->getFilePath());
        this->unload_image(*loaded_entries[i]);
    }
}

std::vector<u8> ThumbnailManager::Impl::download_image(const ThumbIdentifier& identifier, ThumbEntry& entry) {
    // a fake-online session has no server backing it: never hit the network, just give up on
    // this thumbnail (blacklisted = won't be re-queued) so a default placeholder is shown
    if(BanchoState::fake_online) {
        logIfCV(debug_thumbs, "blacklisting {}, no server to download it from", identifier.id);
        entry.state = ThumbEntry::State::Blacklisted;
        return {};
    }

    // TODO: only download a single (response_code == 404) result and share it
    auto& dl = entry.dl_handle;
    // (a transfer aborted along with all others on a disconnect never completes, so that one gets requested anew)
    if(!dl || dl.cancelled()) dl = Downloader::download(identifier.download_url);
    if(!dl.completed()) return {};

    std::vector<u8> data = (dl.failed() || dl.response_code() != 200) ? std::vector<u8>{} : dl.take_data();
    dl.reset();
    if(data.empty()) {
        // network error, 404 and friends, or a 200 with an empty body
        logIfCV(debug_thumbs, "blacklisting {}, download failed", identifier.id);
        entry.state = ThumbEntry::State::Blacklisted;
    }
    return data;
}

ThumbnailManager::Impl::CacheDir& ThumbnailManager::Impl::cache_dir_for(std::string_view dir) {
    auto it = this->cache_dirs.find(dir);
    if(it != this->cache_dirs.end()) {
        return it->second;
    }

    logIfCV(debug_thumbs, "listing {}", dir);
    CacheDir& cache_dir = this->cache_dirs.try_emplace(std::string{dir}).first->second;
    cache_dir.scan = Async::submit(
        [dir = std::string{dir}] {
            std::vector<File::DirEntry> entries;
            File::getDirectoryEntries(dir, File::DirContents::FILES, entries);

            const i64 now = time(nullptr);
            std::vector<std::string> fresh;
            fresh.reserve(entries.size());
            for(auto& entry : entries) {
                // only the id-named files are ours (the main menu keeps the server icon in the avatars dir (TODO: ???))
                if(entry.type != File::FILETYPE::FILE ||
                   entry.name.find_first_not_of("-0123456789") != std::string::npos) {
                    continue;
                }
                if(now - entry.mtime > CACHE_MAX_AGE_SECONDS) {
                    logIfCV(debug_thumbs, "evicting expired {}/{}", dir, entry.name);
                    Environment::deleteFile(fmt::format("{}/{}", dir, entry.name));
                    continue;
                }
                fresh.push_back(std::move(entry.name));
            }
            return fresh;
        },
        Lane::Background);
    return cache_dir;
}

ThumbnailManager::ThumbnailManager() = default;
ThumbnailManager::~ThumbnailManager() = default;

void ThumbnailManager::update() { m_impl->update(); }
void ThumbnailManager::request_image(const ThumbIdentifier& identifier) { m_impl->request_image(identifier); }
void ThumbnailManager::discard_image(const ThumbIdentifier& identifier) { m_impl->discard_image(identifier); }
const Image* ThumbnailManager::try_get_image(const ThumbIdentifier& identifier) {
    return m_impl->try_get_image(identifier);
}
