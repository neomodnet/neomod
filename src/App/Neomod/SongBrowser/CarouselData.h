#pragma once
// Copyright (c) 2026, kiwec, All rights reserved.
#include "types.h"

#include <span>
#include <string>
#include <vector>

class DatabaseBeatmap;
typedef DatabaseBeatmap BeatmapSet;
enum class ScoreGrade : u8;

// a carousel entry: a difficulty, the collapsed difficulties of one set that are next to each other, or a group.
// idx is a difficulty (for a SET the last of the collapsed ones) or a group in CarouselData
struct CarouselNode {
    enum class Kind : u8 { NONE, SET, DIFF, GROUP };
    Kind kind{Kind::NONE};
    u32 idx{0};

    [[nodiscard]] bool operator==(const CarouselNode &) const = default;
    [[nodiscard]] explicit operator bool() const { return this->kind != Kind::NONE; }
    [[nodiscard]] bool isSet() const { return this->kind == Kind::SET; }
    [[nodiscard]] bool isDiff() const { return this->kind == Kind::DIFF; }
    [[nodiscard]] bool isGroup() const { return this->kind == Kind::GROUP; }
    [[nodiscard]] bool isSong() const { return this->kind == Kind::SET || this->kind == Kind::DIFF; }

    static CarouselNode set(u32 lastDiff) { return {.kind = Kind::SET, .idx = lastDiff}; }
    static CarouselNode diff(u32 idx) { return {.kind = Kind::DIFF, .idx = idx}; }
    static CarouselNode group(u32 idx) { return {.kind = Kind::GROUP, .idx = idx}; }
};

struct CarouselGroup {
    std::string name;
    // in carousel order while its grouping is shown
    std::vector<u32> diffs;
    i32 numVisibleChildren{0};
    bool isCollection{false};
};

// the beatmaps the carousel is built from, as plain arrays processed by SongBrowser.
// sets and difficulties are indexed in the order they were added and keep their index until the next database
// load: a removed set only drops out of everything that refers to it, and is nulled in setMap and diffMap
struct CarouselData {
    std::vector<BeatmapSet *> setMap;
    std::vector<u32> setDiffBegin;
    std::vector<u32> setDiffCount;
    std::vector<u32> setDiffs;

    std::vector<DatabaseBeatmap *> diffMap;
    std::vector<u32> diffSet;
    std::vector<ScoreGrade> diffGrade;
    std::vector<u8> diffGradeScheduled;
    std::vector<u8> diffMatch;

    std::vector<CarouselGroup> groups;

    [[nodiscard]] std::span<const u32> setChildren(u32 set) const {
        return {this->setDiffs.data() + this->setDiffBegin[set], this->setDiffCount[set]};
    }

    void reserve(size_t sets, size_t diffs) {
        for(auto *v : {&this->setDiffBegin, &this->setDiffCount}) v->reserve(sets);
        this->setMap.reserve(sets);
        for(auto *v : {&this->setDiffs, &this->diffSet}) v->reserve(diffs);
        this->diffMap.reserve(diffs);
        this->diffGrade.reserve(diffs);
        for(auto *v : {&this->diffGradeScheduled, &this->diffMatch}) v->reserve(diffs);
    }

    void clear() { *this = {}; }
};
