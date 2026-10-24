// Copyright (c) 2026, kiwec, All rights reserved.
#include "StoryboardTest.h"

#include "TestMacros.h"
#include "BeatmapFile.h"
#include "ContainerRanges.h"
#include "Engine.h"
#include "OsuConVars.h"
#include "Environment.h"
#include "File.h"
#include "Parsing.h"
#include "Paths.h"
#include "Storyboard.h"
#include "SyncJthread.h"
#include "Timing.h"
#include "Vectors.h"

#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Mc::Tests {

namespace {
void writeFile(const std::string &path, std::string_view bytes) {
    File file(path, File::MODE::WRITE);
    (void)file.write({reinterpret_cast<const u8 *>(bytes.data()), bytes.size()});
}

// opacity is drawn in steps of a byte
constexpr f32 BYTE = 1.f / 255.f;

Storyboard storyboard(std::string_view events, std::string_view osb = {}) {
    const std::string osu = fmt::format("osu file format v14\n\n[Events]\n{}\n", events);
    return Storyboard{"", osu, osb, "", {}};
}

// a folder of a corpus holding .osu files
struct CorpusSet {
    std::string name;  // relative to the corpus
    std::string folder;
    std::vector<std::string> maps;  // file names, sorted
};

std::vector<CorpusSet> findSets(const std::string &dir) {
    namespace fs = std::filesystem;
    std::vector<CorpusSet> sets;
    std::error_code ec;
    for(auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
        !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path &path = it->path();
        if(path.extension() != ".osu" || !it->is_regular_file(ec)) continue;
        std::string folder = path.parent_path().string() + "/";
        if(sets.empty() || sets.back().folder != folder) {
            sets.push_back({.name = fs::relative(path.parent_path(), dir, ec).generic_string(),
                            .folder = std::move(folder),
                            .maps = {}});
        }
        sets.back().maps.push_back(path.filename().string());
    }
    Mc::ranges::sort(sets, {}, &CorpusSet::name);
    for(auto &set : sets) Mc::ranges::sort(set.maps);
    return sets;
}

std::string readFile(const std::string &path) {
    File file(path);
    return file.canRead() ? file.readToString() : std::string{};
}

// the .osb the game would read next to this map
std::string readOsb(const std::string &folder, std::string_view osu) {
    using neomod::BeatmapFile;
    std::string artist, title, creator;
    BeatmapFile::KeyValue kv;
    for(const auto line : BeatmapFile{osu}.getEntries(BeatmapFile::SectionKind::METADATA)) {
        if(!BeatmapFile::parse(line.text, kv)) continue;
        if(kv.key == "Artist") artist = kv.value;
        if(kv.key == "Title") title = kv.value;
        if(kv.key == "Creator") creator = kv.value;
    }
    const std::string osb = Storyboard::findOsbFile(folder, artist, title, creator);
    return osb.empty() ? std::string{} : readFile(folder + osb);
}

// the latest end of the sprites that end
f64 lastSpriteEnd(const Storyboard &sb) {
    f64 last = 0.0;
    for(uSz i = 0; i < sb.getNumSprites(); i++) {
        if(const f64 end = sb.getSpriteSpan(i).second; std::isfinite(end)) last = std::max(last, end);
    }
    return last;
}

// a sprite's start, its end and 8 times between (a sprite with triggers is there all along: from 0 to the end of the
// storyboard's other sprites)
std::array<f64, 10> sampleTimes(const Storyboard &sb, uSz sprite) {
    auto [start, end] = sb.getSpriteSpan(sprite);
    if(!std::isfinite(start)) start = 0.0;
    if(!std::isfinite(end)) end = std::max(start, lastSpriteEnd(sb));
    std::array<f64, 10> times{};
    for(uSz i = 0; i < times.size(); i++) times[i] = start + (end - start) * (f64)i / (f64)(times.size() - 1);
    return times;
}

bool isFinite(const Storyboard::SpriteState &s) {
    return std::isfinite(s.pos.x) && std::isfinite(s.pos.y) && std::isfinite(s.scale.x) && std::isfinite(s.scale.y) &&
           std::isfinite(s.rotation) && std::isfinite(s.colour.x) && std::isfinite(s.colour.y) &&
           std::isfinite(s.colour.z) && std::isfinite(s.alpha);
}

std::string formatState(const std::optional<Storyboard::SpriteState> &s) {
    if(!s) return "hidden";
    return fmt::format("pos {:.6g},{:.6g} scale {:.6g},{:.6g} rot {:.6g} col {:.4g},{:.4g},{:.4g} a {:.6g} {}{}{} f{}",
                       s->pos.x, s->pos.y, s->scale.x, s->scale.y, s->rotation, s->colour.x, s->colour.y, s->colour.z,
                       s->alpha, s->flipH ? 'H' : '-', s->flipV ? 'V' : '-', s->additive ? 'A' : '-', s->frame);
}

// what one map of a set does in the corpus check
struct MapResult {
    uSz sprites{0};
    uSz samples{0};
    uSz missingImages{0};  // sprites without an image for their first frame
    uSz missingSamples{0};
    f64 parseTime{0.0};
    std::vector<std::string> problems;
    std::string dump;
};

MapResult checkMap(const CorpusSet &set, const std::string &map, bool dump) {
    MapResult result;
    const std::string osu = readFile(set.folder + map);
    const std::string osb = readOsb(set.folder, osu);
    const f64 start = Timing::getTimeReal();
    const Storyboard sb{set.folder, osu, osb, "", {}};
    result.parseTime = Timing::getTimeReal() - start;
    result.sprites = sb.getNumSprites();
    result.samples = sb.getNumSamples();
    for(uSz i = 0; i < result.sprites; i++) result.missingImages += sb.getImageFile(i).empty();
    for(uSz i = 0; i < result.samples; i++) result.missingSamples += sb.getSampleFile(i).empty();

    if(const auto span = sb.getTimeSpan(); span && (!std::isfinite(span->first) || !std::isfinite(span->second))) {
        result.problems.push_back(fmt::format("{}: time span {} to {}", map, span->first, span->second));
    }
    if(dump) result.dump = fmt::format("== {}\n", set.name);
    for(uSz i = 0; i < sb.getNumSprites(); i++) {
        if(const auto [first, last] = sb.getSpriteSpan(i); !(first <= last)) {
            result.problems.push_back(fmt::format("{}: sprite {} spans {} to {}", map, i, first, last));
            continue;
        }
        if(dump) result.dump += fmt::format("-- {}\n", i);
        for(const f64 time : sampleTimes(sb, i)) {
            const auto state = sb.getSpriteState(i, time);
            if(state && !isFinite(*state)) {
                result.problems.push_back(fmt::format("{}: sprite {} at {}: {}", map, i, time, formatState(state)));
            }
            if(dump) result.dump += fmt::format("{:.1f}: {}\n", time, formatState(state));
        }
    }
    return result;
}

// parse time and an update + draw sweep at 60 Hz over the whole storyboard of the heaviest maps, best of `rounds`
// (without the set's folder, so no image is found and drawing only works out each sprite's state)
void bench(const std::vector<CorpusSet> &sets, int rounds) {
    struct Heavy {
        std::string name, osu, osb;
        uSz sprites;
    };
    std::vector<Heavy> heavy;
    for(const auto &set : sets) {
        std::string osu = readFile(set.folder + set.maps.front());
        std::string osb = readOsb(set.folder, osu);
        const uSz sprites = Storyboard{"", osu, osb, "", {}}.getNumSprites();
        heavy.push_back({.name = set.name, .osu = std::move(osu), .osb = std::move(osb), .sprites = sprites});
    }
    Mc::ranges::sort(heavy, std::greater{}, &Heavy::sprites);
    heavy.resize(std::min<uSz>(heavy.size(), 5));

    const Storyboard::View view{.screen = McRect{0.f, 0.f, 1920.f, 1080.f}};
    for(const auto &map : heavy) {
        f64 parse = 1e9, sweep = 1e9;
        u64 frames = 0;
        for(int r = 0; r < rounds; r++) {
            const f64 start = Timing::getTimeReal();
            Storyboard sb{"", map.osu, map.osb, "", {}};
            parse = std::min(parse, Timing::getTimeReal() - start);

            sb.load(true);
            f64 first = 0.0;
            for(uSz i = 0; i < sb.getNumSprites(); i++) first = std::min(first, sb.getSpriteSpan(i).first);
            const f64 last = lastSpriteEnd(sb);
            const f64 sweepStart = Timing::getTimeReal();
            frames = 0;
            for(f64 time = first; time <= last; time += 1000.0 / 60.0, frames++) {
                sb.update(time, true, false);
                sb.draw(Storyboard::Layer::BACKGROUND, Storyboard::Layer::OVERLAY, view);
            }
            sweep = std::min(sweep, Timing::getTimeReal() - sweepStart);
        }
        logRaw("  bench {}: {} sprites, best of {}: parse {:.1f} ms, sweep {:.1f} ms ({} frames, {:.1f} us a frame)",
               map.name, map.sprites, rounds, parse * 1000.0, sweep * 1000.0, frames,
               sweep * 1e6 / (f64)std::max<u64>(frames, 1));
    }
}
}  // namespace

StoryboardTest::StoryboardTest() { logRaw("StoryboardTest created"); }

void StoryboardTest::update() {
    if(!m_ran) {
        m_ran = true;
        runTests();
        if(const auto corpus = getTestArg("corpus")) runCorpus(*corpus);

        TEST_PRINT_RESULTS("StoryboardTest");

        engine->shutdown();
    }
}

void StoryboardTest::runTests() {
    using Layer = Storyboard::Layer;
    using SpriteState = Storyboard::SpriteState;

    TEST_SECTION("commands");
    {
        const auto sb = storyboard(
            "Sprite,Foreground,Centre,\"sb\\a.png\",320,240\n"
            " F,0,1000,2000,0,1\n"
            " M,0,1000,3000,0,0,100,100\n");
        TEST_ASSERT_EQ(sb.getNumSprites(), 1u, "one sprite");
        TEST_ASSERT(!sb.getSpriteState(0, 500), "not there before it fades in");
        const auto mid = sb.getSpriteState(0, 1500);
        TEST_ASSERT(mid.has_value(), "there while fading in");
        if(mid) {
            TEST_ASSERT_NEAR(mid->alpha, 0.5f, BYTE, "halfway faded in");
            TEST_ASSERT_NEAR(mid->pos.x, 25.f, 1e-3f, "moved a quarter of the way");
        }
        const auto held = sb.getSpriteState(0, 2500);
        TEST_ASSERT(held && held->alpha == 1.f, "keeps the end value after a command");
        TEST_ASSERT(sb.getSpriteState(0, 3000) && !sb.getSpriteState(0, 3000.5), "there until the last command ends");
        TEST_ASSERT(sb.getTimeSpan() == std::pair(1000.0, 3000.0), "time span");
    }

    TEST_SECTION("easing");
    {
        const auto sb = storyboard("Sprite,Background,TopLeft,a.png,0,0\n_F,1,0,1000,0,1\n_S,0,0,1000,2\n");
        const auto state = sb.getSpriteState(0, 500);
        TEST_ASSERT(state.has_value(), "sprite there");
        if(state) {
            TEST_ASSERT_NEAR(state->alpha, 0.75f, BYTE, "easing 1 decelerates (osu!'s \"Out\")");
            TEST_ASSERT_NEAR(state->scale.x, 2.f, 1e-4f, "a single value holds over the whole command");
        }
    }

    TEST_SECTION("loops");
    {
        const auto sb = storyboard(
            "Sprite,Pass,Centre,a.png,320,240\n"
            " L,1000,3\n"
            "  F,0,0,500,0,1\n"
            "  F,0,500,1000,1,0\n");
        TEST_ASSERT(!sb.getSpriteState(0, 900), "not there before the loop");
        const auto first = sb.getSpriteState(0, 1250);
        const auto second = sb.getSpriteState(0, 2250);
        const auto fadingOut = sb.getSpriteState(0, 2750);
        TEST_ASSERT(first && second && fadingOut, "there during the loop");
        if(first && second && fadingOut) {
            TEST_ASSERT_NEAR(first->alpha, 0.5f, BYTE, "first iteration");
            TEST_ASSERT_NEAR(second->alpha, 0.5f, BYTE, "second iteration");
            TEST_ASSERT_NEAR(fadingOut->alpha, 0.5f, BYTE, "second iteration fading out");
        }
        TEST_ASSERT(sb.getSpriteState(0, 4000).has_value(), "there until the last iteration ends");
        TEST_ASSERT(!sb.getSpriteState(0, 4000.5), "gone after the last iteration");
    }

    TEST_SECTION("shorthand");
    {
        const auto sb = storyboard("Sprite,Background,Centre,a.png,0,0\n_M,0,0,100,0,0,10,10,20,0\n");
        const auto state = sb.getSpriteState(0, 150);
        TEST_ASSERT(state.has_value(), "sprite there");
        if(state) {
            TEST_ASSERT_NEAR(state->pos.x, 15.f, 1e-4f, "second step x");
            TEST_ASSERT_NEAR(state->pos.y, 5.f, 1e-4f, "second step y");
        }
    }

    TEST_SECTION("parameters");
    {
        const auto sb = storyboard(
            "Sprite,Background,Centre,a.png,0,0\n"
            "_F,0,0,1000,1\n"
            "_P,0,100,200,H\n"
            "_P,0,300,,V\n");
        const auto before = sb.getSpriteState(0, 50);
        const auto during = sb.getSpriteState(0, 150);
        const auto after = sb.getSpriteState(0, 250);
        TEST_ASSERT(before && during && after, "sprite there");
        if(before && during && after) {
            TEST_ASSERT(!before->flipH && during->flipH && !after->flipH, "a lasting parameter only while it lasts");
            TEST_ASSERT(before->flipV && after->flipV, "an instant parameter for good (from the start, as in osu!)");
        }
    }

    TEST_SECTION("quirks");
    {
        const auto sb = storyboard(
            "Sprite,Background,Centre,a.png,0,0\n"
            "_F,0,0,1000,1.5\n"
            "Sprite,Background,Centre,b.png,0,0\n"
            "Sprite,3,1,c.png,1,2\n"
            "_F,0,500,,0.25\n"
            "_F,0,500,1000,0.25\n");
        TEST_ASSERT_EQ(sb.getNumSprites(), 2u, "a sprite without commands is left out");
        const auto flicker = sb.getSpriteState(0, 500);
        TEST_ASSERT(flicker && flicker->alpha == 126.f / 255.f, "an opacity above 1 wraps around");
        const auto numeric = sb.getSpriteState(1, 600);
        TEST_ASSERT(numeric && numeric->pos == vec2(1.f, 2.f) && numeric->alpha == 63.f / 255.f,
                    "layer and origin by number, an empty end time");
        TEST_ASSERT(!storyboard("Sprite,Nowhere,Centre,a.png,0,0\n_F,0,0,1,1\n").getNumSprites(), "unknown layer");
        const auto nan = storyboard("Sprite,Background,Centre,a.png,1,2\n_F,0,0,10,1\n_M,0,5,10,NaN,NaN,NaN,NaN\n");
        const auto kept = nan.getSpriteState(0, 8);
        TEST_ASSERT(kept && kept->pos == vec2(1.f, 2.f), "a NaN value drops its line");
    }

    TEST_SECTION("variables");
    {
        const auto sb = storyboard(
            "", "[Variables]\n$x=320\n$pic=\"a b.png\"\n\n[Events]\nSprite,Overlay,Centre,$pic,$x,240\n_F,0,0,10,1\n");
        const auto state = sb.getSpriteState(0, 5);
        TEST_ASSERT(state && state->pos == vec2(320.f, 240.f), "variables replaced in the .osb");
    }

    TEST_SECTION("triggers");
    {
        auto sb = storyboard(
            "Sprite,Foreground,Centre,a.png,0,0\n"
            "_F,0,0,500,1\n"
            "_S,0,0,750,1\n"
            "_T,Passing,0,10000\n"
            "__F,0,0,100,0,1\n"
            "_T,HitSoundDrumWhistle\n"
            "__S,0,0,100,3\n"
            "Sprite,Foreground,Centre,b.png,0,0\n"
            "_S,0,0,10000,1\n"
            "_T,HitSoundDrumWhistle\n"
            "__S,0,0,100,3\n"
            "Sprite,Foreground,Centre,c.png,0,0\n"
            "_T,HitSoundDrumWhistle,2000,3000\n"
            "__F,0,0,100,1\n");
        sb.update(500, false, false);
        TEST_ASSERT(sb.getSpriteState(0, 550)->alpha == 1.f, "nothing fired yet");
        sb.update(600, true, false);
        TEST_ASSERT_NEAR(sb.getSpriteState(0, 650)->alpha, 0.5f, BYTE, "Passing fired on the change to passing");
        sb.onHitSound({.normalSet = 0, .additionSet = 1, .additions = 1, .index = 0}, 700);
        TEST_ASSERT(sb.getSpriteState(0, 750)->scale.x == 1.f, "a whistle of another set doesn't fire it");
        sb.onHitSound({.normalSet = 0, .additionSet = 2, .additions = 1, .index = 0}, 800);
        TEST_ASSERT(sb.getSpriteState(0, 850)->scale.x == 3.f, "a drum whistle fires it");
        TEST_ASSERT(sb.getSpriteState(1, 850)->scale.x == 1.f, "the sprite's own running command comes first");
        TEST_ASSERT(sb.getSpriteState(0, 20000).has_value(), "a sprite with triggers stays after its own commands");
        sb.onHitSound({.normalSet = 0, .additionSet = 2, .additions = 1, .index = 0}, 20000);
        TEST_ASSERT(sb.getSpriteState(0, 20050)->scale.x == 3.f, "and its triggers fire after them");
        TEST_ASSERT_EQ(sb.getNumSprites(), 3u, "a sprite with only triggers is kept");
        const auto unfired = sb.getSpriteState(2, 850);
        TEST_ASSERT(unfired && unfired->alpha == 0.f, "and shows nothing until a trigger sets its opacity");
        sb.onHitSound({.normalSet = 0, .additionSet = 2, .additions = 1, .index = 0}, 2500);
        TEST_ASSERT(sb.getSpriteState(2, 2550)->alpha == 1.f, "a trigger fires within its own window");
        sb.update(500, true, true);
        TEST_ASSERT(sb.getSpriteState(0, 550)->alpha == 1.f, "seeking forgets what fired");
        TEST_ASSERT(sb.getSpriteState(2, 2550)->alpha == 0.f, "seeking forgets what fired, even far away");
    }

    TEST_SECTION("overlaps");
    {
        // the running command that started first wins, then the one that ended last (in order of start), then the
        // first to come gives its start value; MX on a sprite kept alive by a fade from -5000 to 20000
        struct Expect {
            f64 time;
            f32 x;
        };
        const auto positions = [&](std::string_view what, std::string_view commands,
                                   std::initializer_list<Expect> expects) {
            const auto sb =
                storyboard(fmt::format("Sprite,Background,Centre,a.png,0,0\n_F,0,-5000,20000,1\n{}", commands));
            for(const auto &[time, x] : expects) {
                const auto state = sb.getSpriteState(0, time);
                TEST_ASSERT(state && state->pos.x == x,
                            fmt::format("{} at {}: x {}, got {}", what, time, x, state ? state->pos.x : NAN));
            }
        };
        // (storybrew's test/CommandOverlapTest.cs, test/CommandReversedTest.cs, test/LoopOverlapTest.cs and
        // test/LoopOffsetTest.cs)
        positions("gap", "_MX,0,0,1000,0,100\n_MX,0,2000,3000,20000,30000\n",
                  {{-1000, 0},
                   {0, 0},
                   {500, 50},
                   {1000, 100},
                   {1500, 100},
                   {2000, 20000},
                   {2500, 25000},
                   {3000, 30000},
                   {4000, 30000}});
        positions("touching", "_MX,0,0,1000,0,100\n_MX,0,1000,2000,10000,20000\n",
                  {{-1000, 0}, {0, 0}, {500, 50}, {1000, 100}, {1500, 15000}, {2000, 20000}, {3000, 20000}});
        positions(
            "partial", "_MX,0,0,1000,0,100\n_MX,0,500,1500,5000,15000\n",
            {{-1000, 0}, {250, 25}, {500, 50}, {750, 75}, {1000, 100}, {1250, 12500}, {1500, 15000}, {2000, 15000}});
        positions("complete", "_MX,0,0,1000,0,100\n_MX,0,250,750,200,300\n",
                  {{-1000, 0}, {250, 25}, {500, 50}, {750, 75}, {1000, 100}, {2000, 300}});
        positions("same start", "_MX,0,0,1000,0,100\n_MX,0,0,2000,1000,2000\n",
                  {{-1000, 0}, {500, 50}, {1000, 100}, {1500, 1750}, {2000, 2000}, {3000, 2000}});
        positions("same end", "_MX,0,0,1000,0,100\n_MX,0,500,1000,1000,2000\n",
                  {{-1000, 0}, {500, 50}, {750, 75}, {1000, 100}, {2000, 2000}});
        positions("same start and end", "_MX,0,0,1000,100,200\n_MX,0,0,1000,-100,-200\n",
                  {{-1000, 100}, {0, 100}, {500, 150}, {1000, 200}, {2000, -200}});
        // (an end before the start is the start: storybrew still has 200 at 2000)
        positions("reversed", "_MX,0,2000,1000,200,100\n",
                  {{0, 200}, {1000, 200}, {1999, 200}, {2001, 100}, {3000, 100}});
        const std::initializer_list<Expect> repeated{{-1000, 0},  {0, 0},      {500, 50},   {1000, 100},
                                                     {1500, 150}, {2000, 200}, {2500, 50},  {3000, 100},
                                                     {3500, 150}, {4000, 200}, {10000, 200}};
        positions("plain",
                  "_MX,0,0,1000,0,100\n_MX,0,1000,2000,100,200\n_MX,0,2000,3000,0,100\n_MX,0,3000,4000,100,200\n",
                  repeated);
        positions("loop", "_L,0,2\n__MX,0,0,1000,0,100\n__MX,0,1000,2000,100,200\n", repeated);
        positions("overlapping loops",
                  "_L,0,2\n__MX,0,0,1000,0,100\n__F,0,1000,2000,1,1\n"
                  "_L,0,2\n__F,0,0,1000,1,1\n__MX,0,1000,2000,100,200\n",
                  repeated);
        // (storybrew has 100 where an iteration ends and the next starts: there the command is on the next one)
        positions("loop starting later", "_L,0,2\n__MX,0,1000,2000,0,100\n",
                  {{-1000, 0}, {0, 0}, {1000, 0}, {1500, 50}, {2000, 0}, {2500, 50}, {3000, 100}, {4000, 100}});
        positions("loop starting earlier", "_L,0,2\n__MX,0,-500,500,0,100\n",
                  {{-1000, 0}, {-500, 0}, {0, 50}, {500, 0}, {1000, 50}, {1500, 100}, {2000, 100}});

        // (test/CommandTest.cs) a parameter runs while its P does; one without length is on before and after it
        struct ExpectParameter {
            f64 time;
            bool on;
        };
        const auto additive = [&](std::string_view what, std::string_view commands,
                                  std::initializer_list<ExpectParameter> expects) {
            const auto sb =
                storyboard(fmt::format("Sprite,Background,Centre,a.png,0,0\n_F,0,-5000,20000,1\n{}", commands));
            for(const auto &[time, on] : expects) {
                const auto state = sb.getSpriteState(0, time);
                TEST_ASSERT(state && state->additive == on, fmt::format("{} at {}", what, time));
            }
        };
        additive("point", "_P,0,1000,1000,A\n", {{0, true}, {1000, true}, {2000, true}});
        additive("lasting", "_P,0,1000,2000,A\n",
                 {{0, false}, {1000, true}, {1500, true}, {2000, true}, {3000, false}});
        additive("lasting after a point", "_P,0,1000,1000,A\n_P,0,2000,3000,A\n",
                 {{0, true}, {1000, true}, {1500, true}, {2500, true}, {3000, true}, {4000, false}});
        additive("lasting before a point", "_P,0,1000,2000,A\n_P,0,3000,3000,A\n",
                 {{0, true}, {1500, true}, {2000, true}, {2500, false}, {3000, true}, {4000, true}});
        for(int i = 0; i < 3; i++) {
            const auto sb = storyboard(
                "Sprite,Background,Centre,a.png,0,0\n_F,0,-5000,20000,1\n_L,0,3\n__P,0,0,200,H\n__P,0,100,300,V\n");
            const auto at = [&](f64 time) { return sb.getSpriteState(0, i * 300 + time).value_or(SpriteState{}); };
            TEST_ASSERT(at(0).flipH && at(100).flipH && at(200).flipH && !at(250).flipH, "a P in a loop, H");
            TEST_ASSERT(!at(50).flipV && at(100).flipV && at(200).flipV && at(300).flipV, "a P in a loop, V");
        }
        TEST_ASSERT(!storyboard("Sprite,Background,Centre,a.png,0,0\n_F,0,-5000,20000,1\n_L,0,3\n__P,0,0,200,H\n")
                         .getSpriteState(0, 2000)
                         ->flipH,
                    "a P in a loop, after the loop");
    }

    TEST_SECTION("time span");
    {
        // every command line's own times and the samples': loops not repeated, triggers' commands as written
        const auto sb = storyboard(
            "Sprite,Background,Centre,a.png,0,0\n_F,0,-3000,1000,1\n_L,5000,10\n__F,0,0,1000,1,0\n"
            "_T,HitSound,0,90000\n__S,0,-4000,100,2\nSample,7000,0,a.wav\n");
        TEST_ASSERT(sb.getTimeSpan() == std::pair(-4000.0, 7000.0), "commands and samples as written");
        TEST_ASSERT(!storyboard("Sprite,Background,Centre,a.png,0,0\n").getTimeSpan(), "none without commands");
    }

    TEST_SECTION("shared slots");
    {
        // S and V share one flag: the one that applies sets its value and the other's stays as it was
        const auto sv =
            storyboard("Sprite,Background,Centre,a.png,0,0\n_F,0,0,8000,1\n_S,0,5000,,3\n_V,0,0,1000,2,2,1,1\n");
        const auto scaleAt = [&](f64 time) { return sv.getSpriteState(0, time).value_or(SpriteState{}).scale.x; };
        TEST_ASSERT_NEAR(scaleAt(500), 1.5f, 1e-4f, "a V running leaves S as it was (1, not the 3 to come)");
        TEST_ASSERT_NEAR(scaleAt(2000), 1.f, 1e-4f, "a finished V still beats an S to come");
        TEST_ASSERT_NEAR(scaleAt(6000), 3.f, 1e-4f, "then S applies and V stays at its end");

        // M, MX and MY: a running M doesn't stop a finished MY, an M to come is held back by an MY
        const auto m = storyboard(
            "Sprite,Background,Centre,a.png,320,240\n_F,0,0,8000,1\n_M,0,1000,2000,0,0,100,100\n"
            "_MY,0,0,500,50,60\n");
        const auto posAt = [&](f64 time) { return m.getSpriteState(0, time).value_or(SpriteState{}).pos; };
        TEST_ASSERT(posAt(250) == vec2(320.f, 55.f), "an M to come doesn't apply while MY did");
        TEST_ASSERT(posAt(1500) == vec2(50.f, 60.f), "a finished MY still sets y next to a running M");
        TEST_ASSERT(posAt(3000) == vec2(100.f, 60.f), "after both, the finished MY sets y and M only x");

        // played on a 60 Hz grid from the first command: the same whatever times it's asked for, or updated at
        auto updated =
            storyboard("Sprite,Background,Centre,a.png,0,0\n_F,0,0,8000,1\n_S,0,5000,,3\n_V,0,0,1000,2,2,1,1\n");
        for(f64 time = 0; time < 7000; time += 7.3) updated.update(time, true, false);
        TEST_ASSERT(updated.getSpriteState(0, 6990)->scale == sv.getSpriteState(0, 6990)->scale,
                    "updates don't change it");
        updated.update(500, true, true);
        TEST_ASSERT_NEAR(updated.getSpriteState(0, 500)->scale.x, 1.5f, 1e-4f, "nor does seeking back");
    }

    TEST_SECTION("values");
    {
        const auto sb = storyboard(
            "Sprite,Background,Centre,a.png,0,0\n_F,0,0,1000,2\n_S,0,0,1000,-1\n_V,0,0,1000,-2,1\n"
            "Sprite,Background,Centre,a.png,0,0\n_F,0,0,1000,-0.06\n"
            "Sprite,Background,Centre,a.png,0,0\n_F,0,0,1000,1.004\n");
        const auto opaque = sb.getSpriteState(0, 500);
        TEST_ASSERT(opaque && opaque->alpha == 254.f / 255.f, "an opacity of 2 is nearly opaque");
        TEST_ASSERT(opaque && opaque->scale == vec2(0.f), "a negative S is 0");
        TEST_ASSERT(sb.getSpriteState(1, 500)->alpha == 241.f / 255.f, "a negative opacity wraps around too");
        TEST_ASSERT(sb.getSpriteState(2, 500)->alpha == 0.f, "and one just above 1 is invisible");
        const auto mirrored =
            storyboard("Sprite,Background,Centre,a.png,0,0\n_F,0,0,1000,1\n_V,0,0,1000,-2,1\n").getSpriteState(0, 500);
        TEST_ASSERT(mirrored && mirrored->scale == vec2(-2.f, 1.f), "a negative V mirrors");
    }

    // (from osu!lazer's test resources)
    TEST_SECTION("lifetimes");
    {
        const auto loops = storyboard(
            "Sprite,Background,TopCentre,\"zero-times.png\",320,240\n"
            " L,1000,0\n"
            "  F,0,0,1000,0,1\n"
            "  F,0,1000,2000,1,0\n"
            "Sprite,Background,TopCentre,\"many-times.png\",320,240\n"
            " L,9000,40\n"
            "  F,0,0,1000,0,1\n"
            "  F,0,1000,2000,1,0\n");
        TEST_ASSERT(loops.getSpriteState(0, 3000) && !loops.getSpriteState(0, 3001), "a loop runs at least once");
        TEST_ASSERT(loops.getSpriteState(1, 89000) && !loops.getSpriteState(1, 89001), "a loop runs its count");

        const auto noop = storyboard(
            "Sprite,Background,TopCentre,\"img.jpg\",320,240\n"
            " F,0,1000,1000,0,0 // should be ignored\n"
            " F,0,1500,1600,0,1\n");
        TEST_ASSERT(!noop.getSpriteState(0, 1499) && noop.getSpriteState(0, 1500), "invisible fades don't count");

        const auto loopFade = storyboard(
            "Sprite,Background,TopCentre,\"img.jpg\",320,240\n"
            " L,1000,1\n"
            "  F,0,0,,1\n"
            " F,0,2000,,0\n");
        TEST_ASSERT(loopFade.getSpriteState(0, 1000).has_value(), "a visible fade in a loop counts");

        const auto animation = storyboard(
            "Animation,Background,Centre,\"img.jpg\",320,240,2,150,LoopForever\n"
            " S,0,1000,1500,0.08 // animation should start playing from this point in time..\n"
            " F,0,2000,,0,1      // .. not this point in time\n"
            " F,0,2000,3000,1\n"
            "Animation,Foreground,Centre,\"once.png\",330,240,10,108,1\n"
            " F,0,0,10000,1\n");
        TEST_ASSERT(!animation.getSpriteState(0, 1999), "an animation shows once it fades in");
        const auto frame = animation.getSpriteState(0, 2150);
        TEST_ASSERT(frame && frame->frame == 1, "and its frames count from its first command");
        const auto once = animation.getSpriteState(1, 5000);
        TEST_ASSERT(once && once->frame == 9, "an animation played once stays at its last frame");

        const auto suffix = storyboard(
            "", "[Variables]\n$var=34\n\n[Events]\nSprite,Background,TopCentre,\"img.jpg\",$var56,240\n_F,0,0,1,1\n");
        TEST_ASSERT(suffix.getSpriteState(0, 0) && suffix.getSpriteState(0, 0)->pos.x == 3456.f,
                    "variable with a suffix");
    }

    TEST_SECTION("hash");
    {
        const std::string dir = Mc::Paths::cache() + "/storyboardtest/";
        Environment::createDirectory(dir + "SB");
        writeFile(dir + "SB/a.png", "a");
        writeFile(dir + "b.png", "b");
        writeFile(dir + "bg.jpg", "bg");
        const std::string ab{
            "osu file format v14\n\n[Events]\nSprite,Background,Centre,\"sb\\A.png\",0,0\n_F,0,0,1,1\n"
            "Sprite,Overlay,Centre,b.png,0,0\n_F,0,0,1,1\n"};
        const std::string ba{
            "osu file format v14\n\n[Events]\nSprite,Overlay,Centre,b.png,0,0\n_F,0,0,1,1\n"
            "Sprite,Background,Centre,\"sb\\A.png\",0,0\n_F,0,0,1,1\n"};
        const auto hashOf = [&dir](std::string_view osu, std::string_view osb = {}) {
            Storyboard sb{dir, osu, osb, "bg.jpg", {}};
            sb.load(false);
            return sb.getHash();
        };

        const MD5Hash first = hashOf(ab);
        TEST_ASSERT(first == hashOf(ab), "the same storyboard, the same hash");
        TEST_ASSERT(first == hashOf(ba), "whatever order the script uses its files in");
        TEST_ASSERT(first != hashOf(ab, "[Events]\n"), "an .osb file counts");
        writeFile(dir + "SB/a.png", "changed");
        TEST_ASSERT(first != hashOf(ab), "a changed image counts");
        writeFile(dir + "SB/a.png", "a");
        writeFile(dir + "bg.jpg", "changed");
        TEST_ASSERT(first != hashOf(ab), "a changed background counts");
        Environment::deletePathsRecursive(dir, 3);
    }

    TEST_SECTION("files");
    {
        const std::string dir = Mc::Paths::cache() + "/storyboardtest/";
        Environment::createDirectory(dir + "set/sb");
        Environment::createDirectory(dir + "skin");
        Environment::createDirectory(dir + "default");
        for(const char *file : {"set/sb/plain.jpg", "set/sb/Hit.OGG", "set/ding.wav", "set/both.png", "skin/both.png",
                                "skin/skinned.png", "skin/skinned@2x.png", "skin/sd.png", "default/fallback.png"}) {
            writeFile(dir + file, "x");
        }
        const std::string osu{
            "osu file format v14\n\n[General]\nUseSkinSprites: 1\n\n[Events]\n"
            "Sprite,Background,Centre,\"sb/plain\",0,0\n_F,0,0,1,1\n"
            "Sprite,Background,Centre,\"both.png\",0,0\n_F,0,0,1,1\n"
            "Sprite,Background,Centre,\"skinned.png\",0,0\n_F,0,0,1,1\n"
            "Sprite,Background,Centre,\"sd\",0,0\n_F,0,0,1,1\n"
            "Sprite,Background,Centre,\"fallback.png\",0,0\n_F,0,0,1,1\n"
            "Sprite,Background,Centre,\"nowhere.png\",0,0\n_F,0,0,1,1\n"
            "Sample,0,0,\"sb/hit.wav\"\nSample,0,0,\"ding\"\nSample,0,0,\"nothing.wav\"\n"};
        const std::vector<std::string> skinDirs{dir + "skin/", dir + "default/"};
        const Storyboard sb{dir + "set", osu, "", "", skinDirs};
        TEST_ASSERT_EQ(sb.getImageFile(0), dir + "set/sb/plain.jpg", "an image named without an extension");
        TEST_ASSERT_EQ(sb.getImageFile(1), dir + "set/both.png", "the set's image before the skin's");
        if(cv::skin_hd.getBool()) TEST_ASSERT_EQ(sb.getImageFile(2), dir + "skin/skinned@2x.png", "a skin's @2x image");
        TEST_ASSERT_EQ(sb.getImageFile(3), dir + "skin/sd.png", "a skin's image named without an extension");
        TEST_ASSERT_EQ(sb.getImageFile(4), dir + "default/fallback.png", "the default skin's image");
        TEST_ASSERT_EQ(sb.getImageFile(5), "", "an image nowhere");
        TEST_ASSERT_EQ(sb.getSampleFile(0), dir + "set/sb/Hit.OGG", "a sample found with another extension");
        TEST_ASSERT_EQ(sb.getSampleFile(1), dir + "set/ding.wav", "a sample named without an extension");
        TEST_ASSERT_EQ(sb.getSampleFile(2), "", "a sample nowhere");

        const Storyboard plain{dir + "set", "osu file format v14\n\n[Events]\nSprite,0,1,skinned.png,0,0\n_F,0,0,1,1\n",
                               "", "", skinDirs};
        TEST_ASSERT_EQ(plain.getImageFile(0), "", "no skin images without UseSkinSprites");
        Environment::deletePathsRecursive(dir, 3);
    }

    TEST_SECTION("background");
    {
        const std::string osu{
            "osu file format "
            "v14\n\n[Events]\n0,0,\"bg.jpg\",10,-20\nSprite,Background,Centre,\"BG.jpg\",320,240\n_F,0,0,10,1\n"};
        const Storyboard sb{"", osu, "", "bg.jpg", {}};
        TEST_ASSERT(sb.replacesBackground(), "a background layer sprite of the background replaces it");
        TEST_ASSERT(sb.getBackgroundOffset() == vec2(10.f, -20.f), "background offset");
        TEST_ASSERT(sb.getSpriteState(0, 5).has_value(), "sprite there");
        (void)Layer::OVERLAY;
    }
}

void StoryboardTest::runCorpus(const std::string &dir) {
    TEST_SECTION(fmt::format("corpus {}", dir));
    const auto sets = findSets(dir);
    TEST_ASSERT(!sets.empty(), "the corpus has sets");

    if(const auto rounds = getTestArg("bench")) {
        bench(sets, std::max(1, Parsing::strto<i32>(*rounds)));
        return;
    }

    // every map of every set; only the first map of a set is dumped (the .osb is the same for all)
    struct Job {
        const CorpusSet *set;
        const std::string *map;
    };
    std::vector<Job> jobs;
    for(const auto &set : sets) {
        for(const auto &map : set.maps) jobs.push_back({.set = &set, .map = &map});
    }
    const auto dumpPath = getTestArg("dump");
    const f64 start = Timing::getTimeReal();
    std::vector<MapResult> results(jobs.size());
    {
        std::atomic<uSz> next{0};
        std::vector<Sync::jthread> workers;
        const unsigned count = std::max(1u, std::thread::hardware_concurrency());
        for(unsigned i = 0; i < count; i++) {
            workers.emplace_back([&] {
                for(uSz j; (j = next.fetch_add(1)) < jobs.size();) {
                    const Job &job = jobs[j];
                    results[j] = checkMap(*job.set, *job.map, dumpPath && job.map == &job.set->maps.front());
                }
            });
        }
    }
    logRaw("  {} sets, {} maps in {:.2f} s", sets.size(), jobs.size(), Timing::getTimeReal() - start);

    uSz problems = 0, sprites = 0, samples = 0, setsMissingImages = 0, setsMissingSamples = 0;
    for(uSz j = 0; j < jobs.size();) {
        const CorpusSet &set = *jobs[j].set;
        f64 parse = 0.0;
        uSz setSprites = 0, setSamples = 0, missingImages = 0, missingSamples = 0;
        for(; j < jobs.size() && jobs[j].set == &set; j++) {
            parse += results[j].parseTime;
            setSprites = std::max(setSprites, results[j].sprites);
            setSamples = std::max(setSamples, results[j].samples);
            missingImages = std::max(missingImages, results[j].missingImages);
            missingSamples = std::max(missingSamples, results[j].missingSamples);
            for(const auto &problem : results[j].problems) {
                if(problems++ < 20) logRaw("  {}: {}", set.name, problem);
            }
        }
        sprites += setSprites, samples += setSamples;
        setsMissingImages += missingImages > 0, setsMissingSamples += missingSamples > 0;
        logRaw("  {}: {} maps, {} sprites, {} samples, parsed in {:.1f} ms, files missing: {} images, {} samples",
               set.name, set.maps.size(), setSprites, setSamples, parse * 1000.0, missingImages, missingSamples);
    }
    logRaw("  {} sprites, {} samples (the most of each set's maps); sets missing images: {}, samples: {}", sprites,
           samples, setsMissingImages, setsMissingSamples);
    TEST_ASSERT_EQ(problems, 0, "every span is ordered and every sampled state is finite");

    if(dumpPath) {
        std::ofstream out(*dumpPath, std::ios::binary);
        for(const auto &r : results) out << r.dump;
        logRaw("  wrote {}", *dumpPath);
    }
}

}  // namespace Mc::Tests
