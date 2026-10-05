#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "types.h"
#include "Vectors_fwd.h"

#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace neomod {

// a .osu file over its bytes, which the caller keeps alive as long as the file and the records read from it: the
// sections its lines form, and records for the lines that say something. any bytes are a file, and nothing is lost:
// the sections cover every byte, so a file is written back by keeping the lines nobody changed and formatting the rest
class BeatmapFile {
   public:
    // a section by its name in brackets; NONE for the lines before the first section
    enum class SectionKind : u8 {
        NONE,
        GENERAL,
        EDITOR,
        METADATA,
        DIFFICULTY,
        EVENTS,
        TIMING_POINTS,
        COLOURS,
        HIT_OBJECTS,
        UNKNOWN,
    };

    // a line that starts with '[' and ends with ']', and the lines up to the next one
    struct Section {
        // the "[Name]" line with its line break (empty for the lines before the first section)
        std::string_view header;
        std::string_view body;  // the lines after it, with their line breaks
        std::string_view name;  // between the brackets
        SectionKind kind;
        // what its lines count as: its kind, or for an UNKNOWN name the section before it (as in osu!stable)
        SectionKind readAs;
        u32 bodyLine;  // the number of the first line after the header (1-based)
    };

    struct Line {
        std::string_view text;  // without its line break
        u32 number;             // 1-based
    };

    // the lines with something in them (neither blank nor a "//" comment) of the sections read as one kind, in file order
    class Entries {
       public:
        class Iterator {
           public:
            using value_type = Line;
            using difference_type = std::ptrdiff_t;

            Iterator() = default;
            Iterator(std::span<const Section> sections, SectionKind kind);

            Line operator*() const { return this->line; }
            Iterator &operator++();
            void operator++(int) { ++*this; }
            bool operator==(std::default_sentinel_t) const { return this->done; }

           private:
            std::span<const Section> sections;  // from the one being read on
            std::string_view rest;              // what's left of its body
            Line line{};
            u32 nextNumber{0};
            SectionKind kind{SectionKind::NONE};
            bool done{true};
        };

        Entries(std::span<const Section> sections, SectionKind kind) : sections(sections), kind(kind) {}
        [[nodiscard]] Iterator begin() const { return {this->sections, this->kind}; }
        [[nodiscard]] std::default_sentinel_t end() const { return {}; }

       private:
        std::span<const Section> sections;
        SectionKind kind;
    };

    explicit BeatmapFile(std::string_view bytes);
    explicit BeatmapFile(std::span<const u8> bytes)
        : BeatmapFile(std::string_view{reinterpret_cast<const char *>(bytes.data()), bytes.size()}) {}

    [[nodiscard]] std::string_view getBytes() const { return this->bytes; }
    // whether the bytes start with a UTF-8 byte order mark, which the sections leave out
    [[nodiscard]] bool hasBom() const { return this->bom; }
    // in file order, starting with the lines before the first section (possibly none); they cover every byte after the
    // byte order mark
    [[nodiscard]] std::span<const Section> getSections() const { return this->sections; }
    [[nodiscard]] Entries getEntries(SectionKind kind) const { return {this->sections, kind}; }

    // N from the "osu file format vN" line before the first section
    [[nodiscard]] std::optional<i32> getVersion() const;
    // the value of the last "key: value" line with this key in the sections read as `kind`
    [[nodiscard]] std::optional<std::string_view> getValue(SectionKind kind, std::string_view key) const;

    // records: parse() reads one from an entry (false if the entry isn't one), format() writes one the way osu!stable
    // does. string views in a record point into the line it was read from

    // a "key: value" line of [General], [Editor], [Metadata], [Difficulty] or [Colours], both sides trimmed
    struct KeyValue {
        std::string_view key;
        std::string_view value;
    };
    static bool parse(std::string_view line, KeyValue &out);
    // osu!stable's spacing around the ':' differs per section
    static std::string format(SectionKind kind, const KeyValue &kv);

    // a [TimingPoints] line: time,beatLength,meter,sampleSet,sampleIndex,volume,uninherited,effects. older files stop
    // after any field from the second on, and the fields left out keep their defaults
    struct TimingPoint {
        static constexpr i32 EFFECT_KIAI{1 << 0};
        static constexpr i32 EFFECT_OMIT_FIRST_BARLINE{1 << 3};

        f64 time{0.0};
        f64 beatLength{0.0};  // ms per beat, or for an inherited point -100 divided by its slider velocity
        i32 meter{4};
        i32 sampleSet{0};
        i32 sampleIndex{0};
        i32 volume{100};
        bool uninherited{true};
        i32 effects{0};
    };
    static bool parse(std::string_view line, TimingPoint &out);
    static std::string format(const TimingPoint &tp);

    // the last field of a hit object: normalSet:additionSet:index:volume:filename, of which older files leave out
    // any number from the end
    struct HitSample {
        i32 normalSet{0};
        i32 additionSet{0};
        i32 index{0};
        i32 volume{0};
        std::string_view filename;
        u8 parts{5};  // how many were written (osu!stable writes all five, a line may have none)
    };

    // a [HitObjects] line: x,y,time,type,hitSounds, then for sliders curve,slides,length,edgeSounds,edgeSets, for
    // spinners endTime, then the hit sample
    struct HitObject {
        enum class Kind : u8 { NONE, CIRCLE, SLIDER, SPINNER };

        static constexpr u8 TYPE_CIRCLE{1 << 0};
        static constexpr u8 TYPE_SLIDER{1 << 1};
        static constexpr u8 TYPE_NEW_COMBO{1 << 2};
        static constexpr u8 TYPE_SPINNER{1 << 3};
        static constexpr u8 TYPE_COLOUR_SKIP_SHIFT{4};  // 3 bits: how many combo colours a new combo skips
        static constexpr u8 TYPE_MANIA_HOLD{1 << 7};

        struct EdgeSet {
            i32 normalSet{0};
            i32 additionSet{0};
        };

        f32 x{0.f};  // as written, fractions included
        f32 y{0.f};
        i32 time{0};
        u8 type{0};
        i32 hitSounds{0};
        Kind kind{Kind::NONE};  // from the type: a circle wins over a slider, a slider over a spinner
        HitSample sample;

        // sliders
        char curveType{0};
        std::vector<vec2> curvePoints;  // after the start, as written, without the ones that aren't two finite numbers
        std::optional<i32> slides;
        std::optional<f64> length;  // +-infinity for one written with an exponent too large for a double
        std::vector<u8> edgeSounds;
        std::vector<EdgeSet> edgeSets;

        // spinners
        std::optional<i32> endTime;
    };
    // false for a line no object comes from: too few fields for its kind, a value the first five fields can't hold,
    // or an osu!mania hold. a kind's own fields may still be missing (slides, length, endTime); parsing into a
    // record that's reused keeps the capacity of its vectors
    static bool parse(std::string_view line, HitObject &out);
    static std::string format(const HitObject &ho);

    // an [Events] line that isn't storyboard: the background (0,start,"file",x,y), a video (1 or Video,start,"file")
    // or a break (2,start,end)
    struct Event {
        enum class Kind : u8 { BACKGROUND, VIDEO, BREAK };
        Kind kind{Kind::BACKGROUND};
        i64 start{0};
        i64 end{0};             // breaks
        std::string_view file;  // backgrounds and videos: the name between the quotes, or all that follows unquoted
        std::string_view rest;  // backgrounds and videos: what follows the closing quote (",x,y"), as written
    };
    static bool parse(std::string_view line, Event &out);
    static std::string format(const Event &ev);

    // a [Colours] line: Combo1 to Combo8, SliderTrackOverride or SliderBorder : r,g,b
    struct Colour {
        std::string_view name;
        u8 r{0}, g{0}, b{0};
    };
    static bool parse(std::string_view line, Colour &out);
    static std::string format(const Colour &colour);

   private:
    std::string_view bytes;
    std::vector<Section> sections;
    bool bom;
};

}  // namespace neomod
