// Copyright (c) 2025, WH, All rights reserved.
// global (engine-wide) configuration (constants etc.)
#pragma once

#include "config.h"

#define CASSERT_STR_ENDSWITH(str__, termchar__)                                  \
    static_assert(str__[(sizeof(str__) / sizeof((str__)[0]) - 2)] == termchar__, \
                  #str__ " (" str__ ") must end with " #termchar__)

#ifndef MCENGINE_DATA_DIR

#ifndef MCENGINE_DATA_ROOT
#define MCENGINE_DATA_ROOT "."
#endif

#define MCENGINE_DATA_DIR MCENGINE_DATA_ROOT "/"

#endif

CASSERT_STR_ENDSWITH(MCENGINE_DATA_DIR, '/');

/* *INDENT-OFF* */  // clang-format off

#define MCENGINE_LOCALE_PATH    MCENGINE_DATA_DIR "locale"
#define MCENGINE_IMAGES_PATH	MCENGINE_DATA_DIR "materials"
#define MCENGINE_FONTS_PATH		MCENGINE_DATA_DIR "fonts"
#define MCENGINE_SOUNDS_PATH	MCENGINE_DATA_DIR "sounds"
#define MCENGINE_SHADERS_PATH	MCENGINE_DATA_DIR "shaders"
#ifdef MCENGINE_PLATFORM_WASM
#define MCENGINE_CFG_PATH		"/persist/cfg"
#else
#define MCENGINE_CFG_PATH		MCENGINE_DATA_DIR "cfg"
#endif
#define MCENGINE_LIB_PATH		MCENGINE_DATA_DIR "lib"

/* *INDENT-ON* */  // clang-format on
