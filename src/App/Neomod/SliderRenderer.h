#pragma once
// Copyright (c) 2016, PG, All rights reserved.

#include "Vectors.h"
#include "Color.h"

#include <memory>
#include <span>

class Shader;
class VertexArrayObject;
class RenderTarget;
class Image;
struct Skin;

namespace SliderRenderer {

struct SkinSettings {
    // NULL/default ctor is valid for debug purposes
    SkinSettings() = default;
    SkinSettings(const Skin *skin);

    const Image *i_slider_gradient{nullptr};
    const Image *i_hitcircle{nullptr};
    bool o_slider_track_overridden{false};
    Color c_slider_track_override{};
    Color c_slider_border{rgb(255, 255, 255)};
};

std::unique_ptr<VertexArrayObject> generateVAO(vec2 screenRect, std::span<const vec2> points, f32 hitcircleDiameter,
                                               vec3 translation, bool skipOOBPoints = true);

struct DrawLegacyParams final {
    vec2 screenRect;
    RenderTarget *rt;
    SkinSettings skinSettings;
    std::span<const vec2> points;
    std::span<const vec2> alwaysPoints;
    f32 hitcircleDiameter;
    f32 from = 0.0f;
    f32 to = 1.0f;
    Color undimmedColor = 0xffffffff;
    f32 colorRGBMultiplier = 1.0f;
    f32 alpha = 1.0f;
    i32 sliderTimeForRainbow = 0;
};

void draw(const DrawLegacyParams &p);

struct DrawVAOParams final {
    vec2 screenRect;
    RenderTarget *rt;
    SkinSettings skinSettings;
    VertexArrayObject *vao;
    vec4 bounds;  // the curve's screen-space AABB (minX, minY, maxX, maxY), {} = the whole target
    std::span<const vec2> alwaysPoints;
    vec2 translation;
    f32 scale;
    f32 hitcircleDiameter;
    f32 from = 0.0f;
    f32 to = 1.0f;
    Color undimmedColor = 0xffffffff;
    f32 colorRGBMultiplier = 1.0f;
    f32 alpha = 1.0f;
    i32 sliderTimeForRainbow = 0;
};

void draw(const DrawVAOParams &p);

// for convar callbacks
void onUniformConfigChanged();

// true when slider bodies bake/draw the analytic SDF mesh (false = cone-disc fallback)
bool usingSDF();
};  // namespace SliderRenderer
