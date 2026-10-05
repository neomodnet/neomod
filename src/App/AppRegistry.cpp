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
#include "BeatmapFileTest.h"
#include "ConVarTest.h"
#include "CryptoTest.h"
#include "DirectoryWatcherTest.h"
#include "EmojiRenderTest.h"
#include "NetworkTest.h"
#include "PacketTest.h"
#include "PlayfieldTest.h"
#include "SliderRenderTest.h"

#include <array>

namespace Mc {

static constexpr std::array sDescriptors{
    AppDescriptor{PACKAGE_NAME, [] -> App * { return new Osu(); }, neomod::createInterop, true},
    AppDescriptor{"BaseFrameworkTest", [] -> App * { return new Mc::Tests::BaseFrameworkTest(); }},
    AppDescriptor{"AudioTester", [] -> App * { return new Mc::Tests::AudioTester(); }},
    AppDescriptor{"HitSoundTest", [] -> App * { return new Mc::Tests::HitSoundTest(); }},
    AppDescriptor{"SkinLoadTest", [] -> App * { return new Mc::Tests::SkinLoadTest(); }},
    AppDescriptor{"AsyncPoolTest", [] -> App * { return new Mc::Tests::AsyncPoolTest(); }},
    AppDescriptor{"BeatmapFileTest", [] -> App * { return new Mc::Tests::BeatmapFileTest(); }},
    AppDescriptor{"ConVarTest", [] -> App * { return new Mc::Tests::ConVarTest(); }},
    AppDescriptor{"CryptoTest", [] -> App * { return new Mc::Tests::CryptoTest(); }},
    AppDescriptor{"DirectoryWatcherTest", [] -> App * { return new Mc::Tests::DirectoryWatcherTest(); }},
    AppDescriptor{"EmojiRenderTest", [] -> App * { return new Mc::Tests::EmojiRenderTest(); }},
    AppDescriptor{"NetworkTest", [] -> App * { return new Mc::Tests::NetworkTest(); }},
    AppDescriptor{"PacketTest", [] -> App * { return new Mc::Tests::PacketTest(); }},
    AppDescriptor{"PlayfieldTest", [] -> App * { return new Mc::Tests::PlayfieldTest(); }},
    AppDescriptor{"SliderRenderTest", [] -> App * { return new Mc::Tests::SliderRenderTest(); }},
};

#else

namespace Mc {

static constexpr std::array sDescriptors{
    AppDescriptor{PACKAGE_NAME, [] -> App * { return new Osu(); }, neomod::createInterop, true},
};

#endif  // MCENGINE_TESTS

std::span<const AppDescriptor> getAllAppDescriptors() { return sDescriptors; }
const AppDescriptor &getDefaultAppDescriptor() { return sDescriptors[0]; }

}  // namespace Mc
