#pragma once
// Copyright (c) 2025, kiwec, All rights reserved.

#include "CBaseUIButton.h"

#include <memory>

class ConVar;

class UIAvatar;
struct UserInfo;

class UserCard2 final : public CBaseUIButton {
    NOCOPY_NOMOVE(UserCard2)
   public:
    UserCard2(i32 user_id);
    ~UserCard2() override;

    void update_userid(i32 new_userid);

    void draw() override;

    UserInfo *info{nullptr};
    std::unique_ptr<UIAvatar> avatar{nullptr};
};
