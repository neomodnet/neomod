#pragma once
// Copyright (c) 2026, WH, All rights reserved.
// one running instance per user session: later launches hand their arguments to it instead of starting up

#include "types.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Mc::SingleInstance {

enum class Claim : u8 {
    OWNER,      // this process holds the name, launches forwarded to it queue up for take_forwarded()
    FORWARDED,  // another process holds the name and was handed the arguments, this one should exit
    ALONE,      // another process holds the name but forwarding was off, or there's no way to tell: run anyway
};

// claims `name` for this user session or, if another process holds it and `forward` is set, hands it `args`.
// meant to be called once, early in main(): from the moment the name is claimed, launches forwarded to this process
// are received on a thread of its own, so they never wait for it to finish starting up
[[nodiscard]] Claim claim(std::string_view name, std::span<const std::string> args, bool forward) noexcept;

// the launches forwarded since the last call, oldest first, each with the arguments it was handed over with
// (possibly none, e.g. a plain launch that just asks the instance to come to the front). never waits for the thread
// receiving them: a launch it's queueing right then comes with a later call
[[nodiscard]] std::vector<std::vector<std::string>> take_forwarded() noexcept;

// gives the name up (e.g. before starting the next instance of this program on a restart)
void release() noexcept;

}  // namespace Mc::SingleInstance
