// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "App.h"
#include "Color.h"
#include "PlayfieldView.h"

#include <memory>
#include <string>
#include <vector>

class RenderTarget;
struct Skin;
namespace neomod {
class HitObject;
}

namespace Mc::Tests {

// a map's hitobjects and follow points drawn at any time without gameplay (no BeatmapInterface, no Osu): its own skin, a
// PlainPlayfieldView fitted into the window, the objects from HitObjects::create() posed at the time.
// -testarg:map FILE.osu, -testarg:time MS (pft_time sets it later); Left/Right 100 ms (Shift: 10), Up/Down 1 s, Space
// plays. -testarg:corpus DIR draws every map under DIR at several times instead, then exits
class PlayfieldTest : public App {
    NOCOPY_NOMOVE(PlayfieldTest)
   public:
    PlayfieldTest();
    ~PlayfieldTest() override;

    void draw() override;
    void update() override;
    void onKeyDown(KeyboardEvent &e) override;
    void onResolutionChanged(vec2 newResolution) override;

    // fps limiter behavior
    [[nodiscard]] bool isInGameplay() const override { return true; }
    [[nodiscard]] bool isInUnpausedGameplay() const override { return true; }

   private:
    struct Entry {
        neomod::HitObject *obj;
        bool meshBuilt;
    };

    // false when the map can't be drawn
    bool load(const std::string &path);
    void fitView();
    void drawAt(i32 timeMS);
    void drawCorpusFrame();

    std::unique_ptr<Skin> m_skin;
    RenderTarget *m_sliderRT{nullptr};
    PlainPlayfieldView m_view{nullptr, vec2{0.f}, 1.f, 0.f};

    std::vector<std::unique_ptr<neomod::HitObject>> m_objects;
    std::vector<Entry> m_byEndTime;  // the draw order
    std::vector<Entry *> m_shown;    // posed for the frame being drawn
    i32 m_firstTimeMS{0};
    i32 m_lastTimeMS{0};

    std::vector<std::string> m_corpus;
    uSz m_corpusNext{0};
    uSz m_corpusDrawn{0};
    uSz m_corpusSkipped{0};
    uSz m_corpusObjects{0};
    f64 m_corpusStart{0.0};

    f64 m_timeMS{0.0};
    f32 m_timeConVar{0.f};  // pft_time as last seen
    bool m_loaded{false};
    bool m_playing{false};
};

}  // namespace Mc::Tests
