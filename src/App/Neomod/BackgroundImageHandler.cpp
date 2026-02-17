// Copyright (c) 2020, PG, All rights reserved.
#include "BackgroundImageHandler.h"

#include "OsuConVars.h"
#include "Database.h"
#include "Osu.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "File.h"
#include "Logging.h"
#include "Parsing.h"
#include "ResourceManager.h"
#include "MakeDelegateWrapper.h"
#include "Environment.h"
#include "Hashing.h"
#include "Graphics.h"

#include "Skin.h"

#include "demoji.h"
namespace {
// background image path parser (from .osu files)
class MapBGImagePathLoader final : public Resource {
    NOCOPY_NOMOVE(MapBGImagePathLoader)
   public:
    MapBGImagePathLoader(std::string filePath) : Resource(APPDEFINED) { this->sFilePath = std::move(filePath); }
    ~MapBGImagePathLoader() override { destroy(); }

    [[nodiscard]] inline const std::string &getParsedBGFileName() const { return this->parsed_bg_filename; }
    [[nodiscard]] inline bool foundBrokenFilenameReplacement() const { return this->found_mojibake_filename; }

   protected:
    void init() override {
        // (nothing)
        this->bReady.store(true, std::memory_order_release);
    }
    void initAsync() override;
    void destroy() override { /* nothing */ }

   private:
    bool checkMojibake();

    std::string parsed_bg_filename;
    bool found_mojibake_filename{false};

    // set to true if demoji_bwd returned -1 (failed to initialize)
    static std::atomic<bool> dont_attempt_mojibake_checks;
};

std::atomic<bool> MapBGImagePathLoader::dont_attempt_mojibake_checks{false};
}  // namespace

// actual implementation
struct BGImageHandlerImpl final {
    NOCOPY_NOMOVE(BGImageHandlerImpl)
   public:
    BGImageHandlerImpl();
    ~BGImageHandlerImpl();

    void draw(const DatabaseBeatmap *beatmap, f32 alpha = 1.f);
    void update(bool allowEviction);
    const Image *getLoadBackgroundImage(const DatabaseBeatmap *beatma, bool load_immediately = false,
                                        bool allow_menubg_fallback = true);

    struct ENTRY {
        std::string folder;
        std::string bg_image_filename;

        MapBGImagePathLoader *bg_image_path_ldr;
        Image *image;

        [[nodiscard]] inline bool isStale(u32 num_frames_before_stale) const {
            return this->frame_last_accessed + (num_frames_before_stale + 1) < engine->getFrameCount();
        }

        f64 loading_time;
        u64 frame_last_accessed;

        bool load_scheduled;
        bool overwrite_db_entry;
        bool ready_but_image_not_found;  // we tried getting the background image, but couldn't find one
        bool has_image_ref;              // true if this entry has claimed a reference in shared_images
    };

    // shared image pool to avoid loading the same image multiple times
    // (common when beatmap sets share the same background across difficulties)
    struct ImageRef {
        Image *image{nullptr};
        u32 ref_count{0};
    };

    [[nodiscard]] u32 getMaxEvictions() const;

    void handleLoadPathForEntry(const std::string &path, ENTRY &entry);
    inline void handleLoadImageForEntry(ENTRY &entry) { return this->acquireImageRef(entry); }

    void acquireImageRef(ENTRY &entry);
    void releaseImageRef(ENTRY &entry);

    // store convars as callbacks to avoid convar overhead
    inline void cacheSizeCB(f32 new_value) {
        u32 new_u32 = std::clamp<u32>(static_cast<u32>(std::round(new_value)), 0, 128);
        this->max_cache_size = new_u32;
    }
    inline void evictionDelayCB(f32 new_value) {
        u32 new_u32 = std::clamp<u32>(static_cast<u32>(std::round(new_value)), 0, 1024);
        this->eviction_delay_frames = new_u32;
    }
    inline void loadingDelayCB(f32 new_value) {
        f32 new_delay = std::clamp<f32>(new_value, 0.f, 2.f);
        this->image_loading_delay = new_delay;
    }
    inline void enableToggleCB(f32 new_value) {
        bool enabled = !!static_cast<int>(new_value);
        this->disabled = !enabled;
    }

    Hash::unstable_stringmap<ENTRY> cache;
    Hash::unstable_stringmap<ImageRef> shared_images;  // keyed by full image path
    std::string last_requested_entry;

    u32 max_cache_size;
    u32 eviction_delay_frames;
    f32 image_loading_delay;

    bool frozen{false};
    bool disabled{false};
};

// public
BGImageHandlerImpl::BGImageHandlerImpl() {
    this->max_cache_size =
        std::clamp<u32>(static_cast<u32>(std::round(cv::background_image_cache_size.getFloat())), 0, 128);
    cv::background_image_cache_size.setCallback(SA::MakeDelegate<&BGImageHandlerImpl::cacheSizeCB>(this));

    this->eviction_delay_frames = std::clamp<u32>(cv::background_image_eviction_delay_frames.getVal<u32>(), 0, 1024);
    cv::background_image_eviction_delay_frames.setCallback(
        SA::MakeDelegate<&BGImageHandlerImpl::evictionDelayCB>(this));

    this->image_loading_delay = std::clamp<f32>(cv::background_image_loading_delay.getFloat(), 0.f, 2.f);
    cv::background_image_loading_delay.setCallback(SA::MakeDelegate<&BGImageHandlerImpl::loadingDelayCB>(this));

    this->disabled = !cv::load_beatmap_background_images.getBool();
    cv::load_beatmap_background_images.setCallback(SA::MakeDelegate<&BGImageHandlerImpl::enableToggleCB>(this));
}

BGImageHandlerImpl::~BGImageHandlerImpl() {
    for(auto &[_, entry] : this->cache) {
        if(entry.bg_image_path_ldr) resourceManager->destroyResource(entry.bg_image_path_ldr);
        this->releaseImageRef(entry);
    }
    this->cache.clear();

    // sanity: any remaining shared images should have ref_count == 0
    for(auto &[_, img_ref] : this->shared_images) {
        if(img_ref.image) resourceManager->destroyResource(img_ref.image);
    }
    this->shared_images.clear();

    cv::background_image_cache_size.removeCallback();
    cv::background_image_eviction_delay_frames.removeCallback();
    cv::background_image_loading_delay.removeCallback();
    cv::load_beatmap_background_images.removeCallback();
}

// static method
void BGImageHandler::draw(const Image *image, f32 alpha) {
    if(!image || !image->isReady()) return;

    f32 scale = Osu::getImageScaleToFillResolution(image, osu->getVirtScreenSize());
    g->pushTransform();
    {
        g->setColor(Color(0xff999999).setA(alpha));
        g->scale(scale, scale);
        g->translate(osu->getVirtScreenWidth() / 2, osu->getVirtScreenHeight() / 2);
        g->drawImage(image);
    }
    g->popTransform();
}

void BGImageHandlerImpl::draw(const DatabaseBeatmap *beatmap, f32 alpha) {
    if(beatmap == nullptr) return;
    const Image *backgroundImage = this->getLoadBackgroundImage(beatmap);
    return BGImageHandler::draw(backgroundImage, alpha);
}

void BGImageHandlerImpl::update(bool allow_eviction) {
    if(this->disabled) return;
    const bool doLogging = cv::debug_bg_loader.getBool();

    const bool consider_evictions = !this->frozen && allow_eviction &&
                                    engine->throttledShouldRun(this->eviction_delay_frames) && !env->winMinimized();

    u32 max_to_evict = 0, evicted = 0;
    if(consider_evictions) {
        max_to_evict = getMaxEvictions();
    }

    // (1) if frozen, only check the last requested entry in the cache, and quit the loop after 1 iteration
    // this avoids looping through the entire cache during gameplay for no reason
    for(auto it = this->frozen ? this->cache.find(this->last_requested_entry) : this->cache.begin();
        it != this->cache.end();) {
        auto &[osu_path, entry] = *it;

        // NOTE: avoid load/unload jitter if framerate is below eviction delay
        const bool was_used_last_frame = !entry.isStale(this->eviction_delay_frames);

        // check and handle evictions
        if(evicted < max_to_evict && consider_evictions && !was_used_last_frame) {
            if(entry.bg_image_path_ldr) {
                entry.bg_image_path_ldr->interruptLoad();
                resourceManager->destroyResource(entry.bg_image_path_ldr, ResourceDestroyFlags::RDF_FORCE_ASYNC);
            }
            logIf(doLogging, "evicting entry: {}", entry.bg_image_filename);
            this->releaseImageRef(entry);

            evicted++;

            it = this->cache.erase(it);
            continue;
        } else if(was_used_last_frame) {
            // check and handle scheduled loads
            if(entry.load_scheduled) {
                if(engine->getTime() >= entry.loading_time) {
                    entry.load_scheduled = false;

                    if(entry.bg_image_filename.length() < 2) {
                        // if the backgroundImageFileName is not loaded, then we have to create a full
                        // DatabaseBeatmapBackgroundImagePathLoader
                        logIf(doLogging, "loading path for entry (scheduled): {}", entry.bg_image_filename);
                        entry.image = nullptr;
                        this->handleLoadPathForEntry(osu_path, entry);
                    } else {
                        // if backgroundImageFileName is already loaded/valid, then we can directly load the image
                        logIf(doLogging, "loading image for entry (scheduled): {}", entry.bg_image_filename);
                        entry.bg_image_path_ldr = nullptr;
                        this->handleLoadImageForEntry(entry);
                    }
                }
            } else {
                // no load scheduled (potential load-in-progress if it was necessary), handle backgroundImagePathLoader
                // loading finish
                if(entry.image == nullptr && entry.bg_image_path_ldr != nullptr && entry.bg_image_path_ldr->isReady()) {
                    std::string bg_loaded_name = entry.bg_image_path_ldr->getParsedBGFileName();
                    entry.overwrite_db_entry = entry.bg_image_path_ldr->foundBrokenFilenameReplacement();
                    if(bg_loaded_name.length() > 1) {
                        entry.bg_image_filename = bg_loaded_name;
                        this->handleLoadImageForEntry(entry);
                    } else {
                        entry.ready_but_image_not_found = true;
                    }

                    logIf(doLogging, "loading image for entry (bg path loader finished): {}", entry.bg_image_filename);

                    resourceManager->destroyResource(entry.bg_image_path_ldr, ResourceDestroyFlags::RDF_FORCE_ASYNC);
                    entry.bg_image_path_ldr = nullptr;
                }
            }
        }
        // (2) break early after one iteration if frozen
        if(this->frozen) break;

        ++it;
    }

    // reset flags
    this->frozen = false;

    // DEBUG:
    logIf(cv::debug_bg_loader.getInt() > 1, "shared_images.size() = {:d}", this->shared_images.size());
}

const Image *BGImageHandlerImpl::getLoadBackgroundImage(const DatabaseBeatmap *beatmap, bool load_immediately,
                                                        bool allow_menubg_fallback) {
    if(beatmap == nullptr || this->disabled || !beatmap->draw_background) return nullptr;
    const Image *ret = nullptr;

    // only fall back to skin menu-bg if it was found in the cache but not usable, or if we want to load immediately
    bool try_menubg_fallback = load_immediately && allow_menubg_fallback;

    // NOTE: no references to beatmap are kept anywhere (database can safely be deleted/reloaded without having to
    // notify the BackgroundImageHandler)

    const std::string &beatmap_filepath = beatmap->getFilePath();
    this->last_requested_entry = beatmap_filepath;

    logIf(cv::debug_bg_loader.getInt() > 1, "trying to load image for {}", beatmap_filepath);

    if(const auto &it = this->cache.find(beatmap_filepath); it != this->cache.end()) {
        // 1) if the path or image is already loaded, return image ref immediately (which may still be NULL) and keep track
        // of when it was last requested
        auto &entry = it->second;

        entry.frame_last_accessed = engine->getFrameCount();

        // HACKHACK: to improve future loading speed, if we have already loaded the backgroundImageFileName, force
        // update the database backgroundImageFileName and fullBackgroundImageFilePath this is similar to how it
        // worked before the rework, but 100% safe(r) since we are not async
        if(entry.image != nullptr && entry.bg_image_filename.length() > 1 &&
           (beatmap->getBackgroundImageFileName().length() < 2 || entry.overwrite_db_entry)) {
            const_cast<DatabaseBeatmap *>(beatmap)->sBackgroundImageFileName = entry.bg_image_filename;

            entry.overwrite_db_entry = false;

            // update persistent overrides for this map too (so we keep them on db save)
            db->update_overrides(const_cast<DatabaseBeatmap *>(beatmap));
        }

        ret = entry.image;

        // if we got an image but it failed for whatever reason, return the user skin as a fallback instead
        try_menubg_fallback = allow_menubg_fallback && (entry.ready_but_image_not_found || (ret && ret->failedLoad()));
    } else {
        // 2) not found in cache, so create a new entry which will get handled in the next update

        // try evicting stale not-yet-loaded-nor-started-loading entries on overflow
        if(this->shared_images.size() > this->max_cache_size) {
            // don't evict more than a few at a time
            u32 max_to_evict = getMaxEvictions();
            u32 evicted = 0;

            for(auto it = this->cache.begin(); it != this->cache.end();) {
                if(evicted > max_to_evict) break;

                const auto &[osu_path, entry] = *it;
                if(entry.load_scheduled && entry.isStale(this->eviction_delay_frames)) {
                    it = this->cache.erase(it);
                    evicted++;
                } else {
                    ++it;
                }
            }
        }

        // create entry
        ENTRY entry{.folder = beatmap->getFolder(),
                    .bg_image_filename = beatmap->getBackgroundImageFileName(),
                    .bg_image_path_ldr = nullptr,
                    .image = nullptr,
                    .loading_time = engine->getTime() + (load_immediately ? 0. : this->image_loading_delay),
                    .frame_last_accessed = engine->getFrameCount(),
                    .load_scheduled = true,
                    .overwrite_db_entry = false,
                    .ready_but_image_not_found = false,
                    .has_image_ref = false};

        this->cache.try_emplace(beatmap_filepath, entry);
    }

    if(try_menubg_fallback) {
        const Image *skin_bg = nullptr;
        const Skin *skin = osu->getSkin();
        if(skin && (skin_bg = skin->i_menu_bg) && skin_bg != MISSING_TEXTURE && !skin_bg->failedLoad()) {
            ret = skin_bg;
        }
    }

    return ret;
}

// private

void BGImageHandlerImpl::handleLoadPathForEntry(const std::string &path, ENTRY &entry) {
    entry.bg_image_path_ldr = new MapBGImagePathLoader(path);

    // start path load
    resourceManager->requestNextLoadAsync();
    resourceManager->loadResource(entry.bg_image_path_ldr);
}

void BGImageHandlerImpl::acquireImageRef(ENTRY &entry) {
    std::string full_bg_image_path = fmt::format("{}{}", entry.folder, entry.bg_image_filename);

    auto &img_ref = this->shared_images[full_bg_image_path];
    if(img_ref.image == nullptr) {
        logIfCV(debug_bg_loader, "fresh-loading image for {}", full_bg_image_path);
        resourceManager->requestNextLoadAsync();
        resourceManager->requestNextLoadUnmanaged();
        img_ref.image = resourceManager->loadImageAbsUnnamed(full_bg_image_path, true);
    }

    img_ref.ref_count++;
    entry.image = img_ref.image;
    entry.has_image_ref = true;
}

void BGImageHandlerImpl::releaseImageRef(ENTRY &entry) {
    if(!entry.has_image_ref || entry.image == nullptr) return;

    std::string full_bg_image_path = fmt::format("{}{}", entry.folder, entry.bg_image_filename);

    if(auto it = this->shared_images.find(full_bg_image_path); it != this->shared_images.end()) {
        auto &img_ref = it->second;
        if(img_ref.ref_count > 0) img_ref.ref_count--;

        if(img_ref.ref_count == 0) {
            if(img_ref.image) {
                img_ref.image->interruptLoad();
                resourceManager->destroyResource(img_ref.image, ResourceDestroyFlags::RDF_FORCE_ASYNC);
            }
            this->shared_images.erase(it);
        }
    }

    entry.image = nullptr;
    entry.has_image_ref = false;
}

u32 BGImageHandlerImpl::getMaxEvictions() const {
    // actually, just avoid evicting anything if we only have <=10 loaded backgrounds
    // insanely conservative anyways, this is only like 80mb of vram with 1920x1080 backgrounds
    if(this->shared_images.size() <= 10) return 0;

    u32 ret = static_cast<u32>(static_cast<float>(this->shared_images.size()) * (1.f / 4.f));
    if(this->shared_images.size() > this->max_cache_size) {
        ret += static_cast<u32>(
            std::round(std::log2<u32>(static_cast<u32>(this->shared_images.size() - this->max_cache_size)) * 2u));
    }
    ret = std::clamp<u32>(ret, 0u, this->shared_images.size() / 2u);
    return ret;
}

#ifdef _DEBUG
#include "Thread.h"
#endif

#include <cassert>
#include <utility>

namespace {
void MapBGImagePathLoader::initAsync() {
    if(this->isInterrupted()) return;
    // sanity
    assert(!McThread::is_main_thread());

    bool found = false;
    {
        File file(this->sFilePath);

        if(this->isInterrupted() || !file.canRead()) return;
        const uSz file_size = file.getFileSize();

        static constexpr const uSz CHUNK_SIZE = 64ULL;

        std::array<std::string, CHUNK_SIZE> lines;
        bool quit = false, is_events_block = false;

        uSz lines_in_chunk = std::min<uSz>(file_size, CHUNK_SIZE);

        std::string temp_parsed_filename;
        temp_parsed_filename.reserve(64);

        while(!found && !quit && lines_in_chunk > 0) {
            // read 64 lines at a time
            for(uSz i = 0; i < lines_in_chunk; i++) {
                if(this->isInterrupted()) {
                    return;
                }
                if(!file.canRead()) {
                    // cut short
                    lines_in_chunk = i;
                    break;
                }
                lines[i] = file.readLine();
            }

            for(uSz i = 0; i < lines_in_chunk; i++) {
                if(this->isInterrupted()) {
                    return;
                }

                std::string_view cur_line = lines[i];

                // ignore comments, but only if at the beginning of a line (e.g. allow Artist:DJ'TEKINA//SOMETHING)
                if(cur_line.empty() || SString::is_comment(cur_line)) continue;

                if(!is_events_block && cur_line.contains("[Events]")) {
                    is_events_block = true;
                    continue;
                } else if(cur_line.contains("[TimingPoints]") || cur_line.contains("[Colours]") ||
                          cur_line.contains("[HitObjects]")) {
                    quit = true;
                    break;  // NOTE: stop early
                }

                if(!is_events_block) continue;

                // parse events block for filename
                i32 type{-1}, start;
                if(Parsing::parse(cur_line, &type, ',', &start, ',', &temp_parsed_filename) && (type == 0)) {
                    this->parsed_bg_filename = temp_parsed_filename;
                    found = true;
                    break;
                }
            }
        }
    }

    if(this->isInterrupted()) return;

    if(found && !dont_attempt_mojibake_checks.load(std::memory_order_acquire)) {
        this->found_mojibake_filename = checkMojibake();
    }

    this->bAsyncReady.store(true, std::memory_order_release);
    // NOTE: on purpose. there is nothing to do in init(), so finish 1 frame early
    this->bReady.store(true, std::memory_order_release);
};
}  // namespace

bool MapBGImagePathLoader::checkMojibake() {
    bool ret = false;
    const bool debug = cv::debug_bg_loader.getBool();

    size_t last_slash = this->sFilePath.find_last_of("/\\");
    if(last_slash == std::string::npos) {
        // sanity check... we're not a in a folder
        return ret;
    }

    std::string containing_folder = this->sFilePath.substr(0, last_slash + 1);
    std::string full_image_path = fmt::format("{}{}", containing_folder, this->parsed_bg_filename);
    if(File::exists(full_image_path) == File::FILETYPE::FILE) {
        // we found it, return early
        return ret;
    }

    logIf(debug, "{} doesn't exist, trying to re-mojibake...", full_image_path);
    const size_t out_size = this->parsed_bg_filename.size() * 4;

    auto converted_output = std::make_unique_for_overwrite<char[]>(this->parsed_bg_filename.size() * 4);
    const auto conv_result_len =
        demoji_bwd(this->parsed_bg_filename.data(), this->parsed_bg_filename.size(), converted_output.get(), out_size);

    // if demoji_bwd is broken/unavailable for some reason then don't try to use it again
    // (this function won't be called anymore)
    if(conv_result_len == -1) dont_attempt_mojibake_checks.store(true, std::memory_order_release);

    if(conv_result_len > 0) {
        std::string_view result = {converted_output.get(), converted_output.get() + conv_result_len};

        if(result == this->parsed_bg_filename) {
            logIf(debug, "input matched converted output, nothing to do");
            return ret;
        }

        std::string converted_path = fmt::format("{}{}", containing_folder, result);
        const bool converted_exists = File::exists(converted_path) == File::FILETYPE::FILE;

        if(converted_exists) {
            this->parsed_bg_filename = result;
            ret = true;
        }

        logIf(debug, "got result {}, converted path {}, {} on disk", result, converted_path,
              converted_exists ? "exists" : "does not exist");
    } else if(conv_result_len == 0 && debug) {
        debugLog("got no conversion result for {}", this->parsed_bg_filename);
    } else if(debug) {
        debugLog("got error {}", conv_result_len);
    }

    return ret;
}

// passthroughs to impl.
BGImageHandler::BGImageHandler() : pImpl() {}
BGImageHandler::~BGImageHandler() = default;

void BGImageHandler::draw(const DatabaseBeatmap *beatmap, f32 alpha) { return pImpl->draw(beatmap, alpha); }
void BGImageHandler::update(bool allowEviction) { return pImpl->update(allowEviction); }
const Image *BGImageHandler::getLoadBackgroundImage(const DatabaseBeatmap *beatmap, bool load_immediately,
                                                    bool allow_menubg_fallback) {
    return pImpl->getLoadBackgroundImage(beatmap, load_immediately, allow_menubg_fallback);
}
void BGImageHandler::scheduleFreezeCache() { pImpl->frozen = true; }
