#pragma once

#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <atomic>

#include <core/core.h>
#include <plugins/plugins.h>

#include "AppGatewayCommon.h"
#include "ServiceMock.h"
#include "L0Bootstrap.hpp"
#include "L0TestTypes.hpp"
#include "L0Expect.hpp"

using WPEFramework::Core::ERROR_NONE;
using WPEFramework::Core::ERROR_GENERAL;
using WPEFramework::Core::ERROR_UNAVAILABLE;
using WPEFramework::Core::ERROR_BAD_REQUEST;
using WPEFramework::Core::ERROR_UNKNOWN_KEY;
using WPEFramework::Core::ERROR_INVALID_INPUT_LENGTH;
using WPEFramework::Plugin::AppGatewayCommon;
using WPEFramework::PluginHost::IPlugin;

namespace Exchange = WPEFramework::Exchange;
namespace AGCTest {

// RAII guard for QueryInterface pointers — ensures Release() is always called.
template <typename T>
class QIGuard {
public:
    explicit QIGuard(IPlugin* plugin)
        : _ptr(plugin ? plugin->QueryInterface<T>() : nullptr) {}
    ~QIGuard() { if (_ptr) _ptr->Release(); }
    T* operator->() const { return _ptr; }
    T* get() const { return _ptr; }
    explicit operator bool() const { return _ptr != nullptr; }
    QIGuard(const QIGuard&) = delete;
    QIGuard& operator=(const QIGuard&) = delete;
private:
    T* _ptr;
};

// Use shared L0Test helpers — no local reimplementation.
using L0Test::TestResult;
using L0Test::ExpectTrue;
using L0Test::ExpectEqU32;
using L0Test::ExpectEqStr;

struct PluginAndService {
    L0Test::ServiceMock* service { nullptr };
    IPlugin* plugin { nullptr };

    explicit PluginAndService(const L0Test::ServiceMock::Config& cfg = L0Test::ServiceMock::Config())
        : service(new L0Test::ServiceMock(cfg, /*selfDelete=*/true))
        , plugin(WPEFramework::Core::Service<AppGatewayCommon>::Create<IPlugin>())
    {
    }

    ~PluginAndService()
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
};

inline Exchange::GatewayContext DefaultContext()
{
    Exchange::GatewayContext ctx;
    ctx.requestId = 1001;
    ctx.connectionId = 10;
    ctx.appId = "com.example.test";
    ctx.version = "1.0.0";
    return ctx;
}

// Minimal IEmitter stub for HandleAppEventNotifier tests.
// Heap-allocated, ref-counted; deletes itself when the last reference is released.
class StubEmitter : public Exchange::IAppNotificationHandler::IEmitter {
public:
    StubEmitter() : _refCount(1) {}
    ~StubEmitter() override = default;

    uint32_t AddRef() const override
    {
        return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    uint32_t Release() const override {
        const uint32_t r = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (0 == r) {
            delete this;
            return WPEFramework::Core::ERROR_DESTRUCTION_SUCCEEDED;
        }
        return WPEFramework::Core::ERROR_NONE;
    }
    void* QueryInterface(const uint32_t id) override {
        if (Exchange::IAppNotificationHandler::IEmitter::ID == id) {
            AddRef();
            return static_cast<Exchange::IAppNotificationHandler::IEmitter*>(this);
        }
        return nullptr;
    }
    void Emit(const std::string& /*event*/, const std::string& /*payload*/, const std::string& /*appId*/) override {}

private:
    mutable std::atomic<uint32_t> _refCount;
};

// Shared plugin fixture for tests that don't require plugin isolation.
// Lazily initializes a single PluginAndService, reused across all routing,
// setter, and event tests — avoiding the ~8s SystemDelegate Subscribe
// timeout overhead per init/deinit cycle.
struct SharedFixture {
    static SharedFixture& instance() {
        static SharedFixture sf;
        return sf;
    }

    PluginAndService& ps() {
        if (!_initialized) {
            _ps.plugin->Initialize(_ps.service);
            _initialized = true;
        }
        return _ps;
    }

    void cleanup() {
        if (_initialized) {
            _ps.plugin->Deinitialize(_ps.service);
            _initialized = false;
        }
    }

private:
    SharedFixture() : _initialized(false) {}
    ~SharedFixture() { cleanup(); }
    SharedFixture(const SharedFixture&) = delete;
    SharedFixture& operator=(const SharedFixture&) = delete;

    PluginAndService _ps;
    bool _initialized;
};

// Helper: test a delegate-backed getter method.
// In L0 (no real plugins), the delegate may return ERROR_NONE, ERROR_UNAVAILABLE, or ERROR_GENERAL.
// Uses SharedFixture to avoid per-test init/deinit overhead.
inline uint32_t DelegateGetterTest(const std::string& method,
                                   const Exchange::GatewayContext& ctx = DefaultContext())
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, method, "{}", result);
    const bool ok = (rc == ERROR_NONE || rc == ERROR_UNAVAILABLE || rc == ERROR_GENERAL);
    ExpectTrue(tr, ok, method + " returns acceptable code in L0");
    return tr.failures;
}

} // namespace AGCTest

// Pull helpers into global scope so test functions compile unchanged.
using AGCTest::TestResult;
using AGCTest::ExpectTrue;
using AGCTest::ExpectEqU32;
using AGCTest::ExpectEqStr;
using AGCTest::PluginAndService;
using AGCTest::SharedFixture;
using AGCTest::DefaultContext;
using AGCTest::QIGuard;
using AGCTest::StubEmitter;
using AGCTest::DelegateGetterTest;
using L0Test::PrintTotals;
using L0Test::ResultToExitCode;
