#include "AppGatewayCommon_common_test.h"

// TEST_ID: AGC_L0_012
// HandleAppGatewayRequest with an unknown method returns ERROR_UNKNOWN_KEY.
uint32_t Test_HandleRequest_UnknownMethod()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "invalid.method.xyz", "{}", result);
    ExpectEqU32(tr, rc, ERROR_UNKNOWN_KEY, "invalid.method returns ERROR_UNKNOWN_KEY");

    return tr.failures;
}

// TEST_ID: AGC_L0_013
// HandleAppGatewayRequest for "device.make" in L0 (no real plugins) returns ERROR_NONE
// because SystemDelegate::GetDeviceMake sets make="unknown" and returns ERROR_NONE
// after wrapping the default in quotes.
// In L0 without real Thunder plugins the link acquisition fails; the delegate
// falls through to the "unknown" default and still returns ERROR_NONE.
uint32_t Test_HandleRequest_DeviceMake_DelegateUnavailable()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "device.make", "{}", result);

    // SystemDelegate::GetDeviceMake returns ERROR_NONE with make="\"unknown\""
    // even when the underlying JSON-RPC link is unavailable, because the
    // delegate defaults to "unknown". Accept either ERROR_NONE or ERROR_UNAVAILABLE
    // depending on the environment's Thunder link behavior.
    const bool acceptable = (rc == ERROR_NONE || rc == ERROR_UNAVAILABLE);
    ExpectTrue(tr, acceptable, "device.make returns ERROR_NONE or ERROR_UNAVAILABLE in L0");

    return tr.failures;
}

// TEST_ID: AGC_L0_014
// HandleAppGatewayRequest for "metrics.*" pass-through returns ERROR_NONE with "null".
uint32_t Test_HandleRequest_MetricsPassthrough()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "metrics.someEvent", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "metrics.someEvent returns ERROR_NONE");
    ExpectEqStr(tr, result, "null", "metrics.someEvent result is null");

    return tr.failures;
}

// TEST_ID: AGC_L0_015
// HandleAppGatewayRequest for "discovery.watched" returns ERROR_NONE with "null".
uint32_t Test_HandleRequest_DiscoveryWatched()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "discovery.watched", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "discovery.watched returns ERROR_NONE");
    ExpectEqStr(tr, result, "null", "discovery.watched result is null");

    return tr.failures;
}

// TEST_ID: AGC_L0_016
// HandleAppGatewayRequest is case-insensitive for method names.
uint32_t Test_HandleRequest_CaseInsensitiveMethod()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    // "DEVICE.MAKE" should be lowered to "device.make" internally.
    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "DEVICE.MAKE", "{}", result);
    const bool acceptable = (rc == ERROR_NONE || rc == ERROR_UNAVAILABLE);
    ExpectTrue(tr, acceptable, "DEVICE.MAKE (uppercase) routes same as device.make");

    return tr.failures;
}

// TEST_ID: AGC_L0_017
// HandleAppGatewayRequest for lifecycle.ready via the handler map.
// In L0, InvokeLifecycleDelegate reaches LifecycleDelegate::LifecycleReady because
// both mDelegate and lifecycleDelegate are non-null.  LifecycleReady sets result="null"
// and returns ERROR_NONE when mLifecycleManagerState is nullptr (no /etc/rdkappmanagers).
uint32_t Test_HandleRequest_LifecycleReady()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "lifecycle.ready", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "lifecycle.ready returns ERROR_NONE in L0 (mLifecycleManagerState is null)");

    return tr.failures;
}

// ============================================================================
// Tests AGC_L0_018 – AGC_L0_056 — handler-map getters
// ============================================================================

// TEST_ID: AGC_L0_018
// Handler-map getter: device.name
uint32_t Test_HandleRequest_DeviceName()
{
    return DelegateGetterTest("device.name");
}

// TEST_ID: AGC_L0_019
// Handler-map getter: device.sku
uint32_t Test_HandleRequest_DeviceSku()
{
    return DelegateGetterTest("device.sku");
}

// TEST_ID: AGC_L0_020
// Handler-map getter: device.network
uint32_t Test_HandleRequest_DeviceNetwork()
{
    return DelegateGetterTest("device.network");
}

// TEST_ID: AGC_L0_021
// Handler-map getter: device.version
uint32_t Test_HandleRequest_DeviceVersion()
{
    return DelegateGetterTest("device.version");
}

// TEST_ID: AGC_L0_022
// Handler-map getter: device.screenresolution
uint32_t Test_HandleRequest_DeviceScreenResolution()
{
    return DelegateGetterTest("device.screenresolution");
}

// TEST_ID: AGC_L0_023
// Handler-map getter: device.videoresolution
uint32_t Test_HandleRequest_DeviceVideoResolution()
{
    return DelegateGetterTest("device.videoresolution");
}

// TEST_ID: AGC_L0_024
// Handler-map getter: device.hdcp
uint32_t Test_HandleRequest_DeviceHdcp()
{
    return DelegateGetterTest("device.hdcp");
}

// TEST_ID: AGC_L0_025
// Handler-map getter: device.hdr
uint32_t Test_HandleRequest_DeviceHdr()
{
    return DelegateGetterTest("device.hdr");
}

// TEST_ID: AGC_L0_026
// Handler-map getter: device.audio
uint32_t Test_HandleRequest_DeviceAudio()
{
    return DelegateGetterTest("device.audio");
}

// TEST_ID: AGC_L0_026A
// Handler-map getter: device.dolbyatmosexperienceavailable
uint32_t Test_HandleRequest_DeviceDolbyAtmosExperience()
{
    return DelegateGetterTest("device.dolbyatmosexperienceavailable");
}

// TEST_ID: AGC_L0_027
// Handler-map getter: voiceguidance.enabled
uint32_t Test_HandleRequest_VoiceGuidanceEnabled()
{
    return DelegateGetterTest("voiceguidance.enabled");
}

// TEST_ID: AGC_L0_028
// Handler-map getter: voiceguidance.navigationhints
uint32_t Test_HandleRequest_VoiceGuidanceNavigationHints()
{
    return DelegateGetterTest("voiceguidance.navigationhints");
}

// TEST_ID: AGC_L0_029
// Handler-map getter: accessibility.voiceguidancesettings with ctx.version="1.0.0"
// IsRDK8Compliant("1.0.0") == false → addSpeed = !false = true
uint32_t Test_HandleRequest_VoiceGuidanceSettings_NonRDK8()
{
    Exchange::GatewayContext ctx = DefaultContext();
    ctx.version = "1.0.0";
    return DelegateGetterTest("accessibility.voiceguidancesettings", ctx);
}

// TEST_ID: AGC_L0_030
// Handler-map getter: accessibility.voiceguidancesettings with ctx.version="8"
// IsRDK8Compliant("8") == true → addSpeed = !true = false
uint32_t Test_HandleRequest_VoiceGuidanceSettings_RDK8()
{
    Exchange::GatewayContext ctx = DefaultContext();
    ctx.version = "8";
    return DelegateGetterTest("accessibility.voiceguidancesettings", ctx);
}

// TEST_ID: AGC_L0_031
// Handler-map getter: accessibility.voiceguidance (always addSpeed=true)
uint32_t Test_HandleRequest_AccessibilityVoiceGuidance()
{
    return DelegateGetterTest("accessibility.voiceguidance");
}

// TEST_ID: AGC_L0_032
// Handler-map getter: accessibility.audiodescriptionsettings
uint32_t Test_HandleRequest_AccessibilityAudioDescriptionSettings()
{
    return DelegateGetterTest("accessibility.audiodescriptionsettings");
}

// TEST_ID: AGC_L0_033
// Handler-map getter: accessibility.audiodescription
uint32_t Test_HandleRequest_AccessibilityAudioDescription()
{
    return DelegateGetterTest("accessibility.audiodescription");
}

// TEST_ID: AGC_L0_034
// Handler-map getter: audiodescriptions.enabled
uint32_t Test_HandleRequest_AudioDescriptionsEnabled()
{
    return DelegateGetterTest("audiodescriptions.enabled");
}

// TEST_ID: AGC_L0_035
// Handler-map getter: accessibility.highcontrastui
uint32_t Test_HandleRequest_AccessibilityHighContrastUI()
{
    return DelegateGetterTest("accessibility.highcontrastui");
}

// TEST_ID: AGC_L0_036
// Handler-map getter: closedcaptions.enabled
uint32_t Test_HandleRequest_ClosedCaptionsEnabled()
{
    return DelegateGetterTest("closedcaptions.enabled");
}

// TEST_ID: AGC_L0_037
// Handler-map getter: closedcaptions.preferredlanguages
uint32_t Test_HandleRequest_ClosedCaptionsPreferredLanguages()
{
    return DelegateGetterTest("closedcaptions.preferredlanguages");
}

// TEST_ID: AGC_L0_038
// Handler-map getter: accessibility.closedcaptions
uint32_t Test_HandleRequest_AccessibilityClosedCaptions()
{
    return DelegateGetterTest("accessibility.closedcaptions");
}

// TEST_ID: AGC_L0_039
// Handler-map getter: accessibility.closedcaptionssettings
uint32_t Test_HandleRequest_AccessibilityClosedCaptionsSettings()
{
    return DelegateGetterTest("accessibility.closedcaptionssettings");
}

// TEST_ID: AGC_L0_040
// Handler-map getter: localization.language
uint32_t Test_HandleRequest_LocalizationLanguage()
{
    return DelegateGetterTest("localization.language");
}

// TEST_ID: AGC_L0_041
// Handler-map getter: localization.locale
uint32_t Test_HandleRequest_LocalizationLocale()
{
    return DelegateGetterTest("localization.locale");
}

// TEST_ID: AGC_L0_042
// Handler-map getter: localization.preferredaudiolanguages
uint32_t Test_HandleRequest_LocalizationPreferredAudioLanguages()
{
    return DelegateGetterTest("localization.preferredaudiolanguages");
}

// TEST_ID: AGC_L0_043
// Handler-map getter: localization.countrycode
uint32_t Test_HandleRequest_LocalizationCountryCode()
{
    return DelegateGetterTest("localization.countrycode");
}

// TEST_ID: AGC_L0_044
// Handler-map getter: localization.timezone
uint32_t Test_HandleRequest_LocalizationTimezone()
{
    return DelegateGetterTest("localization.timezone");
}

// TEST_ID: AGC_L0_045
// Handler-map getter: secondscreen.friendlyname
uint32_t Test_HandleRequest_SecondScreenFriendlyName()
{
    return DelegateGetterTest("secondscreen.friendlyname");
}

// TEST_ID: AGC_L0_046
// Handler-map pass-through: localization.addadditionalinfo → ERROR_NONE, result=="null"
uint32_t Test_HandleRequest_LocalizationAddAdditionalInfo()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();
    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "localization.addadditionalinfo", R"({"key":"test","value":"val"})", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "localization.addadditionalinfo returns ERROR_NONE");
    ExpectEqStr(tr, result, "null", "localization.addadditionalinfo result is null");
    return tr.failures;
}

// TEST_ID: AGC_L0_047
// lifecycle.state returns ERROR_NONE (state lookup uses empty map, returns default state string).
uint32_t Test_HandleRequest_LifecycleState()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();
    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "lifecycle.state", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "lifecycle.state returns ERROR_NONE in L0");
    return tr.failures;
}

// TEST_ID: AGC_L0_048
// lifecycle.close in L0 → mLifecycleManagerState is null → ERROR_GENERAL
uint32_t Test_HandleRequest_LifecycleClose()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();
    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "lifecycle.close", R"({"reason":"userExit"})", result);
    ExpectEqU32(tr, rc, ERROR_GENERAL, "lifecycle.close returns ERROR_GENERAL in L0");
    return tr.failures;
}

// TEST_ID: AGC_L0_049
// lifecycle.finished always returns ERROR_NONE with result="null"
// LifecycleDelegate::LifecycleFinished unconditionally sets result="null" and returns ERROR_NONE.
uint32_t Test_HandleRequest_LifecycleFinished()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();
    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "lifecycle.finished", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "lifecycle.finished returns ERROR_NONE");
    ExpectEqStr(tr, result, "null", "lifecycle.finished result is null");
    return tr.failures;
}

// TEST_ID: AGC_L0_050
// lifecycle2.state returns ERROR_NONE (state lookup uses empty map, returns default state string).
uint32_t Test_HandleRequest_Lifecycle2State()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();
    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "lifecycle2.state", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "lifecycle2.state returns ERROR_NONE in L0");
    ExpectEqStr(tr, result, "\"unloaded\"", "lifecycle2.state result is quoted unloaded");
    return tr.failures;
}

// TEST_ID: AGC_L0_051
// lifecycle2.close in L0 → mLifecycleManagerState is null → ERROR_GENERAL
uint32_t Test_HandleRequest_Lifecycle2Close()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();
    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "lifecycle2.close", R"({"type":"deactivate"})", result);
    ExpectEqU32(tr, rc, ERROR_GENERAL, "lifecycle2.close returns ERROR_GENERAL in L0");
    return tr.failures;
}

// TEST_ID: AGC_L0_052
// commoninternal.dispatchintent returns ERROR_NONE with result="null"
// LifecycleDelegate::DispatchLastIntent unconditionally sets result="null" and returns ERROR_NONE.
uint32_t Test_HandleRequest_DispatchIntent()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();
    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "commoninternal.dispatchintent", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "commoninternal.dispatchintent returns ERROR_NONE");
    ExpectEqStr(tr, result, "null", "commoninternal.dispatchintent result is null");
    return tr.failures;
}

// TEST_ID: AGC_L0_053
// commoninternal.getlastintent returns ERROR_NONE with JSON containing intentId and intent fields
// When no intent is stored, intentId=0 and intent is an empty JSON object ({}).
uint32_t Test_HandleRequest_GetLastIntent()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();
    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "commoninternal.getlastintent", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "commoninternal.getlastintent returns ERROR_NONE");
    // Result must be a JSON object with intentId and intent fields
    const bool hasIntentId = result.find("\"intentId\":") != std::string::npos;
    const bool hasIntent   = result.find("\"intent\":")   != std::string::npos;
    // When no intent is stored, intent must be serialized as an empty JSON object
    const bool hasEmptyObject = result.find("\"intent\":{}") != std::string::npos;
    if (!hasIntentId) tr.failures++;
    if (!hasIntent) tr.failures++;
    if (!hasEmptyObject) tr.failures++;
    return tr.failures;
}

// TEST_ID: AGC_L0_057
// actions.start in L0 → IAppActions plugin unavailable → ERROR_UNAVAILABLE
uint32_t Test_HandleRequest_ActionsStart_NoPlugin()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();
    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "actions.start", "{\"intent\":{\"action\":\"play\"}}", result);
    ExpectEqU32(tr, rc, ERROR_UNAVAILABLE, "actions.start returns ERROR_UNAVAILABLE when plugin absent");
    return tr.failures;
}

// TEST_ID: AGC_L0_058
// actions.start with empty payload → ERROR_BAD_REQUEST (validated before reaching plugin)
uint32_t Test_HandleRequest_ActionsStart_EmptyPayload()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();
    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "actions.start", "", result);
    ExpectEqU32(tr, rc, ERROR_BAD_REQUEST, "actions.start with empty payload returns ERROR_BAD_REQUEST");
    return tr.failures;
}

// TEST_ID: AGC_L0_059
// actions.intent in L0 → no stored intent → returns JSON with intentId=0 and intent={}
uint32_t Test_HandleRequest_ActionsIntent_EmptyRegistry()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();
    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "actions.intent", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "actions.intent returns ERROR_NONE");
    const bool hasIntentId    = result.find("\"intentId\":") != std::string::npos;
    const bool hasIntent      = result.find("\"intent\":")   != std::string::npos;
    // When no intent is stored, intent must be serialized as an empty JSON object
    const bool hasEmptyObject = result.find("\"intent\":{}") != std::string::npos;
    if (!hasIntentId) tr.failures++;
    if (!hasIntent) tr.failures++;
    if (!hasEmptyObject) tr.failures++;
    return tr.failures;
}

// TEST_ID: AGC_L0_060
// commoninternal.setintent with non-empty intent stores it and subsequent getlastintent returns it
uint32_t Test_HandleRequest_SetIntent_StoresAndGet()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    // Set a non-empty intent
    const uint32_t rc1 = handler->HandleAppGatewayRequest(ctx, "commoninternal.setintent", R"({"action":"play","content":"video123"})", result);
    ExpectEqU32(tr, rc1, ERROR_NONE, "commoninternal.setintent returns ERROR_NONE");
    ExpectEqStr(tr, result, "null", "commoninternal.setintent result is null");

    // Verify getlastintent returns the stored intent
    const uint32_t rc2 = handler->HandleAppGatewayRequest(ctx, "commoninternal.getlastintent", "{}", result);
    ExpectEqU32(tr, rc2, ERROR_NONE, "commoninternal.getlastintent returns ERROR_NONE");
    const bool hasIntent = result.find("\"action\"") != std::string::npos;
    const bool hasContent = result.find("video123") != std::string::npos;
    ExpectTrue(tr, hasIntent, "getlastintent returns stored intent with action field");
    ExpectTrue(tr, hasContent, "getlastintent returns stored intent with content value");

    return tr.failures;
}

// TEST_ID: AGC_L0_061
// commoninternal.setintent with empty payload no-ops
uint32_t Test_HandleRequest_SetIntent_EmptyPayload()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    // First set a non-empty intent
    handler->HandleAppGatewayRequest(ctx, "commoninternal.setintent", R"({"initial":"intent"})", result);

    // Call setintent with empty payload - should no-op and not overwrite
    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "commoninternal.setintent", "", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "commoninternal.setintent with empty payload returns ERROR_NONE");
    ExpectEqStr(tr, result, "null", "commoninternal.setintent result is null");

    // Verify the original intent is still there
    const uint32_t rc2 = handler->HandleAppGatewayRequest(ctx, "commoninternal.getlastintent", "{}", result);
    ExpectEqU32(tr, rc2, ERROR_NONE, "commoninternal.getlastintent returns ERROR_NONE");
    const bool hasOriginal = result.find("initial") != std::string::npos;
    ExpectTrue(tr, hasOriginal, "Empty payload did not overwrite existing intent");

    return tr.failures;
}

// TEST_ID: AGC_L0_062
// commoninternal.setintent with "null" string payload no-ops
uint32_t Test_HandleRequest_SetIntent_NullStringPayload()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    // First set a non-empty intent
    handler->HandleAppGatewayRequest(ctx, "commoninternal.setintent", R"({"original":"intent"})", result);

    // Call setintent with literal "null" string - should no-op and not overwrite
    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "commoninternal.setintent", "null", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "commoninternal.setintent with 'null' payload returns ERROR_NONE");
    ExpectEqStr(tr, result, "null", "commoninternal.setintent result is null");

    // Verify the original intent is still there
    const uint32_t rc2 = handler->HandleAppGatewayRequest(ctx, "commoninternal.getlastintent", "{}", result);
    ExpectEqU32(tr, rc2, ERROR_NONE, "commoninternal.getlastintent returns ERROR_NONE");
    const bool hasOriginal = result.find("original") != std::string::npos;
    const bool hasNullString = result.find("\"null\"") != std::string::npos; // Should NOT have literal "null" as intent
    ExpectTrue(tr, hasOriginal, "'null' payload did not overwrite existing intent");
    ExpectTrue(tr, !hasNullString, "Intent is not the literal string 'null'");

    return tr.failures;
}

// TEST_ID: AGC_L0_054
// advertising.advertisingid in L0 → SharedStorage unavailable
uint32_t Test_HandleRequest_AdvertisingId()
{
    return DelegateGetterTest("advertising.advertisingid");
}

// TEST_ID: AGC_L0_055
// device.uid in L0 → SharedStorage unavailable
uint32_t Test_HandleRequest_DeviceUid()
{
    return DelegateGetterTest("device.uid");
}

// TEST_ID: AGC_L0_056
// network.connected in L0 → NetworkDelegate
uint32_t Test_HandleRequest_NetworkConnected()
{
    return DelegateGetterTest("network.connected");
}

// ============================================================================
// Tests AGC_L0_098 – AGC_L0_100 — presentation.focused
// ============================================================================

// TEST_ID: AGC_L0_098
// Handler-map getter: presentation.focused is routed and returns ERROR_NONE.
// In L0, mAppIdInstanceIdMap is empty so GetAppInstanceId returns "" and
// IsAppInstanceIdFocused("") is true (focusedAppInstanceId initialises to "").
// result = "true", rc = ERROR_NONE.
uint32_t Test_HandleRequest_PresentationFocused_Routed()
{
    return DelegateGetterTest("presentation.focused");
}

// TEST_ID: AGC_L0_099
// presentation.focused result is always the literal string "true" or "false".
// LifecycleDelegate::GetPresentationFocused never returns an error JSON — it
// unconditionally sets result to one of the two boolean strings and returns ERROR_NONE.
uint32_t Test_HandleRequest_PresentationFocused_ResultIsBooleanString()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "presentation.focused", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "presentation.focused returns ERROR_NONE");
    const bool isBoolStr = (result == "true" || result == "false");
    ExpectTrue(tr, isBoolStr, "presentation.focused result is 'true' or 'false'");

    return tr.failures;
}

// TEST_ID: AGC_L0_100
// presentation.focused is case-insensitive: PRESENTATION.FOCUSED is lowered
// internally and routes to the same handler as presentation.focused.
uint32_t Test_HandleRequest_PresentationFocused_CaseInsensitive()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "PRESENTATION.FOCUSED", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "PRESENTATION.FOCUSED (uppercase) routes same as presentation.focused");

    return tr.failures;
}

// ============================================================================
// Tests AGC_L0_101 – AGC_L0_103 — ParentalControl handler-map getters
// ============================================================================

// TEST_ID: AGC_L0_101
// Handler-map getter: parentalcontrol.pincontrol is routed to AppGatewayCommon.
// In L0 the UserSettings COM interface is unavailable, so GetPinControl returns
// ERROR_UNAVAILABLE.  DelegateGetterTest accepts ERROR_NONE/UNAVAILABLE/GENERAL.
uint32_t Test_HandleRequest_ParentalControl_PinControl()
{
    return DelegateGetterTest("parentalcontrol.pincontrol");
}

// TEST_ID: AGC_L0_102
// Handler-map getter: parentalcontrol.blocknotratedcontent is routed.
uint32_t Test_HandleRequest_ParentalControl_BlockNotRatedContent()
{
    return DelegateGetterTest("parentalcontrol.blocknotratedcontent");
}

// TEST_ID: AGC_L0_103
// Handler-map getter: parentalcontrol.viewingrestrictions is routed.
uint32_t Test_HandleRequest_ParentalControl_ViewingRestrictions()
{
    return DelegateGetterTest("parentalcontrol.viewingrestrictions");
}

// TEST_ID: AGC_L0_104A
// VideoOutput.resolution returns the safe default when Thunder is unavailable.
uint32_t Test_HandleRequest_VideoOutputResolution_NoDisplay()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "videooutput.resolution", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "videooutput.resolution returns ERROR_NONE when Thunder is unavailable");
    ExpectEqStr(tr, result, "{\"width\":0,\"height\":0}", "videooutput.resolution returns zeroed dimensions");
    return tr.failures;
}

// TEST_ID: AGC_L0_104B
// VideoOutput.hdcp returns the safe default when Thunder is unavailable.
uint32_t Test_HandleRequest_VideoOutputHdcp_NoDisplay()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "videooutput.hdcp", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "videooutput.hdcp returns ERROR_NONE when Thunder is unavailable");
    ExpectEqStr(tr, result, "\"none\"", "videooutput.hdcp returns none when Thunder is unavailable");
    return tr.failures;
}

// TEST_ID: AGC_L0_104E
// VideoOutput.colorFormat returns the safe default when Thunder is unavailable.
uint32_t Test_HandleRequest_VideoOutputColorFormat_NoDisplay()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "videooutput.colorformat", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "videooutput.colorformat returns ERROR_NONE when Thunder is unavailable");
    ExpectEqStr(tr, result, "\"none\"", "videooutput.colorformat returns \"none\" when Thunder is unavailable");
    return tr.failures;
}

// TEST_ID: AGC_L0_104F
// VideoOutput.quantizationRange returns the safe default when Thunder is unavailable.
uint32_t Test_HandleRequest_VideoOutputQuantizationRange_NoDisplay()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "videooutput.quantizationrange", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "videooutput.quantizationrange returns ERROR_NONE when Thunder is unavailable");
    ExpectEqStr(tr, result, "\"none\"", "videooutput.quantizationrange returns \"none\" when Thunder is unavailable");
    return tr.failures;
}

// TEST_ID: AGC_L0_104C
// VideoOutput.cecState returns the safe default when Thunder is unavailable.
uint32_t Test_HandleRequest_VideoOutputCecActiveState_NoDisplay()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "videooutput.cecstate", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "videooutput.cecstate returns ERROR_NONE when Thunder is unavailable");
    ExpectEqStr(tr, result, "\"unsupported\"", "videooutput.cecstate returns unsupported when Thunder is unavailable");
    return tr.failures;
}

// TEST_ID: AGC_L0_104D
// VideoOutput.port returns the safe default when Thunder is unavailable.
uint32_t Test_HandleRequest_VideoOutputPort_NoDisplay()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "videooutput.port", "{}", result);
    ExpectEqU32(tr, rc, ERROR_NONE, "videooutput.port returns ERROR_NONE when Thunder is unavailable");
    ExpectEqStr(tr, result, "\"none\"", "videooutput.port returns none when Thunder is unavailable");
    return tr.failures;
}

// ─── VideoOutput routing tests ────────────────────────────────────────

// TEST_ID: AGC_L0_110
// Handler-map getter: videooutput.resolution
uint32_t Test_HandleRequest_VideoOutputResolution()
{
    return DelegateGetterTest("videooutput.resolution");
}

// TEST_ID: AGC_L0_111
// Handler-map getter: videooutput.hdcp
uint32_t Test_HandleRequest_VideoOutputHdcp()
{
    return DelegateGetterTest("videooutput.hdcp");
}

// TEST_ID: AGC_L0_112
// Handler-map getter: videooutput.cecstate
uint32_t Test_HandleRequest_VideoOutputCecActiveState()
{
    return DelegateGetterTest("videooutput.cecstate");
}

// TEST_ID: AGC_L0_113
// Handler-map getter: videooutput.port
uint32_t Test_HandleRequest_VideoOutputPort()
{
    return DelegateGetterTest("videooutput.port");
}

// TEST_ID: AGC_L0_114
// Handler-map getter: videooutput.refreshrate
uint32_t Test_HandleRequest_VideoOutputRefreshRate()
{
    return DelegateGetterTest("videooutput.refreshrate");
}

// TEST_ID: AGC_L0_115
// Handler-map getter: videooutput.colordepth
uint32_t Test_HandleRequest_VideoOutputColorDepth()
{
    return DelegateGetterTest("videooutput.colordepth");
}

// TEST_ID: AGC_L0_116
// Handler-map getter: videooutput.colorformat
uint32_t Test_HandleRequest_VideoOutputColorFormat()
{
    return DelegateGetterTest("videooutput.colorformat");
}

// TEST_ID: AGC_L0_117
// Handler-map getter: videooutput.colorimetry
uint32_t Test_HandleRequest_VideoOutputColorimetry()
{
    return DelegateGetterTest("videooutput.colorimetry");
}

// TEST_ID: AGC_L0_118
// Handler-map getter: videooutput.dynamicrange
uint32_t Test_HandleRequest_VideoOutputDynamicRange()
{
    return DelegateGetterTest("videooutput.dynamicrange");
}

// TEST_ID: AGC_L0_119
// Handler-map getter: videooutput.quantizationrange
uint32_t Test_HandleRequest_VideoOutputQuantizationRange()
{
    return DelegateGetterTest("videooutput.quantizationrange");
}

// ============================================================================
// Tests AGC_L0_108 – AGC_L0_109 — actions.start null/empty intent validation
// ============================================================================
// TEST_ID: AGC_L0_108
// actions.start with {"intent":null} → ERROR_BAD_REQUEST
// The intent field is JSON null; ActionsStart must reject it before forwarding.
uint32_t Test_HandleRequest_ActionsStart_NullIntentField()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();
    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "actions.start", "{\"intent\":null}", result);
    ExpectEqU32(tr, rc, ERROR_BAD_REQUEST, "actions.start with null intent field returns ERROR_BAD_REQUEST");
    return tr.failures;
}

// TEST_ID: AGC_L0_109
// actions.start with {"intent":{}} → ERROR_BAD_REQUEST
// An empty object is semantically meaningless; ActionsStart must reject it.
uint32_t Test_HandleRequest_ActionsStart_EmptyObjectIntentField()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();
    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "actions.start", "{\"intent\":{}}", result);
    ExpectEqU32(tr, rc, ERROR_BAD_REQUEST, "actions.start with empty object intent field returns ERROR_BAD_REQUEST");
    return tr.failures;
}

// ============================================================================
// texttospeech.speak tests
// ============================================================================

// texttospeech.speak with valid payload returns ERROR_UNAVAILABLE in L0
// because the TTS COM-RPC interface (GetTTS()) is nullptr.
uint32_t Test_HandleRequest_TextToSpeechSpeak_ValidPayload()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "texttospeech.speak", R"({"text":"Hello world"})", result);
    ExpectEqU32(tr, rc, ERROR_UNAVAILABLE, "texttospeech.speak returns ERROR_UNAVAILABLE when TTS plugin unavailable");
    return tr.failures;
}

// texttospeech.speak with empty payload returns ERROR_BAD_REQUEST.
uint32_t Test_HandleRequest_TextToSpeechSpeak_EmptyPayload()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "texttospeech.speak", "", result);
    ExpectEqU32(tr, rc, ERROR_BAD_REQUEST, "texttospeech.speak with empty payload returns ERROR_BAD_REQUEST");
    return tr.failures;
}

// texttospeech.speak with null payload returns ERROR_BAD_REQUEST.
uint32_t Test_HandleRequest_TextToSpeechSpeak_NullPayload()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "texttospeech.speak", "null", result);
    ExpectEqU32(tr, rc, ERROR_BAD_REQUEST, "texttospeech.speak with null payload returns ERROR_BAD_REQUEST");
    return tr.failures;
}

// texttospeech.speak with invalid JSON payload returns ERROR_BAD_REQUEST.
uint32_t Test_HandleRequest_TextToSpeechSpeak_InvalidJSON()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "texttospeech.speak", "{invalid json}", result);
    ExpectEqU32(tr, rc, ERROR_BAD_REQUEST, "texttospeech.speak with invalid JSON returns ERROR_BAD_REQUEST");
    return tr.failures;
}

// texttospeech.speak with empty text returns ERROR_INVALID_INPUT_LENGTH.
uint32_t Test_HandleRequest_TextToSpeechSpeak_EmptyText()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "texttospeech.speak", R"({"text":""})", result);
    ExpectEqU32(tr, rc, ERROR_INVALID_INPUT_LENGTH, "texttospeech.speak with empty text returns ERROR_INVALID_INPUT_LENGTH");
    return tr.failures;
}

// texttospeech.speak with empty callsign (from ctx.appId) returns ERROR_INVALID_INPUT_LENGTH.
uint32_t Test_HandleRequest_TextToSpeechSpeak_EmptyCallsign()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();
    ctx.appId = ""; // Empty callsign from context

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "texttospeech.speak", R"({"text":"Hello"})", result);
    ExpectEqU32(tr, rc, ERROR_INVALID_INPUT_LENGTH, "texttospeech.speak with empty callsign returns ERROR_INVALID_INPUT_LENGTH");
    return tr.failures;
}

// texttospeech.speak is case-insensitive: TEXTTOSPEECH.SPEAK is lowered
// internally and routes to the same handler as texttospeech.speak.
uint32_t Test_HandleRequest_TextToSpeechSpeak_CaseInsensitive()
{
    TestResult tr;
    PluginAndService& ps = SharedFixture::instance().ps();

    QIGuard<Exchange::IAppGatewayRequestHandler> handler(ps.plugin);
    std::string result;
    Exchange::GatewayContext ctx = DefaultContext();

    const uint32_t rc = handler->HandleAppGatewayRequest(ctx, "TEXTTOSPEECH.SPEAK", R"({"text":"Hello"})", result);
    ExpectEqU32(tr, rc, ERROR_UNAVAILABLE, "TEXTTOSPEECH.SPEAK (uppercase) routes same as texttospeech.speak");
    return tr.failures;
}
