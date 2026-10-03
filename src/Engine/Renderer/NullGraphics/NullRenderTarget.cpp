// Copyright (c) 2026, WH, All rights reserved.
#include "NullRenderTarget.h"

NullRenderTarget::NullRenderTarget(int x, int y, int width, int height, MultisampleType multiSampleType)
    : RenderTarget(x, y, width, height, multiSampleType) {}

void NullRenderTarget::enable(bool /*clear*/) {}
void NullRenderTarget::disable() {}
void NullRenderTarget::bind(unsigned int /*textureUnit*/) {}
void NullRenderTarget::unbind() {}

void NullRenderTarget::init() { this->setReady(true); }
void NullRenderTarget::initAsync() { this->setAsyncReady(true); }
void NullRenderTarget::destroy() {}
