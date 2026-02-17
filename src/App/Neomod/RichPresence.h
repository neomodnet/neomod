#pragma once
// Copyright (c) 2018, PG, All rights reserved.
#include "BanchoProtocol.h"

namespace RichPresence {
void onMainMenu();
void onSongBrowser();
void onPlayStart();
void onPlayEnd(bool quit);
void onMultiplayerLobby();

void setBanchoStatus(const char *info_text, Action action);
void updateBanchoMods();
};  // namespace RichPresence
