// Copyright (c) 2025-2026, WH, All rights reserved.
// neomod-wide configuration (constants etc.)
#pragma once

#include "EngineConfig.h"

#ifndef NEOMOD_DATA_DIR
#define NEOMOD_DATA_DIR MCENGINE_DATA_DIR
#endif

/* *INDENT-OFF* */  // clang-format off

#define NEOMOD_CFG_PATH			NEOMOD_DATA_DIR "cfg"
#define NEOMOD_MAPS_PATH		NEOMOD_DATA_DIR "maps"
#define NEOMOD_REPLAYS_PATH		NEOMOD_DATA_DIR "replays"
#define NEOMOD_SCREENSHOTS_PATH	NEOMOD_DATA_DIR "screenshots"
#define NEOMOD_SKINS_PATH		NEOMOD_DATA_DIR "skins"
#define NEOMOD_DB_DIR			NEOMOD_DATA_DIR // default is top-level, next to exe

CASSERT_STR_ENDSWITH(NEOMOD_DATA_DIR, '/');
CASSERT_STR_ENDSWITH(NEOMOD_DB_DIR, '/');

/* *INDENT-ON* */  // clang-format on
