#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "CarouselButton.h"
#include "score.h"

class Image;
class SongBrowser;
class SongDifficultyButton;
class DatabaseBeatmap;
using BeatmapSet = DatabaseBeatmap;
using BeatmapDifficulty = DatabaseBeatmap;
class BeatmapCarousel;

class SongButton : public CarouselButton {
    NOCOPY_NOMOVE(SongButton)
   protected:
    // only for SongDifficultyButton as a passthrough (unnecessary inheritance...?)
    SongButton(float xPos, float yPos, float xSize, float ySize);

   public:
    SongButton(float xPos, float yPos, float xSize, float ySize, BeatmapSet *beatmapSet);
    SongButton() = delete;
    ~SongButton() override;

    void draw() override;
    void update(CBaseUIEventCtx &c) override;

    void triggerContextMenu(vec2 pos);

    // returns true if we did actually did something
    bool sortChildren();

    void updateLayoutEx() override;
    virtual void updateGrade() { ; }

    [[nodiscard]] DatabaseBeatmap *getDatabaseBeatmap() const override { return this->databaseBeatmap; }
    ScoreGrade grade = ScoreGrade::N;

   protected:
    SongButton *setVisible(bool visible) override;

    void onSelected(bool wasSelected, SelOpts opts) override;
    void onRightMouseUpInside() override;

    void onContextMenu(const UString &text, int id = -1);
    void onAddToCollectionConfirmed(const UString &text, int id = -1);
    void onCreateNewCollectionConfirmed(const UString &text, int id = -1);

    void drawBeatmapBackgroundThumbnail(const Image *image);
    void drawGrade();
    void drawTitle(float deselectedAlpha = 1.0f, bool forceSelectedStyle = false);
    void drawSubTitle(float deselectedAlpha = 1.0f, bool forceSelectedStyle = false);

    float calculateGradeScale();
    float calculateGradeWidth();

    DatabaseBeatmap *databaseBeatmap{nullptr};

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
    [[nodiscard]] inline std::vector<SongDifficultyButton *> &childDiffBtns() {
        return reinterpret_cast<std::vector<SongDifficultyButton *> &>(this->children);
    }

    float fThumbnailFadeInTime{0.0f};
    void onOpenBeatmapFolderClicked();
};
