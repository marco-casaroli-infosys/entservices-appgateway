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

#include <string>

#include <interfaces/IAudioOutput.h>
#include <plugins/plugins.h>

#include "BaseEventDelegate.h"
#include "UtilsController.h"
#include "UtilsLogging.h"

using namespace WPEFramework;

#ifndef AVOUTPUT_CALLSIGN
#define AVOUTPUT_CALLSIGN "org.rdk.AudioOutput"
#endif

class AvOutputDelegate : public BaseEventDelegate
{
public:
    static constexpr const char* EVENT_ON_DOLBY_ATMOS_EXPERIENCE_AVAILABLE_CHANGED = "Device.onDolbyAtmosExperienceAvailableChanged";

    explicit AvOutputDelegate(PluginHost::IShell* shell)
        : BaseEventDelegate()
        , _shell(shell)
        , _audioOutput(nullptr)
        , _audioOutputSubscribed(false)
        , _audioOutputNotificationHandler(*this)
    {
    }

    ~AvOutputDelegate()
    {
        try {
            if (_audioOutput && isAudioOutputSubscribed()) {
                _audioOutput->Unregister(&_audioOutputNotificationHandler);
            }
        } catch (...) {
        }

        if (_audioOutput) {
            _audioOutput->Release();
            _audioOutput = nullptr;
        }
        _shell = nullptr;
    }

    Core::hresult GetDolbyAtmosExperience(std::string& result)
    {
        bool available = false;
        auto* audioOutput = AcquireAudioOutputInterface();
        if (audioOutput == nullptr) {
            LOGERR("AvOutputDelegate: AudioOutput COM-RPC interface unavailable, returning false");
            result = "false";
            return Core::ERROR_UNAVAILABLE;
        }

        const uint32_t rc = audioOutput->DolbyAtmosExperience(available);
        audioOutput->Release();
        if (rc != Core::ERROR_NONE) {
            LOGERR("AvOutputDelegate: AudioOutput::DolbyAtmosExperience failed rc=%u", rc);
            result = "false";
            return Core::ERROR_GENERAL;
        }

        result = available ? "true" : "false";
        return Core::ERROR_NONE;
    }

    bool EmitOnDolbyAtmosExperienceAvailableChanged()
    {
        std::string payload;
        if (GetDolbyAtmosExperience(payload) != Core::ERROR_NONE) {
            LOGERR("[AppGatewayCommon|DolbyAtmosExperienceChanged] handler=GetDolbyAtmosExperience failed to compute payload");
            return false;
        }
        if (payload.empty()) {
            LOGERR("[AppGatewayCommon|DolbyAtmosExperienceChanged] handler=GetDolbyAtmosExperience returned empty payload");
            return false;
        }

        Dispatch(EVENT_ON_DOLBY_ATMOS_EXPERIENCE_AVAILABLE_CHANGED, payload);
        return true;
    }

    bool HandleEvent(Exchange::IAppNotificationHandler::IEmitter* cb, const std::string& event, const bool listen, bool& registrationError) override
    {
        registrationError = false;
        const std::string evLower = StringUtils::toLower(event);

        if (evLower == "device.ondolbyatmosexperienceavailablechanged") {
            SetupAudioOutputSubscription();
        } else {
            registrationError = true;
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

private:
    void SetupAudioOutputSubscription()
    {
        if (isAudioOutputSubscribed()) {
            return;
        }

        try {
            if (!_audioOutput) {
                _audioOutput = AcquireAudioOutputInterface();
            }

            if (_audioOutput) {
                const uint32_t status = _audioOutput->Register(&_audioOutputNotificationHandler);
                if (status == Core::ERROR_NONE) {
                    markAudioOutputSubscribed();
                    LOGINFO("AvOutputDelegate: Registered for %s COM-RPC notifications", AVOUTPUT_CALLSIGN);
                } else {
                    LOGERR("AvOutputDelegate: Failed to register %s COM-RPC notifications rc=%u", AVOUTPUT_CALLSIGN, status);
                }
            } else {
                LOGERR("AvOutputDelegate: Failed to acquire %s COM-RPC interface", AVOUTPUT_CALLSIGN);
            }
        } catch (...) {
            LOGERR("AvOutputDelegate: exception during AudioOutput COM-RPC registration");
        }
    }

    void OnDolbyAtmosExperienceChanged(const bool available)
    {
        const std::string payload = available ? "true" : "false";
        Dispatch(EVENT_ON_DOLBY_ATMOS_EXPERIENCE_AVAILABLE_CHANGED, payload);
        LOGINFO("[AppGatewayCommon|AudioOutput.onDolbyAtmosExperienceChanged] Dispatched payload=%s",
            payload.c_str());
    }

    class AudioOutputNotificationHandler : public Exchange::IAudioOutput::INotification {
    public:
        explicit AudioOutputNotificationHandler(AvOutputDelegate& parent)
            : _parent(parent)
        {
        }

        void OnDolbyAtmosExperienceChanged(const bool dolbyAtmosExperience) override
        {
            _parent.OnDolbyAtmosExperienceChanged(dolbyAtmosExperience);
        }

        BEGIN_INTERFACE_MAP(AudioOutputNotificationHandler)
        INTERFACE_ENTRY(Exchange::IAudioOutput::INotification)
        END_INTERFACE_MAP

    private:
        AvOutputDelegate& _parent;
    };

    bool isAudioOutputSubscribed() const
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_audioOutputSubscriptionLock);
        return _audioOutputSubscribed;
    }

    void markAudioOutputSubscribed()
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_audioOutputSubscriptionLock);
        _audioOutputSubscribed = true;
    }

    Exchange::IAudioOutput* AcquireAudioOutputInterface() const
    {
        if (nullptr == _shell) {
            LOGERR("AvOutputDelegate: shell is null");
            return nullptr;
        }

        return _shell->QueryInterfaceByCallsign<Exchange::IAudioOutput>(AVOUTPUT_CALLSIGN);
    }

private:
    PluginHost::IShell* _shell;
    Exchange::IAudioOutput* _audioOutput;
    bool _audioOutputSubscribed;
    Core::Sink<AudioOutputNotificationHandler> _audioOutputNotificationHandler;
    mutable Core::CriticalSection _audioOutputSubscriptionLock;
};
