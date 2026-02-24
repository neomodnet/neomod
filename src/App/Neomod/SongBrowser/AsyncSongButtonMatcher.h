#pragma once
// Copyright (c) 2016 PG, All rights reserved.
#include "AsyncCancellable.h"

#include <string>
#include <vector>

class SongButton;
namespace AsyncSongButtonMatcher {
Async::CancellableHandle<void> submitSearchMatch(std::vector<SongButton *> songButtons, const std::string &searchString,
                                                 const std::string &hardcodedSearchString, float speedMultiplier);
}
