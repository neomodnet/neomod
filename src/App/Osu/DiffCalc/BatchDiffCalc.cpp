// Copyright (c) 2024, kiwec & 2025-2026, WH, All rights reserved.

// - Groups all work by beatmap MD5 hash so each .osu file is loaded exactly once
// - Within each beatmap, groups scores by mod parameters (AR/CS/OD/speed/etc.) so
//   difficulty attributes are calculated once per unique parameter set
// - Pre-calculates star ratings for 54 common mod combinations per beatmap
//   (9 speeds x 6 mod combos: None, HR, HD, EZ, HD|HR, HD|EZ)

#include "BatchDiffCalc.h"
#include "StarPrecalc.h"

#include "Database.h"
#include "DatabaseBeatmap.h"
#include "Osu.h"
#include "ConVar.h"
#include "DifficultyCalculator.h"
#include "score.h"
#include "Timing.h"
#include "Logging.h"
#include "Thread.h"
#include "SyncJthread.h"
#include "SyncStoptoken.h"
#include "ContainerRanges.h"

#include <atomic>
#include <memory>

namespace cv {
extern ConVar debug_pp;
extern ConVar diffcalc_threads;
}  // namespace cv

namespace BatchDiffCalc {
namespace {
struct ScoreResult {
    FinishedScore score;
    f64 pp;
    f64 total_stars;
    f64 aim_stars;
    f64 speed_stars;
};
}  // namespace

struct internal {
    static void collect_outdated_db_diffs(const Sync::stop_token& stoken, std::vector<BeatmapDifficulty*>& outdiffs);
    static void flush_score_results(std::vector<ScoreResult>& pending);
};

void internal::collect_outdated_db_diffs(const Sync::stop_token& stoken, std::vector<BeatmapDifficulty*>& outdiffs) {
    // NOTE: the order of these two locks matters to avoid lock-order-inversion, needs to match the order of other
    // occurrences where they're both locked at the same time (like Database::loadMaps, at the end)
    Sync::shared_lock sr_lock(db->star_ratings_mtx);
    Sync::shared_lock readlock(db->beatmap_difficulties_mtx);
    for(const auto& [_, diff] : db->beatmap_difficulties) {
        if(stoken.stop_requested()) break;
        // checking fStarsNomod <= 0.f might cause us to redundantly try re-calculating it, but
        // that might actually be desirable, since we might have only failed to calculate it due to a bug
        // that is now fixed
        if(diff->ppv2Version < DiffCalc::PP_ALGORITHM_VERSION || diff->fStarsNomod <= 0.f ||
           !db->star_ratings.contains(diff->getMD5())) {
            outdiffs.push_back(diff);
        }
    }
}

namespace {

#define logFailure(err__, ...) \
    cv::debug_pp.getBool() ? debugLog("{}: {}", (err__).error_string(), fmt::format(__VA_ARGS__)) : (void)(0)

// Mod parameters that affect difficulty calculation. Scores with identical
// ModParams on the same beatmap can share star rating calculations.
struct ModParams {
    f32 ar{5.f}, cs{5.f}, od{5.f}, hp{5.f};
    f32 speed{1.f};
    bool hd{false}, rx{false}, ap{false}, td{false};

    bool operator==(const ModParams&) const = default;
};

struct ModParamsHash {
    uSz operator()(const ModParams& p) const {
        uSz h = std::hash<f32>{}(p.ar);
        h ^= std::hash<f32>{}(p.cs) << 1;
        h ^= std::hash<f32>{}(p.od) << 2;
        h ^= std::hash<f32>{}(p.speed) << 3;
        h ^= (p.hd ? 1 : 0) << 4;
        h ^= (p.rx ? 1 : 0) << 5;
        h ^= (p.ap ? 1 : 0) << 6;
        h ^= (p.td ? 1 : 0) << 7;
        return h;
    }
};

struct ScoreWork {
    FinishedScore score;
    ModParams params;
};

// All work for a single beatmap: optional map recalc + zero or more scores
struct WorkItem {
    MD5Hash hash;
    BeatmapDifficulty* map;
    bool needs_map_calc;
    std::vector<ScoreWork> scores;
};

struct MapResult {
    BeatmapDifficulty* map{};
    u32 length_ms{};
    u32 nb_circles{};
    u32 nb_sliders{};
    u32 nb_spinners{};
    StarPrecalc::SRArray star_ratings{};
    i32 min_bpm{};
    i32 max_bpm{};
    i32 avg_bpm{};
};

// per-thread mutable state for worker threads
struct WorkerContext {
    std::unique_ptr<std::vector<DifficultyCalculator::DiffObject>> diffobj_cache;
    std::vector<BPMTuple> bpm_calc_buf;
    std::vector<f32> base_span_durations;
    std::vector<f32> base_scoring_times;
};

Timing::Timer recalc_timer;
std::atomic<u32> errored_count{0};

Sync::jthread coordinator_thread;

std::atomic<u32> scores_processed{0};
std::atomic<u32> scores_total{0};
std::atomic<u32> maps_processed{0};
std::atomic<u32> maps_total{0};
std::atomic<bool> workqueue_ready{false};
std::atomic<bool> did_work{false};

std::vector<MapResult> map_results;
std::vector<ScoreResult> score_results;
Sync::mutex results_mutex;

// owned by coordinator thread during execution
std::vector<WorkItem> work_queue;
std::atomic<u32> next_work_index{0};

forceinline bool score_needs_recalc(const FinishedScore& score) {
    return score.ppv2_version < DiffCalc::PP_ALGORITHM_VERSION || (score.score > 0 && score.ppv2_score <= 0.f);
}

// Calculate difficulty and PP for a group of scores sharing mod parameters.
void process_score_group(const BeatmapDifficulty* map, const ModParams& params, std::vector<ScoreWork*>& scores,
                         DatabaseBeatmap::PRIMITIVE_CONTAINER& primitives, const Sync::stop_token& stoken,
                         WorkerContext& ctx) {
    if(scores.empty()) return;

    auto diffres =
        DatabaseBeatmap::loadDifficultyHitObjects(primitives, params.ar, params.cs, params.speed, false, stoken);
    if(stoken.stop_requested()) return;
    if(diffres.error.errc) {
        const u32 item_failed_scores = scores.size();
        errored_count.fetch_add(item_failed_scores, std::memory_order_relaxed);
        logFailure(diffres.error, "loadDifficultyHitObjects map hash {} map path {}", map->getMD5(),
                   map->getFilePath());
        scores_processed.fetch_add(item_failed_scores, std::memory_order_relaxed);
        return;
    }

    DifficultyCalculator::BeatmapDiffcalcData diffcalc_data{.sortedHitObjects = diffres.diffobjects,
                                                            .CS = params.cs,
                                                            .HP = params.hp,
                                                            .AR = params.ar,
                                                            .OD = params.od,
                                                            .hidden = params.hd,
                                                            .relax = params.rx,
                                                            .autopilot = params.ap,
                                                            .touchDevice = params.td,
                                                            .speedMultiplier = params.speed,
                                                            .breakDuration = diffres.totalBreakDuration,
                                                            .playableLength = diffres.playableLength};

    DifficultyCalculator::DifficultyAttributes attributes{};

    DifficultyCalculator::StarCalcParams star_params{.cachedDiffObjects = std::move(ctx.diffobj_cache),
                                                     .outAttributes = attributes,
                                                     .beatmapData = diffcalc_data,
                                                     .outAimStrains = nullptr,
                                                     .outSpeedStrains = nullptr,
                                                     .incremental = nullptr,
                                                     .upToObjectIndex = -1,
                                                     .cancelCheck = stoken};

    f64 total_stars = DifficultyCalculator::calculateStarDiffForHitObjects(star_params);
    ctx.diffobj_cache = std::move(star_params.cachedDiffObjects);
    ctx.diffobj_cache->clear();

    if(stoken.stop_requested()) return;

    // calculate PP for each score using shared difficulty attributes
    std::vector<ScoreResult> group_results;
    group_results.reserve(scores.size());

    for(auto* sw : scores) {
        DifficultyCalculator::PPv2CalcParams ppv2params{.attributes = attributes,
                                                        .modFlags = sw->score.mods.flags,
                                                        .timescale = sw->score.mods.speed,
                                                        .ar = params.ar,
                                                        .od = params.od,
                                                        .numHitObjects = (i32)primitives.getNumObjects(),
                                                        .numCircles = (i32)primitives.hitcircles.size(),
                                                        .numSliders = (i32)primitives.sliders.size(),
                                                        .numSpinners = (i32)primitives.spinners.size(),
                                                        .maxPossibleCombo = (i32)diffres.getTotalMaxCombo(),
                                                        .combo = sw->score.comboMax,
                                                        .misses = sw->score.numMisses,
                                                        .c300 = sw->score.num300s,
                                                        .c100 = sw->score.num100s,
                                                        .c50 = sw->score.num50s,
                                                        .legacyTotalScore = (u32)sw->score.score,
                                                        .isMcOsuImported = sw->score.is_mcosu_imported()};

        // mcosu scores use a different scorev1 algorithm
        const f64 pp = DifficultyCalculator::calculatePPv2(ppv2params);

        if(pp <= 0.f) {
            errored_count.fetch_add(1, std::memory_order_relaxed);
        }

        group_results.push_back(ScoreResult{.score = std::move(sw->score),
                                            .pp = pp,
                                            .total_stars = total_stars,
                                            .aim_stars = attributes.AimDifficulty,
                                            .speed_stars = attributes.SpeedDifficulty});
    }

    {
        Sync::scoped_lock lock(results_mutex);
        Mc::append_range(score_results, std::move(group_results));
    }
    scores_processed.fetch_add(static_cast<u32>(scores.size()), std::memory_order_relaxed);
}

// Build work queue on worker thread to avoid blocking main thread.
// Iterating over 100k+ scores with score_needs_recalc() checks is O(n).
void build_work_queue(const Sync::stop_token& stoken) {
    Hash::flat::map<MD5Hash, WorkItem> work_by_hash;
    std::vector<BeatmapDifficulty*> pending_diffs_to_recalc;

    // add maps needing star rating recalc (ppv2 version outdated)
    internal::collect_outdated_db_diffs(stoken, pending_diffs_to_recalc);
    if(stoken.stop_requested()) return;

    for(auto* diff : pending_diffs_to_recalc) {
        if(stoken.stop_requested()) return;
        const auto& hash = diff->getMD5();
        auto& item = work_by_hash[hash];
        item.hash = hash;
        item.map = diff;
        item.needs_map_calc = true;
    }

    maps_total.store(static_cast<u32>(pending_diffs_to_recalc.size()), std::memory_order_relaxed);

    // find all scores needing PP recalc, grouped by beatmap
    u32 score_count = 0;
    {
        Sync::shared_lock lock(db->scores_mtx);
        for(const auto& [hash, scores] : db->getScores()) {
            if(stoken.stop_requested()) return;

            for(const auto& score : scores) {
                if(!score_needs_recalc(score)) continue;

                auto* diff = db->getBeatmapDifficulty(hash);
                if(!diff) continue;

                auto& item = work_by_hash[hash];
                if(item.map == nullptr) {
                    item.hash = hash;
                    item.map = diff;
                }

                ScoreWork sw;
                sw.score = score;
                sw.params.ar = score.mods.get_naive_ar(diff->getAR());
                sw.params.cs = score.mods.get_naive_cs(diff->getCS());
                sw.params.od = score.mods.get_naive_od(diff->getOD());
                sw.params.hp = score.mods.get_naive_hp(diff->getHP());
                sw.params.speed = score.mods.speed;
                sw.params.hd = score.mods.has(ModFlags::Hidden);
                sw.params.rx = score.mods.has(ModFlags::Relax);
                sw.params.ap = score.mods.has(ModFlags::Autopilot);
                sw.params.td = score.mods.has(ModFlags::TouchDevice);

                item.scores.push_back(std::move(sw));
                score_count++;
            }
        }
    }

    scores_total.store(score_count, std::memory_order_relaxed);

    // flatten to vector for iteration
    work_queue.clear();
    work_queue.reserve(work_by_hash.size());
    for(auto& [_, item] : work_by_hash) {
        work_queue.push_back(std::move(item));
    }

    // put maps with scores associated with them first, so scores are recalculated early instead of spread across all maps
    std::ranges::sort(work_queue, std::ranges::less{}, [](const auto& work) { return work.scores.empty(); });
}

void process_work_item(WorkItem& item, const Sync::stop_token& stoken, WorkerContext& ctx) {
    if(!item.map) {
        errored_count.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // load primitive objects once for this beatmap
    auto primitives = DatabaseBeatmap::loadPrimitiveObjects(item.map->sFilePath, stoken);
    if(stoken.stop_requested()) return;

    if(primitives.error.errc) {
        const u32 item_failed_scores = item.scores.size();
        errored_count.fetch_add(item_failed_scores, std::memory_order_relaxed);
        logFailure(primitives.error, "loadPrimitiveObjects map hash: {} map path: {}", item.map->getMD5(),
                   item.map->sFilePath);
        if(item.needs_map_calc) {
            errored_count.fetch_add(1, std::memory_order_relaxed);
            Sync::scoped_lock lock(results_mutex);
            map_results.push_back(MapResult{.map = item.map});
            maps_processed.fetch_add(1, std::memory_order_relaxed);
        }
        scores_processed.fetch_add(item_failed_scores, std::memory_order_relaxed);
        return;
    }

    // process map calculation (multi-mod star ratings, BPM, object counts)
    if(item.needs_map_calc) {
        MapResult result{.map = item.map,
                         .nb_circles = (u32)primitives.hitcircles.size(),
                         .nb_sliders = (u32)primitives.sliders.size(),
                         .nb_spinners = (u32)primitives.spinners.size()};

        const f32 base_ar = item.map->getAR();
        const f32 base_cs = item.map->getCS();
        const f32 base_od = item.map->getOD();
        const f32 base_hp = item.map->getHP();

        // AR/CS/OD/HP variant: {multiplier for AR/OD/HP, multiplier for CS}
        // BASE=nomod, HR=1.4x (CS=1.3x), EZ=0.5x
        struct ArCsVariant {
            f32 ar_od_hp_mul;
            f32 cs_mul;
            // mod combo indices: [hidden=false, hidden=true]
            u8 combo_idx[2];
        };
        static constexpr std::array VARIANTS{
            ArCsVariant{1.0f, 1.0f, {0, 2}},  // BASE: None(0), HD(2)
            ArCsVariant{1.4f, 1.3f, {1, 4}},  // HR: HR(1), HD|HR(4)
            ArCsVariant{0.5f, 0.5f, {3, 5}},  // EZ: EZ(3), HD|EZ(5)
        };

        for(const auto& var : VARIANTS) {
            if(stoken.stop_requested()) return;

            const f32 ar = std::clamp(base_ar * var.ar_od_hp_mul, 0.f, 10.f);
            const f32 cs = std::clamp(base_cs * var.cs_mul, 0.f, 10.f);
            const f32 od = std::clamp(base_od * var.ar_od_hp_mul, 0.f, 10.f);
            const f32 hp = std::clamp(base_hp * var.ar_od_hp_mul, 0.f, 10.f);

            // build DifficultyHitObjects once at speed=1.0 for this AR/CS variant.
            // object construction, sorting, and stacking are all speed-independent;
            // only the timing fields need rescaling per speed. slider timing is
            // calculated once (sliderTimesCalculated flag on primitives).
            auto diffres = DatabaseBeatmap::loadDifficultyHitObjects(primitives, ar, cs, 1.0f, false, stoken);
            if(stoken.stop_requested()) return;

            if(&var == &VARIANTS[0]) {
                result.length_ms = diffres.playableLength;
            }

            if(diffres.error.errc) {
                logFailure(diffres.error, "loadDifficultyHitObjects map hash: {} map path: {}", item.map->getMD5(),
                           item.map->sFilePath);
                continue;
            }

            // save base slider timing (overwritten by speed rescaling below).
            // baseTime/baseEndTime are already preserved on DifficultyHitObject,
            // but spanDuration and scoringTimes have no base counterpart.
            ctx.base_span_durations.clear();
            ctx.base_scoring_times.clear();
            for(const auto& obj : diffres.diffobjects) {
                if(obj.type == DifficultyHitObject::TYPE::SLIDER) {
                    ctx.base_span_durations.push_back(obj.spanDuration);
                    for(const auto& st : obj.scoringTimes) {
                        ctx.base_scoring_times.push_back(st.time);
                    }
                }
            }

            for(u8 speed_idx = 0; speed_idx < StarPrecalc::SPEEDS_NUM; speed_idx++) {
                if(stoken.stop_requested()) return;
                const f32 speed = StarPrecalc::SPEEDS[speed_idx];
                const f64 inv_speed = 1.0 / (f64)speed;

                // rescale timing fields from base values for this speed
                {
                    uSz si = 0, sti = 0;
                    for(auto& obj : diffres.diffobjects) {
                        obj.time = (i32)((f64)obj.baseTime * inv_speed);
                        obj.endTime = (i32)((f64)obj.baseEndTime * inv_speed);
                        if(obj.type == DifficultyHitObject::TYPE::SLIDER) {
                            obj.spanDuration = (f32)((f64)ctx.base_span_durations[si] * inv_speed);
                            for(auto& st : obj.scoringTimes) {
                                st.time = (f32)((f64)ctx.base_scoring_times[sti] * inv_speed);
                                sti++;
                            }
                            si++;
                        }
                    }
                }

                // HD=0: full calculation, saving raw difficulty values
                {
                    const u8 flat_idx = speed_idx * StarPrecalc::NUM_MOD_COMBOS + var.combo_idx[0];

                    DifficultyCalculator::BeatmapDiffcalcData diffcalc_data{
                        .sortedHitObjects = diffres.diffobjects,
                        .CS = cs,
                        .HP = hp,
                        .AR = ar,
                        .OD = od,
                        .hidden = false,
                        .relax = false,
                        .autopilot = false,
                        .touchDevice = false,
                        .speedMultiplier = speed,
                        .breakDuration = primitives.totalBreakDuration,
                        .playableLength = diffres.playableLength};

                    DifficultyCalculator::DifficultyAttributes attributes{};
                    DifficultyCalculator::RawDifficultyValues raw_diff{};

                    DifficultyCalculator::StarCalcParams star_params{.cachedDiffObjects = std::move(ctx.diffobj_cache),
                                                                     .outAttributes = attributes,
                                                                     .beatmapData = diffcalc_data,
                                                                     .outAimStrains = nullptr,
                                                                     .outSpeedStrains = nullptr,
                                                                     .incremental = nullptr,
                                                                     .upToObjectIndex = -1,
                                                                     .cancelCheck = stoken,
                                                                     .outRawDifficulty = &raw_diff};

                    result.star_ratings[flat_idx] =
                        static_cast<f32>(DifficultyCalculator::calculateStarDiffForHitObjects(star_params));

                    ctx.diffobj_cache = std::move(star_params.cachedDiffObjects);

                    if(stoken.stop_requested()) return;

                    // HD=1: recompute star rating from cached raw difficulty values.
                    // strains are identical (hidden only affects the final rating transform),
                    // so we skip DiffObject construction, strain calc, and calculate_difficulty.
                    const u8 hd_flat_idx = speed_idx * StarPrecalc::NUM_MOD_COMBOS + var.combo_idx[1];
                    diffcalc_data.hidden = true;
                    result.star_ratings[hd_flat_idx] =
                        static_cast<f32>(DifficultyCalculator::recomputeStarRating(raw_diff, diffcalc_data));
                }

                ctx.diffobj_cache->clear();
            }
        }

        if(result.star_ratings[StarPrecalc::NOMOD_1X_INDEX] <= 0.f) {
            errored_count.fetch_add(1, std::memory_order_relaxed);
        }

        if(stoken.stop_requested()) return;

        if(!primitives.timingpoints.empty()) {
            ctx.bpm_calc_buf.resize(primitives.timingpoints.size());
            BPMInfo bpm = getBPM(primitives.timingpoints, ctx.bpm_calc_buf);
            result.min_bpm = bpm.min;
            result.max_bpm = bpm.max;
            result.avg_bpm = bpm.most_common;
        }

        {
            Sync::scoped_lock lock(results_mutex);
            map_results.push_back(result);
        }
        maps_processed.fetch_add(1, std::memory_order_relaxed);
    }

    if(stoken.stop_requested()) return;

    // process score calculations, grouped by mod parameters to share difficulty calc
    // subsequent loadDifficultyHitObjects calls skip slider timing (sliderTimesCalculated == true)
    if(!item.scores.empty()) {
        Hash::flat::map<ModParams, std::vector<ScoreWork*>, ModParamsHash> score_groups;
        for(auto& sw : item.scores) {
            score_groups[sw.params].push_back(&sw);
        }

        for(auto& [params, group] : score_groups) {
            if(stoken.stop_requested()) return;
            process_score_group(item.map, params, group, primitives, stoken, ctx);
        }
    }

    // free memory from processed scores
    item.scores.clear();
    item.scores.shrink_to_fit();
}

void worker_fn(i32 thread_index, const Sync::stop_token& coord_stoken) {
    McThread::set_current_thread_name(fmt::format("diffcalc_{}", thread_index));
    // just use a low priority so we don't eat into main thread cpu time too much
    McThread::set_current_thread_prio(McThread::Priority::LOW);

    WorkerContext ctx;
    ctx.diffobj_cache = std::make_unique<std::vector<DifficultyCalculator::DiffObject>>();

    const u32 queue_size = work_queue.size();
    while(!coord_stoken.stop_requested()) {
        u32 idx = next_work_index.fetch_add(1, std::memory_order_relaxed);
        if(idx >= queue_size) break;

        while(osu->shouldPauseBGThreads() && !coord_stoken.stop_requested()) {
            Timing::sleepMS(100);
        }
        if(coord_stoken.stop_requested()) break;

        process_work_item(work_queue[idx], coord_stoken, ctx);
        Timing::sleep(0);
    }
}

void coordinator(const Sync::stop_token& stoken) {
    McThread::set_current_thread_name(US_("diffcalc_coord"));
    McThread::set_current_thread_prio(McThread::Priority::LOW);  // we don't really do anything here

    errored_count.store(0, std::memory_order_relaxed);
    recalc_timer.reset();

    build_work_queue(stoken);
    workqueue_ready.store(true, std::memory_order_release);

    if(stoken.stop_requested()) return;

    const u32 initial_workqueue_size = work_queue.size();

    debugLog("DB recalculator: {} work items ({} maps, {} scores)", initial_workqueue_size, get_maps_total(),
             get_scores_total());

    // determine thread count
    i32 nb_threads = 0;
    // if we only have a small amount of work don't even bother
    if(initial_workqueue_size < 1000) {
        nb_threads = 1;
    } else {
        const i32 nb_cpus = McThread::get_logical_cpu_count();
        nb_threads = std::clamp(cv::diffcalc_threads.getInt(), 0, nb_cpus <= 2 ? nb_cpus : nb_cpus - 1);
        if(nb_threads == 0) {
            // subtract 1 from real number of CPUs because we don't know if hyperthreading is enabled or not
            // if it is, then nb_cpus / 2 would effectively occupy every "real" cpu core with a pretty heavy task, leaving less
            // cpu time for the main thread
            nb_threads = std::max((nb_cpus - 1) / 2, 1);
        }
        // sanity (1000 cpus?)
        if(static_cast<u32>(nb_threads) > initial_workqueue_size) {
            nb_threads = std::max(static_cast<i32>(initial_workqueue_size), 1);
        }
    }

    next_work_index.store(0, std::memory_order_relaxed);

    // spawn workers
    {
        std::vector<Sync::jthread> workers;
        workers.reserve(nb_threads);
        for(i32 i = 0; i < nb_threads; i++) {
            workers.emplace_back(worker_fn, i, stoken);
        }
        // jthread destructors join all workers
    }

    if((!stoken.stop_requested() || is_finished()) &&
       errored_count.load(std::memory_order_relaxed) < initial_workqueue_size) {
        recalc_timer.update();
        debugLog("DB recalculator: took {} seconds, failed to recalculate {}/{}.", recalc_timer.getDelta(),
                 errored_count.load(std::memory_order_relaxed), initial_workqueue_size);
        did_work.store(true, std::memory_order_release);
    }

    // just in case
    maps_processed.store(get_maps_total(), std::memory_order_release);
    scores_processed.store(get_scores_total(), std::memory_order_release);

    // cleanup
    work_queue.clear();
    work_queue.shrink_to_fit();
}

}  // namespace

void internal::flush_score_results(std::vector<ScoreResult>& pending) {
    bool any_updated = false;

    Sync::unique_lock lk(db->scores_mtx);
    auto& db_scores = db->getScoresMutable();
    for(auto& res : pending) {
        const auto& it = db_scores.find(res.score.beatmap_hash);
        if(it == db_scores.end()) continue;

        auto& scorevec = it->second;

        if(const auto& scoreIt = std::ranges::find(scorevec, res.score); scoreIt != scorevec.end()) {
            scoreIt->ppv2_version = DiffCalc::PP_ALGORITHM_VERSION;
            scoreIt->ppv2_score = res.pp;
            scoreIt->ppv2_total_stars = res.total_stars;
            scoreIt->ppv2_aim_stars = res.aim_stars;
            scoreIt->ppv2_speed_stars = res.speed_stars;
            any_updated = true;
        }
    }
    if(any_updated) {
        db->scores_changed.store(true, std::memory_order_release);
    }
}

void start_calc() {
    abort_calc();

    maps_processed = 0;
    scores_processed = 0;
    maps_total = 0;
    scores_total = 0;
    workqueue_ready = false;
    map_results.clear();
    score_results.clear();

    coordinator_thread = Sync::jthread(coordinator);
}

void abort_calc() {
    if(!coordinator_thread.joinable()) return;

    coordinator_thread = {};

    scores_total = 0;
    maps_total = 0;
    maps_processed = 0;
    scores_processed = 0;
    workqueue_ready = false;
    work_queue.clear();
    map_results.clear();
    score_results.clear();
}

namespace {
struct {
    std::vector<MapResult> pending_maps;
    std::vector<ScoreResult> pending_scores;
    Hash::flat::set<BeatmapSet*> unique_parents;
} updbuf;  // avoid doing too many reallocations with temporary vectors

}  // namespace

bool update_mainthread() {
    if(!running()) return true;

    auto& [pending_maps, pending_scores, unique_parents]{updbuf};

    {
        Sync::unique_lock lock(results_mutex, Sync::try_to_lock);
        if(!lock.owns_lock()) return true;
        pending_maps = std::move(map_results);
        pending_scores = std::move(score_results);
        map_results.clear();
        score_results.clear();
    }

    // apply map results
    if(const uSz num_pending = pending_maps.size(); num_pending > 0) {
        unique_parents.reserve(num_pending);
        {
            Sync::unique_lock lock(db->peppy_overrides_mtx);
            for(const auto& res : pending_maps) {
                auto* map = res.map;
                unique_parents.insert(map->getParentSet());
                // only override existing values if we got some non-zero result, otherwise use what's already there
                map->iNumCircles = res.nb_circles > 0 ? (i32)res.nb_circles : map->iNumCircles;
                map->iNumSliders = res.nb_sliders > 0 ? (i32)res.nb_sliders : map->iNumSliders;
                map->iNumSpinners = res.nb_spinners > 0 ? (i32)res.nb_spinners : map->iNumSpinners;
                map->iLengthMS = res.length_ms > 0 ? res.length_ms : map->iLengthMS;
                if(const f32 calculated_sr = res.star_ratings[StarPrecalc::NOMOD_1X_INDEX]; calculated_sr > 0.f) {
                    map->fStarsNomod = calculated_sr;
                }
                map->iMinBPM = res.min_bpm != 0 ? res.min_bpm : map->iMinBPM;
                map->iMaxBPM = res.max_bpm != 0 ? res.max_bpm : map->iMaxBPM;
                map->iMostCommonBPM = res.avg_bpm != 0 ? res.avg_bpm : map->iMostCommonBPM;
                map->ppv2Version = DiffCalc::PP_ALGORITHM_VERSION;
                if(map->type == DatabaseBeatmap::BeatmapType::PEPPY_DIFFICULTY) {
                    db->peppy_overrides[map->getMD5()] = map->get_overrides();
                }
            }
        }

        {
            Sync::unique_lock slk(db->star_ratings_mtx);
            for(const auto& res : pending_maps) {
                auto& ptr = db->star_ratings[res.map->getMD5()];
                if(!ptr) ptr = std::make_unique<StarPrecalc::SRArray>();
                *ptr = res.star_ratings;
                res.map->star_ratings = ptr.get();
            }
        }

        for(auto* set : unique_parents) {
            set->updateRepresentativeValues();
        }

        pending_maps.clear();
        unique_parents.clear();
    }

    // apply score results
    if(!pending_scores.empty()) {
        internal::flush_score_results(pending_scores);
        pending_scores.clear();
    }

    return !is_finished();
}

u32 get_maps_total() { return maps_total.load(std::memory_order_acquire); }

u32 get_maps_processed() { return maps_processed.load(std::memory_order_acquire); }

u32 get_scores_total() { return scores_total.load(std::memory_order_acquire); }

u32 get_scores_processed() { return scores_processed.load(std::memory_order_acquire); }

bool running() { return workqueue_ready.load(std::memory_order_acquire) && coordinator_thread.joinable(); }

bool scores_finished() {
    const u32 score_total = get_scores_total();
    return get_scores_processed() >= score_total;
}

bool is_finished() {
    const u32 processed = get_maps_processed() + get_scores_processed();
    const u32 total = get_maps_total() + get_scores_total();
    return workqueue_ready.load(std::memory_order_acquire) && processed >= total;
}

// mainly to avoid songbrowser resorting and stuff even if we didn't successfully recalculate anything
bool did_actual_work() { return did_work.exchange(false, std::memory_order_seq_cst); }

}  // namespace BatchDiffCalc
