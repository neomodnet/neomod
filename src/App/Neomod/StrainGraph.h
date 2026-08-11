#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "noinclude.h"
#include "types.h"
#include "Vectors.h"

#include <span>

class VertexArrayObject;

// shared aim/speed strain bar graph (scrubbing timeline + song browser overlay).
class StrainGraph final {
    NOCOPY_NOMOVE(StrainGraph)
   public:
    StrainGraph();
    ~StrainGraph();

    // draws a strain graph with bars growing upwards from the baseline at pos (bottom-left corner)
    // and the highest strain stack reaching heightPx
    void draw(std::span<const f32> aimStrains, std::span<const f32> speedStrains, f64 stars, vec2 pos, f32 widthPx,
              f32 heightPx, f32 alpha, bool drawAim = true, bool drawSpeed = true);

   private:
    void rebuild(std::span<const f32> aimStrains, std::span<const f32> speedStrains, i32 widthPx, bool drawAim,
                 bool drawSpeed);

    VertexArrayObject *aimVao{nullptr};
    VertexArrayObject *speedVao{nullptr};

    // identity of the currently baked geometry (lazy rebuild)
    const void *aimDataKey{nullptr};
    const void *speedDataKey{nullptr};
    uSz strainCountKey{0};
    f64 starsKey{-1.0};
    i32 widthPxKey{-1};
    bool showAimKey{true};
    bool showSpeedKey{true};

    // cached highest-total-strain column for the highlight box (x in pixels, height normalized)
    f32 columnWidth{0.f};
    f32 highlightCenterX{0.f};
    f32 highlightStackH01{0.f};
    bool bValid{false};
};
