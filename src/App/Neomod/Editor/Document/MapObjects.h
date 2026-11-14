#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "Document.h"
#include "Hashing.h"
#include "HitObjects.h"
#include "PlayfieldView.h"
#include "noinclude.h"
#include "types.h"

#include <memory>
#include <optional>
#include <span>
#include <vector>

// the document objects as made by the game (combos, slider timing, stacking), drawn on `view`, and the sounds a perfect
// play of them makes
class MapObjects {
    NOCOPY_NOMOVE(MapObjects)
   public:
    struct Shown {
        neomod::HitObject *object;
        Document::Id id;
        bool meshBuilt;  // sliders build theirs before they're first drawn
    };
    struct Sounds {
        std::vector<neomod::HitObject::SoundCue> cues;  // in time order
        i32 longestSlideMS{0};                          // how far back a slide that still plays can start
    };

    explicit MapObjects(const Document &document);
    ~MapObjects();

    // brings the objects up to the document revision when its last change read only the lines that changed
    // (Document::getPatched()): makes the objects that were read again anew, gives the recounted ones their combos and
    // stacks the runs around them again. false, changing nothing, when the document read everything: the caller makes
    // new MapObjects
    [[nodiscard]] bool update(const Document &document);

    // where and how big the objects are drawn: the drawer sets the skin, the playfield place and scale
    PlainPlayfieldView view{nullptr, vec2{0.f}, 1.f, 0.f};
    std::vector<std::unique_ptr<neomod::HitObject>> objects;  // in time order
    std::vector<Document::Id> ids;                            // the line each one is on
    Hash::flat::map<Document::Id, uSz> indexes;               // into objects, by line
    std::vector<Shown> byEndTime;
    i32 longestObjectMS{0};  // the longest an object lasts (how far after its start it can end)
    u32 revision;            // the document revision they were made from

    // the object made from the line with this id (none: no object comes from it)
    [[nodiscard]] neomod::HitObject *find(Document::Id id) const {
        const auto it = this->indexes.find(id);
        return it != this->indexes.end() ? this->objects[it->second].get() : nullptr;
    }
    // the index of the first object starting at timeMS or later (the number of objects if none does)
    [[nodiscard]] uSz firstFrom(i32 timeMS) const;
    // made when first asked for after the objects changed: only playback needs them
    [[nodiscard]] const Sounds &getSounds() const;

   private:
    // the entry for the object
    Shown &shownOf(const neomod::HitObject &object);
    // these (indexes, in order) made again from the document objects
    void remake(const Document &document, std::span<const uSz> indexes);
    // the objects around these (in order) given their combo start times again
    void startCombosAround(std::span<const uSz> indexes);
    // the runs of objects around these (in order) that stack among themselves found again and stacked again
    void restackAround(std::span<const uSz> indexes, const neomod::Primitives::PRIMITIVE_CONTAINER &c);
    // where the run after the one starting at `start` starts (see Primitives::nextStackingRun())
    [[nodiscard]] uSz nextRun(uSz start, const neomod::Primitives::PRIMITIVE_CONTAINER &c) const;

    std::vector<uSz> stackRuns;  // where the runs of objects that stack only among themselves start
    mutable std::optional<Sounds> sounds;
};
