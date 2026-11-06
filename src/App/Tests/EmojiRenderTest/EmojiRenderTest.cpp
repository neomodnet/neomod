// Copyright (c) 2026, WH, All rights reserved.
#include "EmojiRenderTest.h"

#include "Engine.h"
#include "Logging.h"
#include "ResourceManager.h"
#include "Graphics.h"
#include "Font.h"
#include "KeyBindings.h"
#include "KeyboardEvent.h"
#include "UniString.h"

#include <string>

namespace Mc::Tests {

using std::string_view_literals::operator""sv;

EmojiRenderTest::EmojiRenderTest() {
    m_font16 = resourceManager->loadFont("weblysleekuisb", "EMOJI_TEST_16", 16, true);
    m_font24 = resourceManager->loadFont("weblysleekuisb", "EMOJI_TEST_24", 24, true);
    m_font32 = resourceManager->loadFont("weblysleekuisb", "EMOJI_TEST_32", 32, true);
    m_font96 = resourceManager->loadFont("weblysleekuisb", "EMOJI_TEST_96", 96, true);
}

EmojiRenderTest::~EmojiRenderTest() = default;

void EmojiRenderTest::draw() {
    constexpr float lineSpacing = 1.6f;
    constexpr float x = 20.f;
    float y = 30.f;

    constexpr Color BACKGROUND{rgb(30, 30, 30)};

    g->setColor(BACKGROUND);
    g->fillRect(engine->getScreenRect());

    auto drawLine = [&](McFont *font, std::string_view text, std::optional<TextFX> effects = std::nullopt) {
        g->pushTransform();
        g->translate(x, y);
        g->drawString(font, text, effects);
        g->popTransform();
        y += font->getHeight() * lineSpacing;
    };

    // -- emoji basics --
    g->setColor(0xffffffff);
    drawLine(m_font16, "😀🎉🌍🔥"sv);
    drawLine(m_font24, "😀🎉🌍🔥"sv);

    // mixed text + emoji
    drawLine(m_font24, "Hello 👋 World 🌍"sv);

    // colored text (emoji should stay unmodified)
    g->setColor(0xffff4444);
    drawLine(m_font24, "Red text 🔥"sv);
    g->setColor(0xff44ff44);
    drawLine(m_font24, "Green text 🎉"sv);

    // -- shadow effects --
    y += 10.f;
    g->setColor(0xffffffff);

    // hard shadow (text only)
    drawLine(m_font24, "Hard shadow"sv,
             TextFX{.col_text = rgb(255, 255, 255), .col_shadow = rgb(200, 100, 0), .offs_px = 1.5f});

    // hard shadow (mixed text + emoji)
    drawLine(m_font24, "Shadow 😀 emoji 🔥"sv,
             TextFX{.col_text = rgb(255, 255, 255), .col_shadow = rgb(200, 100, 0), .offs_px = 1.5f});

    // large shadow offset
    drawLine(m_font24, "Large offset shadow"sv,
             TextFX{.col_text = rgb(255, 255, 255), .col_shadow = rgb(0, 0, 0), .offs_px = 2.5f});

    // soft shadow
    drawLine(
        m_font24, "Soft shadow"sv,
        TextFX{
            .col_text = rgb(255, 255, 255), .col_shadow = rgb(0, 0, 0), .offs_px = 1.5f, .shadow_softness_px = 1.5f});

    // shadow with colored text
    drawLine(m_font24, "Colored + shadow"sv,
             TextFX{.col_text = rgb(100, 200, 255), .col_shadow = rgb(0, 0, 80), .offs_px = 1.f});

    // -- outline effects --
    y += 10.f;

    // outline only (no shadow)
    drawLine(m_font24, "Outline only"sv,
             TextFX{.col_text = rgb(255, 255, 255),
                    .col_shadow = argb(0, 0, 0, 0),
                    .col_outline = rgb(255, 0, 0),
                    .outline_px = 1.f});

    // outline + emoji
    drawLine(m_font24, "Outline 😀 emoji"sv,
             TextFX{.col_text = rgb(255, 255, 255),
                    .col_shadow = argb(0, 0, 0, 0),
                    .col_outline = rgb(0, 200, 100),
                    .outline_px = 1.f});

    // thick outline
    drawLine(m_font24, "Thick outline"sv,
             TextFX{.col_text = rgb(255, 255, 0),
                    .col_shadow = argb(0, 0, 0, 0),
                    .col_outline = rgb(80, 0, 0),
                    .outline_px = 2.f});

    // -- combined effects --
    y += 10.f;

    // shadow + outline
    drawLine(m_font24, "Shadow + outline"sv,
             TextFX{.col_text = rgb(255, 255, 255),
                    .col_shadow = rgb(0, 0, 0),
                    .offs_px = 1.5f,
                    .col_outline = rgb(255, 100, 0),
                    .outline_px = 1.f});

    // shadow + outline + emoji
    drawLine(m_font24, "All effects 🎉✨"sv,
             TextFX{.col_text = rgb(255, 255, 255),
                    .col_shadow = rgb(0, 0, 0),
                    .offs_px = 1.5f,
                    .col_outline = rgb(0, 100, 255),
                    .outline_px = 1.f,
                    .shadow_softness_px = 1.f});

    // semi-transparent shadow
    drawLine(m_font24, "Alpha shadow"sv,
             TextFX{.col_text = rgb(255, 255, 255), .col_shadow = argb(128, 255, 0, 0), .offs_px = 2.f});

    // -- misc --
    y += 10.f;
    g->setColor(0xffffffff);
    drawLine(m_font24, "Misc: ✅ ❌ ✨ ☀️"sv);

    // stress: a window sliding over the emoji blocks, more glyphs than the 96pt font's atlas has room for at its
    // maximum size, so it grows all the way and then evicts
    if(m_stress) {
        constexpr u32 FIRST = 0x1F300, RANGE = 0x700, PER_ROW = 10, ROWS = 4;
        g->setColor(0xffffffff);
        for(u32 row = 0; row < ROWS; row++) {
            std::u32string line;
            for(u32 i = 0; i < PER_ROW; i++) {
                line.push_back(static_cast<char32_t>(FIRST + (m_stressOffset + row * PER_ROW + i) % RANGE));
            }
            g->pushTransform();
            g->scale(0.4f, 0.4f);
            g->translate((f32)engine->getScreenWidth() / 2.f, 60.f + (f32)row * m_font96->getHeight() * 0.6f);
            g->drawString(m_font96, UniString::to_utf8(line));
            g->popTransform();
        }
        m_stressOffset += PER_ROW * ROWS;
    }

    // atlas debug
    if(m_showAtlas) {
        (m_stress ? m_font96 : m_font24)->drawTextureAtlas();
    }

    // instructions
    g->setColor(0xaa888888);
    g->pushTransform();
    g->translate(x, (f32)engine->getScreenHeight() - 30.f);
    g->drawString(m_font16, "Press TAB to toggle atlas view, S to toggle the glyph stress test"sv);
    g->popTransform();
}

void EmojiRenderTest::update() {}

void EmojiRenderTest::onKeyDown(KeyboardEvent &e) {
    if(e.getScanCode() == KEY_TAB) {
        m_showAtlas = !m_showAtlas;
        e.consume();
    } else if(e.getScanCode() == KEY_S) {
        m_stress = !m_stress;
        e.consume();
    }
}

}  // namespace Mc::Tests
