#pragma once
// Copyright (c) 2026, WH, All rights reserved.
// what the engine starts with: the startup convars (engine.cfg), with this launch's switches on top. holding shift
// while it starts (or -setup) shows them in a dialog first

#include "types.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Mc::Startup {

enum class Renderer : u8 { GL, GLES, DX11, SDLGPU, NULLGRAPHICS };

struct Settings {
    std::vector<Renderer> renderers;  // in the order to try them; a single one means no fallback
    bool rendererChosen;              // r_renderer picked the first one
    std::string_view sdlgpuDriver;    // the API SDL_gpu tries first (SDL's name for the driver), if any
    bool preferBASS;                  // BASS before SoLoud
    bool highDPI;                     // the full resolution of high DPI displays (and DPI awareness on Windows)
    bool dx11Exclusive;               // exclusive fullscreen instead of flip presentation

    // a switch sets these for this launch, whatever their convar says
    bool rendererSwitched;  // no other renderer gets tried then
    bool soundEngineSwitched;
};

// intended to be called once inside of main(), before SDL is initialized: loads engine.cfg and shows the dialog if
// asked to. false if the player quit from the dialog
namespace detail {
[[nodiscard]] bool init();
}

[[nodiscard]] const Settings &get();

// how far this start got, kept in a file until it is finished() (only for a player's launches, not test or second
// instances). a start that never got there (a crash, or a hang the player ended) opens the startup dialog the next
// time, and a renderer it stopped in goes last in the automatic order until a start gets through
enum class Progress : u8 { RENDERER, APP, FIRST_FRAME };
void progress(Progress progress, Renderer renderer);
// the first frame is drawn, or the game quit in an orderly way before that
void finished();

// what r_renderer calls a renderer. SDL_gpu goes by the API it runs on (SDL's name for the driver)
[[nodiscard]] std::string_view rendererValue(Renderer renderer, std::string_view sdlgpuDriver);
// for messages about a renderer as a whole (every API of SDL_gpu)
[[nodiscard]] std::string rendererName(Renderer renderer);

// what a startup setting can be in this build: its value, and what the UI calls it ("auto" first). empty where there
// is nothing to pick
struct Choice {
    std::string_view value;
    std::string name;
};
[[nodiscard]] std::vector<Choice> rendererChoices();  // SDL_gpu as one choice per API
[[nodiscard]] std::vector<Choice> soundEngineChoices();
// what the UI calls a value of such a list (the value itself if it's none of them)
[[nodiscard]] std::string choiceName(std::span<const Choice> choices, std::string_view value);

}  // namespace Mc::Startup
