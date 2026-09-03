// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#ifndef APPDESCRIPTOR_H
#define APPDESCRIPTOR_H

#include <span>
#include <string_view>

class App;
namespace Mc {
struct AppDescriptor {
    std::string_view name{};
    App *(*create)(){nullptr};
    // null = use base Environment::Interop (no-op)
    void *(*createInterop)(void *env){nullptr};
    // null = skip existing-window check
    void (*handleExistingWindow)(int argc, char *argv[]){nullptr};
};

// implemented in AppRegistry.cpp
std::span<const AppDescriptor> getAllAppDescriptors();
const AppDescriptor &getDefaultAppDescriptor();
}  // namespace Mc

#endif
