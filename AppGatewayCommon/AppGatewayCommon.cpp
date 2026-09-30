/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
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
 */

#include "AppGatewayCommon.h"
#include <interfaces/IConfiguration.h>
#include "StringUtils.h"
#include "UtilsAppGatewayTelemetry.h"
#include "UtilsJsonValidation.h"
#include "ContextUtils.h"


#define API_VERSION_NUMBER_MAJOR    APPGATEWAYCOMMON_MAJOR_VERSION
#define API_VERSION_NUMBER_MINOR    APPGATEWAYCOMMON_MINOR_VERSION
#define API_VERSION_NUMBER_PATCH    APPGATEWAYCOMMON_PATCH_VERSION

AGW_DEFINE_TELEMETRY_CLIENT(AGW_PLUGIN_APPGATEWAYCOMMON)

namespace WPEFramework {

namespace {
    static Plugin::Metadata<Plugin::AppGatewayCommon> metadata(
        // Version (Major, Minor, Patch)
        API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH,
        // Preconditions
        {},
        // Terminations
        {},
        // Controls
        {}
    );
}

namespace Plugin {

    SERVICE_REGISTRATION(AppGatewayCommon, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

    AppGatewayCommon::AppGatewayCommon(): mShell(nullptr), mConnectionId(0)
    {
        SYSLOG(Logging::Startup, (_T("AppGatewayCommon Constructor")));
    }

    AppGatewayCommon::~AppGatewayCommon()
    {
        SYSLOG(Logging::Shutdown, (string(_T("AppGatewayCommon Destructor"))));
    }

    /* virtual */ const string AppGatewayCommon::Initialize(PluginHost::IShell* service)
    {
        ASSERT(service != nullptr);

        SYSLOG(Logging::Startup, (_T("AppGatewayCommon::Initialize: PID=%u"), getpid()));

        mShell = service;
        mShell->AddRef();

        AGW_TELEMETRY_INIT(mShell);
        AGW_RECORD_BOOTSTRAP_TIME();

        // Initialize the settings delegate
        mDelegate = std::make_shared<SettingsDelegate>();
        mDelegate->setShell(mShell);

        return EMPTY_STRING;
    }

    /* virtual */ void AppGatewayCommon::Deinitialize(PluginHost::IShell* service)
    {
        SYSLOG(Logging::Shutdown, (string(_T("AppGatewayCommon::Deinitialize"))));
        ASSERT(service == mShell);
        mConnectionId = 0;

        // Wait for any in-flight EventRegistrationJobs to complete before
        // destroying the delegate.  These jobs run on the worker pool and
        // may be blocked inside Thunder Subscribe() calls (up to the
        // SYSTEM_DELEGATE_SUBSCRIBE_TIMEOUT_MS timeout).  Destroying the
        // delegate while a job is executing causes use-after-free.
        {
            std::unique_lock<std::mutex> lk(mJobDrainMutex);
            mJobDrainCv.wait(lk, [this] { return mActiveJobs.load(std::memory_order_acquire) == 0; });
        }

        mDelegate->Cleanup();
        // Clean up the delegate
        mDelegate.reset();

        mShell->Release();
        mShell = nullptr;

        AGW_TELEMETRY_DEINIT();

        SYSLOG(Logging::Shutdown, (string(_T("AppGatewayCommon de-initialised"))));
    }

    void AppGatewayCommon::Deactivated(RPC::IRemoteConnection* connection)
    {
        if (connection->Id() == mConnectionId) {

            ASSERT(mShell != nullptr);

            Core::IWorkerPool::Instance().Submit(PluginHost::IShell::Job::Create(mShell, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
        }
    }

    Core::hresult AppGatewayCommon::HandleAppEventNotifier(Exchange::IAppNotificationHandler::IEmitter *cb, const string& event /* @in */,
                                    bool listen /* @in */,
                                    bool &status /* @out */) {
            LOGTRACE("HandleAppEventNotifier [event=%s listen=%s]",
                    event.c_str(), listen ? "true" : "false");
            
            status = this->SafeSubmitEventRegistrationJob(cb, event, listen);
            if (false == status) {
                LOGERR("HandleAppEventNotifier: Failed to submit event registration job for event %s", event.c_str());
                return Core::ERROR_GENERAL;
            }
            
            return Core::ERROR_NONE;
    }

            
    // Static handler map used to route GatewayContext requests to the corresponding AppGatewayCommon member handlers.
    const std::unordered_map<std::string, AppGatewayCommon::HandlerFunction> AppGatewayCommon::handlers = {
        { "device.make", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetDeviceMake(result);
        }},
        { "device.name", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetDeviceName(result);
        }},
        { "device.sku", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetDeviceSku(result);
        }},
        { "localization.countrycode", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetCountryCode(result);
        }},
        { "localization.country", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetCountryCode(result);
        }},
        { "localization.timezone", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetTimeZone(result);
        }},
        { "secondscreen.friendlyname", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetSecondScreenFriendlyName(result);
        }},
        { "localization.addadditionalinfo", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string& payload, std::string& result) {
            return ResponseUtils::SetNullResponseForSuccess(self->AddAdditionalInfo(payload, result), result);
        }},
        { "device.network", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetInternetConnectionStatus(result);
        }},
        { "voiceguidance.enabled", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetVoiceGuidance(result);
        }},
        { "device.version", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetFirmwareVersion(result);
        }},
        { "device.screenresolution", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetScreenResolution(result);
        }},
        { "device.videoresolution", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetVideoResolution(result);
        }},
        { "device.hdcp", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetHdcp(result);
        }},
        { "device.hdr", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetHdr(result);
        }},
        { "device.audio", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetAudio(result);
        }},
        { "videooutput.resolution", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetVideoOutputResolution(result);
        }},
        { "videooutput.hdcp", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetVideoOutputHdcp(result);
        }},
        { "videooutput.cecstate", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetVideoOutputCecActiveState(result);
        }},
        { "videooutput.port", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetVideoOutputPort(result);
        }},
        { "videooutput.refreshrate", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetVideoOutputRefreshRate(result);
        }},
        { "videooutput.colordepth", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetVideoOutputColorDepth(result);
        }},
        { "videooutput.colorformat", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetVideoOutputColorFormat(result);
        }},
        { "videooutput.colorimetry", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetVideoOutputColorimetry(result);
        }},
        { "videooutput.dynamicrange", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetVideoOutputDynamicRange(result);
        }},
        { "videooutput.quantizationrange", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetVideoOutputQuantizationRange(result);
        }},
        { "device.dolbyatmosexperienceavailable", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetDolbyAtmosExperience(result);
        }},
        { "voiceguidance.navigationhints", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetVoiceGuidanceHints(result);
        }},
        { "accessibility.voiceguidancesettings", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string&, std::string& result) {
            return self->GetVoiceGuidanceSettings(!ContextUtils::IsRDK8Compliant(ctx.version), result);
        }},
        { "accessibility.voiceguidance", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetVoiceGuidanceSettings(true, result);
        }},
        { "accessibility.audiodescriptionsettings", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetAudioDescription(result);
        }},
        { "accessibility.audiodescription", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetAudioDescriptionsEnabled(result);
        }},
        { "audiodescriptions.enabled", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetAudioDescriptionsEnabled(result);
        }},
        { "accessibility.highcontrastui", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetHighContrast(result);
        }},
        { "closedcaptions.enabled", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetCaptions(result);
        }},
        { "closedcaptions.preferredlanguages", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetPreferredCaptionsLanguages(result);
        }},
        { "accessibility.closedcaptions", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetClosedCaptionsSettings(result);
        }},
        { "accessibility.closedcaptionssettings", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetClosedCaptionsSettings(result);
        }},
        { "localization.language", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetPresentationLanguage(result);
        }},
        { "localization.presentationlanguage", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetPresentationLanguage(result);
        }},
        { "localization.locale", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetLocale(result);
        }},
        { "localization.preferredaudiolanguages", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetPreferredAudioLanguages(result);
        }},
        { "lifecycle2.close", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->Lifecycle2Close(ctx,payload,result);
        }},
        { "lifecycle.state", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->LifecycleState(ctx,payload,result);
        }},
        { "lifecycle2.state", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->Lifecycle2State(ctx,payload,result);
        }},
        { "lifecycle.close", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->LifecycleClose(ctx,payload,result);
        }},
        { "lifecycle.ready", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->LifecycleReady(ctx,payload,result);
        }},
        { "lifecycle.finished", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->LifecycleFinished(ctx,payload,result);
        }},
        { "actions.start", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->ActionsStart(ctx, payload, result);
        }},
        { "actions.intent", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->ActionsIntent(ctx, payload, result);
        }},
        { "commoninternal.dispatchintent", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->DispatchLastIntent(ctx,payload,result);
        }},
        { "commoninternal.getlastintent", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->GetLastIntent(ctx,payload,result);
        }},
        { "commoninternal.setintent", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->SetIntent(ctx, payload, result);
        }},
        {"advertising.advertisingid", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->HandleAppDelegateRequest(ctx, "advertising.advertisingid", payload, result);
        }},
        {"device.uid", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->HandleAppDelegateRequest(ctx, "device.uid", payload, result);
        }},
        {"discovery.watched", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            LOGINFO("Received discovery.watched event. AppId: %s, Payload: %s", ctx.appId.c_str(), payload.c_str());
            result = "null";
            return Core::ERROR_NONE; // No-op for now, as this is only used for RDK8 compliance which requires no action on the AppGatewayCommon side when this event is received.
        }},
        {"network.connected", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetNetworkConnected(result);
        }},
        { "speechsynthesis.voices", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->SpeechSynthesisVoices(ctx, payload, result);
        }},
        { "speechsynthesis.speak", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->SpeechSynthesisSpeak(ctx, payload, result);
        }},
        { "speechsynthesis.cancel", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->SpeechSynthesisCancel(ctx, payload, result);
        }},
        { "speechsynthesis.pause", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->SpeechSynthesisPause(ctx, payload, result);
        }},
        { "speechsynthesis.resume", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->SpeechSynthesisResume(ctx, payload, result);
        }},
        { "device.chipsetid", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetDeviceChipsetId(result);
        }},
        { "device.deviceclass", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetDeviceClass(result);
        }},
        { "device.uptime", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetDeviceUptime(result);
        }},
        { "device.timeinactivestate", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetDeviceTimeInActiveState(result);
        }},
        // Device Branding APIs (Phase 1)
        { "device.setosname", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string& payload, std::string& result) -> Core::hresult {
            std::string osName;
            if (JsonValidation::ValidateAndExtractString(payload, osName)) {
                return ResponseUtils::SetNullResponseForSuccess(self->SetDeviceOsName(osName), result);
            }
            result = "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}";
            return Core::ERROR_BAD_REQUEST;
        }},
        { "device.osname", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetDeviceOsName(result);
        }},
        { "device.setosversion", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string& payload, std::string& result) -> Core::hresult {
            std::string osVersion;
            if (JsonValidation::ValidateAndExtractString(payload, osVersion)) {
                return ResponseUtils::SetNullResponseForSuccess(self->SetDeviceOsVersion(osVersion), result);
            }
            result = "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}";
            return Core::ERROR_BAD_REQUEST;
        }},
        { "device.osversion", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetDeviceOsVersion(result);
        }},
        { "device.firmware", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetDeviceFirmware(result);
        }},
        { "stats.memoryusage", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string&, std::string& result) {
            return self->GetStatsMemoryUsage(ctx.appId, result);
        }},
        { "display.edid", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetDisplayEdid(result);
        }},
        { "display.size", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetDisplaySize(result);
        }},
        { "display.maxresolution", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetDisplayMaxResolution(result);
        }},
        { "display.colorimetry", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetDisplayColorimetry(result);
        }},
        { "display.videoresolutions", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetDisplayVideoResolutions(result);
        }},
        {"presentation.focused", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->GetPresentationFocused(ctx, payload, result);
        }},
        { "parentalcontrol.pincontrol", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetPinControl(result);
        }},
        { "parentalcontrol.blocknotratedcontent", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetBlockNotRatedContent(result);
        }},
        { "parentalcontrol.viewingrestrictions", [](AppGatewayCommon* self, const Exchange::GatewayContext&, const std::string&, std::string& result) {
            return self->GetViewingRestrictions(result);
        }},
        {"texttospeech.speak", [](AppGatewayCommon* self, const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result) {
            return self->TextToSpeechSpeak(ctx, payload, result);
        }}
    };

    Core::hresult AppGatewayCommon::HandleAppGatewayRequest(const Exchange::GatewayContext &context /* @in */,
                                          const string& method /* @in */,
                                          const string &payload /* @in @opaque */,
                                          string& result /*@out @opaque */)
        {
            LOGTRACE("HandleAppGatewayRequest: method=%s, payload=%s, appId=%s",
                    method.c_str(), payload.c_str(), context.appId.c_str());
            
            // Check if delegate is properly initialized
            if (nullptr == mDelegate) {
                LOGERR("HandleAppGatewayRequest: mDelegate is null, plugin not properly initialized");
                result = "{\"error\":\"Service unavailable\"}";
                return Core::ERROR_UNAVAILABLE;
            }
            
            std::string lowerMethod = StringUtils::toLower(method);

            auto it = handlers.find(lowerMethod);
            if (it != handlers.end()) {
                return it->second(this, context, payload, result);
            }
            else if (lowerMethod == "device.setname")
            {
                std::string name;
                if (JsonValidation::ValidateAndExtractString(payload, name)) {
                    return ResponseUtils::SetNullResponseForSuccess(SetDeviceName(std::move(name)), result);
                }
                result = "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}";
                return Core::ERROR_BAD_REQUEST;
            }
            else if (lowerMethod == "localization.setcountrycode")
            {
                std::string countryCode;
                if (JsonValidation::ValidateAndExtractString(payload, countryCode)) {
                    return ResponseUtils::SetNullResponseForSuccess(SetCountryCode(std::move(countryCode)), result);
                }
                result = "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}";
                return Core::ERROR_BAD_REQUEST;
            }
            else if (lowerMethod == "localization.settimezone")
            {
                std::string timeZone;
                if (JsonValidation::ValidateAndExtractString(payload, timeZone)) {
                    return ResponseUtils::SetNullResponseForSuccess(SetTimeZone(std::move(timeZone)), result);
                }
                result = "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}";
                return Core::ERROR_BAD_REQUEST;
            }
            else if (lowerMethod == "voiceguidance.setenabled")
            {
                bool enabled;
                if (JsonValidation::ValidateAndExtractBool(payload, enabled)) {
                    return ResponseUtils::SetNullResponseForSuccess(SetVoiceGuidance(enabled), result);
                }
                result = "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}";
                return Core::ERROR_BAD_REQUEST;
            }
            else if (lowerMethod == "voiceguidance.speed" || lowerMethod == "voiceguidance.rate")
            {
                double speed;
                Core::hresult status = GetSpeed(speed);
                if (status == Core::ERROR_NONE)
                {
                    std::ostringstream jsonStream;
                    jsonStream << speed;
                    result = jsonStream.str();
                }
                return status;
            }
            else if (lowerMethod == "voiceguidance.setspeed" || lowerMethod == "voiceguidance.setrate")
            {
                double speed;
                // Voice guidance speed should be between 0.5 and 2.0
                if (JsonValidation::ValidateAndExtractDouble(payload, speed, "value", 0.5, 2.0, true, true)) {
                    return ResponseUtils::SetNullResponseForSuccess(SetSpeed(speed), result);
                }
                result = "{\"error\":\"Invalid payload: missing, invalid, or out-of-range 'value' field (expected 0.5-2.0)\"}";
                return Core::ERROR_BAD_REQUEST;
            }
            else if (lowerMethod == "voiceguidance.setnavigationhints")
            {
                bool enabled;
                if (JsonValidation::ValidateAndExtractBool(payload, enabled)) {
                    return ResponseUtils::SetNullResponseForSuccess(SetVoiceGuidanceHints(enabled), result);
                }
                result = "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}";
                return Core::ERROR_BAD_REQUEST;
            }
            else if (lowerMethod == "audiodescriptions.setenabled")
            {
                bool enabled;
                if (JsonValidation::ValidateAndExtractBool(payload, enabled)) {
                    return ResponseUtils::SetNullResponseForSuccess(SetAudioDescriptionsEnabled(enabled), result);
                }
                result = "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}";
                return Core::ERROR_BAD_REQUEST;
            }
            else if (lowerMethod == "closedcaptions.setenabled")
            {
                bool enabled;
                if (JsonValidation::ValidateAndExtractBool(payload, enabled)) {
                    return ResponseUtils::SetNullResponseForSuccess(SetCaptions(enabled), result);
                }
                result = "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}";
                return Core::ERROR_BAD_REQUEST;
            }
            
            else if (lowerMethod == "closedcaptions.setpreferredlanguages")
            {
                std::string languages;
                if (JsonValidation::ValidateAndExtractStringOrArray(payload, languages)) {
                    return ResponseUtils::SetNullResponseForSuccess(SetPreferredCaptionsLanguages(languages), result);
                }
                result = "{\"error\":\"Invalid payload: 'value' field must be a string or array\"}";
                return Core::ERROR_BAD_REQUEST;
            }
            
            else if (lowerMethod == "localization.setlocale")
            {
                std::string locale;
                if (JsonValidation::ValidateAndExtractString(payload, locale)) {
                    return ResponseUtils::SetNullResponseForSuccess(SetLocale(locale), result);
                }
                result = "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}";
                return Core::ERROR_BAD_REQUEST;
            }
            else if (lowerMethod == "localization.setpreferredaudiolanguages")
            {
                std::string languages;
                if (JsonValidation::ValidateAndExtractStringOrArray(payload, languages)) {
                    return ResponseUtils::SetNullResponseForSuccess(SetPreferredAudioLanguages(languages), result);
                }
                result = "{\"error\":\"Invalid payload: 'value' field must be a string or array\"}";
                return Core::ERROR_BAD_REQUEST;
            }
            // lowermethod starts with metrics. just log the payload for now, as this is only used for RDK8 compliance and there are no specific requirements around handling this event for RDK8 compliance other than not returning an error when it's received.
            else if (lowerMethod.rfind("metrics.", 0) == 0)
            {
                LOGINFO("Received %s event. AppId: %s, Payload: %s", method.c_str(), context.appId.c_str(), payload.c_str());
                result = "null";
                return Core::ERROR_NONE; // No-op for now, as this is only used for RDK8 compliance which requires no action on the AppGatewayCommon side when this event is received.
            }

            // If method not found, return error
            ErrorUtils::NotSupported(result);
            LOGERR("Unsupported method: %s", method.c_str());
            return Core::ERROR_UNKNOWN_KEY;
        }

Core::hresult AppGatewayCommon::SpeechSynthesisVoices(const Exchange::GatewayContext&, const std::string& payload, std::string& result)
{
    result = "[]";
    if (!mDelegate) { ErrorUtils::NotAvailable(result); return Core::ERROR_UNAVAILABLE; }
    auto ttsDelegate = mDelegate->getTTSDelegate();
    if (!ttsDelegate) { ErrorUtils::NotAvailable(result); return Core::ERROR_UNAVAILABLE; }
    return ttsDelegate->SpeechSynthesisVoices(payload, result);
}

Core::hresult AppGatewayCommon::SpeechSynthesisSpeak(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result)
{
    if (!mDelegate) { ErrorUtils::NotAvailable(result); return Core::ERROR_UNAVAILABLE; }
    auto ttsDelegate = mDelegate->getTTSDelegate();
    if (!ttsDelegate) { ErrorUtils::NotAvailable(result); return Core::ERROR_UNAVAILABLE; }
    return ttsDelegate->SpeechSynthesisSpeak(ctx.appId, payload, result);
}

    Core::hresult AppGatewayCommon::SpeechSynthesisCancel(const Exchange::GatewayContext&, const std::string& payload, std::string& result)
    {
        if (!mDelegate) return Core::ERROR_UNAVAILABLE;
        auto ttsDelegate = mDelegate->getTTSDelegate();
        if (!ttsDelegate) return Core::ERROR_UNAVAILABLE;
        return ttsDelegate->SpeechSynthesisCancel(payload, result);
    }

    Core::hresult AppGatewayCommon::SpeechSynthesisPause(const Exchange::GatewayContext&, const std::string& payload, std::string& result)
    {
        if (!mDelegate) return Core::ERROR_UNAVAILABLE;
        auto ttsDelegate = mDelegate->getTTSDelegate();
        if (!ttsDelegate) return Core::ERROR_UNAVAILABLE;
        return ttsDelegate->SpeechSynthesisPause(payload, result);
    }

    Core::hresult AppGatewayCommon::SpeechSynthesisResume(const Exchange::GatewayContext&, const std::string& payload, std::string& result)
    {
        if (!mDelegate) return Core::ERROR_UNAVAILABLE;
        auto ttsDelegate = mDelegate->getTTSDelegate();
        if (!ttsDelegate) return Core::ERROR_UNAVAILABLE;
        return ttsDelegate->SpeechSynthesisResume(payload, result);
    }
    
    Core::hresult AppGatewayCommon::SetName(const string &value /* @in */, string &result)
        {
            result = "null"; // TBA
            return Core::ERROR_NONE;
        }

        Core::hresult AppGatewayCommon::AddAdditionalInfo(const string &value /* @in @opaque */, string &result)
        {
            result = "null"; // TBA
            return Core::ERROR_NONE;
        }

        /**
         * @brief Safely submits EventRegistrationJob with proper error handling
         * @param cb Callback interface (must be not-null)
         * @param event Event name
         * @param listen Whether to listen or unlisten
         * @return true if job submitted successfully, false otherwise
         */
        bool AppGatewayCommon::SafeSubmitEventRegistrationJob(Exchange::IAppNotificationHandler::IEmitter* cb, 
                                                              const std::string& event, bool listen) {

            if (nullptr == cb) {
                LOGERR("SafeSubmitEventRegistrationJob: Callback emitter is null");
                return false;
            }

            if (nullptr == mDelegate) {
                LOGERR("SafeSubmitEventRegistrationJob: Delegate is null");
                return false;
            }

            auto job = EventRegistrationJob::Create(this, cb, event, listen);
            if (false == job.IsValid()) {
                LOGERR("SafeSubmitEventRegistrationJob: Failed to create EventRegistrationJob");
                return false;
            }

            mActiveJobs.fetch_add(1, std::memory_order_acq_rel);
            Core::IWorkerPool::Instance().Submit(job);
            return true;
        }

        // Delegated alias methods

        Core::hresult AppGatewayCommon::GetDeviceMake(string &make)
        {
            if (!mDelegate)
                return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate)
                return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetDeviceMake(make);
        }

        Core::hresult AppGatewayCommon::GetDeviceName(string &name)
        {
            if (!mDelegate)
                return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate)
                return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetDeviceName(name);
        }

        Core::hresult AppGatewayCommon::SetDeviceName(const string name)
        {
            if (!mDelegate)
                return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate)
                return Core::ERROR_UNAVAILABLE;
            return systemDelegate->SetDeviceName(name);
        }

        Core::hresult AppGatewayCommon::GetDeviceSku(string &sku)
        {
            if (!mDelegate)
                return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate)
                return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetDeviceSku(sku);
        }

        Core::hresult AppGatewayCommon::GetCountryCode(string &countryCode)
        {
            if (!mDelegate)
                return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate)
                return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetCountryCode(countryCode);
        }

        Core::hresult AppGatewayCommon::SetCountryCode(const string countryCode)
        {
            if (!mDelegate)
                return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate)
                return Core::ERROR_UNAVAILABLE;
            return systemDelegate->SetCountryCode(countryCode);
        }

        Core::hresult AppGatewayCommon::GetTimeZone(string &timeZone)
        {
            if (!mDelegate)
                return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate)
                return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetTimeZone(timeZone);
        }

        Core::hresult AppGatewayCommon::SetTimeZone(const string timeZone)
        {
            if (!mDelegate)
                return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate)
                return Core::ERROR_UNAVAILABLE;
            return systemDelegate->SetTimeZone(timeZone);
        }

        Core::hresult AppGatewayCommon::GetSecondScreenFriendlyName(string &name)
        {
            if (!mDelegate)
                return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate)
                return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetSecondScreenFriendlyName(name);
        }

        // UserSettings APIs
        Core::hresult AppGatewayCommon::GetVoiceGuidance(string &result)
        {
            if (!mDelegate)
            {
                result = "{\"error\":\"couldnt get voiceguidance state\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                result = "{\"error\":\"couldnt get voiceguidance state\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->GetVoiceGuidance(result);
        }

        Core::hresult AppGatewayCommon::GetAudioDescription(string &result)
        {
            if (!mDelegate)
            {
                result = "{\"error\":\"couldnt get audio description settings\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                result = "{\"error\":\"couldnt get audio description settings\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->GetAudioDescription(result);
        }

        Core::hresult AppGatewayCommon::GetAudioDescriptionsEnabled(string &result)
        {
            if (!mDelegate)
            {
                result = "{\"error\":\"couldnt get audio descriptions enabled\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                result = "{\"error\":\"couldnt get audio descriptions enabled\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->GetAudioDescriptionsEnabled(result);
        }

        Core::hresult AppGatewayCommon::GetHighContrast(string &result)
        {
            if (!mDelegate)
            {
                result = "{\"error\":\"couldnt get high contrast state\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                result = "{\"error\":\"couldnt get high contrast state\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->GetHighContrast(result);
        }

        Core::hresult AppGatewayCommon::GetCaptions(string &result)
        {
            if (!mDelegate)
            {
                result = "{\"error\":\"couldnt get captions state\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                result = "{\"error\":\"couldnt get captions state\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->GetCaptions(result);
        }

        Core::hresult AppGatewayCommon::GetPresentationLanguage(string &result)
        {
            if (!mDelegate)
            {
                result = "{\"error\":\"couldn't get language\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                result = "{\"error\":\"couldn't get language\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->GetPresentationLanguage(result);
        }

        Core::hresult AppGatewayCommon::GetLocale(string &result)
        {
            if (!mDelegate)
            {
                result = "{\"error\":\"couldn't get locale\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                result = "{\"error\":\"couldn't get locale\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->GetLocale(result);
        }

        Core::hresult AppGatewayCommon::SetLocale(const string &locale)
        {
            if (!mDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->SetLocale(locale);
        }

        Core::hresult AppGatewayCommon::GetPreferredAudioLanguages(string &result)
        {
            if (!mDelegate)
            {
                result = "[]";
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                result = "[]";
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->GetPreferredAudioLanguages(result);
        }

        Core::hresult AppGatewayCommon::GetPreferredCaptionsLanguages(string &result)
        {
            if (!mDelegate)
            {
                result = "[\"eng\"]";
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                result = "[\"eng\"]";
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->GetPreferredCaptionsLanguages(result);
        }

        Core::hresult AppGatewayCommon::SetPreferredAudioLanguages(const string &languages)
        {
            if (!mDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->SetPreferredAudioLanguages(languages);
        }

        Core::hresult AppGatewayCommon::SetPreferredCaptionsLanguages(const string &preferredLanguages)
        {
            if (!mDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->SetPreferredCaptionsLanguages(preferredLanguages);
        }

        Core::hresult AppGatewayCommon::SetVoiceGuidance(const bool enabled)
        {
            if (!mDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->SetVoiceGuidance(enabled);
        }

        Core::hresult AppGatewayCommon::SetAudioDescriptionsEnabled(const bool enabled)
        {
            if (!mDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->SetAudioDescriptionsEnabled(enabled);
        }

        Core::hresult AppGatewayCommon::SetCaptions(const bool enabled)
        {
            if (!mDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->SetCaptions(enabled);
        }

        Core::hresult AppGatewayCommon::SetSpeed(const double speed)
        {
            if (!mDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            // Transform the speed using vg_speed_firebolt2thunder function logic:
            // (if $speed == 2 then 10 elif $speed >= 1.67 then 1.38 elif $speed >= 1.33 then 1.19 elif $speed >= 1 then 1 else 0.1 end)
            double transformedRate;
            if (speed == 2.0)
            {
                transformedRate = 10.0;
            }
            else if (speed >= 1.67)
            {
                transformedRate = 1.38;
            }
            else if (speed >= 1.33)
            {
                transformedRate = 1.19;
            }
            else if (speed >= 1.0)
            {
                transformedRate = 1.0;
            }
            else
            {
                transformedRate = 0.1;
            }

            LOGINFO("SetSpeed: transforming speed %f to rate %f", speed, transformedRate);

            return userSettingsDelegate->SetVoiceGuidanceRate(transformedRate);
        }

        Core::hresult AppGatewayCommon::GetSpeed(double &speed)
        {
            if (!mDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            double rate;
            Core::hresult result = userSettingsDelegate->GetVoiceGuidanceRate(rate);

            if (result != Core::ERROR_NONE)
            {
                LOGERR("Failed to get voice guidance rate");
                return result;
            }

            // Transform the rate using vg_speed_thunder2firebolt function logic:
            // (if $speed >= 1.56 then 2 elif $speed >= 1.38 then 1.67 elif $speed >= 1.19 then 1.33 elif $speed >= 1 then 1 else 0.5 end)
            if (rate >= 1.56)
            {
                speed = 2.0;
            }
            else if (rate >= 1.38)
            {
                speed = 1.67;
            }
            else if (rate >= 1.19)
            {
                speed = 1.33;
            }
            else if (rate >= 1.0)
            {
                speed = 1.0;
            }
            else
            {
                speed = 0.5;
            }

            LOGINFO("GetSpeed: transforming rate %f to speed %f", rate, speed);

            return Core::ERROR_NONE;
        }

        Core::hresult AppGatewayCommon::GetVoiceGuidanceHints(string &result)
        {
            if (!mDelegate)
            {
                result = "{\"error\":\"couldnt get navigationHints\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                result = "{\"error\":\"couldnt get navigationHints\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->GetVoiceGuidanceHints(result);
        }

        Core::hresult AppGatewayCommon::SetVoiceGuidanceHints(const bool enabled)
        {
            if (!mDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->SetVoiceGuidanceHints(enabled);
        }

        Core::hresult AppGatewayCommon::GetVoiceGuidanceSettings(const bool addSpeed, string &result)
        {
            if (!mDelegate)
            {
                result = "{\"error\":\"couldn't get voice guidance settings\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                result = "{\"error\":\"couldn't get voice guidance settings\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            const bool updated = userSettingsDelegate->UpdateVoiceGuidanceSettings(addSpeed, result);
            if (false == updated)
            {
                // Error message already updated in result as part of UpdateVoiceGuidanceSettings
                return Core::ERROR_GENERAL;
            }

            return Core::ERROR_NONE;
        }

        Core::hresult AppGatewayCommon::GetClosedCaptionsSettings(string &result)
        {
            if (!mDelegate)
            {
                result = "{\"error\":\"couldn't get closed captions settings\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                result = "{\"error\":\"couldn't get closed captions settings\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            return userSettingsDelegate->GetClosedCaptionSettings(result);
        }

        Core::hresult AppGatewayCommon::GetInternetConnectionStatus(string &result)
        {
            if (!mDelegate)
            {
                result = "{\"error\":\"couldn't get internet connection status\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            auto networkDelegate = mDelegate->getNetworkDelegate();
            if (!networkDelegate)
            {
                result = "{\"error\":\"couldn't get internet connection status\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            return networkDelegate->GetInternetConnectionStatus(result);
        }

        Core::hresult AppGatewayCommon::GetNetworkConnected(string &result)
        {
            if (!mDelegate)
            {
                result = "{\"error\":\"couldn't get network connected status\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            auto networkDelegate = mDelegate->getNetworkDelegate();
            if (!networkDelegate)
            {
                result = "{\"error\":\"couldn't get network connected status\"}";
                return Core::ERROR_UNAVAILABLE;
            }

            return networkDelegate->GetNetworkConnected(result);
        }

        Core::hresult AppGatewayCommon::GetFirmwareVersion(string &result /* @out */)
        {
            if (!mDelegate)
                return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate)
                return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetFirmwareVersion(result);
        }

        // Core::hresult AppGatewayCommon::GetScreenResolution(string &result /* out */) {
        //     result = R"([1920,1080])";
        //     return Core::ERROR_NONE;
        // }

        // Core::hresult AppGatewayCommon::GetVideoResolution(string &result /* out */) {
        //     result = R"([1920,1080])";
        //     return Core::ERROR_NONE;
        // }

        Core::hresult AppGatewayCommon::GetScreenResolution(string &result)
        {
            LOGINFO("GetScreenResolution AppGatewayCommon");
            if (!mDelegate) {
                result = "[1920,1080]";
                return Core::ERROR_UNAVAILABLE;
            }
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) {
                result = "[1920,1080]";
                return Core::ERROR_UNAVAILABLE;
            }
            return systemDelegate->GetScreenResolution(result);
        }

        Core::hresult AppGatewayCommon::GetVideoResolution(string &result)
        {
            LOGINFO("GetVideoResolution AppGatewayCommon");
            if (!mDelegate) {
                result = "[1920,1080]";
                return Core::ERROR_UNAVAILABLE;
            }
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) {
                result = "[1920,1080]";
                return Core::ERROR_UNAVAILABLE;
            }
            return systemDelegate->GetVideoResolution(result);
        }

        Core::hresult AppGatewayCommon::GetHdcp(string &result)
        {
            LOGINFO("GetHdcp AppGatewayCommon");
            if (!mDelegate) {
                result = "{\"hdcp1.4\":false,\"hdcp2.2\":false}";
                return Core::ERROR_UNAVAILABLE;
            }
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) {
                result = "{\"hdcp1.4\":false,\"hdcp2.2\":false}";
                return Core::ERROR_UNAVAILABLE;
            }
            return systemDelegate->GetHdcp(result);
        }

        Core::hresult AppGatewayCommon::GetHdr(string &result)
        {
            LOGINFO("GetHdr AppGatewayCommon");
            if (!mDelegate) {
                result = "{\"hdr10\":false,\"dolbyVision\":false,\"hlg\":false,\"hdr10Plus\":false}";
                return Core::ERROR_UNAVAILABLE;
            }
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) {
                result = "{\"hdr10\":false,\"dolbyVision\":false,\"hlg\":false,\"hdr10Plus\":false}";
                return Core::ERROR_UNAVAILABLE;
            }
            return systemDelegate->GetHdr(result);
        }

        Core::hresult AppGatewayCommon::GetAudio(string &result)
        {
            LOGINFO("GetAudio AppGatewayCommon");
            if (!mDelegate) {
                result = "{\"stereo\":true,\"dolbyDigital5.1\":false,\"dolbyDigital5.1+\":false,\"dolbyAtmos\":false}";
                return Core::ERROR_UNAVAILABLE;
            }
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) {
                result = "{\"stereo\":true,\"dolbyDigital5.1\":false,\"dolbyDigital5.1+\":false,\"dolbyAtmos\":false}";
                return Core::ERROR_UNAVAILABLE;
            }
            return systemDelegate->GetAudio(result);
        }

        // ─── VideoOutput API wrappers ─────────────────────────────────────

        Core::hresult AppGatewayCommon::GetVideoOutputResolution(string &result)
        {
            LOGINFO("GetVideoOutputResolution AppGatewayCommon");
            result = "{\"width\":0,\"height\":0}";
            if (nullptr == mDelegate) return Core::ERROR_UNAVAILABLE;
            auto videoOutputDelegate = mDelegate->getVideoOutputDelegate();
            if (nullptr == videoOutputDelegate) return Core::ERROR_UNAVAILABLE;
            return videoOutputDelegate->GetVideoOutputResolution(result);
        }

        Core::hresult AppGatewayCommon::GetVideoOutputHdcp(string &result)
        {
            LOGINFO("GetVideoOutputHdcp AppGatewayCommon");
            result = "\"none\"";
            if (nullptr == mDelegate) return Core::ERROR_UNAVAILABLE;
            auto videoOutputDelegate = mDelegate->getVideoOutputDelegate();
            if (nullptr == videoOutputDelegate) return Core::ERROR_UNAVAILABLE;
            return videoOutputDelegate->GetVideoOutputHdcp(result);
        }

        Core::hresult AppGatewayCommon::GetVideoOutputCecActiveState(string &result)
        {
            LOGINFO("GetVideoOutputCecActiveState AppGatewayCommon");
            result = "\"unsupported\"";
            if (nullptr == mDelegate) return Core::ERROR_UNAVAILABLE;
            auto videoOutputDelegate = mDelegate->getVideoOutputDelegate();
            if (nullptr == videoOutputDelegate) return Core::ERROR_UNAVAILABLE;
            return videoOutputDelegate->GetVideoOutputCecActiveState(result);
        }

        Core::hresult AppGatewayCommon::GetVideoOutputPort(string &result)
        {
            LOGINFO("GetVideoOutputPort AppGatewayCommon");
            result = "\"none\"";
            if (nullptr == mDelegate) return Core::ERROR_UNAVAILABLE;
            auto videoOutputDelegate = mDelegate->getVideoOutputDelegate();
            if (nullptr == videoOutputDelegate) return Core::ERROR_UNAVAILABLE;
            return videoOutputDelegate->GetVideoOutputPort(result);
        }

        Core::hresult AppGatewayCommon::GetVideoOutputRefreshRate(string &result)
        {
            LOGINFO("GetVideoOutputRefreshRate AppGatewayCommon");
            result = "0";
            if (nullptr == mDelegate) return Core::ERROR_UNAVAILABLE;
            auto videoOutputDelegate = mDelegate->getVideoOutputDelegate();
            if (nullptr == videoOutputDelegate) return Core::ERROR_UNAVAILABLE;
            return videoOutputDelegate->GetVideoOutputRefreshRate(result);
        }

        Core::hresult AppGatewayCommon::GetVideoOutputColorDepth(string &result)
        {
            LOGINFO("GetVideoOutputColorDepth AppGatewayCommon");
            result = "0";
            if (nullptr == mDelegate) return Core::ERROR_UNAVAILABLE;
            auto videoOutputDelegate = mDelegate->getVideoOutputDelegate();
            if (nullptr == videoOutputDelegate) return Core::ERROR_UNAVAILABLE;
            return videoOutputDelegate->GetVideoOutputColorDepth(result);
        }

        Core::hresult AppGatewayCommon::GetVideoOutputColorFormat(string &result)
        {
            LOGINFO("GetVideoOutputColorFormat AppGatewayCommon");
            result = "\"none\"";
            if (nullptr == mDelegate) return Core::ERROR_UNAVAILABLE;
            auto videoOutputDelegate = mDelegate->getVideoOutputDelegate();
            if (nullptr == videoOutputDelegate) return Core::ERROR_UNAVAILABLE;
            return videoOutputDelegate->GetVideoOutputColorFormat(result);
        }

        Core::hresult AppGatewayCommon::GetVideoOutputColorimetry(string &result)
        {
            LOGINFO("GetVideoOutputColorimetry AppGatewayCommon");
            result = "\"none\"";
            if (nullptr == mDelegate) return Core::ERROR_UNAVAILABLE;
            auto videoOutputDelegate = mDelegate->getVideoOutputDelegate();
            if (nullptr == videoOutputDelegate) return Core::ERROR_UNAVAILABLE;
            return videoOutputDelegate->GetVideoOutputColorimetry(result);
        }

        Core::hresult AppGatewayCommon::GetVideoOutputDynamicRange(string &result)
        {
            LOGINFO("GetVideoOutputDynamicRange AppGatewayCommon");
            result = "\"none\"";
            if (nullptr == mDelegate) return Core::ERROR_UNAVAILABLE;
            auto videoOutputDelegate = mDelegate->getVideoOutputDelegate();
            if (nullptr == videoOutputDelegate) return Core::ERROR_UNAVAILABLE;
            return videoOutputDelegate->GetVideoOutputDynamicRange(result);
        }

        Core::hresult AppGatewayCommon::GetVideoOutputQuantizationRange(string &result)
        {
            LOGINFO("GetVideoOutputQuantizationRange AppGatewayCommon");
            result = "\"none\"";
            if (nullptr == mDelegate) return Core::ERROR_UNAVAILABLE;
            auto videoOutputDelegate = mDelegate->getVideoOutputDelegate();
            if (nullptr == videoOutputDelegate) return Core::ERROR_UNAVAILABLE;
            return videoOutputDelegate->GetVideoOutputQuantizationRange(result);
        }

        Core::hresult AppGatewayCommon::GetDolbyAtmosExperience(string &result)
        {
            LOGINFO("GetDolbyAtmosExperience AppGatewayCommon");
            if (!mDelegate) {
                result = "false";
                return Core::ERROR_UNAVAILABLE;
            }

            auto avOutputDelegate = mDelegate->getAvOutputDelegate();
            if (!avOutputDelegate) {
                result = "false";
                return Core::ERROR_UNAVAILABLE;
            }

            return avOutputDelegate->GetDolbyAtmosExperience(result);
        }

        template <typename DelegateType, typename LifecycleType, typename Func, typename... Args>
        Core::hresult InvokeLifecycleDelegate(const std::shared_ptr<DelegateType>& delegate,
                                            std::shared_ptr<LifecycleType> (DelegateType::*getLifecycleDelegate)() const,
                                            Func func, Args&&... args) {
            if (!delegate) {
                return Core::ERROR_UNAVAILABLE;
            }
            std::shared_ptr<LifecycleType> lifecycleDelegate = ((*delegate).*getLifecycleDelegate)();
            if (!lifecycleDelegate) {
                return Core::ERROR_UNAVAILABLE;
            }
            return ((*lifecycleDelegate).*func)(std::forward<Args>(args)...);
        }


        Core::hresult AppGatewayCommon::Authenticate(const string &sessionId /* @in */, string &appId /* @out */)
        {
            return InvokeLifecycleDelegate(mDelegate, &SettingsDelegate::getLifecycleDelegate, &LifecycleDelegate::Authenticate, sessionId, appId);
        }

        Core::hresult AppGatewayCommon::GetSessionId(const string &appId /* @in */, string &sessionId /* @out */)
        {
            return InvokeLifecycleDelegate(mDelegate, &SettingsDelegate::getLifecycleDelegate, &LifecycleDelegate::GetSessionId, appId, sessionId);
        }

        Core::hresult AppGatewayCommon::LifecycleFinished(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result)
        {
            return InvokeLifecycleDelegate(mDelegate, &SettingsDelegate::getLifecycleDelegate, &LifecycleDelegate::LifecycleFinished, ctx, payload, result);
        }

        Core::hresult AppGatewayCommon::LifecycleReady(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result)
        {
            Core::hresult hr = InvokeLifecycleDelegate(mDelegate, &SettingsDelegate::getLifecycleDelegate, &LifecycleDelegate::LifecycleReady, ctx, payload, result);
            if (Core::ERROR_NONE == hr) {
                // Telemetry: emit APP_READY_split marker
                // CSV format: appId
                AGW_REPORT_EVENT(ctx, AGW_MARKER_APP_READY, ctx.appId);
            }
            return hr;
        }

        Core::hresult AppGatewayCommon::LifecycleClose(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result)
        {
            return InvokeLifecycleDelegate(mDelegate, &SettingsDelegate::getLifecycleDelegate, &LifecycleDelegate::LifecycleClose, ctx, payload, result);
        }

        Core::hresult AppGatewayCommon::Lifecycle2State(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result)
        {
            return InvokeLifecycleDelegate(mDelegate, &SettingsDelegate::getLifecycleDelegate, &LifecycleDelegate::Lifecycle2State, ctx, payload, result);
        }

        Core::hresult AppGatewayCommon::LifecycleState(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result)
        {
            return InvokeLifecycleDelegate(mDelegate, &SettingsDelegate::getLifecycleDelegate, &LifecycleDelegate::LifecycleState, ctx, payload, result);
        }

        Core::hresult AppGatewayCommon::Lifecycle2Close(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result)
        {
            return InvokeLifecycleDelegate(mDelegate, &SettingsDelegate::getLifecycleDelegate, &LifecycleDelegate::Lifecycle2Close, ctx, payload, result);
        }

        Core::hresult AppGatewayCommon::DispatchLastIntent(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result)
        {
            return InvokeLifecycleDelegate(mDelegate, &SettingsDelegate::getLifecycleDelegate, &LifecycleDelegate::DispatchLastIntent, ctx, payload, result);
        }

        Core::hresult AppGatewayCommon::GetLastIntent(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result)
        {
            return InvokeLifecycleDelegate(mDelegate, &SettingsDelegate::getLifecycleDelegate, &LifecycleDelegate::GetLastIntent, ctx, payload, result);
        }

        Core::hresult AppGatewayCommon::ActionsStart(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result)
        {
            return InvokeLifecycleDelegate(mDelegate, &SettingsDelegate::getLifecycleDelegate, &LifecycleDelegate::ActionsStart, ctx, payload, result);
        }

        Core::hresult AppGatewayCommon::ActionsIntent(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result)
        {
            return InvokeLifecycleDelegate(mDelegate, &SettingsDelegate::getLifecycleDelegate, &LifecycleDelegate::ActionsIntent, ctx, payload, result);
        }
        
        Core::hresult AppGatewayCommon::GetPresentationFocused(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result)
        {
            return InvokeLifecycleDelegate(mDelegate, &SettingsDelegate::getLifecycleDelegate, &LifecycleDelegate::GetPresentationFocused, ctx, payload, result);
        }

        Core::hresult AppGatewayCommon::SetIntent(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result)
        {
            return InvokeLifecycleDelegate(mDelegate, &SettingsDelegate::getLifecycleDelegate, &LifecycleDelegate::SetIntent, ctx, payload, result);
        }

        Core::hresult AppGatewayCommon::CheckPermissionGroup(const string &appId /* @in */, const string &permissionGroup /* @in */, bool &allowed /* @out */)
        {
            // Currently there are no permission groups defined so default is allowed
            // This is not a security issue given all packages are signed and only non sensitive app methods are allowed to be accessed.
            // TODO: In future when Permission groups are defined this interface will be implemented
            // 
            allowed = true;
            return Core::ERROR_NONE;
        }

        Core::hresult AppGatewayCommon::HandleAppDelegateRequest(const Exchange::GatewayContext& context ,
                                          const string& method ,
                                          const string& payload /*@opaque */,
                                          string& result /*@out @opaque */) {
            if (!mDelegate) {
                ErrorUtils::CustomInternal("Settings delegate not available", result);
                return Core::ERROR_UNAVAILABLE;
            }
            auto appDelegate = mDelegate->getAppDelegate();
            if (!appDelegate) {
                ErrorUtils::CustomInternal("App delegate not available", result);
                return Core::ERROR_UNAVAILABLE;
            }

            return appDelegate->HandleAppGatewayRequest(context, method, payload, result);
        
        }

        Core::hresult AppGatewayCommon::GetDeviceChipsetId(string &result)
        {
            if (!mDelegate) return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetDeviceChipsetId(result);
        }

        Core::hresult AppGatewayCommon::GetDeviceClass(string &result)
        {
            if (!mDelegate) return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetDeviceClass(result);
        }

        Core::hresult AppGatewayCommon::GetDeviceUptime(string &result)
        {
            if (!mDelegate) return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetDeviceUptime(result);
        }

        Core::hresult AppGatewayCommon::GetDeviceTimeInActiveState(string &result)
        {
            if (!mDelegate) return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetDeviceTimeInActiveState(result);
        }

        // Device Branding APIs (Phase 1)
        Core::hresult AppGatewayCommon::SetDeviceOsName(const string &osName)
        {
            if (!mDelegate) return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) return Core::ERROR_UNAVAILABLE;
            return systemDelegate->SetDeviceOsName(osName);
        }

        Core::hresult AppGatewayCommon::GetDeviceOsName(string &result)
        {
            if (!mDelegate) return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetDeviceOsName(result);
        }

        Core::hresult AppGatewayCommon::SetDeviceOsVersion(const string &osVersion)
        {
            if (!mDelegate) return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) return Core::ERROR_UNAVAILABLE;
            return systemDelegate->SetDeviceOsVersion(osVersion);
        }

        Core::hresult AppGatewayCommon::GetDeviceOsVersion(string &result)
        {
            if (!mDelegate) return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetDeviceOsVersion(result);
        }

        Core::hresult AppGatewayCommon::GetDeviceFirmware(string &result)
        {
            if (!mDelegate) return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetDeviceFirmware(result);
        }

        Core::hresult AppGatewayCommon::GetStatsMemoryUsage(const string &appId, string &result)
        {
            if (!mDelegate) return Core::ERROR_UNAVAILABLE;
            auto lifecycleDelegate = mDelegate->getLifecycleDelegate();
            if (!lifecycleDelegate) return Core::ERROR_UNAVAILABLE;
            return lifecycleDelegate->GetStatsMemoryUsage(appId, result);
        }

        Core::hresult AppGatewayCommon::GetDisplayEdid(string &result)
        {
            result = "\"\"";
            if (!mDelegate) return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetDisplayEdid(result);
        }

        Core::hresult AppGatewayCommon::GetDisplaySize(string &result)
        {
            result = "{\"width\":0,\"height\":0}";
            if (!mDelegate) return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetDisplaySize(result);
        }

        Core::hresult AppGatewayCommon::GetDisplayMaxResolution(string &result)
        {
            result = "{\"width\":0,\"height\":0}";
            if (!mDelegate) return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetDisplayMaxResolution(result);
        }

        Core::hresult AppGatewayCommon::GetDisplayColorimetry(string &result)
        {
            result = "[]";
            if (!mDelegate) return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetDisplayColorimetry(result);
        }

        Core::hresult AppGatewayCommon::GetPinControl(string &result)
        {
            if (!mDelegate)
            {
                result = "{\"error\":\"couldn't get pin control state\"}";
                return Core::ERROR_UNAVAILABLE;
            }
            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                result = "{\"error\":\"couldn't get pin control state\"}";
                return Core::ERROR_UNAVAILABLE;
            }
            return userSettingsDelegate->GetPinControl(result);
        }

        Core::hresult AppGatewayCommon::GetBlockNotRatedContent(string &result)
        {
            if (!mDelegate)
            {
                result = "{\"error\":\"couldn't get block not rated content state\"}";
                return Core::ERROR_UNAVAILABLE;
            }
            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                result = "{\"error\":\"couldn't get block not rated content state\"}";
                return Core::ERROR_UNAVAILABLE;
            }
            return userSettingsDelegate->GetBlockNotRatedContent(result);
        }

        Core::hresult AppGatewayCommon::GetViewingRestrictions(string &result)
        {
            if (!mDelegate)
            {
                result = "{\"error\":\"couldn't get viewing restrictions\"}";
                return Core::ERROR_UNAVAILABLE;
            }
            auto userSettingsDelegate = mDelegate->getUserSettings();
            if (!userSettingsDelegate)
            {
                result = "{\"error\":\"couldn't get viewing restrictions\"}";
                return Core::ERROR_UNAVAILABLE;
            }
            return userSettingsDelegate->GetViewingRestrictions(result);
        }

        Core::hresult AppGatewayCommon::GetDisplayVideoResolutions(string &result)
        {
            result = "[]";
            if (!mDelegate) return Core::ERROR_UNAVAILABLE;
            auto systemDelegate = mDelegate->getSystemDelegate();
            if (!systemDelegate) return Core::ERROR_UNAVAILABLE;
            return systemDelegate->GetDisplayVideoResolutions(result);
        }

        Core::hresult AppGatewayCommon::TextToSpeechSpeak(const Exchange::GatewayContext& ctx, const string& payload, string& result)
        {
            result = "{}";
            if (!mDelegate) return Core::ERROR_UNAVAILABLE;
            auto ttsDelegate = mDelegate->getTTSDelegate();
            if (!ttsDelegate) return Core::ERROR_UNAVAILABLE;
            return ttsDelegate->TextToSpeechSpeak(ctx, payload, result);
        }

} // namespace Plugin
} // namespace WPEFramework
