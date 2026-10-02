/*
 * AppActionsServiceMock.h
 *
 * L0 test ServiceMock for AppActions plugin.
 * Implements PluginHost::IShell and ICOMLink so the plugin can:
 *   - Initialize/Deinitialize via service->Root<>()
 *   - Register/Unregister notifications
 *
 * Pattern mirrors Tests/L0Tests/AppNotifications/AppNotificationsServiceMock.h
 */

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <string>
#include <mutex>
#include <algorithm>
#include <list>
#include <vector>

#include <Module.h>
#include <core/core.h>
#include <plugins/IShell.h>

#include <interfaces/IAppActions.h>
#include <interfaces/IConfiguration.h>
#include <AppActions.h>
#include <set>

#include "L0TestTypes.hpp"

namespace L0Test {

using string = std::string;

// -----------------------------------------------------------------------
// Fake IAppActions::INotification
// Used to capture notification callbacks
// -----------------------------------------------------------------------
class AANotificationFake final : public WPEFramework::Exchange::IAppActions::INotification {
public:
    explicit AANotificationFake()
        : onActionStartRequestCount(0)
        , _refCount(1)
    {
    }

    ~AANotificationFake() override = default;

    uint32_t AddRef() const override
    {
        return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    uint32_t Release() const override
    {
        const uint32_t n = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (0 == n) {
            delete this;
            return WPEFramework::Core::ERROR_DESTRUCTION_SUCCEEDED;
        }
        return WPEFramework::Core::ERROR_NONE;
    }

    void* QueryInterface(const uint32_t id) override
    {
        if (id == WPEFramework::Exchange::IAppActions::INotification::ID) {
            AddRef();
            return static_cast<WPEFramework::Exchange::IAppActions::INotification*>(this);
        }
        return nullptr;
    }

    // IAppActions::INotification
    void OnActionStartRequest(const string& initiator,
                              const string& intent,
                              const string& handlerAppId) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        onActionStartRequestCount++;
        lastInitiator = initiator;
        lastIntent = intent;
        lastHandlerAppId = handlerAppId;
        receivedInitiators.push_back(initiator);
        _cv.notify_all();
    }

    // Block until at least 'count' callbacks have arrived or timeout expires.
    // Returns true if the count was reached, false on timeout.
    bool WaitForCount(uint32_t count,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(500))
    {
        std::unique_lock<std::mutex> lock(_mutex);
        return _cv.wait_for(lock, timeout, [this, count]() {
            return onActionStartRequestCount >= count;
        });
    }

    // Observable state (access under _mutex for thread safety)
    uint32_t onActionStartRequestCount;
    string lastInitiator;
    string lastIntent;
    string lastHandlerAppId;
    std::vector<string> receivedInitiators;  // all arrivals in delivery order

    mutable std::mutex _mutex;
    std::condition_variable _cv;

private:
    mutable std::atomic<uint32_t> _refCount;
};

// -----------------------------------------------------------------------
// Fake RPC::IRemoteConnection
// Used for testing Deactivated callback
// -----------------------------------------------------------------------
class AARemoteConnectionFake final : public WPEFramework::RPC::IRemoteConnection {
public:
    explicit AARemoteConnectionFake(uint32_t id = 1)
        : _refCount(1)
        , _id(id)
    {
    }

    ~AARemoteConnectionFake() override = default;

    uint32_t AddRef() const override
    {
        return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    uint32_t Release() const override
    {
        const uint32_t n = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (0 == n) {
            delete this;
            return WPEFramework::Core::ERROR_DESTRUCTION_SUCCEEDED;
        }
        return WPEFramework::Core::ERROR_NONE;
    }

    void* QueryInterface(const uint32_t id) override
    {
        if (id == WPEFramework::RPC::IRemoteConnection::ID) {
            AddRef();
            return static_cast<WPEFramework::RPC::IRemoteConnection*>(this);
        }
        return nullptr;
    }

    uint32_t Id() const override { return _id; }
    uint32_t RemoteId() const override { return _id; }

    void* Acquire(const uint32_t /*waitTime*/,
                  const string& /*className*/,
                  const uint32_t /*interfaceId*/,
                  const uint32_t /*version*/) override
    {
        return nullptr;
    }

    void Terminate() override
    {
        terminateCalled = true;
    }

    uint32_t Launch() override
    {
        return WPEFramework::Core::ERROR_NONE;
    }

    void PostMortem() override
    {
        // No-op for test fake
    }

    bool terminateCalled{false};

private:
    mutable std::atomic<uint32_t> _refCount;
    uint32_t _id;
};

// -----------------------------------------------------------------------
// Fake IAppActions Implementation
// Used by AppActions plugin shell Initialize() to return via Instantiate().
// -----------------------------------------------------------------------
class AAImplFake final : public WPEFramework::Exchange::IAppActions,
                         public WPEFramework::Exchange::IConfiguration {
public:
    AAImplFake() = default;

    ~AAImplFake() override = default;

    uint32_t AddRef() const override
    {
        return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    uint32_t Release() const override
    {
        const uint32_t n = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (0 == n) {
            delete this;
            return WPEFramework::Core::ERROR_DESTRUCTION_SUCCEEDED;
        }
        return WPEFramework::Core::ERROR_NONE;
    }

    void* QueryInterface(const uint32_t id) override
    {
        if (id == WPEFramework::Exchange::IAppActions::ID) {
            AddRef();
            return static_cast<WPEFramework::Exchange::IAppActions*>(this);
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

    // IAppActions
    WPEFramework::Core::hresult ActionStart(const string& initiator,
                                            const string& intent,
                                            const string& handlerAppId) override
    {
        actionStartCount++;
        lastInitiator = initiator;
        lastIntent = intent;
        lastHandlerAppId = handlerAppId;
        return WPEFramework::Core::ERROR_NONE;
    }

    WPEFramework::Core::hresult Register(INotification* notification) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (std::find(_notifications.begin(), _notifications.end(), notification) == _notifications.end()) {
            _notifications.push_back(notification);
            notification->AddRef();
            registerCount++;
        }
        return WPEFramework::Core::ERROR_NONE;
    }

    WPEFramework::Core::hresult Unregister(INotification* notification) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = std::find(_notifications.begin(), _notifications.end(), notification);
        if (it != _notifications.end()) {
            (*it)->Release();
            _notifications.erase(it);
            unregisterCount++;
        }
        return WPEFramework::Core::ERROR_NONE;
    }

    // Observable state
    uint32_t configureCount{0};
    uint32_t actionStartCount{0};
    uint32_t registerCount{0};
    uint32_t unregisterCount{0};
    string lastInitiator;
    string lastIntent;
    string lastHandlerAppId;

    mutable std::mutex _mutex;
    std::list<INotification*> _notifications;

private:
    mutable std::atomic<uint32_t> _refCount{1};
};

// -----------------------------------------------------------------------
// AppActionsServiceMock
// Full IShell + ICOMLink mock for AppActions L0 tests.
// -----------------------------------------------------------------------
class AppActionsServiceMock final
    : public WPEFramework::PluginHost::IShell
    , public WPEFramework::PluginHost::IShell::ICOMLink {
public:
    struct Config {
        bool provideImplementation{true};
        uint32_t connectionId{1};

        explicit Config(bool impl = true)
            : provideImplementation(impl)
        {
        }
    };

    explicit AppActionsServiceMock(const Config& cfg = Config())
        : _refCount(1)
        , _cfg(cfg)
        , _implFake(nullptr)
    {
    }

    ~AppActionsServiceMock() override
    {
        if (nullptr != _implFake) {
            _implFake->Release();
            _implFake = nullptr;
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
        if (0 == n) {
            delete this;
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
    string Locator() const override { return "libWPEFrameworkAppActionsImplementation.so"; }
    string ClassName() const override { return "AppActionsImplementation"; }
    string Versions() const override { return "1.0.0"; }
    string Callsign() const override { return "org.rdk.AppActions"; }

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
        info = R"({"name":"AppActions","version":"1.0.0"})";
        return WPEFramework::Core::ERROR_NONE;
    }

    bool IsSupported(const uint8_t /*version*/) const override { return true; }
    WPEFramework::PluginHost::ISubSystem* SubSystems() override { return nullptr; }
    void Notify(const string& /*message*/) override {}

    void Register(WPEFramework::PluginHost::IPlugin::INotification* notification) override
    {
        std::lock_guard<std::mutex> lock(_notificationMutex);
        _notifications.push_back(notification);
        notification->AddRef();
    }

    void Unregister(WPEFramework::PluginHost::IPlugin::INotification* notification) override
    {
        std::lock_guard<std::mutex> lock(_notificationMutex);
        auto it = std::find(_notifications.begin(), _notifications.end(), notification);
        if (it != _notifications.end()) {
            (*it)->Release();
            _notifications.erase(it);
        }
    }

    state State() const override { return state::ACTIVATED; }

    void* QueryInterfaceByCallsign(const uint32_t /*id*/, const string& /*name*/) override
    {
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

    // ----------------------------------------------------------------
    // ICOMLink
    // ----------------------------------------------------------------
    void Register(WPEFramework::RPC::IRemoteConnection::INotification* sink) override {
        std::lock_guard<std::mutex> lock(_notificationMutex);
        _rpcNotification = sink;
    }
    void Unregister(const WPEFramework::RPC::IRemoteConnection::INotification* /*sink*/) override {
        std::lock_guard<std::mutex> lock(_notificationMutex);
        _rpcNotification = nullptr;
    }
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
        connectionId = _cfg.connectionId;

        const std::string className = object.ClassName();

        // Accept any class name that ends with "AppActionsImplementation"
        auto endsWith = [](const std::string& s, const std::string& suffix) -> bool {
            if (s.size() < suffix.size()) {
                return false;
            }
            return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        };

        if (endsWith(className, "AppActionsImplementation")
            || endsWith(className, "::AppActionsImplementation")
            || endsWith(className, "IAppActions")) {

            if (!_cfg.provideImplementation) {
                return nullptr;
            }

            if (nullptr == _implFake) {
                _implFake = new AAImplFake();
            }
            _implFake->AddRef();
            return static_cast<WPEFramework::Exchange::IAppActions*>(_implFake);
        }

        return nullptr;
    }

    // Access to internal state for testing
    AAImplFake* GetImplFake() const { return _implFake; }
    uint32_t GetConnectionId() const { return _cfg.connectionId; }

    // Simulate an out-of-process connection Deactivated event.
    // Calls Deactivated() on the registered RPC::IRemoteConnection::INotification sink
    // (which is AppActions::Notification) with the given connection.
    void SimulateDeactivated(WPEFramework::RPC::IRemoteConnection* connection) {
        WPEFramework::RPC::IRemoteConnection::INotification* sink = nullptr;
        {
            std::lock_guard<std::mutex> lock(_notificationMutex);
            sink = _rpcNotification;
        }
        if (nullptr != sink) {
            sink->Deactivated(connection);
        }
    }

private:
    mutable std::atomic<uint32_t> _refCount;
    Config _cfg;
    AAImplFake* _implFake;
    WPEFramework::RPC::IRemoteConnection::INotification* _rpcNotification{nullptr};
    std::mutex _notificationMutex;
    std::list<WPEFramework::PluginHost::IPlugin::INotification*> _notifications;
};

// -----------------------------------------------------------------------
// AAPluginAndService
// RAII helper that creates both the AppActions plugin shell and a service mock
// -----------------------------------------------------------------------
struct AAPluginAndService {
    WPEFramework::Plugin::AppActions* plugin{nullptr};
    AppActionsServiceMock* service{nullptr};

    explicit AAPluginAndService(const AppActionsServiceMock::Config& cfg = AppActionsServiceMock::Config())
    {
        plugin = WPEFramework::Core::Service<WPEFramework::Plugin::AppActions>::Create<WPEFramework::Plugin::AppActions>();
        service = new AppActionsServiceMock(cfg);
    }

    ~AAPluginAndService()
    {
        if (nullptr != plugin) {
            plugin->Release();
            plugin = nullptr;
        }
        if (nullptr != service) {
            service->Release();
            service = nullptr;
        }
    }

    // Non-copyable
    AAPluginAndService(const AAPluginAndService&) = delete;
    AAPluginAndService& operator=(const AAPluginAndService&) = delete;
};

} // namespace L0Test
