/*
* If not stated otherwise in this file or this component's LICENSE file the
* following copyright and licenses apply:
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
*/

#pragma once

#include "Module.h"
#include "WsManager.h"
#include <interfaces/IAppGateway.h>
#include <interfaces/IConfiguration.h>
#include "ContextUtils.h"
#include <com/com.h>
#include <core/core.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <unordered_set>
#include <sstream>
#include <unordered_map>


namespace WPEFramework {
namespace Plugin {
    using Context = Exchange::GatewayContext;
    class AppGatewayResponderImplementation : public Exchange::IConfiguration, public Exchange::IAppGatewayResponder
    {

    public:
        AppGatewayResponderImplementation();
        ~AppGatewayResponderImplementation() override;

        // We do not allow this plugin to be copied !!
        AppGatewayResponderImplementation(const AppGatewayResponderImplementation&) = delete;
        AppGatewayResponderImplementation& operator=(const AppGatewayResponderImplementation&) = delete;

        BEGIN_INTERFACE_MAP(AppGatewayResponderImplementation)
        INTERFACE_ENTRY(Exchange::IConfiguration)
        INTERFACE_ENTRY(Exchange::IAppGatewayResponder)
        END_INTERFACE_MAP

    public:
        Core::hresult Respond(const Context& context, const string& payload) override;
         Core::hresult Emit(const Context& context /* @in */, 
                const string& method /* @in */, const string& payload /* @in @opaque */) override;
        Core::hresult Request(const uint32_t connectionId /* @in */, 
                const uint32_t id /* @in */, const string& method /* @in */, const string& params /* @in @opaque */) override;
        Core::hresult GetGatewayConnectionContext(const uint32_t connectionId /* @in */,
                const string& contextKey /* @in */, 
                 string& contextValue /* @out */) override;
        Core::hresult RecordGatewayConnectionContext(const uint32_t connectionId ,
                const string& contextKey ,
                const string& contextValue) override;
        
        virtual Core::hresult Register(Exchange::IAppGatewayResponder::INotification *notification) override;
        virtual Core::hresult Unregister(Exchange::IAppGatewayResponder::INotification *notification) override;

        virtual void OnConnectionStatusChanged(const string& appId, const uint32_t connectionId, const bool connected);
        
        // IConfiguration interface
        uint32_t Configure(PluginHost::IShell* service) override;

    private:

        template <typename TParent>
        class RefCountedDispatchJob : public Core::IDispatch 
        {
            protected:
                RefCountedDispatchJob(TParent* parent)
                    : mParent(*parent)
                {
                    mParent.AddRef();
                }

                ~RefCountedDispatchJob() override
                {
                    mParent.CompleteJob();
                    mParent.Release();
                }

                TParent& mParent;
        };

        class EXTERNAL WsMsgJob : public RefCountedDispatchJob<AppGatewayResponderImplementation>
        {
        protected:
            WsMsgJob(AppGatewayResponderImplementation *parent, 
            const std::string& method,
            const std::string& params,
            const uint32_t requestId,
            const uint32_t connectionId)
                : RefCountedDispatchJob(parent), mMethod(method), mParams(params), mRequestId(requestId), mConnectionId(connectionId)
            {
            }

        public:
            WsMsgJob() = delete;
            WsMsgJob(const WsMsgJob &) = delete;
            WsMsgJob &operator=(const WsMsgJob &) = delete;

        public:
            static Core::ProxyType<Core::IDispatch> Create(AppGatewayResponderImplementation *parent,
                const std::string& method, const std::string& params, const uint32_t requestId,
                const uint32_t connectionId)
            {
                return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<WsMsgJob>::Create(parent, method, params, requestId, connectionId)));
            }
            virtual void Dispatch()
            {
                mParent.DispatchWsMsg(mMethod, mParams, mRequestId, mConnectionId);
            }

        private:
            const std::string mMethod;
            const std::string mParams;
            const uint32_t mRequestId;
            const uint32_t mConnectionId;
        };

        class EXTERNAL RespondJob : public RefCountedDispatchJob<AppGatewayResponderImplementation>
        {
        protected:
            RespondJob(AppGatewayResponderImplementation *parent, 
            const uint32_t connectionId,
            const uint32_t requestId,
            const std::string& payload
            )
                : RefCountedDispatchJob(parent), mPayload(payload), mRequestId(requestId), mConnectionId(connectionId)
            {
            }

        public:
            RespondJob() = delete;
            RespondJob(const RespondJob &) = delete;
            RespondJob &operator=(const RespondJob &) = delete;

        public:
            static Core::ProxyType<Core::IDispatch> Create(AppGatewayResponderImplementation *parent,
                const uint32_t connectionId, const uint32_t requestId, const std::string& payload)
            {
                return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<RespondJob>::Create(parent, connectionId, requestId, payload)));
            }
            virtual void Dispatch()
            {
                mParent.ReturnMessageInSocket(mConnectionId, mRequestId, mPayload);                
            }

        private:
            const std::string mPayload;
            const uint32_t mRequestId;
            const uint32_t mConnectionId;
        };

          class EXTERNAL EmitJob : public RefCountedDispatchJob<AppGatewayResponderImplementation>
        {
        protected:
            EmitJob(AppGatewayResponderImplementation *parent, 
            const uint32_t connectionId,
            const std::string& designator,
            const std::string& payload
            )
                : RefCountedDispatchJob(parent), mPayload(payload), mDesignator(designator), mConnectionId(connectionId)
            {
            }

        public:
            EmitJob() = delete;
            EmitJob(const EmitJob &) = delete;
            EmitJob &operator=(const EmitJob &) = delete;

        public:
            static Core::ProxyType<Core::IDispatch> Create(AppGatewayResponderImplementation *parent,
                const uint32_t connectionId, const std::string& designator, const std::string& payload)
            {
                return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<EmitJob>::Create(parent, connectionId, designator, payload)));
            }
            virtual void Dispatch()
            {
                mParent.mWsManager.DispatchNotificationToConnection(mConnectionId, mDesignator, mPayload);
            }

        private:
            const std::string mPayload;
            const std::string mDesignator;
            const uint32_t mConnectionId;
        };

        class EXTERNAL RequestJob : public RefCountedDispatchJob<AppGatewayResponderImplementation>
        {
        protected:
            RequestJob(AppGatewayResponderImplementation *parent, 
            const uint32_t connectionId,
            const uint32_t requestId,
            const std::string& designator,
            const std::string& payload
            )
                : RefCountedDispatchJob(parent), mPayload(payload), mDesignator(designator), mConnectionId(connectionId), mRequestId(requestId)
            {
            }

        public:
            RequestJob() = delete;
            RequestJob(const RequestJob &) = delete;
            RequestJob &operator=(const RequestJob &) = delete;

        public:
            static Core::ProxyType<Core::IDispatch> Create(AppGatewayResponderImplementation *parent,
                const uint32_t connectionId, const uint32_t mRequestId, const std::string& designator, const std::string& payload)
            {
                return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<RequestJob>::Create(parent, connectionId, mRequestId, designator, payload)));
            }
            virtual void Dispatch()
            {
                mParent.mWsManager.SendRequestToConnection(mConnectionId, mDesignator, mRequestId, mPayload);
            }

        private:
            const std::string mPayload;
            const std::string mDesignator;
            const uint32_t mConnectionId;
            const uint32_t mRequestId;
        };

        class EXTERNAL ConnectionStatusNotificationJob : public RefCountedDispatchJob<AppGatewayResponderImplementation>
        {
        protected:
            ConnectionStatusNotificationJob(AppGatewayResponderImplementation *parent,
            const uint32_t connectionId,
            const std::string& appId,
            const bool connected
            )
                : RefCountedDispatchJob(parent), mConnectionId(connectionId), mAppId(appId), mConnected(connected)
            {
            }

        public:
            ConnectionStatusNotificationJob() = delete;
            ConnectionStatusNotificationJob(const ConnectionStatusNotificationJob &) = delete;
            ConnectionStatusNotificationJob &operator=(const ConnectionStatusNotificationJob &) = delete;

        public:
            static Core::ProxyType<Core::IDispatch> Create(AppGatewayResponderImplementation *parent,
                const uint32_t connectionId, const std::string& appId, const bool connected)
            {
                return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<ConnectionStatusNotificationJob>::Create(parent, connectionId, appId, connected)));
            }
            virtual void Dispatch()
            {
                mParent.OnConnectionStatusChanged(mAppId, mConnectionId, mConnected);
            }

        private:
            const uint32_t mConnectionId;
            const std::string mAppId;
            const bool mConnected;
        };


        class AppIdRegistry{
        public:
            void Add(const uint32_t connectionId, const std::string& appId) {
                std::lock_guard<std::mutex> lock(mAppIdMutex);
                mAppIdMap[connectionId] = appId;
            }

            void Remove(const uint32_t connectionId) {
                std::lock_guard<std::mutex> lock(mAppIdMutex);
                mAppIdMap.erase(connectionId);
            }

            bool Get(const uint32_t connectionId, string& appId) {
                std::lock_guard<std::mutex> lock(mAppIdMutex);
                auto it = mAppIdMap.find(connectionId);
                if (it != mAppIdMap.end()) {
                    appId = it->second;
                    return true;
                }
                return false;
            }


        private:
            std::unordered_map<uint32_t,string> mAppIdMap;
            std::mutex mAppIdMutex;
        };

        // Create new Registry for Debug Disabled Connections
        class DebugDisabledConnectionsRegistry {
        public:
            void Add(const uint32_t connectionId) {
                std::lock_guard<std::mutex> lock(mDebugDisabledMutex);
                mDebugDisabledConnections[connectionId] = true;
            }

            void Remove(const uint32_t connectionId) {
                std::lock_guard<std::mutex> lock(mDebugDisabledMutex);
                mDebugDisabledConnections.erase(connectionId);
            }

            bool IsDebugDisabled(const uint32_t connectionId) {
                std::lock_guard<std::mutex> lock(mDebugDisabledMutex);
                return mDebugDisabledConnections.find(connectionId) != mDebugDisabledConnections.end();
            }

            private:
            std::unordered_map<uint32_t,bool> mDebugDisabledConnections;
            std::mutex mDebugDisabledMutex;
        };

        // Create new Registry for JSON RPC compliant connections
        class CompliantJsonRpcRegistry {
        public:
        // Token substring that indicates a connection is compliant with JSON RPC (RPC version 2).
        // When this substring is present in the authentication token, the connection is treated
        // as JSON RPC compliant and added to the compliant connections registry.
        static constexpr const char* kCompliantJsonRpcFeatureFlag = "RPCv2=true";

        void CheckAndAddCompliantJsonRpc(const uint32_t connectionId, const string& token) {
            // Split all query parameters based on delimiter '&'
            std::stringstream ss(token);
            std::string param;
            while (std::getline(ss, param, '&')) {
                // If any parameter exactly matches the feature flag, add to compliant list
                if (param == kCompliantJsonRpcFeatureFlag) {
                    std::lock_guard<std::mutex> lock(mCompliantJsonRpcMutex);
                    mCompliantJsonRpcConnections.insert(connectionId);
                    break;
                }
            }
        }

        bool IsCompliantJsonRpc(const uint32_t connectionId) {
            std::lock_guard<std::mutex> lock(mCompliantJsonRpcMutex);
            return (mCompliantJsonRpcConnections.find(connectionId) != mCompliantJsonRpcConnections.end());
        }

        void CleanupConnectionId(const uint32_t connectionId) {
            std::lock_guard<std::mutex> lock(mCompliantJsonRpcMutex);
            auto it = mCompliantJsonRpcConnections.find(connectionId);
            if (it != mCompliantJsonRpcConnections.end()) {
                mCompliantJsonRpcConnections.erase(it);
            }
        }

        private:
            // unordered set of connection IDs which are compliant with JSON RPC
            std::unordered_set<uint32_t> mCompliantJsonRpcConnections;
            std::mutex mCompliantJsonRpcMutex;
        };

        void DispatchWsMsg(const std::string& method,
            const std::string& params,
            const uint32_t requestId,
            const uint32_t connectionId);

        void BeginShutdown();
        void CompleteJob();
        bool QueueWorkerJob(const std::function<Core::ProxyType<Core::IDispatch>()>& jobFactory);

        void ReturnMessageInSocket(const uint32_t connectionId, const int requestId, const string payload);

        PluginHost::IShell* mService;
        WebSocketConnectionManager mWsManager;
        mutable Core::CriticalSection mAuthenticatorLock;
        mutable Core::CriticalSection mResolverLock;
        Exchange::IAppGatewayAuthenticator *mAuthenticator; // Shared pointer to Authenticator
        Exchange::IAppGatewayResolver *mResolver; // Shared pointer to InternalGatewayResolver
        AppIdRegistry mAppIdRegistry;
        uint32_t InitializeWebsocket();
        mutable Core::CriticalSection mConnectionStatusImplLock;
        std::list<Exchange::IAppGatewayResponder::INotification*> mConnectionStatusNotification;
        bool mEnhancedLoggingEnabled;
        std::atomic<bool> mStopping{false};
        std::atomic<uint32_t> mActiveJobs{0};
        std::mutex mShutdownMutex;
        std::condition_variable mShutdownCv;
        CompliantJsonRpcRegistry mCompliantJsonRpcRegistry;
        DebugDisabledConnectionsRegistry mDebugDisabledConnectionsRegistry;
    };
} // namespace Plugin
} // namespace WPEFramework
