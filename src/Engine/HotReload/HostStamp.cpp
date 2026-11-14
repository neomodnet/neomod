// Copyright (c) 2026, WH, All rights reserved.
#include "HostStamp.h"

#include "config.h"

#ifdef MCENGINE_FEATURE_HOT_RELOAD

#include "host_stamp.h"

u64 Mc::hostStamp() { return MC_HOST_STAMP; }

#endif
