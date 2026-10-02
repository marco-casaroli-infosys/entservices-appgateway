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
#include <thread>
#include <chrono>
#include <atomic>

#include "Module.h"

#define private public
#include "AppActions.h"
#include "AppActionsImplementation.h"
#undef private

#include "ServiceMock.h"
#include "WorkerPoolImplementation.h"
#include "ThunderPortability.h"

using namespace WPEFramework;
using namespace WPEFramework::Plugin;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::StrEq;

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

// Brief sleep to allow WorkerPool to dispatch pending async NotifyJobs.
static void DrainNotifyJobs()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

// --------------------------------------------------------------------------
// Mock IAppActions::INotification for capturing callbacks
// --------------------------------------------------------------------------
class MockAppActionsNotification : public Exchange::IAppActions::INotification {
public:
    MockAppActionsNotification() = default;
    virtual ~MockAppActionsNotification() = default;

    MOCK_METHOD(void, OnActionStartRequest,
                (const string& initiator, const string& intent, const string& handlerAppId),
                (override));

    MOCK_METHOD(uint32_t, AddRef, (), (const, override));
    MOCK_METHOD(uint32_t, Release, (), (const, override));
    MOCK_METHOD(void*, QueryInterface, (const uint32_t interfaceNumber), (override));
};

// --------------------------------------------------------------------------
// ICOMLink helpers for AppActionsPluginTest
//
// IShell::Root<T>() is implemented in libWPEFrameworkPlugins.so and calls
// IShell::COMLink()->Instantiate(...). By providing a TestCOMLink that
// returns a real AppActionsImplementation from Instantiate() we can exercise
// the Initialize/Deinitialize paths deterministically without spawning an
// out-of-process host.
// --------------------------------------------------------------------------

// Minimal IRemoteConnection stub — used to exercise the Deinitialize path
// that calls connection->Terminate() when mConnectionId != 0.
class AATestRemoteConnection : public WPEFramework::RPC::IRemoteConnection {
public:
    explicit AATestRemoteConnection(uint32_t id)
        : mId(id), mRefCount(1), mTerminateCalled(false) {}

    uint32_t AddRef() const override { return ++mRefCount; }
    uint32_t Release() const override {
        uint32_t r = --mRefCount;
        if (r == 0) delete this;
        return r;
    }
    void* QueryInterface(const uint32_t) override { return nullptr; }
    uint32_t Id() const override { return mId; }
    uint32_t RemoteId() const override { return 0; }
    void* Acquire(const uint32_t, const string&, const uint32_t, const uint32_t) override { return nullptr; }
    void Terminate() override { mTerminateCalled = true; }
    uint32_t Launch() override { return Core::ERROR_NONE; }
    void PostMortem() override {}

    bool WasTerminated() const { return mTerminateCalled; }

private:
    uint32_t mId;
    mutable std::atomic<uint32_t> mRefCount;
    bool mTerminateCalled;
};

// ICOMLink that returns a real AppActionsImplementation from Instantiate().
class AATestCOMLink : public WPEFramework::PluginHost::IShell::ICOMLink {
public:
    AATestCOMLink() : mRemoteConnection(nullptr) {}

    void Register(WPEFramework::RPC::IRemoteConnection::INotification*) override {}
    void Unregister(const WPEFramework::RPC::IRemoteConnection::INotification*) override {}
    void Register(WPEFramework::PluginHost::IShell::ICOMLink::INotification*) override {}
    void Unregister(WPEFramework::PluginHost::IShell::ICOMLink::INotification*) override {}

    WPEFramework::RPC::IRemoteConnection* RemoteConnection(const uint32_t) override {
        if (mRemoteConnection != nullptr) {
            mRemoteConnection->AddRef();
        }
        return mRemoteConnection;
    }

    void* Instantiate(const WPEFramework::RPC::Object& object,
                      const uint32_t, uint32_t& connectionId) override {
        connectionId = 42;
        if (object.ClassName().find("AppActionsImplementation") != std::string::npos) {
            // Core::Sink provides stable AddRef/Release (never self-deletes).
            mImpl.AddRef();
            return static_cast<Exchange::IAppActions*>(&mImpl);
        }
        return nullptr;
    }

    void SetRemoteConnection(WPEFramework::RPC::IRemoteConnection* conn) {
        mRemoteConnection = conn;
    }

private:
    WPEFramework::RPC::IRemoteConnection* mRemoteConnection;
    Core::Sink<AppActionsImplementation> mImpl;
};

// ICOMLink that always fails instantiation (exercises the failure path).
class AAFailingCOMLink : public WPEFramework::PluginHost::IShell::ICOMLink {
public:
    void Register(WPEFramework::RPC::IRemoteConnection::INotification*) override {}
    void Unregister(const WPEFramework::RPC::IRemoteConnection::INotification*) override {}
    void Register(WPEFramework::PluginHost::IShell::ICOMLink::INotification*) override {}
    void Unregister(WPEFramework::PluginHost::IShell::ICOMLink::INotification*) override {}
    WPEFramework::RPC::IRemoteConnection* RemoteConnection(const uint32_t) override { return nullptr; }
    void* Instantiate(const WPEFramework::RPC::Object&, const uint32_t,
                      uint32_t& connectionId) override {
        connectionId = 0;
        return nullptr;
    }
};

// --------------------------------------------------------------------------
// AppActions Plugin Tests (Shell)
// --------------------------------------------------------------------------
class AppActionsPluginTest : public ::testing::Test {
protected:
    NiceMock<ServiceMock> service;
    AATestCOMLink comLink;
    AAFailingCOMLink failingComLink;

    void SetUp() override
    {
        ON_CALL(service, AddRef()).WillByDefault(Return(1));
        ON_CALL(service, Release()).WillByDefault(Return(Core::ERROR_NONE));
        ON_CALL(service, QueryInterfaceByCallsign(_, _)).WillByDefault(Return(nullptr));
        ON_CALL(service, COMLink()).WillByDefault(Return(&comLink));
    }
};

/* ---------- Plugin Lifecycle Tests ---------- */

TEST_F(AppActionsPluginTest, AA_L1_001_Initialize_Success)
{
    // COMLink returns a real AppActionsImplementation — Initialize must succeed.
    Core::Sink<AppActions> plugin;
    ON_CALL(service, COMLink()).WillByDefault(Return(&comLink));

    const string result = plugin.Initialize(&service);

    EXPECT_TRUE(result.empty()) << "Initialize should return empty string on success, got: " << result;
    EXPECT_NE(nullptr, plugin.mAppActions) << "mAppActions must be non-null after successful Initialize";
    EXPECT_NE(nullptr, plugin.mService)    << "mService must be set after Initialize";

    plugin.Deinitialize(&service);

    EXPECT_EQ(nullptr, plugin.mAppActions) << "mAppActions must be null after Deinitialize";
    EXPECT_EQ(nullptr, plugin.mService)    << "mService must be null after Deinitialize";
}

TEST_F(AppActionsPluginTest, AA_L1_002_Initialize_WithNullCOMLink_SucceedsAndLogsWarning)
{
    // When COMLink() returns null the plugin cannot register for OOP crash-recovery
    // notifications (a SYSLOG warning is emitted), but Initialize must still succeed
    // because AppActionsImplementation is created in-process via SERVICE_REGISTRATION.
    Core::Sink<AppActions> plugin;
    ON_CALL(service, COMLink()).WillByDefault(Return(nullptr));

    const string result = plugin.Initialize(&service);

    EXPECT_TRUE(result.empty())
        << "Initialize should succeed via in-process creation even when COMLink is null";
    EXPECT_NE(nullptr, plugin.mAppActions)
        << "mAppActions must be set after successful in-process Initialize";

    plugin.Deinitialize(&service);
}

TEST_F(AppActionsPluginTest, AA_L1_003_Information_ReturnsEmpty)
{
    Core::Sink<AppActions> plugin;
    ON_CALL(service, COMLink()).WillByDefault(Return(&comLink));

    plugin.Initialize(&service);

    EXPECT_TRUE(plugin.Information().empty());

    plugin.Deinitialize(&service);
}

TEST_F(AppActionsPluginTest, AA_L1_004_Initialize_Deinitialize_TwoCycles_NoLeak)
{
    // Two full Initialize + Deinitialize cycles must leave the plugin in a
    // clean state with no dangling pointers or reference-count leaks.
    Core::Sink<AppActions> plugin;
    ON_CALL(service, COMLink()).WillByDefault(Return(&comLink));

    // First cycle.
    EXPECT_TRUE(plugin.Initialize(&service).empty());
    plugin.Deinitialize(&service);
    EXPECT_EQ(nullptr, plugin.mAppActions);
    EXPECT_EQ(nullptr, plugin.mService);
    EXPECT_EQ(0u,      plugin.mConnectionId);

    // Second cycle.
    EXPECT_TRUE(plugin.Initialize(&service).empty());
    plugin.Deinitialize(&service);
    EXPECT_EQ(nullptr, plugin.mAppActions);
    EXPECT_EQ(nullptr, plugin.mService);
    EXPECT_EQ(0u,      plugin.mConnectionId);
}

TEST_F(AppActionsPluginTest, AA_L1_005_Deinitialize_WithRemoteConnection_Terminates)
{
    // Verify that Deinitialize calls Terminate() on the remote connection when
    // mConnectionId != 0.
    // In the L1 environment Root<>() creates the impl in-process (mConnectionId=0).
    // We manually set mConnectionId=42 after Initialize to simulate an OOP scenario
    // and verify that Deinitialize retrieves and terminates the connection via COMLink.
    Core::Sink<AppActions> plugin;
    auto* remoteConn = new AATestRemoteConnection(42);
    comLink.SetRemoteConnection(remoteConn);
    ON_CALL(service, COMLink()).WillByDefault(Return(&comLink));

    EXPECT_TRUE(plugin.Initialize(&service).empty());

    // Simulate OOP connection: Root<>() gives 0 in-process; override to 42.
    plugin.mConnectionId = 42;

    plugin.Deinitialize(&service);

    EXPECT_TRUE(remoteConn->WasTerminated())
        << "Deinitialize must call Terminate() on the remote connection";
}

TEST_F(AppActionsPluginTest, AA_L1_006_Constructor_Destructor_Lifecycle)
{
    // Constructing and destroying the plugin without Initialize must not crash.
    {
        Core::Sink<AppActions> tempPlugin;
    }
    EXPECT_TRUE(true);
}

// --------------------------------------------------------------------------
// AppActionsImplementation Tests
// --------------------------------------------------------------------------
class AppActionsImplementationTest : public ::testing::Test {
protected:
    Core::Sink<AppActionsImplementation> impl;
    NiceMock<ServiceMock> service;

    void SetUp() override
    {
        EXPECT_CALL(service, AddRef()).Times(AnyNumber());
        EXPECT_CALL(service, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    }

    void TearDown() override
    {
        // Drain any pending async NotifyJobs before the fixture (and impl) destruct.
        // ActionStart() submits a NotifyJob that holds AddRef() on impl. If the job
        // has not yet run when Core::Sink<AppActionsImplementation> destructs, the
        // Sink prints "Oops this is scary" and then the job executes against a
        // destroyed object → pure virtual method called → abort.
        DrainNotifyJobs();
    }
};

/* ---------- ActionStart Tests ---------- */

TEST_F(AppActionsImplementationTest, AA_L1_010_ActionStart_ValidParams)
{
    const auto rc = impl.ActionStart("testInitiator", "{\"action\":\"test\"}", "testApp");
    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(AppActionsImplementationTest, AA_L1_011_ActionStart_EmptyInitiator)
{
    const auto rc = impl.ActionStart("", "{\"action\":\"test\"}", "testApp");
    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(AppActionsImplementationTest, AA_L1_012_ActionStart_EmptyIntent)
{
    const auto rc = impl.ActionStart("initiator", "", "testApp");
    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(AppActionsImplementationTest, AA_L1_013_ActionStart_EmptyHandlerAppId)
{
    const auto rc = impl.ActionStart("initiator", "intent", "");
    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(AppActionsImplementationTest, AA_L1_014_ActionStart_AllEmptyParams)
{
    const auto rc = impl.ActionStart("", "", "");
    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(AppActionsImplementationTest, AA_L1_015_ActionStart_SpecialCharsInParams)
{
    const auto rc = impl.ActionStart("voice-assistant/v2.0@test", 
                                     "{\"action\":\"play\",\"data\":{\"contentId\":\"123\"}}", 
                                     "netflix/player");
    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(AppActionsImplementationTest, AA_L1_016_ActionStart_UnicodeChars)
{
    const auto rc = impl.ActionStart("voice", "{\"title\":\"日本語テスト\"}", "app");
    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(AppActionsImplementationTest, AA_L1_017_ActionStart_LongStrings)
{
    std::string longStr(1000, 'x');
    const auto rc = impl.ActionStart(longStr, longStr, longStr);
    EXPECT_EQ(Core::ERROR_NONE, rc);
}

/* ---------- Register/Unregister Tests ---------- */

TEST_F(AppActionsImplementationTest, AA_L1_020_Register_Success)
{
    NiceMock<MockAppActionsNotification> notification;
    EXPECT_CALL(notification, AddRef()).Times(AnyNumber());
    EXPECT_CALL(notification, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    
    const auto rc = impl.Register(&notification);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    
    impl.Unregister(&notification);
}

TEST_F(AppActionsImplementationTest, AA_L1_021_Register_Duplicate_ReturnsError)
{
    NiceMock<MockAppActionsNotification> notification;
    EXPECT_CALL(notification, AddRef()).Times(AnyNumber());
    EXPECT_CALL(notification, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    
    const auto rc1 = impl.Register(&notification);
    EXPECT_EQ(Core::ERROR_NONE, rc1);
    
    const auto rc2 = impl.Register(&notification);
    EXPECT_EQ(Core::ERROR_GENERAL, rc2);
    
    impl.Unregister(&notification);
}

TEST_F(AppActionsImplementationTest, AA_L1_022_Register_Multiple_Success)
{
    NiceMock<MockAppActionsNotification> notification1;
    NiceMock<MockAppActionsNotification> notification2;
    
    EXPECT_CALL(notification1, AddRef()).Times(AnyNumber());
    EXPECT_CALL(notification1, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    EXPECT_CALL(notification2, AddRef()).Times(AnyNumber());
    EXPECT_CALL(notification2, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    
    EXPECT_EQ(Core::ERROR_NONE, impl.Register(&notification1));
    EXPECT_EQ(Core::ERROR_NONE, impl.Register(&notification2));
    
    impl.Unregister(&notification1);
    impl.Unregister(&notification2);
}

TEST_F(AppActionsImplementationTest, AA_L1_023_Unregister_Success)
{
    NiceMock<MockAppActionsNotification> notification;
    EXPECT_CALL(notification, AddRef()).Times(AnyNumber());
    EXPECT_CALL(notification, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    
    impl.Register(&notification);
    
    const auto rc = impl.Unregister(&notification);
    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(AppActionsImplementationTest, AA_L1_024_Unregister_NotRegistered_ReturnsError)
{
    NiceMock<MockAppActionsNotification> notification;
    EXPECT_CALL(notification, AddRef()).Times(AnyNumber());
    EXPECT_CALL(notification, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    
    const auto rc = impl.Unregister(&notification);
    EXPECT_EQ(Core::ERROR_GENERAL, rc);
}

TEST_F(AppActionsImplementationTest, AA_L1_025_Unregister_Twice_ReturnsError)
{
    NiceMock<MockAppActionsNotification> notification;
    EXPECT_CALL(notification, AddRef()).Times(AnyNumber());
    EXPECT_CALL(notification, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    
    impl.Register(&notification);
    
    EXPECT_EQ(Core::ERROR_NONE, impl.Unregister(&notification));
    EXPECT_EQ(Core::ERROR_GENERAL, impl.Unregister(&notification));
}

/* ---------- Notification Dispatch Tests ---------- */

TEST_F(AppActionsImplementationTest, AA_L1_030_ActionStart_DispatchesToNotification)
{
    NiceMock<MockAppActionsNotification> notification;
    EXPECT_CALL(notification, AddRef()).Times(AnyNumber());
    EXPECT_CALL(notification, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    
    EXPECT_CALL(notification, OnActionStartRequest(
        StrEq("voice"), 
        StrEq("{\"action\":\"play\"}"), 
        StrEq("netflix")))
        .Times(1);
    
    impl.Register(&notification);
    
    impl.ActionStart("voice", "{\"action\":\"play\"}", "netflix");
    DrainNotifyJobs();
    
    impl.Unregister(&notification);
}

TEST_F(AppActionsImplementationTest, AA_L1_031_ActionStart_DispatchesToMultipleNotifications)
{
    NiceMock<MockAppActionsNotification> notification1;
    NiceMock<MockAppActionsNotification> notification2;
    
    EXPECT_CALL(notification1, AddRef()).Times(AnyNumber());
    EXPECT_CALL(notification1, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    EXPECT_CALL(notification2, AddRef()).Times(AnyNumber());
    EXPECT_CALL(notification2, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    
    EXPECT_CALL(notification1, OnActionStartRequest(_, _, _)).Times(1);
    EXPECT_CALL(notification2, OnActionStartRequest(_, _, _)).Times(1);
    
    impl.Register(&notification1);
    impl.Register(&notification2);
    
    impl.ActionStart("initiator", "intent", "appId");
    DrainNotifyJobs();
    
    impl.Unregister(&notification1);
    impl.Unregister(&notification2);
}

TEST_F(AppActionsImplementationTest, AA_L1_032_ActionStart_NoDispatchAfterUnregister)
{
    NiceMock<MockAppActionsNotification> notification;
    EXPECT_CALL(notification, AddRef()).Times(AnyNumber());
    EXPECT_CALL(notification, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    
    // Should be called once before unregister
    EXPECT_CALL(notification, OnActionStartRequest(_, _, _)).Times(1);
    
    impl.Register(&notification);
    impl.ActionStart("init1", "intent1", "app1");
    DrainNotifyJobs();
    impl.Unregister(&notification);
    
    // This should NOT trigger notification
    impl.ActionStart("init2", "intent2", "app2");
    DrainNotifyJobs();
}

TEST_F(AppActionsImplementationTest, AA_L1_033_ActionStart_MultipleCalls)
{
    NiceMock<MockAppActionsNotification> notification;
    EXPECT_CALL(notification, AddRef()).Times(AnyNumber());
    EXPECT_CALL(notification, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    
    EXPECT_CALL(notification, OnActionStartRequest(_, _, _)).Times(3);
    
    impl.Register(&notification);
    
    impl.ActionStart("init1", "intent1", "app1");
    impl.ActionStart("init2", "intent2", "app2");
    impl.ActionStart("init3", "intent3", "app3");
    DrainNotifyJobs();
    
    impl.Unregister(&notification);
}

/* ---------- Configure Tests ---------- */

TEST_F(AppActionsImplementationTest, AA_L1_040_Configure_ValidService)
{
    const auto rc = impl.Configure(&service);
    // Configure with a non-null service stores the pointer and returns ERROR_NONE
    EXPECT_EQ(Core::ERROR_NONE, rc);
    impl.Deinitialize(&service);
}

TEST_F(AppActionsImplementationTest, AA_L1_041_Configure_NullService)
{
    const auto rc = impl.Configure(nullptr);
    EXPECT_EQ(Core::ERROR_GENERAL, rc);
}

/* ---------- Initialize/Deinitialize Tests ---------- */

TEST_F(AppActionsImplementationTest, AA_L1_050_Initialize_CallsConfigure)
{
    const string result = impl.Initialize(&service);
    // Initialize delegates to Configure; for a non-null service Configure returns
    // ERROR_NONE so Initialize returns an empty error string.
    EXPECT_TRUE(result.empty());

    impl.Deinitialize(&service);
}

TEST_F(AppActionsImplementationTest, AA_L1_051_Deinitialize_ReleasesService)
{
    impl.Initialize(&service);
    impl.Deinitialize(&service);

    // Deinitialize must release and null-out the stored service pointer
    EXPECT_EQ(nullptr, impl.mService);
}

TEST_F(AppActionsImplementationTest, AA_L1_052_Information_ReturnsEmpty)
{
    const string info = impl.Information();
    EXPECT_TRUE(info.empty());
}

TEST_F(AppActionsImplementationTest, AA_L1_053_Deinitialize_WithoutInitialize)
{
    // mService is null before Initialize; Deinitialize must be a no-op and not crash
    impl.Deinitialize(&service);
    EXPECT_EQ(nullptr, impl.mService);
}

/* ---------- Interface Map Tests ---------- */

TEST_F(AppActionsImplementationTest, AA_L1_060_InterfaceMap_IPlugin)
{
    auto* pluginIface = static_cast<PluginHost::IPlugin*>(
        impl.QueryInterface(PluginHost::IPlugin::ID));
    EXPECT_NE(nullptr, pluginIface);
    if (nullptr != pluginIface) {
        pluginIface->Release();
    }
}

TEST_F(AppActionsImplementationTest, AA_L1_061_InterfaceMap_IAppActions)
{
    auto* appActionsIface = static_cast<Exchange::IAppActions*>(
        impl.QueryInterface(Exchange::IAppActions::ID));
    EXPECT_NE(nullptr, appActionsIface);
    if (nullptr != appActionsIface) {
        appActionsIface->Release();
    }
}

TEST_F(AppActionsImplementationTest, AA_L1_062_InterfaceMap_IConfiguration)
{
    auto* configIface = static_cast<Exchange::IConfiguration*>(
        impl.QueryInterface(Exchange::IConfiguration::ID));
    EXPECT_NE(nullptr, configIface);
    if (nullptr != configIface) {
        configIface->Release();
    }
}

/* ---------- Re-registration Tests ---------- */

TEST_F(AppActionsImplementationTest, AA_L1_070_Register_AfterUnregister_Success)
{
    NiceMock<MockAppActionsNotification> notification;
    EXPECT_CALL(notification, AddRef()).Times(AnyNumber());
    EXPECT_CALL(notification, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    
    impl.Register(&notification);
    impl.Unregister(&notification);
    
    // Re-register should succeed
    const auto rc = impl.Register(&notification);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    
    impl.Unregister(&notification);
}

TEST_F(AppActionsImplementationTest, AA_L1_071_Unregister_PartialFromMultiple)
{
    NiceMock<MockAppActionsNotification> notification1;
    NiceMock<MockAppActionsNotification> notification2;
    
    EXPECT_CALL(notification1, AddRef()).Times(AnyNumber());
    EXPECT_CALL(notification1, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    EXPECT_CALL(notification2, AddRef()).Times(AnyNumber());
    EXPECT_CALL(notification2, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    
    impl.Register(&notification1);
    impl.Register(&notification2);
    
    // Unregister only notification1
    impl.Unregister(&notification1);
    
    // notification2 should still receive events
    EXPECT_CALL(notification2, OnActionStartRequest(_, _, _)).Times(1);
    
    impl.ActionStart("init", "intent", "app");
    DrainNotifyJobs();
    
    impl.Unregister(&notification2);
}

} // anonymous namespace
