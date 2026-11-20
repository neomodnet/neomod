#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.

#include "CBaseUILabel.h"

class ChatLink final : public CBaseUILabel {
   public:
    ChatLink(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, std::string link = "",
             std::string label = "");

    void updateInput(CBaseUIEventCtx &c) override;
    void onMouseUpInside(MouseButtonFlags buttons = MouseButtonFlags::MF_LEFT) override;

    void open_beatmap_link(i32 map_id, i32 set_id);

    std::string link;
};
