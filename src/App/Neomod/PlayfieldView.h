#pragma once

#include "noinclude.h"
#include "types.h"
#include "Color.h"
#include "Vectors.h"

#include <vector>

struct Skin;
enum class ModFlags : u64;

// what drawing hitobjects needs to know about the playfield they're on: the skin, where osu!px land on the screen, the
// sizes that follow from the circle size, the time animations follow and the mods that change the look. whatever draws
// hitobjects implements it (or uses a PlainPlayfieldView); gameplay's effects are off unless overridden
class PlayfieldView {
   public:
    PlayfieldView() = default;
    virtual ~PlayfieldView() = default;

    PlayfieldView(const PlayfieldView &) = default;
    PlayfieldView &operator=(const PlayfieldView &) = default;
    PlayfieldView(PlayfieldView &&) = default;
    PlayfieldView &operator=(PlayfieldView &&) = default;

    [[nodiscard]] virtual const Skin *getSkin() const = 0;

    // osu!px to screen px
    [[nodiscard]] virtual vec2 osuCoords2Pixels(vec2 coords) const = 0;
    // to the space slider meshes are baked in: osu!px with the playfield's flips and rotation, centered on (0, 0)
    [[nodiscard]] virtual vec2 osuCoords2LegacyPixels(vec2 coords) const = 0;
    // a direction on the playfield (degrees) as drawn on the screen
    [[nodiscard]] virtual f32 osuAngle2PixelAngle(f32 degrees) const = 0;

    [[nodiscard]] virtual f32 getPlayfieldScaleFactor() const = 0;  // osu!px to screen px
    [[nodiscard]] virtual vec2 getPlayfieldCenter() const = 0;      // on the screen
    [[nodiscard]] virtual vec2 getPlayfieldSize() const = 0;        // on the screen
    [[nodiscard]] virtual vec2 getScreenSize() const = 0;           // what drawing culls against

    [[nodiscard]] virtual f32 getHitcircleDiameter() const = 0;           // screen px
    [[nodiscard]] virtual f32 getRawHitcircleDiameter() const = 0;        // osu!px
    [[nodiscard]] virtual f32 getSliderFollowCircleDiameter() const = 0;  // screen px
    [[nodiscard]] virtual f32 getNumberScale() const = 0;
    [[nodiscard]] virtual f32 getHitcircleOverlapScale() const = 0;

    // the music position, and with the offsets objects are timed against
    [[nodiscard]] virtual i32 getCurMusicPos() const = 0;
    [[nodiscard]] virtual i32 getCurMusicPosWithOffsets() const = 0;
    // how long objects take to approach (ms)
    [[nodiscard]] virtual f32 getApproachTime() const = 0;
    // wall-clock animations take 1 / this of their time
    [[nodiscard]] virtual f32 getBaseAnimationSpeed() const = 0;
    // fixed wall-clock durations last this many times as long in music time
    [[nodiscard]] virtual f32 getSpeedAdjustedAnimationSpeed() const = 0;
    [[nodiscard]] virtual ModFlags getModFlags() const = 0;

    // gameplay's effects
    [[nodiscard]] virtual bool isInMafhamRenderChunk() const { return false; }
    [[nodiscard]] virtual bool hasFailed() const { return false; }
    [[nodiscard]] virtual vec2 getFirstPersonCursorDelta() const { return vec2{0.f}; }
    [[nodiscard]] virtual bool slidersRenderDynamically() const { return false; }

    // the colour of a combo (colorCounter from the start, skipped ahead by colorOffset)
    [[nodiscard]] virtual Color getComboColor(i32 colorCounter, i32 colorOffset) const = 0;

    // the number and overlap scale that go with a circle size
    [[nodiscard]] static f32 numberScale(const Skin *skin, f32 rawHitcircleDiameter, f32 hitcircleDiameter);
    [[nodiscard]] static f32 hitcircleOverlapScale(f32 rawHitcircleDiameter, f32 hitcircleDiameter);
};

// a playfield as it is: osu!px scaled and moved onto the screen, no mods or effects
class PlainPlayfieldView final : public PlayfieldView {
   public:
    PlainPlayfieldView(const Skin *skin, vec2 offset, f32 scale, f32 rawHitcircleDiameter)
        : skin(skin), offset(offset), scale(scale), rawHitcircleDiameter(rawHitcircleDiameter) {}

    const Skin *skin;
    std::vector<Color> comboColors;  // the map's, if any
    vec2 offset;                     // where osu!px (0, 0) lands
    f32 scale;                       // osu!px to screen px
    f32 rawHitcircleDiameter;        // osu!px
    f32 approachTimeMS{0.f};
    i32 musicPos{0};

    [[nodiscard]] const Skin *getSkin() const override { return this->skin; }

    [[nodiscard]] vec2 osuCoords2Pixels(vec2 coords) const override { return coords * this->scale + this->offset; }
    [[nodiscard]] vec2 osuCoords2LegacyPixels(vec2 coords) const override;
    [[nodiscard]] f32 osuAngle2PixelAngle(f32 degrees) const override { return degrees; }

    [[nodiscard]] f32 getPlayfieldScaleFactor() const override { return this->scale; }
    [[nodiscard]] vec2 getPlayfieldCenter() const override;
    [[nodiscard]] vec2 getPlayfieldSize() const override;
    [[nodiscard]] vec2 getScreenSize() const override;

    [[nodiscard]] f32 getHitcircleDiameter() const override { return this->rawHitcircleDiameter * this->scale; }
    [[nodiscard]] f32 getRawHitcircleDiameter() const override { return this->rawHitcircleDiameter; }
    [[nodiscard]] f32 getSliderFollowCircleDiameter() const override;
    [[nodiscard]] f32 getNumberScale() const override;
    [[nodiscard]] f32 getHitcircleOverlapScale() const override;

    [[nodiscard]] i32 getCurMusicPos() const override { return this->musicPos; }
    [[nodiscard]] i32 getCurMusicPosWithOffsets() const override { return this->musicPos; }
    [[nodiscard]] f32 getApproachTime() const override { return this->approachTimeMS; }
    [[nodiscard]] f32 getBaseAnimationSpeed() const override { return 1.f; }
    [[nodiscard]] f32 getSpeedAdjustedAnimationSpeed() const override { return 1.f; }
    [[nodiscard]] ModFlags getModFlags() const override;

    [[nodiscard]] Color getComboColor(i32 colorCounter, i32 colorOffset) const override;
};
