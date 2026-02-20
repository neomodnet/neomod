// Copyright (c) 2017, PG, All rights reserved.
#include "SkinImage.h"

#include "OsuConVars.h"
#include "Engine.h"
#include "Environment.h"
#include "Osu.h"
#include "ResourceManager.h"
#include "VertexArrayObject.h"
#include "Skin.h"
#include "Logging.h"

SkinImage::SkinImage(Skin* skin, const std::string& skinElementName, vec2 baseSizeForScaling2x, float osuSize,
                     const std::string& animationSeparator, bool ignoreDefaultSkin) {
    this->skin = skin;
    this->vBaseSizeForScaling2x = baseSizeForScaling2x;
    this->fOsuSize = osuSize;

    this->bReady = false;

    this->iCurMusicPos = 0;
    this->iFrameCounter = 0;
    this->iFrameCounterUnclamped = 0;
    this->iBeatmapAnimationTimeStartOffset = 0;

    this->bIsMissingTexture = false;
    this->bIsFromDefaultSkin = false;

    this->fDrawClipWidthPercent = 1.0f;

    // logic: first load user skin (true), and if no image could be found then load the default skin (false)
    // this is necessary so that all elements can be correctly overridden with a user skin (e.g. if the user skin only
    // has sliderb.png, but the default skin has sliderb0.png!)
    if(!this->load(skinElementName, animationSeparator, true)) {
        if(!ignoreDefaultSkin) this->load(skinElementName, animationSeparator, false);
    }

    this->bHasNonAnimatedImage = this->nonAnimatedImage.img != MISSING_TEXTURE;

    // if we couldn't load ANYTHING at all, gracefully fallback to missing texture
    if(this->images.size() < 1) {
        this->bIsMissingTexture = true;

        IMAGE missingTexture;

        missingTexture.img = MISSING_TEXTURE;
        missingTexture.scale = 2;

        this->images.push_back(missingTexture);
    }

    // if AnimationFramerate is defined in skin, use that. otherwise derive framerate from number of frames
    if(this->skin->anim_framerate > 0.0f)
        this->fFrameDuration = 1.0f / this->skin->anim_framerate;
    else if(this->images.size() > 0)
        this->fFrameDuration = 1.0f / (float)this->images.size();
}

bool SkinImage::load(const std::string& skinElementName, const std::string& animationSeparator,
                     bool ignoreDefaultSkin) {
    std::string animatedSkinElementStartName = skinElementName;
    animatedSkinElementStartName.append(animationSeparator);
    animatedSkinElementStartName.append("0");
    if(this->loadImage(animatedSkinElementStartName, ignoreDefaultSkin, true,
                       true))  // try loading the first animated element (if this exists then we continue
                               // loading until the first missing frame)
    {
        int frame = 1;
        while(true) {
            std::string currentAnimatedSkinElementFrameName = skinElementName;
            currentAnimatedSkinElementFrameName.append(animationSeparator);
            currentAnimatedSkinElementFrameName.append(std::to_string(frame));

            if(!this->loadImage(currentAnimatedSkinElementFrameName, ignoreDefaultSkin, true, true))
                break;  // stop loading on the first missing frame

            frame++;

            // sanity check
            if(frame > 511) {
                debugLog("SkinImage WARNING: Force stopped loading after 512 frames!");
                break;
            }
        }
        // also try to load non-animated skin element, but don't add it to images
        this->loadImage(skinElementName, ignoreDefaultSkin, false, false);
    } else {
        // load non-animated skin element
        if(this->loadImage(skinElementName, ignoreDefaultSkin, false, true))
            this->bDeleteNonAnimatedImage =
                false;  // avoid double-delete (image is in both images[] and nonAnimatedImage)
    }

    return this->images.size() > 0;  // if any image was found
}

bool SkinImage::loadImage(const std::string& skinElementName, bool ignoreDefaultSkin, bool animated, bool addToImages) {
    const size_t n_dirs = ignoreDefaultSkin ? 1 : this->skin->search_dirs.size();

    for(size_t i = 0; i < n_dirs; i++) {
        const auto& dir = this->skin->search_dirs[i];

        std::string base = dir;
        base.append(skinElementName);

        std::string path_2x = base;
        path_2x.append("@2x.png");

        std::string path_1x = base;
        path_1x.append(".png");

        const bool exists_2x = env->fileExists(path_2x);
        const bool exists_1x = env->fileExists(path_1x);

        if(!exists_2x && !exists_1x) continue;

        // only the built-in default dir (last entry in the full search_dirs) counts as "from default"
        // compare against full size, not n_dirs, since ignoreDefaultSkin truncates the search
        if(!this->skin->o_default && i == this->skin->search_dirs.size() - 1) this->bIsFromDefaultSkin = true;

        // try @2x if HD enabled
        if(cv::skin_hd.getBool() && exists_2x) {
            IMAGE image;

            if(cv::skin_async.getBool()) resourceManager->requestNextLoadAsync();

            image.img = resourceManager->loadImageAbsUnnamed(path_2x, cv::skin_mipmaps.getBool());
            image.scale = 2.0f;

            if(!animated) this->nonAnimatedImage = image;

            if(addToImages) {
                this->images.push_back(image);
                this->filepathsForExport.push_back(path_2x);
                if(exists_1x) this->filepathsForExport.push_back(path_1x);
                this->is_2x = true;
            }
            return true;
        }

        // load @1x
        if(exists_1x) {
            IMAGE image;

            if(cv::skin_async.getBool()) resourceManager->requestNextLoadAsync();

            image.img = resourceManager->loadImageAbsUnnamed(path_1x, cv::skin_mipmaps.getBool());
            image.scale = 1.0f;

            if(!animated) this->nonAnimatedImage = image;

            if(addToImages) {
                this->images.push_back(image);
                this->filepathsForExport.push_back(path_1x);
                if(exists_2x) this->filepathsForExport.push_back(path_2x);
                this->is_2x = false;
            }
            return true;
        }
    }

    return false;
}

SkinImage::~SkinImage() {
    for(auto& image : this->images) {
        if(image.img != MISSING_TEXTURE) resourceManager->destroyResource(image.img);
    }
    this->images.clear();
    if(this->bDeleteNonAnimatedImage && this->nonAnimatedImage.img != MISSING_TEXTURE) {
        resourceManager->destroyResource(this->nonAnimatedImage.img);
    }

    this->filepathsForExport.clear();
}

void SkinImage::drawBrightQuad(VertexArrayObject* vao, float brightness) const {
    // it is assumed that the vao is already set up as a quad with the right texcoords/vertices
    const bool oldBlending = g->getBlending();
    const auto oldBlendMode = g->getBlendMode();

    const Color brightColor = argb(brightness, 1.f, 1.f, 1.f);

    g->setBlending(true);
    g->setBlendMode(DrawBlendMode::ADDITIVE);

    vao->setColors(std::vector<Color>(4, brightColor));

    g->drawVAO(vao);

    g->setBlendMode(oldBlendMode);
    g->setBlending(oldBlending);
}

void SkinImage::draw(vec2 pos, float scale, float brightness, bool animated) const {
    if(this->images.size() < 1) return;

    scale *= this->getScale(animated);  // auto scale to current resolution

    g->pushTransform();
    {
        g->scale(scale, scale);
        g->translate(pos.x, pos.y);

        Image* img = this->getImageForCurrentFrame(animated).img;

        if(this->fDrawClipWidthPercent == 1.0f && brightness <= 0.f)
            g->drawImage(img);
        else if(img->isReady()) {
            const float realWidth = img->getWidth();
            const float realHeight = img->getHeight();

            const float width = realWidth * this->fDrawClipWidthPercent;
            const float height = realHeight;

            const float x = -realWidth / 2.f;
            const float y = -realHeight / 2.f;

            VertexArrayObject vao(DrawPrimitive::QUADS);

            vao.addVertex(x, y);
            vao.addTexcoord(0, 0);

            vao.addVertex(x, (y + height));
            vao.addTexcoord(0, 1);

            vao.addVertex((x + width), (y + height));
            vao.addTexcoord(this->fDrawClipWidthPercent, 1);

            vao.addVertex((x + width), y);
            vao.addTexcoord(this->fDrawClipWidthPercent, 0);

            img->bind();
            {
                g->drawVAO(&vao);

                if(brightness > 0.f) {
                    this->drawBrightQuad(&vao, brightness);
                }
            }
            img->unbind();
        }
    }
    g->popTransform();
}

void SkinImage::drawRaw(vec2 pos, float scale, AnchorPoint anchor, float brightness, bool animated) const {
    if(this->images.size() < 1) return;

    g->pushTransform();
    {
        g->scale(scale, scale);
        g->translate(pos.x, pos.y);

        Image* img = this->getImageForCurrentFrame(animated).img;

        if(this->fDrawClipWidthPercent == 1.0f && brightness <= 0.f) {
            g->drawImage(img, anchor);
        } else if(img->isReady()) {
            // NOTE: Anchor point not handled here, but fDrawClipWidthPercent only used for health bar right now
            const float realWidth = img->getWidth();
            const float realHeight = img->getHeight();

            const float width = realWidth * this->fDrawClipWidthPercent;
            const float height = realHeight;

            const float x = -realWidth / 2.f;
            const float y = -realHeight / 2.f;

            VertexArrayObject vao(DrawPrimitive::QUADS);

            vao.addVertex(x, y);
            vao.addTexcoord(0, 0);

            vao.addVertex(x, (y + height));
            vao.addTexcoord(0, 1);

            vao.addVertex((x + width), (y + height));
            vao.addTexcoord(this->fDrawClipWidthPercent, 1);

            vao.addVertex((x + width), y);
            vao.addTexcoord(this->fDrawClipWidthPercent, 0);

            img->bind();
            {
                g->drawVAO(&vao);

                if(brightness > 0.f) {
                    this->drawBrightQuad(&vao, brightness);
                }
            }
            img->unbind();
        }
    }
    g->popTransform();
}

void SkinImage::update(float speedMultiplier, bool useEngineTimeForAnimations, i32 curMusicPos) {
    if(this->images.size() < 1 || speedMultiplier == 0.f) return;

    this->iCurMusicPos = curMusicPos;

    const f64 frameDurationInSeconds =
        (cv::skin_animation_fps_override.getFloat() > 0.0f ? (1.0f / cv::skin_animation_fps_override.getFloat())
                                                           : this->fFrameDuration) /
        speedMultiplier;
    if(frameDurationInSeconds == 0.f) {
        this->iFrameCounter = 0;
        this->iFrameCounterUnclamped = 0;
        return;
    }

    if(useEngineTimeForAnimations) {
        this->iFrameCounter = (i32)(engine->getTime() / frameDurationInSeconds) % this->images.size();
    } else {
        // when playing a beatmap, objects start the animation at frame 0 exactly when they first become visible (this
        // wouldn't work with the engine time method) therefore we need an offset parameter in the same time-space as
        // the beatmap (this->iBeatmapTimeAnimationStartOffset), and we need the beatmap time (curMusicPos) as a
        // relative base m_iBeatmapAnimationTimeStartOffset must be set by all hitobjects live while drawing (e.g. to
        // their click_time-m_iObjectTime), since we don't have any animation state saved in the hitobjects!

        i32 frame_duration_ms = frameDurationInSeconds * 1000.0f;

        // freeze animation on frame 0 on negative offsets
        this->iFrameCounter =
            std::max((i32)((curMusicPos - this->iBeatmapAnimationTimeStartOffset) / frame_duration_ms), 0);
        this->iFrameCounterUnclamped = this->iFrameCounter;
        this->iFrameCounter = this->iFrameCounter % this->images.size();
    }
}

void SkinImage::setAnimationTimeOffset(i32 offset) {
    this->iBeatmapAnimationTimeStartOffset = offset;
    this->update(this->skin->anim_speed, false, this->iCurMusicPos);  // force update
}

void SkinImage::setAnimationFrameForce(int frame) {
    if(this->images.size() < 1) return;
    this->iFrameCounter = frame % this->images.size();
    this->iFrameCounterUnclamped = this->iFrameCounter;
}

void SkinImage::setAnimationFrameClampUp() {
    if(this->images.size() > 0 && this->iFrameCounterUnclamped > this->images.size() - 1)
        this->iFrameCounter = this->images.size() - 1;
}

vec2 SkinImage::getSize(bool animated) const {
    if(this->images.size() > 0)
        return this->getImageForCurrentFrame(animated).img->getSize() * this->getScale();
    else
        return this->getSizeBase();
}

vec2 SkinImage::getSizeBase() const { return this->vBaseSizeForScaling2x * this->getResolutionScale(); }

vec2 SkinImage::getSizeBaseRaw(bool animated) const {
    return this->vBaseSizeForScaling2x * this->getImageForCurrentFrame(animated).scale;
}

vec2 SkinImage::getImageSizeForCurrentFrame(bool animated) const {
    return this->getImageForCurrentFrame(animated).img->getSize();
}

float SkinImage::getScale(bool animated) const { return this->getImageScale(animated) * this->getResolutionScale(); }

float SkinImage::getImageScale(bool animated) const {
    if(this->images.size() > 0)
        return this->vBaseSizeForScaling2x.x / this->getSizeBaseRaw(animated).x;  // allow overscale and underscale
    else
        return 1.0f;
}

float SkinImage::getResolutionScale() const { return Osu::getRectScale(this->vBaseSizeForScaling2x, this->fOsuSize); }

bool SkinImage::isReady() {
    if(this->bReady) return true;

    for(auto& image : this->images) {
        if(resourceManager->isLoadingResource(image.img)) return false;
    }

    if(this->bDeleteNonAnimatedImage && this->nonAnimatedImage.img != MISSING_TEXTURE) {
        if(resourceManager->isLoadingResource(this->nonAnimatedImage.img)) return false;
    }

    this->bReady = true;
    return this->bReady;
}

const SkinImage::IMAGE& SkinImage::getImageForCurrentFrame(bool animated) const {
    if(this->images.size() > 0)
        return (!animated && this->bHasNonAnimatedImage) ? this->nonAnimatedImage
                                                         : this->images[this->iFrameCounter % this->images.size()];
    else {
        static IMAGE image{
            .img = MISSING_TEXTURE,
            .scale = 1.f,
        };

        return image;
    }
}
