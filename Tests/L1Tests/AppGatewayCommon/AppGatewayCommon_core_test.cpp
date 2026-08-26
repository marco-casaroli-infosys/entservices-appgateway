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
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

#include "Module.h"

#define private public
#include "AppGatewayCommon.h"
#undef private

AGW_DEFINE_TELEMETRY_CLIENT(AGW_PLUGIN_APPGATEWAYCOMMON)

#include "ServiceMock.h"
#include "ThunderPortability.h"
#include "WorkerPoolImplementation.h"

using namespace WPEFramework;
using namespace WPEFramework::Plugin;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::NiceMock;
using ::testing::Return;

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

class WorkerPoolDrainGuard final {
public:
    ~WorkerPoolDrainGuard()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};

static WorkerPoolDrainGuard gWorkerPoolDrain;

static Exchange::GatewayContext MakeContext()
{
    Exchange::GatewayContext ctx;
    ctx.appId = "test.app";
    ctx.connectionId = 100;
    ctx.requestId = 200;
    return ctx;
}

class TestEmitter : public Exchange::IAppNotificationHandler::IEmitter {
public:
    void Emit(const string& event, const string& payload, const string& appId) override
    {
        (void)event;
        (void)payload;
        (void)appId;
    }

    BEGIN_INTERFACE_MAP(TestEmitter)
    INTERFACE_ENTRY(Exchange::IAppNotificationHandler::IEmitter)
    END_INTERFACE_MAP
};

class AppGatewayCommonTest : public ::testing::Test {
protected:
    Core::Sink<AppGatewayCommon> plugin;

    void TearDown() override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
};

TEST_F(AppGatewayCommonTest, AGC_L1_001_InitializeAndDeinitialize_Success)
{
    NiceMock<ServiceMock> service;

    EXPECT_CALL(service, QueryInterfaceByCallsign(_, _))
        .Times(AnyNumber())
        .WillRepeatedly(Return(nullptr));
    EXPECT_CALL(service, AddRef()).Times(1);
    EXPECT_CALL(service, Release()).Times(1).WillOnce(Return(Core::ERROR_NONE));

    const string response = plugin.Initialize(&service);
    EXPECT_TRUE(response.empty());

    plugin.Deinitialize(&service);
}

TEST_F(AppGatewayCommonTest, DISABLED_AGC_L1_002_HandleAppEventNotifier_SubmitsJob)
{
    NiceMock<ServiceMock> service;
    EXPECT_CALL(service, QueryInterfaceByCallsign(_, _))
        .Times(AnyNumber())
        .WillRepeatedly(Return(nullptr));
    EXPECT_CALL(service, AddRef()).Times(1);
    EXPECT_CALL(service, Release()).Times(1).WillOnce(Return(Core::ERROR_NONE));

    const string initResponse = plugin.Initialize(&service);
    EXPECT_TRUE(initResponse.empty());

    Core::Sink<TestEmitter> emitter;

    bool status = false;
    const auto rc = plugin.HandleAppEventNotifier(&emitter, "Device.onHdrChanged", true, status);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_TRUE(status);

    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    plugin.Deinitialize(&service);
}

TEST_F(AppGatewayCommonTest, AGC_L1_003_UnknownMethod_ReturnsUnknownKey)
{
    NiceMock<ServiceMock> service;
    EXPECT_CALL(service, QueryInterfaceByCallsign(_, _)).Times(AnyNumber()).WillRepeatedly(Return(nullptr));
    EXPECT_CALL(service, AddRef()).Times(1);
    EXPECT_CALL(service, Release()).Times(1).WillOnce(Return(Core::ERROR_NONE));
    plugin.Initialize(&service);

    const auto ctx = MakeContext();
    string result;

    const auto rc = plugin.HandleAppGatewayRequest(ctx, "foo.bar", "{}", result);

    EXPECT_EQ(Core::ERROR_UNKNOWN_KEY, rc);
    EXPECT_FALSE(result.empty());

    plugin.Deinitialize(&service);
}

TEST_F(AppGatewayCommonTest, AGC_L1_004_DeviceSetName_InvalidPayload)
{
    NiceMock<ServiceMock> service;
    EXPECT_CALL(service, QueryInterfaceByCallsign(_, _)).Times(AnyNumber()).WillRepeatedly(Return(nullptr));
    EXPECT_CALL(service, AddRef()).Times(1);
    EXPECT_CALL(service, Release()).Times(1).WillOnce(Return(Core::ERROR_NONE));
    plugin.Initialize(&service);

    const auto ctx = MakeContext();
    string result;

    const auto rc = plugin.HandleAppGatewayRequest(ctx, "device.setname", "{invalid", result);

    EXPECT_EQ(Core::ERROR_BAD_REQUEST, rc);
    EXPECT_EQ("{\"error\":\"Invalid payload: missing or invalid 'value' field\"}", result);

    plugin.Deinitialize(&service);
}

TEST_F(AppGatewayCommonTest, AGC_L1_005_InvalidPayloads)
{
    NiceMock<ServiceMock> service;
    EXPECT_CALL(service, QueryInterfaceByCallsign(_, _)).Times(AnyNumber()).WillRepeatedly(Return(nullptr));
    EXPECT_CALL(service, AddRef()).Times(1);
    EXPECT_CALL(service, Release()).Times(1).WillOnce(Return(Core::ERROR_NONE));
    plugin.Initialize(&service);

    const auto ctx = MakeContext();

    // Maps each method to the exact error string returned by production code for invalid payload
    const std::vector<std::pair<std::string, std::string>> methodErrors = {
        { "localization.setcountrycode",           "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}" },
        { "localization.settimezone",              "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}" },
        { "voiceguidance.setenabled",              "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}" },
        { "audiodescriptions.setenabled",          "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}" },
        { "closedcaptions.setenabled",             "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}" },
        { "localization.setlocale",                "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}" },
        { "localization.setpreferredaudiolanguages","{\"error\":\"Invalid payload: 'value' field must be a string or array\"}" },
        { "closedcaptions.setpreferredlanguages",  "{\"error\":\"Invalid payload: 'value' field must be a string or array\"}" },
        { "voiceguidance.setspeed",                "{\"error\":\"Invalid payload: missing, invalid, or out-of-range 'value' field (expected 0.5-2.0)\"}" },
        { "voiceguidance.setnavigationhints",      "{\"error\":\"Invalid payload: missing or invalid 'value' field\"}" },
    };

    for (const auto& entry : methodErrors) {
        const auto& method = entry.first;
        const auto& expectedError = entry.second;
        string result;
        const auto rc = plugin.HandleAppGatewayRequest(ctx, method, "{invalid", result);
        EXPECT_EQ(Core::ERROR_BAD_REQUEST, rc) << "method=" << method;
        EXPECT_EQ(expectedError, result) << "method=" << method;
    }

    plugin.Deinitialize(&service);
}

TEST_F(AppGatewayCommonTest, AGC_L1_006_NullDelegate)
{
    plugin.mDelegate.reset();
    const auto ctx = MakeContext();

    const std::vector<std::string> methods = {
        "device.make",
        "device.name",
        "device.sku",
        "localization.countrycode",
        "localization.timezone",
        "device.network",
        "voiceguidance.enabled",
        "audiodescriptions.enabled",
        "closedcaptions.enabled",
        "localization.locale",
        "localization.language",
        "localization.preferredaudiolanguages",
        "closedcaptions.preferredlanguages"
    };

    for (const auto& method : methods) {
        string result;
        const auto rc = plugin.HandleAppGatewayRequest(ctx, method, "{}", result);
        EXPECT_EQ(Core::ERROR_UNAVAILABLE, rc) << "method=" << method;
    }
}

TEST_F(AppGatewayCommonTest, AGC_L1_007_DefaultCapabilityFallbacks_WhenDelegateUnavailable)
{
    plugin.mDelegate.reset();

    string result;

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.GetScreenResolution(result));
    EXPECT_EQ("[1920,1080]", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.GetVideoResolution(result));
    EXPECT_EQ("[1920,1080]", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.GetHdcp(result));
    EXPECT_EQ("{\"hdcp1.4\":false,\"hdcp2.2\":false}", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.GetHdr(result));
    EXPECT_EQ("{\"hdr10\":false,\"dolbyVision\":false,\"hlg\":false,\"hdr10Plus\":false}", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.GetAudio(result));
    EXPECT_EQ("{\"stereo\":true,\"dolbyDigital5.1\":false,\"dolbyDigital5.1+\":false,\"dolbyAtmos\":false}", result);
}

TEST_F(AppGatewayCommonTest, AGC_L1_008_VoiceGuidanceSettings_NullDelegate_ReturnsUnavailable)
{
    plugin.mDelegate.reset();

    string result;
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.GetVoiceGuidanceSettings(true, result));
    EXPECT_EQ("{\"error\":\"couldn't get voice guidance settings\"}", result);
}

TEST_F(AppGatewayCommonTest, AGC_L1_009_ClosedCaptionsSettings_NullDelegate_ReturnsUnavailable)
{
    plugin.mDelegate.reset();

    string result;
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.GetClosedCaptionsSettings(result));
    EXPECT_EQ("{\"error\":\"couldn't get closed captions settings\"}", result);
}

TEST_F(AppGatewayCommonTest, AGC_L1_010_GetSpeed_NullDelegate_ReturnsUnavailable)
{
    plugin.mDelegate.reset();

    double speed = 0.0;
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.GetSpeed(speed));
}

TEST_F(AppGatewayCommonTest, AGC_L1_011_SetSpeed_NullDelegate_ReturnsUnavailable)
{
    plugin.mDelegate.reset();

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.SetSpeed(2.0));   // 033
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.SetSpeed(1.67));  // 034
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.SetSpeed(1.33));  // 035
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.SetSpeed(1.0));   // 036
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.SetSpeed(0.8));   // 037
}

TEST_F(AppGatewayCommonTest, AGC_L1_012_LifecycleAndAuth_NullDelegate_ReturnUnavailable)
{
    plugin.mDelegate.reset();

    const auto ctx = MakeContext();
    string result;
    string out;

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.Authenticate("session-1", out));
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.GetSessionId("test.app", out));

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.LifecycleReady(ctx, "{}", result));
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.LifecycleClose(ctx, "{}", result));
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.LifecycleState(ctx, "{}", result));
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.Lifecycle2State(ctx, "{}", result));
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.Lifecycle2Close(ctx, "{}", result));
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.LifecycleFinished(ctx, "{}", result));
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.DispatchLastIntent(ctx, "{}", result));
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.GetLastIntent(ctx, "{}", result));
}

TEST_F(AppGatewayCommonTest, AGC_L1_013_CheckPermissionGroup_DefaultAllowed)
{
    bool allowed = false;
    const auto rc = plugin.CheckPermissionGroup("test.app", "any.group", allowed);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_TRUE(allowed);
}

TEST_F(AppGatewayCommonTest, AGC_L1_014_FirmwareVersion_NullDelegate_ReturnsUnavailable)
{
    plugin.mDelegate.reset();

    string result;
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.GetFirmwareVersion(result));
}

TEST_F(AppGatewayCommonTest, AGC_L1_015_DeviceSetName_ValidPayload_NoDelegate)
{
    plugin.mDelegate.reset();

    const auto ctx = MakeContext();
    string result;

    const auto rc = plugin.HandleAppGatewayRequest(ctx, "device.setname", "{\"value\":\"Bedroom\"}", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, rc);
}

TEST_F(AppGatewayCommonTest, AGC_L1_016_AddAdditionalInfo_RouteSuccess)
{
    NiceMock<ServiceMock> service;
    EXPECT_CALL(service, QueryInterfaceByCallsign(_, _)).Times(AnyNumber()).WillRepeatedly(Return(nullptr));
    EXPECT_CALL(service, AddRef()).Times(1);
    EXPECT_CALL(service, Release()).Times(1).WillOnce(Return(Core::ERROR_NONE));
    plugin.Initialize(&service);

    const auto ctx = MakeContext();
    string result;

    const auto rc = plugin.HandleAppGatewayRequest(ctx, "localization.addadditionalinfo", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);

    plugin.Deinitialize(&service);
}

TEST_F(AppGatewayCommonTest, AGC_L1_017_CaseInsensitiveRouting_UnknownAndKnown)
{
    plugin.mDelegate.reset();

    const auto ctx = MakeContext();
    string result;

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.HandleAppGatewayRequest(ctx, "DEVICE.NETWORK", "{}", result));

    result.clear();
    // With null delegate the top-level guard fires before routing, so unknown methods also return ERROR_UNAVAILABLE
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, plugin.HandleAppGatewayRequest(ctx, "Not.A.Method", "{}", result));
}

} // namespace
