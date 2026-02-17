#pragma once
// Copyright (c) 2018, PG, All rights reserved.
#include "CBaseUIButton.h"

#include <memory>

class ConVar;

class UIAvatar;

class UserCard final : public CBaseUIButton {
    NOCOPY_NOMOVE(UserCard)
   public:
    UserCard(i32 user_id);
    ~UserCard() override;

    void draw() override;
    void update(CBaseUIEventCtx &c) override;

    void updateUserStats();
    void setID(i32 new_id);

   private:
    std::unique_ptr<UIAvatar> avatar{nullptr};

    i32 user_id = 0;
    float fPP;
    float fAcc;
    int iLevel;
    float fPercentToNextLevel;

    float fPPDelta;
    float fPPDeltaAnim;
};
