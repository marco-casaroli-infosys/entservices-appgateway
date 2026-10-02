/**
 * L0 coverage tests for AppGatewayTelemetry.cpp
 *
 * These tests exercise the paths that were not covered in the existing
 * L0 test suite, targeting:
 *  - Health stat counter methods (Increment/Decrement WS, calls, responses)
 *  - RecordResponse (success and failure branches)
 *  - SetReportingInterval / SetCacheThreshold / SetTelemetryFormat
 *  - HandleHealthStatsMarker (via RecordTelemetryMetric with health markers)
 *  - ParseApiMetricName / RecordApiMethodMetric (success + error)
 *  - ParseApiLatencyMetricName / RecordApiLatencyMetric
 *  - ParseServiceLatencyMetricName / RecordServiceLatencyMetric
 *  - ParseServiceMetricName / RecordServiceMethodMetric (success + error)
 *  - RecordTelemetryEvent for api-error and ext-service-error events
 *  - RecordTelemetryEvent non-immediate event → cache threshold flush
 *  - TelemetrySnapshot::SendHealthStats / SendApiMethodStats / SendApiLatencyStats
 *    / SendServiceMethodStats / SendServiceLatencyStats / SendApiErrorStats
 *    / SendExternalServiceErrorStats / SendAggregatedMetrics with data
 *  - FormatTelemetryPayload COMPACT path
 *  - SendT2Event (const char*, const std::string&, ctx) overload via API error
 */

#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>

#include <core/core.h>

#include "AppGatewayTelemetry.h"
#include "ServiceMock.h"
#include "AppGatewayTelemetryMarkers.h"

using WPEFramework::Core::ERROR_NONE;
using WPEFramework::Core::ERROR_UNAVAILABLE;
using WPEFramework::Exchange::GatewayContext;
using WPEFramework::Plugin::AppGatewayTelemetry;
using WPEFramework::Plugin::TelemetryFormat;

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
        std::cerr << "FAIL: " << what << " expected=" << expected << " actual=" << actual << std::endl;
    }
}

// RAII guard to Initialize/Deinitialize AppGatewayTelemetry singleton around a test.
// We explicitly AddRef before Initialize and Release after Deinitialize so the
// ServiceMock stays alive for the full telemetry lifecycle, regardless of whether
// the production code starts refcounting the IShell* in the future.
struct TelemetryGuard {
    L0Test::ServiceMock* svc { nullptr };

    TelemetryGuard()
        : svc(new L0Test::ServiceMock({}, true))
    {
        svc->AddRef();
        AppGatewayTelemetry::getInstance().Initialize(svc);
    }

    ~TelemetryGuard()
    {
        AppGatewayTelemetry::getInstance().Deinitialize();
        svc->Release();
        svc = nullptr;
    }

    TelemetryGuard(const TelemetryGuard&) = delete;
    TelemetryGuard& operator=(const TelemetryGuard&) = delete;
};

static GatewayContext MakeCtx(uint32_t reqId = 1, uint32_t connId = 1, const std::string& appId = "com.test.app")
{
    GatewayContext ctx;
    ctx.requestId   = reqId;
    ctx.connectionId = connId;
    ctx.appId        = appId;
    return ctx;
}

} // namespace

// ---------------------------------------------------------------------------
// Test 1: SetReportingInterval (timer-running branch), SetCacheThreshold,
//         SetTelemetryFormat, GetTelemetryFormat
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_SettersAndConfig()
{
    TestResult tr;
    TelemetryGuard guard;  // mTimerRunning = true after Initialize

    auto& t = AppGatewayTelemetry::getInstance();

    // SetReportingInterval while timer is running → hits the "restart timer" branch (lines 127-131)
    t.SetReportingInterval(300);

    // SetCacheThreshold (lines 138-139)
    t.SetCacheThreshold(500);

    // SetTelemetryFormat COMPACT (lines 141-143)
    t.SetTelemetryFormat(TelemetryFormat::COMPACT);
    ExpectTrue(tr, t.GetTelemetryFormat() == TelemetryFormat::COMPACT, "GetTelemetryFormat returns COMPACT after SetTelemetryFormat(COMPACT)");

    // Restore to JSON for other tests
    t.SetTelemetryFormat(TelemetryFormat::JSON);
    ExpectTrue(tr, t.GetTelemetryFormat() == TelemetryFormat::JSON, "GetTelemetryFormat returns JSON after restore");

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 2: Health stat counter methods + RecordResponse branches
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_HealthStatCounters_And_RecordResponse()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();

    const auto ctx1 = MakeCtx(10, 10, "com.test.health");
    const auto ctx2 = MakeCtx(11, 11, "com.test.health");
    const auto ctx3 = MakeCtx(12, 12, "com.test.health");
    const auto ctx4 = MakeCtx(13, 13, "com.test.health");
    const auto ctx5 = MakeCtx(14, 14, "com.test.health");

    // IncrementWebSocketConnections (lines 166-171)
    t.IncrementWebSocketConnections(ctx1);
    t.IncrementWebSocketConnections(ctx2);

    // DecrementWebSocketConnections - count > 0 branch (lines 173-181)
    t.DecrementWebSocketConnections(ctx1);
    // DecrementWebSocketConnections - count == 0 branch (decrement when already 0)
    t.DecrementWebSocketConnections(ctx1);
    t.DecrementWebSocketConnections(ctx1);

    // IncrementTotalCalls - new key branch (lines 183-199)
    t.IncrementTotalCalls(ctx3);
    // IncrementTotalCalls - duplicate key branch (same ctx3 again)
    t.IncrementTotalCalls(ctx3);

    // IncrementTotalResponses - with known key (lines 201-222)
    t.IncrementTotalCalls(ctx4);   // add key first
    t.IncrementTotalResponses(ctx4);  // key exists → increment
    // IncrementTotalResponses - unknown key
    t.IncrementTotalResponses(ctx5);  // key doesn't exist → duplicate/unknown log

    // IncrementSuccessfulCalls - with known key (erases entry)
    const auto ctxS1 = MakeCtx(20, 20, "com.test.health");
    t.IncrementTotalCalls(ctxS1);        // create entry
    t.IncrementSuccessfulCalls(ctxS1);   // known → increment + erase (lines 224-241)
    // IncrementSuccessfulCalls - unknown key
    t.IncrementSuccessfulCalls(ctxS1);   // no longer in map → dup/unknown log

    // IncrementFailedCalls - with known key (erases entry)
    const auto ctxF1 = MakeCtx(21, 21, "com.test.health");
    t.IncrementTotalCalls(ctxF1);        // create entry
    t.IncrementFailedCalls(ctxF1);       // known → increment + erase (lines 244-260)
    // IncrementFailedCalls - unknown key
    t.IncrementFailedCalls(ctxF1);       // no longer in map → dup/unknown log

    // RecordResponse - success branch (lines 264-290)
    const auto ctxR1 = MakeCtx(30, 30, "com.test.health");
    t.IncrementTotalCalls(ctxR1);        // create entry
    t.RecordResponse(ctxR1, true);       // isSuccess=true → incr successfulCalls + erase

    // RecordResponse - failure branch
    const auto ctxR2 = MakeCtx(31, 31, "com.test.health");
    t.IncrementTotalCalls(ctxR2);        // create entry
    t.RecordResponse(ctxR2, false);      // isSuccess=false → incr failedCalls + erase

    // RecordResponse - no entry (key doesn't exist)
    const auto ctxR3 = MakeCtx(32, 32, "com.test.health");
    t.RecordResponse(ctxR3, true);       // key missing → log dup/unknown

    // Allow Deinitialize to flush anything accumulated
    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 3: HandleHealthStatsMarker via RecordTelemetryMetric with all health markers
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_HandleHealthStatsMarkers()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(40, 40, "com.test.markers");

    // AGW_MARKER_WEBSOCKET_CONNECTIONS > 0 → IncrementWebSocketConnections (lines 851-912)
    auto rc = t.RecordTelemetryMetric(ctx, AGW_MARKER_WEBSOCKET_CONNECTIONS, 2.0, AGW_UNIT_COUNT);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric WEBSOCKET_CONNECTIONS > 0 returns ERROR_NONE");

    // AGW_MARKER_WEBSOCKET_CONNECTIONS < 0 → DecrementWebSocketConnections
    rc = t.RecordTelemetryMetric(ctx, AGW_MARKER_WEBSOCKET_CONNECTIONS, -1.0, AGW_UNIT_COUNT);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric WEBSOCKET_CONNECTIONS < 0 returns ERROR_NONE");

    // AGW_MARKER_TOTAL_CALLS
    rc = t.RecordTelemetryMetric(ctx, AGW_MARKER_TOTAL_CALLS, 1.0, AGW_UNIT_COUNT);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric TOTAL_CALLS returns ERROR_NONE");

    // AGW_MARKER_RESPONSE_CALLS
    rc = t.RecordTelemetryMetric(ctx, AGW_MARKER_RESPONSE_CALLS, 1.0, AGW_UNIT_COUNT);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric RESPONSE_CALLS returns ERROR_NONE");

    // AGW_MARKER_SUCCESSFUL_CALLS
    rc = t.RecordTelemetryMetric(ctx, AGW_MARKER_SUCCESSFUL_CALLS, 1.0, AGW_UNIT_COUNT);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric SUCCESSFUL_CALLS returns ERROR_NONE");

    // AGW_MARKER_FAILED_CALLS
    rc = t.RecordTelemetryMetric(ctx, AGW_MARKER_FAILED_CALLS, 1.0, AGW_UNIT_COUNT);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric FAILED_CALLS returns ERROR_NONE");

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 4: ParseApiMetricName + RecordApiMethodMetric via RecordTelemetryMetric
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_RecordTelemetryMetric_ApiMethodParse()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(50, 50, "com.test.apimethod");

    // Success metric: "ENTS_INFO_AppGw_PluginName_<P>_MethodName_<M>_Success_split"
    const std::string successMetric =
        std::string(AGW_INTERNAL_PLUGIN_PREFIX) + "TestPlugin_MethodName_getVersion_Success_split";
    auto rc = t.RecordTelemetryMetric(ctx, successMetric, 12.5, AGW_UNIT_MILLISECONDS);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric api-method success returns ERROR_NONE");

    // Call again to cover the "pluginName not empty" branch in RecordApiMethodMetric
    rc = t.RecordTelemetryMetric(ctx, successMetric, 8.0, AGW_UNIT_MILLISECONDS);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric api-method success (2nd call) returns ERROR_NONE");

    // Error metric: "..._Error_split"
    const std::string errorMetric =
        std::string(AGW_INTERNAL_PLUGIN_PREFIX) + "TestPlugin_MethodName_setValue_Error_split";
    rc = t.RecordTelemetryMetric(ctx, errorMetric, 5.0, AGW_UNIT_MILLISECONDS);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric api-method error returns ERROR_NONE");

    // Call again (2nd error entry — covers max/min latency branches both)
    rc = t.RecordTelemetryMetric(ctx, errorMetric, 15.0, AGW_UNIT_MILLISECONDS);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric api-method error (2nd call) returns ERROR_NONE");

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 5: ParseApiLatencyMetricName + RecordApiLatencyMetric
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_RecordTelemetryMetric_ApiLatencyParse()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(60, 60, "com.test.apilatency");

    // "ENTS_INFO_AppGw_PluginName_<P>_ApiName_<A>_ApiLatency_split"
    const std::string latencyMetric =
        std::string(AGW_INTERNAL_PLUGIN_PREFIX) + "TestPlugin_ApiName_getToken_ApiLatency_split";

    auto rc = t.RecordTelemetryMetric(ctx, latencyMetric, 30.0, AGW_UNIT_MILLISECONDS);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric api latency returns ERROR_NONE");

    // Second call: covers "apiName not empty" branch + max/min latency update paths
    rc = t.RecordTelemetryMetric(ctx, latencyMetric, 50.0, AGW_UNIT_MILLISECONDS);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric api latency 2nd call returns ERROR_NONE");

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 6: ParseServiceLatencyMetricName + RecordServiceLatencyMetric
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_RecordTelemetryMetric_ServiceLatencyParse()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(70, 70, "com.test.svclatency");

    // "ENTS_INFO_AppGw_PluginName_<P>_ServiceName_<S>_ServiceLatency_split"
    const std::string svcLatencyMetric =
        std::string(AGW_INTERNAL_PLUGIN_PREFIX) + "TestPlugin_ServiceName_grpcServer_ServiceLatency_split";

    auto rc = t.RecordTelemetryMetric(ctx, svcLatencyMetric, 20.0, AGW_UNIT_MILLISECONDS);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric service latency returns ERROR_NONE");

    // Second call: covers "serviceName not empty" branch + max/min update paths
    rc = t.RecordTelemetryMetric(ctx, svcLatencyMetric, 5.0, AGW_UNIT_MILLISECONDS);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric service latency 2nd call returns ERROR_NONE");

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 7: ParseServiceMetricName + RecordServiceMethodMetric (success + error)
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_RecordTelemetryMetric_ServiceMethodParse()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(80, 80, "com.test.svcmethod");

    // Success: "ENTS_INFO_AppGw_PluginName_<P>_ServiceName_<S>_Success_split"
    // NOTE: ParseServiceMetricName uses "_ServiceName_" tag (not "_MethodName_")
    const std::string svcSuccessMetric =
        std::string(AGW_INTERNAL_PLUGIN_PREFIX) + "TestPlugin_ServiceName_permSvc_Success_split";

    auto rc = t.RecordTelemetryMetric(ctx, svcSuccessMetric, 10.0, AGW_UNIT_MILLISECONDS);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric service method success returns ERROR_NONE");

    // Second call: covers max/min latency update in success branch
    rc = t.RecordTelemetryMetric(ctx, svcSuccessMetric, 25.0, AGW_UNIT_MILLISECONDS);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric service method success 2nd call returns ERROR_NONE");

    // Error: "..._ServiceName_<S>_Error_split"
    const std::string svcErrorMetric =
        std::string(AGW_INTERNAL_PLUGIN_PREFIX) + "TestPlugin_ServiceName_permSvc_Error_split";

    rc = t.RecordTelemetryMetric(ctx, svcErrorMetric, 7.0, AGW_UNIT_MILLISECONDS);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric service method error returns ERROR_NONE");

    // Second call: covers max/min latency update in error branch
    rc = t.RecordTelemetryMetric(ctx, svcErrorMetric, 40.0, AGW_UNIT_MILLISECONDS);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryMetric service method error 2nd call returns ERROR_NONE");

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 8: RecordTelemetryEvent - API error event (covers api-error branch +
//          first SendT2Event string overload at lines 1413-1449)
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_RecordTelemetryEvent_ApiError()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(90, 90, "com.test.apierror");

    // With "api" field in JSON data
    const std::string eventData = R"({"plugin":"TestPlugin","api":"getSettings","error":"timeout"})";
    auto rc = t.RecordTelemetryEvent(ctx, AGW_MARKER_PLUGIN_API_ERROR, eventData);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryEvent api-error returns ERROR_NONE");

    // Without "api" field → uses eventName as api name
    const std::string eventDataNoApi = R"({"plugin":"TestPlugin","error":"timeout"})";
    rc = t.RecordTelemetryEvent(ctx, AGW_MARKER_PLUGIN_API_ERROR, eventDataNoApi);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryEvent api-error (no api field) returns ERROR_NONE");

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 9: RecordTelemetryEvent - external service error event
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_RecordTelemetryEvent_ExtServiceError()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(100, 100, "com.test.svcerror");

    // With "service" field in JSON data
    const std::string eventData = R"({"plugin":"TestPlugin","service":"GrpcServer","error":"connect refused"})";
    auto rc = t.RecordTelemetryEvent(ctx, AGW_MARKER_PLUGIN_EXT_SERVICE_ERROR, eventData);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryEvent ext-service-error returns ERROR_NONE");

    // Without "service" field → uses eventName as service name
    const std::string eventDataNoSvc = R"({"plugin":"TestPlugin","error":"timeout"})";
    rc = t.RecordTelemetryEvent(ctx, AGW_MARKER_PLUGIN_EXT_SERVICE_ERROR, eventDataNoSvc);
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryEvent ext-service-error (no service field) returns ERROR_NONE");

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 10: RecordTelemetryEvent non-immediate event → cache threshold flush
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_RecordTelemetryEvent_NonImmediate_CacheThreshold()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(110, 110, "com.test.cache");

    // Set threshold to 1 so the next non-immediate event triggers flush
    t.SetCacheThreshold(1);

    // Non-immediate event (not api-error, not ext-service-error, not response-tracking)
    const std::string genericEvent = "ENTS_INFO_AppGwSomeGenericEvent";
    auto rc = t.RecordTelemetryEvent(ctx, genericEvent, R"({"value":42})");
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryEvent non-immediate returns ERROR_NONE");

    // Reset threshold
    t.SetCacheThreshold(1000);

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 11: TelemetrySnapshot::SendHealthStats with data
//          (via FlushTelemetryData after populating health stats)
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_Flush_WithHealthData()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();

    // Populate health stats so snapshot has non-zero totalCalls + websocketConnections
    const auto ctx = MakeCtx(120, 120, "com.test.flushhealth");

    t.IncrementWebSocketConnections(ctx);
    t.IncrementWebSocketConnections(ctx);
    t.IncrementTotalCalls(ctx);

    const auto ctxS = MakeCtx(121, 121, "com.test.flushhealth");
    t.IncrementTotalCalls(ctxS);
    t.IncrementSuccessfulCalls(ctxS);

    const auto ctxF = MakeCtx(122, 122, "com.test.flushhealth");
    t.IncrementTotalCalls(ctxF);
    t.IncrementFailedCalls(ctxF);

    // Flush explicitly — covers TelemetrySnapshot::SendHealthStats with data (lines 1658-1672)
    t.FlushTelemetryData();

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 12: TelemetrySnapshot::SendApiMethodStats / SendApiLatencyStats /
//          SendServiceMethodStats / SendServiceLatencyStats with data
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_Flush_WithApiAndServiceStats()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(130, 130, "com.test.flushapiservice");

    // Populate API method stats (success + error) → SendApiMethodStats with data
    const std::string apiSuccessMetric =
        std::string(AGW_INTERNAL_PLUGIN_PREFIX) + "FlushPlugin_MethodName_doSomething_Success_split";
    const std::string apiErrorMetric =
        std::string(AGW_INTERNAL_PLUGIN_PREFIX) + "FlushPlugin_MethodName_doSomething_Error_split";

    t.RecordTelemetryMetric(ctx, apiSuccessMetric, 15.0, AGW_UNIT_MILLISECONDS);
    t.RecordTelemetryMetric(ctx, apiErrorMetric,   8.0,  AGW_UNIT_MILLISECONDS);

    // Populate API latency stats → SendApiLatencyStats with data
    const std::string apiLatencyMetric =
        std::string(AGW_INTERNAL_PLUGIN_PREFIX) + "FlushPlugin_ApiName_fetchData_ApiLatency_split";
    t.RecordTelemetryMetric(ctx, apiLatencyMetric, 22.0, AGW_UNIT_MILLISECONDS);

    // Populate service method stats (success + error) → SendServiceMethodStats with data
    const std::string svcSuccessMetric =
        std::string(AGW_INTERNAL_PLUGIN_PREFIX) + "FlushPlugin_ServiceName_permSvc_Success_split";
    const std::string svcErrorMetric =
        std::string(AGW_INTERNAL_PLUGIN_PREFIX) + "FlushPlugin_ServiceName_permSvc_Error_split";
    t.RecordTelemetryMetric(ctx, svcSuccessMetric, 11.0, AGW_UNIT_MILLISECONDS);
    t.RecordTelemetryMetric(ctx, svcErrorMetric,   3.0,  AGW_UNIT_MILLISECONDS);

    // Populate service latency stats → SendServiceLatencyStats with data
    const std::string svcLatencyMetric =
        std::string(AGW_INTERNAL_PLUGIN_PREFIX) + "FlushPlugin_ServiceName_grpcSvc_ServiceLatency_split";
    t.RecordTelemetryMetric(ctx, svcLatencyMetric, 40.0, AGW_UNIT_MILLISECONDS);

    // Flush → TelemetrySnapshot::Send*() with data
    t.FlushTelemetryData();

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 13: TelemetrySnapshot::SendApiErrorStats and SendExternalServiceErrorStats
//          with data (via RecordApiError and RecordExternalServiceErrorInternal)
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_Flush_WithApiAndServiceErrorCounts()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(140, 140, "com.test.flusherrors");

    // Populate API error counts (RecordApiError is already covered, but we need non-empty map)
    t.RecordApiError(ctx, "TestApi");
    t.RecordApiError(ctx, "TestApi");     // increment same api

    // Populate ext service error counts
    t.RecordExternalServiceErrorInternal(ctx, "TestService");

    // Flush → TelemetrySnapshot::SendApiErrorStats (lines 1888-1922) and
    //         SendExternalServiceErrorStats (lines 1907-) with data
    t.FlushTelemetryData();

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 14: TelemetrySnapshot::SendAggregatedMetrics with data
//          (via RecordGenericMetric path in RecordTelemetryMetric)
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_Flush_WithAggregatedMetrics()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(150, 150, "com.test.flushaggr");

    // Record a generic metric (unrecognized name → RecordGenericMetric) to populate metricsCache
    t.RecordTelemetryMetric(ctx, "ENTS_INFO_AppGwCustomMetric", 99.0, AGW_UNIT_COUNT);
    t.RecordTelemetryMetric(ctx, "ENTS_INFO_AppGwCustomMetric", 42.0, AGW_UNIT_COUNT);

    // Flush → TelemetrySnapshot::SendAggregatedMetrics with data (line 1924+)
    t.FlushTelemetryData();

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 15: FormatTelemetryPayload COMPACT path (lines 1537-1607)
//          Set COMPACT format, add API data, flush → format path hit
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_FormatTelemetryPayload_CompactFormat()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(160, 160, "com.test.compact");

    // Switch to COMPACT format before accumulating data
    t.SetTelemetryFormat(TelemetryFormat::COMPACT);

    // Add API method stat data (so SendApiMethodStats calls FormatTelemetryPayload with COMPACT)
    const std::string apiSuccessMetric =
        std::string(AGW_INTERNAL_PLUGIN_PREFIX) + "CompactPlugin_MethodName_compactMethod_Success_split";
    t.RecordTelemetryMetric(ctx, apiSuccessMetric, 18.0, AGW_UNIT_MILLISECONDS);

    // Add health data to also cover compact path in SendHealthStats
    t.IncrementTotalCalls(ctx);

    // Add generic metric data
    t.RecordTelemetryMetric(ctx, "ENTS_INFO_AppGwCompactTest", 5.0, AGW_UNIT_COUNT);

    // Flush → FormatTelemetryPayload called with COMPACT format (lines 1537-1607)
    t.FlushTelemetryData();

    // Restore JSON format
    t.SetTelemetryFormat(TelemetryFormat::JSON);

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 16: RecordTelemetryMetric when NOT initialized → returns ERROR_UNAVAILABLE
//          (ensures the uninitialized path is covered in case other tests don't do it)
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_RecordTelemetryMetric_WhenNotInitialized()
{
    TestResult tr;

    // Force Deinitialize to ensure mInitialized = false
    AppGatewayTelemetry::getInstance().Deinitialize();

    const auto ctx = MakeCtx(170, 170, "com.test.uninit");
    auto rc = AppGatewayTelemetry::getInstance().RecordTelemetryMetric(ctx, AGW_MARKER_TOTAL_CALLS, 1.0, AGW_UNIT_COUNT);
    ExpectEqU32(tr, rc, ERROR_UNAVAILABLE, "RecordTelemetryMetric when not initialized returns ERROR_UNAVAILABLE");

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 17: RecordTelemetryEvent when NOT initialized → returns ERROR_UNAVAILABLE
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_RecordTelemetryEvent_WhenNotInitialized()
{
    TestResult tr;

    // Ensure not initialized (may already be from Test 16)
    AppGatewayTelemetry::getInstance().Deinitialize();

    const auto ctx = MakeCtx(180, 180, "com.test.uninit2");
    auto rc = AppGatewayTelemetry::getInstance().RecordTelemetryEvent(ctx, "ENTS_INFO_AppGwTest", "{}");
    ExpectEqU32(tr, rc, ERROR_UNAVAILABLE, "RecordTelemetryEvent when not initialized returns ERROR_UNAVAILABLE");

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 18: RecordTelemetryMetric with AGW_MARKER_BOOTSTRAP_DURATION
//          Covers lines 929-932 (bootstrap duration handler path)
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_RecordBootstrapDuration()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(190, 190, "com.test.bootstrap");

    // This metric name matches AGW_MARKER_BOOTSTRAP_DURATION ("ENTS_INFO_AppGwBootstrap"),
    // so RecordTelemetryMetric hits the "bootstrap duration" branch (lines 929-932)
    auto rc = t.RecordTelemetryMetric(ctx, AGW_MARKER_BOOTSTRAP_DURATION, 1500.0, "ms");
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordBootstrapDuration returns ERROR_NONE");

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 19: RecordTelemetryEvent with RESPONSE_PAYLOAD_TRACKING and error JSON
//          Covers lines 327-329 (RecordResponse(false) when code+message are set)
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_ResponsePayloadTracking_ErrorResponse()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(191, 191, "com.test.resptrack");

    // Create a map entry so RecordResponse doesn't log "unknown key"
    t.IncrementTotalCalls(ctx);

    // Payload with "code" and "message" makes Core::JSONRPC::Message::Info::Code and
    // Text both IsSet() → hits RecordResponse(context, false) at lines 327-329
    auto rc = t.RecordTelemetryEvent(ctx,
                                     AGW_MARKER_RESPONSE_PAYLOAD_TRACKING,
                                     "{\"code\":1,\"message\":\"internal error\"}");
    ExpectEqU32(tr, rc, ERROR_NONE, "ResponsePayloadTracking error response returns ERROR_NONE");

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 20: RecordTelemetryEvent with AGW_MARKER_PLUGIN_API_ERROR and non-JSON data
//          Covers line 1439 (non-JSON payload falls back to contextPayload["data"])
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_SendT2Event_NonJsonPayload()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(192, 192, "com.test.nonjson");

    // Sending a non-JSON string as event data:
    //   data.FromString(eventData) succeeds but HasLabel("api") is false for non-JSON,
    //   then SendT2Event(marker, eventData, ctx) is called with "not-json-text".
    //   Inside SendT2Event, payloadObj.FromString("not-json-text") fails → line 1439 hit.
    auto rc = t.RecordTelemetryEvent(ctx, AGW_MARKER_PLUGIN_API_ERROR, "not-json-text");
    ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryEvent with non-JSON data returns ERROR_NONE");

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 21: FormatTelemetryPayload COMPACT with float average and boolean field
//          Covers line 1599 (float non-integer value) and line 1602 (BOOLEAN)
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_Compact_FloatAndBoolean()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(193, 193, "com.test.compactfb");

    t.SetTelemetryFormat(TelemetryFormat::COMPACT);

    // Record two values that produce a non-integer average (1.0 + 2.0) / 2 = 1.5
    // This triggers the float branch (line 1599) in FormatTelemetryPayload COMPACT mode
    t.RecordTelemetryMetric(ctx, "ENTS_INFO_AppGwFloatTest", 1.0, AGW_UNIT_COUNT);
    t.RecordTelemetryMetric(ctx, "ENTS_INFO_AppGwFloatTest", 2.0, AGW_UNIT_COUNT);

    // SendT2Event with a boolean in the JSON triggers line 1602 (BOOLEAN branch)
    // AGW_MARKER_PLUGIN_API_ERROR handler parses the JSON, copies all fields into contextPayload
    t.RecordTelemetryEvent(ctx, AGW_MARKER_PLUGIN_API_ERROR,
                           "{\"api\":\"testApi\",\"active\":true}");

    // Flush so FormatTelemetryPayload is called with COMPACT format on the float avg payload
    t.FlushTelemetryData();

    t.SetTelemetryFormat(TelemetryFormat::JSON);

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 22: FormatTelemetryPayload COMPACT with array payload
//          Covers lines 1547-1590 (array handling in COMPACT format)
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_Compact_ArrayPayload()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(194, 194, "com.test.compactarr");

    t.SetTelemetryFormat(TelemetryFormat::COMPACT);

    // Pass an event data JSON that contains an array field.
    // SendT2Event receives the parsed JSON which includes an ARRAY variant.
    // → FormatTelemetryPayload in COMPACT mode hits the ARRAY branch (lines 1547-1590)
    t.RecordTelemetryEvent(ctx, AGW_MARKER_PLUGIN_API_ERROR,
                           "{\"api\":\"testApi\","
                           "\"items\":[{\"k\":\"v1\"},{\"k\":\"v2\"}]}");

    t.SetTelemetryFormat(TelemetryFormat::JSON);

    return tr.failures;
}

// ---------------------------------------------------------------------------
// Test 23: RecordTelemetryEvent - Ripple Log Markers (raw CSV path)
//          Covers the new SendT2Event(const char*, const std::string&) overload
//          for APP_METRICS_ERROR_split, APP_SIGNIN_STATUS_split, APP_READY_split.
//          These markers bypass JSON wrapping and send raw CSV directly to T2.
// ---------------------------------------------------------------------------
uint32_t Test_Telemetry_RecordTelemetryEvent_RippleLogMarkers()
{
    TestResult tr;
    TelemetryGuard guard;

    auto& t = AppGatewayTelemetry::getInstance();
    const auto ctx = MakeCtx(200, 200, "com.test.app");

    // APP_METRICS_ERROR_split: CSV format appId,Code,type,visible,description
    {
        const std::string csvData = "com.test.app,503,network,true,Service unavailable";
        auto rc = t.RecordTelemetryEvent(ctx, AGW_MARKER_APP_METRICS_ERROR, csvData);
        ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryEvent APP_METRICS_ERROR_split returns ERROR_NONE");
    }

    // APP_SIGNIN_STATUS_split: CSV format appId,signInStatus (true = signed in)
    {
        const std::string csvSignIn = "com.test.app,true";
        auto rc = t.RecordTelemetryEvent(ctx, AGW_MARKER_APP_SIGNIN_STATUS, csvSignIn);
        ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryEvent APP_SIGNIN_STATUS_split (signIn) returns ERROR_NONE");
    }

    // APP_SIGNIN_STATUS_split: CSV format appId,signInStatus (false = signed out)
    {
        const std::string csvSignOut = "com.test.app,false";
        auto rc = t.RecordTelemetryEvent(ctx, AGW_MARKER_APP_SIGNIN_STATUS, csvSignOut);
        ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryEvent APP_SIGNIN_STATUS_split (signOut) returns ERROR_NONE");
    }

    // APP_READY_split: CSV format appId
    {
        const std::string csvReady = "com.test.app";
        auto rc = t.RecordTelemetryEvent(ctx, AGW_MARKER_APP_READY, csvReady);
        ExpectEqU32(tr, rc, ERROR_NONE, "RecordTelemetryEvent APP_READY_split returns ERROR_NONE");
    }

    return tr.failures;
}
