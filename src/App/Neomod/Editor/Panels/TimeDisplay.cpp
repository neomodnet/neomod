// Copyright (c) 2026, WH, All rights reserved.
#include "TimeDisplay.h"

#include "CBaseUIStyledLabel.h"
#include "CBaseUIStyledTextbox.h"
#include "ContainerRanges.h"
#include "EditorState.h"
#include "Font.h"
#include "KeyBindings.h"
#include "KeyboardEvent.h"
#include "SString.h"
#include "UIStyle.h"
#include "i18n.h"

#include "fmt/format.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <utility>

using TimingPoint = neomod::BeatmapFile::TimingPoint;

TimeDisplay::TimeDisplay(EditorState &state, std::function<void(const Timestamp &)> jump)
    : CBaseUIBox(Axis::COLUMN, "editor_time"), state(state), jump(std::move(jump)) {
    // a click anywhere on it types a time in
    this->bClickThroughSelf = false;
    this->setPadding(uiStyle().metrics.padding);
    this->time = new CBaseUIStyledLabel({}, "editor_clock");
    this->progress = new CBaseUIStyledLabel({}, "editor_progress");
    this->progress->setDim(true);
    this->field = new CBaseUIStyledTextbox("editor_clock_field");
    this->field->setVisible(false);
    this->add(this->time, BoxItem::fit())->add(this->field, BoxItem::fit())->add(this->progress, BoxItem::fit());
}

TimeDisplay::~TimeDisplay() = default;

std::string TimeDisplay::format(i32 timeMS) {
    const i32 t = std::abs(timeMS);
    return fmt::format("{}{:02}:{:02}:{:03}", timeMS < 0 ? "-" : "", t / 60000, t % 60000 / 1000, t % 1000);
}

std::optional<TimeDisplay::Timestamp> TimeDisplay::parse(std::string_view text) {
    // a whole number of up to `width` digits
    const auto number = [](std::string_view digits, uSz width) -> std::optional<i32> {
        if(digits.empty() || digits.size() > width ||
           !Mc::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; })) {
            return std::nullopt;
        }
        i32 value = 0;
        for(const char digit : digits) value = value * 10 + (digit - '0');
        return value;
    };

    Timestamp out{.time = 0, .combos = {}};
    text = SString::trimmed(text);
    if(const uSz comment = text.find(" -"); comment != std::string_view::npos)
        text = SString::trimmed(text.substr(0, comment));
    if(text.ends_with(')')) {
        const uSz open = text.rfind('(');
        if(open == std::string_view::npos) return std::nullopt;
        for(const std::string_view combo : SString::split(text.substr(open + 1, text.size() - open - 2), ',')) {
            const auto n = number(SString::trimmed(combo), 9);
            if(!n) return std::nullopt;
            out.combos.push_back(*n);
        }
        text = SString::trimmed(text.substr(0, open));
    }
    const bool negative = text.starts_with('-');
    if(negative) text.remove_prefix(1);
    if(const auto ms = number(text, 9)) {
        out.time = negative ? -*ms : *ms;
        return out;
    }
    const auto fields = SString::split(text, ':');
    if(fields.size() < 2 || fields.size() > 3) return std::nullopt;
    std::string_view seconds = fields[1];
    std::string_view millis = fields.size() == 3 ? fields[2] : std::string_view{};
    if(const uSz dot = seconds.find('.'); dot != std::string_view::npos && fields.size() == 2) {
        millis = seconds.substr(dot + 1);
        seconds = seconds.substr(0, dot);
    }
    // four digits: more minutes don't fit an i32 as milliseconds
    const auto m = number(fields[0], 4);
    const auto s = number(seconds, 2);
    const auto ms =
        millis.empty() && fields.size() == 2 && !fields[1].contains('.') ? std::optional{0} : number(millis, 3);
    if(!m || !s || *s >= 60 || !ms) return std::nullopt;
    const i32 t = *m * 60000 + *s * 1000 + *ms;
    out.time = negative ? -t : t;
    return out;
}

void TimeDisplay::onMouseDownInside(bool left, bool /*right*/) {
    if(!left || !this->state.document || this->field->isVisible()) return;
    this->taken = format(this->state.clock.getTime());
    this->field->setText(this->taken);
    this->time->setVisible(false);
    this->field->setVisible(true);
    this->requestLayout();
    this->field->visit();
}

void TimeDisplay::onKeyDown(KeyboardEvent &e) {
    if(this->field->isVisible() && (e == KEY_ENTER || e == KEY_NUMPAD_ENTER || e == KEY_ESCAPE)) {
        e.consume();
        this->endTyping();
        return;
    }
    CBaseUIBox::onKeyDown(e);
}

void TimeDisplay::endTyping() {
    if(!this->field->isVisible()) return;
    if(const std::string_view text = this->field->getText(); text != this->taken) {
        this->taken = text;
        if(const auto to = parse(text)) this->jump(*to);
    }
    this->field->setVisible(false);
    if(this->field->isFocused()) this->field->stealFocus();
    this->time->setVisible(true);
    this->requestLayout();
}

vec2 TimeDisplay::getNaturalSize() {
    const UIStyle &style = uiStyle();
    const McFont *font = style.font();
    // a time as long as the song, every digit the widest one, after a '-'
    float digit = 0.f;
    for(char32_t d = U'0'; d <= U'9'; d++) digit = std::max(digit, font->getGlyphWidth(d));
    const uSz digits = format((i32)this->state.clock.getLength()).size() - 2;
    float width = font->getStringWidth("-::") + digit * (float)digits;
    const std::array part{"100.0%", _("intro"), _("outro")};
    for(const char *where : part) {
        width = std::max(width, font->getStringWidth(tformat("{} kiai", where)));
    }

    vec2 natural = CBaseUIBox::getNaturalSize();
    natural.x = std::ceil(width) + 2.f * style.px(style.metrics.padding);
    return natural;
}

void TimeDisplay::tick() {
    // each field change that reads as a time is taken at once
    if(this->field->isVisible()) {
        if(!this->field->isFocused() || !this->state.document) {
            this->endTyping();
        } else if(const std::string_view text = this->field->getText(); text != this->taken) {
            this->taken = text;
            if(const auto to = parse(text)) this->jump(*to);
        }
    }
    const EditorState &s = this->state;
    if(!s.document) {
        this->time->setText({});
        this->progress->setText({});
    } else {
        const i32 at = s.clock.getTime();
        this->time->setText(format(at));

        // the point in force (inherited ones included) says whether it's kiai
        const auto points = s.document->getTimingPoints();
        const auto after = Mc::ranges::upper_bound(points, (f64)at, {}, &TimingPoint::time);
        const bool kiai = after != points.begin() && std::prev(after)->effects & TimingPoint::EFFECT_KIAI;
        const u32 length = s.clock.getLength();
        std::string where = at < 0                           ? _("intro")
                            : length > 0 && (u32)at > length ? _("outro")
                            : length > 0                     ? fmt::format("{:.1f}%", 100.0 * at / length)
                                                             : std::string{};
        if(kiai) where = where.empty() ? std::string{_("kiai")} : tformat("{} kiai", where);
        this->progress->setText(std::move(where));
    }
    CBaseUIBox::tick();
}
