#pragma once
// Copyright (c) 2016 PG, All rights reserved.
#include "AsyncCancellable.h"
#include "types.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

class DatabaseBeatmap;
namespace AsyncSongButtonMatcher {

// searchable difficulty metadata, laid out flat so a search only reads contiguous memory.
// built on the main thread with add(), it copies what it needs and never points into the beatmaps
struct SearchIndex {
    // per entry, each followed by a \0: the lowercased latin title, latin artist, creator, difficulty name, source,
    // tags and online ids, then the unicode title and artist as they are (those are matched case-sensitively)
    std::string text;
    std::vector<u32> textEnd;

    std::vector<f32> ar, cs, od, hp;
    std::vector<i32> bpm, circles, sliders, spinners;
    std::vector<u32> lengthMS;

    void add(const DatabaseBeatmap &diff);
    [[nodiscard]] size_t size() const { return this->textEnd.size(); }
};

struct SearchResult {
    std::shared_ptr<const SearchIndex> index;
    std::string query;
    f32 speed{1.f};
    // matches[i] tells whether index entry i matches the query
    std::vector<u8> matches;
};

// the lowercased "<hardcoded> <search>" query that submitSearchMatch() takes
[[nodiscard]] std::string combineQuery(std::string_view searchString, std::string_view hardcodedSearchString);

// only a query that filters by star rating needs the stars snapshotted
[[nodiscard]] bool filtersByStars(std::string_view query);

// stars has one entry per index entry (snapshotted on the main thread, since star ratings depend on the
// active mods), or is empty if the query doesn't filter by them.
// a previous result over the same index and stars whose query the new one narrows down saves checking
// everything it didn't match
Async::CancellableHandle<SearchResult> submitSearchMatch(std::shared_ptr<const SearchIndex> index,
                                                         std::vector<f32> stars, std::string query, f32 speed,
                                                         std::shared_ptr<const SearchResult> previous);
}  // namespace AsyncSongButtonMatcher
