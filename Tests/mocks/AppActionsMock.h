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

#include <gmock/gmock.h>
#include <interfaces/IAppActions.h>

using ::WPEFramework::Exchange::IAppActions;

class AppActionsMock : public IAppActions {
public:
    AppActionsMock() = default;
    virtual ~AppActionsMock() = default;

    MOCK_METHOD(WPEFramework::Core::hresult, ActionStart,
                (const std::string& initiator, const std::string& intent, const std::string& handlerAppId), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Register,   (INotification* sink), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, Unregister, (INotification* sink), (override));

    MOCK_METHOD(uint32_t, AddRef,         (), (const, override));
    MOCK_METHOD(uint32_t, Release,        (), (const, override));
    MOCK_METHOD(void*,    QueryInterface, (const uint32_t interfaceNumber), (override));
};
