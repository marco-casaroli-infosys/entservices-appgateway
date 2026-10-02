/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2024 RDK Management
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

#include <gmock/gmock.h>
#include <interfaces/ILifecycleManager.h>
#include <interfaces/ILifecycleManagerState.h>

using ::WPEFramework::Exchange::ILifecycleManager;
using ::WPEFramework::Exchange::ILifecycleManagerState;

class LifecycleManagerMock : public ILifecycleManager {
public:
    LifecycleManagerMock() = default;
    virtual ~LifecycleManagerMock() = default;

    MOCK_METHOD(WPEFramework::Core::hresult, Register, (INotification *notification), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Unregister, (INotification *notification), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, GetLoadedApps, (const bool verbose, std::string &apps), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, IsAppLoaded, (const std::string &appId, bool &loaded), (const, override));
    MOCK_METHOD(WPEFramework::Core::hresult, SpawnApp, (const string& appId, const string& launchIntent, const LifecycleState targetLifecycleState, const WPEFramework::Exchange::RuntimeConfig& runtimeConfigObject, const string& launchArgs, string& appInstanceId, string& errorReason, bool& success), (override));
    MOCK_METHOD(uint32_t, SetTargetAppState, (const string& appInstanceId , const LifecycleState targetLifecycleState , const string& launchIntent),(override));
    MOCK_METHOD(WPEFramework::Core::hresult, UnloadApp,(const string& appInstanceId , string& errorReason , bool& success) , (override));
    MOCK_METHOD(WPEFramework::Core::hresult, KillApp,(const string& appInstanceId , string& errorReason , bool& success) , (override));
    MOCK_METHOD(WPEFramework::Core::hresult, SendIntentToActiveApp,(const string& appInstanceId , const string& intent , string& errorReason , bool& success), (override)) ;

    MOCK_METHOD(uint32_t, AddRef, (), (const, override));
    MOCK_METHOD(uint32_t, Release, (), (const, override));
    MOCK_METHOD(void*, QueryInterface, (const uint32_t interfaceNummer), (override));

};


class LifecycleManagerNotificationMock : public ILifecycleManager::INotification {
    public:
    LifecycleManagerNotificationMock() = default;
    virtual ~LifecycleManagerNotificationMock() = default;
};

class LifecycleManagerStateMock : public ILifecycleManagerState {
public:
    LifecycleManagerStateMock() = default;
    virtual ~LifecycleManagerStateMock() = default;

    MOCK_METHOD(WPEFramework::Core::hresult, Register, (INotification *notification), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Unregister, (INotification *notification), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, AppReady, (const string& appId), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, StateChangeComplete, (const string& appId, const uint32_t stateChangedId, const bool success ), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, CloseApp, (const string& appId, const AppCloseReason closeReason ), (override));

    MOCK_METHOD(uint32_t, AddRef, (), (const, override));
    MOCK_METHOD(uint32_t, Release, (), (const, override));
    MOCK_METHOD(void*, QueryInterface, (const uint32_t interfaceNummer), (override));
};

