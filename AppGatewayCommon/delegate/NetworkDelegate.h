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

    // "Connected" means at least one interface is physically linked -- not internet
    // reachability, and not which interface is primary (same definition as device.network below).
    Core::hresult GetNetworkConnected(string &result) {
        result.clear();

        Exchange::INetworkManager *networkManager = GetNetworkManagerInterface();
        if (networkManager == nullptr) {
            LOGERR("NetworkManager interface not available");
            result = "{\"error\":\"NetworkManager not available\"}";
            return Core::ERROR_UNAVAILABLE;
        }

        bool connected = false;
        if (IsAnyInterfaceConnected(networkManager, connected) != Core::ERROR_NONE) {
            LOGERR("Failed to get available interfaces on NetworkManager");
            ErrorUtils::CustomInternal("Failed to get NetworkInfo", result);
            return Core::ERROR_GENERAL;
        }

        result = connected ? "true" : "false";
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    Core::hresult GetInternetConnectionStatus(std::string &result)
    {
        LOGINFO("GetInternetConnectionStatus via NetworkManager");
        result.clear();

        Exchange::INetworkManager *networkManager = GetNetworkManagerInterface();
        if (networkManager == nullptr)
        {
            LOGERR("NetworkManager interface not available");
            result = "{\"error\":\"NetworkManager not available\"}";
            return Core::ERROR_UNAVAILABLE;
        }

        Exchange::INetworkManager::InterfaceDetails iface{};
        bool found = false;
        if (FindConnectedInterface(networkManager, iface, found) != Core::ERROR_NONE)
        {
            LOGERR("GetAvailableInterfaces call failed");
            result = "{\"error\":\"Failed to get available interfaces\"}";
            return Core::ERROR_GENERAL;
        }

        if (!found)
        {
            result = "{}";
            LOGINFO("No connected interface found");
            return Core::ERROR_NONE;
        }

        std::string interfaceType;
        switch (iface.type) {
            case Exchange::INetworkManager::INTERFACE_TYPE_ETHERNET: interfaceType = "ethernet"; break;
            case Exchange::INetworkManager::INTERFACE_TYPE_WIFI:     interfaceType = "wifi";     break;
            default:                                                 interfaceType = "unknown";  break;
        }

        std::ostringstream jsonStream;
        jsonStream << "{\"type\":\"" << interfaceType << "\",\"state\":\"connected\"}";
        result = jsonStream.str();
        LOGINFO("Found connected interface: %s", result.c_str());
        return Core::ERROR_NONE;
    }

private:
    // Finds the first interface GetAvailableInterfaces() reports as connected.
    // `found` is false (not an error) if none are connected or the list is empty.
    Core::hresult FindConnectedInterface(Exchange::INetworkManager *networkManager,
                                         Exchange::INetworkManager::InterfaceDetails &iface, bool &found)
    {
        found = false;

        Exchange::INetworkManager::IInterfaceDetailsIterator *interfaces = nullptr;
        Core::hresult rc = networkManager->GetAvailableInterfaces(interfaces);
        if (rc != Core::ERROR_NONE) {
            return rc;
        }
        if (interfaces == nullptr) {
            return Core::ERROR_NONE;
        }

        while (interfaces->Next(iface)) {
            if (iface.connected) {
                found = true;
                break;
            }
        }
        interfaces->Release();
        return Core::ERROR_NONE;
    }

    Core::hresult IsAnyInterfaceConnected(Exchange::INetworkManager *networkManager, bool &connected)
    {
        Exchange::INetworkManager::InterfaceDetails iface{};
        return FindConnectedInterface(networkManager, iface, connected);
    }

    // Skips re-dispatching the same value, e.g. if the link flaps without changing
    // the overall "any interface connected" result.
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

        // onActiveInterfaceChange isn't overridden -- which interface is primary
        // doesn't affect Network.connected, so it falls back to the no-op default.

        // Recomputes "any interface connected" rather than trusting this one interface's
        // state -- e.g. eth0 going down shouldn't report false while wlan0 is still up.
        void onInterfaceStateChange(const Exchange::INetworkManager::InterfaceState state, const string interface) override
        {
            LOGDBG("onInterfaceStateChange: state=%d, interface=%s", state, interface.c_str());

            if (state != Exchange::INetworkManager::INTERFACE_LINK_UP &&
                state != Exchange::INetworkManager::INTERFACE_LINK_DOWN) {
                return;
            }

            Exchange::INetworkManager *networkManager = mParent.GetNetworkManagerInterface();
            if (networkManager == nullptr) {
                return;
            }

            bool connected = false;
            if (mParent.IsAnyInterfaceConnected(networkManager, connected) != Core::ERROR_NONE) {
                return;
            }

            mParent.DispatchConnectedChanged(connected);
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

