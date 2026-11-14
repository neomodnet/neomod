// Copyright (c) 2026, WH, All rights reserved.
// linked into every reloadable library: the program build it was compiled against, see Mc::hostStamp()
#include "config.h"

#ifdef MCENGINE_FEATURE_HOT_RELOAD

#include "host_stamp.h"
#include "types.h"

extern "C" const u64 mc_library_host_stamp{MC_HOST_STAMP};

#endif
