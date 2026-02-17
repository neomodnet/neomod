// Copyright (c) 2020, PG, All rights reserved.
#ifndef OSUBACKGROUNDIMAGEHANDLER_H
#define OSUBACKGROUNDIMAGEHANDLER_H

#include "noinclude.h"
#include "types.h"
#include "StaticPImpl.h"

struct BGImageHandlerImpl;

class Image;
class DatabaseBeatmap;

class BGImageHandler final {
    NOCOPY_NOMOVE(BGImageHandler)
   public:
    BGImageHandler();
    ~BGImageHandler();

    static void draw(const Image *backgroundImage, f32 alpha = 1.f);
    void draw(const DatabaseBeatmap *beatmap, f32 alpha = 1.f);
    void update(bool allowEviction);
    const Image *getLoadBackgroundImage(const DatabaseBeatmap *beatmap, bool load_immediately = false,
                                        bool allow_menubg_fallback = true);

    void scheduleFreezeCache();

   private:
    friend struct BGImageHandlerImpl;
    StaticPImpl<BGImageHandlerImpl, 256> pImpl;
};

#endif
