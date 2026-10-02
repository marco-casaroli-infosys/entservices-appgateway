#pragma once

/*
 * AppNotificationsServiceMock.h
 *
 * L0 test ServiceMock for AppNotifications plugin.
 * Implements PluginHost::IShell and ICOMLink so the plugin can:
 *   - Initialize/Deinitialize via service->Root<>()
 *   - QueryInterfaceByCallsign for AppGatewayResponder, InternalGateway, IAppNotificationHandler
 *
 * Pattern mirrors Tests/L0Tests/AppGateway/ServiceMock.h
 */

#include <atomic>
#include <cstdint>
#include <string>
#include <mutex>
#include <algorithm>

#include <Module.h>
#include <core/core.h>
#include <plugins/IShell.h>

#include <interfaces/IAppGateway.h>
#include <interfaces/IAppNotifications.h>
#include <interfaces/IConfiguration.h>
#include <AppNotifications.h>

#include "L0TestTypes.hpp"
#include "UtilsCallsign.h"

// Test-local callsign — no production macro exists in UtilsCallsign.h for this.
#define FB_SETTINGS_CALLSIGN "org.rdk.FbSettings"

namespace L0Test {

using string = std::string;

// -----------------------------------------------------------------------
// Fake IAppGatewayResponder
// Used for both AppGateway and InternalGateway (LaunchDelegate) dispatch paths.
// -----------------------------------------------------------------------
class ANResponderFake final : public WPEFramework::Exchange::IAppGatewayResponder {
public:
    explicit ANResponderFake()
        : _refCount(1)
    {
    }

    ~ANResponderFake() override = default;

    uint32_t AddRef() const override
    {
        return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    uint32_t Release() const override
    {
        const uint32_t n = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (n == 0) {
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
        return nullptr;
    }

    // IAppGatewayResponder
    WPEFramework::Core::hresult Respond(const WPEFramework::Exchange::GatewayContext& /*ctx*/,
                                        const string& payload) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        respondCount++;
        lastRespondPayload = payload;
        return WPEFramework::Core::ERROR_NONE;
    }

    WPEFramework::Core::hresult Emit(const WPEFramework::Exchange::GatewayContext& ctx,
                                     const string& method,
                                     const string& payload) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        emitCount++;
        lastEmitMethod  = method;
        lastEmitPayload = payload;
        lastEmitContext = ctx;
        return WPEFramework::Core::ERROR_NONE;
    }

    WPEFramework::Core::hresult Request(const uint32_t /*connectionId*/,
                                        const uint32_t /*id*/,
                                        const string& method,
                                        const string& params) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        requestCount++;
        lastRequestMethod = method;
        lastRequestParams = params;
        return WPEFramework::Core::ERROR_NONE;
    }

    WPEFramework::Core::hresult GetGatewayConnectionContext(const uint32_t /*connectionId*/,
                                                            const string& /*contextKey*/,
                                                            string& /*contextValue*/) override
    {
        return WPEFramework::Core::ERROR_NONE;
    }

    WPEFramework::Core::hresult RecordGatewayConnectionContext(const uint32_t /*connectionId*/,
                                                               const string& /*contextKey*/,
                                                               const string& /*contextValue*/) override
    {
        return WPEFramework::Core::ERROR_NONE;
    }

    WPEFramework::Core::hresult Register(INotification* /*notification*/) override
    {
        return WPEFramework::Core::ERROR_NONE;
    }

    WPEFramework::Core::hresult Unregister(INotification* /*notification*/) override
    {
        return WPEFramework::Core::ERROR_NONE;
    }

    // Observable state (access under _mutex for thread safety)
    uint32_t emitCount{0};
    uint32_t respondCount{0};
    uint32_t requestCount{0};
    string lastEmitMethod;
    string lastEmitPayload;
    string lastRequestMethod;
    string lastRequestParams;
    string lastRespondPayload;
    WPEFramework::Exchange::GatewayContext lastEmitContext{};

    mutable std::mutex _mutex;

private:
    mutable std::atomic<uint32_t> _refCount;
};

// -----------------------------------------------------------------------
// Fake IAppNotificationHandler
// Used so ThunderSubscriptionManager::HandleNotifier() can query and call
// HandleAppEventNotifier() without a real Thunder plugin.
// -----------------------------------------------------------------------
class ANNotificationHandlerFake final : public WPEFramework::Exchange::IAppNotificationHandler {
public:
    explicit ANNotificationHandlerFake(bool statusResult = true,
                                       uint32_t handleRc = WPEFramework::Core::ERROR_NONE)
        : _refCount(1)
        , _statusResult(statusResult)
        , _handleRc(handleRc)
    {
    }

    ~ANNotificationHandlerFake() override = default;

    uint32_t AddRef() const override
    {
        return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    uint32_t Release() const override
    {
        const uint32_t n = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (n == 0) {
            delete this;
            return WPEFramework::Core::ERROR_DESTRUCTION_SUCCEEDED;
        }
        return WPEFramework::Core::ERROR_NONE;
    }

    void* QueryInterface(const uint32_t id) override
    {
        if (id == WPEFramework::Exchange::IAppNotificationHandler::ID) {
            AddRef();
            return static_cast<WPEFramework::Exchange::IAppNotificationHandler*>(this);
        }
        return nullptr;
    }

    // IAppNotificationHandler
    WPEFramework::Core::hresult HandleAppEventNotifier(
        IEmitter* emitCb,
        const string& event,
        bool listen,
        bool& status) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        handleCount++;
        lastEmitter = emitCb;
        lastEvent   = event;
        lastListen  = listen;
        status      = _statusResult;
        return _handleRc;
    }

    // Test helpers
    void SetStatusResult(bool s)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _statusResult = s;
    }
    void SetHandleRc(uint32_t rc)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _handleRc = rc;
    }

    // Observable state (access under _mutex for thread safety)
    uint32_t handleCount{0};
    IEmitter* lastEmitter{nullptr};
    string lastEvent;
    bool lastListen{false};

    mutable std::mutex _mutex;

private:
    mutable std::atomic<uint32_t> _refCount;
    bool _statusResult;
    uint32_t _handleRc;
};

// -----------------------------------------------------------------------
// Fake IAppNotificationsImplementation
// Used by AppNotifications plugin shell Initialize() to return a real or
// fake implementation via Instantiate().
// -----------------------------------------------------------------------
class ANImplFake final : public WPEFramework::Exchange::IAppNotifications,
                         public WPEFramework::Exchange::IConfiguration {
public:
    ANImplFake() = default;

    ~ANImplFake() override = default;

    uint32_t AddRef() const override
    {
        return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    uint32_t Release() const override
    {
        const uint32_t n = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (n == 0) {
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
        if (id == WPEFramework::Exchange::IConfiguration::ID) {
            AddRef();
            return static_cast<WPEFramework::Exchange::IConfiguration*>(this);
        }
        return nullptr;
    }

    // IConfiguration
    uint32_t Configure(WPEFramework::PluginHost::IShell* /*shell*/) override
    {
        configureCount++;
        return WPEFramework::Core::ERROR_NONE;
    }

    // IAppNotifications
    WPEFramework::Core::hresult Subscribe(
        const WPEFramework::Exchange::IAppNotifications::AppNotificationContext& /*context*/,
        bool /*listen*/,
        const string& /*module*/,
        const string& /*event*/) override
    {
        subscribeCount++;
        return WPEFramework::Core::ERROR_NONE;
    }

    WPEFramework::Core::hresult Emit(const string& event,
                                     const string& payload,
                                     const string& appId) override
    {
        emitCount++;
        lastEmitEvent   = event;
        lastEmitPayload = payload;
        lastEmitAppId   = appId;
        return WPEFramework::Core::ERROR_NONE;
    }

    WPEFramework::Core::hresult Cleanup(const uint32_t connectionId,
                                        const string& origin) override
    {
        cleanupCount++;
        lastCleanupConnectionId = connectionId;
        lastCleanupOrigin       = origin;
        return WPEFramework::Core::ERROR_NONE;
    }

    // Observable state
    uint32_t configureCount{0};
    uint32_t subscribeCount{0};
    uint32_t emitCount{0};
    uint32_t cleanupCount{0};
    string lastEmitEvent;
    string lastEmitPayload;
    string lastEmitAppId;
    uint32_t lastCleanupConnectionId{0};
    string lastCleanupOrigin;

private:
    mutable std::atomic<uint32_t> _refCount{1};
};

// -----------------------------------------------------------------------
// AppNotificationsServiceMock
// Full IShell + ICOMLink mock for AppNotifications L0 tests.
// -----------------------------------------------------------------------
class AppNotificationsServiceMock final
    : public WPEFramework::PluginHost::IShell
    , public WPEFramework::PluginHost::IShell::ICOMLink {
public:
    struct Config {
        // Whether Instantiate() returns a valid IAppNotifications impl
        bool provideImplementation;
        // Whether QueryInterfaceByCallsign returns AppGateway responder
        bool provideAppGateway;
        // Whether QueryInterfaceByCallsign returns InternalGateway responder (LaunchDelegate)
        bool provideInternalGateway;
        // Callsign for notification handler
        string notificationHandlerCallsign;
        // Whether the notification handler is available
        bool provideNotificationHandler;
        // Whether handler returns status=true (success)
        bool handlerStatusResult;
        // Handler return code
        uint32_t handlerReturnCode;

        explicit Config(bool impl           = true,
                        bool gw             = true,
                        bool igw            = true,
                        bool handler        = true,
                        bool handlerStatus  = true,
                        uint32_t handlerRc  = WPEFramework::Core::ERROR_NONE)
            : provideImplementation(impl)
            , provideAppGateway(gw)
            , provideInternalGateway(igw)
            , notificationHandlerCallsign(FB_SETTINGS_CALLSIGN)
            , provideNotificationHandler(handler)
            , handlerStatusResult(handlerStatus)
            , handlerReturnCode(handlerRc)
        {
        }
    };

    explicit AppNotificationsServiceMock(Config cfg = Config(),
                                         const bool selfDelete = false)
        : _refCount(1)
        , _cfg(cfg)
        , _selfDelete(selfDelete)
        , _implFake(nullptr)
        , _appGatewayFake(nullptr)
        , _internalGatewayFake(nullptr)
        , _handlerFake(nullptr)
    {
    }

    ~AppNotificationsServiceMock() override
    {
        if (_implFake != nullptr) {
            _implFake->Release();
            _implFake = nullptr;
        }
        if (_appGatewayFake != nullptr) {
            _appGatewayFake->Release();
            _appGatewayFake = nullptr;
        }
        if (_internalGatewayFake != nullptr) {
            _internalGatewayFake->Release();
            _internalGatewayFake = nullptr;
        }
        if (_handlerFake != nullptr) {
            _handlerFake->Release();
            _handlerFake = nullptr;
        }
    }

    // Core::IUnknown
    uint32_t AddRef() const override
    {
        return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    uint32_t Release() const override
    {
        const uint32_t n = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (n == 0) {
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

    // ----------------------------------------------------------------
    // IShell
    // ----------------------------------------------------------------
    void EnableWebServer(const string& /*urlPath*/, const string& /*fsPath*/) override {}
    void DisableWebServer() override {}

    string Model() const override { return "l0test-device"; }
    bool Background() const override { return false; }
    string Accessor() const override { return "127.0.0.1:9998"; }
    string WebPrefix() const override { return "/jsonrpc"; }
    string Locator() const override
    {
        string locator;
        WPEFramework::Core::SystemInfo::GetEnvironment(_T("APPNOTIFICATIONS_LOCATOR"), locator);
        if (false == locator.empty()) {
            return locator;
        }
        return "libWPEFrameworkAppNotifications.so";
    }
    string ClassName() const override { return "AppNotificationsImplementation"; }
    string Versions() const override { return "1.0.0"; }
    string Callsign() const override { return APP_NOTIFICATIONS_CALLSIGN; }

    string PersistentPath() const override { return "/tmp"; }
    string VolatilePath() const override { return "/tmp"; }
    string DataPath() const override { return "/tmp"; }
    string ProxyStubPath() const override { return "/tmp"; }
    string SystemPath() const override { return "/tmp"; }
    string PluginPath() const override { return "/tmp"; }
    string SystemRootPath() const override { return "/"; }

    WPEFramework::Core::hresult SystemRootPath(const string& /*value*/) override
    {
        return WPEFramework::Core::ERROR_NONE;
    }

    startup Startup() const override { return startup::ACTIVATED; }
    WPEFramework::Core::hresult Startup(const startup /*value*/) override
    {
        return WPEFramework::Core::ERROR_NONE;
    }

    string Substitute(const string& input) const override { return input; }

    bool Resumed() const override { return false; }
    WPEFramework::Core::hresult Resumed(const bool /*value*/) override
    {
        return WPEFramework::Core::ERROR_NONE;
    }

    string HashKey() const override { return "hash"; }

    string ConfigLine() const override { return ""; }
    WPEFramework::Core::hresult ConfigLine(const string& /*config*/) override
    {
        return WPEFramework::Core::ERROR_NONE;
    }

    WPEFramework::Core::hresult Metadata(string& info /*@out*/) const override
    {
        info = R"({"name":"AppNotifications","version":"1.0.0"})";
        return WPEFramework::Core::ERROR_NONE;
    }

    bool IsSupported(const uint8_t /*version*/) const override { return true; }
    WPEFramework::PluginHost::ISubSystem* SubSystems() override { return nullptr; }
    void Notify(const string& /*message*/) override {}
    void Register(WPEFramework::PluginHost::IPlugin::INotification* /*sink*/) override {}
    void Unregister(WPEFramework::PluginHost::IPlugin::INotification* /*sink*/) override {}
    state State() const override { return state::ACTIVATED; }

    void* QueryInterfaceByCallsign(const uint32_t id,
                                   const string& name) override
    {
        // AppGateway responder
        if (id == WPEFramework::Exchange::IAppGatewayResponder::ID) {
            if (name == APP_GATEWAY_CALLSIGN) {
                if (_cfg.provideAppGateway) {
                    if (_appGatewayFake == nullptr) {
                        _appGatewayFake = new ANResponderFake();
                    }
                    _appGatewayFake->AddRef();
                    return static_cast<WPEFramework::Exchange::IAppGatewayResponder*>(_appGatewayFake);
                }
                return nullptr;
            }
            // InternalGateway (LaunchDelegate)
            if (name == INTERNAL_GATEWAY_CALLSIGN) {
                if (_cfg.provideInternalGateway) {
                    if (_internalGatewayFake == nullptr) {
                        _internalGatewayFake = new ANResponderFake();
                    }
                    _internalGatewayFake->AddRef();
                    return static_cast<WPEFramework::Exchange::IAppGatewayResponder*>(_internalGatewayFake);
                }
                return nullptr;
            }
            return nullptr;
        }

        // Notification handler (for ThunderSubscriptionManager)
        if (id == WPEFramework::Exchange::IAppNotificationHandler::ID) {
            if (_cfg.provideNotificationHandler && name == _cfg.notificationHandlerCallsign) {
                if (_handlerFake == nullptr) {
                    _handlerFake = new ANNotificationHandlerFake(_cfg.handlerStatusResult,
                                                                  _cfg.handlerReturnCode);
                }
                _handlerFake->AddRef();
                return static_cast<WPEFramework::Exchange::IAppNotificationHandler*>(_handlerFake);
            }
            return nullptr;
        }

        return nullptr;
    }

    WPEFramework::Core::hresult Activate(const reason /*why*/) override
    {
        return WPEFramework::Core::ERROR_NONE;
    }
    WPEFramework::Core::hresult Deactivate(const reason /*why*/) override
    {
        return WPEFramework::Core::ERROR_NONE;
    }
    WPEFramework::Core::hresult Unavailable(const reason /*why*/) override
    {
        return WPEFramework::Core::ERROR_NONE;
    }
    WPEFramework::Core::hresult Hibernate(const uint32_t /*timeout*/) override
    {
        return WPEFramework::Core::ERROR_NONE;
    }
    reason Reason() const override { return reason::REQUESTED; }

    uint32_t Submit(const uint32_t /*id*/,
                    const WPEFramework::Core::ProxyType<WPEFramework::Core::JSON::IElement>& /*response*/) override
    {
        return WPEFramework::Core::ERROR_NONE;
    }

    WPEFramework::PluginHost::IShell::ICOMLink* COMLink() override { return this; }

    // ICOMLink
    void Register(WPEFramework::RPC::IRemoteConnection::INotification* /*sink*/) override {}
    void Unregister(const WPEFramework::RPC::IRemoteConnection::INotification* /*sink*/) override {}
    void Register(WPEFramework::PluginHost::IShell::ICOMLink::INotification* /*sink*/) override {}
    void Unregister(WPEFramework::PluginHost::IShell::ICOMLink::INotification* /*sink*/) override {}

    WPEFramework::RPC::IRemoteConnection* RemoteConnection(const uint32_t /*connectionId*/) override
    {
        return nullptr;
    }

    void* Instantiate(const WPEFramework::RPC::Object& object,
                      const uint32_t /*waitTime*/,
                      uint32_t& connectionId) override
    {
        connectionId = 1;

        const std::string className = object.ClassName();

        // Accept any class name that ends with "AppNotificationsImplementation"
        auto endsWith = [](const std::string& s, const std::string& suffix) -> bool {
            if (s.size() < suffix.size()) {
                return false;
            }
            return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        };

        if (endsWith(className, "AppNotificationsImplementation")
            || endsWith(className, "::AppNotificationsImplementation")
            || endsWith(className, "IAppNotifications")) {

            if (!_cfg.provideImplementation) {
                return nullptr;
            }
            if (_implFake == nullptr) {
                _implFake = new ANImplFake();
            }
            _implFake->AddRef();
            return static_cast<WPEFramework::Exchange::IAppNotifications*>(_implFake);
        }

        return nullptr;
    }

    // Test helpers
    void SetHandlerCallsign(const string& callsign)
    {
        _cfg.notificationHandlerCallsign = callsign;
    }

    void SetHandlerStatusResult(bool status)
    {
        _cfg.handlerStatusResult = status;
        if (_handlerFake != nullptr) {
            _handlerFake->SetStatusResult(status);
        }
    }

    void SetHandlerReturnCode(uint32_t rc)
    {
        _cfg.handlerReturnCode = rc;
        if (_handlerFake != nullptr) {
            _handlerFake->SetHandleRc(rc);
        }
    }

    ANImplFake*                GetImplFake()            const { return _implFake; }
    ANResponderFake*           GetAppGatewayFake()       const { return _appGatewayFake; }
    ANResponderFake*           GetInternalGatewayFake()  const { return _internalGatewayFake; }
    ANNotificationHandlerFake* GetHandlerFake()          const { return _handlerFake; }

private:
    mutable std::atomic<uint32_t>  _refCount;
    Config                          _cfg;
    const bool                      _selfDelete;

    ANImplFake*                     _implFake;
    ANResponderFake*                _appGatewayFake;
    ANResponderFake*                _internalGatewayFake;
    ANNotificationHandlerFake*      _handlerFake;
};

// -----------------------------------------------------------------------
// Convenience: shared plugin + service fixture
// -----------------------------------------------------------------------
struct ANPluginAndService {
    AppNotificationsServiceMock* service{nullptr};
    WPEFramework::PluginHost::IPlugin* plugin{nullptr};

    explicit ANPluginAndService(const AppNotificationsServiceMock::Config& cfg = AppNotificationsServiceMock::Config())
        : service(new AppNotificationsServiceMock(cfg, true))
        , plugin(WPEFramework::Core::Service<WPEFramework::Plugin::AppNotifications>::Create<WPEFramework::PluginHost::IPlugin>())
    {
    }

    ~ANPluginAndService()
    {
        if (plugin != nullptr) {
            plugin->Release();
            plugin = nullptr;
        }
        if (service != nullptr) {
            service->Release();
            service = nullptr;
        }
    }
};

} // namespace L0Test
