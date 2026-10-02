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
#include <string>
#include <functional>
#include <mutex>
#include <map>

/*
 * MockJSONRPCDirectLink intercepts calls from SystemDelegate's AcquireLink().
 *
 * SystemDelegate uses:
 *   auto link = AcquireLink(callsign);
 *   link->Invoke<Params,Response>(method, params, response);
 *
 * where AcquireLink calls GetThunderControllerClient(shell, callsign).
 *
 * The approach:  In tests, the ServiceMock's QueryInterfaceByCallsign for
 * ILocalDispatcher returns our MockLocalDispatcher, which intercepts the
 * Invoke call at the dispatcher level.  JSONRPCDirectLink::Invoke ultimately
 * calls mDispatcher->Invoke(channelId, id, token, designator, params, response).
 *
 * We provide MockLocalDispatcher that lets tests set expected responses.
 */

namespace MockJSONRPC {

    using InvokeHandler = std::function<WPEFramework::Core::hresult(
        const std::string& method, const std::string& params, std::string& response)>;

    /*
     * MockLocalDispatcher implements ILocalDispatcher so that
     * JSONRPCDirectLink::Invoke routes through our mock.
     *
     * Usage in tests:
     *   auto dispatcher = WPEFramework::Core::Sink<MockJSONRPC::MockLocalDispatcher>::Create();
     *   dispatcher->SetHandler("getDeviceInfo", [](auto&, auto& params, auto& resp) {
     *       resp = R"({"make":"Arris"})";
     *       return Core::ERROR_NONE;
     *   });
     *   // Wire ServiceMock::QueryInterfaceByCallsign to return dispatcher for the callsign
     */
    class MockLocalDispatcher : public WPEFramework::PluginHost::ILocalDispatcher {
    public:
        MockLocalDispatcher()
            : mLocal(this)
        {
        }

        ~MockLocalDispatcher() override = default;

        // AddRef/Release are no-ops: lifetime is managed by Core::Sink<> wrapper.
        // Core::Sink overrides these virtuals with its own ref-count management,
        // so these bodies are never reached when used as Core::Sink<MockLocalDispatcher>.
        uint32_t AddRef() const override { return 1; }
        uint32_t Release() const override { return 0; }

        void SetHandler(const std::string& method, InvokeHandler handler)
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mHandlers[method] = std::move(handler);
        }

        void SetDefaultResponse(const std::string& response, WPEFramework::Core::hresult rc = WPEFramework::Core::ERROR_NONE)
        {
            mDefaultResponse = response;
            mDefaultRc = rc;
        }

        // ILocalDispatcher
        WPEFramework::Core::hresult Invoke(const uint32_t channelId, const uint32_t id,
            const string& token, const string& method, const string& parameters,
            string& response) override
        {
            (void)channelId;
            (void)id;
            (void)token;

            // Extract the actual method name from "callsign.1.methodName"
            std::string actualMethod = method;
            auto lastDot = method.rfind('.');
            if (lastDot != std::string::npos) {
                actualMethod = method.substr(lastDot + 1);
            }

            {
                std::lock_guard<std::mutex> lock(mMutex);
                auto it = mHandlers.find(actualMethod);
                if (it != mHandlers.end()) {
                    return it->second(actualMethod, parameters, response);
                }
            }

            response = mDefaultResponse;
            return mDefaultRc;
        }

        WPEFramework::PluginHost::ILocalDispatcher* Local() override { return mLocal; }

        // IDispatcher
        WPEFramework::Core::hresult Invoke(ICallback* callback, const uint32_t channelId, const uint32_t id,
            const string& token, const string& method, const string& parameters,
            string& response) override
        {
            (void)callback;
            return Invoke(channelId, id, token, method, parameters, response);
        }
        WPEFramework::Core::hresult Validate(const string&, const string&, const string&) const override
        {
            return WPEFramework::Core::ERROR_NONE;
        }
        WPEFramework::Core::hresult Revoke(ICallback*) override { return WPEFramework::Core::ERROR_NONE; }

        // ILocalDispatcher
        void Activate(WPEFramework::PluginHost::IShell*) override {}
        void Deactivate() override {}
        void Dropped(const uint32_t) override {}

        BEGIN_INTERFACE_MAP(MockLocalDispatcher)
        INTERFACE_ENTRY(WPEFramework::PluginHost::ILocalDispatcher)
        END_INTERFACE_MAP

    private:
        WPEFramework::PluginHost::ILocalDispatcher* mLocal;
        std::mutex mMutex;
        std::map<std::string, InvokeHandler> mHandlers;
        std::string mDefaultResponse{"{}"};
        WPEFramework::Core::hresult mDefaultRc{WPEFramework::Core::ERROR_NONE};
    };

} // namespace MockJSONRPC
