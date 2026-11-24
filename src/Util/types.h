#pragma once
#include <cstdint>
#include <cstddef>

#include <sys/types.h>
// MSVC has special needs.
#ifdef _MSC_VER
#include <sys/timeb.h>
using sSz = ptrdiff_t;
#else
using sSz = ssize_t;
#endif

using uSz = size_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

// to avoid std::vector<bool> bs
using b8 = u8;

using f32 = float;
using f64 = double;

static_assert(sizeof(f32) == 4);
static_assert(sizeof(f64) == 8);
