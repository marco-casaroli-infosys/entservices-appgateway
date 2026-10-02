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
#include <interfaces/IAppNotifications.h>
#include <atomic>

class MockEmitter : public WPEFramework::Exchange::IAppNotificationHandler::IEmitter {
public:
    MockEmitter() : _refCount(0) {}
    ~MockEmitter() override = default;

    // Real ref-counting WITHOUT self-deletion.
    //
    // Production code (EventRegistrationJob, BaseEventDelegate) calls
    // AddRef/Release on the raw IEmitter pointer.  These must adjust a real
    // counter so that:
    //   1. Core::Sink<MockEmitter> sees a balanced count at destruction and
    //      does NOT print the "Oops this is scary" warning.
    //   2. Heap-allocated emitters used by async-event tests stay valid in
    //      memory even after the last Release — the test process exits
    //      shortly after, and the OS reclaims the memory.
    //
    // Why no self-delete:  some tests allocate MockEmitter on the stack via
    // Core::Sink<MockEmitter>.  Self-deleting a stack object is undefined
    // behaviour.  By omitting self-delete we keep both usage patterns safe.
    uint32_t AddRef() const override
    {
        return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    uint32_t Release() const override
    {
        const uint32_t prev = _refCount.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1) {
            return WPEFramework::Core::ERROR_DESTRUCTION_SUCCEEDED;
        }
        return WPEFramework::Core::ERROR_NONE;
    }

    MOCK_METHOD(void, Emit, (const string& event, const string& payload, const string& appId), (override));

    BEGIN_INTERFACE_MAP(MockEmitter)
    INTERFACE_ENTRY(WPEFramework::Exchange::IAppNotificationHandler::IEmitter)
    END_INTERFACE_MAP

private:
    mutable std::atomic<uint32_t> _refCount;
};
