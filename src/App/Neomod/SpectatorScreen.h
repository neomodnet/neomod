#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.

#include "types.h"
#include "BanchoProtocol.h"
#include "ReplayFile.h"
#include "MD5Hash.h"
#include "UIScreen.h"

class McFont;
class PauseButton;
class CBaseUILabel;
class UserCard;
class CBaseUIScrollView;
class UIButton;

struct RemotePlayerUpdate {
    LiveReplayAction action;
    i32 music_pos;
    MD5Hash map_md5;
    ScoreFrame score;
    std::vector<Replay::Frame> replay_frames;
};

class SpectatorScreen final : public UIScreen {
   public:
    SpectatorScreen();

    void tick() override;
    void updateInput(CBaseUIEventCtx& c) override;
    void draw() override;
    void onKeyDown(KeyboardEvent& e) override;
    void onStopSpectatingClicked();
    void handleFrameBundle(PacketReader& packet);

    UserCard* userCard = nullptr;

   private:
    void controlClientState();

    std::vector<RemotePlayerUpdate> player_updates;

    McFont* font = nullptr;
    McFont* lfont = nullptr;
    PauseButton* pauseButton = nullptr;
    CBaseUIScrollView* background = nullptr;
    UIButton* stop_btn = nullptr;
    CBaseUILabel* spectating = nullptr;
    CBaseUILabel* status = nullptr;

    // dedupes the OUTP_CANT_SPECTATE packet to one per failed map
    MD5Hash last_failed_map;
};

namespace Spectating {
// convar callback
void start_by_username(std::string_view username);

void start(int user_id);
void stop();
// ends spectating on this side only (stop() also tells the server; a disconnect has nobody to tell)
void forget();

}  // namespace Spectating
