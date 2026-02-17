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
        float scale;
    };

   public:
    SkinImage(Skin* skin, const std::string& skinElementName, vec2 baseSizeForScaling2x, float osuSize,
              const std::string& animationSeparator = "-", bool ignoreDefaultSkin = false);
    ~SkinImage();

    // for objects scaled automatically to the current resolution
    // brightness: 0.0 = normal, 1.0 = heavenly
    void draw(vec2 pos, float scale = 1.0f, float brightness = 0.f, bool animated = true) const;

    // for objects which scale depending on external factors
    // (e.g. hitobjects, depending on the diameter defined by the CS)
    void drawRaw(vec2 pos, float scale, AnchorPoint anchor = AnchorPoint::CENTER, float brightness = 0.f,
                 bool animated = true) const;

    void update(float speedMultiplier, bool useEngineTimeForAnimations = true, i32 curMusicPos = 0);

    void setAnimationFramerate(float fps) { this->fFrameDuration = 1.0f / std::clamp<float>(fps, 1.0f, 9999.0f); }
    void setAnimationTimeOffset(i32 offset);  // set this every frame (before drawing) to a fixed point in time
                                              // relative to curMusicPos where we become visible
    void setAnimationFrameForce(
        int frame);  // force set a frame, before drawing (e.g. for hitresults in UIRankingScreenRankingPanel)

    void setAnimationFrameClampUp();  // force stop the animation after the last frame, before drawing

    void setDrawClipWidthPercent(float drawClipWidthPercent) { this->fDrawClipWidthPercent = drawClipWidthPercent; }

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

    [[nodiscard]] float getResolutionScale() const;

    bool isReady();

    [[nodiscard]] inline int getNumImages() const { return this->images.size(); }
    [[nodiscard]] inline float getFrameDuration() const { return this->fFrameDuration; }
    [[nodiscard]] inline unsigned int getFrameNumber() const { return this->iFrameCounter; }
    [[nodiscard]] inline bool isMissingTexture() const { return this->bIsMissingTexture; }
    [[nodiscard]] inline bool isFromDefaultSkin() const { return this->bIsFromDefaultSkin; }

    [[nodiscard]] inline std::vector<std::string> getFilepathsForExport() const { return this->filepathsForExport; }

    // TODO: remove is_2x, it's entirely possible for elements to have mixed non-2x and 2x images for different frames
    bool is_2x{false};

   private:
    bool load(const std::string& skinElementName, const std::string& animationSeparator, bool ignoreDefaultSkin);
    bool loadImage(const std::string& skinElementName, bool ignoreDefaultSkin, bool animated, bool addToImages);

    [[nodiscard]] float getScale(bool animated = true) const;
    [[nodiscard]] float getImageScale(bool animated = true) const;
    void drawBrightQuad(VertexArrayObject* vao, float brightness) const;  // helper

    Skin* skin;
    bool bReady;

    // scaling
    vec2 vBaseSizeForScaling2x{0};
    //vec2 vSize{0.f};
    float fOsuSize;

    // animation
    i32 iCurMusicPos;
    unsigned int iFrameCounter;
    u32 iFrameCounterUnclamped;
    float fFrameDuration;
    i32 iBeatmapAnimationTimeStartOffset;

    // raw files
    std::vector<IMAGE> images;
    IMAGE nonAnimatedImage{.img = MISSING_TEXTURE, .scale = 2.f};

    bool bIsMissingTexture;
    bool bIsFromDefaultSkin;

    // if the nonAnimatedImage is inside the images vector, don't try to delete it twice
    bool bDeleteNonAnimatedImage{true};
    bool bHasNonAnimatedImage{false};

    // custom
    float fDrawClipWidthPercent;
    std::vector<std::string> filepathsForExport;
};
