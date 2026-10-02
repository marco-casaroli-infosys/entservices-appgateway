#pragma once

/*
 * L0 test ServiceMock:
 * - Implements PluginHost::IShell so plugins/services can Configure()/Initialize()/Deinitialize().
 * - Implements PluginHost::IShell::ICOMLink so IShell::Root<T>() can instantiate in-proc fakes via COMLink()->Instantiate(...).
 *
 * This mock is designed to keep behavior deterministic and offline:
 *  - No sockets/network
 *  - No dependency on other real Thunder plugins
 *
 * IMPORTANT:
 * - By default, Release() does NOT delete 'this' to allow stack-allocated ServiceMock usage.
 * - If constructed with selfDelete=true, Release(0) deletes the object (useful for heap-owned shells).
 */

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <mutex>
#include <algorithm>

#include <Module.h>

#include <core/core.h>
#include <plugins/IShell.h>

#include <interfaces/IAppGateway.h>
#include <interfaces/IConfiguration.h>
#include <interfaces/IAppNotifications.h>

namespace L0Test {

    using string = std::string;

    // Test-only interface IDs to safely obtain concrete fake instances via QueryInterface
    // without relying on dynamic_cast across shared-library boundaries.
    static constexpr uint32_t ID_RESPONDER_FAKE       = 0xF0F0F001;
    static constexpr uint32_t ID_AUTHENTICATOR_FAKE   = 0xF0F0F002;
    static constexpr uint32_t ID_NOTIFICATIONS_FAKE   = 0xF0F0F003;
    static constexpr uint32_t ID_REQUEST_HANDLER_FAKE = 0xF0F0F004;

    // A simple deterministic resolver fake with multiple error paths.
    // Also records the last call so targeted responder tests can verify callsign/context routing.
    class ResolverFake final : public WPEFramework::Exchange::IAppGatewayResolver,
                               public WPEFramework::Exchange::IConfiguration {
    public:
        ResolverFake()
            : _refCount(1)
        {
        }

        ~ResolverFake() override = default;

        // Recorded state for assertions (test-only).
        uint32_t resolveCount { 0 };
        WPEFramework::Exchange::GatewayContext lastContext {};
        string lastOrigin;
        string lastMethod;
        string lastParams;
        string lastResult;
        uint32_t lastRc { WPEFramework::Core::ERROR_NONE };

        // Core::IUnknown
        uint32_t AddRef() const override
        {
            return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
        }

        uint32_t Release() const override
        {
            const uint32_t newCount = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (newCount == 0) {
                delete this;
                return WPEFramework::Core::ERROR_DESTRUCTION_SUCCEEDED;
            }
            return WPEFramework::Core::ERROR_NONE;
        }

        void* QueryInterface(const uint32_t id) override
        {
            if (id == WPEFramework::Exchange::IAppGatewayResolver::ID) {
                AddRef();
                return static_cast<WPEFramework::Exchange::IAppGatewayResolver*>(this);
            }
            if (id == WPEFramework::Exchange::IConfiguration::ID) {
                AddRef();
                return static_cast<WPEFramework::Exchange::IConfiguration*>(this);
            }
            return nullptr;
        }

        // IConfiguration
        uint32_t Configure(WPEFramework::PluginHost::IShell* /*shell*/) override
        {
            return WPEFramework::Core::ERROR_NONE;
        }

        // IAppGatewayResolver
        WPEFramework::Core::hresult Configure(IStringIterator* const& /*paths*/) override
        {
            return WPEFramework::Core::ERROR_NONE;
        }

        WPEFramework::Core::hresult Resolve(const WPEFramework::Exchange::GatewayContext& context,
                                            const string& origin,
                                            const string& method,
                                            const string& params,
                                            string& result) override
        {
            // Record call for assertions.
            resolveCount++;
            lastContext = context;
            lastOrigin = origin;
            lastMethod = method;
            lastParams = params;

            // Deterministic error mapping controlled by method name.
            if ("l0.notPermitted" == method) {
                result = "{\"error\":\"NotPermitted\"}";
                lastResult = result;
                lastRc = WPEFramework::Core::ERROR_PRIVILIGED_REQUEST;
                return lastRc;
            }
            if ("l0.notSupported" == method) {
                result = "{\"error\":\"NotSupported\"}";
                lastResult = result;
                lastRc = WPEFramework::Core::ERROR_NOT_SUPPORTED;
                return lastRc;
            }
            if ("l0.notAvailable" == method) {
                result = "{\"error\":\"NotAvailable\"}";
                lastResult = result;
                lastRc = WPEFramework::Core::ERROR_UNAVAILABLE;
                return lastRc;
            }

            // Success path: return a JSON 'null' resolution.
            result = "null";
            lastResult = result;
            lastRc = WPEFramework::Core::ERROR_NONE;
            return lastRc;
        }

    private:
        mutable std::atomic<uint32_t> _refCount;
    };

    // Responder fake with controllable transport availability and simple notification/context scaffolding.
    class ResponderFake final : public WPEFramework::Exchange::IAppGatewayResponder,
                                public WPEFramework::Exchange::IConfiguration {
    public:
        explicit ResponderFake(const bool transportEnabled = true)
            : _refCount(1)
            , _transportEnabled(transportEnabled)
        {
        }

        ~ResponderFake() override
        {
            for (auto* n : _notifications) {
                if (n != nullptr) {
                    n->Release();
                }
            }
            _notifications.clear();
        }

        // Core::IUnknown
        uint32_t AddRef() const override
        {
            return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
        }

        uint32_t Release() const override
        {
            const uint32_t newCount = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (newCount == 0) {
                delete this;
                return WPEFramework::Core::ERROR_DESTRUCTION_SUCCEEDED;
            }
            return WPEFramework::Core::ERROR_NONE;
        }

        void* QueryInterface(const uint32_t id) override
        {
            if (id == WPEFramework::Exchange::IAppGatewayResponder::ID) {
                AddRef();
                return static_cast<WPEFramework::Exchange::IAppGatewayResponder*>(this);
            }
            if (id == WPEFramework::Exchange::IConfiguration::ID) {
                AddRef();
                return static_cast<WPEFramework::Exchange::IConfiguration*>(this);
            }
            if (id == ID_RESPONDER_FAKE) {
                AddRef();
                return this;
            }
            return nullptr;
        }

        // IConfiguration
        uint32_t Configure(WPEFramework::PluginHost::IShell* /*shell*/) override
        {
            return WPEFramework::Core::ERROR_NONE;
        }

        // IAppGatewayResponder
        WPEFramework::Core::hresult Respond(const WPEFramework::Exchange::GatewayContext& /*context*/,
                                            const string& payload) override
        {
            lastRespondPayload = payload;
            respondCount++;
            return _transportEnabled ? WPEFramework::Core::ERROR_NONE : WPEFramework::Core::ERROR_UNAVAILABLE;
        }

        WPEFramework::Core::hresult Emit(const WPEFramework::Exchange::GatewayContext& /*context*/,
                                         const string& method,
                                         const string& payload) override
        {
            lastEmitMethod = method;
            lastEmitPayload = payload;
            emitCount++;
            return _transportEnabled ? WPEFramework::Core::ERROR_NONE : WPEFramework::Core::ERROR_UNAVAILABLE;
        }

        WPEFramework::Core::hresult Request(const uint32_t /*connectionId*/,
                                            const uint32_t /*id*/,
                                            const string& method,
                                            const string& params) override
        {
            lastRequestMethod = method;
            lastRequestParams = params;
            requestCount++;
            return _transportEnabled ? WPEFramework::Core::ERROR_NONE : WPEFramework::Core::ERROR_UNAVAILABLE;
        }

        WPEFramework::Core::hresult GetGatewayConnectionContext(const uint32_t connectionId,
                                                                const string& contextKey,
                                                                string& contextValue) override
        {
            std::lock_guard<std::mutex> lock(_ctxMutex);
            auto itConn = _contexts.find(connectionId);
            if (itConn == _contexts.end()) {
                return WPEFramework::Core::ERROR_BAD_REQUEST;
            }
            auto itKey = itConn->second.find(contextKey);
            if (itKey == itConn->second.end()) {
                return WPEFramework::Core::ERROR_BAD_REQUEST;
            }
            contextValue = itKey->second;
            return WPEFramework::Core::ERROR_NONE;
        }

        WPEFramework::Core::hresult RecordGatewayConnectionContext(const uint32_t connectionId,
                                                                   const string& contextKey,
                                                                   const string& contextValue) override
        {
            std::lock_guard<std::mutex> lock(_ctxMutex);
            _contexts[connectionId][contextKey] = contextValue;
            return WPEFramework::Core::ERROR_NONE;
        }

        WPEFramework::Core::hresult Register(INotification* notification) override
        {
            if (notification == nullptr) {
                return WPEFramework::Core::ERROR_GENERAL;
            }
            if (std::find(_notifications.begin(), _notifications.end(), notification) == _notifications.end()) {
                _notifications.push_back(notification);
                notification->AddRef();
            }
            return WPEFramework::Core::ERROR_NONE;
        }

        WPEFramework::Core::hresult Unregister(INotification* notification) override
        {
            if (notification == nullptr) {
                return WPEFramework::Core::ERROR_GENERAL;
            }
            auto it = std::find(_notifications.begin(), _notifications.end(), notification);
            if (it != _notifications.end()) {
                (*it)->Release();
                _notifications.erase(it);
            }
            return WPEFramework::Core::ERROR_NONE;
        }

        // Helpers for tests (not part of Exchange interface)
        void SetTransportEnabled(const bool enabled) { _transportEnabled = enabled; }

        void SetConnectionContext(const uint32_t connectionId, const string& key, const string& value)
        {
            std::lock_guard<std::mutex> lock(_ctxMutex);
            _contexts[connectionId][key] = value;
        }

        void SimulateAppConnectionChanged(const string& appId, const uint32_t connectionId, const bool connected)
        {
            for (auto* n : _notifications) {
                if (n != nullptr) {
                    n->OnAppConnectionChanged(appId, connectionId, connected);
                }
            }
        }

        uint32_t respondCount { 0 };
        uint32_t emitCount { 0 };
        uint32_t requestCount { 0 };
        string lastRespondPayload;
        string lastEmitMethod;
        string lastEmitPayload;
        string lastRequestMethod;
        string lastRequestParams;

    private:
        mutable std::atomic<uint32_t> _refCount;
        bool _transportEnabled;
        std::list<INotification*> _notifications;

        std::mutex _ctxMutex;
        std::unordered_map<uint32_t, std::unordered_map<string, string>> _contexts;
    };

    // Fake Authenticator (used by AppGatewayImplementation permission checks).
    class AuthenticatorFake final : public WPEFramework::Exchange::IAppGatewayAuthenticator {
    public:
        explicit AuthenticatorFake(bool allowed = true, bool failCheck = false)
            : _refCount(1)
            , _allowed(allowed)
            , _failCheck(failCheck)
        {
        }

        ~AuthenticatorFake() override = default;

        uint32_t AddRef() const override
        {
            return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
        }

        uint32_t Release() const override
        {
            const uint32_t newCount = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (newCount == 0) {
                delete this;
                return WPEFramework::Core::ERROR_DESTRUCTION_SUCCEEDED;
            }
            return WPEFramework::Core::ERROR_NONE;
        }

        void* QueryInterface(const uint32_t id) override
        {
            if (id == WPEFramework::Exchange::IAppGatewayAuthenticator::ID) {
                AddRef();
                return static_cast<WPEFramework::Exchange::IAppGatewayAuthenticator*>(this);
            }
            if (id == ID_AUTHENTICATOR_FAKE) {
                AddRef();
                return this;
            }
            return nullptr;
        }

        WPEFramework::Core::hresult Authenticate(const string& /*sessionId*/, string& appId /* @out */) override
        {
            appId = "com.l0.authenticated";
            authenticateCount++;
            return WPEFramework::Core::ERROR_NONE;
        }

        WPEFramework::Core::hresult GetSessionId(const string& /*appId*/, string& sessionId /* @out */) override
        {
            sessionId = "l0.session";
            getSessionIdCount++;
            return WPEFramework::Core::ERROR_NONE;
        }

        WPEFramework::Core::hresult CheckPermissionGroup(const string& appId,
                                                         const string& permissionGroup,
                                                         bool& allowed /* @out */) override
        {
            lastAppId = appId;
            lastPermissionGroup = permissionGroup;
            checkPermissionCount++;

            if (_failCheck) {
                allowed = false;
                return WPEFramework::Core::ERROR_GENERAL;
            }

            allowed = _allowed;
            return WPEFramework::Core::ERROR_NONE;
        }

        void SetAllowed(bool allowed) { _allowed = allowed; }
        void SetFailCheck(bool fail) { _failCheck = fail; }

        uint32_t authenticateCount { 0 };
        uint32_t getSessionIdCount { 0 };
        uint32_t checkPermissionCount { 0 };
        string lastAppId;
        string lastPermissionGroup;

    private:
        mutable std::atomic<uint32_t> _refCount;
        bool _allowed;
        bool _failCheck;
    };

    // Fake AppNotifications (used by AppGatewayImplementation event listen/notify path).
    class AppNotificationsFake final : public WPEFramework::Exchange::IAppNotifications {
    public:
        AppNotificationsFake()
            : _refCount(1)
        {
        }

        ~AppNotificationsFake() override = default;

        uint32_t AddRef() const override
        {
            return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
        }

        uint32_t Release() const override
        {
            const uint32_t newCount = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (newCount == 0) {
                delete this;
                return WPEFramework::Core::ERROR_DESTRUCTION_SUCCEEDED;
            }
            return WPEFramework::Core::ERROR_NONE;
        }

        void* QueryInterface(const uint32_t id) override
        {
            if (id == WPEFramework::Exchange::IAppNotifications::ID) {
                AddRef();
                return static_cast<WPEFramework::Exchange::IAppNotifications*>(this);
            }
            if (id == ID_NOTIFICATIONS_FAKE) {
                AddRef();
                return this;
            }
            return nullptr;
        }

        WPEFramework::Core::hresult Notify(const string& event, const string& payload)
        {
            lastEvent = event;
            lastPayload = payload;
            notifyCount++;
            return WPEFramework::Core::ERROR_NONE;
        }

        WPEFramework::Core::hresult Subscribe(const AppNotificationContext& /*context*/,
                                              bool /*listen*/,
                                              const string& /*module*/,
                                              const string& /*event*/) override
        {
            // Not used in this repo's AppGatewayImplementation (kept for interface compatibility).
            subscribeCount++;
            return WPEFramework::Core::ERROR_NONE;
        }

        WPEFramework::Core::hresult Emit(const string& /*event*/,
                                         const string& /*payload*/,
                                         const string& /*appId*/) override
        {
            emitCount++;
            return WPEFramework::Core::ERROR_NONE;
        }

        WPEFramework::Core::hresult Cleanup(const uint32_t /*connectionId*/, const string& /*origin*/) override
        {
            cleanupCount++;
            return WPEFramework::Core::ERROR_NONE;
        }

        uint32_t notifyCount { 0 };
        uint32_t subscribeCount { 0 };
        uint32_t emitCount { 0 };
        uint32_t cleanupCount { 0 };
        string lastEvent;
        string lastPayload;

    private:
        mutable std::atomic<uint32_t> _refCount;
    };

    // Fake COM-RPC request handler (used by AppGatewayImplementation::ProcessComRpcRequest).
    class RequestHandlerFake final : public WPEFramework::Exchange::IAppGatewayRequestHandler {
    public:
        explicit RequestHandlerFake(uint32_t rc = WPEFramework::Core::ERROR_NONE)
            : _refCount(1)
            , _rc(rc)
        {
        }

        ~RequestHandlerFake() override = default;

        uint32_t AddRef() const override
        {
            return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
        }

        uint32_t Release() const override
        {
            const uint32_t newCount = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (newCount == 0) {
                delete this;
                return WPEFramework::Core::ERROR_DESTRUCTION_SUCCEEDED;
            }
            return WPEFramework::Core::ERROR_NONE;
        }

        void* QueryInterface(const uint32_t id) override
        {
            if (id == WPEFramework::Exchange::IAppGatewayRequestHandler::ID) {
                AddRef();
                return static_cast<WPEFramework::Exchange::IAppGatewayRequestHandler*>(this);
            }
            if (id == ID_REQUEST_HANDLER_FAKE) {
                AddRef();
                return this;
            }
            return nullptr;
        }

        WPEFramework::Core::hresult HandleAppGatewayRequest(const WPEFramework::Exchange::GatewayContext& /*context*/,
                                                            const string& method,
                                                            const string& payload,
                                                            string& result) override
        {
            lastMethod = method;
            lastPayload = payload;
            handleCount++;

            if (_rc == WPEFramework::Core::ERROR_NONE) {
                // A deterministic, simple response.
                result = "null";
            }
            return _rc;
        }

        void SetReturnCode(uint32_t rc) { _rc = rc; }

        uint32_t handleCount { 0 };
        string lastMethod;
        string lastPayload;

    private:
        mutable std::atomic<uint32_t> _refCount;
        uint32_t _rc;
    };

    class ServiceMock final : public WPEFramework::PluginHost::IShell,
                              public WPEFramework::PluginHost::IShell::ICOMLink {
    public:
        struct Config {
            bool provideResolver;
            bool provideResponder;
            bool responderTransportAvailable;

            bool provideAppNotifications;
            bool provideAuthenticator;
            bool authenticatorAllowed;
            bool authenticatorFailCheck;

            bool provideRequestHandler;
            string requestHandlerCallsign; // e.g. "org.rdk.FbSettings"

            // Test-only: override IShell::ConfigLine() output.
            // When set to a JSON containing `"connector"`, AppGatewayResponderImplementation::InitializeWebsocket()
            // will parse it and bind to that connector (we use port 0 for ephemeral ports in tests).
            string configLineOverride;

            // PUBLIC_INTERFACE
            explicit Config(const bool resolver = true, const bool responder = true, const bool responderTransport = true)
                : provideResolver(resolver)
                , provideResponder(responder)
                , responderTransportAvailable(responderTransport)
                , provideAppNotifications(false)
                , provideAuthenticator(false)
                , authenticatorAllowed(true)
                , authenticatorFailCheck(false)
                , provideRequestHandler(false)
                , requestHandlerCallsign("org.rdk.FbSettings")
                , configLineOverride()
            {
            }
        };

        explicit ServiceMock(Config cfg = Config(), const bool selfDelete = false)
            : _refCount(1)
            , _instantiateCount(0)
            , _callsign("org.rdk.AppGateway")
            , _className("AppGateway")
            , _cfg(cfg)
            , _selfDelete(selfDelete)
            , _resolverFake(nullptr)
            , _responderFake(nullptr)
            , _appNotificationsFake(nullptr)
            , _authenticatorFake(nullptr)
            , _requestHandlerFake(nullptr)
        {
            // IMPORTANT: Do NOT override _cfg here; tests depend on explicit per-test configuration.
        }

        ~ServiceMock() override
        {
            if (_resolverFake != nullptr) {
                _resolverFake->Release();
                _resolverFake = nullptr;
            }
            if (_responderFake != nullptr) {
                _responderFake->Release();
                _responderFake = nullptr;
            }
            if (_appNotificationsFake != nullptr) {
                _appNotificationsFake->Release();
                _appNotificationsFake = nullptr;
            }
            if (_authenticatorFake != nullptr) {
                _authenticatorFake->Release();
                _authenticatorFake = nullptr;
            }
            if (_requestHandlerFake != nullptr) {
                _requestHandlerFake->Release();
                _requestHandlerFake = nullptr;
            }
        }

        // Core::IUnknown
        uint32_t AddRef() const override
        {
            return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
        }

        uint32_t Release() const override
        {
            const uint32_t newCount = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (newCount == 0) {
                // Default in this repo's L0 tests: do NOT delete on Release(0) to support stack allocation.
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

            // Some code paths query interfaces directly on IShell.
            if (id == WPEFramework::Exchange::IAppGatewayResolver::ID) {
                if (_cfg.provideResolver) {
                    if (_resolverFake == nullptr) {
                        _resolverFake = new ResolverFake();
                    }
                    _resolverFake->AddRef();
                    return static_cast<WPEFramework::Exchange::IAppGatewayResolver*>(_resolverFake);
                }
                return nullptr;
            }
            if (id == WPEFramework::Exchange::IAppGatewayResponder::ID) {
                if (_cfg.provideResponder) {
                    if (_responderFake == nullptr) {
                        _responderFake = new ResponderFake(_cfg.responderTransportAvailable);
                    }
                    _responderFake->AddRef();
                    return static_cast<WPEFramework::Exchange::IAppGatewayResponder*>(_responderFake);
                }
                return nullptr;
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
        string Locator() const override
        {
            const char* soPath = std::getenv("APPGATEWAY_PLUGIN_SO");
            if (soPath != nullptr && *soPath != '\0') {
                return string(soPath);
            }
            return "libWPEFrameworkAppGateway.so";
        }
        string ClassName() const override { return _className; }
        string Versions() const override { return "1.0.0"; }
        string Callsign() const override { return _callsign; }

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
            // Default behavior (used by most existing L0 tests):
            // Thunder RootConfig parsing expects a JSON object.
            //
            // Some targeted responder implementation tests need to pass the plugin's own websocket config
            // (e.g. {"connector":"127.0.0.1:0"}). Allow a per-instance override.
            if (_cfg.configLineOverride.empty() == false) {
                return _cfg.configLineOverride;
            }

            // Optional debug logging for config resolution in test runs.
            const char* verbose = std::getenv("APPGW_L0_VERBOSE_CONFIGLINE");
            if (verbose != nullptr && *verbose != '\0' && std::string(verbose) != "0") {
                std::cerr << "ConfigLine in service mock called, using default {}" << std::endl;
            }

            return "{}";

        }
        WPEFramework::Core::hresult ConfigLine(const string& /*config*/) override { return WPEFramework::Core::ERROR_NONE; }

        WPEFramework::Core::hresult Metadata(string& info /* @out */) const override
        {
            info = R"({"name":"AppGateway","version":"1.0.0"})";
            return WPEFramework::Core::ERROR_NONE;
        }

        bool IsSupported(const uint8_t /*version*/) const override { return true; }

        WPEFramework::PluginHost::ISubSystem* SubSystems() override { return nullptr; }

        void Notify(const string& /*message*/) override {}

        void Register(WPEFramework::PluginHost::IPlugin::INotification* /*sink*/) override {}
        void Unregister(WPEFramework::PluginHost::IPlugin::INotification* /*sink*/) override {}

        state State() const override { return state::ACTIVATED; }

        void* QueryInterfaceByCallsign(const uint32_t id, const string& name) override
        {
            // Provide per-callsign interfaces used by AppGatewayImplementation and other L0 code paths.
            // NOTE: Return values follow COM semantics: pointer is returned AddRef'd.
            if (id == WPEFramework::Exchange::IAppNotifications::ID) {
                if (_cfg.provideAppNotifications && name == "org.rdk.AppNotifications") {
                    if (_appNotificationsFake == nullptr) {
                        _appNotificationsFake = new AppNotificationsFake();
                    }
                    _appNotificationsFake->AddRef();
                    return static_cast<WPEFramework::Exchange::IAppNotifications*>(_appNotificationsFake);
                }
                return nullptr;
            }

            if (id == WPEFramework::Exchange::IAppGatewayAuthenticator::ID) {
                if (_cfg.provideAuthenticator && name == "org.rdk.LaunchDelegate") {
                    if (_authenticatorFake == nullptr) {
                        _authenticatorFake = new AuthenticatorFake(_cfg.authenticatorAllowed, _cfg.authenticatorFailCheck);
                    }
                    _authenticatorFake->AddRef();
                    return static_cast<WPEFramework::Exchange::IAppGatewayAuthenticator*>(_authenticatorFake);
                }
                return nullptr;
            }

            if (id == WPEFramework::Exchange::IAppGatewayResponder::ID) {
                // INTERNAL_GATEWAY_CALLSIGN maps to org.rdk.LaunchDelegate in this repo's helpers/UtilsCallsign.h.
                if (name == "org.rdk.LaunchDelegate") {
                    // Reuse the responder fake as "internal responder" as well; tests can inspect call counters.
                    if (_cfg.provideResponder) {
                        if (_responderFake == nullptr) {
                            _responderFake = new ResponderFake(_cfg.responderTransportAvailable);
                        }
                        _responderFake->AddRef();
                        return static_cast<WPEFramework::Exchange::IAppGatewayResponder*>(_responderFake);
                    }
                }
                return nullptr;
            }

            if (id == WPEFramework::Exchange::IAppGatewayRequestHandler::ID) {
                if (_cfg.provideRequestHandler && name == _cfg.requestHandlerCallsign) {
                    if (_requestHandlerFake == nullptr) {
                        _requestHandlerFake = new RequestHandlerFake();
                    }
                    _requestHandlerFake->AddRef();
                    return static_cast<WPEFramework::Exchange::IAppGatewayRequestHandler*>(_requestHandlerFake);
                }
                return nullptr;
            }

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

        void* Instantiate(const WPEFramework::RPC::Object& object, const uint32_t /*waitTime*/, uint32_t& connectionId) override
        {
            connectionId = 1;
            _instantiateCount.fetch_add(1, std::memory_order_acq_rel);

            const std::string className = object.ClassName();

            auto endsWith = [](const std::string& s, const std::string& suffix) -> bool {
                if (s.size() < suffix.size()) {
                    return false;
                }
                return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
            };

            // Resolver (aggregate)
            if (endsWith(className, "AppGatewayImplementation") ||
                endsWith(className, "::AppGatewayImplementation") ||
                endsWith(className, "AppGatewayResolver") ||
                endsWith(className, "::AppGatewayResolver") ||
                endsWith(className, "IAppGatewayResolver")) {

                if (_cfg.provideResolver == false) {
                    return nullptr;
                }
                if (_resolverFake == nullptr) {
                    _resolverFake = new ResolverFake();
                }
                _resolverFake->AddRef();
                return static_cast<WPEFramework::Exchange::IAppGatewayResolver*>(_resolverFake);
            }

            // Responder (aggregate)
            if (endsWith(className, "AppGatewayResponderImplementation") ||
                endsWith(className, "::AppGatewayResponderImplementation") ||
                endsWith(className, "AppGatewayResponder") ||
                endsWith(className, "::AppGatewayResponder") ||
                endsWith(className, "IAppGatewayResponder")) {

                if (_cfg.provideResponder == false) {
                    return nullptr;
                }
                if (_responderFake == nullptr) {
                    _responderFake = new ResponderFake(_cfg.responderTransportAvailable);
                }
                _responderFake->AddRef();
                return static_cast<WPEFramework::Exchange::IAppGatewayResponder*>(_responderFake);
            }

            return nullptr;
        }

        // Helpers for tests (non-interface)
        void Callsign(const std::string& cs) { _callsign = cs; }
        void ClassName(const std::string& cn) { _className = cn; }

        ResponderFake* GetResponderFake() const { return _responderFake; }
        ResolverFake* GetResolverFake() const { return _resolverFake; }
        AppNotificationsFake* GetAppNotificationsFake() const { return _appNotificationsFake; }
        AuthenticatorFake* GetAuthenticatorFake() const { return _authenticatorFake; }
        RequestHandlerFake* GetRequestHandlerFake() const { return _requestHandlerFake; }

    private:
        mutable std::atomic<uint32_t> _refCount;
        std::atomic<uint32_t> _instantiateCount;
        std::string _callsign;
        std::string _className;
        Config _cfg;
        const bool _selfDelete;

        ResolverFake* _resolverFake;
        ResponderFake* _responderFake;

        AppNotificationsFake* _appNotificationsFake;
        AuthenticatorFake* _authenticatorFake;
        RequestHandlerFake* _requestHandlerFake;
    };

} // namespace L0Test
