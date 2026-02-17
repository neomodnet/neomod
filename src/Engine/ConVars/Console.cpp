// Copyright (c) 2014, PG, All rights reserved.
#include "Console.h"

#include "AsyncIOHandler.h"
#include "SString.h"
#include "ConVar.h"
#include "ConVarHandler.h"
#include "Engine.h"
#include "File.h"
#include "Logging.h"

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
        for(const auto &command : commands) {
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
                logMessage.append(ConVarHandler::flagsToString(var->getFlags()));
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

void Console::execConfigFile(std::string_view filename_view) {
    if(filename_view.empty()) return;
    std::string filename{filename_view};

    if(!filename.contains('/'))  // allow absolute paths
        filename = fmt::format(MCENGINE_CFG_PATH "/{}", filename_view);

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
            if(!line.empty()) {
                // handle comments - find "//" and remove everything after
                const auto commentIndex = line.find("//");
                if(commentIndex != std::string::npos) line.erase(commentIndex, line.length() - commentIndex);

                // TODO: move this out of engine code
                // McOsu used to prefix all convars with "osu_". Maybe it made sense when McEngine was
                // a separate thing, but in neomod everything is related to osu anyway, so it's redundant.
                // So, to avoid breaking old configs, we're removing the prefix for (almost) all convars here.
                if(line.starts_with("osu_") && !line.starts_with("osu_folder")) {
                    line.erase(0, 4);
                    needs_write = true;
                }

                // add command (original adds all processed lines, even if they become empty after comment removal)
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
        io->write(filename, rewritten_file, [filename](bool success) {
            if(!success) {
                debugLog("WARNING: failed to write out config to {}!", filename);
            }
        });
    }
}
