// Copyright (c) 2011, PG & 2025, WH & 2025, kiwec, All rights reserved.
#pragma once
#include "BaseEnvironment.h"
#include "Hashing.h"

#include <vector>
#include <string>
#include <string_view>
#include <memory>

using namespace std::string_view_literals;
using namespace std::string_literals;

class ConVar;
enum class CvarEditor : uint8_t;

class ConVarHandler {
    NOCOPY_NOMOVE(ConVarHandler)
   public:
    struct ConVarBuiltins;

    ConVarHandler();
    ~ConVarHandler() = default;

    [[nodiscard]] forceinline const std::vector<ConVar *> &getConVarArray() const { return this->vConVarArray; }
    [[nodiscard]] forceinline const Hash::unstable_stringmap<ConVar *> &getConVarMap() const {
        return this->vConVarMap;
    }
    [[nodiscard]] forceinline const ConVar *getConVar(std::string_view name) const {
        return static_cast<const ConVar *>(getConVar_int(name));
    }

    [[nodiscard]] forceinline size_t getNumConVars() const { return getConVarArray().size(); }

    [[nodiscard]] ConVar *getConVarByName(std::string_view name, bool warnIfNotFound = true) const;
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
    Hash::unstable_stringmap<ConVar *> vConVarMap;

    [[nodiscard]] ConVar *getConVar_int(std::string_view name) const;
};

extern ConVarHandler &cvars();
