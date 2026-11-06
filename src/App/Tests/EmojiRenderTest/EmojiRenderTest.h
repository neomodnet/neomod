// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#ifndef EMOJIRENDERTEST_H
#define EMOJIRENDERTEST_H

#include "App.h"

#include <memory>

class McFont;

namespace Mc::Tests {

class EmojiRenderTest : public App {
    NOCOPY_NOMOVE(EmojiRenderTest)
   public:
    EmojiRenderTest();
    ~EmojiRenderTest() override;

    void draw() override;
    void update() override;

    void onKeyDown(KeyboardEvent &e) override;

   private:
    McFont *m_font16{nullptr};
    McFont *m_font24{nullptr};
    McFont *m_font32{nullptr};
    McFont *m_font96{nullptr};
    bool m_showAtlas{false};
    bool m_stress{false};
    u32 m_stressOffset{0};
};

}  // namespace Mc::Tests

#endif
