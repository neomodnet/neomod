#pragma once
// Copyright (c) 2026, kiwec, All rights reserved.

#include <string>
#include <string_view>
#include <vector>

struct Account {
    std::string server;
    std::string username;
    std::string password_md5;
    std::string oauth_token;
    std::string supporter_token;
};

namespace Accounts {
void load();
void save();

[[nodiscard]] const std::vector<Account> &get_all();
[[nodiscard]] Account *find(std::string_view server);
Account &get(std::string_view server);
void remove(std::string_view server);
}  // namespace Accounts
