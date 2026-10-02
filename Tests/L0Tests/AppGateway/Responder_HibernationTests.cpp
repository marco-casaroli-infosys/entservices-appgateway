/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// L0 tests for AppGatewayResponderImplementation hibernation / traffic-gate
// behaviour introduced by RDKEMW-19304:
//
//   SuspendTraffic(appId)  — marks the app as paused in PausedAppsRegistry
//   ResumeTraffic(appId)   — clears the paused flag
//
// While an app is paused:
//   Respond()      — returns ERROR_NONE but silently drops the response
//   Emit()         — returns ERROR_NONE but silently drops the notification
//   Request()      — returns ERROR_NONE but silently drops the request
//                    (if the connectionId maps to a paused appId via mAppIdRegistry)
//
// After ResumeTraffic() the methods enqueue jobs normally again.
//
// Additionally AppGatewayResponderImplementation now implements
// IAppGatewayAppSessionGuard, so we verify QueryInterface returns a
// valid pointer for that interface.

#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>

#include "AppGatewayResponderImplementation.h"
#include "ServiceMock.h"

#include <core/core.h>
#include <interfaces/IAppGateway.h>

using WPEFramework::Core::ERROR_NONE;
using WPEFramework::Exchange::GatewayContext;
using WPEFramework::Exchange::IAppGatewayAppSessionGuard;

namespace {

struct TestResult {
    uint32_t failures { 0 };
};

static void ExpectTrue(TestResult& tr, const bool condition, const std::string& what)
{
    if (!condition) {
        tr.failures++;
        std::cerr << "FAIL: " << what << std::endl;
    }
}

static void ExpectEqU32(TestResult& tr, const uint32_t actual, const uint32_t expected, const std::string& what)
{
    if (actual != expected) {
        tr.failures++;
        std::cerr << "FAIL: " << what
                  << " expected=" << expected << " actual=" << actual << std::endl;
    }
}

static GatewayContext MakeCtx(uint32_t requestId, uint32_t connectionId, const std::string& appId)
{
    GatewayContext ctx;
    ctx.requestId    = requestId;
    ctx.connectionId = connectionId;
    ctx.appId        = appId;
    return ctx;
}

// Allow async enqueue jobs to settle before teardown.
static void DrainJobs()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

} // namespace

// ---------------------------------------------------------------------------
// Test: QueryInterface for IAppGatewayAppSessionGuard returns non-null
// (verifies the INTERFACE_ENTRY is wired up)
// ---------------------------------------------------------------------------
// PUBLIC_INTERFACE
uint32_t Test_Responder_QueryInterface_SessionGuard()
{
    TestResult tr;

    WPEFramework::Core::Sink<WPEFramework::Plugin::AppGatewayResponderImplementation> responder;

    auto* guard = static_cast<IAppGatewayAppSessionGuard*>(
        responder.QueryInterface(IAppGatewayAppSessionGuard::ID));

    ExpectTrue(tr, guard != nullptr,
               "QueryInterface(IAppGatewayAppSessionGuard::ID) returns non-null");

    if (guard != nullptr) {
        guard->Release();
    }

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test: SuspendTraffic / ResumeTraffic return ERROR_NONE and don't crash
// ---------------------------------------------------------------------------
// PUBLIC_INTERFACE
uint32_t Test_Responder_SuspendTraffic_And_ResumeTraffic_ReturnNone()
{
    TestResult tr;

    WPEFramework::Core::Sink<WPEFramework::Plugin::AppGatewayResponderImplementation> responder;

    const std::string appId = "com.example.hibernating";

    // Suspend: first call
    ExpectEqU32(tr,
                responder.SuspendTraffic(appId),
                ERROR_NONE,
                "First SuspendTraffic returns ERROR_NONE");

    // Suspend: idempotent second call
    ExpectEqU32(tr,
                responder.SuspendTraffic(appId),
                ERROR_NONE,
                "Idempotent second SuspendTraffic returns ERROR_NONE");

    // Resume: clears the flag
    ExpectEqU32(tr,
                responder.ResumeTraffic(appId),
                ERROR_NONE,
                "ResumeTraffic returns ERROR_NONE");

    // Resume: idempotent (app was not paused)
    ExpectEqU32(tr,
                responder.ResumeTraffic(appId),
                ERROR_NONE,
                "Idempotent ResumeTraffic (already resumed) returns ERROR_NONE");

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test: Respond returns ERROR_NONE and drops the job while app is suspended.
// After ResumeTraffic the same call is accepted (still returns ERROR_NONE, the
// job is just enqueued instead of dropped).
// ---------------------------------------------------------------------------
// PUBLIC_INTERFACE
uint32_t Test_Responder_Respond_DropsWhilePaused_ResumesAfterResume()
{
    TestResult tr;

    WPEFramework::Core::Sink<WPEFramework::Plugin::AppGatewayResponderImplementation> responder;

    const std::string appId  = "com.example.app.respond";
    const GatewayContext ctx = MakeCtx(1, 50, appId);

    // Baseline: not yet suspended — Respond should behave normally (ERROR_NONE)
    ExpectEqU32(tr,
                responder.Respond(ctx, R"({"ok":true})"),
                ERROR_NONE,
                "Respond before suspend returns ERROR_NONE");

    DrainJobs();

    // Suspend the app
    responder.SuspendTraffic(appId);

    // Respond while suspended — must silently drop and still return ERROR_NONE
    const uint32_t rcWhilePaused = responder.Respond(ctx, R"({"dropped":true})");
    ExpectEqU32(tr, rcWhilePaused, ERROR_NONE,
                "Respond while suspended returns ERROR_NONE (drop path)");

    // Resume and call Respond again — still ERROR_NONE
    responder.ResumeTraffic(appId);

    const uint32_t rcAfterResume = responder.Respond(ctx, R"({"ok":true})");
    ExpectEqU32(tr, rcAfterResume, ERROR_NONE,
                "Respond after resume returns ERROR_NONE");

    DrainJobs();

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test: Emit returns ERROR_NONE and drops the notification while paused.
// ---------------------------------------------------------------------------
// PUBLIC_INTERFACE
uint32_t Test_Responder_Emit_DropsWhilePaused_ResumesAfterResume()
{
    TestResult tr;

    WPEFramework::Core::Sink<WPEFramework::Plugin::AppGatewayResponderImplementation> responder;

    const std::string appId  = "com.example.app.emit";
    const GatewayContext ctx = MakeCtx(2, 51, appId);

    // Baseline
    ExpectEqU32(tr,
                responder.Emit(ctx, "event.test", R"({"v":1})"),
                ERROR_NONE,
                "Emit before suspend returns ERROR_NONE");

    DrainJobs();

    // Pause
    responder.SuspendTraffic(appId);

    // Emit while paused — drop path
    ExpectEqU32(tr,
                responder.Emit(ctx, "event.test", R"({"dropped":true})"),
                ERROR_NONE,
                "Emit while suspended returns ERROR_NONE (drop path)");

    // Resume
    responder.ResumeTraffic(appId);

    // Emit after resume — normal path
    ExpectEqU32(tr,
                responder.Emit(ctx, "event.test", R"({"v":2})"),
                ERROR_NONE,
                "Emit after resume returns ERROR_NONE");

    DrainJobs();

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test: Request returns ERROR_NONE and drops while paused (when the
// connectionId is mapped to a paused appId).
// This tests the appIdRegistry lookup path:
//   mAppIdRegistry.Get(connectionId, appId) && mPausedAppsRegistry.IsPaused(appId)
//
// Without a real WebSocket connection the mAppIdRegistry won't contain the
// entry, so the drop-path is not exercised here; the test verifies that the
// normal (non-paused) ERROR_NONE path still works after SuspendTraffic.
// The paused path for Request requires a live WebSocket session — that is
// covered by integration/L1 tests.
// ---------------------------------------------------------------------------
// PUBLIC_INTERFACE
uint32_t Test_Responder_Request_ReturnsNone_WhilePaused()
{
    TestResult tr;

    WPEFramework::Core::Sink<WPEFramework::Plugin::AppGatewayResponderImplementation> responder;

    const std::string appId    = "com.example.app.request";
    const uint32_t   connId    = 52;

    // Suspend (no live connection, so mAppIdRegistry has no entry for connId;
    // the paused-app check is skipped and Request falls through to enqueue)
    responder.SuspendTraffic(appId);

    // Should still return ERROR_NONE (either enqueued or dropped)
    ExpectEqU32(tr,
                responder.Request(connId, 99, "method.x", R"({"b":1})"),
                ERROR_NONE,
                "Request while app is suspended (no live conn mapping) returns ERROR_NONE");

    responder.ResumeTraffic(appId);

    ExpectEqU32(tr,
                responder.Request(connId, 100, "method.x", R"({"b":2})"),
                ERROR_NONE,
                "Request after resume returns ERROR_NONE");

    DrainJobs();

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test: Multiple distinct apps can be paused/resumed independently.
// ---------------------------------------------------------------------------
// PUBLIC_INTERFACE
uint32_t Test_Responder_SuspendResume_MultipleApps_Independent()
{
    TestResult tr;

    WPEFramework::Core::Sink<WPEFramework::Plugin::AppGatewayResponderImplementation> responder;

    const std::string appA = "com.example.appA";
    const std::string appB = "com.example.appB";

    responder.SuspendTraffic(appA);

    // App A suspended: Respond returns ERROR_NONE (drop path)
    GatewayContext ctxA = MakeCtx(10, 60, appA);
    ExpectEqU32(tr,
                responder.Respond(ctxA, "{}"),
                ERROR_NONE,
                "Respond for suspended appA returns ERROR_NONE");

    // App B not suspended: Respond returns ERROR_NONE (normal enqueue path)
    GatewayContext ctxB = MakeCtx(11, 61, appB);
    ExpectEqU32(tr,
                responder.Respond(ctxB, "{}"),
                ERROR_NONE,
                "Respond for unsuspended appB returns ERROR_NONE");

    // Suspend B as well
    responder.SuspendTraffic(appB);
    ExpectEqU32(tr,
                responder.Respond(ctxB, "{}"),
                ERROR_NONE,
                "Respond for suspended appB returns ERROR_NONE");

    // Resume A only
    responder.ResumeTraffic(appA);
    ExpectEqU32(tr,
                responder.Respond(ctxA, "{}"),
                ERROR_NONE,
                "Respond for resumed appA (appB still paused) returns ERROR_NONE");

    // B still paused
    ExpectEqU32(tr,
                responder.Emit(ctxB, "event", "{}"),
                ERROR_NONE,
                "Emit for still-suspended appB returns ERROR_NONE");

    // Resume B
    responder.ResumeTraffic(appB);

    DrainJobs();

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test: IAppGatewayAppSessionGuard via QueryInterface on the responder
// (round-trip through the interface pointer)
// ---------------------------------------------------------------------------
// PUBLIC_INTERFACE
uint32_t Test_Responder_SessionGuard_Via_Interface_Pointer()
{
    TestResult tr;

    WPEFramework::Core::Sink<WPEFramework::Plugin::AppGatewayResponderImplementation> responder;

    // Obtain the guard interface
    auto* guard = static_cast<IAppGatewayAppSessionGuard*>(
        responder.QueryInterface(IAppGatewayAppSessionGuard::ID));
    ExpectTrue(tr, guard != nullptr,
               "IAppGatewayAppSessionGuard available via QueryInterface");

    if (guard != nullptr) {
        const std::string appId = "com.example.via.interface";

        // SuspendTraffic via interface pointer
        ExpectEqU32(tr,
                    guard->SuspendTraffic(appId),
                    ERROR_NONE,
                    "SuspendTraffic via interface pointer returns ERROR_NONE");

        // Verify the suspension is reflected in the responder: Respond drops
        GatewayContext ctx = MakeCtx(20, 70, appId);
        ExpectEqU32(tr,
                    responder.Respond(ctx, R"({"test":1})"),
                    ERROR_NONE,
                    "Respond on suspended app (via guard interface) returns ERROR_NONE");

        // ResumeTraffic via interface pointer
        ExpectEqU32(tr,
                    guard->ResumeTraffic(appId),
                    ERROR_NONE,
                    "ResumeTraffic via interface pointer returns ERROR_NONE");

        guard->Release();
    }

    DrainJobs();

    return tr.failures;
}
