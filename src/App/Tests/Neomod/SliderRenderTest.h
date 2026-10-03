// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#ifndef SLIDERRENDERTEST_H
#define SLIDERRENDERTEST_H

#include "App.h"
#include "Vectors.h"
#include "Color.h"
#include "SliderRenderer.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

class RenderTarget;
class Image;

namespace Mc::Tests {

// standalone slider-body renderer test scene: synthesizes a battery of representative slider shapes
// and draws them through the real SliderRenderer path, to check the bodies visually (screenshot) and for
// performance (frame time under stress).
// scene: the srt_* convars, also set by the keys S snake, T copies, Z/1-7/0 solo-zoom, B batching, and
// G slider_use_gradient_image (the default skin's slidergradient.png); P runs the benchmark (-headless for uncapped
// frame times)
class SliderRenderTest : public App {
    NOCOPY_NOMOVE(SliderRenderTest)
   public:
    SliderRenderTest();
    ~SliderRenderTest() override;

    void draw() override;
    void update() override;

    void onKeyDown(KeyboardEvent &e) override;

    // fps limiter behavior
    [[nodiscard]] bool isInGameplay() const override { return true; }
    [[nodiscard]] bool isInUnpausedGameplay() const override { return true; }

   private:
    struct TestSlider {
        std::string name;
        SliderRenderer::Mesh mesh;
        std::vector<vec2> screenPoints;  // baked screen-space curve points (for the snake caps)
        Color color{0xffffffff};
    };

    // one drawn copy of a test slider
    struct TestBody final : SliderRenderer::BodySource {
        SliderRenderer::Body body;  // without its spans, they come from points and caps
        std::vector<vec2> points;   // srt_points
        std::vector<vec2> caps;     // the snaking end
        [[nodiscard]] std::optional<SliderRenderer::Body> getBody() const override;
    };

    // everything the frame time depends on; a change restarts the perf readout
    struct Scene {
        int copies;
        int solo;
        f32 diameter;
        f32 snake;
        bool points;
        bool batch;
        bool poison;
        bool gradient;
        bool operator==(const Scene &) const = default;
    };

    void rebuildBattery();
    [[nodiscard]] Scene currentScene() const;
    void benchFrame(f64 frameTime);

    RenderTarget *m_sliderRT{nullptr};
    Image *m_gradient{nullptr};
    std::vector<TestSlider> m_sliders;
    std::vector<TestBody> m_bodies;
    u32 m_totalVerts{0};

    // to rebuild scene when changed
    f32 m_lastSeparation{0.0f};
    vec2 m_lastScreen{0.0f};
    int m_lastSolo{-1};
    f32 m_lastDiameter{0.0f};

    f32 m_hitcircleDiameter{70.0f};
    int m_numShapes{0};  // set by rebuildBattery

    // headless perf readout: frame time stats to stdout (windowed frame time isn't useful on a vsync-blocked
    // swapchain, but -headless doesn't block, so the GPU cost shows up there)
    std::vector<f64> m_perfFrames;
    Scene m_perfScene{};

    // benchmark run (P): a fixed list of scenes, each measured for a fixed number of frames after a warmup
    int m_benchScene{-1};  // -1 = not running
    int m_benchFrame{0};
    std::vector<f64> m_benchFrames;
    std::vector<std::pair<int, f64>> m_benchResults;  // (sliders, median frame time) per finished scene
    Scene m_benchRestore{};
};

}  // namespace Mc::Tests

#endif
