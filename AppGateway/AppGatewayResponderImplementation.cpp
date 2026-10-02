/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
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
 **/

#include <string>
#include <plugins/JSONRPC.h>
#include <plugins/IShell.h>
#include "AppGatewayResponderImplementation.h"
#include "AppGatewayTelemetry.h"
#include "UtilsLogging.h"
#include "UtilsConnections.h"
#include "UtilsCallsign.h"
#include <interfaces/IAppNotifications.h>

// App Gateway is only available via local connections,
// so we can use a simple in-memory registry to track connection IDs and their associated app IDs.
#define APPGATEWAY_SOCKET_ADDRESS "127.0.0.1:3473"
#define DEFAULT_CONFIG_PATH "/etc/app-gateway/resolution.base.json"

namespace WPEFramework
{
    namespace Plugin
    {
        SERVICE_REGISTRATION(AppGatewayResponderImplementation, 1, 0, 0);

        AppGatewayResponderImplementation::AppGatewayResponderImplementation()
            : mService(nullptr),
            mWsManager(),
            mAuthenticator(nullptr),
            mResolver(nullptr),
            mConnectionStatusImplLock(),
            mEnhancedLoggingEnabled(false)
        {
            LOGINFO("AppGatewayResponderImplementation constructor");
#ifdef ENABLE_APP_GATEWAY_AUTOMATION
        #ifdef APP_GATEWAY_ENHANCED_LOGGING_INDICATOR
            struct stat buffer;
            mEnhancedLoggingEnabled = (stat(APP_GATEWAY_ENHANCED_LOGGING_INDICATOR, &buffer) == 0);
            LOGINFO("Enhanced logging enabled: %s (indicator: %s)", mEnhancedLoggingEnabled ? "true" : "false", APP_GATEWAY_ENHANCED_LOGGING_INDICATOR);
        #endif
#endif
        }

        AppGatewayResponderImplementation::~AppGatewayResponderImplementation()
        {
            LOGINFO("AppGatewayResponderImplementation destructor");
            
            // Clear WebSocket handlers before destruction to prevent use-after-free
            mWsManager.SetMessageHandler(nullptr);
            mWsManager.SetAuthHandler(nullptr);
            mWsManager.SetDisconnectHandler(nullptr);
            // Note: WebSocketConnectionManager destructor will handle channel cleanup
            
            if (nullptr != mService)
            {
                mService->Release();
                mService = nullptr;
            }

            if (nullptr != mResolver)
            {
                mResolver->Release();
                mResolver = nullptr;
            }

            if (nullptr != mAuthenticator)
            {
                mAuthenticator->Release();
                mAuthenticator = nullptr;
            }

        }

        uint32_t AppGatewayResponderImplementation::Configure(PluginHost::IShell *shell)
        {
            LOGINFO("Configuring AppGatewayResponderImplementation");
            uint32_t result = Core::ERROR_NONE;
            ASSERT(shell != nullptr);
            mService = shell;
            mService->AddRef();
            result = InitializeWebsocket();

            return result;
        }

        uint32_t AppGatewayResponderImplementation::InitializeWebsocket(){
            // Initialize WebSocket server
            WebSocketConnectionManager::Config config(APPGATEWAY_SOCKET_ADDRESS);
            std::string configLine = mService->ConfigLine();
            
            // Only attempt to parse if configLine is a valid JSON object with content.
            // Skip if empty, just "{}", or doesn't start with '{' (not a JSON object).
            // Find first non-whitespace character to check if it's a JSON object.
            size_t firstNonSpace = configLine.find_first_not_of(" \t\n\r");
            bool isJsonObject = (firstNonSpace != std::string::npos && configLine[firstNonSpace] == '{');
            bool hasContent = (configLine.length() > 2 && configLine != "{}");

            if (isJsonObject && hasContent) {
                Core::OptionalType<Core::JSON::Error> error;
                if (config.FromString(configLine, error) == false)
                {
                    LOGERR("Failed to parse config line, error: '%s', config line: '%s'.",
                           (error.IsSet() ? error.Value().Message().c_str() : "Unknown"),
                           configLine.c_str());
                }
            } else if (!configLine.empty() && configLine != "{}") {
                LOGWARN("Skipping config line parsing (expected JSON object). config line: '%s'.", configLine.c_str());
            }

            LOGINFO("Connector: %s", config.Connector.Value().c_str());
            Core::NodeId source(config.Connector.Value().c_str());
            LOGINFO("Parsed port: %d", source.PortNumber());
            mWsManager.SetMessageHandler(
                [this](const std::string &method, const std::string &params, const int requestId, const uint32_t connectionId)
                {
                    Core::IWorkerPool::Instance().Submit(WsMsgJob::Create(this, method, params, requestId, connectionId));
                });

            mWsManager.SetAuthHandler(
                [this](const uint32_t connectionId, const std::string &token) -> bool
                {
                    Core::SafeSyncType<Core::CriticalSection> lock(mAuthenticatorLock);
                    string interfaceToQuery;
                    string sessionId = Utils::ResolveQuery(token, "session");
                    if (sessionId.empty())
                    {
                        LOGERR("No session token provided");
                        return false;
                    }

                    if (nullptr == mAuthenticator) {
                        //Will update the interface first before query call.
                        if (ConfigUtils::useAppManagers()) {
                            interfaceToQuery = COMMON_GATEWAY_AUTHENTICATOR_CALLSIGN;
                        } else {
                            interfaceToQuery = GATEWAY_AUTHENTICATOR_CALLSIGN;
                        }

                        mAuthenticator = mService->QueryInterfaceByCallsign<Exchange::IAppGatewayAuthenticator>(interfaceToQuery.c_str());
                        if (nullptr == mAuthenticator) {
                            LOGERR("AppGateway Authenticator not available");
                            return false;
                        } else {
                            LOGINFO("AppGateway Authenticator interface acquired");
                        }
                    }

                    string appId;
                    if (Core::ERROR_NONE == mAuthenticator->Authenticate(sessionId,appId)) {
                        LOGTRACE("APP ID %s", appId.c_str());
                        mAppIdRegistry.Add(connectionId, appId);
                        
                        // Track WebSocket connection for telemetry (connection-level event, requestId=0)
                        Exchange::GatewayContext context = {0, connectionId, appId};
                        AppGatewayTelemetry::getInstance().IncrementWebSocketConnections(context);
                        
                        mCompliantJsonRpcRegistry.CheckAndAddCompliantJsonRpc(connectionId, token);
                        #ifdef ENABLE_APP_GATEWAY_AUTOMATION
                        // Check if this is the automation client
                        #ifdef AUTOMATION_APP_ID
                        if (appId == AUTOMATION_APP_ID) {
                            mWsManager.SetAutomationId(connectionId);
                            LOGINFO("Automation server connected with ID: %d, appId: %s", connectionId, appId.c_str());
                        }
                        #endif
                        #endif
                        
                        Core::IWorkerPool::Instance().Submit(ConnectionStatusNotificationJob::Create(this, connectionId, appId, true));

                        return true;
                    }

                    // Track external service error - Authentication service failure (auth failed, no appId available)
                    Exchange::GatewayContext authFailContext = {0, connectionId, "UNKNOWN"};
                    AppGatewayTelemetry::getInstance().RecordExternalServiceErrorInternal(authFailContext, AGW_SERVICE_AUTHENTICATION);

                    return false;
                });

            mWsManager.SetDisconnectHandler(
                [this](const uint32_t connectionId)
                {
                    LOGINFO("Connection disconnected: %d", connectionId);
                    
                    // Track WebSocket disconnection for telemetry (connection-level event, requestId=0)
                    string appId;
                    if (!mAppIdRegistry.Get(connectionId, appId)) {
                        LOGERR("No App ID found for connection %d during disconnect", connectionId);
                        appId = "UNKNOWN";
                    } else {
                        LOGINFO("App ID %s found for connection %d during disconnect", appId.c_str(), connectionId);
                    }
                    Exchange::GatewayContext context = {0, connectionId, appId};
                    AppGatewayTelemetry::getInstance().DecrementWebSocketConnections(context);
                    
                    if (appId != "UNKNOWN") {
                        Core::IWorkerPool::Instance().Submit(ConnectionStatusNotificationJob::Create(this, connectionId, appId, false));
                    }
                    
                    mAppIdRegistry.Remove(connectionId);
                    mCompliantJsonRpcRegistry.CleanupConnectionId(connectionId);
                    Exchange::IAppNotifications* appNotifications = mService->QueryInterfaceByCallsign<Exchange::IAppNotifications>(APP_NOTIFICATIONS_CALLSIGN);
                    if (appNotifications != nullptr) {
                        if (Core::ERROR_NONE != appNotifications->Cleanup(connectionId, APP_GATEWAY_CALLSIGN)) {
                            LOGERR("AppNotifications Cleanup failed for connectionId: %d", connectionId);
                        }
                        appNotifications->Release();
                        appNotifications = nullptr;
                    }
                }
            );
            mWsManager.Start(source);
            return Core::ERROR_NONE;
        }

        Core::hresult AppGatewayResponderImplementation::Respond(const Context& context, const string& payload)
        {
            if (mPausedAppsRegistry.IsPaused(context.appId)) {
                LOGDBG("Respond: dropping outgoing response for hibernated appId=%s", context.appId.c_str());
                return Core::ERROR_NONE;
            }
            Core::IWorkerPool::Instance().Submit(RespondJob::Create(this, context.connectionId, context.requestId, payload));
            return Core::ERROR_NONE;
        }

        Core::hresult AppGatewayResponderImplementation::Emit(const Context& context /* @in */, 
                const string& method /* @in */, const string& payload /* @in @opaque */) {
            if (mPausedAppsRegistry.IsPaused(context.appId)) {
                LOGDBG("Emit: dropping outgoing notification for hibernated appId=%s", context.appId.c_str());
                return Core::ERROR_NONE;
            }
            // check if the connection is compliant with JSON RPC
            if (mCompliantJsonRpcRegistry.IsCompliantJsonRpc(context.connectionId)) {
                Core::IWorkerPool::Instance().Submit(EmitJob::Create(this, context.connectionId, method, payload));
            }
            else {
                Core::IWorkerPool::Instance().Submit(RespondJob::Create(this, context.connectionId, context.requestId, payload));
            }
            return Core::ERROR_NONE;
        }

        Core::hresult AppGatewayResponderImplementation::Request(const uint32_t connectionId /* @in */, 
                const uint32_t id /* @in */, const string& method /* @in */, const string& params /* @in @opaque */) {
            string appId;
            if (mAppIdRegistry.Get(connectionId, appId) && mPausedAppsRegistry.IsPaused(appId)) {
                LOGDBG("Request: dropping outgoing request for hibernated appId=%s", appId.c_str());
                return Core::ERROR_NONE;
            }
            Core::IWorkerPool::Instance().Submit(RequestJob::Create(this, connectionId, id, method, params));
            return Core::ERROR_NONE;
        }

        Core::hresult AppGatewayResponderImplementation::GetGatewayConnectionContext(const uint32_t connectionId /* @in */,
                const string& contextKey /* @in */, 
                 string& contextValue /* @out */) {
            // TODO add support for jsonrpc compliance in later versions
            return Core::ERROR_NONE;
        }

        Core::hresult AppGatewayResponderImplementation::RecordGatewayConnectionContext(const uint32_t connectionId ,
                const string& contextKey ,
                const string& contextValue) {
            LOGINFO("Recording context for connectionId: %d, contextKey: %s, contextValue: %s", connectionId, contextKey.c_str(), contextValue.c_str());
            // if contextKey is DISABLE_DEBUG_FOR_CONNECTION, add connectionId to debug disabled registry
            if (DISABLE_DEBUG_FOR_CONNECTION == contextKey ) {
                mDebugDisabledConnectionsRegistry.Add(connectionId);
            } else if (ENABLE_DEBUG_FOR_CONNECTION == contextKey) {
                mDebugDisabledConnectionsRegistry.Remove(connectionId);
            }
            return Core::ERROR_NONE;
        }


        void AppGatewayResponderImplementation::DispatchWsMsg(const std::string &method,
                                                     const std::string &params,
                                                     const uint32_t requestId,
                                                     const uint32_t connectionId)
        {
            Core::SafeSyncType<Core::CriticalSection> lock(mResolverLock);
            std::string resolution;
            string appId;

            // Get appId first to create proper context
            bool hasAppId = mAppIdRegistry.Get(connectionId, appId);
            if (!hasAppId) {
                appId = "UNKNOWN";
            }

            string version  = LEGACY_FIREBOLT_VERSION;
            if (mCompliantJsonRpcRegistry.IsCompliantJsonRpc(connectionId)) {
                version = RDK8_FIREBOLT_VERSION;
            }
            // Create context 
            Context context = {
                requestId,
                connectionId,
                appId,
                std::move(version)
            };

            // Track total API calls for telemetry
            AppGatewayTelemetry::getInstance().IncrementTotalCalls(context);

            if (hasAppId) {

                if (mPausedAppsRegistry.IsPaused(appId)) {
                    LOGDBG("DispatchWsMsg: dropping incoming message for hibernated appId=%s", appId.c_str());
                    return;
                }

                if (mEnhancedLoggingEnabled || !mDebugDisabledConnectionsRegistry.IsDebugDisabled(connectionId)) {
                    if (mEnhancedLoggingEnabled) {
                        LOGDBG("%s-->[[a-%d-%d]] method=%s, params=%s",
                               appId.c_str(), connectionId, requestId, method.c_str(), params.c_str());
                    } else {
                        bool hasSensitiveParams = false;
                        const std::string safeParams = WPEFramework::LogSanitizer::RedactSensitiveForLog(params, hasSensitiveParams);
                        if (hasSensitiveParams) {
                            LOGDBG("%s-->[[a-%d-%d]] method=%s, %s",
                                   appId.c_str(), connectionId, requestId, method.c_str(), safeParams.c_str());
                        } else {
                            LOGDBG("%s-->[[a-%d-%d]] method=%s, params=%s",
                                   appId.c_str(), connectionId, requestId, method.c_str(), safeParams.c_str());
                        }
                    }
                }

                if (nullptr == mResolver) {
                    mResolver = mService->QueryInterface<Exchange::IAppGatewayResolver>();
                    if (nullptr == mResolver) {
                        LOGERR("Resolver interface not available");
                        // Track failed call
                        AppGatewayTelemetry::getInstance().IncrementFailedCalls(context);
                        AppGatewayTelemetry::getInstance().RecordApiError(context, method);
                        return;
                    } else {
                        LOGINFO("Resolver interface acquired");
                    }
                }

                if (Core::ERROR_NONE != mResolver->Resolve(context, APP_GATEWAY_CALLSIGN, method, params, resolution)) {
                    LOGERR("Resolver Failure");
                    // Track failed call and specific API error
                    AppGatewayTelemetry::getInstance().IncrementFailedCalls(context);
                    AppGatewayTelemetry::getInstance().RecordApiError(context, method);
                } else {
                    // Track successful call
                    // Response will be sent asynchronously, so we will track success/failure when sending the response back to client
                }
            } else {
                // No App ID means the disconnect handler already ran and removed it from
                // the registry. The connection is already gone; no explicit close needed.
                LOGWARN("No App ID found for connection %d, connection already disconnected", connectionId);
                // Track failed call due to missing appId
                AppGatewayTelemetry::getInstance().IncrementFailedCalls(context);
            }
        }

        void AppGatewayResponderImplementation::ReturnMessageInSocket(const uint32_t connectionId, 
                                                    const int requestId, const string payload ) {
            if (mEnhancedLoggingEnabled || !mDebugDisabledConnectionsRegistry.IsDebugDisabled(connectionId)) {
                if (mEnhancedLoggingEnabled) {
                    LOGDBG("<--[[a-%d-%d]] payload=%s", connectionId, requestId, payload.c_str());
                } else {
                    bool hasSensitiveData = false;
                    const std::string safePayload = WPEFramework::LogSanitizer::RedactSensitiveForLog(payload, hasSensitiveData);
                    if (hasSensitiveData) {
                        LOGDBG("<--[[a-%d-%d]] %s", connectionId, requestId, safePayload.c_str());
                    } else {
                        LOGDBG("<--[[a-%d-%d]] payload=%s", connectionId, requestId, safePayload.c_str());
                    }
                }
            }

            // Get appId for context
            string appId;
            if (!mAppIdRegistry.Get(connectionId, appId)) {
                appId = "UNKNOWN";
            }

            // Create context for telemetry
            Exchange::GatewayContext context = {(uint32_t)requestId, connectionId, std::move(appId)};
            AppGatewayTelemetry::getInstance().RecordTelemetryEvent(
                context,
                AGW_MARKER_RESPONSE_PAYLOAD_TRACKING,
                payload
            );
            
            // Send response back to client
            mWsManager.SendMessageToConnection(connectionId, payload, requestId);
        }

        Core::hresult AppGatewayResponderImplementation::Register(Exchange::IAppGatewayResponder::INotification *notification)
        {
            ASSERT (nullptr != notification);

            Core::SafeSyncType<Core::CriticalSection> lock(mConnectionStatusImplLock);

            /* Make sure we can't register the same notification callback multiple times */
            if (std::find(mConnectionStatusNotification.begin(), mConnectionStatusNotification.end(), notification) == mConnectionStatusNotification.end())
            {
                LOGINFO("Register notification");
                mConnectionStatusNotification.push_back(notification);
                notification->AddRef();
            }

            return Core::ERROR_NONE;
        }

        Core::hresult AppGatewayResponderImplementation::Unregister(Exchange::IAppGatewayResponder::INotification *notification )
        {
            Core::hresult status = Core::ERROR_GENERAL;

            ASSERT (nullptr != notification);

            Core::SafeSyncType<Core::CriticalSection> lock(mConnectionStatusImplLock);

            /* Make sure we can't unregister the same notification callback multiple times */
            auto itr = std::find(mConnectionStatusNotification.begin(), mConnectionStatusNotification.end(), notification);
            if (itr != mConnectionStatusNotification.end())
            {
                (*itr)->Release();
                LOGINFO("Unregister notification");
                mConnectionStatusNotification.erase(itr);
                status = Core::ERROR_NONE;
            }
            else
            {
                LOGERR("notification not found");
            }

            return status;
        }

        void AppGatewayResponderImplementation::OnConnectionStatusChanged(const string& appId, const uint32_t connectionId, const bool connected)
        {
            Core::SafeSyncType<Core::CriticalSection> lock(mConnectionStatusImplLock);
            for (auto& notification : mConnectionStatusNotification)
            {
                notification->OnAppConnectionChanged(appId, connectionId, connected);
            }

            #ifdef ENABLE_APP_GATEWAY_AUTOMATION
            // Notify automation server of connection status change
            mWsManager.UpdateConnection(connectionId, appId, connected);
            #endif
        }

        Core::hresult AppGatewayResponderImplementation::SuspendTraffic(const string& appId)
        {
            if (appId.empty()) {
                LOGWARN("SuspendTraffic: appId is empty");
                return Core::ERROR_BAD_REQUEST;
            }
            LOGINFO("SuspendTraffic: suspending traffic for appId=%s", appId.c_str());
            mPausedAppsRegistry.Pause(appId);
            return Core::ERROR_NONE;
        }

        Core::hresult AppGatewayResponderImplementation::ResumeTraffic(const string& appId)
        {
            if (appId.empty()) {
                LOGWARN("ResumeTraffic: appId is empty");
                return Core::ERROR_BAD_REQUEST;
            }
            LOGINFO("ResumeTraffic: resuming traffic for appId=%s", appId.c_str());
            mPausedAppsRegistry.Resume(appId);
            return Core::ERROR_NONE;
        }

    } // namespace Plugin
} // namespace WPEFramework
