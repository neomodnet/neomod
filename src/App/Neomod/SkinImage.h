#pragma once
// Copyright (c) 2017, PG, All rights reserved.

#include "noinclude.h"
#include "types.h"
#include "Vectors_fwd.h"
#include "Graphics_fwd.h"

#include "StaticPImpl.h"

#include <string>
#include <string_view>
#include <vector>

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
    SkinImage();

    // (adds the files it's made of to the skin's files_for_export)
    void init(Skin* skin, const std::string& skinElementName, vec2 baseSizeForScaling2x, f32 osuSize,
              const std::string& animationSeparator = "-", bool ignoreDefaultSkin = false);

    void destroy(bool everything = false);

   public:
    ~SkinImage();

    [[nodiscard]] bool isReady() const;

    // for objects scaled automatically to the current resolution
    // brightness: 0.0 = normal, 1.0 = heavenly
    void draw(vec2 pos, f32 scale = 1.0f, f32 brightness = 0.f, bool animated = true) const;

    // for objects which scale depending on external factors
    // (e.g. hitobjects, depending on the diameter defined by the CS)
    void drawRaw(vec2 pos, f32 scale, AnchorPoint anchor = {}, f32 brightness = 0.f, bool animated = true) const;

    void update(f32 speedMultiplier, bool useEngineTimeForAnimations = true, i32 curMusicPos = 0);

    void setAnimationFramerate(f32 fps);
    void setAnimationTimeOffset(f32 speedMultiplier,
                                i32 offset);  // set this every frame (before drawing) to a fixed point in time
                                              // relative to curMusicPos where we become visible
    void setAnimationFrameForce(
        int frame);  // force set a frame, before drawing (e.g. for hitresults in UIRankingScreenRankingPanel)

    void setAnimationFrameClampUp();  // force stop the animation after the last frame, before drawing

    void setDrawClipWidthPercent(f32 drawClipWidthPercent);

    // absolute size scaled to the current resolution (depending on the osuSize as defined when
    // loaded in Skin.cpp)
    [[nodiscard]] vec2 getSize(bool animated = true) const;

    // default assumed size scaled to the current resolution. this is the base resolution which
    // is used for all scaling calculations (to allow skins to overscale or underscale objects)
    [[nodiscard]] vec2 getSizeBase() const;

    // default assumed size UNSCALED. that means that e.g. hitcircles will return either
    // 128x128 or 256x256 depending on the @2x flag in the filename
    [[nodiscard]] vec2 getSizeBaseRaw(bool animated = true) const;

    [[nodiscard]] vec2 getSizeBaseRawForScaling2x() const;

    // width/height of the actual image texture as loaded from disk
    [[nodiscard]] vec2 getImageSizeForCurrentFrame(bool animated = true) const;

    [[nodiscard]] const IMAGE& getImageForCurrentFrame(bool animated = true) const;

    [[nodiscard]] f32 getResolutionScale() const;

    [[nodiscard]] int getNumImages() const;
    [[nodiscard]] f32 getFrameDuration() const;
    [[nodiscard]] u32 getFrameNumber() const;
    [[nodiscard]] bool isMissingTexture() const;
    [[nodiscard]] bool isFromDefaultSkin() const;

   private:
    bool load(Skin* skin, const std::string& skinElementName, const std::string& animationSeparator,
              bool ignoreDefaultSkin);
    bool loadImage(Skin* skin, const std::string& skinElementName, bool ignoreDefaultSkin, bool animated,
                   bool addToImages);

    [[nodiscard]] f32 getScale(bool animated = true) const;
    [[nodiscard]] f32 getImageScale(bool animated = true) const;

    struct SkinImageImpl;
    StaticPImpl<SkinImageImpl, sizeof(void*) == 8 ? 80 : 60, alignof(void*)> m_impl;
};
