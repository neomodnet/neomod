// Copyright (c) 2017, PG, All rights reserved.
#include "SkinImage.h"

#include "OsuConVars.h"
#include "Engine.h"
#include "Environment.h"
#include "Osu.h"
#include "ResourceManager.h"
#include "VertexArrayObject.h"
#include "Skin.h"
#include "Graphics.h"
#include "Logging.h"
#include "FixedSizeArray.h"

struct SkinImage::SkinImageImpl {
    // raw files
    std::vector<IMAGE> images;
    IMAGE nonAnimatedImage{.img = MISSING_TEXTURE, .scale = 2.f};

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

    bool bIsFromDefaultSkin{false};

    // if the nonAnimatedImage is inside the images vector, don't try to delete it twice
    bool bCanDeleteNonAnimatedImage{false};
    bool bHasNonAnimatedImage{false};

    // set by isReady() if ResourceManager is finished loading all images
    mutable bool bReady{false};
};

SkinImage::SkinImage() : m_impl() {}
SkinImage::~SkinImage() { this->destroy(); }

std::vector<std::string> SkinImage::init(Skin* skin, const std::string& skinElementName, vec2 baseSizeForScaling2x,
                                         float osuSize, const std::string& animationSeparator, bool ignoreDefaultSkin) {
    std::vector<std::string> toExport;

    m_impl->vBaseSizeForScaling2x = baseSizeForScaling2x;
    m_impl->fOsuSize = osuSize;

    // logic: first load user skin (true), and if no image could be found then load the default skin (false)
    // this is necessary so that all elements can be correctly overridden with a user skin (e.g. if the user skin only
    // has sliderb.png, but the default skin has sliderb0.png!)
    if(!this->load(skin, skinElementName, animationSeparator, true, toExport)) {
        if(!ignoreDefaultSkin) this->load(skin, skinElementName, animationSeparator, false, toExport);
    }

    if(m_impl->nonAnimatedImage.img != MISSING_TEXTURE) {
        m_impl->bHasNonAnimatedImage = true;

        // avoid double-delete
        m_impl->bCanDeleteNonAnimatedImage =
            !std::ranges::contains(m_impl->images, m_impl->nonAnimatedImage.img, &IMAGE::img);
    } else {
        m_impl->bHasNonAnimatedImage = false;
        m_impl->bCanDeleteNonAnimatedImage = true;
    }

    // if we couldn't load ANYTHING at all, gracefully fallback to missing texture
    if(m_impl->images.size() < 1) {
        IMAGE missingTexture{
            .img = MISSING_TEXTURE,
            .scale = 2.f,
        };

        m_impl->images.push_back(missingTexture);
    }

    // if AnimationFramerate is defined in skin, use that. otherwise derive framerate from number of frames
    if(skin->anim_framerate > 0.0f)
        m_impl->fFrameDuration = 1.0f / skin->anim_framerate;
    else if(m_impl->images.size() > 0)
        m_impl->fFrameDuration = 1.0f / (float)m_impl->images.size();

    return toExport;
}

bool SkinImage::load(Skin* skin, const std::string& skinElementName, const std::string& animationSeparator,
                     bool ignoreDefaultSkin, std::vector<std::string>& exportVec) {
    std::string animatedSkinElementStartName = skinElementName;
    animatedSkinElementStartName.append(animationSeparator);
    animatedSkinElementStartName.append("0");
    if(this->loadImage(skin, animatedSkinElementStartName, ignoreDefaultSkin, true, true,
                       exportVec))  // try loading the first animated element (if this exists then we continue
                                    // loading until the first missing frame)
    {
        int frame = 1;
        while(true) {
            std::string currentAnimatedSkinElementFrameName = skinElementName;
            currentAnimatedSkinElementFrameName.append(animationSeparator);
            currentAnimatedSkinElementFrameName.append(std::to_string(frame));

            if(!this->loadImage(skin, currentAnimatedSkinElementFrameName, ignoreDefaultSkin, true, true, exportVec))
                break;  // stop loading on the first missing frame

            frame++;

            // sanity check
            if(frame > 511) {
                debugLog("SkinImage WARNING: Force stopped loading after 512 frames!");
                break;
            }
        }
        // also try to load non-animated skin element, but don't add it to images
        this->loadImage(skin, skinElementName, ignoreDefaultSkin, false, false, exportVec);
    } else {
        // load non-animated skin element
        this->loadImage(skin, skinElementName, ignoreDefaultSkin, false, true, exportVec);
    }

    return m_impl->images.size() > 0;  // if any image was found
}

bool SkinImage::loadImage(Skin* skin, const std::string& skinElementName, bool ignoreDefaultSkin, bool animated,
                          bool addToImages, std::vector<std::string>& exportVec) {
    const size_t n_dirs = ignoreDefaultSkin ? 1 : skin->search_dirs.size();

    for(size_t i = 0; i < n_dirs; i++) {
        const auto& dir = skin->search_dirs[i];

        std::string base = dir;
        base.append(skinElementName);

        std::string path_2x = base;
        path_2x.append("@2x.png");

        std::string path_1x = base;
        path_1x.append(".png");

        const bool exists_2x = Environment::fileExists(path_2x);
        const bool exists_1x = Environment::fileExists(path_1x);

        if(!exists_2x && !exists_1x) continue;

        // only the built-in default dir (last entry in the full search_dirs) counts as "from default"
        // compare against full size, not n_dirs, since ignoreDefaultSkin truncates the search
        if(!skin->is_default && i == skin->search_dirs.size() - 1) m_impl->bIsFromDefaultSkin = true;

        // try @2x if HD enabled
        if(cv::skin_hd.getBool() && exists_2x) {
            IMAGE image;

            if(cv::skin_async.getBool()) resourceManager->requestNextLoadAsync();

            image.img = resourceManager->loadImageAbsUnnamed(path_2x, cv::skin_mipmaps.getBool());
            image.scale = 2.0f;

            if(!animated) m_impl->nonAnimatedImage = image;

            if(addToImages) {
                m_impl->images.push_back(image);
                exportVec.push_back(path_2x);
                if(exists_1x) exportVec.push_back(path_1x);
            }
            return true;
        }

        // load @1x
        if(exists_1x) {
            IMAGE image;

            if(cv::skin_async.getBool()) resourceManager->requestNextLoadAsync();

            image.img = resourceManager->loadImageAbsUnnamed(path_1x, cv::skin_mipmaps.getBool());
            image.scale = 1.0f;

            if(!animated) m_impl->nonAnimatedImage = image;

            if(addToImages) {
                m_impl->images.push_back(image);
                exportVec.push_back(path_1x);
                if(exists_2x) exportVec.push_back(path_2x);
            }
            return true;
        }
    }

    return false;
}

void SkinImage::destroy(bool everything) {
    m_impl->bReady = false;

    const auto destroyFlags = everything ? ResourceDestroyFlags::RDF_FORCE_BLOCKING : ResourceDestroyFlags::RDF_DEFAULT;
    auto imagesToDelete = std::move(m_impl->images);
    m_impl->images.clear();

    // we delete everything including default skin resources since they aren't named (thus can't be accidentally shared)
    for(auto& image : imagesToDelete) {
        if(image.img && image.img != MISSING_TEXTURE) {
            resourceManager->destroyResource(image.img, destroyFlags);
            image.img = MISSING_TEXTURE;
        }
    }

    if(m_impl->bHasNonAnimatedImage && m_impl->bCanDeleteNonAnimatedImage && m_impl->nonAnimatedImage.img &&
       m_impl->nonAnimatedImage.img != MISSING_TEXTURE) {
        resourceManager->destroyResource(m_impl->nonAnimatedImage.img, destroyFlags);
        m_impl->nonAnimatedImage.img = MISSING_TEXTURE;
    }

    m_impl->vBaseSizeForScaling2x = {0.f, 0.f};
    m_impl->fOsuSize = 0.f;
    m_impl->iCurMusicPos = 0;
    m_impl->iFrameCounter = 0;
    m_impl->iFrameCounterUnclamped = 0;
    m_impl->fFrameDuration = 0.f;
    m_impl->iBeatmapAnimationTimeStartOffset = 0;
    m_impl->fDrawClipWidthPercent = 1.f;
    m_impl->bIsFromDefaultSkin = false;
    m_impl->bCanDeleteNonAnimatedImage = false;
    m_impl->bHasNonAnimatedImage = false;
}

namespace {

// buffer
static CONSTINIT VertexArrayObject quadVAO{DrawPrimitive::QUADS};

void draw_bright_quad(Graphics* g, VertexArrayObject* vao, float brightness) {
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

}  // namespace

void SkinImage::draw(vec2 pos, float scale, float brightness, bool animated) const {
    if(m_impl->images.size() < 1) return;

    scale *= this->getScale(animated);  // auto scale to current resolution

    g->pushTransform();
    {
        g->scale(scale, scale);
        g->translate(pos.x, pos.y);

        Image* img = this->getImageForCurrentFrame(animated).img;

        if(m_impl->fDrawClipWidthPercent == 1.0f && brightness <= 0.f)
            g->drawImage(img);
        else if(img->isReady()) {
            const float realWidth = img->getWidth();
            const float realHeight = img->getHeight();

            const float width = realWidth * m_impl->fDrawClipWidthPercent;
            const float height = realHeight;

            const float x = -realWidth / 2.f;
            const float y = -realHeight / 2.f;

            quadVAO.clear();

            quadVAO.addVertex(x, y);
            quadVAO.addTexcoord(0, 0);

            quadVAO.addVertex(x, (y + height));
            quadVAO.addTexcoord(0, 1);

            quadVAO.addVertex((x + width), (y + height));
            quadVAO.addTexcoord(m_impl->fDrawClipWidthPercent, 1);

            quadVAO.addVertex((x + width), y);
            quadVAO.addTexcoord(m_impl->fDrawClipWidthPercent, 0);

            img->bind();
            {
                g->drawVAO(&quadVAO);

                if(brightness > 0.f) {
                    draw_bright_quad(g, &quadVAO, brightness);
                }
            }
            img->unbind();
        }
    }
    g->popTransform();
}

static_assert(AnchorPoint{} == AnchorPoint::CENTER);  // sanity
void SkinImage::drawRaw(vec2 pos, float scale, AnchorPoint anchor, float brightness, bool animated) const {
    if(m_impl->images.size() < 1) return;

    g->pushTransform();
    {
        g->scale(scale, scale);
        g->translate(pos.x, pos.y);

        Image* img = this->getImageForCurrentFrame(animated).img;

        if(m_impl->fDrawClipWidthPercent == 1.0f && brightness <= 0.f) {
            g->drawImage(img, anchor);
        } else if(img->isReady()) {
            // NOTE: Anchor point not handled here, but fDrawClipWidthPercent only used for health bar right now
            const float realWidth = img->getWidth();
            const float realHeight = img->getHeight();

            const float width = realWidth * m_impl->fDrawClipWidthPercent;
            const float height = realHeight;

            const float x = -realWidth / 2.f;
            const float y = -realHeight / 2.f;

            quadVAO.clear();

            quadVAO.addVertex(x, y);
            quadVAO.addTexcoord(0, 0);

            quadVAO.addVertex(x, (y + height));
            quadVAO.addTexcoord(0, 1);

            quadVAO.addVertex((x + width), (y + height));
            quadVAO.addTexcoord(m_impl->fDrawClipWidthPercent, 1);

            quadVAO.addVertex((x + width), y);
            quadVAO.addTexcoord(m_impl->fDrawClipWidthPercent, 0);

            img->bind();
            {
                g->drawVAO(&quadVAO);

                if(brightness > 0.f) {
                    draw_bright_quad(g, &quadVAO, brightness);
                }
            }
            img->unbind();
        }
    }
    g->popTransform();
}

void SkinImage::update(float speedMultiplier, bool useEngineTimeForAnimations, i32 curMusicPos) {
    if(m_impl->images.size() < 1 || speedMultiplier == 0.f) return;

    m_impl->iCurMusicPos = curMusicPos;

    const f64 frameDurationInSeconds =
        (cv::skin_animation_fps_override.getFloat() > 0.0f ? (1.0f / cv::skin_animation_fps_override.getFloat())
                                                           : m_impl->fFrameDuration) /
        speedMultiplier;
    if(frameDurationInSeconds == 0.f) {
        m_impl->iFrameCounter = 0;
        m_impl->iFrameCounterUnclamped = 0;
        return;
    }

    if(useEngineTimeForAnimations) {
        m_impl->iFrameCounter = (i32)(engine->getTime() / frameDurationInSeconds) % m_impl->images.size();
    } else {
        // when playing a beatmap, objects start the animation at frame 0 exactly when they first become visible (this
        // wouldn't work with the engine time method) therefore we need an offset parameter in the same time-space as
        // the beatmap (m_impl->iBeatmapTimeAnimationStartOffset), and we need the beatmap time (curMusicPos) as a
        // relative base m_iBeatmapAnimationTimeStartOffset must be set by all hitobjects live while drawing (e.g. to
        // their click_time-m_iObjectTime), since we don't have any animation state saved in the hitobjects!

        i32 frame_duration_ms = frameDurationInSeconds * 1000.0f;

        // freeze animation on frame 0 on negative offsets
        m_impl->iFrameCounter =
            std::max((i32)((curMusicPos - m_impl->iBeatmapAnimationTimeStartOffset) / frame_duration_ms), 0);
        m_impl->iFrameCounterUnclamped = m_impl->iFrameCounter;
        m_impl->iFrameCounter = m_impl->iFrameCounter % m_impl->images.size();
    }
}

void SkinImage::setAnimationFramerate(f32 fps) {
    m_impl->fFrameDuration = 1.0f / (fps > 9999.f ? 9999.f : fps < 1.f ? 1.f : fps);
}

void SkinImage::setAnimationTimeOffset(f32 speedMultiplier, i32 offset) {
    m_impl->iBeatmapAnimationTimeStartOffset = offset;
    this->update(speedMultiplier, false, m_impl->iCurMusicPos);  // force update
}

void SkinImage::setAnimationFrameForce(int frame) {
    if(m_impl->images.size() < 1) return;
    m_impl->iFrameCounter = frame % m_impl->images.size();
    m_impl->iFrameCounterUnclamped = m_impl->iFrameCounter;
}
void SkinImage::setDrawClipWidthPercent(f32 drawClipWidthPercent) {
    m_impl->fDrawClipWidthPercent = drawClipWidthPercent;
}

void SkinImage::setAnimationFrameClampUp() {
    if(m_impl->images.size() > 0 && m_impl->iFrameCounterUnclamped > m_impl->images.size() - 1)
        m_impl->iFrameCounter = m_impl->images.size() - 1;
}

vec2 SkinImage::getSize(bool animated) const {
    if(m_impl->images.size() > 0)
        return vec2{this->getImageForCurrentFrame(animated).img->getSize()} * this->getScale();
    else
        return this->getSizeBase();
}

vec2 SkinImage::getSizeBase() const { return m_impl->vBaseSizeForScaling2x * this->getResolutionScale(); }
vec2 SkinImage::getSizeBaseRawForScaling2x() const { return m_impl->vBaseSizeForScaling2x; }

vec2 SkinImage::getSizeBaseRaw(bool animated) const {
    return m_impl->vBaseSizeForScaling2x * this->getImageForCurrentFrame(animated).scale;
}

vec2 SkinImage::getImageSizeForCurrentFrame(bool animated) const {
    return this->getImageForCurrentFrame(animated).img->getSize();
}

float SkinImage::getScale(bool animated) const { return this->getImageScale(animated) * this->getResolutionScale(); }

float SkinImage::getImageScale(bool animated) const {
    if(m_impl->images.size() > 0)
        return m_impl->vBaseSizeForScaling2x.x / this->getSizeBaseRaw(animated).x;  // allow overscale and underscale
    else
        return 1.0f;
}

float SkinImage::getResolutionScale() const {
    return Osu::getRectScale(m_impl->vBaseSizeForScaling2x, m_impl->fOsuSize);
}

int SkinImage::getNumImages() const { return static_cast<int>(m_impl->images.size()); }
f32 SkinImage::getFrameDuration() const { return m_impl->fFrameDuration; }
u32 SkinImage::getFrameNumber() const { return m_impl->iFrameCounter; }
bool SkinImage::isMissingTexture() const {
    return m_impl->images.size() == 1 && m_impl->images[0].img == MISSING_TEXTURE;
}
bool SkinImage::isFromDefaultSkin() const { return m_impl->bIsFromDefaultSkin; }

bool SkinImage::isReady() const {
    if(m_impl->images.empty()) return false;
    if(m_impl->bReady) return true;

    for(auto& image : m_impl->images) {
        if(resourceManager->isLoadingResource(image.img)) return false;
    }

    if(m_impl->bHasNonAnimatedImage && m_impl->bCanDeleteNonAnimatedImage &&
       m_impl->nonAnimatedImage.img != MISSING_TEXTURE) {
        if(resourceManager->isLoadingResource(m_impl->nonAnimatedImage.img)) return false;
    }

    m_impl->bReady = true;
    return m_impl->bReady;
}

const SkinImage::IMAGE& SkinImage::getImageForCurrentFrame(bool animated) const {
    if(m_impl->images.size() > 0)
        return (!animated && m_impl->bHasNonAnimatedImage)
                   ? m_impl->nonAnimatedImage
                   : m_impl->images[m_impl->iFrameCounter % m_impl->images.size()];
    else {
        static IMAGE image{
            .img = MISSING_TEXTURE,
            .scale = 2.f,
        };

        return image;
    }
}
