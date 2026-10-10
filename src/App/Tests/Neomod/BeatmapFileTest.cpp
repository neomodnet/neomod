// Copyright (c) 2026, WH, All rights reserved.
#include "BeatmapFileTest.h"

#include "TestMacros.h"
#include "BeatmapFile.h"
#include "BeatmapPrimitives.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "File.h"
#include "HitObjects.h"
#include "Parsing.h"
#include "SliderCurves.h"
#include "SyncJthread.h"
#include "Timing.h"
#include "Vectors.h"

#include "fmt/format.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <numbers>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace Mc::Tests {
using namespace neomod;
using Kind = BeatmapFile::SectionKind;

namespace {

std::string concatSections(const BeatmapFile &file) {
    std::string out{file.hasBom() ? "\xEF\xBB\xBF" : ""};
    for(const auto &section : file.getSections()) {
        out.append(section.header);
        out.append(section.body);
    }
    return out;
}

std::vector<std::string> entryTexts(const BeatmapFile &file, Kind kind) {
    std::vector<std::string> out;
    for(const auto line : file.getEntries(kind)) out.emplace_back(line.text);
    return out;
}

// how the records of one kind fared through format() over the corpus
struct RecordStats {
    u64 entries{0};    // lines in sections of this kind
    u64 records{0};    // that parsed
    u64 identical{0};  // formatted back to the same text
    u64 same{0};       // formatted back to text that parses to the same record
    std::vector<std::string> notSame;
    std::vector<std::string> notIdentical;
    std::map<std::string, u64> notSameByVersion;

    void add(const RecordStats &o) {
        entries += o.entries;
        records += o.records;
        identical += o.identical;
        same += o.same;
        for(const auto &e : o.notSame) {
            if(notSame.size() < 12) notSame.push_back(e);
        }
        for(const auto &e : o.notIdentical) {
            if(notIdentical.size() < 8) notIdentical.push_back(e);
        }
        for(const auto &[v, n] : o.notSameByVersion) notSameByVersion[v] += n;
    }
};

struct FileResult {
    bool bytesBack{true};
    std::map<std::string, RecordStats> records;  // by section
    std::string dump;
};

template <typename Record, typename Format>
void roundTrip(const BeatmapFile &file, Kind kind, std::string_view where, RecordStats &stats, Format &&format) {
    const std::string version = fmt::format("v{}", file.getVersion().value_or(-1));
    Record record;
    for(const auto line : file.getEntries(kind)) {
        stats.entries++;
        if(!BeatmapFile::parse(line.text, record)) continue;
        stats.records++;
        const std::string text = format(record);
        if(text == line.text) {
            stats.identical++;
            stats.same++;
            continue;
        }
        if(stats.notIdentical.size() < 8) {
            stats.notIdentical.push_back(fmt::format("{}:{}: {} -> {}", where, line.number, line.text, text));
        }
        if(Record again; BeatmapFile::parse(text, again) && again == record) {
            stats.same++;
            continue;
        }
        stats.notSameByVersion[version]++;
        if(stats.notSame.size() < 12) {
            stats.notSame.push_back(fmt::format("{}:{}: {} -> {}", where, line.number, line.text, text));
        }
    }
}

// what the game loads from one file, in a form two builds can be compared by
std::string dumpGameLoad(const std::string &path, const std::string &relative, std::span<const u8> bytes) {
    std::string out = fmt::format("== {}\n", relative);

    DatabaseBeatmap meta(path, std::filesystem::path(path).parent_path().string() + "/",
                         DatabaseBeatmap::BeatmapType::NEOMOD_DIFFICULTY);
    const auto metaResult = meta.loadMetadata(false);
    fmt::format_to(std::back_inserter(out),
                   "meta err={} ver={} title={:?} titleU={:?} artist={:?} artistU={:?} creator={:?} diff={:?} "
                   "source={:?} tags={:?} audio={:?} bg={:?} id={} set={} preview={} ar={} cs={} hp={} od={} sl={} "
                   "sm={} tr={} bpm={}/{}/{}\n",
                   static_cast<int>(metaResult.error.errc), meta.getVersion(), meta.getTitleLatin(),
                   meta.getTitleUnicode(), meta.getArtistLatin(), meta.getArtistUnicode(), meta.getCreator(),
                   meta.getDifficultyName(), meta.getSource(), meta.getTags(), meta.getAudioFileName(),
                   meta.getBackgroundImageFileName(), meta.getID(), meta.getSetID(), meta.getPreviewTime(),
                   meta.getAR(), meta.getCS(), meta.getHP(), meta.getOD(), meta.getStackLeniency(),
                   meta.getSliderMultiplier(), meta.getSliderTickRate(), meta.getMinBPM(), meta.getMaxBPM(),
                   meta.getMostCommonBPM());
    for(const auto &tp : meta.getTimingpoints()) {
        fmt::format_to(std::back_inserter(out), "mtp {} {} {} {} {} {} {}\n", tp.offset, tp.msPerBeat, tp.sampleSet,
                       tp.sampleIndex, tp.volume, tp.uninherited, tp.kiai);
    }

    const auto c = Primitives::loadPrimitiveObjectsFromData(bytes, {});
    fmt::format_to(std::back_inserter(out),
                   "prim err={} ver={} ar={} cs={} od={} hp={} sl={} sm={} tr={} set={} breaktime={}\n",
                   static_cast<int>(c.error.errc), c.version, c.AR, c.CS, c.OD, c.HP, c.stackLeniency,
                   c.sliderMultiplier, c.sliderTickRate, c.defaultSampleSet, c.totalBreakDuration);
    for(const auto &b : c.breaks) fmt::format_to(std::back_inserter(out), "brk {} {}\n", b.startTime, b.endTime);
    for(const auto col : c.combocolors) fmt::format_to(std::back_inserter(out), "col {:08x}\n", col);
    for(const auto &tp : c.timingpoints) {
        fmt::format_to(std::back_inserter(out), "tp {} {} {} {} {} {} {}\n", tp.offset, tp.msPerBeat, tp.sampleSet,
                       tp.sampleIndex, tp.volume, tp.uninherited, tp.kiai);
    }
    const auto samples = [](const DBType::HITSAMPLE_BITS &s) {
        return fmt::format("{}:{}:{}:{}", s.hitSounds, s.normalSet, s.additionSet, s.volume);
    };
    // what the game looks up at an object's time
    const auto timing = [&c](i32 time) {
        const DBType::TIMING_INFO ti = c.timingpoints.getTimingInfo(time);
        return fmt::format("{}/{}/{}/{}/{}/{}/{}", ti.offset, ti.beatLengthBase, ti.beatLength, ti.sampleSet,
                           ti.sampleIndex, ti.volume, ti.isNaN);
    };
    // in the order they're played, each with whether it ends its combo
    for(const auto &object : c.objects) {
        if(const auto *h = std::get_if<DBType::HITCIRCLE>(&object)) {
            fmt::format_to(std::back_inserter(out), "c {} {} {} {} {} {} {} {} {}\n", h->x, h->y, h->time, h->number,
                           h->colorCounter, h->colorOffset, h->isEndOfCombo, samples(h->samples), timing(h->time));
        } else if(const auto *s = std::get_if<DBType::SLIDER>(&object)) {
            fmt::format_to(std::back_inserter(out), "s {} {} {} {} {} {} {} {} {} {} {} {}", s->x, s->y, s->time,
                           s->number, s->colorCounter, s->colorOffset, s->isEndOfCombo, static_cast<char>(s->type),
                           s->repeat, s->pixelLength, samples(s->hoverSamples), s->points.size());
            for(const auto &p : s->points) fmt::format_to(std::back_inserter(out), " {},{}", p.x, p.y);
            for(const auto &e : s->edgeSamples) fmt::format_to(std::back_inserter(out), " e{}", samples(e));
            fmt::format_to(std::back_inserter(out), " {}/{}/{} {}\n", s->sliderTime, s->sliderTimeWithoutRepeats,
                           s->ticks.size(), timing(s->time));
        } else {
            const auto &spinner = std::get<DBType::SPINNER>(object);
            fmt::format_to(std::back_inserter(out), "sp {} {} {} {} {} {} {}\n", spinner.x, spinner.y, spinner.time,
                           spinner.endTime, spinner.isEndOfCombo, samples(spinner.samples), timing(spinner.time));
        }
    }
    return out;
}

FileResult checkFile(const std::string &path, const std::string &relative, bool dump) {
    FileResult result;
    std::vector<u8> bytes;
    {
        File file(path);
        file.readToVector(bytes);
    }
    const BeatmapFile file{std::span<const u8>{bytes}};

    const std::string back = concatSections(file);
    result.bytesBack =
        back.size() == bytes.size() && (bytes.empty() || std::memcmp(back.data(), bytes.data(), bytes.size()) == 0);

    roundTrip<BeatmapFile::TimingPoint>(file, Kind::TIMING_POINTS, relative, result.records["TimingPoints"],
                                        [](const auto &r) { return BeatmapFile::format(r); });
    roundTrip<BeatmapFile::HitObject>(file, Kind::HIT_OBJECTS, relative, result.records["HitObjects"],
                                      [](const auto &r) { return BeatmapFile::format(r); });
    roundTrip<BeatmapFile::Colour>(file, Kind::COLOURS, relative, result.records["Colours"],
                                   [](const auto &r) { return BeatmapFile::format(r); });
    for(const Kind kind : {Kind::GENERAL, Kind::EDITOR, Kind::METADATA, Kind::DIFFICULTY}) {
        roundTrip<BeatmapFile::KeyValue>(file, kind, relative, result.records[fmt::format("kv{}", (int)kind)],
                                         [kind](const auto &r) { return BeatmapFile::format(kind, r); });
    }
    // only the events the game reads count here (storyboard lines don't parse)
    {
        RecordStats &stats = result.records["Events"];
        RecordStats all;
        roundTrip<BeatmapFile::Event>(file, Kind::EVENTS, relative, all,
                                      [](const auto &r) { return BeatmapFile::format(r); });
        all.entries = all.records;
        stats.add(all);
    }

    if(dump) result.dump = dumpGameLoad(path, relative, bytes);
    return result;
}

// single-threaded parse times over files already in memory, best of `rounds`
void bench(const std::vector<std::pair<std::string, std::string>> &files, int rounds) {
    std::vector<std::vector<u8>> contents(files.size());
    for(uSz f = 0; f < files.size(); f++) {
        File file(files[f].first);
        file.readToVector(contents[f]);
    }

    const auto best = [rounds](auto &&fn) {
        f64 fastest = 1e9;
        for(int r = 0; r < rounds; r++) {
            const f64 start = Timing::getTimeReal();
            fn();
            fastest = std::min(fastest, Timing::getTimeReal() - start);
        }
        return fastest;
    };

    u64 sink = 0;
    const f64 primitives = best([&] {
        for(uSz f = 0; f < files.size(); f++) {
            const auto c = Primitives::loadPrimitiveObjectsFromData(contents[f], {});
            sink += c.getNumObjects();
        }
    });
    const f64 metadata = best([&] {
        for(const auto &[path, relative] : files) {
            DatabaseBeatmap meta(path, std::filesystem::path(path).parent_path().string() + "/",
                                 DatabaseBeatmap::BeatmapType::NEOMOD_DIFFICULTY);
            sink += meta.loadMetadata(false).fileData.size();
        }
    });
    const f64 records = best([&] {
        BeatmapFile::HitObject ho;
        BeatmapFile::TimingPoint tp;
        BeatmapFile::KeyValue kv;
        for(const auto &bytes : contents) {
            const BeatmapFile file{std::span<const u8>{bytes}};
            for(const Kind kind : {Kind::GENERAL, Kind::METADATA, Kind::DIFFICULTY}) {
                for(const auto line : file.getEntries(kind)) sink += BeatmapFile::parse(line.text, kv);
            }
            for(const auto line : file.getEntries(Kind::TIMING_POINTS)) sink += BeatmapFile::parse(line.text, tp);
            for(const auto line : file.getEntries(Kind::HIT_OBJECTS)) sink += BeatmapFile::parse(line.text, ho);
        }
    });
    logRaw("  bench over {} files, best of {}: primitives {:.3f} s, metadata {:.3f} s, records {:.3f} s ({})",
           files.size(), rounds, primitives, metadata, records, sink);
}

}  // namespace

BeatmapFileTest::BeatmapFileTest() { logRaw("BeatmapFileTest created"); }

void BeatmapFileTest::update() {
    if(m_ran) return;
    m_ran = true;

    runTests();
    if(const auto corpus = getTestArg("corpus")) runCorpus(*corpus);

    TEST_PRINT_RESULTS("BeatmapFileTest");
    engine->shutdown();
}

void BeatmapFileTest::runTests() {
    TEST_SECTION("sections");
    {
        const std::string_view text =
            "osu file format v14\r\n"
            "// a comment\r\n"
            "\r\n"
            "[General]\r\n"
            "AudioFilename: audio.mp3\r\n"
            "Mode: 0\r\n"
            "\r\n"
            "[Editor]\r\n"
            "DistanceSpacing: 1.2\r\n"
            "[Metadata]\r\n"
            "Title:Song\r\n"
            "[HitObjects]\r\n"
            "256,192,1000,1,0,0:0:0:0:";  // no final line break
        const BeatmapFile file{text};
        const auto sections = file.getSections();
        TEST_ASSERT_EQ(sections.size(), 5, "preamble + 4 sections");
        TEST_ASSERT(concatSections(file) == text, "sections give back every byte");
        TEST_ASSERT(sections[0].kind == Kind::NONE && sections[0].header.empty(), "lines before the first section");
        TEST_ASSERT(sections[1].kind == Kind::GENERAL && sections[1].name == "General", "[General]");
        TEST_ASSERT_EQ(sections[1].bodyLine, 5, "line number after [General]");
        TEST_ASSERT(sections[0].lines == 3 && sections[1].lines == 3 && sections[4].lines == 1,
                    "lines in a section's body (the last without a final line break)");
        TEST_ASSERT(sections[2].kind == Kind::EDITOR && sections[2].readAs == Kind::EDITOR, "[Editor] is known");
        TEST_ASSERT_EQ(file.getVersion().value_or(-1), 14, "version");

        const auto general = entryTexts(file, Kind::GENERAL);
        TEST_ASSERT(general == std::vector<std::string>({"AudioFilename: audio.mp3", "Mode: 0"}),
                    "entries skip blank lines and drop line breaks");
        TEST_ASSERT(entryTexts(file, Kind::NONE) == std::vector<std::string>{"osu file format v14"},
                    "comments are no entries");
        TEST_ASSERT(entryTexts(file, Kind::HIT_OBJECTS) == std::vector<std::string>{"256,192,1000,1,0,0:0:0:0:"},
                    "a last line without a line break");

        u32 number = 0;
        for(const auto line : file.getEntries(Kind::GENERAL)) number = line.number;
        TEST_ASSERT_EQ(number, 6, "entry line numbers");
        TEST_ASSERT(file.getValue(Kind::METADATA, "Title") == std::optional<std::string_view>{"Song"}, "getValue");
        TEST_ASSERT(!file.getValue(Kind::METADATA, "Artist").has_value(), "getValue of a missing key");
    }
    {
        // unknown names keep the section before them going, as in osu!stable; repeated sections all count
        const std::string_view text =
            "osu file format v14\n[Difficulty]\nCircleSize:4\n[Mystery]\nApproachRate:9\n[Events]\n[Difficulty]\n"
            "CircleSize:5\n";
        const BeatmapFile file{text};
        TEST_ASSERT(concatSections(file) == text, "lf only: sections give back every byte");
        TEST_ASSERT(file.getSections()[2].kind == Kind::UNKNOWN && file.getSections()[2].readAs == Kind::DIFFICULTY,
                    "an unknown section is read as the one before it");
        TEST_ASSERT(entryTexts(file, Kind::DIFFICULTY) ==
                        std::vector<std::string>({"CircleSize:4", "ApproachRate:9", "CircleSize:5"}),
                    "entries of every section read as [Difficulty]");
        TEST_ASSERT(file.getValue(Kind::DIFFICULTY, "CircleSize") == std::optional<std::string_view>{"5"},
                    "the last value wins");
    }
    {
        const BeatmapFile empty{std::string_view{}};
        TEST_ASSERT_EQ(empty.getSections().size(), 1, "an empty file is one empty section");
        TEST_ASSERT(!empty.getVersion().has_value(), "an empty file has no version");

        const BeatmapFile noHeader{std::string_view{"[General]\r\nMode: 1\r\n"}};
        TEST_ASSERT(!noHeader.getVersion().has_value(), "no version line");
        TEST_ASSERT(entryTexts(noHeader, Kind::GENERAL) == std::vector<std::string>{"Mode: 1"},
                    "a file that starts with a section");

        const std::string_view bomText{"\xEF\xBB\xBFosu file format v5\r\n[General]\r\n"};
        const BeatmapFile bom{bomText};
        TEST_ASSERT(bom.hasBom() && bom.getVersion() == std::optional<i32>{5}, "the version line after a BOM");
        TEST_ASSERT(concatSections(bom) == bomText, "a BOM and the sections give back every byte");
        TEST_ASSERT(!BeatmapFile{std::string_view{"osu file format v5"}}.hasBom(), "no BOM");
        const BeatmapFile bomLine{std::string_view{"\xEF\xBB\xBF\n\nosu file format v5\n[General]\n"}};
        TEST_ASSERT_EQ(bomLine.getVersion().value_or(-1), 5, "a version line after other lines");

        const std::string_view crText{"osu file format v14\r[General]\rMode: 0\r\r\nStackLeniency: 0.5\n"};
        const BeatmapFile crOnly{crText};
        TEST_ASSERT_EQ(crOnly.getSections().size(), 2, "a CR of its own ends a line");
        TEST_ASSERT(entryTexts(crOnly, Kind::GENERAL) == std::vector<std::string>({"Mode: 0", "StackLeniency: 0.5"}),
                    "CR, CRLF and LF line breaks mixed");
        TEST_ASSERT(concatSections(crOnly) == crText, "CR line breaks: sections give back every byte");
        TEST_ASSERT_EQ(crOnly.getVersion().value_or(-1), 14, "CR line breaks: version");
    }

    TEST_SECTION("key-values");
    {
        BeatmapFile::KeyValue kv;
        TEST_ASSERT(BeatmapFile::parse("  StackLeniency :  0.7 ", kv) && kv.key == "StackLeniency" && kv.value == "0.7",
                    "both sides trimmed");
        TEST_ASSERT(BeatmapFile::parse("Title:Re:Zero", kv) && kv.key == "Title" && kv.value == "Re:Zero",
                    "split at the first ':'");
        TEST_ASSERT(BeatmapFile::parse("Source:", kv) && kv.value.empty(), "empty value");
        TEST_ASSERT(!BeatmapFile::parse("no colon", kv), "not a key-value");
        TEST_ASSERT_EQ(BeatmapFile::format(Kind::GENERAL, {"Mode", "0"}), "Mode: 0", "[General] spacing");
        TEST_ASSERT_EQ(BeatmapFile::format(Kind::METADATA, {"Title", "x"}), "Title:x", "[Metadata] spacing");
    }

    TEST_SECTION("timing points");
    {
        BeatmapFile::TimingPoint tp;
        TEST_ASSERT(BeatmapFile::parse("1000,333.333333333333,4,2,1,60,1,0", tp), "8 fields");
        TEST_ASSERT(tp.time == 1000 && tp.beatLength == 333.333333333333 && tp.meter == 4 && tp.sampleSet == 2 &&
                        tp.sampleIndex == 1 && tp.volume == 60 && tp.uninherited && tp.effects == 0,
                    "8 fields read");
        TEST_ASSERT_EQ(BeatmapFile::format(tp), "1000,333.333333333333,4,2,1,60,1,0", "8 fields written back");

        TEST_ASSERT(BeatmapFile::parse("1500.5,-50,4,2,0,40,0,9", tp) && tp.time == 1500.5 && !tp.uninherited &&
                        tp.effects == (BeatmapFile::TimingPoint::EFFECT_KIAI |
                                       BeatmapFile::TimingPoint::EFFECT_OMIT_FIRST_BARLINE),
                    "inherited, fractional time, effects");
        TEST_ASSERT_EQ(BeatmapFile::format(tp), "1500.5,-50,4,2,0,40,0,9", "written back");

        TEST_ASSERT(BeatmapFile::parse("100,500", tp) && tp.meter == 4 && tp.sampleSet == 0 && tp.uninherited &&
                        tp.volume == 100,
                    "2 fields, the rest defaults");
        TEST_ASSERT(BeatmapFile::parse("100,500,3,2,1,70", tp) && tp.meter == 3 && tp.sampleSet == 2 &&
                        tp.sampleIndex == 1 && tp.volume == 70 && tp.uninherited && tp.effects == 0,
                    "6 fields");
        TEST_ASSERT(BeatmapFile::parse("100,500,x,1", tp) && tp.meter == 4 && tp.sampleSet == 0,
                    "read up to the first non-number");
        TEST_ASSERT(!BeatmapFile::parse("100", tp), "1 field");
        TEST_ASSERT(BeatmapFile::parse("100,NaN,4,1,0,100,1,0", tp) && std::isnan(tp.beatLength), "NaN");
        TEST_ASSERT_EQ(BeatmapFile::format(tp), "100,NaN,4,1,0,100,1,0", "NaN written back");
        TEST_ASSERT(BeatmapFile::parse("100,1E-05,4,1,0,100,1,0", tp) && tp.beatLength == 1e-05, "exponent");
        TEST_ASSERT_EQ(BeatmapFile::format(tp), "100,1E-05,4,1,0,100,1,0", "exponent written as .NET does");
        TEST_ASSERT(BeatmapFile::parse("100,1.99866755496336E-07,4,1,0,100,1,0", tp), "small exponent");
        TEST_ASSERT_EQ(BeatmapFile::format(tp), "100,1.99866755496336E-07,4,1,0,100,1,0", "small exponent back");
        TEST_ASSERT(BeatmapFile::parse("8250,-166.66666666666669,4,3,1,60,0,0", tp), "17 digits (osu!lazer)");
        TEST_ASSERT_EQ(BeatmapFile::format(tp), "8250,-166.66666666666669,4,3,1,60,0,0", "17 digits written back");
    }

    TEST_SECTION("beats");
    {
        const auto point = [](f64 offset, f64 msPerBeat, bool uninherited) {
            return DBType::TIMINGPOINT{.offset = offset,
                                       .msPerBeat = msPerBeat,
                                       .sampleSet = 0,
                                       .sampleIndex = 0,
                                       .volume = 100,
                                       .uninherited = uninherited,
                                       .kiai = false};
        };
        const Primitives::TimingPoints whole{{point(1000, 500, true), point(2000, -50, false), point(5100, 400, true)}};
        TEST_ASSERT(whole.getBeat(1000) == 0.0, "the uninherited point is beat 0");
        TEST_ASSERT(whole.getBeat(3250) == 4.5, "beats with their fraction, an inherited point doesn't count");
        TEST_ASSERT(whole.getBeat(750) == -0.5, "before the first point, from it");
        TEST_ASSERT(whole.getBeat(5500) == 1.0, "a later uninherited point starts over");

        // 170 BPM: a beat length with a fraction doesn't drift (integer milliseconds would be 0.94 ms off a beat)
        const Primitives::TimingPoints fraction{{point(0, 60000.0 / 170, true)}};
        TEST_ASSERT(std::abs(fraction.getBeat(35294) - 100.0) < 0.001, "the 100th beat of 352.94 ms");

        TEST_ASSERT(Primitives::TimingPoints{{point(0, std::nan(""), true)}}.getBeat(1000) == 0.0, "NaN beat length");
        TEST_ASSERT(Primitives::TimingPoints{}.getBeat(1000) == 0.0, "no points");
    }

    TEST_SECTION("hit objects");
    {
        using HO = BeatmapFile::HitObject;
        HO ho;
        TEST_ASSERT(BeatmapFile::parse("256,192,1000,5,2,0:2:0:30:", ho), "circle");
        TEST_ASSERT(ho.kind == HO::Kind::CIRCLE && ho.x == 256 && ho.y == 192 && ho.time == 1000 && ho.type == 5 &&
                        ho.hitSounds == 2 && ho.sample.additionSet == 2 && ho.sample.volume == 30 &&
                        ho.sample.parts == 5 && ho.sample.filename.empty(),
                    "circle read (an empty filename counts as written)");
        TEST_ASSERT_EQ(BeatmapFile::format(ho), "256,192,1000,5,2,0:2:0:30:", "circle written back");

        const std::string_view slider =
            "100,100,2000,2,0,B|200:100|200:200|300:200,2,240.000009155273,2|0|8,1:2|0:0|2:3,0:0:0:0:";
        TEST_ASSERT(BeatmapFile::parse(slider, ho), "slider");
        TEST_ASSERT(ho.kind == HO::Kind::SLIDER && ho.curveType == 'B' && ho.curvePoints.size() == 3 &&
                        ho.curvePoints[2] == vec2(300, 200) && ho.slides == 2 && ho.length == 240.000009155273 &&
                        ho.edgeSounds == std::vector<u8>({2, 0, 8}) && ho.edgeSets.size() == 3 &&
                        ho.edgeSets[2].normalSet == 2 && ho.edgeSets[2].additionSet == 3 && ho.sample.parts == 5,
                    "slider read");
        TEST_ASSERT(
            BeatmapFile::parse("64,64,3000,2,0,L|128:64,1,64", ho) && ho.edgeSounds.empty() && ho.sample.parts == 0,
            "slider without edges, reusing the record");
        TEST_ASSERT_EQ(BeatmapFile::format(ho), "64,64,3000,2,0,L|128:64,1,64", "slider without edges back");
        TEST_ASSERT(BeatmapFile::parse("64,64,3000,2,0,L|128:64|inf:5|P|7:7,1,64", ho) && ho.curvePoints.size() == 2,
                    "curve parts that aren't two finite numbers are left out");
        TEST_ASSERT(!BeatmapFile::parse("64,64,3000,2,0,L|128:64,x,64", ho), "unreadable slides");
        TEST_ASSERT(
            BeatmapFile::parse("64,64,3000,2,0,L|128:64,1,1e+500", ho) && std::isinf(ho.length) && ho.length > 0,
            "a length too large for a double");
        TEST_ASSERT(!BeatmapFile::parse("64,64,3000,2,0,L|128:64,1,x", ho), "unreadable length");
        TEST_ASSERT(BeatmapFile::parse("64,64,3000,2,0,,1,64", ho) && ho.curveType == '\0', "empty curve field");
        TEST_ASSERT(BeatmapFile::parse("64,64,3000,2,0,L|1:1,1", ho) && ho.length == 0.0,
                    "a slider without its length field");
        TEST_ASSERT_EQ(BeatmapFile::format(ho), "64,64,3000,2,0,L|1:1,1,0",
                       "written with a length of 0, read the same");

        TEST_ASSERT(BeatmapFile::parse("256,192,4000,12,0,6000,0:0:0:0:", ho), "spinner");
        TEST_ASSERT(ho.kind == HO::Kind::SPINNER && ho.endTime == 6000, "spinner read");
        TEST_ASSERT_EQ(BeatmapFile::format(ho), "256,192,4000,12,0,6000,0:0:0:0:", "spinner written back");
        TEST_ASSERT(!BeatmapFile::parse("256,192,4000,12,0,x", ho), "unreadable spinner end");

        TEST_TODO TEST_ASSERT(BeatmapFile::parse("64,192,500,128,0,1000:0:0:0:0:", ho), "osu!mania hold");
        TEST_ASSERT(!BeatmapFile::parse("64,192,500,1", ho), "fewer than 5 fields");
        TEST_ASSERT(!BeatmapFile::parse("nan,192,500,1,0", ho), "position not finite");
        TEST_ASSERT(!BeatmapFile::parse("64,192,500,300,0", ho), "type beyond a byte");
        TEST_ASSERT(BeatmapFile::parse("64.75,192.5,500.9,1,0", ho) && ho.x == 64.75f && ho.time == 500,
                    "fractions: positions as written, times cut");
        TEST_ASSERT_EQ(BeatmapFile::format(ho), "64.75,192.5,500,1,0", "fractional positions written back");
        TEST_ASSERT(BeatmapFile::parse("216,333,226724,2,0,L|124.10634:316.6248,1,65.0000026702881", ho),
                    "fractional control points");
        TEST_ASSERT_EQ(BeatmapFile::format(ho), "216,333,226724,2,0,L|124.10634:316.6248,1,65.0000026702881",
                       "fractional control points and a stable length written back");
        TEST_ASSERT(BeatmapFile::parse("64,192,500,4,0", ho) && ho.kind == HO::Kind::NONE, "no kind bit");
        TEST_ASSERT(BeatmapFile::parse("1,2,3,1,0,0:0:0:70:hit.wav", ho) && ho.sample.filename == "hit.wav",
                    "sample file");
        TEST_ASSERT(BeatmapFile::parse("1,2,3,1,0,1:2:0", ho) && ho.sample.parts == 3 && ho.sample.additionSet == 2,
                    "an older sample of three parts");
        TEST_ASSERT_EQ(BeatmapFile::format(ho), "1,2,3,1,0,1:2:0", "three parts written back");
        TEST_ASSERT(BeatmapFile::parse("1,2,3,1,0,0:0:0:0:hit.wav:x", ho) && ho.sample.parts == 5 &&
                        ho.sample.filename == "hit.wav",
                    "what follows the filename isn't a part");
    }

    TEST_SECTION("the game's reading");
    {
        const auto load = [](std::string_view text) {
            return Primitives::loadPrimitiveObjectsFromData(
                std::span{reinterpret_cast<const u8 *>(text.data()), text.size()}, {});
        };
        const auto bom = load("\xEF\xBB\xBFosu file format v5\r\n[HitObjects]\r\n1,2,3,1,0\r\n");
        TEST_ASSERT_EQ(bom.version, 5, "the version after a BOM");

        const auto cr = load(
            "osu file format v14\r[TimingPoints]\r0,500,4,1,0,100,1,8\r100,500,4,1,0,100,1,9\r"
            "[HitObjects]\r1,2,3,1,0,0:0:0:-5:\r1,2,4,1,0,0:0:0:300:\r");
        TEST_ASSERT_EQ(cr.getNumObjects<DBType::HITCIRCLE>(), 2, "a file with CR line breaks");
        TEST_ASSERT(cr.timingpoints.size() == 2 && !cr.timingpoints[0].kiai && cr.timingpoints[1].kiai,
                    "kiai is the effects field's first bit (8 omits the first barline)");
        const auto volume = [&cr](uSz i) { return std::get<DBType::HITCIRCLE>(cr.objects[i]).samples.volume; };
        TEST_ASSERT(cr.getNumObjects<DBType::HITCIRCLE>() == 2 && volume(0) == 0 && volume(1) == 100,
                    "sample volumes clamped to 0-100");

        const auto times = load(
            "osu file format v14\r\n[TimingPoints]\r\nNaN,500,4,1,0,100,1,0\r\n3e9,500,4,1,0,100,1,0\r\n"
            "-3e9,500,4,1,0,100,1,0\r\n100,500,4,1,0,100,1,0\r\n");
        TEST_ASSERT(times.timingpoints.size() == 1 && times.timingpoints[0].offset == 100,
                    "timing points at times that aren't numbers or don't fit in 32 bits are dropped");

        const auto lengths = load(
            "osu file format v14\r\n[TimingPoints]\r\n0,500,4,1,0,100,1,0\r\n[HitObjects]\r\n0,0,1000,2,0,L|100:0,1\r\n"
            "0,0,2000,2,0,L|0:50,1,0\r\n0,0,3000,2,0,L|30:40,1,20\r\n0,0,4000,2,0,P|50:50|100:0,1\r\n");
        const auto length = [&lengths](uSz i) { return std::get<DBType::SLIDER>(lengths.objects[i]).pixelLength; };
        TEST_ASSERT(lengths.getNumObjects<DBType::SLIDER>() == 4 && length(0) == 100.f && length(1) == 50.f &&
                        length(2) == 20.f && std::abs(length(3) - 50.f * std::numbers::pi_v<f32>) < 0.01f,
                    "a slider without a length, or with 0, is as long as its curve");

        // a curve's own length
        const auto natural = [](SLIDERCURVETYPE type, std::vector<vec2> points) {
            return SliderCurve{type, points, 0.f}.getPixelLength();
        };
        using enum SLIDERCURVETYPE;
        TEST_ASSERT(std::abs(natural(BEZIER, {{0.f, 0.f}, {100.f, 0.f}, {100.f, 0.f}, {100.f, 100.f}}) - 200.f) < 0.01f,
                    "a doubled point starts a new bezier");
        TEST_ASSERT(
            std::abs(natural(BEZIER, {{0.f, 0.f}, {100.f, 0.f}, {100.f, 100.f}, {100.f, 100.f}}) - 158.64f) < 0.5f,
            "...except a doubled last point, which stays in its bezier (the cubic a b c c)");
        TEST_ASSERT(std::abs(natural(PASSTHROUGH, {{0.f, 0.f}, {200.f, 0.f}, {100.f, 0.f}}) - 300.f) < 0.01f,
                    "a perfect circle through three points on a line is the line through them");
        const std::vector<vec2> nearlyOnALine{{83.28f, 117.45f}, {-8.f, 93.f}, {-120.f, 63.f}};
        TEST_ASSERT(
            std::abs(natural(PASSTHROUGH, nearlyOnALine) - vec::distance(nearlyOnALine[0], nearlyOnALine[1]) -
                     vec::distance(nearlyOnALine[1], nearlyOnALine[2])) < 0.01f &&
                vec::distance(SliderCurve{PASSTHROUGH, nearlyOnALine, 0.f}.pointAt(1.f), nearlyOnALine[2]) < 0.01f,
            "...also when floats can't tell them from a line (fractional points: no circle through them)");
        TEST_ASSERT(std::abs(natural(CATMULL, {{0.f, 0.f}, {100.f, 0.f}}) - 100.f) < 0.01f,
                    "a catmull curve through two points goes from one to the other once");
        const std::vector<vec2> corner{{0.f, 0.f}, {100.f, 0.f}, {100.f, 100.f}};
        TEST_ASSERT(
            std::abs(natural(CATMULL, corner) - 204.19f) < 0.01f &&
                vec::distance(SliderCurve{CATMULL, corner, 0.f}.pointAt(0.75f), vec2{105.584f, 49.296f}) < 0.05f,
            "...through more, a curve between each two of them, past the last one going on as it came");

        // where a curve of a given length ends
        const auto end = [](SLIDERCURVETYPE type, std::vector<vec2> points, f32 length) {
            return SliderCurve{type, points, length}.pointAt(1.f);
        };
        TEST_ASSERT(vec::distance(end(LINEAR, {{0.f, 0.f}, {100.f, 0.f}}, 150.f), vec2{150.f, 0.f}) < 0.01f,
                    "a slider longer than its curve goes on along the curve's last segment");
        TEST_ASSERT(
            vec::distance(end(BEZIER, {{0.f, 0.f}, {100.f, 0.f}, {100.f, 100.f}}, 300.f), vec2{100.f, 237.68f}) < 1.5f,
            "...a bezier's too, the way it ends (its last approximated piece, close to the tangent)");
        TEST_ASSERT(
            vec::distance(end(LINEAR, {{0.f, 0.f}, {100.f, 0.f}, {100.f, 0.f}}, 150.f), vec2{100.f, 0.f}) < 0.01f,
            "...but not when that segment has no length");

        // the circle at a time: its number, color counter and offset
        const auto combo = [](const Primitives::PRIMITIVE_CONTAINER &c, i32 time) {
            for(const auto &object : c.objects) {
                const auto *h = std::get_if<DBType::HITCIRCLE>(&object);
                if(h && h->time == time) return fmt::format("{} {} {}", h->number, h->colorCounter, h->colorOffset);
            }
            return std::string{"none"};
        };

        // (lines out of time order: combos as the objects come in time, as osu!stable and lazer number them)
        const auto order =
            load("osu file format v14\r\n[HitObjects]\r\n0,0,3000,37,0\r\n0,0,1000,5,0\r\n0,0,2000,1,0\r\n");
        TEST_ASSERT_EQ(combo(order, 1000), "1 1 0", "the earliest object starts the first combo, wherever its line is");
        TEST_ASSERT_EQ(combo(order, 2000), "2 1 0", "...the next one in time continues it");
        TEST_ASSERT_EQ(combo(order, 3000), "1 2 2", "...and a later new combo (skipping two colours) starts the next");

        const auto sameTime = load(
            "osu file format v14\r\n[HitObjects]\r\n0,0,1000,2,0,L|100:0,1,100\r\n1,0,1000,1,0\r\n"
            "256,192,500,12,0,800\r\n2,0,1000,1,0\r\n");
        std::string kinds;
        for(const auto &object : sameTime.objects) {
            const char kind = std::holds_alternative<DBType::HITCIRCLE>(object) ? 'c'
                              : std::holds_alternative<DBType::SLIDER>(object)  ? 's'
                                                                                : 'p';
            kinds += fmt::format("{}{} ", kind, std::visit([](const auto &o) { return o.x; }, object));
        }
        TEST_ASSERT_EQ(kinds, "p256 s0 c1 c2 ", "the objects by time, equal times in the order of their lines");

        const auto spinners = load(
            "osu file format v14\r\n[HitObjects]\r\n0,0,1000,5,0\r\n0,0,2000,1,0\r\n256,192,3000,12,0,4000\r\n"
            "0,0,5000,1,0\r\n0,0,6000,1,0\r\n256,192,7000,8,0,8000\r\n0,0,9000,1,0\r\n256,192,10000,12,0,11000\r\n"
            "256,192,12000,8,0,13000\r\n0,0,14000,1,0\r\n256,192,15000,44,0,16000\r\n0,0,17000,1,0\r\n");
        TEST_ASSERT_EQ(combo(spinners, 5000), "1 2 0", "the object after a new combo spinner starts the next combo");
        TEST_ASSERT_EQ(combo(spinners, 6000), "2 2 0", "...which the one after continues");
        TEST_ASSERT_EQ(combo(spinners, 9000), "1 3 0",
                       "a spinner without a new combo starts one on the next object too");
        TEST_ASSERT_EQ(combo(spinners, 14000), "1 4 0", "spinners in a row start one combo after them");
        TEST_ASSERT_EQ(combo(spinners, 17000), "1 5 2", "a new combo spinner's colour skip carries over to it");

        const auto breaks = load(
            "osu file format v14\r\n[Events]\r\n2,6500,8500\r\n2,2500,4500\r\n[HitObjects]\r\n0,0,1000,5,0\r\n"
            "0,0,2000,1,0\r\n0,0,5000,1,0\r\n0,0,6000,1,0\r\n0,0,8500,1,0\r\n0,0,9000,1,0\r\n");
        TEST_ASSERT(
            breaks.breaks.size() == 2 && breaks.breaks[0].startTime == 2500 && breaks.breaks[1].startTime == 6500,
            "breaks by start time, whatever the order of their lines");
        TEST_ASSERT_EQ(combo(breaks, 5000), "1 2 0", "the first object after a break starts the next combo");
        TEST_ASSERT_EQ(combo(breaks, 6000), "2 2 0", "...which the one after continues");
        TEST_ASSERT_EQ(combo(breaks, 8500), "3 2 0", "an object where a break ends is still in it");
        TEST_ASSERT_EQ(combo(breaks, 9000), "1 3 0", "...the one after it starts the next combo");

        // which of the game's objects end a combo, and when the combo each is in starts (in the order they're played)
        const auto comboEnds = [&load](std::string_view text) {
            std::string ends;
            for(const auto &object : HitObjects::create(load(text), nullptr, nullptr)) {
                ends += object->isEndOfCombo() ? '1' : '0';
            }
            return ends;
        };
        const auto comboStarts = [&load](std::string_view text) {
            std::string starts;
            for(const auto &object : HitObjects::create(load(text), nullptr, nullptr)) {
                starts += fmt::format("{} ", object->getComboStartTime());
            }
            return starts;
        };
        constexpr std::string_view withSpinners =
            "osu file format v14\r\n[HitObjects]\r\n0,0,1000,5,0\r\n0,0,2000,1,0\r\n256,192,3000,12,0,4000\r\n"
            "0,0,5000,1,0\r\n0,0,6000,1,0\r\n256,192,7000,8,0,8000\r\n0,0,9000,1,0\r\n";
        TEST_ASSERT_EQ(comboEnds(withSpinners), "0110011",
                       "a combo ends before a spinner with a new combo (not one without) and at every spinner");
        TEST_ASSERT_EQ(comboStarts(withSpinners), "1000 1000 3000 5000 5000 5000 9000 ",
                       "...and the next one starts at the object after its end");
        TEST_ASSERT_EQ(comboEnds("osu file format v8\r\n[HitObjects]\r\n0,0,1000,5,0\r\n0,0,2000,1,0\r\n"
                                 "256,192,3000,8,0,4000\r\n0,0,5000,1,0\r\n"),
                       "0111", "a v8 spinner always starts a combo");
    }

    TEST_SECTION("events and colours");
    {
        BeatmapFile::Event ev;
        TEST_ASSERT(BeatmapFile::parse("0,0,\"bg, final.jpg\",0,0", ev) &&
                        ev.kind == BeatmapFile::Event::Kind::BACKGROUND && ev.file == "bg, final.jpg" &&
                        ev.rest == ",0,0",
                    "background with a comma in its name");
        TEST_ASSERT_EQ(BeatmapFile::format(ev), "0,0,\"bg, final.jpg\",0,0", "background written back");
        TEST_ASSERT(BeatmapFile::parse("0,0,bg.jpg", ev) && ev.file == "bg.jpg", "unquoted background");
        TEST_ASSERT(BeatmapFile::parse("2,1000,2500", ev) && ev.kind == BeatmapFile::Event::Kind::BREAK &&
                        ev.start == 1000 && ev.end == 2500,
                    "break");
        TEST_ASSERT_EQ(BeatmapFile::format(ev), "2,1000,2500", "break written back");
        TEST_ASSERT(BeatmapFile::parse("Video,-200,\"v.mp4\"", ev) && ev.kind == BeatmapFile::Event::Kind::VIDEO &&
                        ev.start == -200,
                    "video");
        TEST_ASSERT_EQ(BeatmapFile::format(ev), "Video,-200,\"v.mp4\"", "video written back");
        TEST_TODO TEST_ASSERT(BeatmapFile::parse("Sprite,Foreground,Centre,\"sb/x.png\",320,240", ev), "storyboard");
        TEST_ASSERT(!BeatmapFile::parse("0,0,\"unclosed", ev), "unclosed quote");

        BeatmapFile::Colour colour;
        TEST_ASSERT(BeatmapFile::parse("Combo1 : 255,128,0", colour) && colour.name == "Combo1" && colour.r == 255 &&
                        colour.g == 128 && colour.b == 0,
                    "colour");
        TEST_ASSERT_EQ(BeatmapFile::format(colour), "Combo1 : 255,128,0", "colour written back");
        TEST_ASSERT(BeatmapFile::parse("Combo2 : 100, 100, 100 // a comment", colour) && colour.b == 100,
                    "a comment after the colour");
        TEST_ASSERT(!BeatmapFile::parse("Combo3 : 256,0,0", colour), "a component beyond a byte");
    }
}

void BeatmapFileTest::runCorpus(const std::string &dir) {
    TEST_SECTION(fmt::format("corpus {}", dir));
    namespace fs = std::filesystem;

    std::vector<std::pair<std::string, std::string>> files;  // path, relative to dir
    std::error_code ec;
    for(auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
        !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path &path = it->path();
        if(path.extension() == ".osu" && it->is_regular_file(ec)) {
            files.emplace_back(path.string(), fs::relative(path, dir, ec).generic_string());
        }
    }
    std::ranges::sort(files);
    TEST_ASSERT(!files.empty(), "the corpus has .osu files");

    if(const auto rounds = getTestArg("bench")) {
        bench(files, std::max(1, Parsing::strto<i32>(*rounds)));
        return;
    }

    const auto dumpPath = getTestArg("dump");
    const f64 start = Timing::getTimeReal();
    std::vector<FileResult> results(files.size());
    {
        std::atomic<uSz> next{0};
        std::vector<Sync::jthread> workers;
        const unsigned count = std::max(1u, std::thread::hardware_concurrency());
        for(unsigned i = 0; i < count; i++) {
            workers.emplace_back([&] {
                for(uSz f; (f = next.fetch_add(1)) < files.size();) {
                    results[f] = checkFile(files[f].first, files[f].second, dumpPath.has_value());
                }
            });
        }
    }
    logRaw("  {} files in {:.2f} s", files.size(), Timing::getTimeReal() - start);

    u64 bytesNotBack = 0;
    std::map<std::string, RecordStats> totals;
    for(uSz f = 0; f < files.size(); f++) {
        if(!results[f].bytesBack) {
            if(bytesNotBack++ < 10) logRaw("  bytes not given back: {}", files[f].second);
        }
        for(const auto &[name, stats] : results[f].records) totals[name].add(stats);
    }
    TEST_ASSERT_EQ(bytesNotBack, 0, "every file's sections give back its bytes");
    for(const auto &[name, stats] : totals) {
        logRaw("  {}: {} entries, {} records, {} identical after format, {} the same record", name, stats.entries,
               stats.records, stats.identical, stats.same);
        for(const auto &[version, n] : stats.notSameByVersion) logRaw("    not the same in {}: {}", version, n);
        for(const auto &example : stats.notSame) logRaw("    {}", example);
        for(const auto &example : stats.notIdentical) logRaw("    (not identical) {}", example);
    }

    if(dumpPath) {
        std::ofstream out(*dumpPath, std::ios::binary);
        for(const auto &r : results) out << r.dump;
        logRaw("  wrote {}", *dumpPath);
    }
}

}  // namespace Mc::Tests
