#pragma once
// Copyright (c) 2016 PG, All rights reserved.
#include "AsyncCancellable.h"
#include "types.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class DatabaseBeatmap;
namespace AsyncSongButtonMatcher {

// searchable difficulty metadata, laid out flat so a search only reads contiguous memory.
// built on the main thread with add(), it copies what it needs and never points into the beatmaps
struct SearchIndex {
    struct TextRange {
        u32 begin{0}, end{0};
    };

    std::string text;
    std::vector<TextRange> haystack, artist, creator;

    std::vector<f32> ar, cs, od, hp;
    std::vector<i32> bpm, circles, sliders, spinners;
    std::vector<u32> lengthMS;
    std::vector<u32> ids;

    void add(const DatabaseBeatmap &diff, u32 id);
    [[nodiscard]] size_t size() const { return this->ids.size(); }
    [[nodiscard]] std::string_view view(TextRange range) const {
        return std::string_view{this->text}.substr(range.begin, range.end - range.begin);
    }
};

struct SearchResult {
    std::shared_ptr<const SearchIndex> index;
    std::string query;
    f32 speed{1.f};
    std::vector<b8> matches;
};

// the lowercased "<hardcoded> <search>" query that submitSearchMatch() takes
[[nodiscard]] std::string combineQuery(std::string_view searchString, std::string_view hardcodedSearchString);

[[nodiscard]] bool filtersByStars(std::string_view query);

// stars has one entry per index entry (snapshotted on the main thread, since star ratings depend on the
// active mods), or is empty if the query doesn't filter by them.
// a previous result over the same index and stars whose query the new one narrows down saves checking
// everything it didn't match
Async::CancellableHandle<std::optional<SearchResult>> submitSearchMatch(std::shared_ptr<const SearchIndex> index,
                                                                        std::vector<f32> stars, std::string query,
                                                                        f32 speed,
                                                                        std::shared_ptr<const SearchResult> previous);
}  // namespace AsyncSongButtonMatcher
