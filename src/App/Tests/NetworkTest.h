// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "App.h"
#include "NetworkHandler.h"
#include "SyncStoptoken.h"

namespace Mc::Tests {

// sanity suite for NetworkHandler, exercising the same code on native (curl) and
// emscripten (fetch). focuses on the async cancellation paths added alongside this test.
class NetworkTest : public App {
    NOCOPY_NOMOVE(NetworkTest)
   public:
    NetworkTest();
    ~NetworkTest() override = default;

    void update() override;

   private:
    // one async response, recorded on the main thread by the request's callback
    struct Capture {
        Mc::Net::Response resp;
        bool fired{false};
    };

    void runUrlEncodeTests();
    void runSyncTest();
    void startGet(std::string_view url, Capture& cap, Sync::stop_token token = {});
    void finish();

    int m_passes = 0;
    int m_failures = 0;

    // each network request spans multiple frames: kick it off, then poll its Capture until the
    // callback fires (delivered by networkHandler->update(), which runs just before app->update())
    // or the watchdog elapses. the cancellation cases are the inverse: the callback must NOT arrive.
    enum Phase : u8 {
        START,  // deterministic urlEncode + synchronous GET, then kick off the async GET

        WAIT_GET,       // async GET succeeds
        WAIT_NOTFOUND,  // GET to a missing path reports a 404 failure
        WAIT_BADHOST,   // GET to an unresolvable host reports a connection failure
        WAIT_TOKEN,     // a cancel token that is never stopped still delivers normally

        WAIT_CANCEL_IMMEDIATE,   // stopped in the same frame as submit: callback must never arrive
        CANCEL_INFLIGHT_DECIDE,  // one frame later: cancel if still pending, else accept it finished first
        WAIT_CANCEL_INFLIGHT,    // a request cancelled while in flight must never deliver

        DONE
    };
    Phase m_phase{START};
    double m_watchdog{0.0};  // wall-clock deadline for the current wait

    Capture m_get;
    Capture m_notfound;
    Capture m_badhost;
    Capture m_token;
    Capture m_cancelImmediate;  // stopped same-frame; .fired must stay false
    Capture m_cancelInflight;   // stopped a frame in, unless it finished first

    Sync::stop_source m_token_src;            // armed but never stopped
    Sync::stop_source m_cancelImmediate_src;  // stopped in the same frame as submit
    Sync::stop_source m_cancelInflight_src;   // stopped once the request is in flight
};

}  // namespace Mc::Tests
