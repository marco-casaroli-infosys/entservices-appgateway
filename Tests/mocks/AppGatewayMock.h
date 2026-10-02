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

#include <atomic>
#include <gmock/gmock.h>
#include <interfaces/IAppGateway.h>

using ::WPEFramework::Exchange::IAppGatewayResponder;

class AppGatewayResponderMock : public IAppGatewayResponder {
public:
    AppGatewayResponderMock()
        : _refCount(1)
    {
    }

    ~AppGatewayResponderMock() override = default;

    BEGIN_INTERFACE_MAP(AppGatewayResponderMock)
    INTERFACE_ENTRY(IAppGatewayResponder)
    END_INTERFACE_MAP

    // Real ref-counting so Release() deletes when count hits zero — prevents mock leaks.
    uint32_t AddRef() const override { return ++_refCount; }
    uint32_t Release() const override
    {
        const uint32_t result = --_refCount;
        if (0 == result) {
            delete this;
        }
        return result;
    }

    MOCK_METHOD(WPEFramework::Core::hresult, Respond, (const WPEFramework::Exchange::GatewayContext& context, const string& payload), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Emit, (const WPEFramework::Exchange::GatewayContext& context, const string& method, const string& payload), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Request, (const uint32_t connectionId, const uint32_t id, const string& method, const string& params), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, GetGatewayConnectionContext, (const uint32_t connectionId, const string& contextKey, string& contextValue), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, RecordGatewayConnectionContext, (const uint32_t connectionId, const string& contextKey, const string& contextValue), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Register, (INotification* notification), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Unregister, (INotification* notification), (override));

private:
    mutable std::atomic<uint32_t> _refCount;
};

class AppGatewayResponderNotificationMock : public IAppGatewayResponder::INotification {
public:
    AppGatewayResponderNotificationMock() = default;
    virtual ~AppGatewayResponderNotificationMock() = default;

    MOCK_METHOD(void, OnAppConnectionChanged, (const string& appId, const uint32_t connectionId, const bool connected), (override));

    MOCK_METHOD(uint32_t, AddRef, (), (const, override));
    MOCK_METHOD(uint32_t, Release, (), (const, override));
    MOCK_METHOD(void*, QueryInterface, (const uint32_t interfaceNummer), (override));
};
