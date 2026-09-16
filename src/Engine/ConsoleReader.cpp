// Copyright (c) 2026, WH, All rights reserved.
#include "ConsoleReader.h"

#include "Console.h"
#include "Parsing.h"
#include "Thread.h"
#include "Engine.h"
#include "Timing.h"

#include <algorithm>  // std::clamp
#include <array>
#include <cstdio>

#ifdef MCENGINE_PLATFORM_WASM
#include <emscripten/emscripten.h>
#elif defined(MCENGINE_PLATFORM_WINDOWS)
#include "WinDebloatDefs.h"
#include <windows.h>
#include <io.h>  // _get_osfhandle
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace Mc {

using std::string_view_literals::operator""sv;

#ifndef MCENGINE_PLATFORM_WASM

namespace {
// std::getline(std::cin, line) equivalent (workaround for segfault on llvm-mingw with LTO)
bool readLine(std::string &line) {
    line.clear();
    std::array<char, 512> buf;  // NOLINT
    while(fgets(buf.data(), static_cast<int>(buf.size()), stdin)) {
        line += buf.data();
        if(line.ends_with('\n')) {
            line.pop_back();
            return true;
        }
    }
    return !line.empty();  // last line without a trailing newline
}
}  // namespace

ConsoleReader::ConsoleReader() {
    bool startThread = true;

    // piped scripts (headless testing) must be fully queued before the engine's first frame (for determinism)
    // so (synchronously) drain whatever the writer already piped in early
#ifdef MCENGINE_PLATFORM_WINDOWS
    const int stdinFd = _fileno(stdin);  // -2 if there is no stdin at all (windows-subsystem process without a console)
    const HANDLE stdinHandle = stdinFd >= 0 ? reinterpret_cast<HANDLE>(_get_osfhandle(stdinFd)) : INVALID_HANDLE_VALUE;
    const DWORD stdinType = stdinHandle == INVALID_HANDLE_VALUE ? FILE_TYPE_UNKNOWN : GetFileType(stdinHandle);
    const bool scripted = (stdinType == FILE_TYPE_DISK || stdinType == FILE_TYPE_PIPE);
    // there's no poll() for anonymous pipes, so poll PeekNamedPipe for data instead (it fails once the writer is gone)
    const auto waitReadable = [stdinHandle, stdinType]() -> bool {
        if(stdinType == FILE_TYPE_DISK) return true;  // getline() just runs into EOF
        for(int i = 0; i < 10; i++) {
            DWORD avail = 0;
            if(!PeekNamedPipe(stdinHandle, nullptr, 0, nullptr, &avail, nullptr) || avail > 0) return true;
            Timing::sleepMS(5);
        }
        return false;
    };
#else
    const bool scripted = !isatty(STDIN_FILENO);
    const auto waitReadable = []() -> bool {
        struct pollfd pfd{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
        return poll(&pfd, 1, 50) > 0;
    };
#endif
    if(scripted) {
        std::string line;
        while(waitReadable()) {
            if(!readLine(line)) {
                startThread = false;  // EOF
                break;
            }
            const bool gotExit = (line == "exit"sv || line == "shutdown"sv || line == "restart"sv || line == "crash"sv);
            this->stdinQueue.push_back(std::move(line));
            if(gotExit) {
                startThread = false;
                break;
            }
        }
    }
    if(startThread) {
        this->stdinThread = Sync::jthread{[this](const Sync::stop_token &stoken) { this->stdinReaderThread(stoken); }};
    }
}

ConsoleReader::~ConsoleReader() {
    if(this->stdinThread.joinable()) {
        // there's no portable way to programmatically unblock a thread blocked in fgets()/std::getline()...
        // this just leaves a zombie thread alive until you send an input/close the terminal
        // oh well, we're shutting down anyways
        this->stdinThread.request_stop();
        this->stdinThread.detach();
    }
}

void ConsoleReader::stdinReaderThread(const Sync::stop_token &stopToken) {
    McThread::set_current_thread_name("stdin_reader");
    McThread::set_current_thread_prio(McThread::Priority::LOW);

    std::string line;
    while(!stopToken.stop_requested() && readLine(line)) {
        if(stopToken.stop_requested()) return;

        Sync::scoped_lock lock(this->stdinMutex);
        // this is a bit of a hack but there's no easy way to unblock the stdin read from the main thread
        const bool gotExit = (line == "exit"sv || line == "shutdown"sv || line == "restart"sv || line == "crash"sv);
        this->stdinQueue.push_back(std::move(line));
        if(gotExit) return;
    }
}

#else  // MCENGINE_PLATFORM_WASM

// on WASM, stdin is polled from the main thread via JS (pthreads can't do blocking stdin reads)
ConsoleReader::ConsoleReader() = default;
ConsoleReader::~ConsoleReader() = default;

#endif  // !MCENGINE_PLATFORM_WASM

void ConsoleReader::processStdin(double current_time) {
    // @wait support: count down frames before resuming command processing
    if(this->stdinWaitFrames > 0) {
        this->stdinWaitFrames--;
        return;
    }

    // @wait_secs
    if(this->stdinWaitDeadline > current_time) {
        return;
    }

#ifdef MCENGINE_PLATFORM_WASM
    // poll the JS-side line buffer (filled by process.stdin in wasm-node-polyfill.js)
    while(true) {
        char *line = (char *)EM_ASM_PTR({
            if(globalThis.__stdinLines && globalThis.__stdinLines.length > 0) {
                var line = globalThis.__stdinLines.shift();
                var len = lengthBytesUTF8(line) + 1;
                var ptr = _malloc(len);
                stringToUTF8(line, ptr, len);
                return ptr;
            }
            return 0;
        });
        if(!line) break;
        std::string cmd(line);
        free(line);

        if(cmd.starts_with("@wait_secs")) {
            this->stdinWaitDeadline =
                current_time + std::max(0., Parsing::strto<double>(cmd.substr("@wait_secs"sv.size())));
            break;
        } else if(cmd.starts_with("@wait")) {
            this->stdinWaitFrames = std::max(1, Parsing::strto<int>(cmd.substr("@wait"sv.size())));
            break;  // can't sleep in the main thread in WASM
        }

        Console::processCommand(cmd);
        if(engine->isShuttingDown()) break;
    }
#else
    int sleepMillis = 0;
    {
        Sync::scoped_lock lock(this->stdinMutex);
        while(!this->stdinQueue.empty()) {
            std::string cmd = std::move(this->stdinQueue.front());
            this->stdinQueue.pop_front();

            if(cmd.starts_with("@wait_secs")) {
                this->stdinWaitDeadline =
                    current_time + std::max(0., Parsing::strto<double>(cmd.substr("@wait_secs"sv.size())));
                break;
            } else if(cmd.starts_with("@wait")) {
                this->stdinWaitFrames = std::max(1, Parsing::strto<int>(cmd.substr("@wait"sv.size())));
                break;
            } else if(cmd.starts_with("@sleep")) {
                sleepMillis = std::clamp(Parsing::strto<int>(cmd.substr("@sleep"sv.size())), 0, 60000);
                break;
            }
            Console::processCommand(cmd);
        }
    }
    if(sleepMillis > 0) {
        const int remainder = sleepMillis % 1000;
        const int wholeSeconds = (sleepMillis - remainder) / 1000;
        for(int sec = 0; sec < wholeSeconds; ++sec) {
            Timing::sleepMS(1000);
        }
        if(remainder) {
            Timing::sleepMS(remainder);
        }
    }
#endif
}

}  // namespace Mc