#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "Clock.h"
#include "Compose.h"
#include "Document.h"
#include "MapObjects.h"
#include "Playback.h"
#include "Snapping.h"
#include "noinclude.h"
#include "types.h"

#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class ConVar;

// what the editor parts share: the map shown (its document and objects), the clock, the compose tools and the view
// settings. owned by the root, which the parts it lends it to don't outlive. a document belongs to the map from open()
// to close() and changes only through edit(), drag(), undo(), redo() and revert(), which make its objects again before
// they return
struct EditorState {
    NOCOPY_NOMOVE(EditorState)
   public:
    // what the middle of the editor shows: the playfield with its tools, the timing points, the song setup, the map
    // issues, or its saved versions
    enum class Tab : u8 { COMPOSE, TIMING, SETUP, VERIFY, HISTORY };

    EditorState() = default;
    ~EditorState() = default;

    // a write for a map in the osu!stable songs folder goes to its copy in the neomod library (Database::standIn): true
    // while that copy isn't there, which this asks for, and `then` runs again once it is (see afterCopy)
    bool copyFirst(std::function<void()> then);
    // `document` belongs to the map from now on, its objects made
    void open(std::unique_ptr<Document> document);
    // the map document goes, with its objects, what compose kept from it and the writes waiting for the set copy (once
    // it isn't shown)
    void close();
    // makes `edit` (a callable taking a Document::Change &) a step on the shown map (see Document::change()), says why
    // if the game couldn't read the result, and makes the objects again; whether anything changed
    template <typename F>
    bool edit(Document::Action action, F &&edit, u64 gesture = 0) {
        assert(this->document && "an edit without a map");
        const bool changed = this->document->change(action, std::forward<F>(edit), gesture);
        if(!changed) this->explainRefusal();
        this->refreshObjects();
        return changed;
    }
    // the same for a drag update (see Document::drag())
    template <typename F>
    bool drag(Document::Action action, F &&edit, u64 gesture) {
        assert(this->document && "a drag without a map");
        const bool changed = this->document->drag(action, std::forward<F>(edit), gesture);
        if(!changed) this->explainRefusal();
        this->refreshObjects();
        return changed;
    }
    // see Document::undo() and redo(): the ids for the lines the step changed, whose objects are made by then
    std::optional<std::vector<Document::Id>> undo();
    std::optional<std::vector<Document::Id>> redo();
    // see Document::revert()
    bool revert(std::string_view path);
    // see Document::restore()
    bool restore(std::vector<u8> bytes);
    // says why the document refused its last change, if it did (the game couldn't read the result)
    void explainRefusal() const;
    // past circles show the play hit animations instead of the editor trail
    [[nodiscard]] bool showsHitAnimations() const;
    // the timeline circles say which sounds they play
    [[nodiscard]] bool showsSoundLabels() const;
    // the playfield shows its grid
    [[nodiscard]] bool showsGrid() const;
    // how positions snap now: the grid and distance snap settings as the held keys change them. Shift turns grid snap
    // around and Alt distance snap. with distance snap on and grid snap off, Shift or Ctrl alone snaps the angle
    [[nodiscard]] Snapping::Settings snapSettings() const;
    // an id for the steps of one gesture (see Document::change()), which no other gesture has
    [[nodiscard]] u64 newGesture() { return ++this->gestures; }
    // a changed timing point takes the objects in its section along (see Retiming::apply())
    [[nodiscard]] bool carriesObjects() const;

    std::unique_ptr<Document> document;   // the map shown
    std::unique_ptr<MapObjects> objects;  // made from the document as it is now (see refreshObjects())
    Clock clock;
    Playback playback;
    Compose compose{*this};
    Tab tab{Tab::COMPOSE};
    u64 gestures{0};
    // the folder in maps/ an osu!stable set is being copied into (see copyFirst(); empty: none), and the writes
    // waiting for it, dropped when another map is opened
    std::string copying;
    std::vector<std::function<void()>> afterCopy;

    // the timeline shows 6 s at 1, from 0.1 to 8
    f32 zoom{1.f};
    // ticks per beat (see BeatSnap::DIVISORS)
    i32 divisor{1};
    // for distance snap, 0.1 to 6
    f32 spacing{0.8f};
    // osu!px (see Snapping::GRID_SIZES)
    i32 gridSize{32};
    // editor_hit_animations, editor_grid_snap and editor_distance_snap from the root, which outlive the state (none:
    // off)
    const ConVar *hitAnimations{nullptr};
    ConVar *gridSnap{nullptr};
    ConVar *distanceSnap{nullptr};
    // its editor_metronome and editor_timing_moves_objects (none: on, as they are by default)
    ConVar *metronome{nullptr};
    ConVar *carryObjects{nullptr};
    // its editor_background_dim (none: 0)
    const ConVar *backgroundDim{nullptr};
    // its editor_hitsound_labels (none: off)
    const ConVar *soundLabels{nullptr};
    // its editor_grid (none: on, as it is by default)
    const ConVar *grid{nullptr};

   private:
    // makes the document objects again (only those it patched, if that's all it did) after a change (never while
    // drawing, which keeps pointers to them)
    void refreshObjects();
};
