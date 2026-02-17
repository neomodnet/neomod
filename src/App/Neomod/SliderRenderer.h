#pragma once
// Copyright (c) 2016, PG, All rights reserved.

#include "Vectors.h"
#include "Color.h"

#include <vector>
#include <memory>

class Shader;
class VertexArrayObject;

namespace SliderRenderer {
std::unique_ptr<VertexArrayObject> generateVAO(const std::vector<vec2> &points, float hitcircleDiameter,
                                               vec3 translation = vec3(0, 0, 0), bool skipOOBPoints = true);

void draw(const std::vector<vec2> &points, const std::vector<vec2> &alwaysPoints, float hitcircleDiameter,
          float from = 0.0f, float to = 1.0f, Color undimmedColor = 0xffffffff, float colorRGBMultiplier = 1.0f,
          float alpha = 1.0f, i32 sliderTimeForRainbow = 0);
void draw(VertexArrayObject *vao, const std::vector<vec2> &alwaysPoints, vec2 translation, float scale,
          float hitcircleDiameter, float from = 0.0f, float to = 1.0f, Color undimmedColor = 0xffffffff,
          float colorRGBMultiplier = 1.0f, float alpha = 1.0f, i32 sliderTimeForRainbow = 0,
          bool doEnableRenderTarget = true, bool doDisableRenderTarget = true,
          bool doDrawSliderFrameBufferToScreen = true);
// for convar callbacks
void onUniformConfigChanged();
};  // namespace SliderRenderer
