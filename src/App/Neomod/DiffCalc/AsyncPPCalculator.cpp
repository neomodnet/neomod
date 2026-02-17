// Copyright (c) 2024, kiwec, All rights reserved.
#include "AsyncPPCalculator.h"

#include "DatabaseBeatmap.h"
#include "DifficultyCalculator.h"
#include "Osu.h"
#include "Timing.h"
#include "Thread.h"
#include "SyncMutex.h"
#include "SyncCV.h"
#include "UString.h"

#include "SyncJthread.h"

namespace AsyncPPC {

namespace {  // static namespace
struct hitobject_cache {
    // Selectors
    f32 speed{};
    f32 AR{};
    f32 CS{};

    // Results
    DatabaseBeatmap::LOAD_DIFFOBJ_RESULT diffres{};

    [[nodiscard]] bool matches(f32 spd, f32 ar, f32 cs) const { return speed == spd && AR == ar && CS == cs; }
};

struct info_cache {
    // Selectors
    f32 speed{};
    f32 AR{};
    f32 HP{};
    f32 CS{};
    f32 OD{};
    bool rx{};
    bool td{};
    bool hd{};
    bool ap{};

    // Results
    std::unique_ptr<std::vector<DifficultyCalculator::DiffObject>> cachedDiffObjects{
        std::make_unique<std::vector<DifficultyCalculator::DiffObject>>()};
    pp_res info{};
    DifficultyCalculator::DifficultyAttributes diffattrs{};

    [[nodiscard]] bool matches(f32 spd, f32 ar, f32 hp, f32 cs, f32 od, ModFlags flags) const {
        return speed == spd && AR == ar && HP == hp && CS == cs && OD == od &&
               rx == flags::has<ModFlags::Relax>(flags) && td == flags::has<ModFlags::TouchDevice>(flags) &&
               hd == flags::has<ModFlags::Hidden>(flags) && ap == flags::has<ModFlags::Autopilot>(flags);
    }
};

const BeatmapDifficulty* current_map = nullptr;

Sync::condition_variable_any cond;
Sync::jthread thr;

Sync::mutex work_mtx;

// bool to keep track of "high priority" state
// might need mod updates to be recalc'd mid-gameplay
std::vector<std::pair<pp_calc_request, bool>> work;

Sync::mutex cache_mtx;

std::vector<std::pair<pp_calc_request, pp_res>> cache;
std::vector<hitobject_cache> ho_cache;
std::vector<info_cache> inf_cache;

void clear_caches() {
    Sync::unique_lock work_lock(work_mtx);
    Sync::unique_lock cache_lock(cache_mtx);

    work.clear();
    cache.clear();
    ho_cache.clear();
    inf_cache.clear();
}

void run_thread(const Sync::stop_token& stoken) {
    McThread::set_current_thread_name(US_("async_pp_calc"));
    McThread::set_current_thread_prio(McThread::Priority::LOW);  // reset priority

    while(!stoken.stop_requested()) {
        Sync::unique_lock lock(work_mtx);
        cond.wait(lock, stoken, [] { return !work.empty(); });
        if(stoken.stop_requested()) return;

        while(!work.empty()) {
            if(stoken.stop_requested()) return;

            auto [rqt, highprio] = work.front();
            if(!highprio && osu->shouldPauseBGThreads()) {
                lock.unlock();
                Timing::sleepMS(100);
                lock.lock();
                continue;
            }

            work.erase(work.begin());

            // capture current map before unlocking (work items are specific to this map)
            const BeatmapDifficulty* map_for_rqt = current_map;
            lock.unlock();

            if(!map_for_rqt) continue;

            // skip if already computed
            bool already_cached = false;
            {
                Sync::unique_lock cache_lock(cache_mtx);
                for(const auto& [request, info] : cache) {
                    if(request == rqt) {
                        already_cached = true;
                        break;
                    }
                }
            }

            if(already_cached) {
                lock.lock();
                continue;
            }

            if(stoken.stop_requested()) return;

            // find or compute hitobjects
            hitobject_cache* computed_ho = nullptr;
            for(auto& ho : ho_cache) {
                if(ho.matches(rqt.speedOverride, rqt.AR, rqt.CS)) {
                    computed_ho = &ho;
                    break;
                }
            }

            if(!computed_ho) {
                if(stoken.stop_requested()) return;

                hitobject_cache new_ho{
                    .speed = rqt.speedOverride,
                    .AR = rqt.AR,
                    .CS = rqt.CS,
                };

                new_ho.diffres = DatabaseBeatmap::loadDifficultyHitObjects(map_for_rqt->getFilePath(), rqt.AR, rqt.CS,
                                                                           rqt.speedOverride, false, stoken);

                if(stoken.stop_requested()) return;
                if(new_ho.diffres.error.errc) {
                    // so that we stop trying after failing once
                    ho_cache.push_back(std::move(new_ho));
                    lock.lock();
                    continue;
                }

                ho_cache.push_back(std::move(new_ho));
                computed_ho = &ho_cache.back();
            }

            // find or compute difficulty info
            info_cache* computed_info = nullptr;
            for(auto& info : inf_cache) {
                if(info.matches(rqt.speedOverride, rqt.AR, rqt.HP, rqt.CS, rqt.OD, rqt.modFlags)) {
                    computed_info = &info;
                    break;
                }
            }

            if(!computed_info) {
                if(stoken.stop_requested()) return;

                info_cache new_info{.speed = rqt.speedOverride,
                                    .AR = rqt.AR,
                                    .HP = rqt.HP,
                                    .CS = rqt.CS,
                                    .OD = rqt.OD,
                                    .rx = flags::has<ModFlags::Relax>(rqt.modFlags),
                                    .td = flags::has<ModFlags::TouchDevice>(rqt.modFlags),
                                    .hd = flags::has<ModFlags::Hidden>(rqt.modFlags),
                                    .ap = flags::has<ModFlags::Autopilot>(rqt.modFlags)};

                DifficultyCalculator::BeatmapDiffcalcData diffcalcData{
                    .sortedHitObjects = computed_ho->diffres.diffobjects,
                    .CS = new_info.CS,
                    .HP = new_info.HP,
                    .AR = new_info.AR,
                    .OD = new_info.OD,
                    .hidden = new_info.hd,
                    .relax = new_info.rx,
                    .autopilot = new_info.ap,
                    .touchDevice = new_info.td,
                    .speedMultiplier = new_info.speed,
                    .breakDuration = computed_ho->diffres.totalBreakDuration,
                    .playableLength = computed_ho->diffres.playableLength};

                DifficultyCalculator::StarCalcParams params{.cachedDiffObjects = std::move(new_info.cachedDiffObjects),
                                                            .outAttributes = new_info.diffattrs,
                                                            .beatmapData = diffcalcData,
                                                            .outAimStrains = &new_info.info.aimStrains,
                                                            .outSpeedStrains = &new_info.info.speedStrains,
                                                            .incremental = nullptr,
                                                            .upToObjectIndex = -1,
                                                            .cancelCheck = stoken};

                new_info.info.total_stars = DifficultyCalculator::calculateStarDiffForHitObjects(params);
                new_info.cachedDiffObjects = std::move(params.cachedDiffObjects);

                // TODO: get rid of duplicated pp_res shit (use new DifficultyAttributes)
                new_info.info.aim_stars = new_info.diffattrs.AimDifficulty;
                new_info.info.aim_slider_factor = new_info.diffattrs.SliderFactor;
                new_info.info.difficult_aim_sliders = new_info.diffattrs.AimDifficultSliderCount;
                new_info.info.difficult_aim_strains = new_info.diffattrs.AimDifficultStrainCount;
                new_info.info.speed_stars = new_info.diffattrs.SpeedDifficulty;
                new_info.info.speed_notes = new_info.diffattrs.SpeedNoteCount;
                new_info.info.difficult_speed_strains = new_info.diffattrs.SpeedDifficultStrainCount;

                if(stoken.stop_requested()) return;

                inf_cache.push_back(std::move(new_info));
                computed_info = &inf_cache.back();
            }

            if(stoken.stop_requested()) return;

            DifficultyCalculator::PPv2CalcParams ppv2calcparams{
                .attributes = computed_info->diffattrs,
                .modFlags = rqt.modFlags,
                .timescale = rqt.speedOverride,
                .ar = rqt.AR,
                .od = rqt.OD,
                .numHitObjects = map_for_rqt->getNumObjects(),
                .numCircles = map_for_rqt->iNumCircles,
                .numSliders = map_for_rqt->iNumSliders,
                .numSpinners = map_for_rqt->iNumSpinners,
                .maxPossibleCombo = (i32)computed_ho->diffres.getTotalMaxCombo(),
                .combo = rqt.comboMax,
                .misses = rqt.numMisses,
                .c300 = rqt.num300s,
                .c100 = rqt.num100s,
                .c50 = rqt.num50s,
                .legacyTotalScore = rqt.legacyTotalScore,
                .isMcOsuImported = rqt.scoreFromMcOsu};

            computed_info->info.pp = DifficultyCalculator::calculatePPv2(ppv2calcparams);

            {
                Sync::unique_lock cache_lock(cache_mtx);
                cache.emplace_back(rqt, computed_info->info);
            }

            lock.lock();
        }
    }
}
}  // namespace

void set_map(const DatabaseBeatmap* new_map) {
    if(current_map == new_map) return;
    if(new_map && new_map->do_not_store) return;

    const bool had_map = (current_map != nullptr);
    current_map = new_map;

    if(had_map) {
        clear_caches();
    }

    if(!had_map && new_map != nullptr) {
        thr = Sync::jthread(run_thread);
    } else if(had_map && new_map == nullptr) {
        if(thr.joinable()) {
            thr.request_stop();
            thr.join();
        }
    }
}

pp_res query_result(const pp_calc_request& rqt, bool ignoreBGThreadPause) {
    {
        Sync::unique_lock cache_lock(cache_mtx);
        for(const auto& [request, info] : cache) {
            if(request == rqt) {
                return info;
            }
        }
    }

    {
        Sync::unique_lock work_lock(work_mtx);
        bool work_exists = false;
        for(const auto& [w, prio] : work) {
            if(w == rqt) {
                work_exists = true;
                break;
            }
        }
        if(!work_exists) {
            work.emplace_back(rqt, ignoreBGThreadPause);
            cond.notify_one();
        }
    }

    static pp_res dummy{
        .total_stars = -1.0,
        .aim_stars = -1.0,
        .aim_slider_factor = -1.0,
        .speed_stars = -1.0,
        .speed_notes = -1.0,
        .pp = -1.0,
    };

    return dummy;
}
}  // namespace AsyncPPC
