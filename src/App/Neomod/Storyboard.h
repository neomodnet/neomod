#pragma once
// Copyright (c) 2025, kiwec, All rights reserved.
#include "types.h"
#include "AsyncCancellable.h"
#include "AsyncTypes.h"
#include "MD5Hash.h"
#include "Rect.h"
#include "StaticPImpl.h"
#include "Vectors_fwd.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class DatabaseBeatmap;

// see https://osu.ppy.sh/wiki/en/Storyboard/Scripting
class Storyboard {
    NOCOPY_NOMOVE(Storyboard)
   public:
    // in drawing order
    // everything up to FOREGROUND goes below the hitobjects
    enum class Layer : u8 { BACKGROUND, FAIL, PASS, FOREGROUND, OVERLAY, COUNT };

    struct SpriteState {
        vec2 pos;
        vec2 scale;
        f32 rotation;  // radians, clockwise
        vec3 colour;   // 0 to 1
        f32 alpha;
        bool flipH;
        bool flipV;
        bool additive;
        u32 frame;
    };

    struct HitSound {
        u8 normalSet;
        u8 additionSet;
        u8 additions;
        u16 index;
    };

    // skinDirs: where the skin looks for its elements, first match first (for UseSkinSprites)
    Storyboard(std::string folder, std::string_view osuFile, std::string_view osbFile, std::string backgroundFile,
               std::span<const std::string> skinDirs);
    ~Storyboard();

    // reads and parses a storyboard off the main thread
    [[nodiscard]] static Async::CancellableHandle<std::unique_ptr<Storyboard>> parseAsync(
        const DatabaseBeatmap &map, std::shared_ptr<const std::vector<u8>> osuFile, std::vector<std::string> skinDirs,
        Lane lane);

    // "Artist - Title (Creator).osb", or else the first .osb in the folder
    [[nodiscard]] static std::string findOsbFile(std::string_view folder, std::string_view artist,
                                                 std::string_view title, std::string_view creator);

    [[nodiscard]] bool isEmpty() const;

    [[nodiscard]] bool replacesBackground() const;
    [[nodiscard]] vec2 getBackgroundOffset() const;

    [[nodiscard]] uSz getNumSprites() const;
    [[nodiscard]] uSz getNumSamples() const;
    [[nodiscard]] std::optional<SpriteState> getSpriteState(uSz sprite, f64 time) const;
    // when a sprite is shown, from its first visible command to the end of its last
    [[nodiscard]] std::pair<f64, f64> getSpriteSpan(uSz sprite) const;
    // when the storyboard's commands and samples start and end as written (loops not repeated), if it has any
    [[nodiscard]] std::optional<std::pair<f64, f64>> getTimeSpan() const;
    // the files load() loads for a sprite's frame and for a sample ("" if there's none)
    [[nodiscard]] std::string_view getImageFile(uSz sprite, u32 frame = 0) const;
    [[nodiscard]] std::string_view getSampleFile(uSz sample) const;

    void load(bool visualsOnly);
    [[nodiscard]] bool isReady() const;

    [[nodiscard]] MD5Hash getHash();

    void update(f64 time, bool passing, bool seeked);
    void onHitSound(HitSound sound, f64 time);
    void onHitObjectHit(f64 time);

    struct View {
        // where the 640x480 storyboard area goes: scaled to its height, centered across its width
        McRect screen;
        vec2 pivot{0.f};
        bool flipX{false};
        bool flipY{false};
        f32 rotation{0.f};
        f32 alpha{1.f};
        f32 brightness{1.f};

        void transform() const;
    };

    void draw(Layer first, Layer last, const View &view) const;

   private:
    struct Impl;
    StaticPImpl<Impl, 640> impl;
};
