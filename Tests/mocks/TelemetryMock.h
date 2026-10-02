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

#include "Telemetry.h"
#include <interfaces/ITelemetry.h>

using ::WPEFramework::Core::hresult;
using ::WPEFramework::Exchange::ITelemetry;

class TelemetryApiImplMock : public TelemetryApiImpl, public ITelemetry {
public:
    virtual ~TelemetryApiImplMock() = default;

    MOCK_METHOD(void, t2_init, (char *component), (override));
    MOCK_METHOD(T2ERROR, t2_event_s, (char* marker, char* value), (override));

    MOCK_METHOD(hresult, Register, (INotification *notification), (override));
    MOCK_METHOD(hresult, Unregister, (INotification *notification), (override));
    MOCK_METHOD(hresult, SetReportProfileStatus, (const string &status), (override));
    MOCK_METHOD(hresult, LogApplicationEvent, (const string &eventName , const string &eventValue), (override));
    MOCK_METHOD(hresult, UploadReport, (), (override));
    MOCK_METHOD(hresult, AbortReport, (), (override));
    MOCK_METHOD(hresult, SetOptOutTelemetry, (const bool optOut , TelemetrySuccess& successResult), (override));
    MOCK_METHOD(hresult, IsOptOutTelemetry, (bool& optOut , bool& success), (override));
    MOCK_METHOD(uint32_t, AddRef, (), (const, override));
    MOCK_METHOD(uint32_t, Release, (), (const, override));
    MOCK_METHOD(void*, QueryInterface, (const uint32_t interfacenumber), (override));

};

class TelemetryNotificationMock : public ITelemetry::INotification {
public:
    TelemetryNotificationMock() = default;
    virtual ~TelemetryNotificationMock() = default;

    MOCK_METHOD(void, OnReportUpload, (const string& telemetryUploadStatus), (override));
};

