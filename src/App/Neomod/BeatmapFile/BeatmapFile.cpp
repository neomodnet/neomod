// Copyright (c) 2026, WH, All rights reserved.
#include "BeatmapFile.h"

#include "Parsing.h"
#include "SString.h"
#include "Vectors.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>

// ignored for performance
// NOLINTBEGIN(cppcoreguidelines-init-variables,cppcoreguidelines-pro-type-member-init)

namespace neomod {

namespace {

using enum BeatmapFile::SectionKind;

constexpr std::array<std::pair<std::string_view, BeatmapFile::SectionKind>, 8> KNOWN_SECTIONS{{
    {"General", GENERAL},
    {"Editor", EDITOR},
    {"Metadata", METADATA},
    {"Difficulty", DIFFICULTY},
    {"Events", EVENTS},
    {"TimingPoints", TIMING_POINTS},
    {"Colours", COLOURS},
    {"HitObjects", HIT_OBJECTS},
}};

constexpr std::string_view UTF8_BOM{"\xEF\xBB\xBF"};

// the first line of s with its line break: "\r\n", "\n" or a "\r" of its own, as osu! reads them
std::string_view firstLine(std::string_view s) {
    const uSz lineFeed = s.find('\n');
    const uSz end = lineFeed == std::string_view::npos ? s.size() : lineFeed;
    if(const uSz cr = s.substr(0, end).find('\r'); cr != std::string_view::npos && cr + 1 < end) {
        return s.substr(0, cr + 1);
    }
    return s.substr(0, lineFeed == std::string_view::npos ? s.size() : lineFeed + 1);
}

// a line without its line break
std::string_view lineText(std::string_view line) {
    if(line.ends_with('\n')) line.remove_suffix(1);
    if(line.ends_with('\r')) line.remove_suffix(1);
    return line;
}

std::string_view trimmed(std::string_view s) {
    SString::trim_inplace(s);
    return s;
}

// splits like SString::split (a trailing empty field doesn't count) into the first N fields; returns how many there
// are in all
template <uSz N>
uSz splitFields(std::string_view s, char delim, std::array<std::string_view, N> &out) {
    uSz count = 0;
    uSz start = 0;
    for(uSz i = 0; i < s.size(); i++) {
        if(s[i] != delim) continue;
        if(count < N) out[count] = s.substr(start, i - start);
        count++;
        start = i + 1;
    }
    if(start < s.size()) {
        if(count < N) out[count] = s.substr(start);
        count++;
    }
    return count;
}

// calls fn for each field, the way SString::split splits
template <typename F>
void forEachField(std::string_view s, char delim, const F &&fn) {
    uSz start = 0;
    for(uSz i = 0; i < s.size(); i++) {
        if(s[i] != delim) continue;
        fn(s.substr(start, i - start));
        start = i + 1;
    }
    if(start < s.size()) fn(s.substr(start));
}

void parseHitSample(std::string_view field, BeatmapFile::HitSample &out) {
    out = {};
    std::array<std::string_view, 5> parts;
    const uSz count = splitFields(field, ':', parts);
    if(count >= 1) out.normalSet = Parsing::strto<i32>(parts[0]);
    if(count >= 2) out.additionSet = Parsing::strto<i32>(parts[1]);
    if(count >= 3) out.index = Parsing::strto<i32>(parts[2]);
    if(count >= 4) out.volume = Parsing::strto<i32>(parts[3]);
    if(count >= 5) out.filename = parts[4];
    // (an empty filename after the last ':' counts as written)
    out.parts = static_cast<u8>(std::min<uSz>(std::ranges::count(field, ':') + 1, std::numeric_limits<u8>::max()));
}

void appendInt(std::string &out, i64 value) {
    std::array<char, 24> buf;
    const auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), value);
    out.append(buf.data(), end);
}

void upperExponent(char *begin, char *end) {
    for(char *c = begin; c != end; c++) {
        if(*c == 'e') *c = 'E';
    }
}

// a double the way osu!stable writes one (.NET's "G": 15 significant digits), with more digits only where 15 don't give
// back the same number (osu!lazer writes the shortest that does)
void appendDouble(std::string &out, f64 value) {
    if(std::isnan(value)) {
        out.append("NaN");
        return;
    }
    if(std::isinf(value)) {
        out.append(value < 0 ? "-Infinity" : "Infinity");
        return;
    }
    std::array<char, 40> buf;
    char *end = buf.data();
    for(int precision = 15; precision <= 17; precision++) {
        end = std::to_chars(buf.data(), buf.data() + buf.size(), value, std::chars_format::general, precision).ptr;
        if(f64 back; Parsing::from_chars(buf.data(), end, back).ec == std::errc() && back == value) break;
    }
    upperExponent(buf.data(), end);
    out.append(buf.data(), end);
}

// a position: whole numbers as osu!stable writes them, fractions (osu!lazer) as the shortest text that gives them back
void appendCoordinate(std::string &out, f32 value) {
    if(value == std::trunc(value) && std::abs(value) < 2147483648.f) {
        appendInt(out, static_cast<i64>(value));
        return;
    }
    std::array<char, 40> buf;
    char *end = std::to_chars(buf.data(), buf.data() + buf.size(), value, std::chars_format::general).ptr;
    upperExponent(buf.data(), end);
    out.append(buf.data(), end);
}

void appendHitSample(std::string &out, const BeatmapFile::HitSample &sample) {
    const std::array<i32, 4> numbers{sample.normalSet, sample.additionSet, sample.index, sample.volume};
    for(uSz i = 0; i < numbers.size() && i < sample.parts; i++) {
        out.push_back(i == 0 ? ',' : ':');
        appendInt(out, numbers[i]);
    }
    if(sample.parts >= 5) {
        out.push_back(':');
        out.append(sample.filename);
    }
}

}  // namespace

BeatmapFile::BeatmapFile(std::string_view bytes) : bytes(bytes), bom(bytes.starts_with(UTF8_BOM)) {
    Section current{.header = {}, .body = {}, .name = {}, .kind = NONE, .readAs = NONE, .bodyLine = 1};
    uSz bodyStart = this->bom ? UTF8_BOM.size() : 0;
    u32 number = 1;
    for(uSz pos = bodyStart; pos < bytes.size(); number++) {
        const std::string_view raw = firstLine(bytes.substr(pos));
        const uSz next = pos + raw.size();
        const std::string_view line = lineText(raw);
        if(line.size() >= 2 && line.front() == '[' && line.back() == ']') {
            current.body = bytes.substr(bodyStart, pos - bodyStart);
            this->sections.push_back(current);

            const std::string_view name = line.substr(1, line.size() - 2);
            const auto known = std::ranges::find(KNOWN_SECTIONS, name, &decltype(KNOWN_SECTIONS)::value_type::first);
            const SectionKind kind = known != KNOWN_SECTIONS.end() ? known->second : UNKNOWN;
            current = {.header = bytes.substr(pos, next - pos),
                       .body = {},
                       .name = name,
                       .kind = kind,
                       .readAs = kind == UNKNOWN ? current.readAs : kind,
                       .bodyLine = number + 1};
            bodyStart = next;
        }
        pos = next;
    }
    current.body = bytes.substr(bodyStart);
    this->sections.push_back(current);
}

BeatmapFile::Entries::Iterator::Iterator(std::span<const Section> sections, SectionKind kind)
    : sections(sections), kind(kind), done(false) {
    ++*this;
}

BeatmapFile::Entries::Iterator &BeatmapFile::Entries::Iterator::operator++() {
    for(;;) {
        if(this->rest.empty()) {
            while(!this->sections.empty() && this->sections.front().readAs != this->kind) {
                this->sections = this->sections.subspan(1);
            }
            if(this->sections.empty()) {
                this->done = true;
                return *this;
            }
            this->rest = this->sections.front().body;
            this->nextNumber = this->sections.front().bodyLine;
            this->sections = this->sections.subspan(1);
            continue;
        }

        const std::string_view line = firstLine(this->rest);
        this->rest.remove_prefix(line.size());
        const u32 number = this->nextNumber++;

        // a "//" comment only at the start of a line (Artist:DJ'TEKINA//SOMETHING is a value)
        const std::string_view text = lineText(line);
        if(text.empty() || SString::is_comment(text)) continue;

        this->line = {.text = text, .number = number};
        return *this;
    }
}

std::optional<i32> BeatmapFile::getVersion() const {
    for(const Line line : Entries{std::span{this->sections}.first(1), NONE}) {
        if(i32 version; Parsing::parse(line.text, "osu file format v", &version)) return version;
    }
    return std::nullopt;
}

std::optional<std::string_view> BeatmapFile::getValue(SectionKind kind, std::string_view key) const {
    std::optional<std::string_view> value;
    for(const Line line : this->getEntries(kind)) {
        if(KeyValue kv; parse(line.text, kv) && kv.key == key) value = kv.value;
    }
    return value;
}

bool BeatmapFile::parse(std::string_view line, KeyValue &out) {
    const uSz colon = line.find(':');
    if(colon == std::string_view::npos) return false;
    out = {.key = trimmed(line.substr(0, colon)), .value = trimmed(line.substr(colon + 1))};
    return true;
}

std::string BeatmapFile::format(SectionKind kind, const KeyValue &kv) {
    std::string out{kv.key};
    out.append(kind == GENERAL || kind == EDITOR ? ": " : kind == COLOURS ? " : " : ":");
    out.append(kv.value);
    return out;
}

bool BeatmapFile::parse(std::string_view line, TimingPoint &out) {
    std::array<std::string_view, 8> fields;
    const uSz count = splitFields(line, ',', fields);

    // fields are read up to the first one that isn't a number
    TimingPoint tp;
    i32 uninherited{1};
    uSz read = 0;
    // NOLINTNEXTLINE(bugprone-inc-dec-in-conditions)
    const auto next = [&](auto &value) { return read < count && Parsing::parse(fields[read++], &value); };
    if(!next(tp.time) || !next(tp.beatLength)) return false;
    (void)(next(tp.meter) && next(tp.sampleSet) && next(tp.sampleIndex) && next(tp.volume) && next(uninherited) &&
           next(tp.effects));
    tp.uninherited = uninherited == 1;
    out = tp;
    return true;
}

std::string BeatmapFile::format(const TimingPoint &tp) {
    std::string out;
    appendDouble(out, tp.time);
    out.push_back(',');
    appendDouble(out, tp.beatLength);
    for(const i32 value : {tp.meter, tp.sampleSet, tp.sampleIndex, tp.volume, tp.uninherited ? 1 : 0, tp.effects}) {
        out.push_back(',');
        appendInt(out, value);
    }
    return out;
}

bool BeatmapFile::parse(std::string_view line, HitObject &out) {
    using T = HitObject;

    std::array<std::string_view, 11> fields;
    const uSz count = splitFields(line, ',', fields);
    if(count < 5) return false;

    f32 x, y;
    i32 time, hitSounds;
    u8 type;
    if(!Parsing::parse(fields[0], &x) || !std::isfinite(x) || !Parsing::parse(fields[1], &y) || !std::isfinite(y) ||
       !Parsing::parse(fields[2], &time) || !Parsing::parse(fields[3], &type) ||
       !Parsing::parse(fields[4], &hitSounds)) {
        return false;
    }
    if((type & T::TYPE_SLIDER) && count < 8) return false;
    if((type & T::TYPE_SPINNER) && count < 6) return false;
    if(type & T::TYPE_MANIA_HOLD) return false;

    out.x = x;
    out.y = y;
    out.time = time;
    out.type = type;
    out.hitSounds = hitSounds;
    out.kind = (type & T::TYPE_CIRCLE)    ? T::Kind::CIRCLE
               : (type & T::TYPE_SLIDER)  ? T::Kind::SLIDER
               : (type & T::TYPE_SPINNER) ? T::Kind::SPINNER
                                          : T::Kind::NONE;
    out.sample = {};
    out.sample.parts = 0;
    out.curveType = 0;
    out.curvePoints.clear();
    out.slides.reset();
    out.length.reset();
    out.edgeSounds.clear();
    out.edgeSets.clear();
    out.endTime.reset();

    switch(out.kind) {
        case T::Kind::NONE:
            break;

        case T::Kind::CIRCLE:
            if(count > 5) parseHitSample(fields[5], out.sample);
            break;

        case T::Kind::SLIDER: {
            // the curve type's letter, then x:y control points
            const std::string_view curve = fields[5];
            const uSz firstBar = curve.find('|');
            out.curveType = curve.empty() || firstBar == 0 ? '\0' : curve.front();
            if(firstBar != std::string_view::npos) {
                forEachField(curve.substr(firstBar + 1), '|', [&out](std::string_view part) {
                    if(f32 px, py; Parsing::parse(part, &px, ':', &py) && std::isfinite(px) && std::isfinite(py)) {
                        out.curvePoints.emplace_back(px, py);
                    }
                });
            }

            if(i32 slides; Parsing::parse(fields[6], &slides)) out.slides = slides;

            if(f64 length; Parsing::parse(fields[7], &length)) {
                out.length = length;
            } else if(SString::contains_ncase(fields[7], "e+")) {
                // too large for a double, but a length nonetheless
                out.length = fields[7].starts_with('-') ? -std::numeric_limits<f64>::infinity()
                                                        : std::numeric_limits<f64>::infinity();
            }

            if(count > 8) {
                forEachField(fields[8], '|', [&out](std::string_view part) {
                    u8 sounds{0};
                    (void)Parsing::parse(part, &sounds);
                    out.edgeSounds.push_back(sounds);
                });
            }
            if(count > 9) {
                forEachField(fields[9], '|', [&out](std::string_view part) {
                    std::array<std::string_view, 2> sets;
                    const uSz setCount = splitFields(part, ':', sets);
                    out.edgeSets.push_back({.normalSet = setCount >= 1 ? Parsing::strto<i32>(sets[0]) : 0,
                                            .additionSet = setCount >= 2 ? Parsing::strto<i32>(sets[1]) : 0});
                });
            }
            if(count > 10) parseHitSample(fields[10], out.sample);
            break;
        }

        case T::Kind::SPINNER:
            if(i32 endTime; Parsing::parse(fields[5], &endTime)) out.endTime = endTime;
            if(count > 6) parseHitSample(fields[6], out.sample);
            break;
    }
    return true;
}

std::string BeatmapFile::format(const HitObject &ho) {
    using T = HitObject;

    std::string out;
    appendCoordinate(out, ho.x);
    out.push_back(',');
    appendCoordinate(out, ho.y);
    out.push_back(',');
    appendInt(out, ho.time);
    out.push_back(',');
    appendInt(out, ho.type);
    out.push_back(',');
    appendInt(out, ho.hitSounds);

    switch(ho.kind) {
        case T::Kind::NONE:
            break;

        case T::Kind::CIRCLE:
            appendHitSample(out, ho.sample);
            break;

        case T::Kind::SLIDER: {
            out.push_back(',');
            if(ho.curveType != '\0') out.push_back(ho.curveType);
            for(const vec2 &point : ho.curvePoints) {
                out.push_back('|');
                appendCoordinate(out, point.x);
                out.push_back(':');
                appendCoordinate(out, point.y);
            }
            out.push_back(',');
            appendInt(out, ho.slides.value_or(1));
            out.push_back(',');
            appendDouble(out, ho.length.value_or(0.0));

            // osu!stable writes the edges and the sample together, older files stop after any of them
            const bool sets = !ho.edgeSets.empty() || ho.sample.parts > 0;
            if(sets || !ho.edgeSounds.empty()) {
                out.push_back(',');
                for(uSz i = 0; i < ho.edgeSounds.size(); i++) {
                    if(i > 0) out.push_back('|');
                    appendInt(out, ho.edgeSounds[i]);
                }
            }
            if(sets) {
                out.push_back(',');
                for(uSz i = 0; i < ho.edgeSets.size(); i++) {
                    if(i > 0) out.push_back('|');
                    appendInt(out, ho.edgeSets[i].normalSet);
                    out.push_back(':');
                    appendInt(out, ho.edgeSets[i].additionSet);
                }
            }
            appendHitSample(out, ho.sample);
            break;
        }

        case T::Kind::SPINNER:
            out.push_back(',');
            appendInt(out, ho.endTime.value_or(ho.time));
            appendHitSample(out, ho.sample);
            break;
    }
    return out;
}

bool BeatmapFile::parse(std::string_view line, Event &out) {
    const uSz firstComma = line.find(',');
    if(firstComma == std::string_view::npos) return false;
    const uSz secondComma = line.find(',', firstComma + 1);

    const std::string_view typeField = line.substr(0, firstComma);
    const std::string_view startField = line.substr(
        firstComma + 1, secondComma == std::string_view::npos ? std::string_view::npos : secondComma - firstComma - 1);
    const std::string_view rest =
        secondComma == std::string_view::npos ? std::string_view{} : line.substr(secondComma + 1);

    Event ev;
    if(i64 type; Parsing::parse(typeField, &type)) {
        switch(type) {
            case 0:
                ev.kind = Event::Kind::BACKGROUND;
                break;
            case 1:
                ev.kind = Event::Kind::VIDEO;
                break;
            case 2:
                ev.kind = Event::Kind::BREAK;
                break;
            default:
                return false;
        }
    } else if(trimmed(typeField) == "Video") {
        ev.kind = Event::Kind::VIDEO;
    } else {
        return false;
    }
    if(secondComma == std::string_view::npos || !Parsing::parse(startField, &ev.start)) return false;

    if(ev.kind == Event::Kind::BREAK) {
        if(!Parsing::parse(rest, &ev.end)) return false;
        out = ev;
        return true;
    }

    // the file: between quotes, or unquoted all that follows
    std::string_view file = rest;
    while(!file.empty() && (file.front() == ' ' || file.front() == '\t')) file.remove_prefix(1);
    if(file.starts_with('"')) {
        const uSz closingQuote = file.find('"', 1);
        if(closingQuote == std::string_view::npos) return false;
        ev.rest = file.substr(closingQuote + 1);
        file = file.substr(1, closingQuote - 1);
    } else {
        file = trimmed(file);
    }
    ev.file = file;
    out = ev;
    return true;
}

std::string BeatmapFile::format(const Event &ev) {
    std::string out;
    switch(ev.kind) {
        case Event::Kind::BACKGROUND:
            out.append("0,");
            break;
        case Event::Kind::VIDEO:
            out.append("Video,");
            break;
        case Event::Kind::BREAK:
            out.append("2,");
            appendInt(out, ev.start);
            out.push_back(',');
            appendInt(out, ev.end);
            return out;
    }
    appendInt(out, ev.start);
    out.append(",\"");
    out.append(ev.file);
    out.push_back('"');
    out.append(ev.rest);
    return out;
}

bool BeatmapFile::parse(std::string_view line, Colour &out) {
    KeyValue kv;
    if(!parse(line, kv)) return false;
    std::array<std::string_view, 3> components;
    if(splitFields(kv.value, ',', components) < 3) return false;
    Colour colour{.name = kv.key};
    if(!Parsing::parse(components[0], &colour.r) || !Parsing::parse(components[1], &colour.g) ||
       !Parsing::parse(components[2], &colour.b)) {
        return false;
    }
    out = colour;
    return true;
}

std::string BeatmapFile::format(const Colour &colour) {
    std::string out{colour.name};
    out.append(" : ");
    appendInt(out, colour.r);
    out.push_back(',');
    appendInt(out, colour.g);
    out.push_back(',');
    appendInt(out, colour.b);
    return out;
}

}  // namespace neomod

// NOLINTEND(cppcoreguidelines-init-variables,cppcoreguidelines-pro-type-member-init)
