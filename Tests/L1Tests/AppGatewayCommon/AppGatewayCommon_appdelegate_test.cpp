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
#include <chrono>
#include <string>
#include <thread>

#include "Module.h"

#define private public
#include "AppGatewayCommon.h"
#undef private

#include "ServiceMock.h"
#include "MockSharedStorage.h"
#include "ThunderPortability.h"
#include "WorkerPoolImplementation.h"

using namespace WPEFramework;
using namespace WPEFramework::Plugin;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::Invoke;
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

class AppDelegateTest : public ::testing::Test {
protected:
    static Core::Sink<AppGatewayCommon>* sPlugin;
    static NiceMock<ServiceMock>* sService;
    static NiceMock<MockSharedStorage>* sMockStorage;

    Core::Sink<AppGatewayCommon>& plugin = *sPlugin;
    NiceMock<ServiceMock>& service = *sService;
    NiceMock<MockSharedStorage>& mockStorage = *sMockStorage;

    static void SetUpTestSuite()
    {
        sService = new NiceMock<ServiceMock>();
        sMockStorage = new NiceMock<MockSharedStorage>();
        sPlugin = new Core::Sink<AppGatewayCommon>();

        ON_CALL(*sService, QueryInterfaceByCallsign(_, _))
            .WillByDefault(Return(nullptr));

        ON_CALL(*sService, QueryInterfaceByCallsign(Exchange::ISharedStorage::ID, ::testing::StrEq("org.rdk.SharedStorage")))
            .WillByDefault(::testing::Invoke([](uint32_t, const string&) -> void* {
                sMockStorage->AddRef();
                return static_cast<Exchange::ISharedStorage*>(sMockStorage);
            }));

        EXPECT_CALL(*sService, AddRef()).Times(AnyNumber());
        EXPECT_CALL(*sService, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));

        const string response = sPlugin->Initialize(sService);
        ASSERT_TRUE(response.empty());
    }

    static void TearDownTestSuite()
    {
        if (sPlugin && sService) {
            sPlugin->Deinitialize(sService);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        delete sPlugin;      sPlugin = nullptr;
        delete sMockStorage; sMockStorage = nullptr;
        delete sService;     sService = nullptr;
    }

    void TearDown() override
    {
        ::testing::Mock::VerifyAndClearExpectations(sMockStorage);
    }
};

Core::Sink<AppGatewayCommon>* AppDelegateTest::sPlugin = nullptr;
NiceMock<ServiceMock>* AppDelegateTest::sService = nullptr;
NiceMock<MockSharedStorage>* AppDelegateTest::sMockStorage = nullptr;

/* ---------- GetDeviceUID via HandleAppGatewayRequest ---------- */

TEST_F(AppDelegateTest, AGC_L1_018_DeviceUID_Success_ExistingValue)
{
    EXPECT_CALL(mockStorage, GetValue(Exchange::ISharedStorage::DEVICE, _, "fireboltDeviceUid", _, _, _))
        .WillOnce(DoAll(
            SetArgReferee<3>("existing-uid-1234"),
            SetArgReferee<4>(0),
            SetArgReferee<5>(true),
            Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "device.uid", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("existing-uid-1234"), std::string::npos);
    // Result must be a JSON-quoted string (e.g. "\"existing-uid-1234\"")
    EXPECT_FALSE(result.empty());
    EXPECT_EQ('"', result.front());
    EXPECT_EQ('"', result.back());
}

TEST_F(AppDelegateTest, AGC_L1_019_DeviceUID_CreatesNewWhenNotFound)
{
    // GetValue fails -> triggers UUID creation + SetValue
    EXPECT_CALL(mockStorage, GetValue(Exchange::ISharedStorage::DEVICE, _, "fireboltDeviceUid", _, _, _))
        .WillOnce(Return(Core::ERROR_GENERAL));

    Exchange::ISharedStorage::Success successResult;
    successResult.success = true;
    EXPECT_CALL(mockStorage, SetValue(Exchange::ISharedStorage::DEVICE, _, "fireboltDeviceUid", _, 0, _))
        .WillOnce(DoAll(
            SetArgReferee<5>(successResult),
            Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "device.uid", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    // Result should contain a UUID-style string
    EXPECT_FALSE(result.empty());
    // Newly generated UID must be returned as a JSON-quoted string (e.g. "\"<uuid>\"")
    EXPECT_EQ('"', result.front());
    EXPECT_EQ('"', result.back());
}

TEST_F(AppDelegateTest, AGC_L1_020_DeviceUID_SetValueFails)
{
    EXPECT_CALL(mockStorage, GetValue(Exchange::ISharedStorage::DEVICE, _, "fireboltDeviceUid", _, _, _))
        .WillOnce(Return(Core::ERROR_GENERAL));

    Exchange::ISharedStorage::Success failResult;
    failResult.success = false;
    EXPECT_CALL(mockStorage, SetValue(Exchange::ISharedStorage::DEVICE, _, "fireboltDeviceUid", _, 0, _))
        .WillOnce(DoAll(
            SetArgReferee<5>(failResult),
            Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "device.uid", "{}", result);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
}

/* ---------- GetAdvertisingId via HandleAppGatewayRequest ---------- */

TEST_F(AppDelegateTest, AGC_L1_021_AdvertisingId_Success_ExistingValue)
{
    EXPECT_CALL(mockStorage, GetValue(Exchange::ISharedStorage::DEVICE, _, "fireboltAdvertisingId", _, _, _))
        .WillOnce(DoAll(
            SetArgReferee<3>("existing-ad-id"),
            SetArgReferee<4>(0),
            SetArgReferee<5>(true),
            Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "advertising.advertisingid", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("existing-ad-id"), std::string::npos);
    EXPECT_NE(result.find("ifa"), std::string::npos);
    EXPECT_NE(result.find("ifa_type"), std::string::npos);
    EXPECT_NE(result.find("lmt"), std::string::npos);
}

TEST_F(AppDelegateTest, AGC_L1_022_AdvertisingId_CreatesNewWhenNotFound)
{
    EXPECT_CALL(mockStorage, GetValue(Exchange::ISharedStorage::DEVICE, _, "fireboltAdvertisingId", _, _, _))
        .WillOnce(Return(Core::ERROR_GENERAL));

    Exchange::ISharedStorage::Success successResult;
    successResult.success = true;
    EXPECT_CALL(mockStorage, SetValue(Exchange::ISharedStorage::DEVICE, _, "fireboltAdvertisingId", _, 0, _))
        .WillOnce(DoAll(
            SetArgReferee<5>(successResult),
            Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "advertising.advertisingid", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("ifa"), std::string::npos);
    EXPECT_NE(result.find("ifa_type"), std::string::npos);
    EXPECT_NE(result.find("lmt"), std::string::npos);
}

TEST_F(AppDelegateTest, AGC_L1_023_AdvertisingId_SetValueFails)
{
    EXPECT_CALL(mockStorage, GetValue(Exchange::ISharedStorage::DEVICE, _, "fireboltAdvertisingId", _, _, _))
        .WillOnce(Return(Core::ERROR_GENERAL));

    Exchange::ISharedStorage::Success failResult;
    failResult.success = false;
    EXPECT_CALL(mockStorage, SetValue(Exchange::ISharedStorage::DEVICE, _, "fireboltAdvertisingId", _, 0, _))
        .WillOnce(DoAll(
            SetArgReferee<5>(failResult),
            Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "advertising.advertisingid", "{}", result);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
}

/* ---------- Routing ---------- */

TEST_F(AppDelegateTest, AGC_L1_024_AppDelegate_UnrecognizedMethod)
{
    const auto ctx = MakeContext();
    string result;
    // "advertising.somethingelse" isn't handled by AppDelegate
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "advertising.somethingelse", "{}", result);

    // This goes through the main handler map, not AppDelegate  
    EXPECT_EQ(Core::ERROR_UNKNOWN_KEY, rc);
}

/* ================================================================
 * Category B – SharedStorage null (QueryInterfaceByCallsign fails)
 * ================================================================ */

class AppDelegateNoStorageTest : public ::testing::Test {
protected:
    static Core::Sink<AppGatewayCommon>* sPlugin;
    static NiceMock<ServiceMock>* sService;

    Core::Sink<AppGatewayCommon>& plugin = *sPlugin;
    NiceMock<ServiceMock>& service = *sService;

    static void SetUpTestSuite()
    {
        sService = new NiceMock<ServiceMock>();
        sPlugin = new Core::Sink<AppGatewayCommon>();

        ON_CALL(*sService, QueryInterfaceByCallsign(_, _))
            .WillByDefault(Return(nullptr));

        EXPECT_CALL(*sService, AddRef()).Times(AnyNumber());
        EXPECT_CALL(*sService, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));

        const string response = sPlugin->Initialize(sService);
        ASSERT_TRUE(response.empty());
    }

    static void TearDownTestSuite()
    {
        if (sPlugin && sService) {
            sPlugin->Deinitialize(sService);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        delete sPlugin;  sPlugin = nullptr;
        delete sService; sService = nullptr;
    }
};

Core::Sink<AppGatewayCommon>* AppDelegateNoStorageTest::sPlugin = nullptr;
NiceMock<ServiceMock>* AppDelegateNoStorageTest::sService = nullptr;

TEST_F(AppDelegateNoStorageTest, AGC_L1_025_GetDeviceUID_NoSharedStorage_ReturnsUnavailable)
{
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "device.uid", "{}", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, rc);
    EXPECT_NE(result.find("Unable to get SharedStorage interface"), std::string::npos);
}

TEST_F(AppDelegateNoStorageTest, AGC_L1_026_GetAdvertisingId_NoSharedStorage_ReturnsUnavailable)
{
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "advertising.advertisingid", "{}", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, rc);
    EXPECT_NE(result.find("Unable to get SharedStorage interface"), std::string::npos);
}

} // namespace
