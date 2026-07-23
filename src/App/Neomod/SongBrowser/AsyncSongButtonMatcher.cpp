// Copyright (c) 2016 PG, All rights reserved.
#include "AsyncSongButtonMatcher.h"

#include "AsyncPool.h"
#include "SString.h"

#include "SongButton.h"

#include "DatabaseBeatmap.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <utility>

namespace AsyncSongButtonMatcher {

namespace {

// ---- query grammar ----------------------------------------------------------

enum class OPID : uint8_t { EQ, LT, GT, LE, GE, NE };

// an operator is detected by searching for its string anywhere in a token, so the
// order matters: multi-char operators must be listed before the single-char ones they
// contain (otherwise '=' would shadow "<=", ">=", "==", etc.).
inline constexpr struct Operator {
    std::string_view str;
    OPID id;
} MATCH_OPERATORS[]{
    {"<=", OPID::LE}, {">=", OPID::GE}, {"<", OPID::LT}, {">", OPID::GT},
    {"!=", OPID::NE}, {"==", OPID::EQ}, {"=", OPID::EQ},
};

enum class KWID : uint8_t {
    AR,
    CS,
    OD,
    HP,
    BPM,
    OPM,
    CPM,
    SPM,
    OBJECTS,
    CIRCLES,
    SLIDERS,
    SPINNERS,
    LENGTH,
    STARS,
    STR_CREATOR,
    STR_ARTIST
};
inline constexpr struct Keyword {
    std::string_view str;
    KWID id;
} MATCH_KEYWORDS[]{{"ar", KWID::AR},
                   {"cs", KWID::CS},
                   {"od", KWID::OD},
                   {"hp", KWID::HP},
                   {"bpm", KWID::BPM},
                   {"opm", KWID::OPM},
                   {"cpm", KWID::CPM},
                   {"spm", KWID::SPM},
                   {"object", KWID::OBJECTS},
                   {"objects", KWID::OBJECTS},
                   {"circle", KWID::CIRCLES},
                   {"circles", KWID::CIRCLES},
                   {"slider", KWID::SLIDERS},
                   {"sliders", KWID::SLIDERS},
                   {"spinner", KWID::SPINNERS},
                   {"spinners", KWID::SPINNERS},
                   {"length", KWID::LENGTH},
                   {"len", KWID::LENGTH},
                   {"stars", KWID::STARS},
                   {"star", KWID::STARS},
                   {"creator", KWID::STR_CREATOR},
                   {"artist", KWID::STR_ARTIST}};

// a single parsed "<keyword><op><value>" filter, e.g. "ar>=9" or "creator=foo".
struct Expression {
    KWID keyword;
    OPID op;
    float value{0.0f};  // numeric right-hand side (stays 0 if it wasn't a number)
    bool valueIsPercent{false};
    std::string text;  // raw right-hand side, for the string-valued keywords (CREATOR/ARTIST)
};

// a parsed search string: stat filters plus free-text needles (already lowercased and deduplicated).
struct Query {
    std::vector<Expression> expressions;
    std::vector<std::string> needles;
};

// ---- parsing (done once per search) -----------------------------------------

// interpret one whitespace-delimited token as a keyword expression,
// or return nullopt if it's just free text.
static std::optional<Expression> parse_expression(std::string_view token) {
    for(const auto &[op_str, op_id] : MATCH_OPERATORS) {
        if(!token.contains(op_str)) continue;

        // the operator must split the token into exactly two non-empty sides;
        // anything else (e.g. "0<bpm<1") falls through to free text.
        const auto sides = SString::split(token, op_str);
        if(sides.size() != 2 || sides[0].empty() || sides[1].empty()) return std::nullopt;

        for(const auto &[kw_str, kw_id] : MATCH_KEYWORDS) {
            if(kw_str != sides[0]) continue;

            const std::string_view rhs = sides[1];
            const auto percent = rhs.find('%');

            Expression expr{.keyword = kw_id,
                            .op = op_id,
                            .value = 0.f,
                            .valueIsPercent = percent != std::string_view::npos,
                            .text = std::string{rhs}};

            const std::string_view number = expr.valueIsPercent ? rhs.substr(0, percent) : rhs;
            std::from_chars(number.data(), number.data() + number.size(), expr.value);  // leaves 0 on failure

            return expr;
        }

        // the left side isn't a known keyword: treat the token as free text. don't retry
        // other operators (the first one present decides).
        return std::nullopt;
    }
    return std::nullopt;
}

static Query parse_query(std::string_view search) {
    Query query;
    for(std::string_view token : SString::split(search, ' ')) {
        if(auto expr = parse_expression(token)) {
            query.expressions.push_back(std::move(*expr));
            continue;
        }
        SString::trim_inplace(token);
        if(SString::is_wspace_only(token)) continue;
        if(std::ranges::find(query.needles, token) == query.needles.end()) query.needles.emplace_back(token);
    }
    return query;
}

// ---- matching (done once per button) ----------------------------------------

// case-insensitive substring search where the needle is already lowercased
// (like SString::contains_ncase, but it skips re-lowercasing the needle every call).
static forceinline INLINE_BODY bool haystack_contains_needle(std::string_view haystack, std::string_view lower_needle) {
    return !std::ranges::search(haystack, lower_needle, [](unsigned char hay, unsigned char ndl) {
                return SString::ascii_tolower(hay) == ndl;
            }).empty();
}

// does any of the difficulty's text metadata contain the (lowercased) needle?
static forceinline bool metadata_contains_needle(const DatabaseBeatmap *diff, std::string_view lower_needle) {
    if(haystack_contains_needle(diff->getTitleLatin(), lower_needle) ||
       haystack_contains_needle(diff->getArtistLatin(), lower_needle) ||
       haystack_contains_needle(diff->getCreator(), lower_needle) ||
       haystack_contains_needle(diff->getDifficultyName(), lower_needle) ||
       haystack_contains_needle(diff->getSource(), lower_needle) ||
       haystack_contains_needle(diff->getTags(), lower_needle))
        return true;

    // unicode titles/artists are matched case-sensitively; properly lowercasing non-ASCII
    // is out of scope (see the combined-string TODO in submitSearchMatch).
    if((!diff->getTitleUnicode().empty() && diff->getTitleUnicode().contains(lower_needle)) ||
       (!diff->getArtistUnicode().empty() && diff->getArtistUnicode().contains(lower_needle)))
        return true;

    // online ids
    std::array<char, 16> buf{};
    for(const int id : {diff->getID(), diff->getSetID()}) {
        if(id <= 0) continue;
        if(auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), id);
           ec == std::errc() &&
           haystack_contains_needle({buf.data(), static_cast<size_t>(end - buf.data())}, lower_needle))
            return true;
    }
    return false;
}

// evaluate one stat filter against a difficulty.
static bool eval_expression(const Expression &expr, const DatabaseBeatmap *diff, float stars, float speed) {
    const auto length_ms = diff->getLengthMS();
    const auto num_objects = diff->getNumObjects();

    // object/circle/slider count per minute (scaled by the speed multiplier)
    const auto per_minute = [length_ms, speed](i32 count) -> float {
        return length_ms > 0 ? (float)count / ((float)length_ms / 1000.0f / 60.0f) * speed : 0.0f;
    };

    // a raw count, or its share of all objects as a percentage
    const auto obj_count_or_percent = [is_percent = expr.valueIsPercent, num_objects](i32 count) -> float {
        return is_percent ? (float)count / (float)num_objects * 100.0f : (float)count;
    };

    float lhs = 5.0f;           // the keyword's numeric value for this diff
    std::string_view lhs_text;  // its string value (CREATOR is the only string-valued keyword)
    switch(expr.keyword) {
        using enum KWID;
        case AR:
            lhs = diff->getAR();
            break;
        case CS:
            lhs = diff->getCS();
            break;
        case OD:
            lhs = diff->getOD();
            break;
        case HP:
            lhs = diff->getHP();
            break;
        case BPM:
            lhs = (float)diff->getMostCommonBPM();
            break;
        case OPM:
            lhs = per_minute(num_objects);
            break;
        case CPM:
            lhs = per_minute(diff->getNumCircles());
            break;
        case SPM:
            lhs = per_minute(diff->getNumSliders());
            break;
        case OBJECTS:
            lhs = (float)num_objects;
            break;
        case CIRCLES:
            lhs = obj_count_or_percent(diff->getNumCircles());
            break;
        case SLIDERS:
            lhs = obj_count_or_percent(diff->getNumSliders());
            break;
        case SPINNERS:
            lhs = obj_count_or_percent(diff->getNumSpinners());
            break;
        case LENGTH:
            lhs = (float)length_ms / 1000.0f;
            break;
        case STARS:
            // snapshotted on the main thread at collection time (see SearchEntry)
            lhs = std::round(stars * 100.0f) / 100.0f;  // 2 decimal places
            break;
        case STR_CREATOR:
            lhs_text = diff->getCreator();
            break;
        case STR_ARTIST:
            lhs_text = diff->getArtistLatin();
            break;
    }

    switch(expr.op) {
        using enum OPID;
        case LT:
            return lhs < expr.value;
        case GT:
            return lhs > expr.value;
        case LE:
            return lhs <= expr.value;
        case GE:
            return lhs >= expr.value;
        case NE:
            return lhs != expr.value;
        case EQ:
            return lhs == expr.value || (!lhs_text.empty() && SString::strcase_equal(lhs_text, expr.text));
    }

    std::unreachable();
}

// a beatmap matches when its difficulty satisfies every stat filter and contains every needle.
static bool matches(const DatabaseBeatmap *diff, float stars, const Query &query, float speed) {
    assert(!!diff && "NULL databaseBeatmap");

    for(const auto &expr : query.expressions)
        if(!eval_expression(expr, diff, stars, speed)) return false;

    for(const auto &needle : query.needles)
        if(!metadata_contains_needle(diff, needle)) return false;

    return true;
}

}  // namespace

Async::CancellableHandle<void> submitSearchMatch(std::vector<SearchEntry> entries, std::string searchString,
                                                 std::string hardcodedSearchString, float speed) {
    return Async::submit_cancellable(
        [entries = std::move(entries), hardcodedSearchString = std::move(hardcodedSearchString),
         searchString = std::move(searchString), speed](const Sync::stop_token &tok) {
            // combine the hardcoded prefix with the user's query, lowercased for case-insensitive matching.
            // NOTE: this lowercasing won't work for non-ASCII queries, but
            // SString::lower_inplace shouldn't modify those (if I understand correctly...)
            std::string combined;
            if(!hardcodedSearchString.empty()) {
                combined.append(hardcodedSearchString);
                combined.push_back(' ');
            }
            combined.append(searchString);
            SString::lower_inplace(combined);

            // parse the query once, then flag every difficulty that matches it. each button is a single
            // difficulty (SongBrowser flattens every set into its difficulty children before submitting),
            // so there is nothing to recurse into here.
            const Query query = parse_query(combined);
            for(const auto &entry : entries) {
                entry.button->setIsSearchMatch(matches(entry.button->getDatabaseBeatmap(), entry.stars, query, speed));

                // cancellation point
                if(tok.stop_requested()) break;
            }
        },
        Lane::Background);
}

}  // namespace AsyncSongButtonMatcher
