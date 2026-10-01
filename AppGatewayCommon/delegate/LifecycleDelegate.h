/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
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

#pragma once

#ifndef __LIFECYCLEDELEGATE_H__
#define __LIFECYCLEDELEGATE_H__

#include "StringUtils.h"
#include "BaseEventDelegate.h"
#include <interfaces/ILifecycleManagerState.h>
#include <interfaces/IRDKWindowManager.h>
#include <interfaces/IRuntimeManager.h>
#include <interfaces/IAppActions.h>
#include "UtilsLogging.h"
#include "UtilsCallsign.h"
#include "UtilsFirebolt.h"
#include <atomic>
#include <set>

using namespace WPEFramework;

#define LIFECYCLE_MANAGER_CALLSIGN "org.rdk.LifecycleManager"
#define WINDOW_MANAGER_CALLSIGN "org.rdk.RDKWindowManager"
#define RUNTIME_MANAGER_CALLSIGN "org.rdk.RuntimeManager"
#define APP_ACTIONS_CALLSIGN "org.rdk.AppActions"

// Valid lifecycle events that can be subscribed to
static const std::set<string> VALID_LIFECYCLE_EVENT = {
    "lifecycle.onbackground",
    "lifecycle.onforeground",
    "lifecycle.oninactive",
    "lifecycle.onsuspended",
    "lifecycle.onunloading",
    "lifecycle2.onstatechanged",
    "actions.onintent",
    "discovery.onnavigateto",
    "presentation.onfocusedchanged"
};

class LifecycleDelegate : public BaseEventDelegate
{
    public:
    LifecycleDelegate(PluginHost::IShell *shell) : BaseEventDelegate(), mShell(shell), mLifecycleManagerState(nullptr), mWindowManager(nullptr), mNotificationHandler(*this), mWindowManagerNotificationHandler(*this)
    {
        if (ConfigUtils::useAppManagers()) {
           Exchange::ILifecycleManagerState *lifecycleManagerState = GetLifecycleManagerStateInterface();
           if (nullptr == lifecycleManagerState)
           {
               LOGERR("LifecycleManagerState interface not available");
           } else {
                lifecycleManagerState->Register(&mNotificationHandler);
           }
           

           Exchange::IRDKWindowManager *windowManager = GetWindowManagerInterface();
           if (nullptr == windowManager)
           {
                LOGERR("WindowManager interface not available");
           }
           else {
                windowManager->Register(&mWindowManagerNotificationHandler);
           }
        }
    }

    ~LifecycleDelegate()
    {
        if (ConfigUtils::useAppManagers()) {
            if (nullptr != mLifecycleManagerState)
            {
                mLifecycleManagerState->Unregister(&mNotificationHandler);
                mLifecycleManagerState->Release();
                mLifecycleManagerState = nullptr;
            }
            
            if (nullptr != mWindowManager)
            {
                mWindowManager->Unregister(&mWindowManagerNotificationHandler);
                mWindowManager->Release();
                mWindowManager = nullptr;
            }
        }
    }

    bool HandleSubscription(Exchange::IAppNotificationHandler::IEmitter *cb, const string &event, const bool listen)
    {
        // Check if event is present in VALID_LIFECYCLE_EVENT make check case insensitive
        if (listen)
        {
            AddNotification(event, cb);
        }
        else
        {
            RemoveNotification(event, cb);
        }
        return true;
    }

    bool HandleEvent(Exchange::IAppNotificationHandler::IEmitter *cb, const string &event, const bool listen, bool &registrationError)
    {
        // Check if event is present in VALID_LIFECYCLE_EVENT make check case insensitive
        if (VALID_LIFECYCLE_EVENT.find(StringUtils::toLower(event)) != VALID_LIFECYCLE_EVENT.end())
        {
            // Handle LifecycleManagerState event
            registrationError = !HandleSubscription(cb, event, listen);
            return true;
        }
        registrationError = true; // event not recognized - signal error to caller
        return false;
    }

    // Get AppId from Instance Id
    Core::hresult Authenticate(const string& appInstanceId, string& appId)
    {
        appId = mAppIdInstanceIdMap.GetAppId(appInstanceId);
        if (appId.empty()) {
            return Core::ERROR_GENERAL;
        } else {
            return Core::ERROR_NONE;
        }
    }

    // Get AppInstanceId from AppId
    Core::hresult GetSessionId(const string& appId, string& appInstanceId)
    {
        appInstanceId = mAppIdInstanceIdMap.GetAppInstanceId(appId);
        if (appInstanceId.empty()) {
            return Core::ERROR_GENERAL;
        } else {
            return Core::ERROR_NONE;
        }
    }

    Core::hresult LifecycleClose(const Exchange::GatewayContext& context , const string& payload /*@opaque */, string& result /*@out @opaque */){
        result = "null";
        if (mLifecycleManagerState != nullptr) {
            JsonObject params;
            if (params.FromString(payload))
            {
                string reason = params.Get("reason").String();
                // reason == userExit maps to USER_EXIT enum
                if (reason == "userExit") {
                    return mLifecycleManagerState->CloseApp(context.appId, Exchange::ILifecycleManagerState::USER_EXIT);
                } else {
                    return mLifecycleManagerState->CloseApp(context.appId, Exchange::ILifecycleManagerState::ERROR);
                }
            } else {
                LOGERR("Payload parsing failure %s", payload.c_str());
                ErrorUtils::CustomBadRequest("Invalid close reason", result);
                return Core::ERROR_GENERAL;
            }
        } else {
            LOGERR("LifecycleManagerState interface not available");
            ErrorUtils::CustomInitialize("LifecycleManager not available", result);
            return Core::ERROR_GENERAL;
        }
        return Core::ERROR_NONE;
    }

    Core::hresult Lifecycle2Close(const Exchange::GatewayContext& context , const string& payload /*@opaque */, string& result /*@out @opaque */){
        result = "null";
        if (mLifecycleManagerState != nullptr) {
            JsonObject params;
            if (params.FromString(payload))
            {
                string reason = params.Get("type").String();
                // reason == userExit maps to USER_EXIT enum
                if (reason == "deactivate") {
                    return mLifecycleManagerState->CloseApp(context.appId, Exchange::ILifecycleManagerState::USER_EXIT);
                } else if (reason == "killReload") {
                    return mLifecycleManagerState->CloseApp(context.appId, Exchange::ILifecycleManagerState::KILL_AND_RUN);
                } else if (reason == "killReactivate") {
                    return mLifecycleManagerState->CloseApp(context.appId, Exchange::ILifecycleManagerState::KILL_AND_ACTIVATE);
                } else {
                    return mLifecycleManagerState->CloseApp(context.appId, Exchange::ILifecycleManagerState::ERROR);
                }
            } else {
                LOGERR("Payload parsing failure %s", payload.c_str());
                ErrorUtils::CustomBadRequest("Invalid close reason", result);
                return Core::ERROR_GENERAL;
            }
            
        } else {
                LOGERR("LifecycleManagerState interface not available");
                ErrorUtils::CustomInitialize("LifecycleManager not available", result);
                return Core::ERROR_GENERAL;
        }
        return Core::ERROR_NONE;
    }

    Core::hresult Lifecycle2State(const Exchange::GatewayContext& context , const string& payload /*@opaque */, string& result /*@out @opaque */){
        // get appInstance Id from context.appId
        string appInstanceId = mAppIdInstanceIdMap.GetAppInstanceId(context.appId);
        // Get current LifecycleState for given appInstanceId
        LifecycleStateInfo stateInfo = mLifecycleStateRegistry.GetLifecycleStateInfo(appInstanceId);

        result = "\""+ LifecycleStateToString(stateInfo.currentState) + "\"";
        return Core::ERROR_NONE;
    }

    Core::hresult LifecycleState(const Exchange::GatewayContext& context , const string& payload /*@opaque */, string& result /*@out @opaque */){
         // get appInstance Id from context.appId
        string appInstanceId = mAppIdInstanceIdMap.GetAppInstanceId(context.appId);
        // Get current LifecycleState for given appInstanceId
        LifecycleStateInfo stateInfo = mLifecycleStateRegistry.GetLifecycleStateInfo(appInstanceId);

        result = "\""+ Lifecycle2StateToLifecycle1String(stateInfo.currentState) + "\"";
        
        return Core::ERROR_NONE;
    }

    Core::hresult LifecycleReady(const Exchange::GatewayContext& context , const string& payload /*@opaque */, string& result /*@out @opaque */){
        result = "null";
        if (mLifecycleManagerState != nullptr) {
            return mLifecycleManagerState->AppReady(context.appId);            
        }
        return Core::ERROR_NONE;
    }

    Core::hresult LifecycleFinished(const Exchange::GatewayContext& context , const string& payload /*@opaque */, string& result /*@out @opaque */){
        result = "null";
        return Core::ERROR_NONE;
    }

    Core::hresult DispatchLastIntent(const Exchange::GatewayContext& context , const string& payload /*@opaque */, string& result /*@out @opaque */){
        DispatchLastKnownIntent(context.appId);
        result = "null";
        return Core::ERROR_NONE;
    }

    Core::hresult SetIntent(const Exchange::GatewayContext& context, const string& payload /*@opaque */, string& result /*@out @opaque */) {
        const string& navigationIntent = payload;
        if (navigationIntent.empty() || navigationIntent == "null") {
            result = "null";
            return Core::ERROR_NONE;
        }
        string appInstanceId = mAppIdInstanceIdMap.GetAppInstanceId(context.appId);
        if (appInstanceId.empty()) {
             // On the LifecycleManagement path there is no separate appInstanceId, so appId is used as appInstanceId (identity mapping).
             // On the AppManagers path, OnAppLifecycleStateChanged(INITIALIZING) may later overwrite this with the real appInstanceId,
             // and any identity-mapped intent is discarded in favor of the INITIALIZING event's navigationIntent.
            LOGINFO("SetIntent: bootstrapping identity mapping for appId=%s", context.appId.c_str());
            mAppIdInstanceIdMap.AddAppInstanceId(context.appId, context.appId);
            appInstanceId = context.appId;
        }
        mNavigationIntentRegistry.AddNavigationIntent(appInstanceId, navigationIntent);
        result = "null";
        return Core::ERROR_NONE;
    }

    Core::hresult GetLastIntent(const Exchange::GatewayContext& context , const string& payload /*@opaque */, string& result /*@out @opaque */){
        string intent;
        uint32_t intentId = 0;
        GetLastKnownIntent(context.appId, intent, intentId);
        result = BuildIntentResult(intentId, intent);
        return Core::ERROR_NONE;
    }

    Core::hresult ActionsStart(const Exchange::GatewayContext& context, const string& payload /*@opaque*/, string& result /*@out @opaque*/)
    {
        if (payload.empty() || "null" == payload) {
            LOGWARN("ActionsStart: intent payload is required");
            ErrorUtils::CustomBadRequest("Intent payload is required", result);
            return Core::ERROR_BAD_REQUEST;
        }
        JsonObject args;
        if (!args.FromString(payload)) {
            LOGWARN("ActionsStart: payload is not valid JSON");
            ErrorUtils::CustomBadRequest("Invalid JSON payload", result);
            return Core::ERROR_BAD_REQUEST;
        }
        if (!args.HasLabel("intent")) {
            LOGWARN("ActionsStart: 'intent' field is required in payload");
            ErrorUtils::CustomBadRequest("'intent' field is required", result);
            return Core::ERROR_BAD_REQUEST;
        }

        // Validate and re-serialize the intent sub-document as an opaque JSON string.
        // Reject JSON null (Variant::type::EMPTY) and empty objects ({}) as invalid.
        const JsonValue& intentVariant = args.Get("intent");
        if (intentVariant.Content() == JsonValue::type::EMPTY) {
            LOGWARN("ActionsStart: 'intent' field must not be null");
            ErrorUtils::CustomBadRequest("'intent' field must not be null or empty", result);
            return Core::ERROR_BAD_REQUEST;
        }
        string intent;
        intentVariant.Object().ToString(intent);
        if (intent.empty() || intent == "{}" || "null" == intent) {
            LOGWARN("ActionsStart: 'intent' field must not be an empty object");
            ErrorUtils::CustomBadRequest("'intent' field must not be null or empty", result);
            return Core::ERROR_BAD_REQUEST;
        }

        // Extract optional handlerAppId
        string handlerAppId;
        if (args.HasLabel("handlerAppId")) {
            handlerAppId = args.Get("handlerAppId").String();
        }

        Exchange::IAppActions* appActions = mShell->QueryInterfaceByCallsign<Exchange::IAppActions>(APP_ACTIONS_CALLSIGN);
        if (nullptr == appActions) {
            LOGWARN("ActionsStart: IAppActions interface not available");
            ErrorUtils::NotAvailable(result);
            return Core::ERROR_UNAVAILABLE;
        }
        result = "null";
        Core::hresult rc = appActions->ActionStart(context.appId, intent, handlerAppId); // context.appId maps to 'initiator'
        appActions->Release();
        appActions = nullptr;
        if (Core::ERROR_NONE != rc) {
            LOGERR("ActionsStart: IAppActions::ActionStart failed with error %u", rc);
            ErrorUtils::CustomInternal("Failed to start app action", result);
        }
        return rc;
    }

    Core::hresult ActionsIntent(const Exchange::GatewayContext& context, const string& payload /*@opaque*/, string& result /*@out @opaque*/)
    {
        string intent;
        uint32_t intentId = 0;
        GetLastKnownIntent(context.appId, intent, intentId);
        result = BuildIntentResult(intentId, intent);
        return Core::ERROR_NONE;
    }

    Core::hresult GetStatsMemoryUsage(const std::string& appId, string& result /* @out */) {
        auto returnDefaults = [&result]() {
            JsonObject obj;
            obj["userMemoryUsed"]  = static_cast<int64_t>(0);
            obj["userMemoryLimit"] = static_cast<int64_t>(0);
            obj["gpuMemoryUsed"]   = static_cast<int64_t>(0);
            obj["gpuMemoryLimit"]  = static_cast<int64_t>(0);
            obj.ToString(result);
        };

        std::string appInstanceId = mAppIdInstanceIdMap.GetAppInstanceId(appId);
        if (appInstanceId.empty()) {
            LOGERR("LifecycleDelegate: No appInstanceId found for appId=%s", appId.c_str());
            returnDefaults();
            return Core::ERROR_NONE;
        }

        Exchange::IRuntimeManager* runtimeManager =
            mShell->QueryInterfaceByCallsign<Exchange::IRuntimeManager>(RUNTIME_MANAGER_CALLSIGN);
        if (!runtimeManager) {
            LOGERR("LifecycleDelegate: Failed to get RuntimeManager COM interface");
            returnDefaults();
            return Core::ERROR_NONE;
        }

        string info;
        const Core::hresult rc = runtimeManager->GetInfo(appInstanceId, info);
        runtimeManager->Release();
        runtimeManager = nullptr;

        if (rc != Core::ERROR_NONE) {
            LOGERR("LifecycleDelegate: RuntimeManager.GetInfo failed rc=%u", rc);
            returnDefaults();
            return Core::ERROR_NONE;
        }

        // Parse the info JSON string returned by RuntimeManager::GetInfo.
        // Structure: { "memory": { "user": { "usage": ..., "limit": ... } },
        //              "gpu":    { "memory": { "usage": ..., "limit": ... } } }

        WPEFramework::Core::JSON::VariantContainer infoObj;
        if (!infoObj.FromString(info)) {
            LOGERR("LifecycleDelegate: GetInfo returned malformed JSON: %s", info.c_str());
            returnDefaults();
            return Core::ERROR_NONE;
        }
        if (!infoObj.HasLabel(_T("memory")) || !infoObj.HasLabel(_T("gpu"))) {
            LOGERR("LifecycleDelegate: GetInfo missing memory or gpu field");
            returnDefaults();
            return Core::ERROR_NONE;
        }

        WPEFramework::Core::JSON::VariantContainer memoryObj = infoObj[_T("memory")].Object();
        WPEFramework::Core::JSON::VariantContainer gpuObj    = infoObj[_T("gpu")].Object();
        if (!memoryObj.HasLabel(_T("user")) || !gpuObj.HasLabel(_T("memory"))) {
            LOGERR("LifecycleDelegate: GetInfo missing memory.user or gpu.memory field");
            returnDefaults();
            return Core::ERROR_NONE;
        }

        WPEFramework::Core::JSON::VariantContainer userObj   = memoryObj[_T("user")].Object();
        WPEFramework::Core::JSON::VariantContainer gpuMemObj = gpuObj[_T("memory")].Object();

        JsonObject obj;
        obj["userMemoryUsed"]  = static_cast<int64_t>(userObj[_T("usage")].Number());
        obj["userMemoryLimit"] = static_cast<int64_t>(userObj[_T("limit")].Number());
        obj["gpuMemoryUsed"]   = static_cast<int64_t>(gpuMemObj[_T("usage")].Number());
        obj["gpuMemoryLimit"]  = static_cast<int64_t>(gpuMemObj[_T("limit")].Number());
        obj.ToString(result);
        return Core::ERROR_NONE;
    }
    
    Core::hresult GetPresentationFocused(const Exchange::GatewayContext& context , const string& payload /*@opaque */, string& result /*@out @opaque */){
        // get appInstance Id from context.appId
        string appInstanceId = mAppIdInstanceIdMap.GetAppInstanceId(context.appId);
        if (true == appInstanceId.empty()) {
            LOGWARN("GetPresentationFocused called for unknown or unmapped appId=%s", context.appId.c_str());
            result = "false";
            return Core::ERROR_NONE;
        }
        result = mFocusedAppRegistry.IsAppInstanceIdFocused(appInstanceId) ? "true" : "false";
        return Core::ERROR_NONE;
    }

    private:
    class LifecycleNotificationHandler : public Exchange::ILifecycleManagerState::INotification
    {
    public:
        LifecycleNotificationHandler(LifecycleDelegate &parent) : mParent(parent) {}
        void OnAppLifecycleStateChanged(const string& appId /* @text appId */,
                                        const string& appInstanceId /* @text appInstanceId */,
                                        const Exchange::ILifecycleManager::LifecycleState oldLifecycleState /* @text oldLifecycleState */,
                                        const Exchange::ILifecycleManager::LifecycleState newLifecycleState /* @text newLifecycleState */,
                                        const string& navigationIntent /* @text navigationIntent */) override
        {
            LOGINFO("OnAppLifecycleStateChanged: appId=%s, appInstanceId=%s, oldState=%d, newState=%d, navigationIntent=%s",
                    appId.c_str(), appInstanceId.c_str(), oldLifecycleState, newLifecycleState, navigationIntent.c_str());

            // Only update the registry when a non-empty, non-null intent is provided.
            // An empty or "null" intent on subsequent transitions (e.g. ACTIVE) must not
            // overwrite a previously stored intent (e.g. secondScreen set on INITIALIZING).
            bool bIntentUpdated = false;
            if (!navigationIntent.empty() && navigationIntent != "null") {
                mParent.mNavigationIntentRegistry.AddNavigationIntent(appInstanceId, navigationIntent);
                bIntentUpdated = true;
            }

            // if new Lifecycle state is INITIALIZING then add to app instance map
            if (Exchange::ILifecycleManager::INITIALIZING == newLifecycleState) {
                mParent.mAppIdInstanceIdMap.AddAppInstanceId(appId, appInstanceId);
                // also add to lifecycle state registry
                mParent.mLifecycleStateRegistry.AddLifecycleState(appInstanceId, oldLifecycleState, newLifecycleState);

                // On the AppManagers path, the navigationIntent from the INITIALIZING event
                // is the authoritative source of truth. Always discard any identity-mapped
                // intent stored by SetIntent/HandleNewSession — never migrate it.
                // The intent stored above (if navigationIntent was non-empty) under
                // appInstanceId is what actions.intent will return.
                if (appId != appInstanceId) {
                    string identityIntent;
                    uint32_t identityIntentId = 0;
                    if (mParent.mNavigationIntentRegistry.GetNavigationIntent(appId, identityIntent, identityIntentId)) {
                        LOGINFO("OnAppLifecycleStateChanged: discarding identity-mapped intent for appId=%s, INITIALIZING intent is authoritative",
                                appId.c_str());
                        mParent.mNavigationIntentRegistry.RemoveNavigationIntent(appId);
                    }
                }
            } 

            // handle lifecycle update
            mParent.HandleLifecycleUpdate(appInstanceId, oldLifecycleState, newLifecycleState, bIntentUpdated);

        }

        BEGIN_INTERFACE_MAP(LifecycleNotificationHandler)
        INTERFACE_ENTRY(Exchange::ILifecycleManagerState::INotification)
        END_INTERFACE_MAP

    private:
        LifecycleDelegate &mParent;
    };

    class WindowManagerNotificationHandler : public Exchange::IRDKWindowManager::INotification
    {
    public:
        WindowManagerNotificationHandler(LifecycleDelegate &parent) : mParent(parent) {}

        void OnFocus(const std::string& appInstanceId){
            mParent.mFocusedAppRegistry.SetFocusedAppInstanceId(appInstanceId);
            mParent.Dispatch("Presentation.onFocusedChanged", mParent.mFocusedAppRegistry.GetFocusedEventData(appInstanceId), mParent.mAppIdInstanceIdMap.GetAppId(appInstanceId));
            mParent.HandleAppFocusForLifecycle1(appInstanceId);
        }

        void OnBlur(const std::string& appInstanceId){
            if (mParent.mFocusedAppRegistry.IsAppInstanceIdFocused(appInstanceId)) {
                mParent.mFocusedAppRegistry.ClearFocusedAppInstanceId();
            }
            mParent.Dispatch("Presentation.onFocusedChanged", mParent.mFocusedAppRegistry.GetFocusedEventData(appInstanceId), mParent.mAppIdInstanceIdMap.GetAppId(appInstanceId));
            mParent.HandleAppBlurForLifecycle1(appInstanceId);
        }

        // Implement notification methods if needed
        BEGIN_INTERFACE_MAP(WindowManagerNotificationHandler)
        INTERFACE_ENTRY(Exchange::IRDKWindowManager::INotification)
        END_INTERFACE_MAP

    private:
        LifecycleDelegate &mParent;
    };

    // create a class to store app Id and app instance id map
    class AppIdInstanceIdMap {
        public:
            void AddAppInstanceId(const string& appId, const string& appInstanceId) {
                std::lock_guard<std::mutex> lock(mapMutex);
                appIdInstanceIdMap[appId] = appInstanceId;
            }

            string GetAppInstanceId(const string& appId) {
                std::lock_guard<std::mutex> lock(mapMutex);
                if (appIdInstanceIdMap.find(appId) != appIdInstanceIdMap.end()) {
                    return appIdInstanceIdMap[appId];
                }
                return "";
            }

            // reverse lookup app instance id to app id
            string GetAppId(const string& appInstanceId) {
                std::lock_guard<std::mutex> lock(mapMutex);
                for (const auto& pair : appIdInstanceIdMap) {
                    if (pair.second == appInstanceId) {
                        return pair.first;
                    }
                }
                return "";
            }

            void RemoveAppInstanceId(const string& appId) {
                std::lock_guard<std::mutex> lock(mapMutex);
                appIdInstanceIdMap.erase(appId);
            }
        private:
            std::map<string, string> appIdInstanceIdMap;
            std::mutex mapMutex;
    };

    // struct to contain previous and current const Exchange::ILifecycleManager::LifecycleState
    struct LifecycleStateInfo {
        Exchange::ILifecycleManager::LifecycleState previousState;
        Exchange::ILifecycleManager::LifecycleState currentState;
    };

    // Create a class to act as Registry to contain map of appInstanceId to LifecycleStateInfo
    class LifecycleStateRegistry {
        public:
            // new method to accept previous state and current state for a given appInstanceId
            void AddLifecycleState(const string& appInstanceId, Exchange::ILifecycleManager::LifecycleState previousState, Exchange::ILifecycleManager::LifecycleState currentState) {
                std::lock_guard<std::mutex> lock(registryMutex);
                lifecycleStateMap[appInstanceId] = {previousState, currentState};
            }

            void UpdateLifecycleState(const string& appInstanceId, Exchange::ILifecycleManager::LifecycleState newState) {
                std::lock_guard<std::mutex> lock(registryMutex);
                LifecycleStateInfo& stateInfo = lifecycleStateMap[appInstanceId];
                stateInfo.previousState = stateInfo.currentState;
                stateInfo.currentState = newState;
            }

            // is current app lifecycle state is ACTIVE
            bool IsAppLifecycleActive(const string& appInstanceId) {
                std::lock_guard<std::mutex> lock(registryMutex);
                if (lifecycleStateMap.find(appInstanceId) != lifecycleStateMap.end()) {
                    return lifecycleStateMap[appInstanceId].currentState == Exchange::ILifecycleManager::ACTIVE;
                }
                return false;
            }

            LifecycleStateInfo GetLifecycleStateInfo(const string& appInstanceId) {
                std::lock_guard<std::mutex> lock(registryMutex);
                if (lifecycleStateMap.find(appInstanceId) != lifecycleStateMap.end()) {
                    return lifecycleStateMap[appInstanceId];
                }
                return {Exchange::ILifecycleManager::UNLOADED, Exchange::ILifecycleManager::UNLOADED};
            }

            void RemoveLifecycleStateInfo(const string& appInstanceId) {
                std::lock_guard<std::mutex> lock(registryMutex);
                lifecycleStateMap.erase(appInstanceId);
            }

            // Get json payload of current and previous state for a given appInstanceId.
            // Optional stateOverride allows callers to force payload state (e.g. background on blur).
            string GetLifecycle1StateJson(const string& appInstanceId, const string& stateOverride = "") {
                std::lock_guard<std::mutex> lock(registryMutex);
                auto it = lifecycleStateMap.find(appInstanceId);
                if (it != lifecycleStateMap.end()) {
                    LifecycleStateInfo& stateInfo = it->second;
                    JsonObject object;
                    object["previous"] = Lifecycle2StateToLifecycle1String(stateInfo.previousState);
                    object["state"] = stateOverride.empty() ? Lifecycle2StateToLifecycle1String(stateInfo.currentState) : stateOverride;
                    string jsonPayload;
                    object.ToString(jsonPayload);
                    return jsonPayload;
                }
                return "{}";
            }

            string GetLifecycle2StateJson(const string& appInstanceId) {
                std::lock_guard<std::mutex> lock(registryMutex);
                if (lifecycleStateMap.find(appInstanceId) != lifecycleStateMap.end()) {
                    LifecycleStateInfo& stateInfo = lifecycleStateMap[appInstanceId];
                    JsonObject object;
                    object["oldState"] = LifecycleStateToString(stateInfo.previousState);
                    object["newState"] = LifecycleStateToString(stateInfo.currentState);
                    string jsonPayload;
                    JsonArray array;
                    array.Add(object);
                    array.ToString(jsonPayload);
                    return jsonPayload;
                }
                return "{}";
            }
            
        private:
            std::map<string, LifecycleStateInfo> lifecycleStateMap;
            std::mutex registryMutex;
    };

    // create a class as registry to store appInstance Id and the intent + monotonic index
    class NavigationIntentRegistry {
        public:
            struct IntentEntry {
                string   intent;
                uint32_t intentId;
            };

            void AddNavigationIntent(const string& appInstanceId, const string& intent) {
                if (intent.empty() || "null" == intent) {
                    return; // ignore empty/null intents; preserve the last non-empty intent and intentId
                }
                std::lock_guard<std::mutex> lock(intentMutex);
                navigationIntentMap[appInstanceId] = { intent, ++mIntentIndex };
            }

            bool GetNavigationIntent(const string& appInstanceId, string& intent, uint32_t& intentId) {
                std::lock_guard<std::mutex> lock(intentMutex);
                auto it = navigationIntentMap.find(appInstanceId);
                if (it != navigationIntentMap.end()) {
                    intent   = it->second.intent;
                    intentId = it->second.intentId;
                    return true;
                }
                return false;
            }

            void RemoveNavigationIntent(const string& appInstanceId) {
                std::lock_guard<std::mutex> lock(intentMutex);
                navigationIntentMap.erase(appInstanceId);
            }
        private:
            std::map<string, IntentEntry> navigationIntentMap;
            std::mutex intentMutex;
            std::atomic<uint32_t> mIntentIndex{0};
    };

    // create a class which stores the last app instance id which has focus
    // focus can be cleared by clearing the app instance id when no apps are in focus
    class FocusedAppRegistry {
        public:
            void SetFocusedAppInstanceId(const string& appInstanceId) {
                std::lock_guard<std::mutex> lock(focusMutex);
                focusedAppInstanceId = appInstanceId;
            }

            // check if given instanceId has focus
            bool IsAppInstanceIdFocused(const string& appInstanceId) {
                std::lock_guard<std::mutex> lock(focusMutex);
                return focusedAppInstanceId == appInstanceId;
            }

            string GetFocusedAppInstanceId() {
                std::lock_guard<std::mutex> lock(focusMutex);
                return focusedAppInstanceId;
            }

            void ClearFocusedAppInstanceId() {
                std::lock_guard<std::mutex> lock(focusMutex);
                focusedAppInstanceId.clear();
            }

            // return a focus json string for a given app instanceId {"value": true or false}
            string GetFocusedEventData(const string& appInstanceId) {
                std::lock_guard<std::mutex> lock(focusMutex);
                bool isFocused = (focusedAppInstanceId == appInstanceId);
                JsonObject object;
                object["value"] = isFocused;
                // jsonPayload is boolean for value
                string jsonPayload;
                object.ToString(jsonPayload);
                return jsonPayload;
            }
        private:
            string focusedAppInstanceId;
            std::mutex focusMutex;
    };


    // create a utility function to convert LifecycleState to string
    static string LifecycleStateToString(Exchange::ILifecycleManager::LifecycleState state) {
        switch (state) {
            case Exchange::ILifecycleManager::UNLOADED:
                return "unloaded";
            case Exchange::ILifecycleManager::LOADING:
                return "loading";
            case Exchange::ILifecycleManager::INITIALIZING:
                return "initializing";
            case Exchange::ILifecycleManager::PAUSED:
                return "paused";
            case Exchange::ILifecycleManager::ACTIVE:
                return "active";
            case Exchange::ILifecycleManager::SUSPENDED:
                return "suspended";
            case Exchange::ILifecycleManager::HIBERNATED:
                return "hibernated";
            case Exchange::ILifecycleManager::TERMINATING:
                return "terminating";
            default:
                return "";
        }
    }

    static string Lifecycle2StateToLifecycle1String(Exchange::ILifecycleManager::LifecycleState state) {
        switch (state) {
            // Lifecycle 1 only has unloading state which can be either unloaded or terminating from Lifecycle 2
            case Exchange::ILifecycleManager::UNLOADED:
            case Exchange::ILifecycleManager::TERMINATING:
                return "unloading";
            // Lifecycle 1 does not have loading state separate from initializing, map both to initializing
            case Exchange::ILifecycleManager::LOADING:
            case Exchange::ILifecycleManager::INITIALIZING:
                return "initializing";
            // Lifecycle 1 direct mapping
            case Exchange::ILifecycleManager::PAUSED:
                return "inactive";
            // Lifecycle 1 direct mapping
            case Exchange::ILifecycleManager::ACTIVE:
                return "foreground";
            // Lifecycle 1 maps both suspended and hibernated to suspended
            case Exchange::ILifecycleManager::HIBERNATED:
            case Exchange::ILifecycleManager::SUSPENDED:
                return "suspended";
            default:
                return "";
        }
    }

    // Create method to support lifecycle 1 updates which accepts the instanceId along with current
    // old lifecycle states
    void HandleLifecycle1Update(const string& appInstanceId,  const Exchange::ILifecycleManager::LifecycleState oldLifecycleState, const Exchange::ILifecycleManager::LifecycleState newLifecycleState)
    {
        switch (newLifecycleState) {
            
            case Exchange::ILifecycleManager::PAUSED:
                Dispatch("Lifecycle.onInactive", mLifecycleStateRegistry.GetLifecycle1StateJson(appInstanceId), mAppIdInstanceIdMap.GetAppId(appInstanceId));
                break;
            case Exchange::ILifecycleManager::SUSPENDED:
            case Exchange::ILifecycleManager::HIBERNATED:
                Dispatch("Lifecycle.onSuspended", mLifecycleStateRegistry.GetLifecycle1StateJson(appInstanceId), mAppIdInstanceIdMap.GetAppId(appInstanceId));
                break;
            case Exchange::ILifecycleManager::UNLOADED:
            case Exchange::ILifecycleManager::TERMINATING:
                Dispatch("Lifecycle.onUnloading", mLifecycleStateRegistry.GetLifecycle1StateJson(appInstanceId), mAppIdInstanceIdMap.GetAppId(appInstanceId));
                break;
            case Exchange::ILifecycleManager::ACTIVE:
                // if app is in focus send foreground
                 if (mFocusedAppRegistry.IsAppInstanceIdFocused(appInstanceId)) {
                    Dispatch("Lifecycle.onForeground", mLifecycleStateRegistry.GetLifecycle1StateJson(appInstanceId), mAppIdInstanceIdMap.GetAppId(appInstanceId));
                 } else {
                          Dispatch("Lifecycle.onBackground", mLifecycleStateRegistry.GetLifecycle1StateJson(appInstanceId, "background"), mAppIdInstanceIdMap.GetAppId(appInstanceId));
                 }
                break;
            default:
                // No action for other states
                break;
        }
    }

    // create a function to handle focus for a given app instance id
    void HandleAppFocusForLifecycle1(const string& appInstanceId) {
        // get if current app lifecycle is active
        if (mLifecycleStateRegistry.IsAppLifecycleActive(appInstanceId)) {
            mFocusedAppRegistry.SetFocusedAppInstanceId(appInstanceId);
            Dispatch("Lifecycle.onForeground", mLifecycleStateRegistry.GetLifecycle1StateJson(appInstanceId), mAppIdInstanceIdMap.GetAppId(appInstanceId));
        }
    }

    // create a function to handle blur for a given app instance id
    void HandleAppBlurForLifecycle1(const string& appInstanceId) {
        // get if current app lifecycle is active
        if (mLifecycleStateRegistry.IsAppLifecycleActive(appInstanceId)) {
            mFocusedAppRegistry.ClearFocusedAppInstanceId();
            Dispatch("Lifecycle.onBackground", mLifecycleStateRegistry.GetLifecycle1StateJson(appInstanceId, "background"), mAppIdInstanceIdMap.GetAppId(appInstanceId));
        }
    }

    Exchange::ILifecycleManagerState* GetLifecycleManagerStateInterface()
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mLifecycleManagerStateLock);
        if (nullptr == mLifecycleManagerState && nullptr != mShell)
        {
            mLifecycleManagerState = mShell->QueryInterfaceByCallsign<Exchange::ILifecycleManagerState>(LIFECYCLE_MANAGER_CALLSIGN);
            if (nullptr == mLifecycleManagerState) {
                LOGERR("Failed to get LifecycleManagerState COM interface");
            } else {
                LOGINFO("LifecycleManagerState COM interface acquired successfully");
            }
        }
        return mLifecycleManagerState;
    }

    Exchange::IRDKWindowManager* GetWindowManagerInterface()
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mWindowManagerLock);
        if (nullptr == mWindowManager && nullptr != mShell)
        {
            mWindowManager = mShell->QueryInterfaceByCallsign<Exchange::IRDKWindowManager>(WINDOW_MANAGER_CALLSIGN);
            if (nullptr == mWindowManager) {
                LOGERR("Failed to get RDKWindowManager COM interface");
            } else {
                LOGINFO("RDKWindowManager COM interface acquired successfully");
            }
        }
        return mWindowManager;
    }

    // Dispatch last known intent for a given appId as Actions.onIntent event
    void DispatchLastKnownIntent(const string& appId)
    {
        string intent;
        uint32_t intentId = 0;
        GetLastKnownIntent(appId, intent, intentId);
        if (!intent.empty() && intent != "null") {
            string payloadStr = BuildIntentResult(intentId, intent);
            Dispatch("Actions.onIntent", payloadStr, appId);
            // Mirror LaunchDelegate behavior for apps still consuming Discovery.onNavigateTo.
            JsonObject intentJson;
            if (intentJson.FromString(intent)) {
                string discoveryPayload;
                intentJson.ToString(discoveryPayload);
                Dispatch("Discovery.onNavigateTo", discoveryPayload, appId);
            }
        }
    }

    // Build a JSON response object {"intentId":<n>,"intent":<value>}.
    // The intent is embedded as a raw JSON value when it looks like a JSON
    // object or array, and as a quoted JSON string otherwise.  Building the
    // string manually avoids Thunder's VariantContainer serializer escaping
    // '/' unnecessarily, which would break plain-string intents such as URIs.
    static string BuildIntentResult(uint32_t intentId, const string& intent)
    {
        string intentJson;
        if (intent.empty() || "null" == intent) {
            // No intent available – represent as an empty JSON object
            intentJson = "{}";
        } else if (intent[0] == '{' || intent[0] == '[') {
            // Intent is already a JSON object/array – embed verbatim
            intentJson = intent;
        } else {
            // Plain string – wrap in quotes with minimal JSON escaping
            // (only '"' and '\' must be escaped; '/' must not be)
            intentJson.reserve(intent.size() + 2);
            intentJson += '"';
            for (char c : intent) {
                if (c == '"')       intentJson += "\\\"";
                else if (c == '\\') intentJson += "\\\\";
                else                intentJson += c;
            }
            intentJson += '"';
        }
        return "{\"intentId\":" + std::to_string(intentId) + ",\"intent\":" + intentJson + "}";
    }

    void GetLastKnownIntent(const string& appId, string& intent, uint32_t& intentId)
    {
        intent.clear();
        intentId = 0;
        string appInstanceId = mAppIdInstanceIdMap.GetAppInstanceId(appId);
        if (!appInstanceId.empty()) {
            mNavigationIntentRegistry.GetNavigationIntent(appInstanceId, intent, intentId);
        } else {
            LOGWARN("AppInstanceId not found for Appid: %s", appId.c_str());
        }
    }

    // Handle Lifecycle update for a given appInstanceId by accepting the previous and current lifecycle state
    void HandleLifecycleUpdate(const string& appInstanceId,
                               const Exchange::ILifecycleManager::LifecycleState oldLifecycleState,
                               const Exchange::ILifecycleManager::LifecycleState newLifecycleState,
                               const bool bIntentUpdated = true)
    {
        // update lifecycle state registry
        mLifecycleStateRegistry.UpdateLifecycleState(appInstanceId, newLifecycleState);

        // get appId from appInstanceId
        string appId = mAppIdInstanceIdMap.GetAppId(appInstanceId);
        if (appId.empty()) {
            LOGWARN("HandleLifecycleUpdate: No appId found for appInstanceId=%s, skipping dispatch", appInstanceId.c_str());
            return;
        }

        Dispatch("Lifecycle2.onStateChanged", mLifecycleStateRegistry.GetLifecycle2StateJson(appInstanceId), appId);

        HandleLifecycle1Update(appInstanceId, oldLifecycleState, newLifecycleState);

        // Background / Context: DispatchLastKnownIntent reads app specific intent from from mNavigationIntentRegistry
        // and emits Actions.onIntent.
        // Dispatch the intent only when this lifecycle update supplied a new navigation intent
        // that was stored in the registry.
        if (bIntentUpdated) {
            DispatchLastKnownIntent(appId);
        }

    }

    
    private:
        PluginHost::IShell *mShell;
        mutable Core::CriticalSection mLifecycleManagerStateLock;
        mutable Core::CriticalSection mWindowManagerLock;
        Exchange::ILifecycleManagerState *mLifecycleManagerState;
        Exchange::IRDKWindowManager *mWindowManager;
        Core::Sink<LifecycleNotificationHandler> mNotificationHandler;
        Core::Sink<WindowManagerNotificationHandler> mWindowManagerNotificationHandler;
        // add all registries
        AppIdInstanceIdMap mAppIdInstanceIdMap;
        LifecycleStateRegistry mLifecycleStateRegistry;
        NavigationIntentRegistry mNavigationIntentRegistry;
        FocusedAppRegistry mFocusedAppRegistry;
};


#endif // __LIFECYCLEDELEGATE_H__
