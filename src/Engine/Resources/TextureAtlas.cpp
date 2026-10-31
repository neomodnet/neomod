// Copyright (c) 2017, PG, All rights reserved.
#include "TextureAtlas.h"

#include <algorithm>

#include "Engine.h"
#include "ResourceManager.h"
#include "ContainerRanges.h"
#include "Logging.h"
#include "Image.h"
#include "Graphics.h"

TextureAtlas::TextureAtlas(int width, int height, bool filtering) : Resource(TEXTUREATLAS) {
    this->iWidth = width;
    this->iHeight = height;
    this->bFiltered = filtering;

    resourceManager->requestNextLoadUnmanaged();
    this->atlasImage.reset(resourceManager->createImage(this->iWidth, this->iHeight, false,
                                                        true /* keep in system memory, for faster reloads */));
}

TextureAtlas::~TextureAtlas() { this->destroy(); }

void TextureAtlas::init() {
    resourceManager->loadResource(this->atlasImage.get());

    if(!this->bFiltered) {
        this->atlasImage->setFilterMode(TextureFilterMode::NONE);
    } else {
        // sanity (this is default)
        this->atlasImage->setFilterMode(TextureFilterMode::LINEAR);
    }

    this->setReady(true);
}

void TextureAtlas::initAsync() { this->setAsyncReady(true); }

void TextureAtlas::destroy() { this->atlasImage.reset(); }

void TextureAtlas::reloadAtlasImage() {
    if(this->atlasImage == nullptr) return;

    // reload synchronously, don't bother going through resourceManager
    this->atlasImage->reload();
}

void TextureAtlas::putAt(int x, int y, int width, int height, const u8 *rgbaPixels) {
    if(width < 1 || height < 1 || this->atlasImage == nullptr) return;

    if(x + width > this->iWidth || y + height > this->iHeight || x < 0 || y < 0) {
        debugLog("TextureAtlas::putAt( {}, {}, {}, {} ) WARNING: Out of bounds! Atlas size: {}x{}", x, y, width, height,
                 this->iWidth, this->iHeight);
        return;
    }

    this->atlasImage->setRegion(x, y, width, height, rgbaPixels);
}

void TextureAtlas::clearRegion(int x, int y, int width, int height) {
    if(width < 1 || height < 1 || this->atlasImage == nullptr) return;

    if(x + width > this->iWidth || y + height > this->iHeight || x < 0 || y < 0) return;

    this->atlasImage->clearRegion(x, y, width, height);
}

bool TextureAtlas::packRects(std::vector<PackRect> &rects) {
    if(rects.empty()) return true;

    // sort rectangles by height (tallest first) for better packing efficiency
    Mc::ranges::sort(rects, [](const PackRect &a, const PackRect &b) { return a.height > b.height; });

    // initialize skyline - start with single segment covering width minus right edge padding
    const int packableWidth = this->iWidth - ATLAS_PADDING;
    std::vector<Skyline> skylines = {{.x = 0, .y = ATLAS_PADDING, .width = packableWidth}};

    for(auto &rect : rects) {
        const int rectWidth = rect.width + ATLAS_PADDING;
        const int rectHeight = rect.height + ATLAS_PADDING;

        int bestHeight = this->iHeight;
        int bestIndex = -1;
        int bestX = packableWidth;  // initialize to rightmost position for leftmost preference

        // find best position along skyline
        for(size_t i = 0; i < skylines.size(); ++i) {
            // check if rectangle fits horizontally at this skyline segment
            if(skylines[i].x + rectWidth > packableWidth) continue;

            // find maximum height across all skyline segments this rect would span
            int maxY = skylines[i].y;
            int currentX = skylines[i].x;

            for(size_t j = i; j < skylines.size() && currentX < skylines[i].x + rectWidth; ++j) {
                maxY = std::max(maxY, skylines[j].y);
                currentX += skylines[j].width;
            }

            // select this position if it gives us the minimum height
            if(maxY + rectHeight < bestHeight) {
                bestHeight = maxY + rectHeight;
                bestIndex = static_cast<int>(i);
                bestX = skylines[i].x;
            }
        }

        if(bestIndex == -1 || bestHeight > this->iHeight) {
            debugLog("ERROR: Packing failed for rect id={}: bestIndex={}, bestHeight={}, atlasHeight={}", rect.id,
                     bestIndex, bestHeight, this->iHeight);
            return false;
        }

        // place the rectangle
        rect.x = bestX + ATLAS_PADDING;
        rect.y = bestHeight - rectHeight;

        // update skyline - remove segments covered by this rectangle and add new segment
        std::vector<Skyline> newSkylines;

        // copy segments before the placed rectangle
        for(auto &skyline : skylines) {
            if(skyline.x + skyline.width <= bestX)
                newSkylines.push_back(skyline);
            else if(skyline.x < bestX)  // partial segment before rectangle
                newSkylines.push_back({skyline.x, skyline.y, bestX - skyline.x});
            else
                break;
        }

        // add new segment for placed rectangle
        newSkylines.push_back({bestX, bestHeight, rectWidth});

        // add remaining segments after the placed rectangle
        for(auto &skyline : skylines) {
            if(skyline.x >= bestX + rectWidth) {
                newSkylines.push_back(skyline);
            } else if(skyline.x + skyline.width > bestX + rectWidth) {
                // partial segment after rectangle
                int newX = bestX + rectWidth;
                int newWidth = skyline.x + skyline.width - newX;
                newSkylines.push_back({newX, skyline.y, newWidth});
            }
        }

        skylines = std::move(newSkylines);

        // merge adjacent skylines with same height
        for(size_t i = 0; i < skylines.size() - 1; ++i) {
            if(skylines[i].y == skylines[i + 1].y && skylines[i].x + skylines[i].width == skylines[i + 1].x) {
                skylines[i].width += skylines[i + 1].width;
                skylines.erase(skylines.begin() + i + 1);
                --i;
            }
        }
    }

    return true;
}

size_t TextureAtlas::calculateOptimalSize(const std::vector<PackRect> &rects, float targetOccupancy, size_t minSize,
                                          size_t maxSize) {
    if(rects.empty()) return minSize;

    // calculate total area including padding
    size_t totalArea = 0;
    for(const auto &rect : rects) {
        totalArea += static_cast<size_t>((rect.width + ATLAS_PADDING) * (rect.height + ATLAS_PADDING));
    }

    // add 20% for packing inefficiency
    totalArea = static_cast<size_t>(totalArea * 1.2f);

    // find smallest power of 2 that can fit the rectangles with desired occupancy
    size_t size = minSize;
    while(size * size * targetOccupancy < totalArea && size < maxSize) {
        size *= 2;
    }

    return size;
}
