#pragma once
// Copyright (c) 2013, PG, All rights reserved.
#include "Resource.h"
#include "Graphics_fwd.h"

#include "Color.h"
#include "Vectors.h"

#include <memory>

class ConVar;

class RenderTarget : public Resource {
    NOCOPY_NOMOVE(RenderTarget)
   public:
    RenderTarget(int x, int y, int width, int height,
                 MultisampleType multiSampleType = MultisampleType::X0);
    ~RenderTarget() override;

    virtual void draw(int x, int y);
    virtual void draw(int x, int y, int width, int height);
    virtual void drawRect(int x, int y, int width, int height);

    virtual void enable() = 0;
    virtual void disable() = 0;

    virtual void bind(unsigned int textureUnit = 0) = 0;
    virtual void unbind() = 0;

    void rebuild(int x, int y, int width, int height, MultisampleType multiSampleType);
    void rebuild(int x, int y, int width, int height);
    void rebuild(int width, int height);
    void rebuild(int width, int height, MultisampleType multiSampleType);

    // set
    void setPos(int x, int y) {
        this->vPos.x = x;
        this->vPos.y = y;
    }
    void setPos(vec2 pos) { this->vPos = pos; }
    void setColor(Color color) { this->color = color; }
    void setClearColor(Color clearColor) { this->clearColor = clearColor; }
    void setClearColorOnDraw(bool clearColorOnDraw) { this->bClearColorOnDraw = clearColorOnDraw; }
    void setClearDepthOnDraw(bool clearDepthOnDraw) { this->bClearDepthOnDraw = clearDepthOnDraw; }

    // get
    [[nodiscard]] float getWidth() const { return this->vSize.x; }
    [[nodiscard]] float getHeight() const { return this->vSize.y; }
    [[nodiscard]] inline vec2 getSize() const { return this->vSize; }
    [[nodiscard]] inline vec2 getPos() const { return this->vPos; }
    [[nodiscard]] inline MultisampleType getMultiSampleType() const { return this->multiSampleType; }

    [[nodiscard]] inline bool isMultiSampled() const {
        if constexpr(Env::cfg(OS::WASM)) return false;
        return this->multiSampleType != MultisampleType{};
    }

    RenderTarget *asRenderTarget() final { return this; }
    [[nodiscard]] const RenderTarget *asRenderTarget() const final { return this; }

   protected:
    void init() override = 0;
    void initAsync() override = 0;
    void destroy() override = 0;

    std::unique_ptr<VertexArrayObject> vao1;
    std::unique_ptr<VertexArrayObject> vao2;

    vec2 vPos{0.f};
    vec2 vSize{0.f};

    Color color{static_cast<uint32_t>(-1)};
    Color clearColor{0};

    MultisampleType multiSampleType;

    bool bClearColorOnDraw{true};
    bool bClearDepthOnDraw{true};
};
