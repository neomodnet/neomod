#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include <string_view>

// runs the editor tests whose names contain `filter` (all of them for an empty one), logging what they find; whether
// they all passed (true in a build without tests)
bool runEditorTests(std::string_view filter);
