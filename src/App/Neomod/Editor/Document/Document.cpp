// Copyright (c) 2026, WH, All rights reserved.
#include "Document.h"

#include "ContainerRanges.h"
#include "DatabaseBeatmap.h"
#include "File.h"
#include "GameRules.h"
#include "OsuConVars/DiffCalcDefaults.h"
#include "Parsing.h"
#include "SString.h"
#include "crypto.h"

#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <utility>

using namespace neomod;
using Kind = BeatmapFile::SectionKind;
using HO = BeatmapFile::HitObject;

namespace {
// the usual section order, which a new one follows
constexpr std::array SECTION_ORDER{Kind::GENERAL, Kind::EDITOR,        Kind::METADATA, Kind::DIFFICULTY,
                                   Kind::EVENTS,  Kind::TIMING_POINTS, Kind::COLOURS,  Kind::HIT_OBJECTS};
constexpr std::string_view CRLF{"\r\n"};

// a break starts this long after the latest end before it at the earliest
constexpr i64 BREAK_AFTER_MS{200};
constexpr i64 MIN_BREAK_MS{650};
// a gap shorter than this has no break, one this long gets one
constexpr i64 MIN_GAP_MS{1200};
constexpr i64 FILLED_GAP_MS{5000};
// how far from its object an edge moved by hand can stay
constexpr i64 MAX_EDGE_MS{5000};
constexpr i64 OPEN{std::numeric_limits<i64>::max()};

// the object times as made by the game, in play order
struct Span {
    i64 start;
    i64 end;
    bool spinner;
    bool newCombo;  // from its line
    u8 colorSkip;   // ...and its colour skip
    u32 line;
};

Span spanOf(const Primitives::Object &object) {
    if(const auto *h = std::get_if<DBType::HITCIRCLE>(&object)) {
        return {.start = h->time,
                .end = h->time,
                .spinner = false,
                .newCombo = h->newCombo,
                .colorSkip = h->colorSkip,
                .line = h->line};
    }
    if(const auto *s = std::get_if<DBType::SLIDER>(&object)) {
        // as long as sliders last in the game
        const i32 duration = (i32)s->sliderTime;
        return {.start = s->time,
                .end = (i64)s->time + (duration >= 0 ? duration : 1),
                .spinner = false,
                .newCombo = s->newCombo,
                .colorSkip = s->colorSkip,
                .line = s->line};
    }
    const auto &spinner = std::get<DBType::SPINNER>(object);
    return {.start = spinner.time,
            .end = spinner.endTime,
            .spinner = true,
            .newCombo = spinner.newCombo,
            .colorSkip = spinner.colorSkip,
            .line = spinner.line};
}

std::vector<Span> spansOf(const Primitives::PRIMITIVE_CONTAINER &c) {
    std::vector<Span> spans;
    spans.reserve(c.objects.size());
    for(const auto &object : c.objects) spans.push_back(spanOf(object));
    return spans;
}

// a stretch between objects: gaps[k] ends at object k, from the latest end before it; the first and the last are open
// (before the first object, after the last), so there's one more gap than objects
struct Gap {
    i64 from;
    i64 to;
    [[nodiscard]] bool isOpen() const { return this->from == -OPEN || this->to == OPEN; }
    auto operator<=>(const Gap &) const = default;
};

std::vector<Gap> gapsOf(std::span<const Span> spans) {
    std::vector<Gap> gaps;
    gaps.reserve(spans.size() + 1);
    i64 latest = -OPEN;
    for(const Span &span : spans) {
        gaps.push_back({.from = latest, .to = span.start});
        latest = std::max(latest, span.end);
    }
    gaps.push_back({.from = latest, .to = OPEN});
    return gaps;
}

// the gap a break belongs to: the one its middle is in
uSz gapIndexOf(std::span<const Gap> gaps, const Document::Break &b) {
    return Mc::ranges::lower_bound(gaps, std::midpoint(b.start, b.end), {}, &Gap::to) - gaps.begin();
}

// the approach time breaks are timed by (the map AR within 0 to 10, whatever the game settings are)
i64 preemptOf(f32 AR) {
    return (i64)GameRules::mapDifficultyRange<f64>(std::clamp<f64>(AR, 0.0, 10.0), cv::defaults::approachtime_min,
                                                   cv::defaults::approachtime_mid, cv::defaults::approachtime_max);
}

Document::Break defaultBreak(const Gap &gap, i64 preempt) {
    return {.id = 0, .start = gap.from + BREAK_AFTER_MS, .end = gap.to - preempt};
}

std::string formatBreak(i64 start, i64 end) {
    return BeatmapFile::format(BeatmapFile::Event{
        .kind = BeatmapFile::Event::Kind::BREAK, .start = start, .end = end, .file = {}, .rest = {}});
}

std::shared_ptr<const std::vector<u8>> readFile(std::string_view path) {
    auto bytes = std::make_shared<std::vector<u8>>();
    if(File file(path); file.canRead()) file.readToVector(*bytes);
    return bytes;
}

std::string_view textOf(std::span<const u8> bytes) {
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

// the line break on a line (none for a last line without one)
std::string_view lineBreak(std::string_view line) {
    uSz content = line.size();
    if(content > 0 && line[content - 1] == '\n') content--;
    if(content > 0 && line[content - 1] == '\r') content--;
    return line.substr(content);
}

bool isBlank(std::string_view line) { return lineBreak(line).size() == line.size(); }

// a splice that replaces one line with another text of it
template <typename Splice>
bool inPlace(const Splice &s) {
    return s.removed.size() == 1 && s.inserted.size() == 1 && s.removed[0].id == s.inserted[0].id;
}
}  // namespace

struct Document::Base {
    std::vector<Gap> gaps;
    i64 preempt;
};

Document::Document(Database::MapLocation location, std::shared_ptr<const std::vector<u8>> fileBytes)
    : location(std::move(location)), fileBytes(std::move(fileBytes)), fileMD5(crypto::hash::md5(*this->fileBytes)) {}

Document::~Document() = default;

Document::ReadResult Document::read(Database::MapLocation location, std::string_view path) {
    auto bytes = readFile(path);
    if(bytes->empty()) return {.document = nullptr, .error = {Primitives::LoadError::FILE_LOAD}};
    std::unique_ptr<Document> document{new Document(std::move(location), bytes)};
    document->lines = document->split(textOf(*bytes), bytes);
    if(!document->derive()) return {.document = nullptr, .error = document->error};
    return {.document = std::move(document), .error = {}};
}

Document::ReadResult Document::read(Database::MapLocation location, std::string_view path, std::span<const u8> draft) {
    std::unique_ptr<Document> document{new Document(std::move(location), readFile(path))};
    auto bytes = std::make_shared<const std::vector<u8>>(draft.begin(), draft.end());
    document->lines = document->split(textOf(*bytes), bytes);
    if(!document->derive()) return {.document = nullptr, .error = document->error};
    return {.document = std::move(document), .error = {}};
}

std::vector<Document::Line> Document::split(std::string_view text, const std::shared_ptr<const void> &owner) {
    // a byte order mark goes with the first line: the file layer counts lines after it
    const uSz bom = text.starts_with(BeatmapFile::BOM) ? BeatmapFile::BOM.size() : 0;
    std::vector<Line> out;
    while(!text.empty()) {
        const uSz skip = out.empty() ? bom : 0;
        const uSz length = skip + BeatmapFile::firstLine(text.substr(skip)).size();
        out.push_back({.id = this->nextId++, .text = text.substr(0, length), .owner = owner});
        text.remove_prefix(length);
    }
    return out;
}

Document::Line Document::makeLine(Id id, std::string text) const {
    auto owned = std::make_shared<const std::string>(std::move(text));
    return {.id = id, .text = *owned, .owner = owned};
}

void Document::begin(Action action, u64 gesture) {
    assert(!this->open && "a step within a step");
    this->open = Step{.action = action, .gesture = gesture, .splices = {}};
    this->timingWritten = false;
    this->error = {};
    this->patched = Patch{.from = this->revision, .objects = {}, .combos = {}};
}

bool Document::commit() {
    bool readable = true;
    // later drag updates keep the base the first one took
    if(!this->retaken) this->baseTaken = false;
    if(!this->open->splices.empty()) {
        Hash::flat::set<Id> changed;
        for(const Splice &s : this->open->splices) {
            for(const Line &line : s.inserted) changed.insert(line.id);
        }
        // what the step started from, which fit() compares with
        if(!std::exchange(this->baseTaken, true)) {
            this->base.reset(
                new Base{.gaps = gapsOf(spansOf(this->primitives)), .preempt = preemptOf(this->primitives.AR)});
        }
        // a revert or a restored version is the content as it was written
        const bool reverting = this->open->action == Action::REVERT || this->open->action == Action::RESTORE;
        if(!reverting) this->placeObjects(changed);
        if(!reverting && this->timingWritten) this->placeTimingPoints(changed);
        readable = this->reread();
        if(readable && !reverting && this->fit(changed)) readable = this->reread();
    }
    Step step = std::move(*this->open);
    this->open.reset();
    std::optional<Step> retaken = std::exchange(this->retaken, std::nullopt);
    // lines that end up as they were, e.g. a new combo switched off that the rules put back, are no change either
    const bool unchanged = Mc::ranges::all_of(step.splices, inPlace<Splice>) && [&] {
        Hash::flat::map<Id, std::pair<std::string_view, std::string_view>> texts;  // the first and the last
        for(const Splice &s : step.splices) {
            const auto [it, fresh] = texts.try_emplace(s.removed[0].id, s.removed[0].text, s.inserted[0].text);
            if(!fresh) it->second.second = s.inserted[0].text;
        }
        return Mc::ranges::all_of(texts, [](const auto &entry) { return entry.second.first == entry.second.second; });
    }();
    if(readable && unchanged) {
        // a drag back where it started is as before it, which needs reading again
        if(retaken && step.splices.empty()) this->reread();
        return false;
    }

    if(!readable) {
        // the game couldn't read it: as it was (with the step taken back for it)
        this->apply(step.splices, true);
        if(retaken) {
            this->apply(retaken->splices, false);
            this->steps.push_back(std::move(*retaken));
        }
        this->derive();
        return false;
    }

    // what could be redone goes
    this->steps.resize(this->steps.size() - this->undone);
    this->undone = 0;

    Step *last = this->steps.empty() ? nullptr : &this->steps.back();
    if(last && step.gesture != 0 && last->gesture == step.gesture && last->action == step.action) {
        // as one step: a line a later splice replaces in place again, when no splice moved lines, needs only its last
        // text, in the last splice on that line (the earlier ones keep what undo goes back to)
        const bool compact =
            Mc::ranges::all_of(last->splices, inPlace<Splice>) && Mc::ranges::all_of(step.splices, inPlace<Splice>);
        Hash::flat::map<Id, uSz> latest;  // the last splice on each line
        if(compact) {
            for(uSz i = 0; i < last->splices.size(); i++) latest[last->splices[i].inserted[0].id] = i;
        }
        for(Splice &s : step.splices) {
            if(compact) {
                const Id id = s.inserted[0].id;
                if(const auto it = latest.find(id); it != latest.end()) {
                    last->splices[it->second].inserted = std::move(s.inserted);
                    continue;
                }
                latest[id] = last->splices.size();
            }
            last->splices.push_back(std::move(s));
        }
        return true;
    }

    this->steps.push_back(std::move(step));
    if(this->steps.size() > MAX_STEPS) this->steps.erase(this->steps.begin());
    return true;
}

void Document::takeBack(Action action, u64 gesture) {
    assert(!this->open && "a step within a step");
    assert(gesture != 0 && "a drag without a gesture");
    if(this->undone > 0 || this->steps.empty()) return;
    const Step &last = this->steps.back();
    if(last.gesture != gesture || last.action != action) return;
    this->apply(last.splices, true);
    this->retaken = std::move(this->steps.back());
    this->steps.pop_back();
}

template <typename Key>
void Document::place(const std::vector<Placed<Key>> &entries, const Hash::flat::set<Id> &changed) {
    std::vector<uSz> slots;  // where the lines are
    std::vector<Placed<Key>> kept;
    std::vector<Placed<Key>> moved;
    for(const Placed<Key> &entry : entries) {
        slots.push_back(entry.at);
        (changed.contains(this->lines[entry.at].id) ? moved : kept).push_back(entry);
    }
    if(moved.empty()) return;

    // a changed line goes before the first kept one that is later than it: the first whose time is later and the latest
    // among the kept ones so far
    Mc::ranges::stable_sort(moved, {}, &Placed<Key>::key);
    std::vector<Key> latest(kept.size());
    for(uSz k = 0; k < kept.size(); k++) latest[k] = k > 0 ? std::max(latest[k - 1], kept[k].key) : kept[k].key;
    std::vector<uSz> before(moved.size());
    for(uSz i = 0; i < moved.size(); i++) before[i] = Mc::ranges::upper_bound(latest, moved[i].key) - latest.begin();
    // before[] grows with the keys
    std::vector<uSz> order;  // line positions in their new order
    order.reserve(slots.size());
    uSz next = 0;
    for(uSz k = 0; k <= kept.size(); k++) {
        while(next < moved.size() && before[next] == k) order.push_back(moved[next++].at);
        if(k < kept.size()) order.push_back(kept[k].at);
    }

    uSz first = slots.size();
    uSz last = 0;
    for(uSz j = 0; j < slots.size(); j++) {
        if(order[j] == slots[j]) continue;
        first = std::min(first, j);
        last = j;
    }
    if(first == slots.size()) return;

    // one splice over the lines between (the others among them stay where they are)
    const uSz from = slots[first];
    std::vector<Line> placed{this->lines.begin() + (sSz)from, this->lines.begin() + (sSz)slots[last] + 1};
    for(uSz j = first; j <= last; j++) placed[slots[j] - from] = this->lines[order[j]];
    // the last line, which may have no line break, takes the one the file uses where it goes
    for(uSz i = 0; i < placed.size(); i++) {
        if(from + i + 1 < this->lines.size() && lineBreak(placed[i].text).empty()) {
            placed[i] = this->makeLine(placed[i].id, std::string{placed[i].text}.append(this->lineBreakAt(from + i)));
        }
    }
    // counted before the call: arguments may be evaluated in any order, the moved one first
    const uSz count = placed.size();
    this->splice(from, count, std::move(placed), true);
}

void Document::placeObjects(const Hash::flat::set<Id> &changed) {
    using Entry = Placed<i32>;
    // the third field; a line that has none keeps the time from the line before it
    const auto readTime = [](std::string_view text, i32 &time) {
        if(const uSz first = text.find(','); first != std::string_view::npos) {
            if(const uSz second = text.find(',', first + 1); second != std::string_view::npos) {
                Parsing::parse(text.substr(second + 1, text.find(',', second + 1) - second - 1), &time);
            }
        }
    };
    // the object lines in file order: where they are and their times
    const auto entriesOfFile = [&] {
        std::vector<Entry> entries;
        i32 time{std::numeric_limits<i32>::min()};
        for(const auto line : this->structure().getEntries(Kind::HIT_OBJECTS)) {
            readTime(line.text, time);
            entries.push_back({.at = line.number - 1, .key = time});
        }
        return entries;
    };
    std::vector<Entry> entries;
    if(this->objectsInOrder && !this->reshaped) {
        // the objects as last read are the object lines in order, which are where they were: only the ones rewritten or
        // moved since are read again
        std::vector<uSz> since;
        since.reserve(this->rewritten.size());
        for(const Id id : this->rewritten) since.push_back(this->indexOf(id));
        Mc::ranges::sort(since);
        auto next = since.begin();
        i32 time{std::numeric_limits<i32>::min()};
        entries.reserve(this->primitives.objects.size());
        for(const auto &object : this->primitives.objects) {
            const Span span = spanOf(object);
            const uSz at = span.line - 1;
            for(; next != since.end() && *next < at; ++next);
            if(next != since.end() && *next == at) {
                readTime(this->lines[at].text, time);
            } else {
                time = (i32)span.start;
            }
            entries.push_back({.at = at, .key = time});
        }
        assert(entries == entriesOfFile() && "the objects as last read are the object lines");
    } else {
        entries = entriesOfFile();
    }
    this->place(entries, changed);
}

void Document::placeTimingPoints(const Hash::flat::set<Id> &changed) {
    // in the order the game reads them
    std::vector<Placed<std::pair<f64, bool>>> entries;
    BeatmapFile::TimingPoint point;
    for(const auto line : this->structure().getEntries(Kind::TIMING_POINTS)) {
        // a line that reads as none stays with the one before it
        BeatmapFile::parse(line.text, point);
        entries.push_back({.at = line.number - 1, .key = Primitives::timingOrder(point.time, point.uninherited)});
    }
    this->place(entries, changed);
}

bool Document::fit(const Hash::flat::set<Id> &changed) {
    const std::vector<Span> spans = spansOf(this->primitives);
    const std::vector<Gap> gaps = gapsOf(spans);
    const std::vector<Gap> &baseGaps = this->base->gaps;
    const i64 preempt = preemptOf(this->primitives.AR);
    const i64 basePreempt = this->base->preempt;
    // before any line moves
    std::vector<Id> ids;
    ids.reserve(spans.size());
    for(const Span &span : spans) ids.push_back(this->getIdAt(span.line));

    // the gaps the step changed. `moved` ones have times no gap had before the step. `touched` adds every gap when
    // the approach time changed, which default break ends follow. both lists are in time order: one walk compares them
    std::vector<u8> moved(gaps.size());
    std::vector<u8> touched(gaps.size());
    uSz was = 0;
    for(uSz k = 0; k < gaps.size(); k++) {
        while(was < baseGaps.size() && baseGaps[was] < gaps[k]) was++;
        moved[k] = was == baseGaps.size() || baseGaps[was] != gaps[k];
        touched[k] = moved[k] || preempt != basePreempt;
    }

    Change change{*this};
    bool edited = false;
    std::vector<Break> fitted;          // the breaks after the fit, for the new combos
    std::vector<u8> seen(gaps.size());  // the first break in a touched gap was there; the others in it go
    std::vector<u8> held(gaps.size());  // a touched gap keeps a break
    for(const Break &b : this->breaks) {
        const uSz k = gapIndexOf(gaps, b);
        if(!touched[k]) {
            fitted.push_back(b);
            continue;
        }
        const Gap &gap = gaps[k];
        Break out = b;
        bool goes = seen[k] || gap.isOpen() || gap.to - gap.from < MIN_GAP_MS;
        seen[k] = true;
        if(!goes) {
            // an edge that wasn't at its default in the gap it was in was moved by hand: it keeps its time, within
            // reach of the object; the others go to their defaults now
            const Gap &was = baseGaps[gapIndexOf(baseGaps, b)];
            const Break wasDefault = defaultBreak(was, basePreempt);
            const Break now = defaultBreak(gap, preempt);
            const bool movedStart = !was.isOpen() && b.start != wasDefault.start;
            const bool movedEnd = !was.isOpen() && b.end != wasDefault.end;
            out.start = movedStart ? std::clamp(b.start, now.start, gap.from + MAX_EDGE_MS) : now.start;
            out.end = movedEnd ? std::clamp(b.end, gap.to - MAX_EDGE_MS, now.end) : now.end;
            goes = out.end - out.start < MIN_BREAK_MS;
        }
        if(goes) {
            change.remove(b.id);
            edited = true;
            continue;
        }
        if(out.start != b.start || out.end != b.end) {
            change.replace(b.id, formatBreak(out.start, out.end));
            edited = true;
        }
        held[k] = true;
        fitted.push_back(out);
    }
    // a gap of 5 s or more gets one
    std::vector<u8> added(gaps.size());
    for(uSz k = 0; k < gaps.size(); k++) {
        if(!touched[k] || held[k] || gaps[k].isOpen() || gaps[k].to - gaps[k].from < FILLED_GAP_MS) continue;
        Break b = defaultBreak(gaps[k], preempt);
        b.id = change.addBreak(b.start, b.end);
        fitted.push_back(b);
        added[k] = true;
        edited = true;
    }

    // the game starts a new combo on the first object after a spinner or a break (breaks as the game reads them: by
    // start, each counting for the first object that starts after it ends). it's written to the line where the step
    // changed the line or the times in the gap before it, or gave that gap a break. a forced new combo has no colour
    // skip
    Mc::ranges::stable_sort(fitted, {}, &Break::start);
    uSz nextBreak = 0;
    for(uSz i = 0; i < spans.size(); i++) {
        bool afterBreak = false;
        for(; nextBreak < fitted.size() && fitted[nextBreak].end < spans[i].start; nextBreak++) afterBreak = true;
        const bool forced = !spans[i].spinner && (afterBreak || (i > 0 && spans[i - 1].spinner));
        if(i == 0 || !forced || spans[i].newCombo || (!moved[i] && !added[i] && !changed.contains(ids[i]))) continue;
        if(auto object = this->getObject(ids[i])) {
            object->type = (object->type | HO::TYPE_NEW_COMBO) & ~(0b111 << HO::TYPE_COLOUR_SKIP_SHIFT);
            change.setObject(ids[i], *object);
            edited = true;
        }
    }
    return edited;
}

void Document::splice(uSz at, uSz count, std::vector<Line> inserted, bool reorder) {
    std::vector<Splice> one;
    one.push_back({.at = at,
                   .removed = {this->lines.begin() + (sSz)at, this->lines.begin() + (sSz)(at + count)},
                   .inserted = std::move(inserted),
                   .reorder = reorder});
    this->splice(std::move(one));
}

void Document::splice(std::vector<Splice> splices) {
    assert(this->open && "an edit outside of a step");
    this->apply(splices, false);
    for(Splice &s : splices) this->open->splices.push_back(std::move(s));
}

void Document::apply(std::span<const Splice> splices, bool backwards) {
    // runs of one-line removals at descending positions, where each leaves the lines before it where they were, go in
    // one pass
    const auto removal = [](const Splice &s) { return s.removed.size() == 1 && s.inserted.empty(); };
    std::vector<std::pair<uSz, uSz>> runs;  // [first, end) in the splices
    for(uSz first = 0; first < splices.size();) {
        uSz end = first + 1;
        if(removal(splices[first])) {
            while(end < splices.size() && removal(splices[end]) && splices[end].at < splices[end - 1].at) end++;
        }
        runs.emplace_back(first, end);
        first = end;
    }
    if(backwards) std::reverse(runs.begin(), runs.end());
    for(const auto &[first, end] : runs) {
        if(end - first == 1) {
            this->apply(splices[first], backwards);
            continue;
        }
        if(!backwards) {
            // the positions from the lowest, the last one in the run
            uSz write = splices[end - 1].at;
            uSz next = end;
            for(uSz read = write; read < this->lines.size(); read++) {
                if(next > first && read == splices[next - 1].at) {
                    next--;
                    continue;
                }
                this->lines[write++] = std::move(this->lines[read]);
            }
            this->lines.resize(write);
        } else {
            std::vector<Line> merged;
            merged.reserve(this->lines.size() + (end - first));
            uSz read = 0;
            for(uSz j = end; j-- > first;) {
                while(merged.size() < splices[j].at) merged.push_back(std::move(this->lines[read++]));
                merged.push_back(splices[j].removed[0]);
            }
            while(read < this->lines.size()) merged.push_back(std::move(this->lines[read++]));
            this->lines = std::move(merged);
        }
        this->indexes.clear();
        this->structureStale = true;
        this->reshaped = true;
    }
}

void Document::apply(const Splice &s, bool backwards) {
    const std::vector<Line> &out = backwards ? s.inserted : s.removed;
    const std::vector<Line> &in = backwards ? s.removed : s.inserted;
    const auto from = this->lines.begin() + (sSz)s.at;
    if(out.size() == in.size()) {
        bool moved = false;
        for(uSz i = 0; i < in.size(); i++) {
            // a line that stays as it was isn't rewritten
            if(from[(sSz)i].id == in[i].id && from[(sSz)i].text.data() == in[i].text.data()) continue;
            moved |= from[(sSz)i].id != in[i].id;
            from[(sSz)i] = in[i];
            this->rewritten.push_back(in[i].id);
        }
        if(moved && s.reorder) {
            // the same lines in another order: the others stay where they are
            if(!this->indexes.empty()) {
                for(uSz i = 0; i < in.size(); i++) this->indexes[in[i].id] = s.at + i;
            }
        } else if(moved) {
            this->indexes.clear();
            this->reshaped = true;
        }
    } else {
        this->lines.erase(from, from + (sSz)out.size());
        this->lines.insert(this->lines.begin() + (sSz)s.at, in.begin(), in.end());
        this->indexes.clear();
        this->reshaped = true;
    }
    this->structureStale = true;
}

std::vector<Document::Id> Document::touched(const Step &step, bool backwards) const {
    // the whole content replaced: no line in particular
    if(step.action == Action::REVERT || step.action == Action::RESTORE) return {};
    std::vector<Id> ids;
    Hash::flat::set<Id> seen;
    for(const Splice &s : step.splices) {
        if(s.reorder) continue;
        for(const Line &line : backwards ? s.removed : s.inserted) {
            if(this->indexOf(line.id) < this->lines.size() && seen.insert(line.id).second) ids.push_back(line.id);
        }
    }
    return ids;
}

std::optional<std::vector<Document::Id>> Document::undo() {
    assert(!this->open && "an undo within a step");
    if(!this->canUndo()) return std::nullopt;
    const Step &step = this->steps[this->steps.size() - 1 - this->undone];
    this->patched = Patch{.from = this->revision, .objects = {}, .combos = {}};
    this->apply(step.splices, true);
    this->undone++;
    // what the step started from was read before
    this->reread();
    return this->touched(step, true);
}

std::optional<std::vector<Document::Id>> Document::redo() {
    assert(!this->open && "a redo within a step");
    if(!this->canRedo()) return std::nullopt;
    const Step &step = this->steps[this->steps.size() - this->undone];
    this->patched = Patch{.from = this->revision, .objects = {}, .combos = {}};
    this->apply(step.splices, false);
    this->undone--;
    this->reread();
    return this->touched(step, false);
}

bool Document::revert(std::string_view path) {
    auto bytes = readFile(path);
    if(bytes->empty()) return false;
    const MD5Hash md5 = crypto::hash::md5(*bytes);
    if(md5 == this->fileMD5) bytes = this->fileBytes;

    const auto previousBytes = std::exchange(this->fileBytes, bytes);
    const MD5Hash previousMD5 = std::exchange(this->fileMD5, md5);
    if(Mc::ranges::equal(this->getBytes(), *bytes)) {
        this->changed = false;
        this->changedAt = this->revision;
        return true;
    }
    if(!this->change(Action::REVERT,
                     [&](Change &) { this->splice(0, this->lines.size(), this->split(textOf(*bytes), bytes)); })) {
        this->fileBytes = previousBytes;
        this->fileMD5 = previousMD5;
        return false;
    }
    return true;
}

bool Document::restore(std::vector<u8> bytes) {
    const auto owned = std::make_shared<const std::vector<u8>>(std::move(bytes));
    return this->change(Action::RESTORE,
                        [&](Change &) { this->splice(0, this->lines.size(), this->split(textOf(*owned), owned)); });
}

std::vector<u8> Document::getBytesWith(const EditorSettings &settings) {
    assert(!this->open && "a step within a step");
    // made as edits in a step, then taken back: the lines are as they were, so what was read from them stays, and so
    // does what's left to read
    const uSz unread = this->rewritten.size();
    const bool reshaped = this->reshaped;
    this->open = Step{};
    Change c{*this};
    // a value already in the file stays as written
    if(this->beatDivisor != settings.divisor) {
        c.setValue(Kind::EDITOR, "BeatDivisor", fmt::format("{}", settings.divisor));
    }
    if(this->timelineZoom != settings.zoom) {
        c.setValue(Kind::EDITOR, "TimelineZoom", fmt::format("{}", settings.zoom));
    }
    if(this->distanceSpacing != settings.spacing) {
        c.setValue(Kind::EDITOR, "DistanceSpacing", fmt::format("{}", settings.spacing));
    }
    if(this->gridSize != settings.gridSize) c.setValue(Kind::EDITOR, "GridSize", fmt::format("{}", settings.gridSize));
    std::vector<u8> out{this->getBytes().begin(), this->getBytes().end()};
    this->apply(this->open->splices, true);
    this->open.reset();
    this->rewritten.resize(unread);
    this->reshaped = reshaped;
    return out;
}

void Document::saved(std::span<const u8> written) {
    assert(!this->open && "a save within a step");
    this->fileBytes = std::make_shared<const std::vector<u8>>(this->getBytes().begin(), this->getBytes().end());
    this->fileMD5 = crypto::hash::md5(written);
    this->changed = false;
    this->changedAt = this->revision;
}

uSz Document::indexOf(Id id) const {
    if(this->indexes.empty()) {
        for(uSz i = 0; i < this->lines.size(); i++) this->indexes[this->lines[i].id] = i;
    }
    const auto it = this->indexes.find(id);
    return it != this->indexes.end() ? it->second : this->lines.size();
}

Document::Id Document::getIdAt(u32 number) const {
    return number >= 1 && number <= this->lines.size() ? this->lines[number - 1].id : 0;
}

u32 Document::getNumber(Id id) const {
    const uSz at = this->indexOf(id);
    return at < this->lines.size() ? (u32)at + 1 : 0;
}

std::optional<BeatmapFile::HitObject> Document::getObject(Id id) const {
    const uSz at = this->indexOf(id);
    if(at >= this->lines.size()) return std::nullopt;
    const std::string_view text = this->lines[at].text;
    BeatmapFile::HitObject object;
    if(!BeatmapFile::parse(text.substr(0, text.size() - lineBreak(text).size()), object)) return std::nullopt;
    return object;
}

std::string_view Document::lineBreakAt(uSz at) const {
    for(uSz i = at; i > 0; i--) {
        if(const std::string_view eol = lineBreak(this->lines[i - 1].text); !eol.empty()) return eol;
    }
    for(uSz i = at; i < this->lines.size(); i++) {
        if(const std::string_view eol = lineBreak(this->lines[i].text); !eol.empty()) return eol;
    }
    // as maps are usually written
    return CRLF;
}

std::span<const u8> Document::getBytes() const {
    this->structure();
    return {reinterpret_cast<const u8 *>(this->bytes.data()), this->bytes.size()};
}

const BeatmapFile &Document::structure() const {
    if(this->structureStale || !this->file) {
        this->bytes.clear();
        for(const Line &line : this->lines) this->bytes.append(line.text);
        this->file = std::make_unique<BeatmapFile>(std::string_view{this->bytes});
        this->structureStale = false;
    }
    return *this->file;
}

bool Document::derive() {
    const BeatmapFile &file = this->structure();

    Primitives::PRIMITIVE_CONTAINER c =
        Primitives::loadPrimitiveObjectsFromData(this->getBytes(), DatabaseBeatmap::limits());
    // a map without sliders needs no timing points to be shown
    if(!c.error && c.timingpoints.empty() && c.getNumObjects<DBType::SLIDER>() > 0) {
        c.error.errc = Primitives::LoadError::NO_TIMINGPOINTS;
    }
    if(c.error) {
        this->error = c.error;
        return false;
    }
    this->primitives = std::move(c);

    // as read by the game (Primitives::readTimingPoints)
    std::vector<std::pair<BeatmapFile::TimingPoint, Id>> points;
    BeatmapFile::TimingPoint point;
    for(const auto line : file.getEntries(Kind::TIMING_POINTS)) {
        if(BeatmapFile::parse(line.text, point) && std::abs(point.time) <= std::numeric_limits<i32>::max()) {
            points.emplace_back(point, this->getIdAt(line.number));
        }
    }
    Mc::ranges::stable_sort(points, {},
                            [](const auto &p) { return Primitives::timingOrder(p.first.time, p.first.uninherited); });
    this->timingPoints.clear();
    this->timingIds.clear();
    for(const auto &[read, id] : points) {
        this->timingPoints.push_back(read);
        this->timingIds.push_back(id);
    }
    this->beatSnap = BeatSnap{this->timingPoints};

    // and the older key
    this->bookmarks.clear();
    for(const auto &[kind, key] : {std::pair{Kind::EDITOR, "Bookmarks"}, std::pair{Kind::GENERAL, "EditorBookmarks"}}) {
        if(const auto value = file.getValue(kind, key)) {
            for(const std::string_view part : SString::split<std::string_view>(*value, ',')) {
                i32 time{0};
                if(Parsing::parse(part, &time)) this->bookmarks.push_back(time);
            }
        }
    }
    Mc::ranges::sort(this->bookmarks);
    this->bookmarks.erase(std::unique(this->bookmarks.begin(), this->bookmarks.end()), this->bookmarks.end());

    this->breaks.clear();
    this->background.clear();
    bool backgroundFound = false;
    BeatmapFile::Event event;
    for(const auto line : file.getEntries(Kind::EVENTS)) {
        if(!BeatmapFile::parse(line.text, event)) continue;
        if(event.kind == BeatmapFile::Event::Kind::BREAK) {
            this->breaks.push_back({.id = this->getIdAt(line.number), .start = event.start, .end = event.end});
        } else if(event.kind == BeatmapFile::Event::Kind::BACKGROUND && !std::exchange(backgroundFound, true)) {
            this->background = event.file;
        }
    }
    Mc::ranges::stable_sort(this->breaks, {}, &Break::start);

    this->previewTime = file.getValue<i32>(Kind::GENERAL, "PreviewTime").value_or(-1);
    this->sliderMultiplier =
        file.getValue<f64>(Kind::DIFFICULTY, "SliderMultiplier").value_or(this->primitives.sliderMultiplier);
    this->timelineZoom = file.getValue<f32>(Kind::EDITOR, "TimelineZoom");
    this->beatDivisor = file.getValue<i32>(Kind::EDITOR, "BeatDivisor");
    // else the older key
    this->distanceSpacing = file.getValue<f32>(Kind::EDITOR, "DistanceSpacing");
    if(!this->distanceSpacing) this->distanceSpacing = file.getValue<f32>(Kind::GENERAL, "EditorDistanceSpacing");
    this->gridSize = file.getValue<i32>(Kind::EDITOR, "GridSize");

    // the patch() precondition: placeObjects() moves no object line that keeps its time then
    uSz entries = 0;
    for([[maybe_unused]] const auto line : file.getEntries(Kind::HIT_OBJECTS)) entries++;
    this->objectsInOrder = entries == this->primitives.objects.size() &&
                           std::is_sorted(this->primitives.objects.begin(), this->primitives.objects.end(),
                                          [](const auto &a, const auto &b) { return spanOf(a).line < spanOf(b).line; });
    this->rewritten.clear();
    this->reshaped = false;
    this->patched.reset();
    this->revision++;
    return true;
}

bool Document::patch() {
    if(this->reshaped || !this->objectsInOrder || this->rewritten.empty()) return false;
    Mc::ranges::sort(this->rewritten);
    this->rewritten.erase(std::unique(this->rewritten.begin(), this->rewritten.end()), this->rewritten.end());

    // each line now: an object line, read again in its slot (the objects in play order are the object lines in order,
    // so the place a line has among them is the place its object has), or a break line
    Primitives::PRIMITIVE_CONTAINER &c = this->primitives;
    const auto lineOf = [](const Primitives::Object &o) { return spanOf(o).line; };
    const auto sampleFileOf = [](const Primitives::Object &o) -> u16 {
        if(const auto *h = std::get_if<DBType::HITCIRCLE>(&o)) return h->samples.filename;
        if(const auto *spinner = std::get_if<DBType::SPINNER>(&o)) return spinner->samples.filename;
        return 0;
    };
    std::vector<std::pair<uSz, Primitives::Object>> objects;
    std::vector<std::pair<Break *, BeatmapFile::Event>> events;
    for(const Id id : this->rewritten) {
        const uSz at = this->indexOf(id);
        if(at >= this->lines.size()) return false;
        const std::string_view text = this->lines[at].text;
        const std::string_view content = text.substr(0, text.size() - lineBreak(text).size());
        if(const auto slot = Mc::ranges::lower_bound(c.objects, (u32)at + 1, {}, lineOf);
           slot != c.objects.end() && lineOf(*slot) == at + 1) {
            auto object = Primitives::readObject(content, (u32)at + 1, c, DatabaseBeatmap::limits());
            // a slider needs timing points, which reading it whole says
            if(!object || (c.timingpoints.empty() && std::holds_alternative<DBType::SLIDER>(*object))) return false;
            // the sample files are numbered in the order the lines first name them: a line that names another one
            // (or none now) can change which there are and their numbers
            if(sampleFileOf(*object) != sampleFileOf(*slot)) return false;
            objects.emplace_back(slot - c.objects.begin(), std::move(*object));
        } else if(const auto b = Mc::ranges::find(this->breaks, id, &Break::id); b != this->breaks.end()) {
            BeatmapFile::Event event;
            if(!BeatmapFile::parse(content, event) || event.kind != BeatmapFile::Event::Kind::BREAK) return false;
            events.emplace_back(&*b, event);
        } else {
            return false;
        }
    }
    // in play order still: by time, equal times in line order, which the slots keep
    Mc::ranges::sort(objects, {}, &std::pair<uSz, Primitives::Object>::first);
    const auto startAt = [&](uSz k) {
        const auto read = Mc::ranges::lower_bound(objects, k, {}, &std::pair<uSz, Primitives::Object>::first);
        return spanOf(read != objects.end() && read->first == k ? read->second : c.objects[k]).start;
    };
    for(const auto &[k, object] : objects) {
        if((k > 0 && startAt(k - 1) > startAt(k)) || (k + 1 < c.objects.size() && startAt(k) > startAt(k + 1))) {
            return false;
        }
    }

    for(auto &[k, object] : objects) {
        c.objects[k] = std::move(object);
        if(this->patched) this->patched->objects.push_back(k);
    }
    if(!events.empty()) {
        for(const auto &[b, event] : events) {
            b->start = event.start;
            b->end = event.end;
        }
        // by start, equal starts in line order, as read by the game
        Mc::ranges::sort(this->breaks, {}, [&](const Break &b) { return std::pair{b.start, this->indexOf(b.id)}; });
        c.breaks.clear();
        c.totalBreakDuration = 0;
        for(const Break &b : this->breaks) {
            c.breaks.push_back({.startTime = b.start, .endTime = b.end});
            c.totalBreakDuration += (u32)(b.end - b.start);
        }
    }
    // the object order, new combos, spinners and breaks they follow from
    Primitives::countCombos(c, this->patched ? &this->patched->combos : nullptr);

    if(this->patched) {
        for(std::vector<uSz> *indexes : {&this->patched->objects, &this->patched->combos}) {
            Mc::ranges::sort(*indexes);
            indexes->erase(std::unique(indexes->begin(), indexes->end()), indexes->end());
        }
    }
    this->rewritten.clear();
    this->revision++;
    return true;
}

bool Document::reread() { return this->patch() || this->derive(); }

bool Document::isChanged() const {
    if(this->changedAt != this->revision) {
        // the lines against the file, without joining them; a line still viewing the file where it is is the same
        const std::string_view file = textOf(*this->fileBytes);
        uSz at = 0;
        this->changed = false;
        for(const Line &line : this->lines) {
            if(line.text.data() != file.data() + at && file.substr(at, line.text.size()) != line.text) {
                this->changed = true;
                break;
            }
            at += line.text.size();
        }
        this->changed = this->changed || at != file.size();
        this->changedAt = this->revision;
    }
    return this->changed;
}

bool Document::isRequired(const Break &b) const {
    // its gap, as gapIndexOf() finds it: up to the first object starting at its middle or later
    const i64 middle = std::midpoint(b.start, b.end);
    i64 latest = -OPEN;
    for(const auto &object : this->primitives.objects) {
        const Span span = spanOf(object);
        if(span.start >= middle) return latest != -OPEN && span.start - latest >= FILLED_GAP_MS;
        latest = std::max(latest, span.end);
    }
    return false;
}

void Document::Change::replace(Id id, std::string_view text) {
    Document &d = this->document;
    const uSz at = d.indexOf(id);
    assert(at < d.lines.size() && "a line that isn't there");
    std::string full{text};
    full.append(lineBreak(d.lines[at].text));
    if(full == d.lines[at].text) return;
    d.splice(at, 1, {d.makeLine(id, std::move(full))});
}

Document::Id Document::Change::insert(uSz at, std::string_view text) {
    const std::string line{text};
    return this->insert(at, std::span{&line, 1}).front();
}

std::vector<Document::Id> Document::Change::insert(uSz at, std::span<const std::string> texts) {
    Document &d = this->document;
    if(texts.empty()) return {};
    const std::string_view eol = d.lineBreakAt(at);
    // after a last line without a line break: that one gets one, the last new one is the last without
    const bool last = at > 0 && at == d.lines.size() && lineBreak(d.lines[at - 1].text).empty();
    if(last) d.splice(at - 1, 1, {d.makeLine(d.lines[at - 1].id, std::string{d.lines[at - 1].text}.append(eol))});
    std::vector<Id> ids;
    std::vector<Line> lines;
    for(uSz i = 0; i < texts.size(); i++) {
        ids.push_back(d.nextId++);
        lines.push_back(d.makeLine(ids.back(), last && i + 1 == texts.size() ? texts[i] : texts[i] + std::string{eol}));
    }
    d.splice(at, 0, std::move(lines));
    return ids;
}

void Document::Change::remove(Id id) { this->remove(std::span{&id, 1}); }

void Document::Change::remove(std::span<const Id> ids) {
    Document &d = this->document;
    std::vector<uSz> at;
    at.reserve(ids.size());
    for(const Id id : ids) {
        at.push_back(d.indexOf(id));
        assert(at.back() < d.lines.size() && "a line that isn't there");
    }
    // the last first: each leaves the lines before it where they were (and so they go in one pass)
    Mc::ranges::sort(at, std::greater<>{});
    at.erase(std::unique(at.begin(), at.end()), at.end());
    std::vector<Splice> splices;
    splices.reserve(at.size());
    for(const uSz i : at) splices.push_back({.at = i, .removed = {d.lines[i]}, .inserted = {}, .reorder = false});
    d.splice(std::move(splices));
}

void Document::Change::setValue(Kind kind, std::string_view key, std::optional<std::string_view> value) {
    Document &d = this->document;
    std::vector<u32> found;
    {
        BeatmapFile::KeyValue kv;
        for(const auto line : d.structure().getEntries(kind)) {
            if(BeatmapFile::parse(line.text, kv) && kv.key == key) found.push_back(line.number);
        }
    }
    if(!value) {
        std::vector<Id> ids;
        ids.reserve(found.size());
        for(const u32 number : found) ids.push_back(d.getIdAt(number));
        this->remove(ids);
        return;
    }

    const std::string text = BeatmapFile::format(kind, {.key = key, .value = *value});
    if(!found.empty()) {
        this->replace(d.getIdAt(found.back()), text);
        return;
    }
    this->insert(this->newEntryAt(kind), text);
}

void Document::Change::setObject(Id id, const BeatmapFile::HitObject &object) {
    this->replace(id, BeatmapFile::format(object));
}

Document::Id Document::Change::addObject(const BeatmapFile::HitObject &object) {
    return this->addObjects(std::span{&object, 1}).front();
}

std::vector<Document::Id> Document::Change::addObjects(std::span<const BeatmapFile::HitObject> objects) {
    if(objects.empty()) return {};
    std::vector<std::string> texts;
    texts.reserve(objects.size());
    for(const BeatmapFile::HitObject &object : objects) texts.push_back(BeatmapFile::format(object));
    // the step end moves them to their places in time order
    return this->insert(this->newEntryAt(Kind::HIT_OBJECTS), texts);
}

void Document::Change::setTimingPoint(Id id, const BeatmapFile::TimingPoint &point) {
    this->document.timingWritten = true;
    this->replace(id, BeatmapFile::format(point));
}

Document::Id Document::Change::addTimingPoint(const BeatmapFile::TimingPoint &point) {
    this->document.timingWritten = true;
    // the step end moves it to its place in time order
    return this->insert(this->newEntryAt(Kind::TIMING_POINTS), BeatmapFile::format(point));
}

bool Document::Change::moveBreakEdge(Id id, bool end, i64 timeMS) {
    Document &d = this->document;
    const auto it = Mc::ranges::find(d.breaks, id, &Break::id);
    if(it == d.breaks.end()) return false;
    const std::vector<Gap> gaps = gapsOf(spansOf(d.primitives));
    const Gap &gap = gaps[gapIndexOf(gaps, *it)];
    if(gap.isOpen()) return false;
    const Break usual = defaultBreak(gap, preemptOf(d.primitives.AR));
    Break b = *it;
    if(end) {
        const i64 earliest = std::max(gap.to - MAX_EDGE_MS, b.start + MIN_BREAK_MS);
        if(earliest > usual.end) return false;
        b.end = std::clamp(timeMS, earliest, usual.end);
    } else {
        const i64 latest = std::min(gap.from + MAX_EDGE_MS, b.end - MIN_BREAK_MS);
        if(latest < usual.start) return false;
        b.start = std::clamp(timeMS, usual.start, latest);
    }
    this->replace(id, formatBreak(b.start, b.end));
    return true;
}

Document::Id Document::Change::insertBreak(i64 timeMS) {
    Document &d = this->document;
    const std::vector<Gap> gaps = gapsOf(spansOf(d.primitives));
    const uSz k = Mc::ranges::lower_bound(gaps, timeMS, {}, &Gap::to) - gaps.begin();
    const Gap &gap = gaps[k];
    if(gap.isOpen() || timeMS < gap.from || gap.to - gap.from < MIN_GAP_MS) return 0;
    const Break usual = defaultBreak(gap, preemptOf(d.primitives.AR));
    if(usual.end - usual.start < MIN_BREAK_MS) return 0;
    if(Mc::ranges::any_of(d.breaks, [&](const Break &b) { return gapIndexOf(gaps, b) == k; })) return 0;
    return this->addBreak(usual.start, usual.end);
}

void Document::Change::resetBreaks() {
    Document &d = this->document;
    const std::vector<Gap> gaps = gapsOf(spansOf(d.primitives));
    const i64 preempt = preemptOf(d.primitives.AR);
    const auto filled = [](const Gap &gap) { return !gap.isOpen() && gap.to - gap.from >= FILLED_GAP_MS; };
    std::vector<u8> held(gaps.size());
    for(const Break &b : d.breaks) {
        const uSz k = gapIndexOf(gaps, b);
        if(held[k] || !filled(gaps[k])) {
            this->remove(b.id);
            continue;
        }
        held[k] = true;
        const Break usual = defaultBreak(gaps[k], preempt);
        this->replace(b.id, formatBreak(usual.start, usual.end));
    }
    for(uSz k = 0; k < gaps.size(); k++) {
        if(held[k] || !filled(gaps[k])) continue;
        const Break usual = defaultBreak(gaps[k], preempt);
        this->addBreak(usual.start, usual.end);
    }
}

Document::Id Document::Change::addBreak(i64 start, i64 end) {
    Document &d = this->document;
    const std::string text = formatBreak(start, end);
    // in time order among the breaks there are, those this step added included, else where breaks usually are: under
    // "//Break Periods", after the background and video
    const Break *before = nullptr;
    const Break *after = nullptr;
    const auto place = [&](const Break &b) {
        if(b.start <= start) {
            if(!before || b.start >= before->start) before = &b;
        } else if(!after || b.start < after->start) {
            after = &b;
        }
    };
    for(const Break &b : d.breaks) {
        if(d.indexOf(b.id) < d.lines.size()) place(b);  // not taken out in this step
    }
    for(const Break &b : this->addedBreaks) place(b);
    const auto added = [&](Id id) {
        this->addedBreaks.push_back({.id = id, .start = start, .end = end});
        return id;
    };
    if(before) return added(this->insert(d.indexOf(before->id) + 1, text));
    if(after) return added(this->insert(d.indexOf(after->id), text));

    std::optional<uSz> at;
    for(const auto &section : d.structure().getSections()) {
        if(section.readAs != Kind::EVENTS || section.header.empty()) continue;
        if(!at) at = section.bodyLine - 1;
        BeatmapFile::Event event;
        for(u32 number = section.bodyLine; number < section.bodyLine + section.lines; number++) {
            const std::string_view line = d.lines[number - 1].text;
            const std::string_view content = line.substr(0, line.size() - lineBreak(line).size());
            if(content == "//Break Periods" ||
               (BeatmapFile::parse(content, event) && event.kind != BeatmapFile::Event::Kind::BREAK)) {
                at = number;
            }
        }
    }
    return added(this->insert(at ? *at : this->newEntryAt(Kind::EVENTS), text));
}

void Document::Change::setBackground(std::string_view file) {
    Document &d = this->document;
    const std::string text = BeatmapFile::format(BeatmapFile::Event{
        .kind = BeatmapFile::Event::Kind::BACKGROUND, .start = 0, .end = 0, .file = file, .rest = ",0,0"});
    // a new one goes first in the events, under the comment osu! puts there when there is one
    std::optional<uSz> at;
    for(const auto &section : d.structure().getSections()) {
        if(section.readAs != Kind::EVENTS || section.header.empty()) continue;
        if(!at) at = section.bodyLine - 1;
        BeatmapFile::Event event;
        for(u32 number = section.bodyLine; number < section.bodyLine + section.lines; number++) {
            const std::string_view line = d.lines[number - 1].text;
            const std::string_view content = line.substr(0, line.size() - lineBreak(line).size());
            if(BeatmapFile::parse(content, event) && event.kind == BeatmapFile::Event::Kind::BACKGROUND) {
                this->replace(d.lines[number - 1].id, text);
                return;
            }
            if(content == "//Background and Video events") at = number;
        }
    }
    this->insert(at ? *at : this->newEntryAt(Kind::EVENTS), text);
}

uSz Document::Change::newEntryAt(Kind kind) {
    Document &d = this->document;
    u32 lastEntry{0};
    for(const auto line : d.structure().getEntries(kind)) lastEntry = line.number;
    if(lastEntry > 0) return lastEntry;

    // after the header for the last section read as `kind`
    const auto sections = d.structure().getSections();
    for(auto it = sections.rbegin(); it != sections.rend(); ++it) {
        if(it->readAs == kind && !it->header.empty()) return it->bodyLine - 1;
    }

    // a new section, before the first one that usually comes after it, with a blank line after it
    const auto order = Mc::ranges::find(SECTION_ORDER, kind);
    for(const auto &section : sections) {
        if(section.header.empty()) continue;
        if(const auto other = Mc::ranges::find(SECTION_ORDER, section.kind);
           other != SECTION_ORDER.end() && other > order) {
            const uSz at = section.bodyLine - 2;
            this->insert(at, fmt::format("[{}]", BeatmapFile::sectionName(kind)));
            this->insert(at + 1, "");
            return at + 1;
        }
    }
    // at the end, after a blank line
    if(!d.lines.empty() && !isBlank(d.lines.back().text)) this->insert(d.lines.size(), "");
    this->insert(d.lines.size(), fmt::format("[{}]", BeatmapFile::sectionName(kind)));
    return d.lines.size();
}
