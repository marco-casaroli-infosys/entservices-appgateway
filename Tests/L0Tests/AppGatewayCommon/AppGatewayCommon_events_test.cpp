#include "AppGatewayCommon_common_test.h"

// TEST_ID: AGC_L0_086
// CheckPermissionGroup always returns allowed=true (no permission groups defined yet).
uint32_t Test_CheckPermissionGroup_DefaultAllowed()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayAuthenticator> auth(ps.plugin);
    bool allowed = false;
    const uint32_t rc = auth->CheckPermissionGroup("com.test.app", "someGroup", allowed);
    ExpectEqU32(tr, rc, ERROR_NONE, "CheckPermissionGroup returns ERROR_NONE");
    ExpectTrue(tr, allowed, "CheckPermissionGroup defaults to allowed=true");

    return tr.failures;
}

// TEST_ID: AGC_L0_087
// Authenticate delegates to LifecycleDelegate via InvokeLifecycleDelegate.
// In L0 mDelegate and lifecycleDelegate are both non-null (created in Initialize),
// so LifecycleDelegate::Authenticate is reached.  The session ID is not in the
// AppIdInstanceIdMap, so GetAppId returns empty → ERROR_GENERAL.
uint32_t Test_Authenticate_DelegateUnavailable()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayAuthenticator> auth(ps.plugin);
    std::string appId;
    const uint32_t rc = auth->Authenticate("test-session-id", appId);
    ExpectEqU32(tr, rc, ERROR_GENERAL, "Authenticate returns ERROR_GENERAL in L0 (session not in map)");

    return tr.failures;
}

// TEST_ID: AGC_L0_088
// GetSessionId delegates to LifecycleDelegate via InvokeLifecycleDelegate.
// In L0 the delegate chain is non-null, so LifecycleDelegate::GetSessionId is reached.
// The appId is not in the map → GetAppInstanceId returns empty → ERROR_GENERAL.
uint32_t Test_GetSessionId_DelegateUnavailable()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayAuthenticator> auth(ps.plugin);
    std::string sessionId;
    const uint32_t rc = auth->GetSessionId("com.test.app", sessionId);
    ExpectEqU32(tr, rc, ERROR_GENERAL, "GetSessionId returns ERROR_GENERAL in L0 (appId not in map)");

    return tr.failures;
}

// TEST_ID: AGC_L0_089
// HandleAppEventNotifier with null cb → ERROR_GENERAL, status==false
uint32_t Test_HandleAppEventNotifier_NullCb()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    bool status = true;
    const uint32_t rc = notif->HandleAppEventNotifier(nullptr, "lifecycle.onbackground", true, status);
    ExpectEqU32(tr, rc, ERROR_GENERAL, "HandleAppEventNotifier with null cb returns ERROR_GENERAL");
    ExpectTrue(tr, false == status, "HandleAppEventNotifier with null cb sets status false");
    return tr.failures;
}

// TEST_ID: AGC_L0_090
// HandleAppEventNotifier with valid emitter → ERROR_NONE, status==true; exercise listen and unlisten
uint32_t Test_HandleAppEventNotifier_ValidCb()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter(); // refcount=1

    // listen=true
    bool status1 = false;
    const uint32_t rc1 = notif->HandleAppEventNotifier(emitter, "lifecycle.onbackground", true, status1);
    ExpectEqU32(tr, rc1, ERROR_NONE, "HandleAppEventNotifier listen=true returns ERROR_NONE");
    ExpectTrue(tr, status1, "HandleAppEventNotifier listen=true sets status true");

    // listen=false (unlisten)
    bool status2 = false;
    const uint32_t rc2 = notif->HandleAppEventNotifier(emitter, "lifecycle.onbackground", false, status2);
    ExpectEqU32(tr, rc2, ERROR_NONE, "HandleAppEventNotifier listen=false returns ERROR_NONE");
    ExpectTrue(tr, status2, "HandleAppEventNotifier listen=false sets status true");

    // Best-effort drain: give the WorkerPool time to dispatch the two async
    // EventRegistrationJobs before teardown, preventing use-after-free.
    // The jobs are trivial (single delegate call), so 100ms is generous.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    emitter->Release(); // drop our initial ref
    return tr.failures;
}

// TEST_ID: AGC_L0_091
// HandleAppEventNotifier before Initialize → SafeSubmitEventRegistrationJob hits null mDelegate → ERROR_GENERAL
// Covers the mDelegate null-check branch inside SafeSubmitEventRegistrationJob (lines 481-483).
uint32_t Test_HandleAppEventNotifier_BeforeInit()
{
    TestResult tr;
    PluginAndService ps;
    // Deliberately do NOT call Initialize — mDelegate remains nullptr
    auto* handler = ps.plugin->QueryInterface<Exchange::IAppNotificationHandler>();
    ExpectTrue(tr, nullptr != handler, "QueryInterface<IAppNotificationHandler> returns non-null");
    if (nullptr != handler) {
        auto* emitter = new StubEmitter();
        bool status = true;
        const uint32_t rc = handler->HandleAppEventNotifier(emitter, "lifecycle.onbackground", true, status);
        ExpectEqU32(tr, rc, ERROR_GENERAL, "HandleAppEventNotifier before Init returns ERROR_GENERAL");
        ExpectTrue(tr, false == status, "status is false when delegate is null");
        emitter->Release();
        handler->Release();
    }
    return tr.failures;
}

// ============================================================================
// Gap 1–7 tests — delegate event subscription contract paths
// ============================================================================
// These tests exercise previously untested branches in the SettingsDelegate
// event routing and individual delegate HandleEvent/HandleSubscription methods.
// All tests use the async EventRegistrationJob pattern: HandleAppEventNotifier
// submits a job to the WorkerPool which calls SettingsDelegate::HandleAppEventNotifier.
// The API-level return is always ERROR_NONE/status=true when cb and mDelegate are
// non-null; the coverage gain is from the async dispatch exercising delegate code.

// TEST_ID: AGC_L0_092
// Gap 1: HandleAppEventNotifier with a completely unrecognized event name.
// → SettingsDelegate iterates all 5 delegates (userSettings, systemDelegate,
//   networkDelegate, lifecycleDelegate, ttsDelegate); none matches.
// → handled=false → LOGERR("No Matching registrations")
// → registrationError=true → LOGERR("Error in registering/unregistering...")
// Coverage: SettingsDelegate::HandleAppEventNotifier !handled and registrationError branches.
uint32_t Test_HandleAppEventNotifier_UnrecognizedEvent()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter();

    bool status = false;
    const uint32_t rc = notif->HandleAppEventNotifier(emitter, "completely.unknown.event", true, status);
    ExpectEqU32(tr, rc, ERROR_NONE, "Gap1: unknown event returns ERROR_NONE (job submitted)");
    ExpectTrue(tr, status, "Gap1: status is true (async job submitted successfully)");

    // Wait for async EventRegistrationJob to dispatch and exercise the delegate routing.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    emitter->Release();
    return tr.failures;
}

// TEST_ID: AGC_L0_093
// Gap 2: NetworkDelegate::HandleEvent + HandleSubscription(listen=true) with null interface.
// Event "network.onconnectedchanged" is in VALID_NETWORK_EVENT.
// → SettingsDelegate iterates: userSettings(false) → systemDelegate(false)
//   → networkDelegate::HandleEvent(true) → HandleSubscription(listen=true)
//   → GetNetworkManagerInterface() returns nullptr (L0)
//   → returns false → registrationError = !false = true, handled=true
// Coverage: NetworkDelegate::HandleEvent, HandleSubscription(listen=true),
//   GetNetworkManagerInterface null-guard.
uint32_t Test_HandleAppEventNotifier_NetworkEvent_ListenTrue()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter();

    bool status = false;
    const uint32_t rc = notif->HandleAppEventNotifier(emitter, "network.onconnectedchanged", true, status);
    ExpectEqU32(tr, rc, ERROR_NONE, "Gap2: network event listen=true returns ERROR_NONE (job submitted)");
    ExpectTrue(tr, status, "Gap2: status is true (async job submitted successfully)");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    emitter->Release();
    return tr.failures;
}

// TEST_ID: AGC_L0_094
// Gap 3: UserSettingsDelegate::HandleEvent + HandleSubscription(listen=true) with null interface.
// Event "localization.onlanguagechanged" is in VALID_USER_SETTINGS_EVENT.
// → SettingsDelegate iterates: userSettings::HandleEvent(true)
//   → HandleSubscription(listen=true) → GetUserSettingsInterface() returns nullptr (L0)
//   → returns false → registrationError = !false = true, handled=true
// Coverage: UserSettingsDelegate::HandleEvent, HandleSubscription(listen=true),
//   GetUserSettingsInterface null-guard.
uint32_t Test_HandleAppEventNotifier_UserSettingsEvent_ListenTrue()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter();

    bool status = false;
    const uint32_t rc = notif->HandleAppEventNotifier(emitter, "localization.onlanguagechanged", true, status);
    ExpectEqU32(tr, rc, ERROR_NONE, "Gap3: usersettings event listen=true returns ERROR_NONE (job submitted)");
    ExpectTrue(tr, status, "Gap3: status is true (async job submitted successfully)");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    emitter->Release();
    return tr.failures;
}

// TEST_ID: AGC_L0_095
// Gap 4: TTSDelegate::HandleEvent + HandleSubscription + Register() + GetTTS() null path.
// Event "TextToSpeech.onVoiceChanged" matches the "TextToSpeech." prefix.
// → SettingsDelegate iterates: userSettings(false) → systemDelegate(false)
//   → networkDelegate(false) → lifecycleDelegate(false) [Gap 6 coverage]
//   → ttsDelegate::HandleEvent(true) → HandleSubscription(listen=true)
//   → Register() → GetTTS() → QueryInterfaceByCallsign returns nullptr (L0)
//   → Register returns false → HandleSubscription returns false
//   → registrationError = !false = true, handled=true
// Coverage: TTSDelegate::HandleEvent, HandleSubscription(listen=true), Register(),
//   GetTTS() null-guard. Also transitively covers Gap 6 (LifecycleDelegate not-found branch).
uint32_t Test_HandleAppEventNotifier_TTSEvent_ListenTrue()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter();

    bool status = false;
    const uint32_t rc = notif->HandleAppEventNotifier(emitter, "TextToSpeech.onVoiceChanged", true, status);
    ExpectEqU32(tr, rc, ERROR_NONE, "Gap4: TTS event listen=true returns ERROR_NONE (job submitted)");
    ExpectTrue(tr, status, "Gap4: status is true (async job submitted successfully)");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    emitter->Release();
    return tr.failures;
}

// TEST_ID: AGC_L0_096
// Gap 5: SystemDelegate::HandleEvent success path for recognized device events.
// Event "device.onvideoresolutionchanged" matches SystemDelegate's event set.
// → SettingsDelegate iterates: userSettings(false) → systemDelegate::HandleEvent(true)
//   → listen=true → AddNotification(event, cb) → SetupDisplaySettingsSubscription() etc.
//   → registrationError=false, returns true → handled=true
// This is the ONLY delegate subscription that succeeds at L0.
// NOTE: Only listen=true is tested here. listen=false races on _displayRpc
// (unprotected shared_ptr) when both jobs run concurrently on the worker pool.
// The unsubscribe path is covered in L1 (AGC_L1_146) where mocks prevent blocking.
// Coverage: SystemDelegate::HandleEvent success path and AddNotification.
uint32_t Test_HandleAppEventNotifier_SystemDeviceEvent()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter();

    // listen=true → AddNotification for the event
    bool status1 = false;
    const uint32_t rc1 = notif->HandleAppEventNotifier(emitter, "device.onvideoresolutionchanged", true, status1);
    ExpectEqU32(tr, rc1, ERROR_NONE, "Gap5: system device event listen=true returns ERROR_NONE");
    ExpectTrue(tr, status1, "Gap5: status is true (listen job submitted)");

    // No sleep needed — the job drain in Deinitialize() waits for completion.

    emitter->Release();
    return tr.failures;
}

// TEST_ID: AGC_L0_097
// Gap 7: NetworkDelegate::HandleSubscription(listen=false) without prior listen=true.
// Event "device.onnetworkchanged" is in VALID_NETWORK_EVENT.
// → SettingsDelegate iterates: userSettings(false) → systemDelegate(false)
//   → networkDelegate::HandleEvent(true) → HandleSubscription(listen=false)
//   → RemoveNotification(event, cb) is a no-op (nothing registered)
//   → returns true → registrationError = !true = false, handled=true
// Confirms that unsubscribing from a network event that was never subscribed is safe.
// Coverage: NetworkDelegate::HandleSubscription(listen=false) branch.
uint32_t Test_HandleAppEventNotifier_NetworkEvent_UnsubscribeOnly()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter();

    bool status = false;
    const uint32_t rc = notif->HandleAppEventNotifier(emitter, "device.onnetworkchanged", false, status);
    ExpectEqU32(tr, rc, ERROR_NONE, "Gap7: network event listen=false returns ERROR_NONE (job submitted)");
    ExpectTrue(tr, status, "Gap7: status is true (async job submitted successfully)");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    emitter->Release();
    return tr.failures;
}

// ─── VideoOutput event tests ──────────────────────────────────────────

// TEST_ID: AGC_L0_120
// VideoOutputDelegate registration path: videooutput.onresolutionchanged listen=true.
uint32_t Test_HandleAppEventNotifier_VideoOutputResolutionEvent_ListenTrue()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter();

    bool status = false;
    const uint32_t rc = notif->HandleAppEventNotifier(emitter, "videooutput.onresolutionchanged", true, status);
    ExpectEqU32(tr, rc, ERROR_NONE, "VideoOutput resolution event listen=true returns ERROR_NONE");
    ExpectTrue(tr, status, "VideoOutput resolution event listen=true sets status true");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    emitter->Release();
    return tr.failures;
}

// TEST_ID: AGC_L0_121
// VideoOutputDelegate registration path: videooutput.onhdcpchanged listen=true.
uint32_t Test_HandleAppEventNotifier_VideoOutputHdcpEvent_ListenTrue()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter();

    bool status = false;
    const uint32_t rc = notif->HandleAppEventNotifier(emitter, "videooutput.onhdcpchanged", true, status);
    ExpectEqU32(tr, rc, ERROR_NONE, "VideoOutput HDCP event listen=true returns ERROR_NONE");
    ExpectTrue(tr, status, "VideoOutput HDCP event listen=true sets status true");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    emitter->Release();
    return tr.failures;
}

// TEST_ID: AGC_L0_122
// VideoOutputDelegate registration path: videooutput.oncecstatechanged listen=true.
uint32_t Test_HandleAppEventNotifier_VideoOutputCecActiveStateEvent_ListenTrue()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter();

    bool status = false;
    const uint32_t rc = notif->HandleAppEventNotifier(emitter, "videooutput.oncecstatechanged", true, status);
    ExpectEqU32(tr, rc, ERROR_NONE, "VideoOutput CecActiveState event listen=true returns ERROR_NONE");
    ExpectTrue(tr, status, "VideoOutput CecActiveState event listen=true sets status true");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    emitter->Release();
    return tr.failures;
}

// TEST_ID: AGC_L0_097A
// AvOutputDelegate registration path: device.ondolbyatmosexperienceavailablechanged listen=true.
uint32_t Test_HandleAppEventNotifier_DolbyAtmosEvent_ListenTrue()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter();

    bool status = false;
    const uint32_t rc = notif->HandleAppEventNotifier(emitter, "device.ondolbyatmosexperienceavailablechanged", true, status);
    ExpectEqU32(tr, rc, ERROR_NONE, "Dolby Atmos event listen=true returns ERROR_NONE (job submitted)");
    ExpectTrue(tr, status, "Dolby Atmos event listen=true sets status true");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    emitter->Release();
    return tr.failures;
}

// TEST_ID: AGC_L0_123
// VideoOutputDelegate registration path: videooutput.onportchanged listen=true.
uint32_t Test_HandleAppEventNotifier_VideoOutputPortEvent_ListenTrue()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter();

    bool status = false;
    const uint32_t rc = notif->HandleAppEventNotifier(emitter, "videooutput.onportchanged", true, status);
    ExpectEqU32(tr, rc, ERROR_NONE, "VideoOutput port event listen=true returns ERROR_NONE");
    ExpectTrue(tr, status, "VideoOutput port event listen=true sets status true");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    emitter->Release();
    return tr.failures;
}

// TEST_ID: AGC_L0_125
// Regression: VideoOutputDelegate teardown must only unsubscribe methods that were
// actually registered. This isolated plugin instance listens to the two event paths
// that exercise DisplaySettings and DisplayInfo subscription bookkeeping, then
// deinitializes immediately to validate teardown does not assert.
uint32_t Test_HandleAppEventNotifier_VideoOutputTeardownAfterSubscriptions_NoCrash()
{
    TestResult tr;
    PluginAndService ps;

    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectTrue(tr, initResult.empty(), "Isolated plugin Initialize returns empty string");

    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter();

    bool portStatus = false;
    const uint32_t portRc = notif->HandleAppEventNotifier(emitter, "videooutput.onportchanged", true, portStatus);
    ExpectEqU32(tr, portRc, ERROR_NONE, "VideoOutput port event listen=true returns ERROR_NONE");
    ExpectTrue(tr, portStatus, "VideoOutput port event listen=true sets status true");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    emitter->Release();
    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

// TEST_ID: AGC_L0_126
// Narrow regression: a single consumer-facing DisplaySettings-backed VideoOutput
// subscription must tear down cleanly. This isolates the case that originally
// failed when teardown attempted to unregister an event that had never been
// registered on the underlying JSON-RPC link.
uint32_t Test_HandleAppEventNotifier_VideoOutputResolutionOnlyTeardown_NoCrash()
{
    TestResult tr;
    PluginAndService ps;

    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectTrue(tr, initResult.empty(), "Isolated plugin Initialize returns empty string");

    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter();

    bool resolutionStatus = false;
    const uint32_t resolutionRc = notif->HandleAppEventNotifier(emitter, "videooutput.onresolutionchanged", true, resolutionStatus);
    ExpectEqU32(tr, resolutionRc, ERROR_NONE, "VideoOutput resolution event listen=true returns ERROR_NONE");
    ExpectTrue(tr, resolutionStatus, "VideoOutput resolution event listen=true sets status true");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    emitter->Release();
    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

// TEST_ID: AGC_L0_127
// Narrow regression: a single DisplaySettings-backed port subscription must tear
// down cleanly after registration.
uint32_t Test_HandleAppEventNotifier_VideoOutputPortOnlyTeardown_NoCrash()
{
    TestResult tr;
    PluginAndService ps;

    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectTrue(tr, initResult.empty(), "Isolated plugin Initialize returns empty string");

    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter();

    bool portStatus = false;
    const uint32_t portRc = notif->HandleAppEventNotifier(emitter, "videooutput.onportchanged", true, portStatus);
    ExpectEqU32(tr, portRc, ERROR_NONE, "VideoOutput port event listen=true returns ERROR_NONE");
    ExpectTrue(tr, portStatus, "VideoOutput port event listen=true sets status true");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    emitter->Release();
    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

// TEST_ID: AGC_L0_128
// Narrow regression: a single HdcpProfile-backed subscription must tear down
// cleanly after registration.
uint32_t Test_HandleAppEventNotifier_VideoOutputHdcpOnlyTeardown_NoCrash()
{
    TestResult tr;
    PluginAndService ps;

    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectTrue(tr, initResult.empty(), "Isolated plugin Initialize returns empty string");

    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter();

    bool hdcpStatus = false;
    const uint32_t hdcpRc = notif->HandleAppEventNotifier(emitter, "videooutput.onhdcpchanged", true, hdcpStatus);
    ExpectEqU32(tr, hdcpRc, ERROR_NONE, "VideoOutput HDCP event listen=true returns ERROR_NONE");
    ExpectTrue(tr, hdcpStatus, "VideoOutput HDCP event listen=true sets status true");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    emitter->Release();
    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

// TEST_ID: AGC_L0_129
// Narrow regression: a single HdmiCecSource-backed subscription must tear down
// cleanly after registration.
uint32_t Test_HandleAppEventNotifier_VideoOutputCecOnlyTeardown_NoCrash()
{
    TestResult tr;
    PluginAndService ps;

    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectTrue(tr, initResult.empty(), "Isolated plugin Initialize returns empty string");

    QIGuard<Exchange::IAppNotificationHandler> notif(ps.plugin);
    auto* emitter = new StubEmitter();

    bool cecStatus = false;
    const uint32_t cecRc = notif->HandleAppEventNotifier(emitter, "videooutput.oncecstatechanged", true, cecStatus);
    ExpectEqU32(tr, cecRc, ERROR_NONE, "VideoOutput CEC event listen=true returns ERROR_NONE");
    ExpectTrue(tr, cecStatus, "VideoOutput CEC event listen=true sets status true");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    emitter->Release();
    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}


