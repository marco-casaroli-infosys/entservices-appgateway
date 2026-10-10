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
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include "Module.h"

#define private public
#include "AppGatewayCommon.h"
#undef private

#include "ServiceMock.h"
#include "LifecycleManagerMock.h"
#include "WindowManagerMock.h"
#include "AppActionsMock.h"
#include "MockEmitter.h"
#include "AppSessionGuardMock.h"
#include "ThunderPortability.h"
#include "WorkerPoolImplementation.h"

#include <cstdio>
#include <unistd.h>

using namespace WPEFramework;

namespace {
// Removes /etc/rdkappmanagers whether it is a file or a directory.
// Uses unlink/rmdir directly instead of stat() + branch to avoid
// a TOCTOU (time-of-check time-of-use) filesystem race condition.
inline void RemoveRdkAppManagers()
{
    const char* path = "/etc/rdkappmanagers";
    // unlink() removes files; fails harmlessly with EISDIR/EPERM on directories.
    // rmdir() removes empty directories; fails harmlessly with ENOTDIR on files.
    // If the path does not exist, both fail silently (ENOENT).
    if (0 != unlink(path)) {
        (void)rmdir(path);
    }
}
} // anonymous namespace
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

static Exchange::GatewayContext MakeContext(const std::string& appId = "test.app")
{
    Exchange::GatewayContext ctx;
    ctx.appId = appId;
    ctx.connectionId = 100;
    ctx.requestId = 200;
    return ctx;
}

class LifecycleDelegateTest : public ::testing::Test {
protected:
    Core::Sink<AppGatewayCommon> plugin;
    NiceMock<ServiceMock> service;
    NiceMock<LifecycleManagerStateMock> mockLifecycle;
    NiceMock<WindowManagerMock> mockWindowMgr;
    NiceMock<AppActionsMock> mockAppActions;
    Exchange::ILifecycleManagerState::INotification* capturedNotification = nullptr;
    Exchange::IRDKWindowManager::INotification* capturedWMNotification = nullptr;
    std::vector<MockEmitter*> heapEmitters;
    bool initialized_ = false;

    void SetUp() override
    {
        // LifecycleDelegate only registers notifications when /etc/rdkappmanagers exists
        // (ConfigUtils::useAppManagers() gate). Ensure it exists as a regular file.
        // Remove first in case a prior run or CI step left it as a directory.
        RemoveRdkAppManagers();
        std::FILE* f = std::fopen("/etc/rdkappmanagers", "w");
        if (nullptr == f) {
            GTEST_SKIP() << "Skipping LifecycleDelegate tests: unable to create /etc/rdkappmanagers (insufficient permissions or read-only filesystem)";
        }
        std::fclose(f);

        ON_CALL(service, QueryInterfaceByCallsign(_, _))
            .WillByDefault(Return(nullptr));

        ON_CALL(service, QueryInterfaceByCallsign(Exchange::ILifecycleManagerState::ID, ::testing::StrEq("org.rdk.LifecycleManager")))
            .WillByDefault(::testing::Invoke([this](uint32_t, const string&) -> void* {
                mockLifecycle.AddRef();
                return static_cast<Exchange::ILifecycleManagerState*>(&mockLifecycle);
            }));

        // Production code uses "org.rdk.RDKWindowManager" (WINDOW_MANAGER_CALLSIGN)
        ON_CALL(service, QueryInterfaceByCallsign(Exchange::IRDKWindowManager::ID, ::testing::StrEq("org.rdk.RDKWindowManager")))
            .WillByDefault(::testing::Invoke([this](uint32_t, const string&) -> void* {
                mockWindowMgr.AddRef();
                return static_cast<Exchange::IRDKWindowManager*>(&mockWindowMgr);
            }));

        ON_CALL(service, QueryInterfaceByCallsign(Exchange::IAppActions::ID, ::testing::StrEq("org.rdk.AppActions")))
            .WillByDefault(::testing::Invoke([this](uint32_t, const string&) -> void* {
                mockAppActions.AddRef();
                return static_cast<Exchange::IAppActions*>(&mockAppActions);
            }));

        EXPECT_CALL(service, AddRef()).Times(AnyNumber());
        EXPECT_CALL(service, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
        // Capture the INotification* when Register is called
        EXPECT_CALL(mockLifecycle, Register(_)).Times(AnyNumber())
            .WillRepeatedly(::testing::Invoke([this](Exchange::ILifecycleManagerState::INotification* n) -> uint32_t {
                capturedNotification = n;
                return Core::ERROR_NONE;
            }));
        EXPECT_CALL(mockLifecycle, Unregister(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
        // Capture IRDKWindowManager::INotification* when Register is called
        EXPECT_CALL(mockWindowMgr, Register(_)).Times(AnyNumber())
            .WillRepeatedly(::testing::Invoke([this](Exchange::IRDKWindowManager::INotification* n) -> uint32_t {
                capturedWMNotification = n;
                return Core::ERROR_NONE;
            }));
        EXPECT_CALL(mockWindowMgr, Unregister(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));

        const string response = plugin.Initialize(&service);
        ASSERT_TRUE(response.empty());
        initialized_ = true;
    }

    void TearDown() override
    {
        if (!initialized_) return;
        plugin.Deinitialize(&service);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        for (auto* e : heapEmitters) {
            testing::Mock::VerifyAndClearExpectations(e);
            delete e;
        }
        heapEmitters.clear();
        RemoveRdkAppManagers();
    }
};

/* ---------- Authenticate / GetSessionId ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_162_Authenticate_NotFound)
{
    string appId;
    const auto rc = plugin.Authenticate("unknown-session", appId);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
    EXPECT_TRUE(appId.empty());
}

TEST_F(LifecycleDelegateTest, AGC_L1_163_GetSessionId_NotFound)
{
    string sessionId;
    const auto rc = plugin.GetSessionId("unknown.app", sessionId);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
    EXPECT_TRUE(sessionId.empty());
}

/* ---------- LifecycleClose ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_164_LifecycleClose_UserExit)
{
    EXPECT_CALL(mockLifecycle, CloseApp("test.app", Exchange::ILifecycleManagerState::USER_EXIT))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle.close", R"({"reason":"userExit"})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(LifecycleDelegateTest, AGC_L1_165_LifecycleClose_ErrorReason)
{
    EXPECT_CALL(mockLifecycle, CloseApp("test.app", Exchange::ILifecycleManagerState::ERROR))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle.close", R"({"reason":"other"})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(LifecycleDelegateTest, AGC_L1_166_LifecycleClose_BadPayload)
{
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle.close", "{invalid", result);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
}

/* ---------- Lifecycle2Close ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_167_Lifecycle2Close_Deactivate)
{
    EXPECT_CALL(mockLifecycle, CloseApp("test.app", Exchange::ILifecycleManagerState::USER_EXIT))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle2.close", R"({"type":"deactivate"})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(LifecycleDelegateTest, AGC_L1_168_Lifecycle2Close_KillReload)
{
    EXPECT_CALL(mockLifecycle, CloseApp("test.app", Exchange::ILifecycleManagerState::KILL_AND_RUN))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle2.close", R"({"type":"killReload"})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(LifecycleDelegateTest, AGC_L1_169_Lifecycle2Close_KillReactivate)
{
    EXPECT_CALL(mockLifecycle, CloseApp("test.app", Exchange::ILifecycleManagerState::KILL_AND_ACTIVATE))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle2.close", R"({"type":"killReactivate"})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(LifecycleDelegateTest, AGC_L1_170_Lifecycle2Close_DefaultReason)
{
    EXPECT_CALL(mockLifecycle, CloseApp("test.app", Exchange::ILifecycleManagerState::ERROR))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle2.close", R"({"type":"unknown"})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
}

/* ---------- LifecycleReady ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_171_LifecycleReady_Success)
{
    EXPECT_CALL(mockLifecycle, AppReady("test.app"))
        .WillOnce(Return(Core::ERROR_NONE));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle.ready", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
}

/* ---------- LifecycleFinished ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_172_LifecycleFinished_ReturnsNull)
{
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle.finished", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

/* ---------- LifecycleState / Lifecycle2State (empty state) ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_173_LifecycleState_DefaultState)
{
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle.state", "{}", result);

    // Without prior lifecycle updates, state is default
    EXPECT_EQ(Core::ERROR_NONE, rc);
}

TEST_F(LifecycleDelegateTest, AGC_L1_174_Lifecycle2State_DefaultState)
{
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle2.state", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("\"unloaded\"", result);
}

/* ---------- DispatchLastIntent / GetLastIntent ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_175_DispatchLastIntent_NoIntent)
{
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "commoninternal.dispatchintent", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

TEST_F(LifecycleDelegateTest, AGC_L1_176_GetLastIntent_NoIntent)
{
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "commoninternal.getlastintent", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
}

/* ---------- Authenticate / GetSessionId success via lifecycle callback ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_177_Authenticate_Success_AfterLifecycleEvent)
{
    // Fire OnAppLifecycleStateChanged with INITIALIZING to populate the map
    ASSERT_NE(capturedNotification, nullptr);
    capturedNotification->OnAppLifecycleStateChanged(
        "com.example.myapp",              // appId
        "instance-abc-123",                // appInstanceId
        Exchange::ILifecycleManager::UNLOADED,      // oldState
        Exchange::ILifecycleManager::INITIALIZING,   // newState (triggers map insertion)
        ""                                 // navigationIntent
    );

    string appId;
    const auto rc = plugin.Authenticate("instance-abc-123", appId);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("com.example.myapp", appId);
}

TEST_F(LifecycleDelegateTest, AGC_L1_178_GetSessionId_Success_AfterLifecycleEvent)
{
    ASSERT_NE(capturedNotification, nullptr);
    capturedNotification->OnAppLifecycleStateChanged(
        "com.example.myapp",
        "instance-abc-123",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        ""
    );

    string sessionId;
    const auto rc = plugin.GetSessionId("com.example.myapp", sessionId);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("instance-abc-123", sessionId);
}

/* ---------- LifecycleReady: AppReady failure ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_179_LifecycleReady_AppReadyFails)
{
    EXPECT_CALL(mockLifecycle, AppReady(_))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle.ready", "{}", result);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
}

/* ---------- LifecycleClose: CloseApp failure ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_180_LifecycleClose_CloseAppFails)
{
    EXPECT_CALL(mockLifecycle, CloseApp(_, _))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle.close", R"({"reason":"userExit"})", result);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
}

/* ---------- Lifecycle2Close: CloseApp failure ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_181_Lifecycle2Close_CloseAppFails)
{
    EXPECT_CALL(mockLifecycle, CloseApp(_, _))
        .WillOnce(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle2.close", R"({"type":"deactivate"})", result);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
}

/* ---------- Lifecycle2Close: bad payload ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_182_Lifecycle2Close_BadPayload)
{
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle2.close", "{invalid", result);

    EXPECT_NE(Core::ERROR_NONE, rc);
}

/* ---------- LifecycleState with populated map ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_183_LifecycleState_AfterLifecycleEvent)
{
    ASSERT_NE(capturedNotification, nullptr);
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app",
        "instance-xyz",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        ""
    );

    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle.state", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    // Should contain state info rather than empty
    EXPECT_FALSE(result.empty());
}

/* ---------- DispatchLastIntent with navigation intent ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_184_DispatchLastIntent_WithIntent)
{
    ASSERT_NE(capturedNotification, nullptr);
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app",
        "instance-abc",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        "playback://content/123"
    );

    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "commoninternal.dispatchintent", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
}

/* ---------- Lifecycle2State with populated registry ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_185_Lifecycle2State_AfterLifecycleEvent)
{
    ASSERT_NE(capturedNotification, nullptr);
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app",
        "instance-xyz",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        ""
    );

    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle2.state", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("\"initializing\"", result);
}

/* ---------- GetLastIntent with populated intent ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_186_GetLastIntent_WithIntent)
{
    ASSERT_NE(capturedNotification, nullptr);
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app",
        "instance-abc",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        "search://query=testing"
    );

    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "commoninternal.getlastintent", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("\"intentId\":"),  std::string::npos);
    EXPECT_NE(result.find("\"intent\":"),   std::string::npos);
    EXPECT_NE(result.find("search://query=testing"), std::string::npos);
}

/* ---------- SetIntent tests ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_201_SetIntent_StoresAndGetLastIntent)
{
    const auto ctx = MakeContext("test.app");
    string result;

    // Set a non-empty intent via commoninternal.setintent
    const auto rc1 = plugin.HandleAppGatewayRequest(ctx, "commoninternal.setintent", R"({"action":"play","content":"video456"})", result);
    EXPECT_EQ(Core::ERROR_NONE, rc1);
    EXPECT_EQ("null", result);

    // Verify getlastintent returns the stored intent
    const auto rc2 = plugin.HandleAppGatewayRequest(ctx, "commoninternal.getlastintent", "{}", result);
    EXPECT_EQ(Core::ERROR_NONE, rc2);
    EXPECT_NE(result.find("\"intentId\":"), std::string::npos);
    EXPECT_NE(result.find("\"intent\":"), std::string::npos);
    EXPECT_NE(result.find("video456"), std::string::npos);
}

TEST_F(LifecycleDelegateTest, AGC_L1_202_SetIntent_StoresAndGetActionsIntent)
{
    const auto ctx = MakeContext("test.app");
    string result;

    // Set a non-empty intent via commoninternal.setintent
    const auto rc1 = plugin.HandleAppGatewayRequest(ctx, "commoninternal.setintent", R"({"type":"search","query":"test"})", result);
    EXPECT_EQ(Core::ERROR_NONE, rc1);

    // Verify actions.intent returns the stored intent
    const auto rc2 = plugin.HandleAppGatewayRequest(ctx, "actions.intent", "{}", result);
    EXPECT_EQ(Core::ERROR_NONE, rc2);
    EXPECT_NE(result.find("\"intentId\":"), std::string::npos);
    EXPECT_NE(result.find("\"intent\":"), std::string::npos);
    EXPECT_NE(result.find("test"), std::string::npos);
}

TEST_F(LifecycleDelegateTest, AGC_L1_203_SetIntent_EmptyPayload_NoOverwrite)
{
    const auto ctx = MakeContext("test.app");
    string result;

    // First set a non-empty intent via lifecycle callback
    ASSERT_NE(capturedNotification, nullptr);
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app",
        "instance-setintent",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        "playback://original/intent"
    );

    // Call setintent with empty payload - should no-op and not overwrite
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "commoninternal.setintent", "", result);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);

    // Verify the original intent is still there
    const auto rc2 = plugin.HandleAppGatewayRequest(ctx, "commoninternal.getlastintent", "{}", result);
    EXPECT_EQ(Core::ERROR_NONE, rc2);
    EXPECT_NE(result.find("playback://original/intent"), std::string::npos);
}

TEST_F(LifecycleDelegateTest, AGC_L1_204_SetIntent_NullStringPayload_NoOverwrite)
{
    const auto ctx = MakeContext("test.app");
    string result;

    // First set a non-empty intent via lifecycle callback
    ASSERT_NE(capturedNotification, nullptr);
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app",
        "instance-nulltest",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        "search://original/query"
    );

    // Call setintent with literal "null" string - should no-op and not overwrite
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "commoninternal.setintent", "null", result);
    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);

    // Verify the original intent is still there
    const auto rc2 = plugin.HandleAppGatewayRequest(ctx, "commoninternal.getlastintent", "{}", result);
    EXPECT_EQ(Core::ERROR_NONE, rc2);
    EXPECT_NE(result.find("search://original/query"), std::string::npos);
    // Should NOT contain the literal string "null" as the intent value
    EXPECT_EQ(result.find("\"intent\":\"null\""), std::string::npos);
}

/* ================================================================
 * Category B – Null LifecycleManagerState interface
 *
 * LifecycleDelegate is constructed but /etc/rdkappmanagers does NOT
 * exist, so ConfigUtils::useAppManagers() returns false and the
 * constructor skips Register(). The interfaces remain nullptr.
 * ================================================================ */

class LifecycleNoInterfaceTest : public ::testing::Test {
protected:
    static Core::Sink<AppGatewayCommon>* sPlugin;
    static NiceMock<ServiceMock>* sService;

    Core::Sink<AppGatewayCommon>& plugin = *sPlugin;
    NiceMock<ServiceMock>& service = *sService;

    static void SetUpTestSuite()
    {
        // Ensure /etc/rdkappmanagers does NOT exist (handles both file and directory)
        RemoveRdkAppManagers();

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

Core::Sink<AppGatewayCommon>* LifecycleNoInterfaceTest::sPlugin = nullptr;
NiceMock<ServiceMock>* LifecycleNoInterfaceTest::sService = nullptr;

TEST_F(LifecycleNoInterfaceTest, AGC_L1_187_LifecycleClose_NullInterface_ReturnsError)
{
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle.close", R"({"reason":"userExit"})", result);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
    EXPECT_NE(result.find("LifecycleManager not available"), std::string::npos);
}

TEST_F(LifecycleNoInterfaceTest, AGC_L1_188_Lifecycle2Close_NullInterface_ReturnsError)
{
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle2.close", R"({"type":"deactivate"})", result);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
    EXPECT_NE(result.find("LifecycleManager not available"), std::string::npos);
}

TEST_F(LifecycleNoInterfaceTest, AGC_L1_189_LifecycleReady_NullInterface_ReturnsOK)
{
    // LifecycleReady with null mLifecycleManagerState returns ERROR_NONE silently
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle.ready", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
}

/* ================================================================
 * Category C – Lifecycle notification dispatch
 *
 * Use capturedNotification from the main fixture to fire
 * OnAppLifecycleStateChanged and verify lifecycle events dispatch.
 * ================================================================ */

TEST_F(LifecycleDelegateTest, AGC_L1_190_LifecycleNotification_ActiveState_DispatchesForegroundOrBackground)
{
    ASSERT_NE(capturedNotification, nullptr);

    // First fire INITIALIZING to populate the map
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-001",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        ""
    );

    // Now transition to ACTIVE – should dispatch Lifecycle2.onStateChanged
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-001",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::ACTIVE,
        ""
    );

    // Verify state registry was updated
    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle2.state", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("\"active\"", result);
}

TEST_F(LifecycleDelegateTest, AGC_L1_191_LifecycleNotification_PausedState)
{
    ASSERT_NE(capturedNotification, nullptr);

    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-002",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        ""
    );

    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-002",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::PAUSED,
        ""
    );

    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle.state", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    // Lifecycle1 maps PAUSED to "inactive"
    EXPECT_NE(result.find("inactive"), std::string::npos);
}

TEST_F(LifecycleDelegateTest, AGC_L1_192_LifecycleNotification_SuspendedState)
{
    ASSERT_NE(capturedNotification, nullptr);

    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-003",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        ""
    );

    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-003",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::SUSPENDED,
        ""
    );

    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle.state", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("suspended"), std::string::npos);
}

TEST_F(LifecycleDelegateTest, AGC_L1_193_LifecycleNotification_TerminatingState)
{
    ASSERT_NE(capturedNotification, nullptr);

    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-004",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        ""
    );

    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-004",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::TERMINATING,
        ""
    );

    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "lifecycle2.state", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("\"terminating\"", result);
}

TEST_F(LifecycleDelegateTest, AGC_L1_194_LifecycleNotification_ActiveWithIntent_DispatchesLastIntent)
{
    ASSERT_NE(capturedNotification, nullptr);

    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-005",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        "playback://video/456"
    );

    // Transition to ACTIVE triggers DispatchLastKnownIntent
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-005",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::ACTIVE,
        ""
    );

    // Verify the intent is stored
    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "commoninternal.getlastintent", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
}

/* ---------- HandleEvent / HandleSubscription ---------- */

TEST_F(LifecycleDelegateTest, AGC_L1_195_HandleEvent_InvalidEvent_NotHandled)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();
    bool status = false;
    const auto rc = plugin.HandleAppEventNotifier(emitter, "invalid.nothingmatch", true, status);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    // Event is not recognized by any delegate — status should reflect unsuccessful match
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

TEST_F(LifecycleDelegateTest, AGC_L1_196_HandleEvent_ValidLifecycleEvent_Subscription)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();
    bool status = false;
    const auto rc = plugin.HandleAppEventNotifier(emitter, "Lifecycle.onForeground", true, status);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_TRUE(status);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

TEST_F(LifecycleDelegateTest, AGC_L1_197_HandleEvent_ValidLifecycleEvent_Unsubscription)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    // Subscribe first
    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "Lifecycle.onBackground", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_TRUE(status);

    // Now unsubscribe
    status = false;
    const auto rc = plugin.HandleAppEventNotifier(emitter, "Lifecycle.onBackground", false, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_TRUE(status);
}

/* ================================================================
 * Gap 1 – OnFocus / OnBlur WindowManager notification callbacks
 *
 * These tests verify that WindowManager focus/blur events dispatch
 * Presentation.onFocusedChanged and drive Lifecycle 1 foreground/
 * background transitions when the app is ACTIVE.
 * ================================================================ */

TEST_F(LifecycleDelegateTest, AGC_L1_198_OnFocus_DispatchesFocusedChanged_And_Foreground)
{
    ASSERT_NE(capturedNotification, nullptr);
    ASSERT_NE(capturedWMNotification, nullptr);

    // Make the app known and ACTIVE so focus triggers lifecycle events
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-focus-001",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        ""
    );
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-focus-001",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::ACTIVE,
        ""
    );

    // Subscribe emitters for both events directly on the lifecycle delegate
    auto lifecycleDelegate = plugin.mDelegate->getLifecycleDelegate();
    ASSERT_NE(lifecycleDelegate, nullptr);

    MockEmitter* focusEmitter = new MockEmitter();
    heapEmitters.push_back(focusEmitter);
    focusEmitter->AddRef();
    lifecycleDelegate->AddNotification("Presentation.onFocusedChanged", focusEmitter);

    MockEmitter* fgEmitter = new MockEmitter();
    heapEmitters.push_back(fgEmitter);
    fgEmitter->AddRef();
    lifecycleDelegate->AddNotification("Lifecycle.onForeground", fgEmitter);

    EXPECT_CALL(*focusEmitter, Emit(::testing::HasSubstr("Presentation.onFocusedChanged"), _, _))
        .Times(::testing::AtLeast(1));
    EXPECT_CALL(*fgEmitter, Emit(::testing::HasSubstr("Lifecycle.onForeground"), _, _))
        .Times(::testing::AtLeast(1));

    // Fire OnFocus
    capturedWMNotification->OnFocus("instance-focus-001");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST_F(LifecycleDelegateTest, AGC_L1_199_OnBlur_DispatchesFocusedChanged_And_Background)
{
    ASSERT_NE(capturedNotification, nullptr);
    ASSERT_NE(capturedWMNotification, nullptr);

    // Make the app known and ACTIVE, then focus it first
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-blur-001",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        ""
    );
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-blur-001",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::ACTIVE,
        ""
    );
    // Focus the app so blur will clear it
    capturedWMNotification->OnFocus("instance-blur-001");
    std::this_thread::sleep_for(std::chrono::milliseconds(25));

    auto lifecycleDelegate = plugin.mDelegate->getLifecycleDelegate();
    ASSERT_NE(lifecycleDelegate, nullptr);

    MockEmitter* focusEmitter = new MockEmitter();
    heapEmitters.push_back(focusEmitter);
    focusEmitter->AddRef();
    lifecycleDelegate->AddNotification("Presentation.onFocusedChanged", focusEmitter);

    MockEmitter* bgEmitter = new MockEmitter();
    heapEmitters.push_back(bgEmitter);
    bgEmitter->AddRef();
    lifecycleDelegate->AddNotification("Lifecycle.onBackground", bgEmitter);

    EXPECT_CALL(*focusEmitter, Emit(::testing::HasSubstr("Presentation.onFocusedChanged"), _, _))
        .Times(::testing::AtLeast(1));
    EXPECT_CALL(*bgEmitter, Emit(::testing::HasSubstr("Lifecycle.onBackground"), _, _))
        .Times(::testing::AtLeast(1));

    // Fire OnBlur
    capturedWMNotification->OnBlur("instance-blur-001");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

/* ================================================================
 * Gap 2 – TERMINATING → Lifecycle.onUnloading dispatch
 *
 * Verifies that when an app transitions to TERMINATING, the
 * Lifecycle 1 Lifecycle.onUnloading event is dispatched.
 * ================================================================ */

TEST_F(LifecycleDelegateTest, AGC_L1_200_Terminating_DispatchesOnUnloading)
{
    ASSERT_NE(capturedNotification, nullptr);

    // Initialize the app in the registries
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-term-001",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        ""
    );

    // Subscribe to Lifecycle.onUnloading
    auto lifecycleDelegate = plugin.mDelegate->getLifecycleDelegate();
    ASSERT_NE(lifecycleDelegate, nullptr);

    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();
    lifecycleDelegate->AddNotification("Lifecycle.onUnloading", emitter);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("Lifecycle.onUnloading"), _, _))
        .Times(::testing::AtLeast(1));

    // Transition to TERMINATING
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-term-001",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::TERMINATING,
        ""
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

/* ================================================================
 * Gap 5 – Presentation.onFocusedChanged subscription path
 *
 * Verifies that the event name is accepted by HandleEvent and
 * correctly recorded via AddNotification/RemoveNotification.
 * ================================================================ */

TEST_F(LifecycleDelegateTest, AGC_L1_201_HandleEvent_PresentationFocusedChanged_SubscribeUnsubscribe)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    // Subscribe
    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "Presentation.onFocusedChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_TRUE(status);

    // Unsubscribe
    status = false;
    const auto rc = plugin.HandleAppEventNotifier(emitter, "Presentation.onFocusedChanged", false, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_TRUE(status);
}

/* ================================================================
 * Actions API tests
 * ================================================================ */

TEST_F(LifecycleDelegateTest, AGC_L1_202_ActionsStart_RoutesToAppActionsPlugin)
{
    EXPECT_CALL(mockAppActions, ActionStart(::testing::StrEq("test.app"),
                                      ::testing::StrEq("{\"action\":\"play\"}"),
                                      ::testing::StrEq("")))
        .WillOnce(::testing::Return(Core::ERROR_NONE));
    EXPECT_CALL(mockAppActions, AddRef()).Times(::testing::AnyNumber());
    EXPECT_CALL(mockAppActions, Release()).Times(::testing::AnyNumber()).WillRepeatedly(::testing::Return(0u));

    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "actions.start", "{\"intent\":{\"action\":\"play\"}}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

TEST_F(LifecycleDelegateTest, AGC_L1_202b_ActionsStart_WithHandlerAppId_RoutesToAppActionsPlugin)
{
    EXPECT_CALL(mockAppActions, ActionStart(::testing::StrEq("test.app"),
                                      ::testing::StrEq("{\"action\":\"play\"}"),
                                      ::testing::StrEq("handler.app")))
        .WillOnce(::testing::Return(Core::ERROR_NONE));
    EXPECT_CALL(mockAppActions, AddRef()).Times(::testing::AnyNumber());
    EXPECT_CALL(mockAppActions, Release()).Times(::testing::AnyNumber()).WillRepeatedly(::testing::Return(0u));

    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "actions.start",
        "{\"intent\":{\"action\":\"play\"},\"handlerAppId\":\"handler.app\"}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

TEST_F(LifecycleDelegateTest, AGC_L1_203_ActionsStart_EmptyPayload_ReturnsBadRequest)
{
    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "actions.start", "", result);

    EXPECT_EQ(Core::ERROR_BAD_REQUEST, rc);
}

TEST_F(LifecycleDelegateTest, AGC_L1_203b_ActionsStart_MissingIntentField_ReturnsBadRequest)
{
    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "actions.start", "{\"handlerAppId\":\"handler.app\"}", result);

    EXPECT_EQ(Core::ERROR_BAD_REQUEST, rc);
}

TEST_F(LifecycleDelegateTest, AGC_L1_204_ActionsIntent_ReturnsIntentIdAndIntent)
{
    ASSERT_NE(capturedNotification, nullptr);
    // Store an intent for test.app via lifecycle notification
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-ai-001",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        "{\"action\":\"navigate\"}"
    );

    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "actions.intent", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_NE(result.find("\"intentId\":"),              std::string::npos);
    EXPECT_NE(result.find("\"intent\":"),               std::string::npos);
    EXPECT_NE(result.find("\"action\":\"navigate\""),   std::string::npos);
}

TEST_F(LifecycleDelegateTest, AGC_L1_205_ActionsIntent_NoStoredIntent_ReturnsZeroId)
{
    const auto ctx = MakeContext("unknown.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "actions.intent", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    // intentId is serialized as a JSON number; check for "intentId":0 (numeric, not quoted string)
    EXPECT_NE(result.find("intentId"), std::string::npos);
    EXPECT_NE(result.find("\"intentId\":0"), std::string::npos);
    // When no intent is stored, intent must be an empty JSON object, not an empty string
    EXPECT_NE(result.find("\"intent\":{}"), std::string::npos);
}

TEST_F(LifecycleDelegateTest, AGC_L1_206_ActionsOnIntent_SubscribeUnsubscribe)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "Actions.onIntent", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_TRUE(status);

    status = false;
    const auto rc = plugin.HandleAppEventNotifier(emitter, "Actions.onIntent", false, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_TRUE(status);
}

TEST_F(LifecycleDelegateTest, AGC_L1_207_ActionsOnIntent_EmittedWhenIntentUpdatedOnActiveTransition)
{
    ASSERT_NE(capturedNotification, nullptr);

    // Store an intent
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-ev-001",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        "{\"action\":\"search\"}"
    );

    // Subscribe an emitter for Actions.onIntent
    auto lifecycleDelegate = plugin.mDelegate->getLifecycleDelegate();
    ASSERT_NE(lifecycleDelegate, nullptr);
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();
    lifecycleDelegate->AddNotification("Actions.onIntent", emitter);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("Actions.onIntent"), _, _))
        .Times(::testing::AtLeast(1));

    // Transition to ACTIVE with an updated intent should fire DispatchLastKnownIntent → Actions.onIntent
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-ev-001",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::ACTIVE,
        "{\"action\":\"browse\"}"
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST_F(LifecycleDelegateTest, AGC_L1_207a_ActionsOnIntent_NotEmittedWhenIntentNotUpdated)
{
    ASSERT_NE(capturedNotification, nullptr);

    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-ev-002",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        "{\"action\":\"search\"}"
    );

    auto lifecycleDelegate = plugin.mDelegate->getLifecycleDelegate();
    ASSERT_NE(lifecycleDelegate, nullptr);
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();
    lifecycleDelegate->AddNotification("Actions.onIntent", emitter);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("Actions.onIntent"), _, _)).Times(0);

    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-ev-002",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::ACTIVE,
        ""
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST_F(LifecycleDelegateTest, AGC_L1_208_LifecycleDeliveredBeforeDiscoveryNavigateTo)
{
    ASSERT_NE(capturedNotification, nullptr);

    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-order-001",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        "{\"action\":\"search\"}"
    );

    auto lifecycleDelegate = plugin.mDelegate->getLifecycleDelegate();
    ASSERT_NE(lifecycleDelegate, nullptr);

    std::mutex callbackMutex;
    std::condition_variable callbackCondition;
    std::vector<std::string> callbackOrder;
    bool lifecycleStarted = false;
    bool lifecycleBlocked = false;
    bool releaseLifecycle = false;
    bool navigationStartedWhileLifecycleBlocked = false;
    bool callbacksOverlapped = false;

    MockEmitter* lifecycleEmitter = new MockEmitter();
    heapEmitters.push_back(lifecycleEmitter);
    lifecycleEmitter->AddRef();
    lifecycleDelegate->AddNotification("Lifecycle.onBackground", lifecycleEmitter);

    MockEmitter* navigationEmitter = new MockEmitter();
    heapEmitters.push_back(navigationEmitter);
    navigationEmitter->AddRef();
    lifecycleDelegate->AddNotification("Discovery.onNavigateTo", navigationEmitter);

    EXPECT_CALL(*lifecycleEmitter, Emit(::testing::HasSubstr("Lifecycle.onBackground"), _, _))
        .WillOnce(::testing::Invoke([&](const string&, const string&, const string&) {
            std::unique_lock<std::mutex> lock(callbackMutex);
            lifecycleStarted = true;
            lifecycleBlocked = true;
            callbackCondition.notify_all();

            callbackCondition.wait_for(
                lock,
                std::chrono::seconds(1),
                [&releaseLifecycle] { return releaseLifecycle; });

            lifecycleBlocked = false;
            callbackOrder.emplace_back("lifecycle");
            callbackCondition.notify_all();
        }));
    EXPECT_CALL(*navigationEmitter, Emit(::testing::HasSubstr("Discovery.onNavigateTo"), _, _))
        .WillOnce(::testing::Invoke([&](const string&, const string&, const string&) {
            std::lock_guard<std::mutex> lock(callbackMutex);
            navigationStartedWhileLifecycleBlocked = lifecycleBlocked;
            callbacksOverlapped = lifecycleStarted && lifecycleBlocked;
            callbackOrder.emplace_back("navigation");
            callbackCondition.notify_all();
        }));

    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-order-001",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::ACTIVE,
        "{\"action\":\"browse\"}"
    );

    std::unique_lock<std::mutex> lock(callbackMutex);
    const bool lifecycleWasStarted = callbackCondition.wait_for(
        lock,
        std::chrono::seconds(1),
        [&lifecycleStarted] { return lifecycleStarted; });

    if (!lifecycleWasStarted) {
        releaseLifecycle = true;
        callbackCondition.notify_all();
        lock.unlock();
        FAIL() << "Lifecycle callback did not start";
    }

    releaseLifecycle = true;
    callbackCondition.notify_all();

    const bool bothCallbacksCompleted = callbackCondition.wait_for(
        lock,
        std::chrono::seconds(1),
        [&callbackOrder] { return callbackOrder.size() == 2U; });

    ASSERT_TRUE(bothCallbacksCompleted);
    EXPECT_FALSE(navigationStartedWhileLifecycleBlocked);
    EXPECT_FALSE(callbacksOverlapped);
    ASSERT_EQ(callbackOrder.size(), 2U);
    EXPECT_EQ(callbackOrder[0], "lifecycle");
    EXPECT_EQ(callbackOrder[1], "navigation");
}

/* ================================================================
 * Hibernation / WebSocket traffic-gate tests (RDKEMW-19304)
 *
 * These tests inject a NiceMock<AppSessionGuardMock> via
 * SetSessionGuard() and verify:
 *
 *  AGC_L1_210 – INITIALIZING always calls ResumeTraffic (stale-
 *               suspension clearance) when a session guard is set.
 *  AGC_L1_211 – HIBERNATED→ACTIVE: ResumeTraffic is called before
 *               Dispatch (verified via call ordering).
 *  AGC_L1_212 – ACTIVE→HIBERNATED: SuspendTraffic is called after
 *               Dispatch (verified via call ordering).
 *  AGC_L1_213 – No guard set: HIBERNATED transitions do not crash.
 *  AGC_L1_214 – SetSessionGuard(nullptr) clears the guard; subsequent
 *               lifecycle transitions proceed without crash.
 *  AGC_L1_215 – Multiple HIBERNATED→ACTIVE→HIBERNATED cycles work
 *               correctly.
 * ================================================================ */

TEST_F(LifecycleDelegateTest, AGC_L1_210_INITIALIZING_CallsResumeTraffic_StaleCleanup)
{
    ASSERT_NE(capturedNotification, nullptr);

    auto lifecycleDelegate = plugin.mDelegate->getLifecycleDelegate();
    ASSERT_NE(lifecycleDelegate, nullptr);

    NiceMock<AppSessionGuardMock> guardMock;

    // Inject the guard; NiceMock means unexpected calls are silently ignored.
    EXPECT_CALL(guardMock, AddRef()).Times(::testing::AnyNumber());
    EXPECT_CALL(guardMock, Release()).Times(::testing::AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    EXPECT_CALL(guardMock, QueryInterface(::testing::_)).Times(::testing::AnyNumber()).WillRepeatedly(Return(nullptr));

    // ResumeTraffic MUST be called for INITIALIZING to clear any stale suspension.
    EXPECT_CALL(guardMock, ResumeTraffic(std::string("test.app")))
        .Times(::testing::AtLeast(1))
        .WillRepeatedly(Return(Core::ERROR_NONE));
    // SuspendTraffic must NOT be called for INITIALIZING.
    EXPECT_CALL(guardMock, SuspendTraffic(::testing::_))
        .Times(0);

    lifecycleDelegate->SetSessionGuard(&guardMock);

    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-210",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        ""
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Clean up guard reference before test teardown.
    lifecycleDelegate->SetSessionGuard(nullptr);
}

TEST_F(LifecycleDelegateTest, AGC_L1_211_HibernatedToActive_ResumeTrafficBeforeDispatch)
{
    ASSERT_NE(capturedNotification, nullptr);

    auto lifecycleDelegate = plugin.mDelegate->getLifecycleDelegate();
    ASSERT_NE(lifecycleDelegate, nullptr);

    // Subscribe an emitter to capture Lifecycle2.onStateChanged dispatch.
    MockEmitter* stateEmitter = new MockEmitter();
    heapEmitters.push_back(stateEmitter);
    stateEmitter->AddRef();
    lifecycleDelegate->AddNotification("Lifecycle2.onStateChanged", stateEmitter);

    // Bring the app to HIBERNATED first (without guard, to keep setup simple).
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-211",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        ""
    );
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-211",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::HIBERNATED,
        ""
    );
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Now inject the guard before the HIBERNATED → ACTIVE transition.
    NiceMock<AppSessionGuardMock> guardMock;
    EXPECT_CALL(guardMock, AddRef()).Times(::testing::AnyNumber());
    EXPECT_CALL(guardMock, Release()).Times(::testing::AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    EXPECT_CALL(guardMock, QueryInterface(::testing::_)).Times(::testing::AnyNumber()).WillRepeatedly(Return(nullptr));

    // Track call ordering via InSequence.
    {
        ::testing::InSequence seq;
        EXPECT_CALL(guardMock, ResumeTraffic(std::string("test.app")))
            .Times(1)
            .WillOnce(Return(Core::ERROR_NONE));
        // After ResumeTraffic, Dispatch fires (captured by stateEmitter).
        EXPECT_CALL(*stateEmitter, Emit(::testing::HasSubstr("Lifecycle2.onStateChanged"), _, _))
            .Times(::testing::AtLeast(1));
    }
    // SuspendTraffic must NOT be called on ACTIVE transition.
    EXPECT_CALL(guardMock, SuspendTraffic(::testing::_)).Times(0);

    lifecycleDelegate->SetSessionGuard(&guardMock);

    // HIBERNATED → ACTIVE: triggers ResumeTraffic then Dispatch.
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-211",
        Exchange::ILifecycleManager::HIBERNATED,
        Exchange::ILifecycleManager::ACTIVE,
        ""
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    lifecycleDelegate->SetSessionGuard(nullptr);
}

TEST_F(LifecycleDelegateTest, AGC_L1_212_ActiveToHibernated_SuspendTrafficAfterDispatch)
{
    ASSERT_NE(capturedNotification, nullptr);

    auto lifecycleDelegate = plugin.mDelegate->getLifecycleDelegate();
    ASSERT_NE(lifecycleDelegate, nullptr);

    // Subscribe an emitter for Lifecycle2.onStateChanged dispatch.
    MockEmitter* stateEmitter = new MockEmitter();
    heapEmitters.push_back(stateEmitter);
    stateEmitter->AddRef();
    lifecycleDelegate->AddNotification("Lifecycle2.onStateChanged", stateEmitter);

    // Get app into ACTIVE state without guard.
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-212",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        ""
    );
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-212",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::ACTIVE,
        ""
    );
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Inject guard before ACTIVE → HIBERNATED transition.
    NiceMock<AppSessionGuardMock> guardMock;
    EXPECT_CALL(guardMock, AddRef()).Times(::testing::AnyNumber());
    EXPECT_CALL(guardMock, Release()).Times(::testing::AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    EXPECT_CALL(guardMock, QueryInterface(::testing::_)).Times(::testing::AnyNumber()).WillRepeatedly(Return(nullptr));

    {
        ::testing::InSequence seq;
        EXPECT_CALL(*stateEmitter, Emit(::testing::HasSubstr("Lifecycle2.onStateChanged"), _, _))
            .Times(1);
        EXPECT_CALL(guardMock, SuspendTraffic(std::string("test.app")))
            .Times(1)
            .WillOnce(Return(Core::ERROR_NONE));
    }
    // ResumeTraffic must NOT be called for ACTIVE → HIBERNATED.
    EXPECT_CALL(guardMock, ResumeTraffic(::testing::_)).Times(0);

    lifecycleDelegate->SetSessionGuard(&guardMock);

    // ACTIVE → HIBERNATED: triggers Dispatch then SuspendTraffic.
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-212",
        Exchange::ILifecycleManager::ACTIVE,
        Exchange::ILifecycleManager::HIBERNATED,
        ""
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    lifecycleDelegate->SetSessionGuard(nullptr);
}

TEST_F(LifecycleDelegateTest, AGC_L1_213_NoGuardSet_HibernationTransition_NoCrash)
{
    ASSERT_NE(capturedNotification, nullptr);

    // Deliberately do NOT inject a session guard (nullptr, the default).
    // The lifecycle transitions should proceed without crash.

    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-213",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        ""
    );
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-213",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::ACTIVE,
        ""
    );
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-213",
        Exchange::ILifecycleManager::ACTIVE,
        Exchange::ILifecycleManager::HIBERNATED,
        ""
    );
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-213",
        Exchange::ILifecycleManager::HIBERNATED,
        Exchange::ILifecycleManager::ACTIVE,
        ""
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // If we reach here without crash, the test passes.
}

TEST_F(LifecycleDelegateTest, AGC_L1_214_SetSessionGuard_Nullptr_ClearsGuard_NoCrash)
{
    ASSERT_NE(capturedNotification, nullptr);

    auto lifecycleDelegate = plugin.mDelegate->getLifecycleDelegate();
    ASSERT_NE(lifecycleDelegate, nullptr);

    NiceMock<AppSessionGuardMock> guardMock;
    EXPECT_CALL(guardMock, AddRef()).Times(::testing::AnyNumber());
    EXPECT_CALL(guardMock, Release()).Times(::testing::AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    EXPECT_CALL(guardMock, QueryInterface(::testing::_)).Times(::testing::AnyNumber()).WillRepeatedly(Return(nullptr));
    EXPECT_CALL(guardMock, ResumeTraffic(::testing::_)).Times(::testing::AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    EXPECT_CALL(guardMock, SuspendTraffic(::testing::_)).Times(::testing::AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));

    // Set guard, trigger a transition, then clear it.
    lifecycleDelegate->SetSessionGuard(&guardMock);

    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-214",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        ""
    );
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Clear the guard.
    lifecycleDelegate->SetSessionGuard(nullptr);

    // Further transitions must not crash (guard is null).
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-214",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::ACTIVE,
        ""
    );
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-214",
        Exchange::ILifecycleManager::ACTIVE,
        Exchange::ILifecycleManager::HIBERNATED,
        ""
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // If we reach here without crash, the test passes.
}

TEST_F(LifecycleDelegateTest, AGC_L1_215_MultipleHibernationCycles_CorrectGuardCalls)
{
    ASSERT_NE(capturedNotification, nullptr);

    auto lifecycleDelegate = plugin.mDelegate->getLifecycleDelegate();
    ASSERT_NE(lifecycleDelegate, nullptr);

    NiceMock<AppSessionGuardMock> guardMock;
    EXPECT_CALL(guardMock, AddRef()).Times(::testing::AnyNumber());
    EXPECT_CALL(guardMock, Release()).Times(::testing::AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
    EXPECT_CALL(guardMock, QueryInterface(::testing::_)).Times(::testing::AnyNumber()).WillRepeatedly(Return(nullptr));

    // Two full cycles: INITIALIZING + ACTIVE + HIBERNATED + back to ACTIVE.
    // Each ACTIVE→HIBERNATED must call SuspendTraffic exactly once.
    // Each HIBERNATED→ACTIVE must call ResumeTraffic exactly once.
    // INITIALIZING also calls ResumeTraffic (stale-suspension clearance).

    EXPECT_CALL(guardMock, ResumeTraffic(std::string("test.app")))
        .Times(::testing::Exactly(3)) // 1 for INITIALIZING + 2 for HIBERNATED→ACTIVE
        .WillRepeatedly(Return(Core::ERROR_NONE));
    EXPECT_CALL(guardMock, SuspendTraffic(std::string("test.app")))
        .Times(::testing::Exactly(2)) // 2 for ACTIVE→HIBERNATED
        .WillRepeatedly(Return(Core::ERROR_NONE));

    lifecycleDelegate->SetSessionGuard(&guardMock);

    // Cycle 1
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-215",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        ""
    );
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-215",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::ACTIVE,
        ""
    );
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-215",
        Exchange::ILifecycleManager::ACTIVE,
        Exchange::ILifecycleManager::HIBERNATED,
        ""
    );
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-215",
        Exchange::ILifecycleManager::HIBERNATED,
        Exchange::ILifecycleManager::ACTIVE,
        ""
    );

    // Cycle 2 (ACTIVE → HIBERNATED → ACTIVE, no new INITIALIZING)
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-215",
        Exchange::ILifecycleManager::ACTIVE,
        Exchange::ILifecycleManager::HIBERNATED,
        ""
    );
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-hib-215",
        Exchange::ILifecycleManager::HIBERNATED,
        Exchange::ILifecycleManager::ACTIVE,
        ""
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    lifecycleDelegate->SetSessionGuard(nullptr);
}

/* ================================================================
 * AGC_L1_233–235: null/empty intent validation in ActionsStart and
 * lifecycle notification path.
 * ================================================================ */

TEST_F(LifecycleDelegateTest, AGC_L1_233_ActionsStart_NullIntentField_ReturnsBadRequest)
{
    // {"intent":null} — the intent field is JSON null
    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "actions.start", "{\"intent\":null}", result);

    EXPECT_EQ(Core::ERROR_BAD_REQUEST, rc);
}

TEST_F(LifecycleDelegateTest, AGC_L1_234_ActionsStart_EmptyObjectIntentField_ReturnsBadRequest)
{
    // {"intent":{}} — the intent field is an empty JSON object
    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "actions.start", "{\"intent\":{}}", result);

    EXPECT_EQ(Core::ERROR_BAD_REQUEST, rc);
}

TEST_F(LifecycleDelegateTest, AGC_L1_235_LifecycleNotification_NullNavigationIntent_NotStored)
{
    // OnAppLifecycleStateChanged with navigationIntent="null" must not overwrite a stored intent.
    ASSERT_NE(capturedNotification, nullptr);

    // First store a valid intent via INITIALIZING
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-nullnav-001",
        Exchange::ILifecycleManager::UNLOADED,
        Exchange::ILifecycleManager::INITIALIZING,
        "{\"action\":\"play\"}"
    );

    // Now fire ACTIVE with navigationIntent="null" — should be ignored
    capturedNotification->OnAppLifecycleStateChanged(
        "test.app", "instance-nullnav-001",
        Exchange::ILifecycleManager::INITIALIZING,
        Exchange::ILifecycleManager::ACTIVE,
        "null"
    );

    // The original intent must still be retrievable
    const auto ctx = MakeContext("test.app");
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "commoninternal.getlastintent", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    // Original intent is still there
    EXPECT_NE(result.find("\"action\":\"play\""), std::string::npos);
    // The literal string "null" must not appear as the intent value
    EXPECT_EQ(result.find("\"intent\":\"null\""), std::string::npos);
}

} // namespace
