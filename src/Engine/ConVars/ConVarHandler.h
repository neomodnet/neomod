// Copyright (c) 2011, PG & 2025, WH & 2025, kiwec, All rights reserved.
#pragma once
#include "BaseEnvironment.h"
#include "Hashing.h"

#include <vector>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <memory>

using namespace std::string_view_literals;
using namespace std::string_literals;

class ConVar;
enum class CvarEditor : uint8_t;
enum class CvarSetResult : uint8_t;

class ConVarHandler {
    NOCOPY_NOMOVE(ConVarHandler)
   public:
    struct ConVarBuiltins;

    ConVarHandler();
    ~ConVarHandler() = default;

    [[nodiscard]] forceinline const std::vector<ConVar *> &getConVarArray() const { return this->vConVarArray; }

    [[nodiscard]] forceinline size_t getNumConVars() const { return getConVarArray().size(); }

    // (nullptr if there is no such convar)
    [[nodiscard]] ConVar *getConVarByName(std::string_view name) const;
    [[nodiscard]] std::vector<ConVar *> getConVarByLetter(std::string_view letters) const;

    // whether every protected convar is at its default value, and the ones that aren't
    // (the former is cheap enough to ask every frame: convars keep count whenever their value changes)
    [[nodiscard]] forceinline bool areProtectedCvarsDefault() const { return this->iNumProtectedNonDefault == 0; }
    [[nodiscard]] std::vector<ConVar *> getNonDefaultProtectedCvars() const;

    // while enforced, protected convars read as their default value (unless the server sets them)
    void setProtectionEnforced(bool enforced);
    [[nodiscard]] forceinline bool isProtectionEnforced() const { return this->bProtectionEnforced; }

    // ConVar::clearValue() for every convar: forgets everything a skin/the server has set, which for the server
    // includes what it has protected/unprotected
    void clearLayer(CvarEditor editor);

    // a session is a time during which what the client sets some convars to isn't meant to last, like the mods of a
    // multiplayer room or of a replay: from beginSession() to endSession(), the client's writes to the given convars
    // go to a stand-in for its value (which starts out as a copy of it, and is what gets read instead). the value
    // itself stays what ConVar::getClientString() and with that configs get to see, and it is back in effect
    // afterwards, as one change for all of them. sessions don't nest: beginning one during another adds to it
    void beginSession(std::span<ConVar *const> convars);
    void endSession();
    [[nodiscard]] forceinline bool isInSession() const { return !this->vSessionConVars.empty(); }

    // makes the given values everything that a skin/the server has set, as one change: values it had set before and
    // that aren't among them go away, and whatever ends up with the value it already had is left alone, so callbacks
    // only run for convars whose value actually changed (once everything is in place). commands among them get run
    // after that, in order. returns what became of each entry
    // (not for the client: its values aren't a set that comes and goes as a whole)
    std::vector<CvarSetResult> setLayer(CvarEditor editor, std::span<const std::pair<ConVar *, std::string>> values);

    // the app's say in what happens to convars, which is where its anti-cheat rules go (both are optional)
    struct Policy {
        // asked before every write (for a command that means running it): false refuses it, and the writer gets
        // CvarSetResult::VETOED
        bool (*allowWrite)(const ConVar &cvar, CvarEditor editor){nullptr};

        // told after a convar's value has changed, whatever the reason: a write, a skin/server value going away,
        // the protection lock, a new default, ... (none of which but the write can be refused)
        void (*onValueChanged)(const ConVar &cvar){nullptr};
    };
    void setPolicy(const Policy &newPolicy) { this->policy = newPolicy; }

   private:
    friend class ConVar;

    Policy policy;
    bool bProtectionEnforced{false};
    int iNumProtectedNonDefault{0};
    std::vector<ConVar *> vConVarArray;
    std::vector<ConVar *> vSessionConVars;
    Hash::unstable_stringmap<ConVar *> vConVarMap;
};

extern ConVarHandler &cvars();
