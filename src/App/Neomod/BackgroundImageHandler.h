// Copyright (c) 2020, PG, All rights reserved.
#ifndef OSUBACKGROUNDIMAGEHANDLER_H
#define OSUBACKGROUNDIMAGEHANDLER_H

#include "noinclude.h"
#include "types.h"
#include "StaticPImpl.h"

#include <string_view>

struct BGImageHandlerImpl;

class Image;
class DatabaseBeatmap;

class BGImageHandler final {
    NOCOPY_NOMOVE(BGImageHandler)
   public:
    BGImageHandler();
    ~BGImageHandler();

    [[nodiscard]] bool drawLastImage(f32 alpha = 1.f) const;  // at least attempt to
    void draw(const Image *backgroundImage, f32 alpha = 1.f) const;
    // try to load and draw
    void draw(const DatabaseBeatmap *beatmap, f32 alpha = 1.f);
    void update(bool allowEviction);
    const Image *getLoadBackgroundImage(const DatabaseBeatmap *beatmap, bool load_immediately = false,
                                        bool allow_menubg_fallback = true);
    // drops what's cached for a set folder (the one a map's getFolder() names) whose files changed, so it's read again
    void forgetFolder(std::string_view folder);

    void scheduleFreezeCache();

   private:
    friend struct BGImageHandlerImpl;
    StaticPImpl<BGImageHandlerImpl, 256> pImpl;
};

#endif
