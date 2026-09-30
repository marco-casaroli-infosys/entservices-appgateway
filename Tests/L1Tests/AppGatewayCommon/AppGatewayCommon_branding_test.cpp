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

// L1 tests for the Firebolt Device Branding APIs (Phase 1):
//   Device.setOsName / Device.osName   — via IDeviceInfo::OsName()
//   Device.setOsVersion / Device.osVersion — via IDeviceInfo::OsVersion()
//   Device.firmware                     — via IDeviceInfo::FirmwareVersion().imagename

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>
#include <atomic>

#include "Module.h"

#define private public
#include "AppGatewayCommon.h"
#undef private

#include "ServiceMock.h"
#include "ThunderPortability.h"
#include "WorkerPoolImplementation.h"

#include <interfaces/IDeviceInfo.h>

using namespace WPEFramework;
using namespace WPEFramework::Plugin;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;

namespace {

// ============================================================================
// Worker-pool guard
// ============================================================================
class WorkerPoolGuard final {
public:
    WorkerPoolGuard(const WorkerPoolGuard&) = delete;
    WorkerPoolGuard& operator=(const WorkerPoolGuard&) = delete;
    WorkerPoolGuard() : mPool(2, 0, 64), mAssigned(false)
    {
        if (!Core::IWorkerPool::IsAvailable()) {
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

static Exchange::GatewayContext MakeCtx()
{
    Exchange::GatewayContext ctx;
    ctx.appId   = "test.branding";
    ctx.connectionId = 1;
    ctx.requestId    = 42;
    return ctx;
}

// ============================================================================
// Mock: Exchange::IDeviceInfo
// Mocks only the methods exercised by the branding APIs under test.
// All other pure virtuals are no-op stubs to satisfy the vtable.
// ============================================================================
class MockDeviceInfo : public Exchange::IDeviceInfo {
public:
    explicit MockDeviceInfo() : _refCount(1) {}
    virtual ~MockDeviceInfo() = default;

    uint32_t AddRef() const override { return _refCount.fetch_add(1, std::memory_order_relaxed) + 1; }
    uint32_t Release() const override
    {
        const uint32_t r = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (r == 0) { delete this; return Core::ERROR_DESTRUCTION_SUCCEEDED; }
        return Core::ERROR_NONE;
    }
    void* QueryInterface(const uint32_t id) override
    {
        if (id == Exchange::IDeviceInfo::ID || id == Core::IUnknown::ID) {
            AddRef();
            return static_cast<Exchange::IDeviceInfo*>(this);
        }
        return nullptr;
    }

    // Methods under test
    MOCK_METHOD(Core::hresult, OsName,    (Exchange::IDeviceInfo::DeviceOsName&), (const, override));
    MOCK_METHOD(Core::hresult, OsName,    (const string&),                        (override));
    MOCK_METHOD(Core::hresult, OsVersion, (Exchange::IDeviceInfo::DeviceOsVersion&), (const, override));
    MOCK_METHOD(Core::hresult, OsVersion, (const string&),                           (override));
    MOCK_METHOD(Core::hresult, FirmwareVersion, (Exchange::IDeviceInfo::FirmwareversionInfo&), (const, override));

    // No-op stubs for remaining pure virtuals
    Core::hresult SerialNumber(Exchange::IDeviceInfo::DeviceSerialNo&)  const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult Sku(Exchange::IDeviceInfo::DeviceModelNo&)            const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult Make(Exchange::IDeviceInfo::DeviceMake&)              const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult Model(Exchange::IDeviceInfo::DeviceModel&)            const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult DeviceType(Exchange::IDeviceInfo::DeviceTypeInfos&)   const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult SocName(Exchange::IDeviceInfo::DeviceSoc&)            const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult DistributorId(Exchange::IDeviceInfo::DeviceDistId&)   const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult Brand(Exchange::IDeviceInfo::DeviceBrand&)            const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult ReleaseVersion(Exchange::IDeviceInfo::DeviceReleaseVer&) const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult ChipSet(Exchange::IDeviceInfo::DeviceChip&)           const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult SystemInfo(Exchange::IDeviceInfo::SystemInfos&)       const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult Addresses(Exchange::IDeviceInfo::IAddressesInfoIterator*&) const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult EthMac(Exchange::IDeviceInfo::EthernetMac&)           const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult EstbMac(Exchange::IDeviceInfo::StbMac&)               const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult WifiMac(Exchange::IDeviceInfo::WiFiMac&)              const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult EstbIp(Exchange::IDeviceInfo::StbIp&)                 const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult SupportedAudioPorts(Exchange::IDeviceInfo::IStringIterator*&, bool&) const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult DeviceId(Exchange::IDeviceInfo::DeviceIdInfo&)        const override { return Core::ERROR_UNAVAILABLE; }
    Core::hresult HardwareId(Exchange::IDeviceInfo::HardwareIdInfo&)    const override { return Core::ERROR_UNAVAILABLE; }

private:
    mutable std::atomic<uint32_t> _refCount;
};

// ============================================================================
// Test fixture
// ============================================================================
class BrandingApiTest : public ::testing::Test {
protected:
    NiceMock<ServiceMock>  service;
    Core::Sink<AppGatewayCommon> plugin;
    NiceMock<MockDeviceInfo>* mockDi { nullptr };

    void SetUp() override
    {
        mockDi = new NiceMock<MockDeviceInfo>();

        ON_CALL(service, QueryInterfaceByCallsign(_, _))
            .WillByDefault(Return(nullptr));

        // Wire IDeviceInfo to the "DeviceInfo" callsign
        ON_CALL(service,
                QueryInterfaceByCallsign(Exchange::IDeviceInfo::ID, StrEq("DeviceInfo")))
            .WillByDefault(Invoke([this](const uint32_t, const string&) -> void* {
                mockDi->AddRef();
                return static_cast<Exchange::IDeviceInfo*>(mockDi);
            }));

        EXPECT_CALL(service, AddRef()).Times(1);
        EXPECT_CALL(service, Release()).Times(1).WillOnce(Return(Core::ERROR_NONE));

        plugin.Initialize(&service);
    }

    void TearDown() override
    {
        plugin.Deinitialize(&service);
        // Release the fixture-owned reference (initial refcount of 1).
        // Any reference held by the plugin was already released in Deinitialize.
        mockDi->Release();
    }
};

// ============================================================================
// Device.setOsName tests
// ============================================================================

/* AGC_L1_233 — Device.setOsName: valid payload, IDeviceInfo::OsName(set) succeeds */
TEST_F(BrandingApiTest, AGC_L1_233_SetOsName_Success)
{
    ON_CALL(*mockDi, OsName(testing::An<const string&>()))
        .WillByDefault(Return(Core::ERROR_NONE));

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.setOsName", "{\"value\":\"RDK-E\"}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

/* AGC_L1_234 — Device.setOsName: IDeviceInfo::OsName(set) returns ERROR_GENERAL */
TEST_F(BrandingApiTest, AGC_L1_234_SetOsName_InterfaceFailure)
{
    ON_CALL(*mockDi, OsName(testing::An<const string&>()))
        .WillByDefault(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.setOsName", "{\"value\":\"RDK-E\"}", result);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
}

/* AGC_L1_235 — Device.setOsName: missing 'value' field returns BAD_REQUEST */
TEST_F(BrandingApiTest, AGC_L1_235_SetOsName_InvalidPayload)
{
    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.setOsName", "{}", result);

    EXPECT_EQ(Core::ERROR_BAD_REQUEST, rc);
}

/* AGC_L1_236 — Device.setOsName: null delegate returns UNAVAILABLE */
TEST(BrandingApiNullDelegateTest, AGC_L1_236_SetOsName_NullDelegate)
{
    Core::Sink<AppGatewayCommon> plugin;
    plugin.mDelegate.reset();

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.setOsName", "{\"value\":\"RDK-E\"}", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, rc);
}

// ============================================================================
// Device.osName tests
// ============================================================================

/* AGC_L1_237 — Device.osName: IDeviceInfo::OsName(get) succeeds, returns quoted string */
TEST_F(BrandingApiTest, AGC_L1_237_GetOsName_Success)
{
    ON_CALL(*mockDi, OsName(testing::An<Exchange::IDeviceInfo::DeviceOsName&>()))
        .WillByDefault(Invoke([](Exchange::IDeviceInfo::DeviceOsName& out) {
            out.osName = "RDK-E";
            return Core::ERROR_NONE;
        }));

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.osName", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("\"RDK-E\"", result);
}

/* AGC_L1_238 — Device.osName: empty osname returns quoted empty string */
TEST_F(BrandingApiTest, AGC_L1_238_GetOsName_EmptyValue)
{
    ON_CALL(*mockDi, OsName(testing::An<Exchange::IDeviceInfo::DeviceOsName&>()))
        .WillByDefault(Invoke([](Exchange::IDeviceInfo::DeviceOsName& out) {
            out.osName = "";
            return Core::ERROR_NONE;
        }));

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.osName", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("\"\"", result);
}

/* AGC_L1_239 — Device.osName: IDeviceInfo::OsName(get) returns ERROR_GENERAL */
TEST_F(BrandingApiTest, AGC_L1_239_GetOsName_InterfaceFailure)
{
    ON_CALL(*mockDi, OsName(testing::An<Exchange::IDeviceInfo::DeviceOsName&>()))
        .WillByDefault(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.osName", "{}", result);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
}

/* AGC_L1_240 — Device.osName: DeviceInfo plugin unavailable */
TEST(BrandingApiUnavailableTest, AGC_L1_240_GetOsName_DeviceInfoUnavailable)
{
    NiceMock<ServiceMock> service;
    Core::Sink<AppGatewayCommon> plugin;

    ON_CALL(service, QueryInterfaceByCallsign(_, _)).WillByDefault(Return(nullptr));
    EXPECT_CALL(service, AddRef()).Times(1);
    EXPECT_CALL(service, Release()).Times(1).WillOnce(Return(Core::ERROR_NONE));
    plugin.Initialize(&service);

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.osName", "{}", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, rc);

    plugin.Deinitialize(&service);
}

/* AGC_L1_241 — Device.osName: null delegate returns UNAVAILABLE */
TEST(BrandingApiNullDelegateTest, AGC_L1_241_GetOsName_NullDelegate)
{
    Core::Sink<AppGatewayCommon> plugin;
    plugin.mDelegate.reset();

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.osName", "{}", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, rc);
}

// ============================================================================
// Device.setOsVersion tests
// ============================================================================

/* AGC_L1_242 — Device.setOsVersion: valid payload, IDeviceInfo::OsVersion(set) succeeds */
TEST_F(BrandingApiTest, AGC_L1_242_SetOsVersion_Success)
{
    ON_CALL(*mockDi, OsVersion(testing::An<const string&>()))
        .WillByDefault(Return(Core::ERROR_NONE));

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.setOsVersion", "{\"value\":\"8.3\"}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("null", result);
}

/* AGC_L1_243 — Device.setOsVersion: IDeviceInfo::OsVersion(set) returns ERROR_GENERAL */
TEST_F(BrandingApiTest, AGC_L1_243_SetOsVersion_InterfaceFailure)
{
    ON_CALL(*mockDi, OsVersion(testing::An<const string&>()))
        .WillByDefault(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.setOsVersion", "{\"value\":\"8.3\"}", result);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
}

/* AGC_L1_244 — Device.setOsVersion: missing 'value' field returns BAD_REQUEST */
TEST_F(BrandingApiTest, AGC_L1_244_SetOsVersion_InvalidPayload)
{
    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.setOsVersion", "{}", result);

    EXPECT_EQ(Core::ERROR_BAD_REQUEST, rc);
}

/* AGC_L1_245 — Device.setOsVersion: null delegate returns UNAVAILABLE */
TEST(BrandingApiNullDelegateTest, AGC_L1_245_SetOsVersion_NullDelegate)
{
    Core::Sink<AppGatewayCommon> plugin;
    plugin.mDelegate.reset();

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.setOsVersion", "{\"value\":\"8.3\"}", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, rc);
}

// ============================================================================
// Device.osVersion tests
// ============================================================================

/* AGC_L1_246 — Device.osVersion: IDeviceInfo::OsVersion(get) succeeds, returns quoted string */
TEST_F(BrandingApiTest, AGC_L1_246_GetOsVersion_Success)
{
    ON_CALL(*mockDi, OsVersion(testing::An<Exchange::IDeviceInfo::DeviceOsVersion&>()))
        .WillByDefault(Invoke([](Exchange::IDeviceInfo::DeviceOsVersion& out) {
            out.osVersion = "8.3";
            return Core::ERROR_NONE;
        }));

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.osVersion", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("\"8.3\"", result);
}

/* AGC_L1_247 — Device.osVersion: empty osversion returns quoted empty string */
TEST_F(BrandingApiTest, AGC_L1_247_GetOsVersion_EmptyValue)
{
    ON_CALL(*mockDi, OsVersion(testing::An<Exchange::IDeviceInfo::DeviceOsVersion&>()))
        .WillByDefault(Invoke([](Exchange::IDeviceInfo::DeviceOsVersion& out) {
            out.osVersion = "";
            return Core::ERROR_NONE;
        }));

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.osVersion", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("\"\"", result);
}

/* AGC_L1_248 — Device.osVersion: IDeviceInfo::OsVersion(get) returns ERROR_GENERAL */
TEST_F(BrandingApiTest, AGC_L1_248_GetOsVersion_InterfaceFailure)
{
    ON_CALL(*mockDi, OsVersion(testing::An<Exchange::IDeviceInfo::DeviceOsVersion&>()))
        .WillByDefault(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.osVersion", "{}", result);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
}

/* AGC_L1_249 — Device.osVersion: DeviceInfo plugin unavailable */
TEST(BrandingApiUnavailableTest, AGC_L1_249_GetOsVersion_DeviceInfoUnavailable)
{
    NiceMock<ServiceMock> service;
    Core::Sink<AppGatewayCommon> plugin;

    ON_CALL(service, QueryInterfaceByCallsign(_, _)).WillByDefault(Return(nullptr));
    EXPECT_CALL(service, AddRef()).Times(1);
    EXPECT_CALL(service, Release()).Times(1).WillOnce(Return(Core::ERROR_NONE));
    plugin.Initialize(&service);

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.osVersion", "{}", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, rc);

    plugin.Deinitialize(&service);
}

/* AGC_L1_250 — Device.osVersion: null delegate returns UNAVAILABLE */
TEST(BrandingApiNullDelegateTest, AGC_L1_250_GetOsVersion_NullDelegate)
{
    Core::Sink<AppGatewayCommon> plugin;
    plugin.mDelegate.reset();

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.osVersion", "{}", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, rc);
}

// ============================================================================
// Device.firmware tests
// ============================================================================

/* AGC_L1_251 — Device.firmware: IDeviceInfo::FirmwareVersion().imagename succeeds */
TEST_F(BrandingApiTest, AGC_L1_251_GetFirmware_Success)
{
    ON_CALL(*mockDi, FirmwareVersion(_))
        .WillByDefault(Invoke([](Exchange::IDeviceInfo::FirmwareversionInfo& out) {
            out.imagename = "SKXI11ADS_MIDDLEWARE_DEV_develop_20251101123542";
            return Core::ERROR_NONE;
        }));

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.firmware", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("\"SKXI11ADS_MIDDLEWARE_DEV_develop_20251101123542\"", result);
}

/* AGC_L1_252 — Device.firmware: empty imagename returns quoted empty string */
TEST_F(BrandingApiTest, AGC_L1_252_GetFirmware_EmptyImageName)
{
    ON_CALL(*mockDi, FirmwareVersion(_))
        .WillByDefault(Invoke([](Exchange::IDeviceInfo::FirmwareversionInfo& out) {
            out.imagename = "";
            return Core::ERROR_NONE;
        }));

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.firmware", "{}", result);

    EXPECT_EQ(Core::ERROR_NONE, rc);
    EXPECT_EQ("\"\"", result);
}

/* AGC_L1_253 — Device.firmware: IDeviceInfo::FirmwareVersion() returns ERROR_GENERAL */
TEST_F(BrandingApiTest, AGC_L1_253_GetFirmware_InterfaceFailure)
{
    ON_CALL(*mockDi, FirmwareVersion(_))
        .WillByDefault(Return(Core::ERROR_GENERAL));

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.firmware", "{}", result);

    EXPECT_EQ(Core::ERROR_GENERAL, rc);
}

/* AGC_L1_254 — Device.firmware: DeviceInfo plugin unavailable */
TEST(BrandingApiUnavailableTest, AGC_L1_254_GetFirmware_DeviceInfoUnavailable)
{
    NiceMock<ServiceMock> service;
    Core::Sink<AppGatewayCommon> plugin;

    ON_CALL(service, QueryInterfaceByCallsign(_, _)).WillByDefault(Return(nullptr));
    EXPECT_CALL(service, AddRef()).Times(1);
    EXPECT_CALL(service, Release()).Times(1).WillOnce(Return(Core::ERROR_NONE));
    plugin.Initialize(&service);

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.firmware", "{}", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, rc);

    plugin.Deinitialize(&service);
}

/* AGC_L1_255 — Device.firmware: null delegate returns UNAVAILABLE */
TEST(BrandingApiNullDelegateTest, AGC_L1_255_GetFirmware_NullDelegate)
{
    Core::Sink<AppGatewayCommon> plugin;
    plugin.mDelegate.reset();

    const auto ctx = MakeCtx();
    string result;
    const auto rc = plugin.HandleAppGatewayRequest(ctx, "Device.firmware", "{}", result);

    EXPECT_EQ(Core::ERROR_UNAVAILABLE, rc);
}

// ============================================================================
// Case-insensitivity routing checks
// ============================================================================

/* AGC_L1_256 — Method names are case-insensitive for all branding APIs */
TEST_F(BrandingApiTest, AGC_L1_256_CaseInsensitiveRouting)
{
    ON_CALL(*mockDi, OsName(testing::An<Exchange::IDeviceInfo::DeviceOsName&>()))
        .WillByDefault(Invoke([](Exchange::IDeviceInfo::DeviceOsName& out) {
            out.osName = "RDK-E";
            return Core::ERROR_NONE;
        }));

    const auto ctx = MakeCtx();
    string result;

    EXPECT_EQ(Core::ERROR_NONE,
              plugin.HandleAppGatewayRequest(ctx, "DEVICE.OSNAME", "{}", result));
    EXPECT_EQ("\"RDK-E\"", result);

    result.clear();
    EXPECT_EQ(Core::ERROR_NONE,
              plugin.HandleAppGatewayRequest(ctx, "device.osname", "{}", result));
    EXPECT_EQ("\"RDK-E\"", result);
}

} // namespace
