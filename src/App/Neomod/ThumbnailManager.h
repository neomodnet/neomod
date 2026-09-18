// Copyright (c) 2025-2026, WH, All rights reserved.
#pragma once

#include "noinclude.h"
#include "StaticPImpl.h"
#include "types.h"

#include <string>

class Image;

struct ThumbIdentifier {
    std::string save_path;
    std::string download_url;  // url without scheme prepended
    i32 id{0};

    bool operator==(const ThumbIdentifier&) const = default;
};

class ThumbnailManager final {
    NOCOPY_NOMOVE(ThumbnailManager)
   public:
    ThumbnailManager();
    ~ThumbnailManager();

    // this is run during Osu::update(), while not in unpaused gameplay
    void update();

    // call this when you want to have some images ready soon
    // e.g. called by UIAvatar ctor to add new user id/folder avatar pairs to the loading queue (and tracking)
    void request_image(const ThumbIdentifier& identifier);

    // call this when you no longer care about some image you requested
    // e.g. called ~UIAvatar dtor (removes it from pending queue, to not load/download images we don't need)
    void discard_image(const ThumbIdentifier& identifier);

    // may return null if image is still loading
    [[nodiscard]] const Image* try_get_image(const ThumbIdentifier& identifier);

   private:
    struct Impl;
    StaticPImpl<Impl, 300> m_impl;
};
