#pragma once
// Copyright (c) 2023, kiwec, All rights reserved.

#include "config.h"

#include <string_view>

struct Packet;

#define NEOMOD_DOMAIN PACKAGE_NAME ".net"

// NOTE: Full version can be something like "b20200201.2cuttingedge"
#define OSU_VERSION_DATEONLY 20260711
#define OSU_VERSION "b20260711.1"

namespace BANCHO::Net {

// Queue a packet for the next request to Bancho (copies it, so the packet can be dropped or reused right after)
void send_packet(const Packet& packet);

// Process networking logic. Should be called regularly from main thread.
void update_networking();

// Clean up networking. Should be called once when exiting neomod.
void cleanup_networking();

}  // namespace BANCHO::Net
