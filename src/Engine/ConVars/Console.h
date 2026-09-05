#pragma once
// Copyright (c) 2014, PG & 2025, WH, All rights reserved.

#include "Color.h"
#include "types.h"

#include <string>
#include <string_view>
#include <vector>

// command processing plus the state every console view shares (log scrollback, command history, suggestions)
// the log side is thread-safe: the logger thread appends at any time, the views poll it from the main thread
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

    // newlines must be stripped before being sent here (see Logging.cpp), embedded newlines split into multiple entries
    static void log(std::string_view text, Color color = 0xffffffff);
    static void clearLog();

    // one per appended entry, so views only copy when it moved
    [[nodiscard]] static u64 getLogSequence();
    // bumped by clearLog(), so views drop their own copies
    [[nodiscard]] static u64 getLogClearGeneration();
    // appends the retained entries with a sequence >= fromSeq to out (oldest first); entries older than the
    // returned range's first have been dropped from the scrollback
    static LogRange copyLogSince(u64 fromSeq, std::vector<LogEntry> &out);

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
