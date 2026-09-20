// Copyright (c) 2011, PG & 2025, WH & 2025, kiwec, All rights reserved.
#include "ConVarHandler.h"
#include "ConVar.h"
#include "Console.h"

#include "AsyncIOHandler.h"
#include "Logging.h"
#include "Paths.h"
#include "Engine.h"
#include "SString.h"
#include "Graphics.h"

#include "binary_embed.h"

#include "fmt/chrono.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

// singleton init
ConVarHandler &cvars() {
    static ConVarHandler instance;
    return instance;
}

struct ConVarHandler::PendingChange {
    ConVar *cvar;
    ConVar::Value old;
    bool callbacks;  // whether anything that happened to the convar wanted its callbacks to run
};

ConVarHandler::ConVarHandler() {
    this->vConVarArray.reserve(1024);
    this->vConVarMap.reserve(1024);
}

ConVarHandler::~ConVarHandler() = default;

ConVar *ConVarHandler::getConVarByName(std::string_view name) const {
    auto it = this->vConVarMap.find(name);
    if(it != this->vConVarMap.end()) return it->second;
    return nullptr;
}

std::vector<ConVar *> ConVarHandler::getConVarByLetter(std::string_view letters) const {
    std::unordered_set<std::string_view> matchingConVarNames;
    std::vector<ConVar *> matchingConVars;
    {
        if(letters.length() < 1) return matchingConVars;

        const std::vector<ConVar *> &convars = this->vConVarArray;

        // first try matching exactly
        for(auto convar : convars) {
            if(convar->isFlagSet(cv::HIDDEN)) continue;

            const std::string_view name = convar->getName();
            if(name.find(letters) != std::string::npos) {
                if(letters.length() > 1) matchingConVarNames.insert(name);

                matchingConVars.push_back(convar);
            }
        }

        // then try matching substrings
        if(letters.length() > 1) {
            for(auto convar : convars) {
                if(convar->isFlagSet(cv::HIDDEN)) continue;
                const std::string_view name = convar->getName();

                if(name.find(letters) != std::string::npos) {
                    if(!matchingConVarNames.contains(name)) {
                        matchingConVarNames.insert(name);
                        matchingConVars.push_back(convar);
                    }
                }
            }
        }

        // (results should be displayed in vector order)
    }
    return matchingConVars;
}

std::vector<ConVar *> ConVarHandler::getNonDefaultProtectedCvars() const {
    std::vector<ConVar *> list;

    for(auto *cv : this->vConVarArray) {
        if(!cv->bProtectedNonDefault) continue;

        list.push_back(cv);
    }

    return list;
}

void ConVarHandler::beginChange() {
    assert(McThread::is_main_thread() && "convars belong to the main thread");
    this->iChangeDepth++;
}

void ConVarHandler::endChange() {
    assert(this->iChangeDepth > 0);
    if(--this->iChangeDepth > 0) return;

    // everything is in place. the change is over before anyone hears about it, so that callbacks are free to change
    // convars themselves (or to begin a change of their own)
    const std::vector<PendingChange> pending = std::exchange(this->vPending, {});
    for(const auto &[cv, old, callbacks] : pending) cv->notifyIfChanged(old, callbacks);
}

bool ConVarHandler::remember(ConVar &cvar, bool callbacks) {
    if(this->iChangeDepth == 0) return false;

    // (callbacks run if anything that happened to the convar wanted them to)
    if(const auto it = std::ranges::find(this->vPending, &cvar, &PendingChange::cvar); it != this->vPending.end()) {
        it->callbacks |= callbacks;
    } else {
        this->vPending.push_back({.cvar = &cvar, .old = *cvar.effectiveValue, .callbacks = callbacks});
    }
    return true;
}

void ConVarHandler::setProtectionEnforced(bool enforced) {
    if(enforced == this->bProtectionEnforced) return;
    this->bProtectionEnforced = enforced;

    this->change([&] {
        size_t numProtected = 0;
        for(auto *cv : this->vConVarArray) {
            if(!cv->isProtected()) continue;
            cv->reresolve();
            numProtected++;
        }
        logIfCV(debug_cv, "protection lock {:s} for {:d} protected convars", enforced ? "on" : "off", numProtected);
    });
}

void ConVarHandler::clearLayer(CvarEditor editor) {
    this->change([&] {
        for(auto *cv : this->vConVarArray) {
            cv->clearValue(editor);
            if(editor == CvarEditor::SERVER) cv->setServerProtected(CvarProtection::DEFAULT);
        }
    });
}

void ConVarHandler::beginSession(std::span<ConVar *const> convars) {
    // (nothing for anyone to hear about: the values are what they were, from somewhere else)
    this->change([&] {
        for(auto *cv : convars) {
            if(cv->setInSession(true)) this->vSessionConVars.push_back(cv);
        }
    });
}

void ConVarHandler::endSession() {
    this->change([&] {
        for(auto *cv : this->vSessionConVars) cv->setInSession(false);
        this->vSessionConVars.clear();
    });
}

std::vector<CvarSetResult> ConVarHandler::setLayer(CvarEditor editor,
                                                   std::span<const std::pair<ConVar *, std::string>> values) {
    std::vector<CvarSetResult> results(values.size(), CvarSetResult::DENIED);
    assert(editor != CvarEditor::CLIENT && "the client's values don't get replaced as a whole");
    if(editor == CvarEditor::CLIENT) return results;

    this->change([&] {
        std::vector<ConVar *> kept;  // what stays (or becomes) set
        for(size_t i = 0; i < values.size(); i++) {
            auto *cv = values[i].first;
            if(!cv->canHaveValue()) continue;  // (further down)

            // (a vetoed write changes nothing, which includes not losing the value that may be there already)
            results[i] = cv->setValue(values[i].second, true, editor);
            if(results[i] != CvarSetResult::DENIED && results[i] != CvarSetResult::INVALID) kept.push_back(cv);
        }

        for(auto *cv : this->vConVarArray) {
            if(cv->layer(editor) && !std::ranges::contains(kept, cv)) cv->clearValue(editor);
        }
    });

    for(size_t i = 0; i < values.size(); i++) {
        if(!values[i].first->canHaveValue()) results[i] = values[i].first->setValue(values[i].second, true, editor);
    }

    return results;
}

//*****************************//
//	ConVarHandler ConCommands  //
//*****************************//

struct ConVarHandler::ConVarBuiltins final {
    static void find(std::string_view args);
    static void help(std::string_view args);
    static void listcommands(void);
    static void dumpcommands(void);
    static void echo(std::string_view args);
};

void ConVarHandler::ConVarBuiltins::find(std::string_view args) {
    if(args.length() < 1) {
        logRaw("Usage:  find <string>");
        return;
    }

    const std::vector<ConVar *> &convars = cvars().getConVarArray();

    std::vector<ConVar *> matchingConVars;
    for(auto convar : convars) {
        if(convar->isFlagSet(cv::HIDDEN)) continue;

        const std::string_view name = convar->getName();
        if(name.find(args) != std::string::npos) matchingConVars.push_back(convar);
    }

    if(matchingConVars.size() > 0) {
        std::ranges::sort(matchingConVars, {}, &ConVar::getName);
    }

    if(matchingConVars.size() < 1) {
        logRaw("No commands found containing {:s}.", args);
        return;
    }

    logRaw("----------------------------------------------");
    {
        std::string thelog = "[ find : ";
        thelog.append(args);
        thelog.append(" ]");
        logRaw("{:s}", thelog);

        for(auto &matchingConVar : matchingConVars) {
            logRaw("{:s}", matchingConVar->getName());
        }
    }
    logRaw("----------------------------------------------");
}

void ConVarHandler::ConVarBuiltins::help(std::string_view args) {
    SString::trim_inplace(args);

    if(args.length() < 1) {
        logRaw("Usage:  help <cvarname>");
        logRaw("To get a list of all available commands, type \"listcommands\".");
        return;
    }

    const std::vector<ConVar *> matches = cvars().getConVarByLetter(args);

    if(matches.size() < 1) {
        logRaw("ConVar {:s} does not exist.", args);
        return;
    }

    // use closest match
    size_t index = 0;
    for(size_t i = 0; i < matches.size(); i++) {
        if(matches[i]->getName() == args) {
            index = i;
            break;
        }
    }
    ConVar *match = matches[index];

    std::string_view helpstring = match->getHelpstring();
    if(helpstring.length() < 1) {
        logRaw("ConVar {:s} does not have a helpstring.", match->getName());
        return;
    }

    std::string thelog{match->getName()};
    {
        if(match->canHaveValue()) {
            const auto &cv_str = match->getString();
            const auto &default_str = match->getDefaultString();
            thelog.append(fmt::format(" = {:s} ( def. \"{:s}\" , ", cv_str, default_str));
            thelog.append(ConVar::typeToString(match->getType()));
            thelog.append(", ");
            thelog.append(ConVar::flagsToString(match->getFlags()));
            thelog.append(" )");
        }

        thelog.append(" - ");
        thelog.append(helpstring);
    }
    logRaw("{:s}", thelog);
}

void ConVarHandler::ConVarBuiltins::listcommands(void) {
    logRaw("----------------------------------------------");
    {
        std::vector<ConVar *> convars = cvars().getConVarArray();
        std::ranges::sort(convars, {}, &ConVar::getName);

        for(auto &convar : convars) {
            if(convar->isFlagSet(cv::HIDDEN)) continue;

            ConVar *var = convar;

            std::string tstring{var->getName()};
            {
                if(var->canHaveValue()) {
                    const auto &var_str = var->getString();
                    const auto &default_str = var->getDefaultString();
                    tstring.append(fmt::format(" = {:s} ( def. \"{:s}\" , ", var_str, default_str));
                    tstring.append(ConVar::typeToString(var->getType()));
                    tstring.append(", ");
                    tstring.append(ConVar::flagsToString(var->getFlags()));
                    tstring.append(" )");
                }

                if(var->getHelpstring().length() > 0) {
                    tstring.append(" - ");
                    tstring.append(var->getHelpstring());
                }
            }
            logRaw("{:s}", tstring);
        }
    }
    logRaw("----------------------------------------------");
}

void ConVarHandler::ConVarBuiltins::dumpcommands(void) {
    // in assets/misc/convar_template.html
    assert(ALL_BINMAP.contains("convar_template"));
    std::string html_template{ALL_BINMAP.at("convar_template")};

    std::vector<ConVar *> convars = cvars().getConVarArray();
    std::ranges::sort(convars, {}, &ConVar::getName);

    std::string html = R"(<section class="variables">)";
    for(auto var : convars) {
        // only doing this because of some stupid spurious warning with LTO
#define STRIF_(FLAG__, flag__) var->isFlagSet(cv::FLAG__) ? "<span class=\"flag " #flag__ "\">" #FLAG__ "</span>" : ""
        const std::string flags = fmt::format("\n{:s}{:s}{:s}{:s}{:s}\n",    //
                                              STRIF_(CLIENT, client),        //
                                              STRIF_(SKINS, skins),          //
                                              STRIF_(SERVER, server),        //
                                              STRIF_(PROTECTED, protected),  //
                                              STRIF_(GAMEPLAY, gameplay));   //
#undef STRIF_

        html.append(fmt::format(R"(<div>
    <cv-header>
        <cv-name>{:s}</cv-name>
        <cv-default>{:s}</cv-default>
    </cv-header>
    <cv-description>{:s}</cv-description>
    <cv-flags>{:s}</cv-flags>
</div>)",
                                var->getName(), var->getFancyDefaultValue(), var->getHelpstring(), flags));
    }
    html.append(R"(</section>)");

    html.append(fmt::format(R"(<p style="text-align:center">
        This page was generated on {:%Y-%m-%d} for )" PACKAGE_NAME R"( v{:.2f}.<br>
        Use the <code>dumpcommands</code> command to regenerate it yourself.
    </p>)",
                            fmt::gmtime(std::time(nullptr)), cv::version.getDouble()));

    constexpr std::string_view marker = "{{CONVARS_HERE}}"sv;
    size_t pos = html_template.find(marker);
    html_template.replace(pos, marker.length(), html);

    io->write(Mc::Paths::data() + "/variables.htm", std::move(html_template), [](bool success) -> void {
        if(success) {
            logRaw("ConVars dumped to variables.htm");
        } else {
            logRaw("Failed to dump ConVars to variables.htm");
        }
    });
}

void ConVarHandler::ConVarBuiltins::echo(std::string_view args) {
    if(args.length() > 0) {
        logRaw(args);
    }
}

#undef CONVARDEFS_H
#define DEFINE_CONVARS

#include "ConVarDefs.h"
