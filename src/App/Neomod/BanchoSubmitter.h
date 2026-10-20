#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.
#include "types.h"

#include <string>
#include <vector>

struct FinishedScore;
struct MD5Hash;
namespace BANCHO::Net {

// what the server answered to a submission; numbers it didn't send are 0
struct SubmissionResult {
    struct Stat {
        f64 before{0.0};
        f64 after{0.0};
    };
    struct Chart {
        Stat rank;
        Stat ranked_score;
        Stat total_score;
        Stat max_combo;
        Stat accuracy;
        Stat pp;
    };

    Chart beatmap;
    Chart overall;
    i64 online_score_id{0};
    std::string rejection_reason;  // empty when the server took the score
    bool has_charts{false};
};

// queues the score for submission; it is retried until the server accepts or rejects it
void submit_score(FinishedScore score);

// sends the queued submissions that are due
void update_submissions();

// drops the queue and aborts the requests in flight
void cleanup_submissions();

// scores of the map that the server has neither accepted nor rejected yet
[[nodiscard]] std::vector<FinishedScore> get_pending_scores(const MD5Hash &beatmap_hash);
[[nodiscard]] bool is_score_pending(const FinishedScore &score);

}  // namespace BANCHO::Net
