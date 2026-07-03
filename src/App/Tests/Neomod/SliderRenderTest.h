// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#ifndef SLIDERRENDERTEST_H
#define SLIDERRENDERTEST_H

#include "App.h"
#include "Vectors.h"
#include "Color.h"

#include <memory>
#include <string>
#include <vector>

class VertexArrayObject;
class RenderTarget;

namespace Mc::Tests {

// standalone slider-body renderer test scene: synthesizes a battery of representative slider shapes
// and draws them through the real SliderRenderer path, so the analytic SDF vs cone-disc body mesh can be
// compared visually (screenshot) and for performance (frame time under stress) by toggling the
// slider_body_sdf / slider_curve_points_separation convars.
// keys: S snake, T cycle stress-tile, C toggle SDF/cone, Z cycle solo-zoom
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
        std::unique_ptr<VertexArrayObject> vao;
        std::vector<vec2> screenPoints;  // baked screen-space curve points (for caps + bounds)
        vec4 bounds{};                   // screen-space AABB (minX, minY, maxX, maxY)
        Color color{0xffffffff};
    };

    void rebuildBattery();
    void drawSlider(const TestSlider &s, f32 from, f32 to, vec2 offset);

    RenderTarget *m_sliderRT{nullptr};
    std::vector<TestSlider> m_sliders;
    u32 m_totalVerts{0};

    // to rebuild scene when changed
    bool m_lastSDF{false};
    f32 m_lastSeparation{0.0f};
    vec2 m_lastScreen{0.0f};

    bool m_animateSnake{false};
    int m_stressCount{0};  // 0 = off; otherwise N overlapping copies per shape (fragment-bound overdraw stress)
    int m_solo{-1};        // -1 = grid of all shapes; >=0 = render only that shape, zoomed to fill the screen
    f32 m_hitcircleDiameter{70.0f};
    int m_numShapes{0};  // set by rebuildBattery

    // headless perf readout: averages real frame time to stdout (windowed frame time isn't useful on a vsync-
    // blocked swapchain, but -headless doesn't block, so the SDF vs cone GPU cost shows up there)
    f64 m_perfAccum{0.0};
    int m_perfFrames{0};
    bool m_perfLastSDF{false};
    int m_perfLastStress{-1};
};

}  // namespace Mc::Tests

#endif
