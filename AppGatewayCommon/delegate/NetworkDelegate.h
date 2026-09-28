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

#ifndef __NETWORKDELEGATE_H__
#define __NETWORKDELEGATE_H__

#include "StringUtils.h"
#include "BaseEventDelegate.h"
#include <interfaces/INetworkManager.h>
#include "UtilsLogging.h"
#include <algorithm>
#include <sstream>
#include <set>
#include "ObjectUtils.h"
#include "UtilsFirebolt.h"
#include <mutex>

using namespace WPEFramework;

#define NETWORKMANAGER_CALLSIGN "org.rdk.NetworkManager"

// Valid network events that can be subscribed to
static const std::set<string> VALID_NETWORK_EVENT = {
    "device.onnetworkchanged",
    "network.onconnectedchanged"
};

class NetworkDelegate : public BaseEventDelegate
{
public:
    NetworkDelegate(PluginHost::IShell *shell)
        : BaseEventDelegate(), mNetworkManager(nullptr), mShell(shell), mNotificationHandler(*this)
    {
    }

    ~NetworkDelegate()
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mNetworkManagerLock);
        if (nullptr != mNetworkManager)
        {
            {
                std::lock_guard<std::mutex> lock(mRegistrationMutex);
                if (mNotificationHandler.GetRegistered())
                {
                    mNetworkManager->Unregister(&mNotificationHandler);
                    mNotificationHandler.SetRegistered(false);
                }
            }
            mNetworkManager->Release();
            mNetworkManager = nullptr;
        }

        
    }

    bool HandleSubscription(Exchange::IAppNotificationHandler::IEmitter *cb, const string &event, const bool listen)
    {
        if (listen)
        {
            Exchange::INetworkManager *networkManager = GetNetworkManagerInterface();
            if (networkManager == nullptr)
            {
                LOGERR("NetworkManager interface not available");
                return false;
            }

            AddNotification(event, cb);
            {
                std::lock_guard<std::mutex> lock(mRegistrationMutex);
                if (!mNotificationHandler.GetRegistered())
                {
                    LOGINFO("Registering for NetworkManager notifications");
                    networkManager->Register(&mNotificationHandler);
                    mNotificationHandler.SetRegistered(true);
                }
                else
                {
                    LOGTRACE("Is NetworkManager registered = %s", mNotificationHandler.GetRegistered() ? "true" : "false");
                }
            }
            return true;
        }
        else
        {
            // Not removing the notification subscription for cases where only one event is removed
            RemoveNotification(event, cb);
            return true;
        }
        return false;
    }

    bool HandleEvent(Exchange::IAppNotificationHandler::IEmitter *cb, const string &event, const bool listen, bool &registrationError)
    {
        // Check if event is present in VALID_NETWORK_EVENT make check case insensitive
        if (VALID_NETWORK_EVENT.find(StringUtils::toLower(event)) != VALID_NETWORK_EVENT.end())
        {
            // Handle NetworkManager event
            registrationError = !HandleSubscription(cb, event, listen);
            return true;
        }
        registrationError = true; // event not recognized - signal error to caller
        return false;
    }

    // Common method to ensure mNetworkManager is available for all APIs
    Exchange::INetworkManager *GetNetworkManagerInterface()
    {
        Core::SafeSyncType<Core::CriticalSection> lock(mNetworkManagerLock);
        if (nullptr == mNetworkManager && nullptr != mShell)
        {
            mNetworkManager = mShell->QueryInterfaceByCallsign<Exchange::INetworkManager>(NETWORKMANAGER_CALLSIGN);
            if (nullptr == mNetworkManager) {
                LOGERR("Failed to get NetworkManager COM interface");
            } else {
                LOGINFO("NetworkManager COM interface acquired successfully");
            }
        }
        return mNetworkManager;
    }

    Core::hresult GetNetworkConnected(string &result) {
        result.clear();

        Exchange::INetworkManager *networkManager = GetNetworkManagerInterface();
        if (networkManager == nullptr) {
            LOGERR("NetworkManager interface not available");
            result = "{\"error\":\"NetworkManager not available\"}";
            return Core::ERROR_UNAVAILABLE;
        }

        string primaryInterface;
        Core::hresult rc = networkManager->GetPrimaryInterface(primaryInterface);
        if (rc != Core::ERROR_NONE) {
            LOGERR("Failed to get primary interface on NetworkManager, error: %u", rc);
            ErrorUtils::CustomInternal("Failed to get NetworkInfo", result);
            return Core::ERROR_GENERAL;
        }

        if (primaryInterface.empty()) {
            result = "false";
            return Core::ERROR_NONE;
        }

        // A non-empty primary-interface name isn't enough on its own -- it can persist
        // for a moment after that interface's own link drops. Check its actual link
        // state instead of trusting the name's mere presence.
        bool connected = false;
        if (GetInterfaceConnected(primaryInterface, connected) != Core::ERROR_NONE) {
            LOGERR("Primary interface \"%s\" not found in GetAvailableInterfaces()", primaryInterface.c_str());
        }

        result = connected ? "true" : "false";
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    Core::hresult GetInternetConnectionStatus(std::string &result)
    {
        /**
         * Retrieve the first connected interface from GetAvailableInterfaces
         * Transform: Map connected interfaces and return type in lowercase with state
         * Transform logic: .result.interfaces| .[] | select(."connected"==true) |
         *                  {type: .interface, state: map_connected(.connected)} |
         *                  .type |= ascii_downcase | [., inputs][0]
         */
        LOGINFO("GetInternetConnectionStatus via NetworkManager");
        result.clear();

        // Get NetworkManager interface
        Exchange::INetworkManager *networkManager = GetNetworkManagerInterface();
        if (networkManager == nullptr)
        {
            LOGERR("NetworkManager interface not available");
            result = "{\"error\":\"NetworkManager not available\"}";
            return Core::ERROR_UNAVAILABLE;
        }

        // Get available interfaces
        Exchange::INetworkManager::IInterfaceDetailsIterator *interfaces = nullptr;
        uint32_t rc = networkManager->GetAvailableInterfaces(interfaces);

        if (rc != Core::ERROR_NONE)
        {
            LOGERR("GetAvailableInterfaces call failed with error: %u", rc);
            result = "{\"error\":\"Failed to get available interfaces\"}";
            return Core::ERROR_GENERAL;
        }

        if (interfaces == nullptr)
        {
            LOGERR("GetAvailableInterfaces returned null iterator");
            result = "{}";
            return Core::ERROR_NONE;
        }

        // Iterate through interfaces and find the first connected one
        Exchange::INetworkManager::InterfaceDetails iface{};
        bool foundConnected = false;

        while (interfaces->Next(iface))
        {
            if (iface.connected)
            {
                // Get interface type string - map enum to string manually
                std::string interfaceType;
                switch (iface.type) {
                    case Exchange::INetworkManager::INTERFACE_TYPE_ETHERNET:
                        interfaceType = "ethernet";
                        break;
                    case Exchange::INetworkManager::INTERFACE_TYPE_WIFI:
                        interfaceType = "wifi";
                        break;
                    default:
                        interfaceType = "unknown";
                        break;
                }

                // Build the result JSON: {"type": "<type>", "state": "connected"}
                std::ostringstream jsonStream;
                jsonStream << "{\"type\":\"" << interfaceType
                           << "\",\"state\":\"connected\"}";
                result = jsonStream.str();

                LOGINFO("Found connected interface: %s", result.c_str());
                foundConnected = true;
                break;
            }
        }

        // Release the iterator
        interfaces->Release();

        if (!foundConnected)
        {
            // No connected interface found
            result = "{}";
            LOGINFO("No connected interface found");
        }

        return Core::ERROR_NONE;
    }

private:
    // Looks up `interfaceName` (a device name like "eth0"/"wlan0", the same
    // representation GetPrimaryInterface() and the notification callbacks use) in
    // GetAvailableInterfaces() and reports its own link state. This is the single
    // source of truth for "is this specific interface actually connected" — a
    // primary-interface name alone doesn't tell us that (it can persist briefly
    // after that interface's own link drops, or lag briefly after it comes up).
    Core::hresult GetInterfaceConnected(const string &interfaceName, bool &connected)
    {
        connected = false;

        Exchange::INetworkManager *networkManager = GetNetworkManagerInterface();
        if (networkManager == nullptr) {
            return Core::ERROR_UNAVAILABLE;
        }

        Exchange::INetworkManager::IInterfaceDetailsIterator *interfaces = nullptr;
        Core::hresult rc = networkManager->GetAvailableInterfaces(interfaces);
        if (rc != Core::ERROR_NONE) {
            return rc;
        }
        if (interfaces == nullptr) {
            return Core::ERROR_GENERAL;
        }

        bool found = false;
        Exchange::INetworkManager::InterfaceDetails iface{};
        while (interfaces->Next(iface)) {
            if (StringUtils::toLower(iface.name) == StringUtils::toLower(interfaceName)) {
                connected = iface.connected;
                found = true;
                break;
            }
        }
        interfaces->Release();

        return found ? Core::ERROR_NONE : Core::ERROR_GENERAL;
    }

    // Both onActiveInterfaceChange and onInterfaceStateChange can react to the same
    // real transition (a reconnect changes both the primary interface's identity and
    // its link state), so route dispatch through here to avoid firing the event twice
    // with the same value.
    void DispatchConnectedChanged(bool connected)
    {
        {
            std::lock_guard<std::mutex> lock(mLastConnectedMutex);
            if (mLastConnectedKnown && mLastConnected == connected) {
                return;
            }
            mLastConnectedKnown = true;
            mLastConnected = connected;
        }
        Dispatch("Network.onConnectedChanged", ObjectUtils::CreateBooleanJsonString("value", connected));
    }

    class NetworkNotificationHandler : public Exchange::INetworkManager::INotification
    {
    public:
        NetworkNotificationHandler(NetworkDelegate &parent) : mParent(parent), registered(false) {}
        ~NetworkNotificationHandler() {}

        void onActiveInterfaceChange(const string prevActiveInterface, const string currentActiveInterface) override
        {
            LOGDBG("onActiveInterfaceChange: prev=%s, current=%s", prevActiveInterface.c_str(), currentActiveInterface.c_str());
            bool connected = false;
            if (!currentActiveInterface.empty()) {
                mParent.GetInterfaceConnected(currentActiveInterface, connected);
            }
            mParent.DispatchConnectedChanged(connected);
        }

        // NetworkManager's link-carrier notification -- fires independent of which
        // interface is primary, so Network.connected only reacts when the primary
        // interface's own link is what changed (a secondary interface flapping
        // shouldn't move it). This is what makes link-only flaps observable: previously
        // only onActiveInterfaceChange (primary-interface identity change) was wired
        // up, which never fires when the primary interface stays the same and only its
        // link drops.
        void onInterfaceStateChange(const Exchange::INetworkManager::InterfaceState state, const string interface) override
        {
            LOGDBG("onInterfaceStateChange: state=%d, interface=%s", state, interface.c_str());

            if (state != Exchange::INetworkManager::INTERFACE_LINK_UP &&
                state != Exchange::INetworkManager::INTERFACE_LINK_DOWN) {
                return;
            }

            string primaryInterface;
            Exchange::INetworkManager *networkManager = mParent.GetNetworkManagerInterface();
            if (networkManager == nullptr || networkManager->GetPrimaryInterface(primaryInterface) != Core::ERROR_NONE) {
                return;
            }
            if (StringUtils::toLower(interface) != StringUtils::toLower(primaryInterface)) {
                return;
            }

            mParent.DispatchConnectedChanged(state == Exchange::INetworkManager::INTERFACE_LINK_UP);
        }

        void onInternetStatusChange(const Exchange::INetworkManager::InternetStatus prevState, const Exchange::INetworkManager::InternetStatus currState, const string interface)
        {
            LOGINFO("onInternetStatusChange: prevState=%d, currState=%d, interface=%s", prevState, currState, interface.c_str());

            // Map internet status to readable strings
            auto statusToString = [](Exchange::INetworkManager::InternetStatus status) -> string {
                switch (status) {
                    case Exchange::INetworkManager::INTERNET_FULLY_CONNECTED: return "connected";
                    case Exchange::INetworkManager::INTERNET_CAPTIVE_PORTAL: return "captive_portal";
                    case Exchange::INetworkManager::INTERNET_LIMITED: return "limited";
                    case Exchange::INetworkManager::INTERNET_NOT_AVAILABLE: return "not_available";
                    default: return "unknown";
                }
            };

            // Dispatch network change event for internet status changes
            std::ostringstream jsonStream;
            jsonStream << "{\"network\":{\"state\":\"" << statusToString(currState) 
                      << "\",\"prevState\":\"" << statusToString(prevState) << "\"}}";
            mParent.Dispatch("device.onNetworkChanged", jsonStream.str());
        }
        
        // Registration management methods
        void SetRegistered(bool state)
        {
            std::lock_guard<std::mutex> lock(registerMutex);
            registered = state;
        }

        bool GetRegistered()
        {
            std::lock_guard<std::mutex> lock(registerMutex);
            return registered;
        }

        BEGIN_INTERFACE_MAP(NotificationHandler)
        INTERFACE_ENTRY(Exchange::INetworkManager::INotification)
        END_INTERFACE_MAP

    private:
        NetworkDelegate &mParent;
        bool registered;
        std::mutex registerMutex;
    };
    mutable Core::CriticalSection mNetworkManagerLock;
    Exchange::INetworkManager *mNetworkManager;
    PluginHost::IShell *mShell;
    Core::Sink<NetworkNotificationHandler> mNotificationHandler;
    mutable std::mutex mRegistrationMutex;
    bool mLastConnectedKnown = false;
    bool mLastConnected = false;
    std::mutex mLastConnectedMutex;
};

#endif // __NETWORKDELEGATE_H__

