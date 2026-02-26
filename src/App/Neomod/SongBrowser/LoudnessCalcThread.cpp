// Copyright (c) 2024, kiwec, All rights reserved.
#include "LoudnessCalcThread.h"

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
#include "SyncOnce.h"
#include "SyncJthread.h"
#include "ContainerRanges.h"

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
#include <unordered_map>
#include <utility>

struct VolNormalization::LoudnessCalcThread {
    NOCOPY_NOMOVE(LoudnessCalcThread)
   public:
    std::atomic<u32> nb_computed{0};
    std::atomic<u32> nb_total{0};

    LoudnessCalcThread(std::vector<DatabaseBeatmap *> maps_to_calc) {
        this->maps = std::move(maps_to_calc);
        this->nb_total = this->maps.size() + 1;
        this->thr = Sync::jthread([this](const Sync::stop_token &stoken) { return this->run(stoken); });
    }

    ~LoudnessCalcThread() = default;

   private:
    std::vector<DatabaseBeatmap *> maps;
    Sync::jthread thr;

    void run(const Sync::stop_token &stoken) {
        McThread::set_current_thread_name(US_("loudness_calc"));
        McThread::set_current_thread_prio(McThread::Priority::LOW);

#ifdef MCENGINE_FEATURE_BASS
        if(soundEngine->getTypeId() == SoundEngine::BASS) {
            run_bass(stoken);
            return;
        }
#endif
#ifdef MCENGINE_FEATURE_SOLOUD
        if(soundEngine->getTypeId() == SoundEngine::SOLOUD) {
            run_soloud(stoken);
            return;
        }
#endif
    }

#ifdef MCENGINE_FEATURE_BASS
    void run_bass(const Sync::stop_token &stoken) {
        UString last_song = "";
        f32 last_loudness = 0.f;
        const f32 fallback_loudness = std::clamp<f32>(cv::loudness_fallback.getFloat(), -16.f, 0.f);
        std::array<f32, 44100> buf{};

        while(!BassManager::isLoaded()) {  // this should never happen, but just in case
            Timing::sleepMS(100);
        }

        BASS_SetDevice(0);
        BASS_SetConfig(BASS_CONFIG_UPDATETHREADS, 0);

        for(auto map : this->maps) {
            while(osu->shouldPauseBGThreads() && !stoken.stop_requested()) {
                Timing::sleepMS(100);
            }
            Timing::sleep(0);

            if(stoken.stop_requested()) return;
            if(map->loudness.load(std::memory_order_acquire) != 0.f) continue;

            UString song{map->getFullSoundFilePath()};
            if(song == last_song) {
                map->loudness.store(last_loudness, std::memory_order_release);
                this->nb_computed++;
                continue;
            }

            constexpr unsigned int flags =
                BASS_STREAM_DECODE | BASS_SAMPLE_MONO | (Env::cfg(OS::WINDOWS) ? BASS_UNICODE : 0U);
            auto decoder = BASS_StreamCreateFile(BASS_FILE_NAME, song.plat_str(), 0, 0, flags);
            if(!decoder) {
                if(cv::debug_snd.getBool()) {
                    BassManager::printBassError(fmt::format("BASS_StreamCreateFile({:s})", song.toUtf8()),
                                                BASS_ErrorGetCode());
                }
                map->loudness.store(fallback_loudness, std::memory_order_release);
                this->nb_computed++;
                continue;
            }

            auto loudness = BASS_Loudness_Start(decoder, BASS_LOUDNESS_INTEGRATED, 0);
            if(!loudness) {
                BassManager::printBassError("BASS_Loudness_Start()", BASS_ErrorGetCode());
                BASS_ChannelFree(decoder);
                map->loudness.store(fallback_loudness, std::memory_order_release);
                this->nb_computed++;
                continue;
            }

            // Did you know?
            // If we do while(BASS_ChannelGetData(decoder, buf, sizeof(buf) >= 0), we get an infinite loop!
            // Thanks, Microsoft!
            int c;
            do {
                c = BASS_ChannelGetData(decoder, buf.data(), buf.size());
            } while(c >= 0);

            BASS_ChannelFree(decoder);

            f32 integrated_loudness = fallback_loudness;
            const bool succeeded = BASS_Loudness_GetLevel(loudness, BASS_LOUDNESS_INTEGRATED, &integrated_loudness);
            const int errc = succeeded ? 0 : BASS_ErrorGetCode();

            BASS_Loudness_Stop(loudness);

            if(!succeeded || integrated_loudness == -HUGE_VAL) {
                debugLog("No loudness information available for '{:s}' {}", song.toUtf8(),
                         !succeeded ? BassManager::getErrorString(errc) : "(silent song?)");

                integrated_loudness = fallback_loudness;
            }

            last_loudness = integrated_loudness;
            map->loudness.store(integrated_loudness, std::memory_order_release);
            last_song = song;

            this->nb_computed++;
        }

        this->nb_computed++;
    }
#endif

#ifdef MCENGINE_FEATURE_SOLOUD
    void run_soloud(const Sync::stop_token &stoken) {
        std::string last_song = "";
        f32 last_loudness = 0.f;
        const f32 fallback_loudness = std::clamp<f32>(cv::loudness_fallback.getFloat(), -16.f, 0.f);

        SoLoud::WavStream ws(true /* prefer ffmpeg (faster) */);
        ws.setLooping(false);
        ws.setAutoStop(true);

        for(auto map : this->maps) {
            while(osu->shouldPauseBGThreads() && !stoken.stop_requested()) {
                Timing::sleepMS(100);
            }
            Timing::sleep(0);

            if(stoken.stop_requested()) return;
            if(map->loudness.load(std::memory_order_acquire) != 0.f) continue;
            const std::string song = map->getFullSoundFilePath();
            if(song == last_song) {
                map->loudness.store(last_loudness, std::memory_order_release);
                this->nb_computed++;
                continue;
            }

            FILE *fp = File::fopen_c(song.c_str(), "rb");
            if(!fp) {
                if(cv::debug_snd.getBool()) {
                    debugLog("Failed to open '{:s}' for loudness calc", song.c_str());
                }
                this->nb_computed++;
                map->loudness.store(fallback_loudness, std::memory_order_release);
                continue;
            }

            SoLoud::DiskFile df(fp);
            if(ws.loadFile(&df) != SoLoud::SO_NO_ERROR) {
                if(cv::debug_snd.getBool()) {
                    debugLog("Failed to decode '{:s}' for loudness calc", song.c_str());
                }
                this->nb_computed++;
                map->loudness.store(fallback_loudness, std::memory_order_release);
                continue;
            }

            f32 integrated_loudness = fallback_loudness;
            SoLoud::result ret = SoLoud::Loudness::integratedLoudness(ws, integrated_loudness);

            if(ret != SoLoud::SO_NO_ERROR || integrated_loudness == -HUGE_VAL) {
                debugLog("No loudness information available for '{:s}' {}", song.c_str(),
                         ret != SoLoud::SO_NO_ERROR ? "(decode error)" : "(silent song?)");

                integrated_loudness = fallback_loudness;
            }

            last_loudness = integrated_loudness;
            map->loudness.store(integrated_loudness, std::memory_order_release);
            last_song = song;

            this->nb_computed++;
        }

        this->nb_computed++;
    }
#endif
};

void VolNormalization::loudness_cb() {
    // Restart loudness calc.
    VolNormalization::abort();
    if(db && cv::normalize_loudness.getBool()) {
        VolNormalization::start_calc(db->loudness_to_calc);
    }
}

u32 VolNormalization::get_computed_instance() {
    u32 x = 0;
    for(const auto &thr : this->threads) {
        x += thr->nb_computed.load(std::memory_order_acquire);
    }
    return x;
}

u32 VolNormalization::get_total_instance() {
    u32 x = 0;
    for(const auto &thr : this->threads) {
        x += thr->nb_total.load(std::memory_order_acquire);
    }
    return x;
}

void VolNormalization::start_calc_instance(const std::vector<DatabaseBeatmap *> &maps_to_calc) {
    this->abort_instance();
    if(maps_to_calc.empty()) return;
    if(!cv::normalize_loudness.getBool()) return;

    // group maps by audio file so each file is only decoded once
    // (due to diffs in a beatmapset sharing the same audio file)
    std::unordered_map<std::string, std::vector<DatabaseBeatmap *>> by_file;
    by_file.reserve(maps_to_calc.size());
    for(auto map : maps_to_calc) {
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
    if(groups.size() < (size_t)nb_threads) nb_threads = groups.size();
    int chunk_size = groups.size() / nb_threads;
    int remainder = groups.size() % nb_threads;

    auto it = groups.begin();
    for(int i = 0; i < nb_threads; i++) {
        int cur_chunk_size = chunk_size + (i < remainder ? 1 : 0);

        std::vector<DatabaseBeatmap *> chunk;
        for(int j = 0; j < cur_chunk_size; j++) {
            auto &group = *(it + j);
            Mc::append_range(chunk, group);
        }
        it += cur_chunk_size;

        this->threads.emplace_back(std::make_unique<LoudnessCalcThread>(std::move(chunk)));
    }
}

void VolNormalization::abort_instance() { this->threads.clear(); }

VolNormalization::~VolNormalization() {
    cv::loudness_calc_threads.removeAllCallbacks();
    // only clean up this instance's resources
    abort_instance();
}

VolNormalization &VolNormalization::get_instance() {
    static VolNormalization instance;
    static Sync::once_flag once;

    Sync::call_once(once, []() { cv::loudness_calc_threads.setCallback(CFUNC(VolNormalization::loudness_cb)); });

    return instance;
}
