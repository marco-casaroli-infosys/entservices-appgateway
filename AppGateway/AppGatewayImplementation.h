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
#include "Resolver.h"
#include <interfaces/IAppGateway.h>
#include <interfaces/IConfiguration.h>
#include <interfaces/IAppNotifications.h>
#include "ContextUtils.h"
#include <com/com.h>
#include <core/core.h>
#include <map>
#include "UtilsAppGatewayTelemetry.h"


namespace WPEFramework {
namespace Plugin {
    using Context = Exchange::GatewayContext;
    class AppGatewayImplementation : public Exchange::IAppGatewayResolver, public Exchange::IConfiguration
    {

    public:
        AppGatewayImplementation();
        ~AppGatewayImplementation() override;

        // We do not allow this plugin to be copied !!
        AppGatewayImplementation(const AppGatewayImplementation&) = delete;
        AppGatewayImplementation& operator=(const AppGatewayImplementation&) = delete;

        BEGIN_INTERFACE_MAP(AppGatewayImplementation)
        INTERFACE_ENTRY(Exchange::IConfiguration)
        INTERFACE_ENTRY(Exchange::IAppGatewayResolver)
        END_INTERFACE_MAP

    public:
        Core::hresult Configure(Exchange::IAppGatewayResolver::IStringIterator *const &paths) override;
        Core::hresult Resolve(const Context& context, const string& origin ,const string& method, const string& params, string& result) override;

        // IConfiguration interface
        uint32_t Configure(PluginHost::IShell* service) override;

    private:

        class EXTERNAL RespondJob : public Core::IDispatch
        {
        protected:
            RespondJob(AppGatewayImplementation *parent, 
            const Context& context,
            const std::string& payload,
            const std::string& destination
            )
                : mParent(*parent), mPayload(payload), mContext(context), mDestination(destination),
                  AGW_JOB_CAPTURE_SUBMIT_TIME()
            {
                mParent.AddRef();
            }

        public:
            RespondJob() = delete;
        RespondJob(const RespondJob &) = delete;
            RespondJob &operator=(const RespondJob &) = delete;
            ~RespondJob()
            {
                mParent.Release();
            }

        public:
            static Core::ProxyType<Core::IDispatch> Create(AppGatewayImplementation *parent,
                const Context& context, const std::string& payload, const std::string& origin)
            {
                return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<RespondJob>::Create(parent, context, payload, origin)));
            }
            virtual void Dispatch()
            {
                AGW_TRACK_JOB_LATENCY(timer, "ImplRespondJob",
                    std::to_string(mContext.requestId) + ":" + mContext.appId);
                if(ContextUtils::IsOriginGateway(mDestination)) {
                    mParent.ReturnMessageInSocket(mContext, std::move(mPayload));
                } else {
                    mParent.SendToLaunchDelegate(mContext, std::move(mPayload));
                }
                
            }

        private:
            AppGatewayImplementation &mParent;
            const std::string mPayload;
            const Context mContext;
            const std::string mDestination;
            std::chrono::steady_clock::time_point mSubmitTime;
        };

        class EXTERNAL EventHookJob : public Core::IDispatch
        {
        protected:
            EventHookJob(AppGatewayImplementation* parent,
                const Context& context,
                const std::string& hookMethod)
                : mParent(*parent), mContext(context), mHookMethod(hookMethod),
                  AGW_JOB_CAPTURE_SUBMIT_TIME()
            {
                mParent.AddRef();
            }

        public:
            EventHookJob() = delete;
            EventHookJob(const EventHookJob&) = delete;
            EventHookJob& operator=(const EventHookJob&) = delete;
            ~EventHookJob()
            {
                mParent.Release();
            }

        public:
            static Core::ProxyType<Core::IDispatch> Create(AppGatewayImplementation* parent,
                const Context& context, const std::string& hookMethod)
            {
                return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<EventHookJob>::Create(parent, context, hookMethod)));
            }
            virtual void Dispatch() override;

        private:
            AppGatewayImplementation& mParent;
            const Context mContext;
            const std::string mHookMethod;
            std::chrono::steady_clock::time_point mSubmitTime;
        };

        Core::hresult HandleEvent(const Context &context, const string &alias, const string &event, const string &origin,  const bool listen);
                
        void ReturnMessageInSocket(const Context& context, const string payload ) {
            Core::SafeSyncType<Core::CriticalSection> lock(mAppGatewayResponderLock);
            if (nullptr == mAppGatewayResponder) {
                mAppGatewayResponder = mService->QueryInterface<Exchange::IAppGatewayResponder>();
                if (nullptr == mAppGatewayResponder) {
                    LOGERR("AppGateway Responder not available");
                    return;
                } else {
                    LOGINFO("AppGateway Responder interface acquired");
                }
            }
            if (Core::ERROR_NONE != mAppGatewayResponder->Respond(context, payload)) {
                LOGERR("Failed to Respond in Gateway");
            }
        }

        PluginHost::IShell* mService;
        ResolverPtr mResolverPtr;
        mutable Core::CriticalSection mAppNotificationsLock;
        mutable Core::CriticalSection mAppGatewayResponderLock;
        mutable Core::CriticalSection mInternalGatewayResponderLock;
        Exchange::IAppNotifications *mAppNotifications; // Shared pointer to AppNotifications
        Exchange::IAppGatewayResponder *mAppGatewayResponder;
        Exchange::IAppGatewayResponder *mInternalGatewayResponder; // Shared pointer to InternalGatewayResponder
        uint32_t InitializeResolver();
        uint32_t InitializeWebsocket();
        uint32_t ProcessComRpcRequest(const Context &context, const string& alias, const string& method, const string& params, const string& origin, string &resolution);
        uint32_t PreProcessEvent(const Context &context, const string& alias, const string &method, const string& origin, const string& params, string &resolution);
        string UpdateContext(const Context &context, const string& method, const string& params, const string& origin, const bool& onlyAdditionalContext = false);
        Core::hresult InternalResolve(const Context &context, const string &method, const string &params, const string &origin, string& resolution);
        Core::hresult FetchResolvedData(const Context &context, const string &method, const string &params, const string &origin, string& resolution);
        Core::hresult InternalResolutionConfigure(std::vector<std::string>&& configPaths);
        void SendToLaunchDelegate(const Context& context, const string& payload);
        std::string ReadCountryFromConfigFile();
    };
} // namespace Plugin
} // namespace WPEFramework
