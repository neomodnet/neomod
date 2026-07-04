// Copyright (c) 2016, PG, All rights reserved.
#include "SliderRenderer.h"

#include "OsuConVars.h"
#include "Engine.h"
#include "GameRules.h"
#include "RenderTarget.h"
#include "ResourceManager.h"
#include "Environment.h"
#include "Shader.h"
#include "Skin.h"
#include "VertexArrayObject.h"
#include "Logging.h"
#include "Graphics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <cassert>

namespace SliderRenderer {

namespace {  // static namespace

Shader *s_BLEND_SHADER{nullptr};
Shader *s_FIELD_SHADER{nullptr};      // accumulates the body's distance field into the framebuffer (MAX blend)
Shader *s_COMPOSITE_SHADER{nullptr};  // shades the accumulated field once per pixel while drawing it to screen
f32 s_UNIT_CIRCLE_VAO_DIAMETER{0.0f};

// base mesh
f32 s_MESH_CENTER_HEIGHT{0.5f};     // Camera::buildMatrixOrtho2D() uses -1 to 1 for zn/zf, so don't make this too high
i32 s_UNIT_CIRCLE_SUBDIVISIONS{0};  // see slider_body_unit_circle_subdivisions now

// analytic SDF body: each kept curve point emits one equal-size block = a slab quad (6 verts) + a cap/join fan
// (SDF_FAN_SLICES triangles). these must stay in lockstep: if VERTS_PER_SDF_BLOCK doesn't match the emitted count,
// setDrawPercent() snake-snapping rounds the draw range to the wrong boundary and clips the static end cap
constexpr i32 SDF_FAN_SLICES{4};
constexpr i32 VERTS_PER_SDF_BLOCK{6 + SDF_FAN_SLICES * 3};
std::vector<f32> s_UNIT_CIRCLE;
// managed by RM (renderer-bound baking)
VertexArrayObject *s_UNIT_CIRCLE_VAO_BAKED{nullptr};

// unbaked, basic VAO containers
static CONSTINIT VertexArrayObject s_UNIT_CIRCLE_VAO{DrawPrimitive::TRIANGLE_FAN};
static CONSTINIT VertexArrayObject s_UNIT_CIRCLE_VAO_TRIANGLES{DrawPrimitive::TRIANGLES};
// SDF snake-head disc: a centered 2r quad whose corner texcoords (+/-1) make length(texcoord) the radial distance
static CONSTINIT VertexArrayObject s_UNIT_DISC_QUAD_SDF{DrawPrimitive::TRIANGLES};

// tiny rendering optimization for RenderTarget
f32 s_fBoundingBoxMinX{(std::numeric_limits<f32>::max)()};
f32 s_fBoundingBoxMaxX{0.0f};
f32 s_fBoundingBoxMinY{(std::numeric_limits<f32>::max)()};
f32 s_fBoundingBoxMaxY{0.0f};

struct UniformCache {
    // convar-dependent settings (updated by convar callbacks)
    i32 style{-1};
    f32 bodyAlphaMultiplier{-1.0f};
    f32 bodyColorSaturation{-1.0f};
    f32 borderSizeMultiplier{-1.0f};
    f32 borderFeather{-1.0f};

    // uniforms that change often (colors)
    Color lastBorderColor{0};
    Color lastBodyColor{0};

    bool needsConfigUpdate{true};  // for convar-based uniforms

    Shader *cacheShader{nullptr};  // which program the cached values were applied to (switching forces a re-push)
};

static CONSTINIT UniformCache s_uniformCache{};

// helper function to update color uniforms (after ->enable-ing the shader)
void updateColorUniforms(Shader *shader, Color borderColor, Color bodyColor);
// check if convar-dependent uniforms need to be updated (after ->enable-ing the shader)
void updateConfigUniforms(Shader *shader);

// forward decls
void drawDebugLegacy(const Image *hitcircleImage, std::span<const vec2> points, f32 hitcircleDiameter,
                     Color undimmedColor, f32 colorRGBMultiplier, f32 alpha, uSz drawFromIndex, uSz drawUpToIndex);
void drawDebugVAO(const Image *hitcircleImage, VertexArrayObject *vao, vec2 translation, f32 scale, f32 from, f32 to,
                  Color undimmedColor, f32 colorRGBMultiplier, f32 alpha);

void drawFillSliderBodyPeppy(vec2 screen, std::span<const vec2> points, VertexArrayObject *circleMesh, f32 radius,
                             uSz drawFromIndex, uSz drawUpToIndex);
void checkUpdateVars(f32 hitcircleDiameter);

Color getRainbowColor(i32 rainbowTime, f32 initOffset) {
    const f64 frequency = .3f;
    const f64 time = engine->getTime() * 20.;

    const Channel red = (Channel)(std::sin(frequency * (time * initOffset) + 0 + rainbowTime) * 127.) + 128;
    const Channel green = (Channel)(std::sin(frequency * (time * initOffset) + 2 + rainbowTime) * 127.) + 128;
    const Channel blue = (Channel)(std::sin(frequency * (time * initOffset) + 4 + rainbowTime) * 127.) + 128;

    return rgb(red, green, blue);
}

forceinline Color getBodyColor(const SkinSettings &settings, bool doRainbow, i32 rainbowTime, f32 colorRGBMultiplier,
                               Color undimmedColor) {
    if(doRainbow) {
        return getRainbowColor(rainbowTime, 1.5f);
    } else {
        const Color undimmedBodyColor =
            settings.o_slider_track_overridden ? settings.c_slider_track_override : undimmedColor;

        return Colors::scale(undimmedBodyColor, colorRGBMultiplier);
    }
}

forceinline Color getBorderColor(const SkinSettings &settings, bool doRainbow, i32 rainbowTime, f32 colorRGBMultiplier,
                                 Color undimmedColor) {
    if(doRainbow) {
        return getRainbowColor(rainbowTime, 1.f);
    } else {
        const Color undimmedBorderColor =
            cv::slider_border_tint_combo_color.getBool() ? undimmedColor : settings.c_slider_border;

        return Colors::scale(undimmedBorderColor, colorRGBMultiplier);
    }
}

forceinline void preDrawColorSetup(Shader *shader, const SkinSettings &settings, const Image *gradient, i32 sliderTime,
                                   f32 colorRGBMultiplier, Color undimmedColor) {
    if(gradient) {
        // this only affects the gradient image if used (meaning shaders
        // either don't work or are disabled on purpose)
        g->setColor(argb(1.0f, colorRGBMultiplier, colorRGBMultiplier, colorRGBMultiplier));
        gradient->bind();
    } else {
        const bool doRainbow = cv::slider_rainbow.getBool();

        const Color borderColor = getBorderColor(settings, doRainbow, sliderTime, colorRGBMultiplier, undimmedColor);
        const Color bodyColor = getBodyColor(settings, doRainbow, sliderTime, colorRGBMultiplier, undimmedColor);

        shader->enable();
        updateConfigUniforms(shader);
        updateColorUniforms(shader, borderColor, bodyColor);
    }
}

}  // namespace

SkinSettings::SkinSettings(const Skin *skin) {
    if(skin) {
        this->i_slider_gradient = skin->i_slider_gradient;
        this->i_hitcircle = skin->i_hitcircle;
        this->o_slider_track_overridden = skin->o_slider_track_overridden;
        this->c_slider_track_override = skin->c_slider_track_override;
        this->c_slider_border = skin->c_slider_border;
    }
}

// invalidate config uniforms (convar callbacks)
void onUniformConfigChanged() { s_uniformCache.needsConfigUpdate = true; }

bool usingSDF() {
    // the gradient image path colors the body by texturing the cone mesh fixed-function (no shader), which the
    // field/composite split can't reproduce, so it forces the cone fallback. a failed shader compile falls back
    // the same way via isReady().
    return cv::slider_body_sdf.getBool() && !cv::slider_use_gradient_image.getBool() && s_FIELD_SHADER &&
           s_FIELD_SHADER->isReady() && s_COMPOSITE_SHADER && s_COMPOSITE_SHADER->isReady();
}

std::unique_ptr<VertexArrayObject> generateVAO(vec2 screenRect, std::span<const vec2> points, f32 hitcircleDiameter,
                                               vec3 translation, bool skipOOBPoints) {
    std::unique_ptr<VertexArrayObject> vao{
        g->createVertexArrayObject(DrawPrimitive::TRIANGLES, DrawUsageType::STATIC, /*keepInSystemMemory=*/false)};

    checkUpdateVars(hitcircleDiameter);

    const vec4 bounds{
        -hitcircleDiameter - GameRules::OSU_COORD_WIDTH * 2,                // x = minX
        screenRect.x + hitcircleDiameter + GameRules::OSU_COORD_WIDTH * 2,  // y = maxX
        -hitcircleDiameter - GameRules::OSU_COORD_HEIGHT * 2,               // z = minY
        screenRect.y + hitcircleDiameter + GameRules::OSU_COORD_HEIGHT * 2  // w = maxY
    };
    const auto isOOB = [bounds](vec2 point) -> bool {
        // fuck oob sliders
        return point.x < bounds.x || point.x > bounds.y || point.y < bounds.z || point.y > bounds.w;
    };

    const bool useSDF = !cv::slider_debug_draw_square_vao.getBool() && !points.empty() && usingSDF();

    if(cv::slider_debug_draw_square_vao.getBool()) {  // debug
        const vec3 xOffset = vec3(hitcircleDiameter, 0, 0);
        const vec3 yOffset = vec3(0, hitcircleDiameter, 0);

        for(const auto &point : points) {
            if(skipOOBPoints && isOOB(point)) continue;

            const vec3 topLeft = vec3(point.x, point.y, 0) - xOffset / 2.0f - yOffset / 2.0f + translation;
            const vec3 topRight = topLeft + xOffset;
            const vec3 bottomLeft = topLeft + yOffset;
            const vec3 bottomRight = bottomLeft + xOffset;

            vao->addVertices(std::array<vec3, 6>{topLeft,      //
                                                 bottomLeft,   //
                                                 bottomRight,  //
                                                 topLeft,      //
                                                 bottomRight,  //
                                                 topRight});
            vao->addTexcoords(std::array<vec2, 6>{vec2{0, 0},  //
                                                  vec2{0, 1},  //
                                                  vec2{1, 1},  //
                                                  vec2{0, 0},  //
                                                  vec2{1, 1},  //
                                                  vec2{1, 0}});
        }
    } else if(useSDF) {  // analytic distance-field body (regular fast path)
        // render the body as an exact distance field instead of stamping a full cone disc at every curve point
        // (massive overdraw: neighboring radius-r discs sit only ~2.5 osu!px apart). each primitive's texcoord
        // carries (fragment - nearest curve feature)/r; the sliderField shader emits that feature's radial
        // gradient 1 - length(texcoord) and the MAX blend union resolves every covered pixel to the nearest
        // feature. geometry only has to COVER each feature; the rounding happens per-fragment, so
        // caps/joins/cusps are exact at any tessellation density.
        const f32 r = hitcircleDiameter / 2.0f;
        const uSz n = points.size();

        // primitives that abut without shared vertices leave 1px rasterization cracks (T-junctions), which the
        // max union shows as holes; so every slab/fan overlaps 1-2px into its neighbors. the union is
        // idempotent, so the overlap never widens the silhouette.
        const f32 seamMargin = std::min(2.0f / r, 0.5f);  // fan over-sweep, ~2px of rim arc in radians

        // duplicated consecutive points (bezier piece anchors) yield segments too short for a usable direction,
        // so every point takes its in/out direction from the nearest DISTINCT point instead ({0,0} if that side
        // has none). the duplicates must still emit their own equal-size blocks: one block per point keeps
        // setDrawPercent()'s percent -> block snapping in lockstep with the caller's percent -> curve-point
        // mapping (snake head position).
        constexpr f32 DIR_EPS = 0.01f;
        std::vector<vec2> dirIn(n, vec2{0.0f, 0.0f});
        std::vector<vec2> dirOut(n, vec2{0.0f, 0.0f});
        if(n >= 2) {
            vec2 anchor = points[0];
            vec2 dir{0.0f, 0.0f};
            for(uSz i = 1; i < n; ++i) {
                const vec2 d = points[i] - anchor;
                if(const f32 l = vec::length(d); l > DIR_EPS) {
                    dir = d / l;
                    anchor = points[i];
                }
                dirIn[i] = dir;
            }
            anchor = points[n - 1];
            dir = vec2{0.0f, 0.0f};
            for(uSz i = n - 1; i-- > 0;) {
                const vec2 d = anchor - points[i];
                if(const f32 l = vec::length(d); l > DIR_EPS) {
                    dir = d / l;
                    anchor = points[i];
                }
                dirOut[i] = dir;
            }
        }

        // OOB points emit no blocks at all (see the loop below), so don't reserve for them either
        // TODO: is this double loop faster/worth it over just reserving the entire thing anyways/not reserving anything
        uSz keptPoints = n;
        if(skipOOBPoints) {
            uSz count = 0;
            for(auto point : points)
                if(!isOOB(point)) ++count;
            keptPoints = count;
        }

        std::vector<vec3> meshVerts;
        std::vector<vec2> meshTCs;
        meshVerts.reserve(VERTS_PER_SDF_BLOCK * keptPoints);
        meshTCs.reserve(VERTS_PER_SDF_BLOCK * keptPoints);

        const auto emitVert = [&](vec2 p, vec2 tc) {
            meshVerts.emplace_back(p.x + translation.x, p.y + translation.y, translation.z);
            meshTCs.emplace_back(tc);
        };

        // segment slab a->b: a 2r-wide rectangle whose side texcoords (0, +/-1) make length(texcoord) the
        // perpendicular distance to the segment's line. 6 verts.
        const auto emitSlab = [&](vec2 a, vec2 b, vec2 dir) {
            const vec2 side = vec2{-dir.y, dir.x} * r;
            a -= dir;  // ~1px lengthwise overlap into the neighboring slabs/caps (see seamMargin); the fans
            b += dir;  // still win the max union with the exact round corner, so nothing visibly squares off
            const vec2 tcL{0.0f, 1.0f}, tcR{0.0f, -1.0f};
            emitVert(a + side, tcL);
            emitVert(a - side, tcR);
            emitVert(b - side, tcR);
            emitVert(a + side, tcL);
            emitVert(b - side, tcR);
            emitVert(b + side, tcL);
        };
        // zero-area stand-in where a point has no incoming segment, keeping every block equally sized
        const auto emitFillerSlab = [&](vec2 p) {
            for(i32 k = 0; k < 6; ++k) emitVert(p, vec2{0.0f, 0.0f});
        };

        // cap/join fan at c, sweeping halfSweep (+ seamMargin) to each side of midDir: rim texcoords are
        // circumscribed unit directions, making texcoord = (fragment - c)/r exact across every triangle, so the
        // drawn arc is exactly round regardless of SDF_FAN_SLICES (the covered sliver past the true rim just
        // clamps to field 0 in the shader). midDir = {0,0} emits a zero-area filler.
        const auto emitFan = [&](vec2 c, vec2 midDir, f32 halfSweep) {
            if(midDir == vec2{0.0f, 0.0f}) {
                for(i32 k = 0; k < SDF_FAN_SLICES * 3; ++k) emitVert(c, vec2{0.0f, 0.0f});
                return;
            }
            const f32 from = (f32)std::atan2(midDir.y, midDir.x) - halfSweep - seamMargin;
            const f32 sweep = 2.0f * (halfSweep + seamMargin);
            const f32 circumscribe = 1.0f / (f32)std::cos(sweep / (2.0f * (f32)SDF_FAN_SLICES));
            const auto rimTC = [&](i32 k) {
                const f32 a = from + sweep * (f32)k / (f32)SDF_FAN_SLICES;
                return vec2{(f32)std::cos(a), (f32)std::sin(a)} * circumscribe;
            };
            vec2 tcPrev = rimTC(0);
            for(i32 k = 1; k <= SDF_FAN_SLICES; ++k) {
                const vec2 tcCur = rimTC(k);
                emitVert(c, vec2{0.0f, 0.0f});
                emitVert(c + tcPrev * r, tcPrev);
                emitVert(c + tcCur * r, tcCur);
                tcPrev = tcCur;
            }
        };

        // one block per kept input point: the slab of the segment arriving at the point + the fan rounding it
        for(uSz i = 0; i < n; ++i) {
            // dropping a far-offscreen block desyncs the percent -> block snapping from the caller's snake
            // mapping just like the cone path dropping its discs, but only on the same broken/aspire maps,
            // where baking + transforming millions of offscreen blocks every frame is the greater evil (the
            // OOB bounds are generous enough that a cut end can never reach the viewport)
            if(skipOOBPoints && isOOB(points[i])) continue;

            const vec2 seg = i >= 1 ? points[i] - points[i - 1] : vec2{0.0f, 0.0f};
            const f32 segLen = vec::length(seg);
            if(segLen > DIR_EPS)
                emitSlab(points[i - 1], points[i], seg / segLen);
            else  // first point or a duplicate
                emitFillerSlab(points[i]);

            if(i == 0 || i == n - 1) {
                // cap: a half-disc facing away from the curve, or a full disc if the curve degenerates to a point
                const vec2 away = i == 0 ? -dirOut[i] : dirIn[i];
                if(away == vec2{0.0f, 0.0f})
                    emitFan(points[i], vec2{1.0f, 0.0f}, PI_F);
                else
                    emitFan(points[i], away, PI_F * 0.5f);
            } else if(dirIn[i] == vec2{0.0f, 0.0f} || dirOut[i] == vec2{0.0f, 0.0f}) {
                // inside a duplicate run at the curve's start/end: the cap fan already rounds this spot
                emitFan(points[i], vec2{0.0f, 0.0f}, 0.0f);
            } else {
                // join: round the outer corner (the slabs already cover the concave side), sweeping symmetrically
                // about the outer-wedge bisector dIn - dOut. unlike picking a side from the cross-product sign,
                // the bisector stays well-conditioned at reversals (retraced lines fold with cross == fp noise)
                // and only degenerates near-collinear, where either side works because the slabs overlap.
                const vec2 dIn = dirIn[i], dOut = dirOut[i];
                const f32 turn = std::acos(std::clamp(vec::dot(dIn, dOut), -1.0f, 1.0f));
                vec2 bisector = dIn - dOut;
                if(const f32 l = vec::length(bisector); l > 1e-3f)
                    bisector /= l;
                else
                    bisector = (dIn.x * dOut.y - dIn.y * dOut.x) > 0.0f ? vec2{dIn.y, -dIn.x} : vec2{-dIn.y, dIn.x};
                emitFan(points[i], bisector, turn * 0.5f);
            }
        }

        vao->setVertices(std::move(meshVerts));
        vao->setTexcoords(std::move(meshTCs));
    } else {  // legacy cone discs (one full cone per curve point)
        const std::span<const vec3> triangleMeshVerts = s_UNIT_CIRCLE_VAO_TRIANGLES.getVertices();
        const std::span<const vec2> triangleMeshTCs = s_UNIT_CIRCLE_VAO_TRIANGLES.getTexcoords();
        std::vector<vec2> tempTexCoords{triangleMeshTCs.size() * points.size()};
        std::vector<vec3> tempMeshVerts{triangleMeshVerts.size() * points.size()};

        uSz tempMeshVertOffset = 0;
        uSz tempMeshTCOffset = 0;
        for(const auto &point : points) {
            if(skipOOBPoints && isOOB(point)) continue;

            for(const auto &meshVertex : triangleMeshVerts) {
                tempMeshVerts[tempMeshVertOffset++] = (meshVertex + vec3(point.x, point.y, 0) + translation);
            }
            std::memcpy(&tempTexCoords[tempMeshTCOffset], triangleMeshTCs.data(),
                        triangleMeshTCs.size() * sizeof(decltype(tempTexCoords)::value_type));
            tempMeshTCOffset += triangleMeshTCs.size();
        }

        // resize to post-OOB-clipped amount
        tempMeshVerts.resize(tempMeshVertOffset);
        tempMeshVerts.shrink_to_fit();
        tempTexCoords.resize(tempMeshTCOffset);
        tempTexCoords.shrink_to_fit();
        vao->setVertices(std::move(tempMeshVerts));
        vao->setTexcoords(std::move(tempTexCoords));
    }

    if(vao->getNumVertices() > 0) {
        vao->loadAsync();
        vao->load();
    } else {
        debugLog("ERROR: Zero triangles!");
    }

    return vao;
}

void draw(const DrawLegacyParams &p) {
    if(cv::slider_alpha_multiplier.getFloat() <= 0.0f || p.alpha <= 0.0f) return;

    checkUpdateVars(p.hitcircleDiameter);

    const uSz drawFromIndex = std::clamp<uSz>((uSz)std::round((f64)p.points.size() * p.from), 0UZ, p.points.size());
    const uSz drawUpToIndex = std::clamp<uSz>((uSz)std::round((f64)p.points.size() * p.to), 0UZ, p.points.size());

    // debug sliders
    if(cv::slider_debug_draw.getBool()) {
        drawDebugLegacy(p.skinSettings.i_hitcircle, p.points, p.hitcircleDiameter, p.undimmedColor,
                        p.colorRGBMultiplier, p.alpha, drawFromIndex, drawUpToIndex);
        return;  // nothing more to draw here
    }

    // reset
    s_fBoundingBoxMinX = (std::numeric_limits<f32>::max)();
    s_fBoundingBoxMaxX = 0.0f;
    s_fBoundingBoxMinY = (std::numeric_limits<f32>::max)();
    s_fBoundingBoxMaxY = 0.0f;

    // draw entire slider into framebuffer
    g->setDepthBuffer(true);
    g->setBlending(false);
    {
        p.rt->enable();
        {
            const Image *gradient = nullptr;
            const bool useGradientImage = p.skinSettings.i_slider_gradient && cv::slider_use_gradient_image.getBool();
            if(useGradientImage) {
                gradient = p.skinSettings.i_slider_gradient;
            }
            // legacy/dynamic-mod path always renders cone discs, so use the cone shader
            preDrawColorSetup(s_BLEND_SHADER, p.skinSettings, gradient, p.sliderTimeForRainbow, p.colorRGBMultiplier,
                              p.undimmedColor);

            // draw curve mesh
            drawFillSliderBodyPeppy(
                p.screenRect, p.points,
                (cv::slider_legacy_use_baked_vao.getBool() ? s_UNIT_CIRCLE_VAO_BAKED : &s_UNIT_CIRCLE_VAO),
                p.hitcircleDiameter / 2.0f, drawFromIndex, drawUpToIndex);

            if(p.alwaysPoints.size() > 0)
                drawFillSliderBodyPeppy(p.screenRect, p.alwaysPoints, s_UNIT_CIRCLE_VAO_BAKED,
                                        p.hitcircleDiameter / 2.0f, 0, p.alwaysPoints.size());

            if(!useGradientImage) {
                s_BLEND_SHADER->disable();
            } else {
                gradient->unbind();
            }
        }
        p.rt->disable();
    }
    g->setBlending(true);
    g->setDepthBuffer(false);

    // now draw the slider to the screen (with alpha blending enabled again)
    const f32 pixelFudge = 2.0f;
    s_fBoundingBoxMinX -= pixelFudge;
    s_fBoundingBoxMaxX += pixelFudge;
    s_fBoundingBoxMinY -= pixelFudge;
    s_fBoundingBoxMaxY += pixelFudge;

    p.rt->setColor(argb(p.alpha * cv::slider_alpha_multiplier.getFloat(), 1.0f, 1.0f, 1.0f));
    p.rt->drawRect((i32)s_fBoundingBoxMinX, (i32)s_fBoundingBoxMinY, (i32)(s_fBoundingBoxMaxX - s_fBoundingBoxMinX),
                   (i32)(s_fBoundingBoxMaxY - s_fBoundingBoxMinY));
}

void draw(const DrawVAOParams &p) {
    if((cv::slider_alpha_multiplier.getFloat() <= 0.0f && p.doDrawSliderFrameBufferToScreen) ||
       (p.alpha <= 0.0f && p.doDrawSliderFrameBufferToScreen) || p.vao == nullptr)
        return;

    checkUpdateVars(p.hitcircleDiameter);

    if(cv::slider_debug_draw_square_vao.getBool()) {
        drawDebugVAO(p.skinSettings.i_hitcircle, p.vao, p.translation, p.scale, p.from, p.to, p.undimmedColor,
                     p.colorRGBMultiplier, p.alpha);
        return;
    }

    // bake and draw key off the same global mode: the cvar callbacks (see Osu.cpp) rebake every slider VAO
    // when it flips, so a baked mesh never meets the wrong pipeline. the per-frame legacy path (dynamic mods)
    // always renders cone discs and uses s_BLEND_SHADER directly
    const bool sdf = usingSDF();

    // reset
    s_fBoundingBoxMinX = (std::numeric_limits<f32>::max)();
    s_fBoundingBoxMaxX = 0.0f;
    s_fBoundingBoxMinY = (std::numeric_limits<f32>::max)();
    s_fBoundingBoxMaxY = 0.0f;

    // draw entire slider into framebuffer
    if(sdf) {
        // accumulate the body's distance field: each primitive MAX-blends its radial gradient, so the union
        // needs no depth buffer at all and self-overlapping geometry (retraced/aspire curves stack thousands
        // of blocks on the same pixels) costs only trivial blended fills. the expensive gradient shading runs
        // exactly once per covered pixel in the composite draw below.
        g->setBlending(true);
        g->setBlendMode(DrawBlendMode::MAX);
        {
            if(p.doEnableRenderTarget) p.rt->enable();

            s_FIELD_SHADER->enable();

            // draw curve mesh
            p.vao->setDrawPercent(p.from, p.to, VERTS_PER_SDF_BLOCK);
            g->pushTransform();
            {
                g->scale(p.scale, p.scale);
                g->translate(p.translation.x, p.translation.y);
                /// g->scale(scaleToApplyAfterTranslationX, scaleToApplyAfterTranslationY); // aspire slider
                /// distortions

                g->drawVAO(p.vao);
            }
            g->popTransform();

            // the moving snake head: a disc-quad with the same texcoord encoding as the baked body
            if(p.alwaysPoints.size() > 0)
                drawFillSliderBodyPeppy(p.screenRect, p.alwaysPoints, &s_UNIT_DISC_QUAD_SDF, p.hitcircleDiameter / 2.0f,
                                        0, p.alwaysPoints.size());

            s_FIELD_SHADER->disable();

            if(p.doDisableRenderTarget) p.rt->disable();
        }
        g->setBlendMode(DrawBlendMode::ALPHA);
    } else {
        // legacy cone discs: the opaque draw under GL_LESS resolves the self-overlap in the depth buffer
        g->setDepthBuffer(true);
        g->setBlending(false);
        {
            if(p.doEnableRenderTarget) p.rt->enable();

            const Image *gradient = nullptr;
            const bool useGradientImage = p.skinSettings.i_slider_gradient && cv::slider_use_gradient_image.getBool();
            if(useGradientImage) {
                gradient = p.skinSettings.i_slider_gradient;
            }
            // enables shader if gradient is not being used
            preDrawColorSetup(s_BLEND_SHADER, p.skinSettings, gradient, p.sliderTimeForRainbow, p.colorRGBMultiplier,
                              p.undimmedColor);

            // draw curve mesh
            p.vao->setDrawPercent(p.from, p.to, (i32)s_UNIT_CIRCLE_VAO_TRIANGLES.getVertices().size());
            g->pushTransform();
            {
                g->scale(p.scale, p.scale);
                g->translate(p.translation.x, p.translation.y);

                g->drawVAO(p.vao);
            }
            g->popTransform();

            if(p.alwaysPoints.size() > 0)
                drawFillSliderBodyPeppy(p.screenRect, p.alwaysPoints, s_UNIT_CIRCLE_VAO_BAKED,
                                        p.hitcircleDiameter / 2.0f, 0, p.alwaysPoints.size());

            if(!useGradientImage) {
                s_BLEND_SHADER->disable();
            } else {
                gradient->unbind();
            }

            if(p.doDisableRenderTarget) p.rt->disable();
        }
        g->setBlending(true);
        g->setDepthBuffer(false);
    }

    // optional bounds performance optimization to reduce rt blending overdraw
    if(p.bounds != vec4{}) {
        const f32 pixelFudge = 2.0f;
        s_fBoundingBoxMinX = std::max(0.0f, p.bounds.x - p.hitcircleDiameter / 2.0f - pixelFudge);
        s_fBoundingBoxMaxX = std::min(p.screenRect.x, p.bounds.z + p.hitcircleDiameter / 2.0f + pixelFudge);
        s_fBoundingBoxMinY = std::max(0.0f, p.bounds.y - p.hitcircleDiameter / 2.0f - pixelFudge);
        s_fBoundingBoxMaxY = std::min(p.screenRect.y, p.bounds.w + p.hitcircleDiameter / 2.0f + pixelFudge);
    } else {
        s_fBoundingBoxMinX = 0.0f;
        s_fBoundingBoxMaxX = p.screenRect.x;
        s_fBoundingBoxMinY = 0.0f;
        s_fBoundingBoxMaxY = p.screenRect.y;
    }

    if(p.doDrawSliderFrameBufferToScreen) {
        if(sdf) {
            // shade the accumulated field while compositing it to the screen: colors are only needed here, and
            // the slider's fade rides along as a uniform instead of the framebuffer color modulation
            preDrawColorSetup(s_COMPOSITE_SHADER, p.skinSettings, nullptr, p.sliderTimeForRainbow, p.colorRGBMultiplier,
                              p.undimmedColor);
            s_COMPOSITE_SHADER->setUniform1f("alphaMultiplier", p.alpha * cv::slider_alpha_multiplier.getFloat());
            p.rt->drawRect((i32)s_fBoundingBoxMinX, (i32)s_fBoundingBoxMinY,
                           (i32)(s_fBoundingBoxMaxX - s_fBoundingBoxMinX),
                           (i32)(s_fBoundingBoxMaxY - s_fBoundingBoxMinY));
            s_COMPOSITE_SHADER->disable();
        } else {
            p.rt->setColor(argb(p.alpha * cv::slider_alpha_multiplier.getFloat(), 1.0f, 1.0f, 1.0f));
            p.rt->drawRect((i32)s_fBoundingBoxMinX, (i32)s_fBoundingBoxMinY,
                           (i32)(s_fBoundingBoxMaxX - s_fBoundingBoxMinX),
                           (i32)(s_fBoundingBoxMaxY - s_fBoundingBoxMinY));
        }
    }
}

namespace {  // static

void drawFillSliderBodyPeppy(vec2 screen, std::span<const vec2> points, VertexArrayObject *circleMesh, f32 radius,
                             uSz drawFromIndex, uSz drawUpToIndex) {
    g->pushTransform();
    {
        // now, translate and draw the master vao for every curve point
        f32 startX = 0.0f;
        f32 startY = 0.0f;
        for(uSz i = drawFromIndex; i < drawUpToIndex; ++i) {
            const f32 x = points[i].x;
            const f32 y = points[i].y;

            // fuck oob sliders
            if(x < -radius * 2 || x > screen.x + radius * 2 || y < -radius * 2 || y > screen.y + radius * 2) continue;

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

void checkUpdateVars(f32 hitcircleDiameter) {
    // static globals

    if(!env->usingGL()) {
        // NOTE: compensate for zn/zf Camera::buildMatrixOrtho2DDXLH() differences compared to OpenGL
        if(s_MESH_CENTER_HEIGHT > 0.0f) s_MESH_CENTER_HEIGHT = -s_MESH_CENTER_HEIGHT;
    }

    // build shaders and circle mesh. the cone shader serves the legacy/dynamic-mod path and the cone fallback
    if(s_BLEND_SHADER == nullptr)  // only do this once
        s_BLEND_SHADER = resourceManager->createShaderAuto("slider");
    if(s_FIELD_SHADER == nullptr) s_FIELD_SHADER = resourceManager->createShaderAuto("sliderField");
    if(s_COMPOSITE_SHADER == nullptr) s_COMPOSITE_SHADER = resourceManager->createShaderAuto("sliderComposite");

    const i32 subdivisions = cv::slider_body_unit_circle_subdivisions.getInt();
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

            for(i32 j = 0; j < subdivisions; ++j) {
                const f32 phase = (f32)j * PI_F * 2.0f / (f32)subdivisions;

                // texture coordinates
                s_UNIT_CIRCLE.push_back(0.0f);
                s_UNIT_CIRCLE.push_back(0.0f);

                // positon
                s_UNIT_CIRCLE.push_back((f32)std::sin(phase));
                s_UNIT_CIRCLE.push_back((f32)std::cos(phase));
                s_UNIT_CIRCLE.push_back(0.0f);
            }

            // texture coordinates
            s_UNIT_CIRCLE.push_back(0.0f);
            s_UNIT_CIRCLE.push_back(0.0f);

            // positon
            s_UNIT_CIRCLE.push_back((f32)std::sin(0.0f));
            s_UNIT_CIRCLE.push_back((f32)std::cos(0.0f));
            s_UNIT_CIRCLE.push_back(0.0f);
        }
    }

    // build vaos
    if(s_UNIT_CIRCLE_VAO_BAKED == nullptr)
        s_UNIT_CIRCLE_VAO_BAKED = resourceManager->createVertexArrayObject(DrawPrimitive::TRIANGLE_FAN);

    // (re-)generate master circle mesh (centered) if the size changed
    // dynamic mods like minimize or wobble have to use the legacy renderer anyway, since the slider shape may change
    // every frame
    if(hitcircleDiameter != s_UNIT_CIRCLE_VAO_DIAMETER) {
        const f32 radius = hitcircleDiameter / 2.0f;

        s_UNIT_CIRCLE_VAO_BAKED->release();

        // triangle fan
        s_UNIT_CIRCLE_VAO_DIAMETER = hitcircleDiameter;
        s_UNIT_CIRCLE_VAO.clear();
        for(i32 i = 0; i < s_UNIT_CIRCLE.size() / 5; i++) {
            vec3 vertexPos = vec3((radius * s_UNIT_CIRCLE[i * 5 + 2]), (radius * s_UNIT_CIRCLE[i * 5 + 3]),
                                  s_UNIT_CIRCLE[i * 5 + 4]);
            vec2 vertexTexcoord = vec2(s_UNIT_CIRCLE[i * 5 + 0], s_UNIT_CIRCLE[i * 5 + 1]);

            s_UNIT_CIRCLE_VAO.addVertex(vertexPos);
            s_UNIT_CIRCLE_VAO.addTexcoord(vertexTexcoord);

            s_UNIT_CIRCLE_VAO_BAKED->addVertex(vertexPos);
            s_UNIT_CIRCLE_VAO_BAKED->addTexcoord(vertexTexcoord);
        }

        resourceManager->loadResource(s_UNIT_CIRCLE_VAO_BAKED);

        // pure triangles (needed for VertexArrayObject, because we can't merge multiple triangle fan meshes into one
        // VertexArrayObject)
        s_UNIT_CIRCLE_VAO_TRIANGLES.clear();
        vec3 startVertex =
            vec3((radius * s_UNIT_CIRCLE[0 * 5 + 2]), (radius * s_UNIT_CIRCLE[0 * 5 + 3]), s_UNIT_CIRCLE[0 * 5 + 4]);
        vec2 startUV = vec2(s_UNIT_CIRCLE[0 * 5 + 0], s_UNIT_CIRCLE[0 * 5 + 1]);
        for(i32 i = 1; i < s_UNIT_CIRCLE.size() / 5 - 1; i++) {
            // center
            s_UNIT_CIRCLE_VAO_TRIANGLES.addVertex(startVertex);
            s_UNIT_CIRCLE_VAO_TRIANGLES.addTexcoord(startUV);

            // pizza slice edge 1
            s_UNIT_CIRCLE_VAO_TRIANGLES.addVertex(vec3((radius * s_UNIT_CIRCLE[i * 5 + 2]),
                                                       (radius * s_UNIT_CIRCLE[i * 5 + 3]), s_UNIT_CIRCLE[i * 5 + 4]));
            s_UNIT_CIRCLE_VAO_TRIANGLES.addTexcoord(vec2(s_UNIT_CIRCLE[i * 5 + 0], s_UNIT_CIRCLE[i * 5 + 1]));

            // pizza slice edge 2
            s_UNIT_CIRCLE_VAO_TRIANGLES.addVertex(vec3((radius * s_UNIT_CIRCLE[(i + 1) * 5 + 2]),
                                                       (radius * s_UNIT_CIRCLE[(i + 1) * 5 + 3]),
                                                       s_UNIT_CIRCLE[(i + 1) * 5 + 4]));
            s_UNIT_CIRCLE_VAO_TRIANGLES.addTexcoord(
                vec2(s_UNIT_CIRCLE[(i + 1) * 5 + 0], s_UNIT_CIRCLE[(i + 1) * 5 + 1]));
        }

        // SDF snake-head disc: a centered 2r quad; corner texcoords (+/-1) make length(texcoord) the radial
        // distance, so the sliderField shader renders it as an exact disc that matches the baked body's encoding
        s_UNIT_DISC_QUAD_SDF.clear();
        {
            const std::array<vec2, 4> corners{vec2{-1, -1}, vec2{-1, 1}, vec2{1, 1}, vec2{1, -1}};
            for(i32 k : std::array<i32, 6>{0, 1, 2, 0, 2, 3}) {
                s_UNIT_DISC_QUAD_SDF.addVertex(vec3{corners[k].x * radius, corners[k].y * radius, 0.0f});
                s_UNIT_DISC_QUAD_SDF.addTexcoord(corners[k]);
            }
        }
    }
}

// helper function to update color uniforms
void updateColorUniforms(Shader *shader, Color borderColor, Color bodyColor) {
    assert(!!shader);
    if(s_uniformCache.lastBorderColor != borderColor) {
        shader->setUniform3f("colBorder", borderColor.Rf(), borderColor.Gf(), borderColor.Bf());
        s_uniformCache.lastBorderColor = borderColor;
    }

    if(s_uniformCache.lastBodyColor != bodyColor) {
        shader->setUniform3f("colBody", bodyColor.Rf(), bodyColor.Gf(), bodyColor.Bf());
        s_uniformCache.lastBodyColor = bodyColor;
    }
}

void updateConfigUniforms(Shader *shader) {
    assert(!!shader);
    // a program switch (cone <-> SDF) leaves the new program's uniforms unset, so force a full re-push
    if(s_uniformCache.cacheShader != shader) {
        s_uniformCache = UniformCache{};
        s_uniformCache.cacheShader = shader;
    } else if(!s_uniformCache.needsConfigUpdate) {
        return;
    }

    const i32 newStyle = cv::slider_osu_next_style.getBool() ? 1 : 0;
    const f32 newBodyAlpha = cv::slider_body_alpha_multiplier.getFloat();
    const f32 newBodySat = cv::slider_body_color_saturation.getFloat();
    const f32 newBorderSize = cv::slider_border_size_multiplier.getFloat();
    const f32 newBorderFeather = cv::slider_border_feather.getFloat();

    if(s_uniformCache.style != newStyle) {
        shader->setUniform1i("style", newStyle);
        s_uniformCache.style = newStyle;
    }

    if(s_uniformCache.bodyAlphaMultiplier != newBodyAlpha) {
        shader->setUniform1f("bodyAlphaMultiplier", newBodyAlpha);
        s_uniformCache.bodyAlphaMultiplier = newBodyAlpha;
    }

    if(s_uniformCache.bodyColorSaturation != newBodySat) {
        shader->setUniform1f("bodyColorSaturation", newBodySat);
        s_uniformCache.bodyColorSaturation = newBodySat;
    }

    if(s_uniformCache.borderSizeMultiplier != newBorderSize) {
        shader->setUniform1f("borderSizeMultiplier", newBorderSize);
        s_uniformCache.borderSizeMultiplier = newBorderSize;
    }

    if(s_uniformCache.borderFeather != newBorderFeather) {
        shader->setUniform1f("borderFeather", newBorderFeather);
        s_uniformCache.borderFeather = newBorderFeather;
    }

    s_uniformCache.needsConfigUpdate = false;
}

static CONSTINIT VertexArrayObject quadDebugVAO{DrawPrimitive::QUADS};

void drawDebugLegacy(const Image *hitcircleImage, std::span<const vec2> points, f32 hitcircleDiameter,
                     Color undimmedColor, f32 colorRGBMultiplier, f32 alpha, uSz drawFromIndex, uSz drawUpToIndex) {
    f32 circleImageScale = hitcircleDiameter;
    f32 width{0.f}, height{0.f};
    if(hitcircleImage) {
        circleImageScale /= (f32)hitcircleImage->getWidth();
        width = (f32)hitcircleImage->getWidth();
        height = (f32)hitcircleImage->getHeight();
    }

    const f32 circleImageScaleInv = (1.0f / circleImageScale);

    const f32 x = (-width / 2.0f);
    const f32 y = (-height / 2.0f);
    const f32 z = -1.0f;

    g->pushTransform();
    {
        g->scale(circleImageScale, circleImageScale);

        const Color dimmedColor = Colors::scale(undimmedColor, colorRGBMultiplier);

        g->setColor(Color(dimmedColor).setA(alpha * cv::slider_alpha_multiplier.getFloat()));

        if(hitcircleImage) hitcircleImage->bind();
        {
            for(uSz i = drawFromIndex; i < drawUpToIndex; i++) {
                const vec2 point = points[i] * circleImageScaleInv;

                quadDebugVAO.clear();
                {
                    quadDebugVAO.addTexcoord(0, 0);
                    quadDebugVAO.addVertex(point.x + x, point.y + y, z);

                    quadDebugVAO.addTexcoord(0, 1);
                    quadDebugVAO.addVertex(point.x + x, point.y + y + height, z);

                    quadDebugVAO.addTexcoord(1, 1);
                    quadDebugVAO.addVertex(point.x + x + width, point.y + y + height, z);

                    quadDebugVAO.addTexcoord(1, 0);
                    quadDebugVAO.addVertex(point.x + x + width, point.y + y, z);
                }
                g->drawVAO(&quadDebugVAO);
            }
        }
        if(hitcircleImage) hitcircleImage->unbind();
    }
    g->popTransform();
    return;
}

void drawDebugVAO(const Image *hitcircleImage, VertexArrayObject *vao, vec2 translation, f32 scale, f32 from, f32 to,
                  Color undimmedColor, f32 colorRGBMultiplier, f32 alpha) {
    const Color dimmedColor = Colors::scale(undimmedColor, colorRGBMultiplier);

    g->setColor(Color(dimmedColor).setA(alpha * cv::slider_alpha_multiplier.getFloat()));

    if(hitcircleImage) hitcircleImage->bind();

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

    if(hitcircleImage) hitcircleImage->unbind();

    return;
}

}  // namespace

}  // namespace SliderRenderer
