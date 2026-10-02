#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>

#include "AppGatewayResponderImplementation.h"
#include "ServiceMock.h"

#include <core/core.h>

using WPEFramework::Core::ERROR_GENERAL;
using WPEFramework::Core::ERROR_NONE;
using WPEFramework::Exchange::GatewayContext;

namespace {

class EnvVarGuard final {
public:
    EnvVarGuard(const char* name, const char* value)
        : _name(name ? name : "")
        , _hadOld(false)
    {
        if (_name.empty()) {
            return;
        }

        const char* old = std::getenv(_name.c_str());
        if (old != nullptr) {
            _hadOld = true;
            _oldValue = old;
        }

        if (value != nullptr) {
            setenv(_name.c_str(), value, 1);
        } else {
            unsetenv(_name.c_str());
        }
    }

    ~EnvVarGuard()
    {
        if (_name.empty()) {
            return;
        }

        if (_hadOld) {
            setenv(_name.c_str(), _oldValue.c_str(), 1);
        } else {
            unsetenv(_name.c_str());
        }
    }

    EnvVarGuard(const EnvVarGuard&) = delete;
    EnvVarGuard& operator=(const EnvVarGuard&) = delete;

private:
    std::string _name;
    bool _hadOld;
    std::string _oldValue;
};

struct TestResult {
    uint32_t failures { 0 };
};

static void ExpectTrue(TestResult& tr, const bool condition, const std::string& what)
{
    if (!condition) {
        tr.failures++;
        std::cerr << "FAIL: " << what << std::endl;
    }
}

static void ExpectEqU32(TestResult& tr, const uint32_t actual, const uint32_t expected, const std::string& what)
{
    if (actual != expected) {
        tr.failures++;
        std::cerr << "FAIL: " << what << " expected=" << expected << " actual=" << actual << std::endl;
    }
}

static void ExpectEqStr(TestResult& tr, const std::string& actual, const std::string& expected, const std::string& what)
{
    if (actual != expected) {
        tr.failures++;
        std::cerr << "FAIL: " << what << " expected='" << expected << "' actual='" << actual << "'" << std::endl;
    }
}

class ConnChangeCollector final : public WPEFramework::Exchange::IAppGatewayResponder::INotification {
public:
    ConnChangeCollector() = default;
    ~ConnChangeCollector() override = default;

    // Stack-owned collector: keep refcounting no-op/deterministic for tests.
    uint32_t AddRef() const override { return 1; }
    uint32_t Release() const override { return WPEFramework::Core::ERROR_NONE; }
    void* QueryInterface(const uint32_t id) override
    {
        if (id == WPEFramework::Exchange::IAppGatewayResponder::INotification::ID) {
            return static_cast<WPEFramework::Exchange::IAppGatewayResponder::INotification*>(this);
        }
        return nullptr;
    }

    void OnAppConnectionChanged(const std::string& appId, const uint32_t connectionId, const bool connected) override
    {
        lastAppId = appId;
        lastConnectionId = connectionId;
        lastConnected = connected;
        receivedCount++;
    }

    uint32_t receivedCount { 0 };
    std::string lastAppId;
    uint32_t lastConnectionId { 0 };
    bool lastConnected { false };
};

static GatewayContext MakeContext(uint32_t requestId, uint32_t connectionId, const std::string& appId)
{
    GatewayContext ctx;
    ctx.requestId = requestId;
    ctx.connectionId = connectionId;
    ctx.appId = appId;
    return ctx;
}

// Respond/Emit/Request enqueue worker-pool jobs asynchronously. Give them time
// to complete before responders go out of scope to avoid teardown races.
static void DrainAsyncResponderJobs()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

// Create a ServiceMock whose ConfigLine is websocket-config-compatible so
// AppGatewayResponderImplementation::InitializeWebsocket() will parse it and bind to an ephemeral port.
static L0Test::ServiceMock::Config MakeResponderServiceConfig()
{
    L0Test::ServiceMock::Config cfg(true /*resolver*/, true /*responder*/, true /*transport*/);
    cfg.provideAuthenticator = true;
    cfg.provideAppNotifications = true;

    // IMPORTANT: must contain `"connector"` so InitializeWebsocket() attempts parsing.
    // Port 0 lets the OS choose a free ephemeral port, reducing flakiness across runs.
    cfg.configLineOverride = "{\"connector\":\"127.0.0.1:0\"}";
    return cfg;
}

} // namespace

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayResponderImplementation_Register_Unregister_And_CallbackDelivery()
{
    // Note: AppGatewayResponderImplementation implements COM-style interfaces, so it requires AddRef/Release.
    // Use Core::Sink<> to supply reference counting in a deterministic way for L0.
    TestResult tr;

    WPEFramework::Core::Sink<WPEFramework::Plugin::AppGatewayResponderImplementation> responder;
    ConnChangeCollector collector;

    ExpectEqU32(tr, responder.Register(&collector), ERROR_NONE, "Register returns ERROR_NONE");

    responder.OnConnectionStatusChanged("com.test.app", 111, true);
    ExpectEqU32(tr, collector.receivedCount, 1u, "Registered listener receives connection status change");
    ExpectEqStr(tr, collector.lastAppId, "com.test.app", "Listener received appId");
    ExpectEqU32(tr, collector.lastConnectionId, 111u, "Listener received connectionId");
    ExpectTrue(tr, collector.lastConnected == true, "Listener received connected=true");

    ExpectEqU32(tr, responder.Unregister(&collector), ERROR_NONE, "Unregister returns ERROR_NONE");

    responder.OnConnectionStatusChanged("com.test.app", 111, false);
    ExpectEqU32(tr, collector.receivedCount, 1u, "Unregistered listener does not receive further updates");

    // Unregister again -> error path
    ExpectEqU32(tr, responder.Unregister(&collector), ERROR_GENERAL, "Unregister missing notification returns ERROR_GENERAL");

    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayResponderImplementation_GetGatewayConnectionContext_EnvInjection_And_EmptyKey()
{
    TestResult tr;

    WPEFramework::Core::Sink<WPEFramework::Plugin::AppGatewayResponderImplementation> responder;

    std::string value;

    // Current implementation is a stub returning ERROR_NONE regardless of input.
    ExpectEqU32(tr,
               responder.GetGatewayConnectionContext(1, "" /*empty key*/, value),
               ERROR_NONE,
               "Empty contextKey currently returns ERROR_NONE (stub)");
    ExpectEqStr(tr, value, "", "Stub keeps contextValue unchanged for empty key");

    // Env injection variables are currently ignored by implementation.
    EnvVarGuard connIdGuard("APPGATEWAY_TEST_CONN_ID", "410");
    EnvVarGuard ctxKeyGuard("APPGATEWAY_TEST_CTX_KEY", "header.user-agent");
    EnvVarGuard ctxValueGuard("APPGATEWAY_TEST_CTX_VALUE", "UA/1.0");

    value.clear();
    ExpectEqU32(tr,
               responder.GetGatewayConnectionContext(410, "header.user-agent", value),
               ERROR_NONE,
               "Stub returns ERROR_NONE for matching key");
    ExpectEqStr(tr, value, "", "Stub does not inject env context value");

    // Mismatch connId still returns ERROR_NONE in current stub.
    value.clear();
    ExpectEqU32(tr,
               responder.GetGatewayConnectionContext(411, "header.user-agent", value),
               ERROR_NONE,
               "Stub ignores connectionId and returns ERROR_NONE");
    ExpectEqStr(tr, value, "", "Stub leaves contextValue empty for mismatched connectionId");

    // Mismatch key still returns ERROR_NONE in current stub.
    value.clear();
    ExpectEqU32(tr,
               responder.GetGatewayConnectionContext(410, "missing.key", value),
               ERROR_NONE,
               "Stub ignores contextKey and returns ERROR_NONE");
    ExpectEqStr(tr, value, "", "Stub leaves contextValue empty for mismatched key");

    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayResponderImplementation_RecordGatewayConnectionContext_DebugOps()
{
    /** Exercise all branches of RecordGatewayConnectionContext (lines 250-261):
     *  - DISABLE_DEBUG_FOR_CONNECTION adds the connectionId to the debug-disabled registry.
     *  - ENABLE_DEBUG_FOR_CONNECTION removes it.
     *  - Any other key is accepted without special handling.
     */
    TestResult tr;

    WPEFramework::Core::Sink<WPEFramework::Plugin::AppGatewayResponderImplementation> responder;

    // Branch: DISABLE_DEBUG_FOR_CONNECTION
    ExpectEqU32(tr,
               responder.RecordGatewayConnectionContext(42u, "disableDebugForConnection", "1"),
               ERROR_NONE,
               "RecordGatewayConnectionContext DISABLE_DEBUG returns ERROR_NONE");

    // Branch: ENABLE_DEBUG_FOR_CONNECTION
    ExpectEqU32(tr,
               responder.RecordGatewayConnectionContext(42u, "enableDebugForConnection", "1"),
               ERROR_NONE,
               "RecordGatewayConnectionContext ENABLE_DEBUG returns ERROR_NONE");

    // Branch: generic key (else-path — no special registry operation)
    ExpectEqU32(tr,
               responder.RecordGatewayConnectionContext(42u, "some.arbitrary.key", "value"),
               ERROR_NONE,
               "RecordGatewayConnectionContext generic key returns ERROR_NONE");

    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayResponderImplementation_Configure_And_Public_Methods_NoCrash()
{
    // Goal: execute constructor/configure/initialize-websocket code and some public methods
    // without relying on private-member access hacks (which break libstdc++).
    TestResult tr;

    L0Test::ServiceMock::Config cfg = MakeResponderServiceConfig();
    L0Test::ServiceMock service(cfg);

    WPEFramework::Core::Sink<WPEFramework::Plugin::AppGatewayResponderImplementation> responder;

    // Precondition: Configure must succeed before any responder API that enqueues jobs is used.
    const uint32_t cfgRc = responder.Configure(&service);
    ExpectEqU32(tr, cfgRc, ERROR_NONE, "Configure() returns ERROR_NONE");

    if (cfgRc == ERROR_NONE) {
        // Precondition: WorkerPool must be available (job enqueue relies on it).
        // The l0test main normally bootstraps this, but keep this test robust if used standalone.
        if (WPEFramework::Core::IWorkerPool::IsAvailable() == false) {
            tr.failures++;
            std::cerr << "FAIL: WorkerPool is not available; cannot enqueue responder jobs safely." << std::endl;
            return tr.failures;
        }

        // Keep this as a configure/public smoke test only; async enqueue paths
        // are covered in dedicated responder behavior tests to avoid teardown
        // races under valgrind in this narrow smoke case.
        std::string out;
        ExpectEqU32(tr,
                    responder.GetGatewayConnectionContext(10, "header.user-agent", out),
                    ERROR_NONE,
                    "GetGatewayConnectionContext() returns ERROR_NONE");
        ExpectEqU32(tr,
                    responder.RecordGatewayConnectionContext(10, "some.arbitrary.key", "value"),
                    ERROR_NONE,
                    "RecordGatewayConnectionContext() returns ERROR_NONE");
    } else {
        std::cerr << "NOTE: Skipping responder enqueue-path validation because Configure() failed." << std::endl;
    }

    // Current implementation returns ERROR_NONE even when no env injection is configured.
    EnvVarGuard connIdGuard("APPGATEWAY_TEST_CONN_ID", nullptr);
    EnvVarGuard ctxKeyGuard("APPGATEWAY_TEST_CTX_KEY", nullptr);
    EnvVarGuard ctxValueGuard("APPGATEWAY_TEST_CTX_VALUE", nullptr);

    std::string out;
    ExpectEqU32(tr,
               responder.GetGatewayConnectionContext(10, "header.user-agent", out),
               ERROR_NONE,
               "No env injection configured => ERROR_NONE (current implementation)");

    return tr.failures;
}
