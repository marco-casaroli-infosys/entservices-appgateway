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
#include "NetworkManagerMock.h"
#include "MockInterfaceDetailsIterator.h"
#include "MockEmitter.h"
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

class NetworkDelegateTest : public ::testing::Test {
protected:
    static Core::Sink<AppGatewayCommon>* sPlugin;
    static NiceMock<ServiceMock>* sService;
    static NiceMock<MockINetworkManager>* sMockNetwork;

    Core::Sink<AppGatewayCommon>& plugin = *sPlugin;
    NiceMock<ServiceMock>& service = *sService;
    NiceMock<MockINetworkManager>& mockNetwork = *sMockNetwork;

    static void SetUpTestSuite()
    {
        sService = new NiceMock<ServiceMock>();
        sMockNetwork = new NiceMock<MockINetworkManager>();
        sPlugin = new Core::Sink<AppGatewayCommon>();

        ON_CALL(*sService, QueryInterfaceByCallsign(_, _))
            .WillByDefault(Return(nullptr));

        ON_CALL(*sService, QueryInterfaceByCallsign(Exchange::INetworkManager::ID, ::testing::StrEq("org.rdk.NetworkManager")))
            .WillByDefault(::testing::Invoke([](uint32_t, const string&) -> void* {
                sMockNetwork->AddRef();
                return static_cast<Exchange::INetworkManager*>(sMockNetwork);
            }));

        EXPECT_CALL(*sService, AddRef()).Times(AnyNumber());
        EXPECT_CALL(*sService, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
        EXPECT_CALL(*sMockNetwork, Register(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
        EXPECT_CALL(*sMockNetwork, Unregister(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));

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
        delete sMockNetwork; sMockNetwork = nullptr;
        delete sService;     sService = nullptr;
    }

    void TearDown() override
    {
        ::testing::Mock::VerifyAndClearExpectations(sMockNetwork);
        EXPECT_CALL(*sMockNetwork, Register(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
        EXPECT_CALL(*sMockNetwork, Unregister(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    }
};

Core::Sink<AppGatewayCommon>* NetworkDelegateTest::sPlugin = nullptr;
NiceMock<ServiceMock>* NetworkDelegateTest::sService = nullptr;
NiceMock<MockINetworkManager>* NetworkDelegateTest::sMockNetwork = nullptr;

/* ---------- GetNetworkConnected ---------- */

TEST_F(NetworkDelegateTest, AGC_L1_147_GetNetworkConnected_Connected)
{
    EXPECT_CALL(mockNetwork, GetPrimaryInterface(_))
        .WillOnce(DoAll(SetArgReferee<0>("eth0"), Return(Core::ERROR_NONE)));

    auto* mockIterator = new NiceMock<MockInterfaceDetailsIterator>();
    Exchange::INetworkManager::InterfaceDetails ethernetIface;
    ethernetIface.type = Exchange::INetworkManager::INTERFACE_TYPE_ETHERNET;
    ethernetIface.name = "eth0";
    ethernetIface.connected = true;
    EXPECT_CALL(*mockIterator, Next(_))
        .WillOnce(DoAll(SetArgReferee<0>(ethernetIface), Return(true)))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mockIterator, Release())
        .WillOnce(::testing::Invoke([mockIterator]() { delete mockIterator; return 0; }));
    EXPECT_CALL(mockNetwork, GetAvailableInterfaces(_))
        .WillOnce(DoAll(SetArgReferee<0>(mockIterator), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "network.connected", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("true", result);
}

TEST_F(NetworkDelegateTest, AGC_L1_147b_GetNetworkConnected_PrimaryNameStaleButLinkDown)
{
    // The regression case: GetPrimaryInterface() still names an interface, but that
    // interface's own link is actually down. Network.connected() must not trust the
    // name alone.
    EXPECT_CALL(mockNetwork, GetPrimaryInterface(_))
        .WillOnce(DoAll(SetArgReferee<0>("wlan0"), Return(Core::ERROR_NONE)));

    auto* mockIterator = new NiceMock<MockInterfaceDetailsIterator>();
    Exchange::INetworkManager::InterfaceDetails wifiIface;
    wifiIface.type = Exchange::INetworkManager::INTERFACE_TYPE_WIFI;
    wifiIface.name = "wlan0";
    wifiIface.connected = false;
    EXPECT_CALL(*mockIterator, Next(_))
        .WillOnce(DoAll(SetArgReferee<0>(wifiIface), Return(true)))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mockIterator, Release())
        .WillOnce(::testing::Invoke([mockIterator]() { delete mockIterator; return 0; }));
    EXPECT_CALL(mockNetwork, GetAvailableInterfaces(_))
        .WillOnce(DoAll(SetArgReferee<0>(mockIterator), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "network.connected", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("false", result);
}

TEST_F(NetworkDelegateTest, AGC_L1_147c_GetNetworkConnected_PrimaryNameNotInAvailableInterfaces)
{
    // GetPrimaryInterface() names an interface GetAvailableInterfaces() doesn't know
    // about at all -- treat as disconnected rather than guessing.
    EXPECT_CALL(mockNetwork, GetPrimaryInterface(_))
        .WillOnce(DoAll(SetArgReferee<0>("wlan0"), Return(Core::ERROR_NONE)));

    auto* mockIterator = new NiceMock<MockInterfaceDetailsIterator>();
    EXPECT_CALL(*mockIterator, Next(_)).WillOnce(Return(false));
    EXPECT_CALL(*mockIterator, Release())
        .WillOnce(::testing::Invoke([mockIterator]() { delete mockIterator; return 0; }));
    EXPECT_CALL(mockNetwork, GetAvailableInterfaces(_))
        .WillOnce(DoAll(SetArgReferee<0>(mockIterator), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "network.connected", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("false", result);
}

TEST_F(NetworkDelegateTest, AGC_L1_148_GetNetworkConnected_Disconnected)
{
    EXPECT_CALL(mockNetwork, GetPrimaryInterface(_))
        .WillOnce(DoAll(SetArgReferee<0>(""), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "network.connected", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("false", result);
}

TEST_F(NetworkDelegateTest, AGC_L1_149_GetNetworkConnected_CallFails)
{
    EXPECT_CALL(mockNetwork, GetPrimaryInterface(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "network.connected", "{}", result);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
}

/* ---------- GetInternetConnectionStatus ---------- */

TEST_F(NetworkDelegateTest, AGC_L1_150_GetInternetConnectionStatus_Ethernet)
{
    auto* mockIterator = new NiceMock<MockInterfaceDetailsIterator>();
    
    Exchange::INetworkManager::InterfaceDetails ethernetIface;
    ethernetIface.type = Exchange::INetworkManager::INTERFACE_TYPE_ETHERNET;
    ethernetIface.name = "eth0";
    ethernetIface.connected = true;

    EXPECT_CALL(*mockIterator, Next(_))
        .WillOnce(DoAll(SetArgReferee<0>(ethernetIface), Return(true)))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mockIterator, Release())
        .WillOnce(::testing::Invoke([mockIterator]() { delete mockIterator; return 0; }));

    EXPECT_CALL(mockNetwork, GetAvailableInterfaces(_))
        .WillOnce(DoAll(SetArgReferee<0>(mockIterator), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "device.network", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("ethernet"), std::string::npos);
    EXPECT_NE(result.find("connected"), std::string::npos);
}

TEST_F(NetworkDelegateTest, AGC_L1_151_GetInternetConnectionStatus_WiFi)
{
    auto* mockIterator = new NiceMock<MockInterfaceDetailsIterator>();
    
    Exchange::INetworkManager::InterfaceDetails wifiIface;
    wifiIface.type = Exchange::INetworkManager::INTERFACE_TYPE_WIFI;
    wifiIface.name = "wlan0";
    wifiIface.connected = true;

    EXPECT_CALL(*mockIterator, Next(_))
        .WillOnce(DoAll(SetArgReferee<0>(wifiIface), Return(true)))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mockIterator, Release())
        .WillOnce(::testing::Invoke([mockIterator]() { delete mockIterator; return 0; }));

    EXPECT_CALL(mockNetwork, GetAvailableInterfaces(_))
        .WillOnce(DoAll(SetArgReferee<0>(mockIterator), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "device.network", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("wifi"), std::string::npos);
}

TEST_F(NetworkDelegateTest, AGC_L1_152_GetInternetConnectionStatus_NoneConnected)
{
    auto* mockIterator = new NiceMock<MockInterfaceDetailsIterator>();
    
    Exchange::INetworkManager::InterfaceDetails disconnectedIface;
    disconnectedIface.type = Exchange::INetworkManager::INTERFACE_TYPE_ETHERNET;
    disconnectedIface.name = "eth0";
    disconnectedIface.connected = false;

    EXPECT_CALL(*mockIterator, Next(_))
        .WillOnce(DoAll(SetArgReferee<0>(disconnectedIface), Return(true)))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mockIterator, Release())
        .WillOnce(::testing::Invoke([mockIterator]() { delete mockIterator; return 0; }));

    EXPECT_CALL(mockNetwork, GetAvailableInterfaces(_))
        .WillOnce(DoAll(SetArgReferee<0>(mockIterator), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "device.network", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("{}", result);
}

TEST_F(NetworkDelegateTest, AGC_L1_153_GetInternetConnectionStatus_NullIterator)
{
    EXPECT_CALL(mockNetwork, GetAvailableInterfaces(_))
        .WillOnce(DoAll(SetArgReferee<0>(nullptr), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "device.network", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("{}", result);
}

TEST_F(NetworkDelegateTest, AGC_L1_154_GetInternetConnectionStatus_CallFails)
{
    EXPECT_CALL(mockNetwork, GetAvailableInterfaces(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "device.network", "{}", result);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
}

/* ---------- Additional network error paths ---------- */

TEST_F(NetworkDelegateTest, AGC_L1_155_GetNetworkConnected_EmptyPrimaryInterface)
{
    EXPECT_CALL(mockNetwork, GetPrimaryInterface(_))
        .WillOnce(DoAll(SetArgReferee<0>(string("")), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "network.connected", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("false"), std::string::npos);
}

TEST_F(NetworkDelegateTest, AGC_L1_156_GetInternetConnectionStatus_BothEthernetAndWifi)
{
    auto* mockIterator = new NiceMock<MockInterfaceDetailsIterator>();
    
    Exchange::INetworkManager::InterfaceDetails ethIface;
    ethIface.type = Exchange::INetworkManager::INTERFACE_TYPE_ETHERNET;
    ethIface.name = "eth0";
    ethIface.connected = true;

    Exchange::INetworkManager::InterfaceDetails wifiIface;
    wifiIface.type = Exchange::INetworkManager::INTERFACE_TYPE_WIFI;
    wifiIface.name = "wlan0";
    wifiIface.connected = true;

    // Production code breaks on the first connected interface, so Next() is called
    // only once (ethernet is found and returned). The wifi entry is available in the
    // iterator but never reached — ethernet takes priority by iteration order.
    EXPECT_CALL(*mockIterator, Next(_))
        .WillOnce(DoAll(SetArgReferee<0>(ethIface), Return(true)))
        .WillRepeatedly(DoAll(SetArgReferee<0>(wifiIface), Return(false)));
    EXPECT_CALL(*mockIterator, Release())
        .WillOnce(::testing::Invoke([mockIterator]() { delete mockIterator; return 0; }));

    EXPECT_CALL(mockNetwork, GetAvailableInterfaces(_))
        .WillOnce(DoAll(SetArgReferee<0>(mockIterator), Return(Core::ERROR_NONE)));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "device.network", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    // Ethernet takes priority (first connected interface wins)
    EXPECT_NE(result.find("ethernet"), std::string::npos);
}

/* ================================================================
 * Category B – Null NetworkManager interface
 *
 * All QueryInterfaceByCallsign calls return nullptr, so
 * NetworkDelegate cannot acquire the INetworkManager interface.
 * ================================================================ */

class NetworkNoInterfaceTest : public ::testing::Test {
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

Core::Sink<AppGatewayCommon>* NetworkNoInterfaceTest::sPlugin = nullptr;
NiceMock<ServiceMock>* NetworkNoInterfaceTest::sService = nullptr;

TEST_F(NetworkNoInterfaceTest, AGC_L1_157_GetNetworkConnected_NoInterface_ReturnsUnavailable)
{
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "network.connected", "{}", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, rc);
    EXPECT_NE(result.find("NetworkManager not available"), std::string::npos);
}

TEST_F(NetworkNoInterfaceTest, AGC_L1_158_GetInternetConnectionStatus_NoInterface_ReturnsUnavailable)
{
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "device.network", "{}", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, rc);
    EXPECT_NE(result.find("NetworkManager not available"), std::string::npos);
}

/* ================================================================
 * Category C – Network notification dispatch
 *
 * Capture the INetworkManager::INotification pointer during
 * subscription and fire notification callbacks to verify dispatch.
 * ================================================================ */

class NetworkNotificationTest : public ::testing::Test {
protected:
    Core::Sink<AppGatewayCommon> plugin;
    NiceMock<ServiceMock> service;
    NiceMock<MockINetworkManager> mockNetwork;
    Exchange::INetworkManager::INotification* capturedNotification = nullptr;
    std::vector<MockEmitter*> heapEmitters;

    void SetUp() override
    {
        ON_CALL(service, QueryInterfaceByCallsign(_, _))
            .WillByDefault(Return(nullptr));

        ON_CALL(service, QueryInterfaceByCallsign(Exchange::INetworkManager::ID, ::testing::StrEq("org.rdk.NetworkManager")))
            .WillByDefault(::testing::Invoke([this](uint32_t, const string&) -> void* {
                mockNetwork.AddRef();
                return static_cast<Exchange::INetworkManager*>(&mockNetwork);
            }));

        EXPECT_CALL(service, AddRef()).Times(AnyNumber());
        EXPECT_CALL(service, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));

        // Capture notification pointer on Register
        EXPECT_CALL(mockNetwork, Register(_)).Times(AnyNumber())
            .WillRepeatedly(::testing::Invoke([this](Exchange::INetworkManager::INotification* n) -> uint32_t {
                capturedNotification = n;
                return Core::ERROR_NONE;
            }));
        EXPECT_CALL(mockNetwork, Unregister(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));

        const string response = plugin.Initialize(&service);
        ASSERT_TRUE(response.empty());
    }

    void TearDown() override
    {
        plugin.Deinitialize(&service);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        for (auto* e : heapEmitters) {
            testing::Mock::VerifyAndClearExpectations(e);
            delete e;
        }
        heapEmitters.clear();
    }
};

TEST_F(NetworkNotificationTest, AGC_L1_159_NetworkSubscription_RegistersAndCapturesNotification)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();
    bool status = false;
    const auto rc = plugin.HandleAppEventNotifier(emitter, "Network.onConnectedChanged", true, status);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_TRUE(status);

    // Wait for the async EventRegistrationJob to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // The subscription dispatches asynchronously; verify notification was captured
    EXPECT_NE(capturedNotification, nullptr);
}

TEST_F(NetworkNotificationTest, AGC_L1_160_NetworkNotification_onActiveInterfaceChange_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    // Subscribe to Network.onConnectedChanged
    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "Network.onConnectedChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedNotification, nullptr);

    // Fire onActiveInterfaceChange: empty current → disconnected
    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("Network.onConnectedChanged"), _, _)).Times(::testing::AtLeast(1));
    capturedNotification->onActiveInterfaceChange("eth0", "");

    // Give worker pool time to dispatch
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(NetworkNotificationTest, AGC_L1_160b_NetworkNotification_onActiveInterfaceChange_ChecksActualLinkState)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "Network.onConnectedChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedNotification, nullptr);

    // A new primary interface is named, but GetAvailableInterfaces() says it isn't
    // actually connected -- the dispatched value must follow the real link state,
    // not just "a name was given" (the original defect, in reverse).
    auto* mockIterator = new NiceMock<MockInterfaceDetailsIterator>();
    Exchange::INetworkManager::InterfaceDetails iface;
    iface.type = Exchange::INetworkManager::INTERFACE_TYPE_ETHERNET;
    iface.name = "eth0";
    iface.connected = false;
    EXPECT_CALL(*mockIterator, Next(_))
        .WillOnce(DoAll(SetArgReferee<0>(iface), Return(true)))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mockIterator, Release())
        .WillOnce(::testing::Invoke([mockIterator]() { delete mockIterator; return 0; }));
    EXPECT_CALL(mockNetwork, GetAvailableInterfaces(_))
        .WillOnce(DoAll(SetArgReferee<0>(mockIterator), Return(Core::ERROR_NONE)));

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("Network.onConnectedChanged"),
                               ::testing::HasSubstr("\"value\":false"), _)).Times(1);
    capturedNotification->onActiveInterfaceChange("", "eth0");

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(NetworkNotificationTest, AGC_L1_161_NetworkNotification_onInternetStatusChange_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    // Subscribe to device.onNetworkChanged
    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "device.onNetworkChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedNotification, nullptr);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("device.onNetworkChanged"), _, _)).Times(::testing::AtLeast(1));
    capturedNotification->onInternetStatusChange(
        Exchange::INetworkManager::INTERNET_NOT_AVAILABLE,
        Exchange::INetworkManager::INTERNET_FULLY_CONNECTED,
        "eth0"
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(NetworkNotificationTest, AGC_L1_162_NetworkNotification_onInterfaceStateChange_LinkDownOnPrimary_DispatchesFalse)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "Network.onConnectedChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedNotification, nullptr);

    EXPECT_CALL(mockNetwork, GetPrimaryInterface(_))
        .WillOnce(DoAll(SetArgReferee<0>("wlan0"), Return(Core::ERROR_NONE)));

    // This is the regression case: a link-only flap that leaves the primary interface
    // unchanged, which onActiveInterfaceChange alone would never see.
    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("Network.onConnectedChanged"),
                               ::testing::HasSubstr("\"value\":false"), _)).Times(1);
    capturedNotification->onInterfaceStateChange(Exchange::INetworkManager::INTERFACE_LINK_DOWN, "wlan0");

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(NetworkNotificationTest, AGC_L1_163_NetworkNotification_onInterfaceStateChange_LinkUpOnPrimary_DispatchesTrue)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "Network.onConnectedChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedNotification, nullptr);

    EXPECT_CALL(mockNetwork, GetPrimaryInterface(_))
        .WillOnce(DoAll(SetArgReferee<0>("wlan0"), Return(Core::ERROR_NONE)));

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("Network.onConnectedChanged"),
                               ::testing::HasSubstr("\"value\":true"), _)).Times(1);
    capturedNotification->onInterfaceStateChange(Exchange::INetworkManager::INTERFACE_LINK_UP, "wlan0");

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(NetworkNotificationTest, AGC_L1_164_NetworkNotification_onInterfaceStateChange_NonPrimaryInterface_DoesNotDispatch)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "Network.onConnectedChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedNotification, nullptr);

    // wlan0 is primary; eth0's link flapping must not move Network.connected.
    EXPECT_CALL(mockNetwork, GetPrimaryInterface(_))
        .WillOnce(DoAll(SetArgReferee<0>("wlan0"), Return(Core::ERROR_NONE)));

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("Network.onConnectedChanged"), _, _)).Times(0);
    capturedNotification->onInterfaceStateChange(Exchange::INetworkManager::INTERFACE_LINK_DOWN, "eth0");

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(NetworkNotificationTest, AGC_L1_165_NetworkNotification_onInterfaceStateChange_IrrelevantState_DoesNotDispatch)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "Network.onConnectedChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedNotification, nullptr);

    // INTERFACE_ACQUIRING_IP doesn't cleanly map to connected/disconnected; the handler
    // should bail before even asking for the primary interface.
    EXPECT_CALL(mockNetwork, GetPrimaryInterface(_)).Times(0);
    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("Network.onConnectedChanged"), _, _)).Times(0);
    capturedNotification->onInterfaceStateChange(Exchange::INetworkManager::INTERFACE_ACQUIRING_IP, "wlan0");

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(NetworkNotificationTest, AGC_L1_166_NetworkNotification_DuplicateValue_DispatchesOnce)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "Network.onConnectedChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedNotification, nullptr);

    // Both onActiveInterfaceChange and onInterfaceStateChange can fire for the same
    // real transition; repeating the same resulting value must not re-dispatch.
    EXPECT_CALL(mockNetwork, GetPrimaryInterface(_))
        .WillRepeatedly(DoAll(SetArgReferee<0>("wlan0"), Return(Core::ERROR_NONE)));

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("Network.onConnectedChanged"),
                               ::testing::HasSubstr("\"value\":false"), _)).Times(1);
    capturedNotification->onInterfaceStateChange(Exchange::INetworkManager::INTERFACE_LINK_DOWN, "wlan0");
    capturedNotification->onInterfaceStateChange(Exchange::INetworkManager::INTERFACE_LINK_DOWN, "wlan0");

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

} // namespace
