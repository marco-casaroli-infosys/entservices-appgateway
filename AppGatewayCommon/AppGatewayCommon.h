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
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <map>
#include "UtilsLogging.h"
#include "UtilsController.h"
#include "delegate/SettingsDelegate.h"
#include <unordered_map>
#include <functional>

namespace WPEFramework {

    namespace Plugin {


		class AppGatewayCommon : public PluginHost::IPlugin, Exchange::IAppGatewayRequestHandler, Exchange::IAppNotificationHandler, Exchange::IAppGatewayAuthenticator{
        private:
            using HandlerFunction = std::function<Core::hresult(AppGatewayCommon*, const Exchange::GatewayContext&, const std::string&, std::string&)>;
            static const std::unordered_map<std::string, HandlerFunction> handlers;
            // We do not allow this plugin to be copied !!
            AppGatewayCommon(const AppGatewayCommon&) = delete;
            AppGatewayCommon& operator=(const AppGatewayCommon&) = delete;

            class EXTERNAL EventRegistrationJob : public Core::IDispatch
        {
            protected:
                EventRegistrationJob(AppGatewayCommon *parent,
                Exchange::IAppNotificationHandler::IEmitter *cb,
                const string &event,
                const bool listen): mParent(*parent), mCallback(cb), mEvent(event), mListen(listen) {
                    if (mCallback != nullptr) {
                        mCallback->AddRef();
                    }
                }
            public:
                EventRegistrationJob() = delete;
                EventRegistrationJob(const EventRegistrationJob &) = delete;
                EventRegistrationJob &operator=(const EventRegistrationJob &) = delete;
                ~EventRegistrationJob()
                {
                    if (mCallback != nullptr) {
                        mCallback->Release();
                        mCallback = nullptr;
                    }
                }

                static Core::ProxyType<Core::IDispatch> Create(AppGatewayCommon *parent,
                Exchange::IAppNotificationHandler::IEmitter *cb, const string& event, const bool listen)
                {
                    return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<EventRegistrationJob>::Create(parent, cb, event, listen)));
                }
                virtual void Dispatch()
                {
                    mParent.mDelegate->HandleAppEventNotifier(mCallback, mEvent, mListen);
                    // fetch_sub returns the previous value; if it was 1 the
                    // counter is now 0 (last in-flight job finished). Lock
                    // the mutex so the notify wakes up Deinitialize's
                    // wait — this ensures the signal is never missed.
                    if (1 == mParent.mActiveJobs.fetch_sub(1, std::memory_order_acq_rel)) {
                        std::lock_guard<std::mutex> lk(mParent.mJobDrainMutex);
                        mParent.mJobDrainCv.notify_all();
                    }
                }

            private:
            AppGatewayCommon &mParent;
            Exchange::IAppNotificationHandler::IEmitter *mCallback;
            const string mEvent;
            const bool mListen;

        };


        public:
            AppGatewayCommon();
            virtual ~AppGatewayCommon();
            virtual const string Initialize(PluginHost::IShell* shell) override;
            virtual void Deinitialize(PluginHost::IShell* service) override;
            virtual string Information() const override { return {}; }

            BEGIN_INTERFACE_MAP(AppGatewayCommon)
            INTERFACE_ENTRY(PluginHost::IPlugin)
            INTERFACE_ENTRY(Exchange::IAppGatewayRequestHandler)
            INTERFACE_ENTRY(Exchange::IAppNotificationHandler)
            INTERFACE_ENTRY(Exchange::IAppGatewayAuthenticator)
            END_INTERFACE_MAP
        
        public:
            virtual Core::hresult HandleAppEventNotifier(Exchange::IAppNotificationHandler::IEmitter *cb /* @in */,
                const string& event /* @in */,
                bool listen /* @in */,
                bool& status /* @out */) override;
            
            virtual Core::hresult HandleAppGatewayRequest(const Exchange::GatewayContext &context /* @in */,
                                          const string& method /* @in */,
                                          const string &payload /* @in @opaque */,
                                          string& result /*@out @opaque */) override;
            
            // IAppGatewayAuthenticator interface
            virtual Core::hresult Authenticate(const string &sessionId /* @in */, string &appId /* @out */) override;
            virtual Core::hresult GetSessionId(const string &appId /* @in */, string &sessionId /* @out */) override;
            virtual Core::hresult CheckPermissionGroup(const string &appId /* @in */,
                                               const string &permissionGroup /* @in */,
                                               bool &allowed /* @out */) override;

        private:
            void Deactivated(RPC::IRemoteConnection* connection);
            // Helper methods for System/Device - called by HandleAppGatewayRequest
            Core::hresult GetDeviceMake(string &make /* @out */);
            Core::hresult GetDeviceName(string &name /* @out */);
            Core::hresult SetDeviceName(const string name /* @in */);
            Core::hresult GetDeviceSku(string &sku /* @out */);
            Core::hresult GetCountryCode(string &countryCode /* @out */);
            Core::hresult SetCountryCode(const string countryCode /* @in */);
            Core::hresult GetTimeZone(string &timeZone /* @out */);
            Core::hresult SetTimeZone(const string timeZone /* @in */);
            Core::hresult GetSecondScreenFriendlyName(string &name /* @out */);
            Core::hresult SetName(const string &value /* @in */, string &result /* @out */);
            Core::hresult AddAdditionalInfo(const string &value /* @in */, string &result /* @out */);

            // Helper methods for network status - called by HandleAppGatewayRequest
            Core::hresult GetInternetConnectionStatus(string &result /* @out */);

            // Helper methods for UserSettings - called by HandleAppGatewayRequest
            Core::hresult GetVoiceGuidance(string &result /* @out */);
            Core::hresult GetAudioDescription(string &result /* @out */);
            Core::hresult GetAudioDescriptionsEnabled(string &result /* @out */);
            Core::hresult GetHighContrast(string &result /* @out */);
            Core::hresult GetCaptions(string &result /* @out */);
            Core::hresult GetPinControl(string &result /* @out */);
            Core::hresult GetBlockNotRatedContent(string &result /* @out */);
            Core::hresult GetViewingRestrictions(string &result /* @out */);
            Core::hresult SetVoiceGuidance(const bool enabled /* @in */);
            Core::hresult SetAudioDescriptionsEnabled(const bool enabled /* @in */);
            Core::hresult SetCaptions(const bool enabled /* @in */);
            Core::hresult GetPresentationLanguage(string &result /* @out */);
            Core::hresult GetLocale(string &result /* @out */);
            Core::hresult SetLocale(const string &locale /* @in */);
            Core::hresult GetPreferredAudioLanguages(string &result /* @out */);
            Core::hresult GetPreferredCaptionsLanguages(string &result /* @out */);
            Core::hresult SetPreferredAudioLanguages(const string &languages /* @in */);
            Core::hresult SetPreferredCaptionsLanguages(const string &preferredLanguages /* @in */);
            Core::hresult SetSpeed(const double speed /* @in */);
            Core::hresult GetSpeed(double &speed /* @out */);
            Core::hresult GetVoiceGuidanceHints(string &result /* @out */);
            Core::hresult SetVoiceGuidanceHints(const bool enabled /* @in */);
            Core::hresult GetVoiceGuidanceSettings(const bool addSpeed, string &result /* @out */);
            Core::hresult GetClosedCaptionsSettings(string &result /* @out */);
            Core::hresult GetFirmwareVersion(string &result /* @out */);
            Core::hresult GetScreenResolution(string &result /* out */);
            Core::hresult GetVideoResolution(string &result /* out */);
            Core::hresult GetHdcp(string &result /* @out */);
            Core::hresult GetHdr(string &result /* @out */);
            Core::hresult GetAudio(string &result /* @out */);
            // Helper methods for VideoOutput APIs - called by HandleAppGatewayRequest
            Core::hresult GetVideoOutputResolution(string &result /* @out */);
            Core::hresult GetVideoOutputHdcp(string &result /* @out */);
            Core::hresult GetVideoOutputCecActiveState(string &result /* @out */);
            Core::hresult GetVideoOutputPort(string &result /* @out */);
            Core::hresult GetVideoOutputRefreshRate(string &result /* @out */);
            Core::hresult GetVideoOutputColorDepth(string &result /* @out */);
            Core::hresult GetVideoOutputColorFormat(string &result /* @out */);
            Core::hresult GetVideoOutputColorimetry(string &result /* @out */);
            Core::hresult GetVideoOutputDynamicRange(string &result /* @out */);
            Core::hresult GetVideoOutputQuantizationRange(string &result /* @out */);
            Core::hresult GetDolbyAtmosExperience(string &result /* @out */);
            // Helper methods for Device info APIs - called by HandleAppGatewayRequest
            Core::hresult GetDeviceChipsetId(string &result /* @out */);
            Core::hresult GetDeviceClass(string &result /* @out */);
            Core::hresult GetDeviceUptime(string &result /* @out */);
            Core::hresult GetDeviceTimeInActiveState(string &result /* @out */);
            // Helper methods for Device Branding APIs (Phase 1) - called by HandleAppGatewayRequest
            Core::hresult SetDeviceOsName(const string &osName /* @in */);
            Core::hresult GetDeviceOsName(string &result /* @out */);
            Core::hresult SetDeviceOsVersion(const string &osVersion /* @in */);
            Core::hresult GetDeviceOsVersion(string &result /* @out */);
            Core::hresult GetDeviceFirmware(string &result /* @out */);
            Core::hresult GetStatsMemoryUsage(const string &appId /* @in */, string &result /* @out */);
            Core::hresult LifecycleFinished(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result);
            Core::hresult LifecycleReady(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result);
            Core::hresult LifecycleClose(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result);
            Core::hresult LifecycleState(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result);
            Core::hresult Lifecycle2State(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result);
            Core::hresult Lifecycle2Close(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result);
            Core::hresult DispatchLastIntent(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result);
            Core::hresult GetLastIntent(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result);
            Core::hresult ActionsStart(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result);
            Core::hresult ActionsIntent(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result);
            Core::hresult GetPresentationFocused(const Exchange::GatewayContext& ctx, const std::string& payload, std::string &result /* @out */);
            Core::hresult SetIntent(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result);
            
            // EventRegistrationJob helper method (AppGatewayCommon-specific)
            bool SafeSubmitEventRegistrationJob(Exchange::IAppNotificationHandler::IEmitter* cb, 
                                               const std::string& event, bool listen);

            Core::hresult HandleAppDelegateRequest(const Exchange::GatewayContext& context ,
                                          const string& method ,
                                          const string& payload /*@opaque */,
                                          string& result /*@out @opaque */);
            Core::hresult SpeechSynthesisVoices(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result);
            Core::hresult SpeechSynthesisSpeak(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result);
            Core::hresult SpeechSynthesisCancel(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result);
            Core::hresult SpeechSynthesisPause(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result);
            Core::hresult SpeechSynthesisResume(const Exchange::GatewayContext& ctx, const std::string& payload, std::string& result);
            Core::hresult GetNetworkConnected(string &result /* @out */);
            // Helper methods for Display APIs - called by HandleAppGatewayRequest
            Core::hresult GetDisplayEdid(string &result /* @out */);
            Core::hresult GetDisplaySize(string &result /* @out */);
            Core::hresult GetDisplayMaxResolution(string &result /* @out */);
            Core::hresult GetDisplayColorimetry(string &result /* @out */);
            Core::hresult GetDisplayVideoResolutions(string &result /* @out */);
            Core::hresult TextToSpeechSpeak(const Exchange::GatewayContext& ctx, const string& payload, string& result /* @out */);
        private:
            PluginHost::IShell* mShell;
            uint32_t mConnectionId;
            std::shared_ptr<SettingsDelegate> mDelegate;

            // Track in-flight EventRegistrationJobs so Deinitialize() can
            // wait for them before destroying the delegate.
            std::atomic<int> mActiveJobs{0};
            std::mutex mJobDrainMutex;
            std::condition_variable mJobDrainCv;
        };
	} // namespace Plugin
} // namespace WPEFramework

