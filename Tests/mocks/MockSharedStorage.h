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
#include <interfaces/ISharedStorage.h>

class MockSharedStorage : public WPEFramework::Exchange::ISharedStorage {
public:
    ~MockSharedStorage() override = default;

    MOCK_METHOD(uint32_t, AddRef, (), (const, override));
    MOCK_METHOD(uint32_t, Release, (), (const, override));

    MOCK_METHOD(WPEFramework::Core::hresult, Register, (INotification * notification), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Unregister, (INotification * notification), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, SetValue,
        (const ScopeType scope, const string& ns, const string& key,
         const string& value, const uint32_t ttl, Success& success),
        (override));
    MOCK_METHOD(WPEFramework::Core::hresult, GetValue,
        (const ScopeType scope, const string& ns, const string& key,
         string& value, uint32_t& ttl, bool& success),
        (override));
    MOCK_METHOD(WPEFramework::Core::hresult, DeleteKey,
        (const ScopeType scope, const string& ns, const string& key, Success& success),
        (override));
    MOCK_METHOD(WPEFramework::Core::hresult, DeleteNamespace,
        (const ScopeType scope, const string& ns, Success& success),
        (override));

    BEGIN_INTERFACE_MAP(MockSharedStorage)
    INTERFACE_ENTRY(WPEFramework::Exchange::ISharedStorage)
    END_INTERFACE_MAP
};
