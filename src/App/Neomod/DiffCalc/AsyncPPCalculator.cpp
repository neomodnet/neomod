// Copyright (c) 2024, kiwec, All rights reserved.
#include "AsyncPPCalculator.h"

#include "AsyncPool.h"
#include "DatabaseBeatmap.h"
#include "DifficultyCalculator.h"
#include "Osu.h"
#include "SyncMutex.h"

#include <memory>
#include <optional>
#include <string>

using namespace neomod;

namespace AsyncPPC {

namespace {  // static namespace
struct hitobject_cache {
    // Selectors
    f32 speed{};
    f32 AR{};
    f32 CS{};
    bool hardRock{};  // stacking offset direction (not implied by CS: overrides can alias)

    // Results
    DatabaseBeatmap::LOAD_DIFFOBJ_RESULT diffres{};

    [[nodiscard]] bool matches(f32 spd, f32 ar, f32 cs, bool hr) const {
        return speed == spd && AR == ar && CS == cs && hardRock == hr;
    }
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
    bool fl{};
    bool hr{};  // stacking offset direction (not implied by CS: overrides can alias)

    // Results
    pp_res info{};
    DiffCalc::DifficultyAttributes diffattrs{};

    [[nodiscard]] bool matches(f32 spd, f32 ar, f32 hp, f32 cs, f32 od, ModFlags flags) const {
        return speed == spd && AR == ar && HP == hp && CS == cs && OD == od &&
               rx == flags::has<ModFlags::Relax>(flags) && td == flags::has<ModFlags::TouchDevice>(flags) &&
               hd == flags::has<ModFlags::Hidden>(flags) && ap == flags::has<ModFlags::Autopilot>(flags) &&
               fl == flags::has<ModFlags::Flashlight>(flags) && hr == flags::has<ModFlags::HardRock>(flags);
    }
};

// all state for one selected map. the drain task keeps its own shared_ptr, so set_map() can swap
// in a fresh session without waiting for a superseded task to unwind: the old task finishes (at
// most) its current item against the orphaned session, then the last reference reclaims it. no
// two tasks ever run on the same session (worker_active gates the kick).
struct calc_session {
    // map snapshot, so the task never touches DatabaseBeatmap memory
    std::string osuFilePath;
    i32 numCircles{};
    i32 numSliders{};
    i32 numSpinners{};

    Sync::mutex work_mtx;

    // bool to keep track of "high priority" state
    // might need mod updates to be recalc'd mid-gameplay
    std::vector<std::pair<pp_calc_request, bool>> work;  // guarded by work_mtx
    bool worker_active{false};                           // guarded by work_mtx
    std::optional<pp_calc_request> in_flight;            // guarded by work_mtx

    Sync::shared_mutex cache_mtx;
    std::vector<std::pair<pp_calc_request, pp_res>> cache;  // guarded by cache_mtx

    // only ever touched by this session's single drain task (incl. the computed fields the
    // star calc writes into the cached hitobject vectors)
    std::vector<hitobject_cache> ho_cache;
    std::vector<info_cache> inf_cache;
};

const BeatmapDifficulty* current_map = nullptr;       // main thread only
std::shared_ptr<calc_session> session;                // main thread swaps it, tasks hold their own ref
Async::CancellableHandle<void> drain_handle;          // main thread only, the current session's task
std::vector<Async::CancellableHandle<void>> retired;  // main thread only, superseded tasks still unwinding

pp_res not_computed() {
    return {
        .total_stars = -1.0,
        .aim_stars = -1.0,
        .aim_slider_factor = -1.0,
        .speed_stars = -1.0,
        .speed_notes = -1.0,
        .pp = -1.0,
    };
}

void drain_work(const std::shared_ptr<calc_session>& s, const Sync::stop_token& stoken) {
    for(;;) {
        if(stoken.stop_requested()) return;  // superseded: the session is orphaned, just leave

        pp_calc_request rqt{};
        {
            Sync::scoped_lock lock(s->work_mtx);

            // prefer high priority items, low priority ones only run outside of gameplay
            auto pick = s->work.end();
            for(auto it = s->work.begin(); it != s->work.end(); ++it) {
                if(it->second) {
                    pick = it;
                    break;
                }
            }
            if(pick == s->work.end() && !s->work.empty() && !osu->shouldPauseBGThreads()) {
                pick = s->work.begin();
            }

            if(pick == s->work.end()) {
                // nothing runnable: exit instead of parking a pool thread, the next poll re-kicks us
                s->worker_active = false;
                s->in_flight.reset();
                return;
            }

            rqt = pick->first;
            s->in_flight = rqt;
            s->work.erase(pick);
        }

        // skip if already computed (a request can get re-enqueued in the gap between the cache scan
        // and the work scan in query_result())
        bool already_cached = false;
        {
            Sync::shared_lock cache_lock(s->cache_mtx);
            for(const auto& [request, info] : s->cache) {
                if(request == rqt) {
                    already_cached = true;
                    break;
                }
            }
        }

        if(already_cached) continue;

        if(stoken.stop_requested()) return;

        // find or compute hitobjects
        const bool rqtHardRock = flags::has<ModFlags::HardRock>(rqt.modFlags);
        hitobject_cache* computed_ho = nullptr;
        for(auto& ho : s->ho_cache) {
            if(ho.matches(rqt.speedOverride, rqt.AR, rqt.CS, rqtHardRock)) {
                computed_ho = &ho;
                break;
            }
        }

        if(!computed_ho) {
            hitobject_cache new_ho{
                .speed = rqt.speedOverride,
                .AR = rqt.AR,
                .CS = rqt.CS,
                .hardRock = rqtHardRock,
            };

            new_ho.diffres = DatabaseBeatmap::loadDifficultyHitObjects(s->osuFilePath, rqt.AR, rqt.CS,
                                                                       rqt.speedOverride, rqtHardRock, stoken);

            if(stoken.stop_requested()) return;

            // cached even on error, so that we stop trying after failing once
            s->ho_cache.push_back(std::move(new_ho));
            computed_ho = &s->ho_cache.back();
        }

        if(computed_ho->diffres.error.errc) {
            // publish a terminal result for broken maps so pollers stop asking
            // (equivalent to running the full calc on the empty hitobject vector: all zeros)
            Sync::unique_lock cache_lock(s->cache_mtx);
            s->cache.emplace_back(rqt, pp_res{.pp = 0.0});
            continue;
        }

        // find or compute difficulty info
        info_cache* computed_info = nullptr;
        for(auto& info : s->inf_cache) {
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
                                .ap = flags::has<ModFlags::Autopilot>(rqt.modFlags),
                                .fl = flags::has<ModFlags::Flashlight>(rqt.modFlags)};

            DiffCalc::BeatmapDiffcalcData diffcalcData{.sortedHitObjects = computed_ho->diffres.diffobjects,
                                                       .CS = new_info.CS,
                                                       .HP = new_info.HP,
                                                       .AR = new_info.AR,
                                                       .OD = new_info.OD,
                                                       .fileCS = computed_ho->diffres.fileCS,
                                                       .fileHP = computed_ho->diffres.fileHP,
                                                       .fileOD = computed_ho->diffres.fileOD,
                                                       .hidden = new_info.hd,
                                                       .relax = new_info.rx,
                                                       .autopilot = new_info.ap,
                                                       .touchDevice = new_info.td,
                                                       .flashlight = new_info.fl,
                                                       .speedMultiplier = new_info.speed,
                                                       .breakDuration = computed_ho->diffres.totalBreakDuration};

            // mod combos that don't affect strains (HD/RX/TD/HP toggles) reuse the shared vector's
            // computed fields and skip the whole preprocessing+strain pass.
            // NOTE: on >5000 slider maps this can return the first computation's attributes where a
            // recompute would see the ~2 leftover slider curves of the MCKAY sliding window (see the
            // star calc); measured at ~0.002% pp on a worst-case map, display-only
            DiffCalc::StarCalcParams params{.outAttributes = new_info.diffattrs,
                                            .beatmapData = diffcalcData,
                                            .outAimStrains = &new_info.info.aimStrains,
                                            .outSpeedStrains = &new_info.info.speedStrains,
                                            .upToObjectIndex = -1,
                                            .cancelCheck = stoken,
                                            .strainState = &computed_ho->diffres.strainState};

            new_info.info.total_stars = DiffCalc::calculateStarDiffForHitObjects(params);

            // TODO: get rid of duplicated pp_res shit (use new DifficultyAttributes)
            new_info.info.aim_stars = new_info.diffattrs.AimDifficulty;
            new_info.info.aim_slider_factor = new_info.diffattrs.SliderFactor;
            new_info.info.difficult_aim_sliders = new_info.diffattrs.AimDifficultSliderCount;
            new_info.info.difficult_aim_strains = new_info.diffattrs.AimDifficultStrainCount;
            new_info.info.speed_stars = new_info.diffattrs.SpeedDifficulty;
            new_info.info.speed_notes = new_info.diffattrs.SpeedNoteCount;
            new_info.info.difficult_speed_strains = new_info.diffattrs.SpeedDifficultStrainCount;

            if(stoken.stop_requested()) return;

            s->inf_cache.push_back(std::move(new_info));
            computed_info = &s->inf_cache.back();
        }

        if(stoken.stop_requested()) return;

        DiffCalc::PPv2CalcParams ppv2calcparams{.attributes = computed_info->diffattrs,
                                                .modFlags = rqt.modFlags,
                                                .timescale = rqt.speedOverride,
                                                .ar = rqt.AR,
                                                .od = rqt.OD,
                                                .numHitObjects = s->numCircles + s->numSliders + s->numSpinners,
                                                .numCircles = s->numCircles,
                                                .numSliders = s->numSliders,
                                                .numSpinners = s->numSpinners,
                                                .maxPossibleCombo = (i32)computed_ho->diffres.getTotalMaxCombo(),
                                                .combo = rqt.comboMax,
                                                .misses = rqt.numMisses,
                                                .c300 = rqt.num300s,
                                                .c100 = rqt.num100s,
                                                .c50 = rqt.num50s,
                                                .legacyTotalScore = rqt.legacyTotalScore,
                                                .isMcOsuImported = rqt.scoreFromMcOsu};

        computed_info->info.pp = DiffCalc::calculatePPv2(ppv2calcparams);

        {
            Sync::unique_lock cache_lock(s->cache_mtx);
            s->cache.emplace_back(rqt, computed_info->info);
        }
    }
}
}  // namespace

void set_map(const DatabaseBeatmap* new_map) {
    if(current_map == new_map) return;
    if(new_map && new_map->do_not_store) return;

    current_map = new_map;

    // signal the in-flight task (if any) and keep its handle around so the shutdown path below can join it
    // (map switches themselves never block)
    drain_handle.cancel();
    if(drain_handle.valid()) retired.push_back(std::move(drain_handle));
    std::erase_if(retired, [](const auto& h) { return h.is_ready(); });

    if(new_map) {
        auto s = std::make_shared<calc_session>();
        s->osuFilePath = std::string{new_map->getFilePath()};
        s->numCircles = new_map->getNumCircles();
        s->numSliders = new_map->getNumSliders();
        s->numSpinners = new_map->getNumSpinners();
        session = std::move(s);
        // no eager task spawn, the next query_result() poll kicks the drain
    } else {
        // shutdown / database-wipe path: block until every outstanding task has fully unwound
        session.reset();
        for(auto& h : retired) h.wait();
        retired.clear();
    }
}

pp_res query_result(const pp_calc_request& rqt, bool ignoreBGThreadPause) {
    if(!session) return not_computed();

    {
        Sync::shared_lock cache_lock(session->cache_mtx);
        for(const auto& [request, info] : session->cache) {
            if(request == rqt) {
                return info;
            }
        }
    }

    bool kick = false;
    {
        Sync::scoped_lock work_lock(session->work_mtx);

        bool queued = (session->in_flight == rqt);
        for(auto& [w, prio] : session->work) {
            if(w == rqt) {
                queued = true;
                prio |= ignoreBGThreadPause;  // upgrade priority on re-poll
                break;
            }
        }
        if(!queued) {
            session->work.emplace_back(rqt, ignoreBGThreadPause);
        }

        bool any_highprio = false;
        for(const auto& [w, prio] : session->work) {
            if(prio) {
                any_highprio = true;
                break;
            }
        }

        if(!session->worker_active && !session->work.empty() && (any_highprio || !osu->shouldPauseBGThreads())) {
            session->worker_active = true;
            kick = true;
        }
    }

    if(kick) {
        drain_handle = Async::submit_cancellable(
            [s = session](const Sync::stop_token& stoken) { drain_work(s, stoken); }, Lane::Background);
    }

    return not_computed();
}
}  // namespace AsyncPPC
