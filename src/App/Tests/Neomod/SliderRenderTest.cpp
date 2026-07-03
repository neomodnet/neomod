// Copyright (c) 2026, WH, All rights reserved.
#include "SliderRenderTest.h"

#include "Engine.h"
#include "ResourceManager.h"
#include "Graphics.h"
#include "Font.h"
#include "VertexArrayObject.h"
#include "RenderTarget.h"
#include "OsuConVars.h"
#include "KeyboardEvent.h"
#include "KeyBindings.h"

#include "SliderRenderer.h"
#include "SliderCurves.h"
#include "Logging.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Mc::Tests {
using namespace neomod;

SliderRenderTest::SliderRenderTest() { rebuildBattery(); }

SliderRenderTest::~SliderRenderTest() = default;

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

    // grid uses a gameplay-ish 70px body; solo true-zooms one shape (body + path scaled together, preserving the
    // real body/path-extent ratio) so it nearly fills the screen and any seam cracks become visible.
    const bool solo = m_solo >= 0 && m_solo < m_numShapes;
    m_hitcircleDiameter = 70.0f;

    const int cols = solo ? 1 : 4, rows = solo ? 1 : 2;
    const f32 cellW = (f32)W / (f32)cols, cellH = (f32)H / (f32)rows;
    constexpr std::array<Color, 7> palette = {0xff5599ff, 0xffff7755, 0xff66dd88, 0xffffcc44,
                                              0xffcc66ff, 0xff44ddee, 0xffff55aa};

    for(uSz i = 0; i < shapes.size(); ++i) {
        if(solo && (int)i != m_solo) continue;
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
            const f32 z = std::min(((f32)W - 80.0f) / (lsize.x + 70.0f), ((f32)H - 80.0f) / (lsize.y + 70.0f));
            scale = std::clamp(z, 1.0f, 400.0f);
            m_hitcircleDiameter = 70.0f * scale;  // true zoom: body scales with the path
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
        vec2 smin{(f32)W, (f32)H}, smax{0.0f, 0.0f};
        for(const vec2 &p : local) {
            const vec2 sp = cellCenter + (p - lcenter) * scale;
            s.screenPoints.push_back(sp);
            smin = vec::min(smin, sp);
            smax = vec::max(smax, sp);
        }
        s.bounds = vec4{smin.x, smin.y, smax.x, smax.y};

        s.vao = SliderRenderer::generateVAO(engine->getScreenSize(), s.screenPoints, m_hitcircleDiameter, vec3{0.0f},
                                            /*skipOOBPoints=*/false);
        if(s.vao) m_totalVerts += s.vao->getNumVertices();
        m_sliders.push_back(std::move(s));
    }

    // read the effective mode last: the generateVAO calls above create the shaders on the very first run
    m_lastSDF = SliderRenderer::usingSDF();
    m_lastSeparation = cv::slider_curve_points_separation.getFloat();
    m_lastScreen = engine->getScreenSize();
}

void SliderRenderTest::drawSlider(const TestSlider &s, f32 from, f32 to, vec2 offset) {
    if(s.vao == nullptr || s.screenPoints.size() < 2) return;

    // the static curve ends are rounded by caps baked into the body mesh; only the moving snake/shrink head
    // needs a separate disc cap here (mirrors how Slider::drawBody relies on the smoothsnake alwaysPoint)
    const f32 lastIdx = (f32)(s.screenPoints.size() - 1);
    const auto capAt = [&](f32 t) -> vec2 {
        return s.screenPoints[(uSz)std::clamp(std::round(t * lastIdx), 0.0f, lastIdx)] + offset;
    };
    std::vector<vec2> caps;
    if(from > 0.0f) caps.push_back(capAt(from));
    if(to < 1.0f) caps.push_back(capAt(to));

    SliderRenderer::draw(SliderRenderer::DrawVAOParams{
        .screenRect = engine->getScreenSize(),
        .rt = m_sliderRT,
        .skinSettings = {},
        .vao = s.vao.get(),
        .bounds = vec4{s.bounds.x + offset.x, s.bounds.y + offset.y, s.bounds.z + offset.x, s.bounds.w + offset.y},
        .alwaysPoints = caps,
        .translation = offset,
        .scale = 1.0f,
        .hitcircleDiameter = m_hitcircleDiameter,
        .from = from,
        .to = to,
        .undimmedColor = s.color,
        .colorRGBMultiplier = 1.0f,
        .alpha = 1.0f,
    });
}

void SliderRenderTest::draw() {
    g->setColor(0xff202028);
    g->fillRect(0, 0, engine->getScreenWidth(), engine->getScreenHeight());

    f32 from = 0.0f, to = 1.0f;
    if(m_animateSnake) to = std::clamp((f32)std::fmod(engine->getTime(), 3.0) / 1.5f, 0.0f, 1.0f);

    int drawn = 0;
    for(const TestSlider &s : m_sliders) {
        if(m_stressCount > 0) {
            for(int k = 0; k < m_stressCount; ++k) {
                const int gx = (k % 4) - 2, gy = (k / 4) - 1;  // small grid of overlapping copies
                drawSlider(s, from, to, vec2{(f32)gx * 9.0f, (f32)gy * 9.0f});
                ++drawn;
            }
        } else {
            drawSlider(s, from, to, vec2{0.0f});
            ++drawn;
        }
    }

    // HUD
    McFont *font = engine->getDefaultFont();
    g->setColor(0xffffffff);
    g->pushTransform();
    {
        g->translate(12, font->getHeight() + 10);
        g->drawString(
            font, fmt::format("{}  sep={:.2f}  body verts={}  draws={}  frame={:.2f}ms{}{}",
                              SliderRenderer::usingSDF() ? "SDF body" : "CONE body",
                              cv::slider_curve_points_separation.getFloat(), m_totalVerts, drawn,
                              engine->getFrameTime() * 1000.0,
                              m_stressCount > 0 ? fmt::format("  STRESS x{}", m_stressCount) : "",
                              m_solo >= 0 && !m_sliders.empty() ? fmt::format("  SOLO:{}", m_sliders[0].name) : ""));
    }
    g->popTransform();
    g->pushTransform();
    {
        g->translate(12, (f32)engine->getScreenHeight() - font->getHeight());
        g->drawString(font,
                      "[S] snake   [T] stress   [C] SDF/cone   [Z] solo-zoom   cvars: slider_body_sdf, "
                      "slider_curve_points_separation");
    }
    g->popTransform();

    // headless perf readout: getFrameTime() reflects real CPU+GPU work when the swapchain isn't blocking
    const bool sdf = SliderRenderer::usingSDF();
    if(sdf != m_perfLastSDF || m_stressCount != m_perfLastStress) {
        m_perfAccum = 0.0;
        m_perfFrames = 0;
        m_perfLastSDF = sdf;
        m_perfLastStress = m_stressCount;
    }
    m_perfAccum += engine->getFrameTime();
    if(++m_perfFrames >= 100) {
        logRaw("[perf] {:<4} sep={:.2f} stress=x{:<2} draws={:<4} avg={:.3f} ms ({} frames)", sdf ? "SDF" : "CONE",
               cv::slider_curve_points_separation.getFloat(), m_stressCount, drawn,
               (m_perfAccum / (f64)m_perfFrames) * 1000.0, m_perfFrames);
        m_perfAccum = 0.0;
        m_perfFrames = 0;
    }
}

void SliderRenderTest::update() {
    if(m_sliders.empty() || SliderRenderer::usingSDF() != m_lastSDF ||
       cv::slider_curve_points_separation.getFloat() != m_lastSeparation ||
       engine->getScreenSize().x != m_lastScreen.x || engine->getScreenSize().y != m_lastScreen.y) {
        rebuildBattery();
    }
}

void SliderRenderTest::onKeyDown(KeyboardEvent &e) {
    const SCANCODE sc = e.getScanCode();
    if(sc == KEY_S) {
        m_animateSnake = !m_animateSnake;
        e.consume();
    } else if(sc == KEY_T) {  // cycle overdraw stress: off -> 8 -> 24 overlapping copies per shape
        m_stressCount = m_stressCount == 0 ? 8 : (m_stressCount == 8 ? 24 : 0);
        e.consume();
    } else if(sc == KEY_C) {
        cv::slider_body_sdf.setValue(!cv::slider_body_sdf.getBool());
        e.consume();
    } else if(sc == KEY_Z) {  // cycle solo-zoom: grid -> shape0 -> ... -> shapeN -> grid
        m_solo = m_solo + 1 >= m_numShapes ? -1 : m_solo + 1;
        rebuildBattery();
        e.consume();
    } else if(sc >= KEY_1 && sc <= KEY_0) {
        static_assert((int)KEY_1 + 9 == (int)KEY_0);
        const int shape = (int)sc - (int)KEY_1;
        if(shape == 9 /*KEY_0*/) {
            m_solo = -1;
            rebuildBattery();
        } else if(shape >= 0 && shape < m_numShapes) {
            m_solo = shape;
            rebuildBattery();
        }
        e.consume();
    }
}

}  // namespace Mc::Tests
