//========== Copyright (c) 2012, PG & 2025-2026, WH, All rights reserved. =======//
//
// Purpose:		image wrapper (base)
//
// $NoKeywords: $img
//===============================================================================//

#pragma once
#ifndef IMAGE_H
#define IMAGE_H

#include "noinclude.h"
#include "types.h"

#include "Rect.h"
#include "Vectors.h"
#include "Resource.h"
#include "Color.h"

#include <cstring>
#include <vector>
#include <memory>

enum class TextureFilterMode : u8;
enum class TextureWrapMode : u8;

class Image : public Resource {
   public:
    // returns false on failure
    static bool saveToImage(const u8 *data, i32 width, i32 height, u8 channels, std::string filepath);

    enum class TYPE : uint8_t { TYPE_RGBA, TYPE_PNG, TYPE_JPG };

   public:
    Image(std::string filepath, bool mipmapped = false, bool keepInSystemMemory = false);
    Image(i32 width, i32 height, bool mipmapped = false, bool keepInSystemMemory = false);

    virtual void bind(unsigned int textureUnit = 0) const = 0;
    virtual void unbind() const = 0;

    virtual inline void setFilterMode(TextureFilterMode filterMode) { this->filterMode = filterMode; };
    virtual inline void setWrapMode(TextureWrapMode wrapMode) { this->wrapMode = wrapMode; };

    void setPixel(i32 x, i32 y, Color color);
    void setPixels(const std::vector<u8> &pixels);
    void setRegion(i32 x, i32 y, i32 w, i32 h, const u8 *rgbaPixels);
    void clearRegion(i32 x, i32 y, i32 w, i32 h);

    [[nodiscard]] inline bool failedLoad() const { return this->bLoadError.load(std::memory_order_acquire); }
    [[nodiscard]] Color getPixel(i32 x, i32 y) const;

    [[nodiscard]] inline Image::TYPE getType() const { return this->type; }
    [[nodiscard]] inline i32 getWidth() const { return this->iWidth; }
    [[nodiscard]] inline i32 getHeight() const { return this->iHeight; }
    [[nodiscard]] inline ivec2 getSize() const { return ivec2{this->iWidth, this->iHeight}; }

    Image *asImage() final { return this; }
    [[nodiscard]] const Image *asImage() const final { return this; }

    // all images are converted to RGBA
    static constexpr const u8 NUM_CHANNELS{4};

   protected:
    void init() override = 0;
    void initAsync() override = 0;
    void destroy() override = 0;

    bool loadRawImage();

    struct CFree {
        void operator()(void *p) const noexcept;
    };
    struct SizedRGBABytes final : public std::unique_ptr<u8[], CFree> {
        using unique_ptr::unique_ptr;
        ~SizedRGBABytes() = default;
        SizedRGBABytes(SizedRGBABytes &&other) noexcept = default;
        SizedRGBABytes &operator=(SizedRGBABytes &&other) noexcept = default;

        // for taking ownership of some raw pointer (stb)
        explicit SizedRGBABytes(u8 *to_own, i32 width, i32 height) noexcept
            : unique_ptr(to_own), width(width), height(height) {}

        explicit SizedRGBABytes(i32 width, i32 height) noexcept
            : unique_ptr(static_cast<u8 *>(malloc(static_cast<u64>(width) * height * Image::NUM_CHANNELS))),
              width(width),
              height(height) {}
        explicit SizedRGBABytes(i32 width, i32 height, bool /*zero*/) noexcept
            : unique_ptr(static_cast<u8 *>(calloc(static_cast<u64>(width) * height * Image::NUM_CHANNELS, sizeof(u8)))),
              width(width),
              height(height) {}

        SizedRGBABytes(const SizedRGBABytes &other) noexcept
            : unique_ptr(other.get() ? static_cast<u8 *>(malloc(other.getNumBytes())) : nullptr),
              width(other.width),
              height(other.height) {
            if(this->get()) {
                memcpy(this->get(), other.get(), other.getNumBytes());
            }
        }
        SizedRGBABytes &operator=(const SizedRGBABytes &other) noexcept {
            if(this != &other) {
                this->width = other.width;
                this->height = other.height;
                this->reset(other.get() ? static_cast<u8 *>(malloc(other.getNumBytes())) : nullptr);
                if(this->get()) {
                    memcpy(this->get(), other.get(), other.getNumBytes());
                }
            }
            return *this;
        }

        [[nodiscard]] constexpr forceinline u64 getNumBytes() const {
            return static_cast<u64>(this->width) * this->height * Image::NUM_CHANNELS;
        }
        [[nodiscard]] constexpr forceinline u64 getNumPixels() const {
            return static_cast<u64>(this->width) * this->height;
        }
        [[nodiscard]] constexpr forceinline i32 getX() const { return this->width; }
        [[nodiscard]] constexpr forceinline i32 getY() const { return this->height; }

        void clear() {
            this->reset();
            this->width = 0;
            this->height = 0;
        }

       private:
        // holding actual data width/height separately, just in case
        i32 width{0};
        i32 height{0};
    };

    SizedRGBABytes rawImage;

    [[nodiscard]] constexpr forceinline u64 totalBytes() const { return this->rawImage.getNumBytes(); }

    i32 iWidth;
    i32 iHeight;

    // for reuploads (grid-based multi-region dirty tracking)
    static constexpr i32 DIRTY_TILE_SHIFT = 5;  // 32px tiles
    static constexpr i32 DIRTY_TILE_SIZE = 1 << DIRTY_TILE_SHIFT;

    // extracts dirty rectangles from the grid (destructive - zeros visited tiles)
    [[nodiscard]] std::vector<McIRect> getDirtyRects();

    // reset after uploading to gpu
    void resetDirtyRegion() {
        this->bDirtyFull = false;
        if(!this->dirtyGrid.empty()) std::memset(this->dirtyGrid.data(), 0, this->dirtyGrid.size());
    }

   private:
    std::vector<u8> dirtyGrid;
    i32 dirtyGridW{0};
    i32 dirtyGridH{0};
    bool bDirtyFull{true};  // first upload is always full

   protected:
    TextureWrapMode wrapMode;
    Image::TYPE type;
    TextureFilterMode filterMode;

    bool bMipmapped;
    bool bCreatedImage;
    bool bKeepInSystemMemory;
    std::atomic<bool> bLoadError{false};
    bool bLoadedImageEntirelyTransparent{false};

   private:
    enum class ImageDecodeResult : u8 {
        SUCCESS,
        FAIL,
        INTERRUPTED,
    };

    [[nodiscard]] bool isRawImageCompletelyTransparent() const;
    static bool canHaveTransparency(const u8 *data, u64 size);

    ImageDecodeResult decodeJPEGFromMemory(const u8 *inData, u64 size);
    ImageDecodeResult decodePNGFromMemory(const u8 *inData, u64 size);
    ImageDecodeResult decodeSTBFromMemory(const u8 *inData, u64 size);
};

#endif
