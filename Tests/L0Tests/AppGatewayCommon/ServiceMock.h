/*
 * Copyright 2026 Comcast Cable Communications Management, LLC
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
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/*
 * L0 test ServiceMock for AppGatewayCommon:
 * - Implements PluginHost::IShell so AppGatewayCommon can Configure()/Initialize()/Deinitialize().
 * - Implements PluginHost::IShell::ICOMLink with no-op Instantiate.
 *
 * This mock is intentionally minimal:
 *  - No sockets/network
 *  - No dependency on other real Thunder plugins
 *  - QueryInterfaceByCallsign returns nullptr (delegates handle this gracefully)
 *
 * IMPORTANT:
 * - By default, Release() does NOT delete 'this' to allow stack-allocated/scoped usage.
 * - If constructed with selfDelete=true, Release(0) deletes the object.
 */

#include <atomic>
#include <cstdint>
#include <string>

#include <core/core.h>
#include <plugins/IShell.h>

namespace L0Test {

    using string = std::string;

    class ServiceMock final : public WPEFramework::PluginHost::IShell,
                              public WPEFramework::PluginHost::IShell::ICOMLink {
    public:
        struct Config {
            string configLineOverride;
            explicit Config() : configLineOverride() {}
        };

        explicit ServiceMock(Config cfg = Config(), const bool selfDelete = false)
            : _refCount(1)
            , _cfg(cfg)
            , _selfDelete(selfDelete)
        {
        }

        ~ServiceMock() override = default;

        // Core::IUnknown
        uint32_t AddRef() const override
    {
        return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
    }

        uint32_t Release() const override
        {
            const uint32_t newCount = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (newCount == 0) {
                if (_selfDelete) {
                    delete this;
                }
                return WPEFramework::Core::ERROR_DESTRUCTION_SUCCEEDED;
            }
            return WPEFramework::Core::ERROR_NONE;
        }

        void* QueryInterface(const uint32_t id) override
        {
            if (id == WPEFramework::PluginHost::IShell::ID) {
                AddRef();
                return static_cast<WPEFramework::PluginHost::IShell*>(this);
            }
            return nullptr;
        }

        // IShell
        void EnableWebServer(const string& /*URLPath*/, const string& /*fileSystemPath*/) override {}
        void DisableWebServer() override {}

        string Model() const override { return "l0test-device"; }
        bool Background() const override { return false; }
        string Accessor() const override { return "127.0.0.1:9998"; }
        string WebPrefix() const override { return "/jsonrpc"; }
        string Locator() const override { return "libWPEFrameworkAppGatewayCommon.so"; }
        string ClassName() const override { return "AppGatewayCommon"; }
        string Versions() const override { return "1.0.0"; }
        string Callsign() const override { return "org.rdk.AppGatewayCommon"; }

        string PersistentPath() const override { return "/tmp"; }
        string VolatilePath() const override { return "/tmp"; }
        string DataPath() const override { return "/tmp"; }
        string ProxyStubPath() const override { return "/tmp"; }
        string SystemPath() const override { return "/tmp"; }
        string PluginPath() const override { return "/tmp"; }
        string SystemRootPath() const override { return "/"; }

        WPEFramework::Core::hresult SystemRootPath(const string& /*systemRootPath*/) override { return WPEFramework::Core::ERROR_NONE; }

        startup Startup() const override { return startup::ACTIVATED; }
        WPEFramework::Core::hresult Startup(const startup /*value*/) override { return WPEFramework::Core::ERROR_NONE; }

        string Substitute(const string& input) const override { return input; }

        bool Resumed() const override { return false; }
        WPEFramework::Core::hresult Resumed(const bool /*value*/) override { return WPEFramework::Core::ERROR_NONE; }

        string HashKey() const override { return "hash"; }

        string ConfigLine() const override
        {
            if (_cfg.configLineOverride.empty() == false) {
                return _cfg.configLineOverride;
            }
            return "";
        }
        WPEFramework::Core::hresult ConfigLine(const string& /*config*/) override { return WPEFramework::Core::ERROR_NONE; }

        WPEFramework::Core::hresult Metadata(string& info /* @out */) const override
        {
            info = R"({"name":"AppGatewayCommon","version":"1.0.0"})";
            return WPEFramework::Core::ERROR_NONE;
        }

        bool IsSupported(const uint8_t /*version*/) const override { return true; }

        WPEFramework::PluginHost::ISubSystem* SubSystems() override { return nullptr; }

        void Notify(const string& /*message*/) override {}

        void Register(WPEFramework::PluginHost::IPlugin::INotification* /*sink*/) override {}
        void Unregister(WPEFramework::PluginHost::IPlugin::INotification* /*sink*/) override {}

        state State() const override { return state::ACTIVATED; }

        void* QueryInterfaceByCallsign(const uint32_t /*id*/, const string& /*name*/) override
        {
            // In L0 tests, no external plugins are available.
            // AppGatewayCommon delegates handle nullptr gracefully.
            return nullptr;
        }

        WPEFramework::Core::hresult Activate(const reason /*why*/) override { return WPEFramework::Core::ERROR_NONE; }
        WPEFramework::Core::hresult Deactivate(const reason /*why*/) override { return WPEFramework::Core::ERROR_NONE; }
        WPEFramework::Core::hresult Unavailable(const reason /*why*/) override { return WPEFramework::Core::ERROR_NONE; }
        WPEFramework::Core::hresult Hibernate(const uint32_t /*timeout*/) override { return WPEFramework::Core::ERROR_NONE; }
        reason Reason() const override { return reason::REQUESTED; }

        uint32_t Submit(const uint32_t /*Id*/, const WPEFramework::Core::ProxyType<WPEFramework::Core::JSON::IElement>& /*response*/) override
        {
            return WPEFramework::Core::ERROR_NONE;
        }

        WPEFramework::PluginHost::IShell::ICOMLink* COMLink() override
        {
            return this;
        }

        // ICOMLink
        void Register(WPEFramework::RPC::IRemoteConnection::INotification* /*sink*/) override {}
        void Unregister(const WPEFramework::RPC::IRemoteConnection::INotification* /*sink*/) override {}

        void Register(WPEFramework::PluginHost::IShell::ICOMLink::INotification* /*sink*/) override {}
        void Unregister(WPEFramework::PluginHost::IShell::ICOMLink::INotification* /*sink*/) override {}

        WPEFramework::RPC::IRemoteConnection* RemoteConnection(const uint32_t /*connectionId*/) override
        {
            return nullptr;
        }

        void* Instantiate(const WPEFramework::RPC::Object& /*object*/, const uint32_t /*waitTime*/, uint32_t& connectionId) override
        {
            connectionId = 1;
            return nullptr;
        }

    private:
        mutable std::atomic<uint32_t> _refCount;
        Config _cfg;
        const bool _selfDelete;
    };

} 
