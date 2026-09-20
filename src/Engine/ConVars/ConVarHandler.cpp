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

ConVarHandler::ConVarHandler() {
    this->vConVarArray.reserve(1024);
    this->vConVarMap.reserve(1024);
}

void ConVarHandler::setCVSubmittableCheckFunc(CVSubmittableCriteriaFunc func) {
    this->areAllCvarsSubmittableExtraCheck = func;
}

ConVar *ConVarHandler::getConVar_int(std::string_view name) const {
    auto it = this->vConVarMap.find(name);
    if(it != this->vConVarMap.end()) return it->second;
    return nullptr;
}

// public
static ConVar _emptyDummyConVar(
    "emptyDummyConVar", 42.0f, cv::CLIENT | cv::HIDDEN | cv::NOLOAD | cv::NOSAVE,
    "this placeholder convar is returned by ConVarHandler::getConVarByName() if no matching convar is found");

ConVar *ConVarHandler::getConVarByName(std::string_view name, bool warnIfNotFound) const {
    ConVar *found = this->getConVar_int(name);
    if(found) return found;

    if(warnIfNotFound) {
        std::string errormsg = "ENGINE: ConVar \"";
        errormsg.append(name);
        errormsg.append("\" does not exist...");
        logRaw("{:s}", errormsg);
        engine->showMessageWarning("Engine Error", errormsg.c_str());
    }

    if(!warnIfNotFound)
        return nullptr;
    else
        return &_emptyDummyConVar;
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

std::vector<ConVar *> ConVarHandler::getNonSubmittableCvars() const {
    std::vector<ConVar *> list;

    for(auto *cv : this->vConVarArray) {
        if(!cv->bNonSubmittable) continue;

        list.push_back(cv);
    }

    return list;
}

bool ConVarHandler::areAllCvarsSubmittable() const {
    if(this->iNumNonSubmittable > 0) return false;

    if(!!this->areAllCvarsSubmittableExtraCheck) {
        return this->areAllCvarsSubmittableExtraCheck();
    }

    return true;
}

// the changes below apply to many convars at once: every one of them gets published before any callback runs,
// so that callbacks never get to see a half-applied state

void ConVarHandler::setProtectionEnforced(bool enforced) {
    if(enforced == this->bProtectionEnforced) return;
    this->bProtectionEnforced = enforced;

    std::vector<std::pair<ConVar *, ConVar::Value>> changed;
    for(auto *cv : this->vConVarArray) {
        if(!cv->isProtected()) continue;
        changed.emplace_back(cv, cv->snapshot());
        cv->resolve();
    }
    logIfCV(debug_cv, "protection lock {:s} for {:d} protected convars", enforced ? "on" : "off", changed.size());
    for(const auto &[cv, old] : changed) cv->notifyIfChanged(old);
}

void ConVarHandler::clearLayer(CvarEditor editor) {
    // (nothing gets taken away here, these are regular writes)
    if(editor == CvarEditor::CLIENT) {
        for(auto *cv : this->vConVarArray) cv->clearValue(editor);
        return;
    }

    std::vector<std::pair<ConVar *, ConVar::Value>> changed;
    for(auto *cv : this->vConVarArray) {
        auto &layer = (editor == CvarEditor::SKIN) ? cv->skinValue : cv->serverValue;
        const bool hasPolicy = (editor == CvarEditor::SERVER) && cv->serverProtectionPolicy != CvarProtection::DEFAULT;
        if(!layer && !hasPolicy) continue;
        changed.emplace_back(cv, cv->snapshot());

        layer.reset();
        if(hasPolicy) cv->serverProtectionPolicy = CvarProtection::DEFAULT;
        cv->resolve();
    }
    for(const auto &[cv, old] : changed) cv->notifyIfChanged(old);
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
