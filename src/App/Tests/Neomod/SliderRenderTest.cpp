// Copyright (c) 2026, WH, All rights reserved.
#include "SliderRenderTest.h"

#include "Engine.h"
#include "Environment.h"
#include "ResourceManager.h"
#include "Graphics.h"
#include "Font.h"
#include "VertexArrayObject.h"
#include "RenderTarget.h"
#include "Image.h"
#include "ConVar.h"
#include "OsuConVars.h"
#include "KeyboardEvent.h"
#include "KeyBindings.h"

#include "SliderRenderer.h"
#include "SliderCurves.h"
#include "Logging.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace Mc::Tests {
using namespace neomod;

namespace {
// keeps the scene convars out of configs and console suggestions
constexpr u8 TESTONLY = cv::HIDDEN | cv::NOLOAD | cv::NOSAVE;

ConVar srt_copies("srt_copies", 1, cv::CLIENT | TESTONLY,
                  "copies of every shape (a few px apart, so they overlap), each one drawn as its own slider",
                  cv::Range{1., 1000.});
ConVar srt_solo("srt_solo", -1, cv::CLIENT | TESTONLY,
                "-1 = grid of all shapes, otherwise only that shape, zoomed to fill the screen");
ConVar srt_diameter("srt_diameter", 70.0f, cv::CLIENT | TESTONLY,
                    "body diameter in px (solo-zoom scales it up together with the path)", cv::Range{4., 1024.});
ConVar srt_snake("srt_snake", 1.0f, cv::CLIENT | TESTONLY,
                 "the drawn part of every body (a disc rounds off its snaking end), -1 animates it",
                 cv::Range{-1., 1.});
ConVar srt_points("srt_points", false, cv::CLIENT | TESTONLY,
                  "draw the bodies from their screen-space points, a disc each, like sliders that change shape every "
                  "frame (wobble, minimize)");
ConVar srt_batch("srt_batch", true, cv::CLIENT | TESTONLY,
                 "queue every body before drawing, so their fields render together (off: a field pass per body)");
ConVar srt_poison("srt_poison", false, cv::CLIENT | TESTONLY,
                  "fill the render target with a full field in every channel first: whatever a composite reads "
                  "without its field pass having cleared it shows up as body");

struct BenchScene {
    const char *name;
    int solo;
    int copies;
    f32 diameter;
};
// x8 -> x24 gives the cost of one more small slider, d200 is about CS4 at 1440p, solo wobble is field overdraw
constexpr std::array<BenchScene, 6> BENCH{{
    {"grid x1", -1, 1, 70.0f},
    {"grid x8", -1, 8, 70.0f},
    {"grid x24", -1, 24, 70.0f},
    {"grid x8 d200", -1, 8, 200.0f},
    {"solo straight x8", 0, 8, 70.0f},
    {"solo wobble x8", 6, 8, 70.0f},
}};
constexpr int BENCH_WARMUP_FRAMES = 30;
constexpr int BENCH_FRAMES = 200;

f64 median(std::vector<f64> v) {
    if(v.empty()) return 0.0;
    std::ranges::nth_element(v, v.begin() + (std::ptrdiff_t)(v.size() / 2));
    return v[v.size() / 2];
}

f64 mean(const std::vector<f64> &v) { return v.empty() ? 0.0 : std::reduce(v.begin(), v.end()) / (f64)v.size(); }

}  // namespace

std::optional<SliderRenderer::Body> SliderRenderTest::TestBody::getBody() const {
    SliderRenderer::Body b = body;
    b.points = points;
    b.alwaysPoints = caps;
    return b;
}

SliderRenderTest::SliderRenderTest() {
    m_gradient = resourceManager->loadImage("default/slidergradient.png", "SRT_SLIDERGRADIENT");
    rebuildBattery();
}

SliderRenderTest::~SliderRenderTest() {
    if(m_sliderRT) resourceManager->destroyResource(m_sliderRT);
    if(m_gradient) resourceManager->destroyResource(m_gradient);
}

SliderRenderTest::Scene SliderRenderTest::currentScene() const {
    return {.copies = srt_copies.getInt(),
            .solo = srt_solo.getInt(),
            .diameter = srt_diameter.getFloat(),
            .snake = srt_snake.getFloat(),
            .points = srt_points.getBool(),
            .batch = srt_batch.getBool(),
            .poison = srt_poison.getBool(),
            .gradient = cv::slider_use_gradient_image.getBool()};
}

void SliderRenderTest::rebuildBattery() {
    const int W = engine->getScreenWidth();
    const int H = engine->getScreenHeight();
    if(m_sliderRT == nullptr) {
        m_sliderRT = resourceManager->createRenderTarget(0, 0, W, H);
    } else if((int)m_sliderRT->getWidth() != W || (int)m_sliderRT->getHeight() != H) {
        m_sliderRT->rebuild(W, H);
    }

    m_sliders.clear();
    m_totalVerts = 0;

    struct ShapeDef {
        const char *name;
        SLIDERCURVETYPE type;
        std::vector<vec2> ctrl;  // control points authored in a ~200x150 local osu!pixel box
    };
    std::vector<ShapeDef> shapes = {
        {"straight", SLIDERCURVETYPE::LINEAR, {{0, 75}, {200, 75}}},
        {"gentle", SLIDERCURVETYPE::BEZIER, {{0, 130}, {100, 10}, {200, 130}}},
        {"s-wave", SLIDERCURVETYPE::BEZIER, {{0, 75}, {55, -10}, {145, 160}, {200, 75}}},
        {"tight-hook", SLIDERCURVETYPE::BEZIER, {{15, 140}, {190, 140}, {190, 10}}},
        {"cusp", SLIDERCURVETYPE::BEZIER, {{5, 15}, {100, 150}, {100, 150}, {195, 15}}},  // repeated anchor = cusp
        {"loop", SLIDERCURVETYPE::BEZIER, {{20, 80}, {210, 25}, {210, 130}, {35, 35}, {70, 140}}},  // self-overlapping
    };
    // aspire "wobble" slider: dozens of oscillating, duplicated control points crammed into a tiny box, so the
    // tessellated path retraces itself many times with a cusp at every reversal.
    // stresses zero-length segments + unstable convex sides + T-junctions
    // (see sliders on https://osu.ppy.sh/beatmapsets/1799342#osu/3688621)
    {
        ShapeDef wobble{"wobble", SLIDERCURVETYPE::BEZIER, {}};
        for(int k = 0; k < 60; ++k) {
            const f32 base = (f32)k * 3.0f;                                         // advance rightward => a long tube
            wobble.ctrl.emplace_back(base + (k % 2 ? 5.0f : -5.0f), (f32)(k % 4));  // oscillate around the center
            wobble.ctrl.emplace_back(base, 1.5f);  // advancing center anchor (duplicated => cusp on each reversal)
            wobble.ctrl.emplace_back(base, 1.5f);
        }
        shapes.push_back(std::move(wobble));
    }

    m_numShapes = (int)shapes.size();

    // grid uses a gameplay-ish body; solo true-zooms one shape (body + path scaled together, preserving the
    // real body/path-extent ratio) so it nearly fills the screen and any seam cracks become visible.
    const int soloShape = srt_solo.getInt();
    const bool solo = soloShape >= 0 && soloShape < m_numShapes;
    const f32 diameter = srt_diameter.getFloat();
    m_hitcircleDiameter = diameter;

    const int cols = solo ? 1 : 4, rows = solo ? 1 : 2;
    const f32 cellW = (f32)W / (f32)cols, cellH = (f32)H / (f32)rows;
    constexpr std::array<Color, 7> palette = {0xff5599ff, 0xffff7755, 0xff66dd88, 0xffffcc44,
                                              0xffcc66ff, 0xff44ddee, 0xffff55aa};

    for(uSz i = 0; i < shapes.size(); ++i) {
        if(solo && (int)i != soloShape) continue;
        const ShapeDef &shape = shapes[i];

        // approximate the slider length from the control polyline (good enough for a synthetic test)
        f32 pixelLength = 0.f;
        for(uSz k = 1; k < shape.ctrl.size(); ++k) pixelLength += vec::length(shape.ctrl[k] - shape.ctrl[k - 1]);
        if(pixelLength < 1.0f) continue;

        SliderCurve curve{shape.type, shape.ctrl, pixelLength, cv::slider_curve_points_separation.getFloat()};
        const std::span<const vec2> local = curve.getPoints();
        if(local.size() < 2) continue;

        // fit the curve's local bounds into its grid cell (uniform scale, centered)
        vec2 lmin = local[0], lmax = local[0];
        for(const vec2 &p : local) {
            lmin = vec::min(lmin, p);
            lmax = vec::max(lmax, p);
        }
        const vec2 lsize = vec::max(lmax - lmin, vec2{1.0f, 1.0f});
        const vec2 lcenter = (lmin + lmax) * 0.5f;

        f32 scale = 1.0f;
        if(solo) {
            const f32 z = std::min(((f32)W - 80.0f) / (lsize.x + diameter), ((f32)H - 80.0f) / (lsize.y + diameter));
            scale = std::clamp(z, 1.0f, 400.0f);
            m_hitcircleDiameter = diameter * scale;  // true zoom: body scales with the path
        } else {
            const f32 pad = m_hitcircleDiameter + 40.0f;  // leave room for the body radius + a margin
            scale = std::clamp(std::min((cellW - pad) / lsize.x, (cellH - pad) / lsize.y), 0.25f, 4.0f);
        }

        const int col = (int)(i % cols), row = (int)(i / cols);
        const vec2 cellCenter =
            solo ? vec2{(f32)W * 0.5f, (f32)H * 0.5f} : vec2{((f32)col + 0.5f) * cellW, ((f32)row + 0.5f) * cellH};

        TestSlider s;
        s.name = shape.name;
        s.color = palette[i % palette.size()];
        s.screenPoints.reserve(local.size());
        for(const vec2 &p : local) s.screenPoints.push_back(cellCenter + (p - lcenter) * scale);

        s.mesh = SliderRenderer::generateMesh(engine->getScreenSize(), s.screenPoints, m_hitcircleDiameter,
                                              /*skipOOBPoints=*/false);
        m_totalVerts += s.mesh.vao->getNumVertices();
        m_sliders.push_back(std::move(s));
    }

    m_lastSeparation = cv::slider_curve_points_separation.getFloat();
    m_lastScreen = engine->getScreenSize();
    m_lastSolo = soloShape;
    m_lastDiameter = diameter;
}

void SliderRenderTest::draw() {
    g->setColor(0xff202028);
    g->fillRect(0, 0, engine->getScreenWidth(), engine->getScreenHeight());

    const Scene scene = currentScene();
    const f32 from = 0.0f;
    const f32 to =
        scene.snake < 0.0f ? std::clamp((f32)std::fmod(engine->getTime(), 3.0) / 1.5f, 0.0f, 1.0f) : scene.snake;

    if(scene.poison) {
        m_sliderRT->enable(/*clear=*/false);
        g->setBlending(false);
        g->setColor(0xffffffff);
        g->fillRect(0, 0, (int)m_sliderRT->getWidth(), (int)m_sliderRT->getHeight());
        g->setBlending(true);
        m_sliderRT->disable();
    }

    SliderRenderer::SkinSettings skinSettings;
    skinSettings.i_slider_gradient = m_gradient;

    // every copy of every shape is a slider of its own
    m_bodies.clear();
    for(const TestSlider &s : m_sliders) {
        if(s.screenPoints.size() < 2) continue;
        // the static curve ends are rounded by caps baked into the body mesh; only the moving snake/shrink head
        // needs a separate disc cap here (mirrors how Slider::makeBody relies on the smoothsnake alwaysPoint)
        const f32 lastIdx = (f32)(s.screenPoints.size() - 1);
        for(int k = 0; k < scene.copies; ++k) {
            const int gx = (k % 4) - 2, gy = (k / 4) - 1;  // small grid of overlapping copies
            const vec2 offset = scene.copies > 1 ? vec2{(f32)gx * 9.0f, (f32)gy * 9.0f} : vec2{0.0f};
            const auto capAt = [&](f32 t) -> vec2 {
                return s.screenPoints[(uSz)std::clamp(std::round(t * lastIdx), 0.0f, lastIdx)] + offset;
            };

            TestBody &body = m_bodies.emplace_back();
            if(from > 0.0f) body.caps.push_back(capAt(from));
            if(to < 1.0f) body.caps.push_back(capAt(to));
            if(scene.points)
                for(const vec2 &p : s.screenPoints) body.points.push_back(p + offset);
            body.body = {.mesh = scene.points ? nullptr : &s.mesh,
                         .translation = scene.points ? vec2{0.0f} : offset,
                         .hitcircleDiameter = m_hitcircleDiameter,
                         .from = from,
                         .to = to,
                         .skinSettings = skinSettings,
                         .undimmedColor = s.color};
        }
    }
    {
        SliderRenderer::Batch batch{m_sliderRT};
        if(scene.batch)
            for(const TestBody &body : m_bodies) batch.queue(body);
        for(const TestBody &body : m_bodies) SliderRenderer::draw(body);
    }
    const uSz drawn = m_bodies.size();

    // HUD
    McFont *font = engine->getDefaultFont();
    g->setColor(0xffffffff);
    g->pushTransform();
    {
        g->translate(12, font->getHeight() + 10);
        g->drawString(
            font, fmt::format("sep={:.2f}  d={:.0f}  copies={}  body verts={}  draws={}  frame={:.2f}ms{}{}{}{}{}{}",
                              cv::slider_curve_points_separation.getFloat(), m_hitcircleDiameter, scene.copies,
                              m_totalVerts, drawn, engine->getFrameTime() * 1000.0,
                              scene.solo >= 0 && !m_sliders.empty() ? fmt::format("  SOLO:{}", m_sliders[0].name) : "",
                              scene.points ? "  POINTS" : "", scene.batch ? "" : "  UNBATCHED",
                              scene.poison ? "  POISONED RT" : "", scene.gradient ? "  GRADIENT" : "",
                              m_benchScene >= 0 ? fmt::format("  BENCH {}/{}", m_benchScene + 1, BENCH.size()) : ""));
    }
    g->popTransform();
    g->pushTransform();
    {
        g->translate(12, (f32)engine->getScreenHeight() - font->getHeight());
        g->drawString(font,
                      "[S] snake   [T] copies   [Z] solo-zoom   [B] batching   [G] gradient   [P] benchmark   "
                      "cvars: srt_copies, srt_solo, srt_diameter, slider_curve_points_separation");
    }
    g->popTransform();

    // headless perf readout: getFrameTime() reflects real CPU+GPU work when the swapchain isn't blocking
    if(m_benchScene >= 0) {
        benchFrame(engine->getFrameTime());
        return;
    }
    if(scene != m_perfScene) {
        m_perfFrames.clear();
        m_perfScene = scene;
    }
    m_perfFrames.push_back(engine->getFrameTime());
    if(m_perfFrames.size() >= 100) {
        logRaw(
            "[perf] sep={:.2f} d={:<4.0f} copies={:<3} solo={:<2} draws={:<4} batch={}{}{}{} median={:.3f} ms "
            "mean={:.3f} ms ({} frames)",
            cv::slider_curve_points_separation.getFloat(), m_hitcircleDiameter, scene.copies, scene.solo, drawn,
            scene.batch ? "on" : "off", scene.points ? " points" : "", scene.poison ? " poisoned" : "",
            scene.gradient ? " gradient" : "", median(m_perfFrames) * 1000.0, mean(m_perfFrames) * 1000.0,
            m_perfFrames.size());
        m_perfFrames.clear();
    }
}

void SliderRenderTest::benchFrame(f64 frameTime) {
    // the first frames of a scene include its rebuild and the render pipeline refilling
    if(++m_benchFrame > BENCH_WARMUP_FRAMES) m_benchFrames.push_back(frameTime);
    if((int)m_benchFrames.size() < BENCH_FRAMES) return;

    const auto &[sliders, med] =
        m_benchResults.emplace_back((int)m_sliders.size() * srt_copies.getInt(), median(m_benchFrames));
    logRaw("[bench] {:<18} sliders={:<4} median={:.3f} ms mean={:.3f} ms", BENCH[m_benchScene].name, sliders,
           med * 1000.0, mean(m_benchFrames) * 1000.0);
    m_benchFrame = 0;
    m_benchFrames.clear();

    if(++m_benchScene < (int)BENCH.size()) {
        srt_solo.setValue(BENCH[m_benchScene].solo);
        srt_copies.setValue(BENCH[m_benchScene].copies);
        srt_diameter.setValue(BENCH[m_benchScene].diameter);
        return;
    }

    // grid x8 -> x24
    const auto &[n8, t8] = m_benchResults[1];
    const auto &[n24, t24] = m_benchResults[2];
    logRaw("[bench] one more 70px slider: {:.1f} us", (t24 - t8) / (f64)(n24 - n8) * 1e6);
    m_benchScene = -1;
    srt_copies.setValue(m_benchRestore.copies);
    srt_solo.setValue(m_benchRestore.solo);
    srt_diameter.setValue(m_benchRestore.diameter);
    srt_snake.setValue(m_benchRestore.snake);
    srt_points.setValue(m_benchRestore.points);
    srt_poison.setValue(m_benchRestore.poison);
    if(env->isHeadless()) engine->shutdown();
}

void SliderRenderTest::update() {
    if(m_sliders.empty() || cv::slider_curve_points_separation.getFloat() != m_lastSeparation ||
       srt_solo.getInt() != m_lastSolo || srt_diameter.getFloat() != m_lastDiameter ||
       engine->getScreenSize().x != m_lastScreen.x || engine->getScreenSize().y != m_lastScreen.y) {
        rebuildBattery();
    }
}

void SliderRenderTest::onKeyDown(KeyboardEvent &e) {
    const SCANCODE sc = e.getScanCode();
    if(sc == KEY_S) {
        srt_snake.setValue(srt_snake.getFloat() < 0.0f ? 1.0f : -1.0f);
        e.consume();
    } else if(sc == KEY_T) {  // cycle overdraw stress: 1 -> 8 -> 24 overlapping copies per shape
        const int copies = srt_copies.getInt();
        srt_copies.setValue(copies == 1 ? 8 : (copies == 8 ? 24 : 1));
        e.consume();
    } else if(sc == KEY_B) {
        srt_batch.setValue(!srt_batch.getBool());
        e.consume();
    } else if(sc == KEY_G) {
        cv::slider_use_gradient_image.setValue(!cv::slider_use_gradient_image.getBool());
        e.consume();
    } else if(sc == KEY_Z) {  // cycle solo-zoom: grid -> shape0 -> ... -> shapeN -> grid
        const int solo = srt_solo.getInt();
        srt_solo.setValue(solo + 1 >= m_numShapes ? -1 : solo + 1);
        e.consume();
    } else if(sc == KEY_P) {  // benchmark: prints [bench] lines, quits afterwards when headless
        if(m_benchScene < 0) {
            logRaw("[bench] {}x{}, sep={:.2f}", engine->getScreenWidth(), engine->getScreenHeight(),
                   cv::slider_curve_points_separation.getFloat());
            m_benchRestore = currentScene();
            m_benchResults.clear();
            m_benchScene = 0;
            srt_solo.setValue(BENCH[0].solo);
            srt_copies.setValue(BENCH[0].copies);
            srt_diameter.setValue(BENCH[0].diameter);
            srt_snake.setValue(1.0f);
            srt_points.setValue(false);
            srt_poison.setValue(false);
        }
        e.consume();
    } else if(sc >= KEY_1 && sc <= KEY_0) {
        static_assert((int)KEY_1 + 9 == (int)KEY_0);
        const int shape = (int)sc - (int)KEY_1;
        if(shape == 9 /*KEY_0*/) {
            srt_solo.setValue(-1);
        } else if(shape >= 0 && shape < m_numShapes) {
            srt_solo.setValue(shape);
        }
        e.consume();
    }
}

}  // namespace Mc::Tests
