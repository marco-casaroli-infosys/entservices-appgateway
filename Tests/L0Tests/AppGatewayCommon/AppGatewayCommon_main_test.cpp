#include <iostream>
#include <cstdint>

#include <core/core.h>

#include "L0Bootstrap.hpp"
#include "AppGatewayCommon_common_test.h"

// ---------------------------------------------------------------------------
// Forward declarations — test functions defined in the companion .cpp files.
// ---------------------------------------------------------------------------

// AppGatewayCommon_lifecycle_test.cpp
extern uint32_t Test_Initialize_Deinitialize_Lifecycle();
extern uint32_t Test_Initialize_Twice_Idempotent();
extern uint32_t Test_Deinitialize_Twice_NoCrash();
extern uint32_t Test_Information_ReturnsEmpty();
extern uint32_t Test_InterfaceMap_RequestHandler();
extern uint32_t Test_InterfaceMap_Authenticator();
extern uint32_t Test_InterfaceMap_NotificationHandler();
extern uint32_t Test_HandleRequest_BeforeInit();
extern uint32_t Test_Authenticate_BeforeInit();
extern uint32_t Test_HandleRequest_AfterDeinit();
extern uint32_t Test_Reinitialize_AfterDeinit();

// AppGatewayCommon_routing_test.cpp
extern uint32_t Test_HandleRequest_UnknownMethod();
extern uint32_t Test_HandleRequest_DeviceMake_DelegateUnavailable();
extern uint32_t Test_HandleRequest_MetricsPassthrough();
extern uint32_t Test_HandleRequest_DiscoveryWatched();
extern uint32_t Test_HandleRequest_CaseInsensitiveMethod();
extern uint32_t Test_HandleRequest_LifecycleReady();
extern uint32_t Test_HandleRequest_DeviceName();
extern uint32_t Test_HandleRequest_DeviceSku();
extern uint32_t Test_HandleRequest_DeviceNetwork();
extern uint32_t Test_HandleRequest_DeviceVersion();
extern uint32_t Test_HandleRequest_DeviceScreenResolution();
extern uint32_t Test_HandleRequest_DeviceVideoResolution();
extern uint32_t Test_HandleRequest_DeviceHdcp();
extern uint32_t Test_HandleRequest_DeviceHdr();
extern uint32_t Test_HandleRequest_DeviceAudio();
extern uint32_t Test_HandleRequest_VideoOutputResolution_NoDisplay();
extern uint32_t Test_HandleRequest_VideoOutputHdcp_NoDisplay();
extern uint32_t Test_HandleRequest_VideoOutputCecActiveState_NoDisplay();
extern uint32_t Test_HandleRequest_VideoOutputPort_NoDisplay();
extern uint32_t Test_HandleRequest_VideoOutputResolution();
extern uint32_t Test_HandleRequest_VideoOutputHdcp();
extern uint32_t Test_HandleRequest_VideoOutputCecActiveState();
extern uint32_t Test_HandleRequest_VideoOutputPort();
extern uint32_t Test_HandleRequest_VideoOutputRefreshRate();
extern uint32_t Test_HandleRequest_VideoOutputColorDepth();
extern uint32_t Test_HandleRequest_VideoOutputColorFormat();
extern uint32_t Test_HandleRequest_VideoOutputColorimetry();
extern uint32_t Test_HandleRequest_VideoOutputDynamicRange();
extern uint32_t Test_HandleRequest_VideoOutputQuantizationRange();
extern uint32_t Test_HandleRequest_DeviceDolbyAtmosExperience();
extern uint32_t Test_HandleRequest_VoiceGuidanceEnabled();
extern uint32_t Test_HandleRequest_VoiceGuidanceNavigationHints();
extern uint32_t Test_HandleRequest_VoiceGuidanceSettings_NonRDK8();
extern uint32_t Test_HandleRequest_VoiceGuidanceSettings_RDK8();
extern uint32_t Test_HandleRequest_AccessibilityVoiceGuidance();
extern uint32_t Test_HandleRequest_AccessibilityAudioDescriptionSettings();
extern uint32_t Test_HandleRequest_AccessibilityAudioDescription();
extern uint32_t Test_HandleRequest_AudioDescriptionsEnabled();
extern uint32_t Test_HandleRequest_AccessibilityHighContrastUI();
extern uint32_t Test_HandleRequest_ClosedCaptionsEnabled();
extern uint32_t Test_HandleRequest_ClosedCaptionsPreferredLanguages();
extern uint32_t Test_HandleRequest_AccessibilityClosedCaptions();
extern uint32_t Test_HandleRequest_AccessibilityClosedCaptionsSettings();
extern uint32_t Test_HandleRequest_LocalizationLanguage();
extern uint32_t Test_HandleRequest_LocalizationLocale();
extern uint32_t Test_HandleRequest_LocalizationPreferredAudioLanguages();
extern uint32_t Test_HandleRequest_LocalizationCountryCode();
extern uint32_t Test_HandleRequest_LocalizationTimezone();
extern uint32_t Test_HandleRequest_SecondScreenFriendlyName();
extern uint32_t Test_HandleRequest_LocalizationAddAdditionalInfo();
extern uint32_t Test_HandleRequest_LifecycleState();
extern uint32_t Test_HandleRequest_LifecycleClose();
extern uint32_t Test_HandleRequest_LifecycleFinished();
extern uint32_t Test_HandleRequest_Lifecycle2State();
extern uint32_t Test_HandleRequest_Lifecycle2Close();
extern uint32_t Test_HandleRequest_DispatchIntent();
extern uint32_t Test_HandleRequest_GetLastIntent();
extern uint32_t Test_HandleRequest_ActionsStart_NoPlugin();
extern uint32_t Test_HandleRequest_ActionsStart_EmptyPayload();
extern uint32_t Test_HandleRequest_ActionsIntent_EmptyRegistry();
extern uint32_t Test_HandleRequest_AdvertisingId();
extern uint32_t Test_HandleRequest_DeviceUid();
extern uint32_t Test_HandleRequest_NetworkConnected();

// AppGatewayCommon_setters_test.cpp
extern uint32_t Test_HandleRequest_SetterInvalidPayload();
extern uint32_t Test_HandleRequest_SetterValidPayload_DelegateUnavailable();
extern uint32_t Test_HandleRequest_BoolSetterInvalidPayload();
extern uint32_t Test_HandleRequest_SetCountryCode_InvalidPayload();
extern uint32_t Test_HandleRequest_SetCountryCode_ValidPayload();
extern uint32_t Test_HandleRequest_SetTimezone_InvalidPayload();
extern uint32_t Test_HandleRequest_SetTimezone_ValidPayload();
extern uint32_t Test_HandleRequest_SetLocale_InvalidPayload();
extern uint32_t Test_HandleRequest_SetLocale_ValidPayload();
extern uint32_t Test_HandleRequest_VoiceGuidanceSetEnabled_Valid();
extern uint32_t Test_HandleRequest_VoiceGuidanceSpeed();
extern uint32_t Test_HandleRequest_VoiceGuidanceRate();
extern uint32_t Test_HandleRequest_SetSpeed_MinBoundary();
extern uint32_t Test_HandleRequest_SetSpeed_MaxBoundary();
extern uint32_t Test_HandleRequest_SetSpeed_BelowMin();
extern uint32_t Test_HandleRequest_SetSpeed_AboveMax();
extern uint32_t Test_HandleRequest_SetRate_Alias();
extern uint32_t Test_HandleRequest_SetNavigationHints_Invalid();
extern uint32_t Test_HandleRequest_SetNavigationHints_Valid();
extern uint32_t Test_HandleRequest_AudioDescSetEnabled_Invalid();
extern uint32_t Test_HandleRequest_AudioDescSetEnabled_Valid();
extern uint32_t Test_HandleRequest_CCSetEnabled_Invalid();
extern uint32_t Test_HandleRequest_CCSetEnabled_Valid();
extern uint32_t Test_HandleRequest_CCSetPrefLangs_Invalid();
extern uint32_t Test_HandleRequest_CCSetPrefLangs_ValidString();
extern uint32_t Test_HandleRequest_CCSetPrefLangs_ValidArray();
extern uint32_t Test_HandleRequest_SetPrefAudioLangs_Invalid();
extern uint32_t Test_HandleRequest_SetPrefAudioLangs_ValidString();
extern uint32_t Test_HandleRequest_SetPrefAudioLangs_ValidArray();

// AppGatewayCommon_routing_test.cpp (presentation.focused — AGC_L0_098–AGC_L0_100)
extern uint32_t Test_HandleRequest_PresentationFocused_Routed();
extern uint32_t Test_HandleRequest_PresentationFocused_ResultIsBooleanString();
extern uint32_t Test_HandleRequest_PresentationFocused_CaseInsensitive();

// AppGatewayCommon_routing_test.cpp (texttospeech.speak tests)
extern uint32_t Test_HandleRequest_TextToSpeechSpeak_ValidPayload();
extern uint32_t Test_HandleRequest_TextToSpeechSpeak_EmptyPayload();
extern uint32_t Test_HandleRequest_TextToSpeechSpeak_NullPayload();
extern uint32_t Test_HandleRequest_TextToSpeechSpeak_InvalidJSON();
extern uint32_t Test_HandleRequest_TextToSpeechSpeak_EmptyText();
extern uint32_t Test_HandleRequest_TextToSpeechSpeak_EmptyCallsign();
extern uint32_t Test_HandleRequest_TextToSpeechSpeak_CaseInsensitive();

// AppGatewayCommon_routing_test.cpp (ParentalControl getters — AGC_L0_101–AGC_L0_103)
extern uint32_t Test_HandleRequest_ParentalControl_PinControl();
extern uint32_t Test_HandleRequest_ParentalControl_BlockNotRatedContent();
extern uint32_t Test_HandleRequest_ParentalControl_ViewingRestrictions();

// AppGatewayCommon_routing_test.cpp (actions.start null/empty intent — AGC_L0_108–AGC_L0_109)
extern uint32_t Test_HandleRequest_ActionsStart_NullIntentField();
extern uint32_t Test_HandleRequest_ActionsStart_EmptyObjectIntentField();

// AppGatewayCommon_events_test.cpp
extern uint32_t Test_CheckPermissionGroup_DefaultAllowed();
extern uint32_t Test_Authenticate_DelegateUnavailable();
extern uint32_t Test_GetSessionId_DelegateUnavailable();
extern uint32_t Test_HandleAppEventNotifier_NullCb();
extern uint32_t Test_HandleAppEventNotifier_ValidCb();
extern uint32_t Test_HandleAppEventNotifier_BeforeInit();
// Gap 1–7 delegate event subscription contract tests
extern uint32_t Test_HandleAppEventNotifier_UnrecognizedEvent();
extern uint32_t Test_HandleAppEventNotifier_NetworkEvent_ListenTrue();
extern uint32_t Test_HandleAppEventNotifier_UserSettingsEvent_ListenTrue();
extern uint32_t Test_HandleAppEventNotifier_TTSEvent_ListenTrue();
extern uint32_t Test_HandleAppEventNotifier_SystemDeviceEvent();
extern uint32_t Test_HandleAppEventNotifier_NetworkEvent_UnsubscribeOnly();
extern uint32_t Test_HandleAppEventNotifier_VideoOutputResolutionEvent_ListenTrue();
extern uint32_t Test_HandleAppEventNotifier_VideoOutputHdcpEvent_ListenTrue();
extern uint32_t Test_HandleAppEventNotifier_VideoOutputCecActiveStateEvent_ListenTrue();
extern uint32_t Test_HandleAppEventNotifier_VideoOutputPortEvent_ListenTrue();
extern uint32_t Test_HandleAppEventNotifier_VideoOutputTeardownAfterSubscriptions_NoCrash();
extern uint32_t Test_HandleAppEventNotifier_VideoOutputResolutionOnlyTeardown_NoCrash();
extern uint32_t Test_HandleAppEventNotifier_VideoOutputPortOnlyTeardown_NoCrash();
extern uint32_t Test_HandleAppEventNotifier_VideoOutputHdcpOnlyTeardown_NoCrash();
extern uint32_t Test_HandleAppEventNotifier_VideoOutputCecOnlyTeardown_NoCrash();
extern uint32_t Test_HandleAppEventNotifier_DolbyAtmosEvent_ListenTrue();

// AppGatewayCommon_display_test.cpp
extern uint32_t Test_HandleRequest_DisplayEdid_NoDisplay();
extern uint32_t Test_HandleRequest_DisplayEdid_CaseInsensitive();
extern uint32_t Test_HandleRequest_DisplaySize_NoDisplay();
extern uint32_t Test_HandleRequest_DisplaySize_CaseInsensitive();
extern uint32_t Test_HandleRequest_DisplayMaxResolution_NoDisplay();
extern uint32_t Test_HandleRequest_DisplayMaxResolution_CaseInsensitive();
extern uint32_t Test_HandleRequest_DisplayColorimetry_NoDisplay();
extern uint32_t Test_HandleRequest_DisplayColorimetry_CaseInsensitive();
extern uint32_t Test_HandleRequest_DisplayVideoResolutions_NoDisplay();
extern uint32_t Test_HandleRequest_DisplayVideoResolutions_CaseInsensitive();

int main()
{
    // Test-only bootstrap for WorkerPool.
    // Must be constructed before any plugin Initialize() calls.
    L0Test::L0BootstrapGuard bootstrap;

    struct Case {
        const char* name;
        uint32_t (*fn)();
    };

    const Case cases[] = {
        // --- Lifecycle tests (AGC_L0_001–AGC_L0_011) ---
        { "Initialize_Deinitialize_Lifecycle",           Test_Initialize_Deinitialize_Lifecycle },
        { "Initialize_Twice_Idempotent",                 Test_Initialize_Twice_Idempotent },
        { "Deinitialize_Twice_NoCrash",                  Test_Deinitialize_Twice_NoCrash },
        { "Information_ReturnsEmpty",                     Test_Information_ReturnsEmpty },
        { "InterfaceMap_RequestHandler",                  Test_InterfaceMap_RequestHandler },
        { "InterfaceMap_Authenticator",                   Test_InterfaceMap_Authenticator },
        { "InterfaceMap_NotificationHandler",             Test_InterfaceMap_NotificationHandler },
        { "HandleRequest_BeforeInit",                     Test_HandleRequest_BeforeInit },
        { "Authenticate_BeforeInit",                      Test_Authenticate_BeforeInit },
        { "HandleRequest_AfterDeinit",                    Test_HandleRequest_AfterDeinit },
        { "Reinitialize_AfterDeinit",                     Test_Reinitialize_AfterDeinit },
        // --- Routing tests (AGC_L0_012–AGC_L0_056) ---
        { "HandleRequest_UnknownMethod",                  Test_HandleRequest_UnknownMethod },
        { "HandleRequest_DeviceMake_DelegateUnavailable", Test_HandleRequest_DeviceMake_DelegateUnavailable },
        { "HandleRequest_MetricsPassthrough",             Test_HandleRequest_MetricsPassthrough },
        { "HandleRequest_DiscoveryWatched",               Test_HandleRequest_DiscoveryWatched },
        { "HandleRequest_CaseInsensitiveMethod",          Test_HandleRequest_CaseInsensitiveMethod },
        { "HandleRequest_LifecycleReady",                 Test_HandleRequest_LifecycleReady },
        { "HandleRequest_DeviceName",                     Test_HandleRequest_DeviceName },
        { "HandleRequest_DeviceSku",                      Test_HandleRequest_DeviceSku },
        { "HandleRequest_DeviceNetwork",                  Test_HandleRequest_DeviceNetwork },
        { "HandleRequest_DeviceVersion",                  Test_HandleRequest_DeviceVersion },
        { "HandleRequest_DeviceScreenResolution",         Test_HandleRequest_DeviceScreenResolution },
        { "HandleRequest_DeviceVideoResolution",          Test_HandleRequest_DeviceVideoResolution },
        { "HandleRequest_DeviceHdcp",                     Test_HandleRequest_DeviceHdcp },
        { "HandleRequest_DeviceHdr",                      Test_HandleRequest_DeviceHdr },
        { "HandleRequest_DeviceAudio",                    Test_HandleRequest_DeviceAudio },
        { "HandleRequest_VideoOutputResolution_NoDisplay", Test_HandleRequest_VideoOutputResolution_NoDisplay },
        { "HandleRequest_VideoOutputHdcp_NoDisplay",       Test_HandleRequest_VideoOutputHdcp_NoDisplay },
        { "HandleRequest_VideoOutputCecActiveState_NoDisplay", Test_HandleRequest_VideoOutputCecActiveState_NoDisplay },
        { "HandleRequest_VideoOutputPort_NoDisplay",       Test_HandleRequest_VideoOutputPort_NoDisplay },
        { "HandleRequest_VideoOutputResolution",            Test_HandleRequest_VideoOutputResolution },
        { "HandleRequest_VideoOutputHdcp",                  Test_HandleRequest_VideoOutputHdcp },
        { "HandleRequest_VideoOutputCecActiveState",        Test_HandleRequest_VideoOutputCecActiveState },
        { "HandleRequest_VideoOutputPort",                  Test_HandleRequest_VideoOutputPort },
        { "HandleRequest_VideoOutputRefreshRate",           Test_HandleRequest_VideoOutputRefreshRate },
        { "HandleRequest_VideoOutputColorDepth",            Test_HandleRequest_VideoOutputColorDepth },
        { "HandleRequest_VideoOutputColorFormat",           Test_HandleRequest_VideoOutputColorFormat },
        { "HandleRequest_VideoOutputColorimetry",           Test_HandleRequest_VideoOutputColorimetry },
        { "HandleRequest_VideoOutputDynamicRange",          Test_HandleRequest_VideoOutputDynamicRange },
        { "HandleRequest_VideoOutputQuantizationRange",     Test_HandleRequest_VideoOutputQuantizationRange },
        { "HandleRequest_DeviceDolbyAtmosExperience",     Test_HandleRequest_DeviceDolbyAtmosExperience },
        { "HandleRequest_VoiceGuidanceEnabled",           Test_HandleRequest_VoiceGuidanceEnabled },
        { "HandleRequest_VoiceGuidanceNavigationHints",   Test_HandleRequest_VoiceGuidanceNavigationHints },
        { "HandleRequest_VoiceGuidanceSettings_NonRDK8",  Test_HandleRequest_VoiceGuidanceSettings_NonRDK8 },
        { "HandleRequest_VoiceGuidanceSettings_RDK8",     Test_HandleRequest_VoiceGuidanceSettings_RDK8 },
        { "HandleRequest_AccessibilityVoiceGuidance",     Test_HandleRequest_AccessibilityVoiceGuidance },
        { "HandleRequest_AccessibilityAudioDescSettings", Test_HandleRequest_AccessibilityAudioDescriptionSettings },
        { "HandleRequest_AccessibilityAudioDescription",  Test_HandleRequest_AccessibilityAudioDescription },
        { "HandleRequest_AudioDescriptionsEnabled",       Test_HandleRequest_AudioDescriptionsEnabled },
        { "HandleRequest_AccessibilityHighContrastUI",    Test_HandleRequest_AccessibilityHighContrastUI },
        { "HandleRequest_ClosedCaptionsEnabled",          Test_HandleRequest_ClosedCaptionsEnabled },
        { "HandleRequest_ClosedCaptionsPreferredLangs",   Test_HandleRequest_ClosedCaptionsPreferredLanguages },
        { "HandleRequest_AccessibilityClosedCaptions",    Test_HandleRequest_AccessibilityClosedCaptions },
        { "HandleRequest_AccessibilityClosedCaptSettings",Test_HandleRequest_AccessibilityClosedCaptionsSettings },
        { "HandleRequest_LocalizationLanguage",           Test_HandleRequest_LocalizationLanguage },
        { "HandleRequest_LocalizationLocale",             Test_HandleRequest_LocalizationLocale },
        { "HandleRequest_LocalizationPreferredAudioLangs",Test_HandleRequest_LocalizationPreferredAudioLanguages },
        { "HandleRequest_LocalizationCountryCode",        Test_HandleRequest_LocalizationCountryCode },
        { "HandleRequest_LocalizationTimezone",           Test_HandleRequest_LocalizationTimezone },
        { "HandleRequest_SecondScreenFriendlyName",       Test_HandleRequest_SecondScreenFriendlyName },
        { "HandleRequest_LocalizationAddAdditionalInfo",  Test_HandleRequest_LocalizationAddAdditionalInfo },
        { "HandleRequest_LifecycleState",                 Test_HandleRequest_LifecycleState },
        { "HandleRequest_LifecycleClose",                 Test_HandleRequest_LifecycleClose },
        { "HandleRequest_LifecycleFinished",              Test_HandleRequest_LifecycleFinished },
        { "HandleRequest_Lifecycle2State",                Test_HandleRequest_Lifecycle2State },
        { "HandleRequest_Lifecycle2Close",                Test_HandleRequest_Lifecycle2Close },
        { "HandleRequest_DispatchIntent",                 Test_HandleRequest_DispatchIntent },
        { "HandleRequest_GetLastIntent",                 Test_HandleRequest_GetLastIntent },
        { "HandleRequest_ActionsStart_NoPlugin",          Test_HandleRequest_ActionsStart_NoPlugin },
        { "HandleRequest_ActionsStart_EmptyPayload",      Test_HandleRequest_ActionsStart_EmptyPayload },
        { "HandleRequest_ActionsIntent_EmptyRegistry",    Test_HandleRequest_ActionsIntent_EmptyRegistry },
        { "HandleRequest_AdvertisingId",                  Test_HandleRequest_AdvertisingId },
        { "HandleRequest_DeviceUid",                      Test_HandleRequest_DeviceUid },
        { "HandleRequest_NetworkConnected",               Test_HandleRequest_NetworkConnected },
        // --- presentation.focused tests (AGC_L0_098–AGC_L0_100) ---
        { "HandleRequest_PresentationFocused_Routed",             Test_HandleRequest_PresentationFocused_Routed },
        { "HandleRequest_PresentationFocused_ResultIsBooleanStr", Test_HandleRequest_PresentationFocused_ResultIsBooleanString },
        { "HandleRequest_PresentationFocused_CaseInsensitive",    Test_HandleRequest_PresentationFocused_CaseInsensitive },
        // --- texttospeech.speak tests ---
        { "HandleRequest_TextToSpeechSpeak_ValidPayload",        Test_HandleRequest_TextToSpeechSpeak_ValidPayload },
        { "HandleRequest_TextToSpeechSpeak_EmptyPayload",        Test_HandleRequest_TextToSpeechSpeak_EmptyPayload },
        { "HandleRequest_TextToSpeechSpeak_NullPayload",         Test_HandleRequest_TextToSpeechSpeak_NullPayload },
        { "HandleRequest_TextToSpeechSpeak_InvalidJSON",        Test_HandleRequest_TextToSpeechSpeak_InvalidJSON },
        { "HandleRequest_TextToSpeechSpeak_EmptyText",          Test_HandleRequest_TextToSpeechSpeak_EmptyText },
        { "HandleRequest_TextToSpeechSpeak_EmptyCallsign",       Test_HandleRequest_TextToSpeechSpeak_EmptyCallsign },
        { "HandleRequest_TextToSpeechSpeak_CaseInsensitive",     Test_HandleRequest_TextToSpeechSpeak_CaseInsensitive },
        // --- ParentalControl getter tests (AGC_L0_101–AGC_L0_103) ---
        { "HandleRequest_ParentalControl_PinControl",             Test_HandleRequest_ParentalControl_PinControl },
        { "HandleRequest_ParentalControl_BlockNotRatedContent",   Test_HandleRequest_ParentalControl_BlockNotRatedContent },
        { "HandleRequest_ParentalControl_ViewingRestrictions",    Test_HandleRequest_ParentalControl_ViewingRestrictions },
        // --- actions.start null/empty intent tests (AGC_L0_108–AGC_L0_109) ---
        { "HandleRequest_ActionsStart_NullIntentField",           Test_HandleRequest_ActionsStart_NullIntentField },
        { "HandleRequest_ActionsStart_EmptyObjectIntentField",    Test_HandleRequest_ActionsStart_EmptyObjectIntentField },
        // --- Setters tests (AGC_L0_057–AGC_L0_085) ---
        { "HandleRequest_SetterInvalidPayload",           Test_HandleRequest_SetterInvalidPayload },
        { "HandleRequest_SetterValidPayload_DelegateUnavailable", Test_HandleRequest_SetterValidPayload_DelegateUnavailable },
        { "HandleRequest_BoolSetterInvalidPayload",       Test_HandleRequest_BoolSetterInvalidPayload },
        { "SetCountryCode_InvalidPayload",                Test_HandleRequest_SetCountryCode_InvalidPayload },
        { "SetCountryCode_ValidPayload",                  Test_HandleRequest_SetCountryCode_ValidPayload },
        { "SetTimezone_InvalidPayload",                   Test_HandleRequest_SetTimezone_InvalidPayload },
        { "SetTimezone_ValidPayload",                     Test_HandleRequest_SetTimezone_ValidPayload },
        { "SetLocale_InvalidPayload",                     Test_HandleRequest_SetLocale_InvalidPayload },
        { "SetLocale_ValidPayload",                       Test_HandleRequest_SetLocale_ValidPayload },
        { "VoiceGuidanceSetEnabled_Valid",                Test_HandleRequest_VoiceGuidanceSetEnabled_Valid },
        { "VoiceGuidanceSpeed",                           Test_HandleRequest_VoiceGuidanceSpeed },
        { "VoiceGuidanceRate",                            Test_HandleRequest_VoiceGuidanceRate },
        { "SetSpeed_MinBoundary",                         Test_HandleRequest_SetSpeed_MinBoundary },
        { "SetSpeed_MaxBoundary",                         Test_HandleRequest_SetSpeed_MaxBoundary },
        { "SetSpeed_BelowMin",                            Test_HandleRequest_SetSpeed_BelowMin },
        { "SetSpeed_AboveMax",                            Test_HandleRequest_SetSpeed_AboveMax },
        { "SetRate_Alias",                                Test_HandleRequest_SetRate_Alias },
        { "SetNavigationHints_Invalid",                   Test_HandleRequest_SetNavigationHints_Invalid },
        { "SetNavigationHints_Valid",                     Test_HandleRequest_SetNavigationHints_Valid },
        { "AudioDescSetEnabled_Invalid",                  Test_HandleRequest_AudioDescSetEnabled_Invalid },
        { "AudioDescSetEnabled_Valid",                    Test_HandleRequest_AudioDescSetEnabled_Valid },
        { "CCSetEnabled_Invalid",                         Test_HandleRequest_CCSetEnabled_Invalid },
        { "CCSetEnabled_Valid",                            Test_HandleRequest_CCSetEnabled_Valid },
        { "CCSetPrefLangs_Invalid",                       Test_HandleRequest_CCSetPrefLangs_Invalid },
        { "CCSetPrefLangs_ValidString",                   Test_HandleRequest_CCSetPrefLangs_ValidString },
        { "CCSetPrefLangs_ValidArray",                    Test_HandleRequest_CCSetPrefLangs_ValidArray },
        { "SetPrefAudioLangs_Invalid",                    Test_HandleRequest_SetPrefAudioLangs_Invalid },
        { "SetPrefAudioLangs_ValidString",                Test_HandleRequest_SetPrefAudioLangs_ValidString },
        { "SetPrefAudioLangs_ValidArray",                 Test_HandleRequest_SetPrefAudioLangs_ValidArray },
        // --- Events tests (AGC_L0_086–AGC_L0_097) ---
        { "CheckPermissionGroup_DefaultAllowed",          Test_CheckPermissionGroup_DefaultAllowed },
        { "Authenticate_DelegateUnavailable",             Test_Authenticate_DelegateUnavailable },
        { "GetSessionId_DelegateUnavailable",             Test_GetSessionId_DelegateUnavailable },
        { "HandleAppEventNotifier_NullCb",                Test_HandleAppEventNotifier_NullCb },
        { "HandleAppEventNotifier_ValidCb",               Test_HandleAppEventNotifier_ValidCb },
        { "HandleAppEventNotifier_BeforeInit",            Test_HandleAppEventNotifier_BeforeInit },
        { "EventNotifier_UnrecognizedEvent",              Test_HandleAppEventNotifier_UnrecognizedEvent },
        { "EventNotifier_NetworkEvent_ListenTrue",        Test_HandleAppEventNotifier_NetworkEvent_ListenTrue },
        { "EventNotifier_UserSettingsEvent_ListenTrue",   Test_HandleAppEventNotifier_UserSettingsEvent_ListenTrue },
        { "EventNotifier_TTSEvent_ListenTrue",            Test_HandleAppEventNotifier_TTSEvent_ListenTrue },
        { "EventNotifier_SystemDeviceEvent",              Test_HandleAppEventNotifier_SystemDeviceEvent },
        { "EventNotifier_NetworkEvent_UnsubscribeOnly",   Test_HandleAppEventNotifier_NetworkEvent_UnsubscribeOnly },
        { "EventNotifier_VOResolution_ListenTrue",          Test_HandleAppEventNotifier_VideoOutputResolutionEvent_ListenTrue },
        { "EventNotifier_VOHdcp_ListenTrue",                Test_HandleAppEventNotifier_VideoOutputHdcpEvent_ListenTrue },
        { "EventNotifier_VOCecActiveState_ListenTrue",      Test_HandleAppEventNotifier_VideoOutputCecActiveStateEvent_ListenTrue },
        { "EventNotifier_VOPort_ListenTrue",                Test_HandleAppEventNotifier_VideoOutputPortEvent_ListenTrue },
        { "EventNotifier_VOTeardownAfterSubscriptions_NoCrash", Test_HandleAppEventNotifier_VideoOutputTeardownAfterSubscriptions_NoCrash },
        { "EventNotifier_VOResolutionOnlyTeardown_NoCrash", Test_HandleAppEventNotifier_VideoOutputResolutionOnlyTeardown_NoCrash },
        { "EventNotifier_VOPortOnlyTeardown_NoCrash", Test_HandleAppEventNotifier_VideoOutputPortOnlyTeardown_NoCrash },
        { "EventNotifier_VOHdcpOnlyTeardown_NoCrash", Test_HandleAppEventNotifier_VideoOutputHdcpOnlyTeardown_NoCrash },
        { "EventNotifier_VOCecOnlyTeardown_NoCrash", Test_HandleAppEventNotifier_VideoOutputCecOnlyTeardown_NoCrash },
        { "EventNotifier_DolbyAtmosEvent_ListenTrue",     Test_HandleAppEventNotifier_DolbyAtmosEvent_ListenTrue },
        // --- Display tests (AGC_L0_098–AGC_L0_107) ---
        { "DisplayEdid_NoDisplay",                        Test_HandleRequest_DisplayEdid_NoDisplay },
        { "DisplayEdid_CaseInsensitive",                  Test_HandleRequest_DisplayEdid_CaseInsensitive },
        { "DisplaySize_NoDisplay",                        Test_HandleRequest_DisplaySize_NoDisplay },
        { "DisplaySize_CaseInsensitive",                  Test_HandleRequest_DisplaySize_CaseInsensitive },
        { "DisplayMaxResolution_NoDisplay",               Test_HandleRequest_DisplayMaxResolution_NoDisplay },
        { "DisplayMaxResolution_CaseInsensitive",         Test_HandleRequest_DisplayMaxResolution_CaseInsensitive },
        { "DisplayColorimetry_NoDisplay",                 Test_HandleRequest_DisplayColorimetry_NoDisplay },
        { "DisplayColorimetry_CaseInsensitive",           Test_HandleRequest_DisplayColorimetry_CaseInsensitive },
        { "DisplayVideoResolutions_NoDisplay",            Test_HandleRequest_DisplayVideoResolutions_NoDisplay },
        { "DisplayVideoResolutions_CaseInsensitive",      Test_HandleRequest_DisplayVideoResolutions_CaseInsensitive },
    };

    uint32_t failures = 0;

    for (const auto& c : cases) {
        std::cerr << "[ RUN      ] " << c.name << std::endl;
        const uint32_t f = c.fn();
        if (0 == f) {
            std::cerr << "[       OK ] " << c.name << std::endl;
        } else {
            std::cerr << "[  FAILED  ] " << c.name << " failures=" << f << std::endl;
        }
        failures += f;
    }

    PrintTotals(std::cerr, "AppGatewayCommon l0test", failures);

    // Clean up the shared plugin fixture (single init/deinit for all non-lifecycle tests).
    AGCTest::SharedFixture::instance().cleanup();

    return ResultToExitCode(failures);
}
