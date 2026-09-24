// Copyright (c) 2024, kiwec, All rights reserved.
#include "VolNormalization.h"

#include "OsuConVars.h"
#include "DatabaseBeatmap.h"
#include "Database.h"
#include "Osu.h"
#include "Engine.h"
#include "Sound.h"
#include "SoundEngine.h"
#include "Thread.h"
#include "Timing.h"
#include "Logging.h"
#include "SyncJthread.h"
#include "SyncCV.h"
#include "SyncMutex.h"
#include "ContainerRanges.h"
#include "Hashing.h"
#include "UniString.h"

#ifdef MCENGINE_FEATURE_BASS
#include "BassManager.h"
#endif

#ifdef MCENGINE_FEATURE_SOLOUD
#include "soloud_wavstream.h"
#include "soloud_loudness.h"
#include "soloud_file.h"
#include "soloud_error.h"
#include "File.h"
#endif

#include <atomic>
#include <deque>
#include <vector>
#include <unordered_map>
#include <memory>
#include <utility>

namespace VolNormalization {
namespace {
// the audio backend in use is fixed at startup (the soundEngine can be restarted but never
// swapped between BASS and SoLoud), so each worker picks its backend once and caches it.
enum class Backend : u8 { NONE, BASS, SOLOUD };

// per-worker scratch state. holds the dedup cache for the audio file we just decoded
// (different diffs in a beatmapset share an audio file), plus the BASS scratch buffer.
struct WorkerCtx {
    Backend backend{Backend::NONE};
    std::string last_song;
    f32 last_loudness{0.f};
#ifdef MCENGINE_FEATURE_BASS
    std::array<f32, 44100> bass_buf{};
#endif
#ifdef MCENGINE_FEATURE_SOLOUD
    SoLoud::WavStream soloud_ws{};
#endif
};

// one-shot per-worker setup: pick backend, run BASS init if needed.
// must be called once before the worker's first calc_one() call.
void init_worker_ctx(WorkerCtx &ctx) {
#ifdef MCENGINE_FEATURE_BASS
    if(soundEngine->getTypeId() == SoundEngine::BASS) {
        ctx.backend = Backend::BASS;
        while(!BassManager::isLoaded()) {  // this should never happen, but just in case
            Timing::sleepMS(100);
        }
        BASS_SetDevice(0);
        BASS_SetConfig(BASS_CONFIG_UPDATETHREADS, 0);
        return;
    }
#endif
#ifdef MCENGINE_FEATURE_SOLOUD
    if(soundEngine->getTypeId() == SoundEngine::SOLOUD) {
        ctx.backend = Backend::SOLOUD;
        ctx.soloud_ws.setLooping(false);
        ctx.soloud_ws.setAutoStop(true);
        return;
    }
#endif
}

#ifdef MCENGINE_FEATURE_BASS
// returns the integrated loudness (real or fallback).
f32 calc_one_bass(const std::string &song_path, WorkerCtx &ctx, f32 fallback_loudness) {
    struct UString {
        UString(std::string_view path) : narrow(path) {
            if constexpr(Env::cfg(OS::WINDOWS)) {
                wide = UniString::to_wide(narrow);
            }
        }
        [[nodiscard]] auto plat_str() const {
            if constexpr(Env::cfg(OS::WINDOWS)) {
                return wide.c_str();
            } else {
                return narrow.c_str();
            }
        }
        std::string narrow;
        std::wstring wide;
    };

    if(song_path == ctx.last_song) {
        return ctx.last_loudness;
    }

    UString song{song_path};

    constexpr unsigned int flags = BASS_STREAM_DECODE | BASS_SAMPLE_MONO | (Env::cfg(OS::WINDOWS) ? BASS_UNICODE : 0U);
    auto decoder = BASS_StreamCreateFile(BASS_FILE_NAME, song.plat_str(), 0, 0, flags);
    if(!decoder) {
        if(cv::debug_snd.getBool()) {
            BassManager::printBassError(fmt::format("BASS_StreamCreateFile({:s})", song.narrow), BASS_ErrorGetCode());
        }
        return fallback_loudness;
    }

    auto loudness = BASS_Loudness_Start(decoder, BASS_LOUDNESS_INTEGRATED, 0);
    if(!loudness) {
        BassManager::printBassError("BASS_Loudness_Start()", BASS_ErrorGetCode());
        BASS_ChannelFree(decoder);
        return fallback_loudness;
    }

    for(int res = 0; res >= 0; res = (int)BASS_ChannelGetData(decoder, ctx.bass_buf.data(), ctx.bass_buf.size())) {
    }

    BASS_ChannelFree(decoder);

    f32 integrated_loudness = fallback_loudness;
    const bool succeeded = BASS_Loudness_GetLevel(loudness, BASS_LOUDNESS_INTEGRATED, &integrated_loudness);
    const int errc = succeeded ? 0 : BASS_ErrorGetCode();

    BASS_Loudness_Stop(loudness);

    if(!succeeded || integrated_loudness == -HUGE_VAL) {
        debugLog("No loudness information available for '{:s}' {}", song.narrow,
                 !succeeded ? BassManager::getErrorString(errc) : "(silent song?)");
        integrated_loudness = fallback_loudness;
    }

    ctx.last_song = song_path;
    ctx.last_loudness = integrated_loudness;
    return integrated_loudness;
}
#endif

#ifdef MCENGINE_FEATURE_SOLOUD
f32 calc_one_soloud(const std::string &song_path, WorkerCtx &ctx, f32 fallback_loudness) {
    if(song_path == ctx.last_song) {
        return ctx.last_loudness;
    }

    FILE *fp = File::fopen_c(song_path.c_str(), "rb");
    if(!fp) {
        logIfCV(debug_snd, "Failed to open '{:s}' for loudness calc", song_path.c_str());
        return fallback_loudness;
    }

    SoLoud::DiskFile df(fp);
    if(ctx.soloud_ws.loadFile(&df) != SoLoud::SO_NO_ERROR) {
        logIfCV(debug_snd, "Failed to decode '{:s}' for loudness calc", song_path.c_str());
        return fallback_loudness;
    }

    f32 integrated_loudness = fallback_loudness;
    SoLoud::result ret = SoLoud::Loudness::integratedLoudness(ctx.soloud_ws, integrated_loudness);

    if(ret != SoLoud::SO_NO_ERROR || integrated_loudness == -HUGE_VAL) {
        debugLog("No loudness information available for '{:s}' {}", song_path.c_str(),
                 ret != SoLoud::SO_NO_ERROR ? "(decode error)" : "(silent song?)");
        integrated_loudness = fallback_loudness;
    }

    ctx.last_song = song_path;
    ctx.last_loudness = integrated_loudness;
    return integrated_loudness;
}
#endif

// the integrated loudness of an audio file (real, or the fallback if it can't be measured). never returns 0.f, so
// storing it in DatabaseBeatmap::loudness unambiguously means "calculated".
// caller must have run init_worker_ctx(ctx) first.
f32 calc_one(const std::string &song_path, WorkerCtx &ctx) {
    f32 result = cv::loudness_fallback.getFloat();

    // (nothing to measure, and it would match a fresh worker's empty dedup cache)
    if(song_path.empty()) return result;

    switch(ctx.backend) {
#ifdef MCENGINE_FEATURE_BASS
        case Backend::BASS:
            result = calc_one_bass(song_path, ctx, result);
            break;
#endif
#ifdef MCENGINE_FEATURE_SOLOUD
        case Backend::SOLOUD:
            result = calc_one_soloud(song_path, ctx, result);
            break;
#endif
        default:
            break;
    }

    return result;
}

struct LoudnessCalcThread {
    NOCOPY_NOMOVE(LoudnessCalcThread)
   public:
    std::atomic<u32> nb_computed{0};
    std::atomic<u32> nb_total;

    LoudnessCalcThread(std::vector<DatabaseBeatmap *> maps_to_calc)
        : nb_total(maps_to_calc.size() + 1),
          maps(std::move(maps_to_calc)),
          thr([this](const Sync::stop_token &stoken) { return this->run(stoken); }) {}

    ~LoudnessCalcThread() = default;

   private:
    std::vector<DatabaseBeatmap *> maps;
    Sync::jthread thr;

    void run(const Sync::stop_token &stoken) {
        McThread::set_current_thread_name("loudness_calc");
        McThread::set_current_thread_prio(McThread::Priority::LOW);

        WorkerCtx ctx;
        init_worker_ctx(ctx);

        for(auto *map : this->maps) {
            while(osu->shouldPauseBGThreads() && !stoken.stop_requested()) {
                Timing::sleepMS(100);
            }
            Timing::sleep(0);

            if(stoken.stop_requested()) return;

            // (abort() joins this thread before the maps can go away)
            if(map->loudness.load(std::memory_order_acquire) == 0.f) {
                map->loudness.store(calc_one(map->getFullSoundFilePath(), ctx), std::memory_order_release);
            }
            this->nb_computed++;
        }

        this->nb_computed++;
    }
};

// persistent priority worker: a single long-lived thread that serves one-off requests
// queued by request_priority(). bypasses shouldPauseBGThreads() since these are on the
// critical path of "user clicks map -> hears music".
struct PriorityWorker {
    NOCOPY_NOMOVE(PriorityWorker)
   public:
    PriorityWorker() : thr([this](const Sync::stop_token &stoken) { return this->run(stoken); }) {}

    // members are destroyed in reverse declaration order: thr first, whose destructor calls
    // request_stop() + join(). stoppable_condvar's wait wakes natively on stop_request via the
    // nsync stop_note, so no manual notify is needed.
    ~PriorityWorker() = default;

    void enqueue(DatabaseBeatmap *map) {
        if(!map) return;
        if(map->loudness.load(std::memory_order_acquire) != 0.f) return;

        {
            Sync::unique_lock lock(this->mtx);
            if(this->queued.contains(map)) return;
            this->queue.push_back(map);
            this->queued.insert(map);
        }
        this->cv.notify_one();
    }

    void drop_pending() {
        Sync::unique_lock lock(this->mtx);
        this->queue.clear();
        this->queued.clear();
        // the calculation can't be interrupted, but its result is dropped
        this->calculating = nullptr;
    }

   private:
    Sync::mutex mtx;
    Sync::stoppable_condvar cv;
    std::deque<DatabaseBeatmap *> queue;
    Hash::flat::set<DatabaseBeatmap *> queued;  // dedup against in-flight queue contents
    // (the owners of requested maps call drop_pending() before freeing them, so a map is only known to be alive while
    // it's queued or this, with mtx held)
    DatabaseBeatmap *calculating{nullptr};
    Sync::jthread thr;

    void run(const Sync::stop_token &stoken) {
        McThread::set_current_thread_name("loudness_prio");
        McThread::set_current_thread_prio(McThread::Priority::LOW);

        WorkerCtx ctx;
        init_worker_ctx(ctx);

        while(!stoken.stop_requested()) {
            DatabaseBeatmap *map = nullptr;
            std::string song_path;
            {
                Sync::unique_lock lock(this->mtx);
                this->cv.wait(lock, stoken, [this] { return !this->queue.empty(); });
                if(stoken.stop_requested()) return;

                map = this->queue.front();
                this->queue.pop_front();
                this->queued.erase(map);
                if(map->loudness.load(std::memory_order_acquire) != 0.f) continue;

                song_path = map->getFullSoundFilePath();
                this->calculating = map;
            }

            // the map isn't touched during the calculation, it can be freed meanwhile
            const f32 loudness = calc_one(song_path, ctx);

            Sync::unique_lock lock(this->mtx);
            if(std::exchange(this->calculating, nullptr) == map) {
                map->loudness.store(loudness, std::memory_order_release);
            }
        }
    }
};

std::vector<std::unique_ptr<LoudnessCalcThread>> s_threads;
std::unique_ptr<PriorityWorker> s_prio{nullptr};

}  // namespace

void loudness_cb(float new_value) {
    const bool new_bool = !!static_cast<int>(new_value);

    // Restart loudness calc.
    VolNormalization::abort();
    if(db && new_bool) {
        start_calc(db->loudness_to_calc);
    }
}

u32 get_computed() {
    u32 x = 0;
    for(const auto &thr : s_threads) {
        x += thr->nb_computed.load(std::memory_order_acquire);
    }
    return x;
}

u32 get_total() {
    u32 x = 0;
    for(const auto &thr : s_threads) {
        x += thr->nb_total.load(std::memory_order_acquire);
    }
    return x;
}

void start_calc(std::span<DatabaseBeatmap *const> maps_to_calc) {
    VolNormalization::abort();
    if(maps_to_calc.empty()) return;
    if(!cv::normalize_loudness.getBool()) return;

    // group maps by audio file so each file is only decoded once
    // (due to diffs in a beatmapset sharing the same audio file)
    std::unordered_map<std::string, std::vector<DatabaseBeatmap *>> by_file;
    by_file.reserve(maps_to_calc.size());
    for(auto *map : maps_to_calc) {
        by_file[map->getFullSoundFilePath()].push_back(map);
    }

    // flatten into a list of groups, then distribute whole groups across threads
    // so no audio file is split between threads
    std::vector<std::vector<DatabaseBeatmap *>> groups;
    groups.reserve(by_file.size());
    for(auto &[_, maps] : by_file) {
        groups.push_back(std::move(maps));
    }

    i32 nb_threads = cv::loudness_calc_threads.getInt();
    if(nb_threads <= 0) {
        // dividing by 2 still burns cpu if hyperthreading is enabled, let's keep it at a sane amount of threads
        nb_threads = std::max(McThread::get_logical_cpu_count() / 3, 1);
    }
    const i32 nb_groups = static_cast<int>(groups.size());
    if(groups.size() < nb_threads) nb_threads = nb_groups;
    int chunk_size = nb_groups / nb_threads;
    int remainder = nb_groups % nb_threads;

    auto it = groups.begin();
    for(int i = 0; i < nb_threads; i++) {
        int cur_chunk_size = chunk_size + (i < remainder ? 1 : 0);

        std::vector<DatabaseBeatmap *> chunk;
        for(int j = 0; j < cur_chunk_size; j++) {
            auto &group = *(it + j);
            Mc::ranges::append(chunk, group);
        }
        it += cur_chunk_size;

        s_threads.emplace_back(std::make_unique<LoudnessCalcThread>(std::move(chunk)));
    }
}

void abort() { s_threads.clear(); }

void request_priority(DatabaseBeatmap *map) {
    if(!map) return;
    if(!cv::normalize_loudness.getBool()) return;
    if(!s_prio) {
        s_prio = std::make_unique<PriorityWorker>();
    }
    s_prio->enqueue(map);
}

void flush_priority() {
    if(s_prio) s_prio->drop_pending();
}

void shutdown() {
    VolNormalization::abort();
    s_prio.reset();
    cv::loudness_calc_threads.removeAllCallbacks();
}

}  // namespace VolNormalization
