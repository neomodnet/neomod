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

    // a score only gets submitted while every protected convar is at its default value
    // (cheap enough to ask every frame: convars keep count of that whenever their value changes)
    [[nodiscard]] std::vector<ConVar *> getNonSubmittableCvars() const;
    [[nodiscard]] bool areAllCvarsSubmittable() const;

    // while enforced, protected convars read as their default value (unless the server sets them)
    void setProtectionEnforced(bool enforced);
    [[nodiscard]] forceinline bool isProtectionEnforced() const { return this->bProtectionEnforced; }

    void resetServerCvars();
    void resetSkinCvars();

    bool removeServerValue(std::string_view cvarName);

    // extra check run during areAllCvarsSubmittable
    using CVSubmittableCriteriaFunc = bool (*)();
    void setCVSubmittableCheckFunc(CVSubmittableCriteriaFunc func);

   private:
    friend class ConVar;

    CVSubmittableCriteriaFunc areAllCvarsSubmittableExtraCheck{nullptr};
    bool bProtectionEnforced{false};
    int iNumNonSubmittable{0};
    std::vector<ConVar *> vConVarArray;
    Hash::unstable_stringmap<ConVar *> vConVarMap;

    [[nodiscard]] ConVar *getConVar_int(std::string_view name) const;
};

extern ConVarHandler &cvars();
