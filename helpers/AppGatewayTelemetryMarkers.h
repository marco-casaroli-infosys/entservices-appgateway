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
 * @file AppGatewayTelemetryMarkers.h
 * @brief Predefined T2 telemetry markers for App Gateway ecosystem
 * 
 * This file defines all standard telemetry markers used across the App Gateway
 * plugin ecosystem. Plugins use these markers when reporting telemetry via the
 * IAppGatewayTelemetry COM-RPC interface.
 * 
 * ## Marker Design - Generic Category-Based Approach
 * 
 * The system uses GENERIC markers where the plugin/service name is part of the
 * payload data rather than the marker name itself. This reduces T2 marker count
 * and simplifies codebase maintenance.
 * 
 * All markers follow this pattern:
 *   `AppGw<Category><Type>_split`
 * 
 * Where:
 * - `AppGw` - App Gateway prefix (identifies the source)
 * - `<Category>` - Category of telemetry (e.g., Plugin, Health, Api)
 * - `<Type>` - Type of data (e.g., ApiError, ExtServiceError, ApiLatency)
 * - `_split` - Suffix indicating structured/split format for T2
 * 
 * ## Usage
 * 
 * Plugins should use the helper functions from UtilsAppGatewayTelemetry.h:
 *   - AGW_REPORT_API_ERROR() - Reports API failures (uses RecordTelemetryEvent)
 *   - AGW_REPORT_EXTERNAL_SERVICE_ERROR() - Reports external service failures (uses RecordTelemetryEvent)
 *   - AGW_REPORT_API_LATENCY() - Reports API call latency
 *   - AGW_REPORT_SERVICE_LATENCY() - Reports external service latency (uses RecordTelemetryMetric with aggregation)
 * 
 * Direct COM-RPC interface usage (if helper macros unavailable):
 *   IAppGatewayTelemetry::RecordTelemetryEvent(context, markerName, jsonPayload)
 *   IAppGatewayTelemetry::RecordTelemetryMetric(context, metricName, value, unit)
 * 
 * ## Adding Support for New Plugins
 * 
 * When integrating a new plugin:
 * 1. Add your plugin name constant below (if not already present)
 * 2. Use existing generic markers (AGW_MARKER_PLUGIN_API_ERROR, etc.)
 * 3. Include plugin name in the payload data using your constant
 * 4. No need to create new plugin-specific markers!
 */

//=============================================================================
// METRIC UNITS
// Use these standard units for RecordTelemetryMetric
//=============================================================================

#define AGW_UNIT_MILLISECONDS           "ms"
#define AGW_UNIT_SECONDS                "sec"
#define AGW_UNIT_COUNT                  "count"
#define AGW_UNIT_BYTES                  "bytes"
#define AGW_UNIT_KILOBYTES              "KB"
#define AGW_UNIT_MEGABYTES              "MB"
#define AGW_UNIT_KBPS                   "kbps"
#define AGW_UNIT_MBPS                   "Mbps"
#define AGW_UNIT_PERCENT                "percent"

/**
 * @brief Internal plugin prefix for metric name construction
 * @details Used when building metric names for plugin API/service latency tracking
 * @usage Format: "ENTS_INFO_AppGw_PluginName_<PluginName>_ApiName_<ApiName>_ApiLatency_split"
 * @note INTERNAL: Used by AppGatewayTelemetry parsing logic and TelemetryClient metric recording
 */
#define AGW_INTERNAL_PLUGIN_PREFIX      "ENTS_INFO_AppGw_PluginName_"

/**
 * @brief Bootstrap duration metric (sent once on startup)
 * @details Total time taken to start all App Gateway plugins
 * @payload { "sum": <duration_ms>, "count": 1, "unit": "ms", "reporting_interval_sec": 0 }
 */
#define AGW_MARKER_BOOTSTRAP_DURATION               "ENTS_INFO_AppGwBootstrap"

/**
 * @brief WebSocket connections metric (sent periodically)
 * @details Current active WebSocket connections
 * @payload { "sum": <connections>, "count": 1, "unit": "count", "reporting_interval_sec": 3600 }
 */
#define AGW_MARKER_WEBSOCKET_CONNECTIONS            "ENTS_INFO_AppGwWebSocketConnections"

/**
 * @brief Total API calls metric (sent periodically)
 * @details Total number of API calls in reporting period
 * @payload { "sum": <calls>, "count": 1, "unit": "count", "reporting_interval_sec": 3600 }
 */
#define AGW_MARKER_TOTAL_CALLS                      "ENTS_INFO_AppGwTotalCalls"

/**
 * @brief Successful API calls metric (sent periodically)
 * @details Number of successful API calls in reporting period
 * @payload { "sum": <calls>, "count": 1, "unit": "count", "reporting_interval_sec": 3600 }
 */
#define AGW_MARKER_SUCCESSFUL_CALLS                 "ENTS_INFO_AppGwSuccessfulCalls"

/**
 * @brief Failed API calls metric (sent periodically)
 * @details Number of failed API calls in reporting period
 * @payload { "sum": <calls>, "count": 1, "unit": "count", "reporting_interval_sec": 3600 }
 */
#define AGW_MARKER_FAILED_CALLS                     "ENTS_INFO_AppGwFailedCalls"

/**
 * @brief Total response calls metric (sent periodically)
 * @details Total number of API responses in reporting period
 * @payload { "sum": <responses>, "count": 1, "unit": "count", "reporting_interval_sec": 3600 }
 */
#define AGW_MARKER_RESPONSE_CALLS                   "ENTS_INFO_AppGwResponseCalls"

/**
 * @brief Internal marker for response payload tracking (used by AGW_TRACK_RESPONSE_PAYLOAD macro)
 * @details Special event marker that triggers JSON-RPC 2.0 response parsing and tracking
 * @usage RecordTelemetryEvent(context, AGW_MARKER_RESPONSE_PAYLOAD_TRACKING, jsonPayloadWithResult)
 * @note This is an INTERNAL marker - used by responder implementations
 * @note Event data format: {"payload": "<json-rpc-2.0-response>"}
 * @note AppGateway parses the payload to determine success/failure and calls RecordResponse
 */
#define AGW_MARKER_RESPONSE_PAYLOAD_TRACKING        "ENTS_INFO_AppGwResponsePayloadTracking"

/**
 * @brief Consolidated health statistics marker (sent periodically)
 * @details Aggregated health metrics for AppGateway including WebSocket connections and API call statistics
 * @payload {
 *   "reporting_interval_sec": 3600,
 *   "websocket_connections": <active_connections>,
 *   "total_calls": <total_api_calls>,
 *   "total_responses": <total_responses>,
 *   "successful_calls": <successful_calls>,
 *   "failed_calls": <failed_calls>,
 *   "unit": "count"
 * }
 * @note Individual markers (AGW_MARKER_WEBSOCKET_CONNECTIONS, AGW_MARKER_TOTAL_CALLS,
 *       AGW_MARKER_RESPONSE_CALLS, AGW_MARKER_SUCCESSFUL_CALLS, AGW_MARKER_FAILED_CALLS)
 *       are available for plugin-specific use via RecordTelemetryMetric
 */
#define AGW_MARKER_HEALTH_STATS                     "ENTS_INFO_AppGwHealth"

/**
 * @brief LinchPin connection metric (sent periodically)
 * @details Tracks LinchPin AS connection state changes (connected events)
 * @payload { "sum": <connection_count>, "count": 1, "unit": "count", "reporting_interval_sec": 3600 }
 * @usage AGW_REPORT_METRIC(context, AGW_MARKER_LINCHPIN_CONNECTED, 1.0, AGW_UNIT_COUNT)
 */
#define AGW_MARKER_LINCHPIN_CONNECTED               "ENTS_INFO_AppGwLinchPinConnected"

/**
 * @brief LinchPin disconnection metric (sent periodically)
 * @details Tracks LinchPin AS disconnection state changes (disconnected events)
 * @payload { "sum": <disconnection_count>, "count": 1, "unit": "count", "reporting_interval_sec": 3600 }
 * @usage AGW_REPORT_METRIC(context, AGW_MARKER_LINCHPIN_DISCONNECTED, 1.0, AGW_UNIT_COUNT)
 */
#define AGW_MARKER_LINCHPIN_DISCONNECTED            "ENTS_INFO_AppGwLinchPinDisconnected"

/**
 * @brief LinchPin notification count metric (sent periodically)
 * @details Tracks the number of LinchPin notifications received
 * @payload { "sum": <notification_count>, "count": <occurrences>, "unit": "count", "reporting_interval_sec": 3600 }
 * @usage AGW_REPORT_METRIC(context, AGW_MARKER_LINCHPIN_NOTIFICATION_COUNT, 1.0, AGW_UNIT_COUNT)
 */
#define AGW_MARKER_LINCHPIN_NOTIFICATION_COUNT      "ENTS_INFO_AppGwLinchPinNotificationCount"

/**
 * @brief Job timing metric marker — queue-wait + execution latency
 * @details Reports three timing values per job dispatch:
 *   - queue_wait_ms  : T2 - T1 (submit → Dispatch start)
 *   - exec_ms        : T3 - T2 (Dispatch start → Dispatch end)
 *   - total_ms       : T3 - T1 (submit → Dispatch complete)
 * @payload { "job": "<jobClassName>", "queue_wait_ms": <double>,
 *            "exec_ms": <double>, "total_ms": <double> }
 *          Available request, connection, and app identifiers are reported through GatewayContext.
 * @usage Used by ScopedJobTimer RAII class via AGW_TRACK_JOB_LATENCY macro
 * @note This is an EVENT marker (RecordEvent) not a metric (RecordMetric) because
 *       per-job latency has forensic value as individual occurrences; aggregation
 *       would hide outliers and queue-wait spikes.
 */
#define AGW_MARKER_JOB_TIMING   "ENTS_INFO_AppGwJobTiming"

/**
 * @brief API error count metric prefix
 * @details Per-API error count metrics sent periodically
 */
#define AGW_MARKER_API_ERROR_COUNT           "ENTS_INFO_AppGwApiErrorCount"

/**
 * @brief DEPRECATED: Old aggregated API error stats marker (no longer used)
 * @details Replaced by per-API metrics using AGW_MARKER_API_ERROR_COUNT + <ApiName>
 */
#define AGW_MARKER_API_ERROR_STATS                  "ENTS_INFO_AppGwApiErrorStats"

/**
 * @brief External service error count metric prefix
 * @details Per-service error count metrics sent periodically
 */
#define AGW_MARKER_EXT_SERVICE_ERROR_COUNT   "ENTS_INFO_AppGwExtServiceErrorCount"

/**
 * @brief Per-API method statistics marker (common marker for all plugin/method combinations)
 * @details Used to report detailed per-API statistics including counters and latency metrics
 * @usage Single marker for all: "ENTS_INFO_AppGwApiMethod"
 * @payload {
 *   "plugin_name": "<PluginName>",
 *   "method_name": "<MethodName>",
 *   "reporting_interval_sec": 3600,
 *   "total_count": <total_calls>,
 *   "success_count": <success_count>,
 *   "success_latency_avg_ms": <avg>,
 *   "success_latency_min_ms": <min>,
 *   "success_latency_max_ms": <max>,
 *   "error_count": <error_count>,
 *   "error_latency_avg_ms": <avg>,
 *   "error_latency_min_ms": <min>,
 *   "error_latency_max_ms": <max>
 * }
 * @example For Plugin_Name_1.methodName1():
 *   Marker: "ENTS_INFO_AppGwApiMethod"
 *   Payload includes: "plugin_name": "Plugin_Name_1", "method_name": "methodName1"
 */
#define AGW_MARKER_API_METHOD_STAT                       "ENTS_INFO_AppGwApiMethod"

/**
 * @brief API latency statistics marker (common marker for all plugin/API combinations)
 * @details Used to report aggregated API latency metrics from plugins
 * @usage Single marker for all: "ENTS_INFO_AppGwApiLatency"
 * @payload {
 *   "plugin_name": "<PluginName>",
 *   "api_name": "<ApiName>",
 *   "reporting_interval_sec": 3600,
 *   "count": <total_calls>,
 *   "avg_ms": <average_latency>,
 *   "min_ms": <minimum_latency>,
 *   "max_ms": <maximum_latency>,
 *   "total_ms": <total_latency>,
 *   "unit": "Milliseconds"
 * }
 * @example For Plugin_Name_1.apiMethod1():
 *   Marker: "ENTS_INFO_AppGwApiLatency"
 *   Payload includes: "plugin_name": "Plugin_Name_1", "api_name": "apiMethod1"
 */
#define AGW_MARKER_API_LATENCY                      "ENTS_INFO_AppGwApiLatency"

/**
 * @brief Service latency statistics marker (common marker for all plugin/service combinations)
 * @details Used to report aggregated external service latency metrics from plugins
 * @usage Single marker for all: "ENTS_INFO_AppGwServiceLatency"
 * @payload {
 *   "plugin_name": "<PluginName>",
 *   "service_name": "<ServiceName>",
 *   "reporting_interval_sec": 3600,
 *   "count": <total_calls>,
 *   "avg_ms": <average_latency>,
 *   "min_ms": <minimum_latency>,
 *   "max_ms": <maximum_latency>,
 *   "total_ms": <total_latency>,
 *   "unit": "Milliseconds"
 * }
 * @example For Plugin_Name_1 calling ExternalService1:
 *   Marker: "ENTS_INFO_AppGwServiceLatency"
 *   Payload includes: "plugin_name": "Plugin_Name_1", "service_name": "ExternalService1"
 */
#define AGW_MARKER_SERVICE_LATENCY                  "ENTS_INFO_AppGwServiceLatency"

/**
 * @brief Per-service method statistics marker (common marker for all plugin/service combinations)
 * @details Used to report detailed per-service statistics including counters and latency metrics
 * @usage Single marker for all: "ENTS_INFO_AppGwServiceMethod"
 * @payload {
 *   "plugin_name": "<PluginName>",
 *   "service_name": "<ServiceName>",
 *   "reporting_interval_sec": 3600,
 *   "total_count": <total_calls>,
 *   "success_count": <success_count>,
 *   "success_latency_avg_ms": <avg>,
 *   "success_latency_min_ms": <min>,
 *   "success_latency_max_ms": <max>,
 *   "error_count": <error_count>,
 *   "error_latency_avg_ms": <avg>,
 *   "error_latency_min_ms": <min>,
 *   "error_latency_max_ms": <max>
 * }
 * @example For Plugin_Name_1 calling ExternalService1:
 *   Marker: "ENTS_INFO_AppGwServiceMethod"
 *   Payload includes: "plugin_name": "Plugin_Name_1", "service_name": "ExternalService1"
 */
#define AGW_MARKER_SERVICE_METHOD_STAT              "ENTS_INFO_AppGwServiceMethod"

//=============================================================================
// GENERIC PLUGIN EVENT MARKERS (for forensics)
// Used by plugins for immediate error reporting with JSON context
// Plugins should use helper macros from UtilsAppGatewayTelemetry.h
//=============================================================================

/**
 * @brief Plugin API error event marker
 * @details Reports API failures from any plugin. Plugin name included in payload.
 * @usage Use AGW_REPORT_API_ERROR(context, api, error) helper macro from UtilsAppGatewayTelemetry.h
 * @payload { "plugin": "<pluginName>", "api": "<apiName>", "error": "<errorCode>" }
 * @example AGW_REPORT_API_ERROR(context, "apiMethod1", AGW_ERROR_TIMEOUT)
 * @note Plugin name comes from AGW_TELEMETRY_INIT initialization
 */
#define AGW_MARKER_PLUGIN_API_ERROR                 "ENTS_ERROR_AppGwPluginApiError"

/**
 * @brief Plugin external service error event marker
 * @details Reports external service failures from any plugin. Plugin name included in payload.
 * @usage Use AGW_REPORT_EXTERNAL_SERVICE_ERROR(context, service, error) helper macro from UtilsAppGatewayTelemetry.h
 * @payload { "plugin": "<pluginName>", "service": "<serviceName>", "error": "<errorCode>" }
 * @example AGW_REPORT_EXTERNAL_SERVICE_ERROR(context, AGW_SERVICE_PERMISSION, AGW_ERROR_CONNECTION_TIMEOUT)
 * @note Plugin name comes from AGW_TELEMETRY_INIT initialization
 */
#define AGW_MARKER_PLUGIN_EXT_SERVICE_ERROR         "ENTS_ERROR_AppGwPlugExtnSrvErr"

/**
 * @brief Plugin API latency metric marker
 * @details Reports API call latency from any plugin using RecordTelemetryEvent with JSON payload.
 * @usage Use AGW_REPORT_API_LATENCY(context, api, latencyMs) helper macro from UtilsAppGatewayTelemetry.h
 * @payload { "plugin": "<pluginName>", "api": "<apiName>", "latency_ms": <double> }
 * @example AGW_REPORT_API_LATENCY(context, "apiMethod2", 125.5)
 * @note Plugin name comes from AGW_TELEMETRY_INIT initialization
 */
#define AGW_MARKER_PLUGIN_API_LATENCY               "ENTS_INFO_AppGwPluginApiLat"

/**
 * @brief Metrics error event marker (Firebolt metrics.error API call)
 * @details Reports application error from FbMetrics plugin.
 * @usage Use AGW_REPORT_EVENT(context, AGW_MARKER_APP_METRICS_ERROR, csvData)
 * @payload CSV: appId,Code,type,visible,description
 * @example AGW_REPORT_EVENT(ctx, AGW_MARKER_APP_METRICS_ERROR, "myApp,503,network,true,Service unavailable")
 */
#define AGW_MARKER_APP_METRICS_ERROR                "APP_METRICS_ERROR_split"

/**
 * @brief Sign-in status event marker (Firebolt Discovery signIn/signOut)
 * @details Reports application sign-in state change from FbDiscovery plugin.
 * @usage Use AGW_REPORT_EVENT(context, AGW_MARKER_APP_SIGNIN_STATUS, csvData)
 * @payload CSV: appId,signInStatus  (signInStatus: true=SignIn, false=SignOut)
 * @example AGW_REPORT_EVENT(ctx, AGW_MARKER_APP_SIGNIN_STATUS, "myApp,true")
 */
#define AGW_MARKER_APP_SIGNIN_STATUS                "APP_SIGNIN_STATUS_split"

/**
 * @brief Application ready event marker (lifecycle.ready)
 * @details Reports application readiness from AppGatewayCommon.
 * @usage Use AGW_REPORT_EVENT(context, AGW_MARKER_APP_READY, csvData)
 * @payload CSV: appId
 * @example AGW_REPORT_EVENT(ctx, AGW_MARKER_APP_READY, "myApp")
 */
#define AGW_MARKER_APP_READY                        "APP_READY_split"


//=============================================================================
// PREDEFINED PLUGIN NAMES
// Use these when reporting telemetry for consistency
// NOTE: These are internal constants for existing plugins - not for documentation examples
//=============================================================================

// Only opensource plugins here.
#define AGW_PLUGIN_APPGATEWAY                       "AppGateway"
#define AGW_PLUGIN_APPGATEWAYCOMMON                 "AppGatewayCommon"
#define AGW_PLUGIN_APPNOTIFICATIONS                 "AppNotifications"
#define AGW_PLUGIN_APPACTIONS                       "AppActions"
#define AGW_PLUGIN_FBADVERTISING                    "FbAdvertising"
#define AGW_PLUGIN_FBDISCOVERY                      "FbDiscovery"
#define AGW_PLUGIN_FBENTOS                          "FbEntos"
#define AGW_PLUGIN_FBMETRICS                        "FbMetrics"
#define AGW_PLUGIN_FBPRIVACY                        "FbPrivacy"

//=============================================================================
// PREDEFINED EXTERNAL SERVICE NAMES
// Use these when reporting external service errors for consistency
//=============================================================================
/**
 * @brief Internal Permission Service
 * @details AppGateway internal permission checking
 */
#define AGW_SERVICE_PERMISSION                      "PermissionService"

/**
 * @brief Authentication Service (WebSocket)
 * @details AppGateway WebSocket authentication
 */
#define AGW_SERVICE_AUTHENTICATION                  "AuthenticationService"

//=============================================================================
// PREDEFINED ERROR CODES
// Use these when reporting errors for consistency in analytics
//=============================================================================

#define AGW_ERROR_INTERFACE_UNAVAILABLE             "INTERFACE_UNAVAILABLE"
#define AGW_ERROR_INTERFACE_NOT_FOUND               "INTERFACE_NOT_FOUND"
#define AGW_ERROR_CLIENT_NOT_INITIALIZED            "CLIENT_NOT_INITIALIZED"
#define AGW_ERROR_CONNECTION_REFUSED                "CONNECTION_REFUSED"
#define AGW_ERROR_CONNECTION_TIMEOUT                "CONNECTION_TIMEOUT"
#define AGW_ERROR_TIMEOUT                           "TIMEOUT"
#define AGW_ERROR_PERMISSION_DENIED                 "PERMISSION_DENIED"
#define AGW_ERROR_PERMISSION_FETCH                  "PERMISSION_FETCH_FAILED"
#define AGW_ERROR_AUTH_TOKEN_FETCH_FAILED           "AUTH_TOKEN_FETCH_FAILED"
#define AGW_ERROR_PLATFORM_TOKEN_FAILED             "PLATFORM_TOKEN_FAILED"
#define AGW_ERROR_INVALID_RESPONSE                  "INVALID_RESPONSE"
#define AGW_ERROR_INVALID_REQUEST                   "INVALID_REQUEST"
#define AGW_ERROR_NOT_AVAILABLE                     "NOT_AVAILABLE"
#define AGW_ERROR_FETCH_FAILED                      "FETCH_FAILED"
#define AGW_ERROR_SERVICE_ACCOUNT_ID                "SERVICE_ACCOUNT_ID_FETCH_FAILED"
#define AGW_ERROR_SERVICE_ACCOUNT_TOKEN             "SERVICE_ACCOUNT_TOKEN_FETCH_FAILED"
#define AGW_ERROR_UPDATE_FAILED                     "UPDATE_FAILED"
#define AGW_ERROR_COLLECTION_FAILED                 "COLLECTION_FAILED"
#define AGW_ERROR_ENUMERATE_PERMISSIONS_FAILED      "ENUMERATE_PERMISSIONS_FAILED"
#define AGW_ERROR_GENERAL                           "GENERAL_ERROR"
#define AGW_ERROR_ALREADY_REGISTERED                "ALREADY_REGISTERED"

//=============================================================================
// USAGE EXAMPLES
// How to use the markers with helper functions from UtilsAppGatewayTelemetry.h
//=============================================================================

/*
 * Example 1: Reporting an API error from a plugin
 *
 *   // In YourPlugin.cpp - Include the helper and initialize
 *   #include "UtilsAppGatewayTelemetry.h"
 *
 *   // At top of file, before namespace:
 *   AGW_DEFINE_TELEMETRY_CLIENT(AGW_PLUGIN_YOUR_PLUGIN)
 *
 *   // In Initialize() method:
 *   AGW_TELEMETRY_INIT(mService);
 *
 *   // Report the error (plugin name automatic from AGW_DEFINE_TELEMETRY_CLIENT):
 *   // Note: context parameter contains requestId, connectionId, appId for request correlation
 *   AGW_REPORT_API_ERROR(context, "apiMethod1", AGW_ERROR_INTERFACE_UNAVAILABLE);
 *
 *   // This internally calls RecordTelemetryEvent with:
 *   //   eventName = AGW_MARKER_PLUGIN_API_ERROR
 *   //   eventData = { "plugin": "YourPlugin", "api": "apiMethod1", 
 *   //                 "error": "INTERFACE_UNAVAILABLE" }
 *   //   (Plugin name from initialization)
 *
 *
 * Example 2: Reporting an external service error from a plugin
 *
 *   // In YourPluginImplementation.cpp
 *   #include "UtilsAppGatewayTelemetry.h"
 *
 *   // At top of file, before namespace:
 *   AGW_DEFINE_TELEMETRY_CLIENT(AGW_PLUGIN_YOUR_PLUGIN)
 *
 *   // In Initialize() method:
 *   AGW_TELEMETRY_INIT(mService);
 *
 *   // Report the service error (plugin name automatic from AGW_DEFINE_TELEMETRY_CLIENT):
 *   // Note: context parameter contains requestId, connectionId, appId for request correlation
 *   AGW_REPORT_EXTERNAL_SERVICE_ERROR(context, AGW_SERVICE_EXTERNAL_SERVICE_1,
 *                                      AGW_ERROR_CONNECTION_TIMEOUT);
 *
 *   // This internally calls RecordTelemetryEvent with:
 *   //   eventName = AGW_MARKER_PLUGIN_EXT_SERVICE_ERROR
 *   //   eventData = { "plugin": "YourPlugin", "service": "ExternalService1", 
 *   //                 "error": "CONNECTION_TIMEOUT" }
 *   //   (Plugin name from initialization)
 *
 * Adding a new plugin:
 * 1. Add plugin name constant: #define AGW_PLUGIN_YOUR_PLUGIN "YourPlugin"
 * 2. Use the existing generic markers (shown above)
 * 3. Call helper macros with your plugin name constant
 * 4. No need to create plugin-specific markers!
 */
