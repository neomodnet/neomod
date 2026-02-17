#pragma once
// Copyright (c) 2025, kiwec, All rights reserved.

#include "config.h"

#include <string_view>

namespace neomod {
void handle_neomod_url(std::string_view url);
}

#define NEOMOD_URL_SCHEME PACKAGE_NAME "://"
