// Copyright (c) 2016, PG, All rights reserved.
#include "Graphics.h"

#include "Camera.h"
#include "ConVar.h"
#include "Engine.h"
#include "Environment.h"
#include "Paths.h"
#include "Image.h"
#include "Logging.h"

#include "Graphics_private.h"

#include <algorithm>
#include <cmath>
#include <utility>

vec2 Graphics::getAnchoredOrigin(AnchorPoint anchor, vec2 size) {
    vec2 ret;
    switch(anchor) {
        case AnchorPoint::CENTER:
            ret.x = -size.x / 2;
            ret.y = -size.y / 2;
            break;
        case AnchorPoint::TOP_LEFT:
            ret.x = 0;
            ret.y = 0;
            break;
        case AnchorPoint::TOP_RIGHT:
            ret.x = -size.x;
            ret.y = 0;
            break;
        case AnchorPoint::BOTTOM_LEFT:
            ret.x = 0;
            ret.y = -size.y;
            break;
        case AnchorPoint::LEFT:
            ret.x = 0;
            ret.y = -size.y / 2;
            break;
        default:
            if constexpr(Env::cfg(BUILD::DEBUG)) {
                debugLog("AnchorPoint {} not implemented", static_cast<size_t>(anchor));
                ret.x = 0;
                ret.y = 0;
            } else {
                std::unreachable();
            }
            break;
    }
    return ret;
}

void Graphics::takeScreenshot(ScreenshotParams params) { m_data->pendingScreenshots.push_back(std::move(params)); }

void Graphics::takeScreenshot(std::string_view savePath) {
    std::string path{savePath};
    if(!path.empty() && !Environment::isAbsolutePath(path)) path = fmt::format("{}/{}", Mc::Paths::data(), path);
    takeScreenshot(ScreenshotParams{std::move(path), {}, false});
}

void Graphics::processPendingScreenshot() {
    if(m_data->pendingScreenshots.empty()) return;

    for(auto &screenshot : m_data->pendingScreenshots) {
        auto &savePath = screenshot.savePath;
        auto &callback = screenshot.dataCB;

        if(savePath.empty() && !callback) {
            static i32 num = 0;
            Environment::createDirectory(Mc::Paths::screenshots());
            while(Environment::fileExists(fmt::format("{}/test_screenshot{}.png", Mc::Paths::screenshots(), num)))
                num++;
            savePath = fmt::format("{}/test_screenshot{}.png", Mc::Paths::screenshots(), num);
        }

        std::vector<u8> pixels = this->getScreenshot(screenshot.withAlpha);
        if(pixels.empty()) {
            if(callback) {
                callback({});
            } else {
                debugLog("failed to get pixel data (tried to save to {})", savePath);
            }
            continue;
        }

        if(callback) {
            callback(std::move(pixels));
        } else {
            const auto res = this->getResolution();
            if(Image::saveToImage(pixels.data(), (i32)res.x, (i32)res.y, screenshot.withAlpha ? 4 : 3, savePath)) {
                debugLog("saved to {}", savePath);
            }
        }
    }
    m_data->pendingScreenshots.clear();
}

Graphics::Graphics() : m_data() {
    // init matrix stacks
    m_data->bTransformUpToDate = false;
    m_data->worldTransformStack.emplace_back();
    m_data->projectionTransformStack.emplace_back();

    // init 3d gui scene stack
    m_data->bIs3dScene = false;
    m_data->scene3d_stack.push_back(false);

    cv::vsync.setCallback([](float on) -> void { return !!g ? g->setVSync(!!static_cast<int>(on)) : (void)0; });
}

Graphics::~Graphics() = default;

void Graphics::setBlending(bool enabled) { m_data->bBlendingEnabled = enabled; }
bool Graphics::getBlending() const { return m_data->bBlendingEnabled; }
void Graphics::setBlendMode(DrawBlendMode blendMode) { m_data->currentBlendMode = blendMode; }
DrawBlendMode Graphics::getBlendMode() const { return m_data->currentBlendMode; }
Color Graphics::getColor() const { return m_data->color; }

void Graphics::drawRectf(const RectOptions &opts) {
    // the edges are filled bands rather than lines to avoid different rasterization behavior across backends
    const float t = std::max(opts.lineThickness, 1.f);  // a thinner band can miss every pixel center of an edge
    const float ht = t / 2.f;
    const float x = opts.x, y = opts.y, w = opts.width, h = opts.height;

    if(opts.withColor) this->setColor(opts.top);
    this->fillRectf(x - ht, y - ht, w, t);
    if(opts.withColor) this->setColor(opts.left);
    this->fillRectf(x - ht, y + ht, t, h);
    if(opts.withColor) this->setColor(opts.bottom);
    this->fillRectf(x + ht, y + h - ht, w, t);
    if(opts.withColor) this->setColor(opts.right);
    this->fillRectf(x + w - ht, y - ht, t, h);
}

void Graphics::pushTransform() {
    m_data->worldTransformStack.push_back(m_data->worldTransformStack.back());
    m_data->projectionTransformStack.push_back(m_data->projectionTransformStack.back());
}

void Graphics::popTransform() {
    if(m_data->worldTransformStack.size() < 2) {
        engine->showMessageErrorFatal("World Transform Stack Underflow", "Too many pop*()s!");
        engine->shutdown();
        return;
    }

    if(m_data->projectionTransformStack.size() < 2) {
        engine->showMessageErrorFatal("Projection Transform Stack Underflow", "Too many pop*()s!");
        engine->shutdown();
        return;
    }

    m_data->worldTransformStack.pop_back();
    m_data->projectionTransformStack.pop_back();
    m_data->bTransformUpToDate = false;
}

void Graphics::translate(float x, float y, float z) {
    m_data->worldTransformStack.back().translate(x, y, z);
    m_data->bTransformUpToDate = false;
}

void Graphics::rotate(float deg, float x, float y, float z) {
    m_data->worldTransformStack.back().rotate(deg, x, y, z);
    m_data->bTransformUpToDate = false;
}

void Graphics::scale(float x, float y, float z) {
    m_data->worldTransformStack.back().scale(x, y, z);
    m_data->bTransformUpToDate = false;
}

void Graphics::translate3D(float x, float y, float z) {
    Matrix4 translation;
    translation.translate(x, y, z);
    this->setWorldMatrixMul(translation);
}

void Graphics::rotate3D(float deg, float x, float y, float z) {
    Matrix4 rotation;
    rotation.rotate(deg, x, y, z);
    this->setWorldMatrixMul(rotation);
}

void Graphics::setWorldMatrix(Matrix4 &worldMatrix) {
    m_data->worldTransformStack.pop_back();
    m_data->worldTransformStack.push_back(worldMatrix);
    m_data->bTransformUpToDate = false;
}

void Graphics::setWorldMatrixMul(Matrix4 &worldMatrix) {
    m_data->worldTransformStack.back() *= worldMatrix;
    m_data->bTransformUpToDate = false;
}

void Graphics::setProjectionMatrix(Matrix4 &projectionMatrix) {
    m_data->projectionTransformStack.pop_back();
    m_data->projectionTransformStack.push_back(projectionMatrix);
    m_data->bTransformUpToDate = false;
}

Matrix4 Graphics::getWorldMatrix() const { return m_data->worldTransformStack.back(); }
Matrix4 Graphics::getProjectionMatrix() const { return m_data->projectionTransformStack.back(); }
Matrix4 Graphics::getMVP() const { return m_data->MP; }

void Graphics::push3DScene(const McRect &region) {
    if(cv::r_debug_disable_3dscene.getBool()) return;

    // you can't yet stack 3d scenes!
    if(m_data->scene3d_stack.back()) {
        m_data->scene3d_stack.push_back(false);
        return;
    }

    // reset & init
    m_data->v3dSceneOffset = vec3{};
    constexpr float fov = 60.0f;

    // push true, set region
    m_data->bIs3dScene = true;
    m_data->scene3d_stack.push_back(true);
    m_data->scene3d_region = region;

    // backup transforms
    this->pushTransform();

    // calculate height to fit viewport angle
    float angle = (180.0f - fov) / 2.0f;
    float b = (engine->getScreenHeight() / std::sin(vec::radians(fov))) * std::sin(vec::radians(angle));
    float hc = std::sqrt(std::pow(b, 2.0f) - std::pow((engine->getScreenHeight() / 2.0f), 2.0f));

    // set projection matrix
    Matrix4 trans2 = Matrix4().translate(-1 + (region.getWidth()) / (float)engine->getScreenWidth() +
                                             (region.getX() * 2) / (float)engine->getScreenWidth(),
                                         1 - region.getHeight() / (float)engine->getScreenHeight() -
                                             (region.getY() * 2) / (float)engine->getScreenHeight(),
                                         0);
    Matrix4 projectionMatrix =
        trans2 * Camera::buildMatrixPerspectiveFov(
                     vec::radians(fov), ((float)engine->getScreenWidth()) / ((float)engine->getScreenHeight()),
                     cv::r_3dscene_zn.getFloat(), cv::r_3dscene_zf.getFloat());
    m_data->scene3d_projection_matrix = projectionMatrix;

    // set world matrix
    Matrix4 trans = Matrix4().translate(-(float)region.getWidth() / 2 - region.getX(),
                                        -(float)region.getHeight() / 2 - region.getY(), 0);
    m_data->scene3d_world_matrix = Camera::buildMatrixLookAt(vec3(0, 0, -hc), vec3(0, 0, 0), vec3(0, -1, 0)) * trans;

    // force transform update
    this->updateTransform(true);
}

void Graphics::pop3DScene() {
    if(!m_data->scene3d_stack.back()) return;

    m_data->scene3d_stack.pop_back();

    // restore transforms
    this->popTransform();

    m_data->bIs3dScene = false;
}

void Graphics::translate3DScene(float x, float y, float z) {
    if(!m_data->scene3d_stack.back()) return;  // block if we're not in a 3d scene

    // translate directly
    m_data->scene3d_world_matrix.translate(x, y, z);

    // force transform update
    this->updateTransform(true);
}

void Graphics::rotate3DScene(float rotx, float roty, float rotz) {
    if(!m_data->scene3d_stack.back()) return;  // block if we're not in a 3d scene

    // first translate to the center of the 3d region, then rotate, then translate back
    Matrix4 rot;
    vec3 centerVec =
        vec3(m_data->scene3d_region.getX() + m_data->scene3d_region.getWidth() / 2 + m_data->v3dSceneOffset.x,
             m_data->scene3d_region.getY() + m_data->scene3d_region.getHeight() / 2 + m_data->v3dSceneOffset.y,
             m_data->v3dSceneOffset.z);
    rot.translate(-centerVec);

    // rotate
    if(rotx != 0) rot.rotateX(-rotx);
    if(roty != 0) rot.rotateY(-roty);
    if(rotz != 0) rot.rotateZ(-rotz);

    rot.translate(centerVec);

    // apply the rotation
    m_data->scene3d_world_matrix = m_data->scene3d_world_matrix * rot;

    // force transform update
    this->updateTransform(true);
}

void Graphics::offset3DScene(float x, float y, float z) { m_data->v3dSceneOffset = vec3(x, y, z); }

void Graphics::forceUpdateTransform() { this->updateTransform(); }

void Graphics::updateTransform(bool force) {
    if(!m_data->bTransformUpToDate || force) {
        m_data->worldMatrix = m_data->worldTransformStack.back();
        m_data->projectionMatrix = m_data->projectionTransformStack.back();

        // HACKHACK: 3d gui scenes
        if(m_data->bIs3dScene) {
            m_data->worldMatrix = m_data->scene3d_world_matrix * m_data->worldTransformStack.back();
            m_data->projectionMatrix = m_data->scene3d_projection_matrix;
        }

        m_data->MP = m_data->projectionMatrix * m_data->worldMatrix;

        this->onTransformUpdate();

        m_data->bTransformUpToDate = true;
    }
}

void Graphics::checkStackLeaks() {
    if(m_data->worldTransformStack.size() > 1) {
        engine->showMessageErrorFatal("World Transform Stack Leak", "Make sure all push*() have a pop*()!");
        engine->shutdown();
    }

    if(m_data->projectionTransformStack.size() > 1) {
        engine->showMessageErrorFatal("Projection Transform Stack Leak", "Make sure all push*() have a pop*()!");
        engine->shutdown();
    }

    if(m_data->scene3d_stack.size() > 1) {
        engine->showMessageErrorFatal("3DScene Stack Leak", "Make sure all push*() have a pop*()!");
        engine->shutdown();
    }
}
