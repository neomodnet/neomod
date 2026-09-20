// Copyright (c) 2026, WH, All rights reserved.
#include "ConVarTest.h"

#include "TestMacros.h"
#include "ConVar.h"
#include "ConVarHandler.h"
#include "BaseEnvironment.h"
#include "Engine.h"
#include "SyncJthread.h"
#include "types.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mc::Tests {

namespace {
// keeps the test convars out of configs, replays and console suggestions
constexpr u8 TESTONLY = cv::HIDDEN | cv::NOLOAD | cv::NOSAVE;

ConVar t_float("cvtest_float", 1.0f, cv::CLIENT | TESTONLY);
ConVar t_int("cvtest_int", 5, cv::CLIENT | TESTONLY);
ConVar t_bool("cvtest_bool", true, cv::CLIENT | TESTONLY);
ConVar t_string("cvtest_string", "abc"sv, cv::CLIENT | cv::SKINS | cv::SERVER | TESTONLY);

ConVar t_layered("cvtest_layered", 1.0f, cv::CLIENT | cv::SKINS | cv::SERVER | TESTONLY);
ConVar t_serverOnly("cvtest_serveronly", 0, cv::SERVER | TESTONLY);

ConVar t_protected("cvtest_protected", 0.0f, cv::CLIENT | cv::SERVER | cv::PROTECTED | TESTONLY);
ConVar t_protectedSkin("cvtest_protected_skin", false, cv::CLIENT | cv::SKINS | cv::SERVER | cv::PROTECTED | TESTONLY);
ConVar t_gameplay("cvtest_gameplay", 0.0f, cv::CLIENT | cv::SERVER | cv::GAMEPLAY | TESTONLY);

ConVar t_callbacks("cvtest_callbacks", 1.0f, cv::CLIENT | TESTONLY);
ConVar t_ranged("cvtest_ranged", 5.0f, cv::CLIENT | cv::SKINS | cv::SERVER | TESTONLY, "", cv::Range{1., 10.});

float s_rangedCallbackValue{0.f};
ConVar t_rangedCallback("cvtest_ranged_callback", 5.0f, cv::CLIENT | TESTONLY, "", cv::Range{1., 10.},
                        [](float newValue) -> void { s_rangedCallbackValue = newValue; });

std::string s_cmdArgs;
int s_cmdCalls{0};
ConVar t_cmd("cvtest_cmd", cv::CLIENT | cv::SERVER | TESTONLY, [](std::string_view args) -> void {
    s_cmdCalls++;
    s_cmdArgs = args;
});

// stand-ins for the policy the app normally provides (see Osu's global* callbacks)
bool s_gateOpen{true};  // "not in a multiplayer match": gameplay convars may be changed
int s_gateCalls{0};
std::string s_gateLastName;
CvarEditor s_gateLastEditor{CvarEditor::CLIENT};
const ConVar *s_vetoed{nullptr};
int s_changes{0};
std::string s_lastChanged;
int s_protectedChanges{0};

// "in a multiplayer room": protected convars read as their default
void setLocked(bool locked) { cvars().setProtectionEnforced(locked); }

struct ChangeRecord {
    int calls{0};
    float oldValue{0.f};
    float newValue{0.f};
};
ChangeRecord s_layeredChange;
ChangeRecord s_protectedChange;
ChangeRecord s_cbChange;

int s_cbVoidCalls{0};
int s_cbFloatCalls{0};
float s_cbFloat{0.f};
double s_cbDouble{0.};
std::string s_cbString;
std::string s_cbOldString;
std::string s_cbNewString;

bool isNonDefaultProtected(const ConVar &cvar) {
    return std::ranges::contains(cvars().getNonDefaultProtectedCvars(), &cvar);
}
}  // namespace

ConVarTest::ConVarTest() {
    logRaw("ConVarTest created");

    cvars().setPolicy({.allowWrite = [](const ConVar &cvar, CvarEditor editor) -> bool {
                           s_gateCalls++;
                           s_gateLastName = cvar.getName();
                           s_gateLastEditor = editor;
                           if(&cvar == s_vetoed) return false;
                           return s_gateOpen || !cvar.isFlagSet(cv::GAMEPLAY);
                       },
                       .onValueChanged = [](const ConVar &cvar) -> void {
                           s_changes++;
                           s_lastChanged = cvar.getName();
                           if(cvar.isProtected()) s_protectedChanges++;
                       }});
}

ConVarTest::~ConVarTest() {
    for(auto *cvar : {&t_layered, &t_protected, &t_callbacks, &t_string, &t_cmd}) cvar->removeAllCallbacks();

    cvars().setPolicy({});
}

void ConVarTest::update() {
    if(m_done) return;
    m_done = true;

    this->testTypesAndParsing();
    this->testPermissions();
    this->testLayers();
    this->testProtectionLock();
    this->testPolicy();
    this->testCallbacks();
    this->testProtectedDefaults();
    this->testDefaults();
    this->testCommands();
    this->testSetLayer();
    this->testRange();
    this->testSession();
    this->testChange();
    this->testThreads();

    TEST_PRINT_RESULTS("ConVarTest");
    engine->shutdown();
}

void ConVarTest::testTypesAndParsing() {
    TEST_SECTION("types and parsing");
    using enum ConVar::CONVAR_TYPE;

    TEST_ASSERT(t_float.getType() == FLOAT, "float default makes a FLOAT convar");
    TEST_ASSERT(t_int.getType() == INT, "int default makes an INT convar");
    TEST_ASSERT(t_bool.getType() == BOOL, "bool default makes a BOOL convar");
    TEST_ASSERT(t_string.getType() == STRING, "string default makes a STRING convar");

    TEST_ASSERT_EQ(t_float.getFloat(), 1.0f, "float convar starts at its default");
    TEST_ASSERT_EQ(t_int.getInt(), 5, "int convar starts at its default");
    TEST_ASSERT(t_bool.getBool(), "bool convar starts at its default");
    TEST_ASSERT_EQ(t_string.getString(), "abc", "string convar starts at its default");
    TEST_ASSERT(t_float.isDefault() && t_int.isDefault() && t_bool.isDefault() && t_string.isDefault(),
                "untouched convars are default");

    t_float.setValue(2.5f);
    TEST_ASSERT_EQ(t_float.getFloat(), 2.5f, "numeric set, float view");
    TEST_ASSERT_EQ(t_float.getInt(), 2, "numeric set, int view truncates");
    TEST_ASSERT(t_float.getBool(), "numeric set, bool view");
    TEST_ASSERT_EQ(t_float.getString(), "2.5", "numeric set, string view");
    TEST_ASSERT(!t_float.isDefault(), "changed convar is not default");

    t_float.setValue("3.25");
    TEST_ASSERT_EQ(t_float.getFloat(), 3.25f, "numeric convar parses a string set");
    TEST_ASSERT_EQ(t_float.getString(), "3.25", "string set on a numeric convar, string view");

    t_int.setValue(7);
    TEST_ASSERT_EQ(t_int.getInt(), 7, "int set");
    TEST_ASSERT_EQ(t_int.getString(), "7", "int set, string view");

    t_bool.setValue(false);
    TEST_ASSERT(!t_bool.getBool(), "bool set");
    TEST_ASSERT_EQ(t_bool.getString(), "0", "bool set, string view is 0/1");
    t_bool.setValue("true");
    TEST_ASSERT(t_bool.getBool(), "bool convar accepts \"true\"");
    TEST_ASSERT_EQ(t_bool.getString(), "1", "\"true\" is normalized to 1");
    t_bool.setValue("FALSE");
    TEST_ASSERT(!t_bool.getBool(), "bool convar accepts \"FALSE\"");
    TEST_ASSERT_EQ(t_bool.getString(), "0", "\"FALSE\" is normalized to 0");
    t_bool.setValue(true);
    TEST_ASSERT(t_bool.isDefault(), "bool convar set back to its default value is default");

    t_string.setValue("hello world");
    TEST_ASSERT_EQ(t_string.getString(), "hello world", "string set");
    t_string.setValue("1.5");
    TEST_ASSERT_EQ(t_string.getFloat(), 1.5f, "string convar has a numeric view if its text parses");

    // not every text is something a convar can be set to: it stays what it is then, and whoever wants to know gets told
    using enum CvarSetResult;
    TEST_ASSERT(t_float.setValue("2.5") == APPLIED && t_int.setValue("-7") == APPLIED,
                "numbers are valid for numeric convars");
    TEST_ASSERT(t_float.setValue("garbage") == INVALID && t_int.setValue("") == INVALID, "anything else isn't");
    TEST_ASSERT(t_float.getFloat() == 2.5f && t_float.getString() == "2.5" && t_int.getInt() == -7,
                "invalid text leaves a numeric convar alone");
    TEST_ASSERT(
        t_bool.setValue("FALSE") == APPLIED && t_bool.setValue("true") == APPLIED && t_bool.setValue("1") == APPLIED,
        "bool convars also take true/false");
    TEST_ASSERT(t_bool.setValue("maybe") == INVALID && t_bool.getBool(), "...and nothing else that isn't a number");
    TEST_ASSERT(t_float.setValue("true") == INVALID, "other numeric convars don't take true/false");
    TEST_ASSERT(t_string.setValue("garbage") == APPLIED && t_string.setValue("") == APPLIED,
                "string convars take any text");
    t_float.setDefaultString("garbage");
    TEST_ASSERT_EQ(t_float.getDefaultString(), "1", "invalid text doesn't become a numeric convar's default either");

    t_float.setValue(1.0f);
    t_int.setValue(5);
    t_string.setValue("abc");
    TEST_ASSERT(t_float.isDefault() && t_int.isDefault() && t_string.isDefault(), "convars restored to default");
}

void ConVarTest::testPermissions() {
    TEST_SECTION("editor permissions");

    TEST_ASSERT(t_float.setValue(9.0f, true, CvarEditor::SKIN) == CvarSetResult::DENIED,
                "skin write to a convar without SKINS is denied");
    TEST_ASSERT_EQ(t_float.getFloat(), 1.0f, "skin can't set a convar without SKINS");
    TEST_ASSERT(t_float.setValue(9.0f, true, CvarEditor::SERVER) == CvarSetResult::DENIED,
                "server write to a convar without SERVER is denied");
    TEST_ASSERT_EQ(t_float.getFloat(), 1.0f, "server can't set a convar without SERVER");
    TEST_ASSERT(t_float.getMaster() == CvarEditor::CLIENT, "rejected writes don't change the master");

    TEST_ASSERT(t_serverOnly.setValue(1) == CvarSetResult::DENIED, "client write to a convar without CLIENT is denied");
    TEST_ASSERT_EQ(t_serverOnly.getInt(), 0, "client can't set a convar without CLIENT");
    TEST_ASSERT(t_serverOnly.setValue(1, true, CvarEditor::SERVER) == CvarSetResult::APPLIED,
                "server write to a SERVER convar is applied");
    TEST_ASSERT_EQ(t_serverOnly.getInt(), 1, "server can set a SERVER convar");
    TEST_ASSERT(t_serverOnly.getMaster() == CvarEditor::SERVER, "server is the master of a convar it set");

    cvars().clearLayer(CvarEditor::SERVER);
    TEST_ASSERT_EQ(t_serverOnly.getInt(), 0, "clearing the server layer removes server values");
}

void ConVarTest::testLayers() {
    TEST_SECTION("value layers");

    s_layeredChange = {};
    t_layered.setCallback([](float oldValue, float newValue) -> void {
        s_layeredChange.calls++;
        s_layeredChange.oldValue = oldValue;
        s_layeredChange.newValue = newValue;
    });

    TEST_ASSERT(t_layered.setValue(1.25f) == CvarSetResult::APPLIED, "plain client write is applied");
    TEST_ASSERT_EQ(t_layered.getFloat(), 1.25f, "client value is effective when nothing overrides it");
    TEST_ASSERT(t_layered.getMaster() == CvarEditor::CLIENT, "client is the master of a plain convar");

    t_layered.setValue(1.5f, true, CvarEditor::SKIN);
    TEST_ASSERT_EQ(t_layered.getFloat(), 1.5f, "skin value overrides the client value");
    TEST_ASSERT(t_layered.getMaster() == CvarEditor::SKIN, "skin is the master");
    TEST_ASSERT_EQ(s_layeredChange.oldValue, 1.25f, "skin override, callback old value");
    TEST_ASSERT_EQ(s_layeredChange.newValue, 1.5f, "skin override, callback new value");

    t_layered.setValue(1.75f, true, CvarEditor::SERVER);
    TEST_ASSERT_EQ(t_layered.getFloat(), 1.75f, "server value overrides the skin value");
    TEST_ASSERT_EQ(t_layered.getString(), "1.75", "server value overrides the skin value, string view");
    TEST_ASSERT(t_layered.getMaster() == CvarEditor::SERVER, "server is the master");

    // a client write below an override is kept for later, but nothing about the convar changes (yet),
    // so its callbacks have nothing to hear about until the override goes away
    int callsBefore = s_layeredChange.calls;
    TEST_ASSERT(t_layered.setValue(2.0f) == CvarSetResult::MASKED, "client write below an override is masked");
    TEST_ASSERT(t_layered.setValue(1.5f, true, CvarEditor::SKIN) == CvarSetResult::MASKED,
                "skin write below the server value is masked");
    TEST_ASSERT_EQ(t_layered.getFloat(), 1.75f, "masked client write doesn't change the effective value");
    TEST_ASSERT(t_layered.getMaster() == CvarEditor::SERVER, "masked client write doesn't change the master");
    TEST_ASSERT_EQ(s_layeredChange.calls, callsBefore, "masked client write doesn't run callbacks");

    // what goes into the client's config is the client's own value, not whatever is overriding it
    TEST_ASSERT_EQ(t_layered.getClientString(), "2", "the client's own value is still there below an override");
    TEST_ASSERT(!t_layered.isClientDefault(), "the client's own value is what counts as (non-)default for configs");

    callsBefore = s_layeredChange.calls;
    t_layered.clearValue(CvarEditor::SERVER);
    TEST_ASSERT_EQ(t_layered.getFloat(), 1.5f, "skin value is effective again without the server value");
    TEST_ASSERT(t_layered.getMaster() == CvarEditor::SKIN, "skin is the master again");
    TEST_ASSERT(s_layeredChange.calls == callsBefore + 1, "removing a server value runs callbacks");
    TEST_ASSERT(s_layeredChange.oldValue == 1.75f && s_layeredChange.newValue == 1.5f,
                "removing a server value, callback old/new values");

    callsBefore = s_layeredChange.calls;
    cvars().clearLayer(CvarEditor::SKIN);
    TEST_ASSERT_EQ(t_layered.getFloat(), 2.0f, "the masked client write is effective once the overrides are gone");
    TEST_ASSERT(t_layered.getMaster() == CvarEditor::CLIENT, "client is the master again");
    TEST_ASSERT(s_layeredChange.calls == callsBefore + 1, "removing a skin value runs callbacks");
    TEST_ASSERT(s_layeredChange.oldValue == 1.5f && s_layeredChange.newValue == 2.0f,
                "removing a skin value, callback old/new values");

    callsBefore = s_layeredChange.calls;
    cvars().clearLayer(CvarEditor::SKIN);
    cvars().clearLayer(CvarEditor::SERVER);
    t_layered.clearValue(CvarEditor::SKIN);
    t_layered.clearValue(CvarEditor::SERVER);
    TEST_ASSERT_EQ(s_layeredChange.calls, callsBefore, "clearing layers that hold no value doesn't run callbacks");

    // a skin value that happens to be the default still hides the client's value, which stays what it is
    t_layered.setValue(1.0f, true, CvarEditor::SKIN);
    TEST_ASSERT(t_layered.isDefault() && !t_layered.isClientDefault(), "skin value == default over a client value");
    t_layered.clearValue(CvarEditor::SKIN);

    // clearing the client's value is a regular write of the default
    callsBefore = s_layeredChange.calls;
    t_layered.clearValue(CvarEditor::CLIENT);
    TEST_ASSERT(t_layered.isDefault() && t_layered.isClientDefault(), "cleared client value is the default");
    TEST_ASSERT(s_layeredChange.calls == callsBefore + 1, "clearing the client value runs callbacks");
    TEST_ASSERT(s_layeredChange.oldValue == 2.0f && s_layeredChange.newValue == 1.0f,
                "clearing the client value, callback old/new values");

    t_string.setValue("client");
    t_string.setValue("skin", true, CvarEditor::SKIN);
    TEST_ASSERT_EQ(t_string.getString(), "skin", "string convar, skin value overrides the client value");
    t_string.setValue("server", true, CvarEditor::SERVER);
    TEST_ASSERT_EQ(t_string.getString(), "server", "string convar, server value overrides the skin value");
    TEST_ASSERT_EQ(t_string.getClientString(), "client", "string convar, the client's own value below overrides");
    cvars().clearLayer(CvarEditor::SERVER);
    TEST_ASSERT_EQ(t_string.getString(), "skin", "string convar, skin value is back");
    cvars().clearLayer(CvarEditor::SKIN);
    TEST_ASSERT_EQ(t_string.getString(), "client", "string convar, client value is back");

    t_layered.removeAllCallbacks();
    t_layered.setValue(1.0f);
    t_string.setValue("abc");
}

void ConVarTest::testProtectionLock() {
    TEST_SECTION("protection lock");

    s_protectedChange = {};
    t_protected.setCallback([](float oldValue, float newValue) -> void {
        s_protectedChange.calls++;
        s_protectedChange.oldValue = oldValue;
        s_protectedChange.newValue = newValue;
    });

    TEST_ASSERT(t_protected.isProtected(), "PROTECTED flag makes a convar protected");
    TEST_ASSERT(!t_layered.isProtected(), "a convar without the flag isn't protected");

    t_protected.setValue(45.0f);
    TEST_ASSERT_EQ(t_protected.getFloat(), 45.0f, "protected convar reads normally while unlocked");

    int callsBefore = s_protectedChange.calls;
    setLocked(true);
    TEST_ASSERT_EQ(t_protected.getFloat(), 0.0f, "locked protected convar reads as its default");
    TEST_ASSERT_EQ(t_protected.getString(), "0", "locked protected convar reads as its default, string view");
    TEST_ASSERT(t_protected.getMaster() == CvarEditor::SERVER, "a locked convar isn't the client's");
    TEST_ASSERT(s_protectedChange.calls == callsBefore + 1, "locking runs callbacks of convars it changes");
    TEST_ASSERT(s_protectedChange.oldValue == 45.0f && s_protectedChange.newValue == 0.0f,
                "locking, callback old/new values");

    // a client write under the lock is masked like one below a skin/server value: kept for later, and nobody
    // hears about it (it doesn't make the score unsubmittable either, nothing changed)
    callsBefore = s_protectedChange.calls;
    s_protectedChanges = 0;
    TEST_ASSERT(t_protected.setValue(50.0f) == CvarSetResult::MASKED, "client write under the lock is masked");
    TEST_ASSERT_EQ(t_protected.getFloat(), 0.0f, "client write doesn't get through the lock");
    TEST_ASSERT_EQ(t_protected.getClientString(), "50", "the client's own value is kept under the lock");
    TEST_ASSERT_EQ(s_protectedChange.calls, callsBefore, "client write under the lock doesn't run callbacks");
    TEST_ASSERT_EQ(s_protectedChanges, 0, "client write under the lock doesn't notify the app");

    t_protected.setValue(90.0f, true, CvarEditor::SERVER);
    TEST_ASSERT_EQ(t_protected.getFloat(), 90.0f, "server value beats the lock");
    t_protected.clearValue(CvarEditor::SERVER);
    TEST_ASSERT_EQ(t_protected.getFloat(), 0.0f, "locked again without the server value");

    callsBefore = s_protectedChange.calls;
    setLocked(false);
    TEST_ASSERT_EQ(t_protected.getFloat(), 50.0f, "the client value written under the lock applies once unlocked");
    TEST_ASSERT(t_protected.getMaster() == CvarEditor::CLIENT, "unlocked convar is the client's again");
    TEST_ASSERT(s_protectedChange.calls == callsBefore + 1, "unlocking runs callbacks of convars it changes");

    // a skin must not be able to get around the lock
    t_protectedSkin.setValue(true, true, CvarEditor::SKIN);
    TEST_ASSERT(t_protectedSkin.getBool(), "skin value on a protected convar is effective while unlocked");
    setLocked(true);
    TEST_ASSERT(t_protectedSkin.getMaster() == CvarEditor::SERVER, "lock beats the skin value, master");
    TEST_ASSERT(!t_protectedSkin.getBool(), "lock beats the skin value");
    setLocked(false);
    TEST_ASSERT(t_protectedSkin.getBool(), "skin value is back after unlocking");
    cvars().clearLayer(CvarEditor::SKIN);
    TEST_ASSERT(!t_protectedSkin.getBool(), "protected convar is default without the skin value");

    // per-convar server policy overrides the flag in both directions
    t_protected.setServerProtected(CvarProtection::UNPROTECTED);
    TEST_ASSERT(!t_protected.isProtected(), "server can unprotect a PROTECTED convar");
    setLocked(true);
    TEST_ASSERT_EQ(t_protected.getFloat(), 50.0f, "unprotected convar ignores the lock");
    setLocked(false);

    t_layered.setValue(1.25f);
    t_layered.setServerProtected(CvarProtection::PROTECTED);
    TEST_ASSERT(t_layered.isProtected(), "server can protect a convar without the flag");
    TEST_ASSERT_EQ(t_layered.getFloat(), 1.25f, "server-protected convar reads normally while unlocked");
    setLocked(true);
    TEST_ASSERT(t_layered.getFloat() == 1.0f, "server-protected convar reads as its default once locked");

    cvars().clearLayer(CvarEditor::SERVER);
    TEST_ASSERT(t_protected.isProtected() && !t_layered.isProtected(),
                "clearing the server layer restores flag-based protection");
    TEST_ASSERT_EQ(t_protected.getFloat(), 0.0f, "PROTECTED convar is locked again after the policy reset");
    TEST_ASSERT_EQ(t_layered.getFloat(), 1.25f, "unflagged convar is unlocked again after the policy reset");
    setLocked(false);

    // the app gets told when a protected convar changes value (to stop score submission)
    s_protectedChanges = 0;
    t_protected.setValue(10.0f);
    TEST_ASSERT_EQ(s_protectedChanges, 1, "changing a protected convar notifies the app");
    t_protected.setValue(10.0f);
    TEST_ASSERT_EQ(s_protectedChanges, 1, "setting a protected convar to the same value doesn't");
    t_protected.setValue(11.0f, false);
    TEST_ASSERT_EQ(s_protectedChanges, 2, "the notification doesn't depend on doCallback");
    t_layered.setValue(1.5f);
    TEST_ASSERT_EQ(s_protectedChanges, 2, "a change to an unprotected convar isn't one to a protected convar");
    t_layered.setServerProtected(CvarProtection::PROTECTED);
    t_layered.setValue(1.75f);
    TEST_ASSERT_EQ(s_protectedChanges, 3, "a convar the server protected counts as protected");
    cvars().clearLayer(CvarEditor::SERVER);

    t_protected.removeAllCallbacks();
    t_protected.setValue(0.0f);
    t_layered.setValue(1.0f);
}

void ConVarTest::testPolicy() {
    TEST_SECTION("app policy");

    s_gateCalls = 0;
    t_gameplay.setValue(1.0f);
    TEST_ASSERT_EQ(t_gameplay.getFloat(), 1.0f, "open gate lets a gameplay convar change");
    TEST_ASSERT_EQ(s_gateCalls, 1, "gate is asked once per write");
    TEST_ASSERT_EQ(s_gateLastName, "cvtest_gameplay", "gate gets the convar name");
    TEST_ASSERT(s_gateLastEditor == CvarEditor::CLIENT, "gate gets the editor (client)");

    t_gameplay.setValue(2.0f, true, CvarEditor::SERVER);
    TEST_ASSERT(s_gateLastEditor == CvarEditor::SERVER, "gate gets the editor (server)");
    cvars().clearLayer(CvarEditor::SERVER);

    s_cbFloatCalls = 0;
    t_gameplay.setCallback([](float /*newValue*/) -> void { s_cbFloatCalls++; });
    s_gateOpen = false;
    TEST_ASSERT(t_gameplay.setValue(3.0f) == CvarSetResult::VETOED, "closed gate vetoes the write");
    TEST_ASSERT_EQ(t_gameplay.getFloat(), 1.0f, "closed gate rejects the write");
    TEST_ASSERT_EQ(s_cbFloatCalls, 0, "rejected write doesn't run callbacks");
    s_gateOpen = true;

    // which convars it cares about is up to the app: it gets asked about every write that the flags allow
    s_gateCalls = 0;
    t_float.setValue(2.0f);
    TEST_ASSERT_EQ(s_gateCalls, 1, "the app is asked about writes to any convar");
    TEST_ASSERT(t_serverOnly.setValue(1) == CvarSetResult::DENIED && s_gateCalls == 1,
                "the app isn't asked about a write that isn't allowed to begin with");
    s_vetoed = &t_float;
    TEST_ASSERT(t_float.setValue(3.0f) == CvarSetResult::VETOED, "the app can veto writes to any convar");
    TEST_ASSERT_EQ(t_float.getFloat(), 2.0f, "vetoed write doesn't change the value");

    // running a command is a write too
    s_vetoed = &t_cmd;
    int callsBefore = s_cmdCalls;
    TEST_ASSERT(t_cmd.setValue("vetoed", true, CvarEditor::SERVER) == CvarSetResult::VETOED,
                "the app can veto running a command");
    TEST_ASSERT(s_gateLastEditor == CvarEditor::SERVER, "...and gets to know who wanted to run it");
    TEST_ASSERT_EQ(s_cmdCalls, callsBefore, "vetoed command doesn't run");
    s_vetoed = nullptr;

    // the app gets told about every change of a convar's value, whatever caused it (and only about those)
    s_changes = 0;
    t_layered.setValue(1.25f);
    TEST_ASSERT(s_changes == 1 && s_lastChanged == "cvtest_layered", "a write that changes the value is a change");
    t_layered.setValue(1.25f);
    t_layered.setValue("1.250");
    TEST_ASSERT_EQ(s_changes, 1, "a write of the same value isn't");
    t_layered.setValue(1.5f, false, CvarEditor::SKIN);
    TEST_ASSERT_EQ(s_changes, 2, "a skin value taking over is (doCallback doesn't matter)");
    t_layered.setValue(2.0f);
    TEST_ASSERT_EQ(s_changes, 2, "a masked write isn't");
    t_layered.clearValue(CvarEditor::SKIN);
    TEST_ASSERT_EQ(s_changes, 3, "a skin value going away is");
    t_layered.setValue(2.0f, true, CvarEditor::SKIN);
    t_layered.clearValue(CvarEditor::SKIN);
    TEST_ASSERT_EQ(s_changes, 3, "a skin value that is the client's value coming and going isn't");

    t_string.setValue("other");
    t_string.setValue("other");
    TEST_ASSERT(s_changes == 4 && s_lastChanged == "cvtest_string", "string convars change with their text");

    t_gameplay.setValue(5.0f);
    s_changes = 0;
    setLocked(true);
    TEST_ASSERT_EQ(s_changes, 0, "the lock doesn't change convars that aren't protected");
    t_protected.setValue(45.0f);
    TEST_ASSERT_EQ(s_changes, 0, "...or protected ones through a write below it");
    setLocked(false);
    TEST_ASSERT(s_changes == 1 && s_lastChanged == "cvtest_protected", "unlocking changes what it was hiding");
    t_protected.setDefaultDouble(45.0);
    TEST_ASSERT_EQ(s_changes, 1, "a new default below the value isn't a change");
    t_protected.setDefaultDouble(0.0);

    callsBefore = s_changes;
    t_cmd.setValue("not a value");
    TEST_ASSERT_EQ(s_changes, callsBefore, "running a command isn't a change of value");

    t_gameplay.removeAllCallbacks();
    t_gameplay.setValue(0.0f);
    t_protected.setValue(0.0f);
    t_layered.setValue(1.0f);
    t_string.setValue("abc");
    t_float.setValue(1.0f);
}

void ConVarTest::testCallbacks() {
    TEST_SECTION("callbacks");

    s_cbFloatCalls = 0;
    t_callbacks.setCallback([](float newValue) -> void {
        s_cbFloatCalls++;
        s_cbFloat = newValue;
    });
    t_callbacks.setValue(2.0f);
    TEST_ASSERT_EQ(s_cbFloat, 2.0f, "float callback gets the new value");
    t_callbacks.setValue(2.0f);
    TEST_ASSERT_EQ(s_cbFloatCalls, 2, "callbacks also run when the value stays the same");
    t_callbacks.setValue(3.0f, false);
    TEST_ASSERT_EQ(s_cbFloatCalls, 2, "doCallback=false doesn't run callbacks");
    TEST_ASSERT_EQ(t_callbacks.getFloat(), 3.0f, "doCallback=false still sets the value");

    // (a plain lambda taking a double is also callable with a float, so it would be stored as a float callback)
    t_callbacks.setCallback(ConVar::DoubleCB([](double newValue) -> void { s_cbDouble = newValue; }));
    t_callbacks.setValue(0.1);
    TEST_ASSERT_EQ(s_cbDouble, 0.1, "double callback gets the new value at full precision");
    TEST_ASSERT_EQ(s_cbFloatCalls, 2, "setting a callback replaces the previous one");

    t_callbacks.setCallback([](std::string_view newValue) -> void { s_cbString = newValue; });
    t_callbacks.setValue(5.0f);
    TEST_ASSERT_EQ(s_cbString, "5", "string callback gets the new value");

    s_cbVoidCalls = 0;
    t_callbacks.setCallback([]() -> void { s_cbVoidCalls++; });
    t_callbacks.setValue(6.0f);
    TEST_ASSERT_EQ(s_cbVoidCalls, 1, "void callback runs");

    // the change callback has its own slot next to the regular one
    s_cbChange = {};
    t_callbacks.setCallback([](float oldValue, float newValue) -> void {
        s_cbChange.calls++;
        s_cbChange.oldValue = oldValue;
        s_cbChange.newValue = newValue;
    });
    t_callbacks.setValue(7.0f);
    TEST_ASSERT_EQ(s_cbVoidCalls, 2, "regular callback still runs next to a change callback");
    TEST_ASSERT_EQ(s_cbChange.calls, 1, "change callback runs");
    TEST_ASSERT_EQ(s_cbChange.oldValue, 6.0f, "change callback old value");
    TEST_ASSERT_EQ(s_cbChange.newValue, 7.0f, "change callback new value");

    t_callbacks.removeCallback();
    t_callbacks.setValue(8.0f);
    TEST_ASSERT_EQ(s_cbVoidCalls, 2, "removeCallback removes the regular callback");
    TEST_ASSERT_EQ(s_cbChange.calls, 2, "removeCallback keeps the change callback");
    t_callbacks.removeChangeCallback();
    t_callbacks.setValue(9.0f);
    TEST_ASSERT_EQ(s_cbChange.calls, 2, "removeChangeCallback removes the change callback");

    // clamping from inside the convar's own callback
    t_callbacks.setCallback([](float /*oldValue*/, float newValue) -> void {
        s_cbChange.calls++;
        t_callbacks.setValue(std::clamp(newValue, 0.0f, 10.0f), false);
    });
    s_cbChange.calls = 0;
    t_callbacks.setValue(50.0f);
    TEST_ASSERT_EQ(t_callbacks.getFloat(), 10.0f, "callback can clamp its own convar");
    TEST_ASSERT_EQ(s_cbChange.calls, 1, "clamping with doCallback=false doesn't recurse");

    t_string.setCallback([](std::string_view oldValue, std::string_view newValue) -> void {
        s_cbOldString = oldValue;
        s_cbNewString = newValue;
    });
    t_string.setValue("next");
    TEST_ASSERT_EQ(s_cbOldString, "abc", "string change callback old value");
    TEST_ASSERT_EQ(s_cbNewString, "next", "string change callback new value");

    t_callbacks.removeAllCallbacks();
    t_string.removeAllCallbacks();
    t_callbacks.setValue(1.0f);
    t_string.setValue("abc");
}

void ConVarTest::testProtectedDefaults() {
    TEST_SECTION("protected convars at their default");

    TEST_ASSERT(!isNonDefaultProtected(t_protected), "a protected convar at its default isn't listed");
    TEST_ASSERT(cvars().areProtectedCvarsDefault(), "every protected convar is at its default to begin with");

    t_protected.setValue(45.0f);
    TEST_ASSERT(isNonDefaultProtected(t_protected), "a changed protected convar is listed");
    TEST_ASSERT(!cvars().areProtectedCvarsDefault(), "one changed protected convar is enough");

    setLocked(true);
    TEST_ASSERT(!isNonDefaultProtected(t_protected),
                "a locked protected convar reads as its default, so it isn't listed");
    setLocked(false);

    t_layered.setValue(1.5f);
    TEST_ASSERT(!isNonDefaultProtected(t_layered), "a changed unprotected convar isn't listed");
    t_layered.setServerProtected(CvarProtection::PROTECTED);
    TEST_ASSERT(isNonDefaultProtected(t_layered), "a changed convar that the server protected is listed");
    cvars().clearLayer(CvarEditor::SERVER);

    t_protected.setValue(0.0f);
    t_layered.setValue(1.0f);
    TEST_ASSERT(cvars().areProtectedCvarsDefault(), "every protected convar is at its default again");

    // anything that can change what a protected convar reads as has to keep the answer up to date
    t_protectedSkin.setValue(true, true, CvarEditor::SKIN);
    TEST_ASSERT(!cvars().areProtectedCvarsDefault(), "a skin value on a protected convar counts");
    setLocked(true);
    TEST_ASSERT(cvars().areProtectedCvarsDefault(), "...unless the lock hides it");
    setLocked(false);
    t_protectedSkin.clearValue(CvarEditor::SKIN);
    TEST_ASSERT(cvars().areProtectedCvarsDefault(), "...and doesn't without the skin value");

    t_protected.setValue(45.0f, true, CvarEditor::SERVER);
    TEST_ASSERT(!cvars().areProtectedCvarsDefault(), "a server value on a protected convar counts");
    t_protected.setDefaultDouble(45.0);
    TEST_ASSERT(cvars().areProtectedCvarsDefault(), "a changed default counts");
    t_protected.setDefaultDouble(0.0);
    TEST_ASSERT(!cvars().areProtectedCvarsDefault(), "a changed default counts, back");
    t_protected.setServerProtected(CvarProtection::UNPROTECTED);
    TEST_ASSERT(cvars().areProtectedCvarsDefault(), "a changed convar that the server unprotected doesn't count");
    cvars().clearLayer(CvarEditor::SERVER);
    TEST_ASSERT(cvars().areProtectedCvarsDefault() && cvars().getNonDefaultProtectedCvars().empty(),
                "nothing left without anything from the server");
}

void ConVarTest::testDefaults() {
    TEST_SECTION("runtime defaults");

    t_int.setDefaultDouble(8.0);
    TEST_ASSERT_EQ(t_int.getDefaultDouble(), 8.0, "setDefaultDouble changes the default");
    TEST_ASSERT_EQ(t_int.getDefaultString(), "8", "setDefaultDouble changes the default string");
    TEST_ASSERT_EQ(t_int.getInt(), 5, "changing the default doesn't change the value");
    TEST_ASSERT(!t_int.isDefault(), "the old value isn't default anymore");
    t_int.setValue(8);
    TEST_ASSERT(t_int.isDefault(), "the new default value is default");
    t_int.setDefaultDouble(5.0);
    t_int.setValue(5);

    t_string.setDefaultString("xyz");
    TEST_ASSERT_EQ(t_string.getDefaultString(), "xyz", "setDefaultString changes the default");
    TEST_ASSERT_EQ(t_string.getString(), "abc", "changing the default string doesn't change the value");
    TEST_ASSERT(!t_string.isDefault(), "the old string isn't default anymore");
    t_string.setDefaultString("abc");

    t_protected.setValue(45.0f);
    setLocked(true);
    t_protected.setDefaultDouble(3.0);
    TEST_ASSERT_EQ(t_protected.getFloat(), 3.0f, "locked protected convar follows a changed default");
    t_protected.setDefaultDouble(0.0);
    setLocked(false);
    t_protected.setValue(0.0f);
}

void ConVarTest::testCommands() {
    TEST_SECTION("commands");

    TEST_ASSERT(!t_cmd.canHaveValue(), "a convar made from only a callback is a command");
    TEST_ASSERT(t_cmd.isFlagSet(cv::NOSAVE), "commands are never saved");
    TEST_ASSERT(t_float.canHaveValue(), "a convar made from a default value is not a command");

    s_cmdCalls = 0;
    t_cmd.exec();
    TEST_ASSERT_EQ(s_cmdCalls, 0, "exec only runs void callbacks");
    t_cmd.execArgs("one two");
    TEST_ASSERT_EQ(s_cmdCalls, 1, "execArgs runs a string callback");
    TEST_ASSERT_EQ(s_cmdArgs, "one two", "execArgs passes the arguments");
    t_cmd.setValue("three");
    TEST_ASSERT_EQ(s_cmdCalls, 2, "setting a command runs it");
    TEST_ASSERT_EQ(s_cmdArgs, "three", "setting a command passes the value as arguments");

    // a command has no value the server could override: the client can still run it after the server did
    t_cmd.setValue("from server", true, CvarEditor::SERVER);
    TEST_ASSERT_EQ(s_cmdArgs, "from server", "server can run a SERVER command");
    t_cmd.setValue("from client");
    TEST_ASSERT_EQ(s_cmdArgs, "from client", "client can run a command after the server did");
    TEST_ASSERT(t_cmd.getMaster() == CvarEditor::CLIENT, "commands don't get a master");
    t_cmd.setValue("skin", true, CvarEditor::SKIN);
    TEST_ASSERT_EQ(s_cmdArgs, "from client", "skin can't run a command without SKINS");
    const int callsBefore = s_cmdCalls;
    t_cmd.clearValue(CvarEditor::CLIENT);
    TEST_ASSERT_EQ(s_cmdCalls, callsBefore, "clearing a command's (nonexistent) value doesn't run it");
}

void ConVarTest::testSetLayer() {
    TEST_SECTION("replacing a layer");
    using Values = std::vector<std::pair<ConVar *, std::string>>;
    using enum CvarSetResult;

    // (what the string convar read as when the callback ran: everything has to be in place by then)
    static std::string s_stringSeenByCallback;
    s_layeredChange = {};
    t_layered.setCallback([](float oldValue, float newValue) -> void {
        s_layeredChange.calls++;
        s_layeredChange.oldValue = oldValue;
        s_layeredChange.newValue = newValue;
        s_stringSeenByCallback = t_string.getString();
    });
    s_cbOldString.clear();
    s_cbNewString.clear();
    static int s_stringCalls{0};
    s_stringCalls = 0;
    t_string.setCallback([](std::string_view oldValue, std::string_view newValue) -> void {
        s_stringCalls++;
        s_cbOldString = oldValue;
        s_cbNewString = newValue;
    });

    t_layered.setValue(1.25f);
    t_string.setValue("client");
    int layeredCalls = s_layeredChange.calls;
    int stringCalls = s_stringCalls;

    const Values first{{&t_layered, "1.5"}, {&t_string, "skin"}, {&t_float, "9"}};
    auto results = cvars().setLayer(CvarEditor::SKIN, first);
    TEST_ASSERT(results.size() == 3 && results[0] == APPLIED && results[1] == APPLIED, "values that get set");
    TEST_ASSERT(results[2] == DENIED, "a convar without SKINS is denied");
    TEST_ASSERT(t_layered.getFloat() == 1.5f && t_string.getString() == "skin" && t_float.getFloat() == 1.0f,
                "the values are in place");
    TEST_ASSERT(t_layered.getMaster() == CvarEditor::SKIN, "...as the skin's");
    TEST_ASSERT(s_layeredChange.calls == layeredCalls + 1 && s_stringCalls == stringCalls + 1,
                "callbacks of changed convars run once");
    TEST_ASSERT(s_layeredChange.oldValue == 1.25f && s_layeredChange.newValue == 1.5f, "callback old/new values");
    TEST_ASSERT_EQ(s_stringSeenByCallback, "skin", "callbacks run once everything is in place");

    // the same values again (a skin reload): nothing changes, so nobody hears about anything
    layeredCalls = s_layeredChange.calls;
    stringCalls = s_stringCalls;
    s_changes = 0;
    results = cvars().setLayer(CvarEditor::SKIN, first);
    TEST_ASSERT(results[0] == APPLIED && results[1] == APPLIED, "the same values again, results");
    TEST_ASSERT(s_layeredChange.calls == layeredCalls && s_stringCalls == stringCalls,
                "the same values again don't run callbacks");
    TEST_ASSERT_EQ(s_changes, 0, "the same values again are no change for the app");

    // other values (another skin): what is in both goes from one skin's value to the other's without a detour over
    // the client's, what isn't set anymore goes back to whoever is next
    const Values second{{&t_layered, "1.75"}, {&t_protectedSkin, "1"}};
    results = cvars().setLayer(CvarEditor::SKIN, second);
    TEST_ASSERT(t_layered.getFloat() == 1.75f && t_protectedSkin.getBool(), "the other values are in place");
    TEST_ASSERT_EQ(t_string.getString(), "client", "a value that isn't set anymore is gone");
    TEST_ASSERT(s_layeredChange.calls == layeredCalls + 1 && s_layeredChange.oldValue == 1.5f &&
                    s_layeredChange.newValue == 1.75f,
                "a value that is replaced changes once, from the old one to the new one");
    TEST_ASSERT(s_stringCalls == stringCalls + 1 && s_cbOldString == "skin" && s_cbNewString == "client",
                "a value that isn't set anymore changes once");
    TEST_ASSERT_EQ(s_changes, 3, "the app hears about each of those once");

    // a vetoed value doesn't get set, and what was there stays
    s_vetoed = &t_layered;
    results = cvars().setLayer(CvarEditor::SKIN, Values{{&t_layered, "2.5"}});
    TEST_ASSERT(results[0] == VETOED, "the app can veto a value");
    TEST_ASSERT_EQ(t_layered.getFloat(), 1.75f, "a vetoed value leaves the one that was there before alone");
    TEST_ASSERT(!t_protectedSkin.getBool(), "...unlike leaving a value out");
    s_vetoed = nullptr;

    // below a server value
    t_layered.setValue(3.0f, true, CvarEditor::SERVER);
    layeredCalls = s_layeredChange.calls;
    results = cvars().setLayer(CvarEditor::SKIN, Values{{&t_layered, "2.0"}, {&t_layered, "2.25"}});
    TEST_ASSERT(results[0] == MASKED && results[1] == MASKED, "values below a server value are masked");
    TEST_ASSERT(t_layered.getFloat() == 3.0f && s_layeredChange.calls == layeredCalls, "...and change nothing");
    t_layered.clearValue(CvarEditor::SERVER);
    TEST_ASSERT_EQ(t_layered.getFloat(), 2.25f, "the last one wins if a convar is in there more than once");

    // commands get run once the values are in place
    static float s_layeredSeenByCommand{0.f};
    t_cmd.setCallback([](std::string_view args) -> void {
        s_cmdCalls++;
        s_cmdArgs = args;
        s_layeredSeenByCommand = t_layered.getFloat();
    });
    int cmdCalls = s_cmdCalls;
    results = cvars().setLayer(CvarEditor::SERVER, Values{{&t_cmd, "batched"}, {&t_layered, "4"}});
    TEST_ASSERT(results[0] == APPLIED && results[1] == APPLIED, "a command among the values, results");
    TEST_ASSERT(s_cmdCalls == cmdCalls + 1 && s_cmdArgs == "batched", "a command among the values gets run");
    TEST_ASSERT_EQ(s_layeredSeenByCommand, 4.0f, "...after the values are in place");
    TEST_ASSERT_EQ(t_layered.getFloat(), 4.0f, "server values are in place");
    cvars().setLayer(CvarEditor::SERVER, {});

    // no values at all
    layeredCalls = s_layeredChange.calls;
    results = cvars().setLayer(CvarEditor::SKIN, {});
    TEST_ASSERT(results.empty() && t_layered.getFloat() == 1.25f, "an empty set removes everything");
    TEST_ASSERT(s_layeredChange.calls == layeredCalls + 1, "...running callbacks");
    TEST_ASSERT(t_layered.getMaster() == CvarEditor::CLIENT, "...and the client decides again");

    // the values are text from outside
    results = cvars().setLayer(CvarEditor::SKIN, Values{{&t_layered, "garbage"}, {&t_string, "fine"}});
    TEST_ASSERT(results[0] == INVALID && results[1] == APPLIED, "invalid text doesn't get set, the rest does");
    TEST_ASSERT(t_layered.getFloat() == 1.25f && t_string.getString() == "fine", "...as the values show");
    cvars().setLayer(CvarEditor::SKIN, {});

    // (debug builds assert)
    if constexpr(!Env::cfg(BUILD::DEBUG)) {
        results = cvars().setLayer(CvarEditor::CLIENT, Values{{&t_layered, "5"}});
        TEST_ASSERT(results[0] == DENIED && t_layered.getFloat() == 1.25f, "the client's values can't be replaced");
    }

    t_cmd.setCallback([](std::string_view args) -> void {
        s_cmdCalls++;
        s_cmdArgs = args;
    });
    t_layered.removeAllCallbacks();
    t_string.removeAllCallbacks();
    t_layered.setValue(1.0f);
    t_string.setValue("abc");
}

void ConVarTest::testRange() {
    TEST_SECTION("ranges");

    TEST_ASSERT(t_ranged.getRange().min == 1. && t_ranged.getRange().max == 10., "a convar knows its range");
    TEST_ASSERT(t_float.getRange().min < -1e300 && t_float.getRange().max > 1e300, "...which is everything by default");

    s_cbFloat = 0.f;
    t_ranged.setCallback([](float newValue) -> void { s_cbFloat = newValue; });

    t_ranged.setValue(7.5f);
    TEST_ASSERT_EQ(t_ranged.getFloat(), 7.5f, "a value inside of the range is what it is");
    TEST_ASSERT(t_ranged.setValue(50.0f) == CvarSetResult::APPLIED, "a value outside of the range still gets set");
    TEST_ASSERT_EQ(t_ranged.getFloat(), 10.0f, "...as the closest one inside of it");
    TEST_ASSERT_EQ(t_ranged.getString(), "10", "...string view");
    TEST_ASSERT_EQ(s_cbFloat, 10.0f, "callbacks get the value that was set, not the one that was asked for");
    t_ranged.setValue(-3);
    TEST_ASSERT_EQ(t_ranged.getFloat(), 1.0f, "below the range");

    TEST_ASSERT(t_ranged.setValue("999") == CvarSetResult::APPLIED, "text outside of the range is valid text");
    TEST_ASSERT(t_ranged.getFloat() == 10.0f && t_ranged.getString() == "10", "text outside of the range");
    t_ranged.setValue("7.50");
    TEST_ASSERT(t_ranged.getFloat() == 7.5f && t_ranged.getString() == "7.50",
                "text inside of the range stays as typed");

    // nobody gets around it
    t_ranged.setValue(99.0f, true, CvarEditor::SKIN);
    TEST_ASSERT_EQ(t_ranged.getFloat(), 10.0f, "a skin's value ends up inside of the range");
    TEST_ASSERT_EQ(t_ranged.getClientString(), "7.50", "...without touching the client's");
    t_ranged.setValue(-99.0f, true, CvarEditor::SERVER);
    TEST_ASSERT_EQ(t_ranged.getFloat(), 1.0f, "a server's value ends up inside of the range");
    cvars().clearLayer(CvarEditor::SERVER);
    cvars().setLayer(CvarEditor::SKIN, std::vector<std::pair<ConVar *, std::string>>{{&t_ranged, "1e9"}});
    TEST_ASSERT(t_ranged.getFloat() == 10.0f && t_ranged.getString() == "10", "values set as a layer do too");
    cvars().clearLayer(CvarEditor::SKIN);

    t_ranged.setDefaultDouble(50.0);
    TEST_ASSERT_EQ(t_ranged.getDefaultDouble(), 10.0, "a new default ends up inside of the range");
    t_ranged.setDefaultDouble(5.0);

    t_rangedCallback.setValue(50.0f);
    TEST_ASSERT(t_rangedCallback.getFloat() == 10.0f && s_rangedCallbackValue == 10.0f,
                "a convar can be made with a range and a callback");

    t_ranged.removeAllCallbacks();
    t_ranged.setValue(5.0f);
    t_rangedCallback.setValue(5.0f);
}

void ConVarTest::testSession() {
    TEST_SECTION("sessions");

    s_layeredChange = {};
    t_layered.setCallback([](float oldValue, float newValue) -> void {
        s_layeredChange.calls++;
        s_layeredChange.oldValue = oldValue;
        s_layeredChange.newValue = newValue;
    });
    t_layered.setValue(1.25f);
    t_string.setValue("own");
    t_int.setValue(7);

    // what the client sets during a session isn't meant to last (a multiplayer room's mods, a replay's...)
    int callsBefore = s_layeredChange.calls;
    s_changes = 0;
    TEST_ASSERT(!cvars().isInSession(), "no session to begin with");
    cvars().beginSession(std::array{&t_layered, &t_string, &t_cmd});
    TEST_ASSERT(cvars().isInSession(), "a session has begun");
    TEST_ASSERT(t_layered.getFloat() == 1.25f && t_string.getString() == "own", "beginning one changes no values");
    TEST_ASSERT(s_layeredChange.calls == callsBefore && s_changes == 0, "...so nobody hears about anything");

    TEST_ASSERT(t_layered.setValue(2.0f) == CvarSetResult::APPLIED, "a write during a session is a regular write");
    t_string.setValue("session");
    TEST_ASSERT(t_layered.getFloat() == 2.0f && t_string.getString() == "session", "...that is in effect");
    TEST_ASSERT(s_layeredChange.calls == callsBefore + 1 && s_layeredChange.oldValue == 1.25f, "...with callbacks");
    TEST_ASSERT(t_layered.getMaster() == CvarEditor::CLIENT, "...and it is still the client's convar");
    TEST_ASSERT(t_layered.getClientString() == "1.25" && t_string.getClientString() == "own",
                "the client's own value (what configs save) isn't touched");
    t_layered.setValue(1.0f);
    TEST_ASSERT(t_layered.isDefault() && !t_layered.isClientDefault(), "default during a session, not by itself");

    t_int.setValue(9);
    TEST_ASSERT_EQ(t_int.getClientString(), "9", "convars that aren't part of the session are written as always");

    // everybody else still comes first
    t_layered.setValue(1.5f, true, CvarEditor::SKIN);
    TEST_ASSERT(t_layered.setValue(3.0f) == CvarSetResult::MASKED, "a skin value masks writes during a session too");
    TEST_ASSERT_EQ(t_layered.getFloat(), 1.5f, "a skin value beats what is set during a session");
    t_layered.clearValue(CvarEditor::SKIN);
    TEST_ASSERT_EQ(t_layered.getFloat(), 3.0f, "...which is what is below it");
    t_layered.clearValue(CvarEditor::CLIENT);
    TEST_ASSERT(t_layered.getFloat() == 1.0f && t_layered.getClientString() == "1.25",
                "clearing the client's value during a session is a write like any other");

    // adding to a session that is going on keeps what it has so far
    t_layered.setValue(2.5f);
    cvars().beginSession(std::array{&t_layered, &t_int});
    TEST_ASSERT(t_layered.getFloat() == 2.5f && t_layered.getClientString() == "1.25", "adding to a session, again");
    t_int.setValue(11);
    TEST_ASSERT(t_int.getInt() == 11 && t_int.getClientString() == "9", "adding to a session, new");

    // the end: everything the client had set before is back, as one change
    static std::string s_stringSeenByCallback;
    t_layered.setCallback([](float oldValue, float newValue) -> void {
        s_layeredChange.calls++;
        s_layeredChange.oldValue = oldValue;
        s_layeredChange.newValue = newValue;
        s_stringSeenByCallback = t_string.getString();
    });
    callsBefore = s_layeredChange.calls;
    s_changes = 0;
    cvars().endSession();
    TEST_ASSERT(!cvars().isInSession(), "the session is over");
    TEST_ASSERT(t_layered.getFloat() == 1.25f && t_string.getString() == "own" && t_int.getInt() == 9,
                "the client's own values are back");
    TEST_ASSERT(s_layeredChange.calls == callsBefore + 1 && s_layeredChange.oldValue == 2.5f &&
                    s_layeredChange.newValue == 1.25f,
                "...with callbacks for what changed");
    TEST_ASSERT_EQ(s_stringSeenByCallback, "own", "...once all of them are back");
    TEST_ASSERT_EQ(s_changes, 3, "...and the app hears about each of them");
    t_layered.setValue(1.75f);
    TEST_ASSERT_EQ(t_layered.getClientString(), "1.75", "writes are the client's own again");

    callsBefore = s_layeredChange.calls;
    cvars().endSession();
    TEST_ASSERT_EQ(s_layeredChange.calls, callsBefore, "ending a session that isn't going on does nothing");

    // a session in which nothing got set
    cvars().beginSession(std::array{&t_layered});
    callsBefore = s_layeredChange.calls;
    cvars().endSession();
    TEST_ASSERT(t_layered.getFloat() == 1.75f && s_layeredChange.calls == callsBefore, "a session without writes");

    t_layered.removeAllCallbacks();
    t_layered.setValue(1.0f);
    t_string.setValue("abc");
    t_int.setValue(5);
}

void ConVarTest::testChange() {
    TEST_SECTION("changes");
    // (what the string convar read as when the callback ran: everything has to be in place by then)
    static std::string s_stringSeenByCallback;
    s_layeredChange = {};
    t_layered.setCallback([](float oldValue, float newValue) -> void {
        s_layeredChange.calls++;
        s_layeredChange.oldValue = oldValue;
        s_layeredChange.newValue = newValue;
        s_stringSeenByCallback = t_string.getString();
    });
    t_layered.setValue(1.25f);
    t_string.setValue("before");

    // several things as one change: values are what they get set to right away, but nobody hears about it until the end
    int callsBefore = s_layeredChange.calls;
    s_changes = 0;
    cvars().change([&] {
        TEST_ASSERT(t_layered.setValue(1.5f) == CvarSetResult::APPLIED, "a write during a change is a regular write");
        TEST_ASSERT_EQ(t_layered.getFloat(), 1.5f, "...that is in effect right away");
        TEST_ASSERT(s_layeredChange.calls == callsBefore && s_changes == 0, "...without anyone hearing about it yet");
        t_layered.setValue(1.75f);
        t_string.setValue("after");
    });
    TEST_ASSERT_EQ(s_layeredChange.calls, callsBefore + 1, "callbacks run once when the change ends");
    TEST_ASSERT(s_layeredChange.oldValue == 1.25f && s_layeredChange.newValue == 1.75f,
                "...with the value from before the change and the one after it");
    TEST_ASSERT_EQ(s_stringSeenByCallback, "after", "...once everything is in place");
    TEST_ASSERT_EQ(s_changes, 2, "the app hears about each convar that changed once");

    // whatever ends up as it was isn't a change (outside of one, a write of the same value does run callbacks)
    callsBefore = s_layeredChange.calls;
    s_changes = 0;
    cvars().change([&] {
        t_layered.setValue(3.0f);
        t_layered.setValue(1.75f);
        t_layered.setValue(2.0f, true, CvarEditor::SKIN);
        t_layered.clearValue(CvarEditor::SKIN);
        t_string.setValue("after");
    });
    TEST_ASSERT(s_layeredChange.calls == callsBefore && s_changes == 0, "a value that comes back isn't a change");
    cvars().change([&] {
        cvars().setLayer(CvarEditor::SKIN, std::vector<std::pair<ConVar *, std::string>>{{&t_layered, "2"}});
        TEST_ASSERT_EQ(t_layered.getFloat(), 2.0f, "changes to many convars take part in the change that is going on");
        cvars().clearLayer(CvarEditor::SKIN);
    });
    TEST_ASSERT(s_layeredChange.calls == callsBefore && s_changes == 0, "a layer that comes and goes isn't a change");

    // doCallback
    cvars().change([&] { t_layered.setValue(2.0f, false); });
    TEST_ASSERT(s_layeredChange.calls == callsBefore && s_changes == 1,
                "doCallback=false during a change: the app hears about it, callbacks don't");
    cvars().change([&] {
        t_layered.setValue(2.25f, false);
        t_layered.setValue(2.5f);
    });
    TEST_ASSERT(s_layeredChange.calls == callsBefore + 1 && s_layeredChange.oldValue == 2.0f &&
                    s_layeredChange.newValue == 2.5f,
                "callbacks run if any of what happened during the change wanted them to");

    // changes inside of changes
    callsBefore = s_layeredChange.calls;
    cvars().change([&] {
        cvars().change([&] { t_layered.setValue(3.0f); });
        TEST_ASSERT_EQ(s_layeredChange.calls, callsBefore, "a change inside of another one ends with that one");
        t_layered.setValue(3.5f);
    });
    TEST_ASSERT(s_layeredChange.calls == callsBefore + 1 && s_layeredChange.oldValue == 2.5f &&
                    s_layeredChange.newValue == 3.5f,
                "...as part of it");

    // writes get refused and masked as always
    cvars().change([&] {
        TEST_ASSERT(t_float.setValue(9.0f, true, CvarEditor::SKIN) == CvarSetResult::DENIED, "denied during a change");
        s_vetoed = &t_layered;
        TEST_ASSERT(t_layered.setValue(9.0f) == CvarSetResult::VETOED, "vetoed during a change");
        s_vetoed = nullptr;
        t_layered.setValue(1.5f, true, CvarEditor::SERVER);
        TEST_ASSERT(t_layered.setValue(3.75f) == CvarSetResult::MASKED, "masked during a change");
        TEST_ASSERT_EQ(t_layered.getFloat(), 1.5f, "...by what got set earlier in it");
        t_layered.clearValue(CvarEditor::SERVER);
    });
    TEST_ASSERT(t_layered.getFloat() == 3.75f && s_layeredChange.oldValue == 3.5f && s_layeredChange.newValue == 3.75f,
                "a write that isn't masked anymore by the end of the change is what changed the value");

    // commands aren't values that change: they just run
    const int cmdCalls = s_cmdCalls;
    cvars().change([&] {
        t_cmd.setValue("during a change");
        TEST_ASSERT(s_cmdCalls == cmdCalls + 1 && s_cmdArgs == "during a change", "a command runs right away");
    });
    TEST_ASSERT_EQ(s_cmdCalls, cmdCalls + 1, "...and not again when the change ends");

    // the change is over by the time anyone hears about it: callbacks are free to set convars, or to change several
    t_layered.setCallback([](float /*oldValue*/, float newValue) -> void {
        cvars().change([&] {
            t_int.setValue(static_cast<int>(newValue));
            t_string.setValue("from a callback");
        });
    });
    static std::string s_stringFromCallback;
    t_string.setCallback([](std::string_view newValue) -> void { s_stringFromCallback = newValue; });
    cvars().change([&] { t_layered.setValue(7.0f); });
    TEST_ASSERT(t_int.getInt() == 7 && s_stringFromCallback == "from a callback",
                "a callback that runs when a change ends can begin one of its own");

    // what this is for: things that only make sense together. leaving a multiplayer room takes the protection lock
    // away and ends the session its mods were in. one after the other, those are two changes: what got set below the
    // lock during the session is the value in between
    s_protectedChange = {};
    t_protected.setCallback([](float oldValue, float newValue) -> void {
        s_protectedChange.calls++;
        s_protectedChange.oldValue = oldValue;
        s_protectedChange.newValue = newValue;
    });
    const auto enterRoom = []() -> void {
        setLocked(true);
        cvars().beginSession(std::array{&t_protected});
        t_protected.setValue(45.0f);
    };
    enterRoom();
    TEST_ASSERT(s_protectedChange.calls == 0 && t_protected.getFloat() == 0.0f, "(masked by the lock)");
    setLocked(false);
    cvars().endSession();
    TEST_ASSERT(s_protectedChange.calls == 2 && s_protectedChange.oldValue == 45.0f,
                "two changes, one after the other");

    // as one change, nothing happened to a convar that is what it was before
    enterRoom();
    s_protectedChange = {};
    s_changes = 0;
    cvars().change([&] {
        setLocked(false);
        TEST_ASSERT_EQ(t_protected.getFloat(), 45.0f, "(what is in between is there to be read)");
        cvars().endSession();
    });
    TEST_ASSERT(t_protected.getFloat() == 0.0f && s_protectedChange.calls == 0 && s_changes == 0,
                "the lock going away and the session ending as one change");

    t_layered.removeAllCallbacks();
    t_string.removeAllCallbacks();
    t_protected.removeAllCallbacks();
    t_layered.setValue(1.0f);
    t_string.setValue("abc");
    t_int.setValue(5);
}

void ConVarTest::testThreads() {
    TEST_SECTION("threads");

    // numbers can be read from any thread while the main thread changes the convar (in every way there is): a reader
    // only ever sees values that were the convar's at some point, and it ends up with the last one
    // (the latter is what the cache the getters used to fill in couldn't promise: a reader could put a stale value
    // back into it after the write that should have replaced it)
    std::atomic<bool> writerDone{false};
    std::atomic<int> unexpectedReads{0};
    std::atomic<u64> reads{0};
    std::atomic<float> lastRead{0.f};
    {
        Sync::jthread reader([&](const Sync::stop_token & /*stoken*/) -> void {
            bool lastRound = false;
            while(true) {
                lastRound = writerDone.load(std::memory_order_acquire);
                const float plain = t_layered.getFloat();
                const float locked = t_protected.getFloat();
                if(!(plain >= 1.0f && plain <= 4.0f)) unexpectedReads.fetch_add(1, std::memory_order_relaxed);
                if(!(locked == 0.0f || locked == 45.0f)) unexpectedReads.fetch_add(1, std::memory_order_relaxed);
                reads.fetch_add(1, std::memory_order_relaxed);
                if(lastRound) {
                    lastRead.store(plain, std::memory_order_release);
                    break;
                }
            }
        });

        t_protected.setValue(45.0f);
        for(int i = 0; i < 20000; i++) {
            t_layered.setValue(1.0f + static_cast<float>(i % 100) / 100.f);
            if(i % 3 == 0) t_layered.setValue(2.5f, true, CvarEditor::SKIN);
            if(i % 5 == 0) t_layered.setValue(3.5f, true, CvarEditor::SERVER);
            if(i % 7 == 0) t_layered.clearValue(CvarEditor::SERVER);
            if(i % 11 == 0) cvars().clearLayer(CvarEditor::SKIN);
            if(i % 13 == 0) setLocked(i % 2 == 0);
            if(i % 17 == 0) {
                cvars().change([] {
                    t_layered.setValue(3.0f);
                    t_layered.setValue(2.0f);
                });
            }
        }
        cvars().clearLayer(CvarEditor::SERVER);
        cvars().clearLayer(CvarEditor::SKIN);
        setLocked(false);
        t_layered.setValue(4.0f);
        writerDone.store(true, std::memory_order_release);
    }

    TEST_ASSERT(reads.load() > 0, "the reader thread got to read");
    TEST_ASSERT_EQ(unexpectedReads.load(), 0, "a reader on another thread only sees values that were set");
    TEST_ASSERT_EQ(lastRead.load(), 4.0f, "a reader on another thread ends up with the last value");

    t_layered.setValue(1.0f);
    t_protected.setValue(0.0f);
}

}  // namespace Mc::Tests
