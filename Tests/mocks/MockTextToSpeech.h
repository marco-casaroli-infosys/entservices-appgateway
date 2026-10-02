/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
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
 */

#pragma once

#include <gmock/gmock.h>
#include <interfaces/ITextToSpeech.h>

class MockTextToSpeech : public WPEFramework::Exchange::ITextToSpeech {
public:
    ~MockTextToSpeech() override = default;

    MOCK_METHOD(uint32_t, AddRef, (), (const, override));
    MOCK_METHOD(uint32_t, Release, (), (const, override));

    MOCK_METHOD(WPEFramework::Core::hresult, Register, (INotification * sink), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Unregister, (INotification * sink), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, RegisterWithCallsign, (const string callsign, INotification* sink), (override));

    MOCK_METHOD(WPEFramework::Core::hresult, Enable, (const bool enable), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Enable, (bool & enable), (const, override));

    MOCK_METHOD(WPEFramework::Core::hresult, SetConfiguration, (const Configuration& config, TTSErrorDetail& status), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, GetConfiguration, (Configuration & config), (const, override));

    MOCK_METHOD(WPEFramework::Core::hresult, SetFallbackText, (const string scenario, const string value), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, SetAPIKey, (const string apikey), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, SetPrimaryVolDuck, (const uint8_t primaryVolDuck), (override));

    MOCK_METHOD(WPEFramework::Core::hresult, ListVoices, (const string language, WPEFramework::RPC::IStringIterator*& voices), (const, override));
    MOCK_METHOD(WPEFramework::Core::hresult, Speak, (const string callsign, const string text, uint32_t& speechid, TTSErrorDetail& status), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Cancel, (const uint32_t speechid), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Pause, (const uint32_t speechid, TTSErrorDetail& status), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Resume, (const uint32_t speechid, TTSErrorDetail& status), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, GetSpeechState, (const uint32_t speechid, SpeechState& state), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, SetACL, (const string method, const string apps), (override));

    MOCK_METHOD(WPEFramework::Core::hresult, GetVoices, (const string& language, IVoiceInfoIterator*& voices), (const, override));
    MOCK_METHOD(WPEFramework::Core::hresult, SpeakWithUtterance, (const string& callsign, const SpeechUtterance& utterance, const string& text, uint32_t& speechid, TTSErrorDetail& status), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, GetInterfaceVersion, (uint32_t& version), (const, override));
    MOCK_METHOD(WPEFramework::Core::hresult, GetCapability, (Capability capability, bool& hasCapability), (const, override));
    MOCK_METHOD(WPEFramework::Core::hresult, GetCapabilities, (ICapabilityIterator*& capabilities), (const, override));
    MOCK_METHOD(WPEFramework::Core::hresult, GetDeviceConfiguration, (DeviceConfiguration& config), (const, override));
    MOCK_METHOD(WPEFramework::Core::hresult, SetDeviceConfiguration, (const DeviceConfiguration& config), (override));

    BEGIN_INTERFACE_MAP(MockTextToSpeech)
    INTERFACE_ENTRY(WPEFramework::Exchange::ITextToSpeech)
    END_INTERFACE_MAP
};
