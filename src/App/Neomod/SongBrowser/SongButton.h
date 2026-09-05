#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "CarouselButton.h"

class Image;
class SongBrowser;
class SongDifficultyButton;
class DatabaseBeatmap;
using BeatmapSet = DatabaseBeatmap;
using BeatmapDifficulty = DatabaseBeatmap;
class BeatmapCarousel;

enum class ScoreGrade : u8;

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
    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;

    void triggerContextMenu(vec2 pos);

    // returns true if we did actually did something
    bool sortChildren();

    void updateLayoutEx() override;
    virtual void updateGrade() { ; }

    [[nodiscard]] const DatabaseBeatmap *getDatabaseBeatmap() const override { return this->databaseBeatmap; }
    // the set a parent button was built for (nullptr for difficulty buttons). unlike databaseBeatmap, which
    // tick() repoints at the representative difficulty, this identifies the set for as long as the button lives
    [[nodiscard]] inline const BeatmapSet *getBeatmapSet() const { return this->beatmapSet; }
    [[nodiscard]] inline ScoreGrade getGrade() const { return this->grade; }
    inline void setGrade(ScoreGrade grade) { this->grade = grade; }

   protected:
    SongButton *setVisible(bool visible) override;

    void onSelected(bool wasSelected, SelOpts opts) override;
    void onRightMouseUpInside() override;

    void onContextMenu(std::string_view text, int id = -1);
    void onAddToCollectionConfirmed(std::string_view text, int id = -1);
    void onCreateNewCollectionConfirmed(std::string_view text, int id = -1);
    void onDeleteBeatmapConfirmed(std::string_view text, int id = -1);

    void drawBeatmapBackgroundThumbnail(const Image *image);
    void drawGrade();
    void drawTitle(float deselectedAlpha = 1.0f, bool forceSelectedStyle = false);
    void drawSubTitle(float deselectedAlpha = 1.0f, bool forceSelectedStyle = false);

    float calculateGradeScale();
    float calculateGradeWidth();

    DatabaseBeatmap *databaseBeatmap{nullptr};
    const BeatmapSet *beatmapSet{nullptr};

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

   protected:
    ScoreGrade grade;

   private:
    [[nodiscard]] inline std::vector<SongDifficultyButton *> &childDiffBtns() {
        return reinterpret_cast<std::vector<SongDifficultyButton *> &>(this->children);
    }

    void onOpenBeatmapFolderClicked();
};
