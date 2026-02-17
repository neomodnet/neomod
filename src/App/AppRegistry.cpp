// Copyright (c) 2026, WH, All rights reserved.
#include "AppDescriptor.h"

#include "Osu.h"
#include "BaseFrameworkTest.h"
#include "AudioTester.h"
#include "NeomodEnvInterop.h"

#include <array>

namespace Mc {

static constexpr std::array sDescriptors{
    AppDescriptor{PACKAGE_NAME, [] -> App * { return new Osu(); }, neomod::createInterop, neomod::handleExistingWindow},
    AppDescriptor{"BaseFrameworkTest", [] -> App * { return new Mc::Tests::BaseFrameworkTest(); }},
    AppDescriptor{"AudioTester", [] -> App * { return new Mc::Tests::AudioTester(); }},
};

std::span<const AppDescriptor> getAllAppDescriptors() { return sDescriptors; }
const AppDescriptor &getDefaultAppDescriptor() { return sDescriptors[0]; }

}  // namespace Mc
