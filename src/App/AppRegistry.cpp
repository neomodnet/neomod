// Copyright (c) 2026, WH, All rights reserved.
#include "AppDescriptor.h"
#include "config.h"

#include "Osu.h"
#include "NeomodEnvInterop.h"

#ifdef MCENGINE_TESTS

#include "BaseFrameworkTest.h"
#include "AudioTester.h"
#include "HitSoundTest.h"
#include "SkinLoadTest.h"
#include "AsyncPoolTest.h"
#include "EmojiRenderTest.h"
#include "NetworkTest.h"

#include <array>

namespace Mc {

static constexpr std::array sDescriptors{
    AppDescriptor{PACKAGE_NAME, [] -> App * { return new Osu(); }, neomod::createInterop, neomod::handleExistingWindow},
    AppDescriptor{"BaseFrameworkTest", [] -> App * { return new Mc::Tests::BaseFrameworkTest(); }},
    AppDescriptor{"AudioTester", [] -> App * { return new Mc::Tests::AudioTester(); }},
    AppDescriptor{"HitSoundTest", [] -> App * { return new Mc::Tests::HitSoundTest(); }},
    AppDescriptor{"SkinLoadTest", [] -> App * { return new Mc::Tests::SkinLoadTest(); }},
    AppDescriptor{"AsyncPoolTest", [] -> App * { return new Mc::Tests::AsyncPoolTest(); }},
    AppDescriptor{"EmojiRenderTest", [] -> App * { return new Mc::Tests::EmojiRenderTest(); }},
    AppDescriptor{"NetworkTest", [] -> App * { return new Mc::Tests::NetworkTest(); }},
};

#else

namespace Mc {

static constexpr std::array sDescriptors{
    AppDescriptor{PACKAGE_NAME, [] -> App * { return new Osu(); }, neomod::createInterop, neomod::handleExistingWindow},
};

#endif  // MCENGINE_TESTS

std::span<const AppDescriptor> getAllAppDescriptors() { return sDescriptors; }
const AppDescriptor &getDefaultAppDescriptor() { return sDescriptors[0]; }

}  // namespace Mc
