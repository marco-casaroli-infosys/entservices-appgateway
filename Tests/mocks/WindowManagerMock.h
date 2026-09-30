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
#include <interfaces/IRDKWindowManager.h>

using ::WPEFramework::Exchange::IRDKWindowManager;

/**
 * WindowManagerMock — aligned with entservices-testframework naming.
 *
 * Aligned to the IRDKWindowManager interface currently provided
 * by entservices-apis.
 */
class WindowManagerMock : public IRDKWindowManager {
public:
    WindowManagerMock() = default;
    virtual ~WindowManagerMock() = default;

    MOCK_METHOD(WPEFramework::Core::hresult, Register, (INotification *notification), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Unregister, (INotification *notification), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Initialize, (WPEFramework::PluginHost::IShell* service), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Deinitialize, (WPEFramework::PluginHost::IShell* service), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, CreateDisplay, (const string& displayParams));
    MOCK_METHOD(WPEFramework::Core::hresult, CreateDisplay,
    (const string& clientId,
     const string& displayName,
     const uint32_t displayWidth,
     const uint32_t displayHeight,
     const bool virtualDisplay,
     const uint32_t virtualWidth,
     const uint32_t virtualHeight,
     const uint32_t ownerId,
     const uint32_t groupId,
     const bool topmost,
    const bool focus,
    const string& capabilities),
    (override));
    MOCK_METHOD(WPEFramework::Core::hresult, GetApps, (IRDKWindowManager::IStringIterator*& appsIds), (const, override));
    MOCK_METHOD(WPEFramework::Core::hresult, GetFocused, (string& client), (const, override));
    MOCK_METHOD(WPEFramework::Core::hresult, ShowSplashScreen, (const bool show), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, SetBounds, (const string& clientId, const uint32_t x, const uint32_t y, const uint32_t width, const uint32_t height), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, GetBounds, (const string& clientId, uint32_t& x, uint32_t& y, uint32_t& width, uint32_t& height), (const, override));
    MOCK_METHOD(WPEFramework::Core::hresult, SetScale, (const string& clientId, const double scaleX, const double scaleY), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, GetScale, (const string& clientId, double& scaleX, double& scaleY), (const, override));
    MOCK_METHOD(WPEFramework::Core::hresult, SetFocus, (const string& client), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, SetVisible, (const std::string& client, bool visible), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, AddKeyIntercept, (const string& intercept), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, AddKeyIntercepts, (const string& intercepts));
    MOCK_METHOD(WPEFramework::Core::hresult, AddKeyIntercepts, (const string& clientId, const string& intercepts));
    MOCK_METHOD(WPEFramework::Core::hresult, RemoveKeyIntercept, (const string& intercept));
    MOCK_METHOD(WPEFramework::Core::hresult, RemoveKeyIntercept, (const string& clientId, const uint32_t keyCode, const string& modifiers));
    MOCK_METHOD(WPEFramework::Core::hresult, AddKeyListener, (const string& keyListeners), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, RemoveKeyListener, (const string& keyListeners), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, InjectKey, (uint32_t keyCode, const string& modifiers), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, GenerateKey, (const string& keys, const string& client), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, EnableInactivityReporting, (const bool enable), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, SetInactivityInterval, (const uint32_t interval), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, ResetInactivityTime, (), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, EnableKeyRepeats, (bool enable), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, GetKeyRepeatsEnabled, (bool& keyRepeat), (const, override));
    MOCK_METHOD(WPEFramework::Core::hresult, IgnoreKeyInputs, (bool ignore), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, EnableInputEvents, (const string& clients, bool enable), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, KeyRepeatConfig, (const string& input, const string& keyConfig), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, RenderReady, (const string& client, bool& status), (const, override));
    MOCK_METHOD(WPEFramework::Core::hresult, EnableDisplayRender, (const string& client, bool enable), (override));

    // Legacy methods present in some IRDKWindowManager revisions.
    MOCK_METHOD(WPEFramework::Core::hresult, GetVisibility, (const std::string& client, bool& visible));
    MOCK_METHOD(WPEFramework::Core::hresult, GetLastKeyInfo, (uint32_t& keyCode, uint32_t& modifiers, uint64_t& timestampInSeconds), (const));
    MOCK_METHOD(WPEFramework::Core::hresult, SetZOrder, (const string& appInstanceId, const int32_t zOrder));
    MOCK_METHOD(WPEFramework::Core::hresult, GetZOrder, (const string& appInstanceId, int32_t& zOrder));
    MOCK_METHOD(WPEFramework::Core::hresult, SetAlias, (const string& clientId, const string& alias));
    MOCK_METHOD(WPEFramework::Core::hresult, StartVncServer, ());
    MOCK_METHOD(WPEFramework::Core::hresult, StopVncServer, ());
    MOCK_METHOD(WPEFramework::Core::hresult, GetScreenshot, ());

    MOCK_METHOD(uint32_t, AddRef, (), (const, override));
    MOCK_METHOD(uint32_t, Release, (), (const, override));
    MOCK_METHOD(void*, QueryInterface, (const uint32_t interfaceNumber), (override));
};

class WindowManagerNotificationMock : public IRDKWindowManager::INotification {
public:
    WindowManagerNotificationMock() = default;
    virtual ~WindowManagerNotificationMock() = default;

    MOCK_METHOD(void, OnUserInactivity, (const double minutes), (override));
    MOCK_METHOD(void, OnDisconnected, (const std::string& client), (override));
    MOCK_METHOD(void, OnReady, (const std::string& client), (override));
};
