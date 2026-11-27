#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "CarouselButton.h"

class Image;
class SongBrowser;
class DatabaseBeatmap;
class BeatmapCarousel;

enum class ScoreGrade : u8;

class SongButton : public CarouselButton {
    NOCOPY_NOMOVE(SongButton)
   public:
    explicit SongButton(BeatmapCarousel &carousel);
    ~SongButton() override;

    void draw() override;
    void tick() override;

    void updateLayoutEx() override;

    [[nodiscard]] const DatabaseBeatmap *getDatabaseBeatmap() const;

   protected:
    SongButton *setVisible(bool visible) override;

    void drawBeatmapBackgroundThumbnail(const Image *image);
    void drawGrade();
    void drawTitle(float deselectedAlpha = 1.0f, bool forceSelectedStyle = false);
    void drawSubTitle(float deselectedAlpha = 1.0f, bool forceSelectedStyle = false);

    [[nodiscard]] ScoreGrade getGrade() const;
    float calculateGradeScale();
    float calculateGradeWidth();

    // defaults
    static constexpr const float fTextSpacingScale{0.075f};
    static constexpr const float fTextMarginScale{0.075f};
    static constexpr const float fTitleScale{0.22f};
    static constexpr const float fSubTitleScale{0.14f};
    static constexpr const float fGradeScale{0.45f};

    // dynamic positioning
    float fTextOffset{0.0f};
    float fGradeOffset{0.0f};

    // thumbnails
    float fVisibleFor{0.f};

   private:
    float fThumbnailFadeInTime{0.0f};
};
