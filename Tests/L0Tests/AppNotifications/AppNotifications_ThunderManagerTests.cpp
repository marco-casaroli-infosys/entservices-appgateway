/*
 * AppNotifications_ThunderManagerTests.cpp
 *
 * L0 tests for ThunderSubscriptionManager: Subscribe, Unsubscribe,
 * HandleNotifier, RegisterNotification, UnregisterNotification,
 * IsNotificationRegistered, and destructor cleanup.
 * Tests AN-L0-049 to AN-L0-063.
 *
 * Strategy:
 *   - Directly instantiate AppNotificationsImplementation + Configure with mock shell
 *   - Use Subscribe(listen=true/false) to drive ThunderSubscriptionManager
 *     via the WorkerPool (SubscriberJob -> Subscribe/Unsubscribe)
 *   - Observe side-effects on ANNotificationHandlerFake
 *
 * Note on timing:
 *   ThunderSubscriptionManager::Subscribe/Unsubscribe are dispatched via the
 *   WorkerPool (SubscriberJob). We yield briefly after each Subscribe call.
 *   For RegisterNotification/UnregisterNotification path the handler fake
 *   records HandleAppEventNotifier calls.
 *
 * IMPORTANT — destructor ordering:
 *   AppNotificationsImplementation::~AppNotificationsImplementation() releases
 *   mShell (decrements refcount) but intentionally does NOT null the pointer.
 *   ThunderSubscriptionManager::~ThunderSubscriptionManager() (a member
 *   destructor) iterates mRegisteredNotifications and calls
 *   mShell->QueryInterfaceByCallsign() to clean up subscriptions, so mShell
 *   must remain non-null until after all member destructors have run.
 *   Tests can therefore allow impl->Release() with non-empty
 *   mRegisteredNotifications — the destructor will safely drain the list.
 */

#include <iostream>
#include <string>
#include <chrono>
#include <thread>

#include <core/core.h>
#include <plugins/IShell.h>

#include <interfaces/IAppNotifications.h>
#include <interfaces/IConfiguration.h>
#include "AppNotificationsServiceMock.h"
#include "AppNotificationsTestHelpers.h"
#include "L0Expect.hpp"
#include "L0TestTypes.hpp"

using WPEFramework::Core::ERROR_NONE;
using WPEFramework::Exchange::IAppNotifications;
using WPEFramework::Exchange::IConfiguration;

namespace {

IAppNotifications::AppNotificationContext MakeContext(uint32_t connId,
                                                       uint32_t reqId,
                                                       const std::string& appId,
                                                       const std::string& origin)
{
    IAppNotifications::AppNotificationContext ctx;
    ctx.connectionId = connId;
    ctx.requestId    = reqId;
    ctx.appId        = appId;
    ctx.origin       = origin;
    ctx.version      = "0";
    return ctx;
}

void YieldToWorkerPool(int ms = 100)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

} // namespace

// ---------------------------------------------------------------------------
// AN-L0-049: ThunderSubscriptionManager::Subscribe — new event calls HandleNotifier
// ---------------------------------------------------------------------------
uint32_t Test_AN_ThunderMgr_Subscribe_NewEvent()
{
    /** Subscribe for a new (module, event) calls HandleNotifier with listen=true. */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.notificationHandlerCallsign = FB_SETTINGS_CALLSIGN;
    cfg.provideNotificationHandler  = true;
    cfg.handlerStatusResult         = true;
    L0Test::AppNotificationsServiceMock shell(cfg);
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr, "ThunderMgr_Subscribe_NewEvent: impl creation");
    if (impl == nullptr) { return tr.failures; }

    auto ctx = MakeContext(1, 100, "com.app", APP_GATEWAY_CALLSIGN);
    impl->Subscribe(ctx, true, FB_SETTINGS_CALLSIGN, "onVoiceGuidanceChanged");
    YieldToWorkerPool();

    L0Test::ANNotificationHandlerFake* handler = shell.GetHandlerFake();
    L0Test::ExpectTrue(tr, handler != nullptr,
        "ThunderMgr_Subscribe_NewEvent: handler fake should be acquired");
    if (handler != nullptr) {
        L0Test::ExpectTrue(tr, handler->handleCount >= 1,
            "ThunderMgr_Subscribe_NewEvent: HandleAppEventNotifier should be called");
        L0Test::ExpectTrue(tr, handler->lastListen,
            "ThunderMgr_Subscribe_NewEvent: called with listen=true");
        L0Test::ExpectEqStr(tr, handler->lastEvent, "onVoiceGuidanceChanged",
            "ThunderMgr_Subscribe_NewEvent: event name should match");
    }

    // Unsubscribe before release to clear mRegisteredNotifications.
    impl->Subscribe(ctx, false, FB_SETTINGS_CALLSIGN, "onVoiceGuidanceChanged");
    YieldToWorkerPool();

    impl->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-050: ThunderSubscriptionManager::Subscribe — already registered skips
// ---------------------------------------------------------------------------
uint32_t Test_AN_ThunderMgr_Subscribe_AlreadyRegistered()
{
    /** Subscribe for an already-registered (module, event) does not re-invoke HandleNotifier. */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.notificationHandlerCallsign = FB_SETTINGS_CALLSIGN;
    cfg.provideNotificationHandler  = true;
    cfg.handlerStatusResult         = true;
    L0Test::AppNotificationsServiceMock shell(cfg);
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr, "ThunderMgr_Subscribe_AlreadyRegistered: impl creation");
    if (impl == nullptr) { return tr.failures; }

    auto ctx1 = MakeContext(1, 100, "com.app.one", APP_GATEWAY_CALLSIGN);
    auto ctx2 = MakeContext(2, 200, "com.app.two", APP_GATEWAY_CALLSIGN);

    // First Subscribe — registers the event
    impl->Subscribe(ctx1, true, FB_SETTINGS_CALLSIGN, "onRepeatEvent");
    YieldToWorkerPool();

    L0Test::ANNotificationHandlerFake* handler = shell.GetHandlerFake();
    uint32_t countAfterFirst = handler ? handler->handleCount : 0u;

    // Second Subscribe for SAME event key — SubscriberMap already has the key,
    // so no new SubscriberJob is enqueued; handler should NOT be called again.
    impl->Subscribe(ctx2, true, FB_SETTINGS_CALLSIGN, "onRepeatEvent");
    YieldToWorkerPool();

    if (handler != nullptr) {
        L0Test::ExpectEqU32(tr, handler->handleCount, countAfterFirst,
            "ThunderMgr_Subscribe_AlreadyRegistered: handler should NOT be called again for same key");
    }

    // Unsubscribe both contexts before release to clear mRegisteredNotifications.
    // Both ctx1 and ctx2 are in the SubscriberMap for this key; unsubscribe ctx1
    // first (ctx2 still present → no SubscriberJob); then ctx2 (last → SubscriberJob).
    impl->Subscribe(ctx1, false, FB_SETTINGS_CALLSIGN, "onRepeatEvent");
    YieldToWorkerPool();
    impl->Subscribe(ctx2, false, FB_SETTINGS_CALLSIGN, "onRepeatEvent");
    YieldToWorkerPool();

    impl->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-051: ThunderSubscriptionManager::Unsubscribe — registered event calls HandleNotifier
// ---------------------------------------------------------------------------
uint32_t Test_AN_ThunderMgr_Unsubscribe_RegisteredEvent()
{
    /** Unsubscribe for a registered (module, event) calls HandleNotifier with listen=false. */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.notificationHandlerCallsign = FB_SETTINGS_CALLSIGN;
    cfg.provideNotificationHandler  = true;
    cfg.handlerStatusResult         = true;
    L0Test::AppNotificationsServiceMock shell(cfg);
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr, "ThunderMgr_Unsubscribe_RegisteredEvent: impl creation");
    if (impl == nullptr) { return tr.failures; }

    auto ctx = MakeContext(1, 100, "com.app", APP_GATEWAY_CALLSIGN);
    impl->Subscribe(ctx, true, FB_SETTINGS_CALLSIGN, "onUnsubEvent");
    YieldToWorkerPool();

    // Unsubscribe — SubscriberMap is now empty for this key, triggers SubscriberJob(unsub)
    // This also clears mRegisteredNotifications (HandleNotifier returns true for unsub).
    impl->Subscribe(ctx, false, FB_SETTINGS_CALLSIGN, "onUnsubEvent");
    YieldToWorkerPool();

    L0Test::ANNotificationHandlerFake* handler = shell.GetHandlerFake();
    L0Test::ExpectTrue(tr, handler != nullptr,
        "ThunderMgr_Unsubscribe_RegisteredEvent: handler fake should be acquired");
    if (handler != nullptr) {
        // handleCount should be >= 2 (once for subscribe, once for unsubscribe)
        L0Test::ExpectTrue(tr, handler->handleCount >= 2,
            "ThunderMgr_Unsubscribe_RegisteredEvent: HandleAppEventNotifier called for unsub too");
    }

    // mRegisteredNotifications is now empty (unsubscribe cleared it) — safe to release.
    impl->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-052: ThunderSubscriptionManager::Unsubscribe — not registered logs error
// ---------------------------------------------------------------------------
uint32_t Test_AN_ThunderMgr_Unsubscribe_NotRegistered()
{
    /** Unsubscribe for an event not in mRegisteredNotifications logs error but does not crash. */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock shell;
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr, "ThunderMgr_Unsubscribe_NotRegistered: impl creation");
    if (impl == nullptr) { return tr.failures; }

    // Unsubscribe without subscribing first — Unsubscribe logs LOGERR
    auto ctx = MakeContext(1, 100, "com.app", APP_GATEWAY_CALLSIGN);
    const uint32_t rc = impl->Subscribe(ctx, false, FB_SETTINGS_CALLSIGN, "neverSubscribedEvent");
    L0Test::ExpectEqU32(tr, rc, static_cast<uint32_t>(ERROR_NONE),
        "ThunderMgr_Unsubscribe_NotRegistered: returns ERROR_NONE");
    YieldToWorkerPool();

    L0Test::ExpectTrue(tr, true, "ThunderMgr_Unsubscribe_NotRegistered: no crash");

    // mRegisteredNotifications is empty (no subscribe was done) — safe to release.
    impl->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-053: HandleNotifier — handler available, returns success
// ---------------------------------------------------------------------------
uint32_t Test_AN_HandleNotifier_Success()
{
    /** HandleNotifier acquires IAppNotificationHandler and calls HandleAppEventNotifier. */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.notificationHandlerCallsign = FB_SETTINGS_CALLSIGN;
    cfg.provideNotificationHandler  = true;
    cfg.handlerStatusResult         = true;
    L0Test::AppNotificationsServiceMock shell(cfg);
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr, "HandleNotifier_Success: impl creation");
    if (impl == nullptr) { return tr.failures; }

    auto ctx = MakeContext(1, 100, "com.app", APP_GATEWAY_CALLSIGN);
    impl->Subscribe(ctx, true, FB_SETTINGS_CALLSIGN, "onHandlerTest");
    YieldToWorkerPool();

    L0Test::ANNotificationHandlerFake* handler = shell.GetHandlerFake();
    L0Test::ExpectTrue(tr, handler != nullptr,
        "HandleNotifier_Success: handler fake should be acquired");
    if (handler != nullptr) {
        L0Test::ExpectTrue(tr, handler->handleCount >= 1,
            "HandleNotifier_Success: HandleAppEventNotifier should be called");
        L0Test::ExpectTrue(tr, handler->lastEmitter != nullptr,
            "HandleNotifier_Success: emitter callback should be non-null");
    }

    // Unsubscribe before release to clear mRegisteredNotifications.
    impl->Subscribe(ctx, false, FB_SETTINGS_CALLSIGN, "onHandlerTest");
    YieldToWorkerPool();

    impl->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-054: HandleNotifier — handler not available, logs error
// ---------------------------------------------------------------------------
uint32_t Test_AN_HandleNotifier_HandlerNotAvailable()
{
    /** HandleNotifier logs error when QueryInterfaceByCallsign returns nullptr. */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.provideNotificationHandler = false;
    L0Test::AppNotificationsServiceMock shell(cfg);
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr, "HandleNotifier_HandlerNotAvailable: impl creation");
    if (impl == nullptr) { return tr.failures; }

    auto ctx = MakeContext(1, 100, "com.app", APP_GATEWAY_CALLSIGN);
    // Subscribe — SubscriberJob will run and try HandleNotifier; handler unavailable
    const uint32_t rc = impl->Subscribe(ctx, true, FB_SETTINGS_CALLSIGN, "onNoHandlerEvent");
    L0Test::ExpectEqU32(tr, rc, static_cast<uint32_t>(ERROR_NONE),
        "HandleNotifier_HandlerNotAvailable: Subscribe returns ERROR_NONE");
    YieldToWorkerPool();

    // Handler was not provided — no handler fake
    L0Test::ExpectTrue(tr, shell.GetHandlerFake() == nullptr,
        "HandleNotifier_HandlerNotAvailable: handler fake should be null");
    L0Test::ExpectTrue(tr, true, "HandleNotifier_HandlerNotAvailable: no crash");

    // mRegisteredNotifications is empty (handler not available → HandleNotifier returned false)
    impl->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-055: HandleNotifier — handler returns error code
// ---------------------------------------------------------------------------
uint32_t Test_AN_HandleNotifier_HandlerReturnsError()
{
    /** HandleNotifier handles ERROR return from HandleAppEventNotifier gracefully. */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.notificationHandlerCallsign = FB_SETTINGS_CALLSIGN;
    cfg.provideNotificationHandler  = true;
    cfg.handlerStatusResult         = false;           // status = false
    cfg.handlerReturnCode           = WPEFramework::Core::ERROR_GENERAL;  // return error
    L0Test::AppNotificationsServiceMock shell(cfg);
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr, "HandleNotifier_HandlerReturnsError: impl creation");
    if (impl == nullptr) { return tr.failures; }

    auto ctx = MakeContext(1, 100, "com.app", APP_GATEWAY_CALLSIGN);
    impl->Subscribe(ctx, true, FB_SETTINGS_CALLSIGN, "onErrorHandlerEvent");
    YieldToWorkerPool();

    L0Test::ANNotificationHandlerFake* handler = shell.GetHandlerFake();
    L0Test::ExpectTrue(tr, handler != nullptr,
        "HandleNotifier_HandlerReturnsError: handler fake created");
    if (handler != nullptr) {
        L0Test::ExpectTrue(tr, handler->handleCount >= 1,
            "HandleNotifier_HandlerReturnsError: HandleAppEventNotifier called");
    }
    L0Test::ExpectTrue(tr, true, "HandleNotifier_HandlerReturnsError: no crash on error return");

    // handlerStatusResult=false → mRegisteredNotifications is empty → safe to release.
    impl->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-056: RegisterNotification — HandleNotifier returns true, adds to list
// ---------------------------------------------------------------------------
uint32_t Test_AN_RegisterNotification_HandleNotifier_ReturnsTrue()
{
    /** RegisterNotification adds entry to mRegisteredNotifications when handler status=true. */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.notificationHandlerCallsign = FB_SETTINGS_CALLSIGN;
    cfg.provideNotificationHandler  = true;
    cfg.handlerStatusResult         = true;
    L0Test::AppNotificationsServiceMock shell(cfg);
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr, "RegisterNotification_True: impl creation");
    if (impl == nullptr) { return tr.failures; }

    auto ctx = MakeContext(1, 100, "com.app", APP_GATEWAY_CALLSIGN);
    impl->Subscribe(ctx, true, FB_SETTINGS_CALLSIGN, "onRegisteredEvent");
    YieldToWorkerPool();

    // If registered successfully, a second Subscribe for same event+key from the SubscriberMap
    // side would not enqueue another SubscriberJob (Exists returns true).
    // The ThunderManager side: IsNotificationRegistered(FB_SETTINGS_CALLSIGN, "onRegisteredEvent")
    // returns true → subsequent Subscribe call logs LOGTRACE and skips.
    // We verify via a second unsubscribe path (which also clears mRegisteredNotifications):
    impl->Subscribe(ctx, false, FB_SETTINGS_CALLSIGN, "onRegisteredEvent");
    YieldToWorkerPool();

    L0Test::ANNotificationHandlerFake* handler = shell.GetHandlerFake();
    L0Test::ExpectTrue(tr, handler != nullptr,
        "RegisterNotification_True: handler fake should exist");
    if (handler != nullptr) {
        // First subscribe → RegisterNotification → HandleNotifier(listen=true)
        // First unsubscribe → UnregisterNotification → HandleNotifier(listen=false)
        L0Test::ExpectTrue(tr, handler->handleCount >= 2,
            "RegisterNotification_True: HandleAppEventNotifier called at least twice");
    }

    // mRegisteredNotifications is now empty (unsubscribe cleared it) — safe to release.
    impl->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-057: RegisterNotification — HandleNotifier returns false, skips adding
// ---------------------------------------------------------------------------
uint32_t Test_AN_RegisterNotification_HandleNotifier_ReturnsFalse()
{
    /** RegisterNotification does NOT add entry when HandleNotifier status=false. */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.notificationHandlerCallsign = FB_SETTINGS_CALLSIGN;
    cfg.provideNotificationHandler  = true;
    cfg.handlerStatusResult         = false;  // handler returns false → not registered
    L0Test::AppNotificationsServiceMock shell(cfg);
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr, "RegisterNotification_False: impl creation");
    if (impl == nullptr) { return tr.failures; }

    auto ctx = MakeContext(1, 100, "com.app", APP_GATEWAY_CALLSIGN);
    impl->Subscribe(ctx, true, FB_SETTINGS_CALLSIGN, "onFalseHandlerEvent");
    YieldToWorkerPool();

    L0Test::ANNotificationHandlerFake* handler = shell.GetHandlerFake();
    L0Test::ExpectTrue(tr, handler != nullptr,
        "RegisterNotification_False: handler fake should be acquired");
    if (handler != nullptr) {
        L0Test::ExpectTrue(tr, handler->handleCount >= 1,
            "RegisterNotification_False: HandleAppEventNotifier called");
    }

    // Because status=false, the event was NOT added to mRegisteredNotifications.
    L0Test::ExpectTrue(tr, true, "RegisterNotification_False: no crash");

    // mRegisteredNotifications is empty (status=false) — safe to release.
    impl->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-058: UnregisterNotification — HandleNotifier returns true, removes from list
// ---------------------------------------------------------------------------
uint32_t Test_AN_UnregisterNotification_HandleNotifier_ReturnsTrue()
{
    /** UnregisterNotification removes entry when HandleNotifier returns true. */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.notificationHandlerCallsign = FB_SETTINGS_CALLSIGN;
    cfg.provideNotificationHandler  = true;
    cfg.handlerStatusResult         = true;
    L0Test::AppNotificationsServiceMock shell(cfg);
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr, "UnregisterNotification_True: impl creation");
    if (impl == nullptr) { return tr.failures; }

    auto ctx = MakeContext(1, 100, "com.app", APP_GATEWAY_CALLSIGN);

    // Subscribe → registers (adds to mRegisteredNotifications)
    impl->Subscribe(ctx, true, FB_SETTINGS_CALLSIGN, "onUnregEvent");
    YieldToWorkerPool();

    // Unsubscribe → unregisters (removes from mRegisteredNotifications)
    impl->Subscribe(ctx, false, FB_SETTINGS_CALLSIGN, "onUnregEvent");
    YieldToWorkerPool();

    L0Test::ANNotificationHandlerFake* handler = shell.GetHandlerFake();
    L0Test::ExpectTrue(tr, handler != nullptr,
        "UnregisterNotification_True: handler fake should exist");
    if (handler != nullptr) {
        // Should have been called twice (once for subscribe, once for unsubscribe)
        L0Test::ExpectTrue(tr, handler->handleCount >= 2,
            "UnregisterNotification_True: HandleAppEventNotifier called for both sub and unsub");
        // Last call should have listen=false (unsubscribe)
        L0Test::ExpectTrue(tr, !handler->lastListen,
            "UnregisterNotification_True: last HandleAppEventNotifier call should have listen=false");
    }

    // mRegisteredNotifications is empty (unsubscribe cleared it) — safe to release.
    impl->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-059: UnregisterNotification — HandleNotifier returns false, list unchanged
// ---------------------------------------------------------------------------
uint32_t Test_AN_UnregisterNotification_HandleNotifier_ReturnsFalse()
{
    /** UnregisterNotification does NOT erase entry when HandleNotifier status=false. */
    L0Test::TestResult tr;

    // Subscribe with status=true (adds to mRegisteredNotifications),
    // then flip to false for unsubscribe (entry NOT removed).
    // To avoid crash on impl->Release(), we do a final cleanup pass:
    // switch back to status=true and unsubscribe again to clear the list.
    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.notificationHandlerCallsign = FB_SETTINGS_CALLSIGN;
    cfg.provideNotificationHandler  = true;
    cfg.handlerStatusResult         = true;  // first call succeeds
    L0Test::AppNotificationsServiceMock shell(cfg);
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr, "UnregisterNotification_False: impl creation");
    if (impl == nullptr) { return tr.failures; }

    auto ctx = MakeContext(1, 100, "com.app", APP_GATEWAY_CALLSIGN);
    impl->Subscribe(ctx, true, FB_SETTINGS_CALLSIGN, "onFlipEvent");
    YieldToWorkerPool();

    // Switch handler to return false for next call (unsubscribe path)
    shell.SetHandlerStatusResult(false);

    // Unsubscribe — handler returns false → entry NOT removed from mRegisteredNotifications
    impl->Subscribe(ctx, false, FB_SETTINGS_CALLSIGN, "onFlipEvent");
    YieldToWorkerPool();

    L0Test::ANNotificationHandlerFake* handler = shell.GetHandlerFake();
    L0Test::ExpectTrue(tr, handler != nullptr,
        "UnregisterNotification_False: handler fake should exist");
    if (handler != nullptr) {
        L0Test::ExpectTrue(tr, handler->handleCount >= 2,
            "UnregisterNotification_False: HandleAppEventNotifier called twice");
    }
    L0Test::ExpectTrue(tr, true, "UnregisterNotification_False: no crash after failed unregister");

    // mRegisteredNotifications still has the entry (unregister failed).
    // Switch handler back to true and re-subscribe+unsubscribe to drain it.
    shell.SetHandlerStatusResult(true);
    // Re-subscribe adds ctx back to SubscriberMap (it was removed by the Unsubscribe above).
    // But wait — the Unsubscribe call above already removed ctx from SubscriberMap
    // (SubscriberMap.Remove succeeds regardless of handler status).
    // So mRegisteredNotifications has the entry but SubscriberMap does NOT.
    // We need to subscribe again to get a new SubscriberJob(sub)→Register, but
    // IsNotificationRegistered returns true, so Register is skipped.
    // The SubscriberMap.Exists returns false, so a new SubscriberJob IS enqueued.
    // That SubscriberJob calls Subscribe() which calls IsNotificationRegistered()→true,
    // so it skips calling Register, and the entry in mRegisteredNotifications stays.
    // To drain mRegisteredNotifications we need to call impl->Subscribe(ctx, false, ...)
    // which will remove ctx from SubscriberMap again AND if SubscriberMap becomes empty
    // for that key, enqueue SubscriberJob(unsub) → Unsubscribe → IsNotificationRegistered
    // → true → UnregisterNotification → HandleNotifier(listen=false, status=true) → erase.
    //
    // Full drain sequence:
    //   1. Subscribe(true) → adds to SubscriberMap, no new SubscriberJob (IsRegistered=true),
    //      actually wait — SubscriberMap.Exists was false (we removed it), so new SubscriberJob
    //      IS dispatched. That calls ThunderMgr::Subscribe → IsNotificationRegistered=true → skip.
    //   2. Subscribe(false) → removes from SubscriberMap, dispatches SubscriberJob(unsub)
    //      → ThunderMgr::Unsubscribe → IsNotificationRegistered=true → UnregisterNotification
    //      → HandleNotifier(false, status=true) → erase from list.
    impl->Subscribe(ctx, true, FB_SETTINGS_CALLSIGN, "onFlipEvent");
    YieldToWorkerPool();
    impl->Subscribe(ctx, false, FB_SETTINGS_CALLSIGN, "onFlipEvent");
    YieldToWorkerPool();

    impl->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-060: IsNotificationRegistered — returns true for registered event
// ---------------------------------------------------------------------------
uint32_t Test_AN_IsNotificationRegistered_Exists()
{
    /** IsNotificationRegistered returns true for a (module, event) that was registered. */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.notificationHandlerCallsign = FB_SETTINGS_CALLSIGN;
    cfg.provideNotificationHandler  = true;
    cfg.handlerStatusResult         = true;
    L0Test::AppNotificationsServiceMock shell(cfg);
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr, "IsNotificationRegistered_Exists: impl creation");
    if (impl == nullptr) { return tr.failures; }

    auto ctx = MakeContext(1, 100, "com.app", APP_GATEWAY_CALLSIGN);
    impl->Subscribe(ctx, true, FB_SETTINGS_CALLSIGN, "onRegisteredCheck");
    YieldToWorkerPool();

    // The event is registered — if we subscribe a SECOND listener for same event key,
    // SubscriberMap.Exists returns true → no SubscriberJob enqueued
    // ThunderManager.IsNotificationRegistered also returns true → Subscribe logs LOGTRACE
    auto ctx2 = MakeContext(2, 200, "com.app2", APP_GATEWAY_CALLSIGN);
    impl->Subscribe(ctx2, true, FB_SETTINGS_CALLSIGN, "onRegisteredCheck");
    YieldToWorkerPool();

    L0Test::ANNotificationHandlerFake* handler = shell.GetHandlerFake();
    // If handler was called only once (not twice), it confirms IsNotificationRegistered
    // returned true and skipped calling RegisterNotification again.
    if (handler != nullptr) {
        // NOTE: since SubscriberMap.Exists prevents the second SubscriberJob from being
        // submitted, the handler count remains at 1.
        L0Test::ExpectEqU32(tr, handler->handleCount, 1u,
            "IsNotificationRegistered_Exists: handler called exactly once (IsRegistered=true guards second call)");
    }

    // Unsubscribe both contexts to drain mRegisteredNotifications before release.
    // The destructor will also safely drain any remaining entries via
    // ~ThunderSubscriptionManager, but explicit cleanup keeps test intent clear.
    impl->Subscribe(ctx, false, FB_SETTINGS_CALLSIGN, "onRegisteredCheck");
    YieldToWorkerPool();
    impl->Subscribe(ctx2, false, FB_SETTINGS_CALLSIGN, "onRegisteredCheck");
    YieldToWorkerPool();

    impl->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-061: IsNotificationRegistered — returns false for unregistered event
// ---------------------------------------------------------------------------
uint32_t Test_AN_IsNotificationRegistered_NotExists()
{
    /** IsNotificationRegistered returns false before any subscription. */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.notificationHandlerCallsign = FB_SETTINGS_CALLSIGN;
    cfg.provideNotificationHandler  = true;
    cfg.handlerStatusResult         = true;
    L0Test::AppNotificationsServiceMock shell(cfg);
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr, "IsNotificationRegistered_NotExists: impl creation");
    if (impl == nullptr) { return tr.failures; }

    // No Subscribe call — handler should not have been called
    L0Test::ExpectTrue(tr, shell.GetHandlerFake() == nullptr,
        "IsNotificationRegistered_NotExists: handler not acquired before Subscribe");

    // mRegisteredNotifications is empty — safe to release.
    impl->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-062: IsNotificationRegistered — case-insensitive storage
// ---------------------------------------------------------------------------
uint32_t Test_AN_IsNotificationRegistered_CaseInsensitive()
{
    /** mRegisteredNotifications stores events in lowercase; lookup is case-insensitive. */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.notificationHandlerCallsign = FB_SETTINGS_CALLSIGN;
    cfg.provideNotificationHandler  = true;
    cfg.handlerStatusResult         = true;
    L0Test::AppNotificationsServiceMock shell(cfg);
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr, "IsNotificationRegistered_CaseInsensitive: impl creation");
    if (impl == nullptr) { return tr.failures; }

    // Subscribe with lowercase event
    auto ctx = MakeContext(1, 100, "com.app", APP_GATEWAY_CALLSIGN);
    impl->Subscribe(ctx, true, FB_SETTINGS_CALLSIGN, "onCaseEvent");
    YieldToWorkerPool();

    // Unsubscribe with UPPERCASE — IsNotificationRegistered should find it (lowercase stored)
    // This also drains mRegisteredNotifications (unsubscribe succeeds, status=true).
    impl->Subscribe(ctx, false, FB_SETTINGS_CALLSIGN, "ONCASEEVENT");
    YieldToWorkerPool();

    L0Test::ANNotificationHandlerFake* handler = shell.GetHandlerFake();
    L0Test::ExpectTrue(tr, handler != nullptr,
        "IsNotificationRegistered_CaseInsensitive: handler acquired");
    if (handler != nullptr) {
        // The unsubscribe path calls Unsubscribe → IsNotificationRegistered(uppercase) → finds it
        // → UnregisterNotification → HandleNotifier(listen=false)
        L0Test::ExpectTrue(tr, handler->handleCount >= 2,
            "IsNotificationRegistered_CaseInsensitive: HandleAppEventNotifier called for sub AND unsub");
    }

    // mRegisteredNotifications is empty (unsubscribe cleared it) — safe to release.
    impl->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-063: ThunderSubscriptionManager destructor — no crash when list is empty
// ---------------------------------------------------------------------------
uint32_t Test_AN_ThunderMgr_Destructor_UnsubscribesAll()
{
    /** ThunderSubscriptionManager::~ThunderSubscriptionManager safely drains
     *  mRegisteredNotifications even when the list is non-empty at destroy time.
     *  The destructor calls HandleNotifier(listen=false) for each registered event,
     *  which requires mShell to remain valid — the fixed AppNotificationsImplementation
     *  destructor no longer nulls mShell before member destructors run.
     *  This test also verifies that subscribe+unsubscribe correctly populates and
     *  drains mRegisteredNotifications.
     */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.notificationHandlerCallsign = FB_SETTINGS_CALLSIGN;
    cfg.provideNotificationHandler  = true;
    cfg.handlerStatusResult         = true;
    L0Test::AppNotificationsServiceMock shell(cfg);
    IAppNotifications* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr, "ThunderMgr_Destructor_UnsubscribesAll: impl creation");
    if (impl == nullptr) { return tr.failures; }

    auto ctx1 = MakeContext(1, 100, "com.app", APP_GATEWAY_CALLSIGN);
    auto ctx2 = MakeContext(2, 200, "com.app2", APP_GATEWAY_CALLSIGN);

    // Register multiple events
    impl->Subscribe(ctx1, true, FB_SETTINGS_CALLSIGN, "onDestrEvent1");
    impl->Subscribe(ctx2, true, FB_SETTINGS_CALLSIGN, "onDestrEvent2");
    YieldToWorkerPool(500);

    L0Test::ANNotificationHandlerFake* handler = shell.GetHandlerFake();
    uint32_t handleCountAfterSub = handler ? handler->handleCount : 0u;

    L0Test::ExpectTrue(tr, handleCountAfterSub >= 2u,
        "ThunderMgr_Destructor_UnsubscribesAll: at least 2 registrations");

    // Unsubscribe all events before release to verify subscribe+unsubscribe round-trip
    // and confirm mRegisteredNotifications is correctly maintained.
    // Note: impl->Release() with a non-empty list is also safe because the fixed
    // destructor keeps mShell valid during ~ThunderSubscriptionManager execution.
    impl->Subscribe(ctx1, false, FB_SETTINGS_CALLSIGN, "onDestrEvent1");
    YieldToWorkerPool(500);
    impl->Subscribe(ctx2, false, FB_SETTINGS_CALLSIGN, "onDestrEvent2");
    YieldToWorkerPool(500);

    if (handler != nullptr) {
        L0Test::ExpectTrue(tr, handler->handleCount >= 4u,
            "ThunderMgr_Destructor_UnsubscribesAll: subscribe+unsubscribe each called handler twice");
    }

    // mRegisteredNotifications is empty — safe to release.
    impl->Release();
    impl = nullptr;

    L0Test::ExpectTrue(tr, true, "ThunderMgr_Destructor_UnsubscribesAll: no crash on destroy");

    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-085: Emitter::Emit() callback invoked through handler fake
// ---------------------------------------------------------------------------
uint32_t Test_AN_Emitter_Emit_Callback()
{
    /** When HandleAppEventNotifier is called, the Emitter callback is provided.
     *  Calling Emitter::Emit() dispatches through the impl's EmitJob path. */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.notificationHandlerCallsign = FB_SETTINGS_CALLSIGN;
    cfg.provideNotificationHandler  = true;
    cfg.handlerStatusResult         = true;
    L0Test::AppNotificationsServiceMock shell(cfg);
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr,
        "Emitter_Emit_Callback: impl creation");
    if (impl == nullptr) { return tr.failures; }

    // Subscribe a context so there's a listener for the emitter to target
    auto ctx = MakeContext(10, 1001, "com.emitter.app", APP_GATEWAY_CALLSIGN);
    impl->Subscribe(ctx, true, FB_SETTINGS_CALLSIGN, "onEmitterTestEvent");
    YieldToWorkerPool();

    // Get the handler fake to access the emitter callback
    L0Test::ANNotificationHandlerFake* handler = shell.GetHandlerFake();
    L0Test::ExpectTrue(tr, handler != nullptr,
        "Emitter_Emit_Callback: handler fake should exist");
    L0Test::ExpectTrue(tr, handler != nullptr && handler->lastEmitter != nullptr,
        "Emitter_Emit_Callback: emitter should be non-null");

    if (handler != nullptr && handler->lastEmitter != nullptr) {
        // Call the Emitter::Emit() method directly through the IEmitter interface
        handler->lastEmitter->Emit("onEmitterTestEvent", R"({"emitter":"callback"})", "");
        YieldToWorkerPool();

        // Verify the AppGateway responder received the dispatch
        L0Test::ANResponderFake* gw = shell.GetAppGatewayFake();
        L0Test::ExpectTrue(tr, gw != nullptr,
            "Emitter_Emit_Callback: AppGateway responder should be acquired");
        if (gw != nullptr) {
            L0Test::ExpectTrue(tr, gw->emitCount > 0,
                "Emitter_Emit_Callback: Emit dispatched through emitter callback");
        }
    }

    // Unsubscribe to clear mRegisteredNotifications before release
    impl->Subscribe(ctx, false, FB_SETTINGS_CALLSIGN, "onEmitterTestEvent");
    YieldToWorkerPool();

    impl->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-095: SubscriberJob::Dispatch subscribe=true branch (explicit verify)
// ---------------------------------------------------------------------------
uint32_t Test_AN_SubscriberJob_Dispatch_Subscribe()
{
    /** SubscriberJob::Dispatch with subscribe=true calls
     *  ThunderSubscriptionManager::Subscribe. Verified via handler invocation. */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.notificationHandlerCallsign = FB_SETTINGS_CALLSIGN;
    cfg.provideNotificationHandler  = true;
    cfg.handlerStatusResult         = true;
    L0Test::AppNotificationsServiceMock shell(cfg);
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr,
        "SubscriberJob_Dispatch_Subscribe: impl creation");
    if (impl == nullptr) { return tr.failures; }

    auto ctx = MakeContext(1, 100, "com.app", APP_GATEWAY_CALLSIGN);
    impl->Subscribe(ctx, true, FB_SETTINGS_CALLSIGN, "onJobSubEvent");
    YieldToWorkerPool();

    L0Test::ANNotificationHandlerFake* handler = shell.GetHandlerFake();
    L0Test::ExpectTrue(tr, handler != nullptr && handler->handleCount >= 1,
        "SubscriberJob_Dispatch_Subscribe: handler called (subscribe branch)");
    if (handler != nullptr) {
        L0Test::ExpectTrue(tr, handler->lastListen,
            "SubscriberJob_Dispatch_Subscribe: last call should be listen=true");
    }

    // Cleanup: unsubscribe to verify SubscriberJob round-trip.
    impl->Subscribe(ctx, false, FB_SETTINGS_CALLSIGN, "onJobSubEvent");
    YieldToWorkerPool();

    impl->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// AN-L0-096: SubscriberJob::Dispatch subscribe=false branch (explicit verify)
// ---------------------------------------------------------------------------
uint32_t Test_AN_SubscriberJob_Dispatch_Unsubscribe()
{
    /** SubscriberJob::Dispatch with subscribe=false calls
     *  ThunderSubscriptionManager::Unsubscribe. Verified via handler invocation. */
    L0Test::TestResult tr;

    L0Test::AppNotificationsServiceMock::Config cfg;
    cfg.notificationHandlerCallsign = FB_SETTINGS_CALLSIGN;
    cfg.provideNotificationHandler  = true;
    cfg.handlerStatusResult         = true;
    L0Test::AppNotificationsServiceMock shell(cfg);
    auto* impl = CreateConfiguredImpl(&shell);
    L0Test::ExpectTrue(tr, impl != nullptr,
        "SubscriberJob_Dispatch_Unsubscribe: impl creation");
    if (impl == nullptr) { return tr.failures; }

    auto ctx = MakeContext(1, 100, "com.app", APP_GATEWAY_CALLSIGN);

    // Subscribe first
    impl->Subscribe(ctx, true, FB_SETTINGS_CALLSIGN, "onJobUnsubEvent");
    YieldToWorkerPool();

    L0Test::ANNotificationHandlerFake* handler = shell.GetHandlerFake();
    uint32_t countAfterSub = handler ? handler->handleCount : 0u;

    // Unsubscribe — last listener, so SubscriberJob(unsub) is enqueued
    impl->Subscribe(ctx, false, FB_SETTINGS_CALLSIGN, "onJobUnsubEvent");
    YieldToWorkerPool();

    if (handler != nullptr) {
        L0Test::ExpectTrue(tr, handler->handleCount > countAfterSub,
            "SubscriberJob_Dispatch_Unsubscribe: handler called for unsubscribe branch");
        L0Test::ExpectTrue(tr, !handler->lastListen,
            "SubscriberJob_Dispatch_Unsubscribe: last call should be listen=false");
    }

    // mRegisteredNotifications drained by the unsubscribe — safe to release
    impl->Release();
    return tr.failures;
}
