// Copyright (c) 2016, PG, All rights reserved.
#include "SliderRenderer.h"

#include "OsuConVars.h"
#include "Engine.h"
#include "GameRules.h"
#include "Image.h"
#include "RenderTarget.h"
#include "ResourceManager.h"
#include "Shader.h"
#include "Skin.h"
#include "VertexArrayObject.h"
#include "Logging.h"
#include "Graphics.h"
#include "Matrices.h"
#include "Rect.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include <cassert>

namespace SliderRenderer {

namespace {  // static namespace

Shader *s_FIELD_SHADER{nullptr};      // accumulates the body's distance field into the framebuffer (MAX blend)
Shader *s_COMPOSITE_SHADER{nullptr};  // shades the accumulated field once per pixel while drawing it to screen
Shader *s_GRADIENT_SHADER{nullptr};   // the same with the skin's slidergradient.png (slider_use_gradient_image)

// analytic SDF body: each kept curve point emits one equal-size block = a slab quad (6 verts) + a cap/join fan
// (SDF_FAN_SLICES triangles). these must stay in lockstep: if VERTS_PER_SDF_BLOCK doesn't match the emitted count,
// setDrawPercent() snake-snapping rounds the draw range to the wrong boundary and clips the static end cap
constexpr i32 SDF_FAN_SLICES{4};
constexpr i32 VERTS_PER_SDF_BLOCK{6 + SDF_FAN_SLICES * 3};

// SDF disc (snake heads, per-point bodies): a centered 2r quad whose corner texcoords (+/-1) make length(texcoord) the
// radial distance, matching the baked body's encoding
static CONSTINIT VertexArrayObject s_DISC_QUAD_SDF{DrawPrimitive::TRIANGLES};
f32 s_DISC_QUAD_RADIUS{0.0f};

bool visible(const Body &b) {
    return cv::slider_alpha_multiplier.getFloat() > 0.0f && b.alpha > 0.0f && (b.mesh == nullptr || b.mesh->vao);
}

// debug modes draw the shape directly, without a field
bool debug_drawn(const Body &b) {
    return b.mesh ? cv::slider_debug_draw_square_vao.getBool() : cv::slider_debug_draw.getBool();
}

// [first, last) of the points the body draws
std::pair<uSz, uSz> drawn_points(const Body &b) {
    const uSz n = b.points.size();
    return {std::clamp<uSz>((uSz)std::round((f64)n * b.from), 0UZ, n),
            std::clamp<uSz>((uSz)std::round((f64)n * b.to), 0UZ, n)};
}

// fuck oob sliders
bool is_offscreen(vec2 point, f32 radius, vec2 target) {
    return point.x < -radius * 2 || point.x > target.x + radius * 2 || point.y < -radius * 2 ||
           point.y > target.y + radius * 2;
}

// what draw() composites: everything the body's field can cover, clamped to the target
McIRect composite_area(const Body &b, vec2 target) {
    vec2 lo{(std::numeric_limits<f32>::max)()}, hi{std::numeric_limits<f32>::lowest()};
    if(b.mesh && b.mesh->bounds.x <= b.mesh->bounds.z) {
        const vec2 corner0 = vec2{b.mesh->bounds.x, b.mesh->bounds.y} * b.scale + b.translation;
        const vec2 corner1 = vec2{b.mesh->bounds.z, b.mesh->bounds.w} * b.scale + b.translation;
        lo = vec::min(corner0, corner1);
        hi = vec::max(corner0, corner1);
    }
    const f32 radius = b.hitcircleDiameter / 2.0f;
    const auto addDisc = [&](vec2 point) {
        if(is_offscreen(point, radius, target)) return;
        lo = vec::min(lo, point - radius);
        hi = vec::max(hi, point + radius);
    };
    const auto [first, last] = drawn_points(b);
    for(uSz i = first; i < last; ++i) addDisc(b.points[i]);
    for(const vec2 point : b.alwaysPoints) addDisc(point);

    const f32 pixelFudge = 2.0f;
    const i32 minX = (i32)std::floor(std::clamp(lo.x - pixelFudge, 0.0f, target.x));
    const i32 minY = (i32)std::floor(std::clamp(lo.y - pixelFudge, 0.0f, target.y));
    const i32 maxX = (i32)std::ceil(std::clamp(hi.x + pixelFudge, 0.0f, target.x));
    const i32 maxY = (i32)std::ceil(std::clamp(hi.y + pixelFudge, 0.0f, target.y));
    return {minX, minY, maxX - minX, maxY - minY};
}

void draw_discs(std::span<const vec2> points, f32 radius, vec2 target) {
    if(radius != s_DISC_QUAD_RADIUS) {
        s_DISC_QUAD_RADIUS = radius;
        s_DISC_QUAD_SDF.clear();
        const std::array<vec2, 4> corners{vec2{-1, -1}, vec2{-1, 1}, vec2{1, 1}, vec2{1, -1}};
        for(i32 k : std::array<i32, 6>{0, 1, 2, 0, 2, 3}) {
            s_DISC_QUAD_SDF.addVertex(corners[k] * radius);
            s_DISC_QUAD_SDF.addTexcoord(corners[k]);
        }
    }

    g->pushTransform();
    {
        // now, translate and draw the disc for every curve point
        vec2 prev{0.0f};
        for(const vec2 point : points) {
            if(is_offscreen(point, radius, target)) continue;

            g->translate(point.x - prev.x, point.y - prev.y, 0);
            g->drawVAO(&s_DISC_QUAD_SDF);

            prev = point;
        }
    }
    g->popTransform();
}

// draws the body's geometry into its field
void draw_shape(const Body &b, vec2 target) {
    const f32 radius = b.hitcircleDiameter / 2.0f;
    if(b.mesh) {
        b.mesh->vao->setDrawPercent(b.from, b.to, VERTS_PER_SDF_BLOCK);
        g->pushTransform();
        {
            g->scale(b.scale, b.scale);
            g->translate(b.translation.x, b.translation.y);
            /// g->scale(scaleToApplyAfterTranslationX, scaleToApplyAfterTranslationY); // aspire slider
            /// distortions

            g->drawVAO(b.mesh->vao.get());
        }
        g->popTransform();
    } else {
        const auto [first, last] = drawn_points(b);
        draw_discs(b.points.subspan(first, last - first), radius, target);
    }

    // the moving snake ends: discs with the same texcoord encoding as the rest
    if(!b.alwaysPoints.empty()) draw_discs(b.alwaysPoints, radius, target);
}

static CONSTINIT VertexArrayObject s_quadDebugVAO{DrawPrimitive::QUADS};

// draws a hitcircle image at every drawn point, or the mesh (of squares when baked in debug mode)
void draw_debug(const Body &b) {
    const Image *hitcircleImage = b.skinSettings.i_hitcircle;
    const Color dimmedColor = Colors::scale(b.undimmedColor, b.colorRGBMultiplier);

    g->setColor(Color(dimmedColor).setA(b.alpha * cv::slider_alpha_multiplier.getFloat()));

    if(hitcircleImage) hitcircleImage->bind();
    g->pushTransform();
    if(b.mesh) {
        b.mesh->vao->setDrawPercent(b.from, b.to, 6);  // HACKHACK: hardcoded magic number

        g->scale(b.scale, b.scale);
        g->translate(b.translation.x, b.translation.y);

        g->drawVAO(b.mesh->vao.get());
    } else {
        f32 circleImageScale = b.hitcircleDiameter;
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

        g->scale(circleImageScale, circleImageScale);

        const auto [first, last] = drawn_points(b);
        for(uSz i = first; i < last; i++) {
            const vec2 point = b.points[i] * circleImageScaleInv;

            s_quadDebugVAO.clear();
            {
                s_quadDebugVAO.addTexcoord(0, 0);
                s_quadDebugVAO.addVertex(point.x + x, point.y + y, z);

                s_quadDebugVAO.addTexcoord(0, 1);
                s_quadDebugVAO.addVertex(point.x + x, point.y + y + height, z);

                s_quadDebugVAO.addTexcoord(1, 1);
                s_quadDebugVAO.addVertex(point.x + x + width, point.y + y + height, z);

                s_quadDebugVAO.addTexcoord(1, 0);
                s_quadDebugVAO.addVertex(point.x + x + width, point.y + y, z);
            }
            g->drawVAO(&s_quadDebugVAO);
        }
    }
    g->popTransform();
    if(hitcircleImage) hitcircleImage->unbind();
}

Color get_rainbow_color(i32 rainbowTime, f32 initOffset) {
    const f64 frequency = .3f;
    const f64 time = engine->getTime() * 20.;

    const Channel red = (Channel)(std::sin(frequency * (time * initOffset) + 0 + rainbowTime) * 127.) + 128;
    const Channel green = (Channel)(std::sin(frequency * (time * initOffset) + 2 + rainbowTime) * 127.) + 128;
    const Channel blue = (Channel)(std::sin(frequency * (time * initOffset) + 4 + rainbowTime) * 127.) + 128;

    return rgb(red, green, blue);
}

forceinline Color get_body_color(const SkinSettings &settings, bool doRainbow, i32 rainbowTime, f32 colorRGBMultiplier,
                                 Color undimmedColor) {
    if(doRainbow) {
        return get_rainbow_color(rainbowTime, 1.5f);
    } else {
        const Color undimmedBodyColor =
            settings.o_slider_track_overridden ? settings.c_slider_track_override : undimmedColor;

        return Colors::scale(undimmedBodyColor, colorRGBMultiplier);
    }
}

forceinline Color get_border_color(const SkinSettings &settings, bool doRainbow, i32 rainbowTime,
                                   f32 colorRGBMultiplier, Color undimmedColor) {
    if(doRainbow) {
        return get_rainbow_color(rainbowTime, 1.f);
    } else {
        const Color undimmedBorderColor =
            cv::slider_border_tint_combo_color.getBool() ? undimmedColor : settings.c_slider_border;

        return Colors::scale(undimmedBorderColor, colorRGBMultiplier);
    }
}

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
};

static CONSTINIT UniformCache s_uniformCache{};

// helper function to update color uniforms (after ->enable-ing the shader)
void update_shader_color_uniforms(Shader *shader, Color borderColor, Color bodyColor) {
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

// check if convar-dependent uniforms need to be updated (after ->enable-ing the shader)
void update_shader_config_uniforms(Shader *shader) {
    assert(!!shader);
    if(!s_uniformCache.needsConfigUpdate) return;

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

void set_shader_channel_uniform(Shader *shader, u8 channel) {
    shader->setUniform4f("channel", channel == 0 ? 1.0f : 0.0f, channel == 1 ? 1.0f : 0.0f, channel == 2 ? 1.0f : 0.0f,
                         channel == 3 ? 1.0f : 0.0f);
}

// a body whose field renders in the active batch. sources reuse the buffers behind their spans, so its points are
// copied to the batch (see bodyOf())
struct Entry {
    const BodySource *source;
    Body body;  // without its spans
    uSz pointsAt;
    uSz numPoints;
    uSz numAlwaysPoints;
    McIRect area;   // what its composite covers
    u8 channel{0};  // the render target channel its field goes into
};

// the active Batch, kept around for the capacity
struct BatchState {
    RenderTarget *rt{nullptr};
    std::vector<Entry> entries;  // queue order
    std::vector<vec2> points;
    // entries[residentBegin, residentEnd) have their fields in rt, the ones after are pending
    uSz residentBegin{0};
    uSz residentEnd{0};
};
BatchState s_batch;

// nullopt when the body's field would be entirely off-screen
std::optional<Entry> make_entry(const BodySource &source, const Body &b) {
    const McIRect area = composite_area(b, s_batch.rt->getSize());
    if(area.getWidth() <= 0 || area.getHeight() <= 0) return std::nullopt;

    Entry entry{.source = &source,
                .body = b,
                .pointsAt = s_batch.points.size(),
                .numPoints = b.points.size(),
                .numAlwaysPoints = b.alwaysPoints.size(),
                .area = area};
    entry.body.points = {};
    entry.body.alwaysPoints = {};
    s_batch.points.insert(s_batch.points.end(), b.points.begin(), b.points.end());
    s_batch.points.insert(s_batch.points.end(), b.alwaysPoints.begin(), b.alwaysPoints.end());
    return entry;
}

Body body_of(const Entry &entry) {
    Body b = entry.body;
    b.points = {s_batch.points.data() + entry.pointsAt, entry.numPoints};
    b.alwaysPoints = {s_batch.points.data() + entry.pointsAt + entry.numPoints, entry.numAlwaysPoints};
    return b;
}

// the index of the source's entry in [from, to), to when there's none
uSz find_entry(const BodySource &source, uSz from, uSz to) {
    while(from < to && s_batch.entries[from].source != &source) ++from;
    return from;
}

// gives the entries from first on their channels until one doesn't fit, returns its index: overlapping fields need
// different channels
uSz assign_channels(uSz first) {
    uSz end = first;
    for(; end < s_batch.entries.size(); ++end) {
        Entry &entry = s_batch.entries[end];

        u32 taken = 0;
        for(uSz i = first; i < end; ++i)
            if(s_batch.entries[i].area.intersects(entry.area)) taken |= 1u << s_batch.entries[i].channel;

        const u32 free = ~taken & 0xfu;
        if(free == 0) break;
        entry.channel = (u8)std::countr_zero(free);
    }
    return end;
}

// renders the fields of the entries from first on, as many as fit into one pass
void render_fields(uSz first) {
    s_batch.residentBegin = first;
    s_batch.residentEnd = assign_channels(first);

    const vec2 target = s_batch.rt->getSize();
    const bool blending = g->getBlending();
    const DrawBlendMode blendMode = g->getBlendMode();
    g->pushTransform();
    {
        // the bodies' own transforms place them, wherever draw() was called from
        Matrix4 identity;
        g->setWorldMatrix(identity);

        s_batch.rt->enable(/*clear=*/false);

        // only the composite areas have to start out empty: clearing the whole target instead makes tile-based GPUs
        // clear and store every tile of it
        g->setBlending(false);
        g->setColor(0);
        for(uSz i = first; i < s_batch.residentEnd; ++i) {
            const McIRect &area = s_batch.entries[i].area;
            g->fillRect(area.getX(), area.getY(), area.getWidth(), area.getHeight());
        }
        g->setBlending(true);

        // accumulate the distance fields: each primitive MAX-blends its radial gradient into its body's channel, so
        // the union needs no depth buffer at all and self-overlapping geometry (retraced/aspire curves stack
        // thousands of blocks on the same pixels) costs only trivial blended fills. the expensive gradient shading
        // runs exactly once per covered pixel in composite().
        g->setBlendMode(DrawBlendMode::MAX);
        s_FIELD_SHADER->enable();
        for(uSz i = first; i < s_batch.residentEnd; ++i) {
            set_shader_channel_uniform(s_FIELD_SHADER, s_batch.entries[i].channel);
            draw_shape(body_of(s_batch.entries[i]), target);
        }
        s_FIELD_SHADER->disable();

        s_batch.rt->disable();
    }
    g->popTransform();
    g->setBlending(blending);
    g->setBlendMode(blendMode);
}

// shades the accumulated field while compositing it: colors are only needed here, and the body's fade rides along as a
// uniform instead of the framebuffer color modulation
void composite(const Entry &entry) {
    const Body &b = entry.body;
    const McIRect &area = entry.area;
    const f32 alpha = b.alpha * cv::slider_alpha_multiplier.getFloat();

    // the gradient image takes the place of the dynamic colors and border
    if(const Image *gradient = b.skinSettings.i_slider_gradient;
       cv::slider_use_gradient_image.getBool() && gradient && gradient->isReady()) {
        s_GRADIENT_SHADER->enable();
        {
            s_GRADIENT_SHADER->setUniform1f("colorRGBMultiplier", b.colorRGBMultiplier);
            s_GRADIENT_SHADER->setUniform1f("alphaMultiplier", alpha);
            set_shader_channel_uniform(s_GRADIENT_SHADER, entry.channel);
            gradient->bind(1);  // tex1, drawRect() binds the field to unit 0
            s_batch.rt->drawRect(area.getX(), area.getY(), area.getWidth(), area.getHeight());
            gradient->unbind();
        }
        s_GRADIENT_SHADER->disable();
        return;
    }

    const bool doRainbow = cv::slider_rainbow.getBool();
    const Color borderColor =
        get_border_color(b.skinSettings, doRainbow, b.sliderTimeForRainbow, b.colorRGBMultiplier, b.undimmedColor);
    const Color bodyColor =
        get_body_color(b.skinSettings, doRainbow, b.sliderTimeForRainbow, b.colorRGBMultiplier, b.undimmedColor);

    s_COMPOSITE_SHADER->enable();
    {
        update_shader_config_uniforms(s_COMPOSITE_SHADER);
        update_shader_color_uniforms(s_COMPOSITE_SHADER, borderColor, bodyColor);
        s_COMPOSITE_SHADER->setUniform1f("alphaMultiplier", alpha);
        set_shader_channel_uniform(s_COMPOSITE_SHADER, entry.channel);
        s_batch.rt->drawRect(area.getX(), area.getY(), area.getWidth(), area.getHeight());
    }
    s_COMPOSITE_SHADER->disable();
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

Mesh generateMesh(vec2 screenRect, std::span<const vec2> points, f32 hitcircleDiameter, bool skipOOBPoints) {
    Mesh mesh{.vao{g->createVertexArrayObject(DrawPrimitive::TRIANGLES, DrawUsageType::STATIC,
                                              /*keepInSystemMemory=*/false)}};
    VertexArrayObject *vao = mesh.vao.get();

    // every point's disc, the skipped OOB ones too: the slab of the point after one still reaches back to it
    const f32 radius = hitcircleDiameter / 2.0f;
    mesh.bounds = {(std::numeric_limits<f32>::max)(), (std::numeric_limits<f32>::max)(),
                   std::numeric_limits<f32>::lowest(), std::numeric_limits<f32>::lowest()};
    for(const vec2 point : points) {
        mesh.bounds = {std::min(mesh.bounds.x, point.x - radius), std::min(mesh.bounds.y, point.y - radius),
                       std::max(mesh.bounds.z, point.x + radius), std::max(mesh.bounds.w, point.y + radius)};
    }

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

    if(cv::slider_debug_draw_square_vao.getBool()) {  // debug
        const vec3 xOffset = vec3(hitcircleDiameter, 0, 0);
        const vec3 yOffset = vec3(0, hitcircleDiameter, 0);

        for(const auto &point : points) {
            if(skipOOBPoints && isOOB(point)) continue;

            const vec3 topLeft = vec3(point.x, point.y, 0) - xOffset / 2.0f - yOffset / 2.0f;
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
    } else {  // analytic distance-field body
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
            meshVerts.emplace_back(p.x, p.y, 0.0f);
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
            // mapping, but only on broken/aspire maps, where baking + transforming millions of offscreen blocks
            // every frame is the greater evil (the OOB bounds are generous enough that a cut end can never reach
            // the viewport)
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
    }

    if(vao->getNumVertices() > 0) {
        vao->loadAsync();
        vao->load();
    } else {
        debugLog("ERROR: Zero triangles!");
    }

    return mesh;
}

Batch::Batch(RenderTarget *rt) {
    assert(s_batch.rt == nullptr && "nested SliderRenderer::Batch");
    s_batch.rt = rt;

    if(s_FIELD_SHADER == nullptr) s_FIELD_SHADER = resourceManager->createShaderAuto("sliderField");
    if(s_COMPOSITE_SHADER == nullptr) s_COMPOSITE_SHADER = resourceManager->createShaderAuto("sliderComposite");
    if(s_GRADIENT_SHADER == nullptr) s_GRADIENT_SHADER = resourceManager->createShaderAuto("sliderGradient");
}

Batch::~Batch() {
    s_batch.rt = nullptr;
    s_batch.entries.clear();
    s_batch.points.clear();
    s_batch.residentBegin = s_batch.residentEnd = 0;
}

void Batch::queue(const BodySource &source) {
    // draw() asks again for the bodies that aren't queued here: debug bodies draw without a field
    const std::optional<Body> body = source.getBody();
    if(!body || !visible(*body) || debug_drawn(*body)) return;

    if(const std::optional<Entry> entry = make_entry(source, *body)) s_batch.entries.push_back(*entry);
}

void draw(const BodySource &source) {
    assert(s_batch.rt != nullptr && "SliderRenderer::draw() outside of a Batch");
    if(s_batch.rt == nullptr) return;

    std::vector<Entry> &entries = s_batch.entries;
    uSz i = find_entry(source, s_batch.residentBegin, s_batch.residentEnd);
    if(i == s_batch.residentEnd) {
        // its field renders now, with the ones queued after it. an unqueued body goes in front of them
        i = find_entry(source, s_batch.residentEnd, entries.size());
        if(i == entries.size()) {
            const std::optional<Body> body = source.getBody();
            if(!body || !visible(*body)) return;
            if(debug_drawn(*body)) {
                draw_debug(*body);
                return;
            }

            const std::optional<Entry> entry = make_entry(source, *body);
            if(!entry) return;
            i = s_batch.residentEnd;
            entries.insert(entries.begin() + (sSz)i, *entry);
        }
        render_fields(i);
    }
    composite(entries[i]);
}

}  // namespace SliderRenderer
