// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#ifndef NEOMODENVINTEROP_H
#define NEOMODENVINTEROP_H

#include <span>
#include <string>
#include <string_view>

// TODO: maybe these should be static members of Osu:: ?
namespace neomod {
void *createInterop(void *envptr);

bool handle_osk(std::string_view osk_path, bool auto_select = true);
// opens files and urls the user asked us to open: beatmaps, replays, skins, databases, links
void handle_open_requests(std::span<const std::string> requests);
}  // namespace neomod

#endif
