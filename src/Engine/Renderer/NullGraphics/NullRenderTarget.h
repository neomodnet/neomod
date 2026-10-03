// Copyright (c) 2026, WH, All rights reserved.
// dummy rendertarget
#pragma once
#include "RenderTarget.h"

class NullRenderTarget : public RenderTarget {
   public:
    NullRenderTarget(int x, int y, int width, int height, MultisampleType multiSampleType = MultisampleType{0});

    void enable(bool clear) override;
    void disable() override;
    void bind(unsigned int textureUnit = 0) override;
    void unbind() override;

   protected:
    void init() override;
    void initAsync() override;
    void destroy() override;
};
