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
#include "AppNotificationsImplementation.h"
#include "UtilsLogging.h"
#include "StringUtils.h"

namespace WPEFramework
{
    namespace Exchange {
        bool operator==(const IAppNotifications::AppNotificationContext& lhs,
                        const IAppNotifications::AppNotificationContext& rhs) {
            return lhs.requestId == rhs.requestId &&
                lhs.connectionId == rhs.connectionId &&
                lhs.appId == rhs.appId &&
                lhs.origin == rhs.origin &&
                lhs.version == rhs.version;
        }
    }
    namespace Plugin
    {
        SERVICE_REGISTRATION(AppNotificationsImplementation, 1, 0);

        AppNotificationsImplementation::AppNotificationsImplementation() : 
        mStopping(false),
        mShell(nullptr),
        mSubMap(*this),
        mThunderManager(*this),
        mEmitter(*this)
        {
        }

        AppNotificationsImplementation::~AppNotificationsImplementation()
        {
            Stop();
        }

        void AppNotificationsImplementation::Stop()
        {
            {
                std::lock_guard<std::mutex> lock(mAdminLock);
                if (mStopInitiated) {
                    return;
                }
                mStopInitiated = true;
            }

            // Unsubscribe from all registered Thunder notifications while mShell and
            // mEmitter are still valid. This must happen before mStopping is set,
            // since HandleNotifier()/GetActiveShell() short-circuit once the
            // implementation is marked stopped, which would otherwise leave target
            // plugins holding a callback into a soon-to-be-destroyed mEmitter.
            mThunderManager.UnsubscribeAll();

            PluginHost::IShell* shellToRelease = nullptr;
            {
                std::lock_guard<std::mutex> lock(mAdminLock);
                mStopping = true;
                shellToRelease = mShell;
                mShell = nullptr;
            }

            if (shellToRelease != nullptr)
            {
                shellToRelease->Release();
            }
        }

        bool AppNotificationsImplementation::IsStopping() const
        {
            std::lock_guard<std::mutex> lock(mAdminLock);
            return mStopping;
        }

        PluginHost::IShell* AppNotificationsImplementation::GetActiveShell() const
        {
            std::lock_guard<std::mutex> lock(mAdminLock);
            if (mStopping || mShell == nullptr) {
                return nullptr;
            }
            mShell->AddRef();
            return mShell;
        }

        Core::hresult AppNotificationsImplementation::Subscribe(const Exchange::IAppNotifications::AppNotificationContext &context /* @in */,
                                            bool listen /* @in */,
                                            const string &module /* @in */,
                                            const string &event /* @in */) {
            if (IsStopping()) {
                return Core::ERROR_ILLEGAL_STATE;
            }
            LOGTRACE("Subscribe [requestId=%d appId=%s connectionId=%d] register=%s, module=%s, event=%s, version=%s",
                    context.requestId, context.appId.c_str(), context.connectionId,
                    listen ? "true" : "false", module.c_str(), event.c_str(), context.version.c_str());

            if (module.empty())
            {
                LOGERR("module parameter cannot be empty!");
                return Core::ERROR_INVALID_PARAMETER;
            }
            if (listen) {
                if (!mSubMap.Exists(event)) {
                    // Thunder subscription
                    Core::IWorkerPool::Instance().Submit(SubscriberJob::Create(this, module, event, listen));
                }
                mSubMap.Add(event, context);
            } else {
                mSubMap.Remove(event, context);
                // If all elements are removed the entry is erased automatically
                // This can be used to measure unsubscribe
                if (!mSubMap.Exists(event)) {
                    // Thunder unsubscription
                    Core::IWorkerPool::Instance().Submit(SubscriberJob::Create(this, module, event, listen));
                }
            }
            return Core::ERROR_NONE;
        }

        Core::hresult AppNotificationsImplementation::Emit(const string &event /* @in */,
                                    const string &payload /* @in @opaque */,
                                    const string &appId /* @in */) {
            if (IsStopping()) {
                return Core::ERROR_ILLEGAL_STATE;
            }

            LOGTRACE("Emit [event= %s payload=%s appId=%s]",
                    event.c_str(), payload.c_str(), appId.c_str());
            Core::IWorkerPool::Instance().Submit(EmitJob::Create(this, event, payload, appId));
            return Core::ERROR_NONE;
        }

        Core::hresult AppNotificationsImplementation::Cleanup(const uint32_t connectionId /* @in */, const string &origin /* @in */) {
            if (IsStopping()) {
                return Core::ERROR_NONE;
            }

            LOGTRACE("Cleanup [connectionId=%d origin=%s]", connectionId, origin.c_str());
            mSubMap.CleanupNotifications(connectionId, origin);
            return Core::ERROR_NONE;
        }

        void AppNotificationsImplementation::SubscriberMap::CleanupNotifications(const uint32_t &connectionId, const string& origin) {
            std::lock_guard<std::mutex> lock(mSubscriberMutex);
            for (auto it = mSubscribers.begin(); it != mSubscribers.end(); ) {
                auto& vec = it->second;
                vec.erase(std::remove_if(vec.begin(), vec.end(),
                    [&](const Exchange::IAppNotifications::AppNotificationContext& context) {
                        return (context.connectionId == connectionId) && (context.origin == origin);
                    }), vec.end());
                if (vec.empty()) {
                    it = mSubscribers.erase(it);
                } else {
                    ++it;
                }
            }
        }

        uint32_t AppNotificationsImplementation::Configure(PluginHost::IShell *shell)
        {
            LOGINFO("Configuring AppNotifications");
            uint32_t result = Core::ERROR_NONE;
            ASSERT(shell != nullptr);
            {
                std::lock_guard<std::mutex> lock(mAdminLock);
                mShell = shell;
                mShell->AddRef();
            }
            return result;
        }

        void AppNotificationsImplementation::SubscriberMap::Add(const string& key, const Exchange::IAppNotifications::AppNotificationContext& context) {
            string lowerKey = StringUtils::toLower(key);
            std::lock_guard<std::mutex> lock(mSubscriberMutex);
            mSubscribers[lowerKey].push_back(context);
        }
        
        void AppNotificationsImplementation::SubscriberMap::Remove(const string& key, const Exchange::IAppNotifications::AppNotificationContext& context) {
            std::lock_guard<std::mutex> lock(mSubscriberMutex);
            string lowerKey = StringUtils::toLower(key);
            auto it = mSubscribers.find(lowerKey);
            if (it != mSubscribers.end()) {
                auto& vec = it->second;
                vec.erase(std::remove(vec.begin(), vec.end(), context), vec.end());
                if (vec.empty()) {
                mSubscribers.erase(it);
                }
            }
        }

        std::vector<Exchange::IAppNotifications::AppNotificationContext> AppNotificationsImplementation::SubscriberMap::Get(const string& key) const {
            std::lock_guard<std::mutex> lock(mSubscriberMutex);
            string lowerKey = StringUtils::toLower(key);
            auto it = mSubscribers.find(lowerKey);
            if (it != mSubscribers.end()) {
                return it->second;
            }
            return {};
        }

        bool AppNotificationsImplementation::SubscriberMap::Exists(const string& key) const {
            std::lock_guard<std::mutex> lock(mSubscriberMutex);
            string lowerKey = StringUtils::toLower(key);
            auto it = mSubscribers.find(lowerKey);
            return it != mSubscribers.end();
        }

        void AppNotificationsImplementation::SubscriberMap::EventUpdate(const string& key, const string& payloadStr, const string& appId ) {
            if (mParent.IsStopping()) {
                return;
            }

            std::lock_guard<std::mutex> lock(mSubscriberMutex);
            string lowerKey = StringUtils::toLower(key);
            // Remove version information from the event key to match subscription keys
            string clearKey = ContextUtils::GetBaseEventNameFromVersionedEvent(key);
            auto it = mSubscribers.find(lowerKey);
            if (it != mSubscribers.end()) {
                for (const auto& context : it->second) {
                    // check if app id is not empty if not empty check if context.appId matches appId
                    if (!appId.empty()) {

                        if(context.appId == appId) {
                            // Dispatch the event to the subscriber
                            Dispatch(clearKey, context, payloadStr);
                        }
                        
                    } else {
                        // Dispatch the event to the subscriber
                        Dispatch(clearKey, context, payloadStr);
                    }
                }
            } else {
                // using LOGWARN print a warning that there are no active listeners for this event
                LOGWARN("No active listeners for event: %s", key.c_str());
            }
        }

        void AppNotificationsImplementation::SubscriberMap::Dispatch(const string& key, const Exchange::IAppNotifications::AppNotificationContext& context, const string& payload) {
            if (ContextUtils::IsOriginGateway(context.origin)) {
                DispatchToGateway(key, context, payload);
            } else {
                DispatchToLaunchDelegate(key, context, payload);
            }
        }

        void AppNotificationsImplementation::SubscriberMap::DispatchToGateway(const string& key, const Exchange::IAppNotifications::AppNotificationContext& context, const string& payload) {
            Core::SafeSyncType<Core::CriticalSection> lock(mAppGatewayLock);
            if (nullptr == mAppGateway) {
                PluginHost::IShell* shell = mParent.GetActiveShell();
                if (nullptr == shell) {
                    LOGERR("Shell not available, cannot acquire AppGateway Responder interface");
                    return;
                }
                mAppGateway = shell->QueryInterfaceByCallsign<Exchange::IAppGatewayResponder>(APP_GATEWAY_CALLSIGN);
                shell->Release();
                if (nullptr == mAppGateway) {
                    LOGERR("Failed to get AppGateway Responder interface");
                    return;
                } else {
                    LOGINFO("AppGateway Responder interface acquired successfully");
                }
            }
            Exchange::GatewayContext gatewayContext = ContextUtils::ConvertNotificationToAppGatewayContext(context);
            mAppGateway->Emit(gatewayContext, key, payload);
        }

        void AppNotificationsImplementation::SubscriberMap::DispatchToLaunchDelegate(const string& key, const Exchange::IAppNotifications::AppNotificationContext& context, const string& payload) {
            Core::SafeSyncType<Core::CriticalSection> lock(mInternalGatewayNotifierLock);
            if (nullptr == mInternalGatewayNotifier) {
                PluginHost::IShell* shell = mParent.GetActiveShell();
                if (nullptr == shell) {
                    LOGERR("Shell not available, cannot acquire InternalGatewayNotifier interface");
                    return;
                }
                mInternalGatewayNotifier = shell->QueryInterfaceByCallsign<Exchange::IAppGatewayResponder>(INTERNAL_GATEWAY_CALLSIGN);
                shell->Release();
                if (nullptr == mInternalGatewayNotifier) {
                    LOGERR("Failed to get InternalGatewayNotifier interface");
                    return;
                } else {
                    LOGINFO("InternalGatewayNotifier interface acquired successfully");
                }
            }
            Exchange::GatewayContext gatewayContext = ContextUtils::ConvertNotificationToAppGatewayContext(context);
            mInternalGatewayNotifier->Emit(gatewayContext, key, payload);
        }

        AppNotificationsImplementation::ThunderSubscriptionManager::~ThunderSubscriptionManager() {
            UnsubscribeAll();
        }

        void AppNotificationsImplementation::ThunderSubscriptionManager::UnsubscribeAll() {
            // Copy notifications to avoid holding the lock during external calls
            std::vector<NotificationKey> notificationsCopy;
            {
                std::lock_guard<std::mutex> lock(mThunderSubscriberMutex);
                notificationsCopy = mRegisteredNotifications;
                mRegisteredNotifications.clear();
            }
            // Unsubscribe from all registered notifications outside the lock
            for (const auto& notification : notificationsCopy) {
                HandleNotifier(notification.module, notification.event, false);
            }
        }

        void AppNotificationsImplementation::ThunderSubscriptionManager::Subscribe(const string& module, const string& event) {
            // Subscribe to Thunder notifications

            // check if the notification is already registered
            if (IsNotificationRegistered(module, event)) {
                // log a trace message that the notification is already registered
                LOGTRACE("Notification is already registered: %s", event.c_str());
            } else {
                // trigger RegisterNotification
                RegisterNotification(module, event);
            }
        }

        void AppNotificationsImplementation::ThunderSubscriptionManager::Unsubscribe(const string& module, const string& event) {
            // Unsubscribe from Thunder notifications
            
            // check if the notification is already registered
            if (IsNotificationRegistered(module, event)) {
                // trigger UnregisterNotification
                UnregisterNotification(module, event);
            } else {
                // log an error that the notification is not registered
                LOGERR("Notification is not registered: %s", event.c_str());
            }
        }

        bool AppNotificationsImplementation::ThunderSubscriptionManager::HandleNotifier(const string& module, const string& event, const bool& listen) {
            PluginHost::IShell* shell = mParent.GetActiveShell();
            if (nullptr == shell) {
                LOGERR("Shell not available, cannot handle notifier for module=%s", module.c_str());
                return false;
            }

            // Check if Plugins is activated before making a request
            bool status = false;
            Exchange::IAppNotificationHandler *internalNotifier = shell->QueryInterfaceByCallsign<Exchange::IAppNotificationHandler>(module);
            shell->Release();
            if (internalNotifier != nullptr) {
                if (Core::ERROR_NONE == internalNotifier->HandleAppEventNotifier(&mParent.mEmitter, event, listen, status)) {
                    LOGTRACE("Notifier status for %s:%s is %s", module.c_str(), event.c_str(), status ? "true" : "false");
                } else {
                    LOGERR("Notification subscription failure");
                }
                internalNotifier->Release();
            } else {
                LOGERR("Notification Handler not available for module=%s", module.c_str());
            }
            return status;
        }

        void AppNotificationsImplementation::ThunderSubscriptionManager::RegisterNotification(const string& module, const string& event) {
            string lowerEvent = StringUtils::toLower(event);
            // call notifier and start listening
            if (HandleNotifier(module, event, true)) {
                std::lock_guard<std::mutex> lock(mThunderSubscriberMutex);
                mRegisteredNotifications.push_back({module, std::move(lowerEvent)});
            }
        }

        void AppNotificationsImplementation::ThunderSubscriptionManager::UnregisterNotification(const string& module, const string& event) {
            string lowerEvent = StringUtils::toLower(event);
            if (HandleNotifier(module, lowerEvent, false)) {
                std::lock_guard<std::mutex> lock(mThunderSubscriberMutex);
                mRegisteredNotifications.erase(std::remove(mRegisteredNotifications.begin(), mRegisteredNotifications.end(), NotificationKey{module, std::move(lowerEvent)}), mRegisteredNotifications.end());
            }
        }

        bool AppNotificationsImplementation::ThunderSubscriptionManager::IsNotificationRegistered(const string& module, const string& notification) const {
            string lowerEvent = StringUtils::toLower(notification);
            std::lock_guard<std::mutex> lock(mThunderSubscriberMutex);
            return std::find(mRegisteredNotifications.begin(), mRegisteredNotifications.end(), NotificationKey{module, std::move(lowerEvent)}) != mRegisteredNotifications.end();
        }

    }
}
