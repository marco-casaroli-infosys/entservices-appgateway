#pragma once

/**
 * @file L0ResponderFake.hpp
 * @brief Reusable IAppGatewayResponder fake for L0 tests.
 *
 * This fake can be used by any plugin's L0 test that needs to verify
 * calls to IAppGatewayResponder::Emit(), Respond(), Request(), etc.
 * Placed in common/ so it is available to AppGateway, AppNotifications,
 * and any future plugin L0 tests.
 *
 * Note: This type is named CommonResponderFake to avoid a name collision
 * with the L0Test::ResponderFake already defined in
 * Tests/L0Tests/AppGateway/ServiceMock.h which is a richer fake that also
 * implements IConfiguration and exposes a custom QueryInterface ID.
 */

#include <atomic>
#include <cstdint>
#include <string>
#include <mutex>

#include <core/core.h>
#include <interfaces/IAppGateway.h>

namespace L0Test {

class CommonResponderFake final : public WPEFramework::Exchange::IAppGatewayResponder {
public:
    explicit CommonResponderFake(const bool transportEnabled = true)
        : _refCount(1)
        , _transportEnabled(transportEnabled)
    {
    }

    ~CommonResponderFake() override = default;

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
        return nullptr;
    }

    // IAppGatewayResponder
    WPEFramework::Core::hresult Respond(const WPEFramework::Exchange::GatewayContext& context,
                                        const std::string& payload) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        lastRespondContext = context;
        lastRespondPayload = payload;
        respondCount.fetch_add(1, std::memory_order_relaxed);
        return _transportEnabled ? WPEFramework::Core::ERROR_NONE : WPEFramework::Core::ERROR_UNAVAILABLE;
    }

    WPEFramework::Core::hresult Emit(const WPEFramework::Exchange::GatewayContext& context,
                                     const std::string& method,
                                     const std::string& payload) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        lastEmitContext = context;
        lastEmitMethod = method;
        lastEmitPayload = payload;
        emitCount.fetch_add(1, std::memory_order_relaxed);
        return _transportEnabled ? WPEFramework::Core::ERROR_NONE : WPEFramework::Core::ERROR_UNAVAILABLE;
    }

    WPEFramework::Core::hresult Request(const uint32_t connectionId,
                                        const uint32_t id,
                                        const std::string& method,
                                        const std::string& params) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        lastRequestConnectionId = connectionId;
        lastRequestId = id;
        lastRequestMethod = method;
        lastRequestParams = params;
        requestCount.fetch_add(1, std::memory_order_relaxed);
        return _transportEnabled ? WPEFramework::Core::ERROR_NONE : WPEFramework::Core::ERROR_UNAVAILABLE;
    }

    WPEFramework::Core::hresult GetGatewayConnectionContext(const uint32_t /*connectionId*/,
                                                            const std::string& /*contextKey*/,
                                                            std::string& /*contextValue*/) override
    {
        return WPEFramework::Core::ERROR_NONE;
    }

    WPEFramework::Core::hresult RecordGatewayConnectionContext(const uint32_t /*connectionId*/,
                                                               const std::string& /*contextKey*/,
                                                               const std::string& /*contextValue*/) override
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

    // Test helpers
    void SetTransportEnabled(bool enabled) { _transportEnabled = enabled; }
    void Reset()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        respondCount.store(0, std::memory_order_relaxed);
        emitCount.store(0, std::memory_order_relaxed);
        requestCount.store(0, std::memory_order_relaxed);
        lastRespondPayload.clear();
        lastEmitMethod.clear();
        lastEmitPayload.clear();
        lastRequestMethod.clear();
        lastRequestParams.clear();
    }

    // Observable state: counters are atomic for safe lockless reads;
    // last* string/context fields are guarded by _mutex.
    std::atomic<uint32_t> respondCount{0};
    std::atomic<uint32_t> emitCount{0};
    std::atomic<uint32_t> requestCount{0};
    WPEFramework::Exchange::GatewayContext lastRespondContext{};
    std::string lastRespondPayload;
    WPEFramework::Exchange::GatewayContext lastEmitContext{};
    std::string lastEmitMethod;
    std::string lastEmitPayload;
    uint32_t lastRequestConnectionId{0};
    uint32_t lastRequestId{0};
    std::string lastRequestMethod;
    std::string lastRequestParams;

private:
    mutable std::atomic<uint32_t> _refCount;
    bool _transportEnabled;
    mutable std::mutex _mutex;
};

} // namespace L0Test
