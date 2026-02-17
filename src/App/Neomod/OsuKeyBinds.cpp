// Copyright (c) 2016, PG, 2025, kiwec, 2025, WH, All rights reserved.
#include "OsuKeyBinds.h"
#include "OsuConVars.h"

namespace OsuKeyBinds {

std::vector<ConVar *> &getAll() {
    static std::vector<ConVar *> allBindsList = {&cv::LEFT_CLICK,
                                                 &cv::RIGHT_CLICK,
                                                 &cv::LEFT_CLICK_2,
                                                 &cv::RIGHT_CLICK_2,

                                                 &cv::FPOSU_ZOOM,

                                                 &cv::INCREASE_VOLUME,
                                                 &cv::DECREASE_VOLUME,

                                                 &cv::INCREASE_LOCAL_OFFSET,
                                                 &cv::DECREASE_LOCAL_OFFSET,

                                                 &cv::GAME_PAUSE,
                                                 &cv::SKIP_CUTSCENE,
                                                 &cv::TOGGLE_SCOREBOARD,
                                                 &cv::SEEK_TIME,
                                                 &cv::SEEK_TIME_BACKWARD,
                                                 &cv::SEEK_TIME_FORWARD,
                                                 &cv::QUICK_RETRY,
                                                 &cv::QUICK_SAVE,
                                                 &cv::QUICK_LOAD,
                                                 &cv::INSTANT_REPLAY,
                                                 &cv::TOGGLE_CHAT,
                                                 &cv::TOGGLE_EXTENDED_CHAT,
                                                 &cv::SAVE_SCREENSHOT,
                                                 &cv::DISABLE_MOUSE_BUTTONS,
                                                 &cv::TOGGLE_MAP_BACKGROUND,
                                                 &cv::BOSS_KEY,
                                                 &cv::OPEN_SKIN_SELECT_MENU,

                                                 &cv::TOGGLE_MODSELECT,
                                                 &cv::RANDOM_BEATMAP,

                                                 &cv::MOD_EASY,
                                                 &cv::MOD_NOFAIL,
                                                 &cv::MOD_HALFTIME,
                                                 &cv::MOD_HARDROCK,
                                                 &cv::MOD_SUDDENDEATH,
                                                 &cv::MOD_DOUBLETIME,
                                                 &cv::MOD_HIDDEN,
                                                 &cv::MOD_FLASHLIGHT,
                                                 &cv::MOD_RELAX,
                                                 &cv::MOD_AUTOPILOT,
                                                 &cv::MOD_SPUNOUT,
                                                 &cv::MOD_AUTO,
                                                 &cv::MOD_SCOREV2};
    return allBindsList;
}

}  // namespace OsuKeyBinds
