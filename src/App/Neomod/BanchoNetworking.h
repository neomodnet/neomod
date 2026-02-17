#pragma once
// Copyright (c) 2023, kiwec, All rights reserved.

#include "config.h"

#include <string_view>

struct Packet;

#define NEOMOD_DOMAIN PACKAGE_NAME ".net"

// NOTE: Full version can be something like "b20200201.2cuttingedge"
#define OSU_VERSION_DATEONLY 20260101
#define OSU_VERSION "b20260101.1"

namespace BANCHO::Net {

// Send a packet to Bancho. Do not free it after calling this.
void send_packet(Packet& packet);

// Process networking logic. Should be called regularly from main thread.
void update_networking();

// Clean up networking. Should be called once when exiting neomod.
void cleanup_networking();

}  // namespace BANCHO::Net
