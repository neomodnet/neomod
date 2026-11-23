#pragma once
// Copyright (c) 2026, kiwec, All rights reserved.

namespace Migrations {
[[nodiscard]] bool is_updating_from_old_version();
void run();
}  // namespace Migrations
