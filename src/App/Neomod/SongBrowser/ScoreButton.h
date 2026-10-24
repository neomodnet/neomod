#pragma once
// Copyright (c) 2018, PG, All rights reserved.
#include "AnimationHandler.h"
#include "CBaseUIButton.h"

#include <memory>
#include <string>
#include <vector>

class DatabaseBeatmap;
using BeatmapDifficulty = DatabaseBeatmap;
using BeatmapSet = DatabaseBeatmap;

namespace Replay {
struct Mods;
}

enum class ScoreGrade : uint8_t;

struct FinishedScore;
class SkinImage;

class UIAvatar;
class UIContextMenu;

class ScoreButton final : public CBaseUIButton {
    NOCOPY_NOMOVE(ScoreButton)
   public:
    static std::string getModsStringForDisplay(const Replay::Mods &mods);
    static std::vector<std::string> getExtraModNamesForDisplay(const Replay::Mods &mods);
    static std::string getNotelockNameForDisplay(int notelockType);
    // whether song browser score buttons show pp instead of score (the sort order and the scores_*_pp convars decide)
    static bool showsPP();

    enum class STYLE : uint8_t { SONG_BROWSER, TOP_RANKS };
    ScoreButton() = delete;
    ScoreButton(UIContextMenu *contextMenu, float xPos, float yPos, float xSize, float ySize,
                STYLE style = STYLE::SONG_BROWSER);
    ~ScoreButton() override;

    void draw() override;
    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;

    void highlight();
    void resetHighlight();

    void setScore(const FinishedScore &score, const DatabaseBeatmap *map, int index = 1, std::string titleString = {},
                  float weight = 1.0f);
    void setIndex(int index) { this->iScoreIndexNumber = index; }

    [[nodiscard]] inline const FinishedScore &getScore() const { return *this->storedScore; }
    [[nodiscard]] u64 getScoreUnixTimestamp() const;
    [[nodiscard]] u64 getScoreScore() const;

    [[nodiscard]] inline std::string_view getDateTime() const { return this->sScoreDateTime; }
    [[nodiscard]] inline int getIndex() const { return this->iScoreIndexNumber; }

    void onMouseInside() override;
    void onMouseOutside() override;

    void onFocusStolen() override;

   protected:
    void onClicked(bool left = true, bool right = false) override;

   private:
    static std::string recentScoreIconString;

    void updateElapsedTimeString();

    void onRightMouseUpInside();
    void onContextMenu(std::string_view text, int id = -1);
    void onUseModsClicked();
    void onDeleteScoreClicked();
    void onDeleteScoreConfirmed(std::string_view text, int id);

    bool isContextMenuVisible();

    std::unique_ptr<UIAvatar> avatar{nullptr};
    UIContextMenu *contextMenu;

    // STYLE::SCORE_BROWSER
    // TODO: clean up this mess of confusing variable/names
    std::string sScoreTime;
    std::string sScoreUsername;
    std::string sFmtedScoreScoreWithCombo;
    std::string sFmtedScorePPWithCombo;
    std::string sScoreAccuracy;
    std::string sScoreAccuracyFC;
    std::string sFmtedScoreAccuracyWithCombo;
    std::string sScoreMods;
    std::string sCustom;

    // STYLE::TOP_RANKS
    std::string sScoreTitle;
    std::string sScoreScorePPWeightedPP;
    std::string sScoreScorePPWeightedWeight;
    std::string sScoreWeight;

    std::vector<std::string> tooltipLines;
    std::string sScoreDateTime;

    // score data
    std::unique_ptr<FinishedScore> storedScore;
    int iScoreIndexNumber{1};
    u64 iScoreUnixTimestamp{0};

    ScoreGrade scoreGrade;

    STYLE style;
    AnimFloat fIndexNumberAnim;
    bool bIsPulseAnim{false};

    bool bRightClick{false};
    bool bRightClickCheck{false};
    bool is_friend{false};
    bool is_submitting{false};
};
