// Copyright (c) 2026, WH, All rights reserved.

#include "PreviewTrackManager.h"

#include "AsyncIOHandler.h"
#include "AsyncPool.h"
#include "Bancho.h"
#include "BeatmapInterface.h"
#include "ConVar.h"
#include "Environment.h"
#include "File.h"
#include "Hashing.h"
#include "Logging.h"
#include "NetworkHandler.h"
#include "Osu.h"
#include "OsuConVars.h"
#include "Paths.h"
#include "ResourceManager.h"
#include "Sound.h"
#include "SoundEngine.h"
#include "SyncStoptoken.h"

#include "fmt/format.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
[[nodiscard]] float preview_volume() {
    return cv::volume_music.getFloat() * cv::direct_preview_volume_multiplier.getFloat();
}

// the set id a cache file is named after, like in the server's url (whatever the content is: it can be ogg too, which
// the decoders tell by themselves)
[[nodiscard]] std::optional<i32> preview_file_id(std::string_view name) {
    i32 id{0};
    const auto [end, ec] = std::from_chars(name.data(), name.data() + name.size(), id);
    if(ec != std::errc{} || id <= 0 || name.front() == '0' ||
       std::string_view{end, name.data() + name.size()} != ".mp3")
        return std::nullopt;
    return id;
}
}  // namespace

struct PreviewTrackManager::Impl final {
    NOCOPY_NOMOVE(Impl)
   public:
    Impl() = default;
    ~Impl();

    // cached previews older than this are deleted (and downloaded again if still wanted), like the thumbnails
    static constexpr i64 CACHE_MAX_AGE_SECONDS{7Z * 24 * 60 * 60};
    // past this, the least recently played ones are deleted
    static constexpr u64 CACHE_MAX_BYTES{32ULL * 1024 * 1024};

    // what the current preview is waiting for
    enum class Stage : u8 {
        IDLE,
        SCANNING,     // the listing of the cache directory, which tells whether it's on disk already
        DOWNLOADING,  //
        WRITING,      // downloaded, the file is being written
        LOADING,      // the track is being loaded from the file
        PLAYING,      //
    };

    struct CachedFile {
        u64 size{0};
        i64 last_used{0};  // unix time of when it was written or last played
    };

    void update();
    void play(i32 new_set_id);
    // drops the track and resumes the music it paused, for a preview that ended, failed or got stopped
    void finish();
    // destroys the track and cancels its download, but leaves the music alone
    void drop_track();

    // loads the current preview from the cache, or downloads it
    void start();
    void download();
    void on_written(const std::string &path, i32 written_set_id, u64 size, bool success);
    void load(bool cached_before);
    void start_playing();

    void add_to_cache(i32 cached_set_id, u64 size);
    void remove_from_cache(i32 removed_set_id);
    [[nodiscard]] std::string file_path(i32 for_set_id) const {
        return fmt::format("{}/{}.mp3", this->cache_dir, for_set_id);
    }

    // the current preview
    i32 set_id{0};
    Stage stage{Stage::IDLE};
    // the track comes from a file that was cached before, which is downloaded again (once) if it doesn't decode (a
    // crash may have cut it short)
    bool from_cache{false};
    // a preview paused the selected beatmap's music, which is resumed once no preview plays anymore
    bool music_paused{false};
    Sound *track{nullptr};
    Sync::stop_source download_cancel;

    // the cache directory of the current server (set ids differ between servers), and what's in it
    std::string cache_dir;
    Async::Future<std::vector<File::DirEntry>> scan;
    bool scanned{false};
    Hash::flat::map<i32, CachedFile> cached;
    u64 cached_bytes{0};
    Hash::flat::set<i32> unavailable;

    // the file write callbacks handed to the async io handler run on the main thread, but the io queue is drained
    // after the app is destroyed, so they hold a weak ref to this instead of touching the manager unconditionally
    std::shared_ptr<bool> alive{std::make_shared<bool>(true)};
};

// (the music goes away along with the app)
PreviewTrackManager::Impl::~Impl() { this->drop_track(); }

void PreviewTrackManager::Impl::update() {
    switch(this->stage) {
        case Stage::IDLE:
        case Stage::DOWNLOADING:  // (these two advance through their callbacks)
        case Stage::WRITING:
            return;

        case Stage::SCANNING:
            if(!this->scan.is_ready()) return;
            for(const auto &entry : this->scan.get()) {
                this->cached[*preview_file_id(entry.name)] = {.size = entry.size, .last_used = entry.mtime};
                this->cached_bytes += entry.size;
            }
            this->scanned = true;
            logIfCV(debug_cache, "{} previews ({} bytes) cached in {}", this->cached.size(), this->cached_bytes,
                    this->cache_dir);
            return this->start();

        case Stage::LOADING:
            if(resourceManager->isLoadingResource(this->track)) return;
            if(!this->track->isReady()) {
                logIfCV(debug_snd, "preview {} doesn't play{}", this->set_id,
                        this->from_cache ? ", downloading it again" : "");
                this->drop_track();
                this->remove_from_cache(this->set_id);
                if(this->from_cache) return this->download();
                this->unavailable.insert(this->set_id);
                return this->finish();
            }
            return this->start_playing();

        case Stage::PLAYING:
            if(const Sound *music = osu->getMapInterface()->getMusic(); music && music->isPlaying()) {
                // something else started the music (e.g. a downloaded beatmap got selected), which takes over
                logIfCV(debug_snd, "the music started, stopping preview {}", this->set_id);
                this->music_paused = false;
                return this->finish();
            }
            // (an output device restart ends it too, BASS then reports it as neither playing nor finished)
            if(!this->track->isPlaying() || this->track->isFinished()) this->finish();
            return;
    }
}

void PreviewTrackManager::Impl::play(i32 new_set_id) {
    if(new_set_id == this->set_id && this->stage != Stage::IDLE) return;

    // (the music stays paused if the previous preview paused it, this one takes over)
    this->drop_track();
    this->set_id = new_set_id;
    this->stage = Stage::IDLE;
    if(new_set_id <= 0) return this->finish();

    if(std::string dir = fmt::format("{}/previews/{}", Mc::Paths::cache(), BanchoState::endpoint);
       dir != this->cache_dir) {
        this->cache_dir = std::move(dir);
        this->cached.clear();
        this->cached_bytes = 0;
        this->unavailable.clear();
        this->scanned = false;
        this->scan = Async::submit(
            [dir = this->cache_dir] {
                Environment::createDirectory(dir);
                return File::pruneDirectory(
                    dir, [](std::string_view name) { return preview_file_id(name).has_value(); }, CACHE_MAX_AGE_SECONDS,
                    CACHE_MAX_BYTES);
            },
            Lane::Background);
    }

    // a fake-online session has no server to download it from
    if(BanchoState::fake_online) this->unavailable.insert(new_set_id);
    if(this->unavailable.contains(new_set_id)) return this->finish();

    if(this->scanned) {
        this->start();
    } else {
        this->stage = Stage::SCANNING;
    }
}

void PreviewTrackManager::Impl::finish() {
    this->drop_track();
    this->stage = Stage::IDLE;

    if(!std::exchange(this->music_paused, false)) return;
    // (unless something else started it again meanwhile, or replaced it with music that isn't loaded yet)
    if(Sound *music = osu->getMapInterface()->getMusic(); music && !music->isPlaying()) soundEngine->play(music);
}

void PreviewTrackManager::Impl::drop_track() {
    this->download_cancel.request_stop();
    if(this->track) {
        resourceManager->destroyResource(this->track);
        this->track = nullptr;
    }
}

void PreviewTrackManager::Impl::start() {
    if(const auto it = this->cached.find(this->set_id); it != this->cached.end()) {
        it->second.last_used = time(nullptr);
        return this->load(true);
    }
    this->download();
}

void PreviewTrackManager::Impl::download() {
    this->stage = Stage::DOWNLOADING;

    // not through the Downloader: its queue would put this behind the thumbnails requested from the same host
    const std::string url = fmt::format("b.{}/preview/{}.mp3", BanchoState::endpoint, this->set_id);
    logIfCV(debug_snd, "downloading preview {}", url);

    Mc::Net::RequestOptions options{
        .user_agent = BanchoState::user_agent,
        .timeout = 10,
        .connect_timeout = 5,
        .flags = Mc::Net::RequestOptions::FOLLOW_REDIRECTS,
    };
    this->download_cancel = {};
    options.cancel_token = this->download_cancel.get_token();

    // (a cancelled request never calls back, so this is still the current preview when it does)
    networkHandler->httpRequestAsync(url, std::move(options), [this](Mc::Net::Response response) {
        if(!response.success || response.body.empty()) {
            logIfCV(debug_snd, "downloading preview {} failed: {}", this->set_id, response.error_msg);
            // the server has none: don't ask again (unlike after a network error)
            if(response.response_code >= 400) this->unavailable.insert(this->set_id);
            return this->finish();
        }

        this->stage = Stage::WRITING;
        const u64 size = response.body.size();
        std::string path = this->file_path(this->set_id);
        io->write(path, std::move(response.body),
                  [alive = std::weak_ptr{this->alive}, this, path, written_set_id = this->set_id, size](bool success) {
                      if(!alive.expired()) this->on_written(path, written_set_id, size, success);
                  });
    });
}

void PreviewTrackManager::Impl::on_written(const std::string &path, i32 written_set_id, u64 size, bool success) {
    // (the cache directory of another server, if the endpoint changed meanwhile)
    if(path != this->file_path(written_set_id)) return;

    const bool current = written_set_id == this->set_id && this->stage == Stage::WRITING;
    if(success) {
        this->add_to_cache(written_set_id, size);
        if(current) this->load(false);
    } else if(current) {
        this->finish();
    }
}

void PreviewTrackManager::Impl::load(bool cached_before) {
    this->stage = Stage::LOADING;
    this->from_cache = cached_before;

    const std::string path = this->file_path(this->set_id);
    resourceManager->requestNextLoadAsync();
    // the path is the resource name, like the thumbnails'
    this->track = resourceManager->loadSoundAbs(path, path, /*stream=*/true, /*overlayable=*/false, /*loop=*/false);
}

void PreviewTrackManager::Impl::start_playing() {
    if(Sound *music = osu->getMapInterface()->getMusic(); music && music->isPlaying()) {
        soundEngine->pause(music);
        this->music_paused = true;
    }

    this->track->setBaseVolume(preview_volume());
    if(!soundEngine->play(this->track)) return this->finish();

    logIfCV(debug_snd, "playing preview {}", this->set_id);
    this->stage = Stage::PLAYING;
}

void PreviewTrackManager::Impl::add_to_cache(i32 cached_set_id, u64 size) {
    // (a file downloaded again replaces the old one)
    CachedFile &file = this->cached[cached_set_id];
    this->cached_bytes = this->cached_bytes - file.size + size;
    file = {.size = size, .last_used = time(nullptr)};

    // the least recently played ones go first. never the current one, which may be streamed from its file. deleted
    // right here, since a deletion that runs later could hit the same set's file downloaded again meanwhile
    while(this->cached_bytes > CACHE_MAX_BYTES) {
        const auto oldest = std::ranges::min_element(this->cached, {}, [this](const auto &entry) {
            return entry.first == this->set_id ? INT64_MAX : entry.second.last_used;
        });
        if(oldest == this->cached.end() || oldest->first == this->set_id) break;
        logIfCV(debug_cache, "evicting {}/{}.mp3 (over the size budget)", this->cache_dir, oldest->first);
        this->remove_from_cache(oldest->first);
    }
}

void PreviewTrackManager::Impl::remove_from_cache(i32 removed_set_id) {
    Environment::deleteFile(this->file_path(removed_set_id));
    if(const auto it = this->cached.find(removed_set_id); it != this->cached.end()) {
        this->cached_bytes -= it->second.size;
        this->cached.erase(it);
    }
}

PreviewTrackManager::PreviewTrackManager() = default;
PreviewTrackManager::~PreviewTrackManager() = default;

void PreviewTrackManager::update() { m_impl->update(); }
void PreviewTrackManager::play(i32 set_id) { m_impl->play(set_id); }
void PreviewTrackManager::stop() { m_impl->finish(); }

PreviewTrackManager::State PreviewTrackManager::get_state(i32 set_id) const {
    if(set_id == m_impl->set_id) {
        if(m_impl->stage == Impl::Stage::PLAYING) return State::PLAYING;
        if(m_impl->stage != Impl::Stage::IDLE) return State::LOADING;
    }
    return m_impl->unavailable.contains(set_id) ? State::UNAVAILABLE : State::NONE;
}

f32 PreviewTrackManager::get_progress() const {
    return m_impl->stage == Impl::Stage::PLAYING ? (f32)m_impl->track->getPositionPct() : 0.f;
}

void PreviewTrackManager::apply_music_volume() {
    if(m_impl->stage == Impl::Stage::PLAYING) m_impl->track->setBaseVolume(preview_volume());
}
