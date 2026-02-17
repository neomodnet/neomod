#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.

#include "CBaseUILabel.h"
#include "UString.h"

class ChatLink final : public CBaseUILabel {
   public:
    ChatLink(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, const UString& link = "",
             const UString& label = "");

    void update(CBaseUIEventCtx &c) override;
    void onMouseUpInside(bool left = true, bool right = false) override;

    void open_beatmap_link(i32 map_id, i32 set_id);

    UString link;
};
