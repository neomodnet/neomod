#pragma once
// Copyright (c) 2017, PG, All rights reserved.

#include "Graphics.h"

#include <string>

struct Skin;

class Image;

extern Image* MISSING_TEXTURE;
class SkinImage final {
    NOCOPY_NOMOVE(SkinImage)
   public:
    struct IMAGE {
        Image* img;
        f32 scale;
    };

   private:
    // constructor/destructor helpers (through Skin)
    friend Skin;
    SkinImage() = default;

    // returns filepaths for export on first init
    [[nodiscard]] std::vector<std::string> init(Skin* skin, const std::string& skinElementName,
                                                vec2 baseSizeForScaling2x, f32 osuSize,
                                                const std::string& animationSeparator = "-",
                                                bool ignoreDefaultSkin = false);

    void destroy(bool everything = false);

   public:
    inline ~SkinImage() { this->destroy(); }

    [[nodiscard]] bool isReady() const;

    // for objects scaled automatically to the current resolution
    // brightness: 0.0 = normal, 1.0 = heavenly
    void draw(vec2 pos, f32 scale = 1.0f, f32 brightness = 0.f, bool animated = true) const;

    // for objects which scale depending on external factors
    // (e.g. hitobjects, depending on the diameter defined by the CS)
    void drawRaw(vec2 pos, f32 scale, AnchorPoint anchor = AnchorPoint::CENTER, f32 brightness = 0.f,
                 bool animated = true) const;

    void update(f32 speedMultiplier, bool useEngineTimeForAnimations = true, i32 curMusicPos = 0);

    void setAnimationFramerate(f32 fps) { this->fFrameDuration = 1.0f / std::clamp<f32>(fps, 1.0f, 9999.0f); }
    void setAnimationTimeOffset(i32 offset);  // set this every frame (before drawing) to a fixed point in time
                                              // relative to curMusicPos where we become visible
    void setAnimationFrameForce(
        int frame);  // force set a frame, before drawing (e.g. for hitresults in UIRankingScreenRankingPanel)

    void setAnimationFrameClampUp();  // force stop the animation after the last frame, before drawing

    void setDrawClipWidthPercent(f32 drawClipWidthPercent) { this->fDrawClipWidthPercent = drawClipWidthPercent; }

    // absolute size scaled to the current resolution (depending on the osuSize as defined when
    // loaded in Skin.cpp)
    [[nodiscard]] vec2 getSize(bool animated = true) const;

    // default assumed size scaled to the current resolution. this is the base resolution which
    // is used for all scaling calculations (to allow skins to overscale or underscale objects)
    [[nodiscard]] vec2 getSizeBase() const;

    // default assumed size UNSCALED. that means that e.g. hitcircles will return either
    // 128x128 or 256x256 depending on the @2x flag in the filename
    [[nodiscard]] vec2 getSizeBaseRaw(bool animated = true) const;

    [[nodiscard]] inline vec2 getSizeBaseRawForScaling2x() const { return this->vBaseSizeForScaling2x; }

    // width/height of the actual image texture as loaded from disk
    [[nodiscard]] vec2 getImageSizeForCurrentFrame(bool animated = true) const;

    [[nodiscard]] const IMAGE& getImageForCurrentFrame(bool animated = true) const;

    [[nodiscard]] f32 getResolutionScale() const;

    [[nodiscard]] inline int getNumImages() const { return static_cast<int>(this->images.size()); }
    [[nodiscard]] inline f32 getFrameDuration() const { return this->fFrameDuration; }
    [[nodiscard]] inline u32 getFrameNumber() const { return this->iFrameCounter; }
    [[nodiscard]] inline bool isMissingTexture() const { return this->bIsMissingTexture; }
    [[nodiscard]] inline bool isFromDefaultSkin() const { return this->bIsFromDefaultSkin; }

   private:
    bool load(const std::string& skinElementName, const std::string& animationSeparator, bool ignoreDefaultSkin,
              std::vector<std::string>& exportVec);
    bool loadImage(const std::string& skinElementName, bool ignoreDefaultSkin, bool animated, bool addToImages,
                   std::vector<std::string>& exportVec);

    [[nodiscard]] f32 getScale(bool animated = true) const;
    [[nodiscard]] f32 getImageScale(bool animated = true) const;
    void drawBrightQuad(VertexArrayObject* vao, f32 brightness) const;  // helper

    // raw files
    std::vector<IMAGE> images;
    IMAGE nonAnimatedImage{.img = MISSING_TEXTURE, .scale = 2.f};

    Skin* skin{nullptr};

    // scaling
    vec2 vBaseSizeForScaling2x{0.f, 0.f};
    //vec2 vSize{0.f};
    f32 fOsuSize{0.f};

    // animation
    i32 iCurMusicPos{0};
    u32 iFrameCounter{0};
    u32 iFrameCounterUnclamped{0};
    f32 fFrameDuration{0.f};
    i32 iBeatmapAnimationTimeStartOffset{0};

    // custom
    f32 fDrawClipWidthPercent{1.f};

    bool bIsMissingTexture{false};
    bool bIsFromDefaultSkin{false};

    // if the nonAnimatedImage is inside the images vector, don't try to delete it twice
    bool bCanDeleteNonAnimatedImage{false};
    bool bHasNonAnimatedImage{false};

    // set by isReady() if ResourceManager is finished loading all images
    mutable bool bReady{false};
};
