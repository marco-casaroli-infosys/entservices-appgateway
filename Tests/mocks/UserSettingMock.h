/*
* If not stated otherwise in this file or this component's LICENSE file the
* following copyright and licenses apply:
*
* Copyright 2025 RDK Management
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
#include <interfaces/IUserSettings.h>

using ::WPEFramework::Exchange::IUserSettings;
using ::WPEFramework::Exchange::IUserSettingsInspector;

class UserSettingMock : public IUserSettings, public IUserSettingsInspector {
    public:
    UserSettingMock() = default;
        virtual ~UserSettingMock() = default;

        MOCK_METHOD(uint32_t, Register, (IUserSettings::INotification* notification), (override));
        MOCK_METHOD(uint32_t, Unregister, (IUserSettings::INotification* notification), (override));
        MOCK_METHOD(uint32_t, SetAudioDescription, (const bool enabled), (override));
        MOCK_METHOD(uint32_t, GetAudioDescription, (bool& enabled), (const, override));
        MOCK_METHOD(uint32_t, SetPreferredAudioLanguages, (const string& preferredLanguages), (override));
        MOCK_METHOD(uint32_t, GetPreferredAudioLanguages, (string& preferredLanguages), (const, override));
        MOCK_METHOD(uint32_t, SetPresentationLanguage, (const string& presentationLanguage), (override));
        MOCK_METHOD(uint32_t, GetPresentationLanguage, (string& presentationLanguage), (const, override));
        MOCK_METHOD(uint32_t, SetCaptions, (const bool enabled), (override));
        MOCK_METHOD(uint32_t, GetCaptions, (bool& enabled), (const, override));
        MOCK_METHOD(uint32_t, SetPreferredCaptionsLanguages, (const string& preferredLanguages), (override));
        MOCK_METHOD(uint32_t, GetPreferredCaptionsLanguages, (string& preferredLanguages), (const, override));
        MOCK_METHOD(uint32_t, SetPreferredClosedCaptionService, (const string& service), (override));
        MOCK_METHOD(uint32_t, GetPreferredClosedCaptionService, (string& service), (const, override));
        MOCK_METHOD(uint32_t, SetPrivacyMode, (const string& privacyMode), (override));
        MOCK_METHOD(uint32_t, GetPrivacyMode, (string &privacyMode), (const, override));
        MOCK_METHOD(uint32_t, SetPinControl, (const bool pinControl), (override));
        MOCK_METHOD(uint32_t, GetPinControl, (bool& pinControl), (const, override));
        MOCK_METHOD(uint32_t, SetViewingRestrictions, (const string& viewingRestrictions), (override));
        MOCK_METHOD(uint32_t, GetViewingRestrictions, (string& viewingRestrictions), (const, override));
        MOCK_METHOD(uint32_t, SetViewingRestrictionsWindow, (const string& viewingRestrictionsWindow), (override));
        MOCK_METHOD(uint32_t, GetViewingRestrictionsWindow, (string& viewingRestrictionsWindow), (const, override));
        MOCK_METHOD(uint32_t, SetLiveWatershed, (const bool liveWatershed), (override));
        MOCK_METHOD(uint32_t, GetLiveWatershed, (bool& liveWatershed), (const, override));
        MOCK_METHOD(uint32_t, SetPlaybackWatershed, (const bool playbackWatershed), (override));
        MOCK_METHOD(uint32_t, GetPlaybackWatershed, (bool& playbackWatershed), (const, override));
        MOCK_METHOD(uint32_t, SetBlockNotRatedContent, (const bool blockNotRatedContent), (override));
        MOCK_METHOD(uint32_t, GetBlockNotRatedContent, (bool& blockNotRatedContent), (const, override));
        MOCK_METHOD(uint32_t, SetPinOnPurchase, (const bool pinOnPurchase), (override));
        MOCK_METHOD(uint32_t, GetPinOnPurchase, (bool& pinOnPurchase), (const, override));
        MOCK_METHOD(uint32_t, SetHighContrast, (const bool enabled), (override));
        MOCK_METHOD(uint32_t, GetHighContrast, (bool& enabled), (const, override));
        MOCK_METHOD(uint32_t, SetVoiceGuidance, (const bool enabled), (override));
        MOCK_METHOD(uint32_t, GetVoiceGuidance, (bool& enabled), (const, override));
        MOCK_METHOD(uint32_t, SetVoiceGuidanceRate, (const double rate), (override));
        MOCK_METHOD(uint32_t, GetVoiceGuidanceRate, (double& rate), (const, override));
        MOCK_METHOD(uint32_t, SetVoiceGuidanceHints, (const bool hints), (override));
        MOCK_METHOD(uint32_t, GetVoiceGuidanceHints, (bool& hints), (const, override));
	MOCK_METHOD(uint32_t, SetContentPin, (const string& contentPin), (override));
        MOCK_METHOD(uint32_t, GetContentPin, (string& contentPin), (const, override));

        MOCK_METHOD(WPEFramework::Core::hresult, GetMigrationState, (const SettingsKey key, bool& requiresMigration), (const, override));
        MOCK_METHOD(WPEFramework::Core::hresult, GetMigrationStates, (IUserSettingsMigrationStateIterator*& states), (const, override));

        MOCK_METHOD(uint32_t, AddRef, (), (const, override));
        MOCK_METHOD(uint32_t, Release, (), (const, override));
        MOCK_METHOD(void*, QueryInterface, (const uint32_t interfaceId), (override));
};
