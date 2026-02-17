// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#ifndef NEOMODENVINTEROP_H
#define NEOMODENVINTEROP_H

// TODO: maybe these should be static members of Osu:: ?
namespace neomod {
void *createInterop(void *envptr);
void handleExistingWindow(int argc, char *argv[]);
}  // namespace neomod

#endif
