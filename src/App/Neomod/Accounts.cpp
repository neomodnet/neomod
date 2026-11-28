// Copyright (c) 2026, kiwec, All rights reserved.
#include "Accounts.h"

#include <vector>

#include "AsyncIOHandler.h"
#include "ContainerRanges.h"
#include "File.h"
#include "Logging.h"
#include "NotificationOverlay.h"
#include "UI.h"
#include "i18n.h"
#include "Paths.h"
#include "SString.h"

#include "fmt/format.h"

namespace Accounts {
namespace {
std::vector<Account> accounts;
}  // namespace

void load() {
    accounts.clear();

    File file(Mc::Paths::cfg() + "/accounts.cfg", File::MODE::READ);
    for(auto line = file.readLine(); !line.empty() || file.canRead(); line = file.readLine()) {
        const std::string_view trimmed = SString::trimmed(line);
        if(trimmed.empty() || SString::is_comment(trimmed) || SString::is_comment(trimmed, "#")) continue;

        const size_t space = trimmed.find(' ');
        const std::string_view key = trimmed.substr(0, space);
        const std::string_view value = space == std::string_view::npos ? "" : SString::trimmed(trimmed.substr(space));

        if(key == "server") {
            accounts.emplace_back().server = value;
        } else if(accounts.empty()) {
            debugLog("accounts.cfg: {:s} before the first server", key);
        } else if(key == "username") {
            accounts.back().username = value;
        } else if(key == "password_md5") {
            accounts.back().password_md5 = value;
        } else if(key == "oauth_token") {
            accounts.back().oauth_token = value;
        } else if(key == "supporter_token") {
            accounts.back().supporter_token = value;
        } else {
            debugLog("accounts.cfg: unknown key {:s}", key);
        }
    }
}

void save() {
    std::string config;
    for(const auto &account : accounts) {
        config.append(fmt::format("server {}\n", account.server));
        if(!account.username.empty()) config.append(fmt::format("username {}\n", account.username));
        if(!account.password_md5.empty()) config.append(fmt::format("password_md5 {}\n", account.password_md5));
        if(!account.oauth_token.empty()) config.append(fmt::format("oauth_token {}\n", account.oauth_token));
        if(!account.supporter_token.empty()) {
            config.append(fmt::format("supporter_token {}\n", account.supporter_token));
        }
        config.push_back('\n');
    }

    io->write(Mc::Paths::cfg() + "/accounts.cfg", std::move(config), [](bool success) {
          if(success) return;
          debugLog("WARNING: failed to write accounts.cfg");
          if(ui) ui->getNotificationOverlay()->addToast(_("Couldn't save accounts."), ERROR_TOAST);
      }).detach();
}

const std::vector<Account> &get_all() { return accounts; }

Account *find(std::string_view server) {
    const auto it = Mc::ranges::find(accounts, server, &Account::server);
    return it == accounts.end() ? nullptr : &*it;
}

Account &get(std::string_view server) {
    if(Account *account = find(server)) return *account;
    Account &account = accounts.emplace_back();
    account.server = server;
    return account;
}

void remove(std::string_view server) {
    std::erase_if(accounts, [&](const Account &a) { return a.server == server; });
}
}  // namespace Accounts
