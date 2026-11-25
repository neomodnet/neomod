// Copyright (c) 2025, WH, All rights reserved.
#pragma once

// saves crash data to <data>/crashes/<launch time>_<pid> and starts the crash reporter (see CrashReporter.h).
// call once in main(), after logging is initialized
namespace CrashHandler {
void init();
}
