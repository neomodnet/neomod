// Copyright (c) 2015, PG, 2026, WH, All rights reserved.
#include "BaseFrameworkTest.h"

#include "Engine.h"
#include "Mouse.h"
#include "ResourceManager.h"
#include "ConVar.h"
#include "Logging.h"
#include "Graphics.h"
#include "Font.h"
#include "UString.h"
#include "Image.h"
#include "VertexArrayObject.h"

#include "CBaseUIButton.h"

namespace Mc::Tests {
class FrameworkTestButton : public CBaseUIButton {
    NOCOPY_NOMOVE(FrameworkTestButton)
   public:
    FrameworkTestButton(float xPos, float yPos, float xSize, float ySize, UString name, UString text)
        : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name), std::move(text)) {}
    ~FrameworkTestButton() override = default;

    void onMouseInside() override { debugLog(""); }
    void onMouseOutside() override { debugLog(""); }
    void onMouseDownInside(bool left = true, bool right = false) override {
        debugLog("left: {} right: {}", left, right);
    }
    void onMouseDownOutside(bool left = true, bool right = false) override {
        debugLog("left: {} right: {}", left, right);
    }
    void onMouseUpInside(bool left = true, bool right = false) override { debugLog("left: {} right: {}", left, right); }
    void onMouseUpOutside(bool left = true, bool right = false) override {
        debugLog("left: {} right: {}", left, right);
    }
};

BaseFrameworkTest::BaseFrameworkTest() {
    debugLog("");

    m_testButton = std::make_unique<FrameworkTestButton>(300.f, 600.f, 200.f, 25.f, "TestButton", "TestButton");

    // load resource (NOTE: if it's already loaded it won't load again, and it's automatically destroyed on engine shutdown)
    resourceManager->loadImage("ic_music_48dp.png", "TESTIMAGE");

    // engine overrides
    cv::debug_mouse.setValue(true);

    mouse->addListener(this);
}

BaseFrameworkTest::~BaseFrameworkTest() {
    debugLog("");

    // cleanup
    mouse->removeListener(this);

    cv::debug_mouse.setValue(false);
}

void BaseFrameworkTest::draw() {
    McFont *testFont = engine->getDefaultFont();

    // test general drawing
    g->setColor(0xffff0000);
    int blockSize = 100;
    g->fillRect(engine->getScreenWidth() / 2 - blockSize / 2 + std::sin(engine->getTime() * 3) * 100,
                engine->getScreenHeight() / 2 - blockSize / 2 + std::sin(engine->getTime() * 3 * 1.5f) * 100, blockSize,
                blockSize);

    // test font texture atlas
    g->setColor(0xffffffff);
    g->pushTransform();
    g->translate(100, 100);
    testFont->drawTextureAtlas();
    g->popTransform();

    // test image
    Image *testImage = resourceManager->getImage("TESTIMAGE");
    g->setColor(0xffffffff);
    g->pushTransform();
    g->translate(testImage->getWidth() / 2 + 50, testImage->getHeight() / 2 + 100);
    g->drawImage(testImage);
    g->popTransform();

    assert(MISSING_TEXTURE);

    // test smooth clipping
    g->setColor(0xffffffff);
    g->pushTransform();
    {
        const float cx = engine->getScreenWidth() * 0.5f + 200;
        const float cy = engine->getScreenHeight() * 0.35f;
        const float hw = (float)MISSING_TEXTURE->getWidth() * 0.3f;
        const float hh = (float)MISSING_TEXTURE->getHeight() * 0.3f;
        g->translate(cx, cy);
        g->drawImage(MISSING_TEXTURE, AnchorPoint::CENTER, 20.0f, McRect(cx - hw, cy - hh, hw * 2, hh * 2));
    }
    g->popTransform();

    // test button
    m_testButton->draw();

    // test text
    UString testText = "It's working!";
    g->push3DScene(McRect(800, 300, testFont->getStringWidth(testText), testFont->getHeight()));
    {
        g->rotate3DScene(0, engine->getTime() * 200, 0);
        g->pushTransform();
        {
            g->translate(800, 300 + testFont->getHeight());
            g->drawString(testFont, testText);
        }
        g->popTransform();
    }
    g->pop3DScene();

    // test vao rgb triangle
    const float triangleSizeMultiplier = 125.0f;
    g->pushTransform();
    {
        g->translate(engine->getScreenWidth() * 0.75f - triangleSizeMultiplier * 0.5f,
                     engine->getScreenHeight() * 0.75f - triangleSizeMultiplier * 0.5f);

        VertexArrayObject vao;
        {
            vao.addVertex(-0.5f * triangleSizeMultiplier, 0.5f * triangleSizeMultiplier, 0.0f);
            vao.addColor(0xffff0000);

            vao.addVertex(0.0f, -0.5f * triangleSizeMultiplier, 0.0f);
            vao.addColor(0xff00ff00);

            vao.addVertex(0.5f * triangleSizeMultiplier, 0.5f * triangleSizeMultiplier, 0.0f);
            vao.addColor(0xff0000ff);
        }
        g->drawVAO(&vao);
    }
    g->popTransform();
}

void BaseFrameworkTest::update() {
    CBaseUIEventCtx c;
    m_testButton->update(c);
}

void BaseFrameworkTest::onResolutionChanged(vec2 newResolution) { debugLog("{}", newResolution); }

void BaseFrameworkTest::onDPIChanged() { debugLog("{}"); }

bool BaseFrameworkTest::isInGameplay() const {
    // debugLog("");
    return false;
}
bool BaseFrameworkTest::isInUnpausedGameplay() const {
    // debugLog("");
    return false;
}

void BaseFrameworkTest::stealFocus() { debugLog(""); }

bool BaseFrameworkTest::onShutdown() {
    debugLog("");
    return true;
}

Sound *BaseFrameworkTest::getSound(ActionSound action) const {
    debugLog("{}", static_cast<size_t>(action));
    return nullptr;
}

void BaseFrameworkTest::showNotification(const NotificationInfo &notif) {
    debugLog("text: {} color: {} duration: {} class: {} preset: {} cb: {:p}", notif.text, notif.custom_color,
             notif.duration, static_cast<size_t>(notif.nclass), static_cast<size_t>(notif.preset),
             fmt::ptr(&notif.callback));
    if(notif.callback) {
        notif.callback();
    }
}

void BaseFrameworkTest::onFocusGained() { debugLog(""); }

void BaseFrameworkTest::onFocusLost() { debugLog(""); }

void BaseFrameworkTest::onMinimized() { debugLog(""); }

void BaseFrameworkTest::onRestored() { debugLog(""); }

void BaseFrameworkTest::onKeyDown(KeyboardEvent &e) { debugLog("keyDown: {}", e.getScanCode()); }

void BaseFrameworkTest::onKeyUp(KeyboardEvent &e) { debugLog("keyUp: {}", e.getScanCode()); }

void BaseFrameworkTest::onChar(KeyboardEvent &e) {
    const char16_t charray[]{e.getCharCode(), u'\0'};
    debugLog("charCode: {}", UString{&charray[0]});
}

void BaseFrameworkTest::onButtonChange(ButtonEvent event) {
    debugLog("button: {} down: {} timestamp: {}", static_cast<size_t>(event.btn), event.down, event.timestamp);
}
void BaseFrameworkTest::onWheelVertical(int delta) { debugLog("{}", delta); }
void BaseFrameworkTest::onWheelHorizontal(int delta) { debugLog("{}", delta); }

}  // namespace Mc::Tests
