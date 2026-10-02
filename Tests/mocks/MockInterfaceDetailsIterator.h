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
#include <interfaces/INetworkManager.h>

/**
 * Extension mock for INetworkManager::IInterfaceDetailsIterator.
 *
 * The test framework (entservices-testframework) does not provide this iterator
 * mock. It is required by AppGatewayCommon network tests that exercise
 * GetAvailableInterfaces().
 */
class MockInterfaceDetailsIterator : public WPEFramework::Exchange::INetworkManager::IInterfaceDetailsIterator {
public:
    ~MockInterfaceDetailsIterator() override = default;

    MOCK_METHOD(uint32_t, AddRef, (), (const, override));
    MOCK_METHOD(uint32_t, Release, (), (const, override));

    MOCK_METHOD(bool, Next, (WPEFramework::Exchange::INetworkManager::InterfaceDetails & details), (override));
    MOCK_METHOD(bool, Previous, (WPEFramework::Exchange::INetworkManager::InterfaceDetails & details), (override));
    MOCK_METHOD(void, Reset, (const uint32_t position), (override));
    MOCK_METHOD(bool, IsValid, (), (const, override));
    MOCK_METHOD(uint32_t, Count, (), (const, override));
    MOCK_METHOD(WPEFramework::Exchange::INetworkManager::InterfaceDetails, Current, (), (const, override));

    BEGIN_INTERFACE_MAP(MockInterfaceDetailsIterator)
    INTERFACE_ENTRY(WPEFramework::Exchange::INetworkManager::IInterfaceDetailsIterator)
    END_INTERFACE_MAP
};
