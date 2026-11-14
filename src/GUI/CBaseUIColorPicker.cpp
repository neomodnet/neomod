// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIColorPicker.h"

#include "CBaseUINumberField.h"
#include "CBaseUIStyledLabel.h"
#include "CBaseUIStyledTextbox.h"
#include "ContainerRanges.h"
#include "Engine.h"
#include "Graphics.h"
#include "KeyBindings.h"
#include "KeyboardEvent.h"
#include "Mouse.h"
#include "UIStyle.h"

#include "fmt/format.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace {
// units
constexpr float SQUARE_WIDTH{220.f};
constexpr float SQUARE_HEIGHT{140.f};
constexpr float STRIP_HEIGHT{16.f};
constexpr float CHANNEL_WIDTH{46.f};
constexpr float PREVIEW_WIDTH{64.f};

// a frame around the place a drag set, dark outside and light inside so that it shows on any colour
void drawMarker(const McRect &rect) {
    g->setColor(0xff000000);
    g->drawRect(rect);
    g->setColor(0xffffffff);
    g->drawRect(McRect{rect.getX() + 1.f, rect.getY() + 1.f, rect.getWidth() - 2.f, rect.getHeight() - 2.f});
}

std::string hexOf(Color c) { return fmt::format("#{:02X}{:02X}{:02X}", c.r, c.g, c.b); }

// "#RRGGBB" or "#RGB", with or without the #
std::optional<Color> parseHex(std::string_view text) {
    if(text.starts_with('#')) text.remove_prefix(1);
    if(text.size() != 6 && text.size() != 3) return std::nullopt;
    u32 number{0};
    for(const char c : text) {
        const int digit = c >= '0' && c <= '9'   ? c - '0'
                          : c >= 'a' && c <= 'f' ? c - 'a' + 10
                          : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                 : -1;
        if(digit < 0) return std::nullopt;
        // the digits in a short code count twice
        number = text.size() == 3 ? (number << 8) | (u32)(digit * 17) : (number << 4) | (u32)digit;
    }
    return Color{0xff000000u | number};
}
}  // namespace

// the saturations and values for the picker hue. a press or a drag sets both to where the cursor is (follow(), each way
// from 0 to 1), and the picker settles when the button is released
class CBaseUIColorPicker::Square : public CBaseUIElement {
    NOCOPY_NOMOVE(Square)
   public:
    Square(CBaseUIColorPicker &picker, std::string name)
        : CBaseUIElement(0, 0, 0, 0, std::move(name)), picker(picker) {}
    ~Square() override = default;

    void draw() override {
        if(!this->isVisible()) return;
        // every saturation in the hue from left to right, darkened to black from top to bottom
        const Color full = fromHSV(this->picker.hue, 1.f, 1.f);
        g->fillGradient(this->getPos(), this->getSize(), 0xffffffff, full, 0xffffffff, full);
        g->fillGradient(this->getPos(), this->getSize(), 0x00000000, 0x00000000, 0xff000000, 0xff000000);
        g->setColor(uiStyle().palette.border);
        g->drawRect(this->getRect());

        const float r = std::round(uiStyle().px(5.f));
        const vec2 at{std::round(this->getPos().x + this->picker.saturation * (this->getSize().x - 1.f)),
                      std::round(this->getPos().y + (1.f - this->picker.value) * (this->getSize().y - 1.f))};
        drawMarker({at.x - r, at.y - r, 2.f * r + 1.f, 2.f * r + 1.f});
    }
    [[nodiscard]] vec2 getNaturalSize() override { return {uiStyle().px(SQUARE_WIDTH), uiStyle().px(SQUARE_HEIGHT)}; }
    [[nodiscard]] bool isDragged() const { return this->pressed; }

   protected:
    virtual void follow(vec2 at) {
        this->picker.saturation = at.x;
        this->picker.value = 1.f - at.y;
    }

    void onMouseDownInside(bool left, bool right) override {
        CBaseUIElement::onMouseDownInside(left, right);
        if(!left) return;
        this->lockCapture();
        this->pressed = true;
        this->onCapturedMouseMove();
    }
    void onCapturedMouseMove() override {
        if(!this->pressed) return;
        const vec2 span = vec::max(this->getSize() - 1.f, vec2{1.f});
        this->follow(vec::clamp((mouse->getPos() - this->getPos()) / span, vec2{0.f}, vec2{1.f}));
        this->picker.show(fromHSV(this->picker.hue, this->picker.saturation, this->picker.value), true);
    }
    void onMouseUpInside(bool left, bool right) override {
        CBaseUIElement::onMouseUpInside(left, right);
        this->release();
    }
    void onMouseUpOutside(bool left, bool right) override {
        CBaseUIElement::onMouseUpOutside(left, right);
        this->release();
    }
    void onMouseCancel() override {
        if(this->pressed) this->picker.show(this->picker.settled, false);
        this->pressed = false;
        CBaseUIElement::onMouseCancel();
    }

    CBaseUIColorPicker &picker;

   private:
    void release() {
        if(!std::exchange(this->pressed, false)) return;
        this->picker.settle();
    }

    bool pressed{false};
};

// the hues, set the same way
class CBaseUIColorPicker::Strip final : public Square {
    NOCOPY_NOMOVE(Strip)
   public:
    using Square::Square;
    ~Strip() override = default;

    void draw() override {
        if(!this->isVisible()) return;
        // between two of the six colours a sixth of the way apart, one channel changes: a gradient is exact
        const float x = this->getPos().x, width = this->getSize().x;
        for(int i = 0; i < 6; i++) {
            const int from = (int)std::round(x + width * (float)i / 6.f);
            const int to = (int)std::round(x + width * (float)(i + 1) / 6.f);
            const Color left = fromHSV((float)i / 6.f, 1.f, 1.f), right = fromHSV((float)(i + 1) / 6.f, 1.f, 1.f);
            g->fillGradient(from, (int)this->getPos().y, to - from, (int)this->getSize().y, left, right, left, right);
        }
        g->setColor(uiStyle().palette.border);
        g->drawRect(this->getRect());

        const float r = std::round(uiStyle().px(2.f));
        const float at = std::round(x + this->picker.hue * (width - 1.f));
        drawMarker({at - r, this->getPos().y - 1.f, 2.f * r + 1.f, this->getSize().y + 2.f});
    }
    [[nodiscard]] vec2 getNaturalSize() override { return {uiStyle().px(SQUARE_WIDTH), uiStyle().px(STRIP_HEIGHT)}; }

   protected:
    void follow(vec2 at) override { this->picker.hue = at.x; }
};

// the colour to compare with (if there's one) and the colour shown, side by side
class CBaseUIColorPicker::Preview final : public CBaseUIElement {
    NOCOPY_NOMOVE(Preview)
   public:
    Preview(CBaseUIColorPicker &picker, std::string name)
        : CBaseUIElement(0, 0, 0, 0, std::move(name)), picker(picker) {}
    ~Preview() override = default;

    void draw() override {
        if(!this->isVisible()) return;
        const McRect &rect = this->getRect();
        const float half = this->picker.reference ? std::round(rect.getWidth() / 2.f) : 0.f;
        if(this->picker.reference) {
            g->setColor(*this->picker.reference);
            g->fillRect(McRect{rect.getX(), rect.getY(), half, rect.getHeight()});
        }
        g->setColor(this->picker.colour);
        g->fillRect(McRect{rect.getX() + half, rect.getY(), rect.getWidth() - half, rect.getHeight()});
        g->setColor(uiStyle().palette.border);
        g->drawRect(rect);
    }
    [[nodiscard]] vec2 getNaturalSize() override {
        return {uiStyle().px(PREVIEW_WIDTH), uiStyle().px(uiStyle().metrics.controlHeight)};
    }

   protected:
    void onMouseUpInside(bool left, bool right) override {
        CBaseUIElement::onMouseUpInside(left, right);
        if(!left || !this->picker.reference) return;
        if(mouse->getPos().x >= this->getPos().x + this->getSize().x / 2.f) return;
        this->picker.show(*this->picker.reference, false);
        this->picker.settle();
    }

   private:
    CBaseUIColorPicker &picker;
};

CBaseUIColorPicker::CBaseUIColorPicker(std::string name) : CBaseUIBox(Axis::COLUMN, std::move(name)) {
    const UIStyle &style = uiStyle();
    this->setGap(style.metrics.gap);

    this->square = new Square(*this, fmt::format("{}_square", this->getName()));
    this->strip = new Strip(*this, fmt::format("{}_hue", this->getName()));

    auto *numbers = new CBaseUIBox(Axis::ROW);
    numbers->setGap(style.metrics.gap);
    for(uSz i = 0; i < this->channels.size(); i++) {
        auto *field = new CBaseUINumberField(fmt::format("{}_{}", this->getName(), "rgb"[i]));
        field->setRange(0.0, 255.0)->setStep(1.0, 1.0)->setNaturalWidth(CHANNEL_WIDTH);
        field->setChangeCallback([this, i](f64 number) {
            Color c = this->colour;
            *std::array{&c.r, &c.g, &c.b}[i] = (u8)number;
            this->show(c, false);
            this->settle();
        });
        this->channels[i] = field;
        numbers->add(new CBaseUIStyledLabel(std::string{"RGB"[i]}), BoxItem::fit())->add(field, BoxItem::flex());
    }

    this->hex = new CBaseUIStyledTextbox(fmt::format("{}_hex", this->getName()));
    this->hex->setFilter([](char32_t c) {
        return (c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'f') || (c >= U'A' && c <= U'F') || c == U'#';
    });
    this->hex->setChangeCallback([this](std::string_view text) {
        if(const auto typed = parseHex(text)) {
            this->show(*typed, false);
            this->settle();
        }
        // as it's written here, or what it was if that was no colour
        this->hex->setText(hexOf(this->colour));
    });
    auto *code = new CBaseUIBox(Axis::ROW);
    code->setGap(style.metrics.gap);
    code->add(this->hex, BoxItem::flex())
        ->add(new Preview(*this, fmt::format("{}_preview", this->getName())), BoxItem::fit());

    this->add(this->square, BoxItem::fit())
        ->add(this->strip, BoxItem::fit())
        ->add(numbers, BoxItem::fit())
        ->add(code, BoxItem::fit());
    this->show(this->colour, false);
}

CBaseUIColorPicker::~CBaseUIColorPicker() = default;

CBaseUIColorPicker *CBaseUIColorPicker::setColor(Color c) {
    c.a = 255;
    this->settled = c;
    if(c != this->colour) this->show(c, false);
    return this;
}

CBaseUIColorPicker *CBaseUIColorPicker::setReference(Color c) {
    c.a = 255;
    this->reference = c;
    return this;
}

CBaseUIColorPicker *CBaseUIColorPicker::setChangeCallback(std::function<void(Color)> callback) {
    this->changeCallback = std::move(callback);
    return this;
}

bool CBaseUIColorPicker::isAdjusting() {
    return this->square->isDragged() || this->strip->isDragged() || this->hex->isFocused() ||
           Mc::ranges::any_of(this->channels,
                              [](CBaseUINumberField *field) { return field->isFocused() || field->isAdjusting(); });
}

void CBaseUIColorPicker::tick() {
    // a channel being dragged shows where it would be
    for(uSz i = 0; i < this->channels.size(); i++) {
        if(!this->channels[i]->isAdjusting()) continue;
        Color c = this->colour;
        *std::array{&c.r, &c.g, &c.b}[i] = (u8)this->channels[i]->getValue();
        if(c != this->colour) this->show(c, false);
    }
    CBaseUIBox::tick();
}

void CBaseUIColorPicker::onKeyDown(KeyboardEvent &e) {
    CBaseUIBox::onKeyDown(e);
    if(e.isConsumed() || !this->isVisible() || e != KEY_TAB) return;
    e.consume();

    const std::array<CBaseUIStyledTextbox *, 4> fields{this->channels[0], this->channels[1], this->channels[2],
                                                       this->hex};
    const auto focused = Mc::ranges::find_if(fields, [](CBaseUIStyledTextbox *field) { return field->isFocused(); });
    const uSz count = fields.size();
    const uSz step = (e.getModifiers() & KEYMOD_SHIFT) ? count - 1 : 1;
    fields[focused == fields.end() ? (step == 1 ? 0 : count - 1) : ((uSz)(focused - fields.begin()) + step) % count]
        ->visit();
}

Color CBaseUIColorPicker::fromHSV(float hue, float saturation, float value) {
    // one turn through the hues: the sixth it is in, and how far into it
    const float turn = (hue - std::floor(hue)) * 6.f;
    const int sixth = (int)turn % 6;
    const float into = turn - std::floor(turn);
    const float low = value * (1.f - saturation);
    const float falling = value * (1.f - saturation * into);
    const float rising = value * (1.f - saturation * (1.f - into));
    const std::array<std::array<float, 3>, 6> sixths{{{value, rising, low},
                                                      {falling, value, low},
                                                      {low, value, rising},
                                                      {low, falling, value},
                                                      {rising, low, value},
                                                      {value, low, falling}}};
    const auto byte = [](float f) { return (int)std::round(std::clamp(f, 0.f, 1.f) * 255.f); };
    return rgb(byte(sixths[sixth][0]), byte(sixths[sixth][1]), byte(sixths[sixth][2]));
}

void CBaseUIColorPicker::show(Color c, bool keepHSV) {
    this->colour = c;
    if(!keepHSV) {
        const float r = c.Rf(), green = c.Gf(), b = c.Bf();
        const float most = std::max({r, green, b});
        const float range = most - std::min({r, green, b});
        this->value = most;
        if(most > 0.f) this->saturation = range / most;
        if(range > 0.f) {
            const float sixths = most == r       ? (green - b) / range
                                 : most == green ? 2.f + (b - r) / range
                                                 : 4.f + (r - green) / range;
            this->hue = (sixths < 0.f ? sixths + 6.f : sixths) / 6.f;
        }
    }
    const std::array<u8, 3> parts{c.r, c.g, c.b};
    for(uSz i = 0; i < this->channels.size(); i++) {
        if(!this->channels[i]->isAdjusting()) this->channels[i]->setValue(parts[i]);
    }
    if(!this->hex->isFocused()) this->hex->setText(hexOf(c));
}

void CBaseUIColorPicker::settle() {
    if(this->colour == this->settled) return;
    this->settled = this->colour;
    if(this->changeCallback) this->changeCallback(this->colour);
}
