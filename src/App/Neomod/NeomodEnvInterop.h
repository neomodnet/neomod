// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#ifndef NEOMODENVINTEROP_H
#define NEOMODENVINTEROP_H

#include <string_view>

// TODO: maybe these should be static members of Osu:: ?
namespace neomod {
void *createInterop(void *envptr);
void handleExistingWindow(int argc, char *argv[]);

bool handle_osk(std::string_view osk_path);
}  // namespace neomod

#endif
