// Copyright (c) 2026, WH, All rights reserved.
#include "ConVarTest.h"

#include "TestMacros.h"
#include "ConVar.h"
#include "ConVarHandler.h"
#include "Engine.h"
#include "types.h"

#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>

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

std::string s_cmdArgs;
int s_cmdCalls{0};
ConVar t_cmd("cvtest_cmd", cv::CLIENT | cv::SERVER | TESTONLY, [](std::string_view args) -> void {
    s_cmdCalls++;
    s_cmdArgs = args;
});

// stand-ins for the policy the app normally provides (see Osu's globalOn* callbacks)
bool s_gateOpen{true};  // "not in a multiplayer match": gameplay convars may be changed
int s_gateCalls{0};
std::string s_gateLastName;
CvarEditor s_gateLastEditor{CvarEditor::CLIENT};
int s_protectedChanges{0};
bool s_extraSubmittable{true};

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

bool isNonSubmittable(const ConVar &cvar) { return std::ranges::contains(cvars().getNonSubmittableCvars(), &cvar); }
}  // namespace

ConVarTest::ConVarTest() {
    logRaw("ConVarTest created");

    ConVar::setOnSetValueGameplayCallback([](std::string_view name, CvarEditor editor) -> bool {
        s_gateCalls++;
        s_gateLastName = name;
        s_gateLastEditor = editor;
        return s_gateOpen;
    });
    ConVar::setOnSetValueProtectedCallback(ConVar::VoidCB([]() -> void { s_protectedChanges++; }));
    cvars().setCVSubmittableCheckFunc([]() -> bool { return s_extraSubmittable; });
}

ConVarTest::~ConVarTest() {
    for(auto *cvar : {&t_layered, &t_protected, &t_callbacks, &t_string, &t_cmd}) cvar->removeAllCallbacks();

    cvars().setCVSubmittableCheckFunc({});
    ConVar::setOnSetValueProtectedCallback({});
    ConVar::setOnSetValueGameplayCallback({});
}

void ConVarTest::update() {
    if(m_done) return;
    m_done = true;

    this->testTypesAndParsing();
    this->testPermissions();
    this->testLayers();
    this->testProtectionLock();
    this->testGameplayGate();
    this->testCallbacks();
    this->testSubmittable();
    this->testDefaults();
    this->testCommands();

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
    TEST_ASSERT_EQ(s_protectedChanges, 2, "changing an unprotected convar doesn't notify");

    t_protected.removeAllCallbacks();
    t_protected.setValue(0.0f);
    t_layered.setValue(1.0f);
}

void ConVarTest::testGameplayGate() {
    TEST_SECTION("gameplay gate");

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

    s_gateCalls = 0;
    t_float.setValue(2.0f);
    TEST_ASSERT_EQ(s_gateCalls, 0, "gate isn't asked about convars without GAMEPLAY");

    t_gameplay.removeAllCallbacks();
    t_gameplay.setValue(0.0f);
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

void ConVarTest::testSubmittable() {
    TEST_SECTION("submittable");

    TEST_ASSERT(!isNonSubmittable(t_protected), "default protected convar is submittable");
    TEST_ASSERT(cvars().areAllCvarsSubmittable(), "everything is submittable to begin with");

    t_protected.setValue(45.0f);
    TEST_ASSERT(isNonSubmittable(t_protected), "changed protected convar is not submittable");
    TEST_ASSERT(!cvars().areAllCvarsSubmittable(), "one changed protected convar makes everything unsubmittable");

    setLocked(true);
    TEST_ASSERT(!isNonSubmittable(t_protected), "locked protected convar reads as default, so it is submittable");
    setLocked(false);

    t_layered.setValue(1.5f);
    TEST_ASSERT(!isNonSubmittable(t_layered), "changed unprotected convar is submittable");
    t_layered.setServerProtected(CvarProtection::PROTECTED);
    TEST_ASSERT(isNonSubmittable(t_layered), "changed server-protected convar is not submittable");
    cvars().clearLayer(CvarEditor::SERVER);

    t_protected.setValue(0.0f);
    t_layered.setValue(1.0f);
    TEST_ASSERT(cvars().areAllCvarsSubmittable(), "everything is submittable again");

    // anything that can change what a protected convar reads as has to keep the answer up to date
    t_protectedSkin.setValue(true, true, CvarEditor::SKIN);
    TEST_ASSERT(!cvars().areAllCvarsSubmittable(), "a skin value on a protected convar is not submittable");
    setLocked(true);
    TEST_ASSERT(cvars().areAllCvarsSubmittable(), "...unless the lock hides it");
    setLocked(false);
    t_protectedSkin.clearValue(CvarEditor::SKIN);
    TEST_ASSERT(cvars().areAllCvarsSubmittable(), "submittable again without the skin value");

    t_protected.setValue(45.0f, true, CvarEditor::SERVER);
    TEST_ASSERT(!cvars().areAllCvarsSubmittable(), "a server value on a protected convar is not submittable");
    t_protected.setDefaultDouble(45.0);
    TEST_ASSERT(cvars().areAllCvarsSubmittable(), "a changed default counts");
    t_protected.setDefaultDouble(0.0);
    TEST_ASSERT(!cvars().areAllCvarsSubmittable(), "a changed default counts, back");
    t_protected.setServerProtected(CvarProtection::UNPROTECTED);
    TEST_ASSERT(cvars().areAllCvarsSubmittable(), "unprotecting a changed convar makes it submittable");
    cvars().clearLayer(CvarEditor::SERVER);
    TEST_ASSERT(cvars().areAllCvarsSubmittable() && cvars().getNonSubmittableCvars().empty(),
                "submittable again without anything from the server");

    s_extraSubmittable = false;
    TEST_ASSERT(!cvars().areAllCvarsSubmittable(), "the app's extra check can veto");
    s_extraSubmittable = true;
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

}  // namespace Mc::Tests
