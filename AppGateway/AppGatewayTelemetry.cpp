/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management.
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

#include "AppGatewayTelemetry.h"
#include "UtilsLogging.h"
#include "UtilsTelemetry.h"
#include <limits>
#include <sstream>
#include <iomanip>
#include <functional>
#include <unordered_map>

namespace WPEFramework {
namespace Plugin {

    AppGatewayTelemetry& AppGatewayTelemetry::getInstance()
    {
        static Core::ProxyType<AppGatewayTelemetry> instance = Core::ProxyType<AppGatewayTelemetry>::Create();
        ASSERT(instance.IsValid());
        return *instance;
    }

    AppGatewayTelemetry::AppGatewayTelemetry()
        : mService(nullptr)
        , mReportingIntervalSec(TELEMETRY_DEFAULT_REPORTING_INTERVAL_SEC)
        , mCacheThreshold(TELEMETRY_DEFAULT_CACHE_THRESHOLD)
        , mTelemetryFormat(TelemetryFormat::JSON)  // Default to JSON format
        , mTimer(Core::ProxyType<TelemetryTimer>::Create(this))
        , mTimerHandler(1024 * 64, _T("AppGwTelemetryTimer"))
        , mTimerRunning(false)
        , mCachedEventCount(0)
        , mInitialized(false)
    {
        LOGTRACE("AppGatewayTelemetry constructor");
    }

    AppGatewayTelemetry::~AppGatewayTelemetry()
    {
        LOGTRACE("AppGatewayTelemetry destructor");
        Deinitialize();
    }

    void AppGatewayTelemetry::Initialize(PluginHost::IShell* service)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);

        if (mInitialized) {
            LOGWARN("AppGatewayTelemetry already initialized");
            return;
        }

        mService = service;
        mReportingStartTime = std::chrono::steady_clock::now();

        // Telemetry initialization is handled by PowerManager for all Thunder plugins,
        // including AppGateway. Avoid calling init() here to prevent multiple initializations.
        // Utils::Telemetry::init();

        // Start the periodic reporting timer
        if (!mTimerRunning) {
            uint64_t intervalMs = static_cast<uint64_t>(mReportingIntervalSec) * 1000;
            mTimerHandler.Schedule(Core::Time::Now().Add(intervalMs), *mTimer);
            mTimerRunning = true;
            LOGINFO("AppGatewayTelemetry: Started periodic reporting timer with interval %u seconds", mReportingIntervalSec);
        }

        mInitialized = true;
        LOGTRACE("AppGatewayTelemetry initialized successfully");
    }

    void AppGatewayTelemetry::Deinitialize()
    {
        {
            Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);

            if (!mInitialized) {
                return;
            }

            // Stop the timer
            if (mTimerRunning) {
                mTimerHandler.Revoke(*mTimer);
                mTimerRunning = false;
            }
            
            // Mark as not initialized to prevent new telemetry recording
            mInitialized = false;
        }
        // Lock released before flushing to avoid deadlock

        LOGINFO("AppGatewayTelemetry: Flushing final telemetry data on shutdown");

        // Flush any remaining telemetry data synchronously
        FlushTelemetryData();

        {
            Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
            mService = nullptr;
        }

        LOGTRACE("AppGatewayTelemetry deinitialized");
    }

    void AppGatewayTelemetry::SetReportingInterval(uint32_t intervalSec)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
        mReportingIntervalSec = intervalSec;
        LOGTRACE("AppGatewayTelemetry: Reporting interval set to %u seconds", intervalSec);

        // Restart timer with new interval if running
        if (mTimerRunning) {
            mTimerHandler.Revoke(*mTimer);
            uint64_t intervalMs = static_cast<uint64_t>(mReportingIntervalSec) * 1000;
            mTimerHandler.Schedule(Core::Time::Now().Add(intervalMs), *mTimer);
        }
    }

    void AppGatewayTelemetry::SetCacheThreshold(uint32_t threshold)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
        mCacheThreshold = threshold;
        LOGINFO("AppGatewayTelemetry: Cache threshold set to %u", threshold);
    }

    void AppGatewayTelemetry::SetTelemetryFormat(TelemetryFormat format)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
        mTelemetryFormat = format;
        LOGTRACE("AppGatewayTelemetry: Telemetry format set to %s", 
                format == TelemetryFormat::JSON ? "JSON" : "COMPACT");
    }

    TelemetryFormat AppGatewayTelemetry::GetTelemetryFormat() const
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
        return mTelemetryFormat;
    }

    void AppGatewayTelemetry::RecordBootstrapTime(double durationMs)
    {
        LOGINFO("Plugin bootstrap time recorded: %.2f ms", durationMs);
        
        // Create system context for this system-level event
        Exchange::GatewayContext sysContext = CreateSystemContext();
        
        // Pass individual (non-cumulative) values to RecordGenericMetric for proper aggregation
        RecordGenericMetric(sysContext, AGW_MARKER_BOOTSTRAP_DURATION,
                            durationMs, AGW_UNIT_MILLISECONDS);
    }

    void AppGatewayTelemetry::IncrementWebSocketConnections(const Exchange::GatewayContext& context)
    {
        mHealthStats.websocketConnections.fetch_add(1, std::memory_order_relaxed);
        LOGTRACE("WebSocket connection incremented (appId=%s, connId=%u)",
                 context.appId.c_str(), context.connectionId);
    }

    void AppGatewayTelemetry::DecrementWebSocketConnections(const Exchange::GatewayContext& context)
    {
        uint32_t current = mHealthStats.websocketConnections.load(std::memory_order_relaxed);
        if (current > 0) {
            mHealthStats.websocketConnections.fetch_sub(1, std::memory_order_relaxed);
            LOGTRACE("WebSocket connection decremented (appId=%s, connId=%u)",
                     context.appId.c_str(), context.connectionId);
        }
    }

    void AppGatewayTelemetry::IncrementTotalCalls(const Exchange::GatewayContext& context)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);

        RequestKey key = {context.connectionId, context.requestId};

        // Track this request
        if (mRequestStates.end() == mRequestStates.find(key)) {
            mRequestStates[key].appId = context.appId;
            mHealthStats.totalCalls.fetch_add(1, std::memory_order_relaxed);
            LOGTRACE("Total call incremented (appId=%s, connId=%u, reqId=%u)",
                     context.appId.c_str(), context.connectionId, context.requestId);
        } else {
            LOGTRACE("Duplicate call ignored (appId=%s, connId=%u, reqId=%u)",
                     context.appId.c_str(), context.connectionId, context.requestId);
        }
    }

    void AppGatewayTelemetry::IncrementTotalResponses(const Exchange::GatewayContext& context)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);

        RequestKey key = {context.connectionId, context.requestId};
        auto it = mRequestStates.find(key);
        // NOTE: This method intentionally does NOT erase the entry from mRequestStates.
        // It is designed as a standalone counter increment for callers that track responses
        // independently from success/failure (i.e., via AGW_MARKER_RESPONSE_CALLS only).
        // If the caller also invokes IncrementSuccessfulCalls or IncrementFailedCalls
        // for the same request, do NOT call this method — those methods already increment
        // totalResponses and erase the entry atomically to avoid double-counting.

        if (mRequestStates.end() != it) {
            mHealthStats.totalResponses.fetch_add(1, std::memory_order_relaxed);
            LOGTRACE("Total response incremented (appId=%s, connId=%u, reqId=%u)",
                     context.appId.c_str(), context.connectionId, context.requestId);
        } else {
            LOGTRACE("Duplicate/unknown response ignored (appId=%s, connId=%u, reqId=%u)",
                     context.appId.c_str(), context.connectionId, context.requestId);
        }
    }

    void AppGatewayTelemetry::IncrementSuccessfulCalls(const Exchange::GatewayContext& context)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);

        RequestKey key = {context.connectionId, context.requestId};
        auto it = mRequestStates.find(key);

        if (mRequestStates.end() != it) {
            mHealthStats.totalResponses.fetch_add(1, std::memory_order_relaxed);
            mHealthStats.successfulCalls.fetch_add(1, std::memory_order_relaxed);
            LOGTRACE("Successful call incremented (appId=%s, connId=%u, reqId=%u)",
                     context.appId.c_str(), context.connectionId, context.requestId);
            mRequestStates.erase(it);
        } else {
            LOGTRACE("Duplicate/unknown success ignored (appId=%s, connId=%u, reqId=%u)",
                     context.appId.c_str(), context.connectionId, context.requestId);
        }
    }

    void AppGatewayTelemetry::IncrementFailedCalls(const Exchange::GatewayContext& context)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);

        RequestKey key = {context.connectionId, context.requestId};
        auto it = mRequestStates.find(key);

        if (mRequestStates.end() != it) {
            mHealthStats.totalResponses.fetch_add(1, std::memory_order_relaxed);
            mHealthStats.failedCalls.fetch_add(1, std::memory_order_relaxed);
            LOGTRACE("Failed call incremented (appId=%s, connId=%u, reqId=%u)",
                     context.appId.c_str(), context.connectionId, context.requestId);
            mRequestStates.erase(it);
        } else {
            LOGTRACE("Duplicate/unknown failure ignored (appId=%s, connId=%u, reqId=%u)",
                     context.appId.c_str(), context.connectionId, context.requestId);
        }
    }

    void AppGatewayTelemetry::RecordResponse(const Exchange::GatewayContext& context, bool isSuccess)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);

        RequestKey key = {context.connectionId, context.requestId};
        auto it = mRequestStates.find(key);

        // Check if this request exists (presence in map means no response recorded yet)
        if (mRequestStates.end() != it) {
            // Atomically increment both counters
            mHealthStats.totalResponses.fetch_add(1, std::memory_order_relaxed);

            if (isSuccess) {
                mHealthStats.successfulCalls.fetch_add(1, std::memory_order_relaxed);
                LOGTRACE("Response recorded as SUCCESS (appId=%s, connId=%u, reqId=%u)",
                         context.appId.c_str(), context.connectionId, context.requestId);
            } else {
                mHealthStats.failedCalls.fetch_add(1, std::memory_order_relaxed);
                LOGTRACE("Response recorded as FAILURE (appId=%s, connId=%u, reqId=%u)",
                         context.appId.c_str(), context.connectionId, context.requestId);
            }

            // Entry served its purpose — erase to prevent unbounded map growth
            mRequestStates.erase(it);
        } else {
            LOGTRACE("Duplicate/unknown response ignored (appId=%s, connId=%u, reqId=%u)",
                     context.appId.c_str(), context.connectionId, context.requestId);
        }
    }

    void AppGatewayTelemetry::RecordApiError(const Exchange::GatewayContext& context, const std::string& apiName)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
        mApiErrorCounts[apiName]++;
        LOGTRACE("API error recorded: %s (count: %u, appId=%s, connId=%u, reqId=%u)",
                 apiName.c_str(), mApiErrorCounts[apiName],
                 context.appId.c_str(), context.connectionId, context.requestId);
    }

    void AppGatewayTelemetry::RecordExternalServiceErrorInternal(const Exchange::GatewayContext& context, const std::string& serviceName)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
        mExternalServiceErrorCounts[serviceName]++;
        LOGTRACE("External service error recorded: %s (count: %u, appId=%s, connId=%u, reqId=%u)",
                 serviceName.c_str(), mExternalServiceErrorCounts[serviceName],
                 context.appId.c_str(), context.connectionId, context.requestId);
    }

    // IAppGatewayTelemetry Interface Implementation
    // (Called by external plugins via COM-RPC)

    Core::hresult AppGatewayTelemetry::RecordTelemetryEvent(
        const Exchange::GatewayContext& context,
        const string& eventName,
        const string& eventData)
    {
        {
            Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
            if (!mInitialized) {
                LOGERR("AppGatewayTelemetry not initialized");
                return Core::ERROR_UNAVAILABLE;
            }
        }

        // Handle internal response payload tracking event
        if (AGW_MARKER_RESPONSE_PAYLOAD_TRACKING == eventName) {
            Core::JSONRPC::Message::Info info;
            if (info.FromString(eventData) && info.Code.IsSet() && info.Text.IsSet()) {
                LOGTRACE("Response recorded as FAILURE (appId=%s, connId=%u, reqId=%u)",
                context.appId.c_str(), context.connectionId, context.requestId);
                RecordResponse(context, false);
            } else {
                LOGTRACE("Response recorded as SUCCESS (appId=%s, connId=%u, reqId=%u)",
                context.appId.c_str(), context.connectionId, context.requestId);
                RecordResponse(context, true);
            }
            return Core::ERROR_NONE;
        }

        // Supported event name patterns:
        // - "APP_METRICS_ERROR_split" - App error from FbMetrics (sent immediately, raw CSV)
        // - "APP_SIGNIN_STATUS_split" - Sign-in/sign-out from FbDiscovery (sent immediately, raw CSV)
        // - "APP_READY_split"         - App readiness from AppGatewayCommon (sent immediately, raw CSV)
        // - "ENTS_ERROR_AppGwPluginApiError" - API errors from other plugins (sent immediately)
        // - "ENTS_ERROR_AppGwPlugExtnSrvErr" - External service errors (sent immediately)
        // - Any other event name - Generic telemetry event (cached and flushed periodically)

        // Ripple Log Markers: raw CSV payload sent immediately — no JSON wrapping, no context fields.
        if (AGW_MARKER_APP_METRICS_ERROR == eventName ||
            AGW_MARKER_APP_SIGNIN_STATUS == eventName ||
            AGW_MARKER_APP_READY == eventName) {
            SendT2Event(eventName.c_str(), eventData);
            return Core::ERROR_NONE;
        }

        // Immediate JSON-wrapped events: each entry names the JSON field to extract,
        // the aggregate-tracking method to call, and maps to the same send-then-return path.
        // eventData expected format: {"plugin": "<pluginName>", "<jsonField>": "<value>", "error": "<errorDetails>"}
        using RecordFn = void (AppGatewayTelemetry::*)(const Exchange::GatewayContext&, const std::string&);
        struct ImmediateJsonEvent { const char* marker; const char* jsonField; RecordFn recordFn; };
        static const ImmediateJsonEvent kImmediateJsonEvents[] = {
            { AGW_MARKER_PLUGIN_API_ERROR,         "api",     &AppGatewayTelemetry::RecordApiError                  },
            { AGW_MARKER_PLUGIN_EXT_SERVICE_ERROR, "service", &AppGatewayTelemetry::RecordExternalServiceErrorInternal },
        };

        for (const auto& cfg : kImmediateJsonEvents) {
            if (eventName == cfg.marker) {
                JsonObject data;
                std::string name = (data.FromString(eventData) && data.HasLabel(cfg.jsonField))
                                       ? data[cfg.jsonField].String()
                                       : eventName;
                (this->*cfg.recordFn)(context, name);
                LOGINFO("Sending immediate event to T2: marker=%s, %s=%s",
                        eventName.c_str(), cfg.jsonField, name.c_str());
                SendT2Event(eventName.c_str(), eventData, context);
                return Core::ERROR_NONE;
            }
        }

        // Generic event: cache and flush when threshold is reached.
        bool shouldFlush = false;
        {
            Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
            if (++mCachedEventCount >= mCacheThreshold) {
                shouldFlush = true;
                LOGINFO("Cache threshold reached (%u), flushing telemetry data", mCachedEventCount);
            }
        }

        if (shouldFlush) {
            FlushTelemetryData();
        }

        return Core::ERROR_NONE;
    }

    bool AppGatewayTelemetry::ParseApiMetricName(const std::string& metricName,
                                                 std::string& pluginName,
                                                 std::string& methodName,
                                                 bool& isError)
    {
        // SPECIFIC format for API method metrics with explicit tags:
        // "ENTS_INFO_AppGw_PluginName_<Plugin>_MethodName_<Method>_Success_split"
        // "ENTS_INFO_AppGw_PluginName_<Plugin>_MethodName_<Method>_Error_split"
        //
        // Examples:
        //   "ENTS_INFO_AppGw_PluginName_YourPlugin_MethodName_session_Success_split"
        //   "ENTS_INFO_AppGw_PluginName_YourPlugin_MethodName_setValue_Error_split"
        //
        // Other metrics like "AppGwBootstrapDuration_split" or "AppGXYS_abc_def_split" 
        // will NOT match because they lack the explicit "PluginName_" and "MethodName_" tags
        
        const std::string successSuffix = "_Success_split";
        const std::string errorSuffix = "_Error_split";
        const std::string prefix = AGW_INTERNAL_PLUGIN_PREFIX;
        const std::string methodTag = "_MethodName_";

        // Check if it ends with "_Success_split" or "_Error_split"
        bool hasSuccessSuffix = false;

        if (metricName.length() > successSuffix.length() && 
            successSuffix == metricName.substr(metricName.length() - successSuffix.length())) {
            isError = false;
            hasSuccessSuffix = true;
        } else if (metricName.length() > errorSuffix.length() && 
                   errorSuffix == metricName.substr(metricName.length() - errorSuffix.length())) {
            isError = true;
        } else {
            return false;
        }

        // Check if it starts with the explicit prefix AGW_INTERNAL_PLUGIN_PREFIX
        if (metricName.length() <= prefix.length() || 
            metricName.substr(0, prefix.length()) != prefix) {
            return false;
        }

        // Remove prefix and suffix to get "Plugin_MethodName_Method_Success"
        size_t suffixLen = hasSuccessSuffix ? successSuffix.length() : errorSuffix.length();
        std::string middle = metricName.substr(prefix.length(), 
                                              metricName.length() - prefix.length() - suffixLen);

        // Find "_MethodName_" tag
        size_t methodTagPos = middle.find(methodTag);
        if (std::string::npos == methodTagPos || 0 == methodTagPos) {
            return false;
        }

        // Extract plugin name (everything before "_MethodName_")
        pluginName = middle.substr(0, methodTagPos);

        // Extract method name (everything after "_MethodName_")
        methodName = middle.substr(methodTagPos + methodTag.length());

        // Validate that both names are non-empty
        if (pluginName.empty() || methodName.empty()) {
            return false;
        }

        return true;
    }

    bool AppGatewayTelemetry::ParseApiLatencyMetricName(const std::string& metricName,
                                                        std::string& pluginName,
                                                        std::string& apiName)
    {
        // SPECIFIC format for API latency metrics with explicit tags:
        // "ENTS_INFO_AppGw_PluginName_<Plugin>_ApiName_<Api>_ApiLatency_split"
        //
        // Examples:
        //   "ENTS_INFO_AppGw_PluginName_YourPlugin_ApiName_GetSettings_ApiLatency_split"
        //   "ENTS_INFO_AppGw_PluginName_YourServices_ApiName_GetToken_ApiLatency_split"

        const std::string suffix = "_ApiLatency_split";
        const std::string prefix = AGW_INTERNAL_PLUGIN_PREFIX;
        const std::string apiTag = "_ApiName_";

        // Check if it ends with "_ApiLatency_split"
        if (metricName.length() <= suffix.length() || 
            suffix != metricName.substr(metricName.length() - suffix.length())) {
            return false;
        }

        // Check if it starts with AGW_INTERNAL_PLUGIN_PREFIX
        if (metricName.length() <= prefix.length() || 
            prefix != metricName.substr(0, prefix.length())) {
            return false;
        }

        // Remove prefix and suffix
        std::string middle = metricName.substr(prefix.length(), 
                                              metricName.length() - prefix.length() - suffix.length());

        // Find "_ApiName_" tag
        size_t apiTagPos = middle.find(apiTag);
        if (std::string::npos == apiTagPos || 0 == apiTagPos) {
            return false;
        }

        // Extract plugin name (everything before "_ApiName_")
        pluginName = middle.substr(0, apiTagPos);

        // Extract API name (everything after "_ApiName_")
        apiName = middle.substr(apiTagPos + apiTag.length());

        // Validate that both names are non-empty
        if (pluginName.empty() || apiName.empty()) {
            return false;
        }

        return true;
    }

    bool AppGatewayTelemetry::ParseServiceLatencyMetricName(const std::string& metricName,
                                                            std::string& pluginName,
                                                            std::string& serviceName)
    {
        // SPECIFIC format for service latency metrics with explicit tags:
        // "ENTS_INFO_AppGw_PluginName_<Plugin>_ServiceName_<Service>_ServiceLatency_split"
        //
        // Examples:
        //   "ENTS_INFO_AppGw_PluginName_YourServices_ServiceName_PermissionService_ServiceLatency_split"
        //   "ENTS_INFO_AppGw_PluginName_YourPlugin_ServiceName_AuthService_ServiceLatency_split"

        const std::string suffix = "_ServiceLatency_split";
        const std::string prefix = AGW_INTERNAL_PLUGIN_PREFIX;
        const std::string serviceTag = "_ServiceName_";

        // Check if it ends with "_ServiceLatency_split"
        if (metricName.length() <= suffix.length() || 
            suffix != metricName.substr(metricName.length() - suffix.length())) {
            return false;
        }

        // Check if it starts with AGW_INTERNAL_PLUGIN_PREFIX
        if (metricName.length() <= prefix.length() || 
            prefix != metricName.substr(0, prefix.length())) {
            return false;
        }

        // Remove prefix and suffix
        std::string middle = metricName.substr(prefix.length(), 
                                              metricName.length() - prefix.length() - suffix.length());

        // Find "_ServiceName_" tag
        size_t serviceTagPos = middle.find(serviceTag);
        if (std::string::npos == serviceTagPos || 0 == serviceTagPos) {
            return false;
        }

        // Extract plugin name (everything before "_ServiceName_")
        pluginName = middle.substr(0, serviceTagPos);

        // Extract service name (everything after "_ServiceName_")
        serviceName = middle.substr(serviceTagPos + serviceTag.length());

        // Validate that both names are non-empty
        if (pluginName.empty() || serviceName.empty()) {
            return false;
        }

        return true;
    }

    bool AppGatewayTelemetry::ParseServiceMetricName(const std::string& metricName,
                                                     std::string& pluginName,
                                                     std::string& serviceName,
                                                     bool& isError)
    {
        // SPECIFIC format for service method metrics with explicit tags:
        // "ENTS_INFO_AppGw_PluginName_<Plugin>_ServiceName_<Service>_Success_split"
        // "ENTS_INFO_AppGw_PluginName_<Plugin>_ServiceName_<Service>_Error_split"
        //
        // Examples:
        //   "ENTS_INFO_AppGw_PluginName_YourServices_ServiceName_PermissionService_Success_split"
        //   "ENTS_INFO_AppGw_PluginName_YourPlugin_ServiceName_AuthService_Error_split"
        //
        // Other metrics like "AppGwBootstrapDuration_split" or service latency metrics
        // will NOT match because they lack the Success/Error suffix or use different patterns

        const std::string successSuffix = "_Success_split";
        const std::string errorSuffix = "_Error_split";
        const std::string prefix = AGW_INTERNAL_PLUGIN_PREFIX;
        const std::string serviceTag = "_ServiceName_";

        // Check if it ends with "_Success_split" or "_Error_split"
        bool hasSuccessSuffix = false;

        if (metricName.length() > successSuffix.length() && 
            successSuffix == metricName.substr(metricName.length() - successSuffix.length())) {
            isError = false;
            hasSuccessSuffix = true;
        } else if (metricName.length() > errorSuffix.length() && 
                   errorSuffix == metricName.substr(metricName.length() - errorSuffix.length())) {
            isError = true;
        } else {
            return false;
        }

        // Check if it starts with the explicit prefix AGW_INTERNAL_PLUGIN_PREFIX
        if (metricName.length() <= prefix.length() || 
            prefix != metricName.substr(0, prefix.length())) {
            return false;
        }

        // Remove prefix and suffix to get "Plugin_ServiceName_Service_Success"
        size_t suffixLen = hasSuccessSuffix ? successSuffix.length() : errorSuffix.length();
        std::string middle = metricName.substr(prefix.length(), 
                                              metricName.length() - prefix.length() - suffixLen);

        // Find "_ServiceName_" tag
        size_t serviceTagPos = middle.find(serviceTag);
        if (std::string::npos == serviceTagPos || 0 == serviceTagPos) {
            return false;
        }

        // Extract plugin name (everything before "_ServiceName_")
        pluginName = middle.substr(0, serviceTagPos);

        // Extract service name (everything after "_ServiceName_")
        serviceName = middle.substr(serviceTagPos + serviceTag.length());

        // Validate that both names are non-empty
        if (pluginName.empty() || serviceName.empty()) {
            return false;
        }

        return true;
    }

    void AppGatewayTelemetry::RecordApiMethodMetric(
        const Exchange::GatewayContext& context,
        const std::string& pluginName,
        const std::string& methodName,
        double latencyMs,
        bool isError)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);

        std::string apiKey = pluginName + "_" + methodName;
        ApiMethodStats& stats = mApiMethodStats[apiKey];

        // Initialize plugin and method names on first use
        if (stats.pluginName.empty()) {
            stats.pluginName = pluginName;
            stats.methodName = methodName;
        }

        // Track counters (plugin-specific stats only, do NOT affect AppGateway's health stats)

        if (isError) {
            // Error case
            stats.errorCount++;
            stats.totalErrorLatencyMs += latencyMs;
    
            if (latencyMs < stats.minErrorLatencyMs) {
                stats.minErrorLatencyMs = latencyMs;
            }
            if (latencyMs > stats.maxErrorLatencyMs) {
                stats.maxErrorLatencyMs = latencyMs;
            }
    
            LOGTRACE("API error tracked: %s::%s (error_count=%u, latency=%.2f ms, appId=%s, connId=%u, reqId=%u)",
                     pluginName.c_str(), methodName.c_str(), stats.errorCount, latencyMs,
                     context.appId.c_str(), context.connectionId, context.requestId);
        } else {
            // Success case
            stats.successCount++;
            stats.totalSuccessLatencyMs += latencyMs;

            if (latencyMs < stats.minSuccessLatencyMs) {
                stats.minSuccessLatencyMs = latencyMs;
            }
            if (latencyMs > stats.maxSuccessLatencyMs) {
                stats.maxSuccessLatencyMs = latencyMs;
            }
    
            LOGTRACE("API success tracked: %s::%s (success_count=%u, latency=%.2f ms, appId=%s, connId=%u, reqId=%u)",
                     pluginName.c_str(), methodName.c_str(), stats.successCount, latencyMs,
                     context.appId.c_str(), context.connectionId, context.requestId);
        }
        
        mCachedEventCount++;
    }

    void AppGatewayTelemetry::RecordApiLatencyMetric(
        const Exchange::GatewayContext& context,
        const std::string& pluginName,
        const std::string& apiName,
        double latencyMs)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);

        std::string latencyKey = pluginName + "_" + apiName;
        ApiLatencyStats& stats = mApiLatencyStats[latencyKey];

        // Initialize plugin and API names on first use
        if (stats.pluginName.empty()) {
            stats.pluginName = pluginName;
            stats.apiName = apiName;
        }

        // Track latency
        stats.count++;
        stats.totalLatencyMs += latencyMs;

        if (latencyMs < stats.minLatencyMs) {
            stats.minLatencyMs = latencyMs;
        }
        if (latencyMs > stats.maxLatencyMs) {
            stats.maxLatencyMs = latencyMs;
        }

        LOGTRACE("API latency tracked: %s::%s (count=%u, latency=%.2f ms, appId=%s, connId=%u, reqId=%u)",
                 pluginName.c_str(), apiName.c_str(), stats.count, latencyMs,
                 context.appId.c_str(), context.connectionId, context.requestId);

        mCachedEventCount++;
    }

    void AppGatewayTelemetry::RecordServiceLatencyMetric(
        const Exchange::GatewayContext& context,
        const std::string& pluginName,
        const std::string& serviceName,
        double latencyMs)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);

        std::string latencyKey = pluginName + "_" + serviceName;
        ServiceLatencyStats& stats = mServiceLatencyStats[latencyKey];

        // Initialize plugin and service names on first use
        if (stats.pluginName.empty()) {
            stats.pluginName = pluginName;
            stats.serviceName = serviceName;
        }

        // Track latency
        stats.count++;
        stats.totalLatencyMs += latencyMs;

        if (latencyMs < stats.minLatencyMs) {
            stats.minLatencyMs = latencyMs;
        }
        if (latencyMs > stats.maxLatencyMs) {
            stats.maxLatencyMs = latencyMs;
        }

        LOGTRACE("Service latency tracked: %s::%s (count=%u, latency=%.2f ms, appId=%s, connId=%u, reqId=%u)",
                 pluginName.c_str(), serviceName.c_str(), stats.count, latencyMs,
                 context.appId.c_str(), context.connectionId, context.requestId);

        mCachedEventCount++;
    }

    void AppGatewayTelemetry::RecordServiceMethodMetric(
        const Exchange::GatewayContext& context,
        const std::string& pluginName,
        const std::string& serviceName,
        double latencyMs,
        bool isError)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);

        std::string serviceKey = pluginName + "_" + serviceName;
        ServiceMethodStats& stats = mServiceMethodStats[serviceKey];

        // Initialize plugin and service names on first use
        if (stats.pluginName.empty()) {
            stats.pluginName = pluginName;
            stats.serviceName = serviceName;
        }

        // Track counters (plugin-specific stats only)

        if (isError) {
            // Error case
            stats.errorCount++;
            stats.totalErrorLatencyMs += latencyMs;

            if (latencyMs < stats.minErrorLatencyMs) {
                stats.minErrorLatencyMs = latencyMs;
            }
            if (latencyMs > stats.maxErrorLatencyMs) {
                stats.maxErrorLatencyMs = latencyMs;
            }

            LOGTRACE("Service error tracked: %s::%s (error_count=%u, latency=%.2f ms, appId=%s, connId=%u, reqId=%u)",
                     pluginName.c_str(), serviceName.c_str(), stats.errorCount, latencyMs,
                     context.appId.c_str(), context.connectionId, context.requestId);
        } else {
            // Success case
            stats.successCount++;
            stats.totalSuccessLatencyMs += latencyMs;

            if (latencyMs < stats.minSuccessLatencyMs) {
                stats.minSuccessLatencyMs = latencyMs;
            }
            if (latencyMs > stats.maxSuccessLatencyMs) {
                stats.maxSuccessLatencyMs = latencyMs;
            }

            LOGTRACE("Service success tracked: %s::%s (success_count=%u, latency=%.2f ms, appId=%s, connId=%u, reqId=%u)",
                     pluginName.c_str(), serviceName.c_str(), stats.successCount, latencyMs,
                     context.appId.c_str(), context.connectionId, context.requestId);
        }

        mCachedEventCount++;
    }

    void AppGatewayTelemetry::RecordGenericMetric(
        const Exchange::GatewayContext& context,
        const std::string& metricName,
        double metricValue,
        const std::string& metricUnit)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);

        MetricData& data = mMetricsCache[metricName];
        data.sum += metricValue;
        data.count++;

        if (metricValue < data.min) {
            data.min = metricValue;
        }
        if (metricValue > data.max) {
            data.max = metricValue;
        }
        if (data.unit.empty()) {
            data.unit = metricUnit;
        }

        LOGTRACE("Generic metric tracked: %s (value=%.2f %s, appId=%s, connId=%u, reqId=%u)",
                 metricName.c_str(), metricValue, metricUnit.c_str(),
                 context.appId.c_str(), context.connectionId, context.requestId);
        
        mCachedEventCount++;
    }

    bool AppGatewayTelemetry::HandleHealthStatsMarker(
        const Exchange::GatewayContext& context,
        const std::string& metricName,
        double metricValue)
    {
        // Match-like pattern using static map for health stats marker routing
        using MarkerHandler = std::function<void(AppGatewayTelemetry*, const Exchange::GatewayContext&, double)>;
    
        static const std::unordered_map<std::string, MarkerHandler> markerHandlers = {
            // WebSocket connections: supports increment/decrement by value
            {AGW_MARKER_WEBSOCKET_CONNECTIONS, [](AppGatewayTelemetry* self, const Exchange::GatewayContext& ctx, double value) {
                int count = static_cast<int>(std::abs(value));
                if (value > 0) {
                    for (int i = 0; i < count; i++) {
                        self->IncrementWebSocketConnections(ctx);
                    }
                } else if (value < 0) {
                    for (int i = 0; i < count; i++) {
                        self->DecrementWebSocketConnections(ctx);
                    }
                }
            }},

            // All other health stats: increment by value (typically 1)
            {AGW_MARKER_TOTAL_CALLS, [](AppGatewayTelemetry* self, const Exchange::GatewayContext& ctx, double value) {
                int count = static_cast<int>(value);
                for (int i = 0; i < count; i++) {
                    self->IncrementTotalCalls(ctx);
                }
            }},
            
            {AGW_MARKER_RESPONSE_CALLS, [](AppGatewayTelemetry* self, const Exchange::GatewayContext& ctx, double value) {
                int count = static_cast<int>(value);
                for (int i = 0; i < count; i++) {
                    self->IncrementTotalResponses(ctx);
                }
            }},

            {AGW_MARKER_SUCCESSFUL_CALLS, [](AppGatewayTelemetry* self, const Exchange::GatewayContext& ctx, double value) {
                int count = static_cast<int>(value);
                for (int i = 0; i < count; i++) {
                    self->IncrementSuccessfulCalls(ctx);
                }
            }},

            {AGW_MARKER_FAILED_CALLS, [](AppGatewayTelemetry* self, const Exchange::GatewayContext& ctx, double value) {
                int count = static_cast<int>(value);
                for (int i = 0; i < count; i++) {
                    self->IncrementFailedCalls(ctx);
                }
            }}
        };

        // Pattern match: look up and execute handler if found
        auto it = markerHandlers.find(metricName);
        if (markerHandlers.end() != it) {
            it->second(this, context, metricValue);
            return true;
        }

        return false;  // Not a health stats marker
    }

    Core::hresult AppGatewayTelemetry::RecordTelemetryMetric(
        const Exchange::GatewayContext& context,
        const string& metricName,
        const double metricValue,
        const string& metricUnit)
    {
        {
            Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
            if (!mInitialized) {
                LOGERR("AppGatewayTelemetry not initialized");
                return Core::ERROR_UNAVAILABLE;
            }
        }

        LOGTRACE("RecordTelemetryMetric from %s: metric=%s, value=%f, unit=%s",
                    context.appId.c_str(), metricName.c_str(), metricValue, metricUnit.c_str());

        // Handle bootstrap duration metric
        if (AGW_MARKER_BOOTSTRAP_DURATION == metricName) {
            RecordBootstrapTime(metricValue);
            return Core::ERROR_NONE;
        }

        // Handle health stats markers (websocket connections, calls, responses, etc.)
        if (HandleHealthStatsMarker(context, metricName, metricValue)) {
            return Core::ERROR_NONE;
        }

        // Determine metric type and record accordingly
        std::string pluginName, apiOrMethodName;
        bool isError = false;

        if (ParseApiMetricName(metricName, pluginName, apiOrMethodName, isError)) {
            // API method metric (success/error with latency tracking)
            RecordApiMethodMetric(context, pluginName, apiOrMethodName, metricValue, isError);
        } else if (ParseServiceMetricName(metricName, pluginName, apiOrMethodName, isError)) {
            // Service method metric (success/error with latency tracking from AGW_TRACK_SERVICE_CALL)
            RecordServiceMethodMetric(context, pluginName, apiOrMethodName, metricValue, isError);
        } else if (ParseApiLatencyMetricName(metricName, pluginName, apiOrMethodName)) {
            // API latency metric (deprecated, but still supported)
            RecordApiLatencyMetric(context, pluginName, apiOrMethodName, metricValue);
        } else if (ParseServiceLatencyMetricName(metricName, pluginName, apiOrMethodName)) {
            // External service latency metric
            RecordServiceLatencyMetric(context, pluginName, apiOrMethodName, metricValue);
        } else {
            // Generic metric aggregation (bootstrap time, etc.)
            RecordGenericMetric(context, metricName, metricValue, metricUnit);
        }

        // Check if cache threshold reached and flush if needed
        bool shouldFlush = false;
        {
            Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
            shouldFlush = (mCachedEventCount >= mCacheThreshold);
        }
        if (shouldFlush) {
            FlushTelemetryData();
        }

        return Core::ERROR_NONE;
    }

    void AppGatewayTelemetry::OnTimerExpired()
    {
        LOGINFO("Telemetry reporting timer expired, flushing data");
        FlushTelemetryData();

        // Reschedule the timer
        Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
        if (mTimerRunning && mInitialized) {
            uint64_t intervalMs = static_cast<uint64_t>(mReportingIntervalSec) * 1000;
            mTimerHandler.Schedule(Core::Time::Now().Add(intervalMs), *mTimer);
        }
    }

    void AppGatewayTelemetry::FlushTelemetryData()
    {
        std::unique_ptr<TelemetrySnapshot> snapshot(new TelemetrySnapshot());

        {
            Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - mReportingStartTime).count();

            LOGINFO("Flushing telemetry data synchronously (reporting period: %ld seconds)", elapsed);

            // Snapshot configuration
            snapshot->reportingIntervalSec = static_cast<uint32_t>(elapsed);  // Actual elapsed time, not configured interval
            snapshot->reportingStartTime = mReportingStartTime;
            snapshot->parent = this;
            snapshot->format = mTelemetryFormat;
            
            // Snapshot health statistics (atomic values)
            snapshot->websocketConnections = mHealthStats.websocketConnections.load(std::memory_order_relaxed);
            snapshot->totalCalls = mHealthStats.totalCalls.load(std::memory_order_relaxed);
            snapshot->totalResponses = mHealthStats.totalResponses.load(std::memory_order_relaxed);
            snapshot->successfulCalls = mHealthStats.successfulCalls.load(std::memory_order_relaxed);
            snapshot->failedCalls = mHealthStats.failedCalls.load(std::memory_order_relaxed);
            
            // Evict stale entries (requests with no response for > 5 minutes)
            auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(300);
            for (auto it = mRequestStates.begin(); it != mRequestStates.end(); ) {
                if (it->second.requestTime < cutoff) {
                    LOGTRACE("Evicting stale request (connId=%u, reqId=%u, appId=%s)",
                             it->first.connectionId, it->first.requestId,
                             it->second.appId.c_str());
                    it = mRequestStates.erase(it);
                } else {
                    ++it;
                }
            }

            // Snapshot request states (copy; keep live map for in-flight response tracking)
            snapshot->requestStates = mRequestStates;
            
            // Snapshot aggregated statistics (move semantics for efficiency)
            snapshot->apiMethodStats = std::move(mApiMethodStats);
            snapshot->apiLatencyStats = std::move(mApiLatencyStats);
            snapshot->serviceMethodStats = std::move(mServiceMethodStats);
            snapshot->serviceLatencyStats = std::move(mServiceLatencyStats);
            snapshot->apiErrorCounts = std::move(mApiErrorCounts);
            snapshot->externalServiceErrorCounts = std::move(mExternalServiceErrorCounts);
            snapshot->metricsCache = std::move(mMetricsCache);

            // Reset for next reporting period
            ResetHealthStats();
            mCachedEventCount = 0;
            mReportingStartTime = now;
            
            LOGTRACE("Snapshot created, sending telemetry synchronously");
        }
        // Lock released - new telemetry can now be recorded while snapshot is being sent

        // Send telemetry synchronously (no WorkerPool dispatch)
        snapshot->SendAll();
    }

    void AppGatewayTelemetry::SendHealthStats()
    {
        uint32_t wsConnections = mHealthStats.websocketConnections.load(std::memory_order_relaxed);
        uint32_t totalCalls = mHealthStats.totalCalls.load(std::memory_order_relaxed);
        uint32_t totalResponses = mHealthStats.totalResponses.load(std::memory_order_relaxed);
        uint32_t successfulCalls = mHealthStats.successfulCalls.load(std::memory_order_relaxed);
        uint32_t failedCalls = mHealthStats.failedCalls.load(std::memory_order_relaxed);
        uint32_t pendingCount = 0;
        uint32_t reportingIntervalSec = 0;

        {
            Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
            // All entries in mRequestStates are pending (responded entries are erased immediately)
            pendingCount = static_cast<uint32_t>(mRequestStates.size());
            reportingIntervalSec = mReportingIntervalSec;
        }

        // Only send if there's data
        if (0 == totalCalls && 0 == wsConnections && 0 == pendingCount) {
            LOGINFO("No health stats to report");
            return;
        }

        // Send all health stats in a single consolidated payload to T2
        JsonObject healthPayload;
        healthPayload["reporting_interval_sec"] = reportingIntervalSec;
        healthPayload["websocket_connections"] = wsConnections;
        healthPayload["total_calls"] = totalCalls;
        healthPayload["total_responses"] = totalResponses;
        healthPayload["successful_calls"] = successfulCalls;
        healthPayload["failed_calls"] = failedCalls;
        healthPayload["unit"] = AGW_UNIT_COUNT;

        //LOGINFO("Sending health stats to T2 (pending=%u)", pendingCount);
        LOGINFO("Sending health stats to T2");
        Exchange::GatewayContext sysContext = CreateSystemContext();
        SendT2Event(AGW_MARKER_HEALTH_STATS, healthPayload, sysContext);

        LOGTRACE("Health stats sent: ws=%u, total=%u, responses=%u, success=%u, failed=%u, pending=%u",
                wsConnections, totalCalls, totalResponses, successfulCalls, failedCalls, pendingCount);
    }

    void AppGatewayTelemetry::SendApiErrorStats()
    {
        std::map<std::string, uint32_t> apiErrorCounts;
        uint32_t reportingIntervalSec = 0;

        {
            Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
            if (mApiErrorCounts.empty()) {
                LOGTRACE("No API error stats to report");
                return;
            }
            apiErrorCounts = mApiErrorCounts;
            reportingIntervalSec = mReportingIntervalSec;
        }

        // Send each API error count with common marker and API name in payload
        std::string metricName = std::string(AGW_MARKER_API_ERROR_COUNT);
        
        for (const auto& item : apiErrorCounts) {
            JsonObject metricPayload;
            metricPayload["reporting_interval_sec"] = reportingIntervalSec;
            metricPayload["ApiName"] = item.first;
            metricPayload["count"] = item.second;
            metricPayload["unit"] = AGW_UNIT_COUNT;
            
            LOGINFO("Sending API error metric to T2");
            Exchange::GatewayContext sysContext = CreateSystemContext();
            SendT2Event(metricName.c_str(), metricPayload, sysContext);
        }

        LOGINFO("API error stats sent as metrics: %zu APIs with errors", apiErrorCounts.size());
    }

    void AppGatewayTelemetry::SendExternalServiceErrorStats()
    {
        std::map<std::string, uint32_t> externalServiceErrorCounts;
        uint32_t reportingIntervalSec = 0;

        {
            Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
            if (mExternalServiceErrorCounts.empty()) {
                LOGTRACE("No external service error stats to report");
                return;
            }
            externalServiceErrorCounts = mExternalServiceErrorCounts;
            reportingIntervalSec = mReportingIntervalSec;
        }

        // Send each external service error count with common marker and service name in payload
        std::string metricName = std::string(AGW_MARKER_EXT_SERVICE_ERROR_COUNT);
        
        for (const auto& item : externalServiceErrorCounts) {
            JsonObject metricPayload;
            metricPayload["reporting_interval_sec"] = reportingIntervalSec;
            metricPayload["ServiceName"] = item.first;
            metricPayload["count"] = item.second;
            metricPayload["unit"] = AGW_UNIT_COUNT;
            
            LOGINFO("Sending external service error metric to T2");
            Exchange::GatewayContext sysContext = CreateSystemContext();
            SendT2Event(metricName.c_str(), metricPayload, sysContext);
        }

        LOGINFO("External service error stats sent as metrics: %zu services with errors", 
            externalServiceErrorCounts.size());
    }

    void AppGatewayTelemetry::SendAggregatedMetrics()
    {
        std::map<std::string, MetricData> metricsCache;
        uint32_t reportingIntervalSec = 0;

        {
            Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
            if (mMetricsCache.empty()) {
                LOGTRACE("No aggregated metrics to report");
                return;
            }
            metricsCache = mMetricsCache;
            reportingIntervalSec = mReportingIntervalSec;
        }

        // Send each metric with its own marker (the metric name)
        for (const auto& item : metricsCache) {
            const std::string& metricName = item.first;
            const MetricData& data = item.second;
            
            if (data.count == 0) {
                continue;
            }

            double minVal = (data.min == std::numeric_limits<double>::max()) ? 0.0 : data.min;
            double maxVal = (data.max == std::numeric_limits<double>::lowest()) ? 0.0 : data.max;
            double avgVal = data.sum / static_cast<double>(data.count);

            JsonObject payload;
            payload["min"] = minVal;
            payload["max"] = maxVal;
            payload["count"] = data.count;
            payload["avg"] = avgVal;
            payload["unit"] = data.unit;
            payload["reporting_interval_sec"] = reportingIntervalSec;

            // Use the metric name as the T2 marker
            LOGINFO("Sending aggregated metric to T2: %s", metricName.c_str());
            Exchange::GatewayContext sysContext = CreateSystemContext();
            SendT2Event(metricName.c_str(), payload, sysContext);

        }
    }

    void AppGatewayTelemetry::SendApiMethodStats()
    {
        std::map<std::string, ApiMethodStats> apiMethodStats;
        uint32_t reportingIntervalSec = 0;

        {
            Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
            if (mApiMethodStats.empty()) {
                LOGTRACE("No API method stats to report");
                return;
            }
            apiMethodStats = mApiMethodStats;
            reportingIntervalSec = mReportingIntervalSec;
        }

        // Send each plugin/method combination as a separate T2 marker
        for (const auto& item : apiMethodStats) {
            const ApiMethodStats& stats = item.second;
            
            if (stats.successCount == 0 && stats.errorCount == 0) {
                continue;
            }

            // Build detailed payload with plugin name, method name, counters, and latency stats
            JsonObject payload;
            payload["plugin_name"] = stats.pluginName;
            payload["method_name"] = stats.methodName;
            payload["reporting_interval_sec"] = reportingIntervalSec;
            
            // Success metrics
            if (stats.successCount > 0) {
                double avgSuccessLatency = stats.totalSuccessLatencyMs / stats.successCount;
                double minSuccess = (stats.minSuccessLatencyMs == std::numeric_limits<double>::max()) 
                                    ? 0.0 : stats.minSuccessLatencyMs;
                double maxSuccess = (stats.maxSuccessLatencyMs == std::numeric_limits<double>::lowest()) 
                                    ? 0.0 : stats.maxSuccessLatencyMs;
                
                payload["success_count"] = stats.successCount;
                payload["success_latency_min_ms"] = minSuccess;
                payload["success_latency_max_ms"] = maxSuccess;
                payload["success_latency_avg_ms"] = avgSuccessLatency;

            } else {
                payload["success_count"] = 0;
            }
            
            // Error metrics
            if (stats.errorCount > 0) {
                double avgErrorLatency = stats.totalErrorLatencyMs / stats.errorCount;
                double minError = (stats.minErrorLatencyMs == std::numeric_limits<double>::max()) 
                                    ? 0.0 : stats.minErrorLatencyMs;
                double maxError = (stats.maxErrorLatencyMs == std::numeric_limits<double>::lowest()) 
                                    ? 0.0 : stats.maxErrorLatencyMs;
                
                payload["error_count"] = stats.errorCount;
                payload["error_latency_min_ms"] = minError;
                payload["error_latency_max_ms"] = maxError;
                payload["error_latency_avg_ms"] = avgErrorLatency;

            } else {
                payload["error_count"] = 0;
            }
            
            // Total counts
            uint32_t totalCalls = stats.successCount + stats.errorCount;
            payload["total_count"] = totalCalls;
            
            // T2 marker: Use common marker since plugin_name and method_name are in payload
            LOGINFO("Sending API method stats to T2");
            Exchange::GatewayContext sysContext = CreateSystemContext();
            SendT2Event(AGW_MARKER_API_METHOD_STAT, payload, sysContext);
            
            LOGTRACE("API method stats sent: %s::%s (total=%u, success=%u, error=%u, avg_success_latency=%.2f ms)",
                    stats.pluginName.c_str(), stats.methodName.c_str(),
                    totalCalls, stats.successCount, stats.errorCount,
                    stats.successCount > 0 ? stats.totalSuccessLatencyMs / stats.successCount : 0.0);
        }

        LOGINFO("API method stats sent: %zu plugin/method combinations", apiMethodStats.size());
    }

    void AppGatewayTelemetry::SendApiLatencyStats()
    {
        std::map<std::string, ApiLatencyStats> apiLatencyStats;
        uint32_t reportingIntervalSec = 0;

        {
            Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
            if (mApiLatencyStats.empty()) {
                LOGTRACE("No API latency stats to report");
                return;
            }
            apiLatencyStats = mApiLatencyStats;
            reportingIntervalSec = mReportingIntervalSec;
        }

        // Send each plugin/API combination using common marker AGW_MARKER_API_LATENCY
        for (const auto& item : apiLatencyStats) {
            const ApiLatencyStats& stats = item.second;
            
            if (stats.count == 0) {
                continue;
            }

            // Build payload with plugin name, API name, and latency statistics
            JsonObject payload;
            payload["plugin_name"] = stats.pluginName;
            payload["api_name"] = stats.apiName;
            payload["reporting_interval_sec"] = reportingIntervalSec;
            payload["count"] = stats.count;
            
            double avgLatency = stats.totalLatencyMs / stats.count;
            double minLatency = (stats.minLatencyMs == std::numeric_limits<double>::max()) 
                                ? 0.0 : stats.minLatencyMs;
            double maxLatency = (stats.maxLatencyMs == std::numeric_limits<double>::lowest()) 
                                ? 0.0 : stats.maxLatencyMs;
            
            payload["avg_ms"] = avgLatency;
            payload["min_ms"] = minLatency;
            payload["max_ms"] = maxLatency;
            payload["unit"] = AGW_UNIT_MILLISECONDS;
            
            // Use common T2 marker - plugin and API names are in payload
            LOGINFO("Sending API latency stats to T2");
            Exchange::GatewayContext sysContext = CreateSystemContext();
            SendT2Event(AGW_MARKER_API_LATENCY, payload, sysContext);

            LOGTRACE("API latency stats sent: %s::%s (count=%u, avg=%.2f ms, min=%.2f ms, max=%.2f ms)",
                    stats.pluginName.c_str(), stats.apiName.c_str(),
                    stats.count, avgLatency, minLatency, maxLatency);
        }

        LOGINFO("API latency stats sent: %zu plugin/API combinations", apiLatencyStats.size());
    }

    void AppGatewayTelemetry::SendServiceLatencyStats()
    {
        std::map<std::string, ServiceLatencyStats> serviceLatencyStats;
        uint32_t reportingIntervalSec = 0;

        {
            Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
            if (mServiceLatencyStats.empty()) {
                LOGTRACE("No service latency stats to report");
                return;
            }
            serviceLatencyStats = mServiceLatencyStats;
            reportingIntervalSec = mReportingIntervalSec;
        }

        // Send each plugin/service combination using common marker AGW_MARKER_SERVICE_LATENCY
        for (const auto& item : serviceLatencyStats) {
            const ServiceLatencyStats& stats = item.second;
            
            if (stats.count == 0) {
                continue;
            }

            // Build payload with plugin name, service name, and latency statistics
            JsonObject payload;
            payload["plugin_name"] = stats.pluginName;
            payload["service_name"] = stats.serviceName;
            payload["reporting_interval_sec"] = reportingIntervalSec;
            payload["count"] = stats.count;
            
            double avgLatency = stats.totalLatencyMs / stats.count;
            double minLatency = (stats.minLatencyMs == std::numeric_limits<double>::max()) 
                                ? 0.0 : stats.minLatencyMs;
            double maxLatency = (stats.maxLatencyMs == std::numeric_limits<double>::lowest()) 
                                ? 0.0 : stats.maxLatencyMs;
            
            payload["avg_ms"] = avgLatency;
            payload["min_ms"] = minLatency;
            payload["max_ms"] = maxLatency;
            payload["unit"] = AGW_UNIT_MILLISECONDS;
            
            // Use common T2 marker - plugin and service names are in payload
            LOGINFO("Sending service latency stats to T2");
            Exchange::GatewayContext sysContext = CreateSystemContext();
            SendT2Event(AGW_MARKER_SERVICE_LATENCY, payload, sysContext);
            
            LOGTRACE("Service latency stats sent: %s::%s (count=%u, avg=%.2f ms, min=%.2f ms, max=%.2f ms)",
                    stats.pluginName.c_str(), stats.serviceName.c_str(),
                    stats.count, avgLatency, minLatency, maxLatency);
        }

        LOGINFO("Service latency stats sent: %zu plugin/service combinations", serviceLatencyStats.size());
    }

    void AppGatewayTelemetry::SendServiceMethodStats()
    {
        std::map<std::string, ServiceMethodStats> serviceMethodStats;
        uint32_t reportingIntervalSec = 0;

        {
            Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
            if (mServiceMethodStats.empty()) {
                LOGTRACE("No service method stats to report");
                return;
            }
            serviceMethodStats = mServiceMethodStats;
            reportingIntervalSec = mReportingIntervalSec;
        }

        // Send each plugin/service combination as a separate T2 marker
        for (const auto& item : serviceMethodStats) {
            const ServiceMethodStats& stats = item.second;
            
            if (stats.successCount == 0 && stats.errorCount == 0) {
                continue;
            }

            // Build detailed payload with plugin name, service name, counters, and latency stats
            JsonObject payload;
            payload["plugin_name"] = stats.pluginName;
            payload["service_name"] = stats.serviceName;
            payload["reporting_interval_sec"] = reportingIntervalSec;
            
            // Success metrics
            if (stats.successCount > 0) {
                double avgSuccessLatency = stats.totalSuccessLatencyMs / stats.successCount;
                double minSuccess = (stats.minSuccessLatencyMs == std::numeric_limits<double>::max()) 
                                    ? 0.0 : stats.minSuccessLatencyMs;
                double maxSuccess = (stats.maxSuccessLatencyMs == std::numeric_limits<double>::lowest()) 
                                    ? 0.0 : stats.maxSuccessLatencyMs;
                
                payload["success_count"] = stats.successCount;
                payload["success_latency_avg_ms"] = avgSuccessLatency;
                payload["success_latency_min_ms"] = minSuccess;
                payload["success_latency_max_ms"] = maxSuccess;
            } else {
                payload["success_count"] = 0;
            }
            
            // Error metrics
            if (stats.errorCount > 0) {
                double avgErrorLatency = stats.totalErrorLatencyMs / stats.errorCount;
                double minError = (stats.minErrorLatencyMs == std::numeric_limits<double>::max()) 
                                    ? 0.0 : stats.minErrorLatencyMs;
                double maxError = (stats.maxErrorLatencyMs == std::numeric_limits<double>::lowest()) 
                                    ? 0.0 : stats.maxErrorLatencyMs;
                
                payload["error_count"] = stats.errorCount;
                payload["error_latency_avg_ms"] = avgErrorLatency;
                payload["error_latency_min_ms"] = minError;
                payload["error_latency_max_ms"] = maxError;
            } else {
                payload["error_count"] = 0;
            }
            
            // Total counts
            uint32_t totalCalls = stats.successCount + stats.errorCount;
            payload["total_count"] = totalCalls;
            
            // T2 marker: Use AGW_MARKER_SERVICE_METHOD_STAT since plugin_name and service_name are in payload
            LOGINFO("Sending service method stats to T2");
            Exchange::GatewayContext sysContext = CreateSystemContext();
            SendT2Event(AGW_MARKER_SERVICE_METHOD_STAT, payload, sysContext);
            
            LOGTRACE("Service method stats sent: %s::%s (total=%u, success=%u, error=%u, avg_success_latency=%.2f ms)",
                    stats.pluginName.c_str(), stats.serviceName.c_str(),
                    totalCalls, stats.successCount, stats.errorCount,
                    stats.successCount > 0 ? stats.totalSuccessLatencyMs / stats.successCount : 0.0);
        }

        LOGINFO("Service method stats sent: %zu plugin/service combinations", serviceMethodStats.size());
    }

    Exchange::GatewayContext AppGatewayTelemetry::CreateSystemContext()
    {
        Exchange::GatewayContext sysContext;
        sysContext.requestId = 0;        // 0 indicates system/aggregated metric
        sysContext.connectionId = 0;     // 0 indicates system/aggregated metric
        sysContext.appId = "SYSTEM";     // System identifier for aggregated metrics
        return sysContext;
    }

    void AppGatewayTelemetry::SendT2Event(const char* marker, const std::string& payload, const Exchange::GatewayContext& context)
    {
        // Build a JSON object with context prepended to the payload
        JsonObject contextPayload;

        // Context fields first
        // Skip adding context fields for UNKNOWN or SYSTEM contexts
        bool includeContext = (context.appId != "UNKNOWN" && context.appId != "SYSTEM");

        if (includeContext) {
            contextPayload["request_id"] = context.requestId;
            contextPayload["connection_id"] = context.connectionId;
            contextPayload["app_id"] = context.appId;
        }

        // Parse the payload string as JSON if possible, otherwise include as raw string
        JsonObject payloadObj;
        if (payloadObj.FromString(payload)) {
            // Payload is valid JSON - merge its fields into contextPayload
            auto it = payloadObj.Variants();
            while (it.Next()) {
                const auto& key = it.Label();
                const auto& value = it.Current();
                contextPayload[key] = value;
            }
        } else {
            // Payload is not JSON - include it as a "data" field
            contextPayload["data"] = payload;
        }

        // Format according to telemetry format setting and send
        std::string formattedPayload = FormatTelemetryPayload(contextPayload);

        // The T2 API signature takes non-const char* but doesn't modify the strings
        LOGINFO("marker=%s, payload=%s", marker, formattedPayload.c_str());
        Utils::Telemetry::sendMessage(const_cast<char*>(marker), 
                                        const_cast<char*>(formattedPayload.c_str()));
    }

    void AppGatewayTelemetry::SendT2Event(const char* marker, const JsonObject& payload, const Exchange::GatewayContext& context)
    {
        // Build a new JSON object with context prepended
        JsonObject contextPayload;

        // Context fields first
        // Skip adding context fields for UNKNOWN or SYSTEM contexts
        bool includeContext = (context.appId != "UNKNOWN" && context.appId != "SYSTEM");

        if (includeContext) {
            contextPayload["request_id"] = context.requestId;
            contextPayload["connection_id"] = context.connectionId;
            contextPayload["app_id"] = context.appId;
        }

        // Merge payload fields into contextPayload
        auto it = payload.Variants();
        while (it.Next()) {
            const auto& key = it.Label();
            const auto& value = it.Current();
            contextPayload[key] = value;
        }

        // Format according to telemetry format setting and send
        std::string formattedPayload = FormatTelemetryPayload(contextPayload);

        // The T2 API signature takes non-const char* but doesn't modify the strings
        LOGINFO("marker=%s, payload=%s", marker, formattedPayload.c_str());
        Utils::Telemetry::sendMessage(const_cast<char*>(marker),
                                        const_cast<char*>(formattedPayload.c_str()));
    }

    void AppGatewayTelemetry::SendT2Event(const char* marker, const std::string& rawPayload)
    {
        // Raw overload: sends the payload string as-is to T2 without any JSON wrapping or
        // context fields prepended. Intended for _split markers whose eventData is already
        // a correctly-formatted CSV string (e.g. APP_METRICS_ERROR_split, APP_SIGNIN_STATUS_split,
        // APP_READY_split).
        LOGINFO("marker=%s, payload=%s", marker, rawPayload.c_str());
        Utils::Telemetry::sendMessage(const_cast<char*>(marker),
                                      const_cast<char*>(rawPayload.c_str()));
    }

    void AppGatewayTelemetry::ResetHealthStats()
    {
        // Note: We don't reset websocketConnections as it represents current state
        mHealthStats.totalCalls.store(0, std::memory_order_relaxed);
        mHealthStats.totalResponses.store(0, std::memory_order_relaxed);
        mHealthStats.successfulCalls.store(0, std::memory_order_relaxed);
        mHealthStats.failedCalls.store(0, std::memory_order_relaxed);

        // Clear request state tracking after reporting
        mRequestStates.clear();
        LOGTRACE("Request states cleared after reporting");
    }

    void AppGatewayTelemetry::ResetApiErrorStats()
    {
        mApiErrorCounts.clear();
    }

    void AppGatewayTelemetry::ResetExternalServiceErrorStats()
    {
        mExternalServiceErrorCounts.clear();
    }

    void AppGatewayTelemetry::ResetApiMethodStats()
    {
        mApiMethodStats.clear();
    }

    void AppGatewayTelemetry::ResetApiLatencyStats()
    {
        mApiLatencyStats.clear();
    }

    void AppGatewayTelemetry::ResetServiceLatencyStats()
    {
        mServiceLatencyStats.clear();
    }

    void AppGatewayTelemetry::ResetServiceMethodStats()
    {
        mServiceMethodStats.clear();
    }

    std::string AppGatewayTelemetry::FormatTelemetryPayload(const JsonObject& jsonPayload)
    {
        TelemetryFormat telemetryFormat;
        {
            Core::SafeSyncType<Core::CriticalSection> lock(mAdminLock);
            telemetryFormat = mTelemetryFormat;
        }

        if (telemetryFormat == TelemetryFormat::JSON) {
            // JSON format: Return as-is
            std::string payloadStr;
            jsonPayload.ToString(payloadStr);
            return payloadStr;
        }

        // COMPACT format: Extract values only, comma-separated
        // For arrays containing objects, flatten to key:value pairs
        std::ostringstream oss;
        bool first = true;

        auto it = jsonPayload.Variants();
        while (it.Next()) {
            const auto& value = it.Current();
            
            if (!first) {
                oss << ",";
            }
            first = false;

            // Check if this is an array (for nested structures like api_failures)
            if (value.Content() == Core::JSON::Variant::type::ARRAY) {
                // Handle array of objects - wrap each item in parentheses
                // e.g., (GetData,5),(SetConfig,2),(LoadResource,1)
                JsonArray arr(value.Array());
                auto arrIt = arr.Elements();
                bool firstArrItem = true;
                
                while (arrIt.Next()) {
                    if (!firstArrItem) {
                        oss << ",";
                    }
                    firstArrItem = false;
                    
                    // Each array element is an object like {"api":"GetData","count":5}
                    // or {"service":"AuthService","count":3}
                    // Wrap in parentheses with comma-separated values inside
                    oss << "(";
                    JsonObject obj(arrIt.Current());
                    
                    auto objIt = obj.Variants();
                    bool firstField = true;
                    while (objIt.Next()) {
                        const auto& field = objIt.Current();
                        if (!firstField) {
                            oss << ",";
                        }
                        firstField = false;
                        
                        if (field.Content() == Core::JSON::Variant::type::STRING) {
                            oss << field.String();
                        } else if (field.Content() == Core::JSON::Variant::type::NUMBER) {
                            double num = field.Number();
                            if (num == static_cast<int64_t>(num)) {
                                oss << static_cast<int64_t>(num);
                            } else {
                                oss << std::fixed << std::setprecision(2) << num;
                            }
                        }
                    }
                    oss << ")";
                }
            } else if (value.Content() == Core::JSON::Variant::type::STRING) {
                oss << value.String();
            } else if (value.Content() == Core::JSON::Variant::type::NUMBER) {
                // Check if it's a floating point or integer
                double num = value.Number();
                if (num == static_cast<int64_t>(num)) {
                    oss << static_cast<int64_t>(num);
                } else {
                    oss << std::fixed << std::setprecision(2) << num;
                }
            } else if (value.Content() == Core::JSON::Variant::type::BOOLEAN) {
                oss << (value.Boolean() ? "true" : "false");
            }
        }

        return oss.str();
    }

    // TelemetrySnapshot Implementation

    void AppGatewayTelemetry::TelemetrySnapshot::SendAll()
    {
        if (nullptr == parent) {
            LOGERR("TelemetrySnapshot::SendAll: Invalid parent pointer");
            return;
        }

        LOGINFO("TelemetrySnapshot: Sending telemetry data (reporting interval: %u sec)", reportingIntervalSec);

        // Send all aggregated telemetry data
        // Note: These methods use the snapshot's data members directly
        
        // Send health statistics
        SendHealthStats();
        
        // Send API/method statistics  
        SendApiMethodStats();
        SendApiLatencyStats();
        
        // Send service statistics
        SendServiceMethodStats();
        SendServiceLatencyStats();
        
        // Send error statistics
        SendApiErrorStats();
        SendExternalServiceErrorStats();
        
        // Send generic aggregated metrics
        SendAggregatedMetrics();

        LOGTRACE("TelemetrySnapshot: All telemetry data sent successfully");
    }

    void AppGatewayTelemetry::TelemetrySnapshot::SendHealthStats()
    {
        // All entries in requestStates are pending (responded entries are erased immediately)
        uint32_t pendingCount = static_cast<uint32_t>(requestStates.size());

        // Only send if there's data
        if (totalCalls == 0 && websocketConnections == 0 && pendingCount == 0) {
            LOGINFO("TelemetrySnapshot: No health stats to report");
            return;
        }

        // Send all health stats in a single consolidated payload to T2
        JsonObject healthPayload;
        healthPayload["reporting_interval_sec"] = reportingIntervalSec;
        healthPayload["websocket_connections"] = websocketConnections;
        healthPayload["total_calls"] = totalCalls;
        healthPayload["total_responses"] = totalResponses;
        healthPayload["successful_calls"] = successfulCalls;
        healthPayload["failed_calls"] = failedCalls;
        healthPayload["unit"] = AGW_UNIT_COUNT;

        //LOGINFO("TelemetrySnapshot: Sending health stats (pending=%u)", pendingCount);
        LOGINFO("TelemetrySnapshot: Sending health stats");
        Exchange::GatewayContext sysContext = parent->CreateSystemContext();
        parent->SendT2Event(AGW_MARKER_HEALTH_STATS, healthPayload, sysContext);

        LOGTRACE("TelemetrySnapshot: Health stats sent: ws=%u, total=%u, responses=%u, success=%u, failed=%u, pending=%u",
                websocketConnections, totalCalls, totalResponses, successfulCalls, failedCalls, pendingCount);
    }

    void AppGatewayTelemetry::TelemetrySnapshot::SendApiMethodStats()
    {
        if (apiMethodStats.empty()) {
            LOGTRACE("TelemetrySnapshot: No API method stats to report");
            return;
        }

        // Send each plugin/method combination as a separate T2 event
        for (const auto& item : apiMethodStats) {
            const ApiMethodStats& stats = item.second;
            
            if (stats.successCount == 0 && stats.errorCount == 0) {
                continue;
            }

            // Build detailed payload with plugin name, method name, counters, and latency stats
            JsonObject payload;
            payload["plugin_name"] = stats.pluginName;
            payload["method_name"] = stats.methodName;
            payload["reporting_interval_sec"] = reportingIntervalSec;
            
            // Success metrics
            if (stats.successCount > 0) {
                double avgSuccessLatency = stats.totalSuccessLatencyMs / stats.successCount;
                double minSuccess = (stats.minSuccessLatencyMs == std::numeric_limits<double>::max()) 
                                    ? 0.0 : stats.minSuccessLatencyMs;
                double maxSuccess = (stats.maxSuccessLatencyMs == std::numeric_limits<double>::lowest()) 
                                    ? 0.0 : stats.maxSuccessLatencyMs;
                
                payload["success_count"] = stats.successCount;
                payload["success_latency_min_ms"] = minSuccess;
                payload["success_latency_max_ms"] = maxSuccess;
                payload["success_latency_avg_ms"] = avgSuccessLatency;
            } else {
                payload["success_count"] = 0;
            }
            
            // Error metrics
            if (stats.errorCount > 0) {
                double avgErrorLatency = stats.totalErrorLatencyMs / stats.errorCount;
                double minError = (stats.minErrorLatencyMs == std::numeric_limits<double>::max()) 
                                    ? 0.0 : stats.minErrorLatencyMs;
                double maxError = (stats.maxErrorLatencyMs == std::numeric_limits<double>::lowest()) 
                                    ? 0.0 : stats.maxErrorLatencyMs;
                
                payload["error_count"] = stats.errorCount;
                payload["error_latency_min_ms"] = minError;
                payload["error_latency_max_ms"] = maxError;
                payload["error_latency_avg_ms"] = avgErrorLatency;
            } else {
                payload["error_count"] = 0;
            }
            
            // Total counts
            uint32_t totalCalls = stats.successCount + stats.errorCount;
            payload["total_count"] = totalCalls;
            
            Exchange::GatewayContext sysContext = parent->CreateSystemContext();
            parent->SendT2Event(AGW_MARKER_API_METHOD_STAT, payload, sysContext);
        }

        LOGINFO("TelemetrySnapshot: API method stats sent: %zu plugin/method combinations", apiMethodStats.size());
    }

    void AppGatewayTelemetry::TelemetrySnapshot::SendApiLatencyStats()
    {
        if (apiLatencyStats.empty()) {
            LOGTRACE("TelemetrySnapshot: No API latency stats to report");
            return;
        }

        // Send each plugin/API combination
        for (const auto& item : apiLatencyStats) {
            const ApiLatencyStats& stats = item.second;
            
            if (stats.count == 0) {
                continue;
            }

            JsonObject payload;
            payload["plugin_name"] = stats.pluginName;
            payload["api_name"] = stats.apiName;
            payload["reporting_interval_sec"] = reportingIntervalSec;
            payload["count"] = stats.count;
            
            double avgLatency = stats.totalLatencyMs / stats.count;
            double minLatency = (stats.minLatencyMs == std::numeric_limits<double>::max()) 
                                ? 0.0 : stats.minLatencyMs;
            double maxLatency = (stats.maxLatencyMs == std::numeric_limits<double>::lowest()) 
                                ? 0.0 : stats.maxLatencyMs;
            
            payload["avg_ms"] = avgLatency;
            payload["min_ms"] = minLatency;
            payload["max_ms"] = maxLatency;
            payload["unit"] = AGW_UNIT_MILLISECONDS;
            
            Exchange::GatewayContext sysContext = parent->CreateSystemContext();
            parent->SendT2Event(AGW_MARKER_API_LATENCY, payload, sysContext);
        }

        LOGINFO("TelemetrySnapshot: API latency stats sent: %zu plugin/API combinations", apiLatencyStats.size());
    }

    void AppGatewayTelemetry::TelemetrySnapshot::SendServiceMethodStats()
    {
        if (serviceMethodStats.empty()) {
            LOGTRACE("TelemetrySnapshot: No service method stats to report");
            return;
        }

        // Send each plugin/service combination
        for (const auto& item : serviceMethodStats) {
            const ServiceMethodStats& stats = item.second;
            
            if (stats.successCount == 0 && stats.errorCount == 0) {
                continue;
            }

            JsonObject payload;
            payload["plugin_name"] = stats.pluginName;
            payload["service_name"] = stats.serviceName;
            payload["reporting_interval_sec"] = reportingIntervalSec;
            
            // Success metrics
            if (stats.successCount > 0) {
                double avgSuccessLatency = stats.totalSuccessLatencyMs / stats.successCount;
                double minSuccess = (stats.minSuccessLatencyMs == std::numeric_limits<double>::max()) 
                                    ? 0.0 : stats.minSuccessLatencyMs;
                double maxSuccess = (stats.maxSuccessLatencyMs == std::numeric_limits<double>::lowest()) 
                                    ? 0.0 : stats.maxSuccessLatencyMs;
                
                payload["success_count"] = stats.successCount;
                payload["success_latency_min_ms"] = minSuccess;
                payload["success_latency_max_ms"] = maxSuccess;
                payload["success_latency_avg_ms"] = avgSuccessLatency;
            } else {
                payload["success_count"] = 0;
            }
            
            // Error metrics
            if (stats.errorCount > 0) {
                double avgErrorLatency = stats.totalErrorLatencyMs / stats.errorCount;
                double minError = (stats.minErrorLatencyMs == std::numeric_limits<double>::max()) 
                                    ? 0.0 : stats.minErrorLatencyMs;
                double maxError = (stats.maxErrorLatencyMs == std::numeric_limits<double>::lowest()) 
                                    ? 0.0 : stats.maxErrorLatencyMs;
                
                payload["error_count"] = stats.errorCount;
                payload["error_latency_min_ms"] = minError;
                payload["error_latency_max_ms"] = maxError;
                payload["error_latency_avg_ms"] = avgErrorLatency;
            } else {
                payload["error_count"] = 0;
            }
            
            Exchange::GatewayContext sysContext = parent->CreateSystemContext();
            parent->SendT2Event(AGW_MARKER_SERVICE_METHOD_STAT, payload, sysContext);
        }

        LOGINFO("TelemetrySnapshot: Service method stats sent: %zu plugin/service combinations", serviceMethodStats.size());
    }

    void AppGatewayTelemetry::TelemetrySnapshot::SendServiceLatencyStats()
    {
        if (serviceLatencyStats.empty()) {
            LOGTRACE("TelemetrySnapshot: No service latency stats to report");
            return;
        }

        // Send each plugin/service combination
        for (const auto& item : serviceLatencyStats) {
            const ServiceLatencyStats& stats = item.second;
            
            if (stats.count == 0) {
                continue;
            }

            JsonObject payload;
            payload["plugin_name"] = stats.pluginName;
            payload["service_name"] = stats.serviceName;
            payload["reporting_interval_sec"] = reportingIntervalSec;
            payload["count"] = stats.count;
            
            double avgLatency = stats.totalLatencyMs / stats.count;
            double minLatency = (stats.minLatencyMs == std::numeric_limits<double>::max()) 
                                ? 0.0 : stats.minLatencyMs;
            double maxLatency = (stats.maxLatencyMs == std::numeric_limits<double>::lowest()) 
                                ? 0.0 : stats.maxLatencyMs;
            
            payload["avg_ms"] = avgLatency;
            payload["min_ms"] = minLatency;
            payload["max_ms"] = maxLatency;
            payload["unit"] = AGW_UNIT_MILLISECONDS;
            
            Exchange::GatewayContext sysContext = parent->CreateSystemContext();
            parent->SendT2Event(AGW_MARKER_SERVICE_LATENCY, payload, sysContext);
        }

        LOGINFO("TelemetrySnapshot: Service latency stats sent: %zu plugin/service combinations", serviceLatencyStats.size());
    }

    void AppGatewayTelemetry::TelemetrySnapshot::SendApiErrorStats()
    {
        if (apiErrorCounts.empty()) {
            LOGTRACE("TelemetrySnapshot: No API error stats to report");
            return;
        }

        // Send each API error count with common marker and API name in payload
        std::string metricName = std::string(AGW_MARKER_API_ERROR_COUNT);
        
        for (const auto& item : apiErrorCounts) {
            JsonObject metricPayload;
            metricPayload["reporting_interval_sec"] = reportingIntervalSec;
            metricPayload["ApiName"] = item.first;
            metricPayload["count"] = item.second;
            metricPayload["unit"] = AGW_UNIT_COUNT;
            
            Exchange::GatewayContext sysContext = parent->CreateSystemContext();
            parent->SendT2Event(metricName.c_str(), metricPayload, sysContext);
        }

        LOGINFO("TelemetrySnapshot: API error stats sent: %zu APIs with errors", apiErrorCounts.size());
    }

    void AppGatewayTelemetry::TelemetrySnapshot::SendExternalServiceErrorStats()
    {
        if (externalServiceErrorCounts.empty()) {
            LOGTRACE("TelemetrySnapshot: No external service error stats to report");
            return;
        }

        // Send each external service error count with common marker and service name in payload
        std::string metricName = std::string(AGW_MARKER_EXT_SERVICE_ERROR_COUNT);
        
        for (const auto& item : externalServiceErrorCounts) {
            JsonObject metricPayload;
            metricPayload["reporting_interval_sec"] = reportingIntervalSec;
            metricPayload["ServiceName"] = item.first;
            metricPayload["count"] = item.second;
            metricPayload["unit"] = AGW_UNIT_COUNT;
            
            Exchange::GatewayContext sysContext = parent->CreateSystemContext();
            parent->SendT2Event(metricName.c_str(), metricPayload, sysContext);
        }

        LOGINFO("TelemetrySnapshot: External service error stats sent: %zu services with errors", 
                externalServiceErrorCounts.size());
    }

    void AppGatewayTelemetry::TelemetrySnapshot::SendAggregatedMetrics()
    {
        if (metricsCache.empty()) {
            LOGTRACE("TelemetrySnapshot: No aggregated metrics to report");
            return;
        }

        // Send each metric with its own marker
        for (const auto& item : metricsCache) {
            const std::string& metricName = item.first;
            const MetricData& data = item.second;
            
            if (data.count == 0) {
                continue;
            }

            double minVal = (data.min == std::numeric_limits<double>::max()) ? 0.0 : data.min;
            double maxVal = (data.max == std::numeric_limits<double>::lowest()) ? 0.0 : data.max;
            double avgVal = data.sum / static_cast<double>(data.count);

            JsonObject payload;
            payload["min"] = minVal;
            payload["max"] = maxVal;
            payload["count"] = data.count;
            payload["avg"] = avgVal;
            payload["unit"] = data.unit;
            payload["reporting_interval_sec"] = reportingIntervalSec;

            Exchange::GatewayContext sysContext = parent->CreateSystemContext();
            parent->SendT2Event(metricName.c_str(), payload, sysContext);
        }

        LOGINFO("TelemetrySnapshot: Aggregated metrics sent: %zu metrics", metricsCache.size());
    }

    // FlushJob Implementation - Async telemetry sending (DEPRECATED - kept for compatibility)

    void AppGatewayTelemetry::FlushJob::Dispatch()
    {
        if (nullptr == mSnapshot || nullptr == mSnapshot->parent) {
            LOGERR("FlushJob: Invalid snapshot or parent pointer");
            return;
        }

        LOGINFO("FlushJob: Sending telemetry snapshot asynchronously");

        // Send all aggregated data from snapshot
        SendHealthStats();
        SendApiMethodStats();
        SendApiLatencyStats();
        SendServiceMethodStats();
        SendServiceLatencyStats();
        SendApiErrorStats();
        SendExternalServiceErrorStats();
        SendAggregatedMetrics();

        LOGTRACE("FlushJob: Telemetry snapshot sent successfully");
    }

    void AppGatewayTelemetry::FlushJob::SendHealthStats()
    {
        // All entries in requestStates are pending (responded entries are erased immediately)
        uint32_t pendingCount = static_cast<uint32_t>(mSnapshot->requestStates.size());

        // Only send if there's data
        if (mSnapshot->totalCalls == 0 && mSnapshot->websocketConnections == 0 && pendingCount == 0) {
            LOGINFO("FlushJob: No health stats to report");
            return;
        }

        // Send all health stats in a single consolidated payload
        JsonObject healthPayload;
        healthPayload["reporting_interval_sec"] = mSnapshot->reportingIntervalSec;
        healthPayload["websocket_connections"] = mSnapshot->websocketConnections;
        healthPayload["total_calls"] = mSnapshot->totalCalls;
        healthPayload["total_responses"] = mSnapshot->totalResponses;
        healthPayload["successful_calls"] = mSnapshot->successfulCalls;
        healthPayload["failed_calls"] = mSnapshot->failedCalls;
        healthPayload["unit"] = AGW_UNIT_COUNT;

        //LOGINFO("FlushJob: Sending health stats (pending=%u)", pendingCount);
        LOGINFO("FlushJob: Sending health stats");
        Exchange::GatewayContext sysContext = AppGatewayTelemetry::CreateSystemContext();
        mSnapshot->parent->SendT2Event(AGW_MARKER_HEALTH_STATS, healthPayload, sysContext);
    }

    void AppGatewayTelemetry::FlushJob::SendApiMethodStats()
    {
        if (mSnapshot->apiMethodStats.empty()) {
            LOGTRACE("FlushJob: No API method stats to report");
            return;
        }

        for (const auto& item : mSnapshot->apiMethodStats) {
            const ApiMethodStats& stats = item.second;

            if (stats.successCount == 0 && stats.errorCount == 0) {
                continue;
            }

            JsonObject payload;
            payload["plugin_name"] = stats.pluginName;
            payload["method_name"] = stats.methodName;
            payload["reporting_interval_sec"] = mSnapshot->reportingIntervalSec;

            // Success metrics
            if (stats.successCount > 0) {
                double avgSuccessLatency = stats.totalSuccessLatencyMs / stats.successCount;
                double minSuccess = (stats.minSuccessLatencyMs == std::numeric_limits<double>::max()) 
                                    ? 0.0 : stats.minSuccessLatencyMs;
                double maxSuccess = (stats.maxSuccessLatencyMs == std::numeric_limits<double>::lowest()) 
                                    ? 0.0 : stats.maxSuccessLatencyMs;

                payload["success_count"] = stats.successCount;
                payload["success_latency_min_ms"] = minSuccess;
                payload["success_latency_max_ms"] = maxSuccess;
                payload["success_latency_avg_ms"] = avgSuccessLatency;
            } else {
                payload["success_count"] = 0;
            }

            // Error metrics
            if (stats.errorCount > 0) {
                double avgErrorLatency = stats.totalErrorLatencyMs / stats.errorCount;
                double minError = (stats.minErrorLatencyMs == std::numeric_limits<double>::max()) 
                                    ? 0.0 : stats.minErrorLatencyMs;
                double maxError = (stats.maxErrorLatencyMs == std::numeric_limits<double>::lowest()) 
                                    ? 0.0 : stats.maxErrorLatencyMs;

                payload["error_count"] = stats.errorCount;
                payload["error_latency_min_ms"] = minError;
                payload["error_latency_max_ms"] = maxError;
                payload["error_latency_avg_ms"] = avgErrorLatency;
            } else {
                payload["error_count"] = 0;
            }

            payload["total_count"] = stats.successCount + stats.errorCount;
            
            LOGINFO("FlushJob: Sending API method stats");
            Exchange::GatewayContext sysContext = AppGatewayTelemetry::CreateSystemContext();
            mSnapshot->parent->SendT2Event(AGW_MARKER_API_METHOD_STAT, payload, sysContext);
        }

        LOGINFO("FlushJob: API method stats sent: %zu plugin/method combinations", mSnapshot->apiMethodStats.size());
    }

    void AppGatewayTelemetry::FlushJob::SendApiLatencyStats()
    {
        if (mSnapshot->apiLatencyStats.empty()) {
            LOGTRACE("FlushJob: No API latency stats to report");
            return;
        }

        for (const auto& item : mSnapshot->apiLatencyStats) {
            const ApiLatencyStats& stats = item.second;
            
            if (stats.count == 0) {
                continue;
            }

            JsonObject payload;
            payload["plugin_name"] = stats.pluginName;
            payload["api_name"] = stats.apiName;
            payload["reporting_interval_sec"] = mSnapshot->reportingIntervalSec;
            payload["count"] = stats.count;
            
            double avgLatency = stats.totalLatencyMs / stats.count;
            double minLatency = (stats.minLatencyMs == std::numeric_limits<double>::max()) 
                                ? 0.0 : stats.minLatencyMs;
            double maxLatency = (stats.maxLatencyMs == std::numeric_limits<double>::lowest()) 
                                ? 0.0 : stats.maxLatencyMs;
            
            payload["avg_ms"] = avgLatency;
            payload["min_ms"] = minLatency;
            payload["max_ms"] = maxLatency;
            payload["unit"] = AGW_UNIT_MILLISECONDS;
            
            LOGINFO("FlushJob: Sending API latency stats");
            Exchange::GatewayContext sysContext = AppGatewayTelemetry::CreateSystemContext();
            mSnapshot->parent->SendT2Event(AGW_MARKER_API_LATENCY, payload, sysContext);
        }
        
        LOGINFO("FlushJob: API latency stats sent: %zu plugin/API combinations", mSnapshot->apiLatencyStats.size());
    }

    void AppGatewayTelemetry::FlushJob::SendServiceMethodStats()
    {
        if (mSnapshot->serviceMethodStats.empty()) {
            LOGTRACE("FlushJob: No service method stats to report");
            return;
        }

        for (const auto& item : mSnapshot->serviceMethodStats) {
            const ServiceMethodStats& stats = item.second;
            
            if (stats.successCount == 0 && stats.errorCount == 0) {
                continue;
            }

            JsonObject payload;
            payload["plugin_name"] = stats.pluginName;
            payload["service_name"] = stats.serviceName;
            payload["reporting_interval_sec"] = mSnapshot->reportingIntervalSec;
            
            // Success metrics
            if (stats.successCount > 0) {
                double avgSuccessLatency = stats.totalSuccessLatencyMs / stats.successCount;
                double minSuccess = (stats.minSuccessLatencyMs == std::numeric_limits<double>::max()) 
                                    ? 0.0 : stats.minSuccessLatencyMs;
                double maxSuccess = (stats.maxSuccessLatencyMs == std::numeric_limits<double>::lowest()) 
                                    ? 0.0 : stats.maxSuccessLatencyMs;
                
                payload["success_count"] = stats.successCount;
                payload["success_latency_avg_ms"] = avgSuccessLatency;
                payload["success_latency_min_ms"] = minSuccess;
                payload["success_latency_max_ms"] = maxSuccess;
            } else {
                payload["success_count"] = 0;
            }
            
            // Error metrics
            if (stats.errorCount > 0) {
                double avgErrorLatency = stats.totalErrorLatencyMs / stats.errorCount;
                double minError = (stats.minErrorLatencyMs == std::numeric_limits<double>::max()) 
                                  ? 0.0 : stats.minErrorLatencyMs;
                double maxError = (stats.maxErrorLatencyMs == std::numeric_limits<double>::lowest()) 
                                  ? 0.0 : stats.maxErrorLatencyMs;
                
                payload["error_count"] = stats.errorCount;
                payload["error_latency_avg_ms"] = avgErrorLatency;
                payload["error_latency_min_ms"] = minError;
                payload["error_latency_max_ms"] = maxError;
            } else {
                payload["error_count"] = 0;
            }
            
            payload["total_count"] = stats.successCount + stats.errorCount;
            
            LOGINFO("FlushJob: Sending service method stats");
            Exchange::GatewayContext sysContext = AppGatewayTelemetry::CreateSystemContext();
            mSnapshot->parent->SendT2Event(AGW_MARKER_SERVICE_METHOD_STAT, payload, sysContext);
        }
        
        LOGINFO("FlushJob: Service method stats sent: %zu plugin/service combinations", mSnapshot->serviceMethodStats.size());
    }

    void AppGatewayTelemetry::FlushJob::SendServiceLatencyStats()
    {
        if (mSnapshot->serviceLatencyStats.empty()) {
            LOGTRACE("FlushJob: No service latency stats to report");
            return;
        }

        for (const auto& item : mSnapshot->serviceLatencyStats) {
            const ServiceLatencyStats& stats = item.second;
            
            if (stats.count == 0) {
                continue;
            }

            JsonObject payload;
            payload["plugin_name"] = stats.pluginName;
            payload["service_name"] = stats.serviceName;
            payload["reporting_interval_sec"] = mSnapshot->reportingIntervalSec;
            payload["count"] = stats.count;
            
            double avgLatency = stats.totalLatencyMs / stats.count;
            double minLatency = (stats.minLatencyMs == std::numeric_limits<double>::max()) 
                                ? 0.0 : stats.minLatencyMs;
            double maxLatency = (stats.maxLatencyMs == std::numeric_limits<double>::lowest()) 
                                ? 0.0 : stats.maxLatencyMs;
            
            payload["avg_ms"] = avgLatency;
            payload["min_ms"] = minLatency;
            payload["max_ms"] = maxLatency;
            payload["unit"] = AGW_UNIT_MILLISECONDS;
            
            LOGINFO("FlushJob: Sending service latency stats");
            Exchange::GatewayContext sysContext = AppGatewayTelemetry::CreateSystemContext();
            mSnapshot->parent->SendT2Event(AGW_MARKER_SERVICE_LATENCY, payload, sysContext);
        }
        
        LOGINFO("FlushJob: Service latency stats sent: %zu plugin/service combinations", mSnapshot->serviceLatencyStats.size());
    }

    void AppGatewayTelemetry::FlushJob::SendApiErrorStats()
    {
        if (mSnapshot->apiErrorCounts.empty()) {
            LOGTRACE("FlushJob: No API error stats to report");
            return;
        }

        std::string metricName = std::string(AGW_MARKER_API_ERROR_COUNT);
        
        for (const auto& item : mSnapshot->apiErrorCounts) {
            JsonObject metricPayload;
            metricPayload["reporting_interval_sec"] = mSnapshot->reportingIntervalSec;
            metricPayload["ApiName"] = item.first;
            metricPayload["count"] = item.second;
            metricPayload["unit"] = AGW_UNIT_COUNT;
            
            LOGINFO("FlushJob: Sending API error metric");
            Exchange::GatewayContext sysContext = AppGatewayTelemetry::CreateSystemContext();
            mSnapshot->parent->SendT2Event(metricName.c_str(), metricPayload, sysContext);
        }
        
        LOGINFO("FlushJob: API error stats sent: %zu APIs with errors", mSnapshot->apiErrorCounts.size());
    }

    void AppGatewayTelemetry::FlushJob::SendExternalServiceErrorStats()
    {
        if (mSnapshot->externalServiceErrorCounts.empty()) {
            LOGTRACE("FlushJob: No external service error stats to report");
            return;
        }

        std::string metricName = std::string(AGW_MARKER_EXT_SERVICE_ERROR_COUNT);
        
        for (const auto& item : mSnapshot->externalServiceErrorCounts) {
            JsonObject metricPayload;
            metricPayload["reporting_interval_sec"] = mSnapshot->reportingIntervalSec;
            metricPayload["ServiceName"] = item.first;
            metricPayload["count"] = item.second;
            metricPayload["unit"] = AGW_UNIT_COUNT;
            
            LOGINFO("FlushJob: Sending external service error metric");
            Exchange::GatewayContext sysContext = AppGatewayTelemetry::CreateSystemContext();
            mSnapshot->parent->SendT2Event(metricName.c_str(), metricPayload, sysContext);
        }
        
        LOGINFO("FlushJob: External service error stats sent: %zu services with errors", 
                mSnapshot->externalServiceErrorCounts.size());
    }

    void AppGatewayTelemetry::FlushJob::SendAggregatedMetrics()
    {
        if (mSnapshot->metricsCache.empty()) {
            LOGTRACE("FlushJob: No aggregated metrics to report");
            return;
        }

        for (const auto& item : mSnapshot->metricsCache) {
            const std::string& metricName = item.first;
            const MetricData& data = item.second;
            
            if (data.count == 0) {
                continue;
            }

            double minVal = (data.min == std::numeric_limits<double>::max()) ? 0.0 : data.min;
            double maxVal = (data.max == std::numeric_limits<double>::lowest()) ? 0.0 : data.max;
            double avgVal = data.sum / static_cast<double>(data.count);

            JsonObject payload;
            payload["min"] = minVal;
            payload["max"] = maxVal;
            payload["count"] = data.count;
            payload["avg"] = avgVal;
            payload["unit"] = data.unit;
            payload["reporting_interval_sec"] = mSnapshot->reportingIntervalSec;

            LOGINFO("FlushJob: Sending aggregated metric: %s", metricName.c_str());
            Exchange::GatewayContext sysContext = AppGatewayTelemetry::CreateSystemContext();
            mSnapshot->parent->SendT2Event(metricName.c_str(), payload, sysContext);
        }
    }

} // namespace Plugin
} // namespace WPEFramework
