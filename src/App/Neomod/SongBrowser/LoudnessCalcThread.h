#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.
#include "noinclude.h"
#include "types.h"

#include <memory>
#include <vector>

class DatabaseBeatmap;
class VolNormalization {
    NOCOPY_NOMOVE(VolNormalization)
   public:
    VolNormalization() = default;
    ~VolNormalization();

    static inline void start_calc(const std::vector<DatabaseBeatmap*>& maps_to_calc) {
        get_instance().start_calc_instance(maps_to_calc);
    }

    static inline u32 get_total() { return get_instance().get_total_instance(); }

    static inline u32 get_computed() { return get_instance().get_computed_instance(); }

    static inline void abort() { get_instance().abort_instance(); }

    // shutdown the singleton
    static inline void shutdown() { get_instance().abort_instance(); }

    // convar callback
    static void loudness_cb();

   private:
    static VolNormalization& get_instance();

    void start_calc_instance(const std::vector<DatabaseBeatmap*>& maps_to_calc);

    u32 get_total_instance();
    u32 get_computed_instance();

    void abort_instance();

    struct LoudnessCalcThread;
    std::vector<std::unique_ptr<LoudnessCalcThread>> threads;
};
