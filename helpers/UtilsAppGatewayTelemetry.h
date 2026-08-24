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
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

/**
 * @file UtilsAppGatewayTelemetry.h
 * @brief Helper macros and utilities for reporting telemetry to AppGateway
 * 
 * This file provides a standardized way for plugins to report telemetry data
 * to the AppGateway telemetry aggregator via COM-RPC. The AppGateway aggregates
 * data and periodically reports to the T2 telemetry server.
 * 
 * ## Quick Start
 * 
 * 1. Include this header in your plugin:
 *    #include "UtilsAppGatewayTelemetry.h"
 * 
 * 2. Define the telemetry client in your plugin's .cpp file (top-level, before namespace):
 *    AGW_DEFINE_TELEMETRY_CLIENT(AGW_PLUGIN_YOUR_PLUGIN)
 * 
 * 3. In your plugin's Initialize(), init telemetry first, then start the bootstrap timer:
 *    AGW_TELEMETRY_INIT(service);
 *    AGW_RECORD_BOOTSTRAP_TIME();  // Timer starts here
 *    // ... plugin initialization code ...
 *    // Timer automatically records on scope exit
 * 
 * 4. Report high value error as events using the macros (all require context parameter):
 *    - AGW_REPORT_API_ERROR(context, "GetSettings", AGW_ERROR_TIMEOUT)
 *    - AGW_REPORT_EXTERNAL_SERVICE_ERROR(context, AGW_SERVICE_YOUR_SERVICE, AGW_ERROR_INTERFACE_UNAVAILABLE)
 * 
 * 5. Cleanup in Deinitialize:
 *    AGW_TELEMETRY_DEINIT()
 * 
 */

#include <interfaces/IAppGateway.h>
#include <chrono>
#include "UtilsLogging.h"
#include "UtilsCallsign.h"
#include "AppGatewayTelemetryMarkers.h"

// Ensure gDefaultLogLevel from utils.h (via UtilsCallsign.h) is considered used
// by checking the log level at static initialization time
namespace {
    static inline bool CheckLogLevelInitialized() {
        return (gDefaultLogLevel >= FATAL_LEVEL);
    }
    static const bool logLevelChecked [[maybe_unused]] = CheckLogLevelInitialized();
}

namespace WPEFramework {
namespace Plugin {
namespace AppGatewayTelemetryHelper {

    /**
     * @brief Telemetry client that manages connection to AppGateway's IAppGatewayTelemetry
     * 
     * This class provides a RAII-style wrapper for the telemetry interface.
     * It automatically acquires and releases the COM-RPC interface.
     * 
     * ## Events vs Metrics
     * 
     * **Events** (RecordEvent/RecordTelemetryEvent):
     * - For individual occurrences that happen at specific points in time
     * - Contains JSON payload with context about what happened
     * - Each event is sent to T2 immediately or queued individually
     * - Use for: errors, state changes, user actions
     * - Example: API error, service failure, user login
     * 
     * **Metrics** (RecordMetric/RecordTelemetryMetric):
     * - For numeric values that should be aggregated over time
     * - Aggregation includes: sum, count, min, max, average
     * - Aggregated values sent to T2 periodically (e.g., hourly)
     * - Use for: latencies, counters, measurements
     * - Example: API latency, service latency, request count
     */
    class TelemetryClient
    {
    public:
        TelemetryClient()
            : mService(nullptr)
            , mTelemetry(nullptr)
            , mPluginName()
        {
        }

        ~TelemetryClient()
        {
            Deinitialize();
        }

        /**
         * @brief Initialize the telemetry client
         * @param service The IShell service pointer
         * @param pluginName Name of the plugin (used in telemetry context)
         * @return true if successful, false otherwise
         */
        bool Initialize(PluginHost::IShell* service, const std::string& pluginName)
        {
            if (nullptr == service) {
                LOGERR("TelemetryClient: service is null");
                return false;
            }

            mService = service;
            mPluginName = pluginName;

            // Query for the AppGateway telemetry interface
            mTelemetry = service->QueryInterfaceByCallsign<Exchange::IAppGatewayTelemetry>(APP_GATEWAY_CALLSIGN);
            if (nullptr == mTelemetry) {
                LOGWARN("TelemetryClient: AppGateway telemetry interface not available: %s", pluginName.c_str());
                return false;
            }

            LOGINFO("TelemetryClient: Initialized for plugin '%s'", pluginName.c_str());
            return true;
        }

        /**
         * @brief Deinitialize and release the telemetry interface
         */
        void Deinitialize()
        {
            if (nullptr != mTelemetry) {
                mTelemetry->Release();
                mTelemetry = nullptr;
            }
            mService = nullptr;
            LOGINFO("TelemetryClient: Deinitialized");
        }

        /**
         * @brief Check if the telemetry client is initialized
         * @return true if initialized and interface is available
         */
        bool IsAvailable() const
        {
            if (nullptr == mService) {
                return false;
            }

            if (nullptr == mTelemetry) {
                mTelemetry = mService->QueryInterfaceByCallsign<Exchange::IAppGatewayTelemetry>(APP_GATEWAY_CALLSIGN);
            }
            return nullptr != mTelemetry;
        }

        /**
         * @brief Record a telemetry event
         * @param context Gateway context with request/connection/app info
         * @param eventName Event name (used as T2 marker)
         * @param eventData JSON string with event details
         * @return Core::hresult
         */
        Core::hresult RecordEvent(const Exchange::GatewayContext& context,
                                  const std::string& eventName, 
                                  const std::string& eventData)
        {
            if (!IsAvailable()) {
                return Core::ERROR_UNAVAILABLE;
            }

            return mTelemetry->RecordTelemetryEvent(context, eventName, eventData);
        }

        /**
         * @brief Record a telemetry metric
         * @param context Gateway context with request/connection/app info
         * @param metricName Metric name (used as T2 marker)
         * @param value Numeric value
         * @param unit Unit of measurement
         * @return Core::hresult
         */
        Core::hresult RecordMetric(const Exchange::GatewayContext& context,
                                   const std::string& metricName, 
                                   double value, 
                                   const std::string& unit)
        {
            if (!IsAvailable()) {
                return Core::ERROR_UNAVAILABLE;
            }

            return mTelemetry->RecordTelemetryMetric(context, metricName, value, unit);
        }

        /**
         * @brief Record an API error event (individual occurrence)
         * @param context Gateway context with request/connection/app info
         * @param apiName Name of the API that failed
         * @param errorCode Predefined error code from AppGatewayTelemetryMarkers.h
         * @return Core::hresult
         * 
         * This records an EVENT - each error occurrence is sent individually to T2.
         * Use this to track WHAT errors happened, not how many.
         */
        Core::hresult RecordApiError(const Exchange::GatewayContext& context,
                                     const std::string& apiName, 
                                     const std::string& errorCode)
        {
            JsonObject data;
            data["plugin"] = mPluginName;
            data["api"] = apiName;
            data["error"] = errorCode;

            std::string eventData;
            data.ToString(eventData);
            
            LOGTRACE("TelemetryClient: Recording API error - plugin=%s, api=%s, error=%s",
                     mPluginName.c_str(), apiName.c_str(), errorCode.c_str());

            return RecordEvent(context, AGW_MARKER_PLUGIN_API_ERROR, eventData);
        }

        /**
         * @brief Record an external service error event (individual occurrence)
         * @param context Gateway context with request/connection/app info
         * @param serviceName Predefined service name from AppGatewayTelemetryMarkers.h
         * @param errorCode Predefined error code from AppGatewayTelemetryMarkers.h
         * @return Core::hresult
         * 
         * This records an EVENT - each error occurrence is sent individually to T2.
         * Use this to track WHAT service errors happened, not how many.
         */
        Core::hresult RecordExternalServiceError(const Exchange::GatewayContext& context,
                                                  const std::string& serviceName, 
                                                  const std::string& errorCode)
        {
            JsonObject data;
            data["plugin"] = mPluginName;
            data["service"] = serviceName;
            data["error"] = errorCode;

            std::string eventData;
            data.ToString(eventData);

            LOGINFO("TelemetryClient: Recording external service error - plugin=%s, service=%s, error=%s",
                    mPluginName.c_str(), serviceName.c_str(), errorCode.c_str());

            return RecordEvent(context, AGW_MARKER_PLUGIN_EXT_SERVICE_ERROR, eventData);
        }

        /**
         * @brief Record an API latency metric (aggregated value)
         * @param context Gateway context with request/connection/app info
         * @param apiName Name of the API
         * @param latencyMs Latency in milliseconds
         * @return Core::hresult
         * 
         */
        Core::hresult RecordApiLatency(const Exchange::GatewayContext& context,
                                       const std::string& apiName, 
                                       double latencyMs)
        {
            std::string metricName = AGW_INTERNAL_PLUGIN_PREFIX + mPluginName + "_ApiName_" + apiName + "_ApiLatency_split";
            
            LOGTRACE("TelemetryClient: Recording API latency - plugin=%s, api=%s, latency=%.2fms, metric=%s",
                     mPluginName.c_str(), apiName.c_str(), latencyMs, metricName.c_str());

            return RecordMetric(context, metricName, latencyMs, AGW_UNIT_MILLISECONDS);
        }

        /**
         * @brief Record an external service latency metric (aggregated value)
         * @param context Gateway context with request/connection/app info
         * @param serviceName Predefined service name from AppGatewayTelemetryMarkers.h
         * @param latencyMs Latency in milliseconds
         * @return Core::hresult
         * 
         */
        Core::hresult RecordServiceLatency(const Exchange::GatewayContext& context,
                                           const std::string& serviceName, 
                                           double latencyMs)
        {
            std::string metricName = AGW_INTERNAL_PLUGIN_PREFIX + mPluginName + "_ServiceName_" + serviceName + "_ServiceLatency_split";
            
            LOGTRACE("TelemetryClient: Recording service latency - plugin=%s, service=%s, latency=%.2fms, metric=%s",
                     mPluginName.c_str(), serviceName.c_str(), latencyMs, metricName.c_str());

            return RecordMetric(context, metricName, latencyMs, AGW_UNIT_MILLISECONDS);
        }

        /**
         * @brief Record plugin bootstrap time
         * @param durationMs Bootstrap duration in milliseconds (supports sub-millisecond precision)
         * @return Core::hresult
         * 
         * Reports the time taken for this plugin to initialize.
         * Uses standard bootstrap metric marker. AppGatewayTelemetry aggregates
         * all plugin bootstrap times and increments the plugin counter automatically.
         */
        Core::hresult RecordBootstrapTime(double durationMs)
        {
            Exchange::GatewayContext context;
            context.requestId = 0;
            context.connectionId = 0;
            context.appId = mPluginName;  // Plugin identity in context

            LOGINFO("TelemetryClient: Recording bootstrap time - plugin=%s, duration=%.2fms",
                    mPluginName.c_str(), durationMs);

            // Use standard bootstrap metric - AppGatewayTelemetry will handle cumulative tracking
            return RecordMetric(context, AGW_MARKER_BOOTSTRAP_DURATION, 
                              durationMs, AGW_UNIT_MILLISECONDS);
        }

        /**
         * @brief Track response payload for automatic success/failure detection
         * @param context Gateway context with request/connection/app info
         * @param payload appGateway response payload string
         * @return Core::hresult
         */
        Core::hresult TrackResponsePayload(const Exchange::GatewayContext& context, const std::string& payload)
        {
            if (!IsAvailable()) {
                return Core::ERROR_UNAVAILABLE;
            }

            // Send to AppGateway for parsing and tracking
            return mTelemetry->RecordTelemetryEvent(context,
                                                     AGW_MARKER_RESPONSE_PAYLOAD_TRACKING,
                                                     payload);
        }

        /**
         * @brief Get the plugin name
         * @return The plugin name
         */
        const std::string& GetPluginName() const
        {
            return mPluginName;
        }

    private:
        PluginHost::IShell* mService;
        mutable Exchange::IAppGatewayTelemetry* mTelemetry;  // mutable for lazy initialization in IsAvailable()
        std::string mPluginName;
    };

} // namespace AppGatewayTelemetryHelper
} // namespace Plugin
} // namespace WPEFramework

//=============================================================================
// RAII HELPER CLASSES FOR AUTOMATIC TIMING
//=============================================================================

namespace WPEFramework {
namespace Plugin {
namespace AppGatewayTelemetryHelper {

    /**
     * @brief RAII timer for automatic bootstrap time tracking
     * 
     * Takes a TelemetryClient pointer to report bootstrap time via COM-RPC.
     * Timer starts on construction and reports on destruction.
     * 
     * @note This class is used by AGW_RECORD_BOOTSTRAP_TIME() macro.
     *       Direct instantiation is not recommended.
     */
    class ScopedBootstrapTimer
    {
    public:
        ScopedBootstrapTimer(TelemetryClient* client)
            : mClient(client)
            , mStartTime(std::chrono::steady_clock::now())
        {
        }

        ~ScopedBootstrapTimer()
        {
            if (nullptr == mClient || false == mClient->IsAvailable()) {
                return;
            }

            auto endTime = std::chrono::steady_clock::now();
            // Use microseconds for precision, then convert to milliseconds as double
            auto durationUs = std::chrono::duration_cast<std::chrono::microseconds>(
                endTime - mStartTime).count();
            double durationMs = durationUs / 1000.0;  // Convert to milliseconds with decimal precision
            
            // Report bootstrap time via TelemetryClient (COM-RPC to AppGateway)
            mClient->RecordBootstrapTime(durationMs);
        }

    private:
        TelemetryClient* mClient;
        std::chrono::steady_clock::time_point mStartTime;
    };

    /**
     * @brief RAII tracker for automatic API latency and error tracking
     * 
     * Times an API call from construction to destruction, automatically reporting:
     * - Success latency metric (if SetFailed() not called)
     * - Error event + error latency metric (if SetFailed() called)
     * 
     * @note This class is used by AGW_TRACK_API_CALL() macro.
     *       Direct instantiation is not recommended.
     * 
     * Usage:
     *   {
     *       ScopedApiTimer timer(&GetLocalTelemetryClient(), context, "GetSettings");
     *       // ... do API work ...
     *       if (failed) timer.SetFailed("TIMEOUT");
     *   } // Timer automatically reports on destruction
     */
    class ScopedApiTimer
    {
    public:
        ScopedApiTimer(TelemetryClient* client, const Exchange::GatewayContext& context, const std::string& apiName)
            : mClient(client)
            , mContext(context)
            , mApiName(apiName)
            , mFailed(false)
            , mErrorDetails()
            , mStartTime(std::chrono::steady_clock::now())
        {
        }

        ~ScopedApiTimer()
        {
            if (nullptr == mClient || false == mClient->IsAvailable()) {
                return;
            }

            auto endTime = std::chrono::steady_clock::now();
            // Use microseconds for precision, then convert to milliseconds as double
            auto durationUs = std::chrono::duration_cast<std::chrono::microseconds>(
                endTime - mStartTime).count();
            double durationMs = durationUs / 1000.0;  // Convert to milliseconds with decimal precision
            
            if (mFailed) {
                mClient->RecordApiError(mContext, mApiName, mErrorDetails);
                std::string metricName = AGW_INTERNAL_PLUGIN_PREFIX + mClient->GetPluginName() +
                                         "_MethodName_" + mApiName + "_Error_split";
                mClient->RecordMetric(mContext, metricName, durationMs, AGW_UNIT_MILLISECONDS);
            } else {
                std::string metricName = AGW_INTERNAL_PLUGIN_PREFIX + mClient->GetPluginName() +
                                         "_MethodName_" + mApiName + "_Success_split";
                mClient->RecordMetric(mContext, metricName, durationMs, AGW_UNIT_MILLISECONDS);
            }
        }

        void SetFailed(const std::string& errorDetails)
        {
            mFailed = true;
            mErrorDetails = errorDetails;
        }

    private:
        TelemetryClient* mClient;
        Exchange::GatewayContext mContext;
        std::string mApiName;
        bool mFailed;
        std::string mErrorDetails;
        std::chrono::steady_clock::time_point mStartTime;
    };

    /**
     * @brief RAII tracker for automatic external service latency and error tracking
     * 
     * Times an external service call from construction to destruction, automatically reporting:
     * - Success latency metric (if SetFailed() not called)
     * - Error event + error latency metric (if SetFailed() called)
     * 
     * @note This class is used by AGW_TRACK_SERVICE_CALL() macro.
     *       Direct instantiation is not recommended.
     * 
     * Usage:
     *   {
     *       ScopedServiceTimer timer(&GetLocalTelemetryClient(), context, AGW_SERVICE_XYZ_PERMISSION);
     *       // ... make external service call ...
     *       if (failed) timer.SetFailed(AGW_ERROR_CONNECTION_TIMEOUT);
     *   } // Timer automatically reports on destruction
     */
    class ScopedServiceTimer
    {
    public:
        ScopedServiceTimer(TelemetryClient* client, const Exchange::GatewayContext& context, const std::string& serviceName)
            : mClient(client)
            , mContext(context)
            , mServiceName(serviceName)
            , mFailed(false)
            , mErrorDetails()
            , mStartTime(std::chrono::steady_clock::now())
        {
        }

        ~ScopedServiceTimer()
        {
            if (nullptr == mClient || false == mClient->IsAvailable()) {
                return;
            }

            auto endTime = std::chrono::steady_clock::now();
            // Use microseconds for precision, then convert to milliseconds as double
            auto durationUs = std::chrono::duration_cast<std::chrono::microseconds>(
                endTime - mStartTime).count();
            double durationMs = durationUs / 1000.0;  // Convert to milliseconds with decimal precision
            
            if (mFailed) {
                mClient->RecordExternalServiceError(mContext, mServiceName, mErrorDetails);
                std::string metricName = AGW_INTERNAL_PLUGIN_PREFIX + mClient->GetPluginName() +
                                         "_ServiceName_" + mServiceName + "_Error_split";
                mClient->RecordMetric(mContext, metricName, durationMs, AGW_UNIT_MILLISECONDS);
            } else {
                std::string metricName = AGW_INTERNAL_PLUGIN_PREFIX + mClient->GetPluginName() +
                                         "_ServiceName_" + mServiceName + "_Success_split";
                mClient->RecordMetric(mContext, metricName, durationMs, AGW_UNIT_MILLISECONDS);
            }
        }

        void SetFailed(const std::string& errorDetails)
        {
            mFailed = true;
            mErrorDetails = errorDetails;
        }

    private:
        TelemetryClient* mClient;
        Exchange::GatewayContext mContext;
        std::string mServiceName;
        bool mFailed;
        std::string mErrorDetails;
        std::chrono::steady_clock::time_point mStartTime;
    };

    /**
     * @brief RAII timer that captures and reports all three job timing timestamps.
     *
     * Construct at the TOP of Dispatch() — the submit time was already captured
     * in the job's constructor and passed in here. The destructor fires at the
     * end of Dispatch(), recording queue-wait, execution, and total durations.
     *
     * @param client        Telemetry client (from GetLocalTelemetryClient())
     * @param jobName       Job class name — label only, e.g. "WsMsgJob"
     * @param correlationId Any identity string from the job's own fields
     *                      (e.g. requestId, appId, method) — label only
     * @param submitTime    mSubmitTime captured in the job's constructor (T1)
     *
     * @note Used exclusively via the AGW_TRACK_JOB_LATENCY macro.
     */
    class ScopedJobTimer
    {
    public:
        ScopedJobTimer(TelemetryClient* client,
                       const std::string& jobName,
                       const std::string& correlationId,
                       const std::chrono::steady_clock::time_point& submitTime)
            : mClient(client)
            , mJobName(jobName)           // payload label only
            , mCorrelationId(correlationId) // payload label only
            , mSubmitTime(submitTime)
            , mDispatchStartTime(std::chrono::steady_clock::now())  // T2
        {
        }

        ~ScopedJobTimer()  // fires at end of Dispatch() scope → T3
        {
            if (nullptr == mClient || !mClient->IsAvailable()) {
                return;
            }

            auto endTime = std::chrono::steady_clock::now();  // T3

            auto toMs = [](std::chrono::steady_clock::duration d) -> double {
                return std::chrono::duration_cast<std::chrono::microseconds>(d).count() / 1000.0;
            };

            double queueWaitMs = toMs(mDispatchStartTime - mSubmitTime);  // T2 - T1
            double execMs      = toMs(endTime - mDispatchStartTime);      // T3 - T2
            double totalMs     = toMs(endTime - mSubmitTime);             // T3 - T1

            JsonObject data;
            data["job"]           = mJobName;          // which job class
            data["id"]            = mCorrelationId;    // which invocation (human label)
            data["queue_wait_ms"] = queueWaitMs;
            data["exec_ms"]       = execMs;
            data["total_ms"]      = totalMs;

            std::string payload;
            data.ToString(payload);

            Exchange::GatewayContext ctx;
            ctx.requestId    = 0;
            ctx.connectionId = 0;
            ctx.appId        = mJobName;

            mClient->RecordEvent(ctx, AGW_MARKER_JOB_TIMING, payload);
        }

    private:
        TelemetryClient* mClient;
        std::string mJobName;                                      // payload label
        std::string mCorrelationId;                                // payload label
        std::chrono::steady_clock::time_point mSubmitTime;         // T1 (from job ctor)
        std::chrono::steady_clock::time_point mDispatchStartTime;  // T2 (captured in ctor above)
    };

} // namespace AppGatewayTelemetryHelper
} // namespace Plugin
} // namespace WPEFramework

//=============================================================================
// TELEMETRY REPORTING MACROS
//=============================================================================

/**
 * This section provides convenience macros for reporting telemetry from plugins.
 * Macros are organized into categories by their purpose and usage pattern.
 * 
 * ## Events vs Metrics
 * 
 * **Events** (via RecordTelemetryEvent):
 * - Individual occurrences reported immediately
 * - Contains JSON payload with contextual information
 * - Not aggregated - each event sent separately to T2
 * - Use for: errors, state changes, significant occurrences
 * - Example: API error with error code, service failure with details
 * 
 * **Metrics** (via RecordTelemetryMetric):
 * - Numeric values aggregated over time (sum, count, min, max, avg)
 * - Reported periodically to T2 (e.g., hourly summary)
 * - Use for: latencies, counters, measurements
 * - Example: API latency, call counts, success rates
 * 
 * ## Macro Categories
 * 
 * 1. **Framework/Initialization**: Setup and teardown of telemetry client
 * 2. **Bootstrap Time Tracking**: Measure plugin initialization time
 * 3. **Error Reporting (Events)**: Report API and service errors
 * 4. **Latency Tracking (Metrics)**: Report timing measurements
 * 5. **Response Tracking**: Track response payloads for success/failure detection
 * 6. **Job Timing Macros**: Capture WorkerPool submit-to-dispatch latency
 * 7. **Generic Telemetry**: Low-level event/metric reporting
 */

//=============================================================================
// 1. FRAMEWORK/INITIALIZATION MACROS
//=============================================================================

/**
 * @brief Define a telemetry client instance for this plugin
 * @param pluginName Plugin name constant from AppGatewayTelemetryMarkers.h (e.g., AGW_PLUGIN_YOUR_PLUGIN)
 * 
 * This macro MUST be called once in each plugin's implementation file (.cpp) to create
 * a plugin-specific telemetry client instance. Each plugin gets its own separate instance
 * to avoid conflicts when multiple plugins report telemetry simultaneously.
 * 
 * **IMPORTANT**: Place this macro at the top of your plugin's .cpp file, outside any class/function.
 * 
 * Example in YourPlugin.cpp:
 *   #include "UtilsAppGatewayTelemetry.h"
 *   
 *   AGW_DEFINE_TELEMETRY_CLIENT(AGW_PLUGIN_YOUR_PLUGIN)
 *   
 *   namespace WPEFramework {
 *   namespace Plugin {
 *       // ... rest of implementation
 *   }}
 */
#define AGW_DEFINE_TELEMETRY_CLIENT(pluginName) \
    namespace { \
        WPEFramework::Plugin::AppGatewayTelemetryHelper::TelemetryClient& GetLocalTelemetryClient() { \
            static WPEFramework::Plugin::AppGatewayTelemetryHelper::TelemetryClient instance; \
            return instance; \
        } \
        const char* GetLocalPluginName() { \
            return pluginName; \
        } \
    }


/**
 * @brief Initialize the AppGateway telemetry client
 * @param service PluginHost::IShell* pointer
 * 
 * Call this in your plugin's Initialize() method to connect to AppGateway's telemetry interface.
 * The plugin name was already specified in AGW_DEFINE_TELEMETRY_CLIENT macro.
 * 
 * Example:
 *   const string MyPlugin::Initialize(PluginHost::IShell* service) {
 *       AGW_TELEMETRY_INIT(service);
 *       // ... rest of initialization ...
 *   }
 */
#define AGW_TELEMETRY_INIT(service) \
    do { \
        GetLocalTelemetryClient().Initialize(service, GetLocalPluginName()); \
    } while(0)

/**
 * @brief Deinitialize the AppGateway telemetry client
 * 
 * Call this in your plugin's Deinitialize() method to release the telemetry interface.
 * 
 * Example:
 *   void MyPlugin::Deinitialize(PluginHost::IShell* service) {
 *       AGW_TELEMETRY_DEINIT();
 *   }
 */
#define AGW_TELEMETRY_DEINIT() \
    do { \
        GetLocalTelemetryClient().Deinitialize(); \
    } while(0)

//=============================================================================
// 2. BOOTSTRAP TIME TRACKING MACROS
//=============================================================================

/**
 * @brief Record plugin bootstrap time using RAII (RECOMMENDED)
 * 
 * Creates a scoped timer that automatically measures bootstrap time from
 * invocation until the end of the current scope (typically end of Initialize).
 * Reports the bootstrap time via TelemetryClient to AppGateway.
 * 
 * **Data Flow**:
 * - Uses RecordTelemetryMetric internally
 * - Reports to standard marker: AGW_MARKER_BOOTSTRAP_DURATION
 * - AppGateway aggregates all plugin bootstrap times cumulatively
 * - AppGateway tracks total plugins loaded and total bootstrap time
 * 
 * Example:
 *   const string MyPlugin::Initialize(PluginHost::IShell* service) {
 *       AGW_TELEMETRY_INIT(service);  // Initialize telemetry first
 *       AGW_RECORD_BOOTSTRAP_TIME();  // Timer starts here
 *       
 *       // ... plugin initialization code ...
 *       
 *       return EMPTY_STRING;
 *   } // Timer automatically records on scope exit
 */
#define AGW_RECORD_BOOTSTRAP_TIME() \
    WPEFramework::Plugin::AppGatewayTelemetryHelper::ScopedBootstrapTimer bootstrapTimer(&GetLocalTelemetryClient())

//=============================================================================
// 3. ERROR REPORTING MACROS (Events via RecordTelemetryEvent)
//=============================================================================

/**
 * @brief Report an API error event to AppGateway telemetry
 * @param context Gateway context with request/connection/app info
 * @param apiName Name of the API that failed
 * @param errorCode Predefined error code from AppGatewayTelemetryMarkers.h (e.g., AGW_ERROR_TIMEOUT)
 * 
 * **Data Flow**:
 * - Uses RecordTelemetryEvent internally (individual occurrence)
 * - Marker: AGW_MARKER_PLUGIN_API_ERROR
 * - Payload: {"plugin": "<name>", "api": "<apiName>", "error": "<errorCode>"}
 * - Each error SENT IMMEDIATELY to T2 (not cached, not aggregated)
 * - Error counts also tracked for aggregated metrics (sent periodically)
 * 
 * **When to Use**:
 * - Use for tracking WHAT errors occurred (immediate forensics)
 * - Each error event sent to T2 as soon as it happens
 * - For aggregated error counting, use AGW_TRACK_API_CALL instead
 * 
 * Example:
 *   AGW_REPORT_API_ERROR(context, "GetSettings", AGW_ERROR_TIMEOUT)
 *   AGW_REPORT_API_ERROR(context, "GetAppPermissions", AGW_ERROR_PERMISSION_DENIED)
 */
#define AGW_REPORT_API_ERROR(context, apiName, errorCode) \
    do { \
        auto& client = GetLocalTelemetryClient(); \
        if (client.IsAvailable()) { \
            client.RecordApiError(context, apiName, errorCode); \
        } \
    } while(0)

/**
 * @brief Report an external service error to AppGateway telemetry
 * @param context Gateway context with request/connection/app info
 * @param serviceName Predefined service name from AppGatewayTelemetryMarkers.h (e.g., AGW_SERVICE_YOUR_SERVICE)
 * @param errorCode Predefined error code from AppGatewayTelemetryMarkers.h (e.g., AGW_ERROR_INTERFACE_UNAVAILABLE)
 * 
 * **Data Flow**:
 * - Uses RecordTelemetryEvent internally (individual occurrence)
 * - Marker: AGW_MARKER_PLUGIN_EXT_SERVICE_ERROR
 * - Payload: {"plugin": "<name>", "service": "<serviceName>", "error": "<errorCode>"}
 * - Each error SENT IMMEDIATELY to T2 (not cached, not aggregated)
 * - Error counts also tracked for aggregated metrics (sent periodically)
 * 
 * **When to Use**:
 * - Use for tracking WHAT service errors occurred (immediate forensics)
 * - Each error event sent to T2 as soon as it happens
 * - For aggregated error counting with latency, use AGW_TRACK_SERVICE_CALL instead
 * 
 * Example:
 *   AGW_REPORT_EXTERNAL_SERVICE_ERROR(context, AGW_SERVICE_YOUR_SERVICE, AGW_ERROR_INTERFACE_UNAVAILABLE)
 *   AGW_REPORT_EXTERNAL_SERVICE_ERROR(context, AGW_SERVICE_XYZ_SERVICE, AGW_ERROR_CONNECTION_TIMEOUT)
 */
#define AGW_REPORT_EXTERNAL_SERVICE_ERROR(context, serviceName, errorCode) \
    do { \
        auto& client = GetLocalTelemetryClient(); \
        if (client.IsAvailable()) { \
            client.RecordExternalServiceError(context, serviceName, errorCode); \
        } \
    } while(0)

//=============================================================================
// 4. LATENCY TRACKING MACROS (Metrics via RecordTelemetryMetric)
//=============================================================================

/**
 * @brief Automatic API timing with RAII (RECOMMENDED for API methods)
 * @param varName Variable name for the timer
 * @param context Gateway context with request/connection/app info
 * @param apiName Name of the API being timed
 * 
 * 
 * Example:
 *   Core::hresult MyPlugin::SomeMethod(const Exchange::GatewayContext& context)
 *   {
 *       AGW_TRACK_API_CALL(timer, context, "SomeMethod");
 *       
 *       auto result = DoWork();
 *       if (result != Core::ERROR_NONE) {
 *           timer.SetFailed(AGW_ERROR_TIMEOUT);
 *           return result;
 *       }
 *       
 *       return Core::ERROR_NONE;
 *   } // Timer automatically reports success/failure with timing
 */
#define AGW_TRACK_API_CALL(varName, context, apiName) \
    WPEFramework::Plugin::AppGatewayTelemetryHelper::ScopedApiTimer varName(&GetLocalTelemetryClient(), context, apiName)

/**
 * @brief Automatic external service call tracking with RAII (RECOMMENDED for service calls)
 * @param varName Variable name for the tracker
 * @param context Gateway context with request/connection/app info
 * @param serviceName Name of the external service (use predefined constants from AppGatewayTelemetryMarkers.h)
 * 
 * Example:
 *   Core::hresult MyPlugin::CallExternalService(const Exchange::GatewayContext& context)
 *   {
 *       AGW_TRACK_SERVICE_CALL(serviceTracker, context, AGW_SERVICE_XYZ_PERMISSION);
 *       
 *       auto result = XYZClient->CheckPermission(...);
 *       if (result != Core::ERROR_NONE) {
 *           serviceTracker.SetFailed(AGW_ERROR_CONNECTION_TIMEOUT);
 *           return result;
 *       }
 *       
 *       return Core::ERROR_NONE;
 *   } // Tracker automatically reports success/failure with timing
 */
#define AGW_TRACK_SERVICE_CALL(varName, context, serviceName) \
    WPEFramework::Plugin::AppGatewayTelemetryHelper::ScopedServiceTimer varName(&GetLocalTelemetryClient(), context, serviceName)

/**
 * @brief Report an API latency metric to AppGateway telemetry (manual)
 * @param context Gateway context with request/connection/app info
 * @param apiName Name of the API
 * @param latencyMs Latency in milliseconds
 * 
 * 
 * Example:
 *   auto start = std::chrono::steady_clock::now();
 *   DoWork();
 *   auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
 *       std::chrono::steady_clock::now() - start).count();
 *   AGW_REPORT_API_LATENCY(context, "GetSettings", durationMs);
 */
#define AGW_REPORT_API_LATENCY(context, apiName, latencyMs) \
    do { \
        auto& client = GetLocalTelemetryClient(); \
        if (client.IsAvailable()) { \
            client.RecordApiLatency(context, apiName, latencyMs); \
        } \
    } while(0)

/**
 * @brief Report an external service latency metric to AppGateway telemetry
 * @param context Gateway context with request/connection/app info
 * @param serviceName Predefined service name from AppGatewayTelemetryMarkers.h
 * @param latencyMs Latency in milliseconds
 * 
 * **When to Use**:
 * - Track latency of external service calls (gRPC, COM-RPC, HTTP)
 * - Helps identify slow external dependencies
 * 
 * Example:
 *   auto start = std::chrono::steady_clock::now();
 *   auto result = XYZPermissionClient->CheckPermission(...);
 *   auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
 *       std::chrono::steady_clock::now() - start).count();
 *   AGW_REPORT_SERVICE_LATENCY(context, AGW_SERVICE_XYZ_PERMISSION, durationMs);
 */
#define AGW_REPORT_SERVICE_LATENCY(context, serviceName, latencyMs) \
    do { \
        auto& client = GetLocalTelemetryClient(); \
        if (client.IsAvailable()) { \
            client.RecordServiceLatency(context, serviceName, latencyMs); \
        } \
    } while(0)

//=============================================================================
// 5. RESPONSE TRACKING MACRO
//=============================================================================

/**
 * @brief Track response payload by delegating response parsing to AppGateway telemetry
 * @param context Gateway context with request/connection/app info
 * @param payload appGateway response string
 */
#define AGW_TRACK_RESPONSE_PAYLOAD(context, payload) \
    do { \
        auto& client = GetLocalTelemetryClient(); \
        if (client.IsAvailable()) { \
            client.TrackResponsePayload(context, payload); \
        } \
    } while(0)

//=============================================================================
// 6. JOB TIMING MACROS (Submit → Dispatch Start → Dispatch End)
//=============================================================================

/**
 * @brief Capture T1: Record the submit timestamp in the job constructor.
 *
 * Expands to an initializer-list entry — place it LAST in the job's
 * constructor initializer list (after all other members are initialized).
 * Also add `std::chrono::steady_clock::time_point mSubmitTime;` to
 * the job's private members.
 *
 * Why last? C++ initializes members in declaration order. Placing the
 * timestamp capture last ensures all other fields are ready, and that
 * the timestamp is as close as possible to the actual Submit() call.
 *
 * Example:
 *   WsMsgJob(...) : mParent(*parent), mMethod(method), mParams(params),
 *                   mRequestId(requestId), mConnectionId(connectionId),
 *                   AGW_JOB_CAPTURE_SUBMIT_TIME()   // ← last, T1
 *   { mParent.AddRef(); }
 *
 *   private:
 *     ...existing members...
 *     std::chrono::steady_clock::time_point mSubmitTime;  // ← add this
 *     WPEFramework::Plugin::AppGatewayTelemetryHelper::TelemetryClient* mTelemetryClient;  // ← add this
 *
 * Why capture the client pointer here?
 * The constructor is always called from a .cpp file that has
 * AGW_DEFINE_TELEMETRY_CLIENT in scope, so GetLocalTelemetryClient() is
 * reachable. Dispatch() may be defined inline in a header included by
 * multiple .cpp files, where GetLocalTelemetryClient() would be out of scope.
 */
#define AGW_JOB_CAPTURE_SUBMIT_TIME() \
    mSubmitTime(std::chrono::steady_clock::now()),   \
    mTelemetryClient(&GetLocalTelemetryClient())

/**
 * @brief T2 + T3: Report all three timing values via RAII.
 * @param varName        Variable name for the ScopedJobTimer instance
 * @param jobName        Descriptive string for this job — payload label only.
 *                       Can be a plain class name OR enriched with job-specific
 *                       context by appending any member field value.
 *                       e.g., "WsMsgJob[" + mMethod + "]"
 *                            "SubscriberJob[subscribe:" + mEvent + "]"
 *                            "ConnectionStatusNotificationJob[" + (mConnected?"connect":"disconnect") + "]"
 * @param correlationId  Any string from the job's own fields — payload label only
 *                       (e.g., std::to_string(mRequestId), mAppId, mMethod)
 *                       Pass "" if no natural identity field exists.
 *
 * Place at the VERY FIRST LINE of the job's Dispatch() body.
 * ScopedJobTimer constructor captures T2 (Dispatch start).
 * ScopedJobTimer destructor fires at end of Dispatch() and captures T3,
 * then emits: queue_wait_ms (T2-T1), exec_ms (T3-T2), total_ms (T3-T1).
 *
 * Example in WsMsgJob::Dispatch():
 *   void Dispatch() override {
 *       AGW_TRACK_JOB_LATENCY(timer,
 *           "WsMsgJob[" + mMethod + "]",
 *           std::to_string(mRequestId) + ":" + std::to_string(mConnectionId));
 *       mParent.DispatchWsMsg(mMethod, mParams, mRequestId, mConnectionId);
 *   }  // ← timer destructs here, T3 captured, telemetry emitted
 */
#define AGW_TRACK_JOB_LATENCY(varName, jobName, correlationId)                         \
    WPEFramework::Plugin::AppGatewayTelemetryHelper::ScopedJobTimer varName(           \
        mTelemetryClient, jobName, correlationId, mSubmitTime)

//=============================================================================
// 8. GENERIC TELEMETRY REPORTING MACROS (Low-level Interface)
//=============================================================================

/**
 * @brief Report a custom numeric metric to AppGateway telemetry
 * @param context Gateway context with request/connection/app info
 * @param metricName Custom metric name
 * @param value Numeric value
 * @param unit Predefined unit from AppGatewayTelemetryMarkers.h (e.g., AGW_UNIT_MILLISECONDS)
 * 
 * **Data Flow**:
 * - Uses RecordTelemetryMetric internally (aggregated values)
 * - Direct low-level metric reporting
 * - AppGateway aggregates over time (sum, count, min, max, avg)
 * 
 * **When to Use**:
 * - Custom measurements not covered by standard macros
 * - Prefer specific macros (AGW_REPORT_API_LATENCY, etc.) when available
 * 
 * Example:
 *   static uint32_t cacheHitCount = 0;
 *   cacheHitCount++;
 *   AGW_REPORT_METRIC(context, "AppGwCacheHits", 
 *                     static_cast<double>(cacheHitCount), AGW_UNIT_COUNT);
 */
#define AGW_REPORT_METRIC(context, metricName, value, unit) \
    do { \
        auto& client = GetLocalTelemetryClient(); \
        if (client.IsAvailable()) { \
            client.RecordMetric(context, metricName, value, unit); \
        } \
    } while(0)

/**
 * @brief Report a custom telemetry event to AppGateway
 * @param context Gateway context with request/connection/app info
 * @param eventName Event name (becomes T2 marker)
 * @param eventData JSON string with event data
 * 
 * **Data Flow**:
 * - Uses RecordTelemetryEvent internally (individual occurrence)
 * - Direct low-level event reporting
 * - Each event sent individually to T2 (not aggregated)
 * 
 * **When to Use**:
 * - Custom events not covered by standard macros
 * - State changes, user actions, significant occurrences
 * - Prefer specific macros (AGW_REPORT_API_ERROR, etc.) when available
 */
#define AGW_REPORT_EVENT(context, eventName, eventData) \
    do { \
        auto& client = GetLocalTelemetryClient(); \
        if (client.IsAvailable()) { \
            client.RecordEvent(context, eventName, eventData); \
        } \
    } while(0)

