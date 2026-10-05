/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>

#include "Module.h"

#define private public
#include "AppGatewayCommon.h"
#undef private

#include "ServiceMock.h"
#include "UserSettingMock.h"
#include "ThunderPortability.h"
#include "WorkerPoolImplementation.h"

using namespace WPEFramework;
using namespace WPEFramework::Plugin;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgReferee;

namespace {

class WorkerPoolGuard final {
public:
    WorkerPoolGuard(const WorkerPoolGuard&) = delete;
    WorkerPoolGuard& operator=(const WorkerPoolGuard&) = delete;

    WorkerPoolGuard()
        : mPool(2, 0, 64)
        , mAssigned(false)
    {
        if (Core::IWorkerPool::IsAvailable() == false) {
            Core::IWorkerPool::Assign(&mPool);
            mAssigned = true;
            mPool.Run();
        }
    }

    ~WorkerPoolGuard()
    {
        if (mAssigned) {
            mPool.Stop();
            Core::IWorkerPool::Assign(nullptr);
        }
    }

private:
    WorkerPoolImplementation mPool;
    bool mAssigned;
};

static WorkerPoolGuard gWorkerPool;

static Exchange::GatewayContext MakeContext()
{
    Exchange::GatewayContext ctx;
    ctx.appId = "test.app";
    ctx.connectionId = 100;
    ctx.requestId = 200;
    return ctx;
}

class UserSettingsTest : public ::testing::Test {
protected:
    // Shared across all tests in this suite — initialized once in SetUpTestSuite.
    static Core::Sink<AppGatewayCommon>* sPlugin;
    static NiceMock<ServiceMock>* sService;
    static NiceMock<UserSettingMock>* sMockUserSettings;

    // Convenience references so existing TEST_F bodies compile unchanged.
    Core::Sink<AppGatewayCommon>& plugin = *sPlugin;
    NiceMock<ServiceMock>& service = *sService;
    NiceMock<UserSettingMock>& mockUserSettings = *sMockUserSettings;

    static void SetUpTestSuite()
    {
        sService = new NiceMock<ServiceMock>();
        sMockUserSettings = new NiceMock<UserSettingMock>();
        sPlugin = new Core::Sink<AppGatewayCommon>();

        ON_CALL(*sService, QueryInterfaceByCallsign(_, _))
            .WillByDefault(Return(nullptr));

        ON_CALL(*sService, QueryInterfaceByCallsign(Exchange::IUserSettings::ID, ::testing::StrEq("org.rdk.UserSettings")))
            .WillByDefault(::testing::Invoke([](uint32_t, const string&) -> void* {
                sMockUserSettings->AddRef();
                return static_cast<Exchange::IUserSettings*>(sMockUserSettings);
            }));

        EXPECT_CALL(*sService, AddRef()).Times(AnyNumber());
        EXPECT_CALL(*sService, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
        EXPECT_CALL(*sMockUserSettings, Register(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
        EXPECT_CALL(*sMockUserSettings, Unregister(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));

        const string response = sPlugin->Initialize(sService);
        ASSERT_TRUE(response.empty());
    }

    static void TearDownTestSuite()
    {
        if (sPlugin && sService) {
            sPlugin->Deinitialize(sService);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        delete sPlugin;       sPlugin = nullptr;
        delete sMockUserSettings; sMockUserSettings = nullptr;
        delete sService;       sService = nullptr;
    }

    void TearDown() override
    {
        // Clear per-test EXPECT_CALL expectations; ON_CALL defaults survive.
        ::testing::Mock::VerifyAndClearExpectations(sMockUserSettings);
        // Re-establish suite-wide expectations cleared by VerifyAndClear.
        EXPECT_CALL(*sMockUserSettings, Register(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
        EXPECT_CALL(*sMockUserSettings, Unregister(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    }
};

Core::Sink<AppGatewayCommon>* UserSettingsTest::sPlugin = nullptr;
NiceMock<ServiceMock>* UserSettingsTest::sService = nullptr;
NiceMock<UserSettingMock>* UserSettingsTest::sMockUserSettings = nullptr;

/* ---------- Voice Guidance ---------- */

TEST_F(UserSettingsTest, AGC_L1_027_GetVoiceGuidance_Success)
{
    EXPECT_CALL(mockUserSettings, GetVoiceGuidance(_))
        .WillOnce(DoAll(SetArgReferee<0>(true), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.enabled", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("true", result);
}

TEST_F(UserSettingsTest, AGC_L1_028_SetVoiceGuidance_Success)
{
    EXPECT_CALL(mockUserSettings, SetVoiceGuidance(true))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.setenabled", R"({"value":true})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

/* ---------- Audio Descriptions ---------- */

TEST_F(UserSettingsTest, AGC_L1_029_GetAudioDescriptionsEnabled_Success)
{
    EXPECT_CALL(mockUserSettings, GetAudioDescription(_))
        .WillOnce(DoAll(SetArgReferee<0>(true), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "audiodescriptions.enabled", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("true", result);
}

TEST_F(UserSettingsTest, AGC_L1_030_SetAudioDescriptionsEnabled_Success)
{
    EXPECT_CALL(mockUserSettings, SetAudioDescription(true))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "audiodescriptions.setenabled", R"({"value":true})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

/* ---------- Captions ---------- */

TEST_F(UserSettingsTest, AGC_L1_031_GetCaptions_Success)
{
    EXPECT_CALL(mockUserSettings, GetCaptions(_))
        .WillOnce(DoAll(SetArgReferee<0>(true), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "closedcaptions.enabled", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("true", result);
}

TEST_F(UserSettingsTest, AGC_L1_032_SetCaptions_Success)
{
    EXPECT_CALL(mockUserSettings, SetCaptions(false))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "closedcaptions.setenabled", R"({"value":false})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

/* ---------- Locale ---------- */

TEST_F(UserSettingsTest, AGC_L1_033_GetLocale_Success)
{
    EXPECT_CALL(mockUserSettings, GetPresentationLanguage(_))
        .WillOnce(DoAll(SetArgReferee<0>("en-US"), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.locale", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("en"), std::string::npos);
}

TEST_F(UserSettingsTest, AGC_L1_034_SetLocale_Success)
{
    EXPECT_CALL(mockUserSettings, SetPresentationLanguage("en-US"))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.setlocale", R"({"value":"en-US"})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

/* ---------- Languages ---------- */

TEST_F(UserSettingsTest, AGC_L1_035_GetPreferredAudioLanguages_Success)
{
    EXPECT_CALL(mockUserSettings, GetPreferredAudioLanguages(_))
        .WillOnce(DoAll(SetArgReferee<0>("eng,fra"), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.preferredaudiolanguages", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("eng"), std::string::npos);
}

TEST_F(UserSettingsTest, AGC_L1_036_SetPreferredAudioLanguages_Success)
{
    EXPECT_CALL(mockUserSettings, SetPreferredAudioLanguages(_))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.setpreferredaudiolanguages", R"({"value":["eng","fra"]})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

TEST_F(UserSettingsTest, AGC_L1_037_GetPreferredCaptionsLanguages_Success)
{
    EXPECT_CALL(mockUserSettings, GetPreferredCaptionsLanguages(_))
        .WillOnce(DoAll(SetArgReferee<0>("eng"), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "closedcaptions.preferredlanguages", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("eng"), std::string::npos);
}

TEST_F(UserSettingsTest, AGC_L1_038_SetPreferredCaptionsLanguages_Success)
{
    EXPECT_CALL(mockUserSettings, SetPreferredCaptionsLanguages(_))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "closedcaptions.setpreferredlanguages", R"({"value":"eng"})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

/* ---------- Presentation Language ---------- */

TEST_F(UserSettingsTest, AGC_L1_039_GetPresentationLanguage_Success)
{
    EXPECT_CALL(mockUserSettings, GetPresentationLanguage(_))
        .WillOnce(DoAll(SetArgReferee<0>("en-US"), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.language", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    // UserSettingsDelegate splits "en-US" at dash -> "en"
    EXPECT_NE(result.find("en"), std::string::npos);
}

TEST_F(UserSettingsTest, AGC_L1_039A_GetPresentationLanguage_FireboltReturnsFullBcp47Value)
{
    EXPECT_CALL(mockUserSettings, GetPresentationLanguage(_))
        .WillOnce(DoAll(SetArgReferee<0>("en-US"), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.presentationlanguage", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("\"en-US\"", result);
}

/* ---------- High Contrast ---------- */

TEST_F(UserSettingsTest, AGC_L1_040_GetHighContrast_Success)
{
    EXPECT_CALL(mockUserSettings, GetHighContrast(_))
        .WillOnce(DoAll(SetArgReferee<0>(false), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "accessibility.highcontrastui", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("false", result);
}

/* ---------- Voice Guidance Hints ---------- */

TEST_F(UserSettingsTest, AGC_L1_041_GetVoiceGuidanceHints_Success)
{
    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceHints(_))
        .WillOnce(DoAll(SetArgReferee<0>(true), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.navigationhints", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("true", result);
}

TEST_F(UserSettingsTest, AGC_L1_042_SetVoiceGuidanceHints_Success)
{
    EXPECT_CALL(mockUserSettings, SetVoiceGuidanceHints(true))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.setnavigationhints", R"({"value":true})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

/* ---------- Speed (Firebolt<->Thunder transform) ---------- */

TEST_F(UserSettingsTest, AGC_L1_043_SetSpeed_2_0_TransformsTo_10_0)
{
    // Number() truncates 2.0 to 2, transform: speed==2 → rate=10.0
    EXPECT_CALL(mockUserSettings, SetVoiceGuidanceRate(10.0))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.setspeed", R"({"value":2.0})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(UserSettingsTest, AGC_L1_044_SetSpeed_1_67_TruncatedTo_1)
{
    // Number() truncates 1.67 to 1, transform: speed>=1.0 → rate=1.0
    EXPECT_CALL(mockUserSettings, SetVoiceGuidanceRate(1.0))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.setspeed", R"({"value":1.67})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(UserSettingsTest, AGC_L1_045_SetSpeed_1_33_TruncatedTo_1)
{
    // Number() truncates 1.33 to 1, transform: speed>=1.0 → rate=1.0
    EXPECT_CALL(mockUserSettings, SetVoiceGuidanceRate(1.0))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.setspeed", R"({"value":1.33})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(UserSettingsTest, AGC_L1_046_SetSpeed_1_0_TransformsTo_1_0)
{
    // Number() truncates 1.0 to 1, transform: speed>=1.0 → rate=1.0
    EXPECT_CALL(mockUserSettings, SetVoiceGuidanceRate(1.0))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.setspeed", R"({"value":1.0})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(UserSettingsTest, AGC_L1_047_SetSpeed_0_5_RejectedAsFloat)
{
    // 0.5 is FLOAT type → rejected by ValidateAndExtractDouble
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.setspeed", R"({"value":0.5})", result);

    EXPECT_EQ(Core::ERROR_BAD_REQUEST, rc);
}

TEST_F(UserSettingsTest, AGC_L1_048_GetSpeed_HighRate_Returns_2_0)
{
    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceRate(_))
        .WillOnce(DoAll(SetArgReferee<0>(1.56), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.speed", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("2", result);
}

TEST_F(UserSettingsTest, AGC_L1_049_GetSpeed_MediumRate_Returns_1_0)
{
    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceRate(_))
        .WillOnce(DoAll(SetArgReferee<0>(1.0), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.speed", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("1", result);
}

TEST_F(UserSettingsTest, AGC_L1_050_GetSpeed_LowRate_Returns_0_5)
{
    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceRate(_))
        .WillOnce(DoAll(SetArgReferee<0>(0.5), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.speed", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("0.5", result);
}

/* ---------- Composite settings ---------- */

TEST_F(UserSettingsTest, AGC_L1_051_GetVoiceGuidanceSettings_Success)
{
    // UpdateVoiceGuidanceSettings calls GetVoiceGuidance, GetVoiceGuidanceRate, GetVoiceGuidanceHints
    EXPECT_CALL(mockUserSettings, GetVoiceGuidance(_))
        .WillOnce(DoAll(SetArgReferee<0>(true), Return(Core::ERROR_NONE)));
    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceRate(_))
        .WillOnce(DoAll(SetArgReferee<0>(1.0), Return(Core::ERROR_NONE)));
    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceHints(_))
        .WillOnce(DoAll(SetArgReferee<0>(true), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "accessibility.voiceguidancesettings", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("enabled"), std::string::npos);
}

TEST_F(UserSettingsTest, AGC_L1_052_GetClosedCaptionsSettings_Success)
{
    // GetClosedCaptionSettings calls GetCaptions, GetPreferredCaptionsLanguages
    EXPECT_CALL(mockUserSettings, GetCaptions(_))
        .WillOnce(DoAll(SetArgReferee<0>(true), Return(Core::ERROR_NONE)));
    EXPECT_CALL(mockUserSettings, GetPreferredCaptionsLanguages(_))
        .WillOnce(DoAll(SetArgReferee<0>("eng"), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "accessibility.closedcaptions", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("enabled"), std::string::npos);
}

/* ---------- accessibility.voiceguidance (addSpeed=true) ---------- */

TEST_F(UserSettingsTest, AGC_L1_053_AccessibilityVoiceGuidance_WithSpeed)
{
    EXPECT_CALL(mockUserSettings, GetVoiceGuidance(_))
        .WillOnce(DoAll(SetArgReferee<0>(true), Return(Core::ERROR_NONE)));
    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceRate(_))
        .WillOnce(DoAll(SetArgReferee<0>(1.0), Return(Core::ERROR_NONE)));
    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceHints(_))
        .WillOnce(DoAll(SetArgReferee<0>(false), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "accessibility.voiceguidance", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    // accessibility.voiceguidance always calls with addSpeed=true
    EXPECT_NE(result.find("\"speed\""), std::string::npos);
    EXPECT_NE(result.find("\"enabled\":true"), std::string::npos);
    EXPECT_NE(result.find("\"rate\""), std::string::npos);
    EXPECT_NE(result.find("\"navigationHints\":false"), std::string::npos);
}

/* ---------- accessibility.audiodescriptionsettings (composite) ---------- */

TEST_F(UserSettingsTest, AGC_L1_054_AudioDescriptionSettings_Success)
{
    EXPECT_CALL(mockUserSettings, GetAudioDescription(_))
        .WillOnce(DoAll(SetArgReferee<0>(true), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "accessibility.audiodescriptionsettings", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    // GetAudioDescription wraps result in {"enabled":...}
    EXPECT_NE(result.find("\"enabled\":true"), std::string::npos);
}

/* ---------- accessibility.closedcaptionssettings (alias) ---------- */

TEST_F(UserSettingsTest, AGC_L1_055_ClosedCaptionsSettingsAlias_Success)
{
    EXPECT_CALL(mockUserSettings, GetCaptions(_))
        .WillOnce(DoAll(SetArgReferee<0>(false), Return(Core::ERROR_NONE)));
    EXPECT_CALL(mockUserSettings, GetPreferredCaptionsLanguages(_))
        .WillOnce(DoAll(SetArgReferee<0>("spa,fra"), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "accessibility.closedcaptionssettings", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("\"enabled\":false"), std::string::npos);
    EXPECT_NE(result.find("preferredLanguages"), std::string::npos);
}

/* ---------- COM-RPC failure paths ---------- */

TEST_F(UserSettingsTest, AGC_L1_056_GetVoiceGuidance_COMFailure)
{
    EXPECT_CALL(mockUserSettings, GetVoiceGuidance(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.enabled", "{}", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("error"), std::string::npos);
}

TEST_F(UserSettingsTest, AGC_L1_057_GetCaptions_COMFailure)
{
    EXPECT_CALL(mockUserSettings, GetCaptions(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "closedcaptions.enabled", "{}", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("error"), std::string::npos);
}

TEST_F(UserSettingsTest, AGC_L1_058_GetHighContrast_COMFailure)
{
    EXPECT_CALL(mockUserSettings, GetHighContrast(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "accessibility.highcontrastui", "{}", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("error"), std::string::npos);
}

TEST_F(UserSettingsTest, AGC_L1_059_SetVoiceGuidance_COMFailure)
{
    EXPECT_CALL(mockUserSettings, SetVoiceGuidance(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.setenabled", R"({"value":true})", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
}

TEST_F(UserSettingsTest, AGC_L1_060_SetCaptions_COMFailure)
{
    EXPECT_CALL(mockUserSettings, SetCaptions(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "closedcaptions.setenabled", R"({"value":false})", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
}

TEST_F(UserSettingsTest, AGC_L1_061_GetSpeed_RateFetchFails)
{
    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceRate(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.speed", "{}", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
}

TEST_F(UserSettingsTest, AGC_L1_062_SetSpeed_RateSetFails)
{
    // Number() truncates 1.5 to 1, transform: speed>=1.0 → rate=1.0
    EXPECT_CALL(mockUserSettings, SetVoiceGuidanceRate(1.0))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.setspeed", R"({"value":1.5})", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
}

TEST_F(UserSettingsTest, AGC_L1_063_VoiceGuidanceSettings_EnabledFetchFails)
{
    // UpdateVoiceGuidanceSettings: if GetVoiceGuidance fails, entire composite fails
    EXPECT_CALL(mockUserSettings, GetVoiceGuidance(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "accessibility.voiceguidancesettings", "{}", result);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
    EXPECT_NE(result.find("voiceguidance"), std::string::npos);
}

TEST_F(UserSettingsTest, AGC_L1_064_ClosedCaptionsSettings_CaptionsFetchFails)
{
    EXPECT_CALL(mockUserSettings, GetCaptions(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "accessibility.closedcaptions", "{}", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("captions"), std::string::npos);
}

TEST_F(UserSettingsTest, AGC_L1_065_GetAudioDescriptionsEnabled_COMFailure)
{
    EXPECT_CALL(mockUserSettings, GetAudioDescription(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "audiodescriptions.enabled", "{}", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("error"), std::string::npos);
}

TEST_F(UserSettingsTest, AGC_L1_066_SetAudioDescriptions_COMFailure)
{
    EXPECT_CALL(mockUserSettings, SetAudioDescription(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "audiodescriptions.setenabled", R"({"value":true})", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
}

TEST_F(UserSettingsTest, AGC_L1_067_GetPresentationLanguage_EmptyResult)
{
    EXPECT_CALL(mockUserSettings, GetPresentationLanguage(_))
        .WillOnce(DoAll(SetArgReferee<0>(""), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.language", "{}", result);

    // Empty presentation language should produce an error
    EXPECT_NE(Core::ERROR_NONE, rc);
}

TEST_F(UserSettingsTest, AGC_L1_068_GetLocale_COMFailure)
{
    EXPECT_CALL(mockUserSettings, GetPresentationLanguage(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.locale", "{}", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("error"), std::string::npos);
}

TEST_F(UserSettingsTest, AGC_L1_069_SetLocale_COMFailure)
{
    EXPECT_CALL(mockUserSettings, SetPresentationLanguage(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.setlocale", R"({"value":"fr-FR"})", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
}

TEST_F(UserSettingsTest, AGC_L1_070_GetPreferredAudioLanguages_COMFailure)
{
    EXPECT_CALL(mockUserSettings, GetPreferredAudioLanguages(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.preferredaudiolanguages", "{}", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
    EXPECT_EQ("[]", result);
}

TEST_F(UserSettingsTest, AGC_L1_071_SetPreferredAudioLanguages_COMFailure)
{
    EXPECT_CALL(mockUserSettings, SetPreferredAudioLanguages(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.setpreferredaudiolanguages", R"({"value":"eng,fra"})", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
}

TEST_F(UserSettingsTest, AGC_L1_072_SetPreferredCaptionsLanguages_COMFailure)
{
    EXPECT_CALL(mockUserSettings, SetPreferredCaptionsLanguages(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "closedcaptions.setpreferredlanguages", R"({"value":"eng,spa"})", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
}

TEST_F(UserSettingsTest, AGC_L1_073_SetVoiceGuidanceHints_COMFailure)
{
    EXPECT_CALL(mockUserSettings, SetVoiceGuidanceHints(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.setnavigationhints", R"({"value":true})", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
}

/* ---------- GetSpeed transform: middle ranges ---------- */

TEST_F(UserSettingsTest, AGC_L1_074_GetSpeed_Rate1_38_Returns_1_67)
{
    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceRate(_))
        .WillOnce(DoAll(SetArgReferee<0>(1.38), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.speed", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("1.67", result);
}

TEST_F(UserSettingsTest, AGC_L1_075_GetSpeed_Rate1_19_Returns_1_33)
{
    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceRate(_))
        .WillOnce(DoAll(SetArgReferee<0>(1.19), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.speed", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("1.33", result);
}

/* ---------- voiceguidance.rate GET alias ---------- */

TEST_F(UserSettingsTest, AGC_L1_076_GetRate_Alias_Success)
{
    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceRate(_))
        .WillOnce(DoAll(SetArgReferee<0>(1.0), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.rate", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("1", result);
}

/* ---------- voiceguidance.setrate SET alias ---------- */

TEST_F(UserSettingsTest, AGC_L1_077_SetRate_Alias_Success)
{
    EXPECT_CALL(mockUserSettings, SetVoiceGuidanceRate(10.0))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "voiceguidance.setrate", R"({"value":2.0})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

/* ---------- accessibility.voiceguidancesettings with RDK8 version (addSpeed=false) ---------- */

TEST_F(UserSettingsTest, AGC_L1_078_VoiceGuidanceSettings_RDK8_NoSpeed)
{
    EXPECT_CALL(mockUserSettings, GetVoiceGuidance(_))
        .WillOnce(DoAll(SetArgReferee<0>(true), Return(Core::ERROR_NONE)));
    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceRate(_))
        .WillOnce(DoAll(SetArgReferee<0>(1.0), Return(Core::ERROR_NONE)));
    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceHints(_))
        .WillOnce(DoAll(SetArgReferee<0>(false), Return(Core::ERROR_NONE)));

    Exchange::GatewayContext ctx;
    ctx.appId = "test.app";
    ctx.connectionId = 100;
    ctx.requestId = 200;
    ctx.version = "8";

    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "accessibility.voiceguidancesettings", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("\"enabled\":true"), std::string::npos);
    EXPECT_NE(result.find("\"rate\""), std::string::npos);
    // RDK8 version → addSpeed=false → no "speed" key
    EXPECT_EQ(result.find("\"speed\""), std::string::npos);
}

/* ---------- accessibility.audiodescription route ---------- */

TEST_F(UserSettingsTest, AGC_L1_079_AccessibilityAudioDescription_Success)
{
    EXPECT_CALL(mockUserSettings, GetAudioDescription(_))
        .WillOnce(DoAll(SetArgReferee<0>(false), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "accessibility.audiodescription", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    // Reverted handler maps to GetAudioDescriptionsEnabled which returns bare "false" string
    EXPECT_EQ("false", result);
}

/* ========================================================================
 * Category A Coverage: UserSettingsDelegate uncovered branches
 * ======================================================================== */

/* ---------- GetPresentationLanguage: no-dash locale ---------- */

TEST_F(UserSettingsTest, AGC_L1_080_GetPresentationLanguage_NoDashLocale)
{
    EXPECT_CALL(mockUserSettings, GetPresentationLanguage(_))
        .WillOnce(DoAll(SetArgReferee<0>(string("eng")), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.language", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    // No dash in "eng" → return full string as-is
    EXPECT_EQ("\"eng\"", result);
}

/* ---------- GetPresentationLanguage: empty returns error ---------- */

TEST_F(UserSettingsTest, AGC_L1_081_GetPresentationLanguage_EmptyReturnsError)
{
    EXPECT_CALL(mockUserSettings, GetPresentationLanguage(_))
        .WillOnce(DoAll(SetArgReferee<0>(string("")), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.language", "{}", result);

    // COM call succeeds but empty result → ERROR_GENERAL
    EXPECT_NE(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("error"), std::string::npos);
}

/* ---------- GetLocale: empty result is a valid "uninitialized" value, not an error ---------- */

TEST_F(UserSettingsTest, AGC_L1_082_GetLocale_EmptyReturnsEmptyString)
{
    EXPECT_CALL(mockUserSettings, GetPresentationLanguage(_))
        .WillOnce(DoAll(SetArgReferee<0>(string("")), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.locale", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("\"\"", result);
}

TEST_F(UserSettingsTest, AGC_L1_082A_GetPresentationLanguage_EmptyReturnsEmptyString)
{
    EXPECT_CALL(mockUserSettings, GetPresentationLanguage(_))
        .WillOnce(DoAll(SetArgReferee<0>(string("")), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.presentationlanguage", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("\"\"", result);
}

/* ---------- SetPreferredAudioLanguages: JSON array input ---------- */

TEST_F(UserSettingsTest, AGC_L1_083_SetPreferredAudioLanguages_JsonArrayInput)
{
    // The delegate should convert JSON array ["eng","fra"] to comma-separated "eng,fra"
    EXPECT_CALL(mockUserSettings, SetPreferredAudioLanguages(string("eng,fra")))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.setpreferredaudiolanguages", R"({"value":["eng","fra"]})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

/* ---------- SetPreferredCaptionsLanguages: JSON array input ---------- */

TEST_F(UserSettingsTest, AGC_L1_084_SetPreferredCaptionsLanguages_JsonArrayInput)
{
    EXPECT_CALL(mockUserSettings, SetPreferredCaptionsLanguages(string("eng,spa")))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "closedcaptions.setpreferredlanguages", R"({"value":["eng","spa"]})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

/* ---------- GetClosedCaptionSettings: preferred language failure ---------- */

TEST_F(UserSettingsTest, AGC_L1_085_GetClosedCaptionSettings_PreferredLanguageFailure)
{
    EXPECT_CALL(mockUserSettings, GetCaptions(_))
        .WillOnce(DoAll(SetArgReferee<0>(true), Return(Core::ERROR_NONE)));

    EXPECT_CALL(mockUserSettings, GetPreferredCaptionsLanguages(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "accessibility.closedcaptions", "{}", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("couldn't get preferred captions languages"), std::string::npos);
}

/* ---------- GetPreferredAudioLanguages: comma parsing ---------- */

TEST_F(UserSettingsTest, AGC_L1_086_GetPreferredAudioLanguages_CommaSeparatedParsing)
{
    EXPECT_CALL(mockUserSettings, GetPreferredAudioLanguages(_))
        .WillOnce(DoAll(SetArgReferee<0>(string("eng,fra,spa")), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.preferredaudiolanguages", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    // Should be a JSON array with three elements
    EXPECT_NE(result.find("eng"), std::string::npos);
    EXPECT_NE(result.find("fra"), std::string::npos);
    EXPECT_NE(result.find("spa"), std::string::npos);
}

/* ---------- SetPreferredAudioLanguages: empty array input ---------- */

TEST_F(UserSettingsTest, AGC_L1_087_SetPreferredAudioLanguages_EmptyArrayInput)
{
    EXPECT_CALL(mockUserSettings, SetPreferredAudioLanguages(string("")))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.setpreferredaudiolanguages", R"({"value":"[]"})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

/* ---------- VoiceGuidanceSettings: rate fetch failure ---------- */

TEST_F(UserSettingsTest, AGC_L1_088_VoiceGuidanceSettings_RateFetchFailure)
{
    EXPECT_CALL(mockUserSettings, GetVoiceGuidance(_))
        .WillOnce(DoAll(SetArgReferee<0>(true), Return(Core::ERROR_NONE)));

    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceRate(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "accessibility.voiceguidance", "{}", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("couldn't get voiceguidance rate"), std::string::npos);
}

/* ---------- VoiceGuidanceSettings: hints fetch failure ---------- */

TEST_F(UserSettingsTest, AGC_L1_089_VoiceGuidanceSettings_HintsFetchFailure)
{
    EXPECT_CALL(mockUserSettings, GetVoiceGuidance(_))
        .WillOnce(DoAll(SetArgReferee<0>(true), Return(Core::ERROR_NONE)));

    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceRate(_))
        .WillOnce(DoAll(SetArgReferee<0>(1.0), Return(Core::ERROR_NONE)));

    EXPECT_CALL(mockUserSettings, GetVoiceGuidanceHints(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "accessibility.voiceguidance", "{}", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("couldn't get voiceguidance hints"), std::string::npos);
}

/* ========================================================================
 * ParentalControl APIs (RDKEMW-19326)
 * ======================================================================== */

/* ---------- GetPinControl ---------- */

TEST_F(UserSettingsTest, AGC_L1_090_GetPinControl_Success_True)
{
    EXPECT_CALL(mockUserSettings, GetPinControl(_))
        .WillOnce(DoAll(SetArgReferee<0>(true), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "parentalcontrol.pincontrol", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("true", result);
}

TEST_F(UserSettingsTest, AGC_L1_091_GetPinControl_Success_False)
{
    EXPECT_CALL(mockUserSettings, GetPinControl(_))
        .WillOnce(DoAll(SetArgReferee<0>(false), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "parentalcontrol.pincontrol", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("false", result);
}

TEST_F(UserSettingsTest, AGC_L1_092_GetPinControl_COMFailure)
{
    EXPECT_CALL(mockUserSettings, GetPinControl(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "parentalcontrol.pincontrol", "{}", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("error"), std::string::npos);
}

/* ---------- GetBlockNotRatedContent ---------- */

TEST_F(UserSettingsTest, AGC_L1_093_GetBlockNotRatedContent_Success_True)
{
    EXPECT_CALL(mockUserSettings, GetBlockNotRatedContent(_))
        .WillOnce(DoAll(SetArgReferee<0>(true), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "parentalcontrol.blocknotratedcontent", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("true", result);
}

TEST_F(UserSettingsTest, AGC_L1_094_GetBlockNotRatedContent_Success_False)
{
    EXPECT_CALL(mockUserSettings, GetBlockNotRatedContent(_))
        .WillOnce(DoAll(SetArgReferee<0>(false), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "parentalcontrol.blocknotratedcontent", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("false", result);
}

TEST_F(UserSettingsTest, AGC_L1_095_GetBlockNotRatedContent_COMFailure)
{
    EXPECT_CALL(mockUserSettings, GetBlockNotRatedContent(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "parentalcontrol.blocknotratedcontent", "{}", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("error"), std::string::npos);
}

/* ---------- GetViewingRestrictions ---------- */

TEST_F(UserSettingsTest, AGC_L1_096_GetViewingRestrictions_Success)
{
    // Thunder returns a JSON object string; the delegate returns it directly
    // without additional serialization.
    const string thunderValue = R"({"maxRating":"PG-13"})";
    const string expectedResult = R"({"maxRating":"PG-13"})";

    EXPECT_CALL(mockUserSettings, GetViewingRestrictions(_))
        .WillOnce(DoAll(SetArgReferee<0>(thunderValue), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "parentalcontrol.viewingrestrictions", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ(expectedResult, result);
}

TEST_F(UserSettingsTest, AGC_L1_097_GetViewingRestrictions_COMFailure)
{
    EXPECT_CALL(mockUserSettings, GetViewingRestrictions(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "parentalcontrol.viewingrestrictions", "{}", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
    EXPECT_EQ(result, "{\"error\":\"couldn't get viewing restrictions\"}");
}

} // namespace
