#include <iostream>
#include <string>

#include <core/core.h>
#include <plugins/IDispatcher.h>

#include <AppGateway.h>
#include "ServiceMock.h"

#include "helpers/ContextUtils.h"
#include "L0Bootstrap.hpp"
#include "L0Expect.hpp"
#include "L0TestTypes.hpp"

using WPEFramework::Core::ERROR_BAD_REQUEST;
using WPEFramework::Core::ERROR_NONE;
using WPEFramework::Core::ERROR_NOT_SUPPORTED;
using WPEFramework::Core::ERROR_PRIVILIGED_REQUEST;
using WPEFramework::Core::ERROR_UNAVAILABLE;
using WPEFramework::Core::ERROR_UNKNOWN_METHOD;
using WPEFramework::Plugin::AppGateway;
using WPEFramework::PluginHost::IDispatcher;
using WPEFramework::PluginHost::IPlugin;
using L0Test::ExpectEqStr;
using L0Test::ExpectEqU32;
using L0Test::ExpectTrue;
using L0Test::TestResult;

// Prototypes for additional l0test cases implemented in AppGateway_Init_DeinitTests.cpp
extern uint32_t Test_Initialize_WithValidConfig_Succeeds();
extern uint32_t Test_Initialize_Twice_Idempotent();
extern uint32_t Test_Deinitialize_Twice_NoCrash();
extern uint32_t Test_JsonRpc_Registration_And_Unregistration();
extern uint32_t Test_Resolve_HappyPath_JSONRPC();
extern uint32_t Test_Resolve_MissingFields_BadRequest();
extern uint32_t Test_Resolve_ParamsEmpty_DefaultsToEmptyObject();
extern uint32_t Test_Resolve_ImplementationError_Propagates();
extern uint32_t Test_Resolver_Configure_WithBaseOnly_LoadsOK();
extern uint32_t Test_Resolver_Configure_WithOverride_TakesPrecedence();
extern uint32_t Test_Resolver_Resolve_UnknownMethod_ReturnsNotFound();
extern uint32_t Test_Resolver_Resolve_MalformedParams_ReturnsBadRequest();
extern uint32_t Test_Resolver_Configure_InvalidJson_ReturnsError();
// Resolver gap tests (cover ClearResolutions, LoadConfig error paths, CallThunderPlugin, missing-key lookups)
extern uint32_t Test_Resolver_ClearResolutions();
extern uint32_t Test_Resolver_LoadConfig_FileNotFound();
extern uint32_t Test_Resolver_LoadConfig_NoResolutionsKey();
extern uint32_t Test_Resolver_CallThunderPlugin_NullService_EmptyAlias();
extern uint32_t Test_Resolver_LookupMissingKey();

// New Responder behavior tests
extern uint32_t Test_Responder_Register_Unregister_Notifications();
extern uint32_t Test_Responder_Respond_Success_And_Unavailable();
extern uint32_t Test_Responder_Emit_Success_And_Unavailable();
extern uint32_t Test_Responder_Request_Success_And_Unavailable();
extern uint32_t Test_Responder_GetGatewayConnectionContext_Known_And_Unknown();

// New ContextUtils and JSON boundary/round-trip tests
extern uint32_t Test_ContextUtils_BoundaryValues_Conversions();
extern uint32_t Test_Json_Boundary_RequestId_ConnectionId();
extern uint32_t Test_Json_Params_Empty_Equals_EmptyObject();
extern uint32_t Test_Json_EmptyAppId_BadRequest();

// New AppGatewayImplementation branch tests
extern uint32_t Test_AppGatewayImplementation_PermissionGroup_Denied();
extern uint32_t Test_AppGatewayImplementation_PermissionGroup_Allowed_ComRpcDisabled();
extern uint32_t Test_AppGatewayImplementation_EventListen_TriggersNotify();
extern uint32_t Test_AppGatewayImplementation_IncludeContext_Path_Executes();
extern uint32_t Test_AppGatewayImplementation_ComRpc_RequestHandler_ReceivesAdditionalContext();
extern uint32_t Test_AppGatewayImplementation_Configure_NullPaths_ReturnsBadRequest();
extern uint32_t Test_AppGatewayImplementation_Configure_EmptyPaths_ReturnsBadRequest();
extern uint32_t Test_AppGatewayImplementation_Authenticator_CheckPermission_Fails();
extern uint32_t Test_AppGatewayImplementation_ComRpc_HandleRequestFails();

// Targeted AppGatewayResponderImplementation coverage tests
extern uint32_t Test_AppGatewayResponderImplementation_Register_Unregister_And_CallbackDelivery();
extern uint32_t Test_AppGatewayResponderImplementation_GetGatewayConnectionContext_EnvInjection_And_EmptyKey();
extern uint32_t Test_AppGatewayResponderImplementation_RecordGatewayConnectionContext_DebugOps();
extern uint32_t Test_AppGatewayResponderImplementation_Configure_And_Public_Methods_NoCrash();

// Hibernation / traffic-gate coverage tests (RDKEMW-19304)
extern uint32_t Test_Responder_QueryInterface_SessionGuard();
extern uint32_t Test_Responder_SuspendTraffic_And_ResumeTraffic_ReturnNone();
extern uint32_t Test_Responder_Respond_DropsWhilePaused_ResumesAfterResume();
extern uint32_t Test_Responder_Emit_DropsWhilePaused_ResumesAfterResume();
extern uint32_t Test_Responder_Request_ReturnsNone_WhilePaused();
extern uint32_t Test_Responder_SuspendResume_MultipleApps_Independent();
extern uint32_t Test_Responder_SessionGuard_Via_Interface_Pointer();

// AppGatewayTelemetry coverage tests
extern uint32_t Test_Telemetry_SettersAndConfig();
extern uint32_t Test_Telemetry_HealthStatCounters_And_RecordResponse();
extern uint32_t Test_Telemetry_HandleHealthStatsMarkers();
extern uint32_t Test_Telemetry_RecordTelemetryMetric_ApiMethodParse();
extern uint32_t Test_Telemetry_RecordTelemetryMetric_ApiLatencyParse();
extern uint32_t Test_Telemetry_RecordTelemetryMetric_ServiceLatencyParse();
extern uint32_t Test_Telemetry_RecordTelemetryMetric_ServiceMethodParse();
extern uint32_t Test_Telemetry_RecordTelemetryEvent_ApiError();
extern uint32_t Test_Telemetry_RecordTelemetryEvent_ExtServiceError();
extern uint32_t Test_Telemetry_RecordTelemetryEvent_NonImmediate_CacheThreshold();
extern uint32_t Test_Telemetry_Flush_WithHealthData();
extern uint32_t Test_Telemetry_Flush_WithApiAndServiceStats();
extern uint32_t Test_Telemetry_Flush_WithApiAndServiceErrorCounts();
extern uint32_t Test_Telemetry_Flush_WithAggregatedMetrics();
extern uint32_t Test_Telemetry_FormatTelemetryPayload_CompactFormat();
extern uint32_t Test_Telemetry_RecordTelemetryMetric_WhenNotInitialized();
extern uint32_t Test_Telemetry_RecordTelemetryEvent_WhenNotInitialized();
// New telemetry coverage tests
extern uint32_t Test_Telemetry_RecordBootstrapDuration();
extern uint32_t Test_Telemetry_ResponsePayloadTracking_ErrorResponse();
extern uint32_t Test_Telemetry_SendT2Event_NonJsonPayload();
extern uint32_t Test_Telemetry_Compact_FloatAndBoolean();
extern uint32_t Test_Telemetry_Compact_ArrayPayload();
extern uint32_t Test_Telemetry_RecordTelemetryEvent_RippleLogMarkers();
// New AppGatewayImplementation coverage tests
extern uint32_t Test_AppGatewayImplementation_Event_MissingListenParam();
extern uint32_t Test_AppGatewayImplementation_UpdateContext_NonJsonParams();
extern uint32_t Test_AppGatewayImplementation_ComRpc_AdditionalContext_NotObject();
extern uint32_t Test_AppGatewayImplementation_Resolve_BeforeShellConfigure();
extern uint32_t Test_AppGatewayImplementation_Resolve_NotConfigured();
extern uint32_t Test_AppGatewayImplementation_RegionalConfig();

// Direct-access coverage tests (use #define private public to reach private Send* and FlushJob methods)
extern uint32_t Test_Telemetry_DirectAccess_AlreadyInitialized();
extern uint32_t Test_Telemetry_DirectAccess_SendHealthStats_Empty();
extern uint32_t Test_Telemetry_DirectAccess_SendHealthStats_WithData();
extern uint32_t Test_Telemetry_DirectAccess_SendApiErrorStats_Empty();
extern uint32_t Test_Telemetry_DirectAccess_SendApiErrorStats_WithData();
extern uint32_t Test_Telemetry_DirectAccess_SendExternalServiceErrorStats_Empty();
extern uint32_t Test_Telemetry_DirectAccess_SendExternalServiceErrorStats_WithData();
extern uint32_t Test_Telemetry_DirectAccess_SendAggregatedMetrics_Empty();
extern uint32_t Test_Telemetry_DirectAccess_SendAggregatedMetrics_WithData();
extern uint32_t Test_Telemetry_DirectAccess_SendApiMethodStats_Empty();
extern uint32_t Test_Telemetry_DirectAccess_SendApiMethodStats_WithData();
extern uint32_t Test_Telemetry_DirectAccess_SendApiLatencyStats_Empty();
extern uint32_t Test_Telemetry_DirectAccess_SendApiLatencyStats_WithData();
extern uint32_t Test_Telemetry_DirectAccess_SendServiceLatencyStats_Empty();
extern uint32_t Test_Telemetry_DirectAccess_SendServiceLatencyStats_WithData();
extern uint32_t Test_Telemetry_DirectAccess_SendServiceMethodStats_Empty();
extern uint32_t Test_Telemetry_DirectAccess_SendServiceMethodStats_WithData();
extern uint32_t Test_Telemetry_DirectAccess_FlushJob_Dispatch_WithData();
extern uint32_t Test_Telemetry_DirectAccess_FlushJob_Dispatch_Empty();
extern uint32_t Test_Telemetry_DirectAccess_FlushJob_Dispatch_NullParent();
extern uint32_t Test_Telemetry_DirectAccess_FlushJob_Dispatch_CompactFormat();
extern uint32_t Test_Telemetry_DirectAccess_ParseApiMetricName_WrongPrefix();
extern uint32_t Test_Telemetry_DirectAccess_ParseApiMetricName_NoMethodTag();
extern uint32_t Test_Telemetry_DirectAccess_ParseApiLatencyMetricName_WrongSuffix();

namespace {

struct PluginAndService {
    L0Test::ServiceMock* service { nullptr };
    IPlugin* plugin { nullptr };

    explicit PluginAndService(const L0Test::ServiceMock::Config& cfg = {})
        : service(new L0Test::ServiceMock(cfg, true))
        , plugin(WPEFramework::Core::Service<AppGateway>::Create<IPlugin>())
    {
    }

    ~PluginAndService()
    {
        if (plugin != nullptr) {
            plugin->Release();
            plugin = nullptr;
        }
        if (service != nullptr) {
            service->Release();
            service = nullptr;
        }
    }
};

static std::string ResolveParamsJson(const std::string& method, const std::string& paramsJson = "{}")
{
    // The real JSON-RPC contract (see plugin/AppGateway/tests/CurlCmds.md) is:
    //   {
    //     "context": { "requestId":..., "connectionId":..., "appId":"..." },
    //     "method": "Some.Method",
    //     "params": { ... }   // JSON object/value, NOT a JSON-encoded string
    //   }
    //
    // If we send "params" as a quoted string, Thunder's JSON parser expects '{' or 'null' but sees '\"',
    // producing: Invalid value. "null" or "{" expected.
    const std::string effectiveParams = paramsJson.empty() ? "{}" : paramsJson;

    return std::string("{")
        + "\"context\":{"
        +   "\"requestId\":1001,"
        +   "\"connectionId\":10,"
        +   "\"appId\":\"com.example.test\""
        + "},"
        + "\"origin\":\"org.rdk.AppGateway\","
        + "\"method\":\"" + method + "\","
        + "\"params\":" + effectiveParams
        + "}";
}

} // namespace

static uint32_t Test_Initialize_Deinitialize_HappyPath()
{
    TestResult tr;
    PluginAndService ps;

    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectEqStr(tr, initResult, "", "Initialize() happy path returns empty string");

    auto dispatcher = ps.plugin->QueryInterface<IDispatcher>();
    ExpectTrue(tr, dispatcher != nullptr, "IDispatcher available after Initialize()");
    if (dispatcher != nullptr) {
        dispatcher->Release();
    }

    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

static uint32_t Test_JsonRpcResolve_Success()
{
    TestResult tr;
    PluginAndService ps;

    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectEqStr(tr, initResult, "", "Initialize() succeeds for JSON-RPC resolve test");

    auto dispatcher = ps.plugin->QueryInterface<IDispatcher>();
    ExpectTrue(tr, dispatcher != nullptr, "IDispatcher available");
    if (dispatcher != nullptr) {
        const std::string paramsJson = ResolveParamsJson("dummy.method", "{}");

        std::string jsonResponse;
        const uint32_t rc = dispatcher->Invoke(nullptr, 0, 0, "", "resolve", paramsJson, jsonResponse);

        ExpectEqU32(tr, rc, ERROR_UNKNOWN_METHOD, "JSON-RPC resolve returns ERROR_UNKNOWN_METHOD (resolve is not JSON-RPC exposed)");

        dispatcher->Release();
    }

    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

static uint32_t Test_DirectResolver_Success()
{
    TestResult tr;
    PluginAndService ps;

    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectEqStr(tr, initResult, "", "Initialize() succeeds for direct resolver test");

    auto* resolver = static_cast<WPEFramework::Exchange::IAppGatewayResolver*>(
        ps.plugin->QueryInterface(WPEFramework::Exchange::IAppGatewayResolver::ID));

    ExpectTrue(tr, resolver != nullptr, "Aggregate IAppGatewayResolver is available via QueryInterface(ID)");
    if (resolver != nullptr) {
        WPEFramework::Exchange::GatewayContext ctx;
        ctx.requestId = 42;
        ctx.connectionId = 7;
        ctx.appId = "com.example.test";

        std::string resolution;
        const uint32_t rc = resolver->Resolve(ctx, "org.rdk.AppGateway", "dummy.method", "{}", resolution);

        ExpectEqU32(tr, rc, WPEFramework::Core::ERROR_GENERAL, "Direct Resolve() returns ERROR_GENERAL for unknown alias in current config");
        ExpectTrue(tr, resolution.find("NotSupported") != std::string::npos, "Direct Resolve() returns NotSupported payload");

        resolver->Release();
    }

    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

static uint32_t Test_JsonRpcResolve_Error_NotPermitted()
{
    TestResult tr;
    PluginAndService ps;

    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectEqStr(tr, initResult, "", "Initialize() succeeds for NotPermitted test");

    auto dispatcher = ps.plugin->QueryInterface<IDispatcher>();
    ExpectTrue(tr, dispatcher != nullptr, "IDispatcher available");
    if (dispatcher != nullptr) {
        const std::string paramsJson = ResolveParamsJson("l0.notPermitted", "{}");
        std::string jsonResponse;
        const uint32_t rc = dispatcher->Invoke(nullptr, 0, 0, "", "resolve", paramsJson, jsonResponse);

        ExpectEqU32(tr, rc, ERROR_UNKNOWN_METHOD, "NotPermitted JSON-RPC path returns ERROR_UNKNOWN_METHOD");
        dispatcher->Release();
    }

    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

static uint32_t Test_JsonRpcResolve_Error_NotSupported()
{
    TestResult tr;
    PluginAndService ps;

    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectEqStr(tr, initResult, "", "Initialize() succeeds for NotSupported test");

    auto dispatcher = ps.plugin->QueryInterface<IDispatcher>();
    ExpectTrue(tr, dispatcher != nullptr, "IDispatcher available");
    if (dispatcher != nullptr) {
        const std::string paramsJson = ResolveParamsJson("l0.notSupported", "{}");
        std::string jsonResponse;
        const uint32_t rc = dispatcher->Invoke(nullptr, 0, 0, "", "resolve", paramsJson, jsonResponse);

        ExpectEqU32(tr, rc, ERROR_UNKNOWN_METHOD, "NotSupported JSON-RPC path returns ERROR_UNKNOWN_METHOD");
        dispatcher->Release();
    }

    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

static uint32_t Test_JsonRpcResolve_Error_NotAvailable()
{
    TestResult tr;
    PluginAndService ps;

    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectEqStr(tr, initResult, "", "Initialize() succeeds for NotAvailable test");

    auto dispatcher = ps.plugin->QueryInterface<IDispatcher>();
    ExpectTrue(tr, dispatcher != nullptr, "IDispatcher available");
    if (dispatcher != nullptr) {
        const std::string paramsJson = ResolveParamsJson("l0.notAvailable", "{}");
        std::string jsonResponse;
        const uint32_t rc = dispatcher->Invoke(nullptr, 0, 0, "", "resolve", paramsJson, jsonResponse);

        ExpectEqU32(tr, rc, ERROR_UNKNOWN_METHOD, "NotAvailable JSON-RPC path returns ERROR_UNKNOWN_METHOD");
        dispatcher->Release();
    }

    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

static uint32_t Test_JsonRpcResolve_Error_MalformedInput()
{
    TestResult tr;
    PluginAndService ps;

    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectEqStr(tr, initResult, "", "Initialize() succeeds for malformed input test");

    auto dispatcher = ps.plugin->QueryInterface<IDispatcher>();
    ExpectTrue(tr, dispatcher != nullptr, "IDispatcher available");
    if (dispatcher != nullptr) {
        const std::string badJson = "{ this is not valid json }";
        std::string jsonResponse;
        const uint32_t rc = dispatcher->Invoke(nullptr, 0, 0, "", "resolve", badJson, jsonResponse);

        ExpectEqU32(tr, rc, ERROR_UNKNOWN_METHOD, "Malformed JSON for resolve returns ERROR_UNKNOWN_METHOD");
        dispatcher->Release();
    }

    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

static uint32_t Test_JsonRpcResolve_Error_MissingHandler_WhenResolverMissing()
{
    TestResult tr;

    // Provide no resolver (so JAppGatewayResolver::Register is never called).
    // Provide no responder as well to keep Initialize in a deterministic failure state.
    PluginAndService ps(L0Test::ServiceMock::Config(false, false));

    const std::string initResult = ps.plugin->Initialize(ps.service);
    (void)initResult;

    auto dispatcher = ps.plugin->QueryInterface<IDispatcher>();
    ExpectTrue(tr, dispatcher != nullptr, "IDispatcher is still available even if Initialize() failed");
    if (dispatcher != nullptr) {
        const std::string paramsJson = ResolveParamsJson("dummy.method", "{}");
        std::string jsonResponse;
        const uint32_t rc = dispatcher->Invoke(nullptr, 0, 0, "", "resolve", paramsJson, jsonResponse);

        // In the current interface contract resolve is not exposed over JSON-RPC.
        ExpectEqU32(tr, rc, ERROR_UNKNOWN_METHOD, "Missing resolve handler returns ERROR_UNKNOWN_METHOD");
        dispatcher->Release();
    }

    // Deinitialize still cleans up mService reference acquired by Initialize().
    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

static uint32_t Test_ContextUtils_Conversions()
{
    TestResult tr;

    WPEFramework::Exchange::GatewayContext gw;
    gw.requestId = 7;
    gw.connectionId = 11;
    gw.appId = "app.id";

    const std::string origin = "org.rdk.AppGateway";

    auto notif = ContextUtils::ConvertAppGatewayToNotificationContext(gw, origin);
    ExpectEqU32(tr, notif.requestId, gw.requestId, "NotificationContext.requestId matches");
    ExpectEqU32(tr, notif.connectionId, gw.connectionId, "NotificationContext.connectionId matches");
    ExpectEqStr(tr, notif.appId, gw.appId, "NotificationContext.appId matches");
    ExpectEqStr(tr, notif.origin, origin, "NotificationContext.origin matches");

    auto gw2 = ContextUtils::ConvertNotificationToAppGatewayContext(notif);
    ExpectEqU32(tr, gw2.requestId, gw.requestId, "ConvertNotificationToAppGatewayContext.requestId");
    ExpectEqU32(tr, gw2.connectionId, gw.connectionId, "ConvertNotificationToAppGatewayContext.connectionId");
    ExpectEqStr(tr, gw2.appId, gw.appId, "ConvertNotificationToAppGatewayContext.appId");

    // auto provider = ContextUtils::ConvertAppGatewayToProviderContext(gw, origin);
    // ExpectEqU32(tr, static_cast<uint32_t>(provider.requestId), gw.requestId, "Provider.Context.requestId matches (int in stub)");
    // ExpectEqU32(tr, provider.connectionId, gw.connectionId, "Provider.Context.connectionId matches");
    // ExpectEqStr(tr, provider.appId, gw.appId, "Provider.Context.appId matches");
    // ExpectEqStr(tr, provider.origin, origin, "Provider.Context.origin matches");

    // auto gw3 = ContextUtils::ConvertProviderToAppGatewayContext(provider);
    // ExpectEqU32(tr, gw3.requestId, gw.requestId, "ConvertProviderToAppGatewayContext.requestId");
    // ExpectEqU32(tr, gw3.connectionId, gw.connectionId, "ConvertProviderToAppGatewayContext.connectionId");
    // ExpectEqStr(tr, gw3.appId, gw.appId, "ConvertProviderToAppGatewayContext.appId");
    return tr.failures;
}

int main()
{
    // Shared L0 bootstrap for WorkerPool lifecycle.
    L0Test::L0BootstrapGuard bootstrap;

    // Run instructions (from repo root):
    //   export LD_LIBRARY_PATH=$PWD/dependencies/install/lib:$PWD/build/appgatewayl0test/AppGateway:$LD_LIBRARY_PATH
    //   ./build/appgatewayl0test/AppGateway/appgateway_l0test

    struct Case {
        const char* name;
        uint32_t (*fn)();
    };

    const Case cases[] = {
        // New Init/Deinit lifecycle tests
        { "Initialize_WithValidConfig_Succeeds", Test_Initialize_WithValidConfig_Succeeds },
        { "Initialize_Twice_Idempotent", Test_Initialize_Twice_Idempotent },
        { "Deinitialize_Twice_NoCrash", Test_Deinitialize_Twice_NoCrash },
        { "JsonRpc_Registration_And_Unregistration", Test_JsonRpc_Registration_And_Unregistration },
        { "Resolve_HappyPath_JSONRPC", Test_Resolve_HappyPath_JSONRPC },
        { "Resolve_MissingFields_BadRequest", Test_Resolve_MissingFields_BadRequest },
        { "Resolve_ParamsEmpty_DefaultsToEmptyObject", Test_Resolve_ParamsEmpty_DefaultsToEmptyObject },
        { "Resolve_ImplementationError_Propagates", Test_Resolve_ImplementationError_Propagates },

        // Existing tests
        { "Initialize_Deinitialize_HappyPath", Test_Initialize_Deinitialize_HappyPath },
        { "JsonRpcResolve_Success", Test_JsonRpcResolve_Success },
        { "DirectResolver_Success", Test_DirectResolver_Success },
        { "JsonRpcResolve_Error_NotPermitted", Test_JsonRpcResolve_Error_NotPermitted },
        { "JsonRpcResolve_Error_NotSupported", Test_JsonRpcResolve_Error_NotSupported },
        { "JsonRpcResolve_Error_NotAvailable", Test_JsonRpcResolve_Error_NotAvailable },
        { "JsonRpcResolve_Error_MalformedInput", Test_JsonRpcResolve_Error_MalformedInput },
        { "JsonRpcResolve_Error_MissingHandler_WhenResolverMissing", Test_JsonRpcResolve_Error_MissingHandler_WhenResolverMissing },
        { "ContextUtils_Conversions", Test_ContextUtils_Conversions },

        // New Resolver Configure/Resolve tests
        { "Resolver_Configure_WithBaseOnly_LoadsOK", Test_Resolver_Configure_WithBaseOnly_LoadsOK },
        { "Resolver_Configure_WithOverride_TakesPrecedence", Test_Resolver_Configure_WithOverride_TakesPrecedence },
        { "Resolver_Resolve_UnknownMethod_ReturnsNotFound", Test_Resolver_Resolve_UnknownMethod_ReturnsNotFound },
        { "Resolver_Resolve_MalformedParams_ReturnsBadRequest", Test_Resolver_Resolve_MalformedParams_ReturnsBadRequest },
        { "Resolver_Configure_InvalidJson_ReturnsError", Test_Resolver_Configure_InvalidJson_ReturnsError },
        { "Resolver_ClearResolutions", Test_Resolver_ClearResolutions },
        { "Resolver_LoadConfig_FileNotFound", Test_Resolver_LoadConfig_FileNotFound },
        { "Resolver_LoadConfig_NoResolutionsKey", Test_Resolver_LoadConfig_NoResolutionsKey },
        { "Resolver_CallThunderPlugin_NullService_EmptyAlias", Test_Resolver_CallThunderPlugin_NullService_EmptyAlias },
        { "Resolver_LookupMissingKey", Test_Resolver_LookupMissingKey },

        // New Responder behavior tests
        { "Responder_Register_Unregister_Notifications", Test_Responder_Register_Unregister_Notifications },
        { "Responder_Respond_Success_And_Unavailable", Test_Responder_Respond_Success_And_Unavailable },
        { "Responder_Emit_Success_And_Unavailable", Test_Responder_Emit_Success_And_Unavailable },
        { "Responder_Request_Success_And_Unavailable", Test_Responder_Request_Success_And_Unavailable },
        { "Responder_GetGatewayConnectionContext_Known_And_Unknown", Test_Responder_GetGatewayConnectionContext_Known_And_Unknown },

        // New ContextUtils conversion and JSON boundary tests
        { "ContextUtils_BoundaryValues_Conversions", Test_ContextUtils_BoundaryValues_Conversions },
        { "Json_Boundary_RequestId_ConnectionId", Test_Json_Boundary_RequestId_ConnectionId },
        { "Json_Params_Empty_Equals_EmptyObject", Test_Json_Params_Empty_Equals_EmptyObject },
        { "Json_EmptyAppId_BadRequest", Test_Json_EmptyAppId_BadRequest },

        // New AppGatewayImplementation branch/coverage tests
        { "AppGatewayImplementation_PermissionGroup_Denied", Test_AppGatewayImplementation_PermissionGroup_Denied },
        { "AppGatewayImplementation_PermissionGroup_Allowed_ComRpcDisabled", Test_AppGatewayImplementation_PermissionGroup_Allowed_ComRpcDisabled },
        { "AppGatewayImplementation_EventListen_TriggersNotify", Test_AppGatewayImplementation_EventListen_TriggersNotify },
        { "AppGatewayImplementation_IncludeContext_Path_Executes", Test_AppGatewayImplementation_IncludeContext_Path_Executes },
        { "AppGatewayImplementation_ComRpc_RequestHandler_ReceivesAdditionalContext", Test_AppGatewayImplementation_ComRpc_RequestHandler_ReceivesAdditionalContext },
        { "AppGatewayImplementation_Configure_NullPaths_ReturnsBadRequest", Test_AppGatewayImplementation_Configure_NullPaths_ReturnsBadRequest },
        { "AppGatewayImplementation_Configure_EmptyPaths_ReturnsBadRequest", Test_AppGatewayImplementation_Configure_EmptyPaths_ReturnsBadRequest },
        { "AppGatewayImplementation_Authenticator_CheckPermission_Fails", Test_AppGatewayImplementation_Authenticator_CheckPermission_Fails },
        { "AppGatewayImplementation_ComRpc_HandleRequestFails", Test_AppGatewayImplementation_ComRpc_HandleRequestFails },

        // Targeted AppGatewayResponderImplementation coverage tests
        { "AppGatewayResponderImplementation_Register_Unregister_And_CallbackDelivery", Test_AppGatewayResponderImplementation_Register_Unregister_And_CallbackDelivery },
        { "AppGatewayResponderImplementation_GetGatewayConnectionContext_EnvInjection_And_EmptyKey", Test_AppGatewayResponderImplementation_GetGatewayConnectionContext_EnvInjection_And_EmptyKey },
        { "AppGatewayResponderImplementation_RecordGatewayConnectionContext_DebugOps", Test_AppGatewayResponderImplementation_RecordGatewayConnectionContext_DebugOps },
        { "AppGatewayResponderImplementation_Configure_And_Public_Methods_NoCrash", Test_AppGatewayResponderImplementation_Configure_And_Public_Methods_NoCrash },

        // Hibernation / traffic-gate coverage tests (RDKEMW-19304)
        { "Responder_QueryInterface_SessionGuard", Test_Responder_QueryInterface_SessionGuard },
        { "Responder_SuspendTraffic_And_ResumeTraffic_ReturnNone", Test_Responder_SuspendTraffic_And_ResumeTraffic_ReturnNone },
        { "Responder_Respond_DropsWhilePaused_ResumesAfterResume", Test_Responder_Respond_DropsWhilePaused_ResumesAfterResume },
        { "Responder_Emit_DropsWhilePaused_ResumesAfterResume", Test_Responder_Emit_DropsWhilePaused_ResumesAfterResume },
        { "Responder_Request_ReturnsNone_WhilePaused", Test_Responder_Request_ReturnsNone_WhilePaused },
        { "Responder_SuspendResume_MultipleApps_Independent", Test_Responder_SuspendResume_MultipleApps_Independent },
        { "Responder_SessionGuard_Via_Interface_Pointer", Test_Responder_SessionGuard_Via_Interface_Pointer },

        // AppGatewayTelemetry coverage tests
        { "Telemetry_SettersAndConfig", Test_Telemetry_SettersAndConfig },
        { "Telemetry_HealthStatCounters_And_RecordResponse", Test_Telemetry_HealthStatCounters_And_RecordResponse },
        { "Telemetry_HandleHealthStatsMarkers", Test_Telemetry_HandleHealthStatsMarkers },
        { "Telemetry_RecordTelemetryMetric_ApiMethodParse", Test_Telemetry_RecordTelemetryMetric_ApiMethodParse },
        { "Telemetry_RecordTelemetryMetric_ApiLatencyParse", Test_Telemetry_RecordTelemetryMetric_ApiLatencyParse },
        { "Telemetry_RecordTelemetryMetric_ServiceLatencyParse", Test_Telemetry_RecordTelemetryMetric_ServiceLatencyParse },
        { "Telemetry_RecordTelemetryMetric_ServiceMethodParse", Test_Telemetry_RecordTelemetryMetric_ServiceMethodParse },
        { "Telemetry_RecordTelemetryEvent_ApiError", Test_Telemetry_RecordTelemetryEvent_ApiError },
        { "Telemetry_RecordTelemetryEvent_ExtServiceError", Test_Telemetry_RecordTelemetryEvent_ExtServiceError },
        { "Telemetry_RecordTelemetryEvent_NonImmediate_CacheThreshold", Test_Telemetry_RecordTelemetryEvent_NonImmediate_CacheThreshold },
        { "Telemetry_Flush_WithHealthData", Test_Telemetry_Flush_WithHealthData },
        { "Telemetry_Flush_WithApiAndServiceStats", Test_Telemetry_Flush_WithApiAndServiceStats },
        { "Telemetry_Flush_WithApiAndServiceErrorCounts", Test_Telemetry_Flush_WithApiAndServiceErrorCounts },
        { "Telemetry_Flush_WithAggregatedMetrics", Test_Telemetry_Flush_WithAggregatedMetrics },
        { "Telemetry_FormatTelemetryPayload_CompactFormat", Test_Telemetry_FormatTelemetryPayload_CompactFormat },
        { "Telemetry_RecordTelemetryMetric_WhenNotInitialized", Test_Telemetry_RecordTelemetryMetric_WhenNotInitialized },
        { "Telemetry_RecordTelemetryEvent_WhenNotInitialized", Test_Telemetry_RecordTelemetryEvent_WhenNotInitialized },
        // New telemetry coverage tests
        { "Telemetry_RecordBootstrapDuration", Test_Telemetry_RecordBootstrapDuration },
        { "Telemetry_ResponsePayloadTracking_ErrorResponse", Test_Telemetry_ResponsePayloadTracking_ErrorResponse },
        { "Telemetry_SendT2Event_NonJsonPayload", Test_Telemetry_SendT2Event_NonJsonPayload },
        { "Telemetry_Compact_FloatAndBoolean", Test_Telemetry_Compact_FloatAndBoolean },
        { "Telemetry_Compact_ArrayPayload", Test_Telemetry_Compact_ArrayPayload },
        { "Telemetry_RecordTelemetryEvent_RippleLogMarkers", Test_Telemetry_RecordTelemetryEvent_RippleLogMarkers },
        // New AppGatewayImplementation coverage tests
        { "AppGatewayImplementation_Event_MissingListenParam", Test_AppGatewayImplementation_Event_MissingListenParam },
        { "AppGatewayImplementation_UpdateContext_NonJsonParams", Test_AppGatewayImplementation_UpdateContext_NonJsonParams },
        { "AppGatewayImplementation_ComRpc_AdditionalContext_NotObject", Test_AppGatewayImplementation_ComRpc_AdditionalContext_NotObject },
        { "AppGatewayImplementation_Resolve_BeforeShellConfigure", Test_AppGatewayImplementation_Resolve_BeforeShellConfigure },
        { "AppGatewayImplementation_Resolve_NotConfigured", Test_AppGatewayImplementation_Resolve_NotConfigured },
        { "AppGatewayImplementation_RegionalConfig", Test_AppGatewayImplementation_RegionalConfig },

        // Direct-access coverage tests (private Send* and FlushJob methods)
        { "Telemetry_DirectAccess_AlreadyInitialized", Test_Telemetry_DirectAccess_AlreadyInitialized },
        { "Telemetry_DirectAccess_SendHealthStats_Empty", Test_Telemetry_DirectAccess_SendHealthStats_Empty },
        { "Telemetry_DirectAccess_SendHealthStats_WithData", Test_Telemetry_DirectAccess_SendHealthStats_WithData },
        { "Telemetry_DirectAccess_SendApiErrorStats_Empty", Test_Telemetry_DirectAccess_SendApiErrorStats_Empty },
        { "Telemetry_DirectAccess_SendApiErrorStats_WithData", Test_Telemetry_DirectAccess_SendApiErrorStats_WithData },
        { "Telemetry_DirectAccess_SendExternalServiceErrorStats_Empty", Test_Telemetry_DirectAccess_SendExternalServiceErrorStats_Empty },
        { "Telemetry_DirectAccess_SendExternalServiceErrorStats_WithData", Test_Telemetry_DirectAccess_SendExternalServiceErrorStats_WithData },
        { "Telemetry_DirectAccess_SendAggregatedMetrics_Empty", Test_Telemetry_DirectAccess_SendAggregatedMetrics_Empty },
        { "Telemetry_DirectAccess_SendAggregatedMetrics_WithData", Test_Telemetry_DirectAccess_SendAggregatedMetrics_WithData },
        { "Telemetry_DirectAccess_SendApiMethodStats_Empty", Test_Telemetry_DirectAccess_SendApiMethodStats_Empty },
        { "Telemetry_DirectAccess_SendApiMethodStats_WithData", Test_Telemetry_DirectAccess_SendApiMethodStats_WithData },
        { "Telemetry_DirectAccess_SendApiLatencyStats_Empty", Test_Telemetry_DirectAccess_SendApiLatencyStats_Empty },
        { "Telemetry_DirectAccess_SendApiLatencyStats_WithData", Test_Telemetry_DirectAccess_SendApiLatencyStats_WithData },
        { "Telemetry_DirectAccess_SendServiceLatencyStats_Empty", Test_Telemetry_DirectAccess_SendServiceLatencyStats_Empty },
        { "Telemetry_DirectAccess_SendServiceLatencyStats_WithData", Test_Telemetry_DirectAccess_SendServiceLatencyStats_WithData },
        { "Telemetry_DirectAccess_SendServiceMethodStats_Empty", Test_Telemetry_DirectAccess_SendServiceMethodStats_Empty },
        { "Telemetry_DirectAccess_SendServiceMethodStats_WithData", Test_Telemetry_DirectAccess_SendServiceMethodStats_WithData },
        { "Telemetry_DirectAccess_FlushJob_Dispatch_WithData", Test_Telemetry_DirectAccess_FlushJob_Dispatch_WithData },
        { "Telemetry_DirectAccess_FlushJob_Dispatch_Empty", Test_Telemetry_DirectAccess_FlushJob_Dispatch_Empty },
        { "Telemetry_DirectAccess_FlushJob_Dispatch_NullParent", Test_Telemetry_DirectAccess_FlushJob_Dispatch_NullParent },
        { "Telemetry_DirectAccess_FlushJob_Dispatch_CompactFormat", Test_Telemetry_DirectAccess_FlushJob_Dispatch_CompactFormat },
        { "Telemetry_DirectAccess_ParseApiMetricName_WrongPrefix", Test_Telemetry_DirectAccess_ParseApiMetricName_WrongPrefix },
        { "Telemetry_DirectAccess_ParseApiMetricName_NoMethodTag", Test_Telemetry_DirectAccess_ParseApiMetricName_NoMethodTag },
        { "Telemetry_DirectAccess_ParseApiLatencyMetricName_WrongSuffix", Test_Telemetry_DirectAccess_ParseApiLatencyMetricName_WrongSuffix },
    };

    uint32_t failures = 0;

    for (const auto& c : cases) {
        std::cerr << "[ RUN      ] " << c.name << std::endl;
        const uint32_t f = c.fn();
        if (f == 0) {
            std::cerr << "[       OK ] " << c.name << std::endl;
        } else {
            std::cerr << "[  FAILED  ] " << c.name << " failures=" << f << std::endl;
        }
        failures += f;
    }

    if (failures == 0) {
        L0Test::PrintTotals(std::cout, "AppGateway l0test", failures);
    } else {
        L0Test::PrintTotals(std::cerr, "AppGateway l0test", failures);
    }
    return L0Test::ResultToExitCode(failures);
}
