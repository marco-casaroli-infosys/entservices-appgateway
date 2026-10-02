/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management
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

#include <algorithm>
#include <functional>
#include <memory>
#include <string>

#include <core/JSON.h>
#include <plugins/plugins.h>
#include <interfaces/IDisplayInfo.h>

#include "BaseEventDelegate.h"
#include "UtilsController.h"
#include "UtilsJsonrpcDirectLink.h"
#include "UtilsLogging.h"
#include "StringUtils.h"

using namespace WPEFramework;

#ifndef DISPLAYSETTINGS_CALLSIGN
#define DISPLAYSETTINGS_CALLSIGN "org.rdk.DisplaySettings"
#endif

#ifndef HDCPPROFILE_CALLSIGN
#define HDCPPROFILE_CALLSIGN "org.rdk.HdcpProfile"
#endif

#ifndef HDMICECSOURCE_CALLSIGN
#define HDMICECSOURCE_CALLSIGN "org.rdk.HdmiCecSource"
#endif

#ifndef DISPLAYINFO_CALLSIGN
#define DISPLAYINFO_CALLSIGN "DisplayInfo"
#endif

#ifndef CALLSIGN_CALLER_APPGATEWAY_VIDEOOUTPUT
#define CALLSIGN_CALLER_APPGATEWAY_VIDEOOUTPUT "org.rdk.AppGatewayCommon.VideoOutputDelegate"
#endif

#ifndef VIDEOOUTPUT_SUBSCRIBE_TIMEOUT_MS
#define VIDEOOUTPUT_SUBSCRIBE_TIMEOUT_MS 2000
#endif

class VideoOutputDelegate : public BaseEventDelegate
{
private:
    // Small helper job that runs an arbitrary std::function on a worker-pool thread.
    // Used to move re-query/emit work OFF the Thunder notification-delivery thread,
    // so we never make a blocking COM/JSON-RPC call back into the same plugin that
    // is currently dispatching the event to us (avoids circular-wait/timeout risk,
    // same root cause pattern as RDKEMW-24422).
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

public:
    // Firebolt VideoOutput event constants
    static constexpr const char* EVENT_ON_VO_RESOLUTION_CHANGED       = "VideoOutput.onResolutionChanged";
    static constexpr const char* EVENT_ON_VO_HDCP_CHANGED             = "VideoOutput.onHdcpChanged";
    static constexpr const char* EVENT_ON_VO_CEC_ACTIVE_STATE_CHANGED = "VideoOutput.onCecStateChanged";
    static constexpr const char* EVENT_ON_VO_PORT_CHANGED             = "VideoOutput.onPortChanged";
    static constexpr const char* EVENT_ON_VO_REFRESH_RATE_CHANGED     = "VideoOutput.onRefreshRateChanged";

    explicit VideoOutputDelegate(PluginHost::IShell* shell)
        : BaseEventDelegate()
        , _shell(shell)
        , _displaySettingsRpc(nullptr)
        , _hdcpProfileRpc(nullptr)
        , _hdmiCecSourceRpc(nullptr)
        , _displayInfoRpc(nullptr)
        , _resolutionChangedSubscribed(false)
        , _connectedVideoDisplaysUpdatedSubscribed(false)
        , _hdcpProfileSubscribed(false)
        , _hdmiCecSourceSubscribed(false)
        , _displayInfoSubscribed(false)
    {
    }

    ~VideoOutputDelegate()
    {
        try {
            if ((_displaySettingsRpc != nullptr) && isResolutionChangedSubscribed()) {
                _displaySettingsRpc->Unsubscribe(VIDEOOUTPUT_SUBSCRIBE_TIMEOUT_MS, _T("resolutionChanged"));
            }
            if ((_displaySettingsRpc != nullptr) && isConnectedVideoDisplaysUpdatedSubscribed()) {
                _displaySettingsRpc->Unsubscribe(VIDEOOUTPUT_SUBSCRIBE_TIMEOUT_MS, _T("connectedVideoDisplaysUpdated"));
            }
#if 0
            if ((_displayInfoRpc != nullptr) && isDisplayInfoSubscribed()) {
                _displayInfoRpc->Unsubscribe(VIDEOOUTPUT_SUBSCRIBE_TIMEOUT_MS, _T("updated"));
            }
#endif
            if ((_hdcpProfileRpc != nullptr) && isHdcpProfileSubscribed()) {
                _hdcpProfileRpc->Unsubscribe(VIDEOOUTPUT_SUBSCRIBE_TIMEOUT_MS, _T("onDisplayConnectionChanged"));
            }
            if ((_hdmiCecSourceRpc != nullptr) && isHdmiCecSourceSubscribed()) {
                _hdmiCecSourceRpc->Unsubscribe(VIDEOOUTPUT_SUBSCRIBE_TIMEOUT_MS, _T("onActiveSourceStatusUpdated"));
            }
        } catch (...) {
        }
        _displaySettingsRpc.reset();
        _hdcpProfileRpc.reset();
        _hdmiCecSourceRpc.reset();
        _displayInfoRpc.reset();
        _shell = nullptr;
    }

    bool isTVPanel()
    {
        std::string port;
        // Prefer the explicit w/h or port returned by the DisplaySettings API.
        if (Core::ERROR_NONE == GetVideoOutputPort(port)) {
            // Port strings are returned with quotes, e.g. "internal". Normalize.
            if (!port.empty() && port.front() == '"' && port.back() == '"') {
                port = port.substr(1, port.size() - 2);
            }
            port = StringUtils::toLower(port);
            return (port == "internal");
        }
        return false;
    }

    // ─── PUBLIC API: 10 Firebolt VideoOutput getters ───────────────────────

    // AC1: VideoOutput.resolution
    Core::hresult GetVideoOutputResolution(std::string& result)
    {
        result = "{\"width\":0,\"height\":0}";
        auto link = AcquireLink(DISPLAYSETTINGS_CALLSIGN);
        if (nullptr == link) {
            LOGERR("VideoOutputDelegate: DisplaySettings link unavailable for GetVideoOutputResolution");
            return Core::ERROR_NONE;
        }

        Core::JSON::VariantContainer params;
        Core::JSON::VariantContainer response;
        const uint32_t rc = link->Invoke<decltype(params), decltype(response)>("getCurrentResolution", params, response);
        if (Core::ERROR_NONE != rc) {
            LOGERR("VideoOutputDelegate: getCurrentResolution failed rc=%u", rc);
            return Core::ERROR_NONE;
        }

        int w = 0, h = 0;
        if (response.HasLabel(_T("w")) && response.HasLabel(_T("h"))) {
            w = static_cast<int>(response.Get(_T("w")).Number());
            h = static_cast<int>(response.Get(_T("h")).Number());
        } else if (response.HasLabel(_T("resolution"))) {
            std::string res = response.Get(_T("resolution")).String();
            ParseResolutionString(res, w, h);
        }

        result = "{\"width\":" + std::to_string(w) + ",\"height\":" + std::to_string(h) + "}";
        LOGDBG("VideoOutputDelegate: GetVideoOutputResolution -> %s", result.c_str());
        return Core::ERROR_NONE;
    }

    // AC2: VideoOutput.hdcp
    Core::hresult GetVideoOutputHdcp(std::string& result)
    {
        result = "\"none\"";
        // TV device detection → return "\"direct\""
        if (true == isTVPanel())
        {
            result = "\"direct\"";
            return Core::ERROR_NONE;
        }
        auto link = AcquireLink(HDCPPROFILE_CALLSIGN);
        if (nullptr == link) {
            LOGERR("VideoOutputDelegate: HdcpProfile link unavailable for GetVideoOutputHdcp");
            return Core::ERROR_NONE;
        }

        Core::JSON::VariantContainer params;
        Core::JSON::VariantContainer response;
        const uint32_t rc = link->Invoke<decltype(params), decltype(response)>("getHDCPStatus", params, response);
        if (Core::ERROR_NONE != rc) {
            LOGERR("VideoOutputDelegate: getHDCPStatus failed rc=%u", rc);
            return Core::ERROR_NONE;
        }

        (void)ExtractHdcpValue(response, result);
        LOGDBG("VideoOutputDelegate: GetVideoOutputHdcp -> %s", result.c_str());
        return Core::ERROR_NONE;
    }

    // AC3: VideoOutput.cecActiveState
    Core::hresult GetVideoOutputCecActiveState(std::string& result)
    {
        result = "\"unsupported\"";
        //To handle the TV Panel usecases
        if (true == isTVPanel())
        {
            result = "\"Wrong device class\"";
            return Core::ERROR_UNAVAILABLE;
        }
        auto link = AcquireLink(HDMICECSOURCE_CALLSIGN);
        if (nullptr == link) {
            LOGERR("VideoOutputDelegate: HdmiCecSource link unavailable for GetVideoOutputCecActiveState");
            return Core::ERROR_NONE;
        }

        // Step 1: Check if CEC is enabled
        Core::JSON::VariantContainer params;
        Core::JSON::VariantContainer enabledResp;
        uint32_t rc = link->Invoke<decltype(params), decltype(enabledResp)>("getEnabled", params, enabledResp);
        if (Core::ERROR_NONE != rc) {
            LOGERR("VideoOutputDelegate: getEnabled failed rc=%u", rc);
            return Core::ERROR_NONE; // return "unsupported"
        }

        bool enabled = false;
        if (enabledResp.HasLabel(_T("enabled"))) {
            enabled = enabledResp.Get(_T("enabled")).Boolean();
        }
        if (false == enabled) {
            return Core::ERROR_NONE; // return "unsupported"
        }

        // Step 2: Check active source status
        Core::JSON::VariantContainer statusResp;
        rc = link->Invoke<decltype(params), decltype(statusResp)>("getActiveSourceStatus", params, statusResp);
        if (Core::ERROR_NONE != rc) {
            LOGERR("VideoOutputDelegate: getActiveSourceStatus failed rc=%u", rc);
            return Core::ERROR_NONE; // return "unsupported"
        }

        if (statusResp.HasLabel(_T("status"))) {
            result = statusResp.Get(_T("status")).Boolean() ? "\"active\"" : "\"inactive\"";
        }

        LOGDBG("VideoOutputDelegate: GetVideoOutputCecActiveState -> %s", result.c_str());
        return Core::ERROR_NONE;
    }

    // AC4: VideoOutput.port
    Core::hresult GetVideoOutputPort(std::string& result)
    {
        result = "\"none\"";
        auto link = AcquireLink(DISPLAYSETTINGS_CALLSIGN);
        if (nullptr == link) {
            LOGERR("VideoOutputDelegate: DisplaySettings link unavailable for GetVideoOutputPort");
            return Core::ERROR_NONE;
        }

        Core::JSON::VariantContainer params;
        Core::JSON::VariantContainer response;
        const uint32_t rc = link->Invoke<decltype(params), decltype(response)>("getConnectedVideoDisplays", params, response);
        if (Core::ERROR_NONE != rc) {
            LOGERR("VideoOutputDelegate: getConnectedVideoDisplays failed rc=%u", rc);
            return Core::ERROR_NONE;
        }

        if (response.HasLabel(_T("connectedVideoDisplays"))) {
            auto displays = response.Get(_T("connectedVideoDisplays"));
            if (displays.Content() == Core::JSON::Variant::type::ARRAY) {
                auto arr = displays.Array();
                if (arr.Length() > 0) {
                    std::string portName = arr[0].String();
                    std::string lowerPort = StringUtils::toLower(portName);
                    if (lowerPort.find("hdmi") != std::string::npos) {
                        result = "\"hdmi\"";
                    } else if (lowerPort.find("internal") != std::string::npos) {
                        result = "\"internal\"";
                    }
                }
            }
        }

        LOGDBG("VideoOutputDelegate: GetVideoOutputPort -> %s", result.c_str());
        return Core::ERROR_NONE;
    }

    // AC5: VideoOutput.refreshRate (COM-RPC)
    Core::hresult GetVideoOutputRefreshRate(std::string& result)
    {
        result = "0";
        if (nullptr == _shell) {
            LOGERR("VideoOutputDelegate: shell is null for GetVideoOutputRefreshRate");
            return Core::ERROR_NONE;
        }

        auto* displayProps = _shell->QueryInterfaceByCallsign<Exchange::IDisplayProperties>(DISPLAYINFO_CALLSIGN);
        if (nullptr == displayProps) {
            LOGWARN("VideoOutputDelegate: IDisplayProperties unavailable for refreshRate");
            return Core::ERROR_NONE;
        }

        Exchange::IDisplayProperties::FrameRateType rate{};
        const Core::hresult rc = displayProps->FrameRate(rate);
        displayProps->Release();

        if (Core::ERROR_NONE != rc) {
            LOGWARN("VideoOutputDelegate: FrameRate() failed rc=%u", rc);
            return Core::ERROR_NONE;
        }

        switch (rate) {
            case Exchange::IDisplayProperties::FRAMERATE_23_976: result = "23.976"; break;
            case Exchange::IDisplayProperties::FRAMERATE_24:     result = "24"; break;
            case Exchange::IDisplayProperties::FRAMERATE_25:     result = "25"; break;
            case Exchange::IDisplayProperties::FRAMERATE_29_97:  result = "29.97"; break;
            case Exchange::IDisplayProperties::FRAMERATE_30:     result = "30"; break;
            case Exchange::IDisplayProperties::FRAMERATE_50:     result = "50"; break;
            case Exchange::IDisplayProperties::FRAMERATE_59_94:  result = "59.94"; break;
            case Exchange::IDisplayProperties::FRAMERATE_60:     result = "60"; break;
            case Exchange::IDisplayProperties::FRAMERATE_UNKNOWN:
            default: result = "0"; break;
        }

        LOGDBG("VideoOutputDelegate: GetVideoOutputRefreshRate -> %s", result.c_str());
        return Core::ERROR_NONE;
    }

    // AC6: VideoOutput.colorDepth (COM-RPC)
    Core::hresult GetVideoOutputColorDepth(std::string& result)
    {
        result = "0";
        if (nullptr == _shell) {
            LOGERR("VideoOutputDelegate: shell is null for GetVideoOutputColorDepth");
            return Core::ERROR_NONE;
        }

        auto* displayProps = _shell->QueryInterfaceByCallsign<Exchange::IDisplayProperties>(DISPLAYINFO_CALLSIGN);
        if (nullptr == displayProps) {
            LOGWARN("VideoOutputDelegate: IDisplayProperties unavailable for colorDepth");
            return Core::ERROR_NONE;
        }

        Exchange::IDisplayProperties::ColourDepthType depth{};
        const Core::hresult rc = displayProps->ColourDepth(depth);
        displayProps->Release();

        if (Core::ERROR_NONE != rc) {
            LOGWARN("VideoOutputDelegate: ColourDepth() failed rc=%u", rc);
            return Core::ERROR_NONE;
        }

        switch (depth) {
            case Exchange::IDisplayProperties::COLORDEPTH_8_BIT:  result = "8"; break;
            case Exchange::IDisplayProperties::COLORDEPTH_10_BIT: result = "10"; break;
            case Exchange::IDisplayProperties::COLORDEPTH_12_BIT: result = "12"; break;
            case Exchange::IDisplayProperties::COLORDEPTH_UNKNOWN:
            default: result = "0"; break;
        }

        LOGDBG("VideoOutputDelegate: GetVideoOutputColorDepth -> %s", result.c_str());
        return Core::ERROR_NONE;
    }

    // AC7: VideoOutput.colorFormat (JSON-RPC)
    Core::hresult GetVideoOutputColorFormat(std::string& result)
    {
        result = "\"none\"";
        if (nullptr == _shell) {
            LOGERR("VideoOutputDelegate: shell is null for GetVideoOutputColorDepth");
            return Core::ERROR_NONE;
        }

        auto* displayProps = _shell->QueryInterfaceByCallsign<Exchange::IDisplayProperties>(DISPLAYINFO_CALLSIGN);
        if (nullptr == displayProps) {
            LOGWARN("VideoOutputDelegate: IDisplayProperties unavailable for colorDepth");
            return Core::ERROR_NONE;
        }

        Exchange::IDisplayProperties::ColourSpaceType cs{};
        const Core::hresult rc = displayProps->ColorSpace(cs);
        displayProps->Release();

        if (Core::ERROR_NONE != rc) {
            LOGWARN("VideoOutputDelegate: ColorSpace() failed rc=%u", rc);
            return Core::ERROR_NONE;
        }

        switch(cs)
        {
            case Exchange::IDisplayProperties::FORMAT_RGB_444:
                result = "\"rgb444\"";
                break;
            case Exchange::IDisplayProperties::FORMAT_YCBCR_444:
                result = "\"ycbcr444\"";
                break;
            case Exchange::IDisplayProperties::FORMAT_YCBCR_422:
                result = "\"ycbcr422\"";
                break;
            case Exchange::IDisplayProperties::FORMAT_YCBCR_420:
                result = "\"ycbcr420\"";
                break;
            case Exchange::IDisplayProperties::FORMAT_OTHER:
            case Exchange::IDisplayProperties::FORMAT_UNKNOWN:
            default:
                result = "\"none\"";
        }

        LOGDBG("VideoOutputDelegate: GetVideoOutputColorFormat -> %s", result.c_str());
        return Core::ERROR_NONE;
    }

    // AC8: VideoOutput.colorimetry (JSON-RPC)
    Core::hresult GetVideoOutputColorimetry(std::string& result)
    {
        result = "\"none\"";

        if (nullptr == _shell) {
            LOGERR("VideoOutputDelegate: shell is null for GetVideoOutputColorDepth");
            return Core::ERROR_NONE;
        }

        auto* displayProps = _shell->QueryInterfaceByCallsign<Exchange::IDisplayProperties>(DISPLAYINFO_CALLSIGN);
        if (nullptr == displayProps) {
            LOGWARN("VideoOutputDelegate: IDisplayProperties unavailable for colorDepth");
            return Core::ERROR_NONE;
        }

        Exchange::IDisplayProperties::ColorimetryTypeInfo Colourimetry;
        const Core::hresult rc = displayProps->GetCurrentColorimetry(Colourimetry);
        displayProps->Release();

        if (Core::ERROR_NONE != rc) {
            LOGWARN("VideoOutputDelegate: GetCurrentColorimetry() failed rc=%u", rc);
            return Core::ERROR_NONE;
        }

        switch(Colourimetry.colorimetry)
        {
            case Exchange::IDisplayProperties::COLORIMETRY_BT2020RGB_YCBCR:
                result = "\"bt2020rgb\"";
                break;
            case Exchange::IDisplayProperties::COLORIMETRY_BT2020YCCBCBRC:
                result = "\"bt2020ycc\"";
                break;
            case Exchange::IDisplayProperties::COLORIMETRY_BT709:
                result = "\"bt709\"";
                break;
            case Exchange::IDisplayProperties::COLORIMETRY_OPRGB:
                result = "\"oprgb\"";
                break;
            case Exchange::IDisplayProperties::COLORIMETRY_XVYCC601:
            case Exchange::IDisplayProperties::COLORIMETRY_XVYCC709:
            case Exchange::IDisplayProperties::COLORIMETRY_SMPTE170M:
            case Exchange::IDisplayProperties::COLORIMETRY_SYCC601:
            case Exchange::IDisplayProperties::COLORIMETRY_OPYCC601:
            case Exchange::IDisplayProperties::COLORIMETRY_OTHER:
            case Exchange::IDisplayProperties::COLORIMETRY_UNKNOWN:
            default:
                result = "\"none\"";
        }

        LOGDBG("VideoOutputDelegate: GetVideoOutputColorimetry -> %s", result.c_str());
        return Core::ERROR_NONE;
    }

    // AC9: VideoOutput.dynamicRange (JSON-RPC)
    Core::hresult GetVideoOutputDynamicRange(std::string& result)
    {
        result = "\"none\"";
        auto link = AcquireLink(DISPLAYSETTINGS_CALLSIGN);
        if (nullptr == link) {
            LOGERR("VideoOutputDelegate: DisplaySettings link unavailable for GetVideoOutputDynamicRange");
            return Core::ERROR_NONE;
        }

        Core::JSON::VariantContainer params;
        Core::JSON::VariantContainer response;
        const uint32_t rc = link->Invoke<decltype(params), decltype(response)>("getVideoFormat", params, response);
        if (Core::ERROR_NONE != rc) {
            LOGERR("VideoOutputDelegate: getVideoFormat failed rc=%u", rc);
            return Core::ERROR_NONE;
        }

        if (response.HasLabel(_T("currentVideoFormat"))) {
            std::string fmt = response.Get(_T("currentVideoFormat")).String();
            if (fmt == "HDR10")          result = "\"hdr10\"";
            else if (fmt == "HDR10PLUS") result = "\"hdr10plus\"";
            else if (fmt == "DV")        result = "\"dolbyVision\"";
            else if (fmt == "HLG")       result = "\"hlg\"";
            else if (fmt == "SDR")       result = "\"sdr\"";
        }

        LOGDBG("VideoOutputDelegate: GetVideoOutputDynamicRange -> %s", result.c_str());
        return Core::ERROR_NONE;
    }

    // AC10: VideoOutput.quantizationRange (JSON-RPC)
    Core::hresult GetVideoOutputQuantizationRange(std::string& result)
    {
        result = "\"none\"";
        if (nullptr == _shell) {
            LOGERR("VideoOutputDelegate: shell is null for GetVideoOutputColorDepth");
            return Core::ERROR_NONE;
        }

        auto* displayProps = _shell->QueryInterfaceByCallsign<Exchange::IDisplayProperties>(DISPLAYINFO_CALLSIGN);
        if (nullptr == displayProps) {
            LOGWARN("VideoOutputDelegate: IDisplayProperties unavailable for colorDepth");
            return Core::ERROR_NONE;
        }

        Exchange::IDisplayProperties::QuantizationRangeType quantizationRange;
        const Core::hresult rc = displayProps->QuantizationRange(quantizationRange);
        displayProps->Release();

        if (Core::ERROR_NONE != rc) {
            LOGWARN("VideoOutputDelegate: QuantizationRange() failed rc=%u", rc);
            return Core::ERROR_NONE;
        }

        if (quantizationRange == Exchange::IDisplayProperties::QUANTIZATIONRANGE_LIMITED) {
            result = "\"limited\"";
        } else if (quantizationRange == Exchange::IDisplayProperties::QUANTIZATIONRANGE_FULL) {
            result = "\"full\"";
        } else if (quantizationRange == Exchange::IDisplayProperties::QUANTIZATIONRANGE_UNKNOWN) {
            result = "\"none\"";
        }
        LOGDBG("VideoOutputDelegate: GetVideoOutputQuantizationRange -> %s", result.c_str());
        return Core::ERROR_NONE;
    }

    // ─── EVENT EMITTERS ───────────────────────────────────────────────────

    bool EmitOnResolutionChanged()
    {
        std::string payload;
        if (Core::ERROR_NONE != GetVideoOutputResolution(payload)) {
            LOGERR("[AppGatewayCommon|VideoOutput.onResolutionChanged] GetVideoOutputResolution failed");
            return false;
        }
        return Dispatch(EVENT_ON_VO_RESOLUTION_CHANGED, payload);
    }

    bool EmitOnHdcpChanged()
    {
        std::string payload;
        if (Core::ERROR_NONE != GetVideoOutputHdcp(payload)) {
            LOGERR("[AppGatewayCommon|VideoOutput.onHdcpChanged] GetVideoOutputHdcp failed");
            return false;
        }
        return Dispatch(EVENT_ON_VO_HDCP_CHANGED, payload);
    }

    bool EmitOnCecActiveStateChanged()
    {
        std::string payload;
        if (Core::ERROR_NONE != GetVideoOutputCecActiveState(payload)) {
            LOGERR("[AppGatewayCommon|VideoOutput.onCecStateChanged] GetVideoOutputCecActiveState failed");
            return false;
        }
        return Dispatch(EVENT_ON_VO_CEC_ACTIVE_STATE_CHANGED, payload);
    }

    bool EmitOnPortChanged()
    {
        std::string payload;
        if (Core::ERROR_NONE != GetVideoOutputPort(payload)) {
            LOGERR("[AppGatewayCommon|VideoOutput.onPortChanged] GetVideoOutputPort failed");
            return false;
        }
        return Dispatch(EVENT_ON_VO_PORT_CHANGED, payload);
    }

#if 0
    bool EmitOnRefreshRateChanged()
    {
        std::string payload;
        if (Core::ERROR_NONE != GetVideoOutputRefreshRate(payload)) {
            LOGERR("[AppGatewayCommon|VideoOutput.onRefreshRateChanged] GetVideoOutputRefreshRate failed");
            return false;
        }
        return Dispatch(EVENT_ON_VO_REFRESH_RATE_CHANGED, payload);
    }
#endif

    // ─── HandleEvent (BaseEventDelegate) ──────────────────────────────────

    bool HandleEvent(Exchange::IAppNotificationHandler::IEmitter* cb, const std::string& event, const bool listen, bool& registrationError) override
    {
        registrationError = false;
        const std::string evLower = StringUtils::toLower(event);

        if (evLower == "videooutput.onresolutionchanged") {
            SetupDisplaySettingsSubscription();
        } else if (evLower == "videooutput.onhdcpchanged") {
            SetupHdcpProfileSubscription();
        } else if (evLower == "videooutput.oncecstatechanged") {
            SetupHdmiCecSourceSubscription();
        } else if (evLower == "videooutput.onportchanged") {
            SetupDisplaySettingsSubscription();
#if 0
        } else if (evLower == "videooutput.onrefreshratechanged") {
            SetupDisplayInfoSubscription();
#endif
        } else {
            registrationError = true;
            return false;
        }

        if (!registrationError) {
            LOGINFO("[AppGatewayCommon|VideoOutput|EventRegistration] event=%s listen=%s", event.c_str(), listen ? "true" : "false");
            if (true == listen) {
                AddNotification(event, cb);
            } else {
                RemoveNotification(event, cb);
            }
        }

        return true;
    }

private:
    // ─── Resolution string parser ─────────────────────────────────────────

    static void ParseResolutionString(const std::string& res, int& w, int& h)
    {
        // Map common named resolutions to {width, height}
        std::string lower = StringUtils::toLower(res);
        // First try WxH (e.g. "3840x2160")
        auto xpos = lower.find('x');
        if (xpos != std::string::npos) {
            try {
                w = std::stoi(lower.substr(0, xpos));
                h = std::stoi(lower.substr(xpos + 1));
                return;
            } catch (...) {
                w = 0; h = 0;
                return;
            }
        }

        // Otherwise extract the leading numeric height (e.g. "1080p60" -> 1080)
        size_t pos = 0;
        while (pos < lower.size() && !std::isdigit(static_cast<unsigned char>(lower[pos]))) {
            ++pos;
        }
        if (pos == lower.size()) {
            w = 0; h = 0;
            return;
        }

        size_t end = pos;
        while (end < lower.size() && std::isdigit(static_cast<unsigned char>(lower[end]))) {
            ++end;
        }

        std::string heightStr = lower.substr(pos, end - pos);
        if (heightStr.empty()) {
            w = 0; h = 0;
            return;
        }

        try {
            int height = std::stoi(heightStr);
            switch (height) {
                case 480:  w = 720;  h = 480;  break;
                case 576:  w = 720;  h = 576;  break;
                case 720:  w = 1280; h = 720;  break;
                case 1080: w = 1920; h = 1080; break;
                case 2160: w = 3840; h = 2160; break;
                default:   w = 0;    h = 0;    break;
            }
        } catch (...) {
            w = 0; h = 0;
        }
    }

    // ─── HDCP extraction helper ───────────────────────────────────────────

    static bool ExtractHdcpValue(const Core::JSON::VariantContainer& response, std::string& result)
    {
        // Try nested "HDCPStatus" object
        Core::JSON::VariantContainer statusObj;
        bool hasStatus = false;

        if (response.HasLabel(_T("HDCPStatus"))) {
            auto s = response.Get(_T("HDCPStatus"));
            if (s.Content() == Core::JSON::Variant::type::OBJECT) {
                statusObj = s.Object();
                hasStatus = true;
            }
        } else if (response.HasLabel(_T("result"))) {
            auto r = response.Get(_T("result"));
            if (r.Content() == Core::JSON::Variant::type::OBJECT) {
                auto inner = r.Object();
                if (inner.HasLabel(_T("HDCPStatus"))) {
                    auto s = inner.Get(_T("HDCPStatus"));
                    if (s.Content() == Core::JSON::Variant::type::OBJECT) {
                        statusObj = s.Object();
                        hasStatus = true;
                    }
                }
            }
        }

        if (false == hasStatus) {
            return false;
        }

        bool isConnected = false;
        bool isHDCPEnabled = false;

        if (statusObj.HasLabel(_T("isConnected"))) {
            isConnected = statusObj.Get(_T("isConnected")).Boolean();
        }
        if (statusObj.HasLabel(_T("isHDCPEnabled"))) {
            isHDCPEnabled = statusObj.Get(_T("isHDCPEnabled")).Boolean();
        }

        if (isConnected && isHDCPEnabled) {
            if (statusObj.HasLabel(_T("currentHDCPVersion"))) {
                std::string version = statusObj.Get(_T("currentHDCPVersion")).String();
                if (version == "2.2") {
                    result = "\"hdcp2.2\"";
                } else if (version == "1.4") {
                    result = "\"hdcp1.4\"";
                }
            }
        }
        // else result stays "\"none\""

        return true;
    }

    // ─── Thunder event subscriptions ──────────────────────────────────────

    void SetupDisplaySettingsSubscription()
    {
        if ((true == isResolutionChangedSubscribed()) && (true == isConnectedVideoDisplaysUpdatedSubscribed())) {
            return;
        }

        try {
            if (nullptr == _displaySettingsRpc) {
                _displaySettingsRpc = ::Utils::getThunderControllerClient(DISPLAYSETTINGS_CALLSIGN, CALLSIGN_CALLER_APPGATEWAY_VIDEOOUTPUT);
            }

            if (nullptr != _displaySettingsRpc) {
                if (false == isResolutionChangedSubscribed()) {
                    const uint32_t status = _displaySettingsRpc->Subscribe<Core::JSON::VariantContainer>(
                        VIDEOOUTPUT_SUBSCRIBE_TIMEOUT_MS,
                        _T("resolutionChanged"),
                        &VideoOutputDelegate::OnResolutionChanged,
                        this);

                    if (Core::ERROR_NONE == status) {
                        markResolutionChangedSubscribed();
                    } else {
                        LOGERR("VideoOutputDelegate: Failed to subscribe to resolutionChanged rc=%u", status);
                    }
                }

                if (false == isConnectedVideoDisplaysUpdatedSubscribed()) {
                    const uint32_t status = _displaySettingsRpc->Subscribe<Core::JSON::VariantContainer>(
                        VIDEOOUTPUT_SUBSCRIBE_TIMEOUT_MS,
                        _T("connectedVideoDisplaysUpdated"),
                        &VideoOutputDelegate::OnConnectedVideoDisplaysUpdated,
                        this);

                    if (Core::ERROR_NONE == status) {
                        markConnectedVideoDisplaysUpdatedSubscribed();
                    } else {
                        LOGERR("VideoOutputDelegate: Failed to subscribe to connectedVideoDisplaysUpdated rc=%u", status);
                    }
                }

                LOGINFO("VideoOutputDelegate: Subscribed to %s events", DISPLAYSETTINGS_CALLSIGN);
            }
        } catch (...) {
            LOGERR("VideoOutputDelegate: exception during DisplaySettings subscription");
        }
    }

#if 0
    void SetupDisplayInfoSubscription()
    {
        if (true == isDisplayInfoSubscribed()) {
            return;
        }
        try {
            if (nullptr == _displayInfoRpc) {
                _displayInfoRpc = ::Utils::getThunderControllerClient(DISPLAYINFO_CALLSIGN, CALLSIGN_CALLER_APPGATEWAY_VIDEOOUTPUT);
            }

            if (nullptr != _displayInfoRpc) {
                const uint32_t status = _displayInfoRpc->Subscribe<Core::JSON::VariantContainer>(
                    VIDEOOUTPUT_SUBSCRIBE_TIMEOUT_MS,
                    _T("updated"),
                    &VideoOutputDelegate::OnDisplayInfoUpdated,
                    this);

                if (Core::ERROR_NONE == status) {
                    markDisplayInfoSubscribed();
                    LOGINFO("VideoOutputDelegate: Subscribed to %s.updated", DISPLAYINFO_CALLSIGN);
                } else {
                    LOGERR("VideoOutputDelegate: Failed to subscribe to DisplayInfo.updated rc=%u", status);
                }
            }
        } catch (...) {
            LOGERR("VideoOutputDelegate: exception during DisplayInfo subscription");
        }
    }
#endif
    void SetupHdcpProfileSubscription()
    {
        if (true == isHdcpProfileSubscribed()) {
            return;
        }

        try {
            if (nullptr == _hdcpProfileRpc) {
                _hdcpProfileRpc = ::Utils::getThunderControllerClient(HDCPPROFILE_CALLSIGN, CALLSIGN_CALLER_APPGATEWAY_VIDEOOUTPUT);
            }

            if (nullptr != _hdcpProfileRpc) {
                const uint32_t status = _hdcpProfileRpc->Subscribe<Core::JSON::VariantContainer>(
                    VIDEOOUTPUT_SUBSCRIBE_TIMEOUT_MS,
                    _T("onDisplayConnectionChanged"),
                    &VideoOutputDelegate::OnDisplayConnectionChanged,
                    this);

                if (Core::ERROR_NONE == status) {
                    markHdcpProfileSubscribed();
                    LOGINFO("VideoOutputDelegate: Subscribed to %s.onDisplayConnectionChanged", HDCPPROFILE_CALLSIGN);
                } else {
                    LOGERR("VideoOutputDelegate: Failed to subscribe to onDisplayConnectionChanged rc=%u", status);
                }
            }
        } catch (...) {
            LOGERR("VideoOutputDelegate: exception during HdcpProfile subscription");
        }
    }

    void SetupHdmiCecSourceSubscription()
    {
        if (true == isHdmiCecSourceSubscribed()) {
            return;
        }

        try {
            if (nullptr == _hdmiCecSourceRpc) {
                _hdmiCecSourceRpc = ::Utils::getThunderControllerClient(HDMICECSOURCE_CALLSIGN, CALLSIGN_CALLER_APPGATEWAY_VIDEOOUTPUT);
            }

            if (nullptr != _hdmiCecSourceRpc) {
                const uint32_t status = _hdmiCecSourceRpc->Subscribe<Core::JSON::VariantContainer>(
                    VIDEOOUTPUT_SUBSCRIBE_TIMEOUT_MS,
                    _T("onActiveSourceStatusUpdated"),
                    &VideoOutputDelegate::OnActiveSourceStatusUpdated,
                    this);

                if (Core::ERROR_NONE == status) {
                    markHdmiCecSourceSubscribed();
                    LOGINFO("VideoOutputDelegate: Subscribed to %s.onActiveSourceStatusUpdated", HDMICECSOURCE_CALLSIGN);
                } else {
                    LOGERR("VideoOutputDelegate: Failed to subscribe to onActiveSourceStatusUpdated rc=%u", status);
                }
            }
        } catch (...) {
            LOGERR("VideoOutputDelegate: exception during HdmiCecSource subscription");
        }
    }

    // ─── Thunder event handlers ───────────────────────────────────────────

    void OnResolutionChanged(const Core::JSON::VariantContainer& params)
    {
        (void)params;
        LOGINFO("[AppGatewayCommon|DisplaySettings.resolutionChanged] Incoming, posting to workerpool...");
        PostToWorkerPool([this]() {
            (void)EmitOnResolutionChanged();
        });
    }

    void OnDisplayConnectionChanged(const Core::JSON::VariantContainer& params)
    {
        (void)params;
        LOGINFO("[AppGatewayCommon|HdcpProfile.onDisplayConnectionChanged] Incoming, posting to workerpool...");
        PostToWorkerPool([this]() {
            (void)EmitOnHdcpChanged();
        });
    }

    void OnActiveSourceStatusUpdated(const Core::JSON::VariantContainer& params)
    {
        (void)params;
        LOGINFO("[AppGatewayCommon|HdmiCecSource.onActiveSourceStatusUpdated] Incoming, posting to workerpool...");
        PostToWorkerPool([this]() {
            (void)EmitOnCecActiveStateChanged();
        });
    }

    void OnConnectedVideoDisplaysUpdated(const Core::JSON::VariantContainer& params)
    {
        (void)params;
        LOGINFO("[AppGatewayCommon|DisplaySettings.connectedVideoDisplaysUpdated] Incoming, posting to workerpool...");
        PostToWorkerPool([this]() {
            (void)EmitOnPortChanged();
        });
    }

#if 0
    void OnDisplayInfoUpdated(const Core::JSON::VariantContainer& params)
    {
        (void)params;
        LOGINFO("[AppGatewayCommon|DisplayInfo.updated] Incoming, re-querying refreshRate and colorimetry...");
        (void)EmitOnRefreshRateChanged();
        std::string colorimetry;
        (void)GetVideoOutputColorimetry(colorimetry);
    }
#endif

    // ─── Subscription state tracking ──────────────────────────────────────

    bool isResolutionChangedSubscribed() const
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_subscriptionLock);
        return _resolutionChangedSubscribed;
    }

    void markResolutionChangedSubscribed()
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_subscriptionLock);
        _resolutionChangedSubscribed = true;
    }

    bool isConnectedVideoDisplaysUpdatedSubscribed() const
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_subscriptionLock);
        return _connectedVideoDisplaysUpdatedSubscribed;
    }

    void markConnectedVideoDisplaysUpdatedSubscribed()
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_subscriptionLock);
        _connectedVideoDisplaysUpdatedSubscribed = true;
    }

    bool isHdcpProfileSubscribed() const
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_subscriptionLock);
        return _hdcpProfileSubscribed;
    }

    void markHdcpProfileSubscribed()
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_subscriptionLock);
        _hdcpProfileSubscribed = true;
    }

    bool isHdmiCecSourceSubscribed() const
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_subscriptionLock);
        return _hdmiCecSourceSubscribed;
    }

    void markHdmiCecSourceSubscribed()
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_subscriptionLock);
        _hdmiCecSourceSubscribed = true;
    }
#if 0
    bool isDisplayInfoSubscribed() const
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_subscriptionLock);
        return _displayInfoSubscribed;
    }

    void markDisplayInfoSubscribed()
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_subscriptionLock);
        _displayInfoSubscribed = true;
    }
#endif
    // ─── JSON-RPC link acquisition ────────────────────────────────────────

    std::shared_ptr<WPEFramework::Utils::JSONRPCDirectLink> AcquireLink(const std::string& callsign) const
    {
        if (nullptr == _shell) {
            LOGERR("VideoOutputDelegate: shell is null");
            return nullptr;
        }
        return WPEFramework::Utils::GetThunderControllerClient(_shell, callsign);
    }

private:
    PluginHost::IShell* _shell;
    std::shared_ptr<WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement>> _displaySettingsRpc;
    std::shared_ptr<WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement>> _hdcpProfileRpc;
    std::shared_ptr<WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement>> _hdmiCecSourceRpc;
    std::shared_ptr<WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement>> _displayInfoRpc;
    bool _resolutionChangedSubscribed;
    bool _connectedVideoDisplaysUpdatedSubscribed;
    bool _hdcpProfileSubscribed;
    bool _hdmiCecSourceSubscribed;
    bool _displayInfoSubscribed;
    mutable Core::CriticalSection _subscriptionLock;
};
