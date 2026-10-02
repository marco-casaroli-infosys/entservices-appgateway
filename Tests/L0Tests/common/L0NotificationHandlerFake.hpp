#pragma once

/**
 * @file L0NotificationHandlerFake.hpp
 * @brief Reusable IAppNotificationHandler fake for L0 tests.
 *
 * This is a simple, non-GMock fake that implements IAppNotificationHandler.
 * It records calls to HandleAppEventNotifier() and allows tests to control
 * the return value and status output.
 *
 * Placed in common/ so any plugin L0 test needing IAppNotificationHandler
 * can reuse it without depending on GMock.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

#include <core/core.h>
#include <interfaces/IAppNotifications.h>

namespace L0Test {

class NotificationHandlerFake final : public WPEFramework::Exchange::IAppNotificationHandler {
public:
    explicit NotificationHandlerFake(bool statusResult = true,
                                     uint32_t handleRc = WPEFramework::Core::ERROR_NONE)
        : _refCount(1)
        , _statusResult(statusResult)
        , _handleRc(handleRc)
        , _releaseCount(0)
    {
    }

    ~NotificationHandlerFake() override = default;

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
        // Signal waiters whenever a Release brings us back to refcount == 1.
        // This happens when HandleNotifier() finishes calling internalNotifier->Release()
        // after HandleAppEventNotifier returns (whether success or error).
        if (newCount == 1) {
            _releaseCount.fetch_add(1, std::memory_order_release);
            _releaseCv.notify_all();
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
        const std::string& event,
        bool listen,
        bool& status) override
    {
        {
            std::lock_guard<std::mutex> lock(_observableMutex);
            lastEmitter = emitCb;
            lastEvent = event;
            lastListen = listen;
        }
        handleCount.fetch_add(1, std::memory_order_relaxed);
        status = _statusResult;
        return _handleRc;
    }

    // Test helpers
    void SetStatusResult(bool status) { _statusResult = status; }
    void SetHandleRc(uint32_t rc) { _handleRc = rc; }
    void Reset()
    {
        std::lock_guard<std::mutex> lock(_observableMutex);
        handleCount.store(0, std::memory_order_relaxed);
        lastEmitter = nullptr;
        lastEvent.clear();
        lastListen = false;
    }

    /**
     * WaitForHandlerRelease: block until the background thread has called
     * internalNotifier->Release() (i.e. refcount drops back to 1), or until
     * the timeout expires.  Call this after the worker pool has drained in tests
     * that use SetHandleRc(ERROR_GENERAL) or SetStatusResult(false) to guarantee
     * the background SubscriberJob has fully completed before service is destroyed.
     *
     * @param expectedReleases  How many Release-to-1 signals to wait for (total cumulative).
     * @param timeoutMs         Timeout in milliseconds (default 5000).
     * @return true if all expected releases were observed within the timeout.
     */
    bool WaitForHandlerRelease(uint32_t expectedReleases = 1,
                                uint32_t timeoutMs = 5000) const
    {
        std::unique_lock<std::mutex> lock(_releaseMutex);
        return _releaseCv.wait_for(lock,
            std::chrono::milliseconds(timeoutMs),
            [this, expectedReleases]() {
                return _releaseCount.load(std::memory_order_acquire) >= expectedReleases;
            });
    }

    /**
     * ResetReleaseCount: reset the cumulative release counter.
     * Call this just before starting the operations you want to synchronize on,
     * then call WaitForHandlerRelease(N) where N is the number of
     * HandleNotifier calls you expect.
     */
    void ResetReleaseCount()
    {
        _releaseCount.store(0, std::memory_order_release);
    }

    // Observable state: handleCount is atomic for safe lockless reads;
    // lastEmitter/lastEvent/lastListen are guarded by _observableMutex.
    std::atomic<uint32_t> handleCount{0};
    IEmitter* lastEmitter{nullptr};
    std::string lastEvent;
    bool lastListen{false};

private:
    mutable std::atomic<uint32_t> _refCount;
    bool _statusResult;
    uint32_t _handleRc;

    mutable std::mutex _observableMutex;
    mutable std::atomic<uint32_t> _releaseCount;
    mutable std::mutex _releaseMutex;
    mutable std::condition_variable _releaseCv;
};

} // namespace L0Test
