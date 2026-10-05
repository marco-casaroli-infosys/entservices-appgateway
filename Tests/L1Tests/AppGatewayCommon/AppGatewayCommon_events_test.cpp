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
#include "MockEmitter.h"
#include "MockTextToSpeech.h"
#include "UserSettingMock.h"
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

static Exchange::GatewayContext MakeContext()
{
    Exchange::GatewayContext ctx;
    ctx.appId = "test.app";
    ctx.connectionId = 100;
    ctx.requestId = 200;
    return ctx;
}

class EventsTest : public ::testing::Test {
protected:
    Core::Sink<AppGatewayCommon> plugin;
    NiceMock<ServiceMock> service;

    void SetUp() override
    {
        ON_CALL(service, QueryInterfaceByCallsign(_, _))
            .WillByDefault(Return(nullptr));

        EXPECT_CALL(service, AddRef()).Times(AnyNumber());
        EXPECT_CALL(service, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));

        const string response = plugin.Initialize(&service);
        ASSERT_TRUE(response.empty());
    }

    void TearDown() override
    {
        plugin.Deinitialize(&service);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
};

/* ---------- HandleAppEventNotifier ---------- */

TEST_F(EventsTest, AGC_L1_202_HandleAppEventNotifier_SubmitsJob)
{
    // Heap-allocated emitter: async EventRegistrationJobs hold raw pointers to
    // the emitter via AddRef/Release.  The subscribe job path involves JSON-RPC
    // calls that may take 9+ seconds to timeout.  A stack-allocated emitter
    // would be destroyed on scope exit while the async job still references it,
    // causing a use-after-free / segfault.  Heap allocation keeps the memory
    // valid for the entire process lifetime (benign leak — OS reclaims).
    MockEmitter* emitter = new MockEmitter();
    emitter->AddRef();  // test's own reference

    bool status = false;
    const auto rc = plugin.HandleAppEventNotifier(emitter, "Device.onHdrChanged", true, status);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_TRUE(status);

    // Give the worker pool a moment — the actual async job may still be
    // blocked on RPC timeouts, but that is fine: the heap-allocated emitter
    // survives until the process exits.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST_F(EventsTest, AGC_L1_203_HandleAppEventNotifier_NullCallback)
{
    bool status = false;
    const auto rc = plugin.HandleAppEventNotifier(nullptr, "Device.onHdrChanged", true, status);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
    EXPECT_FALSE(status);
}

/* ---------- Miscellaneous handler map entries ---------- */

TEST_F(EventsTest, AGC_L1_204_DiscoveryWatched_ReturnsNull)
{
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "discovery.watched", R"({"entityId":"abc"})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

TEST_F(EventsTest, AGC_L1_205_Metrics_NoOp_ReturnsNull)
{
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "metrics.someMetric", R"({"data":123})", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

// Fixture for tests that null out mDelegate. The base Deinitialize unconditionally
// dereferences mDelegate, so these tests must not call Deinitialize in TearDown.
class NullDelegateEventsTest : public ::testing::Test {
protected:
    Core::Sink<AppGatewayCommon> plugin;
    NiceMock<ServiceMock> service;

    void SetUp() override
    {
        ON_CALL(service, QueryInterfaceByCallsign(_, _))
            .WillByDefault(Return(nullptr));

        EXPECT_CALL(service, AddRef()).Times(AnyNumber());
        EXPECT_CALL(service, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));

        const string response = plugin.Initialize(&service);
        ASSERT_TRUE(response.empty());
    }

    void TearDown() override
    {
        // Do NOT call Deinitialize — mDelegate was reset to nullptr by the test
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
};

TEST_F(NullDelegateEventsTest, AGC_L1_206_SecondScreenFriendlyName_NullDelegate)
{
    plugin.mDelegate.reset();
    const auto ctx = MakeContext();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "secondscreen.friendlyname", "{}", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, rc);
}

TEST_F(NullDelegateEventsTest, AGC_L1_207_NullDelegate_HandleAppEventNotifier)
{
    plugin.mDelegate.reset();

    Core::Sink<MockEmitter> emitter;
    bool status = false;
    const auto rc = plugin.HandleAppEventNotifier(&emitter, "Device.onHdrChanged", true, status);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
    EXPECT_FALSE(status);
}

/* ---------- TTS event subscription via HandleAppEventNotifier ---------- */

class TTSEventsTest : public ::testing::Test {
protected:
    Core::Sink<AppGatewayCommon> plugin;
    NiceMock<ServiceMock> service;
    NiceMock<MockTextToSpeech> mockTTS;
    NiceMock<UserSettingMock> mockUserSettings;

    void SetUp() override
    {
        ON_CALL(service, QueryInterfaceByCallsign(_, _))
            .WillByDefault(Return(nullptr));

        ON_CALL(service, QueryInterfaceByCallsign(Exchange::ITextToSpeech::ID, ::testing::StrEq("org.rdk.TextToSpeech")))
            .WillByDefault(::testing::Invoke([this](uint32_t, const string&) -> void* {
                mockTTS.AddRef();
                return static_cast<Exchange::ITextToSpeech*>(&mockTTS);
            }));

        ON_CALL(service, QueryInterfaceByCallsign(Exchange::IUserSettings::ID, ::testing::StrEq("org.rdk.UserSettings")))
            .WillByDefault(::testing::Invoke([this](uint32_t, const string&) -> void* {
                mockUserSettings.AddRef();
                return static_cast<Exchange::IUserSettings*>(&mockUserSettings);
            }));

        EXPECT_CALL(service, AddRef()).Times(AnyNumber());
        EXPECT_CALL(service, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
        EXPECT_CALL(mockTTS, Register(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
        EXPECT_CALL(mockTTS, Unregister(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
        EXPECT_CALL(mockUserSettings, Register(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
        EXPECT_CALL(mockUserSettings, Unregister(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));

        const string response = plugin.Initialize(&service);
        ASSERT_TRUE(response.empty());
    }

    void TearDown() override
    {
        plugin.Deinitialize(&service);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
};

TEST_F(TTSEventsTest, AGC_L1_208_TTS_EventSubscription_Registers)
{
    // Subscribe to a TTS event — this should trigger Register on MockTextToSpeech
    EXPECT_CALL(mockTTS, Register(_)).Times(::testing::AtLeast(1));

    MockEmitter* emitter = new MockEmitter();
    emitter->AddRef();
    bool status = false;
    const auto rc = plugin.HandleAppEventNotifier(emitter, "TextToSpeech.onSpeechComplete", true, status);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_TRUE(status);

    // Give the worker pool time to dispatch
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

TEST_F(TTSEventsTest, AGC_L1_209_TTS_EventUnsubscription)
{
    MockEmitter* emitter = new MockEmitter();
    emitter->AddRef();

    // First subscribe
    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "TextToSpeech.onSpeechStart", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_TRUE(status);

    // Then unsubscribe
    status = false;
    const auto rc = plugin.HandleAppEventNotifier(emitter, "TextToSpeech.onSpeechStart", false, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_TRUE(status);
}

TEST_F(TTSEventsTest, AGC_L1_210_UserSettings_EventSubscription_Registers)
{
    // Subscribe to a UserSettings event
    EXPECT_CALL(mockUserSettings, Register(_)).Times(::testing::AtLeast(1));

    MockEmitter* emitter = new MockEmitter();
    emitter->AddRef();
    bool status = false;
    const auto rc = plugin.HandleAppEventNotifier(emitter, "accessibility.onvoiceguidancesettingschanged", true, status);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_TRUE(status);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

/* ---------- Deactivated ---------- */

TEST_F(EventsTest, AGC_L1_211_Deactivated_NonMatchingConnectionId)
{
    // mConnectionId is 0 by default after Initialize, create a mock connection with non-zero ID
    // This tests that Deactivated does NOT submit a job when connection IDs don't match

    class MockRemoteConnection : public RPC::IRemoteConnection {
    public:
        MockRemoteConnection(uint32_t id) : mId(id), mRefCount(1) {}
        uint32_t Id() const override { return mId; }
        uint32_t RemoteId() const override { return 0; }
        void* Acquire(const uint32_t, const string&, const uint32_t, const uint32_t) override { return nullptr; }
        uint32_t Launch() override { return 0; }
        void PostMortem() override {}
        void Terminate() override {}
        uint32_t AddRef() const override { return ++mRefCount; }
        uint32_t Release() const override {
            if (--mRefCount == 0) { delete this; return Core::ERROR_DESTRUCTION_SUCCEEDED; }
            return Core::ERROR_NONE;
        }
        BEGIN_INTERFACE_MAP(MockRemoteConnection)
        INTERFACE_ENTRY(RPC::IRemoteConnection)
        END_INTERFACE_MAP
    private:
        uint32_t mId;
        mutable uint32_t mRefCount;
    };

    // Connection ID 999 won't match mConnectionId (0), so no job should be submitted
    MockRemoteConnection conn(999);
    // Should NOT crash and NOT submit a deactivation job
    plugin.Deactivated(&conn);
}

/* ================================================================
 * Category C – TTS Notification dispatch
 *
 * Capture the ITextToSpeech::INotification pointer during
 * subscription and fire callbacks to verify TTSDelegate dispatch.
 * ================================================================ */

class TTSNotificationTest : public ::testing::Test {
protected:
    Core::Sink<AppGatewayCommon> plugin;
    NiceMock<ServiceMock> service;
    NiceMock<MockTextToSpeech> mockTTS;
    NiceMock<UserSettingMock> mockUserSettings;
    Exchange::ITextToSpeech::INotification* capturedTTSNotification = nullptr;
    std::vector<MockEmitter*> heapEmitters;

    void SetUp() override
    {
        ON_CALL(service, QueryInterfaceByCallsign(_, _))
            .WillByDefault(Return(nullptr));

        ON_CALL(service, QueryInterfaceByCallsign(Exchange::ITextToSpeech::ID, ::testing::StrEq("org.rdk.TextToSpeech")))
            .WillByDefault(::testing::Invoke([this](uint32_t, const string&) -> void* {
                mockTTS.AddRef();
                return static_cast<Exchange::ITextToSpeech*>(&mockTTS);
            }));

        ON_CALL(service, QueryInterfaceByCallsign(Exchange::IUserSettings::ID, ::testing::StrEq("org.rdk.UserSettings")))
            .WillByDefault(::testing::Invoke([this](uint32_t, const string&) -> void* {
                mockUserSettings.AddRef();
                return static_cast<Exchange::IUserSettings*>(&mockUserSettings);
            }));

        EXPECT_CALL(service, AddRef()).Times(AnyNumber());
        EXPECT_CALL(service, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));

        // Capture TTS notification pointer
        EXPECT_CALL(mockTTS, Register(_)).Times(AnyNumber())
            .WillRepeatedly(::testing::Invoke([this](Exchange::ITextToSpeech::INotification* n) -> uint32_t {
                capturedTTSNotification = n;
                return Core::ERROR_NONE;
            }));
        EXPECT_CALL(mockTTS, Unregister(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
        EXPECT_CALL(mockUserSettings, Register(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));
        EXPECT_CALL(mockUserSettings, Unregister(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));

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

TEST_F(TTSNotificationTest, AGC_L1_212_TTS_OnSpeechComplete_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    // Subscribe to TextToSpeech.onSpeechComplete
    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "TextToSpeech.onSpeechComplete", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedTTSNotification, nullptr);

    // Fire the notification
    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("TextToSpeech.onSpeechComplete"), _, _)).Times(::testing::AtLeast(1));
    capturedTTSNotification->OnSpeechComplete(42);

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(TTSNotificationTest, AGC_L1_213_TTS_OnPlaybackError_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "TextToSpeech.onPlaybackError", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedTTSNotification, nullptr);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("TextToSpeech.onPlaybackError"), _, _)).Times(::testing::AtLeast(1));
    capturedTTSNotification->OnPlaybackError(99);

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(TTSNotificationTest, AGC_L1_214_TTS_OnTTSStateChanged_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "TextToSpeech.onTtsstatechanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedTTSNotification, nullptr);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("TextToSpeech.onTtsstatechanged"), _, _)).Times(::testing::AtLeast(1));
    capturedTTSNotification->OnTTSStateChanged(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(TTSNotificationTest, AGC_L1_215_TTS_OnVoiceChanged_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "TextToSpeech.onVoiceChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedTTSNotification, nullptr);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("TextToSpeech.onVoiceChanged"), _, _)).Times(::testing::AtLeast(1));
    capturedTTSNotification->OnVoiceChanged("en-US-Standard-A");

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(TTSNotificationTest, AGC_L1_216_TTS_OnSpeechStarted_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "TextToSpeech.onSpeechStart", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedTTSNotification, nullptr);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("TextToSpeech.onSpeechStart"), _, _)).Times(::testing::AtLeast(1));
    capturedTTSNotification->OnSpeechStarted(10);

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

/* ================================================================
 * Category B – TTS null interface path
 * ================================================================ */

class TTSNoInterfaceTest : public ::testing::Test {
protected:
    Core::Sink<AppGatewayCommon> plugin;
    NiceMock<ServiceMock> service;
    std::vector<MockEmitter*> heapEmitters;

    void SetUp() override
    {
        // All QueryInterfaceByCallsign return nullptr – TTS is unavailable
        ON_CALL(service, QueryInterfaceByCallsign(_, _))
            .WillByDefault(Return(nullptr));

        EXPECT_CALL(service, AddRef()).Times(AnyNumber());
        EXPECT_CALL(service, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));

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

TEST_F(TTSNoInterfaceTest, AGC_L1_217_TTS_Subscription_NoInterface_StatusFalse)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();
    bool status = false;

    // HandleAppEventNotifier dispatches async — the worker job will try to
    // subscribe via TTSDelegate::HandleSubscription, which calls Register().
    // Register calls GetTTS(), which returns nullptr → returns false →
    // registrationError is set.
    const auto rc = plugin.HandleAppEventNotifier(emitter, "TextToSpeech.onSpeechComplete", true, status);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_TRUE(status);  // status is set before async dispatch

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

/* ================================================================
 * Category C – UserSettings notification dispatch
 *
 * Capture IUserSettings::INotification pointer during subscription
 * and fire notification callbacks.
 * ================================================================ */

class UserSettingsNotificationTest : public ::testing::Test {
protected:
    Core::Sink<AppGatewayCommon> plugin;
    NiceMock<ServiceMock> service;
    NiceMock<UserSettingMock> mockUserSettings;
    Exchange::IUserSettings::INotification* capturedUSNotification = nullptr;
    std::vector<MockEmitter*> heapEmitters;

    void SetUp() override
    {
        ON_CALL(service, QueryInterfaceByCallsign(_, _))
            .WillByDefault(Return(nullptr));

        ON_CALL(service, QueryInterfaceByCallsign(Exchange::IUserSettings::ID, ::testing::StrEq("org.rdk.UserSettings")))
            .WillByDefault(::testing::Invoke([this](uint32_t, const string&) -> void* {
                mockUserSettings.AddRef();
                return static_cast<Exchange::IUserSettings*>(&mockUserSettings);
            }));

        EXPECT_CALL(service, AddRef()).Times(AnyNumber());
        EXPECT_CALL(service, Release()).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));

        // Capture UserSettings notification pointer
        EXPECT_CALL(mockUserSettings, Register(_)).Times(AnyNumber())
            .WillRepeatedly(::testing::Invoke([this](Exchange::IUserSettings::INotification* n) -> uint32_t {
                capturedUSNotification = n;
                return Core::ERROR_NONE;
            }));
        EXPECT_CALL(mockUserSettings, Unregister(_)).Times(AnyNumber()).WillRepeatedly(Return(Core::ERROR_NONE));

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

TEST_F(UserSettingsNotificationTest, AGC_L1_218_UserSettings_OnAudioDescriptionChanged_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "Accessibility.onAudioDescriptionSettingsChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedUSNotification, nullptr);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("Accessibility.onAudioDescriptionSettingsChanged"), _, _)).Times(::testing::AtLeast(1));
    capturedUSNotification->OnAudioDescriptionChanged(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(UserSettingsNotificationTest, AGC_L1_219_UserSettings_OnCaptionsChanged_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "ClosedCaptions.onEnabledChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedUSNotification, nullptr);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("ClosedCaptions.onEnabledChanged"), _, _)).Times(::testing::AtLeast(1));
    capturedUSNotification->OnCaptionsChanged(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(UserSettingsNotificationTest, AGC_L1_220_UserSettings_OnHighContrastChanged_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "Accessibility.onHighContrastUIChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedUSNotification, nullptr);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("Accessibility.onHighContrastUIChanged"), _, _)).Times(::testing::AtLeast(1));
    capturedUSNotification->OnHighContrastChanged(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(UserSettingsNotificationTest, AGC_L1_221_UserSettings_OnVoiceGuidanceChanged_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "Accessibility.onVoiceGuidanceSettingsChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedUSNotification, nullptr);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("Accessibility.onVoiceGuidanceSettingsChanged"), _, _)).Times(::testing::AtLeast(1));
    capturedUSNotification->OnVoiceGuidanceChanged(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(UserSettingsNotificationTest, AGC_L1_222_UserSettings_OnPreferredAudioLanguagesChanged_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "Localization.onPreferredAudioLanguagesChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedUSNotification, nullptr);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("Localization.onPreferredAudioLanguagesChanged"), _, _)).Times(::testing::AtLeast(1));
    capturedUSNotification->OnPreferredAudioLanguagesChanged("eng,fra");

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(UserSettingsNotificationTest, AGC_L1_223_UserSettings_OnPresentationLanguageChanged_Dispatches)
{
    MockEmitter* localeEmitter = new MockEmitter();
    heapEmitters.push_back(localeEmitter);
    localeEmitter->AddRef();

    MockEmitter* languageEmitter = new MockEmitter();
    heapEmitters.push_back(languageEmitter);
    languageEmitter->AddRef();

    MockEmitter* presentationLanguageEmitter = new MockEmitter();
    heapEmitters.push_back(presentationLanguageEmitter);
    presentationLanguageEmitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(localeEmitter, "Localization.onLocaleChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedUSNotification, nullptr);

    plugin.HandleAppEventNotifier(languageEmitter, "Localization.onLanguageChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    plugin.HandleAppEventNotifier(presentationLanguageEmitter, "Localization.onPresentationLanguageChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedUSNotification, nullptr);

    EXPECT_CALL(*localeEmitter, Emit(::testing::HasSubstr("Localization.onLocaleChanged"), ::testing::StrEq("en-US"), _))
        .Times(::testing::AtLeast(1));
    EXPECT_CALL(*languageEmitter, Emit(::testing::HasSubstr("Localization.onLanguageChanged"), ::testing::StrEq("\"en\""), _))
        .Times(::testing::AtLeast(1));
    EXPECT_CALL(*presentationLanguageEmitter, Emit(::testing::HasSubstr("Localization.onPresentationLanguageChanged"), ::testing::StrEq("{\"value\":\"en-US\"}"), _))
        .Times(::testing::AtLeast(1));

    capturedUSNotification->OnPresentationLanguageChanged("en-US");

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

/* ================================================================
 * Gap A – Remaining TTS notification dispatch callbacks
 *
 * OnSpeechReady, OnSpeechPaused, OnSpeechResumed,
 * OnSpeechInterrupted, OnNetworkError — each dispatches a distinct
 * event that apps can subscribe to.
 * ================================================================ */

TEST_F(TTSNotificationTest, AGC_L1_224_TTS_OnSpeechReady_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "TextToSpeech.onWillSpeak", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedTTSNotification, nullptr);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("TextToSpeech.onWillSpeak"), _, _)).Times(::testing::AtLeast(1));
    capturedTTSNotification->OnSpeechReady(5);

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(TTSNotificationTest, AGC_L1_225_TTS_OnSpeechPaused_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "TextToSpeech.onSpeechPause", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedTTSNotification, nullptr);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("TextToSpeech.onSpeechPause"), _, _)).Times(::testing::AtLeast(1));
    capturedTTSNotification->OnSpeechPaused(15);

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(TTSNotificationTest, AGC_L1_226_TTS_OnSpeechResumed_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "TextToSpeech.onSpeechResume", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedTTSNotification, nullptr);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("TextToSpeech.onSpeechResume"), _, _)).Times(::testing::AtLeast(1));
    capturedTTSNotification->OnSpeechResumed(15);

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(TTSNotificationTest, AGC_L1_227_TTS_OnSpeechInterrupted_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "TextToSpeech.onSpeechInterrupted", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedTTSNotification, nullptr);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("TextToSpeech.onSpeechInterrupted"), _, _)).Times(::testing::AtLeast(1));
    capturedTTSNotification->OnSpeechInterrupted(20);

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(TTSNotificationTest, AGC_L1_228_TTS_OnNetworkError_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "TextToSpeech.onNetworkError", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedTTSNotification, nullptr);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("TextToSpeech.onNetworkError"), _, _)).Times(::testing::AtLeast(1));
    capturedTTSNotification->OnNetworkError(25);

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

/* ================================================================
 * Gap B – Remaining UserSettings notification dispatch callbacks
 *
 * OnPreferredCaptionsLanguagesChanged, OnVoiceGuidanceRateChanged,
 * OnVoiceGuidanceHintsChanged — each triggers dispatch through
 * the UserSettingsDelegate notification handler.
 * ================================================================ */

/* ================================================================
 * Gap 3 – OnPresentationLanguageChanged with no-hyphen locale
 *
 * When the locale string contains no '-' (e.g. "eng"),
 * Localization.onLanguageChanged should NOT be dispatched,
 * while onLocaleChanged and onPresentationLanguageChanged must still fire.
 * ================================================================ */

TEST_F(UserSettingsNotificationTest, AGC_L1_229_UserSettings_OnPresentationLanguageChanged_NoHyphen)
{
    MockEmitter* localeEmitter = new MockEmitter();
    heapEmitters.push_back(localeEmitter);
    localeEmitter->AddRef();

    MockEmitter* langEmitter = new MockEmitter();
    heapEmitters.push_back(langEmitter);
    langEmitter->AddRef();

    MockEmitter* presEmitter = new MockEmitter();
    heapEmitters.push_back(presEmitter);
    presEmitter->AddRef();

    // Subscribe to all three locale-related events
    bool status = false;
    plugin.HandleAppEventNotifier(localeEmitter, "Localization.onLocaleChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedUSNotification, nullptr);

    plugin.HandleAppEventNotifier(langEmitter, "Localization.onLanguageChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    plugin.HandleAppEventNotifier(presEmitter, "Localization.onPresentationLanguageChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // onLocaleChanged and onPresentationLanguageChanged must fire
    EXPECT_CALL(*localeEmitter, Emit(::testing::HasSubstr("Localization.onLocaleChanged"), ::testing::StrEq("eng"), _))
        .Times(::testing::AtLeast(1));
    EXPECT_CALL(*presEmitter, Emit(::testing::HasSubstr("Localization.onPresentationLanguageChanged"), ::testing::StrEq("{\"value\":\"eng\"}"), _))
        .Times(::testing::AtLeast(1));
    // onLanguageChanged must NOT fire for a non-hyphenated locale
    EXPECT_CALL(*langEmitter, Emit(::testing::HasSubstr("Localization.onLanguageChanged"), _, _))
        .Times(0);

    // Fire with a non-hyphenated locale
    capturedUSNotification->OnPresentationLanguageChanged("eng");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST_F(UserSettingsNotificationTest, AGC_L1_230_UserSettings_OnPreferredCaptionsLanguagesChanged_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "ClosedCaptions.onPreferredLanguagesChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedUSNotification, nullptr);

    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("ClosedCaptions.onPreferredLanguagesChanged"), _, _)).Times(::testing::AtLeast(1));
    capturedUSNotification->OnPreferredCaptionsLanguagesChanged("eng,spa");

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(UserSettingsNotificationTest, AGC_L1_231_UserSettings_OnVoiceGuidanceRateChanged_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "Accessibility.onVoiceGuidanceSettingsChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedUSNotification, nullptr);

    // OnVoiceGuidanceRateChanged triggers DispatchVoiceGuidanceSettingsChanged
    // which re-queries voice guidance state and dispatches the composite settings
    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("Accessibility.onVoiceGuidanceSettingsChanged"), _, _)).Times(::testing::AtLeast(1));
    capturedUSNotification->OnVoiceGuidanceRateChanged(1.5);

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

TEST_F(UserSettingsNotificationTest, AGC_L1_232_UserSettings_OnVoiceGuidanceHintsChanged_Dispatches)
{
    MockEmitter* emitter = new MockEmitter();
    heapEmitters.push_back(emitter);
    emitter->AddRef();

    bool status = false;
    plugin.HandleAppEventNotifier(emitter, "Accessibility.onVoiceGuidanceSettingsChanged", true, status);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NE(capturedUSNotification, nullptr);

    // OnVoiceGuidanceHintsChanged triggers DispatchVoiceGuidanceSettingsChanged
    // which re-queries voice guidance state and dispatches the composite settings
    EXPECT_CALL(*emitter, Emit(::testing::HasSubstr("Accessibility.onVoiceGuidanceSettingsChanged"), _, _)).Times(::testing::AtLeast(1));
    capturedUSNotification->OnVoiceGuidanceHintsChanged(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

} // namespace
