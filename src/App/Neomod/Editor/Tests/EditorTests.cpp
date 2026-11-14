// Copyright (c) 2026, WH, All rights reserved.
#include "EditorTests.h"

#include "Logging.h"

#ifdef MCENGINE_TESTS
#include "BeatSnap.h"
#include "CBaseUIList.h"
#include "CBaseUINumberField.h"
#include "CBaseUIColorButton.h"
#include "CBaseUIPopupLayer.h"
#include "CBaseUIStyledButton.h"
#include "CBaseUIStyledTextbox.h"
#include "Compose.h"
#include "ContainerRanges.h"
#include "DatabaseBeatmap.h"
#include "DatabaseBeatmapTypes.h"
#include "Document.h"
#include "EditorHistory.h"
#include "EditorSession.h"
#include "EditorState.h"
#include "Deflate.h"
#include "Environment.h"
#include "File.h"
#include "HitObjects.h"
#include "KeyBindings.h"
#include "KeyboardEvent.h"
#include "MapFiles.h"
#include "MapObjects.h"
#include "Metronome.h"
#include "Osu.h"
#include "Paths.h"
#include "Playback.h"
#include "Retiming.h"
#include "SampleSets.h"
#include "SetupPanel.h"
#include "SliderCurves.h"
#include "SliderPath.h"
#include "Snapping.h"
#include "TestMacros.h"
#include "TimeDisplay.h"
#include "Timeline.h"
#include "Timing.h"
#include "TimingPanel.h"
#include "Verify.h"
#include "crypto.h"

#include "fmt/format.h"
#include "fmt/ranges.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {
using TimingPoint = neomod::BeatmapFile::TimingPoint;
using Cue = neomod::HitObject::SoundCue;

TimingPoint uninherited(f64 time, f64 beatLength) {
    TimingPoint point;
    point.time = time;
    point.beatLength = beatLength;
    return point;
}

Database::MapLocation location(std::string file) {
    Database::MapLocation where;
    where.folder = "set";
    where.file = std::move(file);
    return where;
}

class EditorTests {
    NOCOPY_NOMOVE(EditorTests)
   public:
    explicit EditorTests(std::string_view filter) : filter(filter) {}
    ~EditorTests() = default;

    bool run() {
        if(this->wants("beatsnap")) this->beatSnap();
        if(this->wants("divisor")) this->divisors();
        if(this->wants("time")) this->timeFormat();
        if(this->wants("session")) this->session();
        if(this->wants("files")) this->files();
        if(this->wants("cues")) this->cues();
        if(this->wants("document")) this->document();
        if(this->wants("editing")) this->editing();
        if(this->wants("compose")) this->compose();
        if(this->wants("breaks")) this->breaks();
        if(this->wants("clipboard")) this->clipboard();
        if(this->wants("snapping")) this->snapping();
        if(this->wants("patching")) this->patching();
        if(this->wants("sliders")) this->sliders();
        if(this->wants("setup")) this->setup();
        if(this->wants("timing")) this->timing();
        if(this->wants("verify")) this->verify();
        if(this->wants("history")) this->history();
        // only when asked for: it reads every map in a directory
        if(this->filter == "corpus") this->corpus();
        TEST_PRINT_RESULTS("editor tests");
        return m_failures == 0;
    }

   private:
    [[nodiscard]] bool wants(std::string_view name) const {
        return this->filter.empty() || name.find(this->filter) != std::string_view::npos;
    }

    // the expected values come from a model of snapping and stepping in doubles, truncated to whole milliseconds (where
    // ticks are in existing maps)
    void beatSnap() {
        TEST_SECTION("beatsnap");
        const std::vector points{uninherited(1000, 500), uninherited(9000, 400)};
        const BeatSnap snap{points};
        TEST_ASSERT_EQ(snap.snap(1260, 4, 1260), 1250, "the nearest tick of the section");
        TEST_ASSERT_EQ(snap.snap(1100, 3, 1100), 1166, "ticks fall on whole milliseconds, truncated");
        TEST_ASSERT_EQ(snap.snap(9150, 4, 9150), 9200, "a tie goes to the later tick");
        TEST_ASSERT_EQ(snap.snap(700, 4, 700), 750, "before the first point, its grid goes on");
        TEST_ASSERT_EQ(snap.step(1000, 1, 4, false), 1125, "a step is a tick");
        TEST_ASSERT_EQ(snap.step(1000, 4, 4, false), 1500, "four of them");
        TEST_ASSERT_EQ(snap.step(1000, 1, 3, false), 1166, "a step moves past the tick it starts on");
        TEST_ASSERT_EQ(snap.step(1166, -1, 3, false), 1000, "back too");
        TEST_ASSERT_EQ(snap.step(1000, 1, 4, true), 1500, "while playing, a step is a beat");
        TEST_ASSERT_EQ(snap.step(8900, 1, 1, false), 9500, "a step across a point keeps the grid it starts in");

        const BeatSnap fast{std::vector{uninherited(0, 200)}};
        TEST_ASSERT_EQ(fast.step(1000, 1, 4, true), 1400, "while playing, enough beats to clear 250 ms");
        const BeatSnap odd{std::vector{uninherited(337, 333.333333333333)}};
        TEST_ASSERT_EQ(odd.snap(1337, 4, 1337), 1336, "a tick at x.999... truncates to the millisecond before");

        const BeatSnap none{std::span<const TimingPoint>{}};
        TEST_ASSERT_EQ(none.step(1100, 1, 4, false), 1250, "without timing, a beat is 1000 ms");
        TEST_ASSERT_EQ(none.step(1100, -1, 4, false), 750, "back too");
        TEST_ASSERT_EQ(none.step(100, -1, 4, false), 0, "not before 0");

        // beat lengths a file can say: none (NaN, infinite) snaps nothing, the shortest and longest stay defined
        const BeatSnap nan{std::vector{uninherited(0, 500), uninherited(1000, std::numeric_limits<f64>::quiet_NaN())}};
        TEST_ASSERT(nan.snap(1260.7, 4, 1260.7) == 1260 && nan.step(1260, 1, 1, false) == 2260,
                    "a section without a tempo: times as they are, steps of 1000 ms");
        const BeatSnap infinite{std::vector{uninherited(1000, std::numeric_limits<f64>::infinity())}};
        TEST_ASSERT_EQ(infinite.snap(1260.7, 4, 1260.7), 1260, "...an infinite beat");
        const BeatSnap tiny{std::vector{uninherited(2.0e9, 0.0001)}};
        TEST_ASSERT(
            std::abs(tiny.snap(1260.0, 4, 1260.0) - 1260) <= 1 && std::abs(tiny.step(1260, 1, 4, false) - 1260) <= 1,
            "a first section too short to walk back from late in the song (a tick every millisecond)");
        const BeatSnap huge{std::vector{uninherited(0, 1.0e300)}};
        TEST_ASSERT(huge.snap(1260.0, 4, 1260.0) == 0 && huge.step(1260, 1, 4, false) >= 1260,
                    "...one so long a step goes past the song");
    }

    void divisors() {
        TEST_SECTION("divisor");
        TEST_ASSERT_EQ(BeatSnap::clampDivisor(8), 8, "a divisor stays");
        TEST_ASSERT_EQ(BeatSnap::clampDivisor(5), 4, "5 goes to 4");
        TEST_ASSERT_EQ(BeatSnap::clampDivisor(7), 6, "7 goes to 6");
        TEST_ASSERT_EQ(BeatSnap::clampDivisor(10), 12, "9 to 11 go to 12");
        TEST_ASSERT_EQ(BeatSnap::clampDivisor(14), 16, "13 to 15 go to 16");
        TEST_ASSERT_EQ(BeatSnap::clampDivisor(17), 1, "past 16 goes to 1");
        TEST_ASSERT_EQ(BeatSnap::clampDivisor(0), 1, "below 1 goes to 1");
    }

    void timeFormat() {
        TEST_SECTION("time");
        TEST_ASSERT_EQ(TimeDisplay::format(0), "00:00:000", "zero");
        TEST_ASSERT_EQ(TimeDisplay::format(61234), "01:01:234", "minutes, seconds, milliseconds");
        TEST_ASSERT_EQ(TimeDisplay::format(-500), "-00:00:500", "before the song");
        TEST_ASSERT_EQ(TimeDisplay::format(3600001), "60:00:001", "minutes don't wrap");

        const auto parsed = [](std::string_view text) {
            const auto to = TimeDisplay::parse(text);
            return to ? fmt::format("{} ({})", to->time, fmt::join(to->combos, ",")) : std::string{"none"};
        };
        TEST_ASSERT(
            parsed("01:01:234") == "61234 ()" && parsed("01:01") == "61000 ()" && parsed("1:02.5") == "62005 ()",
            "mm:ss:mmm, mm:ss, '.' before the milliseconds (read as a number)");
        TEST_ASSERT(parsed("12345") == "12345 ()" && parsed("-500") == "-500 ()" && parsed("-00:00:500") == "-500 ()",
                    "milliseconds, and times before the song");
        TEST_ASSERT_EQ(parsed("  00:12:345 (1,2, 3) - check the spacing "), "12345 (1,2,3)",
                       "osu!'s timestamp: combo numbers, a comment after \" - \"");
        TEST_ASSERT(parsed("120:00:000") == "7200000 ()" && parsed("9999:59:999") == "599999999 ()" &&
                        parsed("10000:00:000") == "none",
                    "minutes of up to four digits");
        TEST_ASSERT(parsed("00:61:000") == "none" && parsed("00:01:0000") == "none" &&
                        parsed("00:01:000 (1,") == "none" && parsed("00:01:000 (a)") == "none" &&
                        parsed("01:") == "none" && parsed("") == "none",
                    "nothing for what isn't a time (seconds past 59, four digits of milliseconds, a list cut off)");
    }

    void session() {
        TEST_SECTION("session");
        EditorSession saved;
        saved.opened(location("a.osu"));
        saved.views.front().time = 1234;
        saved.views.front().zoom = 2.5f;
        saved.views.front().divisor = 3;
        saved.views.front().spacing = 1.25f;
        saved.views.front().gridSize = 8;
        saved.opened(location("b.osu"));
        TEST_ASSERT_EQ(saved.views.size(), (uSz)2, "a map opened goes first");
        TEST_ASSERT(saved.views.front().location == location("b.osu"), "...where the open one is");

        const std::vector<u8> bytes = saved.write();
        const EditorSession read = EditorSession::read(bytes);
        TEST_ASSERT(read.open == saved.open && read.views == saved.views, "the session reads back as written");

        saved.opened(location("a.osu"));
        TEST_ASSERT(saved.views.size() == 2 && saved.views.front().time == 1234,
                    "a map opened again has its earlier view");

        for(i32 i = 0; i < 60; i++) saved.opened(location(fmt::format("{}.osu", i)));
        TEST_ASSERT_EQ(saved.views.size(), EditorSession::MAX_VIEWS, "the last maps' views, as many as kept");

        EditorSession odd;
        odd.opened(location("c.osu"));
        odd.views.front().divisor = 5;
        TEST_ASSERT_EQ(EditorSession::read(odd.write()).views.front().divisor.value_or(0), 4,
                       "a divisor read is one of the divisors");
        odd.views.front().spacing = 9.f;
        odd.views.front().gridSize = 10;
        TEST_ASSERT(EditorSession::read(odd.write()).views.front().spacing == 6.f &&
                        EditorSession::read(odd.write()).views.front().gridSize == 8,
                    "...a spacing from 0.1 to 6, a grid size one of the grid's");

        EditorSession drafted;
        drafted.opened(location("d.osu"));
        drafted.draft = {.base = crypto::hash::md5("the file"), .bytes = {1, 2, 3}};
        TEST_ASSERT(EditorSession::read(drafted.write()).draft == drafted.draft,
                    "the open map's unsaved changes are in the session");

        const std::vector<u8> v2{2, 0, 0};
        const EditorSession old = EditorSession::read(v2);
        TEST_ASSERT(!old.open && old.views.empty(), "another version is a fresh session");
        const std::span<const u8> cut{bytes.data(), bytes.size() - 1};
        TEST_ASSERT(EditorSession::read(cut).views.empty(), "a cut-off session is a fresh one");

        // anything reads as a session
        u32 seed = 12345;
        bool sane = true;
        for(i32 run = 0; run < 200; run++) {
            std::vector<u8> garbage(run % 64);
            for(u8 &b : garbage) b = (u8)((seed = seed * 1103515245u + 12345u) >> 16);
            if(!garbage.empty()) garbage[0] = 3;
            const EditorSession s = EditorSession::read(garbage);
            sane &= s.views.size() <= EditorSession::MAX_VIEWS &&
                    (!s.open || (!s.views.empty() && s.views.front().location == *s.open));
        }
        TEST_ASSERT(sane, "garbage reads as a session that holds together");

        const EditorSession::Draft draft{.base = crypto::hash::md5("the file"), .bytes = {4, 5, 6}};
        TEST_ASSERT(EditorSession::putDraft(location("e.osu"), draft), "another map's draft goes into its file");
        TEST_ASSERT(!EditorSession::draftOf(location("f.osu")), "a map without a draft file has none");
        TEST_ASSERT(
            EditorSession::draftOf(location("e.osu")) == draft && EditorSession::draftOf(location("e.osu")) == draft,
            "another map's draft comes back from its file, which stays");
        EditorSession::dropDraft(location("e.osu"));
        TEST_ASSERT(!EditorSession::draftOf(location("e.osu")), "...until it's dropped");

        EditorSession renamed;
        renamed.opened(location("new.osu"));
        renamed.opened(location("old.osu"));
        renamed.views.front().time = 4321;
        TEST_ASSERT(EditorSession::putDraft(location("new.osu"), draft), "a draft where a map is renamed to");
        renamed.moved(location("new.osu"));
        TEST_ASSERT(renamed.open == location("new.osu") && renamed.views.size() == 1 &&
                        renamed.views.front().location == location("new.osu") && renamed.views.front().time == 4321,
                    "a renamed map stays open with its view, in place of what another map left there");
        TEST_ASSERT(!EditorSession::draftOf(location("new.osu")), "...and of its draft");

        const Database::MapLocation stable{.root = Database::MapRoot::Peppy, .folder = "Pack/1 A - T", .file = "x.osu"};
        const Database::MapLocation copy = Database::standIn(stable);
        TEST_ASSERT(copy.root == Database::MapRoot::Neomod && copy.folder == "Pack_1 A - T" && copy.file == "x.osu",
                    "a map of the songs folder is in maps/, in the folder named after its folder");
        TEST_ASSERT(EditorSession::putDraft(stable, draft) && EditorSession::draftOf(stable) == draft,
                    "...its draft is its own");
        TEST_ASSERT(EditorSession::draftOf(copy) == draft, "...and its copy's once its set was copied there");
        EditorSession::dropDraft(copy);
        TEST_ASSERT(!EditorSession::draftOf(stable), "...one file for both");
        EditorSession followed;
        followed.opened(stable);
        followed.views.front().time = 777;
        followed.opened(location("other.osu"));
        followed.opened(copy);
        TEST_ASSERT(
            followed.views.size() == 2 && followed.views.front().location == copy && followed.views.front().time == 777,
            "...as is its view");
    }

    void files() {
        TEST_SECTION("files");
        auto nameOf = [](std::string_view metadata) {
            const std::string text = fmt::format(
                "osu file format v14\r\n\r\n[General]\r\nAudioFilename: Some Song.mp3\r\n\r\n[Metadata]\r\n{}\r\n",
                metadata);
            return MapFiles::nameFor(neomod::BeatmapFile{text});
        };
        TEST_ASSERT_EQ(nameOf("Title:Song\r\nArtist:Band\r\nCreator:me\r\nVersion:Hard"), "Band - Song (me) [Hard].osu",
                       "a map's file is named after its metadata");
        TEST_ASSERT_EQ(
            nameOf(
                "Title:Song\r\nTitleUnicode:歌\r\nArtist:Band\r\nArtistUnicode:バンド\r\nCreator:me\r\nVersion:Hard"),
            "Band - Song (me) [Hard].osu", "...the romanised names");
        TEST_ASSERT_EQ(nameOf("Title:Song\r\nCreator:me\r\nVersion:Hard"), "Some Song (me) [Hard].osu",
                       "the audio file's name without an artist");
        TEST_ASSERT_EQ(nameOf("Title:Song\r\nArtist:Band"), "Band - Song.osu",
                       "nothing for an empty creator or version");
        TEST_ASSERT_EQ(nameOf("Title:a/b\\c:d*e?f\"g<h>i|j\r\nArtist:Band\r\nCreator:me\r\nVersion:x"),
                       "Band - abcdefghij (me) [x].osu", "the characters file names can't have taken out");
        std::string accents;
        for(i32 i = 0; i < 100; i++) accents += "é";
        std::string cut = "Band - ";
        for(i32 i = 0; i < 96; i++) cut += "é";
        TEST_ASSERT_EQ(nameOf(fmt::format("Title:{}\r\nArtist:Band", accents)), cut + ".osu",
                       "a long name is cut before 200 bytes, between characters");

        const std::string head =
            "osu file format v14\r\n\r\n[General]\r\nAudioFilename: a.mp3\r\n\r\n[Editor]\r\nBookmarks: "
            "500,9000\r\n\r\n"
            "[Metadata]\r\nTitle:Song\r\nArtist:Band\r\nCreator:me\r\nVersion:Hard\r\nBeatmapID:123\r\nBeatmapSetID:"
            "45\r\n";
        const std::string body =
            "\r\n[Difficulty]\r\nApproachRate:9\r\n\r\n[Events]\r\n0,0,\"bg.jpg\",0,0\r\n//Break Periods\r\n";
        const std::string timing =
            "\r\n[TimingPoints]\r\n0,500,4,2,0,100,1,0\r\n\r\n[Colours]\r\nCombo1 : 255,0,0\r\n\r\n[HitObjects]\r\n";
        const std::string objects = "256,192,1000,5,0,0:0:0:0:\r\n256,192,9000,1,0,0:0:0:0:\r\n";
        const std::string map = head + body + "2,1500,8500\r\n" + timing + objects;
        auto text = [](std::span<const u8> bytes) { return std::string{bytes.begin(), bytes.end()}; };
        const std::string newHead =
            "osu file format v14\r\n\r\n[General]\r\nAudioFilename: a.mp3\r\n\r\n[Editor]\r\nBookmarks: "
            "500,9000\r\n\r\n"
            "[Metadata]\r\nTitle:Song\r\nArtist:Band\r\nCreator:me\r\nVersion:Hard "
            "(copy)\r\nBeatmapID:0\r\nBeatmapSetID:45\r\n";
        const std::vector<u8> bytes{map.begin(), map.end()};
        TEST_ASSERT_EQ(text(MapFiles::newDifficulty(bytes, "Hard (copy)", false)),
                       fmt::format("{}{}2,1500,8500\r\n{}{}", newHead, body, timing, objects),
                       "a copy of a difficulty is its content with another name, without an online id of its own");
        TEST_ASSERT_EQ(text(MapFiles::newDifficulty(bytes, "Hard (copy)", true)),
                       fmt::format("{}{}{}", newHead, body, timing), "...an empty one without its objects and breaks");

        const std::string first = MapFiles::newSet("曲 01.mp3", "me");
        TEST_ASSERT_EQ(
            first,
            "osu file format v14\r\n\r\n[General]\r\nAudioFilename: 曲 01.mp3\r\nAudioLeadIn: 0\r\n"
            "PreviewTime: -1\r\nCountdown: 1\r\nSampleSet: Normal\r\nStackLeniency: 0.7\r\nMode: 0\r\n"
            "LetterboxInBreaks: 0\r\nWidescreenStoryboard: 0\r\n\r\n[Editor]\r\nBeatDivisor: 4\r\n\r\n[Metadata]\r\n"
            "Title:01\r\n"
            "TitleUnicode:曲 01\r\nArtist:\r\nArtistUnicode:\r\nCreator:me\r\nVersion:\r\nSource:\r\nTags:\r\n"
            "BeatmapID:0\r\nBeatmapSetID:-1\r\n\r\n[Difficulty]\r\nHPDrainRate:5\r\nCircleSize:5\r\n"
            "OverallDifficulty:5\r\nApproachRate:5\r\nSliderMultiplier:1.4\r\nSliderTickRate:1\r\n\r\n"
            "[Events]\r\n//Background and Video events\r\n//Break Periods\r\n"
            "//Storyboard Layer 0 (Background)\r\n//Storyboard Layer 1 (Fail)\r\n//Storyboard Layer 2 (Pass)\r\n"
            "//Storyboard Layer 3 (Foreground)\r\n//Storyboard Sound Samples\r\n\r\n[TimingPoints]\r\n"
            "0,500,4,1,0,100,1,0\r\n\r\n\r\n[HitObjects]\r\n",
            "a new set's first map is osu!'s fresh one, titled after its audio file, with a timing point");
        TEST_ASSERT_EQ(MapFiles::nameFor(neomod::BeatmapFile{first}), "曲 01 (me).osu",
                       "...named after the audio file");
        const std::string path = Mc::Paths::cache() + "/editor-tests-new.osu";
        File(path, File::MODE::WRITE).writeLine(first, false);
        DatabaseBeatmap read(path, Mc::Paths::cache() + "/", DatabaseBeatmap::BeatmapType::NEOMOD_DIFFICULTY);
        const auto result = read.loadMetadata(false);
        TEST_ASSERT(!result && read.getTitleUnicode() == "曲 01" && read.getTitleLatin() == "01" &&
                        read.getCreator() == "me" && read.getSetID() == -1 && read.getMostCommonBPM() == 120,
                    "...which the game reads as an unsubmitted map at 120 BPM");

        // a file taken into a folder: the one that's there, else a copy under its name or a free one
        const std::string folder = Mc::Paths::cache() + "/editor-tests-folder/";
        const std::string outside = Mc::Paths::cache() + "/editor-tests-outside/";
        Environment::createDirectory(folder);
        Environment::createDirectory(outside);
        const auto write = [](const std::string &file, std::string_view content) {
            File(file, File::MODE::WRITE).writeLine(content, false);
        };
        write(outside + "kick.wav", "new");
        TEST_ASSERT(MapFiles::takeIn(folder, outside + "kick.wav") == "kick.wav" &&
                        Environment::fileExists(folder + "kick.wav"),
                    "a file taken into a folder is a copy under its name");
        TEST_ASSERT_EQ(MapFiles::takeIn(folder, folder + "kick.wav").value_or(""), "kick.wav",
                       "...the folder's own file is itself");
        TEST_ASSERT(MapFiles::takeIn(folder, outside + "kick.wav") == "kick (2).wav" &&
                        Environment::fileExists(folder + "kick (2).wav"),
                    "...and one whose name is taken there comes in under a free one");
        TEST_ASSERT(!MapFiles::takeIn(folder, outside + "none.wav"), "a file that isn't there is none");
        for(const char *file : {"kick.wav", "kick (2).wav"}) Environment::deleteFile(folder + file);
        Environment::deleteFile(outside + "kick.wav");

        // the samples a map has, by their names
        const std::vector<std::string> samples{"soft-hitclap2.ogg",
                                               "Soft-HitClap2.WAV",
                                               "normal-hitnormal.mp3",
                                               "drum-slidertick12.wav",
                                               "soft-hitclap1.wav",
                                               "soft-hitclapx.wav",
                                               "kick.wav",
                                               "soft-hitclap2.txt",
                                               "audio.mp3"};
        TEST_ASSERT_EQ(SampleSets::fileFor(samples, 1, 3, 2), "Soft-HitClap2.WAV",
                       "a sample set's file for an index, in any case, a .wav before an .ogg");
        TEST_ASSERT(SampleSets::fileFor(samples, 0, 0, 1) == "normal-hitnormal.mp3" &&
                        SampleSets::fileFor(samples, 2, 6, 12) == "drum-slidertick12.wav",
                    "...the first index's without a number");
        TEST_ASSERT(SampleSets::fileFor(samples, 1, 3, 1).empty() && SampleSets::fileFor(samples, 0, 0, 2).empty(),
                    "...none where the folder has none (a 1 in a name is no index)");
        TEST_ASSERT(SampleSets::indicesIn(samples) == std::vector<i32>({1, 2, 12}),
                    "the indices a folder has samples for");
    }

    void cues() {
        TEST_SECTION("cues");
        std::vector<Cue> cues;
        for(const i32 time : {100, 200, 200, 300}) {
            cues.push_back({.timeMS = time, .endTimeMS = time, .kind = Cue::Kind::HIT, .samples = {}, .rawPos = {}});
        }
        TEST_ASSERT_EQ(Playback::crossed(cues, 100, 200).size(), (uSz)2,
                       "after the last frame's time, up to this one's");
        TEST_ASSERT_EQ(Playback::crossed(cues, 99, 100).size(), (uSz)1, "one at the time itself");
        TEST_ASSERT(Playback::crossed(cues, 0, 99).empty(), "none before");
        TEST_ASSERT(Playback::crossed(cues, 300, 400).empty(), "none after");
    }

    void document() {
        TEST_SECTION("document");
        const std::string path = Mc::Paths::cache() + "/editor-tests.osu";
        const auto write = [&](std::string_view text) { File(path, File::MODE::WRITE).writeLine(text, false); };

        write(
            "osu file format v14\n\n[General]\nPreviewTime: 1500\n\n[Editor]\nBookmarks: 3000,1000\nBeatDivisor: 3\n"
            "TimelineZoom: 2.5\n\n[Difficulty]\nSliderMultiplier:1.4\nSliderTickRate:1\n\n[TimingPoints]\n"
            "2000,-100,4,2,0,100,0,1\n0,500,4,2,0,100,1,0\n2000,400,3,2,0,100,1,0\n\n[HitObjects]\n"
            "100,100,1000,1,0,0:0:0:0:\n64,192,2000,2,0,L|344:192,1,280\n");
        auto [document, error] = Document::read(location("editor-tests.osu"), path);
        TEST_ASSERT(document != nullptr, "a map reads");
        if(document) {
            const auto points = document->getTimingPoints();
            TEST_ASSERT(points.size() == 3 && points[0].time == 0 && points[1].uninherited && !points[2].uninherited,
                        "timing points in time order, an uninherited one first at the same time");
            TEST_ASSERT(document->getBookmarks().size() == 2 && document->getBookmarks()[0] == 1000,
                        "bookmarks in time order");
            TEST_ASSERT_EQ(document->getPreviewTime(), 1500, "the preview time");
            TEST_ASSERT_EQ(document->getBeatDivisor().value_or(0), 3, "the divisor it was edited with");
            TEST_ASSERT_EQ(document->getTimelineZoom().value_or(0.f), 2.5f, "and the zoom");

            const MapObjects objects{*document};
            TEST_ASSERT_EQ(objects.objects.size(), (uSz)2, "its objects");
            // the circle; the slider head, slide, the tick in the middle between its two beats (400 ms each), its end
            TEST_ASSERT_EQ(objects.getSounds().cues.size(), (uSz)5, "the sounds a perfect play makes");
        }

        write(
            "osu file format v14\n\n[General]\nMode: 1\n\n[TimingPoints]\n0,500,4,2,0,100,1,0\n\n[HitObjects]\n"
            "100,100,1000,1,0,0:0:0:0:\n");
        auto [taiko, taikoError] = Document::read(location("editor-tests.osu"), path);
        TEST_ASSERT(!taiko && taikoError.errc == neomod::Primitives::LoadError::NON_STD_GAMEMODE,
                    "another mode's map doesn't");
        Environment::deleteFile(path);
    }

    // the bytes in a document
    static std::string_view text(const Document &document) {
        return {reinterpret_cast<const char *>(document.getBytes().data()), document.getBytes().size()};
    }

    // all of one object in a map as read
    static std::string describe(const neomod::Primitives::Object &object) {
        using namespace neomod::DatabaseBeatmapTypes;
        const auto samples = [](const HITSAMPLE_BITS &s) {
            return fmt::format("{}:{}:{}:{}:{}:{}", s.hitSounds, s.normalSet, s.additionSet, s.volume, s.index,
                               s.filename);
        };
        if(const auto *h = std::get_if<HITCIRCLE>(&object)) {
            return fmt::format("circle {},{} {} #{} {}+{} {} {} {} {} line {}", h->x, h->y, h->time, h->number,
                               h->colorCounter, h->colorOffset, h->newCombo, h->colorSkip, h->isEndOfCombo,
                               samples(h->samples), h->line);
        }
        if(const auto *s = std::get_if<SLIDER>(&object)) {
            std::string out = fmt::format(
                "slider {},{} {} #{} {}+{} {} {} {} {}x{} {}/{} {} {} line {}", s->x, s->y, s->time, s->number,
                s->colorCounter, s->colorOffset, s->newCombo, s->colorSkip, s->isEndOfCombo, s->repeat, s->pixelLength,
                s->sliderTime, s->sliderTimeWithoutRepeats, (char)s->type, samples(s->hoverSamples), s->line);
            for(const vec2 point : s->points) out += fmt::format(" p{},{}", point.x, point.y);
            for(const f32 tick : s->ticks) out += fmt::format(" t{}", tick);
            for(const auto &scoring : s->scoringTimesForStarCalc) {
                out += fmt::format(" s{}:{}", scoring.time, (i32)scoring.type);
            }
            for(const auto &edge : s->edgeSamples) out += " e" + samples(edge);
            return out;
        }
        const auto &spinner = std::get<SPINNER>(object);
        return fmt::format("spinner {},{} {}-{} {} {} {} {} line {}", spinner.x, spinner.y, spinner.time,
                           spinner.endTime, spinner.newCombo, spinner.colorSkip, spinner.isEndOfCombo,
                           samples(spinner.samples), spinner.line);
    }

    // where a document and its objects differ from reading its bytes again whole (empty: nowhere)
    static std::string unlikeRead(const Document &doc, const MapObjects &objects) {
        auto [read, error] = Document::read(location("read.osu"), "", doc.getBytes());
        if(!read) return "its bytes don't read";
        const auto &were = doc.getPrimitives().objects;
        const auto &are = read->getPrimitives().objects;
        if(were.size() != are.size()) return "the number of objects";
        for(uSz i = 0; i < are.size(); i++) {
            if(describe(were[i]) != describe(are[i])) {
                return fmt::format("object {}: {} | {}", i, describe(were[i]), describe(are[i]));
            }
        }
        if(doc.getPrimitives().sampleFiles != read->getPrimitives().sampleFiles) return "the sample files";
        const MapObjects made{*read};
        const auto look = [](const MapObjects &o, uSz i) {
            const neomod::HitObject &h = *o.objects[i];
            const vec2 head = h.getRawPosAt(h.getClickTime());
            const vec2 tail = h.getRawPosAt(h.getEndTime());
            return fmt::format("{} {}-{} {},{} {},{} stack {} #{} {}+{} {} from {} line {}", (i32)h.getType(),
                               h.getClickTime(), h.getEndTime(), head.x, head.y, tail.x, tail.y, h.getStack(),
                               h.getComboNumber(), h.getColorCounter(), h.getColorOffset(), h.isEndOfCombo(),
                               h.getComboStartTime(), h.getLine());
        };
        if(objects.objects.size() != made.objects.size()) return "the number of objects made";
        for(uSz i = 0; i < made.objects.size(); i++) {
            if(look(objects, i) != look(made, i)) {
                return fmt::format("made {}: {} | {}", i, look(objects, i), look(made, i));
            }
            if(objects.byEndTime[i].object->getLine() != made.byEndTime[i].object->getLine()) {
                return fmt::format("by end time {}", i);
            }
        }
        const auto &cues = objects.getSounds().cues;
        const auto &madeCues = made.getSounds().cues;
        if(cues.size() != madeCues.size()) return "the number of sounds";
        for(uSz i = 0; i < cues.size(); i++) {
            const Cue &a = cues[i], &b = madeCues[i];
            if(a.timeMS != b.timeMS || a.endTimeMS != b.endTimeMS || a.kind != b.kind ||
               a.samples.hitSounds != b.samples.hitSounds || a.samples.normalSet != b.samples.normalSet ||
               a.samples.additionSet != b.samples.additionSet || a.samples.volume != b.samples.volume ||
               a.samples.index != b.samples.index || a.samples.filename != b.samples.filename || a.rawPos != b.rawPos) {
                return fmt::format("sound {}", i);
            }
        }
        return {};
    }

    void editing() {
        TEST_SECTION("editing");
        using Kind = neomod::BeatmapFile::SectionKind;
        using Action = Document::Action;
        const std::string path = Mc::Paths::cache() + "/editor-tests.osu";
        const auto write = [&](std::string_view content) { File(path, File::MODE::WRITE).writeLine(content, false); };
        const auto replaced = [](std::string s, std::string_view from, std::string_view to) {
            return s.replace(s.find(from), from.size(), to);
        };

        // as other programs leave files: a byte order mark, CRLF and LF, a comment, an unknown section, no line break
        // after the last line
        const std::string original =
            "\xEF\xBB\xBF"
            "osu file format v14\r\n\r\n[General]\r\nAudioFilename: a.mp3\r\nPreviewTime: 100\r\n// a comment\r\n\r\n"
            "[Editor]\nDistanceSpacing: 1\n\n[Metadata]\r\nTitle:x\r\n[Custom]\r\nwhatever\r\n\r\n[TimingPoints]\r\n"
            "0,500,4,2,0,100,1,0\r\n\r\n[HitObjects]\r\n256,192,1000,1,0,0:0:0:0:";
        write(original);
        auto [doc, error] = Document::read(location("editor-tests.osu"), path);
        TEST_ASSERT(doc != nullptr, "a map reads");
        if(!doc) return;
        TEST_ASSERT_EQ(text(*doc), original, "a document is its file's lines: written, it's what was read");
        TEST_ASSERT(!doc->isChanged(), "...and unchanged");
        TEST_ASSERT_EQ(doc->getIdAt(1), doc->getLines()[0].id, "line 1 is the first line (after the byte order mark)");

        const auto set = [&](Kind kind, std::string_view key, std::optional<std::string_view> value) {
            return doc->change(Action::MAP_SETTINGS, [&](Document::Change &c) { c.setValue(kind, key, value); });
        };

        set(Kind::EDITOR, "DistanceSpacing", "1.4");
        const std::string spacing = replaced(original, "DistanceSpacing: 1\n", "DistanceSpacing: 1.4\n");
        TEST_ASSERT_EQ(text(*doc), spacing, "a key's line changes in place, keeping its line break");
        TEST_ASSERT(doc->isChanged() && doc->canUndo() && !doc->canRedo(), "...a change, which can be undone");
        doc->undo();
        TEST_ASSERT_EQ(text(*doc), original, "undo gives back the bytes");
        TEST_ASSERT(!doc->isChanged() && doc->canRedo(), "...unchanged again, which can be redone");
        doc->redo();
        TEST_ASSERT_EQ(text(*doc), spacing, "redo does it again");
        doc->undo();

        set(Kind::EDITOR, "Bookmarks", "500,1500");
        TEST_ASSERT_EQ(text(*doc),
                       replaced(original, "DistanceSpacing: 1\n", "DistanceSpacing: 1\nBookmarks: 500,1500\n"),
                       "a key the section doesn't have goes after its last line that says something, its line break "
                       "the section's");
        TEST_ASSERT(doc->getBookmarks().size() == 2 && doc->getBookmarks()[1] == 1500, "...and is read");
        doc->undo();

        set(Kind::GENERAL, "Countdown", "0");
        TEST_ASSERT_EQ(text(*doc), replaced(original, "PreviewTime: 100\r\n", "PreviewTime: 100\r\nCountdown: 0\r\n"),
                       "...before a comment after it");
        doc->undo();

        set(Kind::GENERAL, "PreviewTime", std::nullopt);
        TEST_ASSERT_EQ(text(*doc), replaced(original, "PreviewTime: 100\r\n", ""), "no value removes the key's lines");
        TEST_ASSERT_EQ(doc->getPreviewTime(), -1, "...and it's gone from what's read");
        doc->undo();

        set(Kind::DIFFICULTY, "SliderTickRate", "2");
        TEST_ASSERT_EQ(text(*doc),
                       replaced(original, "[TimingPoints]", "[Difficulty]\r\nSliderTickRate:2\r\n\r\n[TimingPoints]"),
                       "a section the file doesn't have goes in the usual order (an unknown one counts as the one "
                       "before it)");
        doc->undo();
        TEST_ASSERT_EQ(text(*doc), original, "...and undo takes it out again");

        doc->change(Action::MAP_SETTINGS,
                    [&](Document::Change &c) { c.insert(doc->getLines().size(), "64,64,2000,1,0,0:0:0:0:"); });
        TEST_ASSERT_EQ(text(*doc), original + "\r\n64,64,2000,1,0,0:0:0:0:",
                       "after a last line without a line break: it gets one, the new line is the last without");
        TEST_ASSERT_EQ(doc->getPrimitives().getNumObjects<DBType::HITCIRCLE>(), u32{2}, "...and the game reads it");
        doc->undo();
        TEST_ASSERT_EQ(text(*doc), original, "...undone byte for byte");
        doc->change(Action::MAP_SETTINGS, [&](Document::Change &c) {
            c.insert(doc->getLines().size(),
                     std::vector<std::string>{"64,64,2000,1,0,0:0:0:0:", "96,96,3000,1,0,0:0:0:0:"});
        });
        TEST_ASSERT_EQ(text(*doc), original + "\r\n64,64,2000,1,0,0:0:0:0:\r\n96,96,3000,1,0,0:0:0:0:",
                       "...several new lines at once too");
        doc->undo();
        doc->change(Action::MAP_SETTINGS,
                    [&](Document::Change &c) { c.insert(doc->getLines().size(), "64,64,500,1,0,0:0:0:0:"); });
        TEST_ASSERT_EQ(
            text(*doc),
            replaced(original, "256,192,1000,1,0,0:0:0:0:", "64,64,500,1,0,0:0:0:0:\r\n256,192,1000,1,0,0:0:0:0:\r\n"),
            "...an object placed before others takes a line break along");
        TEST_ASSERT_EQ(doc->getPrimitives().getNumObjects(), u32{2}, "...and both objects are read");
        doc->undo();
        TEST_ASSERT_EQ(text(*doc), original, "...undone byte for byte");

        // lines far apart, in any order, one twice
        std::vector<Document::Id> apart;
        for(const auto &line : doc->getLines()) {
            if(line.text.starts_with("whatever") || line.text.starts_with("PreviewTime") ||
               line.text.starts_with("Title")) {
                apart.insert(apart.begin(), line.id);
            }
        }
        apart.push_back(apart.front());
        doc->change(Action::MAP_SETTINGS, [&](Document::Change &c) { c.remove(apart); });
        const std::string removed =
            replaced(replaced(replaced(original, "PreviewTime: 100\r\n", ""), "Title:x\r\n", ""), "whatever\r\n", "");
        TEST_ASSERT_EQ(text(*doc), removed, "lines removed at once");
        doc->undo();
        TEST_ASSERT_EQ(text(*doc), original, "...undone byte for byte");
        doc->redo();
        TEST_ASSERT_EQ(text(*doc), removed, "...and redone");
        doc->undo();

        Document::Id spacingLine{0};
        for(const auto &line : doc->getLines()) {
            if(line.text.starts_with("DistanceSpacing")) spacingLine = line.id;
        }
        const u32 revision = doc->getRevision();
        for(int i = 0; i < 5; i++) {
            doc->change(
                Action::MAP_SETTINGS,
                [&](Document::Change &c) { c.replace(spacingLine, fmt::format("DistanceSpacing: {}", i + 2)); }, 42);
        }
        TEST_ASSERT_EQ(doc->getRevision(), revision + 5, "every change makes a revision");
        TEST_ASSERT_EQ(text(*doc), replaced(original, "DistanceSpacing: 1\n", "DistanceSpacing: 6\n"),
                       "a gesture's changes");
        doc->undo();
        TEST_ASSERT_EQ(text(*doc), original, "...are one step");
        doc->redo();
        TEST_ASSERT_EQ(text(*doc), replaced(original, "DistanceSpacing: 1\n", "DistanceSpacing: 6\n"),
                       "...redone as its last one");
        doc->undo();

        set(Kind::EDITOR, "DistanceSpacing", "1.4");
        doc->undo();
        TEST_ASSERT(!set(Kind::EDITOR, "DistanceSpacing", "1") && doc->canRedo(),
                    "a step that changes nothing isn't one, and leaves what can be redone");

        TEST_ASSERT(!set(Kind::GENERAL, "Mode", "1") && text(*doc) == original &&
                        doc->getError().errc == neomod::Primitives::LoadError::NON_STD_GAMEMODE,
                    "an edit the game can't read isn't made");
        TEST_ASSERT(!set(Kind::EDITOR, "DistanceSpacing", "1") && !doc->getError(),
                    "...which the next change, made or not, no longer reports");
        const auto dragSpacing = [&](std::string_view value, bool unreadable) {
            return doc->drag(
                Action::MAP_SETTINGS,
                [&](Document::Change &c) {
                    c.setValue(Kind::EDITOR, "DistanceSpacing", value);
                    if(unreadable) c.setValue(Kind::GENERAL, "Mode", "1");
                },
                77);
        };
        dragSpacing("3", false);
        TEST_ASSERT(
            !dragSpacing("4", true) && text(*doc) == replaced(original, "DistanceSpacing: 1\n", "DistanceSpacing: 3\n"),
            "a drag's update the game can't read leaves the drag where its last one was");
        doc->undo();
        TEST_ASSERT_EQ(text(*doc), original, "...its step");

        set(Kind::EDITOR, "DistanceSpacing", "2");
        doc->undo();
        const u32 unsaved = doc->getRevision();
        const Document::EditorSettings settings{.zoom = 1.5f, .divisor = 4, .spacing = 1.f, .gridSize = 32};
        const std::vector<u8> written = doc->getBytesWith(settings);
        TEST_ASSERT_EQ(std::string(written.begin(), written.end()),
                       replaced(original, "DistanceSpacing: 1\n",
                                "DistanceSpacing: 1\nBeatDivisor: 4\nTimelineZoom: 1.5\nGridSize: 32\n"),
                       "a save's file has the editor's settings in [Editor] (a value it says already stays)");
        TEST_ASSERT(text(*doc) == original && doc->getRevision() == unsaved && doc->canRedo(),
                    "...which are no step: the content and what can be redone stay");
        doc->saved(written);
        TEST_ASSERT(!doc->isChanged() && doc->canRedo() && doc->getMD5() == crypto::hash::md5(written),
                    "saved with them, the content is unchanged and the file's md5 is the written one's");
        doc->redo();
        TEST_ASSERT(doc->isChanged(), "...a step redone after the save is a change");
        doc->undo();
        TEST_ASSERT(!doc->isChanged(), "...undone, the content is as saved again");
        write({reinterpret_cast<const char *>(written.data()), written.size()});
        TEST_ASSERT(doc->revert(path) && text(*doc) == original && !doc->isChanged() && doc->canRedo(),
                    "revert to saved with the file as saved gives back the content, without the settings: no step");
        write(original);

        set(Kind::EDITOR, "DistanceSpacing", "2");
        const std::string other = replaced(original, "Title:x", "Title:y");
        write(other);
        TEST_ASSERT(doc->revert(path) && text(*doc) == other && !doc->isChanged(),
                    "revert to saved reads the file again (changed by another program here)");
        doc->undo();
        TEST_ASSERT(
            text(*doc) == replaced(original, "DistanceSpacing: 1\n", "DistanceSpacing: 2\n") && doc->isChanged(),
            "...as a step that undo takes back, a change against the file as read now");
        const std::string unordered = original + "\r\n256,192,500,1,0,0:0:0:0:";
        write(unordered);
        TEST_ASSERT(doc->revert(path) && text(*doc) == unordered && !doc->isChanged(),
                    "...as the file has it, objects out of time order too");
        doc->undo();

        doc->saved(doc->getBytesWith(settings));
        TEST_ASSERT(!doc->isChanged(), "saved: what an unchanged document has");
        doc->undo();
        TEST_ASSERT(doc->isChanged(), "...so undoing what was saved is a change");

        const std::string draft = replaced(original, "Title:x", "Title:draft");
        write(original);
        auto [drafted, draftError] = Document::read(location("editor-tests.osu"), path,
                                                    {reinterpret_cast<const u8 *>(draft.data()), draft.size()});
        TEST_ASSERT(drafted && text(*drafted) == draft && drafted->isChanged() && !drafted->canUndo(),
                    "a draft reads as the file's changed content, without a history");
        Environment::deleteFile(path);
    }

    // the edits the compose tools make on a map: what they write, and undone, the bytes they started from
    void compose() {
        TEST_SECTION("compose");
        using HitSoundType = neomod::DatabaseBeatmapTypes::HitSoundType;
        using Tool = Compose::Tool;
        const std::string path = Mc::Paths::cache() + "/editor-compose-tests.osu";
        // 500 ms beats; at circle size 4 the circle radius is 36.48 osu!px
        const std::string original =
            "osu file format v14\r\n\r\n[General]\r\nAudioFilename: a.mp3\r\nMode: 0\r\n\r\n[Difficulty]\r\n"
            "CircleSize:4\r\nApproachRate:5\r\nSliderMultiplier:1.4\r\nSliderTickRate:1\r\n\r\n[TimingPoints]\r\n"
            "0,500,4,2,0,100,1,0\r\n\r\n[HitObjects]\r\n"
            "100,100,1000,5,0,0:0:0:0:\r\n"                              // a
            "200,100,1500,1,2,0:0:0:0:\r\n"                              // b, a whistle
            "64,192,2000,2,0,L|344:192,1,280\r\n"                        // c, a slider without edge fields
            "256,192,4000,12,0,5000,0:0:0:0:\r\n"                        // d, a spinner
            "300,300,6000,1,0,0:0:0:0:\r\n"                              // e
            "64,300,8000,2,0,L|344:300,1,280,0|8,0:0|0:0,0:0:0:0:\r\n";  // f, a slider with edge fields

        File(path, File::MODE::WRITE).writeLine(original, false);
        auto [read, error] = Document::read(location("editor-compose-tests.osu"), path);
        TEST_ASSERT(read != nullptr, "the compose map reads");
        if(!read) return;

        EditorState state;
        state.open(std::move(read));
        Document &doc = *state.document;
        Compose &compose = state.compose;
        // snapping has its own section
        const Snapping::Settings unsnapped{};
        const auto objectsText = [&] {
            const std::string_view all = text(doc);
            return all.substr(all.find("[HitObjects]\r\n") + 14);
        };
        const auto idAt = [&](i32 timeMS) {
            const MapObjects &objects = *state.objects;
            for(uSz i = 0; i < objects.objects.size(); i++) {
                if(objects.objects[i]->getClickTime() == timeMS) return objects.ids[i];
            }
            return Document::Id{0};
        };
        const auto undoAll = [&] {
            compose.endGesture();
            while(doc.canUndo()) state.undo();
        };
        const auto lineOf = [&](Document::Id id) {
            const auto object = doc.getObject(id);
            return object ? neomod::BeatmapFile::format(*object) : std::string{};
        };
        const Document::Id a = idAt(1000), b = idAt(1500), c = idAt(2000), d = idAt(4000), e = idAt(6000),
                           f = idAt(8000);
        TEST_ASSERT(a && b && c && d && e && f, "every object knows its line");

        // placing
        compose.setTool(Tool::CIRCLE);
        TEST_ASSERT(compose.getTool() == Tool::CIRCLE, "a placing tool");
        state.clock.seek(3100);
        compose.placeCircle({300.4f, 50.6f}, unsnapped);
        TEST_ASSERT_EQ(objectsText(),
                       "100,100,1000,5,0,0:0:0:0:\r\n200,100,1500,1,2,0:0:0:0:\r\n64,192,2000,2,0,L|344:192,1,280\r\n"
                       "300,51,3000,1,0,0:0:0:0:\r\n256,192,4000,12,0,5000,0:0:0:0:\r\n300,300,6000,1,0,0:0:0:0:\r\n"
                       "64,300,8000,2,0,L|344:300,1,280,0|8,0:0|0:0,0:0:0:0:\r\n",
                       "a circle at the time snapped to the divisor and the position rounded, in its place in time, "
                       "written with all five sample parts");
        undoAll();
        TEST_ASSERT_EQ(text(doc), original, "...undone byte for byte");

        state.clock.seek(1500);
        compose.placeCircle({10.f, 10.f}, unsnapped);
        TEST_ASSERT(objectsText().find("10,10,1500,1,0,0:0:0:0:\r\n") != std::string::npos &&
                        objectsText().find("200,100,1500") == std::string::npos,
                    "an object starting at exactly that time is replaced");
        undoAll();

        compose.placeCircle({600.f, 10.f}, unsnapped);
        TEST_ASSERT(!doc.canUndo(), "nothing is placed outside the field");

        compose.toggleHitSound(HitSoundType::WHISTLE);
        compose.toggleNewCombo();
        state.clock.seek(3000);
        compose.placeCircle({1.f, 2.f}, unsnapped);
        TEST_ASSERT(objectsText().find("1,2,3000,5,2,0:0:0:0:") != std::string::npos,
                    "the next object's new combo and whistle (without a selection the toggles are its)");
        TEST_ASSERT(!compose.getBrush().newCombo && compose.getBrush().hitSounds == HitSoundType::WHISTLE,
                    "...the new combo used up, the whistle kept");
        compose.toggleHitSound(HitSoundType::WHISTLE);
        state.clock.seek(5500);
        compose.placeCircle({3.f, 4.f}, unsnapped);
        TEST_ASSERT(objectsText().find("3,4,5500,5,0,0:0:0:0:") != std::string::npos,
                    "an object right after a spinner starts a combo");
        undoAll();

        state.clock.seek(1250);
        TEST_ASSERT_EQ(
            compose.previewCircle({50.f, 60.f}, unsnapped) ? compose.previewCircle({50.f, 60.f}, unsnapped)->number : 0,
            2, "the preview has the number the circle would get");
        compose.toggleNewCombo();
        TEST_ASSERT_EQ(
            compose.previewCircle({50.f, 60.f}, unsnapped) ? compose.previewCircle({50.f, 60.f}, unsnapped)->number : 0,
            1, "...a new combo's");
        compose.toggleNewCombo();

        // spinners
        compose.setTool(Tool::SPINNER);
        state.clock.seek(5500);
        compose.startSpinner();
        TEST_ASSERT(objectsText().find("256,192,5500,12,0,6000,0:0:0:0:\r\n300,300,6000,5,") != std::string::npos,
                    "a spinner from the snapped time to a beat later, with a new combo, the next object's forced");
        state.clock.seek(6800);
        compose.update();
        TEST_ASSERT(objectsText().find("256,192,5500,12,0,7500,") != std::string::npos,
                    "while it's placed its end follows the snapped playhead, a beat on");
        compose.finishSpinner();
        TEST_ASSERT(objectsText().find("256,192,5500,12,0,7000,") != std::string::npos,
                    "finished, it ends at the snapped playhead");
        state.undo();
        TEST_ASSERT_EQ(text(doc), original, "...the whole placement one step");
        compose.startSpinner();
        compose.cancelSpinner();
        TEST_ASSERT(text(doc) == original && !doc.canUndo(), "a spinner taken back leaves no step");
        state.clock.seek(5500);
        compose.startSpinner();
        compose.finishSpinner();
        TEST_ASSERT(text(doc) == original && !doc.canUndo(), "...nor does one finished where it started");
        undoAll();
        compose.setTool(Tool::SELECT);

        // selecting, deleting
        compose.select({b});
        compose.deleteSelection();
        TEST_ASSERT(objectsText().find("200,100,1500") == std::string::npos && compose.getSelection().empty(),
                    "Delete removes the selection");
        if(auto touched = state.undo()) compose.selectTouched(std::move(*touched));
        TEST_ASSERT(text(doc) == original && compose.getSelection() == std::vector<Document::Id>{b},
                    "undoing it brings it back, selected");
        compose.setTool(Tool::CIRCLE);
        TEST_ASSERT(compose.getSelection().empty(), "leaving Select clears the selection");
        compose.select({b});
        TEST_ASSERT(compose.getTool() == Tool::SELECT, "selecting goes back to Select");
        compose.toggle(a);
        compose.toggle(b);
        TEST_ASSERT(compose.getSelection() == std::vector<Document::Id>{a}, "Ctrl+click toggles");
        compose.clearSelection();

        state.clock.seek(1000);
        TEST_ASSERT(compose.objectAt({110.f, 105.f}) && compose.objectAt({110.f, 105.f})->id == a,
                    "a click on a shown object's head hits it");
        TEST_ASSERT(!compose.objectAt({300.f, 300.f}), "...not one that isn't shown yet");
        TEST_ASSERT(compose.objectAt({200.f, 192.f}) && compose.objectAt({200.f, 192.f})->id == c,
                    "a slider's body when nothing else is there");
        TEST_ASSERT(!compose.objectAt({165.f, 100.f}), "objects are hit within 0.8 of their radius");
        compose.select({b});
        TEST_ASSERT(compose.objectAt({165.f, 100.f}) && compose.objectAt({165.f, 100.f})->id == b,
                    "...a selected one within all of it, and first");
        compose.select({e});
        TEST_ASSERT(compose.objectAt({300.f, 300.f}) && compose.objectAt({300.f, 300.f})->id == e,
                    "...however far it is from the time (its ring shows)");
        compose.select({d});
        TEST_ASSERT(compose.objectAt({256.f, 192.f}) && compose.objectAt({256.f, 192.f})->id == c,
                    "...but not a spinner that isn't shown (it has no ring)");
        compose.clearSelection();
        TEST_ASSERT_EQ(compose.objectsIn(McRect{vec2{90.f, 90.f}, vec2{120.f, 20.f}}).size(), uSz{2},
                       "a box holds the shown objects whose heads are inside");
        TEST_ASSERT_EQ(compose.objectsBetween(1900.0, 4500.0).size(), uSz{2},
                       "a range on the timeline holds what starts or ends in it");
        TEST_ASSERT(compose.objectAtTime(1010.0, 20.0) && compose.objectAtTime(1010.0, 20.0)->id == a &&
                        compose.objectAtTime(1010.0, 20.0)->node == 0,
                    "a press on the timeline hits the circle nearest in time within its reach, a start");
        TEST_ASSERT(compose.objectAtTime(2990.0, 20.0) && compose.objectAtTime(2990.0, 20.0)->id == c &&
                        compose.objectAtTime(2990.0, 20.0)->node == 1,
                    "...or an end");
        TEST_ASSERT(!compose.objectAtTime(1010.0, 5.0), "...none further");
        TEST_ASSERT(compose.objectAtTime(2250.0, 5.0) && compose.objectAtTime(2250.0, 5.0)->id == c &&
                        !compose.objectAtTime(2250.0, 5.0)->node,
                    "...else what spans the time, a slider's body");

        // moving
        compose.select({a, b});
        compose.startMove();
        compose.move({10.4f, -5.f}, unsnapped);
        compose.move({20.f, 0.f}, unsnapped);
        TEST_ASSERT(lineOf(a) == "120,100,1000,5,0,0:0:0:0:" && lineOf(b) == "220,100,1500,1,2,0:0:0:0:",
                    "a drag moves the selection by whole osu!px, each update from where it started");
        compose.endGesture();
        state.undo();
        TEST_ASSERT_EQ(text(doc), original, "...as one step");
        compose.startMove();
        compose.move({10.f, 0.f}, unsnapped);
        compose.move({0.f, 0.f}, unsnapped);
        TEST_ASSERT(text(doc) == original && !doc.canUndo(), "a drag back where it started leaves no step");
        compose.endGesture();
        compose.startMove();
        compose.move({10.f, 0.f}, unsnapped);
        TEST_ASSERT(compose.canCancel(), "Escape takes a drag back first");
        compose.cancel();
        compose.move({30.f, 0.f}, unsnapped);
        TEST_ASSERT(text(doc) == original && !doc.canUndo() && compose.getSelection().size() == 2,
                    "...which ends it, the selection staying");
        compose.endGesture();
        compose.startMove();
        compose.move({10.f, 0.f}, unsnapped);
        compose.setTool(Tool::CIRCLE);
        compose.move({30.f, 0.f}, unsnapped);
        TEST_ASSERT(lineOf(a) == "110,100,1000,5,0,0:0:0:0:" && compose.getSelection().empty(),
                    "another tool ends a drag where it is");
        compose.setTool(Tool::SELECT);
        compose.endGesture();
        undoAll();
        compose.select({e, d});
        compose.startMove();
        compose.move({400.f, 0.f}, unsnapped);
        TEST_ASSERT(lineOf(e) == "512,300,6000,5,0,0:0:0:0:" && lineOf(d) == "256,192,4000,12,0,5000,0:0:0:0:",
                    "the move stops at the field's edge, spinners stay (the object after one, changed, gets the new "
                    "combo the game gives it written)");
        undoAll();
        compose.select({c});
        compose.nudge({1.f, 0.f}, unsnapped);
        compose.nudge({1.f, 0.f}, unsnapped);
        TEST_ASSERT_EQ(lineOf(c), "66,192,2000,2,0,L|346:192,1,280", "Ctrl+arrows move by an osu!px, the curve along");
        compose.endNudge();
        compose.nudge({0.f, 1.f}, unsnapped);
        state.undo();
        state.undo();
        TEST_ASSERT_EQ(text(doc), original, "...one step while the arrows are held");
        compose.select({e});
        compose.nudge({-1.f, 0.f}, unsnapped);
        compose.nudge({-1.f, 0.f}, unsnapped);
        compose.endNudge();
        state.undo();
        state.redo();
        TEST_ASSERT_EQ(lineOf(e), "298,300,6000,5,0,0:0:0:0:",
                       "...redone where the last nudge left it (the first one also wrote the new combo after a "
                       "spinner)");
        undoAll();
        const auto rewrite = [&](Document::Id id, auto &&how) {
            auto object = doc.getObject(id);
            how(*object);
            state.edit(Document::Action::MOVE, [&](Document::Change &ch) { ch.setObject(id, *object); });
        };
        rewrite(a, [](neomod::BeatmapFile::HitObject &o) { o.x = 520.f; });
        compose.select({a});
        compose.nudge({-1.f, 0.f}, unsnapped);
        compose.nudge({1.f, 0.f}, unsnapped);
        compose.endNudge();
        TEST_ASSERT(lineOf(a).starts_with("519,100,1000,"),
                    "Ctrl+arrows take an object off the field back toward it, never further out");
        undoAll();

        // a slider that repeats: its tail is where its curve ends, not where it ends in time (its head)
        rewrite(c, [](neomod::BeatmapFile::HitObject &o) { o.slides = 2; });
        compose.select({c});
        state.clock.seek(30000);
        TEST_ASSERT(compose.objectAt({344.f, 192.f}) && compose.objectAt({344.f, 192.f})->id == c,
                    "a press on a selected slider's tail grabs it when it repeats too (its ring is there)");
        compose.startMove();
        compose.move({200.f, 0.f}, unsnapped);
        compose.endGesture();
        TEST_ASSERT_EQ(lineOf(c), "232,192,2000,2,0,L|512:192,2,280", "...and a drag keeps that tail in the field");
        undoAll();

        // moving in time
        compose.select({a, b});
        compose.startTimeMove(a);
        compose.moveInTime(2500);
        TEST_ASSERT(objectsText().starts_with("64,192,2000,2,0,L|344:192,1,280\r\n100,100,2500,5,0,0:0:0:0:\r\n"
                                              "200,100,3000,1,2,0:0:0:0:\r\n"),
                    "a drag on the timeline moves the selection in time, the lines to their place in time");
        TEST_ASSERT(idAt(2500) == a && idAt(3000) == b, "...keeping their ids");
        compose.moveInTime(-700);
        TEST_ASSERT(lineOf(a) == "100,100,0,5,0,0:0:0:0:" && lineOf(b) == "200,100,500,1,2,0:0:0:0:",
                    "...not before 0");
        compose.endGesture();
        state.undo();
        TEST_ASSERT_EQ(text(doc), original, "...one step");
        // what the sidebar shows for the selection, and the earliest selected object, through such a drag
        const auto earliestAt = [&] {
            const neomod::HitObject *earliest = compose.getEarliestSelected();
            return earliest ? earliest->getClickTime() : -1;
        };
        compose.select({a, b});
        TEST_ASSERT(earliestAt() == 1000 && !compose.hasHitSound(HitSoundType::WHISTLE),
                    "the selection's sounds show (whistle on one of them: off)");
        compose.startTimeMove(a);
        compose.moveInTime(2500);
        TEST_ASSERT(earliestAt() == 2500 && !compose.hasHitSound(HitSoundType::WHISTLE),
                    "...still while it moves in time, its earliest object found again past the slider");
        compose.toggleHitSound(HitSoundType::WHISTLE);
        compose.moveInTime(2600);
        TEST_ASSERT(compose.hasHitSound(HitSoundType::WHISTLE) && lineOf(a) == "100,100,2500,5,2,0:0:0:0:",
                    "...and a step during the drag, which ends it");
        state.undo();
        TEST_ASSERT(!compose.hasHitSound(HitSoundType::WHISTLE), "...and its undo");
        compose.endGesture();
        state.undo();
        compose.toggleHitSound(HitSoundType::WHISTLE);
        compose.startTimeMove(a);
        compose.moveInTime(2500);
        TEST_ASSERT(compose.hasHitSound(HitSoundType::WHISTLE), "...and a step before the drag, not shown yet");
        compose.endGesture();
        state.undo();
        state.undo();
        TEST_ASSERT_EQ(text(doc), original, "...all undone");
        state.divisor = 2;
        compose.select({b});
        compose.nudgeInTime(1);
        TEST_ASSERT_EQ(lineOf(b), "200,100,1750,1,2,0:0:0:0:", "K: a divisor tick later");
        compose.nudgeInTime(-1);
        TEST_ASSERT_EQ(lineOf(b), "200,100,1500,1,2,0:0:0:0:", "J: earlier");
        undoAll();
        state.divisor = 1;
        {
            // ticks that aren't whole milliseconds: 175 BPM in quarters has them at 85, 171, 257
            EditorState fractional;
            fractional.open(
                mapOf("osu file format v14\r\n\r\n[General]\r\nAudioFilename: a.mp3\r\nMode: 0\r\n\r\n"
                      "[TimingPoints]\r\n0,342.857142857143,4,2,0,100,1,0\r\n\r\n"
                      "[HitObjects]\r\n100,100,171,5,0,0:0:0:0:\r\n"));
            fractional.divisor = 4;
            fractional.compose.selectAll();
            fractional.compose.nudgeInTime(1);
            TEST_ASSERT(text(*fractional.document).ends_with("100,100,257,5,0,0:0:0:0:\r\n"),
                        "...onto the next tick, not a truncated tick length further");
            fractional.compose.nudgeInTime(-1);
            fractional.compose.nudgeInTime(-1);
            TEST_ASSERT(text(*fractional.document).ends_with("100,100,85,5,0,0:0:0:0:\r\n"), "...and back");
        }

        // end drags on the timeline
        compose.select({c, d});
        TEST_ASSERT(!compose.startEndDrag(*compose.objectAtTime(3000.0, 20.0)),
                    "a press on the end of one of a larger selection moves the selection");
        compose.clearSelection();
        TEST_ASSERT(compose.startEndDrag(*compose.objectAtTime(3000.0, 20.0)) &&
                        compose.getSelection() == std::vector<Document::Id>{c},
                    "a press on a slider's end drags it, selected");
        compose.dragEnd(4400.0, true);
        TEST_ASSERT_EQ(
            lineOf(c), "64,192,2000,2,0,L|344:192,2,280",
            "the span end nearest to the cursor gives the repeats (spans of 1000 ms, the cursor not snapped)");
        compose.dragEnd(4600.0, true);
        TEST_ASSERT_EQ(lineOf(c), "64,192,2000,2,0,L|344:192,3,280", "...past half a span, one more");
        TEST_ASSERT(compose.objectAtTime(4010.0, 20.0) && compose.objectAtTime(4010.0, 20.0)->id == c &&
                        compose.objectAtTime(4010.0, 20.0)->node == 2,
                    "a repeat's circle is hit as one");
        compose.dragEnd(1000.0, true);
        TEST_ASSERT(text(doc) == original && !doc.canUndo(),
                    "never under one span: back where it started, the drag leaves no step");
        compose.endGesture();
        compose.startEndDrag(*compose.objectAtTime(9000.0, 20.0));
        compose.dragEnd(10900.0, true);
        compose.endGesture();
        TEST_ASSERT_EQ(lineOf(f), "64,300,8000,2,0,L|344:300,3,280,0|0|0|8,0:0|0:0|0:0|0:0,0:0:0:0:",
                       "the tail keeps its edge sounds, the repeats added get the body's");
        compose.startEndDrag(*compose.objectAtTime(11000.0, 20.0));
        compose.dragEnd(9400.0, true);
        compose.endGesture();
        TEST_ASSERT_EQ(lineOf(f),
                       "64,300,8000,2,0,L|344:300,1,280,0|8,0:0|0:0,0:0:0:0:", "...fewer: the last repeats go");

        state.divisor = 2;
        TEST_ASSERT(compose.startEndDrag(*compose.objectAtTime(5000.0, 20.0)), "a press on a spinner's end drags it");
        compose.dragEnd(5600.0, true);
        TEST_ASSERT_EQ(lineOf(d), "256,192,4000,12,0,5500,0:0:0:0:", "...to the divisor's tick nearest to the cursor");
        compose.dragEnd(5600.0, false);
        TEST_ASSERT_EQ(lineOf(d), "256,192,4000,12,0,5600,0:0:0:0:", "...or to it (Shift)");
        compose.dragEnd(3800.0, true);
        TEST_ASSERT_EQ(lineOf(d), "256,192,4000,12,0,5600,0:0:0:0:", "...never to its start or before");
        compose.endGesture();
        state.divisor = 1;
        for(i32 i = 0; i < 3; i++) state.undo();
        TEST_ASSERT(text(doc) == original && !doc.canUndo(), "each drag one step");
        undoAll();

        // reversing
        compose.select({a, b, c});
        compose.reverse();
        TEST_ASSERT(objectsText().starts_with("344,192,1000,6,0,L|64:192,1,280\r\n200,100,2500,1,2,0:0:0:0:\r\n"
                                              "100,100,3000,1,0,0:0:0:0:\r\n"),
                    "Ctrl+G: each start where the selection's span mirrors its end, the slider's path the other way, "
                    "the new combo where it was in time");
        compose.reverse();
        TEST_ASSERT_EQ(text(doc), original, "...twice: as it was");
        compose.select({f});
        compose.reverse();
        TEST_ASSERT_EQ(lineOf(f), "344,300,8000,2,0,L|64:300,1,280,0|8,0:0|0:0,0:0:0:0:",
                       "a slider alone keeps its time, its edge sounds stay with their places in time");
        undoAll();
        TEST_ASSERT_EQ(text(doc), original, "...undone");
        compose.select({e});
        compose.reverse();
        TEST_ASSERT(!doc.canUndo(), "a circle alone: no step");

        // flipping, turning
        compose.select({a, c, d});
        compose.flip(false);
        TEST_ASSERT(lineOf(a) == "412,100,1000,5,0,0:0:0:0:" && lineOf(c) == "448,192,2000,2,0,L|168:192,1,280" &&
                        lineOf(d) == "256,192,4000,12,0,5000,0:0:0:0:",
                    "Ctrl+H: across the playfield's vertical middle, a slider's points too, a spinner stays");
        TEST_ASSERT(doc.getUndoAction() == Document::Action::FLIP, "...one step");
        compose.flip(false);
        TEST_ASSERT_EQ(text(doc), original, "...twice: as it was");
        compose.flip(true);
        TEST_ASSERT(lineOf(a) == "100,284,1000,5,0,0:0:0:0:" && lineOf(c) == "64,192,2000,2,0,L|344:192,1,280",
                    "Ctrl+J: across the horizontal middle (a slider along it stays)");
        undoAll();
        compose.select({a, c, e, f});
        compose.rotate(1);
        TEST_ASSERT(lineOf(a) == "348,36,1000,5,0,0:0:0:0:" && lineOf(c) == "256,0,2000,2,0,L|256:280,1,280" &&
                        lineOf(e) == "148,236,6000,5,0,0:0:0:0:" &&
                        lineOf(f) == "148,0,8000,2,0,L|148:280,1,280,0|8,0:0|0:0,0:0:0:0:",
                    "Ctrl+>: a quarter turn clockwise around the playfield's center (the object after the spinner "
                    "gets its new combo written, as with any change of its line)");
        TEST_ASSERT(doc.getUndoAction() == Document::Action::ROTATE, "...one step");
        undoAll();
        compose.select({a, c, f});
        compose.rotate(1);
        compose.rotate(-1);
        TEST_ASSERT_EQ(text(doc), original, "...and Ctrl+< back: as it was");
        for(i32 i = 0; i < 4; i++) compose.rotate(1);
        TEST_ASSERT_EQ(text(doc), original, "...four turns: as it was");
        undoAll();
        compose.select({d});
        compose.flip(false);
        compose.rotate(1);
        TEST_ASSERT(!doc.canUndo(), "a spinner alone: no step");
        compose.setTool(Tool::CIRCLE);
        state.clock.seek(3000);
        compose.placeCircle({500.f, 50.f}, unsnapped);
        compose.select({idAt(3000)});
        compose.rotate(1);
        TEST_ASSERT_EQ(lineOf(idAt(3000)),
                       "398,436,3000,1,0,0:0:0:0:", "what a turn carries off the field stays there");
        undoAll();

        // previews (as made by a step popup): each update makes the step again from the map as it was at the start
        const vec2 middle{256.f, 192.f};
        const auto quarterTurn = [&](vec2 v) { return middle + vec2{middle.y - v.y, v.x - middle.x}; };
        compose.select({a, c, d});
        compose.startPreview(Document::Action::ROTATE);
        compose.previewMap(quarterTurn, false);
        TEST_ASSERT(compose.previewMap(quarterTurn, false) && lineOf(a) == "348,36,1000,5,0,0:0:0:0:" &&
                        lineOf(c) == "256,0,2000,2,0,L|256:280,1,280",
                    "a preview made twice is made once");
        compose.endPreview(true);
        TEST_ASSERT(doc.getUndoAction() == Document::Action::ROTATE && !compose.isPreviewing(), "...kept, one step");
        undoAll();
        compose.select({a, c});
        compose.startPreview(Document::Action::ROTATE);
        compose.previewMap(quarterTurn, false);
        compose.endPreview(false);
        TEST_ASSERT(text(doc) == original && !doc.canUndo(), "...taken back: nothing");
        compose.select({c});
        const auto center = compose.selectionCenter();
        TEST_ASSERT(center && *center == vec2(204.f, 192.f), "the selection's center is its heads' and ends' middle");
        TEST_ASSERT(std::abs(compose.maxScale(*center, true, true) - 204.f / 140.f) < 1e-4f &&
                        compose.maxScale(*center, false, true) == 5.f,
                    "it scales as far as its heads and ends stay in the field (5 with nothing off its center)");
        compose.startPreview(Document::Action::SCALE);
        compose.previewMap([&](vec2 v) { return *center + (v - *center) * 0.5f; }, true);
        TEST_ASSERT_EQ(lineOf(c), "134,192,2000,2,0,L|274:192,1,140", "scaled with its length snapped again");
        compose.previewMap([&](vec2 v) { return *center + (v - *center) * vec2{0.1f, 1.f}; }, true);
        TEST_ASSERT_EQ(lineOf(c), "190,192,2000,2,0,L|218:192,1,140", "...at least a tick long");
        compose.endPreview(false);
        compose.clearSelection();

        // polygon circles and streams, previewed in place (140 osu!px a beat)
        const auto lines = [](std::span<const neomod::BeatmapFile::HitObject> records) {
            std::string out;
            for(const auto &record : records) out += neomod::BeatmapFile::format(record) + "\r\n";
            return out;
        };
        state.clock.seek(1000);
        TEST_ASSERT_EQ(lines(compose.polygon(3, 1, 0.0, 1.0)),
                       "216,262,1000,1,0,0:0:0:0:\r\n216,122,1500,1,0,0:0:0:0:\r\n337,192,2000,1,0,0:0:0:0:\r\n",
                       "a polygon: around the playfield's center from the snapped time, a tick apart, its side the "
                       "spacing times a tick's distance, its first corner 360/n degrees clockwise from the right");
        TEST_ASSERT(compose.polygon(3, 1, 0.0, 6.0).empty(), "...none with a corner off the field");
        compose.startPreview(Document::Action::POLYGON);
        compose.previewPlace(compose.polygon(4, 1, 0.0, 1.0));
        TEST_ASSERT(compose.previewPlace(compose.polygon(3, 1, 0.0, 1.0)) &&
                        objectsText().starts_with("216,262,1000,1,0,0:0:0:0:\r\n216,122,1500,1,0,0:0:0:0:\r\n"
                                                  "337,192,2000,1,0,0:0:0:0:\r\n256,192,4000,12,"),
                    "placed in a preview, they replace the objects at their times, as the map was before it");
        TEST_ASSERT(!compose.previewPlace({}) && text(doc) == original, "...nothing placed: nothing replaced");
        compose.previewPlace(compose.polygon(3, 1, 0.0, 1.0));
        compose.endPreview(true);
        TEST_ASSERT(doc.getUndoAction() == Document::Action::POLYGON && compose.getSelection().size() == 3,
                    "...kept: one step, the circles selected");
        undoAll();
        compose.select({c});
        const auto slider = compose.getSelectedSlider();
        TEST_ASSERT(slider && slider->first == c, "the selected slider");
        TEST_ASSERT_EQ(lines(compose.stream(slider->second, 2, 1.0, 0)),
                       "64,192,2000,1,0,0:0:0:0:\r\n134,192,2250,1,0,0:0:0:0:\r\n204,192,2500,1,0,0:0:0:0:\r\n"
                       "274,192,2750,1,0,0:0:0:0:\r\n344,192,3000,1,0,0:0:0:0:\r\n",
                       "a stream: a circle a tick apart where the slider is at each tick (spacing 1)");
        TEST_ASSERT_EQ(lines(compose.stream(slider->second, 2, 1.0, 3)),
                       "64,192,2000,1,0,0:0:0:0:\r\n204,192,2250,1,0,0:0:0:0:\r\n344,192,2500,1,0,0:0:0:0:\r\n",
                       "...or a count spread to its end");
        compose.startPreview(Document::Action::STREAM);
        compose.previewPlace(compose.stream(slider->second, 2, 1.0, 3), slider->first);
        compose.endPreview(true);
        TEST_ASSERT(!objectsText().contains("L|344:192") && objectsText().contains("204,192,2250,1,0,0:0:0:0:") &&
                        doc.getUndoAction() == Document::Action::STREAM,
                    "...in the slider's place, one step");
        undoAll();
        state.clock.seek(0);

        // selecting by combo numbers (as a timestamp does): a 1, b 2, c 3, the spinner d, e 1 (after it), f 2
        TEST_ASSERT_EQ(compose.selectCombos(1200, std::vector<i32>{3, 1, 2}), 1500,
                       "a timestamp goes to the first object from its time");
        TEST_ASSERT(compose.getSelection() == std::vector<Document::Id>({c, d, f}),
                    "...and its numbers select the next object with each, across combos (a spinner has 1)");
        compose.selectCombos(1000, std::vector<i32>{2, 7, 3});
        TEST_ASSERT(compose.getSelection() == std::vector<Document::Id>({b, c}), "a number nothing has is skipped");
        TEST_ASSERT_EQ(compose.selectCombos(9000, std::vector<i32>{1}), 9000, "...past the last object: its time");
        compose.clearSelection();

        // resetting samples and colour skips
        compose.select({b, f});
        compose.resetSamples(false);
        TEST_ASSERT(lineOf(b) == "200,100,1500,1,0,0:0:0:0:" && lineOf(f) == "64,300,8000,2,0,L|344:300,1,280",
                    "Reset Samples: the selection's sounds gone, a slider's edge lists and sample too");
        TEST_ASSERT(doc.getUndoAction() == Document::Action::RESET_SAMPLES, "...one step");
        undoAll();
        state.edit(Document::Action::HITSOUNDS, [&](Document::Change &c) {
            auto record = doc.getObject(a);
            record->hitSounds = 8;
            record->sample = {.normalSet = 2, .additionSet = 3, .index = 4, .volume = 70, .filename = "x.wav"};
            c.setObject(a, *record);
        });
        compose.clearSelection();
        compose.resetSamples(true);
        TEST_ASSERT_EQ(objectsText(),
                       "100,100,1000,5,0,0:0:0:0:\r\n200,100,1500,1,0,0:0:0:0:\r\n64,192,2000,2,0,L|344:192,1,280\r\n"
                       "256,192,4000,12,0,5000,0:0:0:0:\r\n300,300,6000,1,0,0:0:0:0:\r\n"
                       "64,300,8000,2,0,L|344:300,1,280\r\n",
                       "Reset All Samples: every object's, sets, index, volume and file included");
        undoAll();
        compose.select({a});
        compose.setColourSkip(2);
        compose.clearSelection();
        compose.resetColourSkips();
        TEST_ASSERT(text(doc) == original && doc.getUndoAction() == Document::Action::RESET_COLOURS,
                    "Reset Combo Colours: every colour skip gone, the new combos kept, one step");
        undoAll();

        // recalculating slider lengths (140 osu!px a beat)
        state.edit(Document::Action::EDIT_SLIDER, [&](Document::Change &change) {
            auto shortened = doc.getObject(c);
            shortened->length = 200.0;
            change.setObject(c, *shortened);
            auto tiny = doc.getObject(f);
            tiny->curvePoints = {{100.f, 300.f}};
            tiny->length = 36.0;
            change.setObject(f, *tiny);
        });
        state.divisor = 2;
        compose.recalculateSliderLengths();
        TEST_ASSERT(lineOf(c) == "64,192,2000,2,0,L|344:192,1,280" &&
                        lineOf(f) == "64,300,8000,2,0,L|100:300,1,36,0|8,0:0|0:0,0:0:0:0:" &&
                        doc.getUndoAction() == Document::Action::SLIDER_LENGTHS,
                    "Recalculate Slider Lengths: each from its whole curve, snapped down to the divisor's ticks "
                    "(longer than it was here), one step; one under a tick stays");
        state.divisor = 1;
        undoAll();

        // new combos, colour skips
        compose.select({b});
        compose.toggleNewCombo();
        TEST_ASSERT_EQ(lineOf(b), "200,100,1500,5,2,0:0:0:0:", "Q on the selection");
        compose.setColourSkip(2);
        TEST_ASSERT_EQ(lineOf(b), "200,100,1500,37,2,0:0:0:0:", "a colour skip");
        // the drawer gives the objects the skin
        state.objects->view.skin = osu->getSkin();
        const auto skips = compose.getColourSkips();
        TEST_ASSERT(skips && skips->current == 2 && skips->count >= 3, "...which the sidebar's swatches show");
        compose.toggleNewCombo();
        TEST_ASSERT_EQ(lineOf(b), "200,100,1500,1,2,0:0:0:0:", "no new combo takes its skip along");
        compose.select({e});
        compose.toggleNewCombo();
        compose.toggleNewCombo();
        TEST_ASSERT_EQ(lineOf(e), "300,300,6000,5,0,0:0:0:0:", "an object right after a spinner keeps its new combo");
        undoAll();

        // hitsounds, sample sets
        compose.select({a, b});
        compose.toggleHitSound(HitSoundType::WHISTLE);
        TEST_ASSERT(lineOf(a) == "100,100,1000,5,2,0:0:0:0:" && lineOf(b) == "200,100,1500,1,2,0:0:0:0:",
                    "a mixed selection all gets it");
        TEST_ASSERT(compose.hasHitSound(HitSoundType::WHISTLE), "...shown on");
        compose.toggleHitSound(HitSoundType::WHISTLE);
        TEST_ASSERT(lineOf(a) == "100,100,1000,5,0,0:0:0:0:" && lineOf(b) == "200,100,1500,1,0,0:0:0:0:",
                    "...then none");
        compose.select({c, f});
        compose.toggleHitSound(HitSoundType::CLAP);
        TEST_ASSERT(lineOf(c) == "64,192,2000,2,8,L|344:192,1,280" &&
                        lineOf(f) == "64,300,8000,2,8,L|344:300,1,280,8|8,0:0|0:0,0:0:0:0:",
                    "a slider's body and every edge it lists");
        undoAll();
        compose.select({c});
        compose.setSampleSet(false, 2);
        TEST_ASSERT_EQ(lineOf(c), "64,192,2000,2,0,L|344:192,1,280,0|0,2:0|2:0,2:0:0:0:",
                       "a sample set on a slider without edge sets writes them, for every edge");
        compose.select({a, c});
        TEST_ASSERT(!compose.getSampleSet(false), "a mixed selection's set is none");
        compose.setSampleSet(true, 3);
        TEST_ASSERT(lineOf(a) == "100,100,1000,5,0,0:3:0:0:" &&
                        lineOf(c) == "64,192,2000,2,0,L|344:192,1,280,0|0,2:3|2:3,2:3:0:0:",
                    "the additions' set");
        undoAll();

        // the sample on a hit itself: its index, its volume and its file
        compose.select({a, d});
        TEST_ASSERT(compose.getSampleIndex() == 0 && compose.getSampleVolume() == 0 &&
                        compose.getSampleFile() == std::string_view{},
                    "a circle and a spinner without samples of their own");
        TEST_ASSERT(compose.setSampleIndex(2) && lineOf(a) == "100,100,1000,5,0,0:0:2:0:" &&
                        lineOf(d) == "256,192,4000,12,0,5000,0:0:2:0:" &&
                        doc.getUndoAction() == Document::Action::SAMPLE_INDEX,
                    "a sample index for the selected circles and spinners, one step");
        compose.setSampleVolume(60);
        compose.setSampleFile("kick.wav");
        TEST_ASSERT(lineOf(a) == "100,100,1000,5,0,0:0:2:60:kick.wav" &&
                        lineOf(d) == "256,192,4000,12,0,5000,0:0:2:60:kick.wav",
                    "...a volume and a file");
        TEST_ASSERT(
            compose.getSampleIndex() == 2 && compose.getSampleVolume() == 60 && compose.getSampleFile() == "kick.wav",
            "...which the sidebar shows");
        compose.select({a, b});
        TEST_ASSERT(!compose.getSampleIndex() && !compose.getSampleVolume() && !compose.getSampleFile(),
                    "hits whose own samples differ show none");
        compose.select({a, c});
        TEST_ASSERT(compose.getSampleIndex() == 2 && compose.getSampleFile() == "kick.wav",
                    "a slider selected with them doesn't count");
        TEST_ASSERT(compose.setSampleIndex(3) && lineOf(a) == "100,100,1000,5,0,0:0:3:60:kick.wav" &&
                        lineOf(c) == "64,192,2000,2,0,L|344:192,1,280",
                    "...and stays as it is");
        compose.select({c, f});
        const u32 before = doc.getRevision();
        TEST_ASSERT(!compose.setSampleIndex(5) && !compose.setSampleVolume(50) && !compose.setSampleFile("x.wav") &&
                        doc.getRevision() == before,
                    "sliders alone take none: nothing changes");
        TEST_ASSERT(
            !compose.getSampleIndex() && !compose.getSampleVolume() && compose.getSampleFile() == std::string_view{},
            "...and they show none");
        compose.select({a});
        compose.setSampleFile({});
        TEST_ASSERT_EQ(lineOf(a), "100,100,1000,5,0,0:0:3:60:", "no file again");
        undoAll();
        compose.setTool(Tool::CIRCLE);
        compose.setSampleIndex(4);
        compose.setSampleVolume(30);
        compose.setSampleFile("snare.ogg");
        TEST_ASSERT(compose.getSampleIndex() == 4 && compose.getSampleVolume() == 30 &&
                        compose.getSampleFile() == "snare.ogg" && !doc.canUndo(),
                    "without a selection they're the next object's");
        state.clock.seek(3000);
        compose.placeCircle({300.f, 50.f}, unsnapped);
        TEST_ASSERT_EQ(lineOf(idAt(3000)), "300,50,3000,1,0,0:0:4:30:snare.ogg", "...which a circle placed gets");
        compose.setTool(Tool::SELECT);
        compose.clearSelection();
        TEST_ASSERT(compose.getSampleIndex() == 0 && compose.getSampleFile() == std::string_view{},
                    "...until the selection is cleared");
        undoAll();
        TEST_ASSERT_EQ(text(doc), original, "every edit undone, the bytes the map started from");

        // a selection that isn't there any more
        compose.select({a});
        state.edit(Document::Action::DELETE_OBJECTS, [&](Document::Change &ch) { ch.remove(a); });
        TEST_ASSERT(compose.getSelection().empty(), "a deleted object drops out of the selection");
        undoAll();
        Environment::deleteFile(path);
    }

    // what a step does to the breaks and new combos that follow from the objects (in the gaps the step changed)
    void breaks() {
        TEST_SECTION("breaks");
        using Action = Document::Action;
        using HO = neomod::BeatmapFile::HitObject;
        const std::string path = Mc::Paths::cache() + "/editor-breaks-tests.osu";
        // approach rate 5: 1200 ms. a break fitted to its gap (2000 to 10000), a gap of 9 s without one (11000 to 20000),
        // an object after a spinner without its new combo written (23500), a break in a gap of 3 s (24000 to 27000)
        const std::string original =
            "osu file format v14\r\n\r\n[General]\r\nAudioFilename: a.mp3\r\nMode: 0\r\n\r\n[Difficulty]\r\n"
            "CircleSize:4\r\nApproachRate:5\r\nSliderMultiplier:1.4\r\nSliderTickRate:1\r\n\r\n[Events]\r\n"
            "//Background and Video events\r\n0,0,\"bg.png\",0,0\r\n//Break Periods\r\n2,2200,8800\r\n2,27200,28800\r\n"
            "//Storyboard Layer 0 (Background)\r\n\r\n[TimingPoints]\r\n0,500,4,2,0,100,1,0\r\n\r\n[HitObjects]\r\n"
            "256,192,1000,5,0,0:0:0:0:\r\n"          // a
            "256,192,2000,1,0,0:0:0:0:\r\n"          // b
            "256,192,10000,5,0,0:0:0:0:\r\n"         // c
            "256,192,11000,1,0,0:0:0:0:\r\n"         // d
            "256,192,20000,1,0,0:0:0:0:\r\n"         // e
            "256,192,21000,1,0,0:0:0:0:\r\n"         // f
            "256,192,22000,12,0,23000,0:0:0:0:\r\n"  // g, a spinner
            "256,192,23500,1,0,0:0:0:0:\r\n"         // h
            "256,192,24000,1,0,0:0:0:0:\r\n"         // i
            "256,192,27000,1,0,0:0:0:0:\r\n"         // j
            "256,192,30000,5,0,0:0:0:0:\r\n";        // k

        File(path, File::MODE::WRITE).writeLine(original, false);
        auto [read, error] = Document::read(location("editor-breaks-tests.osu"), path);
        TEST_ASSERT(read != nullptr, "the breaks map reads");
        if(!read) return;

        EditorState state;
        state.open(std::move(read));
        Document &doc = *state.document;
        Compose &compose = state.compose;
        const auto events = [&] {
            const std::string_view all = text(doc);
            const uSz from = all.find("//Break Periods\r\n") + 17;
            return all.substr(from, all.find("//Storyboard") - from);
        };
        const auto idAt = [&](i32 timeMS) {
            const MapObjects &objects = *state.objects;
            for(uSz i = 0; i < objects.objects.size(); i++) {
                if(objects.objects[i]->getClickTime() == timeMS) return objects.ids[i];
            }
            return Document::Id{0};
        };
        const auto lineOf = [&](Document::Id id) {
            const auto object = doc.getObject(id);
            return object ? neomod::BeatmapFile::format(*object) : std::string{};
        };
        // an object to another time (or anything else about it), as one step
        const auto edit = [&](Document::Id id, auto &&how) {
            auto object = doc.getObject(id);
            if(!object) return false;
            how(*object);
            const bool changed = state.edit(Action::MOVE, [&](Document::Change &c) { c.setObject(id, *object); });
            return changed;
        };
        const auto moveTo = [&](Document::Id id, i32 timeMS) { edit(id, [&](HO &o) { o.time = timeMS; }); };
        const auto replaceBreak = [&](uSz index, std::string_view line) {
            state.edit(Action::REMOVE_BREAK, [&](Document::Change &c) { c.replace(doc.getBreaks()[index].id, line); });
        };
        const auto undoAll = [&] {
            compose.endGesture();
            while(doc.canUndo()) state.undo();
        };
        const Document::Id b = idAt(2000), c = idAt(10000), d = idAt(11000), e = idAt(20000), h = idAt(23500),
                           i = idAt(24000);
        const std::string fitted = "2,2200,8800\r\n2,27200,28800\r\n";
        TEST_ASSERT(b && c && d && e && h && i && events() == fitted && doc.getBreaks().size() == 2,
                    "the map's breaks read, with their lines");

        edit(e, [](HO &o) { o.hitSounds = 2; });
        TEST_ASSERT(events() == fitted && lineOf(e) == "256,192,20000,1,2,0:0:0:0:" &&
                        lineOf(h) == "256,192,23500,1,0,0:0:0:0:",
                    "a step that changes no gap leaves the breaks alone, also where one is missing, and the new "
                    "combos it didn't touch");
        undoAll();

        moveTo(b, 3000);
        TEST_ASSERT_EQ(events(), "2,3200,8800\r\n2,27200,28800\r\n",
                       "a break's edges at their defaults follow its gap");
        undoAll();
        TEST_ASSERT_EQ(text(doc), original, "...undone byte for byte");

        replaceBreak(0, "2,7500,8800");
        TEST_ASSERT_EQ(events(), "2,7500,8800\r\n2,27200,28800\r\n", "a break moved by hand in its file stays there");
        moveTo(b, 1500);
        TEST_ASSERT_EQ(events(), "2,6500,8800\r\n2,27200,28800\r\n",
                       "...its moved edge keeps its time within 5 s of its object, the other follows");
        undoAll();

        replaceBreak(0, "2,7500,8800");
        moveTo(c, 8500);
        TEST_ASSERT_EQ(events(), "2,2200,7300\r\n2,27200,28800\r\n",
                       "a break the fit leaves shorter than 650 ms goes, and its gap of 5 s or more gets the default");
        undoAll();

        state.edit(Action::ADD_CIRCLE, [&](Document::Change &ch) {
            HO circle;
            circle.kind = HO::Kind::CIRCLE;
            circle.x = 100.f;
            circle.y = 100.f;
            circle.type = HO::TYPE_CIRCLE;
            circle.time = 5000;
            ch.addObject(circle);
            circle.time = 6000;
            ch.addObject(circle);
        });
        TEST_ASSERT_EQ(events(), "2,27200,28800\r\n", "a gap under 1.2 s loses its break, gaps under 5 s get none");
        undoAll();

        state.edit(Action::REMOVE_BREAK, [](Document::Change &ch) { ch.addBreak(5000, 6000); });
        TEST_ASSERT_EQ(events(), "2,2200,8800\r\n2,5000,6000\r\n2,27200,28800\r\n",
                       "a new break line goes in time order among the others");
        moveTo(b, 2500);
        TEST_ASSERT_EQ(events(), "2,2700,8800\r\n2,27200,28800\r\n", "of two breaks in a changed gap the first stays");
        undoAll();
        state.edit(Action::REMOVE_BREAK, [](Document::Change &ch) {
            ch.addBreak(5000, 6000);
            ch.addBreak(7000, 8000);
            ch.addBreak(300, 400);
            ch.addBreak(100, 200);
        });
        TEST_ASSERT(events().starts_with("2,100,200\r\n2,300,400\r\n2,2200,8800\r\n2,5000,6000\r\n2,7000,8000\r\n"),
                    "...also among those the same step added, in whatever order");
        undoAll();

        moveTo(d, 12000);
        TEST_ASSERT_EQ(events(), "2,2200,8800\r\n2,12200,18800\r\n2,27200,28800\r\n",
                       "a changed gap of 5 s or more gets its default break");
        TEST_ASSERT_EQ(lineOf(e), "256,192,20000,5,0,0:0:0:0:", "...and the object after it its new combo");
        undoAll();

        state.edit(Action::DELETE_OBJECTS, [&](Document::Change &ch) { ch.remove(h); });
        TEST_ASSERT_EQ(lineOf(i), "256,192,24000,5,0,0:0:0:0:",
                       "an object that comes right after a spinner gets its new combo written");
        undoAll();
        compose.select({h});
        TEST_ASSERT(!compose.hasNewCombo(), "the selected object after a spinner has no new combo written");
        compose.startMove();
        compose.move({10.f, 0.f}, {});
        compose.endGesture();
        TEST_ASSERT(lineOf(h) == "266,192,23500,5,0,0:0:0:0:" && compose.hasNewCombo(),
                    "...moved, it gets one, which the selection shows");
        compose.clearSelection();
        undoAll();
        TEST_ASSERT(!edit(c, [](HO &o) { o.type = HO::TYPE_CIRCLE; }) && text(doc) == original && !doc.canUndo(),
                    "a new combo switched off where the game gives one is put back: no step");

        compose.select({d});
        compose.startTimeMove(d);
        compose.moveInTime(12000);
        TEST_ASSERT(events() != fitted, "a drag on the timeline changes the breaks as it goes");
        compose.moveInTime(11000);
        compose.endGesture();
        TEST_ASSERT(text(doc) == original && !doc.canUndo(), "...and back where it started leaves no step");
        compose.clearSelection();

        state.edit(Action::REMOVE_BREAK, [&](Document::Change &ch) {
            ch.remove(doc.getBreaks()[1].id);
            ch.remove(doc.getBreaks()[0].id);
        });
        moveTo(d, 12000);
        TEST_ASSERT_EQ(events(), "2,12200,18800\r\n",
                       "a break without others goes under //Break Periods; the gap whose break was taken out by hand "
                       "stays without one");
        undoAll();

        state.edit(Action::DELETE_OBJECTS, [&](Document::Change &ch) {
            for(const auto &object : state.objects->ids) {
                if(object != idAt(1000)) ch.remove(object);
            }
        });
        TEST_ASSERT_EQ(events(), "", "with fewer than two objects there are no breaks");
        undoAll();

        TEST_ASSERT(compose.removeBreakAt(5000.0) && text(doc) == original,
                    "a right-click can't take out a break the rules give its gap (5 s or more)");
        TEST_ASSERT(compose.removeBreakAt(28000.0) && events() == "2,2200,8800\r\n", "...it can in a smaller gap");
        TEST_ASSERT(!compose.removeBreakAt(10500.0), "...and there's none outside of breaks");
        undoAll();
        replaceBreak(0, "2,2100,8800");
        TEST_ASSERT(compose.removeBreakAt(5000.0) && events() == "2,2100,8800\r\n2,27200,28800\r\n",
                    "...wherever its edges are (the next fit of its gap would put one back)");
        undoAll();

        state.edit(Action::DIFFICULTY, [](Document::Change &ch) {
            ch.setValue(neomod::BeatmapFile::SectionKind::DIFFICULTY, "ApproachRate", "9");
        });
        TEST_ASSERT(events() == "2,2200,9400\r\n2,11200,19400\r\n2,27200,29400\r\n" &&
                        lineOf(e) == "256,192,20000,5,0,0:0:0:0:" && lineOf(h) == "256,192,23500,1,0,0:0:0:0:",
                    "another approach time moves every break's default end and fills the long gaps, the new combos "
                    "written only after a new break");
        undoAll();
        compose.setTool(Compose::Tool::CIRCLE);
        state.clock.seek(9000);
        TEST_ASSERT(compose.previewCircle({256.f, 192.f}, {}) && compose.previewCircle({256.f, 192.f}, {})->number == 1,
                    "the circle a click would place after a break is numbered as the first of a new combo");
        compose.setTool(Compose::Tool::SELECT);
        TEST_ASSERT_EQ(text(doc), original, "every step undone, the bytes the map started from");

        // break edges by hand: the end within 5 s from the object after it (10000) and up to where it is by itself
        TEST_ASSERT(!compose.startBreakDrag(5000.0, 50.0) && compose.startBreakDrag(8790.0, 50.0),
                    "a break's edge is grabbed near it");
        compose.dragBreak(7000.0, false);
        TEST_ASSERT_EQ(events(), "2,2200,7000\r\n2,27200,28800\r\n", "the end goes where it's dragged");
        compose.dragBreak(4000.0, false);
        TEST_ASSERT_EQ(events(), "2,2200,5000\r\n2,27200,28800\r\n", "...no further than 5 s from its object");
        compose.dragBreak(9500.0, false);
        TEST_ASSERT(!doc.isChanged() && !doc.canRedo(), "...and back where it was by itself, which is no step");
        compose.endGesture();
        TEST_ASSERT(compose.startBreakDrag(2200.0, 50.0), "the start");
        compose.dragBreak(1000.0, false);
        TEST_ASSERT(!doc.isChanged(), "...never earlier than it is by itself");
        compose.dragBreak(9000.0, false);
        TEST_ASSERT_EQ(events(), "2,7000,8800\r\n2,27200,28800\r\n", "...and within 5 s of the object before it");
        compose.dragBreak(8500.0, true);
        compose.endGesture();
        TEST_ASSERT_EQ(events(), "2,7000,8800\r\n2,27200,28800\r\n",
                       "...which wins over the 650 ms it keeps from the end, and over the tick");
        TEST_ASSERT(doc.getUndoAction() == Action::MOVE_BREAK, "one step for the drag");

        // a break by hand in a gap that gets none by itself (i to j, 3 s), and the one a long gap is missing (d to e)
        state.clock.seek(25000);
        TEST_ASSERT(compose.insertBreak() && !compose.insertBreak(), "a break for the gap the time is in, once");
        state.clock.seek(15000);
        TEST_ASSERT(compose.insertBreak(), "...");
        TEST_ASSERT_EQ(events(), "2,7000,8800\r\n2,11200,18800\r\n2,24200,25800\r\n2,27200,28800\r\n",
                       "...from 200 ms after the object before it to the approach time before the next, in time order");
        state.clock.seek(1500);
        const bool tooShort = compose.insertBreak();
        state.clock.seek(500);
        TEST_ASSERT(!tooShort && !compose.insertBreak(), "none in a gap under 1.2 s, or before the first object");

        compose.resetBreaks();
        TEST_ASSERT_EQ(events(), "2,2200,8800\r\n2,11200,18800\r\n",
                       "reset: the breaks the map gets by itself, one for every gap of 5 s");
        TEST_ASSERT(doc.getUndoAction() == Action::RESET_BREAKS, "...in one step");
        undoAll();
        TEST_ASSERT_EQ(text(doc), original, "all of it undone");
        Environment::deleteFile(path);
    }

    // copy, cut, paste and clone: what they put in, where, and what the system clipboard gets
    void clipboard() {
        TEST_SECTION("clipboard");
        using Tool = Compose::Tool;
        const std::string path = Mc::Paths::cache() + "/editor-clipboard-tests.osu";
        const std::string original =
            "osu file format v14\r\n\r\n[General]\r\nAudioFilename: a.mp3\r\nMode: 0\r\n\r\n[Difficulty]\r\n"
            "CircleSize:4\r\nApproachRate:5\r\nSliderMultiplier:1.4\r\nSliderTickRate:1\r\n\r\n[TimingPoints]\r\n"
            "0,500,4,2,0,100,1,0\r\n\r\n[HitObjects]\r\n"
            "100,100,1000,5,0,0:0:0:0:\r\n"        // a
            "200,100,1500,1,2,0:0:0:0:\r\n"        // b, a whistle
            "256,192,2000,12,0,3000,0:0:0:0:\r\n"  // s, a spinner
            "300,300,4000,5,0,0:0:0:0:\r\n"        // e
            "400,300,6000,1,0,0:0:0:0:\r\n";       // f

        File(path, File::MODE::WRITE).writeLine(original, false);
        auto [read, error] = Document::read(location("editor-clipboard-tests.osu"), path);
        TEST_ASSERT(read != nullptr, "the clipboard map reads");
        if(!read) return;

        EditorState state;
        state.open(std::move(read));
        Document &doc = *state.document;
        Compose &compose = state.compose;
        const auto objectsText = [&] {
            const std::string_view all = text(doc);
            return all.substr(all.find("[HitObjects]\r\n") + 14);
        };
        const auto idAt = [&](i32 timeMS) {
            const MapObjects &objects = *state.objects;
            for(uSz i = 0; i < objects.objects.size(); i++) {
                if(objects.objects[i]->getClickTime() == timeMS) return objects.ids[i];
            }
            return Document::Id{0};
        };
        const auto clipboardText = [] { return std::string{env->getClipBoardText()}; };
        const auto undoAll = [&] {
            while(doc.canUndo()) state.undo();
        };
        const Document::Id a = idAt(1000), b = idAt(1500), s = idAt(2000), e = idAt(4000), f = idAt(6000);

        compose.select({b, a});
        compose.copy();
        TEST_ASSERT_EQ(clipboardText(), "00:01:000 (1,2) - ",
                       "copy puts osu!'s timestamp of the selection on the clipboard: its start and combo numbers");
        TEST_ASSERT(compose.canPaste() && text(doc) == original, "...and keeps the objects, changing nothing");
        compose.clearSelection();
        state.clock.seek(7100);
        TEST_ASSERT(compose.paste() == Compose::Placed::DONE, "paste puts them in");
        TEST_ASSERT(objectsText().ends_with("400,300,6000,1,0,0:0:0:0:\r\n100,100,7000,5,0,0:0:0:0:\r\n"
                                            "200,100,7500,1,2,0:0:0:0:\r\n"),
                    "...the earliest at the snapped time, the other as far after it as it was");
        TEST_ASSERT(compose.getSelection().size() == 2 && compose.isSelected(idAt(7000)) &&
                        compose.isSelected(idAt(7500)) && compose.getTool() == Tool::SELECT,
                    "...selected");
        state.undo();
        TEST_ASSERT_EQ(text(doc), original, "...as one step");

        state.clock.seek(6000);
        compose.paste();
        TEST_ASSERT(objectsText().find("400,300,6000") == std::string::npos &&
                        objectsText().ends_with("100,100,6000,5,0,0:0:0:0:\r\n200,100,6500,1,2,0:0:0:0:\r\n"),
                    "an object at exactly a pasted object's time is replaced");
        undoAll();

        compose.clearSelection();
        compose.copy();
        TEST_ASSERT(clipboardText() == "00:06:000 - " && !compose.canPaste(),
                    "copy without a selection: the editor's time, and nothing to paste");

        compose.select({b});
        compose.cut();
        TEST_ASSERT(objectsText().find("200,100,1500") == std::string::npos && compose.getSelection().empty() &&
                        clipboardText() == "00:01:500 (2) - " && compose.canPaste(),
                    "cut copies the selection and takes it out");
        if(auto touched = state.undo()) compose.selectTouched(std::move(*touched));
        TEST_ASSERT(text(doc) == original && compose.getSelection() == std::vector<Document::Id>{b},
                    "...one step, which undone brings it back selected");

        compose.select({s});
        compose.copy();
        TEST_ASSERT_EQ(clipboardText(), "00:02:000 (1) - ", "a spinner, which has no number, counts as 1");

        compose.select({e});
        compose.clone();
        TEST_ASSERT(
            objectsText().find("300,300,4000,5,0,0:0:0:0:\r\n300,300,4500,5,0,0:0:0:0:\r\n") != std::string::npos,
            "clone puts a copy a beat after the selection's end, snapped");
        TEST_ASSERT(compose.getSelection() == std::vector<Document::Id>{idAt(4500)} && compose.canPaste(),
                    "...selects the copy and leaves what was copied");
        undoAll();
        TEST_ASSERT(f && text(doc) == original, "every step undone, the bytes the map started from");

        compose.reset();
        TEST_ASSERT(compose.canPaste(), "what was copied outlives another map being shown");
        Environment::deleteFile(path);
    }

    // where placed and dragged objects go: the grid, distance snap, angle snap and stacking
    void snapping() {
        TEST_SECTION("snapping");
        using Settings = Snapping::Settings;
        TEST_ASSERT(Snapping::clampGridSize(10) == 8 && Snapping::clampGridSize(1) == 4 &&
                        Snapping::clampGridSize(100) == 32 && Snapping::clampGridSize(16) == 16,
                    "a grid size is the nearest of 4, 8, 16 and 32");
        TEST_ASSERT(Snapping::clampSpacing(0.05f) == 0.1f && Snapping::clampSpacing(7.f) == 6.f &&
                        Snapping::clampSpacing(1.234f) == 1.23f && Snapping::clampSpacing(std::nanf("")) == 0.1f,
                    "a spacing is from 0.1 to 6, at two decimals");
        TEST_ASSERT(Snapping::grid({17.f, 30.f}, 16) == vec2(16.f, 32.f), "the grid rounds to its nearest lines");
        const Snapping::Reference from{.end = {100.f, 100.f}, .endTime = 1000};
        TEST_ASSERT(Snapping::distance(from, {300.f, 100.f}, 1500, 0.28, 1.f) == vec2(240.f, 100.f),
                    "distance snap: as far as the time covers at the velocity, toward the cursor");
        TEST_ASSERT(!Snapping::distance({.end = {500.f, 100.f}, .endTime = 1000}, {600.f, 100.f}, 1500, 0.28, 1.f),
                    "...none outside the field");
        TEST_ASSERT(Snapping::angle({0.f, 0.f}, {10.f, 98.f}).has_value() &&
                        std::abs(Snapping::angle({0.f, 0.f}, {10.f, 98.f})->x) < 0.001f,
                    "angle snap: within 10 degrees of a multiple of 45, onto it");
        TEST_ASSERT(!Snapping::angle({0.f, 0.f}, {30.f, 70.f}), "...not further");

        // 500 ms beats at slider multiplier 1.4: 0.28 osu!px per ms; at circle size 4 the circle radius is 36.48
        const std::string path = Mc::Paths::cache() + "/editor-snapping-tests.osu";
        const std::string original =
            "osu file format v14\r\n\r\n[General]\r\nAudioFilename: a.mp3\r\nMode: 0\r\n\r\n[Editor]\r\n"
            "DistanceSpacing: 1.3\r\nGridSize: 8\r\n\r\n[Difficulty]\r\nCircleSize:4\r\nApproachRate:5\r\n"
            "SliderMultiplier:1.4\r\nSliderTickRate:1\r\n\r\n[TimingPoints]\r\n0,500,4,2,0,100,1,0\r\n\r\n"
            "[HitObjects]\r\n"
            "100,100,1000,5,0,0:0:0:0:\r\n"   // a
            "300,100,2000,1,0,0:0:0:0:\r\n"   // b
            "400,300,2500,1,0,0:0:0:0:\r\n";  // c

        File(path, File::MODE::WRITE).writeLine(original, false);
        auto [read, error] = Document::read(location("editor-snapping-tests.osu"), path);
        TEST_ASSERT(read != nullptr, "the snapping map reads");
        if(!read) return;
        TEST_ASSERT(read->getDistanceSpacing() == 1.3f && read->getGridSize() == 8,
                    "the map's [Editor] spacing and grid size read");

        EditorState state;
        state.open(std::move(read));
        Document &doc = *state.document;
        Compose &compose = state.compose;
        const auto idAt = [&](i32 timeMS) {
            const MapObjects &objects = *state.objects;
            for(uSz i = 0; i < objects.objects.size(); i++) {
                if(objects.objects[i]->getClickTime() == timeMS) return objects.ids[i];
            }
            return Document::Id{0};
        };
        const auto lineOf = [&](Document::Id id) {
            const auto object = doc.getObject(id);
            return object ? neomod::BeatmapFile::format(*object) : std::string{};
        };
        const auto placed = [&](vec2 cursor, const Settings &snap) {
            const auto at = compose.placement(cursor, snap);
            return at ? at->pos : vec2{-1.f};
        };
        const Settings grid{.grid = true, .gridSize = 16};
        const Settings spaced{.distance = true, .spacing = 1.f};
        const Settings both{.grid = true, .gridSize = 16, .distance = true, .spacing = 1.f};
        const Settings angled{.distance = true, .spacing = 1.f, .angle = true};
        const Document::Id b = idAt(2000);

        // placing: at 3500 only the trail behind c is over, the object before is c (its end 1 s ago, so 280 osu!px
        // away)
        state.clock.seek(3500);
        TEST_ASSERT(placed({37.f, 70.f}, grid) == vec2(32.f, 64.f), "a placement on the grid");
        TEST_ASSERT(placed({400.f, 100.f}, spaced) == vec2(400.f, 20.f), "...spaced from the object before");
        TEST_ASSERT(placed({400.f, 100.f}, {.distance = true, .spacing = 0.5f}) == vec2(400.f, 160.f),
                    "...by the spacing");
        TEST_ASSERT(placed({400.f, 100.f}, both) == vec2(400.f, 16.f), "...then on the grid, which wins");
        TEST_ASSERT(placed({430.f, 100.f}, angled) == vec2(400.f, 20.f) &&
                        compose.placement({430.f, 100.f}, angled)->guide.has_value(),
                    "...at 45 degree angles, with the guide line");
        TEST_ASSERT(!compose.placement({600.f, 100.f}, grid), "nothing outside the field");
        state.clock.seek(2000);
        TEST_ASSERT(placed({310.f, 105.f}, spaced) == vec2(300.f, 100.f),
                    "onto a shown object's head within 0.6 of its radius, with distance snap");
        TEST_ASSERT(placed({310.f, 105.f}, {}) == vec2(310.f, 105.f), "...within 0.15 without");

        const auto hovered = compose.spot({200.f, 150.f}, {});
        TEST_ASSERT(
            hovered && !hovered->selected && hovered->pos == vec2(200.f, 150.f) && !hovered->previous && !hovered->next,
            "the readout with nothing selected: the cursor's position, no object's spacing");
        compose.setTool(Compose::Tool::CIRCLE);
        const auto placing = compose.spot({200.f, 150.f}, {});
        TEST_ASSERT(placing && placing->previous && placing->next, "...the circle a click would place's, with it");
        compose.setTool(Compose::Tool::SELECT);
        compose.select({b});
        const auto spot = compose.spot({0.f, 0.f}, {});
        TEST_ASSERT(spot && spot->selected && spot->pos == vec2(300.f, 100.f) && spot->previous && spot->next &&
                        std::abs(*spot->previous - 0.714f) < 0.001f && std::abs(*spot->next - 1.597f) < 0.001f,
                    "...the selection's position and its spacing to the objects before and after it");
        const std::vector<std::string> sampleFiles{"kick.wav", "a very long name.ogg"};
        const auto label = [&](u8 hitSounds, u8 normalSet, u16 index = 0, u8 volume = 0, u16 filename = 0) {
            const neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples{.hitSounds = hitSounds,
                                                                       .normalSet = normalSet,
                                                                       .additionSet = 0,
                                                                       .volume = volume,
                                                                       .index = index,
                                                                       .filename = filename};
            return Timeline::soundLabel(samples, sampleFiles);
        };
        TEST_ASSERT(label(0, 0).empty() && label(1, 0).empty() && label(2 | 8, 0) == "WC" && label(4, 3) == "D:F" &&
                        label(1, 2) == "S",
                    "a timeline label: the hit's own set, then its whistle, finish and clap");
        TEST_ASSERT(label(0, 3, 2) == "D2" && label(8, 0, 2) == "2:C" && label(0, 0, 0, 60) == "60%" &&
                        label(2, 2, 3, 5) == "S3:W 5%",
                    "...its own sample index after the set, and its own volume last");
        TEST_ASSERT(label(0, 2, 2, 0, 1) == "kick" && label(8, 0, 0, 40, 2) == "a very l..:C 40%" &&
                        label(0, 2, 0, 0, 3) == "S",
                    "...the start of its file's name in place of the set and the index");
        TEST_ASSERT(std::abs(compose.currentSpacing().value_or(0.f) - 0.714f) < 0.001f,
                    "the map's spacing here: from the object before the selected one to it");
        compose.clearSelection();
        TEST_ASSERT(std::abs(compose.currentSpacing().value_or(0.f) - 0.714f) < 0.001f,
                    "...without a selection, to the object at the time");
        state.clock.seek(2100);
        TEST_ASSERT(std::abs(compose.currentSpacing().value_or(0.f) - 1.597f) < 0.001f, "...or the next one after it");
        state.clock.seek(3000);
        TEST_ASSERT(!compose.currentSpacing(), "...none after the last object");

        // the distance snap rings at 1500, a beat after a ends: 140 osu!px at 1x, a quarter of it per tick at 1/4
        state.clock.seek(1500);
        state.divisor = 4;
        TEST_ASSERT(!compose.snapRings(spaced), "no rings with the Select tool at rest");
        compose.setTool(Compose::Tool::CIRCLE);
        const auto rings = compose.snapRings({.distance = true, .spacing = 1.f});
        TEST_ASSERT(rings && rings->center == vec2(100.f, 100.f) && std::abs(rings->radius - 140.f) < 0.001f &&
                        std::abs(rings->tick - 35.f) < 0.001f,
                    "placing: around the end of the object before, this time's distance and a tick's");
        TEST_ASSERT(!compose.snapRings({}), "...none without distance snap");
        compose.setTool(Compose::Tool::SELECT);
        state.divisor = 1;
        compose.select({b});

        // dragging b (a and c are shown at 1500)
        state.clock.seek(1500);
        const auto drag = [&](vec2 delta, const Settings &snap) {
            compose.move(delta, snap);
            return lineOf(b);
        };
        const auto undoDrag = [&] {
            compose.endGesture();
            while(doc.canUndo()) state.undo();
        };
        compose.startMove();
        TEST_ASSERT_EQ(drag({5.f, 5.f}, grid), "304,112,2000,1,0,0:0:0:0:", "a drag on the grid");
        TEST_ASSERT_EQ(drag({0.f, 50.f}, spaced), "372,168,2000,1,0,0:0:0:0:", "...spaced from the object before");
        undoDrag();
        compose.startMove();
        TEST_ASSERT_EQ(drag({-198.f, 2.f}, {}), "100,100,2000,1,0,0:0:0:0:",
                       "without distance snap, onto the object before within 0.15 of a radius");
        TEST_ASSERT_EQ(drag({-193.f, 2.f}, {}), "100,100,2000,1,0,0:0:0:0:", "...held there for 8 osu!px");
        TEST_ASSERT_EQ(drag({-188.f, 2.f}, {}), "112,102,2000,1,0,0:0:0:0:", "...then let go");
        TEST_ASSERT_EQ(drag({101.f, 202.f}, {}),
                       "400,300,2000,1,0,0:0:0:0:", "onto another object's head near the dragged one's");
        undoDrag();
        compose.nudge({1.f, 0.f}, grid);
        TEST_ASSERT_EQ(lineOf(b), "304,96,2000,1,0,0:0:0:0:",
                       "Ctrl+arrows with grid snap: to the next grid line, onto the grid the other way");
        compose.nudge({1.f, 0.f}, grid);
        TEST_ASSERT_EQ(lineOf(b), "320,96,2000,1,0,0:0:0:0:", "...and the next");
        compose.endNudge();
        undoDrag();
        TEST_ASSERT_EQ(text(doc), original, "every step undone, the bytes the map started from");
        Environment::deleteFile(path);
    }

    // steps on the objects read only the lines they changed (and make only those objects again, stacking the runs they
    // stack in again), which reads as the map read whole
    void patching() {
        TEST_SECTION("patching");
        using Action = Document::Action;
        using HO = neomod::BeatmapFile::HitObject;
        const std::string path = Mc::Paths::cache() + "/editor-patching-tests.osu";
        // approach rate 5: objects 840 ms apart or less stack (1200 ms times the stack leniency); the slider lasts a
        // second; the break between f and g where the rules fit it, g after it without its new combo written
        const std::string original =
            "osu file format v14\r\n\r\n[General]\r\nAudioFilename: a.mp3\r\nMode: 0\r\n\r\n[Difficulty]\r\n"
            "CircleSize:4\r\nApproachRate:5\r\nSliderMultiplier:1.4\r\nSliderTickRate:1\r\n\r\n[Events]\r\n"
            "//Break Periods\r\n2,6200,14800\r\n\r\n[TimingPoints]\r\n0,500,4,2,0,100,1,0\r\n\r\n[HitObjects]\r\n"
            "100,100,1000,5,0,0:0:0:0:\r\n"        // a
            "200,100,1500,1,0,0:0:0:0:\r\n"        // b
            "300,100,2000,1,0,0:0:0:0:\r\n"        // c
            "300,200,2000,1,0,0:0:0:0:\r\n"        // d, at the time c has
            "64,192,4000,2,0,L|344:192,1,280\r\n"  // e, a slider
            "400,300,6000,1,0,0:0:0:0:\r\n"        // f
            "256,192,16000,1,0,0:0:0:0:\r\n";      // g
        const auto write = [&](std::string_view content) { File(path, File::MODE::WRITE).writeLine(content, false); };
        write(original);
        auto [read, error] = Document::read(location("editor-patching-tests.osu"), path);
        TEST_ASSERT(read != nullptr, "the patching map reads");
        if(!read) return;
        EditorState state;
        state.open(std::move(read));
        Document &doc = *state.document;
        const auto idAt = [&](uSz index) { return state.objects->ids[index]; };
        const Document::Id b = idAt(1), c = idAt(2), e = idAt(4), f = idAt(5), g = idAt(6);
        const auto edit = [&](Action action, Document::Id id, auto &&change) {
            auto object = doc.getObject(id);
            change(*object);
            state.edit(action, [&](Document::Change &ch) { ch.setObject(id, *object); });
        };
        const auto moveBy = [](vec2 by) {
            return [by](HO &object) {
                object.x += by.x;
                object.y += by.y;
                for(vec2 &point : object.curvePoints) point += by;
            };
        };
        const auto at = [](i32 time) { return [time](HO &object) { object.time = time; }; };
        const auto patched = [&] { return doc.getPatched().has_value(); };
        const auto contains = [&](std::string_view part) { return text(doc).find(part) != std::string::npos; };

        const MapObjects *objects = state.objects.get();
        const neomod::HitObject *kept = state.objects->objects[0].get();
        edit(Action::MOVE, b, moveBy({5.f, 0.f}));
        TEST_ASSERT(patched() && doc.getPatched()->objects == std::vector<uSz>{1},
                    "a move reads only the object it moved");
        TEST_ASSERT(state.objects.get() == objects && state.objects->objects[0].get() == kept,
                    "...and makes only that one again");
        TEST_ASSERT_EQ(unlikeRead(doc, *state.objects), std::string{}, "...as reading the map whole would");
        edit(Action::MOVE, b, [](HO &object) {
            object.x = 100;
            object.y = 100;
        });
        TEST_ASSERT(patched() && state.objects->objects[0]->getStack() != 0,
                    "onto the object before it, which stacks under it");
        TEST_ASSERT_EQ(unlikeRead(doc, *state.objects), std::string{}, "...as reading the map whole would");
        edit(Action::MOVE, e, moveBy({0.f, 50.f}));
        edit(Action::HITSOUNDS, f, [](HO &object) { object.hitSounds = 2; });
        TEST_ASSERT(patched(), "a slider moved, a hitsound");
        TEST_ASSERT_EQ(unlikeRead(doc, *state.objects), std::string{}, "...as reading the map whole would");
        state.undo();
        TEST_ASSERT(patched(), "undo reads only what it changed back");
        TEST_ASSERT_EQ(unlikeRead(doc, *state.objects), std::string{}, "...as reading the map whole would");

        edit(Action::NEW_COMBO, b, [](HO &object) { object.type |= HO::TYPE_NEW_COMBO; });
        TEST_ASSERT(patched() && state.objects->objects[2]->getComboNumber() == 2,
                    "a new combo, the combos after it counted again");
        TEST_ASSERT_EQ(unlikeRead(doc, *state.objects), std::string{}, "...as reading the map whole would");
        edit(Action::MOVE, b, at(1600));
        TEST_ASSERT(patched(), "a time");
        TEST_ASSERT_EQ(unlikeRead(doc, *state.objects), std::string{}, "...as reading the map whole would");
        edit(Action::MOVE, b, at(2500));
        TEST_ASSERT(patched() && contains("300,200,2000,1,0,0:0:0:0:\r\n100,100,2500,") && idAt(3) == b,
                    "a time past other objects, its line among theirs in time order");
        TEST_ASSERT_EQ(unlikeRead(doc, *state.objects), std::string{}, "...as reading the map whole would");
        edit(Action::MOVE, c, moveBy({5.f, 0.f}));
        TEST_ASSERT(patched() && contains("300,200,2000,1,0,0:0:0:0:\r\n305,100,2000"),
                    "a move of an object followed by one at its time, whose line it goes after");
        TEST_ASSERT_EQ(unlikeRead(doc, *state.objects), std::string{}, "...as reading the map whole would");
        edit(Action::MOVE, g, at(15000));
        TEST_ASSERT(patched() && contains("2,6200,13800") && contains("256,192,15000,5,"),
                    "a gap made shorter: its break fitted to it, the new combo after it written");
        TEST_ASSERT_EQ(unlikeRead(doc, *state.objects), std::string{}, "...as reading the map whole would");
        edit(Action::MOVE, f, at(10000));
        TEST_ASSERT(!patched() && contains("2,5200,8800") && contains("2,10200,13800") &&
                        unlikeRead(doc, *state.objects).empty(),
                    "a break put in (5 s on both sides of f, one gap keeping the break) reads the map whole");

        // a drag in time: each update taken back, the line placed again
        state.undo();
        const u64 gesture = 1234;
        bool everyUpdate = true;
        for(const i32 time : {1700, 2600, 3500, 900, 2100}) {
            auto object = doc.getObject(b);
            object->time = time;
            state.drag(Action::MOVE, [&](Document::Change &ch) { ch.setObject(b, *object); }, gesture);
            everyUpdate = everyUpdate && patched();
        }
        TEST_ASSERT(everyUpdate, "a drag in time reads only what each update changed");
        TEST_ASSERT_EQ(unlikeRead(doc, *state.objects), std::string{}, "...as reading the map whole would");
        while(doc.canUndo()) state.undo();
        TEST_ASSERT(text(doc) == original && unlikeRead(doc, *state.objects).empty(), "every step undone");

        // sample files are numbered in the order the lines first name them
        const auto &files = doc.getPrimitives().sampleFiles;
        const std::vector<std::string> kick{"kick.wav"}, snareKick{"snare.wav", "kick.wav"};
        edit(Action::SAMPLE_FILE, c, [](HO &object) { object.sample.filename = "kick.wav"; });
        TEST_ASSERT(!patched() && files == kick, "a line that names a sample file reads the map whole");
        edit(Action::SAMPLE_FILE, b, [](HO &object) { object.sample.filename = "snare.wav"; });
        TEST_ASSERT(!patched() && files == snareKick, "...and so does one that names another, numbered before it");
        TEST_ASSERT_EQ(unlikeRead(doc, *state.objects), std::string{}, "...as reading the map whole would");
        edit(Action::HITSOUNDS, b, [](HO &object) { object.hitSounds = 4; });
        TEST_ASSERT(patched(), "a line that keeps its sample file is read alone");
        TEST_ASSERT_EQ(unlikeRead(doc, *state.objects), std::string{}, "...as reading the map whole would");
        edit(Action::SAMPLE_FILE, b, [](HO &object) { object.sample.filename = "kick.wav"; });
        TEST_ASSERT(!patched() && files == kick, "a file no line names any more is gone");
        TEST_ASSERT_EQ(unlikeRead(doc, *state.objects), std::string{}, "...as reading the map whole would");
        while(doc.canUndo()) state.undo();

        // a line in [HitObjects] that's no object, which the step end places changed lines around
        const std::string odd = original + "junk\r\n";
        write(odd);
        auto [junked, junkError] = Document::read(location("editor-patching-tests.osu"), path);
        if(junked) {
            state.close();
            state.open(std::move(junked));
            const Document::Id first = state.objects->ids[0];
            auto object = state.document->getObject(first);
            object->x += 1;
            state.edit(Action::MOVE, [&](Document::Change &ch) { ch.setObject(first, *object); });
            TEST_ASSERT(!state.document->getPatched() && unlikeRead(*state.document, *state.objects).empty(),
                        "a map with a line in [HitObjects] that's no object is read whole");
        }
        Environment::deleteFile(path);
    }

    // the element named `name` under `root`
    static CBaseUIElement *find(CBaseUIElement *root, std::string_view name) {
        if(root->getName() == name) return root;
        for(CBaseUIElement *child : root->getAllChildren()) {
            if(CBaseUIElement *found = find(child, name)) return found;
        }
        return nullptr;
    }

    void setup() {
        TEST_SECTION("setup");
        const std::string path = Mc::Paths::cache() + "/editor-setup-tests.osu";
        // no approach rate, so the overall difficulty 7 applies: 900 ms. a slider of two beats (2000 to 3000), then a
        // break fitted to its gap (3000 to 10000)
        const std::string original =
            "osu file format v14\r\n\r\n[General]\r\nAudioFilename: a.mp3\r\nMode: 0\r\n\r\n[Metadata]\r\n"
            "Title:Song\r\nArtist:Band\r\nCreator:me\r\nVersion:Hard\r\n\r\n[Difficulty]\r\nHPDrainRate:5\r\n"
            "CircleSize:4\r\nOverallDifficulty:7\r\nSliderMultiplier:1.4\r\nSliderTickRate:1\r\n\r\n[Events]\r\n"
            "//Break Periods\r\n2,3200,9100\r\n\r\n[TimingPoints]\r\n0,500,4,2,0,100,1,0\r\n\r\n[HitObjects]\r\n"
            "64,192,2000,6,0,L|344:192,1,280\r\n256,192,10000,5,0,0:0:0:0:\r\n";
        File(path, File::MODE::WRITE).writeLine(original, false);
        auto [read, error] = Document::read(location("editor-setup-tests.osu"), path);
        TEST_ASSERT(read != nullptr, "the setup map reads");
        if(!read) return;

        EditorState state;
        state.open(std::move(read));
        Document &doc = *state.document;
        CBaseUIPopupLayer popups("popups");
        SetupPanel panel(state, popups);
        panel.setSize(1200.f, 500.f);
        panel.tick();

        const auto text = [&](std::string_view name) {
            return static_cast<CBaseUIStyledTextbox *>(find(&panel, name));
        };
        const auto number = [&](std::string_view name) {
            return static_cast<CBaseUINumberField *>(find(&panel, name));
        };
        const auto type = [&](CBaseUIStyledTextbox *field, std::u32string_view characters) {
            for(const char32_t character : characters) {
                KeyboardEvent e{0, 0, character, 0, KEYMOD_NONE};
                field->onChar(e);
            }
        };
        const auto press = [&](CBaseUIElement *target, SCANCODE scancode, KEYMOD modifiers = KEYMOD_NONE) {
            KeyboardEvent e{scancode, 0, 0, 0, modifiers};
            target->onKeyDown(e);
            return e.isConsumed();
        };
        const auto section = [&](std::string_view header) {
            const std::string_view all = EditorTests::text(doc);
            const uSz from = all.find(header) + header.size() + 2;
            return all.substr(from, all.find("\r\n\r\n", from) - from);
        };
        const auto undoAll = [&] {
            while(doc.canUndo()) state.undo();
            panel.tick();
        };

        CBaseUIStyledTextbox *title = text("editor_setup_title");
        CBaseUIStyledTextbox *romanised = text("editor_setup_title_romanised");
        TEST_ASSERT(title && romanised && title->getText() == "Song" && romanised->getText() == "Song" &&
                        !romanised->isEnabled(),
                    "a field shows what the file has; a name's romanised form follows it while it's ASCII");

        // typing
        title->visit();
        type(title, U"New");
        panel.tick();
        TEST_ASSERT(title->getText() == "New" && EditorTests::text(doc) == original && !doc.canUndo(),
                    "what's typed into a field is the field's until its visit ends");
        press(title, KEY_ENTER);
        panel.tick();
        TEST_ASSERT_EQ(section("[Metadata]"), "Title:New\r\nArtist:Band\r\nCreator:me\r\nVersion:Hard",
                       "an ASCII name typed is its romanised form too");
        TEST_ASSERT_EQ(romanised->getText(), "New", "...which its romanised field shows");
        state.undo();
        TEST_ASSERT(EditorTests::text(doc) == original && !doc.canUndo(), "a visit to a field is one step");
        panel.tick();

        title->visit();
        type(title, U"New");
        press(title, KEY_ESCAPE);
        TEST_ASSERT(EditorTests::text(doc) == original && !doc.canUndo(), "...and none when Escape takes it back");
        title->visit();
        type(title, U"Song");
        press(title, KEY_ENTER);
        TEST_ASSERT(!doc.canUndo(), "...or when what was typed is what it had");
        title->visit();
        type(title, U"New");
        panel.commit();
        TEST_ASSERT(section("[Metadata]").starts_with("Title:New\r\n") && title->isFocused(),
                    "committed, what's typed is a step with the visit going on");
        type(title, U"er");
        press(title, KEY_ESCAPE);
        TEST_ASSERT(section("[Metadata]").starts_with("Title:New\r\n") && title->getText() == "New",
                    "...which Escape takes back to there");
        undoAll();

        title->visit();
        press(title, KEY_END);
        type(title, U"é");
        press(title, KEY_ENTER);
        panel.tick();
        TEST_ASSERT_EQ(section("[Metadata]"),
                       "Title:Song\r\nArtist:Band\r\nCreator:me\r\nVersion:Hard\r\nTitleUnicode:Songé",
                       "a name that isn't ASCII gets a key of its own and leaves its romanised form as it was");
        TEST_ASSERT(romanised->isEnabled(), "...for its own field to set");
        romanised->visit();
        type(romanised, U"eé");
        press(romanised, KEY_ENTER);
        TEST_ASSERT_EQ(section("[Metadata]"),
                       "Title:e\r\nArtist:Band\r\nCreator:me\r\nVersion:Hard\r\nTitleUnicode:Songé",
                       "...which takes ASCII only");
        undoAll();

        // Tab
        title->visit();
        TEST_ASSERT(!press(title, KEY_TAB) && press(&panel, KEY_TAB) && text("editor_setup_creator")->isFocused(),
                    "Tab goes to the next field that can be typed in");
        press(&panel, KEY_TAB, KEYMOD_LSHIFT);
        TEST_ASSERT(title->isFocused() && title->hasSelectedText(), "...and Shift+Tab back, with its text selected");
        type(title, U"é");
        press(&panel, KEY_TAB);
        TEST_ASSERT(romanised->isFocused() && section("[Metadata]").ends_with("TitleUnicode:é"),
                    "Tab ends the visit first, so a name typed that isn't ASCII is followed by its romanised field");
        romanised->stealFocus();
        undoAll();

        // numbers
        CBaseUINumberField *ar = number("editor_setup_ar");
        TEST_ASSERT_EQ(ar->getValue(), 7.0, "a file without an approach rate has its overall difficulty's");
        ar->visit();
        type(ar, U"9");
        press(ar, KEY_ENTER);
        TEST_ASSERT_EQ(section("[Difficulty]"),
                       "HPDrainRate:5\r\nCircleSize:4\r\nOverallDifficulty:7\r\nSliderMultiplier:1.4\r\n"
                       "SliderTickRate:1\r\nApproachRate:9",
                       "an approach rate set is written");
        TEST_ASSERT_EQ(section("[Events]"), "//Break Periods\r\n2,3200,9400", "...and the breaks end by it");
        state.undo();
        TEST_ASSERT(EditorTests::text(doc) == original, "...in one step");
        panel.tick();

        CBaseUINumberField *hp = number("editor_setup_hp");
        hp->visit();
        press(hp, KEY_UP);
        press(hp, KEY_UP);
        press(hp, KEY_UP);
        press(hp, KEY_ENTER);
        TEST_ASSERT(section("[Difficulty]").starts_with("HPDrainRate:5.3\r\n"), "steps add up to round numbers");
        state.undo();
        TEST_ASSERT(EditorTests::text(doc) == original, "...and a visit's steps are one");
        panel.tick();

        CBaseUINumberField *velocity = number("editor_setup_slider_velocity");
        TEST_ASSERT_EQ(state.objects->objects.front()->getEndTime(), 3000, "a slider of two beats");
        velocity->visit();
        type(velocity, U"2.8");
        press(velocity, KEY_ENTER);
        TEST_ASSERT(EditorTests::text(doc).contains("SliderMultiplier:2.8\r\n") &&
                        EditorTests::text(doc).contains("64,192,2000,6,0,L|344:192,1,280\r\n"),
                    "a slider velocity set leaves the sliders' lengths alone");
        TEST_ASSERT_EQ(state.objects->objects.front()->getEndTime(), 2500, "...so that they end sooner");
        TEST_ASSERT_EQ(section("[Events]"), "//Break Periods\r\n2,2700,9100",
                       "...and the break after one starts sooner");
        undoAll();
        state.edit(Document::Action::DIFFICULTY, [](Document::Change &c) {
            c.setValue(neomod::BeatmapFile::SectionKind::DIFFICULTY, "SliderMultiplier", std::nullopt);
        });
        panel.tick();
        TEST_ASSERT_EQ(velocity->getValue(), 1.0, "...without one, the velocity the game plays the map at");
        undoAll();
        // in the map folder, where it would be copied
        const std::string folder = Mc::Paths::maps() + "/set";
        const std::string quoted = folder + "/a \"quoted\" name.png";
        File(quoted, File::MODE::WRITE).writeLine("png", false);
        panel.useBackground(quoted);
        Environment::deletePathsRecursive(folder);
        TEST_ASSERT(EditorTests::text(doc) == original && !doc.canUndo(),
                    "an image whose name can't be written into the map's line (a quote) isn't its background");

        // toggles and choices
        auto *widescreen = static_cast<CBaseUIStyledButton *>(find(&panel, "editor_setup_widescreen"));
        auto *epilepsy = static_cast<CBaseUIStyledButton *>(find(&panel, "editor_setup_epilepsy"));
        widescreen->click();
        epilepsy->click();
        panel.tick();
        TEST_ASSERT_EQ(section("[General]"),
                       "AudioFilename: a.mp3\r\nMode: 0\r\nWidescreenStoryboard: 1\r\nEpilepsyWarning: 1",
                       "a toggle switched on is written");
        TEST_ASSERT(widescreen->isChecked() && epilepsy->isChecked(), "...and shows");
        widescreen->click();
        epilepsy->click();
        TEST_ASSERT_EQ(section("[General]"), "AudioFilename: a.mp3\r\nMode: 0\r\nWidescreenStoryboard: 0",
                       "switched off, a key osu! always writes stays, another goes");
        undoAll();
        TEST_ASSERT(EditorTests::text(doc) == original, "all of it undone, the file is as it was");

        // colours: the first combo has the second colour in the file. the key spelling another writer used stays while
        // its colour does
        const std::string coloured =
            "osu file format v14\r\n\r\n[General]\r\nAudioFilename: a.mp3\r\nMode: 0\r\n\r\n[Difficulty]\r\n"
            "CircleSize:4\r\n\r\n[TimingPoints]\r\n0,500,4,2,0,100,1,0\r\n\r\n[Colours]\r\nCombo1 : 255,0,0\r\n"
            "Combo2 : 0,255,0\r\nCombo3: 0,0,255\r\n\r\n[HitObjects]\r\n256,192,1000,5,0,0:0:0:0:\r\n";
        File(path, File::MODE::WRITE).writeLine(coloured, false);
        auto [readColoured, colouredError] = Document::read(location("editor-setup-tests.osu"), path);
        TEST_ASSERT(readColoured != nullptr, "the coloured map reads");
        if(!readColoured) return;
        state.close();
        state.open(std::move(readColoured));
        Document &colours = *state.document;
        panel.tick();
        const auto colourLines = [&] {
            const std::string_view all = EditorTests::text(colours);
            const uSz from = all.find("[Colours]\r\n") + 11;
            return all.substr(from, all.find("\r\n\r\n", from) - from);
        };
        const auto click = [&](std::string_view name) {
            static_cast<CBaseUIStyledButton *>(find(&panel, name))->click();
            panel.tick();
        };

        const auto colour = [&](std::string_view name) {
            return static_cast<CBaseUIColorButton *>(find(&panel, name));
        };
        TEST_ASSERT(colour("editor_setup_colour_1")->getColor() == rgb(0, 255, 0) &&
                        colour("editor_setup_colour_3")->getColor() == rgb(255, 0, 0) &&
                        !find(&panel, "editor_setup_colour_row_4")->isVisible(),
                    "the combo colours show in the order combos get them");
        // a colour picker opens in the popups: its fields are typed in there
        popups.setSize(1200.f, 500.f);
        colour("editor_setup_colour_1")->open();
        const auto channel = [&](std::string_view name) {
            return static_cast<CBaseUINumberField *>(find(&popups, name));
        };
        CBaseUINumberField *red = channel("editor_setup_colour_1_picker_r");
        red->visit();
        type(red, U"9");
        press(red, KEY_ENTER);
        TEST_ASSERT_EQ(colourLines(), "Combo1 : 255,0,0\r\nCombo2 : 9,255,0\r\nCombo3: 0,0,255",
                       "a colour changed is the only line written");
        panel.tick();
        CBaseUINumberField *green = channel("editor_setup_colour_1_picker_g");
        green->visit();
        type(green, U"200");
        press(green, KEY_ENTER);
        TEST_ASSERT_EQ(colourLines(), "Combo1 : 255,0,0\r\nCombo2 : 9,200,0\r\nCombo3: 0,0,255",
                       "...and changed again while its picker stays open");
        colour("editor_setup_colour_1")->close();
        popups.tick();
        state.undo();
        panel.tick();
        TEST_ASSERT(EditorTests::text(colours) == coloured && !colours.canUndo(),
                    "one visit of a colour's picker is one step");
        click("editor_setup_colour_1_remove");
        TEST_ASSERT_EQ(colourLines(), "Combo1 : 255,0,0\r\nCombo2 : 0,0,255",
                       "a colour removed, the ones after it move up");
        click("editor_setup_colour_add");
        TEST_ASSERT_EQ(colourLines(), "Combo1 : 240,240,240\r\nCombo2 : 0,0,255\r\nCombo3 : 255,0,0",
                       "a colour added is the last one combos get, which the file has first");
        panel.tick();
        TEST_ASSERT(colour("editor_setup_colour_3")->getColor() == rgb(240, 240, 240), "...and the last one shown");
        state.undo();
        state.undo();
        TEST_ASSERT(EditorTests::text(colours) == coloured && !colours.canUndo(),
                    "undone, the colours are as they were");

        // the background: whether the map events become `events` with the image chosen (which the document reads as its
        // background then), undone exactly
        const auto background = [](std::string_view sections, std::string_view events) {
            const std::string original =
                fmt::format("osu file format v14\r\n\r\n{}[TimingPoints]\r\n0,500,4,2,0,100,1,0\r\n", sections);
            auto map = mapOf(original);
            map->change(Document::Action::BACKGROUND, [](Document::Change &c) { c.setBackground("new.png"); });
            const bool chosen = sectionOf(*map, "[Events]") == events && map->getBackground() == "new.png";
            map->undo();
            return chosen && EditorTests::text(*map) == original;
        };
        TEST_ASSERT(background("[Events]\r\n0,0,\"old.jpg\",0,0\r\n//Break Periods\r\n2,100,9000\r\n\r\n",
                               "0,0,\"new.png\",0,0\r\n//Break Periods\r\n2,100,9000"),
                    "a map's background is replaced by the image chosen (undone exactly)");
        TEST_ASSERT(background("[Events]\r\n//Background and Video events\r\n//Break Periods\r\n\r\n",
                               "//Background and Video events\r\n0,0,\"new.png\",0,0\r\n//Break Periods"),
                    "...a map without one gets it under the comment for it");
        TEST_ASSERT(background("[Events]\r\nVideo,0,\"v.avi\"\r\n2,100,9000\r\n\r\n",
                               "0,0,\"new.png\",0,0\r\nVideo,0,\"v.avi\"\r\n2,100,9000"),
                    "...else first in its events");
        TEST_ASSERT(background("", "0,0,\"new.png\",0,0"), "...in events of its own without any");
        TEST_ASSERT_EQ(
            std::string{mapOf("osu file format v14\r\n\r\n[Events]\r\n0,0,\"a.jpg\",0,0\r\n0,0,\"b.jpg\",0,0\r\n\r\n"
                              "[TimingPoints]\r\n0,500,4,2,0,100,1,0\r\n")
                            ->getBackground()},
            "a.jpg", "a map's background is its first background event's");
    }

    // a map of `text`, in memory
    static std::unique_ptr<Document> mapOf(std::string_view text) {
        return Document::read(location("editor-tests.osu"), "",
                              {reinterpret_cast<const u8 *>(text.data()), text.size()})
            .document;
    }
    // the lines in the section under `header`
    static std::string_view sectionOf(const Document &document, std::string_view header) {
        const std::string_view all = text(document);
        const uSz from = all.find(header) + header.size() + 2;
        const uSz to = all.find("\r\n\r\n", from);
        return all.substr(from, to == std::string::npos ? to : to - from);
    }

    void history() {
        TEST_SECTION("history");
        using Changes = EditorHistory::Changes;
        const auto bytes = [](std::string_view text) {
            return std::span<const u8>{reinterpret_cast<const u8 *>(text.data()), text.size()};
        };
        const auto md5 = [&](std::string_view text) { return crypto::hash::md5(bytes(text)); };
        const std::string head = "osu file format v14\r\n\r\n[General]\r\nAudioFilename: ";
        const std::string timing =
            "\r\nMode: 0\r\n\r\n[Editor]\r\nBeatDivisor: 4\r\n\r\n[TimingPoints]\r\n0,500,4,2,0,100,1,0\r\n";
        const std::string a =
            head + "a.mp3" + timing + "\r\n[HitObjects]\r\n100,100,1000,5,0,0:0:0:0:\r\n200,100,1500,1,0,0:0:0:0:\r\n";
        // the second object moved and a third one, an inherited point, another audio file and another divisor
        const std::string b = head + "b.mp3" +
                              "\r\nMode: 0\r\n\r\n[Editor]\r\nBeatDivisor: 8\r\n\r\n[TimingPoints]\r\n"
                              "0,500,4,2,0,100,1,0\r\n1000,-50,4,2,0,100,0,0\r\n\r\n[HitObjects]\r\n"
                              "100,100,1000,5,0,0:0:0:0:\r\n300,100,1500,1,0,0:0:0:0:\r\n300,200,2000,1,0,0:0:0:0:\r\n";
        TEST_ASSERT(EditorHistory::compare(bytes(a), bytes(a)) == Changes{}, "a map compared with itself: nothing");
        TEST_ASSERT(
            (EditorHistory::compare(bytes(a), bytes(b)) ==
             Changes{.objectsAdded = 2, .objectsRemoved = 1, .timingAdded = 1, .timingRemoved = 0, .otherLines = 2}),
            "two maps compared by lines: objects, timing points and the rest, [Editor] left out");
        TEST_ASSERT(
            (EditorHistory::compare(bytes(b), bytes(a)) ==
             Changes{.objectsAdded = 1, .objectsRemoved = 2, .timingAdded = 0, .timingRemoved = 1, .otherLines = 2}),
            "...the other way around");

        const std::string c = b + "400,200,2500,1,0,0:0:0:0:\r\n";
        const std::string d = c + "400,300,3000,1,0,0:0:0:0:\r\n";
        const std::string e = d + "400,350,3500,1,0,0:0:0:0:\r\n";
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::remove_all(File::getFsPath(Mc::Paths::editor() + "/history/editor-history-tests"), ec);
        const auto at = [](std::string file) {
            return Database::MapLocation{
                .root = Database::MapRoot::Neomod, .folder = "editor-history-tests", .file = std::move(file)};
        };
        // a first save of a.osu
        EditorHistory::record("a.osu", md5(a), bytes(a), at("a.osu"), bytes(b), 1000);
        auto versions = EditorHistory::versionsOf(at("a.osu"), md5(b));
        TEST_ASSERT(versions.size() == 2 && versions[0].saved && versions[0].time == 1000 &&
                        versions[0].md5 == md5(b) && versions[0].objects == 3 &&
                        versions[0].changes == EditorHistory::compare(bytes(a), bytes(b)) && !versions[1].saved &&
                        versions[1].md5 == md5(a) && versions[1].objects == 2,
                    "a map's first save keeps what it wrote, and what it overwrote before it");
        if(versions.size() == 2) {
            const auto first = EditorHistory::read(versions[1]);
            const auto second = EditorHistory::read(versions[0]);
            TEST_ASSERT(first && Mc::ranges::equal(*first, bytes(a)) && second && Mc::ranges::equal(*second, bytes(b)),
                        "...each read back as it was");
        }
        // a save that renamed the file, then one after another program changed it
        EditorHistory::record("a.osu", md5(b), bytes(b), at("renamed.osu"), bytes(c), 2000);
        TEST_ASSERT(EditorHistory::versionsOf(at("renamed.osu"), md5(c)).size() == 3,
                    "the next save adds one version, also under another file name");
        EditorHistory::record("renamed.osu", md5(d), bytes(d), at("renamed.osu"), bytes(e), 3000);
        versions = EditorHistory::versionsOf(at("renamed.osu"), md5(e));
        TEST_ASSERT(versions.size() == 5 && versions[0].md5 == md5(e) && !versions[1].saved &&
                        versions[1].md5 == md5(d) &&
                        versions[1].changes == EditorHistory::compare(bytes(c), bytes(d)) &&
                        versions[2].md5 == md5(c) && versions[4].md5 == md5(a),
                    "content the editor didn't write is kept before the save over it, with the same difficulty");
        // renamed by another program
        EditorHistory::record("other.osu", md5(e), bytes(e), at("other.osu"), bytes(a), 4000);
        TEST_ASSERT(EditorHistory::versionsOf(at("other.osu"), md5(a)).size() == 6,
                    "a file with the latest version's content continues that difficulty, whatever it's called");
        EditorHistory::record("new.osu", md5("x\r\n"), bytes("x\r\n"), at("new.osu"), bytes("y\r\n"), 5000);
        TEST_ASSERT(EditorHistory::versionsOf(at("new.osu"), md5("y\r\n")).size() == 2 &&
                        EditorHistory::versionsOf(at("other.osu"), md5(a)).size() == 6,
                    "...and one with neither starts its own");
        TEST_ASSERT(EditorHistory::versionsOf(at("unknown.osu"), md5("z")).empty(), "a map never saved here has none");
        // two new difficulties that started with the same content
        EditorHistory::record("fresh.osu", md5("p\r\n"), bytes("p\r\n"), at("hard.osu"), bytes("q\r\n"), 6000);
        EditorHistory::record("fresh.osu", md5("p\r\n"), bytes("p\r\n"), at("normal.osu"), bytes("r\r\n"), 7000);
        TEST_ASSERT(EditorHistory::versionsOf(at("hard.osu"), md5("q\r\n")).size() == 2 &&
                        EditorHistory::versionsOf(at("normal.osu"), md5("r\r\n")).size() == 2,
                    "difficulties that started alike keep their versions apart");
        // files that aren't versions
        {
            const auto listed = EditorHistory::versionsOf(at("normal.osu"), md5("r\r\n"));
            if(listed.empty()) return;
            // the latest one again, saying its content has sizes no file has
            std::string damaged = File(listed[0].path).readToString();
            const uSz packed = Mc::Deflate::pack(bytes("r\r\n")).size();
            if(damaged.size() < packed + 8) return;
            damaged.replace(damaged.size() - packed - 8, 8, 8, '\xFF');
            const std::string dir = Environment::getFolderFromFilePath(listed[0].path);
            File(dir + "damaged.snap", File::MODE::WRITE).writeLine(damaged, false);
            File(dir + "half-written.snap.tmp", File::MODE::WRITE).writeLine(damaged, false);
            const auto again = EditorHistory::versionsOf(at("normal.osu"), md5("r\r\n"));
            const auto bad = Mc::ranges::find_if(again, [](const auto &v) { return v.path.ends_with("damaged.snap"); });
            TEST_ASSERT(again.size() == 3 && bad != again.end() && !EditorHistory::read(*bad),
                        "a .tmp is no version, and a version with sizes its file can't hold reads as none");
        }
        fs::remove_all(File::getFsPath(Mc::Paths::editor() + "/history/editor-history-tests"), ec);

        // restoring a version is a step
        auto doc = mapOf(a);
        TEST_ASSERT(doc != nullptr, "the history map reads");
        if(!doc) return;
        TEST_ASSERT(
            doc->restore({b.begin(), b.end()}) && text(*doc) == b && doc->getUndoAction() == Document::Action::RESTORE,
            "a restored version is the document's content");
        const auto touched = doc->undo();
        TEST_ASSERT_EQ(text(*doc), a, "...until it's undone");
        TEST_ASSERT(touched && touched->empty(), "...which touches no line in particular: nothing to select");
    }

    void verify() {
        TEST_SECTION("verify");
        using Kind = Verify::Kind;
        using Severity = Verify::Severity;
        // kind, severity and time for each, in order
        const auto kind = [](Kind kind, Severity severity, i32 time = -1) {
            return fmt::format("{}{}@{} ", (int)kind, "PWM"[(uSz)severity], time);
        };
        const auto found = [&](const Document &document, std::string_view folder) {
            const MapObjects objects{document};
            std::string out;
            for(const Verify::Issue &issue : Verify::check(document, objects, folder)) {
                out += kind(issue.kind, issue.severity, issue.time.value_or(-1));
            }
            return out;
        };
        const std::string head = "osu file format v14\r\n\r\n[General]\r\nAudioFilename: a.mp3\r\nMode: 0\r\n";
        const std::string timing = "[TimingPoints]\r\n0,500,4,2,0,100,1,0\r\n";

        // a circle on every beat for 31 s
        std::string clean =
            head +
            "PreviewTime: 1500\r\n\r\n[Difficulty]\r\nHPDrainRate:5\r\nCircleSize:4\r\n"
            "OverallDifficulty:5\r\nApproachRate:9.3\r\nSliderMultiplier:1.4\r\nSliderTickRate:1\r\n\r\n"
            "[Events]\r\n0,0,\"bg.jpg\",0,0\r\n\r\n" +
            timing + "\r\n[HitObjects]\r\n";
        for(i32 time = 1000; time <= 32000; time += 500) {
            clean += fmt::format("256,192,{},{},0,0:0:0:0:\r\n", time, time == 1000 ? 5 : 1);
        }
        const auto cleanMap = mapOf(clean);
        TEST_ASSERT(cleanMap != nullptr, "the clean map reads");
        if(cleanMap) {
            TEST_ASSERT_EQ(found(*cleanMap, ""), std::string{}, "a map with nothing wrong has no issues");
            TEST_ASSERT_EQ(
                found(*cleanMap, Mc::Paths::cache() + "/no-such-folder"),
                kind(Kind::MISSING_BACKGROUND, Severity::PROBLEM) + kind(Kind::MISSING_AUDIO, Severity::PROBLEM),
                "its files are looked for in its folder");
        }

        const auto flawed =
            mapOf(head +
                  "\r\n[Difficulty]\r\nHPDrainRate:5\r\nCircleSize:4.25\r\nOverallDifficulty:5\r\nApproachRate:11\r\n"
                  "SliderMultiplier:1.4\r\nSliderTickRate:1\r\n\r\n" +
                  timing +
                  "8000,-100,4,2,0,15,0,0\r\n8000,-100,4,2,0,15,0,0\r\n8400,-100,4,2,0,100,0,0\r\n\r\n[HitObjects]\r\n"
                  // 3 ms late; 1.3 ms after a twelfth; off the right edge; two at once; two 5 ms apart
                  "256,192,1003,5,0,0:0:0:0:\r\n256,192,1543,1,0,0:0:0:0:\r\n560,192,2000,1,0,0:0:0:0:\r\n"
                  "100,100,2500,1,0,0:0:0:0:\r\n200,100,2500,1,0,0:0:0:0:\r\n"
                  "100,300,3000,1,0,0:0:0:0:\r\n200,300,3005,1,0,0:0:0:0:\r\n"
                  // a quarter second of spinner without a new combo; a slider that leaves the screen on its way
                  "256,192,4000,8,0,4250,0:0:0:0:\r\n400,192,5000,2,0,L|610:192,1,210\r\n"
                  // a muted circle; one under a quiet timing point; a slider ending between ticks
                  "256,192,7000,1,0,0:0:0:3:\r\n256,192,8000,1,0,0:0:0:0:\r\n100,100,9000,2,0,L|180:100,1,80\r\n");
        TEST_ASSERT(flawed != nullptr, "the flawed map reads");
        if(!flawed) return;
        const MapObjects objects{*flawed};
        const std::vector<Verify::Issue> issues = Verify::check(*flawed, objects, "");
        const auto at = [&](Kind wanted) {
            const auto it = Mc::ranges::find(issues, wanted, &Verify::Issue::kind);
            return it != issues.end() ? *it : Verify::Issue{.kind = wanted, .severity = Severity::MINOR};
        };
        i32 leaves = -1;
        for(const Verify::Issue &issue : issues) {
            if(issue.kind == Kind::OFFSCREEN && issue.time != 2000) leaves = issue.time.value_or(-1);
        }
        const i32 ends = at(Kind::UNSNAPPED_END).time.value_or(-1);
        TEST_ASSERT_EQ(
            found(*flawed, ""),
            kind(Kind::NO_PREVIEW, Severity::PROBLEM) + kind(Kind::NO_BACKGROUND, Severity::PROBLEM) +
                kind(Kind::SETTING_DECIMALS, Severity::PROBLEM) + kind(Kind::SETTING_RANGE, Severity::WARNING) +
                kind(Kind::SHORT_DRAIN, Severity::PROBLEM) + kind(Kind::UNSNAPPED, Severity::PROBLEM, 1003) +
                kind(Kind::UNSNAPPED, Severity::MINOR, 1543) + kind(Kind::OFFSCREEN, Severity::PROBLEM, 2000) +
                kind(Kind::CONCURRENT, Severity::PROBLEM, 2500) +
                kind(Kind::ALMOST_CONCURRENT, Severity::PROBLEM, 3000) +
                kind(Kind::UNSNAPPED, Severity::PROBLEM, 3005) + kind(Kind::SHORT_SPINNER, Severity::PROBLEM, 4000) +
                kind(Kind::SPINNER_WITHOUT_NEW_COMBO, Severity::WARNING, 4000) +
                kind(Kind::OFFSCREEN, Severity::PROBLEM, leaves) + kind(Kind::QUIET, Severity::PROBLEM, 7000) +
                kind(Kind::QUIET, Severity::WARNING, 8000) + kind(Kind::TIMING_OVERLAP, Severity::PROBLEM, 8000) +
                kind(Kind::UNSNAPPED_END, Severity::PROBLEM, ends),
            "each flaw is found once, the whole map's first, the others by time");
        // the slider edge passes x 543.6 (the screen width 579 less the circle radius) 143.6 osu!px in, out of 210 in
        // 750 ms
        TEST_ASSERT(leaves >= 5510 && leaves <= 5520, "a slider's issue is where it leaves the screen");
        TEST_ASSERT(ends == 9285 || ends == 9286, "...and an unsnapped end's at the end");
        TEST_ASSERT(std::abs(at(Kind::UNSNAPPED).value - 3.0) < 0.001, "how far off the grid, in ms");
        TEST_ASSERT(at(Kind::SETTING_DECIMALS).setting == Verify::Setting::CS &&
                        at(Kind::SETTING_RANGE).setting == Verify::Setting::AR,
                    "which setting");
        TEST_ASSERT(at(Kind::CONCURRENT).lines[0] == objects.ids[3] && at(Kind::CONCURRENT).lines[1] == objects.ids[4],
                    "both objects of a pair");
        TEST_ASSERT_EQ((i32)at(Kind::QUIET).value, 3, "a hit's own volume");
        TEST_ASSERT(at(Kind::TIMING_OVERLAP).lines[0] == flawed->getTimingPointIds()[1] &&
                        at(Kind::TIMING_OVERLAP).lines[1] == flawed->getTimingPointIds()[2],
                    "both timing points");

        const BeatSnap &snap = flawed->getBeatSnap();
        TEST_ASSERT(snap.offGrid(1000.0) < 0.001 && std::abs(snap.offGrid(1041.0) - 0.6667) < 0.001 &&
                        std::abs(snap.offGrid(1031.0) - 0.25) < 0.001,
                    "off the grid: the distance to the nearest twelfth or sixteenth");
        const std::array sections{uninherited(0.0, 500.0), uninherited(10010.0, 400.0)};
        TEST_ASSERT(std::abs(BeatSnap{sections}.offGrid(10009.5) - 0.5) < 0.001 &&
                        std::abs(BeatSnap{sections}.offGrid(10004.0) - 4.0) < 0.001,
                    "...or to the start of the next section, if that's nearer");

        // only when asked for: every map in a corpus checked, for what real maps do to the checks
        if(const auto dir = Mc::Tests::getTestArg("corpus")) {
            namespace fs = std::filesystem;
            u32 maps{0};
            std::array<u64, 3> counts{};
            std::array<u32, (uSz)Kind::SHORT_DRAIN + 1> problems{};  // by kind
            f64 slowest{0.0};
            std::string slowestMap;
            std::error_code ec;
            for(auto it = fs::recursive_directory_iterator(fs::path{*dir}, ec);
                !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if(it->path().extension() != ".osu") continue;
                const auto doc = Document::read(location(it->path().filename().string()), it->path().string()).document;
                if(!doc) continue;
                const MapObjects made{*doc};
                const f64 start = Timing::getTimeReal();
                const std::vector<Verify::Issue> all = Verify::check(*doc, made, "");
                const f64 took = (Timing::getTimeReal() - start) * 1000.0;
                for(const Verify::Issue &issue : all) {
                    counts[(uSz)issue.severity]++;
                    if(issue.severity == Severity::PROBLEM) problems[(uSz)issue.kind]++;
                }
                if(took > slowest) {
                    slowest = took;
                    slowestMap = it->path().filename().string();
                }
                maps++;
            }
            logRaw("  corpus: {} maps checked: {} problems, {} warnings, {} minor; the slowest in {:.1f} ms ({})", maps,
                   counts[0], counts[1], counts[2], slowest, slowestMap);
            logRaw("  corpus: problems by kind: {}", fmt::join(problems, " "));
        }
    }

    void timing() {
        TEST_SECTION("timing");
        using Change = Document::Change;
        using Action = Document::Action;
        const std::string head =
            "osu file format v14\r\n\r\n[General]\r\nAudioFilename: a.mp3\r\nMode: 0\r\n\r\n[Difficulty]\r\n"
            "CircleSize:4\r\nOverallDifficulty:7\r\nSliderMultiplier:1.4\r\nSliderTickRate:1\r\n\r\n";
        const std::string original = head +
                                     "[TimingPoints]\r\n1000,500,4,2,0,100,1,0\r\n3000,-50,4,2,0,100,0,0\r\n"
                                     "5000,400,4,2,0,100,1,0\r\n\r\n[HitObjects]\r\n256,192,1000,5,0,0:0:0:0:\r\n";
        auto read = mapOf(original);
        TEST_ASSERT(read != nullptr, "the timing map reads");
        if(!read) return;
        Document &doc = *read;

        // lines by id, kept in time order
        {
            const auto ids = doc.getTimingPointIds();
            const auto points = doc.getTimingPoints();
            TEST_ASSERT(ids.size() == 3 && points.size() == 3 &&
                            doc.getLines()[doc.getNumber(ids[1]) - 1].text == "3000,-50,4,2,0,100,0,0\r\n",
                        "the timing points come with their lines' ids");
            if(ids.size() != 3) return;
            const Document::Id green = ids[1];
            TimingPoint moved = points[1];
            moved.time = 6000.0;
            doc.change(Action::TIMING, [&](Change &c) { c.setTimingPoint(green, moved); });
            TEST_ASSERT_EQ(sectionOf(doc, "[TimingPoints]"),
                           "1000,500,4,2,0,100,1,0\r\n5000,400,4,2,0,100,1,0\r\n6000,-50,4,2,0,100,0,0",
                           "a timing line set to another time moves to its place");
            TEST_ASSERT(doc.getTimingPointIds()[2] == green, "...with its id");
            doc.undo();
            TEST_ASSERT(text(doc) == original, "...and back");

            TimingPoint red = uninherited(3000.0, 333.333333333333);
            TimingPoint twin = points[1];
            twin.time = 5000.0;
            doc.change(Action::ADD_TIMING_POINT, [&](Change &c) {
                c.addTimingPoint(twin);
                c.addTimingPoint(red);
            });
            TEST_ASSERT_EQ(sectionOf(doc, "[TimingPoints]"),
                           "1000,500,4,2,0,100,1,0\r\n3000,333.333333333333,4,0,0,100,1,0\r\n3000,-50,4,2,0,100,0,0\r\n"
                           "5000,400,4,2,0,100,1,0\r\n5000,-50,4,2,0,100,0,0",
                           "points added go where their times are, an uninherited one before an inherited one there");
            TEST_ASSERT(doc.getTimingPoints().size() == 5 && doc.getTimingPoints()[1].beatLength == 333.333333333333,
                        "...and read back as they were written");
            doc.undo();
            TEST_ASSERT(text(doc) == original && !doc.canUndo(), "...in one step");
        }

        // lines that aren't in order stay as they are
        {
            auto unordered = mapOf(head +
                                   "[TimingPoints]\r\n5000,400,4,2,0,100,1,0\r\n1000,500,4,2,0,100,1,0\r\n\r\n"
                                   "[HitObjects]\r\n256,192,1000,5,0,0:0:0:0:\r\n");
            unordered->change(Action::ADD_TIMING_POINT,
                              [&](Change &c) { c.addTimingPoint(uninherited(2000.0, 300.0)); });
            TEST_ASSERT_EQ(sectionOf(*unordered, "[TimingPoints]"),
                           "2000,300,4,0,0,100,1,0\r\n5000,400,4,2,0,100,1,0\r\n1000,500,4,2,0,100,1,0",
                           "a point added among lines out of order goes before the first later one, and they stay");
            TEST_ASSERT(
                unordered->getTimingPoints()[0].time == 1000.0 && unordered->getTimingPoints()[1].time == 2000.0,
                "...while the points are read in time order");
        }

        // a map without the section
        {
            auto bare = mapOf(head + "[HitObjects]\r\n256,192,1000,5,0,0:0:0:0:\r\n");
            TEST_ASSERT(bare && bare->getTimingPoints().empty(), "a map without timing points reads");
            if(!bare) return;
            bare->change(Action::ADD_TIMING_POINT, [&](Change &c) { c.addTimingPoint(uninherited(0.0, 500.0)); });
            TEST_ASSERT(
                text(*bare) ==
                    head + "[TimingPoints]\r\n0,500,4,0,0,100,1,0\r\n\r\n[HitObjects]\r\n256,192,1000,5,0,0:0:0:0:\r\n",
                "its first point gets a section where one usually is");
        }

        // sections carried: an object before the first point, on its beat, on a quarter, on a third, on no tick, a
        // slider with an inherited point 1 ms before it, a spinner; the next section has an object (a new combo, as
        // after any spinner) and a point of its own
        const std::string timed =
            head +
            "[TimingPoints]\r\n1000,500,4,2,0,100,1,0\r\n2999,-50,4,2,0,100,0,0\r\n5000,400,4,2,0,100,1,0\r\n"
            "5400,-200,4,2,0,100,0,0\r\n\r\n[HitObjects]\r\n256,192,500,5,0,0:0:0:0:\r\n256,192,1000,1,0,0:0:0:0:\r\n"
            "100,100,1125,1,0,0:0:0:0:\r\n100,100,1166,1,0,0:0:0:0:\r\n100,100,1200,1,0,0:0:0:0:\r\n"
            "64,192,3000,2,0,L|204:192,1,140\r\n256,192,4000,12,0,4500,0:0:0:0:\r\n256,192,5400,5,0,0:0:0:0:\r\n";
        auto readTimed = mapOf(timed);
        TEST_ASSERT(readTimed != nullptr, "the timed map reads");
        if(!readTimed) return;
        Document &map = *readTimed;
        const auto retime = [&](uSz index, f64 time, f64 beatLength, bool carry) {
            TimingPoint point = map.getTimingPoints()[index];
            point.time = time;
            point.beatLength = beatLength;
            const Retiming::Edit edit{.id = map.getTimingPointIds()[index], .point = point};
            return map.change(Action::TIMING, [&](Change &c) { Retiming::apply(c, map, std::span{&edit, 1}, carry); });
        };

        retime(0, 1000.0, 250.0, true);
        TEST_ASSERT_EQ(
            sectionOf(map, "[TimingPoints]"),
            "1000,250,4,2,0,100,1,0\r\n1999,-50,4,2,0,100,0,0\r\n5000,400,4,2,0,100,1,0\r\n"
            "5400,-200,4,2,0,100,0,0",
            "a tempo doubled takes its section's inherited point along, to the millisecond before its place");
        TEST_ASSERT_EQ(sectionOf(map, "[HitObjects]"),
                       "256,192,750,5,0,0:0:0:0:\r\n256,192,1000,1,0,0:0:0:0:\r\n100,100,1062,1,0,0:0:0:0:\r\n"
                       "100,100,1083,1,0,0:0:0:0:\r\n100,100,1100,1,0,0:0:0:0:\r\n64,192,2000,2,0,L|204:192,1,140\r\n"
                       "256,192,2500,12,0,2750,0:0:0:0:\r\n256,192,5400,5,0,0:0:0:0:\r\n",
                       "...and its objects to their beats: on the grid's ticks where they were, a spinner's end too, "
                       "the next section's staying");
        map.undo();
        TEST_ASSERT(text(map) == timed && !map.canUndo(), "...in one step");

        retime(0, 1000.0, 1100.0, true);
        TEST_ASSERT(sectionOf(map, "[TimingPoints]")
                        .starts_with("1000,1100,4,2,0,100,1,0\r\n4999,-50,4,2,0,100,0,0\r\n5000,400,4,2,0,100,1,0\r\n"),
                    "a tempo slowed takes its section's inherited point no further than the millisecond before the "
                    "next uninherited point");
        map.undo();

        retime(0, 1010.0, 500.0, true);
        TEST_ASSERT_EQ(sectionOf(map, "[TimingPoints]"),
                       "1010,500,4,2,0,100,1,0\r\n3009,-50,4,2,0,100,0,0\r\n5000,400,4,2,0,100,1,0\r\n"
                       "5400,-200,4,2,0,100,0,0",
                       "a point moved takes its section along");
        TEST_ASSERT_EQ(sectionOf(map, "[HitObjects]"),
                       "256,192,510,5,0,0:0:0:0:\r\n256,192,1010,1,0,0:0:0:0:\r\n100,100,1135,1,0,0:0:0:0:\r\n"
                       "100,100,1176,1,0,0:0:0:0:\r\n100,100,1210,1,0,0:0:0:0:\r\n64,192,3010,2,0,L|204:192,1,140\r\n"
                       "256,192,4010,12,0,4510,0:0:0:0:\r\n256,192,5400,5,0,0:0:0:0:\r\n",
                       "...what's before the first point too");
        map.undo();

        retime(2, 5000.0, 800.0, true);
        TEST_ASSERT(sectionOf(map, "[TimingPoints]").ends_with("5000,800,4,2,0,100,1,0\r\n5800,-200,4,2,0,100,0,0") &&
                        sectionOf(map, "[HitObjects]")
                            .ends_with("256,192,4000,12,0,4500,0:0:0:0:\r\n256,192,5800,5,0,0:0:0:0:\r\n"),
                    "a later section's change leaves the ones before it");
        map.undo();

        retime(0, 1010.0, 250.0, false);
        TEST_ASSERT(sectionOf(map, "[TimingPoints]").starts_with("1010,250,4,2,0,100,1,0\r\n2999,-50,") &&
                        sectionOf(map, "[HitObjects]") == sectionOf(*mapOf(timed), "[HitObjects]"),
                    "without carrying, only the point changes");
        map.undo();

        map.change(Action::RESNAP, [&](Change &c) { Retiming::resnap(c, map, 4); });
        TEST_ASSERT_EQ(sectionOf(map, "[HitObjects]"),
                       "256,192,500,5,0,0:0:0:0:\r\n256,192,1000,1,0,0:0:0:0:\r\n100,100,1125,1,0,0:0:0:0:\r\n"
                       "100,100,1125,1,0,0:0:0:0:\r\n100,100,1250,1,0,0:0:0:0:\r\n64,192,3000,2,0,L|204:192,1,140\r\n"
                       "256,192,4000,12,0,4500,0:0:0:0:\r\n256,192,5400,5,0,0:0:0:0:\r\n",
                       "a resnap puts every object on the nearest tick of the divisor");
        TEST_ASSERT(
            sectionOf(map, "[TimingPoints]").starts_with("1000,500,4,2,0,100,1,0\r\n3000,-50,4,2,0,100,0,0\r\n"),
            "...and the inherited points with them");
        map.undo();
        TEST_ASSERT(text(map) == timed && !map.canUndo(), "...in one step");
        {
            // its nearest tick, 2000, is past the next uninherited point
            auto late = mapOf(head +
                              "[TimingPoints]\r\n1000,500,4,2,0,100,1,0\r\n1940,-50,4,2,0,100,0,0\r\n"
                              "1950,400,4,2,0,100,1,0\r\n\r\n[HitObjects]\r\n256,192,1000,5,0,0:0:0:0:\r\n");
            late->change(Action::RESNAP, [&](Change &c) { Retiming::resnap(c, *late, 4); });
            TEST_ASSERT_EQ(sectionOf(*late, "[TimingPoints]"),
                           "1000,500,4,2,0,100,1,0\r\n1949,-50,4,2,0,100,0,0\r\n1950,400,4,2,0,100,1,0",
                           "...which stay in their sections: at the millisecond before the next uninherited point at "
                           "the latest");
        }

        const auto lineOf = [](f64 beatLength) { return neomod::BeatmapFile::format(uninherited(0.0, beatLength)); };
        TEST_ASSERT(lineOf(Retiming::beatLengthOf(180.0)) == "0,333.333333333333,4,0,0,100,1,0" &&
                        lineOf(Retiming::velocityBeatLength(0.75)) == "0,-133.333333333333,4,0,0,100,1,0" &&
                        Retiming::beatLengthOf(120.0) == 500.0 && Retiming::velocityBeatLength(2.0) == -50.0,
                    "beat lengths are made with the fifteen digits they're written with");

        // the panel: the list selection, its fields, adding and deleting (the time is 0 without music)
        {
            EditorState state;
            state.open(mapOf(timed));
            Document &shown = *state.document;
            CBaseUIPopupLayer popups("popups");
            TimingPanel panel(state, popups);
            panel.setSize(1200.f, 500.f);
            panel.tick();
            auto *list = static_cast<CBaseUIList *>(find(&panel, "editor_timing_points"));
            const auto number = [&](std::string_view name) {
                return static_cast<CBaseUINumberField *>(find(&panel, name));
            };
            const auto press = [&](CBaseUIElement *target, SCANCODE scancode, KEYMOD modifiers = KEYMOD_NONE) {
                KeyboardEvent e{scancode, 0, 0, 0, modifiers};
                target->onKeyDown(e);
            };
            const auto set = [&](std::string_view name, std::u32string_view value) {
                CBaseUINumberField *field = number(name);
                field->visit();
                for(const char32_t character : value) {
                    KeyboardEvent e{0, 0, character, 0, KEYMOD_NONE};
                    field->onChar(e);
                }
                press(field, KEY_ENTER);
                panel.tick();
            };
            TEST_ASSERT(list && list->getModel().getRowCount() == 4 && !panel.hasSelection() &&
                            !number("editor_timing_time")->isEnabled(),
                        "the panel lists the map's points; without a selection its fields are off");

            panel.selectInForce();
            panel.tick();
            TEST_ASSERT(number("editor_timing_time")->getValue() == 1000.0 &&
                            number("editor_timing_bpm")->getValue() == 120.0 &&
                            !number("editor_timing_velocity")->isEnabled(),
                        "before the first point, that one is in force: its fields show it");

            list->requestFocus();
            press(list, KEY_A, KEYMOD_LCONTROL);
            panel.tick();
            TEST_ASSERT(std::isnan(number("editor_timing_bpm")->getValue()) &&
                            std::isnan(number("editor_timing_velocity")->getValue()) &&
                            number("editor_timing_volume")->getValue() == 100.0 &&
                            number("editor_timing_time")->getValue() == 1000.0,
                        "of several, a field shows what they all have, and the time the first one's");
            set("editor_timing_volume", U"50");
            TEST_ASSERT_EQ(sectionOf(shown, "[TimingPoints]"),
                           "1000,500,4,2,0,50,1,0\r\n2999,-50,4,2,0,50,0,0\r\n5000,400,4,2,0,50,1,0\r\n"
                           "5400,-200,4,2,0,50,0,0",
                           "a value set goes to every selected point");
            state.undo();
            TEST_ASSERT(text(shown) == timed && !shown.canUndo(), "...in one step");
            panel.tick();
            set("editor_timing_bpm", U"240");
            TEST_ASSERT_EQ(sectionOf(shown, "[TimingPoints]"),
                           "1000,250,4,2,0,100,1,0\r\n1999,-50,4,2,0,100,0,0\r\n5000,250,4,2,0,100,1,0\r\n"
                           "5250,-200,4,2,0,100,0,0",
                           "a tempo goes to the uninherited ones, each carrying its section");
            state.undo();
            panel.tick();
            set("editor_timing_time", U"1010");
            TEST_ASSERT(sectionOf(shown, "[TimingPoints]") ==
                                "1010,500,4,2,0,100,1,0\r\n3009,-50,4,2,0,100,0,0\r\n5010,400,4,2,0,100,1,0\r\n"
                                "5410,-200,4,2,0,100,0,0" &&
                            sectionOf(shown, "[HitObjects]").starts_with("256,192,510,5,0,0:0:0:0:\r\n") &&
                            sectionOf(shown, "[HitObjects]").ends_with("256,192,5410,5,0,0:0:0:0:\r\n"),
                        "a time set moves every selected point by as much as the first, with their sections");
            state.undo();
            TEST_ASSERT(text(shown) == timed && !shown.canUndo(), "...in one step too");
            panel.tick();

            panel.removePoints(false);
            TEST_ASSERT(text(shown) == timed, "the last uninherited point can't be deleted");
            list->requestFocus();
            press(list, KEY_HOME);
            press(list, KEY_DOWN);
            panel.tick();
            TEST_ASSERT(
                number("editor_timing_velocity")->getValue() == 2.0 && !number("editor_timing_bpm")->isEnabled(),
                "an inherited point has a velocity and no tempo");
            set("editor_timing_velocity", U"0.75");
            TEST_ASSERT(sectionOf(shown, "[TimingPoints]").contains("\r\n2999,-133.333333333333,4,2,0,100,0,0\r\n"),
                        "a velocity set is written as its beat length");
            panel.removePoints(false);
            panel.tick();
            TEST_ASSERT(shown.getTimingPoints().size() == 3 && !panel.hasSelection(),
                        "a selected point deleted is gone, and the selection with it");

            // adding at the time (0)
            TEST_ASSERT(!panel.addPoint(false) && shown.getTimingPoints().size() == 3,
                        "an inherited point can't be added before the first timing point");
            TEST_ASSERT(
                panel.addPoint(true) &&
                    sectionOf(shown, "[TimingPoints]").starts_with("0,500,4,2,0,100,1,0\r\n1000,500,4,2,0,100,1,0\r\n"),
                "a point added before the first one has that one's tempo and sounds");
            panel.tick();
            TEST_ASSERT(panel.hasSelection() && number("editor_timing_time")->getValue() == 0.0, "...and is selected");
            TEST_ASSERT(panel.addPoint(true) && shown.getTimingPoints().size() == 4,
                        "another one at its time is that one");
            TEST_ASSERT(
                panel.addPoint(false) &&
                    sectionOf(shown, "[TimingPoints]").starts_with("0,500,4,2,0,100,1,0\r\n0,-100,4,2,0,100,0,0\r\n"),
                "an inherited point added has the velocity in force");
            panel.removePoints(true);
            TEST_ASSERT(sectionOf(shown, "[TimingPoints]").starts_with("0,500,4,2,0,100,1,0\r\n1000,500,"),
                        "deleting at the time takes out the point in force there");
            state.clock.seek(6000);
            TEST_ASSERT(
                panel.addPoint(true) &&
                    sectionOf(shown, "[TimingPoints]")
                        .ends_with("5400,-200,4,2,0,100,0,0\r\n6000,400,4,2,0,100,1,0\r\n6000,-200,4,2,0,100,0,0"),
                "an uninherited point keeps the slider velocity in force (on an inherited one at its time)");
            state.undo();
            TEST_ASSERT(
                sectionOf(shown, "[TimingPoints]").ends_with("5000,400,4,2,0,100,1,0\r\n5400,-200,4,2,0,100,0,0"),
                "...one step");
            state.clock.seek(5400);
            TEST_ASSERT(
                panel.addPoint(true) &&
                    sectionOf(shown, "[TimingPoints]")
                        .ends_with("5000,400,4,2,0,100,1,0\r\n5400,400,4,2,0,100,1,0\r\n5400,-200,4,2,0,100,0,0"),
                "...which an inherited point already at its time does by itself");
            state.undo();

            // nudges: the points there are selected without a step
            auto *faster = static_cast<CBaseUIButton *>(find(&panel, "editor_timing_bpm_more"));
            state.clock.seek(1000);
            panel.addPoint(true);
            panel.tick();
            faster->click();
            state.clock.seek(5000);
            panel.addPoint(true);
            panel.tick();
            faster->click();
            TEST_ASSERT(shown.getTimingPoints()[1].beatLength < 500.0 && shown.getTimingPoints()[2].beatLength < 400.0,
                        "a nudge after another selection leaves the nudge before it");
            state.undo();
            state.undo();
            TEST_ASSERT(
                shown.getTimingPoints()[1].beatLength == 500.0 && shown.getTimingPoints()[2].beatLength == 400.0,
                "...each a step of its own");
            panel.tick();

            // copy, cut, paste
            const std::string before{sectionOf(shown, "[TimingPoints]")};
            list->requestFocus();
            press(list, KEY_END);
            press(list, KEY_UP, KEYMOD_LSHIFT);
            panel.tick();
            panel.copyPoints();
            TEST_ASSERT_EQ(std::string{env->getClipBoardText()},
                           "5000,400,4,2,0,100,1,0\r\n5400,-200,4,2,0,100,0,0\r\n",
                           "copying puts the selected points' lines on the clipboard");
            panel.cutPoints();
            TEST_ASSERT(!sectionOf(shown, "[TimingPoints]").contains("5000,") &&
                            shown.getUndoAction() == Document::Action::CUT && !panel.hasSelection(),
                        "cutting takes them out, one step");
            TEST_ASSERT(TimingPanel::canPastePoints(), "...and they can be pasted");
            panel.pastePoints();
            panel.tick();
            TEST_ASSERT(sectionOf(shown, "[TimingPoints]") == before && panel.hasSelection() &&
                            number("editor_timing_time")->getValue() == 5000.0,
                        "pasted, they're back at their own times, selected");
            panel.pastePoints();
            TEST_ASSERT_EQ(sectionOf(shown, "[TimingPoints]"), before,
                           "...pasted again, they replace the points of their kind at those times");
            env->setClipBoardText("5400,300,4,1,0,80,1,0\r\n");
            panel.pastePoints();
            TEST_ASSERT(sectionOf(shown, "[TimingPoints]").contains("5400,300,4,1,0,80,1,0") &&
                            sectionOf(shown, "[TimingPoints]").contains("5400,-200,4,2,0,100,0,0"),
                        "an uninherited point pasted at an inherited one's time leaves it");
            env->setClipBoardText("5400,300,4,1,0,80,1,0\r\nnot a point\r\n");
            TEST_ASSERT(!TimingPanel::canPastePoints(), "a clipboard with other lines has nothing to paste");
            list->stealFocus();
        }

        // a run of tempo nudges is one step, made from where things were before it
        {
            EditorState state;
            state.open(mapOf(timed));
            Document &shown = *state.document;
            CBaseUIPopupLayer popups("popups");
            TimingPanel panel(state, popups);
            panel.setSize(1200.f, 500.f);
            panel.selectInForce();
            panel.tick();
            auto *faster = static_cast<CBaseUIStyledButton *>(find(&panel, "editor_timing_bpm_more"));
            auto *slower = static_cast<CBaseUIStyledButton *>(find(&panel, "editor_timing_bpm_less"));
            for(int i = 0; i < 100; i++) {
                faster->click();
                panel.tick();
            }
            auto once = mapOf(timed);
            TimingPoint tempo = once->getTimingPoints()[0];
            tempo.beatLength = Retiming::beatLengthOf(130.0);
            const Retiming::Edit edit{.id = once->getTimingPointIds()[0], .point = tempo};
            once->change(Action::TIMING, [&](Change &c) { Retiming::apply(c, *once, std::span{&edit, 1}, true); });
            TEST_ASSERT(sectionOf(shown, "[TimingPoints]").starts_with("1000,461.538461538462,4,2,0,100,1,0\r\n") &&
                            text(shown) == text(*once),
                        "a tempo nudged up a hundred times is as if it was set once: nothing rounds on the way");
            for(int i = 0; i < 100; i++) slower->click();
            TEST_ASSERT(text(shown) == timed && !shown.canUndo(), "...and nudged back, it's as it was, without a step");
            faster->click();
            faster->click();
            state.undo();
            TEST_ASSERT(text(shown) == timed && !shown.canUndo(), "a run of nudges is one step to undo");
        }

        // so is a run of time nudges, of one selection
        {
            EditorState state;
            state.open(mapOf(timed));
            Document &shown = *state.document;
            CBaseUIPopupLayer popups("popups");
            TimingPanel panel(state, popups);
            panel.setSize(1200.f, 500.f);
            state.clock.seek(5100);
            panel.selectInForce();
            panel.tick();
            auto *earlier = static_cast<CBaseUIStyledButton *>(find(&panel, "editor_timing_time_less"));
            for(int i = 0; i < 1100; i++) {
                earlier->click();
                panel.tick();
            }
            auto once = mapOf(timed);
            TimingPoint moved = once->getTimingPoints()[2];
            moved.time = 3900.0;
            const Retiming::Edit edit{.id = once->getTimingPointIds()[2], .point = moved};
            once->change(Action::TIMING, [&](Change &c) { Retiming::apply(c, *once, std::span{&edit, 1}, true); });
            TEST_ASSERT_EQ(text(shown), text(*once),
                           "a point nudged 1100 ms earlier takes its section along as if moved once (not the spinner "
                           "it passed on the way)");
            auto *list = static_cast<CBaseUIList *>(find(&panel, "editor_timing_points"));
            list->requestFocus();
            KeyboardEvent home{KEY_HOME, 0, 0, 0, KEYMOD_NONE};
            list->onKeyDown(home);
            panel.tick();
            earlier->click();
            panel.tick();
            state.undo();
            TEST_ASSERT(text(shown) == text(*once), "...another selection's nudges are a step of their own");
            list->stealFocus();
        }

        // the metronome beats: bars are counted from each uninherited point
        {
            TimingPoint waltz = uninherited(5000.0, 400.0);
            waltz.meter = 3;
            const std::vector<TimingPoint> points{uninherited(1000.0, 500.0), waltz};
            using enum Metronome::Tick;
            TEST_ASSERT(
                Metronome::crossed(points, 990, 1005) == BAR && Metronome::crossed(points, 1005, 1400) == NONE &&
                    Metronome::crossed(points, 1400, 1510) == BEAT && Metronome::crossed(points, 2990, 3000) == BAR &&
                    Metronome::crossed(points, 3000, 3010) == NONE,
                "a beat ticks when the time reaches it, a bar's first one otherwise");
            TEST_ASSERT(Metronome::crossed(points, 400, 510) == BEAT && Metronome::crossed(points, -1010, -990) == BAR,
                        "...before the first point too");
            TEST_ASSERT(
                Metronome::crossed(points, 4990, 5005) == BAR && Metronome::crossed(points, 5390, 5410) == BEAT &&
                    Metronome::crossed(points, 6190, 6210) == BAR && Metronome::crossed(points, 5410, 5390) == NONE,
                "a point starts a bar, of its own meter");
            const auto beat = Metronome::beatAt(points, 1750.0);
            const auto early = Metronome::beatAt(points, 500.0);
            TEST_ASSERT(
                beat && beat->inBar == 1 && beat->meter == 4 && beat->phase == 0.5 && early && early->inBar == 3,
                "the beat a time is in is counted the same way");
            TEST_ASSERT(!Metronome::beatAt({}, 0.0) && Metronome::crossed({}, 0, 1000) == NONE,
                        "without a timing point there's no beat");
        }

        // taps
        {
            const auto tapped = [](std::vector<i32> taps) { return Retiming::tappedBpm(taps); };
            TEST_ASSERT(!tapped({0, 500, 1000}) && tapped({0, 500, 1000, 1500}) == 120.0,
                        "taps are a tempo from the fourth on");
            TEST_ASSERT(tapped({0, 510, 995, 1505, 1998, 2502}) == 120.0 && tapped({0, 333, 667, 1000, 1333}) == 180.0,
                        "...a whole one, through taps that are off");
            TEST_ASSERT(tapped({0, 500, 1000, 2000, 2500}) == 120.0, "a tap left out is a beat all the same");
            TEST_ASSERT(!tapped({5, 5, 5, 5}), "taps at one time are none");
        }

        // only when asked for: how long timing steps take on a map, e.g. a marathon
        if(const auto file = Mc::Tests::getTestArg("timing_map")) {
            EditorState state;
            auto read = Document::read(location("timing-map.osu"), *file).document;
            if(!read) {
                logRaw("  (can't read {})", *file);
                return;
            }
            state.open(std::move(read));
            Document &big = *state.document;
            const auto timedStep = [&](std::string_view what, auto &&step) {
                const f64 start = Timing::getTimeReal();
                step();
                logRaw("  {}: {:.1f} ms", what, (Timing::getTimeReal() - start) * 1000.0);
            };
            const auto reds = Mc::ranges::count(big.getTimingPoints(), true, &TimingPoint::uninherited);
            logRaw("  {}: {} objects, {} timing points ({} uninherited)", *file, state.objects->objects.size(),
                   big.getTimingPoints().size(), reds);
            const auto points = big.getTimingPoints();
            const auto first = Mc::ranges::find(points, true, &TimingPoint::uninherited);
            if(first == points.end()) return;
            TimingPoint tempo = *first;
            tempo.beatLength = Retiming::beatLengthOf(60000.0 / tempo.beatLength + 1.0);
            const Retiming::Edit edit{.id = big.getTimingPointIds()[(uSz)(first - big.getTimingPoints().begin())],
                                      .point = tempo};
            timedStep("a tempo change that carries its section", [&] {
                state.edit(Action::TIMING, [&](Change &c) { Retiming::apply(c, big, std::span{&edit, 1}, true); });
            });
            timedStep("its undo", [&] { state.undo(); });
            timedStep("the same without carrying", [&] {
                state.edit(Action::TIMING, [&](Change &c) { Retiming::apply(c, big, std::span{&edit, 1}, false); });
            });
            state.undo();
            timedStep("a resnap", [&] { state.edit(Action::RESNAP, [&](Change &c) { Retiming::resnap(c, big, 4); }); });
            state.undo();
            // every combo after it counted again
            state.compose.select({state.objects->ids[std::min<uSz>(1, state.objects->ids.size() - 1)]});
            timedStep("a new combo near the start", [&] { state.compose.toggleNewCombo(); });
            timedStep("its undo", [&] { state.undo(); });
            state.compose.clearSelection();

            CBaseUIPopupLayer popups("popups");
            TimingPanel panel(state, popups);
            panel.setSize(1200.f, 500.f);
            auto *list = static_cast<CBaseUIList *>(find(&panel, "editor_timing_points"));
            list->requestFocus();
            KeyboardEvent all{KEY_A, 0, 0, 0, KEYMOD_LCONTROL};
            list->onKeyDown(all);
            timedStep("the panel's tick after selecting every point", [&] { panel.tick(); });
            const f64 start = Timing::getTimeReal();
            for(int i = 0; i < 100; i++) panel.tick();
            logRaw("  the panel's tick with every point selected: {:.3f} ms", (Timing::getTimeReal() - start) * 10.0);
            list->stealFocus();
        }
    }

    void sliders() {
        TEST_SECTION("sliders");
        using enum neomod::SLIDERCURVETYPE;
        using HO = neomod::BeatmapFile::HitObject;
        const auto typeOf = [](std::vector<vec2> points) { return SliderPath::curveTypeFor(points); };
        TEST_ASSERT(typeOf({{0.f, 0.f}, {100.f, 0.f}}) == LINEAR, "two points: a line");
        TEST_ASSERT(typeOf({{0.f, 0.f}, {50.f, 50.f}, {100.f, 0.f}}) == PASSTHROUGH, "three: a perfect circle");
        TEST_ASSERT(typeOf({{0.f, 0.f}, {50.f, 0.f}, {100.f, 0.f}}) == LINEAR &&
                        typeOf({{0.f, 0.f}, {100.f, 0.f}, {100.f, 0.f}}) == LINEAR,
                    "...three on a line (two of them equal too): lines");
        TEST_ASSERT(typeOf({{0.f, 0.f}, {200.f, 1.f}, {100.f, 0.f}}) == BEZIER,
                    "...three whose arc would go far around: a bezier");
        TEST_ASSERT(typeOf({{0.f, 0.f}, {50.f, 50.f}, {100.f, 0.f}, {150.f, 50.f}}) == BEZIER &&
                        typeOf({{0.f, 0.f}, {100.f, 0.f}, {100.f, 0.f}, {100.f, 100.f}}) == BEZIER,
                    "four or more (a red anchor counts as two): a bezier");

        const auto afterEdit = [](neomod::SLIDERCURVETYPE type, std::vector<vec2> points, bool moved) {
            return SliderPath::typeAfterEdit(type, points, moved);
        };
        TEST_ASSERT(afterEdit(BEZIER, {{0.f, 0.f}, {50.f, 50.f}, {100.f, 0.f}}, true) == BEZIER &&
                        afterEdit(PASSTHROUGH, {{0.f, 0.f}, {50.f, 0.f}, {100.f, 0.f}}, true) == LINEAR,
                    "a moved point keeps the type (a perfect circle while its points make one)");
        TEST_ASSERT(afterEdit(LINEAR, {{0.f, 0.f}, {50.f, 50.f}, {100.f, 0.f}}, false) == PASSTHROUGH,
                    "...one added or taken out gives the type placing would");
        TEST_ASSERT(afterEdit(CATMULL, {{0.f, 0.f}, {50.f, 50.f}, {100.f, 0.f}}, false) == CATMULL &&
                        afterEdit(CATMULL, {{0.f, 0.f}, {100.f, 0.f}}, true) == CATMULL,
                    "...but a catmull slider stays one");

        const auto lengthOf = [](neomod::SLIDERCURVETYPE type, std::vector<vec2> points) {
            return SliderPath::curveLength(type, points);
        };
        TEST_ASSERT(std::abs(lengthOf(LINEAR, {{0.f, 0.f}, {100.f, 0.f}, {100.f, 50.f}}) - 150.0) < 0.01,
                    "a curve's length: lines");
        TEST_ASSERT(
            std::abs(lengthOf(PASSTHROUGH, {{0.f, 0.f}, {50.f, 50.f}, {100.f, 0.f}}) - 50.0 * std::numbers::pi) < 0.01,
            "...a perfect circle's arc");
        TEST_ASSERT_EQ(SliderPath::snappedLength(150.0, 35.0), 140.0, "a length snaps down to whole ticks");
        TEST_ASSERT_EQ(SliderPath::snappedLength(139.5, 35.0), 140.0, "...with 1 osu!px of slack");
        TEST_ASSERT_EQ(SliderPath::snappedLength(33.0, 35.0), 0.0, "...none under a tick");
        TEST_ASSERT_EQ(SliderPath::snappedLength(240.0, 100.0 * 2.3 / 4.0), 230.0,
                       "...at the precision lengths are written with (a tick at slider multiplier 2.3 is 57.5)");

        const std::vector<vec2> corner{{0.f, 0.f}, {100.f, 0.f}, {100.f, 100.f}};
        TEST_ASSERT_EQ(SliderPath::insertionIndex(corner, {50.f, 10.f}), 1, "a new point goes on the nearest edge");
        TEST_ASSERT_EQ(SliderPath::insertionIndex(corner, {110.f, 50.f}), 2, "...the second one");
        const std::vector<vec2> anchored{{0.f, 0.f}, {100.f, 0.f}, {100.f, 0.f}, {100.f, 100.f}};
        TEST_ASSERT_EQ(SliderPath::insertionIndex(anchored, {100.f, 1.f}), 3,
                       "...never between the points of a red anchor");

        const auto withSpans = [](std::string_view line, i32 spans) {
            HO edged;
            if(!neomod::BeatmapFile::parse(line, edged)) return std::string{};
            SliderPath::setSpans(edged, spans);
            return neomod::BeatmapFile::format(edged);
        };
        TEST_ASSERT_EQ(withSpans("0,0,0,2,2,L|100:0,2,100,2|0|8,1:0|0:0|2:3,3:0:0:0:", 3),
                       "0,0,0,2,2,L|100:0,3,100,2|0|2|8,1:0|0:0|3:0|2:3,3:0:0:0:",
                       "more spans: the head and the tail keep their edges, a repeat added gets the body's");
        TEST_ASSERT_EQ(withSpans("0,0,0,2,2,L|100:0,2,100,2|0|8,1:0|0:0|2:3,3:0:0:0:", 1),
                       "0,0,0,2,2,L|100:0,1,100,2|8,1:0|2:3,3:0:0:0:", "...fewer: the last repeats go");
        TEST_ASSERT_EQ(withSpans("0,0,0,2,2,L|100:0,1,100", 3), "0,0,0,2,2,L|100:0,3,100",
                       "...a slider without edge lists stays without");
        TEST_ASSERT_EQ(withSpans("0,0,0,2,4,L|100:0,1,100,2,0:0|0:0,0:0:0:0:", 2),
                       "0,0,0,2,4,L|100:0,2,100,2|4|4,0:0|0:0|0:0,0:0:0:0:",
                       "...lists of different lengths: each edge as the game plays it (one the shorter list leaves "
                       "out has the body's sounds)");
        HO short3;
        const std::vector<SliderPath::Edge> played{
            {.sounds = 2, .sets = {}}, {.sounds = 2, .sets = {}}, {.sounds = 2, .sets = {}}, {.sounds = 8, .sets = {}}};
        TEST_ASSERT(
            neomod::BeatmapFile::parse("0,0,0,2,0,L|100:0,3,100,2|8", short3) && SliderPath::edgesOf(short3) == played,
            "a repeat past the listed edges plays the last one before the tail");

        // reversed: as drawn by the game, the path backwards (osu!px at most between them, along their lengths)
        const auto apart = [](neomod::SLIDERCURVETYPE type, std::span<const vec2> points, f64 length,
                              const SliderPath::Path &other) {
            const neomod::SliderCurve a{type, points, (f32)length};
            const neomod::SliderCurve b{other.type, other.points, (f32)length};
            f32 most = 0.f;
            for(i32 i = 0; i <= 64; i++) {
                most = std::max(most, vec::distance(a.pointAt(1.f - (f32)i / 64.f), b.pointAt((f32)i / 64.f)));
            }
            return most;
        };
        const auto reversedOf = [](neomod::SLIDERCURVETYPE type, std::vector<vec2> points, f64 length) {
            return SliderPath::reversed(type, points, length);
        };
        const auto pathIs = [](const SliderPath::Path &path, neomod::SLIDERCURVETYPE type,
                               const std::vector<vec2> &points) { return path.type == type && path.points == points; };
        TEST_ASSERT(pathIs(reversedOf(LINEAR, {{100.f, 100.f}, {300.f, 100.f}}, 140.0), LINEAR,
                           {{240.f, 100.f}, {100.f, 100.f}}),
                    "reversed: lines from where the length ends");
        TEST_ASSERT(pathIs(reversedOf(LINEAR, {{0.f, 0.f}, {100.f, 0.f}, {100.f, 100.f}}, 120.0), LINEAR,
                           {{100.f, 20.f}, {100.f, 0.f}, {0.f, 0.f}}),
                    "...the points after it gone");
        TEST_ASSERT(pathIs(reversedOf(LINEAR, {{0.f, 0.f}, {100.f, 0.f}}, 150.0), LINEAR, {{150.f, 0.f}, {0.f, 0.f}}),
                    "...from where the last line runs on to");
        TEST_ASSERT(pathIs(reversedOf(PASSTHROUGH, {{0.f, 0.f}, {50.f, 50.f}, {100.f, 0.f}}, 0.0), PASSTHROUGH,
                           {{100.f, 0.f}, {50.f, 50.f}, {0.f, 0.f}}),
                    "...a perfect circle through its end, its arc's middle and its head");
        const std::vector<vec2> arc{{0.f, 0.f}, {50.f, 50.f}, {100.f, 0.f}};
        TEST_ASSERT(apart(PASSTHROUGH, arc, 70.0, reversedOf(PASSTHROUGH, arc, 70.0)) < 1.f,
                    "...cut short, on the same arc");
        const std::vector<vec2> pieces{{0.f, 0.f},     {100.f, 0.f},   {100.f, 100.f}, {100.f, 100.f},
                                       {200.f, 150.f}, {300.f, 100.f}, {250.f, 0.f}};
        const std::vector<vec2> backwards(pieces.rbegin(), pieces.rend());
        TEST_ASSERT(reversedOf(BEZIER, pieces, 0.0).points == backwards, "...a bezier whole: its points backwards");
        for(const f64 length : {100.0, 150.0, 300.0, 350.0}) {
            const auto back = reversedOf(BEZIER, pieces, length);
            TEST_ASSERT(back.type == BEZIER && apart(BEZIER, pieces, length, back) < 1.f &&
                            apart(back.type, back.points, length, reversedOf(back.type, back.points, length)) < 1.f,
                        fmt::format("...cut at {} osu!px (in a piece split exactly), and back again", length));
        }
        const std::vector<vec2> doubledEnd{{0.f, 0.f}, {100.f, 0.f}, {100.f, 100.f}, {100.f, 100.f}};
        const auto fromDoubled = reversedOf(BEZIER, doubledEnd, 0.0);
        TEST_ASSERT(fromDoubled.points.size() == 4 && fromDoubled.points[0] != fromDoubled.points[1] &&
                        apart(BEZIER, doubledEnd, 0.0, fromDoubled) < 1.f,
                    "...one ending on a doubled point starts with it one osu!px on (the same curve)");
        const std::vector<vec2> quadratic{{0.f, 0.f}, {100.f, 0.f}, {100.f, 100.f}};
        TEST_ASSERT(apart(BEZIER, quadratic, 220.0, reversedOf(BEZIER, quadratic, 220.0)) < 1.f,
                    "...one run on past its end: a line first");
        const std::vector<vec2> wave{{0.f, 0.f}, {100.f, 50.f}, {200.f, 0.f}, {300.f, 50.f}};
        const auto catmull = reversedOf(CATMULL, wave, 250.0);
        const std::vector<vec2> catmullPoints{vec::round(neomod::SliderCurve{CATMULL, wave, 250.f}.pointAt(1.f)),
                                              wave[2], wave[1], wave[0]};
        TEST_ASSERT(
            catmull.type == CATMULL && catmull.points == catmullPoints && apart(CATMULL, wave, 250.0, catmull) < 10.f,
            "...a catmull curve: its points before the cut, from its visual end (its ends' tangents change)");

        HO slider;
        const std::vector<vec2> points{{10.f, 20.f}, {50.f, 60.f}, {90.f, 20.f}};
        TEST_ASSERT(neomod::BeatmapFile::parse("10,20,1000,2,0,B|10:20|50:60|90:20,1,100", slider) &&
                        SliderPath::pointsOf(slider) == points,
                    "a slider's points start at its head (a first point there is the head)");

        // 500 ms beats at slider multiplier 1.4: 140 osu!px a beat, 35 a tick at 1/4; twice that from 8000
        const std::string path = Mc::Paths::cache() + "/editor-slider-tests.osu";
        const std::string original =
            "osu file format v14\r\n\r\n[General]\r\nAudioFilename: a.mp3\r\nMode: 0\r\n\r\n[Difficulty]\r\n"
            "CircleSize:4\r\nApproachRate:5\r\nSliderMultiplier:1.4\r\nSliderTickRate:1\r\n\r\n[TimingPoints]\r\n"
            "0,500,4,2,0,100,1,0\r\n8000,-50,4,2,0,100,0,0\r\n\r\n[HitObjects]\r\n"
            "100,100,1000,5,0,0:0:0:0:\r\n"
            "100,100,2000,2,0,L|300:100,1,140\r\n"          // a line
            "100,200,4000,2,0,B|200:300|300:200,1,140\r\n"  // a bezier of three points, as older editors wrote them
            "300,300,5000,1,0,0:0:0:0:\r\n";
        File(path, File::MODE::WRITE).writeLine(original, false);
        auto [read, error] = Document::read(location("editor-slider-tests.osu"), path);
        TEST_ASSERT(read != nullptr, "the slider map reads");
        if(!read) return;

        EditorState state;
        state.open(std::move(read));
        state.divisor = 4;
        Document &doc = *state.document;
        Compose &compose = state.compose;
        const Snapping::Settings unsnapped{};
        const auto objectsText = [&] {
            const std::string_view all = text(doc);
            return all.substr(all.find("[HitObjects]\r\n") + 14);
        };
        const auto undoAll = [&] {
            compose.endGesture();
            while(doc.canUndo()) state.undo();
        };

        // placing
        compose.setTool(Compose::Tool::SLIDER);
        state.clock.seek(3000);
        compose.startSlider({100.f, 100.f}, unsnapped);
        TEST_ASSERT(compose.isPlacingSlider() && !doc.canUndo(), "a slider's head alone isn't in the map");
        compose.aimSlider(vec2{200.4f, 100.f}, unsnapped);
        TEST_ASSERT(objectsText().find("100,100,3000,2,0,L|200:100,1,70\r\n") != std::string::npos,
                    "aimed: a line to the cursor (whole osu!px), its length down to whole ticks (2 of 35)");
        compose.addSliderPoint();
        compose.aimSlider(vec2{200.f, 200.f}, unsnapped);
        TEST_ASSERT(objectsText().find("100,100,3000,2,0,P|200:100|200:200,1,210\r\n") != std::string::npos,
                    "a point kept, the next aimed: a perfect circle (half of one, 222 osu!px: 6 ticks)");
        compose.addSliderPoint();
        compose.aimSlider(std::nullopt, unsnapped);
        compose.addSliderPoint();
        compose.aimSlider(vec2{300.f, 200.f}, unsnapped);
        TEST_ASSERT(
            objectsText().find("100,100,3000,2,0,B|200:100|200:200|200:200|300:200,1,245\r\n") != std::string::npos,
            "a click on the last point again makes it a red anchor: a bezier, then a line from it");
        compose.takeBackSliderPoint();
        TEST_ASSERT(objectsText().find("100,100,3000,2,0,B|200:100|200:200|300:200,1,210\r\n") != std::string::npos,
                    "undo while placing takes back the last point kept (the anchor's second; the aim stays)");
        compose.finishSlider();
        TEST_ASSERT(!compose.isPlacingSlider() &&
                        objectsText() ==
                            "100,100,1000,5,0,0:0:0:0:\r\n100,100,2000,2,0,L|300:100,1,140\r\n"
                            "100,100,3000,2,0,B|200:100|200:200|300:200,1,210\r\n"
                            "100,200,4000,2,0,B|200:300|300:200,1,140\r\n300,300,5000,1,0,0:0:0:0:\r\n",
                    "finished as it is, in its place in time");
        undoAll();
        TEST_ASSERT_EQ(text(doc), original, "...one step, undone byte for byte");

        state.clock.seek(3000);
        compose.startSlider({100.f, 100.f}, unsnapped);
        compose.aimSlider(vec2{120.f, 100.f}, unsnapped);
        compose.finishSlider();
        TEST_ASSERT(!doc.canUndo(), "one shorter than a tick isn't placed");

        compose.toggleNewCombo();
        compose.setSampleSet(false, 2);
        state.clock.seek(5000);
        compose.startSlider({300.f, 300.f}, unsnapped);
        compose.aimSlider(vec2{400.f, 300.f}, unsnapped);
        compose.finishSlider();
        TEST_ASSERT(
            objectsText().find("300,300,5000,6,0,L|400:300,1,70,0|0,2:0|2:0,2:0:0:0:\r\n") != std::string::npos &&
                objectsText().find("300,300,5000,1,") == std::string::npos,
            "the sidebar's new combo and sample set (on every edge, with the sample), replacing the object at "
            "its start");
        TEST_ASSERT(!compose.getBrush().newCombo, "...the new combo used up");
        undoAll();
        compose.setSampleSet(false, 0);

        state.clock.seek(9000);
        compose.startSlider({100.f, 300.f}, unsnapped);
        compose.aimSlider(vec2{200.f, 300.f}, unsnapped);
        compose.finishSlider();
        TEST_ASSERT(objectsText().find("100,300,9000,2,0,L|200:300,1,70\r\n") != std::string::npos,
                    "at twice the slider velocity a tick is twice as long");
        undoAll();

        state.clock.seek(3000);
        compose.startSlider({100.f, 100.f}, unsnapped);
        compose.aimSlider(vec2{300.f, 100.f}, unsnapped);
        compose.cancelSlider();
        TEST_ASSERT(!compose.isPlacingSlider() && text(doc) == original, "Escape takes a slider being placed back");

        state.clock.seek(3000);
        compose.startSlider({100.f, 100.f}, unsnapped);
        compose.aimSlider(vec2{200.4f, 100.f}, unsnapped);
        compose.endPlacing();
        TEST_ASSERT(!compose.isPlacingSlider() &&
                        objectsText().find("100,100,3000,2,0,L|200:100,1,70\r\n") != std::string::npos,
                    "what takes the editor elsewhere keeps a slider being placed as it is");
        undoAll();

        // shaping
        const auto lineAt = [&](i32 timeMS) {
            const std::string_view all = objectsText();
            const uSz at = all.find(fmt::format(",{},", timeMS));
            const uSz start = all.rfind('\n', at) + 1;
            return all.substr(start, all.find('\r', at) - start);
        };
        const auto idAt = [&](i32 timeMS) {
            const MapObjects &objects = *state.objects;
            for(uSz i = 0; i < objects.objects.size(); i++) {
                if(objects.objects[i]->getClickTime() == timeMS) return objects.ids[i];
            }
            return Document::Id{0};
        };
        compose.setTool(Compose::Tool::SELECT);
        compose.select({idAt(2000)});
        TEST_ASSERT(compose.controlPoints() && compose.controlPoints()->points.size() == 2,
                    "the selected slider shows its control points");
        TEST_ASSERT(compose.controlPointAt({298.f, 101.f}, 5.f) == uSz{1}, "a press near one takes it");
        compose.startPointDrag(1);
        compose.dragPoint({300.f, 200.4f}, unsnapped);
        TEST_ASSERT_EQ(lineAt(2000), "100,100,2000,2,0,L|300:200,1,210",
                       "a dragged point moves (whole osu!px), the length snaps again (224 osu!px: 6 ticks)");
        compose.dragPoint({110.f, 100.f}, unsnapped);
        TEST_ASSERT_EQ(lineAt(2000), "100,100,2000,2,0,L|300:200,1,210",
                       "...not to where the slider would be under a tick (it stays where it last fit)");
        compose.dragPoint({300.f, 100.f}, unsnapped);
        TEST_ASSERT_EQ(lineAt(2000), "100,100,2000,2,0,L|300:100,1,140",
                       "...back where it started the slider is as it was (its length doesn't snap again)");
        compose.endGesture();
        TEST_ASSERT(!doc.canUndo(), "...which leaves no step");

        compose.startPointDrag(0);
        compose.dragPoint({50.4f, 100.2f}, unsnapped);
        compose.endGesture();
        TEST_ASSERT_EQ(lineAt(2000), "50,100,2000,2,0,L|300:100,1,245", "the head point moves the head only");
        state.undo();

        compose.select({idAt(2000)});
        compose.addControlPoint({200.f, 50.f}, unsnapped);
        compose.endGesture();
        TEST_ASSERT_EQ(lineAt(2000), "100,100,2000,2,0,P|200:50|300:100,1,210",
                       "Ctrl+click adds a point on the nearest edge: the type the points get (a perfect circle)");
        compose.removeControlPoint(1);
        TEST_ASSERT_EQ(lineAt(2000), "100,100,2000,2,0,L|300:100,1,175", "a point taken out: a line again");
        compose.removeControlPoint(1);
        TEST_ASSERT_EQ(lineAt(2000), "100,100,2000,2,0,L|300:100,1,175", "...never below two");

        compose.select({idAt(2000)});
        compose.setCurveType(PASSTHROUGH);
        TEST_ASSERT(!compose.takesCurveType(PASSTHROUGH) && lineAt(2000) == "100,100,2000,2,0,L|300:100,1,175",
                    "the curve type: not a perfect circle through two points");
        compose.setCurveType(CATMULL);
        TEST_ASSERT_EQ(lineAt(2000), "100,100,2000,2,0,C|300:100,1,175",
                       "...a catmull curve: the points stay, the length snaps again (5 ticks of the 200 osu!px)");
        TEST_ASSERT(compose.getCurveType() == CATMULL, "...which the selection says");
        compose.addControlPoint({200.f, 50.f}, unsnapped);
        compose.endGesture();
        TEST_ASSERT(lineAt(2000).starts_with("100,100,2000,2,0,C|200:50|300:100,1,"),
                    "a catmull slider stays one when a point is added");
        state.undo();
        state.undo();

        compose.select({idAt(4000)});
        compose.startPointDrag(1);
        compose.dragPoint({200.f, 320.f}, unsnapped);
        compose.endGesture();
        TEST_ASSERT(lineAt(4000).starts_with("100,200,4000,2,0,B|200:320|300:200,1,"),
                    "a moved point keeps the slider's type (a bezier of three points stays one)");
        state.undo();
        compose.select({idAt(4000)});
        compose.makeRedAnchor(1);
        TEST_ASSERT_EQ(lineAt(4000), "100,200,4000,2,0,B|200:300|200:300|300:200,1,280",
                       "Ctrl+click on an inner point makes it a red anchor: two lines (283 osu!px: 8 ticks)");
        TEST_ASSERT(compose.controlPointAt({200.f, 300.f}, 5.f) == uSz{1},
                    "a press on a red anchor takes its first point");
        compose.startPointDrag(1);
        compose.dragPoint({200.f, 280.f}, unsnapped);
        compose.endGesture();
        TEST_ASSERT(lineAt(4000).starts_with("100,200,4000,2,0,B|200:280|200:280|300:200,1,"), "...and drags both");
        compose.removeControlPoint(2);
        TEST_ASSERT(lineAt(4000).starts_with("100,200,4000,2,0,P|200:280|300:200,1,"),
                    "a right-click on it takes the anchor out (one of its points): the type the points get");
        undoAll();

        // another step between the press and the first update in a drag ends the drag (its lines may have changed or
        // gone)
        compose.select({idAt(2000)});
        compose.startPointDrag(1);
        compose.deleteSelection();
        compose.dragPoint({300.f, 200.f}, unsnapped);
        compose.endGesture();
        TEST_ASSERT(objectsText().find(",2000,") == std::string::npos && doc.canUndo(),
                    "a point drag whose slider was deleted under it ends");
        undoAll();
        compose.select({idAt(2000)});
        compose.startPointDrag(1);
        compose.toggleHitSound(neomod::DatabaseBeatmapTypes::HitSoundType::WHISTLE);
        compose.dragPoint({300.f, 200.f}, unsnapped);
        compose.endGesture();
        TEST_ASSERT_EQ(lineAt(2000), "100,100,2000,2,2,L|300:100,1,140",
                       "...one whose slider changed under it too (the change stays)");
        undoAll();
        compose.select({idAt(5000)});
        compose.toggleNewCombo();
        compose.startEndDrag(*compose.objectAtTime(2500.0, 20.0));
        state.revert(path);
        compose.dragEnd(3000.0, true);
        compose.endGesture();
        TEST_ASSERT_EQ(text(doc), original, "...an end drag across a revert (every line new)");
        compose.select({idAt(5000)});
        compose.toggleNewCombo();
        compose.setTool(Compose::Tool::SLIDER);
        state.clock.seek(5000);
        compose.startSlider({300.f, 300.f}, unsnapped);
        state.revert(path);
        compose.aimSlider(vec2{400.f, 300.f}, unsnapped);
        TEST_ASSERT(!compose.isPlacingSlider() && text(doc) == original,
                    "...a slider being placed over an object, across a revert");
        compose.setTool(Compose::Tool::SELECT);
        undoAll();
        compose.setTool(Compose::Tool::SLIDER);
        state.clock.seek(5000);
        compose.startSlider({300.f, 300.f}, unsnapped);
        compose.aimSlider(vec2{400.f, 300.f}, unsnapped);
        compose.startPreview(Document::Action::POLYGON);
        TEST_ASSERT(!compose.isPlacingSlider() && compose.isPreviewing() && text(doc) != original,
                    "a step popup opened over a slider being placed finishes it first");
        compose.endPreview(false);
        compose.setTool(Compose::Tool::SELECT);
        undoAll();

        // a part picked: its hitsounds and sample sets
        using HitSoundType = neomod::DatabaseBeatmapTypes::HitSoundType;
        const Compose::Part head{.edge = 0}, tail{.edge = 1}, body{.edge = std::nullopt};
        compose.select({idAt(2000)});
        state.clock.seek(2000);
        TEST_ASSERT(compose.partAt({101.f, 99.f}) == head && compose.partAt({239.f, 101.f}) == tail &&
                        compose.partAt({170.f, 110.f}) == body && !compose.partAt({170.f, 200.f}),
                    "a click on the selected slider picks the edge there, else its body");
        compose.pick(tail);
        compose.toggleHitSound(HitSoundType::WHISTLE);
        TEST_ASSERT_EQ(lineAt(2000), "100,100,2000,2,0,L|300:100,1,140,0|2,0:0|0:0,0:0:0:0:",
                       "a whistle on the tail: the edge lists written, with the sample");
        TEST_ASSERT(compose.hasHitSound(HitSoundType::WHISTLE), "...which the sidebar shows");
        compose.pick(head);
        TEST_ASSERT(!compose.hasHitSound(HitSoundType::WHISTLE), "...not on the head");
        compose.setSampleSet(false, 2);
        TEST_ASSERT_EQ(lineAt(2000), "100,100,2000,2,0,L|300:100,1,140,0|2,2:0|0:0,0:0:0:0:", "the head's sample set");
        compose.pick(body);
        compose.toggleHitSound(HitSoundType::CLAP);
        compose.setSampleSet(true, 3);
        TEST_ASSERT_EQ(lineAt(2000), "100,100,2000,2,8,L|300:100,1,140,0|2,2:0|0:0,0:3:0:0:",
                       "the body's (the slide's) clap and addition set, its edges as they were");
        TEST_ASSERT(compose.canCancel(), "...Escape takes the pick back first");
        compose.cancel();
        TEST_ASSERT(!compose.getPicked() && compose.getSelection().size() == 1, "...keeping the selection");
        compose.toggleHitSound(HitSoundType::FINISH);
        TEST_ASSERT_EQ(lineAt(2000), "100,100,2000,2,12,L|300:100,1,140,4|6,2:0|0:0,0:3:0:0:",
                       "without one, the whole slider's: its body and every edge");
        compose.startEndDrag(*compose.objectAtTime(2500.0, 20.0));
        compose.dragEnd(3000.0, true);
        compose.endGesture();
        compose.pick({.edge = 2});
        state.clock.seek(3000);
        TEST_ASSERT(compose.partAt({101.f, 99.f}) == Compose::Part{.edge = 2},
                    "where edges meet, the one nearest in time to the playhead");
        compose.select({idAt(4000)});
        compose.select({idAt(2000)});
        TEST_ASSERT(!compose.getPicked(), "the pick goes with the selection");
        undoAll();
        TEST_ASSERT_EQ(text(doc), original, "every edit undone byte for byte");
    }

    // every .osu under -testarg:corpus DIR reads and writes back byte for byte, and edits undo back to it
    void corpus() {
        TEST_SECTION("corpus");
        using Kind = neomod::BeatmapFile::SectionKind;
        using Action = Document::Action;
        const auto dir = Mc::Tests::getTestArg("corpus");
        if(!dir) {
            logRaw("  (needs -testarg:corpus DIR)");
            return;
        }
        namespace fs = std::filesystem;
        u32 files{0}, skipped{0}, roundTrips{0}, undos{0}, patchedMaps{0}, steps{0}, patchedSteps{0};
        u32 shapedMaps{0}, shapedSliders{0}, shapingSteps{0}, refusedSteps{0};
        // catmull curves, lines, perfect circles, beziers
        struct Reversals {
            u32 sliders{0}, offOnce{0}, offTwice{0};  // more than 2 osu!px off
            f32 worst{0.f};
            std::string worstLine;
        };
        std::array<Reversals, 4> reversals{};
        std::error_code ec;
        for(auto it = fs::recursive_directory_iterator(fs::path{*dir}, ec);
            !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if(it->path().extension() != ".osu") continue;
            files++;
            const std::string path = it->path().string();
            auto [doc, error] = Document::read(location(it->path().filename().string()), path);
            if(!doc) {
                skipped++;
                continue;
            }
            std::vector<u8> original;
            if(File file(path); file.canRead()) file.readToVector(original);
            if(Mc::ranges::equal(doc->getBytes(), original)) {
                roundTrips++;
            } else {
                logRaw("  FAIL: {} doesn't write back as it was read", path);
            }

            // a few objects moved, then onto the object before them (stacking under it), given a whistle, a step each,
            // then dragged in time past others: what's read of only the lines that changed (and made again of those
            // objects) is what reading the map whole gives
            {
                auto objects = std::make_unique<MapObjects>(*doc);
                const auto readAgain = [&] {
                    steps++;
                    if(doc->getPatched()) patchedSteps++;
                    if(!objects->update(*doc)) objects = std::make_unique<MapObjects>(*doc);
                };
                const uSz count = objects->objects.size();
                for(uSz i = 1; i < count; i += std::max<uSz>(count / 3, 1)) {
                    const Document::Id id = objects->ids[i];
                    const neomod::HitObject &previous = *objects->objects[i - 1];
                    const vec2 before = previous.getOriginalRawPosAt(previous.getClickTime());
                    const i32 earlier = objects->objects[i >= 3 ? i - 3 : 0]->getClickTime() - 1;
                    const i32 later = objects->objects[std::min(i + 5, count - 1)]->getClickTime() + 1;
                    for(i32 step = 0; step < 3; step++) {
                        auto object = doc->getObject(id);
                        if(!object) break;
                        const vec2 by = step == 0   ? vec2{1.f, 0.f}
                                        : step == 1 ? before - vec2{object->x, object->y}
                                                    : vec2{0.f};
                        object->x += by.x;
                        object->y += by.y;
                        for(vec2 &point : object->curvePoints) point += by;
                        if(step == 2) object->hitSounds ^= 2;
                        doc->change(Action::MOVE, [&](Document::Change &c) { c.setObject(id, *object); });
                        readAgain();
                    }
                    for(const i32 time : {later, earlier, later - 37}) {
                        doc->drag(
                            Action::MOVE,
                            [&](Document::Change &c) {
                                auto object = doc->getObject(id);
                                if(!object) return;
                                const i32 by = time - object->time;
                                object->time += by;
                                if(object->kind == neomod::BeatmapFile::HitObject::Kind::SPINNER) object->endTime += by;
                                c.setObject(id, *object);
                            },
                            1000 + i);
                        readAgain();
                    }
                }
                if(const std::string unlike = unlikeRead(*doc, *objects); !unlike.empty()) {
                    logRaw("  FAIL: {}: what's read of the lines changed differs from the map read whole: {}", path,
                           unlike);
                } else {
                    patchedMaps++;
                }
            }

            // a few sliders shaped by the compose tools (a point moved, one put in on the first edge and taken out
            // again; the first of them also a span more by dragging its end, a whistle on its head and its body
            // additions set, reversed twice alone and twice with the four objects after it), each a step the game
            // reads; what's read of only the lines that changed is what reading the map whole gives, and it all undoes
            // back to the file
            {
                EditorState state;
                state.open(std::move(doc));
                state.divisor = 4;
                Compose &compose = state.compose;
                std::vector<Document::Id> sliders;
                for(uSz i = 0; i < state.objects->objects.size(); i++) {
                    if(state.objects->objects[i]->isSlider()) sliders.push_back(state.objects->ids[i]);
                }
                bool first = true;
                for(uSz i = 0; i < sliders.size(); i += std::max<uSz>(sliders.size() / 3, 1)) {
                    compose.select({sliders[i]});
                    const auto shown = compose.controlPoints();
                    if(!shown || shown->points.size() < 2) continue;
                    shapedSliders++;
                    // the points belong to the slider until the next change
                    const std::vector<vec2> points{shown->points.begin(), shown->points.end()};
                    const vec2 offset = shown->offset;
                    const uSz last = points.size() - 1;
                    const u32 before = state.document->getRevision();
                    compose.startPointDrag(last);
                    compose.dragPoint(points[last] + offset + vec2{8.f, 4.f}, {});
                    compose.endGesture();
                    compose.addControlPoint((points[0] + points[1]) / 2.f + offset, {});
                    compose.endGesture();
                    compose.removeControlPoint(1);
                    if(std::exchange(first, false)) {
                        if(neomod::HitObject *object = state.objects->find(sliders[i]);
                           object && object->isSlider() && object->getDuration() > 0) {
                            const i32 spans = static_cast<const neomod::Slider &>(*object).getRepeat();
                            const f64 end = object->getEndTime() + (f64)object->getDuration() / std::max(spans, 1);
                            if(compose.startEndDrag({.object = object, .id = sliders[i], .node = spans})) {
                                compose.dragEnd(end, true);
                                compose.endGesture();
                            }
                        }
                        compose.select({sliders[i]});
                        compose.pick({.edge = 0});
                        compose.toggleHitSound(neomod::DatabaseBeatmapTypes::HitSoundType::WHISTLE);
                        compose.pick({.edge = std::nullopt});
                        compose.setSampleSet(true, 2);
                        compose.reverse();
                        compose.reverse();
                        std::vector<Document::Id> run;
                        for(uSz j = state.objects->indexes.at(sliders[i]);
                            j < state.objects->objects.size() && run.size() < 5; j++) {
                            run.push_back(state.objects->ids[j]);
                        }
                        compose.select(run);
                        compose.reverse();
                        compose.reverse();
                    }
                    shapingSteps += state.document->getRevision() - before;
                    if(state.document->getError()) refusedSteps++;
                }
                if(const std::string unlike = unlikeRead(*state.document, *state.objects); !unlike.empty()) {
                    logRaw("  FAIL: {}: shaped sliders read otherwise when the map is read whole: {}", path, unlike);
                } else {
                    while(state.document->canUndo()) state.undo();
                    if(Mc::ranges::equal(state.document->getBytes(), original)) {
                        shapedMaps++;
                    } else {
                        logRaw("  FAIL: {}: shaped sliders don't undo back to the file", path);
                    }
                }
                doc = std::move(state.document);
            }

            // every slider path reversed: how far it is from the path backwards, and from it after reversing it again
            // (osu!px at most, along their lengths, both as drawn by the game)
            for(const auto line : doc->getFile().getEntries(Kind::HIT_OBJECTS)) {
                const auto record = doc->getObject(doc->getIdAt(line.number));
                if(!record || record->kind != neomod::BeatmapFile::HitObject::Kind::SLIDER) continue;
                const std::vector<vec2> points = SliderPath::pointsOf(*record);
                const auto type = (neomod::SLIDERCURVETYPE)record->curveType;
                // as read by the game: lengths and points far out are clamped
                if(points.size() < 2 || !std::isfinite(record->length) || std::abs(record->length) > 30000.0 ||
                   Mc::ranges::any_of(points,
                                      [](vec2 p) { return std::abs(p.x) > 30000.f || std::abs(p.y) > 30000.f; })) {
                    continue;
                }
                const f32 length = (f32)std::abs(record->length);
                const auto once = SliderPath::reversed(type, points, length);
                const auto twice = SliderPath::reversed(once.type, once.points, length);
                const neomod::SliderCurve curve{type, points, length};
                const neomod::SliderCurve back{once.type, once.points, length};
                const neomod::SliderCurve again{twice.type, twice.points, length};
                f32 backwards = 0.f, forwards = 0.f;
                for(i32 i = 0; i <= 64; i++) {
                    const f32 t = (f32)i / 64.f;
                    backwards = std::max(backwards, vec::distance(curve.pointAt(1.f - t), back.pointAt(t)));
                    forwards = std::max(forwards, vec::distance(curve.pointAt(t), again.pointAt(t)));
                }
                using enum neomod::SLIDERCURVETYPE;
                Reversals &r = reversals[type == CATMULL                             ? 0
                                         : type == LINEAR                            ? 1
                                         : type == PASSTHROUGH && points.size() == 3 ? 2
                                                                                     : 3];
                r.sliders++;
                if(backwards > 2.f) r.offOnce++;
                if(forwards > 2.f) r.offTwice++;
                if(backwards > r.worst) {
                    r.worst = backwards;
                    r.worstLine =
                        fmt::format("{}: {}", it->path().filename().string(), neomod::BeatmapFile::format(*record));
                }
            }

            // a few edits of each kind, undone, then redone
            doc->change(Action::MAP_SETTINGS, [](Document::Change &c) {
                c.setValue(Kind::EDITOR, "Bookmarks", "1,2,3");
                c.setValue(Kind::GENERAL, "PreviewTime", std::nullopt);
            });
            doc->change(Action::MAP_SETTINGS, [&](Document::Change &c) {
                c.insert(doc->getLines().size(), "256,192,1,1,0,0:0:0:0:");
                c.remove(doc->getLines()[0].id);
            });
            // every object a second later (their lines in time order again, where the file had them out of it)
            std::vector<Document::Id> objectLines;
            for(const auto line : doc->getFile().getEntries(Kind::HIT_OBJECTS)) {
                objectLines.push_back(doc->getIdAt(line.number));
            }
            doc->change(Action::MOVE, [&](Document::Change &c) {
                for(const Document::Id id : objectLines) {
                    if(auto object = doc->getObject(id)) {
                        object->time += 1000;
                        object->endTime += object->kind == neomod::BeatmapFile::HitObject::Kind::SPINNER ? 1000 : 0;
                        c.setObject(id, *object);
                    }
                }
            });
            // every other object at once
            doc->change(Action::DELETE_OBJECTS, [&](Document::Change &c) {
                std::vector<Document::Id> everyOther;
                for(uSz i = 0; i < objectLines.size(); i += 2) everyOther.push_back(objectLines[i]);
                c.remove(everyOther);
            });
            const std::string edited{text(*doc)};
            while(doc->canUndo()) doc->undo();
            const bool backAgain = Mc::ranges::equal(doc->getBytes(), original) && !doc->isChanged();
            while(doc->canRedo()) doc->redo();
            if(backAgain && text(*doc) == edited) {
                undos++;
            } else {
                logRaw("  FAIL: {}: edits don't undo and redo exactly", path);
            }
        }
        logRaw("  corpus: {} files, {} not read (other modes, errors), {} written back as read, {} undone and redone",
               files, skipped, roundTrips, undos);
        logRaw("  corpus: {} of {} steps read only the lines they changed, {} maps read as a whole read reads them",
               patchedSteps, steps, patchedMaps);
        logRaw("  corpus: {} sliders shaped in {} steps ({} refused), {} maps read and undone as they should be",
               shapedSliders, shapingSteps, refusedSteps, shapedMaps);
        for(uSz k = 0; k < reversals.size(); k++) {
            const Reversals &r = reversals[k];
            logRaw(
                "  corpus: {} {} reversed, {} more than 2 osu!px off backwards, {} reversed twice; the worst {:.2f} "
                "({})",
                r.sliders, std::array{"catmull curves", "lines", "perfect circles", "beziers"}[k], r.offOnce,
                r.offTwice, r.worst, r.worstLine);
        }
        TEST_ASSERT(files > 0 && roundTrips == files - skipped && undos == files - skipped,
                    "every map writes back as it was read, and its edits undo and redo exactly");
        TEST_ASSERT_EQ(patchedMaps, files - skipped, "...and what's read of the lines changed is what's read whole");
        TEST_ASSERT_EQ(shapedMaps, files - skipped, "...also with sliders shaped, undone back to the file");
        const auto offPerMillion = [&](uSz k) {
            const Reversals &r = reversals[k];
            return r.sliders == 0 ? 0 : (u64)(r.offOnce + r.offTwice) * 1000000 / r.sliders;
        };
        TEST_ASSERT(offPerMillion(1) <= 500 && offPerMillion(2) <= 500 && offPerMillion(3) <= 500,
                    "lines, perfect circles and beziers reversed (once and twice) are within 2 osu!px of their paths, "
                    "but for at most 1 in 2,000 of each (a short line or a loop's short chord in whole osu!px)");
    }

    std::string_view filter;
    int m_passes{0};
    int m_failures{0};
};
}  // namespace
#endif

bool runEditorTests(std::string_view filter) {
#ifdef MCENGINE_TESTS
    return EditorTests{filter}.run();
#else
    (void)filter;
    debugLog("this build has no tests");
    return true;
#endif
}
