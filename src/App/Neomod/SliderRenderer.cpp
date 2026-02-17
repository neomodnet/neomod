// Copyright (c) 2016, PG, All rights reserved.
#include "SliderRenderer.h"

#include "OsuConVars.h"
#include "Engine.h"
#include "GameRules.h"
#include "Osu.h"
#include "RenderTarget.h"
#include "ResourceManager.h"
#include "Environment.h"
#include "Shader.h"
#include "Skin.h"
#include "UI.h"
#include "VertexArrayObject.h"
#include "Logging.h"

#include <limits>

namespace SliderRenderer {

namespace {  // static namespace

Shader *s_BLEND_SHADER = nullptr;
float s_UNIT_CIRCLE_VAO_DIAMETER = 0.0f;

// base mesh
float s_MESH_CENTER_HEIGHT = 0.5f;   // Camera::buildMatrixOrtho2D() uses -1 to 1 for zn/zf, so don't make this too high
int s_UNIT_CIRCLE_SUBDIVISIONS = 0;  // see osu_slider_body_unit_circle_subdivisions now
std::vector<float> s_UNIT_CIRCLE;
VertexArrayObject *s_UNIT_CIRCLE_VAO = nullptr;
VertexArrayObject *s_UNIT_CIRCLE_VAO_BAKED = nullptr;
VertexArrayObject *s_UNIT_CIRCLE_VAO_TRIANGLES = nullptr;

// tiny rendering optimization for RenderTarget
float s_fBoundingBoxMinX = (std::numeric_limits<float>::max)();
float s_fBoundingBoxMaxX = 0.0f;
float s_fBoundingBoxMinY = (std::numeric_limits<float>::max)();
float s_fBoundingBoxMaxY = 0.0f;

struct UniformCache {
    // convar-dependent settings (updated by convar callbacks)
    int style{-1};
    float bodyAlphaMultiplier{-1.0f};
    float bodyColorSaturation{-1.0f};
    float borderSizeMultiplier{-1.0f};
    float borderFeather{-1.0f};

    // uniforms that change often (colors)
    Color lastBorderColor{0};
    Color lastBodyColor{0};

    bool needsConfigUpdate{true};  // for convar-based uniforms
};

UniformCache s_uniformCache;

// helper function to update color uniforms (after ->enable-ing the shader)
void updateColorUniforms(Color borderColor, Color bodyColor);
// check if convar-dependent uniforms need to be updated (after ->enable-ing the shader)
void updateConfigUniforms();

// forward decls
void drawDebugLegacy(const std::vector<vec2> &points, float hitcircleDiameter, Color undimmedColor,
                     float colorRGBMultiplier, float alpha, int drawFromIndex, int drawUpToIndex);
void drawDebugVAO(VertexArrayObject *vao, vec2 translation, float scale, float from, float to, Color undimmedColor,
                  float colorRGBMultiplier, float alpha);

void drawFillSliderBodyPeppy(const std::vector<vec2> &points, VertexArrayObject *circleMesh, float radius,
                             int drawFromIndex, int drawUpToIndex);
void checkUpdateVars(float hitcircleDiameter);

Color getRainbowColor(i32 rainbowTime, float initOffset) {
    const float frequency = 0.3f;
    const float time = engine->getTime() * 20;

    const Channel red = std::sin(frequency * (time * initOffset) + 0 + rainbowTime) * 127 + 128;
    const Channel green = std::sin(frequency * (time * initOffset) + 2 + rainbowTime) * 127 + 128;
    const Channel blue = std::sin(frequency * (time * initOffset) + 4 + rainbowTime) * 127 + 128;

    return rgb(red, green, blue);
}

forceinline Color getBodyColor(bool doRainbow, i32 rainbowTime, float colorRGBMultiplier, Color undimmedColor) {
    if(doRainbow) {
        return getRainbowColor(rainbowTime, 1.5f);
    } else {
        const Color undimmedBodyColor =
            osu->getSkin()->o_slider_track_overridden ? osu->getSkin()->c_slider_track_override : undimmedColor;

        return Colors::scale(undimmedBodyColor, colorRGBMultiplier);
    }
}

forceinline Color getBorderColor(bool doRainbow, i32 rainbowTime, float colorRGBMultiplier, Color undimmedColor) {
    if(doRainbow) {
        return getRainbowColor(rainbowTime, 1.f);
    } else {
        const Color undimmedBorderColor =
            cv::slider_border_tint_combo_color.getBool() ? undimmedColor : osu->getSkin()->c_slider_border;

        return Colors::scale(undimmedBorderColor, colorRGBMultiplier);
    }
}

forceinline void preDrawColorSetup(bool gradient, i32 sliderTime, float colorRGBMultiplier, Color undimmedColor) {
    if(gradient) {
        // this only affects the gradient image if used (meaning shaders
        // either don't work or are disabled on purpose)
        g->setColor(argb(1.0f, colorRGBMultiplier, colorRGBMultiplier, colorRGBMultiplier));
        osu->getSkin()->i_slider_gradient->bind();
    } else {
        const bool doRainbow = cv::slider_rainbow.getBool();

        const Color borderColor = getBorderColor(doRainbow, sliderTime, colorRGBMultiplier, undimmedColor);
        const Color bodyColor = getBodyColor(doRainbow, sliderTime, colorRGBMultiplier, undimmedColor);

        s_BLEND_SHADER->enable();
        updateConfigUniforms();
        updateColorUniforms(borderColor, bodyColor);
    }
}

}  // namespace

// invalidate config uniforms (convar callbacks)
void onUniformConfigChanged() { s_uniformCache.needsConfigUpdate = true; }

std::unique_ptr<VertexArrayObject> generateVAO(const std::vector<vec2> &points, float hitcircleDiameter,
                                               vec3 translation, bool skipOOBPoints) {
    resourceManager->requestNextLoadUnmanaged();
    std::unique_ptr<VertexArrayObject> vao{resourceManager->createVertexArrayObject()};

    checkUpdateVars(hitcircleDiameter);

    const vec3 xOffset = vec3(hitcircleDiameter, 0, 0);
    const vec3 yOffset = vec3(0, hitcircleDiameter, 0);

    const bool debugSquareVao = cv::slider_debug_draw_square_vao.getBool();

    for(const auto &point : points) {
        // fuck oob sliders
        if(skipOOBPoints) {
            if(point.x < -hitcircleDiameter - GameRules::OSU_COORD_WIDTH * 2 ||
               point.x > osu->getVirtScreenWidth() + hitcircleDiameter + GameRules::OSU_COORD_WIDTH * 2 ||
               point.y < -hitcircleDiameter - GameRules::OSU_COORD_HEIGHT * 2 ||
               point.y > osu->getVirtScreenHeight() + hitcircleDiameter + GameRules::OSU_COORD_HEIGHT * 2)
                continue;
        }

        if(!debugSquareVao) {
            const std::vector<vec3> &meshVertices = s_UNIT_CIRCLE_VAO_TRIANGLES->getVertices();
            const std::vector<vec2> &meshTexCoords = s_UNIT_CIRCLE_VAO_TRIANGLES->getTexcoords();
            for(int v = 0; v < meshVertices.size(); v++) {
                vao->addVertex(meshVertices[v] + vec3(point.x, point.y, 0) + translation);
                vao->addTexcoord(meshTexCoords[v]);
            }
        } else {
            const vec3 topLeft = vec3(point.x, point.y, 0) - xOffset / 2.0f - yOffset / 2.0f + translation;
            const vec3 topRight = topLeft + xOffset;
            const vec3 bottomLeft = topLeft + yOffset;
            const vec3 bottomRight = bottomLeft + xOffset;

            vao->addVertex(topLeft);
            vao->addTexcoord(0, 0);

            vao->addVertex(bottomLeft);
            vao->addTexcoord(0, 1);

            vao->addVertex(bottomRight);
            vao->addTexcoord(1, 1);

            vao->addVertex(topLeft);
            vao->addTexcoord(0, 0);

            vao->addVertex(bottomRight);
            vao->addTexcoord(1, 1);

            vao->addVertex(topRight);
            vao->addTexcoord(1, 0);
        }
    }

    if(vao->getNumVertices() > 0)
        resourceManager->loadResource(vao.get());
    else
        debugLog("generateSliderVAO() ERROR: Zero triangles!");

    return vao;
}

void draw(const std::vector<vec2> &points, const std::vector<vec2> &alwaysPoints, float hitcircleDiameter, float from,
          float to, Color undimmedColor, float colorRGBMultiplier, float alpha, i32 sliderTimeForRainbow) {
    if(cv::slider_alpha_multiplier.getFloat() <= 0.0f || alpha <= 0.0f) return;

    checkUpdateVars(hitcircleDiameter);

    const int drawFromIndex = std::clamp<int>((int)std::round(points.size() * from), 0, points.size());
    const int drawUpToIndex = std::clamp<int>((int)std::round(points.size() * to), 0, points.size());

    // debug sliders
    if(cv::slider_debug_draw.getBool()) {
        drawDebugLegacy(points, hitcircleDiameter, undimmedColor, colorRGBMultiplier, alpha, drawFromIndex,
                        drawUpToIndex);
        return;  // nothing more to draw here
    }

    // reset
    s_fBoundingBoxMinX = (std::numeric_limits<float>::max)();
    s_fBoundingBoxMaxX = 0.0f;
    s_fBoundingBoxMinY = (std::numeric_limits<float>::max)();
    s_fBoundingBoxMaxY = 0.0f;

    // draw entire slider into framebuffer
    g->setDepthBuffer(true);
    g->setBlending(false);
    {
        osu->getSliderFrameBuffer()->enable();
        {
            const bool useGradientImage = cv::slider_use_gradient_image.getBool();
            preDrawColorSetup(useGradientImage, sliderTimeForRainbow, colorRGBMultiplier, undimmedColor);

            // draw curve mesh
            drawFillSliderBodyPeppy(
                points, (cv::slider_legacy_use_baked_vao.getBool() ? s_UNIT_CIRCLE_VAO_BAKED : s_UNIT_CIRCLE_VAO),
                hitcircleDiameter / 2.0f, drawFromIndex, drawUpToIndex);

            if(alwaysPoints.size() > 0)
                drawFillSliderBodyPeppy(alwaysPoints, s_UNIT_CIRCLE_VAO_BAKED, hitcircleDiameter / 2.0f, 0,
                                        alwaysPoints.size());

            if(!useGradientImage) {
                s_BLEND_SHADER->disable();
            } else {
                osu->getSkin()->i_slider_gradient->unbind();
            }
        }
        osu->getSliderFrameBuffer()->disable();
    }
    g->setBlending(true);
    g->setDepthBuffer(false);

    // now draw the slider to the screen (with alpha blending enabled again)
    const int pixelFudge = 2;
    s_fBoundingBoxMinX -= pixelFudge;
    s_fBoundingBoxMaxX += pixelFudge;
    s_fBoundingBoxMinY -= pixelFudge;
    s_fBoundingBoxMaxY += pixelFudge;

    osu->getSliderFrameBuffer()->setColor(argb(alpha * cv::slider_alpha_multiplier.getFloat(), 1.0f, 1.0f, 1.0f));
    osu->getSliderFrameBuffer()->drawRect(s_fBoundingBoxMinX, s_fBoundingBoxMinY,
                                          s_fBoundingBoxMaxX - s_fBoundingBoxMinX,
                                          s_fBoundingBoxMaxY - s_fBoundingBoxMinY);
}

void draw(VertexArrayObject *vao, const std::vector<vec2> &alwaysPoints, vec2 translation, float scale,
          float hitcircleDiameter, float from, float to, Color undimmedColor, float colorRGBMultiplier, float alpha,
          i32 sliderTimeForRainbow, bool doEnableRenderTarget, bool doDisableRenderTarget,
          bool doDrawSliderFrameBufferToScreen) {
    if((cv::slider_alpha_multiplier.getFloat() <= 0.0f && doDrawSliderFrameBufferToScreen) ||
       (alpha <= 0.0f && doDrawSliderFrameBufferToScreen) || vao == nullptr)
        return;

    checkUpdateVars(hitcircleDiameter);

    if(cv::slider_debug_draw_square_vao.getBool()) {
        drawDebugVAO(vao, translation, scale, from, to, undimmedColor, colorRGBMultiplier, alpha);
        return;
    }

    // draw entire slider into framebuffer
    g->setDepthBuffer(true);
    g->setBlending(false);
    {
        if(doEnableRenderTarget) osu->getSliderFrameBuffer()->enable();

        // render
        {
            const bool useGradientImage = cv::slider_use_gradient_image.getBool();
            preDrawColorSetup(useGradientImage, sliderTimeForRainbow, colorRGBMultiplier, undimmedColor);

            // draw curve mesh
            vao->setDrawPercent(from, to, s_UNIT_CIRCLE_VAO_TRIANGLES->getVertices().size());
            g->pushTransform();
            {
                g->scale(scale, scale);
                g->translate(translation.x, translation.y);
                /// g->scale(scaleToApplyAfterTranslationX, scaleToApplyAfterTranslationY); // aspire slider
                /// distortions

                g->drawVAO(vao);
            }
            g->popTransform();

            if(alwaysPoints.size() > 0)
                drawFillSliderBodyPeppy(alwaysPoints, s_UNIT_CIRCLE_VAO_BAKED, hitcircleDiameter / 2.0f, 0,
                                        alwaysPoints.size());

            if(!useGradientImage) {
                s_BLEND_SHADER->disable();
            } else {
                osu->getSkin()->i_slider_gradient->unbind();
            }
        }

        if(doDisableRenderTarget) osu->getSliderFrameBuffer()->disable();
    }
    g->setBlending(true);
    g->setDepthBuffer(false);

    if(doDrawSliderFrameBufferToScreen) {
        osu->getSliderFrameBuffer()->setColor(argb(alpha * cv::slider_alpha_multiplier.getFloat(), 1.0f, 1.0f, 1.0f));
        osu->getSliderFrameBuffer()->draw(0, 0);
    }
}

namespace {  // static

void drawFillSliderBodyPeppy(const std::vector<vec2> &points, VertexArrayObject *circleMesh, float radius,
                             int drawFromIndex, int drawUpToIndex) {
    if(drawFromIndex < 0) drawFromIndex = 0;
    if(drawUpToIndex < 0) drawUpToIndex = points.size();

    g->pushTransform();
    {
        // now, translate and draw the master vao for every curve point
        float startX = 0.0f;
        float startY = 0.0f;
        for(int i = drawFromIndex; i < drawUpToIndex; ++i) {
            const float x = points[i].x;
            const float y = points[i].y;

            // fuck oob sliders
            if(x < -radius * 2 || x > osu->getVirtScreenWidth() + radius * 2 || y < -radius * 2 ||
               y > osu->getVirtScreenHeight() + radius * 2)
                continue;

            g->translate(x - startX, y - startY, 0);
            g->drawVAO(circleMesh);

            startX = x;
            startY = y;

            if(x - radius < s_fBoundingBoxMinX) s_fBoundingBoxMinX = x - radius;
            if(x + radius > s_fBoundingBoxMaxX) s_fBoundingBoxMaxX = x + radius;
            if(y - radius < s_fBoundingBoxMinY) s_fBoundingBoxMinY = y - radius;
            if(y + radius > s_fBoundingBoxMaxY) s_fBoundingBoxMaxY = y + radius;
        }
    }
    g->popTransform();
}

void checkUpdateVars(float hitcircleDiameter) {
    // static globals

    if(!env->usingGL()) {
        // NOTE: compensate for zn/zf Camera::buildMatrixOrtho2DDXLH() differences compared to OpenGL
        if(s_MESH_CENTER_HEIGHT > 0.0f) s_MESH_CENTER_HEIGHT = -s_MESH_CENTER_HEIGHT;
    }

    // build shaders and circle mesh
    if(s_BLEND_SHADER == nullptr)  // only do this once
    {
        // build shaders
        s_BLEND_SHADER = resourceManager->createShaderAuto("slider");
    }

    const int subdivisions = cv::slider_body_unit_circle_subdivisions.getInt();
    if(subdivisions != s_UNIT_CIRCLE_SUBDIVISIONS) {
        s_UNIT_CIRCLE_SUBDIVISIONS = subdivisions;

        // build unit cone
        {
            s_UNIT_CIRCLE.clear();

            // tip of the cone
            // texture coordinates
            s_UNIT_CIRCLE.push_back(1.0f);
            s_UNIT_CIRCLE.push_back(0.0f);

            // position
            s_UNIT_CIRCLE.push_back(0.0f);
            s_UNIT_CIRCLE.push_back(0.0f);
            s_UNIT_CIRCLE.push_back(s_MESH_CENTER_HEIGHT);

            for(int j = 0; j < subdivisions; ++j) {
                float phase = j * (float)PI * 2.0f / subdivisions;

                // texture coordinates
                s_UNIT_CIRCLE.push_back(0.0f);
                s_UNIT_CIRCLE.push_back(0.0f);

                // positon
                s_UNIT_CIRCLE.push_back((float)std::sin(phase));
                s_UNIT_CIRCLE.push_back((float)std::cos(phase));
                s_UNIT_CIRCLE.push_back(0.0f);
            }

            // texture coordinates
            s_UNIT_CIRCLE.push_back(0.0f);
            s_UNIT_CIRCLE.push_back(0.0f);

            // positon
            s_UNIT_CIRCLE.push_back((float)std::sin(0.0f));
            s_UNIT_CIRCLE.push_back((float)std::cos(0.0f));
            s_UNIT_CIRCLE.push_back(0.0f);
        }
    }

    // build vaos
    if(s_UNIT_CIRCLE_VAO == nullptr) s_UNIT_CIRCLE_VAO = new VertexArrayObject(DrawPrimitive::TRIANGLE_FAN);
    if(s_UNIT_CIRCLE_VAO_BAKED == nullptr)
        s_UNIT_CIRCLE_VAO_BAKED = resourceManager->createVertexArrayObject(DrawPrimitive::TRIANGLE_FAN);
    if(s_UNIT_CIRCLE_VAO_TRIANGLES == nullptr)
        s_UNIT_CIRCLE_VAO_TRIANGLES = new VertexArrayObject(DrawPrimitive::TRIANGLES);

    // (re-)generate master circle mesh (centered) if the size changed
    // dynamic mods like minimize or wobble have to use the legacy renderer anyway, since the slider shape may change
    // every frame
    if(hitcircleDiameter != s_UNIT_CIRCLE_VAO_DIAMETER) {
        const float radius = hitcircleDiameter / 2.0f;

        s_UNIT_CIRCLE_VAO_BAKED->release();

        // triangle fan
        s_UNIT_CIRCLE_VAO_DIAMETER = hitcircleDiameter;
        s_UNIT_CIRCLE_VAO->clear();
        for(int i = 0; i < s_UNIT_CIRCLE.size() / 5; i++) {
            vec3 vertexPos = vec3((radius * s_UNIT_CIRCLE[i * 5 + 2]), (radius * s_UNIT_CIRCLE[i * 5 + 3]),
                                  s_UNIT_CIRCLE[i * 5 + 4]);
            vec2 vertexTexcoord = vec2(s_UNIT_CIRCLE[i * 5 + 0], s_UNIT_CIRCLE[i * 5 + 1]);

            s_UNIT_CIRCLE_VAO->addVertex(vertexPos);
            s_UNIT_CIRCLE_VAO->addTexcoord(vertexTexcoord);

            s_UNIT_CIRCLE_VAO_BAKED->addVertex(vertexPos);
            s_UNIT_CIRCLE_VAO_BAKED->addTexcoord(vertexTexcoord);
        }

        resourceManager->loadResource(s_UNIT_CIRCLE_VAO_BAKED);

        // pure triangles (needed for VertexArrayObject, because we can't merge multiple triangle fan meshes into one
        // VertexArrayObject)
        s_UNIT_CIRCLE_VAO_TRIANGLES->clear();
        vec3 startVertex =
            vec3((radius * s_UNIT_CIRCLE[0 * 5 + 2]), (radius * s_UNIT_CIRCLE[0 * 5 + 3]), s_UNIT_CIRCLE[0 * 5 + 4]);
        vec2 startUV = vec2(s_UNIT_CIRCLE[0 * 5 + 0], s_UNIT_CIRCLE[0 * 5 + 1]);
        for(int i = 1; i < s_UNIT_CIRCLE.size() / 5 - 1; i++) {
            // center
            s_UNIT_CIRCLE_VAO_TRIANGLES->addVertex(startVertex);
            s_UNIT_CIRCLE_VAO_TRIANGLES->addTexcoord(startUV);

            // pizza slice edge 1
            s_UNIT_CIRCLE_VAO_TRIANGLES->addVertex(vec3((radius * s_UNIT_CIRCLE[i * 5 + 2]),
                                                        (radius * s_UNIT_CIRCLE[i * 5 + 3]), s_UNIT_CIRCLE[i * 5 + 4]));
            s_UNIT_CIRCLE_VAO_TRIANGLES->addTexcoord(vec2(s_UNIT_CIRCLE[i * 5 + 0], s_UNIT_CIRCLE[i * 5 + 1]));

            // pizza slice edge 2
            s_UNIT_CIRCLE_VAO_TRIANGLES->addVertex(vec3((radius * s_UNIT_CIRCLE[(i + 1) * 5 + 2]),
                                                        (radius * s_UNIT_CIRCLE[(i + 1) * 5 + 3]),
                                                        s_UNIT_CIRCLE[(i + 1) * 5 + 4]));
            s_UNIT_CIRCLE_VAO_TRIANGLES->addTexcoord(
                vec2(s_UNIT_CIRCLE[(i + 1) * 5 + 0], s_UNIT_CIRCLE[(i + 1) * 5 + 1]));
        }
    }
}

// helper function to update color uniforms
void updateColorUniforms(Color borderColor, Color bodyColor) {
    if(!s_BLEND_SHADER) return;

    if(s_uniformCache.lastBorderColor != borderColor) {
        s_BLEND_SHADER->setUniform3f("colBorder", borderColor.Rf(), borderColor.Gf(), borderColor.Bf());
        s_uniformCache.lastBorderColor = borderColor;
    }

    if(s_uniformCache.lastBodyColor != bodyColor) {
        s_BLEND_SHADER->setUniform3f("colBody", bodyColor.Rf(), bodyColor.Gf(), bodyColor.Bf());
        s_uniformCache.lastBodyColor = bodyColor;
    }
}

void updateConfigUniforms() {
    if(!s_BLEND_SHADER || !s_uniformCache.needsConfigUpdate) return;

    const int newStyle = cv::slider_osu_next_style.getBool() ? 1 : 0;
    const float newBodyAlpha = cv::slider_body_alpha_multiplier.getFloat();
    const float newBodySat = cv::slider_body_color_saturation.getFloat();
    const float newBorderSize = cv::slider_border_size_multiplier.getFloat();
    const float newBorderFeather = cv::slider_border_feather.getFloat();

    if(s_uniformCache.style != newStyle) {
        s_BLEND_SHADER->setUniform1i("style", newStyle);
        s_uniformCache.style = newStyle;
    }

    if(s_uniformCache.bodyAlphaMultiplier != newBodyAlpha) {
        s_BLEND_SHADER->setUniform1f("bodyAlphaMultiplier", newBodyAlpha);
        s_uniformCache.bodyAlphaMultiplier = newBodyAlpha;
    }

    if(s_uniformCache.bodyColorSaturation != newBodySat) {
        s_BLEND_SHADER->setUniform1f("bodyColorSaturation", newBodySat);
        s_uniformCache.bodyColorSaturation = newBodySat;
    }

    if(s_uniformCache.borderSizeMultiplier != newBorderSize) {
        s_BLEND_SHADER->setUniform1f("borderSizeMultiplier", newBorderSize);
        s_uniformCache.borderSizeMultiplier = newBorderSize;
    }

    if(s_uniformCache.borderFeather != newBorderFeather) {
        s_BLEND_SHADER->setUniform1f("borderFeather", newBorderFeather);
        s_uniformCache.borderFeather = newBorderFeather;
    }

    s_uniformCache.needsConfigUpdate = false;
}

void drawDebugLegacy(const std::vector<vec2> &points, float hitcircleDiameter, Color undimmedColor,
                     float colorRGBMultiplier, float alpha, int drawFromIndex, int drawUpToIndex) {
    const float circleImageScale = hitcircleDiameter / (float)osu->getSkin()->i_hitcircle->getWidth();
    const float circleImageScaleInv = (1.0f / circleImageScale);

    const auto width = (float)osu->getSkin()->i_hitcircle->getWidth();
    const auto height = (float)osu->getSkin()->i_hitcircle->getHeight();

    const float x = (-width / 2.0f);
    const float y = (-height / 2.0f);
    const float z = -1.0f;

    g->pushTransform();
    {
        g->scale(circleImageScale, circleImageScale);

        const Color dimmedColor = Colors::scale(undimmedColor, colorRGBMultiplier);

        g->setColor(Color(dimmedColor).setA(alpha * cv::slider_alpha_multiplier.getFloat()));

        osu->getSkin()->i_hitcircle->bind();
        {
            for(int i = drawFromIndex; i < drawUpToIndex; i++) {
                const vec2 point = points[i] * circleImageScaleInv;

                static VertexArrayObject vao(DrawPrimitive::QUADS);
                vao.clear();
                {
                    vao.addTexcoord(0, 0);
                    vao.addVertex(point.x + x, point.y + y, z);

                    vao.addTexcoord(0, 1);
                    vao.addVertex(point.x + x, point.y + y + height, z);

                    vao.addTexcoord(1, 1);
                    vao.addVertex(point.x + x + width, point.y + y + height, z);

                    vao.addTexcoord(1, 0);
                    vao.addVertex(point.x + x + width, point.y + y, z);
                }
                g->drawVAO(&vao);
            }
        }
        osu->getSkin()->i_hitcircle->unbind();
    }
    g->popTransform();
    return;
}

void drawDebugVAO(VertexArrayObject *vao, vec2 translation, float scale, float from, float to, Color undimmedColor,
                  float colorRGBMultiplier, float alpha) {
    const Color dimmedColor = Colors::scale(undimmedColor, colorRGBMultiplier);

    g->setColor(Color(dimmedColor).setA(alpha * cv::slider_alpha_multiplier.getFloat()));

    osu->getSkin()->i_hitcircle->bind();

    vao->setDrawPercent(from, to, 6);  // HACKHACK: hardcoded magic number
    {
        g->pushTransform();
        {
            g->scale(scale, scale);
            g->translate(translation.x, translation.y);

            g->drawVAO(vao);
        }
        g->popTransform();
    }

    return;
}

}  // namespace

}  // namespace SliderRenderer
