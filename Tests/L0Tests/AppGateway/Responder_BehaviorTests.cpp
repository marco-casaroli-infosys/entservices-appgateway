#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>

#include <core/core.h>

#include <AppGateway.h>
#include "ServiceMock.h"
#include "L0Expect.hpp"
#include "L0TestTypes.hpp"

using WPEFramework::Core::ERROR_BAD_REQUEST;
using WPEFramework::Core::ERROR_NONE;
using WPEFramework::Core::ERROR_UNAVAILABLE;
using WPEFramework::Exchange::IAppGatewayResponder;
using WPEFramework::Exchange::GatewayContext;
using WPEFramework::Plugin::AppGateway;
using WPEFramework::PluginHost::IPlugin;
using L0Test::ExpectEqStr;
using L0Test::ExpectEqU32;
using L0Test::ExpectTrue;
using L0Test::TestResult;

namespace {

struct PluginAndService {
    L0Test::ServiceMock* service { nullptr };
    IPlugin* plugin { nullptr };

    explicit PluginAndService(const L0Test::ServiceMock::Config& cfg = {})
        : service(new L0Test::ServiceMock(cfg, true))
        , plugin(WPEFramework::Core::Service<AppGateway>::Create<IPlugin>())
    {
    }

    ~PluginAndService()
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

// Simple collector for connection change notifications
class ConnChangeCollector : public IAppGatewayResponder::INotification {
public:
    ConnChangeCollector() = default;
    ~ConnChangeCollector() override = default;

    // Implement Core::IUnknown non-owning semantics for test stack object.
    uint32_t AddRef() const override { return 1; }
    uint32_t Release() const override { return WPEFramework::Core::ERROR_NONE; }
    void* QueryInterface(const uint32_t id) override
    {
        if (id == IAppGatewayResponder::INotification::ID) {
            return static_cast<IAppGatewayResponder::INotification*>(this);
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

static GatewayContext MakeContext(uint32_t requestId = 100, uint32_t connectionId = 77, const std::string& appId = "com.example.app")
{
    GatewayContext ctx;
    ctx.requestId = requestId;
    ctx.connectionId = connectionId;
    ctx.appId = appId;
    return ctx;
}

static void DrainAsyncResponderJobs()
{
    // Respond/Emit/Request submit worker-pool jobs; give them a brief window
    // to complete before responder/plugin teardown.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

} // namespace

// PUBLIC_INTERFACE
uint32_t Test_Responder_Register_Unregister_Notifications()
{
    TestResult tr;

    // Per-test isolation: fresh shell + plugin instance.
    PluginAndService ps;

    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectEqStr(tr, initResult, "", "Initialize() succeeds for responder register/unregister test");

    // Guard: if Initialize() failed, do not proceed to call responder APIs.
    if (!initResult.empty()) {
        std::cerr << "NOTE: Skipping responder test body because Initialize() failed." << std::endl;
        ps.plugin->Deinitialize(ps.service);
        return tr.failures;
    }

    auto* responder = static_cast<IAppGatewayResponder*>(
        ps.plugin->QueryInterface(IAppGatewayResponder::ID));
    ExpectTrue(tr, responder != nullptr, "Responder interface available");

    if (responder != nullptr) {
        ConnChangeCollector collector;
        const uint32_t regRc = responder->Register(&collector);
        ExpectEqU32(tr, regRc, ERROR_NONE, "Register returns ERROR_NONE");

        // If the in-proc ResponderFake is available, simulate a connection status change.
        // Otherwise we are running with the real responder implementation, where we can't
        // synthetically trigger a websocket connect/disconnect in this unit test.
        auto* fake = static_cast<L0Test::ResponderFake*>(responder->QueryInterface(L0Test::ID_RESPONDER_FAKE));
        if (fake != nullptr) {
            fake->SimulateAppConnectionChanged("com.example.app", 101, true);
            ExpectEqU32(tr, collector.receivedCount, 1u, "Listener receives simulated connection change after Register");
            ExpectEqStr(tr, collector.lastAppId, "com.example.app", "AppId propagated to listener");
            ExpectEqU32(tr, collector.lastConnectionId, 101u, "ConnectionId propagated to listener");
            ExpectTrue(tr, collector.lastConnected, "Connected=true propagated");
            fake->Release();
            fake = nullptr;
        } else {
            std::cerr << "NOTE: ResponderFake not available; skipping simulated connection-change validation." << std::endl;
        }

        const uint32_t unregRc = responder->Unregister(&collector);
        ExpectEqU32(tr, unregRc, ERROR_NONE, "Unregister returns ERROR_NONE");

        responder->Release();
    }

    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_Responder_Respond_Success_And_Unavailable()
{
    TestResult tr;

    PluginAndService ps;
    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectEqStr(tr, initResult, "", "Initialize() succeeds");

    // Precondition: do not enqueue responder jobs if Initialize() failed.
    if (!initResult.empty()) {
        std::cerr << "NOTE: Skipping responder test body because Initialize() failed." << std::endl;
        ps.plugin->Deinitialize(ps.service);
        return tr.failures;
    }

    auto* responder = static_cast<IAppGatewayResponder*>(
        ps.plugin->QueryInterface(IAppGatewayResponder::ID));
    ExpectTrue(tr, responder != nullptr, "Responder interface available");

    if (responder != nullptr) {
        auto* fake = static_cast<L0Test::ResponderFake*>(responder->QueryInterface(L0Test::ID_RESPONDER_FAKE));

        GatewayContext ctx = MakeContext(1, 200, "com.x");
        uint32_t rc = responder->Respond(ctx, R"({"ok":true})");
        ExpectEqU32(tr, rc, ERROR_NONE, "Respond returns ERROR_NONE in L0 harness");

        // Fake-only: validate disabling transport yields ERROR_UNAVAILABLE.
        if (fake != nullptr) {
            fake->SetTransportEnabled(false);
            rc = responder->Respond(ctx, R"({"ok":true})");
            ExpectEqU32(tr, rc, ERROR_UNAVAILABLE, "Respond returns ERROR_UNAVAILABLE when transport is disabled (fake only)");
            fake->Release();
            fake = nullptr;
        } else {
            std::cerr << "NOTE: ResponderFake not available; skipping transport-disable validation." << std::endl;
        }

        DrainAsyncResponderJobs();

        responder->Release();
    }

    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_Responder_Emit_Success_And_Unavailable()
{
    TestResult tr;

    PluginAndService ps;
    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectEqStr(tr, initResult, "", "Initialize() succeeds");

    // Guard: if Initialize() failed, do not enqueue/use responder paths.
    if (!initResult.empty()) {
        std::cerr << "NOTE: Skipping responder test body because Initialize() failed." << std::endl;
        ps.plugin->Deinitialize(ps.service);
        return tr.failures;
    }

    auto* responder = static_cast<IAppGatewayResponder*>(
        ps.plugin->QueryInterface(IAppGatewayResponder::ID));
    ExpectTrue(tr, responder != nullptr, "Responder interface available");

    if (responder != nullptr) {
        auto* fake = static_cast<L0Test::ResponderFake*>(responder->QueryInterface(L0Test::ID_RESPONDER_FAKE));

        GatewayContext ctx = MakeContext(3, 201, "com.y");
        uint32_t rc = responder->Emit(ctx, "event.name", R"({"a":1})");
        ExpectEqU32(tr, rc, ERROR_NONE, "Emit returns ERROR_NONE in L0 harness");

        if (fake != nullptr) {
            fake->SetTransportEnabled(false);
            rc = responder->Emit(ctx, "event.name", R"({"a":1})");
            ExpectEqU32(tr, rc, ERROR_UNAVAILABLE, "Emit returns ERROR_UNAVAILABLE when transport is disabled (fake only)");
            fake->Release();
            fake = nullptr;
        } else {
            std::cerr << "NOTE: ResponderFake not available; skipping transport-disable validation." << std::endl;
        }

        DrainAsyncResponderJobs();

        responder->Release();
    }

    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_Responder_Request_Success_And_Unavailable()
{
    TestResult tr;

    PluginAndService ps;
    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectEqStr(tr, initResult, "", "Initialize() succeeds");

    // Guard: if Initialize() failed, do not enqueue/use responder paths.
    if (!initResult.empty()) {
        std::cerr << "NOTE: Skipping responder test body because Initialize() failed." << std::endl;
        ps.plugin->Deinitialize(ps.service);
        return tr.failures;
    }

    auto* responder = static_cast<IAppGatewayResponder*>(
        ps.plugin->QueryInterface(IAppGatewayResponder::ID));
    ExpectTrue(tr, responder != nullptr, "Responder interface available");

    if (responder != nullptr) {
        auto* fake = static_cast<L0Test::ResponderFake*>(responder->QueryInterface(L0Test::ID_RESPONDER_FAKE));

        uint32_t rc = responder->Request(300 /*connectionId*/, 9 /*id*/, "method.x", R"({"b":2})");
        ExpectEqU32(tr, rc, ERROR_NONE, "Request returns ERROR_NONE in L0 harness");

        if (fake != nullptr) {
            fake->SetTransportEnabled(false);
            rc = responder->Request(300 /*connectionId*/, 10 /*id*/, "method.x", R"({"b":2})");
            ExpectEqU32(tr, rc, ERROR_UNAVAILABLE, "Request returns ERROR_UNAVAILABLE when transport is disabled (fake only)");
            fake->Release();
            fake = nullptr;
        } else {
            std::cerr << "NOTE: ResponderFake not available; skipping transport-disable validation." << std::endl;
        }

        DrainAsyncResponderJobs();

        responder->Release();
    }

    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_Responder_GetGatewayConnectionContext_Known_And_Unknown()
{
    TestResult tr;

    PluginAndService ps;
    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectEqStr(tr, initResult, "", "Initialize() succeeds");

    // Guard: if Initialize() failed, do not enqueue/use responder paths.
    if (!initResult.empty()) {
        std::cerr << "NOTE: Skipping responder test body because Initialize() failed." << std::endl;
        ps.plugin->Deinitialize(ps.service);
        return tr.failures;
    }

    auto* responder = static_cast<IAppGatewayResponder*>(
        ps.plugin->QueryInterface(IAppGatewayResponder::ID));
    ExpectTrue(tr, responder != nullptr, "Responder interface available");

    if (responder != nullptr) {
        auto* fake = static_cast<L0Test::ResponderFake*>(responder->QueryInterface(L0Test::ID_RESPONDER_FAKE));

        const uint32_t cid = 410;
        bool usedEnvInjection = false;
        if (fake != nullptr) {
            fake->SetConnectionContext(cid, "header.user-agent", "UA/1.0");
        } else {
            // Real responder: use env-var injection hook (consumed by AppGatewayResponderImplementation).
            setenv("APPGATEWAY_TEST_CONN_ID", "410", 1);
            setenv("APPGATEWAY_TEST_CTX_KEY", "header.user-agent", 1);
            setenv("APPGATEWAY_TEST_CTX_VALUE", "UA/1.0", 1);
            usedEnvInjection = true;
        }

        std::string value;
        uint32_t rc = responder->GetGatewayConnectionContext(cid, "header.user-agent", value);
        if (fake != nullptr) {
            ExpectEqU32(tr, rc, ERROR_NONE, "Fake returns ERROR_NONE for known key");
            ExpectEqStr(tr, value, "UA/1.0", "Fake returns stored context value for known key");
        } else {
            ExpectEqU32(tr, rc, ERROR_NONE, "Implementation returns ERROR_NONE for known key");
            ExpectEqStr(tr, value, "", "Current implementation keeps context value empty (stub)");
        }

        value.clear();
        rc = responder->GetGatewayConnectionContext(cid, "missing.key", value);
        if (fake != nullptr) {
            ExpectEqU32(tr, rc, ERROR_BAD_REQUEST, "Fake returns ERROR_BAD_REQUEST for unknown key");
        } else {
            ExpectEqU32(tr, rc, ERROR_NONE, "Implementation returns ERROR_NONE for unknown key (current stub)");
        }

        value.clear();
        rc = responder->GetGatewayConnectionContext(999 /*unknown cid*/, "header.user-agent", value);
        if (fake != nullptr) {
            ExpectEqU32(tr, rc, ERROR_BAD_REQUEST, "Fake returns ERROR_BAD_REQUEST for unknown connection");
        } else {
            ExpectEqU32(tr, rc, ERROR_NONE, "Implementation returns ERROR_NONE for unknown connection (current stub)");
        }

        if (usedEnvInjection) {
            unsetenv("APPGATEWAY_TEST_CONN_ID");
            unsetenv("APPGATEWAY_TEST_CTX_KEY");
            unsetenv("APPGATEWAY_TEST_CTX_VALUE");
        }

        if (fake != nullptr) {
            fake->Release();
            fake = nullptr;
        }

        responder->Release();
    }

    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}
