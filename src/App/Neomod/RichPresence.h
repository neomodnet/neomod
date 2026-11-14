#pragma once
// Copyright (c) 2018, PG, All rights reserved.
enum class Action : unsigned char;
class DatabaseBeatmap;

namespace RichPresence {
void refreshStatus();  // re-run last callback
void onMainMenu();
void onSongBrowser();
void onPlayStart();
void onEditor(const DatabaseBeatmap &map);  // editing `map`
void onPlayEnd(bool quit);
void onMultiplayerLobby();

void setBanchoStatus(const char *info_text, Action action);
void resetBanchoStatus();
void updateBanchoMods();
};  // namespace RichPresence
