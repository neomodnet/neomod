// Copyright (c) 2026, WH, All rights reserved.
#include "ConsoleWindow.h"

#include "CBaseUIButton.h"
#include "CBaseUIContainer.h"
#include "CBaseUISelectableTextView.h"
#include "ConVar.h"
#include "Console.h"
#include "ConsoleWidgets.h"
#include "Engine.h"
#include "Environment.h"
#include "Font.h"
#include "Graphics.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "MakeDelegateWrapper.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <utility>

// the scrollback: draws wrapped lines straight from its own copy of the log instead of holding one element per
// line; scrolling/scrollbar/wheel and the drag-selection (ctrl+c copies, see ConsoleWindow) come from the bases
class ConsoleLogView final : public CBaseUISelectableTextView {
    NOCOPY_NOMOVE(ConsoleLogView)
   public:
    ConsoleLogView(float xPos, float yPos, float xSize, float ySize, std::string name)
        : CBaseUISelectableTextView(xPos, yPos, xSize, ySize, std::move(name)),
          font(engine->getConsoleFont()),
          fLastHeight(ySize) {
        this->setDrawBackground(false);  // the window body shows through, the base only adds the frame + scrollbar
        this->setHorizontalScrolling(false);
        this->setVerticalScrolling(true);
    }
    ~ConsoleLogView() override = default;

    void draw() override;
    void tick() override;

    void onResized() override;

    // dir > 0 = down
    void page(int dir) { this->scrollY(-dir * (int)this->getSize().y); }

   protected:
    [[nodiscard]] size_t getRunCount() const override { return this->lines.size(); }
    [[nodiscard]] TextRun getRun(size_t index) const override;

   private:
    struct Line {
        std::string text;
        Color color;
        float width;  // of the text, in font units
        u64 entrySeq;
    };

    // the line at the view's bottom edge, kept there across relayouts (by log entry, so it survives a re-wrap)
    struct Anchor {
        bool atBottom;
        u64 entrySeq;
        size_t subLine;
        f64 fraction;
    };

    void rewrap(Console::LogRange range);
    void append(Console::LogRange range);
    void wrapInto(u64 fromSeq, u64 toSeq);
    void updateScrollSize();
    [[nodiscard]] Anchor captureAnchor(f64 viewHeight) const;
    void applyAnchor(const Anchor &anchor);

    [[nodiscard]] float getTextScale() const { return Mc::consoleLogScale(env->getDPIScale()); }
    [[nodiscard]] float getLineHeight() const {
        return std::round((this->font->getHeight() + 3.f) * this->getTextScale());
    }
    [[nodiscard]] float getPadding() const { return 2.f * this->getTextScale(); }
    [[nodiscard]] float getTextLeft() const { return this->getContentOrigin().x + this->getPadding(); }
    // screen y of the first line
    [[nodiscard]] float getContentTop() const { return this->getContentOrigin().y + this->getPadding(); }

    std::deque<Line> lines;
    McFont *font;
    float fWrapWidth{-1.f};
    f64 fLastHeight;
    Console::LogRange logRange{.first = 0, .next = 0};  // the scrollback range the lines were built from
    bool bRewrapPending{true};
    Anchor pendingAnchor{.atBottom = true, .entrySeq = 0, .subLine = 0, .fraction = 0.};
};

void ConsoleLogView::draw() {
    if(!this->isVisible()) return;

    const float scale = this->getTextScale();
    const float lineHeight = this->getLineHeight();
    const float top = this->getContentTop();

    // only the lines inside the view
    const float scrollY = top - this->getPos().y;
    const size_t firstLine = static_cast<size_t>(std::max(0.f, std::floor(-scrollY / lineHeight)));
    const size_t lastLine = std::min(
        this->lines.size(), static_cast<size_t>(std::max(0.f, (this->getSize().y - scrollY) / lineHeight)) + 1);

    this->drawSelection();

    g->pushClipRect(this->getClipRect());
    {
        const float x = this->getTextLeft();
        const float firstBaseline = top + (this->font->getHeight() + 1) * scale;
        for(size_t i = firstLine; i < lastLine; i++) {
            const Line &line = this->lines[i];
            g->setColor(line.color);
            g->pushTransform();
            {
                g->scale(scale, scale);
                g->translate((int)x, (int)(firstBaseline + i * lineHeight));
                g->drawString(this->font, line.text);
            }
            g->popTransform();
        }
    }
    g->popClipRect();

    CBaseUIScrollView::draw();
}

void ConsoleLogView::tick() {
    CBaseUIScrollView::tick();

    // the range moves at its end for new entries and at its start when old ones fall out (or on clear)
    const Console::LogRange range = Console::getLogRange();
    if(this->bRewrapPending)
        this->rewrap(range);
    else if(range.first != this->logRange.first || range.next != this->logRange.next)
        this->append(range);
}

void ConsoleLogView::onResized() {
    // whatever the new size, the line at the bottom edge stays there (following the newest line keeps following)
    const Anchor anchor = this->captureAnchor(this->fLastHeight);
    this->fLastHeight = this->getSize().y;

    CBaseUIScrollView::onResized();

    if(this->getSize().x != this->fWrapWidth) {
        // the anchor is applied once the lines are re-wrapped (in tick())
        this->bRewrapPending = true;
        this->pendingAnchor = anchor;
    } else
        this->applyAnchor(anchor);
}

void ConsoleLogView::rewrap(Console::LogRange range) {
    this->bRewrapPending = false;
    this->fWrapWidth = this->getSize().x;

    this->lines.clear();
    this->clearSelection();
    this->wrapInto(range.first, range.next);
    this->logRange = range;
    this->updateScrollSize();
    this->applyAnchor(this->pendingAnchor);
}

void ConsoleLogView::append(Console::LogRange range) {
    const bool wasAtBottom = this->isAtBottom();

    // entries older than the retained range fell out of the scrollback: the selection slides along
    size_t dropped = 0;
    while(!this->lines.empty() && this->lines.front().entrySeq < range.first) {
        this->lines.pop_front();
        dropped++;
    }
    if(dropped > 0) this->shiftSelection(dropped);

    const bool appended = range.next > this->logRange.next;
    this->wrapInto(std::max(this->logRange.next, range.first), range.next);
    this->logRange = range;
    this->updateScrollSize();

    // follow the newest line unless the user scrolled up (then the dropped rows must not shift the view)
    if(wasAtBottom && appended)
        this->scrollToY(-(int)this->vScrollSize.y, false);
    else if(dropped > 0 && !this->lines.empty())
        this->scrollToY((int)(this->vScrollPos.y + dropped * this->getLineHeight()), false);
}

void ConsoleLogView::wrapInto(u64 fromSeq, u64 toSeq) {
    const float scale = this->getTextScale();
    const f64 maxWidth =
        std::max(1.f, this->getSize().x - 4 * scale - cv::ui_scrollview_scrollbarwidth.getFloat()) / scale;
    for(u64 seq = fromSeq; seq < toSeq; seq++) {
        const Console::LogEntry &entry = Console::getLogEntry(seq);
        for(auto &text : this->font->wrap(entry.text, maxWidth)) {
            const float width = this->font->getStringWidth(text);
            this->lines.push_back({.text = std::move(text), .color = entry.color, .width = width, .entrySeq = seq});
        }
    }
}

void ConsoleLogView::updateScrollSize() {
    this->vScrollSize.x = 1.;
    this->vScrollSize.y = std::max(1.f, this->lines.size() * this->getLineHeight() + 2 * this->getPadding());

    // content that fits shows from the top (same as CBaseUIScrollView::onResized)
    if(this->vScrollSize.y < this->getSize().y && this->vScrollPos.y != 1) this->scrollToY(1);

    this->updateScrollbars();
}

ConsoleLogView::Anchor ConsoleLogView::captureAnchor(f64 viewHeight) const {
    // content y at the view's bottom edge
    const f64 bottomY = -this->vScrollPos.y + viewHeight;
    if(this->lines.empty() || bottomY >= this->vScrollSize.y)
        return {.atBottom = true, .entrySeq = 0, .subLine = 0, .fraction = 0.};

    const f64 row = (bottomY - this->getPadding()) / this->getLineHeight();
    const size_t index = static_cast<size_t>(std::clamp(std::floor(row), 0., static_cast<f64>(this->lines.size() - 1)));
    const u64 entrySeq = this->lines[index].entrySeq;
    size_t subLine = 0;
    while(index - subLine > 0 && this->lines[index - subLine - 1].entrySeq == entrySeq) subLine++;
    return {.atBottom = false, .entrySeq = entrySeq, .subLine = subLine, .fraction = row - std::floor(row)};
}

void ConsoleLogView::applyAnchor(const Anchor &anchor) {
    if(anchor.atBottom) {
        this->scrollToY(-(int)this->vScrollSize.y, false);
        return;
    }

    // the anchored entry's line again (it may wrap differently now, or have fallen out of the scrollback)
    const auto it = std::ranges::lower_bound(this->lines, anchor.entrySeq, {}, &Line::entrySeq);
    if(it == this->lines.end()) {
        this->scrollToY(-(int)this->vScrollSize.y, false);
        return;
    }
    size_t index = static_cast<size_t>(it - this->lines.begin());
    for(size_t sub = 0;
        sub < anchor.subLine && index + 1 < this->lines.size() && this->lines[index + 1].entrySeq == anchor.entrySeq;
        sub++)
        index++;

    const f64 bottomY = (static_cast<f64>(index) + anchor.fraction) * this->getLineHeight() + this->getPadding();
    this->scrollToY(static_cast<int>(std::round(this->getSize().y - bottomY)), false);
}

ConsoleLogView::TextRun ConsoleLogView::getRun(size_t index) const {
    const Line &line = this->lines[index];
    // a wrapped continuation of the same entry rejoins with the space the wrap dropped
    const bool continues = index > 0 && this->lines[index - 1].entrySeq == line.entrySeq;
    return {.text = line.text,
            .font = this->font,
            .pos = {this->getPadding(), this->getPadding() + index * this->getLineHeight()},
            .height = this->getLineHeight(),
            .width = line.width,
            .scale = this->getTextScale(),
            .separator = continues ? " " : "\n"};
}

ConsoleWindow::ConsoleWindow() : CBaseUIWindow(0, 0, 100, 100, "consolewindow") {
    this->setTitle("Console");

    CBaseUIContainer *content = this->getContainer();

    // added in draw order: the suggestion popup overlaps the log and must draw (and hit-test) above everything.
    // the window body is the only background under them (translucent, see tick()); only the popup, which overlays
    // the log text, fills its own
    this->logView = new ConsoleLogView(0, 0, 0, 0, "consolewindow_log");
    content->addBaseUIElement(this->logView);

    this->suggestions = new ConsoleSuggestionList(0, 0, 0, 0, "consolewindow_suggestions");
    this->suggestions->setBackgroundColor(argb(217, 0, 0, 0));  // the log lines it covers stay readable through it

    this->input = new ConsoleTextbox(0, 0, 0, 0, "consolewindow_input", this->suggestions);
    this->input->setFont(engine->getDefaultFont());
    this->input->setDrawBackground(false);
    content->addBaseUIElement(this->input);

    this->submitButton = new CBaseUIButton(0, 0, 0, 0, "consolewindow_submit", "Submit");
    this->submitButton->setDrawBackground(false);
    this->submitButton->setClickCallback([this]() -> void { this->input->submit(); });
    content->addBaseUIElement(this->submitButton);

    content->addBaseUIElement(this->suggestions);

    this->layout();

    cv::console_window_alpha.setCallback(SA::MakeDelegate<&ConsoleWindow::onAlphaChangedCallback>(this));
    this->onAlphaChangedCallback(cv::console_window_alpha.getFloat());
}

ConsoleWindow::~ConsoleWindow() { cv::console_window_alpha.removeAllCallbacks(); }

void ConsoleWindow::onAlphaChangedCallback(float newValue) {
    // the body's opacity is the user's (the text, the frames and the popup are not affected)
    this->setBackgroundColor(Color(0xff000000).setA(newValue));
}

void ConsoleWindow::tick() {
    if(!this->isVisible()) return;

    CBaseUIWindow::tick();

    // the popup's height follows the matches as they are typed, its bottom edge stays above the input box
    this->placeSuggestions();
}

void ConsoleWindow::updateInput(CBaseUIEventCtx &c) {
    if(!this->isVisible()) return;

    CBaseUIWindow::updateInput(c);

    // a press anywhere inside the window keeps the keyboard on the input box (the log, the buttons and the frame
    // are no focus holders); only a press outside releases it, through the textbox's own onMouseDownOutside
    if(mouse->isLeftPressed() && this->getRect().contains(mouse->getPos())) this->input->requestFocus();
}

void ConsoleWindow::onKeyDown(KeyboardEvent &e) {
    if(!this->isVisible()) return;

    // ctrl+c copies a log selection (unless the input box has its own selection to copy)
    if(e == KEY_C && keyboard->isControlDown() && this->logView->hasSelection() && !this->input->hasSelectedText()) {
        env->setClipBoardText(this->logView->getSelectedText());
        e.consume();
        return;
    }

    // the focused textbox consumes every key it sees, which only stops the listener chain, not the checks below
    const bool focused = this->input->isFocused();
    CBaseUIWindow::onKeyDown(e);
    if(!focused) return;

    if(e == KEY_ESCAPE) {
        this->close();
        e.consume();
    } else if(e == KEY_PAGEUP || e == KEY_PAGEDOWN) {
        this->logView->page(e == KEY_PAGEDOWN ? 1 : -1);
        e.consume();
    }
}

void ConsoleWindow::onResized() {
    CBaseUIWindow::onResized();
    this->layout();
}

void ConsoleWindow::toggle() {
    if(this->isVisible())
        this->close();
    else
        this->show();
}

void ConsoleWindow::show() {
    if(!this->bPlaced) {
        this->bPlaced = true;

        const vec2 screen = engine->getScreenSize();
        const vec2 size{std::round(screen.x * 0.6f), std::round(screen.y * 0.55f)};
        this->setSize(size);
        this->setPos(std::round((screen.x - size.x) / 2.f), std::round((screen.y - size.y) / 2.f));
    }

    this->open();
    this->input->requestFocus();
}

void ConsoleWindow::onClosed() { this->input->stealFocus(); }

void ConsoleWindow::layout() {
    const float dpiScale = env->getDPIScale();
    const float margin = 5 * dpiScale;
    const float gap = 4 * dpiScale;
    const float inputHeight = 26 * dpiScale;
    const vec2 size = this->getContainer()->getSize();

    const float buttonWidth = this->submitButton->getFont()->getStringWidth("Submit") + 20 * dpiScale;

    const float inputY = size.y - margin - inputHeight;
    const float inputWidth = std::max(1.f, size.x - 2 * margin - buttonWidth - gap);

    this->input->setRelPos(margin, inputY);
    this->input->setSize(inputWidth, inputHeight);
    this->submitButton->setRelPos(margin + inputWidth + gap, inputY);
    this->submitButton->setSize(buttonWidth, inputHeight);

    this->logView->setRelPos(margin, margin);
    this->logView->setSize(size.x - 2 * margin, std::max(1.f, inputY - gap - margin));

    this->suggestions->setRelPosX(margin);
    this->suggestions->setSizeX(inputWidth);
    this->getContainer()->update_pos();

    this->placeSuggestions();
}

void ConsoleWindow::placeSuggestions() {
    // right above the input box, growing upward over the bottom of the log (a no-op unless the popup's height or
    // the input box moved, so calling this every tick costs a compare)
    const float gap = 4 * env->getDPIScale();
    const float y = this->input->getRelPos().y - gap - this->suggestions->getSize().y;
    if(y == this->suggestions->getRelPos().y) return;
    this->suggestions->setRelPosY(y);
    this->getContainer()->update_pos();
}
