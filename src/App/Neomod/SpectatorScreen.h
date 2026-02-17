#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.

#include "DownloadHandle.h"
#include "UIScreen.h"

class McFont;
class PauseButton;
class CBaseUILabel;
class UserCard;
class CBaseUIScrollView;
class UIButton;

class SpectatorScreen final : public UIScreen {
   public:
    SpectatorScreen();

    void update(CBaseUIEventCtx& c) override;
    void draw() override;
    void onKeyDown(KeyboardEvent& e) override;
    void onStopSpectatingClicked();

    UserCard* userCard = nullptr;

   private:
    McFont* font = nullptr;
    McFont* lfont = nullptr;
    PauseButton* pauseButton = nullptr;
    CBaseUIScrollView* background = nullptr;
    UIButton* stop_btn = nullptr;
    CBaseUILabel* spectating = nullptr;
    CBaseUILabel* status = nullptr;
    Downloader::DownloadHandle map_dl;
};

namespace Spectating {
// convar callback
void start_by_username(std::string_view username);

void start(int user_id);
void stop();

}  // namespace Spectating
