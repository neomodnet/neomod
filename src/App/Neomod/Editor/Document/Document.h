#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "BeatSnap.h"
#include "BeatmapFile/BeatmapFile.h"
#include "BeatmapFile/BeatmapPrimitives.h"
#include "Database.h"
#include "Hashing.h"
#include "MD5Hash.h"
#include "noinclude.h"
#include "types.h"

#include <cassert>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// a map open in the editor: the lines of its .osu file, each with an id that edits, undo and redo keep. it changes only
// in named steps (change(), drag()), which undo() and redo() take back and make again exactly. after each step the
// game reads the lines again: only the changed ones when they're object or break lines, else all of them. a step also
// keeps the object lines in time order, fits the breaks to the gaps it changed and writes the new combo the game gives
// an object after a spinner or a break. unchanged lines stay the bytes the file was read from
class Document {
    NOCOPY_NOMOVE(Document)
   public:
    // the identity a line keeps across edits, undo and redo
    using Id = u32;

    // what a step did, for the Edit menu to name it
    enum class Action : u8 {
        ADD_BOOKMARK,
        REMOVE_BOOKMARK,
        REVERT,
        RESTORE,
        ADD_CIRCLE,
        ADD_SLIDER,
        ADD_SPINNER,
        EDIT_SLIDER,
        CURVE_TYPE,
        REPEATS,
        SPINNER_END,
        REVERSE,
        FLIP,
        ROTATE,
        SCALE,
        POLYGON,
        STREAM,
        RESET_SAMPLES,
        RESET_COLOURS,
        DELETE_OBJECTS,
        MOVE,
        NUDGE,
        NEW_COMBO,
        COLOUR_SKIP,
        HITSOUNDS,
        SAMPLE_SET,
        ADDITION_SET,
        SAMPLE_INDEX,
        SAMPLE_VOLUME,
        SAMPLE_FILE,
        REMOVE_BREAK,
        MOVE_BREAK,
        INSERT_BREAK,
        RESET_BREAKS,
        CUT,
        PASTE,
        CLONE,
        METADATA,
        DIFFICULTY,
        MAP_SETTINGS,
        COLOURS,
        TIMING,
        ADD_TIMING_POINT,
        DELETE_TIMING_POINT,
        RESNAP,
        SLIDER_LENGTHS,
        BACKGROUND,
    };

    struct Line {
        Id id{0};
        std::string_view text;                // with its line break (the last line may have none)
        std::shared_ptr<const void> owner{};  // what text points into
    };

    // the line a break is on, and when it is
    struct Break {
        Id id;
        i64 start;
        i64 end;
    };

    // the edits in one step (see change()), each a splice in the document lines
    class Change {
        NOCOPY_NOMOVE(Change)
       public:
        ~Change() = default;

        // the line becomes `text` (without a line break: it keeps its own)
        void replace(Id id, std::string_view text);
        // a new line before position `at` (the number of lines: at the end), with the line break the file uses there
        Id insert(uSz at, std::string_view text);
        // new lines there, in this order
        std::vector<Id> insert(uSz at, std::span<const std::string> texts);
        void remove(Id id);
        void remove(std::span<const Id> ids);

        // the last `key: value` line in the sections read as `kind` becomes `key: value` with this value, or a new one
        // goes after the last line of those sections that says something (in a new section if there's none); no value
        // removes every line with that key there
        void setValue(neomod::BeatmapFile::SectionKind kind, std::string_view key,
                      std::optional<std::string_view> value);

        // the line for an object becomes `object`
        void setObject(Id id, const neomod::BeatmapFile::HitObject &object);
        // a new object line (in a new [HitObjects] if there's none)
        Id addObject(const neomod::BeatmapFile::HitObject &object);
        std::vector<Id> addObjects(std::span<const neomod::BeatmapFile::HitObject> objects);
        // a new break line, in time order among the others (in a new [Events] if there's none)
        Id addBreak(i64 start, i64 end);
        // one break edge goes to timeMS, as near as its gap allows: no earlier than a break starts by itself or later
        // than it ends, within 5 s of the object on its side and 650 ms from the other edge. false, nothing changed,
        // when the break is between no two objects or no such time exists
        bool moveBreakEdge(Id id, bool end, i64 timeMS);
        // the break a gap of 5 s gets by itself, for the gap between objects timeMS is in: only if that gap has none and
        // is long enough for one (1.2 s, and 650 ms of break). its line, or 0
        Id insertBreak(i64 timeMS);
        // every gap has the breaks it gets by itself again: one from 5 s on, none under
        void resetBreaks();
        // the line for a timing point becomes `point`
        void setTimingPoint(Id id, const neomod::BeatmapFile::TimingPoint &point);
        // a new timing point line (in a new [TimingPoints] if there's none)
        Id addTimingPoint(const neomod::BeatmapFile::TimingPoint &point);
        // the map background (its first background event) becomes the image `file` in its folder; a map without one
        // gets a new event, first in its events (under "//Background and Video events" when it has that)
        void setBackground(std::string_view file);

       private:
        friend class Document;
        explicit Change(Document &document) : document(document) {
            assert(document.open && "a change outside of a step");
        }
        // where a new entry of `kind` goes: after the last one there, else after the header in the last such section,
        // else in a new section, in the usual section order
        uSz newEntryAt(neomod::BeatmapFile::SectionKind kind);
        Document &document;
        std::vector<Break> addedBreaks;  // by addBreak(), which the document doesn't list before the step is read
    };

    struct ReadResult {
        std::unique_ptr<Document> document;
        neomod::Primitives::LoadError error;
    };
    // the file at `path` (on the main thread, as read by a play)
    static ReadResult read(Database::MapLocation location, std::string_view path);
    // the file at `path` with `draft` as its content: unsaved changes made to it
    static ReadResult read(Database::MapLocation location, std::string_view path, std::span<const u8> draft);
    ~Document();

    // makes the edits `edit` (a callable taking a Change &) does as one step; whether anything changed. it changes
    // nothing if the game couldn't read the result (getError() says why). consecutive steps with the same action and
    // nonzero gesture (a key held down) are one. object lines a step changed move to their place in time order,
    // keeping their ids, and the breaks and new combos they affect follow them; so do the timing point lines it set or
    // added (in the order the game reads them, Primitives::timingOrder())
    template <typename F>
    bool change(Action action, F &&edit, u64 gesture = 0) {
        this->begin(action, gesture);
        Change change{*this};
        std::forward<F>(edit)(change);
        return this->commit();
    }
    // the same for a drag, whose every update says all it changed since it started: the step the gesture made so far
    // is taken back before `edit` runs, so a drag that ends where it started leaves no step. `edit` sees the lines as
    // they were before the drag, but getPrimitives() and the other reads still as the last update left them
    template <typename F>
    bool drag(Action action, F &&edit, u64 gesture) {
        this->takeBack(action, gesture);
        return this->change(action, std::forward<F>(edit), gesture);
    }
    [[nodiscard]] bool canUndo() const { return this->undone < this->steps.size(); }
    [[nodiscard]] bool canRedo() const { return this->undone > 0; }
    // the steps undo() and redo() would take back or do again
    [[nodiscard]] Action getUndoAction() const {
        assert(this->canUndo());
        return this->steps[this->steps.size() - 1 - this->undone].action;
    }
    [[nodiscard]] Action getRedoAction() const {
        assert(this->canRedo());
        return this->steps[this->steps.size() - this->undone].action;
    }
    // the gesture in the last step done (0: none, or it was undone)
    [[nodiscard]] u64 getLastGesture() const {
        return this->undone == 0 && !this->steps.empty() ? this->steps.back().gesture : 0;
    }
    // the ids for the lines the step changed (those there now), or nothing if there was none to undo or redo
    std::optional<std::vector<Id>> undo();
    std::optional<std::vector<Id>> redo();
    // the file read again from disk (`path`), as a step if that changes anything; false if it can't be read. a file
    // still as it was last read or saved gives back the content then (without the settings a save added to it)
    bool revert(std::string_view path);
    // the content becomes `bytes` (another version of the map), as a step; false, nothing changed, if the game can't
    // read them. the file is what it was: the document is changed until it's saved
    bool restore(std::vector<u8> bytes);
    // the timeline zoom, the beat divisor, the distance snap spacing and the grid size, which a save writes to [Editor]
    struct EditorSettings {
        f32 zoom;
        i32 divisor;
        f32 spacing;
        i32 gridSize;
    };
    // the file a save writes: the content with these settings, which are no change to the map (no step, and the
    // content stays as it is)
    [[nodiscard]] std::vector<u8> getBytesWith(const EditorSettings &settings);
    // the content was written to the file as `written` (as getBytesWith() made it): an unchanged document has it from
    // now on
    void saved(std::span<const u8> written);
    // the file is at `location` from now on (renamed): the content, its changes and the history stay
    void moveTo(Database::MapLocation location) { this->location = std::move(location); }

    [[nodiscard]] const Database::MapLocation &getLocation() const { return this->location; }
    // the md5 of the file as last read or written
    [[nodiscard]] const MD5Hash &getMD5() const { return this->fileMD5; }
    // the content when the file was last read or saved. after a save it lacks the editor settings the save wrote (see
    // getBytesWith()), so it doesn't hash to getMD5()
    [[nodiscard]] std::span<const u8> getSavedBytes() const { return *this->fileBytes; }
    // the content differs from what was last read or saved
    [[nodiscard]] bool isChanged() const;
    // why the last change was refused (see change()), if it was
    [[nodiscard]] const neomod::Primitives::LoadError &getError() const { return this->error; }
    // the content as it is now (a save adds the editor settings, see getBytesWith())
    [[nodiscard]] std::span<const u8> getBytes() const;
    [[nodiscard]] std::span<const Line> getLines() const { return this->lines; }
    // the id for line `number` (1-based) in getBytes(), as the file layer counts lines (0 without one)
    [[nodiscard]] Id getIdAt(u32 number) const;
    // the line number for this id (1-based, 0 without one)
    [[nodiscard]] u32 getNumber(Id id) const;
    // the record for an object line (none if it reads as no object)
    [[nodiscard]] std::optional<neomod::BeatmapFile::HitObject> getObject(Id id) const;

    // what the game reads from it now
    [[nodiscard]] const neomod::BeatmapFile &getFile() const { return this->structure(); }
    [[nodiscard]] const neomod::Primitives::PRIMITIVE_CONTAINER &getPrimitives() const { return this->primitives; }
    // what the last change, undo or redo read of the objects when it read only lines that changed (none: it read
    // everything): the revision before it, the objects read again and those whose combo was counted otherwise (indexes
    // into getPrimitives().objects, in order); the others are as they were
    struct Patch {
        u32 from;
        std::vector<uSz> objects;
        std::vector<uSz> combos;
    };
    [[nodiscard]] const std::optional<Patch> &getPatched() const { return this->patched; }
    // in time order, with their meters, kiai and omitted bar lines
    [[nodiscard]] std::span<const neomod::BeatmapFile::TimingPoint> getTimingPoints() const {
        return this->timingPoints;
    }
    // the line each one is on
    [[nodiscard]] std::span<const Id> getTimingPointIds() const { return this->timingIds; }
    // the beat grid from its uninherited ones
    [[nodiscard]] const BeatSnap &getBeatSnap() const { return this->beatSnap; }
    // in time order
    [[nodiscard]] std::span<const i32> getBookmarks() const { return this->bookmarks; }
    // by start, as read by the game
    [[nodiscard]] std::span<const Break> getBreaks() const { return this->breaks; }
    // the rules give its gap a break (a gap of 5 s or more): taken out, the next fit of that gap would put one back
    [[nodiscard]] bool isRequired(const Break &b) const;
    // where the song select preview starts (-1 without one)
    [[nodiscard]] i32 getPreviewTime() const { return this->previewTime; }
    // the slider velocity base in full precision, as written (what's computed from it may be written too)
    [[nodiscard]] f64 getSliderMultiplier() const { return this->sliderMultiplier; }
    // the image the game shows behind the map (the file in its first background event; empty without one)
    [[nodiscard]] std::string_view getBackground() const { return this->background; }
    // the editor settings the content has in [Editor], if it says (as the file was read: a save writes the settings to
    // the file only)
    [[nodiscard]] std::optional<f32> getTimelineZoom() const { return this->timelineZoom; }
    [[nodiscard]] std::optional<i32> getBeatDivisor() const { return this->beatDivisor; }
    [[nodiscard]] std::optional<f32> getDistanceSpacing() const { return this->distanceSpacing; }
    [[nodiscard]] std::optional<i32> getGridSize() const { return this->gridSize; }
    // goes up with every change, undo and redo, so views know when to look again
    [[nodiscard]] u32 getRevision() const { return this->revision; }

   private:
    struct Splice {
        uSz at;
        std::vector<Line> removed;
        std::vector<Line> inserted;
        bool reorder{false};  // the same lines in another order (see placeObjects())
    };
    struct Step {
        Action action;
        u64 gesture;
        std::vector<Splice> splices;
    };
    static constexpr uSz MAX_STEPS{1000};

    Document(Database::MapLocation location, std::shared_ptr<const std::vector<u8>> fileBytes);
    // the lines of `text`, new ids each, which `owner` keeps alive
    [[nodiscard]] std::vector<Line> split(std::string_view text, const std::shared_ptr<const void> &owner);
    [[nodiscard]] Line makeLine(Id id, std::string text) const;

    void begin(Action action, u64 gesture);
    bool commit();
    // takes back the last step if the gesture made it (see drag())
    void takeBack(Action action, u64 gesture);
    // a line that's ordered by a key: where it is
    template <typename Key>
    struct Placed {
        uSz at;
        Key key;
        bool operator==(const Placed &) const = default;
    };
    // moves the ones of these lines (in file order) that `changed` has to their place by key among the others, which
    // keep theirs
    template <typename Key>
    void place(const std::vector<Placed<Key>> &entries, const Hash::flat::set<Id> &changed);
    // moves the object lines the open step changed to their place in time order, among the others, which keep theirs
    void placeObjects(const Hash::flat::set<Id> &changed);
    // the same for its timing point lines
    void placeTimingPoints(const Hash::flat::set<Id> &changed);
    // the breaks in the gaps the open step changed (against `base`) fitted to them, and the new combo written for
    // objects the game starts one on (after a spinner or a break) whose line or gap the step changed; whether that
    // edited anything
    bool fit(const Hash::flat::set<Id> &changed);
    // replaces `count` lines from `at` with `inserted`, in the open step (reorder: the same lines in another order)
    void splice(uSz at, uSz count, std::vector<Line> inserted, bool reorder = false);
    // these, made from the lines as they are, in the open step
    void splice(std::vector<Splice> splices);
    // makes the lines what the splices made them (backwards: what they were before them)
    void apply(std::span<const Splice> splices, bool backwards);
    void apply(const Splice &splice, bool backwards);
    // the ids a step brought in, or (backwards) took out, that are there now (none for a step that replaced the whole
    // content)
    [[nodiscard]] std::vector<Id> touched(const Step &step, bool backwards) const;
    [[nodiscard]] uSz indexOf(Id id) const;
    // the line break the file uses at `at`, for a new line there
    [[nodiscard]] std::string_view lineBreakAt(uSz at) const;
    // the bytes and the file layer over the lines as they are now (within a step too)
    const neomod::BeatmapFile &structure() const;
    // reads what the game reads from the lines as they are now; false if it can't (what was read before stays)
    bool derive();
    // reads only the lines rewritten or moved since the lines were last read, when each is an object line (the objects
    // staying in play order) or a break line, and counts the combos again; false, reading nothing, otherwise
    bool patch();
    // patch(), else derive()
    bool reread();

    Database::MapLocation location;
    std::shared_ptr<const std::vector<u8>> fileBytes;  // see getSavedBytes()
    MD5Hash fileMD5;                                   // see getMD5()
    std::vector<Line> lines;
    Id nextId{1};
    mutable Hash::flat::map<Id, uSz> indexes;  // line positions by id, made again when they moved (empty)

    std::vector<Step> steps;  // done, then the undone ones
    uSz undone{0};            // how many of the last steps are undone
    std::optional<Step> open;
    bool timingWritten{false};    // the open step set or added timing point lines (see placeTimingPoints())
    std::optional<Step> retaken;  // the step taken back for the open one (see drag())

    // from the lines (structure(), derive(), patch())
    mutable std::string bytes;
    mutable std::unique_ptr<neomod::BeatmapFile> file;
    mutable bool structureStale{true};
    neomod::Primitives::PRIMITIVE_CONTAINER primitives;
    // what changed since the lines were last read: lines rewritten or moved in place, or more (lines put in, taken out,
    // replaced by others)
    std::vector<Id> rewritten;
    bool reshaped{true};
    // every [HitObjects] entry is one of the objects, which are in line order
    bool objectsInOrder{false};
    std::optional<Patch> patched;
    // what the lines the open step started from read as (for later drag updates, those the first update started from),
    // which fit() compares with
    struct Base;
    std::unique_ptr<Base> base;
    bool baseTaken{false};
    neomod::Primitives::LoadError error;
    std::vector<neomod::BeatmapFile::TimingPoint> timingPoints;
    std::vector<Id> timingIds;
    BeatSnap beatSnap{std::span<const neomod::BeatmapFile::TimingPoint>{}};
    std::vector<i32> bookmarks;
    std::vector<Break> breaks;
    i32 previewTime{-1};
    f64 sliderMultiplier{1.0};
    std::string background;
    std::optional<f32> timelineZoom;
    std::optional<i32> beatDivisor;
    std::optional<f32> distanceSpacing;
    std::optional<i32> gridSize;
    mutable bool changed{false};
    mutable std::optional<u32> changedAt;  // the revision `changed` was found for
    u32 revision{0};
};
