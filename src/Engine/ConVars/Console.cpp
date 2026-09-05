// Copyright (c) 2014, PG, All rights reserved.
#include "Console.h"

#include "AsyncIOHandler.h"
#include "SString.h"
#include "ConVar.h"
#include "ConVarHandler.h"
#include "Engine.h"
#include "File.h"
#include "Logging.h"
#include "SyncMutex.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <deque>

namespace {
// log scrollback, appended from the logger thread and polled by the console views on the main thread
Sync::mutex s_logMutex;
std::deque<Console::LogEntry> s_logEntries;
u64 s_logFirst{0};              // sequence of s_logEntries.front() (guarded by s_logMutex)
std::atomic<u64> s_logNext{0};  // sequence the next entry gets
std::atomic<u64> s_logClearGeneration{0};

// command history (main thread only)
std::vector<std::string> s_history;
int s_historySelection{-1};
}  // namespace

bool Console::processCommand(std::string_view command, bool fromFile) {
    if(command.length() < 1) return false;

    // remove whitespace from beginning/end of string
    SString::trim_inplace(command);

    // handle multiple commands separated by semicolons
    // TODO: some "commands" (like for user skins) can contain semicolons
    // as a workaround, avoid reading semicolon-separated commands from files as separate commands
    if(!fromFile && command.find(';') != std::string::npos && command.find("echo") == std::string::npos) {
        int unprocessed = 0;

        const auto commands = SString::split(command, ';');
        for(const auto command : commands) {
            unprocessed += processCommand(command);
        }
        // TODO:
        // if(unprocessed == 0) {
        (void)unprocessed;
        return true;
        // }
    }

    // separate convar name and value
    const auto tokens = SString::split(command, ' ');
    std::string commandName;
    std::string commandValue;
    for(size_t i = 0; i < tokens.size(); i++) {
        if(i == 0)
            commandName = tokens[i];
        else {
            commandValue.append(tokens[i]);
            if(i < (tokens.size() - 1)) commandValue.push_back(' ');
        }
    }

    // get convar
    ConVar *var = cvars().getConVarByName(commandName, false);
    if(!var) {
        debugLog("Unknown command: {:s}", commandName);
        return false;
    }

    if(fromFile && var->isFlagSet(cv::NOLOAD)) {
        return false;
    }

    // set new value (this handles all callbacks internally)
    if(commandValue.length() > 0) {
        var->setValue(commandValue);
    } else {
        var->exec();
        var->execArgs("");
        var->execFloat(var->getFloat());
    }

    // log
    if(cv::console_logging.getBool() && !var->isFlagSet(cv::HIDDEN)) {
        std::string logMessage;

        bool doLog = false;
        if(commandValue.length() < 1) {
            doLog = var->canHaveValue();  // assume ConCommands never have helpstrings

            logMessage = commandName;

            if(var->canHaveValue()) {
                logMessage.append(fmt::format(" = {:s} ( def. \"{:s}\" , ", var->getString(), var->getDefaultString()));
                logMessage.append(ConVar::typeToString(var->getType()));
                logMessage.append(", ");
                logMessage.append(ConVar::flagsToString(var->getFlags()));
                logMessage.append(" )");
            }

            std::string_view helpstring = var->getHelpstring();
            if(helpstring.length() > 0) {
                logMessage.append(" - ");
                logMessage.append(helpstring);
            }
        } else if(var->canHaveValue()) {
            doLog = true;

            logMessage = commandName;
            logMessage.append(" : ");
            logMessage.append(var->getString());
        }

        if(logMessage.length() > 0 && doLog) debugLog("{:s}", logMessage);
    }

    return true;
}

// TODO: move this bullshit osu_ prefix rewriting out of engine code or preferably dont do it at all
void Console::execConfigFile(std::string_view filename_view) {
    if(filename_view.empty()) return;
    std::string filename{filename_view};
    File::normalizeSlashes(filename);

    const bool is_absolute = filename.contains('/');
    if(!is_absolute) {  // allow absolute paths
        filename = fmt::format(MCENGINE_CFG_PATH "/{}", filename_view);
    }

    // handle extension
    if(!filename.ends_with(".cfg")) filename.append(".cfg");

    bool needs_write = false;

    std::string rewritten_file;

    {
        File configFile(filename, File::MODE::READ);
        if(!configFile.canRead()) {
            debugLog("NOTICE: file \"{:s}\" not found!", filename);
            return;
        }

        // collect commands first
        std::vector<std::string> cmds;
        for(auto line = configFile.readLine(); !line.empty() || configFile.canRead(); line = configFile.readLine()) {
            // only process non-empty lines
            if(!line.empty() && !SString::is_comment(line) && !SString::is_comment(line, "#")) {
                // McOsu used to prefix all convars with "osu_". Maybe it made sense when McEngine was
                // a separate thing, but in neomod everything is related to osu anyway, so it's redundant.
                // So, to avoid breaking old configs, we're removing the prefix for (almost) all convars here.
                if(line.starts_with("osu_") && !line.starts_with("osu_folder")) {
                    line.erase(0, 4);
                    needs_write = !is_absolute;  // DON'T OVERWRITE CONFIGS COMING FROM OTHER INSTALLATIONS!!!
                }

                // add command
                cmds.push_back(line);
            }

            rewritten_file.append(line);
            rewritten_file.push_back('\n');
        }

        // process the collected commands
        for(const auto &cmd : cmds) processCommand(cmd, true);
    }

    // if we don't remove prefixed lines, this could prevent users from
    // setting some convars back to their default value
    if(needs_write) {
        if(is_absolute) {
            fubar_abort();
        }
        io->write(filename, rewritten_file, [filename](bool success) {
            if(!success) {
                debugLog("WARNING: failed to write out config to {}!", filename);
            }
        });
    }
}

void Console::log(std::string_view text, Color color) {
    assert(!text.ends_with('\n') && !text.ends_with('\r') && "Console log strings can't end with a newline.");

    const auto maxLines = static_cast<size_t>(std::max(1, cv::console_scrollback_lines.getInt()));

    Sync::scoped_lock lock(s_logMutex);

    const auto push = [color](std::string_view line) {
        s_logEntries.emplace_back(std::string{line}, color);
        s_logNext.fetch_add(1, std::memory_order_release);
    };

    // split on any newlines inside the string
    if(text.find('\n') != std::string_view::npos) {
        auto lines = SString::split_newlines(text);
        for(auto &line : lines) {
            SString::trim_inplace(line);
            if(!line.empty()) push(line);
        }
    } else {
        push(text);
    }

    while(s_logEntries.size() > maxLines) {
        s_logEntries.pop_front();
        ++s_logFirst;
    }
}

void Console::clearLog() {
    Sync::scoped_lock lock(s_logMutex);
    s_logEntries.clear();
    s_logFirst = s_logNext.load(std::memory_order_acquire);
    s_logClearGeneration.fetch_add(1, std::memory_order_release);
}

u64 Console::getLogSequence() { return s_logNext.load(std::memory_order_acquire); }

u64 Console::getLogClearGeneration() { return s_logClearGeneration.load(std::memory_order_acquire); }

Console::LogRange Console::copyLogSince(u64 fromSeq, std::vector<LogEntry> &out) {
    Sync::scoped_lock lock(s_logMutex);
    const u64 next = s_logFirst + s_logEntries.size();
    for(u64 seq = std::max(fromSeq, s_logFirst); seq < next; ++seq) out.push_back(s_logEntries[seq - s_logFirst]);
    return {.first = s_logFirst, .next = next};
}

void Console::submit(std::string_view command) {
    s_historySelection = -1;
    if(command.empty()) return;

    s_history.emplace_back(command);
    processCommand(command);
    Logger::flush();  // make sure it's output immediately
}

std::string_view Console::cycleHistory(int dir) {
    if(s_history.empty()) return {};

    const int size = static_cast<int>(s_history.size());
    if(dir > 0)
        s_historySelection = (s_historySelection > size - 2) ? 0 : s_historySelection + 1;
    else
        s_historySelection = (s_historySelection < 1) ? size - 1 : s_historySelection - 1;

    return s_history[s_historySelection];
}

std::vector<Console::Suggestion> Console::getSuggestions(std::string_view input) {
    std::vector<Suggestion> suggestions;
    for(const auto *var : cvars().getConVarByLetter(input)) {
        std::string display{var->getName()};
        if(var->canHaveValue()) {
            switch(var->getType()) {
                using enum ConVar::CONVAR_TYPE;
                case BOOL:
                    display.append(fmt::format(" {}", (int)var->getBool()));
                    break;
                case INT:
                    display.append(fmt::format(" {}", var->getInt()));
                    break;
                case FLOAT:
                    display.append(fmt::format(" {:g}", var->getFloat()));
                    break;
                case STRING:
                    display.append(" ");
                    display.append(var->getString());
                    break;
            }
        }
        suggestions.push_back({.command = var->getName(), .display = std::move(display), .help = var->getHelpstring()});
    }
    return suggestions;
}
