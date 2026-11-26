#pragma once
// Copyright (c) 2016, PG, All rights reserved.

#include "noinclude.h"
#include "Vectors.h"
#include "Color.h"

#include <memory>
#include <optional>
#include <span>

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

// a body shape baked once, for curves that only move and scale while they're on screen
struct Mesh {
    std::unique_ptr<VertexArrayObject> vao;
    vec4 bounds{};  // what its field covers (minX, minY, maxX, maxY), minX > maxX when nothing
};

Mesh generateMesh(vec2 screenRect, std::span<const vec2> points, f32 hitcircleDiameter, bool skipOOBPoints = true);

struct Body final {
    // a mesh, or screen-space curve points drawn as one disc each (curves that change shape every frame)
    const Mesh *mesh{nullptr};
    vec2 translation{0.0f};  // mesh -> screen
    f32 scale{1.0f};
    std::span<const vec2> points{};
    std::span<const vec2> alwaysPoints{};  // discs drawn regardless of from/to (the moving snake ends)
    f32 hitcircleDiameter{0.0f};
    f32 from = 0.0f;  // the drawn part of the curve
    f32 to = 1.0f;

    SkinSettings skinSettings{};
    Color undimmedColor = 0xffffffff;
    std::optional<Color> borderColor{};  // instead of the skin colour (or the combo colour, or rainbow)
    f32 colorRGBMultiplier = 1.0f;
    f32 alpha = 1.0f;
    i32 sliderTimeForRainbow = 0;
};

// draws slider bodies through a render target. add() the bodies about to be drawn, in draw order, then draw() each one
// where it goes among the other draws: the first draw() renders the fields of as many of them as fit into the target's
// channels in one pass, instead of switching render targets for every body
class Batch {
    NOCOPY_NOMOVE(Batch)
   public:
    explicit Batch(RenderTarget *rt);  // target-sized, untouched by anything else while the batch lives
    ~Batch();

    // copies the body and its points, so the spans only have to stay valid until it returns.
    // id is what draw() takes, unique within the batch
    void add(u32 id, const Body &body);
    // shades the field of the body added as id while compositing it here, rendering the field first if needed.
    // nothing if no body was added as id
    void draw(u32 id);
};

// for convar callbacks
void onUniformConfigChanged();
};  // namespace SliderRenderer
