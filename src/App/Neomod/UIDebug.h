#pragma once
// Copyright (c)  2026, WH, All rights reserved.
#include "noinclude.h"

#include <string_view>

class UIScreen;
struct UI;

// for debugging
class UIDebug final {
    NOCOPY_NOMOVE(UIDebug);

   public:
    UIDebug() = delete;
    UIDebug(UI* ui_parent);
    ~UIDebug();

    void setScreenByName(std::string_view screenGetterNameWithoutGet);
    [[nodiscard]] UIScreen* findScreenByName(std::string_view lowerName) const;
    void debugDumpScreens();
    void debugDumpElements(std::string_view screenName);
    void debugAssert(std::string_view args);
    void debugPrompt(std::string_view msg);
    void debugFakeOnline(std::string_view arg);
    void debugFakeRoom();
    void debugChatMessage(std::string_view args);

   private:
    UI* m_ui;
};