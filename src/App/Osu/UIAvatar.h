#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.

#include "CBaseUIButton.h"

struct ThumbIdentifier;

class UIAvatar final : public CBaseUIButton {
    NOCOPY_NOMOVE(UIAvatar)
   public:
    UIAvatar(CBaseUIElement *parent, i32 player_id, float xPos, float yPos, float xSize, float ySize);
    ~UIAvatar() override;

    bool isVisible() override;

    void draw() override { this->draw_avatar(1.f); }
    void draw_avatar(float alpha);

    void onAvatarClicked(CBaseUIButton *btn);

    CBaseUIElement *parent;
    std::unique_ptr<ThumbIdentifier> thumb_id;
};
