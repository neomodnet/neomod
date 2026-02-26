#pragma once
// Copyright (c) 2017, PG, All rights reserved.
#ifndef TEXTUREATLAS_H
#define TEXTUREATLAS_H

#include "Resource.h"
#include "types.h"
#include <vector>
#include <memory>

class Image;

class TextureAtlas final : public Resource {
    NOCOPY_NOMOVE(TextureAtlas)
   public:
    // should basically never need to be changed
    static constexpr const int ATLAS_PADDING{1};

    struct PackRect {
        int x, y, width, height;
        int id;  // user-defined identifier for tracking
    };

    TextureAtlas(int width = 512, int height = 512, bool filtering = false);
    ~TextureAtlas() override;

    void reloadAtlasImage();

    // place RGBA pixels at specific coordinates (for use after packing)
    void putAt(int x, int y, int width, int height, const u8 *rgbaPixels);
    // set image region to black
    void clearRegion(int x, int y, int width, int height);

    // advanced skyline packing for efficient atlas utilization
    bool packRects(std::vector<PackRect> &rects);

    // calculate optimal atlas size for given rectangles
    static size_t calculateOptimalSize(const std::vector<PackRect> &rects, float targetOccupancy = 0.75f,
                                       size_t minSize = 256, size_t maxSize = 4096);

    [[nodiscard]] inline int getWidth() const { return this->iWidth; }
    [[nodiscard]] inline int getHeight() const { return this->iHeight; }
    [[nodiscard]] inline Image *getAtlasImage() const { return this->atlasImage.get(); }

    TextureAtlas *asTextureAtlas() final { return this; }
    [[nodiscard]] const TextureAtlas *asTextureAtlas() const final { return this; }

   private:
    struct Skyline {
        int x, y, width;
    };

    void init() override;
    void initAsync() override;
    void destroy() override;

    std::unique_ptr<Image> atlasImage;

    int iWidth;
    int iHeight;

    bool bFiltered;
};

#endif
