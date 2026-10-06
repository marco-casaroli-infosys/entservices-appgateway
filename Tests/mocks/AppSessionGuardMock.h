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
#include <interfaces/IAppGateway.h>
#include <atomic>
#include <string>
#include <vector>
#include <mutex>

using ::WPEFramework::Exchange::IAppGatewayAppSessionGuard;

// GMock-based mock for IAppGatewayAppSessionGuard.
// Used in L1 tests to verify SuspendTraffic / ResumeTraffic call ordering.
class AppSessionGuardMock : public IAppGatewayAppSessionGuard {
public:
    AppSessionGuardMock() = default;
    virtual ~AppSessionGuardMock() = default;

    MOCK_METHOD(WPEFramework::Core::hresult, SuspendTraffic, (const std::string& appId), (override));
    MOCK_METHOD(WPEFramework::Core::hresult, ResumeTraffic,  (const std::string& appId), (override));

    MOCK_METHOD(uint32_t, AddRef,         (), (const, override));
    MOCK_METHOD(uint32_t, Release,        (), (const, override));
    MOCK_METHOD(void*,    QueryInterface, (const uint32_t interfaceNumber), (override));
};

// Lightweight hand-rolled spy for IAppGatewayAppSessionGuard.
// Records all calls with ordering information, without requiring GMock macros.
// Suitable for use in L0 tests that have no GTest/GMock dependency.
class AppSessionGuardSpy : public IAppGatewayAppSessionGuard {
public:
    AppSessionGuardSpy() : _refCount(1) {}
    ~AppSessionGuardSpy() override = default;

    uint32_t AddRef() const override
    {
        return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    uint32_t Release() const override
    {
        const uint32_t prev = _refCount.fetch_sub(1, std::memory_order_acq_rel);
        if (1 == prev) {
            delete this;
            return WPEFramework::Core::ERROR_DESTRUCTION_SUCCEEDED;
        }
        return WPEFramework::Core::ERROR_NONE;
    }

    void* QueryInterface(const uint32_t id) override
    {
        if (IAppGatewayAppSessionGuard::ID == id) {
            AddRef();
            return static_cast<IAppGatewayAppSessionGuard*>(this);
        }
        return nullptr;
    }

    WPEFramework::Core::hresult SuspendTraffic(const std::string& appId) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _callLog.push_back("Suspend:" + appId);
        _suspendCount++;
        _lastSuspendedAppId = appId;
        return WPEFramework::Core::ERROR_NONE;
    }

    WPEFramework::Core::hresult ResumeTraffic(const std::string& appId) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _callLog.push_back("Resume:" + appId);
        _resumeCount++;
        _lastResumedAppId = appId;
        return WPEFramework::Core::ERROR_NONE;
    }

    // Accessors (thread-safe reads after all operations complete)
    uint32_t suspendCount() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _suspendCount;
    }

    uint32_t resumeCount() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _resumeCount;
    }

    std::string lastSuspendedAppId() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _lastSuspendedAppId;
    }

    std::string lastResumedAppId() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _lastResumedAppId;
    }

    std::vector<std::string> callLog() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _callLog;
    }

    void reset()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _callLog.clear();
        _suspendCount = 0;
        _resumeCount = 0;
        _lastSuspendedAppId.clear();
        _lastResumedAppId.clear();
    }

private:
    mutable std::atomic<uint32_t> _refCount;
    mutable std::mutex _mutex;
    std::vector<std::string> _callLog;
    uint32_t _suspendCount { 0 };
    uint32_t _resumeCount  { 0 };
    std::string _lastSuspendedAppId;
    std::string _lastResumedAppId;
};
