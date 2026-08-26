#pragma once
#ifndef __APPACTIONSIMPLEMENTATION_H__
#define __APPACTIONSIMPLEMENTATION_H__
#include <algorithm>
#include <list>
#include <mutex>
#include "Module.h"
#include <interfaces/IConfiguration.h>
#include <interfaces/IAppActions.h>
#include "UtilsAppGatewayTelemetry.h"

namespace WPEFramework {
namespace Plugin {

class AppActionsImplementation :
    public PluginHost::IPlugin,
    public Exchange::IAppActions,
    public Exchange::IConfiguration {
        public:

            AppActionsImplementation();
            ~AppActionsImplementation() override;

            // We do not allow this plugin to be copied !!
            AppActionsImplementation(const AppActionsImplementation&) = delete;
            AppActionsImplementation& operator=(const AppActionsImplementation&) = delete;

        BEGIN_INTERFACE_MAP(AppActionsImplementation)
            INTERFACE_ENTRY(Exchange::IConfiguration)
            INTERFACE_ENTRY(PluginHost::IPlugin)
            INTERFACE_ENTRY(Exchange::IAppActions)
        END_INTERFACE_MAP

        const string Initialize(PluginHost::IShell* service) override;
        void Deinitialize(PluginHost::IShell* service) override;
        string Information() const override;
        Core::hresult Configure(PluginHost::IShell* service) override;

        // IAppActions
        Core::hresult ActionStart(
            const string& Initiator,
            const string& Intent,
            const string& HandlerAppId
        ) override;

        Core::hresult Register(
            Exchange::IAppActions::INotification* notification
        ) override;

        Core::hresult Unregister(
            Exchange::IAppActions::INotification* notification
        ) override;

        void DispatchActionStartRequest(const string& initiator, const string& intent, const string& handlerAppId);

    private:

        class EXTERNAL NotifyJob : public Core::IDispatch
        {
        public:
            NotifyJob(AppActionsImplementation* parent, const string& initiator, const string& intent, const string& handlerAppId)
                : mParent(*parent), mInitiator(initiator), mIntent(intent), mHandlerAppId(handlerAppId),
                  AGW_JOB_CAPTURE_SUBMIT_TIME()
            {
                mParent.AddRef();
            }

            NotifyJob() = delete;
            NotifyJob(const NotifyJob&) = delete;
            NotifyJob& operator=(const NotifyJob&) = delete;
            ~NotifyJob()
            {
                mParent.Release();
            }

            static Core::ProxyType<Core::IDispatch> Create(AppActionsImplementation* parent,
                const string& initiator, const string& intent, const string& handlerAppId)
            {
                return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<NotifyJob>::Create(parent, initiator, intent, handlerAppId)));
            }

            void Dispatch() override
            {
                AGW_TRACK_JOB_LATENCY(timer, "NotifyJob[" + mIntent + "]",
                    0, 0, mHandlerAppId);
                mParent.DispatchActionStartRequest(mInitiator, mIntent, mHandlerAppId);
            }

        private:
            AppActionsImplementation& mParent;
            const string mInitiator;
            const string mIntent;
            const string mHandlerAppId;
            std::chrono::steady_clock::time_point mSubmitTime;
        };

        PluginHost::IShell *mService;
        std::list<Exchange::IAppActions::INotification*> mAppActionsNotifications;
        mutable std::mutex mAdminLock;
    };

} // namespace Plugin
} // namespace WPEFramework
#endif // __APPACTIONSIMPLEMENTATION_H__