// Copyright (c) 2026, WH, All rights reserved.
#include "MapObjects.h"

#include "BeatmapFile/BeatmapPrimitives.h"
#include "BeatmapStacking.h"
#include "ContainerRanges.h"
#include "DatabaseBeatmapTypes.h"
#include "Document.h"
#include "GameRules.h"

#include <algorithm>
#include <iterator>
#include <span>
#include <tuple>
#include <variant>

using namespace neomod;

namespace {
// what the combo for an object was counted as: its number, colour counter and offset (a spinner has none), whether it
// ends it
std::tuple<i32, i32, i32, bool> comboOf(const Primitives::Object &object) {
    if(const auto *h = std::get_if<DBType::HITCIRCLE>(&object)) {
        return {h->number, h->colorCounter, h->colorOffset, h->isEndOfCombo};
    }
    if(const auto *s = std::get_if<DBType::SLIDER>(&object)) {
        return {s->number, s->colorCounter, s->colorOffset, s->isEndOfCombo};
    }
    return {0, 0, 0, std::get<DBType::SPINNER>(object).isEndOfCombo};
}

// by end time, equal ends in play order (by start, then line)
std::tuple<i32, i32, u32> endOrder(const MapObjects::Shown &shown) {
    return {shown.object->getEndTime(), shown.object->getClickTime(), shown.object->getLine()};
}
}  // namespace

MapObjects::MapObjects(const Document &document) : revision(document.getRevision()) {
    const Primitives::PRIMITIVE_CONTAINER &c = document.getPrimitives();
    this->view.rawHitcircleDiameter = GameRules::getRawHitCircleDiameter(c.CS);
    this->view.approachTimeMS = GameRules::mapDifficultyRange(
        c.AR, GameRules::getMinApproachTime(), GameRules::getMidApproachTime(), GameRules::getMaxApproachTime());
    this->view.comboColors = c.combocolors;

    this->objects = HitObjects::create(c, nullptr, &this->view);
    HitObjects::stack(this->objects, c.AR, c.version, c.stackLeniency, this->view.rawHitcircleDiameter, false);
    for(uSz run = 0; run < this->objects.size(); run = this->nextRun(run, c)) this->stackRuns.push_back(run);

    this->ids.reserve(this->objects.size());
    this->indexes.reserve(this->objects.size());
    this->byEndTime.reserve(this->objects.size());
    for(const auto &obj : this->objects) {
        const Document::Id id = document.getIdAt(obj->getLine());
        this->indexes[id] = this->ids.size();
        this->ids.push_back(id);
        this->byEndTime.push_back({.object = obj.get(), .id = id, .meshBuilt = false});
        this->longestObjectMS = std::max(this->longestObjectMS, obj->getDuration());
    }
    Mc::ranges::stable_sort(this->byEndTime, {}, [](const Shown &shown) { return shown.object->getEndTime(); });
}

MapObjects::~MapObjects() = default;

bool MapObjects::update(const Document &document) {
    const auto &patched = document.getPatched();
    if(!patched || patched->from != this->revision) return false;
    const Primitives::PRIMITIVE_CONTAINER &c = document.getPrimitives();
    if(!patched->objects.empty()) this->remake(document, patched->objects);
    // one made again has its combo already
    std::vector<uSz> recounted;
    std::set_difference(patched->combos.begin(), patched->combos.end(), patched->objects.begin(),
                        patched->objects.end(), std::back_inserter(recounted));
    for(const uSz i : recounted) {
        const auto [number, colorCounter, colorOffset, isEndOfCombo] = comboOf(c.objects[i]);
        this->objects[i]->setCombo(number, colorCounter, colorOffset, isEndOfCombo);
    }
    std::vector<uSz> changed;
    std::set_union(patched->objects.begin(), patched->objects.end(), recounted.begin(), recounted.end(),
                   std::back_inserter(changed));
    if(!changed.empty()) this->startCombosAround(changed);
    // stacking doesn't go by combos: the ones only counted again stay as they are stacked
    if(!patched->objects.empty()) this->restackAround(patched->objects, c);
    this->sounds.reset();
    this->revision = document.getRevision();
    return true;
}

uSz MapObjects::firstFrom(i32 timeMS) const {
    return Mc::ranges::lower_bound(this->objects, timeMS, {}, [](const auto &obj) { return obj->getClickTime(); }) -
           this->objects.begin();
}

const MapObjects::Sounds &MapObjects::getSounds() const {
    if(!this->sounds) {
        Sounds made;
        for(const auto &obj : this->objects) obj->addSoundCues(made.cues);
        Mc::ranges::stable_sort(made.cues, {}, &HitObject::SoundCue::timeMS);
        for(const auto &cue : made.cues) {
            if(cue.kind == HitObject::SoundCue::Kind::SLIDE) {
                made.longestSlideMS = std::max(made.longestSlideMS, cue.endTimeMS - cue.timeMS);
            }
        }
        this->sounds = std::move(made);
    }
    return *this->sounds;
}

MapObjects::Shown &MapObjects::shownOf(const HitObject &object) {
    auto it = Mc::ranges::lower_bound(this->byEndTime, object.getEndTime(), {},
                                      [](const Shown &shown) { return shown.object->getEndTime(); });
    while(it->object != &object) ++it;
    return *it;
}

void MapObjects::remake(const Document &document, std::span<const uSz> indexes) {
    const Primitives::PRIMITIVE_CONTAINER &c = document.getPrimitives();
    // their entries by end time give way to the new entries, each where it goes
    std::vector<const HitObject *> gone;
    for(const uSz i : indexes) gone.push_back(this->objects[i].get());
    Mc::ranges::sort(gone);
    std::erase_if(this->byEndTime,
                  [&](const Shown &shown) { return std::binary_search(gone.begin(), gone.end(), shown.object); });
    // the longest an object lasts, found again when one that lasted as long got shorter
    bool shorter = false;
    std::vector<Shown> made;
    for(const uSz i : indexes) {
        const i32 duration = this->objects[i]->getDuration();
        this->objects[i] = HitObjects::create(c, i, nullptr, &this->view);
        shorter |= duration == this->longestObjectMS && this->objects[i]->getDuration() < duration;
        this->longestObjectMS = std::max(this->longestObjectMS, this->objects[i]->getDuration());
        const Document::Id id = document.getIdAt(this->objects[i]->getLine());
        this->ids[i] = id;
        this->indexes[id] = i;
        made.push_back({.object = this->objects[i].get(), .id = id, .meshBuilt = false});
    }
    if(shorter) {
        this->longestObjectMS = 0;
        for(const auto &obj : this->objects) {
            this->longestObjectMS = std::max(this->longestObjectMS, obj->getDuration());
        }
    }
    Mc::ranges::sort(made, {}, endOrder);
    std::vector<Shown> merged;
    merged.reserve(this->byEndTime.size() + made.size());
    auto next = this->byEndTime.begin();
    for(const Shown &shown : made) {
        const auto at = std::upper_bound(next, this->byEndTime.end(), endOrder(shown),
                                         [](const auto &order, const Shown &s) { return order < endOrder(s); });
        merged.insert(merged.end(), next, at);
        merged.push_back(shown);
        next = at;
    }
    merged.insert(merged.end(), next, this->byEndTime.end());
    this->byEndTime = std::move(merged);
}

void MapObjects::startCombosAround(std::span<const uSz> indexes) {
    // from the combo start for each one through the end of the combo after it (its end may have moved)
    const uSz count = this->objects.size();
    uSz done = 0;
    for(const uSz i : indexes) {
        if(i < done) continue;
        uSz from = i;
        while(from > 0 && !this->objects[from - 1]->isEndOfCombo()) from--;
        uSz to = i;
        for(i32 ends = 0; to < count && ends < 2; to++) ends += this->objects[to]->isEndOfCombo() ? 1 : 0;
        HitObjects::startCombos(std::span{this->objects}.subspan(from, to - from));
        done = to;
    }
}

void MapObjects::restackAround(std::span<const uSz> indexes, const Primitives::PRIMITIVE_CONTAINER &c) {
    // the runs found again from the one holding the object before the first (which starts where it did) until, past
    // the last, one starts where one did; the ones in between stacked again (a slider whose stack changed draws its
    // body anew)
    const uSz before = indexes.front() > 0 ? indexes.front() - 1 : 0;
    const auto first = std::prev(Mc::ranges::upper_bound(this->stackRuns, before));
    std::vector<uSz> runs{*first};
    uSz end = this->objects.size();
    for(uSz run = this->nextRun(*first, c); run < this->objects.size(); run = this->nextRun(run, c)) {
        if(run > indexes.back() && std::binary_search(this->stackRuns.begin(), this->stackRuns.end(), run)) {
            end = run;
            break;
        }
        runs.push_back(run);
    }
    for(uSz r = 0; r < runs.size(); r++) {
        const uSz to = r + 1 < runs.size() ? runs[r + 1] : end;
        const auto run = std::span{this->objects}.subspan(runs[r], to - runs[r]);
        std::vector<i32> stacks;
        stacks.reserve(run.size());
        for(const auto &obj : run) stacks.push_back(obj->getStack());
        HitObjects::stack(run, c.AR, c.version, c.stackLeniency, this->view.rawHitcircleDiameter, false);
        for(uSz j = 0; j < run.size(); j++) {
            if(run[j]->isSlider() && run[j]->getStack() != stacks[j]) this->shownOf(*run[j]).meshBuilt = false;
        }
    }
    const auto last = Mc::ranges::lower_bound(this->stackRuns, end);
    this->stackRuns.insert(this->stackRuns.erase(first, last), runs.begin(), runs.end());
}

uSz MapObjects::nextRun(uSz start, const Primitives::PRIMITIVE_CONTAINER &c) const {
    return Primitives::nextStackingRun(
        Primitives::ObjectGetter<HitObject>{[this](uSz i) { return this->objects[i].get(); }}, this->objects.size(),
        start, c.AR, c.stackLeniency);
}
