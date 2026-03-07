#pragma once
// instantiate (non-pointer) types you need CDynArray for in here
#if (defined(_CLANGD) || defined(Q_CREATOR_RUN) || defined(__INTELLISENSE__) || defined(__CDT_PARSER__) || \
     defined(__clang_analyzer__))
#define IS_ANALYZER_
#ifndef INSTANTIATE_CDYNARRAY
#include "CDynArray.h"
#endif
#endif

#ifndef INSTANTIATE_CDYNARRAY
#ifndef IS_ANALYZER_
#error "only CDynArray.cpp should include this header"
#endif
#endif

#include "Color.h"
#include "Vectors.h"

template struct Mc::CDynArray<Color>;

template struct Mc::CDynArray<vec2>;
template struct Mc::CDynArray<vec3>;
template struct Mc::CDynArray<vec4>;

// instantiate for common types

template struct Mc::CDynArray<u8>;
template struct Mc::CDynArray<u16>;
template struct Mc::CDynArray<u32>;
template struct Mc::CDynArray<u64>;

template struct Mc::CDynArray<i8>;
template struct Mc::CDynArray<i16>;
template struct Mc::CDynArray<i32>;
template struct Mc::CDynArray<i64>;

template struct Mc::CDynArray<f32>;
template struct Mc::CDynArray<f64>;

#include "config.h"

#ifdef MCENGINE_PLATFORM_WASM
// idk why?
template struct Mc::CDynArray<unsigned long>;
#endif

#ifdef MCENGINE_FEATURE_SDLGPU
#include "SDLGPUInterface.h"
template struct Mc::CDynArray<SDLGPUInterface::DrawCommand>;
template struct Mc::CDynArray<SDLGPUSimpleVertex>;
#endif

#ifdef MCENGINE_FEATURE_DIRECTX11
#include "DirectX11Interface.h"
template struct Mc::CDynArray<DirectX11Interface::SimpleVertex>;
#endif

#undef IS_ANALYZER_
