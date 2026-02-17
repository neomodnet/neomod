// Copyright (c) 2025-26, WH, All rights reserved.

#include "ThumbnailManager.h"
#include "AsyncIOHandler.h"

#include "Downloader.h"
#include "Graphics.h"
#include "ResourceManager.h"
#include "Engine.h"
#include "OsuConVars.h"
#include "File.h"
#include "Logging.h"
#include "Thread.h"
#include "Timing.h"

#include <sys/stat.h>

Image* ThumbnailManager::try_get_image(const ThumbIdentifier& identifier) {
    assert(McThread::is_main_thread());

    auto it = this->images.find(identifier);
    if(it == this->images.end()) {
        return nullptr;
    }

    ThumbEntry& entry = it->second;
    entry.last_access_time = engine->getTime();

    // not yet downloaded/found on disk
    if(entry.file_path.empty()) {
        return nullptr;
    }

    // lazy load if not in memory (won't block)
    if(!entry.image) {
        entry.image = this->load_image(entry);
    }

    // return only if ready (async loading complete)
    if(entry.image->isReady()) {
        return entry.image;
    } else if(entry.image->failedLoad()) {
        // blacklist files we couldn't load
        this->id_blacklist.insert(it->first);
        entry.last_access_time = 0.0;  // deprioritize it completely
    }
    return nullptr;
}

// this is run during Osu::update(), while not in unpaused gameplay
void ThumbnailManager::update() {
    const uSz cur_load_queue_size = this->load_queue.size();

    // nothing to do
    if(cur_load_queue_size == 0) {
        return;
    }

    // remove oldest entries if we have too many loaded
    this->prune_oldest_entries();

    // process 4 elements at a time from the download queue
    // we might not drain it fully due to only checking download progress once,
    // but we'll check again next update
    static constexpr const uSz ELEMS_TO_CHECK{4};

    // sort by priority: items that had try_get_image called recently come first
    std::ranges::stable_sort(this->load_queue, [this](const ThumbIdentifier& a, const ThumbIdentifier& b) {
        return this->images[a].last_access_time > this->images[b].last_access_time;
    });

    for(uSz i = 0, num_checked = 0; num_checked < ELEMS_TO_CHECK && i < this->load_queue.size(); ++num_checked, ++i) {
        auto& identifier = this->load_queue[i];

        bool exists_on_disk = false;
        struct stat64 attr;
        if(File::stat_c(identifier.save_path.c_str(), &attr) == 0) {
            time_t now = time(nullptr);
            struct tm expiration_date;
            localtime_x(&attr.st_mtime, &expiration_date);
            expiration_date.tm_mday += 7;
            if(now <= mktime(&expiration_date)) {
                exists_on_disk = true;
            }
        }

        // if we have the file or the download just finished, mark the entry as resolved
        // but only actually load the image when it's needed (in try_get_image)
        bool newly_downloaded = false;
        if(exists_on_disk) {
            this->images[identifier].file_path = identifier.save_path;
        } else if((newly_downloaded = this->download_image(identifier))) {
            // write async
            io->write(identifier.save_path, std::move(this->temp_img_download_data),
                      [&images = this->images, key = identifier](bool success) -> void {
                          if(engine->isShuttingDown())
                              return;  // dirty but there's not really a better way to detect this scenario atm
                          if(success) {
                              images[key].file_path = key.save_path;
                          }
                      });
        }

        if(exists_on_disk || newly_downloaded) {
            this->load_queue.erase(this->load_queue.begin() + (sSz)i);  // remove it from the queue
        }
    }
}

void ThumbnailManager::request_image(const ThumbIdentifier& identifier) {
    assert(McThread::is_main_thread());
    const bool debug = cv::debug_thumbs.getBool();

    // increment refcount even if we didn't add to load queue
    auto& entry = this->images[identifier];
    const u32 current_refcount = ++entry.refcount;
    logIf(debug, "trying to add {} to load queue, current refcount: {}", identifier.id, current_refcount);

    if(current_refcount > 1 || this->id_blacklist.contains(identifier) || !entry.file_path.empty()) {
        logIf(debug, "not adding {} to load queue, {}", identifier.id,
              current_refcount > 1 ? "refcount > 1" : (!entry.file_path.empty() ? "already have it" : "blacklisted"));
        return;
    }

    if(resourceManager->getImage(identifier.save_path)) {
        // shouldn't happen...
        logIf(debug, "{} already tracked by ResourceManager, not adding", identifier.save_path);
        return;
    }

    // avoid duplicates in queue
    if(!std::ranges::contains(this->load_queue, identifier)) {
        logIf(debug, "added {} to load queue", identifier.id);
        this->load_queue.push_back(identifier);
    }
}

void ThumbnailManager::discard_image(const ThumbIdentifier& identifier) {
    assert(McThread::is_main_thread());
    const u32 current_refcount = --this->images[identifier].refcount;
    logIfCV(debug_thumbs, "current refcount for {} is {}", identifier.id, current_refcount);

    if(current_refcount == 0) {
        // dequeue if it's waiting to be loaded, that's all
        if(std::erase(this->load_queue, identifier) > 0) {
            logIfCV(debug_thumbs, "removed {} from load queue", identifier.id);
        }
    }
}

Image* ThumbnailManager::load_image(const ThumbEntry& entry) {
    assert(!entry.image && !entry.file_path.empty());

    resourceManager->requestNextLoadAsync();
    // the path *is* the resource name
    Image *ret = resourceManager->loadImageAbs(entry.file_path, entry.file_path);
    assert(ret && "ThumbnailManager::load_image: malloc failed");
    return ret;
}

void ThumbnailManager::prune_oldest_entries() {
    // don't even do anything if we're not close to the limit (incl. unloaded)
    if(this->images.size() <= (uSz)(MAX_LOADED_IMAGES * (7.f / 8.f))) return;

    // collect all loaded entries
    std::vector<Hash::flat::map<ThumbIdentifier, ThumbEntry>::iterator> loaded_entries;

    for(auto it = this->images.begin(); it != this->images.end(); ++it) {
        const Image* image = it->second.image;
        if(image && (image->isReady() || image->failedLoad() || image->isInterrupted())) {
            loaded_entries.push_back(it);
        }
    }

    if(loaded_entries.size() <= MAX_LOADED_IMAGES) {
        return;
    }

    std::ranges::sort(loaded_entries, [](const auto& a, const auto& b) {
        return a->second.last_access_time < b->second.last_access_time;
    });

    // unload oldest images (a bit more, to not constantly be unloading images for each new image added after we hit the limit once)
    uSz to_unload = std::clamp<uSz>((uSz)(MAX_LOADED_IMAGES / 4.f), 0, loaded_entries.size() / 2);
    for(uSz i = 0; i < to_unload; ++i) {
        logIfCV(debug_thumbs, "unloading {} from memory due to age", loaded_entries[i]->second.file_path);
        resourceManager->destroyResource(loaded_entries[i]->second.image);
        loaded_entries[i]->second.image = nullptr;
    }
}

bool ThumbnailManager::download_image(const ThumbIdentifier& identifier) {
    // TODO: only download a single (response_code == 404) result and share it
    auto& dl = this->images[identifier].dl_handle;
    if(!dl) dl = Downloader::download(identifier.download_url);
    if(dl.failed()) {
        this->id_blacklist.insert(identifier);
        return false;
    }
    if(!dl.completed()) return false;
    if(dl.response_code() != 200) {
        this->id_blacklist.insert(identifier);
        return false;
    }
    this->temp_img_download_data = dl.take_data();
    dl.reset();
    return !this->temp_img_download_data.empty();
}

void ThumbnailManager::clear() {
    for(auto& [identifier, entry] : this->images) {
        if(entry.image) {
            resourceManager->destroyResource(entry.image);
        }
    }

    this->images.clear();
    this->load_queue.clear();
    this->id_blacklist.clear();
}
