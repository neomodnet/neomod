// Copyright (c) 2026, WH, All rights reserved.
#include "NetworkTest.h"

#include "TestMacros.h"
#include "Engine.h"
#include "Timing.h"

#include <string_view>
#include <utility>

namespace Mc::Tests {

using namespace Mc::Net;

// stable, low-traffic targets reachable from both native and browser builds.
// example.com returns 200 with "Example Domain" in the body, 404 for any unknown path.
// the .invalid TLD (RFC 2606) never resolves, so it always yields a connection failure.
namespace {
constexpr std::string_view URL_OK = "https://example.com";
constexpr std::string_view URL_404 = "https://example.com/neomod-nonexistent-xyz";
constexpr std::string_view URL_BADHOST = "https://nonexistent-host-neomod-test.invalid";
constexpr double WATCHDOG_SECS = 15.0;      // a request that hung past its own timeout is a failure
constexpr double CANCEL_SETTLE_SECS = 4.0;  // long enough that an un-cancelled request would have completed
}  // namespace

NetworkTest::NetworkTest() { logRaw("NetworkTest created"); }

void NetworkTest::startGet(std::string_view url, Capture& cap, Sync::stop_token token) {
    cap = {};
    RequestOptions options{
        .user_agent = "neomod-NetworkTest",
        .timeout = 10,
        .connect_timeout = 5,
    };
    options.cancel_token = std::move(token);
    networkHandler->httpRequestAsync(url, std::move(options), [&cap](Response response) {
        cap.resp = std::move(response);
        cap.fired = true;
    });
}

void NetworkTest::runUrlEncodeTests() {
    TEST_SECTION("urlEncode");
    TEST_ASSERT_EQ(urlEncode("hello world"), "hello%20world", "encodes spaces");
    TEST_ASSERT_EQ(urlEncode("a/b?c=d&e"), "a%2Fb%3Fc%3Dd%26e", "encodes reserved characters");
    TEST_ASSERT_EQ(urlEncode("AZaz09-_.~"), "AZaz09-_.~", "leaves RFC 3986 unreserved characters untouched");
    TEST_ASSERT_EQ(urlEncode(""), "", "empty input stays empty");
}

void NetworkTest::runSyncTest() {
    TEST_SECTION("synchronous GET");
    RequestOptions options{
        .user_agent = "neomod-NetworkTest",
        .timeout = 10,
        .connect_timeout = 5,
    };
    const Response resp = networkHandler->httpRequestSynchronous(URL_OK, std::move(options));
    TEST_ASSERT(resp.success, "synchronous GET to example.com succeeds");
    TEST_ASSERT_EQ(resp.response_code, 200L, "synchronous GET returns HTTP 200");
    TEST_ASSERT(resp.body.contains("Example Domain"), "synchronous GET body has expected content");
}

void NetworkTest::update() {
    const double now = Timing::getTimeReal();

    switch(m_phase) {
        case START:
            runUrlEncodeTests();
            runSyncTest();

            TEST_SECTION("async GET");
            startGet(URL_OK, m_get);
            m_watchdog = now + WATCHDOG_SECS;
            m_phase = WAIT_GET;
            return;

        case WAIT_GET:
            if(!m_get.fired && now <= m_watchdog) return;
            if(m_get.fired) {
                TEST_ASSERT(m_get.resp.success, "async GET to example.com succeeds");
                TEST_ASSERT_EQ(m_get.resp.response_code, 200L, "async GET returns HTTP 200");
                TEST_ASSERT(m_get.resp.body.contains("Example Domain"), "async GET body has expected content");
            } else {
                TEST_ASSERT(false, "async GET callback never arrived");
            }

            TEST_SECTION("async 404");
            startGet(URL_404, m_notfound);
            m_watchdog = now + WATCHDOG_SECS;
            m_phase = WAIT_NOTFOUND;
            return;

        case WAIT_NOTFOUND:
            if(!m_notfound.fired && now <= m_watchdog) return;
            if(m_notfound.fired) {
                TEST_ASSERT(!m_notfound.resp.success, "404 is reported as a failure");
                TEST_ASSERT_EQ(m_notfound.resp.response_code, 404L, "404 carries the HTTP status code");
            } else {
                TEST_ASSERT(false, "async 404 callback never arrived");
            }

            TEST_SECTION("async connection failure");
            startGet(URL_BADHOST, m_badhost);
            m_watchdog = now + WATCHDOG_SECS;
            m_phase = WAIT_BADHOST;
            return;

        case WAIT_BADHOST:
            if(!m_badhost.fired && now <= m_watchdog) return;
            if(m_badhost.fired) {
                TEST_ASSERT(!m_badhost.resp.success, "unresolvable host is reported as a failure");
                TEST_ASSERT(!m_badhost.resp.error_msg.empty(), "connection failure carries an error message");
            } else {
                TEST_ASSERT(false, "async connection-failure callback never arrived");
            }

            TEST_SECTION("unstopped cancel token still delivers");
            startGet(URL_OK, m_token, m_token_src.get_token());
            m_watchdog = now + WATCHDOG_SECS;
            m_phase = WAIT_TOKEN;
            return;

        case WAIT_TOKEN:
            if(!m_token.fired && now <= m_watchdog) return;
            if(m_token.fired) {
                TEST_ASSERT(m_token.resp.success, "request with an armed-but-unstopped token delivers normally");
            } else {
                TEST_ASSERT(false, "unstopped-token callback never arrived (token presence wrongly suppressed it)");
            }

            // stop in the same frame we submit: networkHandler->update() only delivers at frame boundaries, so
            // nothing can slip through in between. the callback must never arrive, whether the request is dropped
            // before it starts, aborted in flight, or completed-then-suppressed by the update-loop re-check.
            TEST_SECTION("immediate cancellation");
            startGet(URL_OK, m_cancelImmediate, m_cancelImmediate_src.get_token());
            m_cancelImmediate_src.request_stop();
            m_watchdog = now + CANCEL_SETTLE_SECS;
            m_phase = WAIT_CANCEL_IMMEDIATE;
            return;

        case WAIT_CANCEL_IMMEDIATE:
            if(m_cancelImmediate.fired) {
                TEST_ASSERT(false, "request stopped in the same frame must never deliver its callback");
            } else if(now <= m_watchdog) {
                return;  // wait out the settle window to be sure nothing arrives late
            } else {
                TEST_ASSERT(!m_cancelImmediate.fired, "request stopped in the same frame never delivers its callback");
            }

            // now a genuine in-flight cancel: submit, let one frame pass so the request can actually start
            TEST_SECTION("in-flight cancellation");
            startGet(URL_OK, m_cancelInflight, m_cancelInflight_src.get_token());
            m_phase = CANCEL_INFLIGHT_DECIDE;
            return;

        case CANCEL_INFLIGHT_DECIDE:
            // race-tolerant: if it already finished, there was nothing to cancel (a valid outcome). otherwise it is
            // still pending right now (delivery only happens at a frame boundary, before this runs), so stopping it
            // here must suppress the callback for good.
            if(m_cancelInflight.fired) {
                TEST_ASSERT(m_cancelInflight.resp.success, "request finished before in-flight cancellation applied");
                finish();
                return;
            }
            m_cancelInflight_src.request_stop();
            m_watchdog = now + CANCEL_SETTLE_SECS;
            m_phase = WAIT_CANCEL_INFLIGHT;
            return;

        case WAIT_CANCEL_INFLIGHT:
            if(m_cancelInflight.fired) {
                TEST_ASSERT(false, "in-flight request delivered its callback after being cancelled");
                finish();
            } else if(now > m_watchdog) {
                TEST_ASSERT(!m_cancelInflight.fired, "in-flight request was cancelled (callback suppressed)");
                finish();
            }
            return;

        case DONE:
            return;
    }
}

void NetworkTest::finish() {
    m_phase = DONE;
    TEST_PRINT_RESULTS("NetworkTest");
    engine->shutdown();
}

}  // namespace Mc::Tests
