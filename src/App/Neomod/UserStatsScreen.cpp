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
#include "ModSelector.h"
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

UserStatsScreen::UserStatsScreen() : ScreenBackable() {
    this->m_userCard = new UserCard(0);
    this->addBaseUIElement(this->m_userCard);

    this->m_contextMenu = std::make_unique<UIContextMenu>();
    this->m_contextMenu->setVisible(true);

    this->m_scores = new CBaseUIScrollView();
    this->m_scores->setBackgroundColor(0xff222222);
    this->m_scores->setHorizontalScrolling(false);
    this->m_scores->setVerticalScrolling(true);
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

void UserStatsScreen::update(CBaseUIEventCtx &c) {
    if(!this->isVisible()) return;

    m_contextMenu->update(c);
    ScreenBackable::update(c);
}

CBaseUIContainer *UserStatsScreen::setVisible(bool visible) {
    if(visible == this->isVisible()) return this;

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

    this->m_userCard->setID(BanchoState::get_uid());
    this->m_userCard->updateUserStats();

    i32 i = 0;
    const std::vector<FinishedScore *> &scores = db->getPlayerPPScores(BanchoState::get_username()).ppScores;
    for(auto &score : scores | std::views::reverse) {
        if(i >= cv::ui_top_ranks_max.getInt()) break;
        const float weight = Database::getWeightForIndex(i);

        DatabaseBeatmap *map = db->getBeatmapDifficulty(score->beatmap_hash);
        if(!map) continue;

        UString title = "...";
        if(map != nullptr) {
            title = map->getArtist().c_str();
            title.append(" - ");
            title.append(map->getTitle().c_str());
            title.append(" [");
            title.append(map->getDifficultyName().c_str());
            title.append("]");
        }

        auto *button = new ScoreButton(this->m_contextMenu.get(), 0, 0, 300, 100, ScoreButton::STYLE::TOP_RANKS);
        button->setScore(*score, map, ++i, title, weight);
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
    this->m_userCard->setSize(userButtonHeight * 3.5f, userButtonHeight);
    this->m_userCard->setPos(osu->getVirtScreenWidth() / 2 - this->m_userCard->getSize().x / 2,
                             m_scores->getPos().y / 2 - this->m_userCard->getSize().y / 2);
}
