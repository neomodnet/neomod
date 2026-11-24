// Copyright (c) 2026, kiwec, All rights reserved.
#include "JudgeTest.h"

#include "ContainerRanges.h"
#include "TestMacros.h"
#include "ConVar.h"
#include "ConVarDefs.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "File.h"
#include "GameRules.h"
#include "GameplayJudge.h"
#include "Graphics.h"
#include "HitObjects.h"
#include "HitSounds.h"
#include "Logging.h"
#include "ModFlags.h"
#include "OsuConVars.h"
#include "Parsing.h"
#include "Paths.h"
#include "PlayfieldView.h"
#include "RenderTarget.h"
#include "Replay.h"
#include "ReplayFile.h"
#include "ResourceManager.h"
#include "Skin.h"
#include "Timing.h"
#include "crypto.h"
#include "score.h"

#include "fmt/format.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Mc::Tests {
using namespace neomod;
using namespace flags::operators;

namespace {

struct Hash {
    u64 h{1469598103934665603ULL};
    void bytes(const void *p, uSz n) {
        const auto *b = static_cast<const u8 *>(p);
        for(uSz i = 0; i < n; i++) {
            h ^= b[i];
            h *= 1099511628211ULL;
        }
    }
    template <typename T>
    void add(const T &v) {
        bytes(&v, sizeof(T));
    }
};

// what the judge tells its listener, written down
class Recorder final : public GameplayJudge::Listener {
   public:
    Recorder(std::string &out, const GameplayJudge *&judge) : out(out), judge(judge) {}

    void playHitSound(DatabaseBeatmapTypes::HITSAMPLE_BITS s, vec2 rawPos, i32 delta, i32 timeMS) override {
        fmt::format_to(std::back_inserter(out), "hs {} {} {} {} {} {} {} {} {} {}\n", now(), s.hitSounds, s.normalSet,
                       s.additionSet, s.volume, s.index, rawPos.x, rawPos.y, delta, timeMS);
    }
    void playSliderTickSound(DatabaseBeatmapTypes::HITSAMPLE_BITS s, vec2 rawPos, i32 timeMS) override {
        fmt::format_to(std::back_inserter(out), "tick {} {} {} {} {} {}\n", now(), s.hitSounds, s.normalSet, rawPos.x,
                       rawPos.y, timeMS);
    }
    std::vector<HitSoundUtils::Set_Slider_Hit> updateSliderSlideSounds(
        bool sliding, DatabaseBeatmapTypes::HITSAMPLE_BITS s, vec2 rawPos,
        const std::vector<HitSoundUtils::Set_Slider_Hit> & /*started*/) override {
        slides.add(now());
        slides.add(sliding);
        slides.add(s.hitSounds);
        slides.add(rawPos);
        return {};
    }
    void stopSliderSounds(const std::vector<HitSoundUtils::Set_Slider_Hit> & /*started*/) override {
        out += fmt::format("stopslide {}\n", now());
    }
    void playSpinnerSpinSound(f32 ratio, DatabaseBeatmapTypes::HITSAMPLE_BITS s) override {
        spins.add(now());
        spins.add(ratio);
        spins.add(s.hitSounds);
    }
    void stopSpinnerSpinSound() override { spins.add(-now()); }
    void playSpinnerBonusSound(DatabaseBeatmapTypes::HITSAMPLE_BITS /*s*/) override {
        out += fmt::format("bonus {}\n", now());
    }
    void addTargetHit(f32 delta, f32 angle) override { out += fmt::format("target {} {} {}\n", now(), delta, angle); }
    void onHitResult(i32 object, LiveHitResult hit, i32 delta, bool timed) override {
        out += fmt::format("hit {} {} {} {} {}\n", now(), object, (int)hit, delta, timed);
    }
    void onComboBreak() override { out += fmt::format("break {}\n", now()); }
    void onExtraClick() override { out += fmt::format("extra {}\n", now()); }
    void onHealthGain(f64 healthBefore) override { out += fmt::format("hp {} {}\n", now(), healthBefore); }
    bool onFail(bool byMods) override {
        out += fmt::format("fail {} {}\n", now(), byMods);
        return true;
    }

    std::string &out;
    const GameplayJudge *&judge;
    Hash slides, spins;

   private:
    [[nodiscard]] i32 now() const { return judge ? judge->getCurMusicPos() : 0; }
};

struct Scenario {
    std::string name;
    Replay::Mods mods;
};

void hashState(Hash &frames, Hash &looks, const GameplayJudge &judge, f64 posMS, i32 maxDurationMS) {
    const LiveScore &score = judge.getScore();
    frames.add(judge.getCurMusicPos());
    frames.add(judge.getHealth());
    frames.add(judge.hasFailed());
    frames.add(score.getScore());
    frames.add(score.getCombo());
    frames.add(score.getAccuracy());
    frames.add(judge.getCurrentHitObjectIndex());
    frames.add(judge.getNextHitObjectTime());
    frames.add(judge.getPreviousHitObjectTime());
    frames.add(judge.getPreviousFollowPointObjectIndex());
    frames.add(judge.isInBreak());
    frames.add(judge.isInSkippableSection());
    frames.add(judge.isSpinnerActive());
    frames.add(judge.getNPS());
    frames.add(judge.getND());
    frames.add(judge.getNumCirclesFinished());
    frames.add(judge.getNumSlidersFinished());
    frames.add(judge.getNumSpinnersFinished());
    frames.add(judge.getAutoCursorPos());
    frames.add(judge.isPassing());
    frames.add(judge.getPlayfieldRotation());
    frames.add(judge.holding_slider);
    frames.add(judge.getMisaimObjects().size());
    const HitObjects &objects = judge.getHitObjects();
    const i32 cur = judge.getCurrentHitObject();
    frames.add(cur >= 0 ? objects.clickTimes[cur] : -1);

    // what is on screen around now
    const f32 approach = judge.getApproachTime();
    const auto from = std::partition_point(objects.clickTimes.begin(), objects.clickTimes.end(),
                                           [&](i32 clickTime) { return clickTime < posMS - 1000 - maxDurationMS; });
    for(uSz i = from - objects.clickTimes.begin(); i < objects.size(); i++) {
        if(objects.clickTimes[i] > posMS + approach) break;
        if(objects.endTimes[i] < posMS - 1000) continue;
        const HitObjectLook &look = objects.looks[i];
        looks.add((bool)objects.visible[i]);
        looks.add((bool)objects.finished[i]);
        looks.add((bool)objects.blocked[i]);
        looks.add(look.approachScale);
        looks.add(look.deltaMS);
        looks.add(look.approachTimeMS);
        looks.add(objects.rawPosAt(i, (i32)posMS));
        looks.add(judge.getAutoCursorPos((u32)i, (i32)posMS));
        if(objects.types[i] == HitObjectType::SLIDER) {
            const SliderState &slider = objects.sliderAt(i);
            looks.add(slider.startFinished);
            for(const auto &click : slider.clicks) {
                looks.add(click.finished);
                looks.add(click.successful);
            }
        }
    }
}

// one play of map: the replay frames, or autoplay stepped at 144 fps
std::string judge(DatabaseBeatmap &map, const Scenario &scenario, const std::vector<Replay::Frame> *replay,
                  f64 *benchSecs = nullptr, u64 *benchFrames = nullptr) {
    std::string out = fmt::format("== {}\n", scenario.name);
    const GameplayJudge *judgePtr = nullptr;
    Recorder rec(out, judgePtr);
    LiveScore score(true);
    score.mods = scenario.mods;
    GameplayJudge judge(&map, score, nullptr, benchSecs ? nullptr : &rec);
    judgePtr = &judge;
    if(const auto err = judge.load()) {
        out += fmt::format("load error {}\n", err.error_string());
        return out;
    }

    const HitObjects &objects = judge.getHitObjects();
    const i32 firstMS = objects.clickTimes.front();
    const i32 lastMS = objects.endTimes[judge.getHitObjectsSortedByEndTime().back()];
    i32 maxDurationMS = 0;
    for(uSz i = 0; i < objects.size(); i++) {
        maxDurationMS = std::max(maxDurationMS, objects.endTimes[i] - objects.clickTimes[i]);
    }
    const f64 frameMS = 1000.0 / 144.0;
    Hash frames, looks;

    const f64 start = Timing::getTimeReal();
    u64 numFrames = 0;
    if(replay) {
        judge.replay = *replay;
        const f64 endMS = (f64)std::max(replay->back().cur_music_pos, lastMS) + 1000.0;
        for(f64 t = replay->front().cur_music_pos; t < endMS; t += frameMS) {
            judge.simulate_to((i32)std::round(t));
            judge.animate(t);
            if(!benchSecs) hashState(frames, looks, judge, t, maxDurationMS);
            numFrames++;
        }
    } else {
        const f32 speed = judge.getSpeedMultiplier();
        for(f64 t = firstMS - 2000.0; t < lastMS + 1000.0; t += frameMS * speed) {
            const vec2 cursor = judge.getAutoCursorPos();
            const Replay::Frame frame{.cur_music_pos = (i32)std::round(t),
                                      .milliseconds_since_last_frame = 0,
                                      .x = cursor.x,
                                      .y = cursor.y,
                                      .key_flags = 0};
            judge.step(frame, frameMS / 1000.0, 0);
            judge.animate(t);
            if(!benchSecs) hashState(frames, looks, judge, t, maxDurationMS);
            numFrames++;
        }
    }
    if(benchSecs) *benchSecs += Timing::getTimeReal() - start;
    if(benchFrames) *benchFrames += numFrames;

    for(uSz i = 0; i < score.hitresults.size(); i++) {
        fmt::format_to(std::back_inserter(out), "r {} {}\n", score.hitresults[i].timeMS,
                       (int)score.hitresults[i].result);
    }
    fmt::format_to(std::back_inserter(out), "deltas");
    for(int d : score.hitdeltas) fmt::format_to(std::back_inserter(out), " {}", d);
    fmt::format_to(std::back_inserter(out),
                   "\nscore {} combo {} max {} full {} acc {} ur {} 300 {} 300g {} 100 {} 100k {} 50 {} miss {} sb {} "
                   "hp {} failed {} keys {} {} {} {}\n",
                   score.getScore(), score.getCombo(), score.getComboMax(), score.getComboFull(), score.getAccuracy(),
                   score.getUnstableRate(), score.getNum300s(), score.getNum300gs(), score.getNum100s(),
                   score.getNum100ks(), score.getNum50s(), score.getNumMisses(), score.getNumSliderBreaks(),
                   judge.getHealth(), judge.hasFailed(), score.getKeyCount(Replay::K1), score.getKeyCount(Replay::K2),
                   score.getKeyCount(Replay::M1), score.getKeyCount(Replay::M2));
    fmt::format_to(std::back_inserter(out), "frames {} {:016x} looks {:016x} slides {:016x} spins {:016x}\n", numFrames,
                   frames.h, looks.h, rec.slides.h, rec.spins.h);
    return out;
}

Replay::Mods with(Replay::Mods mods, ModFlags add) {
    mods.flags |= add;
    return mods;
}

std::vector<Scenario> variants(const std::string &name, const Replay::Mods &base, bool autoplay) {
    std::vector<Scenario> out;
    const auto add = [&](std::string_view suffix, Replay::Mods mods) {
        out.push_back({fmt::format("{} {}", name, suffix), mods});
    };
    add("own", base);
    add("+HR", with(base, ModFlags::HardRock));
    add("+EZ", with(base, ModFlags::Easy));
    {
        Replay::Mods dt = base;
        dt.speed = 1.5f;
        add("dt", dt);
        Replay::Mods ht = base;
        ht.speed = 0.75f;
        add("ht", ht);
    }
    add("+FF", with(base, ModFlags::FreezeFrame));
    add("+HD", with(base, ModFlags::Hidden));
    if(autoplay) return out;

    for(int notelock = 0; notelock <= 3; notelock++) {
        Replay::Mods mods = base;
        mods.notelock_type = notelock;
        add(fmt::format("notelock{}", notelock), mods);
    }
    add("+RX", with(base, ModFlags::Relax));
    add("+AP", with(base, ModFlags::Autopilot));
    add("+SO", with(base, ModFlags::SpunOut));
    add("+V2", with(base, ModFlags::ScoreV2));
    add("+SD", with(base, ModFlags::SuddenDeath));
    add("+PF", with(base, ModFlags::Perfect));
    add("+NF", with(base, ModFlags::NoFail));
    add("+TP", with(base, ModFlags::Target));
    add("+ST", with(base, ModFlags::StrictTracking));
    add("+SC", with(base, ModFlags::StrictClicks | ModFlags::PreciseSliders));
    add("+HW", with(base, ModFlags::HalfWindow));
    add("+M3", with(base, ModFlags::Ming3012 | ModFlags::No50s));
    add("+NK", with(base, ModFlags::NoKeylock));
    add("+DKS", with(base, ModFlags::DKS));
    add("+MIN", with(base, ModFlags::Minimize));
    add("+ARTW", with(base, ModFlags::ARTimewarp));
    add("+ARW", with(base, ModFlags::ARWobble));
    add("+W1", with(base, ModFlags::Wobble1));
    add("+W2", with(base, ModFlags::Wobble2));
    add("+MAF", with(base, ModFlags::Mafham));
    add("+NOHP", with(base, ModFlags::NoHP));
    return out;
}

std::vector<std::string> filesUnder(const std::string &dir, std::string_view ext) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    for(auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
        !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if(it->path().extension() == ext && it->is_regular_file(ec)) out.push_back(it->path().string());
    }
    Mc::ranges::sort(out);
    return out;
}

std::unique_ptr<DatabaseBeatmap> openMap(const std::string &path) {
    auto map = std::make_unique<DatabaseBeatmap>(path, std::filesystem::path(path).parent_path().string() + "/",
                                                 DatabaseBeatmap::BeatmapType::NEOMOD_DIFFICULTY);
    map->loadMetadata(false);
    return map;
}

// the judged play as drawn: the default skin, the playfield fitted into the window, at its time and with its mods
class DrawView final : public PlayfieldView {
   public:
    DrawView(const Skin *skin, ModFlags mods) : skin(skin), mods(mods) {
        const vec2 screen = engine->getScreenSize();
        const vec2 playfield{GameRules::OSU_COORD_WIDTH, GameRules::OSU_COORD_HEIGHT};
        this->scale = std::min((screen.x - 80.f) / playfield.x, (screen.y - 80.f) / playfield.y);
        this->offset = (screen - playfield * this->scale) / 2.0f;
    }

    const Skin *skin;
    const GameplayJudge *judge{nullptr};
    ModFlags mods;
    vec2 offset{0.f};
    f32 scale{1.f};
    f64 timeMS{0.0};

    [[nodiscard]] const Skin *getSkin() const override { return this->skin; }
    [[nodiscard]] vec2 osuCoords2Pixels(vec2 coords) const override {
        if(flags::has<ModFlags::HardRock>(this->mods)) coords.y = GameRules::OSU_COORD_HEIGHT - coords.y;
        return coords * this->scale + this->offset;
    }
    [[nodiscard]] vec2 osuCoords2LegacyPixels(vec2 coords) const override {
        if(flags::has<ModFlags::HardRock>(this->mods)) coords.y = GameRules::OSU_COORD_HEIGHT - coords.y;
        return coords - vec2{GameRules::OSU_COORD_WIDTH / 2, GameRules::OSU_COORD_HEIGHT / 2};
    }
    [[nodiscard]] f32 osuAngle2PixelAngle(f32 degrees) const override {
        return flags::has<ModFlags::HardRock>(this->mods) ? 360.0f - degrees : degrees;
    }
    [[nodiscard]] f32 getPlayfieldScaleFactor() const override { return this->scale; }
    [[nodiscard]] vec2 getPlayfieldCenter() const override {
        return vec2{GameRules::OSU_COORD_WIDTH / 2.f, GameRules::OSU_COORD_HEIGHT / 2.f} * this->scale + this->offset;
    }
    [[nodiscard]] vec2 getPlayfieldSize() const override {
        return vec2{GameRules::OSU_COORD_WIDTH, GameRules::OSU_COORD_HEIGHT} * this->scale;
    }
    [[nodiscard]] vec2 getScreenSize() const override { return engine->getScreenSize(); }
    [[nodiscard]] f32 getHitcircleDiameter() const override { return this->judge->fRawHitcircleDiameter * this->scale; }
    [[nodiscard]] f32 getRawHitcircleDiameter() const override { return this->judge->fRawHitcircleDiameter; }
    [[nodiscard]] f32 getSliderFollowCircleDiameter() const override {
        return this->judge->fSliderFollowCircleDiameter * this->scale;
    }
    [[nodiscard]] f32 getNumberScale() const override {
        return numberScale(this->skin, this->getRawHitcircleDiameter(), this->getHitcircleDiameter());
    }
    [[nodiscard]] f32 getHitcircleOverlapScale() const override {
        return hitcircleOverlapScale(this->getRawHitcircleDiameter(), this->getHitcircleDiameter());
    }
    [[nodiscard]] f64 getCurMusicPos() const override { return this->timeMS; }
    [[nodiscard]] f64 getCurMusicPosWithOffsets() const override { return this->timeMS; }
    [[nodiscard]] f32 getApproachTime() const override { return this->judge->getApproachTime(); }
    [[nodiscard]] f32 getBaseAnimationSpeed() const override { return 1.f; }
    [[nodiscard]] f32 getSpeedAdjustedAnimationSpeed() const override { return this->judge->getSpeedMultiplier(); }
    [[nodiscard]] ModFlags getModFlags() const override { return this->mods; }
    [[nodiscard]] Color getComboColor(i32 colorCounter, i32 colorOffset) const override {
        return this->skin->getComboColorForCounter(colorCounter, colorOffset, this->judge->getComboColors());
    }
};

}  // namespace

struct JudgeTest::Drawn {
    std::unique_ptr<Skin> skin;
    std::unique_ptr<DatabaseBeatmap> map;
    LiveScore score{true};
    std::unique_ptr<GameplayJudge> judge;
    std::unique_ptr<DrawView> view;
    RenderTarget *sliderRT{nullptr};
    std::vector<u32> objectsToDraw;
    f64 timeMS{0.0};
    f64 endMS{0.0};
    i32 frames{0};  // since the start, which waits for the skin a fixed number of them
    bool started{false};

    ~Drawn() {
        if(this->sliderRT) resourceManager->destroyResource(this->sliderRT);
    }

    void step() {
        const f32 speed = this->judge->getSpeedMultiplier();
        this->timeMS += 1000.0 / 60.0 * speed;
        const vec2 cursor = this->judge->getAutoCursorPos();
        const Replay::Frame frame{.cur_music_pos = (i32)std::round(this->timeMS),
                                  .milliseconds_since_last_frame = 0,
                                  .x = cursor.x,
                                  .y = cursor.y,
                                  .key_flags = 0};
        this->judge->step(frame, 1.0 / 60.0, 0);
        this->judge->animate(this->timeMS);
        this->view->timeMS = this->timeMS;
    }

    // as BeatmapInterface::drawHitObjects() draws them without Mafham
    void draw() {
        const f64 curPos = this->timeMS;
        const i32 pvs = this->judge->getPVS();
        const HitObjects &objects = this->judge->getHitObjects();
        const auto sorted = this->judge->getHitObjectsSortedByEndTime();
        const bool freezeFrame = flags::has<ModFlags::FreezeFrame>(this->view->mods);

        HitObjectDrawing::drawFollowPoints(*this->view, this->judge->getHitObjects(),
                                           (uSz)std::max(0, this->judge->getPreviousFollowPointObjectIndex()));

        this->objectsToDraw.clear();
        i32 mostDistantEndTimeDrawn = 0;
        for(sSz i = (sSz)this->judge->getFirstFutureObjectByEndTime(curPos + pvs) - 1; i >= 0; i--) {
            const u32 obj = sorted[i];
            if((freezeFrame ? objects.comboStartTimes[obj] : objects.clickTimes[obj]) > curPos + pvs) continue;
            if(objects.finished[obj] && curPos - pvs > objects.endTimes[obj]) break;
            mostDistantEndTimeDrawn = std::max(mostDistantEndTimeDrawn, objects.endTimes[obj]);
            this->objectsToDraw.push_back(obj);
        }
        HitObjectDrawing::draw(objects, this->objectsToDraw, this->sliderRT);

        const f64 mostDistantFutureObjectPVS = std::max((f64)mostDistantEndTimeDrawn, curPos + pvs);
        for(const u32 obj : sorted.subspan(this->judge->getFirstLiveObjectByEndTime(curPos - pvs))) {
            if(objects.finished[obj] && curPos - pvs > objects.endTimes[obj]) continue;
            if(objects.clickTimes[obj] > mostDistantFutureObjectPVS) break;
            objects[obj].draw2();
        }
    }
};

JudgeTest::JudgeTest() { logRaw("JudgeTest created"); }

JudgeTest::~JudgeTest() = default;

void JudgeTest::draw() {
    if(!m_drawn || !m_drawn->started) return;
    g->setColor(0xff000000);
    g->fillRect(0, 0, engine->getScreenWidth(), engine->getScreenHeight());
    m_drawn->draw();
}

void JudgeTest::update() {
    if(m_drawn) {
        Drawn &d = *m_drawn;
        if(++d.frames < 60) return;
        if(!d.started) {
            if(!d.skin->isReady()) logRaw("JudgeTest: the skin took more than 60 frames to load");
            d.started = true;
            d.sliderRT = resourceManager->createRenderTarget(0, 0, engine->getScreenWidth(), engine->getScreenHeight());
            d.timeMS = d.judge->getHitObjects().clickTimes.front() - 2000.0;
            if(cv::debug_fixed_frametime.getFloat() <= 0.f) logRaw("JudgeTest: draw without debug_fixed_frametime");
        }
        if(d.timeMS < d.endMS) {
            d.step();
            d.skin->update(true, true, (i32)d.timeMS);
        }
        return;
    }

    if(m_ran) return;
    m_ran = true;

    if(const auto path = getTestArg("draw")) {
        auto d = std::make_unique<Drawn>();
        d->skin = std::make_unique<Skin>("default", Mc::Paths::materials() + "/default/");
        d->map = openMap(*path);
        ModFlags mods = ModFlags::Autoplay;
        const std::string modList = getTestArg("mods").value_or("");
        if(modList.find("HD") != std::string::npos) mods |= ModFlags::Hidden;
        if(modList.find("FF") != std::string::npos) mods |= ModFlags::FreezeFrame;
        if(modList.find("HR") != std::string::npos) mods |= ModFlags::HardRock;
        if(modList.find("EZ") != std::string::npos) mods |= ModFlags::Easy;
        if(modList.find("TC") != std::string::npos) mods |= ModFlags::Traceable;
        d->score.mods.flags = mods;
        if(modList.find("DT") != std::string::npos) d->score.mods.speed = 1.5f;
        if(modList.find("HT") != std::string::npos) d->score.mods.speed = 0.75f;
        d->view = std::make_unique<DrawView>(d->skin.get(), mods);
        d->judge = std::make_unique<GameplayJudge>(d->map.get(), d->score, d->view.get());
        d->view->judge = d->judge.get();
        if(const auto err = d->judge->load(); err || d->judge->getHitObjects().empty()) {
            logRaw("JudgeTest: can't draw {}", *path);
            engine->shutdown();
            return;
        }
        const HitObjects &objects = d->judge->getHitObjects();
        for(uSz i = 0; i < objects.size(); i++) objects[i].rebuildVertexBuffer();
        d->endMS = objects.endTimes[d->judge->getHitObjectsSortedByEndTime().back()] + 1000.0;
        m_drawn = std::move(d);
        return;
    }

    cv::hiterrorbar_misaims.setValue(true);

    const auto mapsDir = getTestArg("maps");
    if(!mapsDir) {
        logRaw("JudgeTest: no -testarg:maps");
        engine->shutdown();
        return;
    }
    const std::vector<std::string> maps = filesUnder(*mapsDir, ".osu");

    if(const auto bench = getTestArg("bench")) {
        const int rounds = std::max(1, Parsing::strto<i32>(*bench));
        for(const auto &path : maps) {
            auto map = openMap(path);
            Replay::Mods mods;
            mods.flags = ModFlags::Autoplay;
            f64 best = 1e9;
            u64 numFrames = 0;
            for(int r = 0; r < rounds; r++) {
                f64 secs = 0.0;
                numFrames = 0;
                judge(*map, {"bench", mods}, nullptr, &secs, &numFrames);
                best = std::min(best, secs);
            }
            logRaw("JudgeTest bench: {} objects, {} frames, {:.3f} ms total, {:.3f} us/frame ({})",
                   map->getNumObjects(), numFrames, best * 1000.0, best * 1e6 / (f64)std::max<u64>(numFrames, 1),
                   std::filesystem::path(path).filename().string());
        }
        engine->shutdown();
        return;
    }

    std::string out;
    const f64 start = Timing::getTimeReal();

    if(const auto replays = getTestArg("replays")) {
        std::unordered_map<std::string, std::string> byMD5;
        for(const auto &path : maps) {
            if(const auto md5 = crypto::hash::md5_file(path)) byMD5[std::string{md5->to_chars().string()}] = path;
        }
        for(const auto &osr : filesUnder(*replays, ".osr")) {
            FinishedScore score;
            if(!Replay::load_osr(osr, score)) continue;
            const auto it = byMD5.find(std::string{score.beatmap_hash.to_chars().string()});
            if(it == byMD5.end()) continue;
            auto map = openMap(it->second);
            const std::string name = std::filesystem::path(osr).filename().string();
            for(const auto &scenario : variants(name, score.mods, false)) {
                out += judge(*map, scenario, &score.replay);
            }
            logRaw("JudgeTest: judged {}", name);
        }
    }

    if(getTestArg("auto") == "1") {
        for(const auto &path : maps) {
            auto map = openMap(path);
            Replay::Mods mods;
            mods.flags = ModFlags::Autoplay;
            const std::string name = std::filesystem::path(path).filename().string();
            for(const auto &scenario : variants(name, mods, true)) {
                out += judge(*map, scenario, nullptr);
            }
            logRaw("JudgeTest: autoplayed {}", name);
        }
    }

    logRaw("JudgeTest: done in {:.1f} s, {} bytes", Timing::getTimeReal() - start, out.size());
    if(const auto outPath = getTestArg("out")) {
        File file(*outPath, File::MODE::WRITE);
        file.write(std::span<const u8>{reinterpret_cast<const u8 *>(out.data()), out.size()});
    }
    engine->shutdown();
}

}  // namespace Mc::Tests
