// Copyright (c) 2026, WH, All rights reserved.
#include "Transport.h"

#include "CBaseUIStyledButton.h"
#include "Commands.h"
#include "EditorState.h"
#include "UIIconButton.h"
#include "UIStyle.h"

#include "fmt/format.h"

#include <cmath>
#include <string>
#include <utility>

namespace {
constexpr std::array<i32, 4> RATES{25, 50, 75, 100};

class EditorPauseButton final : public PauseButton {
    NOCOPY_NOMOVE(EditorPauseButton)
   public:
    explicit EditorPauseButton(Clock &clock, std::string name) : PauseButton(std::move(name)), clock(clock) {}
    ~EditorPauseButton() override = default;

   protected:
    [[nodiscard]] bool isPlaying() const override { return this->clock.isPlaying(); }
    void toggle() override { this->clock.togglePause(); }

   private:
    Clock &clock;
};

}  // namespace

Transport::Transport(EditorState &state) : CBaseUIBox(Axis::ROW, "editor_transport"), state(state) {
    const UIStyle &style = uiStyle();
    this->setPadding(style.metrics.padding);
    this->setGap(style.metrics.gap);

    this->playButton = new EditorPauseButton(this->state.clock, "editor_play");
    this->add(this->playButton, BoxItem::fixed(style.metrics.controlHeight));
    for(uSz i = 0; i < RATES.size(); i++) {
        auto *button = new CBaseUIStyledButton(fmt::format("{}%", RATES[i]), fmt::format("editor_rate_{}", RATES[i]));
        button->setClickCallback([this, rate = RATES[i]] { this->state.clock.setRate((f32)rate / 100.f); });
        this->rateButtons[i] = button;
        this->add(button, BoxItem::fit());
    }
}

Transport::~Transport() = default;

void Transport::tick() {
    // without a map the music doesn't belong to the editor
    const bool open = this->state.clock.isAttached();
    this->playButton->setEnabled(open);
    Commands::hint(*this->playButton, Commands::Command::TOGGLE_PAUSE);
    const i32 rate = (i32)std::lround(this->state.clock.getRate() * 100.f);
    for(uSz i = 0; i < RATES.size(); i++) {
        // a rate between two of them (set by the keys) shows on the next one up
        const bool current = rate <= RATES[i] && (i == 0 || rate > RATES[i - 1]);
        this->rateButtons[i]->setEnabled(open);
        this->rateButtons[i]->setChecked(current);
        if(rate != this->shownRate) this->rateButtons[i]->setText(fmt::format("{}%", current ? rate : RATES[i]));
    }
    this->shownRate = rate;
    CBaseUIBox::tick();
}
