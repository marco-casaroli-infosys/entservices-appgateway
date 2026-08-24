/*
 * Copyright 2023 Comcast Cable Communications Management, LLC
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
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "Module.h"
#include <interfaces/IAppGateway.h>
#include <interfaces/IAppNotifications.h>
#include <interfaces/IConfiguration.h>
#include <mutex>
#include <map>
#include "UtilsLogging.h"
#include "UtilsController.h"
#include "ContextUtils.h"
#include "UtilsCallsign.h"
#include "UtilsAppGatewayTelemetry.h"

namespace WPEFramework {
namespace Plugin {
    class AppNotificationsImplementation : public Exchange::IAppNotifications, public Exchange::IConfiguration {
    private:
        class SubscriberMap {
        public:
            SubscriberMap(AppNotificationsImplementation& parent) : mParent(parent),
            mSubscriberMutex(),
            mSubscribers(),
            mAppGateway(nullptr),
            mInternalGatewayNotifier(nullptr){}

            ~SubscriberMap() {
                // cleanup mutex and map
                std::lock_guard<std::mutex> lock(mSubscriberMutex);
                mSubscribers.clear();
                if (mAppGateway != nullptr) {
                    mAppGateway->Release();
                    mAppGateway = nullptr;
                } 

                if (mInternalGatewayNotifier != nullptr) {
                    mInternalGatewayNotifier->Release();
                    mInternalGatewayNotifier = nullptr;
                }
            }

            void Add(const string& key, const Exchange::IAppNotifications::AppNotificationContext& context);

            void Remove(const string& key, const Exchange::IAppNotifications::AppNotificationContext& context);

            std::vector<Exchange::IAppNotifications::AppNotificationContext> Get(const string& key) const;

            bool Exists(const string& key) const;

            // create a new method called EventUpdate which accepts the key and JsonObject parameters
            // the method first checks if the given key is available in the mSubscribers uses the mSubscriberMutex lock
            // after verifications it loops through the iterator of values for the given key
            void EventUpdate(const string& key, const string& payloadStr, const string& appId );

            // Create a method to check if the SubscriberMap
            void Dispatch(const string& key, const Exchange::IAppNotifications::AppNotificationContext& context, const string& payload);

            void DispatchToGateway(const string& key, const Exchange::IAppNotifications::AppNotificationContext& context, const string& payload);

            void DispatchToLaunchDelegate(const string& key, const Exchange::IAppNotifications::AppNotificationContext& context, const string& payload);
            void CleanupNotifications(const uint32_t &connectionId, const string& origin);
        private:
            AppNotificationsImplementation& mParent;
            mutable std::mutex mSubscriberMutex;
            mutable Core::CriticalSection mAppGatewayLock;
            mutable Core::CriticalSection mInternalGatewayNotifierLock;
            std::map<string, std::vector<Exchange::IAppNotifications::AppNotificationContext>> mSubscribers;
            Exchange::IAppGatewayResponder *mAppGateway;
            Exchange::IAppGatewayResponder *mInternalGatewayNotifier;
        };

        // Class to accept the module and event and subscribe to Thunder and use a handler
        // to print the notifications dispatched from Thunder.
        // Class will have options for:
        // 1. Subscribe which accepts module event
        // 2. Unsubscribe which accepts module and event
        class ThunderSubscriptionManager {
            public:
                ThunderSubscriptionManager(AppNotificationsImplementation& parent) : mParent(parent) {
                }

                ~ThunderSubscriptionManager();

                void Subscribe(const string& module, const string& event);

                void Unsubscribe(const string& module, const string& event);

                bool HandleNotifier(const string& module, const string& event, const bool& listen);

                // Add a method to lock the mThunderSubscriberMutex mutex and add an entry to mRegisteredNotifications
                void RegisterNotification(const string& module, const string& event);

                // Remove an entry from the mRegisteredNotifications
                void UnregisterNotification(const string& module, const string& event);

                // check if a given string has an existing entry in mRegisteredNotifications
                bool IsNotificationRegistered(const string& module, const string& notification) const;

            private:

                struct NotificationKey {
                    string module;
                    string event;

                    bool operator==(const NotificationKey& other) const {
                        return (this->module == other.module) && (this->event == other.event);
                    }
                };

                AppNotificationsImplementation& mParent;
                mutable std::mutex mThunderSubscriberMutex;
                std::vector<NotificationKey> mRegisteredNotifications;
        };

        AppNotificationsImplementation(const AppNotificationsImplementation&) = delete;
        AppNotificationsImplementation& operator=(const AppNotificationsImplementation&) = delete;

    public:
        AppNotificationsImplementation();
        ~AppNotificationsImplementation();

        BEGIN_INTERFACE_MAP(AppNotificationsImplementation)
        INTERFACE_ENTRY(Exchange::IAppNotifications)
        INTERFACE_ENTRY(Exchange::IConfiguration)
        END_INTERFACE_MAP

        // IAppNotifications interface
        Core::hresult Subscribe(const Exchange::IAppNotifications::AppNotificationContext &context /* @in */,
                                            bool listen /* @in */,
                                            const string &module /* @in */,
                                            const string &event /* @in */) override;

        Core::hresult Emit(const string &event /* @in */,
                                    const string &payload /* @in @opaque */,
                                    const string &appId /* @in */) override;

        Core::hresult Cleanup(const uint32_t connectionId /* @in */, const string &origin /* @in */) override;

        // IConfiguration interface
        uint32_t Configure(PluginHost::IShell* shell);

        class EXTERNAL SubscriberJob : public Core::IDispatch 
        {
            public:
                SubscriberJob(AppNotificationsImplementation* delegate, const string& module, const string& event, const bool subscribe)
                    : mParent(*delegate), mEvent(event), mModule(module), mSubscribe(subscribe),
                      AGW_JOB_CAPTURE_SUBMIT_TIME() {}

                SubscriberJob() = delete;
                SubscriberJob(const SubscriberJob &) = delete;
                SubscriberJob &operator=(const SubscriberJob &) = delete;
                ~SubscriberJob()
                {
                }

                static Core::ProxyType<Core::IDispatch> Create(AppNotificationsImplementation *parent,
                const string& module, const string& event, const bool subscribe)
                {
                    return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<SubscriberJob>::Create(parent, module, event, subscribe)));
                }
                
                virtual void Dispatch()
                {
                    AGW_TRACK_JOB_LATENCY(timer, "SubscriberJob[" + std::string(mSubscribe?"sub":"unsub") + ":" + mEvent + "]",
                        mEvent);
                    if (mSubscribe) {
                        mParent.mThunderManager.Subscribe(mModule, mEvent);
                    } else {
                        mParent.mThunderManager.Unsubscribe(mModule, mEvent);
                    }
                }

            private:
                AppNotificationsImplementation &mParent;
                string mEvent;
                string mModule;
                bool mSubscribe;
                std::chrono::steady_clock::time_point mSubmitTime;
        };

        class EXTERNAL EmitJob : public Core::IDispatch 
        {
            public:
                EmitJob(AppNotificationsImplementation* delegate, const string& event, const string& payload, const string& appId)
                    : mParent(*delegate), mEvent(event), mPayload(payload), mAppId(appId),
                      AGW_JOB_CAPTURE_SUBMIT_TIME() {}

                EmitJob() = delete;
                EmitJob(const EmitJob &) = delete;
                EmitJob &operator=(const EmitJob &) = delete;
                ~EmitJob()
                {
                }

                static Core::ProxyType<Core::IDispatch> Create(AppNotificationsImplementation *parent,
                const string& event, const string& payload, const string& appId)
                {
                    return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<EmitJob>::Create(parent, event, payload, appId)));
                }
                
                virtual void Dispatch()
                {
                    AGW_TRACK_JOB_LATENCY(timer, "NotifEmitJob[" + mEvent + "]",
                        mAppId);
                    mParent.mSubMap.EventUpdate(mEvent, mPayload, mAppId);
                }

            private:
                AppNotificationsImplementation &mParent;
                string mEvent;
                string mPayload;
                string mAppId;
                std::chrono::steady_clock::time_point mSubmitTime;
        };

        class Emitter: public Exchange::IAppNotificationHandler::IEmitter {
        private:
            Emitter(const Emitter&) = delete;
            Emitter& operator=(const Emitter&) = delete;
        public:
            Emitter(AppNotificationsImplementation& parent) : mParent(parent) {}

            virtual void Emit(const string &event /* @in */,
                              const string &payload /* @in @opaque */,
                              const string &appId /* @in */) override
            {
                LOGINFO("Emit [event= %s payload=%s appId=%s]",
                    event.c_str(), payload.c_str(), appId.c_str());
                Core::IWorkerPool::Instance().Submit(EmitJob::Create(&mParent, event, payload, appId));
            }

            BEGIN_INTERFACE_MAP(Emitter)
            INTERFACE_ENTRY(Exchange::IAppNotificationHandler::IEmitter)
            END_INTERFACE_MAP

        private:
            AppNotificationsImplementation& mParent;
        };

    private:
        PluginHost::IShell* mShell;
        SubscriberMap mSubMap;
        ThunderSubscriptionManager mThunderManager;
        Core::Sink<Emitter> mEmitter;
    };
}
}
