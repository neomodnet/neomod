// Copyright (c) 2026, WH, All rights reserved.
#include "StrainGraph.h"

#include "Graphics.h"
#include "Osu.h"
#include "OsuConVars.h"
#include "ResourceManager.h"
#include "VertexArrayObject.h"

#include <algorithm>
#include <cmath>

StrainGraph::StrainGraph() = default;

StrainGraph::~StrainGraph() {
    if(this->aimVao) resourceManager->destroyResource(this->aimVao);
    if(this->speedVao) resourceManager->destroyResource(this->speedVao);
}

void StrainGraph::draw(std::span<const f32> aimStrains, std::span<const f32> speedStrains, f64 stars, vec2 pos,
                       f32 widthPx, f32 heightPx, f32 alpha, bool drawAim, bool drawSpeed) {
    if((!drawAim && !drawSpeed) || widthPx < 1.f || heightPx <= 0.f) return;
    if(aimStrains.empty() || aimStrains.size() != speedStrains.size()) return;

    const i32 width = std::max(1, (i32)widthPx);
    if(aimStrains.data() != this->aimDataKey || speedStrains.data() != this->speedDataKey ||
       aimStrains.size() != this->strainCountKey || stars != this->starsKey || width != this->widthPxKey ||
       drawAim != this->showAimKey || drawSpeed != this->showSpeedKey) {
        this->rebuild(aimStrains, speedStrains, width, drawAim, drawSpeed);
        this->aimDataKey = aimStrains.data();
        this->speedDataKey = speedStrains.data();
        this->strainCountKey = aimStrains.size();
        this->starsKey = stars;
        this->widthPxKey = width;
        this->showAimKey = drawAim;
        this->showSpeedKey = drawSpeed;
    }

    if(!this->bValid) return;

    // draw strain bar graph
    g->pushTransform();
    {
        g->scale(1.f, heightPx);
        g->translate(pos.x, pos.y);

        if(drawAim) {
            g->setColor(RGB_CV_TO_COL(hud_scrubbing_timeline_strains_aim_color).setA(alpha));
            g->drawVAO(this->aimVao);
        }
        if(drawSpeed) {
            g->setColor(RGB_CV_TO_COL(hud_scrubbing_timeline_strains_speed_color).setA(alpha));
            g->drawVAO(this->speedVao);
        }
    }
    g->popTransform();

    // highlight highest total strain value (+- section block)
    const f32 margin = 5.0f * Osu::getUIScale();
    const f32 stackHeight = this->highlightStackH01 * heightPx;

    const f32 rectX = pos.x + this->highlightCenterX - margin * this->columnWidth;
    const f32 rectY = pos.y - stackHeight - margin * this->columnWidth;
    const f32 rectW = this->columnWidth * 2.f * margin;
    const f32 rectH = stackHeight + 2.f * margin * this->columnWidth;

    g->setColor(0xffffffff);
    g->setAlpha(alpha);
    g->drawRect((i32)rectX, (i32)rectY, (i32)rectW, (i32)rectH);
    g->setAlpha(alpha * 0.5f);
    g->drawRect((i32)(rectX - 2), (i32)(rectY - 2), (i32)(rectW + 4), (i32)(rectH + 4));
    g->setAlpha(alpha * 0.25f);
    g->drawRect((i32)(rectX - 4), (i32)(rectY - 4), (i32)(rectW + 8), (i32)(rectH + 8));
}

void StrainGraph::rebuild(std::span<const f32> aimStrains, std::span<const f32> speedStrains, i32 widthPx, bool drawAim,
                          bool drawSpeed) {
    this->bValid = false;

    // get highest strain values for normalization
    f32 highestAimStrain = 0.0f;
    f32 highestSpeedStrain = 0.0f;
    f32 highestStrain = 0.0f;
    uSz highestStrainIndex = 0;
    for(uSz i = 0; i < aimStrains.size(); i++) {
        const f32 strain = aimStrains[i] + speedStrains[i];
        if(strain > highestStrain) {
            highestStrain = strain;
            highestStrainIndex = i;
        }
        if(aimStrains[i] > highestAimStrain) highestAimStrain = aimStrains[i];
        if(speedStrains[i] > highestSpeedStrain) highestSpeedStrain = speedStrains[i];
    }
    if(!(highestAimStrain > 0.0 && highestSpeedStrain > 0.0 && highestStrain > 0.0)) return;

    const uSz count = aimStrains.size();
    const uSz numBuckets = std::min(count, (uSz)widthPx);

    // plain triangles: QUADS would need primitive conversion on every modern backend (and GLES can't at all)
    if(!this->aimVao) this->aimVao = resourceManager->createVertexArrayObject(DrawPrimitive::TRIANGLES);
    if(!this->speedVao) this->speedVao = resourceManager->createVertexArrayObject(DrawPrimitive::TRIANGLES);
    this->aimVao->release();
    this->speedVao->release();
    if(drawAim) this->aimVao->reserve(numBuckets * 6);
    if(drawSpeed) this->speedVao->reserve(numBuckets * 6);

    constexpr auto addColumn = [](VertexArrayObject *vao, f32 x0, f32 x1, f32 yTop, f32 yBottom) {
        vao->addVertex(x0, yTop);
        vao->addVertex(x0, yBottom);
        vao->addVertex(x1, yBottom);
        vao->addVertex(x0, yTop);
        vao->addVertex(x1, yBottom);
        vao->addVertex(x1, yTop);
    };

    uSz highlightBucket = 0;
    for(uSz b = 0; b < numBuckets; b++) {
        const uSz i0 = (b * count) / numBuckets;
        const uSz i1 = ((b + 1) * count) / numBuckets;
        if(highestStrainIndex >= i0 && highestStrainIndex < i1) highlightBucket = b;

        // represent the bucket by its highest total strain section (peaks matter, averages don't)
        uSz peak = i0;
        f32 peakStrain = -1.0f;
        for(uSz i = i0; i < i1; i++) {
            const f32 strain = aimStrains[i] + speedStrains[i];
            if(strain > peakStrain) {
                peakStrain = strain;
                peak = i;
            }
        }

        // integer column edges tile the graph exactly, so no overlap (no double blending) and no gaps
        const f32 x0 = std::round((f32)b * (f32)widthPx / (f32)numBuckets);
        const f32 x1 = std::round((f32)(b + 1) * (f32)widthPx / (f32)numBuckets);

        const auto aimH = (f32)(aimStrains[peak] / highestStrain);
        const auto speedH = (f32)(speedStrains[peak] / highestStrain);

        // stacked columns in normalized height (scaled to pixels at draw time), growing upwards
        f32 y = 0.f;
        if(drawAim && aimH > 0.f) {
            addColumn(this->aimVao, x0, x1, y, y - aimH);
            y -= aimH;
        }
        if(drawSpeed && speedH > 0.f) {
            addColumn(this->speedVao, x0, x1, y, y - speedH);
        }
    }

    if(drawAim) resourceManager->loadResource(this->aimVao);
    if(drawSpeed) resourceManager->loadResource(this->speedVao);

    this->columnWidth = (f32)widthPx / (f32)numBuckets;
    this->highlightCenterX = ((f32)highlightBucket + 0.5f) * this->columnWidth;
    this->highlightStackH01 = (f32)(((drawAim ? aimStrains[highestStrainIndex] : 0.0) +
                                     (drawSpeed ? speedStrains[highestStrainIndex] : 0.0)) /
                                    highestStrain);
    this->bValid = true;
}
