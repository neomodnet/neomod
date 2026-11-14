#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "noinclude.h"

#include <string>

class Sound;

// plays sound files one at a time, each from its start: for hearing a file before it's used
class SamplePreview final {
    NOCOPY_NOMOVE(SamplePreview)
   public:
    SamplePreview() = default;
    ~SamplePreview();

    // stops the last one and plays the file at `path` (empty: only stops)
    void play(const std::string &path);

   private:
    Sound *sound{nullptr};
};
