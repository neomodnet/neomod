// Copyright (c) 2016 PG, All rights reserved.
#include "AsyncSongButtonMatcher.h"

#include "AsyncPool.h"
#include "ContainerRanges.h"
#include "Parsing.h"
#include "SString.h"

#include "fmt/format.h"

#include "DatabaseBeatmap.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
            Parsing::from_chars(number.data(), number.data() + number.size(), expr.value);  // leaves 0 on failure

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
        if(Mc::ranges::find(query.needles, token) == query.needles.end()) query.needles.emplace_back(token);
    }
    return query;
}

bool eval_expression(const Expression &expr, const SearchIndex &index, size_t i, float stars, float speed) {
    const auto length_ms = index.lengthMS[i];
    const auto num_objects = index.circles[i] + index.sliders[i] + index.spinners[i];

    // object/circle/slider count per minute (scaled by the speed multiplier)
    const auto per_minute = [length_ms, speed](i32 count) -> float {
        return length_ms > 0 ? (float)count / ((float)length_ms / 1000.0f / 60.0f) * speed : 0.0f;
    };

    // a raw count, or its share of all objects as a percentage
    const auto obj_count_or_percent = [is_percent = expr.valueIsPercent, num_objects](i32 count) -> float {
        return is_percent ? (float)count / (float)num_objects * 100.0f : (float)count;
    };

    float lhs = 5.0f;  // the keyword's numeric value for this diff
    std::string_view lhs_text;
    switch(expr.keyword) {
        using enum KWID;
        case AR:
            lhs = index.ar[i];
            break;
        case CS:
            lhs = index.cs[i];
            break;
        case OD:
            lhs = index.od[i];
            break;
        case HP:
            lhs = index.hp[i];
            break;
        case BPM:
            lhs = (float)index.bpm[i];
            break;
        case OPM:
            lhs = per_minute(num_objects);
            break;
        case CPM:
            lhs = per_minute(index.circles[i]);
            break;
        case SPM:
            lhs = per_minute(index.sliders[i]);
            break;
        case OBJECTS:
            lhs = (float)num_objects;
            break;
        case CIRCLES:
            lhs = obj_count_or_percent(index.circles[i]);
            break;
        case SLIDERS:
            lhs = obj_count_or_percent(index.sliders[i]);
            break;
        case SPINNERS:
            lhs = obj_count_or_percent(index.spinners[i]);
            break;
        case LENGTH:
            lhs = (float)length_ms / 1000.0f;
            break;
        case STARS:
            lhs = std::round(stars * 100.0f) / 100.0f;  // 2 decimal places
            break;
        case STR_CREATOR:
            lhs_text = index.view(index.creator[i]);
            break;
        case STR_ARTIST:
            lhs_text = index.view(index.artist[i]);
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

bool matches(const SearchIndex &index, size_t i, float stars, const Query &query, float speed) {
    for(const auto &expr : query.expressions)
        if(!eval_expression(expr, index, i, stars, speed)) return false;

    const std::string_view text = index.view(index.haystack[i]);
    for(const auto &needle : query.needles)
        if(!text.contains(needle)) return false;

    return true;
}

bool narrows(const Query &prev, const Query &next) {
    for(const auto &e : prev.expressions) {
        if(!Mc::ranges::any_of(next.expressions, [&e](const Expression &n) {
               return n.keyword == e.keyword && n.op == e.op && n.value == e.value &&
                      n.valueIsPercent == e.valueIsPercent && n.text == e.text;
           }))
            return false;
    }
    for(const auto &needle : prev.needles) {
        if(!Mc::ranges::any_of(next.needles, [&needle](const std::string &n) { return n.contains(needle); }))
            return false;
    }
    return true;
}

SearchIndex::TextRange append_lower(std::string &out, std::string_view str) {
    const u32 begin = (u32)out.size();
    out.resize(begin + str.size());
    std::ranges::transform(str, out.begin() + begin,
                           [](char c) { return (char)SString::ascii_tolower((unsigned char)c); });
    const u32 end = (u32)out.size();
    out.push_back('\0');
    return {begin, end};
}

}  // namespace

void SearchIndex::add(const DatabaseBeatmap &diff, u32 id) {
    this->ids.push_back(id);
    const u32 begin = (u32)this->text.size();
    append_lower(this->text, diff.getTitleLatin());
    this->artist.push_back(append_lower(this->text, diff.getArtistLatin()));
    this->creator.push_back(append_lower(this->text, diff.getCreator()));
    append_lower(this->text, diff.getDifficultyName());
    append_lower(this->text, diff.getSource());
    append_lower(this->text, diff.getTags());
    for(const int id : {diff.getID(), diff.getSetID()}) {
        if(id <= 0) continue;
        const fmt::format_int str{id};
        this->text.append(str.data(), str.size());
        this->text.push_back('\0');
    }
    for(const std::string_view unicode : {diff.getTitleUnicode(), diff.getArtistUnicode()}) {
        this->text.append(unicode);
        this->text.push_back('\0');
    }
    this->haystack.push_back({begin, (u32)this->text.size()});

    this->ar.push_back(diff.getAR());
    this->cs.push_back(diff.getCS());
    this->od.push_back(diff.getOD());
    this->hp.push_back(diff.getHP());
    this->bpm.push_back(diff.getMostCommonBPM());
    this->circles.push_back(diff.getNumCircles());
    this->sliders.push_back(diff.getNumSliders());
    this->spinners.push_back(diff.getNumSpinners());
    this->lengthMS.push_back(diff.getLengthMS());
}

std::string combineQuery(std::string_view searchString, std::string_view hardcodedSearchString) {
    // NOTE: this lowercasing won't work for non-ASCII queries, but
    // SString::lower_inplace shouldn't modify those (if I understand correctly...)
    std::string combined;
    if(!hardcodedSearchString.empty()) {
        combined.append(hardcodedSearchString);
        combined.push_back(' ');
    }
    combined.append(searchString);
    SString::lower_inplace(combined);
    return combined;
}

bool filtersByStars(std::string_view query) {
    return Mc::ranges::any_of(parse_query(query).expressions,
                              [](const Expression &e) { return e.keyword == KWID::STARS; });
}

Async::CancellableHandle<std::optional<SearchResult>> submitSearchMatch(std::shared_ptr<const SearchIndex> index,
                                                                        std::vector<f32> stars, std::string query,
                                                                        f32 speed,
                                                                        std::shared_ptr<const SearchResult> previous) {
    return Async::submit_cancellable(
        [index = std::move(index), stars = std::move(stars), query = std::move(query), speed,
         previous = std::move(previous)](const Sync::stop_token &tok) -> std::optional<SearchResult> {
            const Query parsed = parse_query(query);
            const bool narrowing = previous && previous->index == index && previous->speed == speed &&
                                   narrows(parse_query(previous->query), parsed);

            SearchResult result{.index = index, .query = query, .speed = speed, .matches = {}};
            result.matches.resize(index->size(), 0);
            for(size_t i = 0; i < index->size(); i++) {
                if(narrowing && !previous->matches[i]) continue;
                result.matches[i] = matches(*index, i, stars.empty() ? 0.f : stars[i], parsed, speed);

                if((i & 1023) == 0 && tok.stop_requested()) return std::nullopt;
            }
            return result;
        },
        Lane::Background);
}

}  // namespace AsyncSongButtonMatcher
