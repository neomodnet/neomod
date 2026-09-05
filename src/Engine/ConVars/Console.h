#pragma once
// Copyright (c) 2014, PG & 2025, WH, All rights reserved.

#include "Color.h"
#include "types.h"

#include <string>
#include <string_view>
#include <vector>

// command processing plus the state every console view shares (log scrollback, command history, suggestions)
// the logger thread hands over log lines at any time (log()), the main thread moves them into the scrollback once per
// frame (updateLog()) and the views read it from there, without any locking
class Console {
   public:
    static bool processCommand(std::string_view command, bool fromFile = false);
    static void execConfigFile(std::string_view filename_view);

    // log scrollback
    struct LogEntry {
        std::string text;
        Color color;
    };

    // sequences of the oldest retained entry and of the one after the newest
    struct LogRange {
        u64 first;
        u64 next;
    };

    // any thread. newlines must be stripped before being sent here (see Logging.cpp), embedded newlines split into
    // multiple entries
    static void log(std::string_view text, Color color = 0xffffffff);

    // everything below is main thread only

    // moves the lines logged since the last call into the scrollback (the engine calls it before the views tick).
    // never waits for the logger thread: a batch it is still appending to is picked up next frame
    static void updateLog();
    static void clearLog();
    // one sequence per appended entry, so views only re-read when it moved; clearLog() moves first up to next
    [[nodiscard]] static LogRange getLogRange();
    // seq must be inside getLogRange(); the reference is valid until the next updateLog()/clearLog()
    [[nodiscard]] static const LogEntry &getLogEntry(u64 seq);

    // command history
    // history push + processCommand + log flush (an empty command only resets the history selection)
    static void submit(std::string_view command);
    // dir > 0 = newer, dir < 0 = older, wrapping around; empty if there is no history
    [[nodiscard]] static std::string_view cycleHistory(int dir);

    // suggestions
    struct Suggestion {
        std::string_view command;  // convar name
        std::string display;       // name plus current value
        std::string_view help;
    };
    [[nodiscard]] static std::vector<Suggestion> getSuggestions(std::string_view input);
};
