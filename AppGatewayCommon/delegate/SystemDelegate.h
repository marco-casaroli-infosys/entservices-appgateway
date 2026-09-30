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

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include <utility>

#include <plugins/plugins.h>
#include <core/JSON.h>
#include <interfaces/IDisplayInfo.h>
#include <interfaces/IDeviceInfo.h>
#include <interfaces/IPowerManager.h>
#include "UtilsLogging.h"
#include "UtilsJsonrpcDirectLink.h"
#include "UtilsController.h"
#include "BaseEventDelegate.h"
#include <algorithm>
#include "ContextUtils.h"
#include <mutex>
#include <interfaces/ISystemServices.h>


using namespace WPEFramework;

#ifndef CALLSIGN_CALLER_APPGATEWAY
#define CALLSIGN_CALLER_APPGATEWAY "org.rdk.AppGatewayCommon.SystemDelegate"
#endif

// Define a callsign constant to match the AUTHSERVICE_CALLSIGN-style pattern.
#ifndef SYSTEM_CALLSIGN
#define SYSTEM_CALLSIGN "org.rdk.System"
#endif

#ifndef DISPLAYSETTINGS_CALLSIGN
#define DISPLAYSETTINGS_CALLSIGN "org.rdk.DisplaySettings"
#endif

#ifndef HDCPPROFILE_CALLSIGN
#define HDCPPROFILE_CALLSIGN "org.rdk.HdcpProfile"
#endif

#ifndef DEVICEINFO_CALLSIGN
#define DEVICEINFO_CALLSIGN "DeviceInfo"
#endif

#ifndef POWERMANAGER_CALLSIGN
#define POWERMANAGER_CALLSIGN "org.rdk.PowerManager"
#endif

#ifndef DISPLAYINFO_CALLSIGN
#define DISPLAYINFO_CALLSIGN "DisplayInfo"
#endif
// Timeout (ms) for proactive Thunder event subscriptions during construction.
// Override at compile time (e.g. -DSYSTEM_DELEGATE_SUBSCRIBE_TIMEOUT_MS=100)
// to reduce startup latency in environments where Thunder is unavailable.
#ifndef SYSTEM_DELEGATE_SUBSCRIBE_TIMEOUT_MS
#define SYSTEM_DELEGATE_SUBSCRIBE_TIMEOUT_MS 2000
#endif

class SystemDelegate: public BaseEventDelegate
{
public:

    // Event names exposed by this delegate (consumer subscriptions may vary in case)
    static constexpr const char* EVENT_ON_VIDEO_RES_CHANGED   = "Device.onVideoResolutionChanged";
    static constexpr const char* EVENT_ON_SCREEN_RES_CHANGED  = "Device.onScreenResolutionChanged";
    static constexpr const char* EVENT_ON_HDR_CHANGED         = "Device.onHdrChanged";
    static constexpr const char* EVENT_ON_HDCP_CHANGED        = "Device.onHdcpChanged";
    static constexpr const char* EVENT_ON_AUDIO_CHANGED       = "Device.onAudioChanged";
    static constexpr const char* EVENT_ON_NAME_CHANGED        = "Device.onNameChanged";
    static constexpr const char* EVENT_ON_DEVICE_NAME_CHANGED = "Device.onDeviceNameChanged";
    static constexpr const char* EVENT_ON_TIMEZONE_CHANGED    = "Localization.onTimeZoneChanged";
    static constexpr const char* EVENT_ON_COUNTRY_CHANGED     = "Localization.onCountryChanged";

private:
    // Small helper job that runs an arbitrary std::function on a worker-pool thread.
    // Used to move re-query/emit work OFF the Thunder notification-delivery thread,
    // so we never make a blocking COM/JSON-RPC call back into the same plugin that
    // is currently dispatching the event to us (this was the root cause of the
    // 20+ second timeouts described in RDKEMW-24422).
    //
    // NOTE: intentionally NOT using Utils::Job here — that class's base type
    // (Core::IDispatchType<void> vs Core::IDispatch) depends on whether
    // USE_THUNDER_R4 is defined for this translation unit, which caused a
    // "template argument 1" compile failure in the Yocto build because
    // AppGatewayCommon does not define USE_THUNDER_R4. WorkerPoolTask always
    // derives from Core::IDispatch, so it is unaffected by that macro.
    class EXTERNAL WorkerPoolTask : public Core::IDispatch
    {
    public:
        explicit WorkerPoolTask(std::function<void()> work)
            : _work(std::move(work))
        {
        }
        WorkerPoolTask() = delete;
        WorkerPoolTask(const WorkerPoolTask&) = delete;
        WorkerPoolTask& operator=(const WorkerPoolTask&) = delete;
        ~WorkerPoolTask() override = default;

        void Dispatch() override
        {
            _work();
        }

    private:
        std::function<void()> _work;
    };

    // Convenience helper: submit work to the Thunder worker pool.
    static void PostToWorkerPool(std::function<void()> work)
    {
        Core::IWorkerPool::Instance().Submit(
            Core::ProxyType<Core::IDispatch>(Core::ProxyType<WorkerPoolTask>::Create(std::move(work))));
    }

    class SystemServicesNotification : public Exchange::ISystemServices::INotification
    {
    private:
        SystemServicesNotification(const SystemServicesNotification&) = delete;
        SystemServicesNotification& operator=(const SystemServicesNotification&) = delete;

    public:
        explicit SystemServicesNotification(SystemDelegate& parent)
            : _parent(parent)
        {
        }
        ~SystemServicesNotification() override = default;

    public:
        void OnFriendlyNameChanged(const string& friendlyName) override
        {
            LOGINFO("[AppGatewayCommon|OnFriendlyNameChanged] friendlyName=%s", friendlyName.c_str());
            WPEFramework::Core::JSON::VariantContainer params;
            params[_T("friendlyName")] = friendlyName;
            _parent.OnSystemFriendlyNameChanged(params);
        }

        void OnTimeZoneDSTChanged(const string& oldTimeZone, const string& newTimeZone, const string& oldAccuracy, const string& newAccuracy) override
        {
            LOGINFO("[AppGatewayCommon|OnTimeZoneDSTChanged] newTimeZone=%s", newTimeZone.c_str());
            WPEFramework::Core::JSON::VariantContainer params;
            params[_T("oldTimeZone")] = oldTimeZone;
            params[_T("newTimeZone")] = newTimeZone;
            params[_T("oldAccuracy")] = oldAccuracy;
            params[_T("newAccuracy")] = newAccuracy;
            _parent.OnSystemTimezoneChanged(params);
        }

        void OnTerritoryChanged(const string& oldTerritory, const string& newTerritory, const string& oldRegion, const string& newRegion) override
        {
            LOGINFO("[AppGatewayCommon|OnTerritoryChanged] newTerritory=%s", newTerritory.c_str());
            WPEFramework::Core::JSON::VariantContainer params;
            params[_T("oldTerritory")] = oldTerritory;
            params[_T("newTerritory")] = newTerritory;
            params[_T("oldRegion")] = oldRegion;
            params[_T("newRegion")] = newRegion;
            _parent.OnSystemTerritoryChanged(params);
        }

        BEGIN_INTERFACE_MAP(SystemServicesNotification)
        INTERFACE_ENTRY(Exchange::ISystemServices::INotification)
        END_INTERFACE_MAP
    private:
        SystemDelegate& _parent;
    };

public:
    SystemDelegate(PluginHost::IShell *shell)
        : BaseEventDelegate()
        , _shell(shell)
        , _subscriptions()
        , _displayRpc(nullptr)
        , _hdcpRpc(nullptr)
        , _systemServicesPlugin(nullptr)
        , _systemServicesNotification(*this)
        , _registeredSystemEventHandlers(false)
        , _displaySubscribed(false)
        , _displayAudioSubscribed(false)
        , _hdcpSubscribed(false)
    {
            SetupSystemSub();
            LOGINFO("SystemDelegate initialized");
    }

    ~SystemDelegate()
    {
        // Cleanup subscriptions
        try {
            if (_displayRpc) {
                if (_displaySubscribed) {
                    _displayRpc->Unsubscribe(2000, _T("resolutionChanged"));
                }
                if (_displayAudioSubscribed) {
                    _displayRpc->Unsubscribe(2000, _T("audioFormatChanged"));
                }
            }
            if (_hdcpRpc && _hdcpSubscribed) {
                _hdcpRpc->Unsubscribe(2000, _T("onDisplayConnectionChanged"));
            }
            if (_systemServicesPlugin != nullptr) {
                if (_registeredSystemEventHandlers) {
                    _systemServicesPlugin->Unregister(&_systemServicesNotification);
                    _registeredSystemEventHandlers = false;
                }
                _systemServicesPlugin->Release();
                _systemServicesPlugin = nullptr;
            }
        } catch (...) {
            // Safe-guard against destructor exceptions
        }
        _displayRpc.reset();
        _hdcpRpc.reset();
        _shell = nullptr;
    }

    // PUBLIC_INTERFACE
    Core::hresult GetDeviceMake(std::string &make)
    {
        /** Retrieve the device make using org.rdk.System.getDeviceInfo */
        LOGINFO("GetDeviceMake AppGatewayCommon Delegate");
        make.clear();
        auto sysServices = AcquireSystemServices();
        if (!sysServices)
        {
            make = "unknown";
            return Core::ERROR_UNAVAILABLE;
        }

        Exchange::ISystemServices::DeviceInfo deviceInfo;
        // The first GetDeviceInfo parameter is an optional selector/context.
        // Passing nullptr here is the expected way to query the default/current
        // device rather than a specific target.
        const uint32_t rc = sysServices->GetDeviceInfo(nullptr, deviceInfo);
        if (rc == Core::ERROR_NONE && deviceInfo.success && !deviceInfo.make.empty())
        {
            make = deviceInfo.make;
        }
        else
        {
            // Per transform: return_or_else(.result.make, "unknown")
            make = "unknown";
        }
        // Wrap in quotes to make it a valid JSON string
        make = "\"" + make + "\"";
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    Core::hresult GetDeviceName(std::string &name)
    {
        /** Retrieve the friendly name using org.rdk.System.getFriendlyName */
        name.clear();
        auto sysServices = AcquireSystemServices();
        if (!sysServices)
        {
            name = "Living Room";
            return Core::ERROR_UNAVAILABLE;
        }

        string friendlyName;
        bool success = false;
        const uint32_t rc = sysServices->GetFriendlyName(friendlyName, success);
        if (rc == Core::ERROR_NONE && success && !friendlyName.empty())
        {
            name = std::move(friendlyName);
        }
        else
        {
            name = "Living Room";
        }
        // Wrap in quotes to make it a valid JSON string
        name = "\"" + name + "\"";
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    Core::hresult SetDeviceName(const std::string &name)
    {
        /** Set the friendly name using org.rdk.System.setFriendlyName */
        auto sysServices = AcquireSystemServices();
        if (!sysServices)
        {
            return Core::ERROR_UNAVAILABLE;
        }

        Exchange::ISystemServices::SystemResult result;
        const uint32_t rc = sysServices->SetFriendlyName(name, result);
        if (rc == Core::ERROR_NONE && result.success)
        {
            return Core::ERROR_NONE;
        }
        LOGERR("SystemDelegate: couldn't set name");
        return Core::ERROR_GENERAL;
    }

    // PUBLIC_INTERFACE
    Core::hresult GetDeviceSku(std::string &skuOut)
    {
        /** Retrieve the device SKU from org.rdk.System.getSystemVersions.stbVersion */
        skuOut.clear();
        auto sysServices = AcquireSystemServices();
        if (!sysServices)
        {
            return Core::ERROR_UNAVAILABLE;
        }

        Exchange::ISystemServices::SystemVersionsInfo systemVersionsInfo;
        const uint32_t rc = sysServices->GetSystemVersions(systemVersionsInfo);
        if (rc != Core::ERROR_NONE || !systemVersionsInfo.success)
        {
            LOGERR("SystemDelegate: GetSystemVersions failed rc=%u success=%d", rc, systemVersionsInfo.success);
            return Core::ERROR_UNAVAILABLE;
        }
        if (systemVersionsInfo.stbVersion.empty())
        {
            LOGERR("SystemDelegate: GetSystemVersions returned empty stbVersion");
            return Core::ERROR_UNAVAILABLE;
        }
        // Per transform: split("_")[0]
        auto pos = systemVersionsInfo.stbVersion.find('_');
        skuOut = (pos == std::string::npos) ? systemVersionsInfo.stbVersion : systemVersionsInfo.stbVersion.substr(0, pos);
        if (skuOut.empty())
        {
            LOGERR("SystemDelegate: Failed to get SKU");
            return Core::ERROR_UNAVAILABLE;
        }
        // Wrap in quotes to make it a valid JSON string
        skuOut = "\"" + skuOut + "\"";
        return Core::ERROR_NONE;
    }

    Core::hresult GetFirmwareVersion(std::string &version)
    {
        mAdminLock.Lock();
        version = mVersionResponse;
        mAdminLock.Unlock();
        
        if (!version.empty()) {
            return Core::ERROR_NONE;
        }

        auto sysServices = AcquireSystemServices();
        if (!sysServices)
        {
            return Core::ERROR_UNAVAILABLE;
        }

        Exchange::ISystemServices::SystemVersionsInfo systemVersionsInfo;
        const uint32_t rc = sysServices->GetSystemVersions(systemVersionsInfo);
        if (rc != Core::ERROR_NONE || !systemVersionsInfo.success)
        {
            LOGERR("SystemDelegate: GetSystemVersions failed rc=%u success=%d", rc, systemVersionsInfo.success);
            return Core::ERROR_UNAVAILABLE;
        }
        if (systemVersionsInfo.receiverVersion.empty()) {
            LOGERR("SystemDelegate: GetSystemVersions returned empty receiverVersion");
            return Core::ERROR_UNAVAILABLE;
        }
        std::string receiverVersion = systemVersionsInfo.receiverVersion;
        std::string stbVersion = systemVersionsInfo.stbVersion;
        if (stbVersion.empty())
        {
            LOGERR("SystemDelegate: Failed to get STB Version");
            return Core::ERROR_UNAVAILABLE;
        }

        // receiver version is typically in 99.99.15.07 format need to set extract the first three parts only for major.minor.patch
        // if receiverversion is not in number format return error
        uint32_t major;
        uint32_t minor;
        uint32_t patch;

        if (sscanf(receiverVersion.c_str(), "%u.%u.%u", &major, &minor, &patch) != 3)
        {
            LOGERR("SystemDelegate: receiverVersion is not in number format");
            return Core::ERROR_UNAVAILABLE;
        }

        JsonObject versionObj;
        JsonObject api;
        api["major"] = 1;
        api["minor"] = 7;
        api["patch"] = 0;
        api["readable"] = "Firebolt API v1.7.0";

        JsonObject firmwareInfo;
        firmwareInfo["major"] = major;
        firmwareInfo["minor"] = minor;
        firmwareInfo["patch"] = patch;
        firmwareInfo["readable"] = stbVersion;
        // Build this json data structure {"api":{"major":1,"minor":7,"patch":0,"readable":"Firebolt API v1.7.0"},"firmware":{"major":99,"minor":99,"patch":15,"readable":"SKXI11ADS_MIDDLEWARE_DEV_develop_20251101123542_TEST_CD"},"os":{"major":99,"minor":99,"patch":15,"readable":"SKXI11ADS_MIDDLEWARE_DEV_develop_20251101123542_TEST_CD"},"debug":"4.0.0"}
        versionObj["api"] = api;
        versionObj["firmware"] = firmwareInfo;
        versionObj["os"] = firmwareInfo;
        versionObj["debug"] = "4.0.0";

        mAdminLock.Lock();
        versionObj.ToString(mVersionResponse);
        version = mVersionResponse;
        mAdminLock.Unlock();

        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    Core::hresult GetCountryCode(std::string &code)
    {
        /** Retrieve Firebolt country code derived from org.rdk.System.getTerritory */
        code.clear();
        auto sysServices = AcquireSystemServices();
        if (!sysServices)
        {
            code = "US";
            return Core::ERROR_UNAVAILABLE;
        }

        string territory;
        string region;
        bool success = false;
        const uint32_t rc = sysServices->GetTerritory(territory, region, success);
        if (rc == Core::ERROR_NONE && success)
        {
            code = TerritoryThunderToFirebolt(territory, "");
        }
        // Wrap in quotes to make it a valid JSON string
        code = "\"" + code + "\"";
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    Core::hresult SetCountryCode(const std::string &code)
    {
        /** Set territory using org.rdk.System.setTerritory mapped from Firebolt country code */
        auto sysServices = AcquireSystemServices();
        if (!sysServices)
        {
            return Core::ERROR_UNAVAILABLE;
        }

        const std::string territory = TerritoryFireboltToThunder(code, "USA");
        const std::string region = "";  // Empty region as it's not used in current implementation
        Exchange::ISystemServices::SystemError error;
        bool success = false;
        const uint32_t rc = sysServices->SetTerritory(territory, region, error, success);
        if (rc == Core::ERROR_NONE && success)
        {
            return Core::ERROR_NONE;
        }
        LOGERR("SystemDelegate: couldn't set countrycode");
        return Core::ERROR_GENERAL;
    }

    // PUBLIC_INTERFACE
    Core::hresult GetTimeZone(std::string &tz)
    {
        /** Retrieve timezone using org.rdk.System.getTimeZoneDST */
        tz.clear();
        auto sysServices = AcquireSystemServices();
        if (!sysServices)
        {
            return Core::ERROR_UNAVAILABLE;
        }

        string timeZone;
        string accuracy;
        bool success = false;
        const uint32_t rc = sysServices->GetTimeZoneDST(timeZone, accuracy, success);
        if (rc == Core::ERROR_NONE && success)
        {
            tz = std::move(timeZone);
            // Wrap in quotes to make it a valid JSON string
            tz = "\"" + tz + "\"";
            return Core::ERROR_NONE;
        }
        LOGERR("SystemDelegate: couldn't get timezone");
        return Core::ERROR_UNAVAILABLE;
    }

    // PUBLIC_INTERFACE
    Core::hresult SetTimeZone(const std::string &tz)
    {
        /** Set timezone using org.rdk.System.setTimeZoneDST */
        auto sysServices = AcquireSystemServices();
        if (!sysServices)
        {
            return Core::ERROR_UNAVAILABLE;
        }

        const std::string accuracy = "";
        uint32_t serviceStatus = 0;
        string errorMessage;
        bool success = false;
        const uint32_t rc = sysServices->SetTimeZoneDST(tz, accuracy, serviceStatus, errorMessage, success);
        if (rc == Core::ERROR_NONE && success)
        {
            return Core::ERROR_NONE;
        }
        LOGERR("SystemDelegate: couldn't set timezone, error: %s", errorMessage.c_str());
        return Core::ERROR_GENERAL;
    }

    // PUBLIC_INTERFACE
    Core::hresult GetSecondScreenFriendlyName(std::string &name)
    {
        /** Alias to GetDeviceName using org.rdk.System.getFriendlyName */
        return GetDeviceName(name);
    }

    // PUBLIC_INTERFACE
    Core::hresult GetScreenResolution(std::string &jsonArray)
    {
        /**
         * Get [w, h] screen resolution using DisplaySettings.getCurrentResolution.
         * Returns "[1920,1080]" as fallback when unavailable.
         */
        LOGDBG("[AppGatewayCommon|GetScreenResolution] Invoked");
        jsonArray = "[1920,1080]";
        auto link = AcquireLink(DISPLAYSETTINGS_CALLSIGN);
        if (!link) {
            LOGERR("[AppGatewayCommon|GetScreenResolution] DisplaySettings link unavailable, returning default %s", jsonArray.c_str());
            return Core::ERROR_UNAVAILABLE;
        }

        WPEFramework::Core::JSON::VariantContainer params;
        WPEFramework::Core::JSON::VariantContainer response;
        const uint32_t rc = link->Invoke<decltype(params), decltype(response)>("getCurrentResolution", params, response);
        if (rc != Core::ERROR_NONE) {
            LOGERR("[AppGatewayCommon|GetScreenResolution] getCurrentResolution failed rc=%u, returning default %s", rc, jsonArray.c_str());
            return Core::ERROR_GENERAL;
        }

        int w = 1920, h = 1080;

        // Try top-level first
        if (response.HasLabel(_T("w")) && response.HasLabel(_T("h"))) {
            auto wv = response.Get(_T("w"));
            auto hv = response.Get(_T("h"));
            if (wv.Content() == WPEFramework::Core::JSON::Variant::type::NUMBER &&
                hv.Content() == WPEFramework::Core::JSON::Variant::type::NUMBER) {
                w = static_cast<int>(wv.Number());
                h = static_cast<int>(hv.Number());
            }
        } else if (response.HasLabel(_T("result"))) {
            // Try nested "result"
            auto r = response.Get(_T("result"));
            if (r.Content() == WPEFramework::Core::JSON::Variant::type::OBJECT) {
                auto wnv = r.Object().Get(_T("w"));
                auto hnv = r.Object().Get(_T("h"));
                auto wdv = r.Object().Get(_T("width"));
                auto hdv = r.Object().Get(_T("height"));

                if (wnv.Content() == WPEFramework::Core::JSON::Variant::type::NUMBER &&
                    hnv.Content() == WPEFramework::Core::JSON::Variant::type::NUMBER) {
                    w = static_cast<int>(wnv.Number());
                    h = static_cast<int>(hnv.Number());
                } else if (wdv.Content() == WPEFramework::Core::JSON::Variant::type::NUMBER &&
                           hdv.Content() == WPEFramework::Core::JSON::Variant::type::NUMBER) {
                    w = static_cast<int>(wdv.Number());
                    h = static_cast<int>(hdv.Number());
                }
            }
        }

        jsonArray = "[" + std::to_string(w) + "," + std::to_string(h) + "]";
        LOGDBG("[AppGatewayCommon|GetScreenResolution] Computed screenResolution: w=%d h=%d -> %s", w, h, jsonArray.c_str());
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    Core::hresult GetVideoResolution(std::string &jsonArray)
    {
        /**
         * Get [w, h] video resolution. Prefer DisplaySettings.getCurrentResolution width
         * to infer UHD vs FHD; else default to 1080p.
         * This is a stubbed approximation of the /system/hdmi.uhdConfigured logic.
         */
        std::string sr;
        (void)GetScreenResolution(sr);
        // sr expected format: "[w,h]"
        int w = 1920, h = 1080;
        if (sr.size() > 2 && sr.front() == '[' && sr.back() == ']') {
            try {
                // Simple parsing without heavy JSON dependencies
                auto comma = sr.find(',');
                if (comma != std::string::npos) {
                    int sw = std::stoi(sr.substr(1, comma - 1));
                    int sh = std::stoi(sr.substr(comma + 1, sr.size() - comma - 2));
                    if (sw >= 3840 || sh >= 2160) {
                        w = 3840; h = 2160;
                    } else {
                        w = 1920; h = 1080;
                    }
                    LOGDBG("[AppGatewayCommon|GetVideoResolution] Transform screen(%d x %d) -> video(%d x %d)", sw, sh, w, h);
                }
            } catch (...) {
                // keep defaults
                LOGDBG("[AppGatewayCommon|GetVideoResolution] Transform parse error for %s -> using defaults (%d x %d)", sr.c_str(), w, h);
            }
        }
        jsonArray = "[" + std::to_string(w) + "," + std::to_string(h) + "]";
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    Core::hresult GetHdcp(std::string &jsonObject)
    {
        /**
         * Get HDCP status via HdcpProfile.getHDCPStatus.
         * Return {"hdcp1.4":bool,"hdcp2.2":bool} with sensible defaults.
         */
        jsonObject = "{\"hdcp1.4\":false,\"hdcp2.2\":false}";
        LOGDBG("[AppGatewayCommon|GetHdcp] Invoked");
        auto link = AcquireLink(HDCPPROFILE_CALLSIGN);
        if (!link) {
            LOGERR("[AppGatewayCommon|GetHdcp] HdcpProfile link unavailable, returning default %s", jsonObject.c_str());
            return Core::ERROR_UNAVAILABLE;
        }

        WPEFramework::Core::JSON::VariantContainer params;
        WPEFramework::Core::JSON::VariantContainer response;
        const uint32_t rc = link->Invoke<decltype(params), decltype(response)>("getHDCPStatus", params, response);
        if (rc != Core::ERROR_NONE) {
            LOGERR("[AppGatewayCommon|GetHdcp] getHDCPStatus failed rc=%u, returning default %s", rc, jsonObject.c_str());
            return Core::ERROR_GENERAL;
        }

        bool hdcp14 = false;
        bool hdcp22 = false;

        // Prefer nested "result" if available
        if (response.HasLabel(_T("result"))) {
            auto r = response.Get(_T("result"));
            if (r.Content() == WPEFramework::Core::JSON::Variant::type::OBJECT) {
                auto succ = r.Object().Get(_T("success"));
                auto status = r.Object().Get(_T("HDCPStatus"));
                if (succ.Content() == WPEFramework::Core::JSON::Variant::type::BOOLEAN && succ.Boolean() &&
                    status.Content() == WPEFramework::Core::JSON::Variant::type::OBJECT) {
                    auto reason = status.Object().Get(_T("hdcpReason"));
                    auto version = status.Object().Get(_T("currentHDCPVersion"));
                    if (reason.Content() == WPEFramework::Core::JSON::Variant::type::NUMBER &&
                        static_cast<int>(reason.Number()) == 2 &&
                        version.Content() == WPEFramework::Core::JSON::Variant::type::STRING) {
                        const std::string v = version.String();
                        if (v == "1.4") { hdcp14 = true; }
                        else { hdcp22 = true; }
                    }
                }
            }
        } else {
            // Fallback: try top-level fields if present
            auto status = response.Get(_T("HDCPStatus"));
            if (status.Content() == WPEFramework::Core::JSON::Variant::type::OBJECT) {
                auto reason = status.Object().Get(_T("hdcpReason"));
                auto version = status.Object().Get(_T("currentHDCPVersion"));
                if (reason.Content() == WPEFramework::Core::JSON::Variant::type::NUMBER &&
                    static_cast<int>(reason.Number()) == 2 &&
                    version.Content() == WPEFramework::Core::JSON::Variant::type::STRING) {
                    const std::string v = version.String();
                    if (v == "1.4") { hdcp14 = true; }
                    else { hdcp22 = true; }
                }
            }
        }

        jsonObject = std::string("{\"hdcp1.4\":") + (hdcp14 ? "true" : "false")
                   + ",\"hdcp2.2\":" + (hdcp22 ? "true" : "false") + "}";
        LOGDBG("[AppGatewayCommon|GetHdcp] Computed HDCP flags: hdcp1.4=%s hdcp2.2=%s -> %s",
               hdcp14 ? "true" : "false", hdcp22 ? "true" : "false", jsonObject.c_str());
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    Core::hresult GetHdr(std::string &jsonObject)
    {
        /**
         * Retrieve HDR capability/state via DisplaySettings.getTVHDRCapabilities.
         * Returns object with hdr10, dolbyVision, hlg, hdr10Plus flags (defaults false).
         */
        jsonObject = "{\"hdr10\":false,\"dolbyVision\":false,\"hlg\":false,\"hdr10Plus\":false}";
        LOGDBG("[AppGatewayCommon|GetHdr] Invoked");
        auto link = AcquireLink(DISPLAYSETTINGS_CALLSIGN);
        if (!link) {
            LOGERR("[AppGatewayCommon|GetHdr] DisplaySettings link unavailable, returning default %s", jsonObject.c_str());
            return Core::ERROR_UNAVAILABLE;
        }

        WPEFramework::Core::JSON::VariantContainer params;
        WPEFramework::Core::JSON::VariantContainer response;
        const uint32_t rc = link->Invoke<decltype(params), decltype(response)>("getTVHDRCapabilities", params, response);
        if (rc != Core::ERROR_NONE) {
            LOGERR("[AppGatewayCommon|GetHdr] getTVHDRCapabilities failed rc=%u, returning default %s", rc, jsonObject.c_str());
            return Core::ERROR_GENERAL;
        }

        bool hdr10 = false, dv = false, hlg = false, hdr10plus = false;

        // Parse HDR capabilities bitmask
        // HDRSTANDARD_NONE = 0x0
        // HDRSTANDARD_HDR10 = 0x1
        // HDRSTANDARD_HLG = 0x2
        // HDRSTANDARD_DolbyVision = 0x4
        // HDRSTANDARD_TechnicolorPrime = 0x8
        // HDRSTANDARD_HDR10PLUS = 0x10
        // HDRSTANDARD_SDR = 0x20

        auto parseCapabilities = [&](const WPEFramework::Core::JSON::Variant& vobj) {
            uint32_t capabilities = 0;

            // For ex. if DisplaySettings returns: {"capabilities":32,"success":true}
            // extract the "capabilities" field from the object
            if (vobj.Content() == WPEFramework::Core::JSON::Variant::type::OBJECT) {
                auto caps = vobj.Object().Get(_T("capabilities"));
                if (caps.Content() == WPEFramework::Core::JSON::Variant::type::NUMBER) {
                    capabilities = static_cast<uint32_t>(caps.Number());
                    LOGDBG("[AppGatewayCommon|GetHdr] Got capabilities from object: 0x%x (%d)",
                           capabilities, capabilities);
                }
            }

            // Parse bitmask - always parse, even if 0 (HDRSTANDARD_NONE is valid)
            hdr10     = (capabilities & 0x01) != 0;  // HDRSTANDARD_HDR10
            hlg       = (capabilities & 0x02) != 0;  // HDRSTANDARD_HLG
            dv        = (capabilities & 0x04) != 0;  // HDRSTANDARD_DolbyVision
            hdr10plus = (capabilities & 0x10) != 0;  // HDRSTANDARD_HDR10PLUS
            LOGDBG("[AppGatewayCommon|GetHdr] Parsed capabilities bitmask: 0x%x -> hdr10=%d hlg=%d dv=%d hdr10plus=%d",
                   capabilities, hdr10, hlg, dv, hdr10plus);
        };

        // Response is at top level: {"capabilities":32,"success":true}
        parseCapabilities(response);

        jsonObject = std::string("{\"hdr10\":") + (hdr10 ? "true" : "false")
                   + ",\"dolbyVision\":" + (dv ? "true" : "false")
                   + ",\"hlg\":" + (hlg ? "true" : "false")
                   + ",\"hdr10Plus\":" + (hdr10plus ? "true" : "false") + "}";
        LOGDBG("[AppGatewayCommon|GetHdr] Computed HDR flags: hdr10=%s dolbyVision=%s hlg=%s hdr10Plus=%s -> %s",
               hdr10 ? "true" : "false",
               dv ? "true" : "false",
               hlg ? "true" : "false",
               hdr10plus ? "true" : "false",
               jsonObject.c_str());
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
     Core::hresult GetAudio(std::string &jsonObject)
     {
         /**
          * Retrieve supported audio formats from DisplaySettings.getAudioFormat(audioPort: "HDMI0") and
          * compute flags from supportedAudioFormat array only. Multiple true values are allowed.
          * Flags:
          *  - stereo: true if a token contains "PCM" or "STEREO"
          *  - dolbyDigital5.1: true if a token contains "AC3" or "DOLBY AC3"
          *  - dolbyDigital5.1+: true if a token contains any of "EAC3", "DD+", or "AC4"
          *  - dolbyAtmos: true if a token contains "ATMOS"
          */
         bool stereo = false;
         bool dd51 = false;
         bool dd51p = false;
         bool atmos = false;

         auto link = AcquireLink(DISPLAYSETTINGS_CALLSIGN);
         if (!link) {
             LOGERR("[AppGatewayCommon|GetAudio] DisplaySettings link unavailable, returning default audio flags");
             jsonObject = "{\"stereo\":false,\"dolbyDigital5.1\":false,\"dolbyDigital5.1+\":false,\"dolbyAtmos\":false}";
             return Core::ERROR_UNAVAILABLE;
         }

         const std::string paramsStr = "{\"audioPort\":\"HDMI0\"}";
         std::string response;
         const Core::hresult rc = link->Invoke<std::string, std::string>("getAudioFormat", paramsStr, response);
         if (rc != Core::ERROR_NONE) {
             jsonObject = "{\"stereo\":false,\"dolbyDigital5.1\":false,\"dolbyDigital5.1+\":false,\"dolbyAtmos\":false}";
             return Core::ERROR_GENERAL;
         }

         WPEFramework::Core::JSON::VariantContainer v;
         WPEFramework::Core::OptionalType<WPEFramework::Core::JSON::Error> error;
         if (v.FromString(response, error)) {
             WPEFramework::Core::JSON::Variant supported;
             if (v.HasLabel(_T("result"))) {
                 auto r = v.Get(_T("result"));
                 if (r.Content() == WPEFramework::Core::JSON::Variant::type::OBJECT) {
                     supported = r.Object().Get(_T("supportedAudioFormat"));
                 }
             }
             if (supported.Content() != WPEFramework::Core::JSON::Variant::type::ARRAY) {
                 supported = v.Get(_T("supportedAudioFormat"));
             }
             // Aggregate flags only from supportedAudioFormat
             (void)SetFlagsFromSupported(supported, stereo, dd51, dd51p, atmos);
         }

         jsonObject = std::string("{\"stereo\":") + (stereo ? "true" : "false")
                    + ",\"dolbyDigital5.1\":" + (dd51 ? "true" : "false")
                    + ",\"dolbyDigital5.1+\":" + (dd51p ? "true" : "false")
                    + ",\"dolbyAtmos\":" + (atmos ? "true" : "false") + "}";
         return Core::ERROR_NONE;
     }

     /**
      * Helper: Parse supportedAudioFormat array and set flags. Returns true iff an array was found
      * and at least one recognized token was matched. Tokens are matched case-insensitively:
      * - stereo: contains "PCM" or "STEREO"
      * - dolbyDigital5.1: contains "AC3" or "DOLBY AC3" or "DOLBY DIGITAL"
      * - dolbyDigital5.1+: contains "EAC3" or "DD+" or "DOLBY DIGITAL PLUS" or "AC4"
      * - dolbyAtmos: contains "ATMOS"
      */
     static bool SetFlagsFromSupported(const WPEFramework::Core::JSON::Variant& supportedNode,
                                       bool& stereo, bool& dd51, bool& dd51p, bool& atmos)
     {
         using Var = WPEFramework::Core::JSON::Variant;
         bool anyRecognized = false;
         if (supportedNode.Content() == Var::type::ARRAY) {
             auto arr = supportedNode.Array();
             const uint16_t n = arr.Length();
             for (uint16_t i = 0; i < n; ++i) {
                 std::string token = arr[i].String();
                 if (token.empty()) {
                     continue;
                 }
                 std::string u = std::move(token);
                 std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c){ return static_cast<char>(::toupper(c)); });

                 // Stereo detection
                 if (u.find("PCM") != std::string::npos || u.find("STEREO") != std::string::npos) {
                     stereo = true; anyRecognized = true;
                 }
                 // Dolby Digital (AC3)
                 // Only match "AC3" if not preceded by 'E' (i.e., not "EAC3")
                 bool isAC3 = false;
                 // Check for "AC3" not preceded by 'E'
                 size_t ac3_pos = u.find("AC3");
                 if (ac3_pos != std::string::npos) {
                     if (ac3_pos == 0 || u[ac3_pos - 1] != 'E') {
                         isAC3 = true;
                     }
                 }
                 if (isAC3 || u.find("DOLBY AC3") != std::string::npos || u.find("DOLBY DIGITAL") != std::string::npos) {
                     dd51 = true; anyRecognized = true;
                 }
                 // Dolby Digital Plus (EAC3/DD+/AC4)
                 if (u.find("EAC3") != std::string::npos || u.find("DD+") != std::string::npos || u.find("DOLBY DIGITAL PLUS") != std::string::npos || u.find("AC4") != std::string::npos) {
                     dd51p = true; anyRecognized = true;
                 }
                 // Atmos (any transport)
                 if (u.find("ATMOS") != std::string::npos) {
                     atmos = true; anyRecognized = true;
                 }
             }
         }
         return anyRecognized;
     }

    // ---- Event exposure (Emit helpers) ----

    // PUBLIC_INTERFACE
    bool EmitOnVideoResolutionChanged()
    {
        std::string payload;
        if (GetVideoResolution(payload) != Core::ERROR_NONE) {
            LOGERR("[AppGatewayCommon|VideoResolutionChanged] handler=GetVideoResolution failed to compute payload");
            return false;
        }
        if (payload.empty()) {
            LOGERR("[AppGatewayCommon|VideoResolutionChanged] handler=GetVideoResolution returned empty payload");
            return false;
        }
        Dispatch(EVENT_ON_VIDEO_RES_CHANGED, payload);
            return true;
    }

    // PUBLIC_INTERFACE
    bool EmitOnScreenResolutionChanged()
    {
        std::string payload;
        if (GetScreenResolution(payload) != Core::ERROR_NONE) {
            LOGERR("[AppGatewayCommon|ScreenResolutionChanged] handler=GetScreenResolution failed to compute payload");
            return false;
        }

        if (payload.empty()) {
            LOGERR("[AppGatewayCommon|ScreenResolutionChanged] handler=GetScreenResolution returned empty payload");
            return false;
        }
        
        Dispatch(EVENT_ON_SCREEN_RES_CHANGED, payload);
        return true;

    }

    // PUBLIC_INTERFACE
    bool EmitOnHdcpChanged()
    {
        std::string payload;
        if (GetHdcp(payload) != Core::ERROR_NONE) {
            LOGERR("[AppGatewayCommon|HdcpChanged] handler=GetHdcp failed to compute payload");
            return false;
        }

        if (payload.empty()) {
            LOGERR("[AppGatewayCommon|HdcpChanged] handler=GetHdcp returned empty payload");
            return false;
        }

        Dispatch(EVENT_ON_HDCP_CHANGED, payload);
        return true;

    }

    // PUBLIC_INTERFACE
    bool EmitOnHdrChanged()
    {
        std::string payload;
        if (GetHdr(payload) != Core::ERROR_NONE) {
            LOGERR("[AppGatewayCommon|HdrChanged] handler=GetHdr failed to compute payload");
            return false;
        }

        if (payload.empty()) {
            LOGERR("[AppGatewayCommon|HdrChanged] handler=GetHdr returned empty payload");
            return false;
        }

        // Legacy payload 
        Dispatch(EVENT_ON_HDR_CHANGED, payload);
        
        return true;

    }

    // PUBLIC_INTERFACE
    bool EmitOnNameChanged()
    {
        std::string payload;
        if (GetDeviceName(payload) != Core::ERROR_NONE) {
            LOGERR("[AppGatewayCommon|NameChanged] handler=GetDeviceName failed to compute payload");
            return false;
        }

        if (payload.empty()) {
            LOGERR("[AppGatewayCommon|NameChanged] handler=GetDeviceName returned empty payload");
            return false;
        }

        Dispatch(EVENT_ON_NAME_CHANGED, payload);
        Dispatch(EVENT_ON_DEVICE_NAME_CHANGED, payload);
        return true;
    }

    // PUBLIC_INTERFACE
    bool EmitOnAudioChanged()
    {
        std::string payload;
        if (GetAudio(payload) != Core::ERROR_NONE) {
            LOGERR("[AppGatewayCommon|AudioChanged] handler=GetAudio failed to compute payload");
            return false;
        }

        if (payload.empty()) {
            LOGERR("[AppGatewayCommon|AudioChanged] handler=GetAudio returned empty payload");
            return false;
        }

        Dispatch(EVENT_ON_AUDIO_CHANGED, payload);
        return true;
    }

    // ---- New Device info APIs (Firebolt Device module) ----

    // PUBLIC_INTERFACE
    Core::hresult GetDeviceChipsetId(std::string &chipset)
    {
        chipset.clear();
        if (nullptr == _shell) return Core::ERROR_UNAVAILABLE;

        auto* di = _shell->QueryInterfaceByCallsign<Exchange::IDeviceInfo>(DEVICEINFO_CALLSIGN);
        if (nullptr == di)
        {
            LOGWARN("SystemDelegate: IDeviceInfo unavailable for ChipSet");
            return Core::ERROR_UNAVAILABLE;
        }

        Exchange::IDeviceInfo::DeviceChip chip{};
        const Core::hresult rc = di->ChipSet(chip);
        di->Release();

        if (rc != Core::ERROR_NONE)
        {
            LOGERR("SystemDelegate: IDeviceInfo::ChipSet failed rc=%u", rc);
            return Core::ERROR_GENERAL;
        }
        chipset = "\"" + chip.chipset + "\"";
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    Core::hresult GetDeviceClass(std::string &typeOut)
    {
        typeOut.clear();
        if (nullptr == _shell) return Core::ERROR_UNAVAILABLE;

        auto* di = _shell->QueryInterfaceByCallsign<Exchange::IDeviceInfo>(DEVICEINFO_CALLSIGN);
        if (nullptr == di)
        {
            LOGWARN("SystemDelegate: IDeviceInfo unavailable for DeviceType");
            return Core::ERROR_UNAVAILABLE;
        }

        Exchange::IDeviceInfo::DeviceTypeInfos info{};
        const Core::hresult rc = di->DeviceType(info);
        di->Release();

        if (rc != Core::ERROR_NONE)
        {
            LOGERR("SystemDelegate: IDeviceInfo::DeviceType failed rc=%u", rc);
            return Core::ERROR_GENERAL;
        }
        typeOut = "\"" + MapDeviceTypeEnumToFirebolt(info.devicetype) + "\"";
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    Core::hresult GetDeviceUptime(std::string &uptime)
    {
        uptime.clear();
        if (nullptr == _shell) return Core::ERROR_UNAVAILABLE;

        auto* di = _shell->QueryInterfaceByCallsign<Exchange::IDeviceInfo>(DEVICEINFO_CALLSIGN);
        if (nullptr == di)
        {
            LOGWARN("SystemDelegate: IDeviceInfo unavailable for SystemInfo");
            return Core::ERROR_UNAVAILABLE;
        }

        Exchange::IDeviceInfo::SystemInfos sysInfo{};
        const Core::hresult rc = di->SystemInfo(sysInfo);
        di->Release();

        if (rc != Core::ERROR_NONE)
        {
            LOGERR("SystemDelegate: IDeviceInfo::SystemInfo failed rc=%u", rc);
            return Core::ERROR_GENERAL;
        }
        uptime = std::to_string(static_cast<uint64_t>(sysInfo.uptime));
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    Core::hresult GetDeviceTimeInActiveState(std::string &result)
    {
        result.clear();
        if (nullptr == _shell) return Core::ERROR_UNAVAILABLE;

        auto* pm = _shell->QueryInterfaceByCallsign<Exchange::IPowerManager>(POWERMANAGER_CALLSIGN);
        if (nullptr == pm)
        {
            LOGWARN("SystemDelegate: IPowerManager unavailable for GetTimeSinceWakeup");
            return Core::ERROR_UNAVAILABLE;
        }

        Exchange::IPowerManager::TimeSinceWakeup tsw{};
        const Core::hresult rc = pm->GetTimeSinceWakeup(tsw);
        pm->Release();

        if (rc != Core::ERROR_NONE)
        {
            LOGERR("SystemDelegate: IPowerManager::GetTimeSinceWakeup failed rc=%u", rc);
            return Core::ERROR_GENERAL;
        }
        result = std::to_string(static_cast<uint64_t>(tsw.secondsSinceWakeup));
        return Core::ERROR_NONE;
    }

    // ---- Device Branding APIs (Phase 1) ----

    // PUBLIC_INTERFACE
    // Device.setOsName — writes operating system name to DeviceInfo.osname (persisted)
    Core::hresult SetDeviceOsName(const std::string &osName)
    {
        if (nullptr == _shell) return Core::ERROR_UNAVAILABLE;

        auto* di = _shell->QueryInterfaceByCallsign<Exchange::IDeviceInfo>(DEVICEINFO_CALLSIGN);
        if (nullptr == di)
        {
            LOGWARN("SystemDelegate: IDeviceInfo unavailable for SetOsName");
            return Core::ERROR_UNAVAILABLE;
        }

        const Core::hresult rc = di->OsName(osName);
        di->Release();

        if (rc != Core::ERROR_NONE)
        {
            LOGERR("SystemDelegate: IDeviceInfo::OsName(set) failed rc=%u", rc);
            return Core::ERROR_GENERAL;
        }
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    // Device.osName — reads operating system name from DeviceInfo.osname
    Core::hresult GetDeviceOsName(std::string &result)
    {
        result.clear();
        if (nullptr == _shell) return Core::ERROR_UNAVAILABLE;

        auto* di = _shell->QueryInterfaceByCallsign<Exchange::IDeviceInfo>(DEVICEINFO_CALLSIGN);
        if (nullptr == di)
        {
            LOGWARN("SystemDelegate: IDeviceInfo unavailable for GetOsName");
            return Core::ERROR_UNAVAILABLE;
        }

        Exchange::IDeviceInfo::DeviceOsName info{};
        const Core::hresult rc = di->OsName(info);
        di->Release();

        if (rc != Core::ERROR_NONE)
        {
            LOGERR("SystemDelegate: IDeviceInfo::OsName(get) failed rc=%u", rc);
            return Core::ERROR_GENERAL;
        }
        Core::JSON::String jsonStr;
        jsonStr = info.osName;
        jsonStr.ToString(result);
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    // Device.setOsVersion — writes operating system version to DeviceInfo.osversion (persisted)
    Core::hresult SetDeviceOsVersion(const std::string &osVersion)
    {
        if (nullptr == _shell) return Core::ERROR_UNAVAILABLE;

        auto* di = _shell->QueryInterfaceByCallsign<Exchange::IDeviceInfo>(DEVICEINFO_CALLSIGN);
        if (nullptr == di)
        {
            LOGWARN("SystemDelegate: IDeviceInfo unavailable for SetOsVersion");
            return Core::ERROR_UNAVAILABLE;
        }

        const Core::hresult rc = di->OsVersion(osVersion);
        di->Release();

        if (rc != Core::ERROR_NONE)
        {
            LOGERR("SystemDelegate: IDeviceInfo::OsVersion(set) failed rc=%u", rc);
            return Core::ERROR_GENERAL;
        }
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    // Device.osVersion — reads operating system version from DeviceInfo.osversion
    Core::hresult GetDeviceOsVersion(std::string &result)
    {
        result.clear();
        if (nullptr == _shell) return Core::ERROR_UNAVAILABLE;

        auto* di = _shell->QueryInterfaceByCallsign<Exchange::IDeviceInfo>(DEVICEINFO_CALLSIGN);
        if (nullptr == di)
        {
            LOGWARN("SystemDelegate: IDeviceInfo unavailable for GetOsVersion");
            return Core::ERROR_UNAVAILABLE;
        }

        Exchange::IDeviceInfo::DeviceOsVersion info{};
        const Core::hresult rc = di->OsVersion(info);
        di->Release();

        if (rc != Core::ERROR_NONE)
        {
            LOGERR("SystemDelegate: IDeviceInfo::OsVersion(get) failed rc=%u", rc);
            return Core::ERROR_GENERAL;
        }
        Core::JSON::String jsonStr;
        jsonStr = info.osVersion;
        jsonStr.ToString(result);
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    // Device.firmware — reads firmware image name from DeviceInfo.firmwareversion.imagename
    Core::hresult GetDeviceFirmware(std::string &result)
    {
        result.clear();
        if (nullptr == _shell) return Core::ERROR_UNAVAILABLE;

        auto* di = _shell->QueryInterfaceByCallsign<Exchange::IDeviceInfo>(DEVICEINFO_CALLSIGN);
        if (nullptr == di)
        {
            LOGWARN("SystemDelegate: IDeviceInfo unavailable for FirmwareVersion");
            return Core::ERROR_UNAVAILABLE;
        }

        Exchange::IDeviceInfo::FirmwareversionInfo info{};
        const Core::hresult rc = di->FirmwareVersion(info);
        di->Release();

        if (rc != Core::ERROR_NONE)
        {
            LOGERR("SystemDelegate: IDeviceInfo::FirmwareVersion failed rc=%u", rc);
            return Core::ERROR_GENERAL;
        }
        Core::JSON::String jsonStr;
        jsonStr = info.imagename;
        jsonStr.ToString(result);
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    bool EmitOnTimezoneChanged(const WPEFramework::Core::JSON::VariantContainer& params)
    {
        if (!params.HasLabel(_T("newTimeZone"))) {
            LOGERR("[AppGatewayCommon|TimezoneChanged] missing newTimeZone parameter in event");
            return false;
        }

        const std::string newTz =  params[_T("newTimeZone")].String();
        if (newTz.empty()) {
            LOGERR("[AppGatewayCommon|TimezoneChanged] newTimeZone parameter is empty");
            return false;
        }
        Dispatch(EVENT_ON_TIMEZONE_CHANGED, "\"" + newTz + "\"");
        return true;
    }

    bool EmitOnTerritoryChanged(const WPEFramework::Core::JSON::VariantContainer& params)
    {
        if (!params.HasLabel(_T("newTerritory"))) {
            LOGERR("[AppGatewayCommon|TerritoryChanged] missing newTerritory parameter in event");
            return false;
        }

        const std::string newTerritory =  params[_T("newTerritory")].String();
        if (newTerritory.empty()) {
            LOGERR("[AppGatewayCommon|TerritoryChanged] newTerritory parameter is empty");
            return false;
        }
        const std::string code = TerritoryThunderToFirebolt(newTerritory, "");

        if (code.empty()) {
            LOGERR("[AppGatewayCommon|TerritoryChanged] unrecognized territory code %s in event", newTerritory.c_str());
            return false;
        }

        JsonObject object;
        object["value"] = code;
        string result;
        object.ToString(result);

        Dispatch(EVENT_ON_COUNTRY_CHANGED, result);
        return true;
    }

    // ---- AppNotifications registration hook ----
    // Called by SettingsDelegate when app subscribes/unsubscribes to events.
    bool HandleEvent(Exchange::IAppNotificationHandler::IEmitter *cb, const std::string &event, const bool listen, bool &registrationError)
    {
        registrationError = false;

        const std::string evLower = ToLower(event);

        if (evLower == "device.onvideoresolutionchanged" || evLower == "device.onscreenresolutionchanged") {
            SetupDisplaySettingsSubscription();
        } else if (evLower == "device.onhdcpchanged" || evLower == "device.onhdrchanged") {
            SetupHdcpProfileSubscription();
        } else if (evLower == "device.onaudiochanged") {
            SetupDisplaySettingsAudioSubscription();
        } else if (evLower == "device.ondevicenamechanged" || evLower == "device.onnamechanged") {
            SetupSystemSub();
        } else if (evLower == "localization.ontimezonechanged") {
            SetupSystemSub();
        } else if (evLower == "localization.oncountrychanged") {
            SetupSystemSub();
        } else {
            registrationError = true; // event not recognized - signal error to caller
            return false;
        }

        if (!registrationError) {
            LOGINFO("[AppGatewayCommon|EventRegistration] event=%s listen=%s", event.c_str(), listen ? "true" : "false");
            if (listen) {
                AddNotification(event, cb);
            } else {
                RemoveNotification(event, cb);
            }
        }

        return true;

    }

    // ---- Display APIs (Firebolt Display module) ----

    // PUBLIC_INTERFACE
    // Display.edid — reads raw EDID bytes from DisplayInfo via ComRPC (Exchange::IConnectionProperties::EDID),
    // then Base64-encodes them to produce the Firebolt string result.
    // Returns "" when no display is connected (STB/OTT) or the interface is unavailable.
    Core::hresult GetDisplayEdid(std::string &result)
    {
        result = "\"\"";

        if (nullptr == _shell)
        {
            LOGERR("SystemDelegate: shell is null for GetDisplayEdid");
            return Core::ERROR_UNAVAILABLE;
        }

        auto* connProps = _shell->QueryInterfaceByCallsign<Exchange::IConnectionProperties>(DISPLAYINFO_CALLSIGN);
        if (nullptr == connProps)
        {
            LOGWARN("SystemDelegate: IConnectionProperties unavailable for EDID (no display or plugin absent)");
            return Core::ERROR_NONE;
        }

        // Check HDMI connection before requesting EDID
        bool connected = false;
        const Core::hresult connRc = connProps->Connected(connected);
        if (connRc != Core::ERROR_NONE)
        {
            LOGWARN("SystemDelegate: IConnectionProperties::Connected rc=%u — assuming no display, returning empty EDID", connRc);
            connProps->Release();
            return Core::ERROR_NONE;
        }
        if (!connected)
        {
            LOGWARN("SystemDelegate: No display connected — returning empty EDID");
            connProps->Release();
            return Core::ERROR_NONE;
        }

        // Allocate buffer for up to 4 EDID blocks (128 bytes each, max 512 bytes).
        // EDID() uses length as @inout: pass the buffer capacity, receives the actual byte count.
        static constexpr uint16_t kMaxEdidBytes = 512;
        std::array<uint8_t, kMaxEdidBytes> edidBuf{};
        uint16_t edidLen = kMaxEdidBytes;
        const Core::hresult rc = connProps->EDID(edidLen, edidBuf.data());
        connProps->Release();

        if (rc != Core::ERROR_NONE || edidLen == 0)
        {
            LOGWARN("SystemDelegate: IConnectionProperties::EDID rc=%u len=%u (no display or not supported)", rc, static_cast<unsigned>(edidLen));
            return Core::ERROR_NONE;
        }

        const std::string encoded = Base64Encode(edidBuf.data(), edidLen);
        LogEdidInfo(encoded);
        result = "\"" + encoded + "\"";
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    // Display.size — reads physical display dimensions via ComRPC (Exchange::IConnectionProperties).
    // Uses WidthInCentimeters() / HeightInCentimeters() which return uint8_t directly.
    // Returns {"width":0,"height":0} when no display is connected (STB/OTT) or the interface is unavailable.
    Core::hresult GetDisplaySize(std::string &result)
    {
        result = "{\"width\":0,\"height\":0}";

        if (nullptr == _shell)
        {
            LOGERR("SystemDelegate: shell is null for GetDisplaySize");
            return Core::ERROR_UNAVAILABLE;
        }

        auto* connProps = _shell->QueryInterfaceByCallsign<Exchange::IConnectionProperties>(DISPLAYINFO_CALLSIGN);
        if (nullptr == connProps)
        {
            LOGWARN("SystemDelegate: IConnectionProperties unavailable for size (no display or plugin absent)");
            return Core::ERROR_NONE;
        }

        uint8_t width = 0, height = 0;
        const Core::hresult wRc = connProps->WidthInCentimeters(width);
        const Core::hresult hRc = connProps->HeightInCentimeters(height);
        connProps->Release();

        if (wRc != Core::ERROR_NONE && hRc != Core::ERROR_NONE)
        {
            LOGWARN("SystemDelegate: IConnectionProperties WidthInCentimeters rc=%u HeightInCentimeters rc=%u (no display)", wRc, hRc);
            return Core::ERROR_NONE;
        }

        LOGDBG("SystemDelegate: GetDisplaySize width=%u cm height=%u cm",
               static_cast<unsigned>(width), static_cast<unsigned>(height));

        JsonObject obj;
        obj["width"]  = static_cast<int>(width);
        obj["height"] = static_cast<int>(height);
        obj.ToString(result);
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    // Display.maxResolution — reads TV pixel resolution via ComRPC (Exchange::IConnectionProperties).
    // Uses Width() / Height() which return uint32_t directly.
    // Returns {"width":0,"height":0} when no display is connected (STB/OTT) or the interface is unavailable.
    Core::hresult GetDisplayMaxResolution(std::string &result)
    {
        result = "{\"width\":0,\"height\":0}";

        if (nullptr == _shell)
        {
            LOGERR("SystemDelegate: shell is null for GetDisplayMaxResolution");
            return Core::ERROR_UNAVAILABLE;
        }

        auto* connProps = _shell->QueryInterfaceByCallsign<Exchange::IConnectionProperties>(DISPLAYINFO_CALLSIGN);
        if (nullptr == connProps)
        {
            LOGWARN("SystemDelegate: IConnectionProperties unavailable for maxResolution (no display or plugin absent)");
            return Core::ERROR_NONE;
        }

        uint32_t width = 0, height = 0;
        const Core::hresult wRc = connProps->Width(width);
        const Core::hresult hRc = connProps->Height(height);
        connProps->Release();

        if (wRc != Core::ERROR_NONE && hRc != Core::ERROR_NONE)
        {
            LOGWARN("SystemDelegate: IConnectionProperties Width rc=%u Height rc=%u (no display)", wRc, hRc);
            return Core::ERROR_NONE;
        }

        LOGDBG("SystemDelegate: GetDisplayMaxResolution width=%u height=%u", width, height);

        JsonObject obj;
        obj["width"]  = static_cast<int>(width);
        obj["height"] = static_cast<int>(height);
        obj.ToString(result);
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    // Display.colorimetry — reads colorimetry from DisplayInfo via ComRPC (Exchange::IDisplayProperties).
    // Uses IDisplayProperties::Colorimetry(IColorimetryIterator*&) to get typed enum values directly,
    // then maps them to Firebolt enum strings ("bt709", "bt2020").
    // Returns "[]" when no display is connected (STB/OTT) or the interface is unavailable.
    Core::hresult GetDisplayColorimetry(std::string &result)
    {
        result = "[]";

        if (nullptr == _shell)
        {
            LOGERR("SystemDelegate: shell is null for GetDisplayColorimetry");
            return Core::ERROR_UNAVAILABLE;
        }

        auto* displayProps = _shell->QueryInterfaceByCallsign<Exchange::IDisplayProperties>(DISPLAYINFO_CALLSIGN);
        if (nullptr == displayProps)
        {
            LOGWARN("SystemDelegate: IDisplayProperties unavailable for colorimetry (no display or plugin absent)");
            return Core::ERROR_NONE;
        }

        Exchange::IDisplayProperties::IColorimetryIterator* iter = nullptr;
        const Core::hresult rc = displayProps->Colorimetry(iter);
        displayProps->Release();

        if (rc != Core::ERROR_NONE || nullptr == iter)
        {
            LOGWARN("SystemDelegate: IDisplayProperties::Colorimetry rc=%u (no display or not supported)", rc);
            if (nullptr != iter) iter->Release();
            return Core::ERROR_NONE;
        }

        // Iterate and map ColorimetryType enum values to Firebolt strings.
        // bt709  ← COLORIMETRY_BT709
        // bt2020 ← COLORIMETRY_BT2020YCCBCBRC, COLORIMETRY_BT2020RGB_YCBCR
        bool hasBt709  = false;
        bool hasBt2020 = false;
        Exchange::IDisplayProperties::ColorimetryType value{};
        while (iter->Next(value) == true)
        {
            if (value == Exchange::IDisplayProperties::COLORIMETRY_BT709)
                hasBt709 = true;
            else if (value == Exchange::IDisplayProperties::COLORIMETRY_BT2020YCCBCBRC ||
                     value == Exchange::IDisplayProperties::COLORIMETRY_BT2020RGB_YCBCR)
                hasBt2020 = true;
        }
        iter->Release();

        if (!hasBt709 && !hasBt2020)
        {
            LOGWARN("SystemDelegate: IDisplayProperties::Colorimetry: no recognized values in response");
            return Core::ERROR_NONE;
        }

        result = "[";
        bool first = true;
        if (hasBt709)
        {
            result += "\"bt709\"";
            first = false;
        }
        if (hasBt2020)
        {
            if (!first) result += ",";
            result += "\"bt2020\"";
        }
        result += "]";

        LOGDBG("SystemDelegate: GetDisplayColorimetry result=%s", result.c_str());
        return Core::ERROR_NONE;
    }

    // PUBLIC_INTERFACE
    // Display.videoResolutions — reads supported resolutions from org.rdk.DisplaySettings.getSupportedResolutions.
    // Maps Thunder resolution strings (e.g. "720p", "720p50", "1080p60") to Firebolt VideoResolution enum strings.
    // Only HD resolutions (720p and above) are included; results are deduplicated by resolution class.
    // Returns "[]" when no display is connected (STB/OTT) or the plugin is unavailable.
    Core::hresult GetDisplayVideoResolutions(std::string &result)
    {
        result = "[]";
        auto link = AcquireLink(DISPLAYSETTINGS_CALLSIGN);
        if (!link)
        {
            LOGWARN("SystemDelegate: DisplaySettings link unavailable for getSupportedResolutions (no display or plugin absent)");
            return Core::ERROR_NONE;
        }

        WPEFramework::Core::JSON::VariantContainer params;
        WPEFramework::Core::JSON::VariantContainer response;
        const uint32_t rc = link->Invoke<decltype(params), decltype(response)>("getSupportedResolutions", params, response);
        if (rc != Core::ERROR_NONE || !response.HasLabel(_T("supportedResolutions")))
        {
            LOGWARN("SystemDelegate: DisplaySettings.getSupportedResolutions rc=%u (no display or not supported)", rc);
            return Core::ERROR_NONE;
        }

        const WPEFramework::Core::JSON::Variant& resVar = response.Get(_T("supportedResolutions"));
        if (resVar.Content() != WPEFramework::Core::JSON::Variant::type::ARRAY)
        {
            LOGWARN("SystemDelegate: DisplaySettings.getSupportedResolutions: unexpected non-array type (%d) for supportedResolutions; treating as unsupported",
                    static_cast<int>(resVar.Content()));
            return Core::ERROR_NONE;
        }

        std::string arrJson;
        {
            auto arr = resVar.Array();
            const uint16_t n = arr.Length();
            for (uint16_t i = 0; i < n; ++i)
            {
                arrJson += arr[i].String();
                arrJson += ' ';
            }
        }

        LOGDBG("SystemDelegate: DisplaySettings.getSupportedResolutions supportedResolutions=%s", arrJson.c_str());

        // Map each Thunder resolution token to Firebolt VideoResolution enum strings.
        // Firebolt spec defines exactly 6 values: 720p50, 720p60, 1080p50, 1080p60, 2160p50, 2160p60.
        //
        // Mapping rules:
        //  - Generic "720p"  (no framerate) → both 720p50 and 720p60 (display supports both rates)
        //  - Specific "720p50" → 720p50 only; "720p60" → 720p60 only
        //  - Same pattern for 1080p and 2160p classes
        //  - "1080p24/25/30", "2160p24/25/30", "480p", "576p*", "768p*", "1080i*" → no Firebolt mapping
        struct ThunderToFb {
            const char* thunder;
            const char* fb1;
            const char* fb2;
        };
        static const std::array<ThunderToFb, 9> kMap = {{
            { "720p",    "720p50",  "720p60"  },
            { "720p50",  "720p50",  nullptr   },
            { "720p60",  "720p60",  nullptr   },
            { "1080p",   "1080p50", "1080p60" },
            { "1080p50", "1080p50", nullptr   },
            { "1080p60", "1080p60", nullptr   },
            { "2160p",   "2160p50", "2160p60" },
            { "2160p50", "2160p50", nullptr   },
            { "2160p60", "2160p60", nullptr   },
        }};

        // Collect results preserving insertion order, deduplicated (e.g. "720p" + "720p50" → 720p50 once)
        // resVar is guaranteed to be ARRAY here (non-array was rejected above).
        std::unordered_set<std::string> seen;
        std::vector<std::string> fbResults;
        {
            auto arr = resVar.Array();
            const uint16_t n = arr.Length();
            for (uint16_t i = 0; i < n; ++i)
            {
                std::string token = arr[i].String();
                std::transform(token.begin(), token.end(), token.begin(),
                               [](unsigned char c){ return static_cast<char>(::tolower(c)); });
                for (const auto& entry : kMap)
                {
                    if (token == entry.thunder)
                    {
                        if (seen.insert(entry.fb1).second)
                            fbResults.push_back(entry.fb1);
                        if (entry.fb2 != nullptr && seen.insert(entry.fb2).second)
                            fbResults.push_back(entry.fb2);
                        break;
                    }
                }
            }
        }

        std::string items;
        for (const auto& en : fbResults)
        {
            if (!items.empty()) items += ',';
            items += '"';
            items += en;
            items += '"';
        }

        if (items.empty())
        {
            LOGWARN("SystemDelegate: DisplaySettings.getSupportedResolutions: no Firebolt HD enum values in response: %s", arrJson.c_str());
            return Core::ERROR_NONE;
        }

        result = "[" + items + "]";

        LOGDBG("SystemDelegate: GetDisplayVideoResolutions result=%s", result.c_str());
        return Core::ERROR_NONE;
    }


private:
    // Encode raw bytes to a standard Base64 string (RFC 4648, with '=' padding).
    static std::string Base64Encode(const uint8_t* in, size_t inLen)
    {
        static const char kAlpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((inLen + 2) / 3) * 4);
        uint32_t acc = 0;
        int      bits = 0;
        for (size_t i = 0; i < inLen; ++i)
        {
            acc   = (acc << 8) | static_cast<uint32_t>(in[i]);
            bits += 8;
            while (bits >= 6)
            {
                bits -= 6;
                out += kAlpha[(acc >> bits) & 0x3Fu];
            }
        }
        if (bits > 0)
        {
            acc <<= (6 - bits);
            out += kAlpha[acc & 0x3Fu];
        }
        while (out.size() % 4 != 0)
            out += '=';
        return out;
    }

    // Decode a single Base64 character to its 6-bit value; returns -1 for padding/invalid.
    static int B64CharVal(char c)
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    // Decode a Base64 string (already normalized, no escaped slashes) into raw bytes.
    // Returns number of bytes written into out[0..outMax-1].
    // Uses a uint32_t accumulator to avoid signed-integer overflow (which would be UB).
    // After emitting each byte the consumed bits are masked off, keeping acc bounded.
    // '=' padding characters are treated as terminators (B64CharVal returns -1 for them).
    static size_t Base64Decode(const std::string& in, uint8_t* out, size_t outMax)
    {
        size_t   outLen = 0;
        uint32_t acc    = 0;
        int      bits   = 0;
        for (char c : in)
        {
            if (c == '=') break;          // padding marks end of data
            const int val = B64CharVal(c);
            if (val < 0) continue;        // skip whitespace / invalid chars
            acc   = (acc << 6) | static_cast<uint32_t>(val);
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                if (outLen < outMax)
                    out[outLen++] = static_cast<uint8_t>((acc >> bits) & 0xFFu);
                acc &= (1u << bits) - 1u; // discard emitted bits, keep only remainder
            }
        }
        return outLen;
    }

    // Parse the normalized Base64 EDID string and log display information via LOGINFO
    // in a tabular ASCII format. Collects base-block fields (manufacturer, product, year,
    // interface, physical size, monitor name, checksum) and CEA-861 extension capabilities
    // (audio flags, video SVDs, colorimetry, HDR EOTFs), then prints them as a single table.
    // Fully failsafe: all accesses are bounds-checked; any unexpected input is caught
    // by the outer try/catch and logged without crashing.
    static void LogEdidInfo(const std::string& base64edid)
    {
        if (base64edid.empty()) return;

        try
        {
            std::array<uint8_t, 512> raw{};
            const size_t rawLen = Base64Decode(base64edid, raw.data(), raw.size());
            if (rawLen < 128)
            {
                LOGERR("SystemDelegate: [EDID] Too short to parse (%zu bytes)", rawLen);
                return;
            }

            // cap to avoid arithmetic overflows; rawLen is at most raw.size()==512
            const size_t safeLen = (rawLen < raw.size()) ? rawLen : raw.size();
            const uint8_t* e = raw.data();

            // Validate magic header: 00 FF FF FF FF FF FF 00
            static const uint8_t kHdr[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
            for (int i = 0; i < 8; ++i)
            {
                if (e[i] != kHdr[i])
                {
                    LOGERR("SystemDelegate: [EDID] Invalid header byte[%d]=0x%02X — not a valid EDID", i, e[i]);
                    return;
                }
            }

            // ---- Collect base-block fields ----

            // Manufacturer ID: 3 letters packed in 2 big-endian bytes (each field 1-26 → A-Z)
            const uint16_t mfrRaw = static_cast<uint16_t>((e[8] << 8) | e[9]);
            char mfr[4] = { '?', '?', '?', '\0' };
            {
                const uint8_t f0 = (mfrRaw >> 10) & 0x1F;
                const uint8_t f1 = (mfrRaw >>  5) & 0x1F;
                const uint8_t f2 =  mfrRaw        & 0x1F;
                if (f0 >= 1 && f0 <= 26) mfr[0] = static_cast<char>(f0 + 'A' - 1);
                if (f1 >= 1 && f1 <= 26) mfr[1] = static_cast<char>(f1 + 'A' - 1);
                if (f2 >= 1 && f2 <= 26) mfr[2] = static_cast<char>(f2 + 'A' - 1);
            }

            char prodStr[12];
            snprintf(prodStr, sizeof(prodStr), "0x%04X",
                     static_cast<unsigned>(e[10] | (e[11] << 8)));

            char yearStr[20];
            snprintf(yearStr, sizeof(yearStr), "%d / Week %d", 1990 + (int)e[17], (int)e[16]);

            char edidVerStr[8];
            snprintf(edidVerStr, sizeof(edidVerStr), "%d.%d", (int)e[18], (int)e[19]);

            // Interface type
            const uint8_t inp      = e[20];
            const bool    isDigital = (inp & 0x80) != 0;
            const char*   ifaceStr  = "Analog";
            if (isDigital)
            {
                static const char* kIface[] = { "undef","DVI","HDMIa","HDMIb","MDDI","DisplayPort" };
                const uint8_t idx = inp & 0x0F;
                ifaceStr = (idx < 6) ? kIface[idx] : "Digital";
            }
            static const char* kBpc[] = { "undef","6bpc","8bpc","10bpc","12bpc","14bpc","16bpc" };
            const uint8_t bpcIdx = (inp >> 4) & 0x07;
            const char*   bpcStr = (bpcIdx < 7) ? kBpc[bpcIdx] : "undef";
            char ifaceFullStr[24];
            snprintf(ifaceFullStr, sizeof(ifaceFullStr), "%s / %s", ifaceStr, bpcStr);

            char sizeStr[20];
            snprintf(sizeStr, sizeof(sizeStr), "%d x %d cm", (int)e[21], (int)e[22]);

            const int numExts = static_cast<int>(e[126]);
            char numExtsStr[8];
            snprintf(numExtsStr, sizeof(numExtsStr), "%d", numExts);

            // Base checksum
            uint32_t baseSum = 0;
            for (int i = 0; i < 128; ++i) baseSum += e[i];
            const char* baseCsumStr = (baseSum % 256 == 0) ? "OK" : "FAIL";

            // Monitor name from descriptor blocks (tag 0xFC)
            char monName[14] = {};
            bool foundMonName = false;
            for (int d = 0; d < 4; ++d)
            {
                const int descOff = 54 + d * 18;
                if (descOff + 18 > 128) break;
                const uint8_t* desc = e + descOff;
                if (desc[0] == 0 && desc[1] == 0 && desc[3] == 0xFC)
                {
                    int ni = 0;
                    for (int j = 5; j < 18 && ni < 13; ++j)
                    {
                        if (desc[j] == 0x0A || desc[j] == 0x00) break;
                        const uint8_t ch = desc[j];
                        monName[ni++] = (ch >= 0x20 && ch <= 0x7E) ? static_cast<char>(ch) : '?';
                    }
                    monName[ni] = '\0';
                    foundMonName = true;
                    break;
                }
            }
            const char* monNameDisp = foundMonName ? monName : "(not found)";

            // ---- Collect CEA-861 extension fields (first CEA block only) ----
            bool        hasCea     = false;
            int         ceaRev     = 0;
            bool        underscan  = false;
            bool        basicAudio = false;
            bool        ycbcr444   = false;
            bool        ycbcr422   = false;
            int         videoSVDs  = 0;
            char        nativeVicStr[28] = "N/A";
            bool        has4k      = false;
            std::string colorimetry = "none";
            std::string hdrEotfs    = "none";
            const char* extCsumStr  = "N/A";

            for (int xi = 0; xi < numExts; ++xi)
            {
                const size_t extBase = 128 + static_cast<size_t>(xi) * 128;
                if (extBase + 128 > safeLen) break;
                const uint8_t* ext = raw.data() + extBase;
                if (ext[0] != 0x02) continue;  // only CEA-861 extensions

                hasCea     = true;
                ceaRev     = static_cast<int>(ext[1]);
                const uint8_t flags = ext[3];
                underscan  = (flags & 0x80) != 0;
                basicAudio = (flags & 0x40) != 0;
                ycbcr444   = (flags & 0x20) != 0;
                ycbcr422   = (flags & 0x10) != 0;

                uint32_t extSumLocal = 0;
                for (int i = 0; i < 128; ++i) extSumLocal += ext[i];
                extCsumStr = (extSumLocal % 256 == 0) ? "OK" : "FAIL";

                // dtdOff: valid range 4..127; clamp to [4,127]
                const int dtdOff = (ext[2] >= 4 && ext[2] <= 127) ? static_cast<int>(ext[2])
                                 : (ext[2] == 0 ? 4 : 127);

                // Walk CEA data blocks (int arithmetic avoids uint8_t wrap-around)
                int pos = 4;
                while (pos < dtdOff && pos < 127)
                {
                    const uint8_t blkHdr = ext[pos];
                    const int     blkTag = (blkHdr >> 5) & 0x07;
                    const int     blkLen =  blkHdr       & 0x1F;
                    if (pos + 1 + blkLen > 127) break;
                    const uint8_t* bd = ext + pos + 1;
                    pos += 1 + blkLen;

                    if (blkTag == 2 && blkLen > 0)
                    {
                        // Video Data Block
                        videoSVDs = blkLen;
                        const int nativeVIC = static_cast<int>(bd[0] & 0x7F);
                        for (int vi = 0; vi < blkLen; ++vi)
                        {
                            const uint8_t vic = bd[vi] & 0x7F;
                            if (vic == 95 || vic == 96 || vic == 97) has4k = true;
                        }
                        // VIC → timing name lookup (common VICs)
                        const char* vicName = nullptr;
                        if      (nativeVIC == 1)  vicName = "640x480p@60";
                        else if (nativeVIC == 4)  vicName = "1280x720p@60";
                        else if (nativeVIC == 16) vicName = "1920x1080p@60";
                        else if (nativeVIC == 17) vicName = "720x576p@50";
                        else if (nativeVIC == 19) vicName = "1280x720p@50";
                        else if (nativeVIC == 31) vicName = "1920x1080p@50";
                        else if (nativeVIC == 32) vicName = "1920x1080p@24";
                        else if (nativeVIC == 95) vicName = "3840x2160p@30";
                        else if (nativeVIC == 96) vicName = "3840x2160p@50";
                        else if (nativeVIC == 97) vicName = "3840x2160p@60";
                        if (vicName)
                            snprintf(nativeVicStr, sizeof(nativeVicStr), "%d (%s)", nativeVIC, vicName);
                        else
                            snprintf(nativeVicStr, sizeof(nativeVicStr), "%d", nativeVIC);
                    }
                    else if (blkTag == 7 && blkLen >= 1)
                    {
                        const uint8_t etag = bd[0];
                        if (etag == 13 && blkLen >= 3)
                        {
                            // Colorimetry Data Block — CEA-861-F §7.5.5
                            const uint8_t m1 = bd[1];
                            const uint8_t m2 = bd[2];
                            std::string cmts;
                            if (m1 & 0x01) cmts += "xvYCC601 ";
                            if (m1 & 0x02) cmts += "xvYCC709 ";
                            if (m1 & 0x04) cmts += "sYCC601 ";
                            if (m1 & 0x08) cmts += "opYCC601 ";
                            if (m1 & 0x10) cmts += "opRGB ";
                            if (m1 & 0x20) cmts += "BT2020cYCC ";
                            if (m1 & 0x40) cmts += "BT2020YCC ";
                            if (m1 & 0x80) cmts += "BT2020RGB ";
                            if (m2 & 0x80) cmts += "DCI-P3 ";
                            if (!cmts.empty() && cmts.back() == ' ') cmts.pop_back();
                            colorimetry = cmts.empty() ? "none" : cmts;
                        }
                        else if (etag == 14 && blkLen >= 2)
                        {
                            // HDR Static Metadata Block — CEA-861-3 §4.2
                            const uint8_t ef = bd[1];
                            std::string eotfs;
                            if (ef & 0x01) eotfs += "SDR ";
                            if (ef & 0x02) eotfs += "HDR ";
                            if (ef & 0x04) eotfs += "HDR10/ST2084 ";
                            if (ef & 0x08) eotfs += "HLG ";
                            if (!eotfs.empty() && eotfs.back() == ' ') eotfs.pop_back();
                            hdrEotfs = eotfs.empty() ? "none" : eotfs;
                        }
                    }
                }
                break;  // process only the first CEA-861 extension
            }

            // ---- Print ASCII table ----
            // Layout: | %-22s | %-30s |
            //         +------------------------+--------------------------------+
            //          ^24 dashes               ^32 dashes  (total row = 59 chars)
            const char* kDiv = "+------------------------+--------------------------------+";
            LOGINFO("SystemDelegate: [EDID] %s", kDiv);
            LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "Field",               "Value");
            LOGINFO("SystemDelegate: [EDID] %s", kDiv);
            LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "Manufacturer",         mfr);
            LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "Product Code",          prodStr);
            LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "Year / Week",           yearStr);
            LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "EDID Version",          edidVerStr);
            LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "Interface",             ifaceFullStr);
            LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "Physical Size (cm)",    sizeStr);
            LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "Extensions",            numExtsStr);
            LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "Base Checksum",         baseCsumStr);
            LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "Monitor Name",          monNameDisp);
            if (hasCea)
            {
                char ceaRevStr[8]; snprintf(ceaRevStr, sizeof(ceaRevStr), "%d", ceaRev);
                char svdStr[8];    snprintf(svdStr,    sizeof(svdStr),    "%d", videoSVDs);
                LOGINFO("SystemDelegate: [EDID] %s", kDiv);
                LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "CEA-861 Revision",  ceaRevStr);
                LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "Underscan",         underscan  ? "Yes" : "No");
                LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "Basic Audio",       basicAudio ? "Yes" : "No");
                LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "YCbCr 4:4:4",      ycbcr444   ? "Yes" : "No");
                LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "YCbCr 4:2:2",      ycbcr422   ? "Yes" : "No");
                LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "Video SVDs",        svdStr);
                LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "Native VIC",        nativeVicStr);
                LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "4K Support",        has4k ? "Yes" : "No");
                LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "Colorimetry",       colorimetry.c_str());
                LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "HDR EOTFs",         hdrEotfs.c_str());
                LOGINFO("SystemDelegate: [EDID] | %-22s | %-30s |", "Ext Checksum",      extCsumStr);
            }
            LOGINFO("SystemDelegate: [EDID] %s", kDiv);
        }
        catch (...)
        {
            LOGERR("SystemDelegate: [EDID] LogEdidInfo caught unexpected exception — skipping EDID logging");
        }
    }

    inline std::shared_ptr<WPEFramework::Utils::JSONRPCDirectLink> AcquireLink(const std::string& callsign) const
    {
        // Create a direct JSON-RPC link to the specified Thunder plugin using the Supporting_Files helper.
        if (nullptr == _shell)
        {
            LOGERR("SystemDelegate: shell is null");
            return nullptr;
        }
        return WPEFramework::Utils::GetThunderControllerClient(_shell, callsign);
    }

    inline Exchange::ISystemServices* AcquireSystemServices() const
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_systemRegistrationLock);

        if (_systemServicesPlugin == nullptr && _shell != nullptr)
        {
            _systemServicesPlugin = 
                _shell->QueryInterfaceByCallsign<WPEFramework::Exchange::ISystemServices>(SYSTEM_CALLSIGN);
            if (_systemServicesPlugin != nullptr)
            {
                LOGINFO("SystemDelegate: Successfully acquired ISystemServices interface");
            }
            else
            {
                LOGERR("SystemDelegate: Failed to acquire ISystemServices interface");
            }
        }
        return _systemServicesPlugin;
    }
    
    static std::string ToLower(const std::string &in)
    {
        std::string out;
        out.reserve(in.size());
        for (char c : in)
        {
            out.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(c))));
        }
        return out;
    }

    static std::string MapDeviceTypeEnumToFirebolt(Exchange::IDeviceInfo::DeviceTypeInfo deviceType)
    {
        // Exchange::IDeviceInfo::DeviceTypeInfo enum → Firebolt Types.DeviceType string
        switch (deviceType)
        {
            case Exchange::IDeviceInfo::DEVICE_TYPE_IPTV:     return "tv";
            case Exchange::IDeviceInfo::DEVICE_TYPE_IPSTB:    return "stb";
            case Exchange::IDeviceInfo::DEVICE_TYPE_QAMIPSTB: return "stb";
            default:                                          return "ott";
        }
    }

    static std::string TerritoryThunderToFirebolt(const std::string &terr, const std::string &deflt)
    {
        if (EqualsIgnoreCase(terr, "USA"))
            return "US";
        if (EqualsIgnoreCase(terr, "CAN"))
            return "CA";
        if (EqualsIgnoreCase(terr, "ITA"))
            return "IT";
        if (EqualsIgnoreCase(terr, "GBR"))
            return "GB";
        if (EqualsIgnoreCase(terr, "IRL"))
            return "IE";
        if (EqualsIgnoreCase(terr, "AUS"))
            return "AU";
        if (EqualsIgnoreCase(terr, "AUT"))
            return "AT";
        if (EqualsIgnoreCase(terr, "CHE"))
            return "CH";
        if (EqualsIgnoreCase(terr, "DEU"))
            return "DE";
        return deflt;
    }

    static std::string TerritoryFireboltToThunder(const std::string &code, const std::string &deflt)
    {
        if (EqualsIgnoreCase(code, "US"))
            return "USA";
        if (EqualsIgnoreCase(code, "CA"))
            return "CAN";
        if (EqualsIgnoreCase(code, "IT"))
            return "ITA";
        if (EqualsIgnoreCase(code, "GB"))
            return "GBR";
        if (EqualsIgnoreCase(code, "IE"))
            return "IRL";
        if (EqualsIgnoreCase(code, "AU"))
            return "AUS";
        if (EqualsIgnoreCase(code, "AT"))
            return "AUT";
        if (EqualsIgnoreCase(code, "CH"))
            return "CHE";
        if (EqualsIgnoreCase(code, "DE"))
            return "DEU";
        return deflt;
    }

    static bool EqualsIgnoreCase(const std::string &a, const std::string &b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (::tolower(static_cast<unsigned char>(a[i])) != ::tolower(static_cast<unsigned char>(b[i])))
            {
                return false;
            }
        }
        return true;
    }
    // Setup subscriptions to underlying Thunder plugin events
    void SetupDisplaySettingsSubscription()
    {
        if (isDisplaySubscribed()) return;
        try {
            if (!_displayRpc) {
                _displayRpc = ::Utils::getThunderControllerClient(DISPLAYSETTINGS_CALLSIGN, CALLSIGN_CALLER_APPGATEWAY);
            }
            if (_displayRpc) {
                const uint32_t status = _displayRpc->Subscribe<WPEFramework::Core::JSON::VariantContainer>(
                    SYSTEM_DELEGATE_SUBSCRIBE_TIMEOUT_MS, _T("resolutionChanged"), &SystemDelegate::OnDisplaySettingsResolutionChanged, this);
                if (status == Core::ERROR_NONE) {
                    LOGINFO("SystemDelegate: Subscribed to %s.resolutionChanged", DISPLAYSETTINGS_CALLSIGN);
                    markDisplaySubscribed();
                } else {
                    LOGERR("SystemDelegate: Failed to subscribe to %s.resolutionChanged rc=%u", DISPLAYSETTINGS_CALLSIGN, status);
                }
            }
        } catch (...) {
            LOGERR("SystemDelegate: exception during DisplaySettings (resolution) subscription");
        }
    }

    void SetupDisplaySettingsAudioSubscription()
    {
        if (isDisplayAudioSubscribed()) return;
        try {
            if (!_displayRpc) {
                _displayRpc = ::Utils::getThunderControllerClient(DISPLAYSETTINGS_CALLSIGN, CALLSIGN_CALLER_APPGATEWAY);
            }
            if (_displayRpc) {
                const uint32_t status = _displayRpc->Subscribe<WPEFramework::Core::JSON::VariantContainer>(
                    SYSTEM_DELEGATE_SUBSCRIBE_TIMEOUT_MS, _T("audioFormatChanged"), &SystemDelegate::OnDisplaySettingsAudioFormatChanged, this);
                if (status == Core::ERROR_NONE) {
                    LOGINFO("SystemDelegate: Subscribed to %s.audioFormatChanged", DISPLAYSETTINGS_CALLSIGN);
                    markDisplayAudioSubscribed();
                } else {
                    LOGERR("SystemDelegate: Failed to subscribe to %s.audioFormatChanged rc=%u", DISPLAYSETTINGS_CALLSIGN, status);
                }
            }
        } catch (...) {
            LOGERR("SystemDelegate: exception during DisplaySettings (audio) subscription");
        }
    }

    void SetupHdcpProfileSubscription()
    {
        if (isHdcpSubscribed()) return;
        try {
            if (!_hdcpRpc) {
                _hdcpRpc = ::Utils::getThunderControllerClient(HDCPPROFILE_CALLSIGN, CALLSIGN_CALLER_APPGATEWAY);
            }
            if (_hdcpRpc) {
                const uint32_t status = _hdcpRpc->Subscribe<WPEFramework::Core::JSON::VariantContainer>(
                    SYSTEM_DELEGATE_SUBSCRIBE_TIMEOUT_MS, _T("onDisplayConnectionChanged"), &SystemDelegate::OnHdcpProfileDisplayConnectionChanged, this);
                if (status == Core::ERROR_NONE) {
                    LOGINFO("SystemDelegate: Subscribed to %s.onDisplayConnectionChanged", HDCPPROFILE_CALLSIGN);
                    markHdcpSubscribed();
                } else {
                    LOGERR("SystemDelegate: Failed to subscribe to %s.onDisplayConnectionChanged rc=%u", HDCPPROFILE_CALLSIGN, status);
                }
            }
        } catch (...) {
            LOGERR("SystemDelegate: exception during HdcpProfile subscription");
        }
    }

    void SetupSystemSub()
    {
        if (isSystemRegistered()) return;
        try {
            auto sysServices = AcquireSystemServices();
            if (sysServices) {
                const uint32_t status = sysServices->Register(&_systemServicesNotification);
                if (status == Core::ERROR_NONE) {
                    LOGINFO("SystemDelegate: Registered ISystemServices notifications (FriendlyName + TimeZone + Country)");
                    markSystemRegistered();
                } else {
                    LOGWARN("SystemDelegate: Failed to register ISystemServices notifications rc=%u", status);
                }
            }
        } catch (...) {
            LOGERR("SystemDelegate: exception during ISystemServices registration");
        }
    }

    // Event handlers invoked by Thunder JSON-RPC subscription
    void OnDisplaySettingsResolutionChanged(const WPEFramework::Core::JSON::VariantContainer& params)
    {
        (void)params;
        LOGINFO("[AppGatewayCommon|DisplaySettings.resolutionChanged] Incoming alias=%s.%s, posting to workerpool...",
                DISPLAYSETTINGS_CALLSIGN, "resolutionChanged");
        // Re-query state and dispatch debounced events on a worker-pool thread, NOT on this
        // notification thread, to avoid a blocking COM-RPC call back into the same plugin
        // that is currently dispatching this event (circular-wait/timeout risk).
        PostToWorkerPool([this]() {
            const bool screenEmitted = EmitOnScreenResolutionChanged();
            const bool videoEmitted = EmitOnVideoResolutionChanged();
            LOGINFO("[AppGatewayCommon|DisplaySettings.resolutionChanged] Handler responses: onScreenResolutionChanged=%s onVideoResolutionChanged=%s",
                    screenEmitted ? "emitted" : "skipped", videoEmitted ? "emitted" : "skipped");
        });
    }

    void OnHdcpProfileDisplayConnectionChanged(const WPEFramework::Core::JSON::VariantContainer& params)
    {
        (void)params;
        LOGINFO("[AppGatewayCommon|HdcpProfile.onDisplayConnectionChanged] Incoming alias=%s.%s, posting to workerpool...",
                HDCPPROFILE_CALLSIGN, "onDisplayConnectionChanged");
        // Re-query state and dispatch debounced events on a worker-pool thread, NOT on this
        // notification thread, to avoid a blocking COM-RPC call back into the same plugin
        // that is currently dispatching this event (circular-wait/timeout risk).
        PostToWorkerPool([this]() {
            const bool hdcpEmitted = EmitOnHdcpChanged();
            const bool hdrEmitted = EmitOnHdrChanged();
            LOGINFO("[AppGatewayCommon|HdcpProfile.onDisplayConnectionChanged] Handler responses: onHdcpChanged=%s onHdrChanged=%s",
                    hdcpEmitted ? "emitted" : "skipped", hdrEmitted ? "emitted" : "skipped");
        });
    }

    void OnSystemFriendlyNameChanged(const WPEFramework::Core::JSON::VariantContainer& params)
    {
        (void)params;
        LOGINFO("[AppGatewayCommon|System.onFriendlyNameChanged] Incoming alias=%s.%s, invoking handlers...",
                SYSTEM_CALLSIGN, "onFriendlyNameChanged");
        // Re-query state and dispatch event
        const bool nameEmitted = EmitOnNameChanged();
        LOGINFO("[AppGatewayCommon|System.onFriendlyNameChanged] Handler responses: onNameChanged=%s",
                nameEmitted ? "emitted" : "skipped");
    }

    void OnDisplaySettingsAudioFormatChanged(const WPEFramework::Core::JSON::VariantContainer& params)
    {
        (void)params;
        LOGINFO("[AppGatewayCommon|DisplaySettings.audioFormatChanged] Incoming alias=%s.%s, posting to workerpool...",
                DISPLAYSETTINGS_CALLSIGN, "audioFormatChanged");
        // Re-query state and dispatch event on a worker-pool thread, NOT on this notification
        // thread, to avoid a blocking COM-RPC call back into the same plugin that is currently
        // dispatching this event (circular-wait/timeout risk).
        PostToWorkerPool([this]() {
            const bool audioEmitted = EmitOnAudioChanged();
            LOGINFO("[AppGatewayCommon|DisplaySettings.audioFormatChanged] Handler responses: onAudioChanged=%s",
                    audioEmitted ? "emitted" : "skipped");
        });
    }

    void OnSystemTimezoneChanged(const WPEFramework::Core::JSON::VariantContainer& params)
    {
        LOGINFO("[AppGatewayCommon|System.onTimezoneChanged] Incoming alias=%s.%s, invoking handlers...",
                SYSTEM_CALLSIGN, "onTimezoneChanged");
        // Re-query state and dispatch event
        const bool timezoneEmitted = EmitOnTimezoneChanged(params);
        LOGINFO("[AppGatewayCommon|System.onTimezoneChanged] Handler responses: onTimezoneChanged=%s",
                timezoneEmitted ? "emitted" : "skipped");
    }

    void OnSystemTerritoryChanged(const WPEFramework::Core::JSON::VariantContainer& params)
    {
        LOGINFO("[AppGatewayCommon|System.onTerritoryChanged] Incoming alias=%s.%s, invoking handlers...",
                SYSTEM_CALLSIGN, "onTerritoryChanged");
        // Re-query state and dispatch event
        const bool territoryEmitted = EmitOnTerritoryChanged(params);
        LOGINFO("[AppGatewayCommon|System.onTerritoryChanged] Handler responses: onTerritoryChanged=%s",
                territoryEmitted ? "emitted" : "skipped");
    }

    bool isDisplaySubscribed() const
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_displaySubscriptionLock);
        return _displaySubscribed;
    }

    void markDisplaySubscribed()
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_displaySubscriptionLock);
        _displaySubscribed = true;
    }

    bool isDisplayAudioSubscribed() const
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_displayAudioSubscriptionLock);
        return _displayAudioSubscribed;
    }

    void markDisplayAudioSubscribed()
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_displayAudioSubscriptionLock);
        _displayAudioSubscribed = true;
    }

    bool isHdcpSubscribed() const
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_hdcpSubscriptionLock);
        return _hdcpSubscribed;
    }

     void markHdcpSubscribed()
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_hdcpSubscriptionLock);
        _hdcpSubscribed = true;
    }

    bool isSystemRegistered() const
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_systemRegistrationLock);
        return _registeredSystemEventHandlers;
    }

    void markSystemRegistered()
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_systemRegistrationLock);
        _registeredSystemEventHandlers = true;
    }


private:
    PluginHost::IShell *_shell;
    std::unordered_set<std::string> _subscriptions;
    mutable Core::CriticalSection mAdminLock;
    std::string mVersionResponse;

    // JSONRPC clients for event subscriptions
    std::shared_ptr<WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement>> _displayRpc;
    std::shared_ptr<WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement>> _hdcpRpc;

    // COM-RPC interface for SystemServices
    mutable Exchange::ISystemServices* _systemServicesPlugin;
    Core::Sink<SystemServicesNotification> _systemServicesNotification;
    bool _registeredSystemEventHandlers;

    bool _displaySubscribed;
    mutable Core::CriticalSection _displaySubscriptionLock;
    bool _displayAudioSubscribed;
    mutable Core::CriticalSection _displayAudioSubscriptionLock;
    bool _hdcpSubscribed;
    mutable Core::CriticalSection _hdcpSubscriptionLock;
    mutable Core::CriticalSection _systemRegistrationLock;
};
