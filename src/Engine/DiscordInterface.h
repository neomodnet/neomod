#pragma once
// Copyright (c) 2026, kiwec, All rights reserved.
#include <array>
#include <string>

#include "types.h"

struct DiscordActivity {
    std::string details;      // first line
    std::string state;        // second line
    i64 start_timestamp{0};   // unix seconds, shown as elapsed time
    std::string large_image;  // asset key or image url
    std::string large_text;
    std::string small_image;
    std::string small_text;
    i32 party_size{0};
    i32 party_max{0};
    struct Button {
        std::string label;
        std::string url;
        bool operator==(const Button &) const = default;
    };
    std::array<Button, 2> buttons{};  // empty label == unused slot

    bool operator==(const DiscordActivity &) const = default;
};

namespace Discord {
void set_activity(const DiscordActivity &activity);
void tick();
void shutdown();
}  // namespace Discord
