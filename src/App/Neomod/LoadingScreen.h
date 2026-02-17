#pragma once
// Copyright (c) 2026, kiwec, All rights reserved.
#include "types.h"
#include "UIScreen.h"

#include <functional>

class LoadingScreen;
using LoadingProgressFn = std::function<f32(LoadingScreen *)>;
using LoadingFinishedFn = std::function<void(LoadingScreen *)>;

class LoadingScreen final : public UIOverlay {
    NOCOPY_NOMOVE(LoadingScreen)
   public:
    LoadingScreen(UIScreen *parent, LoadingProgressFn get_progress_fn, LoadingFinishedFn on_finished_fn)
        : UIOverlay(parent), on_finished_fn(std::move(on_finished_fn)), get_progress_fn(std::move(get_progress_fn)) {}

    ~LoadingScreen() final { this->finish(); }

    void update(CBaseUIEventCtx &c) final;
    void draw() final;

    void onKeyDown(KeyboardEvent &e) final;

    inline bool isVisible() final { return UIOverlay::isVisible() && this->get_progress_fn && this->on_finished_fn; }
    inline bool isFinished() { return this->progress >= 1.f || !this->get_progress_fn || !this->on_finished_fn; }

   private:
    friend LoadingFinishedFn;
    friend LoadingProgressFn;

    // private so you can't put LoadingScreen::finish inside the callback
    void finish();

    LoadingFinishedFn on_finished_fn;
    LoadingProgressFn get_progress_fn;

    f32 progress{0.f};
};
