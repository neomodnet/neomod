// Copyright (c) 2011, PG & 2025, WH & 2025, kiwec, All rights reserved.
#include "ConVarHandler.h"
#include "ConVar.h"

#include "AsyncIOHandler.h"
#include "Logging.h"
#include "Engine.h"
#include "SString.h"
#include "Graphics.h"

#include "binary_embed.h"

#include "fmt/chrono.h"

#include <algorithm>
#include <unordered_set>

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
ConVar *ConVarHandler::getConVarByName(std::string_view name, bool warnIfNotFound) const {
    static ConVar _emptyDummyConVar(
        "emptyDummyConVar", 42.0f, cv::CLIENT,
        "this placeholder convar is returned by cvars().getConVarByName() if no matching convar is found");

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

std::string ConVarHandler::flagsToString(uint8_t flags) {
    if(flags == 0) {
        return "no flags";
    }

    static constexpr const auto flagStringPairArray = std::array{
        std::pair{cv::CLIENT, "client"},       std::pair{cv::SERVER, "server"},     std::pair{cv::SKINS, "skins"},
        std::pair{cv::PROTECTED, "protected"}, std::pair{cv::GAMEPLAY, "gameplay"}, std::pair{cv::HIDDEN, "hidden"},
        std::pair{cv::NOSAVE, "nosave"},       std::pair{cv::NOLOAD, "noload"}};

    std::string string;
    for(bool first = true; const auto &[flag, str] : flagStringPairArray) {
        if((flags & flag) == flag) {
            if(!first) {
                string.push_back(' ');
            }
            first = false;
            string.append(str);
        }
    }

    return string;
}

std::vector<ConVar *> ConVarHandler::getNonSubmittableCvars() const {
    std::vector<ConVar *> list;

    for(auto *cv : this->vConVarArray) {
        if(!cv->isProtected() || cv->isDefault()) continue;

        list.push_back(cv);
    }

    return list;
}

bool ConVarHandler::areAllCvarsSubmittable() {
    if(!this->getNonSubmittableCvars().empty()) return false;

    if(!!this->areAllCvarsSubmittableExtraCheck) {
        return this->areAllCvarsSubmittableExtraCheck();
    }

    return true;
}

void ConVarHandler::invalidateAllProtectedCaches() {
    for(auto *cv : this->getConVarArray()) {
        if(cv->isFlagSet(cv::PROTECTED)) cv->invalidateCache();
    }
}

void ConVarHandler::resetServerCvars() {
    for(auto *cv : this->getConVarArray()) {
        cv->hasServerValue.store(false, std::memory_order_release);
        cv->setServerProtected(CvarProtection::DEFAULT);
        cv->invalidateCache();
    }
}

void ConVarHandler::resetSkinCvars() {
    for(auto *cv : this->getConVarArray()) {
        cv->hasSkinValue.store(false, std::memory_order_release);
        cv->invalidateCache();
    }
}

bool ConVarHandler::removeServerValue(std::string_view cvarName) {
    ConVar *cvarToChange = this->getConVar_int(cvarName);
    if(!cvarToChange) return false;
    cvarToChange->hasServerValue.store(false, std::memory_order_release);
    cvarToChange->invalidateCache();
    return true;
}

//*****************************//
//	ConVarHandler ConCommands  //
//*****************************//

struct ConVarHandler::ConVarBuiltins {
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
        std::ranges::sort(
            matchingConVars, [](const char *s1, const char *s2) -> bool { return strcmp(s1, s2) < 0; },
            [](const ConVar *v) { return v->getName(); });
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
            thelog.append(ConVarHandler::flagsToString(match->getFlags()));
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
        std::ranges::sort(
            convars, [](const char *s1, const char *s2) -> bool { return strcmp(s1, s2) < 0; },
            [](const ConVar *v) { return v->getName(); });

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
                    tstring.append(ConVarHandler::flagsToString(var->getFlags()));
                    tstring.append(" )");
                }

                if(std::string_view{var->getHelpstring()}.length() > 0) {
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
    std::ranges::sort(
        convars, [](const char *s1, const char *s2) -> bool { return strcmp(s1, s2) < 0; },
        [](const ConVar *v) { return v->getName(); });

    std::string html = R"(<section class="variables">)";
    for(auto var : convars) {
        // only doing this because of some stupid spurious warning with LTO
#define STRIF_(FLAG__, flag__) var->isFlagSet(cv::FLAG__) ? "<span class=\"flag " #flag__ "\">" #FLAG__ "</span>" : ""
        const std::string flags = fmt::format("\n{}{}{}{}{}\n",              //
                                              STRIF_(CLIENT, client),        //
                                              STRIF_(SKINS, skins),          //
                                              STRIF_(SERVER, server),        //
                                              STRIF_(PROTECTED, protected),  //
                                              STRIF_(GAMEPLAY, gameplay));   //
#undef STRIF_

        html.append(fmt::format(R"(<div>
    <cv-header>
        <cv-name>{}</cv-name>
        <cv-default>{}</cv-default>
    </cv-header>
    <cv-description>{}</cv-description>
    <cv-flags>{}</cv-flags>
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

    io->write(MCENGINE_DATA_DIR "variables.htm", html_template, [](bool success) -> void {
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
