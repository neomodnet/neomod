// Copyright (c) 2026, WH, All rights reserved.
#include "PlayfieldTest.h"

#include "TestMacros.h"
#include "BeatmapPrimitives.h"
#include "ConVar.h"
#include "Engine.h"
#include "File.h"
#include "Font.h"
#include "GameRules.h"
#include "Graphics.h"
#include "HitObjects.h"
#include "KeyBindings.h"
#include "Keyboard.h"
#include "KeyboardEvent.h"
#include "Logging.h"
#include "Parsing.h"
#include "Paths.h"
#include "RenderTarget.h"
#include "ResourceManager.h"
#include "Skin.h"
#include "SliderRenderer.h"
#include "Timing.h"

#include "fmt/format.h"

#include <algorithm>
#include <filesystem>
#include <ranges>

namespace Mc::Tests {
using namespace neomod;

namespace {
ConVar pft_time("pft_time", 0.0f, cv::CLIENT | cv::HIDDEN | cv::NOLOAD | cv::NOSAVE,
                "the time the map is shown at (ms)");

// the corpus run draws every map at this many times, from before its first object to after its last
constexpr int CORPUS_POSES = 8;
}  // namespace

PlayfieldTest::PlayfieldTest() {
    m_skin = std::make_unique<Skin>("default", Mc::Paths::materials() + "/default/");
    m_view.skin = m_skin.get();
    m_sliderRT = resourceManager->createRenderTarget(0, 0, engine->getScreenWidth(), engine->getScreenHeight());
    this->fitView();

    if(const auto corpus = getTestArg("corpus")) {
        namespace fs = std::filesystem;
        std::error_code ec;
        for(auto it = fs::recursive_directory_iterator(*corpus, fs::directory_options::skip_permission_denied, ec);
            !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if(it->path().extension() == ".osu" && it->is_regular_file(ec)) m_corpus.push_back(it->path().string());
        }
        std::ranges::sort(m_corpus);
        logRaw("PlayfieldTest: {} maps under {}", m_corpus.size(), *corpus);
        return;
    }

    if(const auto map = getTestArg("map")) {
        if(!this->load(*map)) logRaw("PlayfieldTest: can't draw {}", *map);
    }
    if(const auto time = getTestArg("time")) m_timeMS = Parsing::strto<i32>(*time);
}

PlayfieldTest::~PlayfieldTest() {
    // (the objects' meshes and the skin go with the members)
    if(m_sliderRT) resourceManager->destroyResource(m_sliderRT);
}

bool PlayfieldTest::load(const std::string &path) {
    m_shown.clear();
    m_byEndTime.clear();
    m_objects.clear();
    m_loaded = false;

    std::vector<u8> bytes;
    {
        File file(path);
        file.readToVector(bytes);
    }
    Primitives::PRIMITIVE_CONTAINER c = Primitives::loadPrimitiveObjectsFromData(bytes, Primitives::Limits{});
    if(c.error || c.getNumObjects() == 0 || c.timingpoints.empty()) return false;
    if(Primitives::calculateSliderTimesClicksTicks(c.version, c.sliders, c.timingpoints, c.sliderMultiplier,
                                                   c.sliderTickRate, c.limits))
        return false;

    m_view.rawHitcircleDiameter = GameRules::getRawHitCircleDiameter(c.CS);
    m_view.approachTimeMS = GameRules::mapDifficultyRange(
        c.AR, GameRules::getMinApproachTime(), GameRules::getMidApproachTime(), GameRules::getMaxApproachTime());
    m_view.comboColors = c.combocolors;

    m_objects = HitObjects::create(c, nullptr, &m_view);
    HitObjects::stack(m_objects, c.AR, c.version, c.stackLeniency, m_view.rawHitcircleDiameter, false);

    for(const auto &obj : m_objects) m_byEndTime.push_back({obj.get(), false});
    std::ranges::sort(m_byEndTime,
                      [](const Entry &a, const Entry &b) { return HitObject::sortByEndTimeComp(a.obj, b.obj); });

    m_firstTimeMS = m_objects.front()->getClickTime();
    m_lastTimeMS = m_byEndTime.back().obj->getEndTime();
    m_loaded = true;
    return true;
}

void PlayfieldTest::fitView() {
    const vec2 screen = engine->getScreenSize();
    const vec2 playfield{GameRules::OSU_COORD_WIDTH, GameRules::OSU_COORD_HEIGHT};
    constexpr f32 margin = 40.0f;
    m_view.scale =
        std::max(0.01f, std::min((screen.x - 2 * margin) / playfield.x, (screen.y - 2 * margin) / playfield.y));
    m_view.offset = (screen - playfield * m_view.scale) / 2.0f;
}

void PlayfieldTest::drawAt(i32 timeMS) {
    const i32 fadeOutMS = (i32)(GameRules::getFadeOutTime() * 1000.0f);
    m_view.musicPos = timeMS;
    m_skin->update(true, true, timeMS);

    // what can show at timeMS (approaching or fading out), posed
    const i32 lead = (i32)m_view.approachTimeMS + GameRules::getFadeInTime() + 1000;
    m_shown.clear();
    for(Entry &e : m_byEndTime) {
        if(e.obj->getClickTime() - lead > timeMS || e.obj->getEndTime() + 2000 < timeMS) continue;
        if(!e.meshBuilt) {
            e.obj->rebuildVertexBuffer();
            e.meshBuilt = true;
        }
        e.obj->pose(timeMS, fadeOutMS);
        m_shown.push_back(&e);
    }

    HitObjects::drawFollowPoints(m_view, m_objects, 0);

    // as gameplay draws them: spinners first, then the others latest-ending first (so earlier ones end up on top), their
    // second pass the other way round
    for(Entry *e : std::views::reverse(m_shown)) {
        if(e->obj->isSpinner()) e->obj->draw();
    }
    {
        SliderRenderer::Batch sliderBodies{m_sliderRT};
        for(Entry *e : std::views::reverse(m_shown)) {
            if(e->obj->isSlider()) sliderBodies.queue(*static_cast<const Slider *>(e->obj));
        }
        for(Entry *e : std::views::reverse(m_shown)) {
            if(!e->obj->isSpinner()) e->obj->draw();
        }
    }
    for(Entry *e : m_shown) e->obj->draw2();
}

void PlayfieldTest::drawCorpusFrame() {
    if(m_corpusNext >= m_corpus.size()) return;
    if(m_corpusNext == 0) m_corpusStart = Timing::getTimeReal();

    if(!this->load(m_corpus[m_corpusNext++])) {
        m_corpusSkipped++;
        return;
    }
    m_corpusDrawn++;
    m_corpusObjects += m_objects.size();

    const i64 from = (i64)m_firstTimeMS - 1000;
    const i64 to = (i64)m_lastTimeMS + 1000;
    for(int i = 0; i < CORPUS_POSES; i++) {
        this->drawAt((i32)std::clamp<i64>(from + (to - from) * i / (CORPUS_POSES - 1), INT32_MIN, INT32_MAX));
    }
    if(m_corpusNext % 1000 == 0) logRaw("  {}/{}", m_corpusNext, m_corpus.size());
}

void PlayfieldTest::draw() {
    g->setColor(0xff000000);
    g->fillRect(0, 0, engine->getScreenWidth(), engine->getScreenHeight());
    if(!m_skin->isReady()) return;

    if(!m_corpus.empty()) {
        this->drawCorpusFrame();
        return;
    }
    if(!m_loaded) return;

    this->drawAt((i32)m_timeMS);

    McFont *font = engine->getDefaultFont();
    g->setColor(0xffffffff);
    g->pushTransform();
    {
        g->translate(12, font->getHeight() + 10);
        g->drawString(font, fmt::format("{} ms{}", (i32)m_timeMS, m_playing ? "" : "  (paused)"));
    }
    g->popTransform();
}

void PlayfieldTest::update() {
    if(!m_corpus.empty()) {
        if(m_corpusNext == m_corpus.size()) {
            m_corpusNext++;  // (once)
            logRaw("PlayfieldTest: drew {} maps ({} objects) at {} times each in {:.1f} s, {} skipped", m_corpusDrawn,
                   m_corpusObjects, CORPUS_POSES, Timing::getTimeReal() - m_corpusStart, m_corpusSkipped);
            engine->shutdown();
        }
        return;
    }

    if(pft_time.getFloat() != m_timeConVar) {
        m_timeConVar = pft_time.getFloat();
        m_timeMS = m_timeConVar;
    }
    if(m_playing) m_timeMS += engine->getFrameTime() * 1000.0;
}

void PlayfieldTest::onKeyDown(KeyboardEvent &e) {
    const SCANCODE sc = e.getScanCode();
    const f64 step = keyboard->isShiftDown() ? 10.0 : 100.0;
    if(sc == KEY_LEFT) {
        m_timeMS -= step;
    } else if(sc == KEY_RIGHT) {
        m_timeMS += step;
    } else if(sc == KEY_DOWN) {
        m_timeMS -= 1000.0;
    } else if(sc == KEY_UP) {
        m_timeMS += 1000.0;
    } else if(sc == KEY_SPACE) {
        m_playing = !m_playing;
    } else {
        return;
    }
    e.consume();
}

void PlayfieldTest::onResolutionChanged(vec2 newResolution) {
    this->fitView();
    m_sliderRT->rebuild((int)newResolution.x, (int)newResolution.y);
}

}  // namespace Mc::Tests
