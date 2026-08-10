// Copyright (c) 2019, PG, All rights reserved.
#include "UserStatsScreen.h"

#include "Bancho.h"
#include "CBaseUIContainer.h"
#include "CBaseUIScrollView.h"
#include "OsuConVars.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "HUD.h"
#include "OptionsOverlay.h"
#include "Osu.h"
#include "Graphics.h"
#include "Replay.h"
#include "Skin.h"
#include "SongBrowser/ScoreButton.h"
#include "SongBrowser/SongBrowser.h"
#include "SoundEngine.h"
#include "UI.h"
#include "UIContextMenu.h"
#include "UserCard.h"
#include "SongDifficultyButton.h"
#include "score.h"

#include <ranges>

UserStatsScreen::UserStatsScreen() : ScreenBackable() {
    m_userCard = new UserCard(0);
    m_userCard->setVisible(false);
    this->addBaseUIElement(m_userCard);

    m_contextMenu = std::make_unique<UIContextMenu>();
    m_contextMenu->setVisible(false);
    m_contextMenu->setVisible2(false);

    m_scores = new CBaseUIScrollView();
    m_scores->setBackgroundColor(0xff222222);
    m_scores->setHorizontalScrolling(false);
    m_scores->setVerticalScrolling(true);
    m_scores->setVisible(false);
    this->addBaseUIElement(m_scores);
}

void UserStatsScreen::draw() {
    if(!this->isVisible()) return;

    auto screen = osu->getVirtScreenSize();
    g->setColor(rgb(0, 0, 0));
    g->fillRect(0, 0, screen.x, screen.y);

    ScreenBackable::draw();
    m_contextMenu->draw();
}

void UserStatsScreen::updateInput(CBaseUIEventCtx &c) {
    if(!this->isVisible()) return;

    m_contextMenu->updateInput(c);
    ScreenBackable::updateInput(c);
}

CBaseUIContainer *UserStatsScreen::setVisible(bool visible) {
    if(visible == this->isVisible()) return this;
    m_userCard->setVisible(visible);
    m_contextMenu->setVisible(visible);
    m_scores->setVisible(visible);
    ScreenBackable::setVisible(visible);

    if(this->isVisible()) {
        rebuildScoreButtons();
    } else {
        m_contextMenu->setVisible2(false);
    }

    return this;
}

void UserStatsScreen::onBack() { ui->setScreen(ui->getSongBrowser()); }

void UserStatsScreen::rebuildScoreButtons() {
    // hard reset (delete)
    m_scores->container.freeElements();
    m_scoreButtons.clear();

    m_userCard->setID(BanchoState::get_uid());
    m_userCard->updateUserStats();

    i32 n = 0;
    const std::vector<FinishedScore *> &scores = db->getPlayerPPScores(BanchoState::get_username()).ppScores;
    for(sSz i = static_cast<sSz>(scores.size()) - 1; i >= 0; --i) {
        if(n >= cv::ui_top_ranks_max.getInt()) break;
        const float weight = Database::getWeightForIndex(n);
        ++n;
        auto *score = scores[i];

        DatabaseBeatmap *map = db->getBeatmapDifficulty(score->beatmap_hash);
        if(!map) continue;

        std::string title{
            fmt::format("{:s} - {:s} [{:s}]", map->getArtist(), map->getTitle(), map->getDifficultyName())};

        auto *button = new ScoreButton(m_contextMenu.get(), 0, 0, 300, 100, ScoreButton::STYLE::TOP_RANKS);
        button->setScore(*score, map, n, std::move(title), weight);
        button->setClickCallback(SA::MakeDelegate([](ScoreButton *button) -> void {
            const FinishedScore &btnsc = button->getScore();
            SongDifficultyButton *diff_btn = ui->getSongBrowser()->getDiffButtonByHash(btnsc.beatmap_hash);
            ui->setScreen(ui->getSongBrowser());
            ui->getSongBrowser()->selectSongButton(diff_btn);
            ui->getSongBrowser()->highlightScore(btnsc);
        }));

        m_scoreButtons.push_back(button);
        m_scores->container.addBaseUIElement(button);
    }

    updateLayout();
}

void UserStatsScreen::updateLayout() {
    ScreenBackable::updateLayout();

    const float dpiScale = Osu::getUIScale();

    setSize(osu->getVirtScreenSize());

    const int scoreListHeight = osu->getVirtScreenHeight() * 0.8f;
    m_scores->setSize(osu->getVirtScreenWidth() * 0.6f, scoreListHeight);
    m_scores->setPos(osu->getVirtScreenWidth() / 2 - m_scores->getSize().x / 2,
                     osu->getVirtScreenHeight() - scoreListHeight);

    const int margin = 5 * dpiScale;
    const int padding = 5 * dpiScale;

    // NOTE: these can't really be uiScale'd, because of the fixed aspect ratio
    const int scoreButtonWidth = m_scores->getSize().x * 0.92f - 2 * margin;
    const int scoreButtonHeight = scoreButtonWidth * 0.065f;
    for(int i = 0; i < m_scoreButtons.size(); i++) {
        m_scoreButtons[i]->setSize(scoreButtonWidth - 2, scoreButtonHeight);
        m_scoreButtons[i]->setRelPos(margin, margin + i * (scoreButtonHeight + padding));
    }
    m_scores->container.update_pos();

    m_scores->setScrollSizeToContent();

    const int userButtonHeight = m_scores->getPos().y * 0.6f;
    m_userCard->setSize(userButtonHeight * 3.5f, userButtonHeight);
    m_userCard->setPos(osu->getVirtScreenWidth() / 2 - m_userCard->getSize().x / 2,
                       m_scores->getPos().y / 2 - m_userCard->getSize().y / 2);
}
