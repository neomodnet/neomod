// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "AnimationHandler.h"
#include "CBaseUIContainer.h"

#include <string>

class MainMenu;
class UIIconButton;
class PauseButton;
class McFont;

namespace neomod::mainmenu {

// the main menu's music player: the song's artist and title (scrolling through it when it doesn't fit), which unroll
// into previous/play/next buttons and a seek bar while hovered or pinned open
class NowPlaying final : public CBaseUIContainer {
    NOCOPY_NOMOVE(NowPlaying)
   public:
    NowPlaying(MainMenu *mm);
    ~NowPlaying() override;

    void draw() override;
    void tick() override;

    // sizes the panel for the current UI scale and how far it's unrolled, and lays out its buttons
    void updateLayout();

   private:
    class SeekBar;
    class PinButton;

    void drawTitle();
    void drawTimes();

    UIIconButton *prevButton;
    PauseButton *pauseButton;
    UIIconButton *nextButton;
    PinButton *pinButton;
    SeekBar *seekBar;

    McFont *font;
    McFont *iconFont;

    std::string title;
    f64 marqueeTime{0.};        // into the title's scroll cycle
    bool marqueeScrollFinished{false};  // resting at its start until the next song or expansion

    f64 expandedUntil;  // hovered, dragged or just started
    bool expanded{false};
    AnimFloat expandAnim{0.f};
};

}  // namespace neomod::mainmenu
