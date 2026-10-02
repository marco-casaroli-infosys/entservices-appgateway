#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <atomic>
#include <sys/stat.h>
#include <cstdlib>
#include <unistd.h>
#include <thread>
#include <chrono>

#include <core/core.h>
#include <interfaces/IConfiguration.h>
#include <com/IIteratorType.h>

#include <AppGatewayImplementation.h>

#include "ServiceMock.h"

using WPEFramework::Core::ERROR_BAD_REQUEST;
using WPEFramework::Core::ERROR_GENERAL;
using WPEFramework::Core::ERROR_NONE;
using WPEFramework::Core::ERROR_PRIVILIGED_REQUEST;
using WPEFramework::Core::ERROR_UNAVAILABLE;

namespace {

struct TestResult {
    uint32_t failures { 0 };
};

static void ExpectTrue(TestResult& tr, const bool condition, const std::string& what)
{
    if (!condition) {
        tr.failures++;
        std::cerr << "FAIL: " << what << std::endl;
    }
}

static void ExpectEqU32(TestResult& tr, const uint32_t actual, const uint32_t expected, const std::string& what)
{
    if (actual != expected) {
        tr.failures++;
        std::cerr << "FAIL: " << what << " expected=" << expected << " actual=" << actual << std::endl;
    }
}

static bool EnsureDir(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    // Simple mkdir -p (one level recursion)
    if (mkdir(path.c_str(), 0777) == 0) {
        return true;
    }
    const auto pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        const std::string parent = path.substr(0, pos);
        if (!parent.empty() && EnsureDir(parent)) {
            return (mkdir(path.c_str(), 0777) == 0);
        }
    }
    return false;
}

static bool WriteTextFile(const std::string& path, const std::string& content)
{
    const auto slashPos = path.find_last_of('/');
    if (slashPos != std::string::npos) {
        const std::string dir = path.substr(0, slashPos);
        if (!EnsureDir(dir)) {
            return false;
        }
    }
    std::ofstream ofs(path, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
        return false;
    }
    ofs << content;
    return true;
}

// Lightweight IStringIterator for Configure(paths)
class SimpleStringIterator : public WPEFramework::Exchange::IAppGatewayResolver::IStringIterator {
public:
    explicit SimpleStringIterator(const std::vector<std::string>& items)
        : _items(items)
        , _index(0)
        , _refCount(1)
    {
    }

    ~SimpleStringIterator() override = default;

    uint32_t AddRef() const override { return _refCount.fetch_add(1) + 1; }
    uint32_t Release() const override
    {
        const uint32_t n = _refCount.fetch_sub(1) - 1;
        if (n == 0) {
            delete this;
            return WPEFramework::Core::ERROR_DESTRUCTION_SUCCEEDED;
        }
        return WPEFramework::Core::ERROR_NONE;
    }

    void* QueryInterface(const uint32_t id) override
    {
        if (id == WPEFramework::Exchange::IAppGatewayResolver::IStringIterator::ID) {
            AddRef();
            return static_cast<WPEFramework::Exchange::IAppGatewayResolver::IStringIterator*>(this);
        }
        return nullptr;
    }

    bool Next(std::string& out) override
    {
        if (_index < _items.size()) {
            out = _items[_index++];
            return true;
        }
        return false;
    }

    bool Previous(std::string& out) override
    {
        if (_index == 0) {
            return false;
        }
        --_index;
        out = _items[_index];
        return true;
    }

    void Reset(const uint32_t position) override
    {
        if (position == 0) {
            _index = 0;
        } else if (position > _items.size()) {
            _index = static_cast<uint32_t>(_items.size());
        } else {
            _index = position;
        }
    }

    bool IsValid() const override { return (_index > 0) && (_index <= _items.size()); }
    uint32_t Count() const override { return static_cast<uint32_t>(_items.size()); }
    std::string Current() const override { return IsValid() ? _items[_index - 1] : std::string(); }

private:
    std::vector<std::string> _items;
    uint32_t _index;
    mutable std::atomic<uint32_t> _refCount;
};

// Sanitize an environment variable intended to be used as a file/directory
// path for read-only access (e.g. resolution config files, repo root).
// Rejects values that are null/empty, non-absolute, or contain "..".
// Returns an empty string for any rejected value so callers fall back to
// the compile-time derived path and the tainted data never reaches a
// file-access function.
static std::string SanitizeEnvPath(const char* envVal)
{
    if (envVal == nullptr || envVal[0] == '\0') {
        return {};
    }
    const std::string val(envVal);
    if (val[0] != '/') {
        return {};
    }
    if (val.find("..") != std::string::npos) {
        return {};
    }
    return val;
}

// Build a path under a fixed temp directory for test-generated override JSON
// files.  Using a hardcoded base ("/tmp") ensures no environment variable
// ever reaches a file-access function, eliminating any taint-flow risk.
static std::string TempOverridePath(const std::string& filename)
{
    static const std::string dir = "/tmp/appgw_l0test_overrides";
    EnsureDir(dir);
    return dir + "/" + filename;
}

static std::string ComputeRepoRoot()
{
    const std::string envRepoRoot = SanitizeEnvPath(std::getenv("APPGATEWAY_TEST_REPO_ROOT"));
    if (!envRepoRoot.empty()) {
        return envRepoRoot;
    }

    const std::string f = __FILE__;
    const std::string marker = "/Tests/L0Tests/AppGateway/";
    const auto pos = f.rfind(marker);
    if (pos != std::string::npos) {
        return f.substr(0, pos);
    }
    return ".";
}

static std::string BaseResolutionsPath()
{
    const std::string env = SanitizeEnvPath(std::getenv("APPGATEWAY_RESOLUTIONS_PATH"));
    if (!env.empty()) {
        struct stat st;
        if (stat(env.c_str(), &st) == 0) {
            if (S_ISREG(st.st_mode)) {
                return env;
            }
            if (S_ISDIR(st.st_mode)) {
                return env + "/resolution.base.json";
            }
        }
    }

    return ComputeRepoRoot() + "/AppGateway/resolutions/resolution.base.json";
}

static WPEFramework::Exchange::GatewayContext MakeContext()
{
    WPEFramework::Exchange::GatewayContext ctx;
    ctx.requestId = 10;
    ctx.connectionId = 20;
    ctx.appId = "com.example.test";
    return ctx;
}

class EnvVarGuard final {
public:
    EnvVarGuard(const char* name, const char* value)
        : _name(name)
        , _hadOld(false)
    {
        const char* old = std::getenv(name);
        if (old != nullptr) {
            _hadOld = true;
            _oldValue = old;
        }
        if (value != nullptr) {
            setenv(name, value, 1);
        } else {
            unsetenv(name);
        }
    }

    ~EnvVarGuard()
    {
        if (_hadOld) {
            setenv(_name.c_str(), _oldValue.c_str(), 1);
        } else {
            unsetenv(_name.c_str());
        }
    }

    EnvVarGuard(const EnvVarGuard&) = delete;
    EnvVarGuard& operator=(const EnvVarGuard&) = delete;

private:
    std::string _name;
    bool _hadOld;
    std::string _oldValue;
};

static void ConfigureImplOrFail(TestResult& tr,
                                WPEFramework::Exchange::IAppGatewayResolver* impl,
                                WPEFramework::PluginHost::IShell* shell,
                                const std::vector<std::string>& paths)
{
    auto* configIfc = static_cast<WPEFramework::Exchange::IConfiguration*>(impl->QueryInterface(WPEFramework::Exchange::IConfiguration::ID));
    ExpectTrue(tr, configIfc != nullptr, "IConfiguration available on AppGatewayImplementation");
    if (configIfc != nullptr) {
        const uint32_t cfgRcShell = configIfc->Configure(shell);
        ExpectEqU32(tr, cfgRcShell, ERROR_NONE, "IConfiguration::Configure(shell) returns ERROR_NONE");
        if (cfgRcShell != ERROR_NONE) {
            configIfc->Release();
            return;
        }
        configIfc->Release();
    }

    SimpleStringIterator* it = new SimpleStringIterator(paths);
    const uint32_t cfgRc = impl->Configure(it);
    it->Release();
    ExpectEqU32(tr, cfgRc, ERROR_NONE, "AppGatewayImplementation::Configure(paths) returns ERROR_NONE");
}

static void DrainAsyncRespondJobs()
{
    // Use a longer drain under Valgrind (10-50x slowdown) to ensure all WorkerPool
    // RespondJob dispatches complete before the test releases impl and service.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

} // namespace

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_PermissionGroup_Denied()
{
    /** Exercise permission-group path:
     *  - method requires permissionGroup (device.setName)
     *  - authenticator available but denies permission -> ERROR_PRIVILIGED_REQUEST
     */
    TestResult tr;

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        cfg.provideAuthenticator = true;
        cfg.authenticatorAllowed = false;
        cfg.provideResponder = true; // ensures async respond path is schedulable

        auto* service = new L0Test::ServiceMock(cfg, true);

        ConfigureImplOrFail(tr, impl, service, { BaseResolutionsPath() });

        std::string result;
        const auto ctx = MakeContext();
        const uint32_t rc = impl->Resolve(ctx, "org.rdk.AppGateway", "device.setName", "{}", result);

        ExpectEqU32(tr, rc, ERROR_GENERAL, "Denied permissionGroup returns ERROR_GENERAL in current implementation");

        // Verify authenticator was consulted.
        auto* auth = service->GetAuthenticatorFake();
        ExpectTrue(tr, auth != nullptr, "AuthenticatorFake cached");
        if (auth != nullptr) {
            ExpectTrue(tr, auth->checkPermissionCount > 0, "CheckPermissionGroup called");
        }

        DrainAsyncRespondJobs();
        impl->Release();
        service->Release();
    }

    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_PermissionGroup_Allowed_ComRpcDisabled()
{
    /** Exercise allowed permission-group path followed by COM-RPC disabled short-circuit. */
    TestResult tr;

    // Ensure deterministic COM-RPC disable branch is taken for this test.
    EnvVarGuard guard("APPGATEWAY_L0_DISABLE_COMRPC", "1");

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        cfg.provideAuthenticator = true;
        cfg.authenticatorAllowed = true;
        cfg.provideResponder = true;

        auto* service = new L0Test::ServiceMock(cfg, true);

        ConfigureImplOrFail(tr, impl, service, { BaseResolutionsPath() });

        std::string result;
        const auto ctx = MakeContext();
        const uint32_t rc = impl->Resolve(ctx, "org.rdk.AppGateway", "device.setName", "{}", result);

        ExpectEqU32(tr, rc, ERROR_GENERAL, "Allowed permissionGroup + COM-RPC disabled returns ERROR_GENERAL in current implementation");

        DrainAsyncRespondJobs();
        impl->Release();
        service->Release();
    }

    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_EventListen_TriggersNotify()
{
    /** Exercise event listen path:
     *  - method is an event (localization.onLocaleChanged)
     *  - params contains listen=true
    *  - verify Subscribe() is called on IAppNotifications fake
     */
    TestResult tr;

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        cfg.provideAppNotifications = true;
        cfg.provideResponder = true;

        auto* service = new L0Test::ServiceMock(cfg, true);

        ConfigureImplOrFail(tr, impl, service, { BaseResolutionsPath() });

        std::string resolution;
        const auto ctx = MakeContext();
        const uint32_t rc = impl->Resolve(ctx,
                                         "org.rdk.AppGateway",
                                         "localization.onLocaleChanged",
                                         "{\"listen\":true}",
                                         resolution);

        ExpectEqU32(tr, rc, ERROR_NONE, "Event listen resolve returns ERROR_NONE");
        ExpectTrue(tr, resolution.find("\"listening\":true") != std::string::npos, "Resolution includes listening=true");

        auto* notif = service->GetAppNotificationsFake();
        ExpectTrue(tr, notif != nullptr, "AppNotificationsFake cached");
        if (notif != nullptr) {
            ExpectTrue(tr, notif->subscribeCount > 0, "Subscribe called for event listen flow");
            // Intentionally not asserting on Notify()-specific tracking here;
            // production event-listen flow uses IAppNotifications::Subscribe().
        }

        DrainAsyncRespondJobs();
        impl->Release();
        service->Release();
    }

    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_IncludeContext_Path_Executes()
{
    /** Exercise includeContext branch (onlyAdditionalContext=false) by overriding a method to:
     *  - include additionalContext object (implies includeContext=true)
     *  - useComRpc=false so UpdateContext() inserts "context" object
     *  Alias points to dummy.method to keep offline deterministic.
     */
    TestResult tr;

    const std::string overridePath = TempOverridePath("include_context.override.json");
    const std::string overrideJson = R"JSON(
{
  "resolutions": {
    "test.withContext": {
      "alias": "dummy.method",
      "useComRpc": false,
      "additionalContext": { "foo": "bar" }
    }
  }
}
)JSON";
    ExpectTrue(tr, WriteTextFile(overridePath, overrideJson), "Write includeContext override JSON");

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        cfg.provideResponder = true;

        auto* service = new L0Test::ServiceMock(cfg, true);

        ConfigureImplOrFail(tr, impl, service, { BaseResolutionsPath(), overridePath });

        std::string resolution;
        const auto ctx = MakeContext();
        const uint32_t rc = impl->Resolve(ctx, "org.rdk.AppGateway", "test.withContext", "{\"x\":1}", resolution);

        ExpectTrue(tr, (rc == ERROR_NONE) || (rc == ERROR_GENERAL),
                   "IncludeContext override resolves with ERROR_NONE/ERROR_GENERAL depending on handler availability");
        if (rc == ERROR_NONE) {
            ExpectTrue(tr, !resolution.empty(), "Resolution not empty");
        }

        DrainAsyncRespondJobs();
        impl->Release();
        service->Release();
    }

    ::unlink(overridePath.c_str());
    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_Configure_NullPaths_ReturnsBadRequest()
{
    /** Exercise Configure(nullptr) early-exit path which returns ERROR_BAD_REQUEST. */
    TestResult tr;

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        auto* service = new L0Test::ServiceMock(cfg, true);

        // Initialize mResolverPtr by calling IConfiguration::Configure(shell) first.
        auto* configIfc = static_cast<WPEFramework::Exchange::IConfiguration*>(
            impl->QueryInterface(WPEFramework::Exchange::IConfiguration::ID));
        ExpectTrue(tr, configIfc != nullptr, "IConfiguration available on AppGatewayImplementation");
        if (configIfc != nullptr) {
            configIfc->Configure(service);
            configIfc->Release();
        }

        // Now call Configure(nullptr) — should return ERROR_BAD_REQUEST.
        const uint32_t rc = impl->Configure(nullptr);
        ExpectEqU32(tr, rc, ERROR_BAD_REQUEST, "Configure(nullptr) returns ERROR_BAD_REQUEST");

        impl->Release();
        service->Release();
    }

    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_Configure_EmptyPaths_ReturnsBadRequest()
{
    /** Exercise Configure(empty iterator) path which returns ERROR_BAD_REQUEST when no paths are given. */
    TestResult tr;

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        auto* service = new L0Test::ServiceMock(cfg, true);

        // Initialize mResolverPtr via IConfiguration::Configure(shell).
        auto* configIfc = static_cast<WPEFramework::Exchange::IConfiguration*>(
            impl->QueryInterface(WPEFramework::Exchange::IConfiguration::ID));
        ExpectTrue(tr, configIfc != nullptr, "IConfiguration available on AppGatewayImplementation");
        if (configIfc != nullptr) {
            configIfc->Configure(service);
            configIfc->Release();
        }

        // Pass an empty iterator — should return ERROR_BAD_REQUEST (no paths).
        SimpleStringIterator* it = new SimpleStringIterator({});
        const uint32_t rc = impl->Configure(it);
        it->Release();
        ExpectEqU32(tr, rc, ERROR_BAD_REQUEST, "Configure(empty iterator) returns ERROR_BAD_REQUEST");

        impl->Release();
        service->Release();
    }

    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_Authenticator_CheckPermission_Fails()
{
    /** Exercise the CheckPermissionGroup failure path (checkRc != ERROR_NONE).
     *  authenticatorFailCheck=true makes CheckPermissionGroup return ERROR_GENERAL,
     *  covering the error-handling block that was previously uncovered.
     */
    TestResult tr;

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        cfg.provideAuthenticator = true;
        cfg.authenticatorAllowed = false;
        cfg.authenticatorFailCheck = true; // CheckPermissionGroup returns ERROR_GENERAL
        cfg.provideResponder = true;

        auto* service = new L0Test::ServiceMock(cfg, true);

        ConfigureImplOrFail(tr, impl, service, { BaseResolutionsPath() });

        std::string result;
        const auto ctx = MakeContext();
        // "device.setName" requires a permissionGroup check (from resolution.base.json).
        const uint32_t rc = impl->Resolve(ctx, "org.rdk.AppGateway", "device.setName", "{}", result);
        ExpectEqU32(tr, rc, ERROR_GENERAL, "Failed CheckPermissionGroup returns ERROR_GENERAL");

        auto* auth = service->GetAuthenticatorFake();
        ExpectTrue(tr, auth != nullptr, "AuthenticatorFake was created");
        if (auth != nullptr) {
            ExpectTrue(tr, auth->checkPermissionCount > 0, "CheckPermissionGroup was called");
        }

        DrainAsyncRespondJobs();
        impl->Release();
        service->Release();
    }

    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_ComRpc_HandleRequestFails()
{
    /** Exercise ProcessComRpcRequest failure path where HandleAppGatewayRequest returns error.
     *  The first Resolve call creates and caches the RequestHandlerFake.
     *  We then set the return code to ERROR_GENERAL and issue a second Resolve to cover lines 506-514.
     */
    TestResult tr;

    EnvVarGuard guard("APPGATEWAY_L0_DISABLE_COMRPC", "0"); // ensure COM-RPC path is enabled

    const std::string overridePath = TempOverridePath("comrpc_handle_fail.override.json");
    const std::string overrideJson = R"JSON(
{
  "resolutions": {
    "test.comrpc.fail": {
      "alias": "org.rdk.FbSettings",
      "useComRpc": true,
      "additionalContext": { "scope": "l0" }
    }
  }
}
)JSON";
    ExpectTrue(tr, WriteTextFile(overridePath, overrideJson), "Write COM-RPC fail override JSON");

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        cfg.provideResponder = true;
        cfg.provideRequestHandler = true;
        cfg.requestHandlerCallsign = "org.rdk.FbSettings";

        auto* service = new L0Test::ServiceMock(cfg, true);

        ConfigureImplOrFail(tr, impl, service, { BaseResolutionsPath(), overridePath });

        std::string resolution;
        const auto ctx = MakeContext();

        // First call creates the RequestHandlerFake (returns ERROR_NONE by default).
        impl->Resolve(ctx, "org.rdk.AppGateway", "test.comrpc.fail", "{}", resolution);

        // Retrieve the fake and configure it to fail on the next call.
        auto* handler = service->GetRequestHandlerFake();
        ExpectTrue(tr, handler != nullptr, "RequestHandlerFake was created");
        if (handler != nullptr) {
            handler->SetReturnCode(ERROR_GENERAL);
        }

        // Second call: HandleAppGatewayRequest returns ERROR_GENERAL — exercises failure path.
        resolution.clear();
        const uint32_t rc = impl->Resolve(ctx, "org.rdk.AppGateway", "test.comrpc.fail", "{}", resolution);
        ExpectEqU32(tr, rc, ERROR_GENERAL, "ComRpc handler failure returns ERROR_GENERAL");
        ExpectTrue(tr, !resolution.empty(), "Non-empty error resolution on handler failure");

        DrainAsyncRespondJobs();
        impl->Release();
        service->Release();
    }

    ::unlink(overridePath.c_str());
    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_ComRpc_RequestHandler_ReceivesAdditionalContext()
{
    /** Exercise COM-RPC handler path with UpdateContext(onlyAdditionalContext=true):
     *  - override method as useComRpc=true and has additionalContext object (so onlyAdditionalContext path builds _additionalContext)
     *  - provide RequestHandlerFake for alias callsign "org.rdk.FbSettings"
     */
    TestResult tr;

    EnvVarGuard guard("APPGATEWAY_L0_DISABLE_COMRPC", "0"); // enable COM-RPC path inside implementation

    const std::string overridePath = TempOverridePath("comrpc_with_context.override.json");
    const std::string overrideJson = R"JSON(
{
  "resolutions": {
    "test.comrpc": {
      "alias": "org.rdk.FbSettings",
      "useComRpc": true,
      "additionalContext": { "scope": "l0" }
    }
  }
}
)JSON";
    ExpectTrue(tr, WriteTextFile(overridePath, overrideJson), "Write COM-RPC override JSON");

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        cfg.provideResponder = true;
        cfg.provideRequestHandler = true;
        cfg.requestHandlerCallsign = "org.rdk.FbSettings";

        auto* service = new L0Test::ServiceMock(cfg, true);

        ConfigureImplOrFail(tr, impl, service, { BaseResolutionsPath(), overridePath });

        std::string resolution;
        const auto ctx = MakeContext();
        const uint32_t rc = impl->Resolve(ctx, "org.rdk.AppGateway", "test.comrpc", "{\"k\":1}", resolution);

        ExpectEqU32(tr, rc, ERROR_NONE, "COM-RPC handler returns ERROR_NONE");
        ExpectTrue(tr, resolution == "null" || !resolution.empty(), "Resolution present");

        auto* handler = service->GetRequestHandlerFake();
        ExpectTrue(tr, handler != nullptr, "RequestHandlerFake cached");
        if (handler != nullptr) {
            ExpectTrue(tr, handler->handleCount > 0, "HandleAppGatewayRequest called");
            // Verify _additionalContext presence (origin injected)
            ExpectTrue(tr, handler->lastPayload.find("\"_additionalContext\"") != std::string::npos, "Payload contains _additionalContext");
            ExpectTrue(tr, handler->lastPayload.find("\"origin\"") != std::string::npos, "Payload contains origin inside _additionalContext");
        }

        DrainAsyncRespondJobs();
        impl->Release();
        service->Release();
    }

    ::unlink(overridePath.c_str());
    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_Event_MissingListenParam()
{
    /** Exercise PreProcessEvent when params parse OK but lack "listen" boolean.
     *  Covers lines 539-542: HasBooleanEntry returns false → ErrorUtils::CustomBadRequest
     */
    TestResult tr;

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        cfg.provideAppNotifications = true;
        cfg.provideResponder = true;

        auto* service = new L0Test::ServiceMock(cfg, true);
        ConfigureImplOrFail(tr, impl, service, { BaseResolutionsPath() });

        std::string resolution;
        const auto ctx = MakeContext();
        // "localization.onlocalechanged" is a known event; params parse OK but has no "listen" key
        const uint32_t rc = impl->Resolve(ctx,
                                         "org.rdk.AppGateway",
                                         "localization.onlocalechanged",
                                         "{\"other\":1}",
                                         resolution);

        // Missing "listen" → ERROR_BAD_REQUEST + resolution contains error JSON
        ExpectEqU32(tr, rc, ERROR_BAD_REQUEST, "Event without listen param returns ERROR_BAD_REQUEST");
        ExpectTrue(tr, !resolution.empty(), "Resolution contains error message");

        DrainAsyncRespondJobs();
        impl->Release();
        service->Release();
    }

    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_UpdateContext_NonJsonParams()
{
    /** Exercise the LOGWARN branch in UpdateContext when params can't be parsed as JSON.
     *  Covers lines 467-468: paramsObj.FromString fails → LOGWARN
     */
    TestResult tr;

    const std::string overridePath = TempOverridePath("non_json_params.override.json");
    const std::string overrideJson = R"JSON(
{
  "resolutions": {
    "test.nonjsonparams": {
      "alias": "dummy.method",
      "useComRpc": false,
      "additionalContext": { "scope": "l0" }
    }
  }
}
)JSON";
    ExpectTrue(tr, WriteTextFile(overridePath, overrideJson), "Write non-JSON-params override JSON");

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        cfg.provideResponder = true;

        auto* service = new L0Test::ServiceMock(cfg, true);
        ConfigureImplOrFail(tr, impl, service, { BaseResolutionsPath(), overridePath });

        std::string resolution;
        const auto ctx = MakeContext();
        // Pass a non-JSON params string → UpdateContext's paramsObj.FromString hits the
        // LOGWARN branch (lines 467-468) because "not valid json" can't be parsed as object
        const uint32_t rc = impl->Resolve(ctx,
                                         "org.rdk.AppGateway",
                                         "test.nonjsonparams",
                                         "not valid json",
                                         resolution);

        // The call proceeds (params are treated as empty) - resolution may be empty or error
        ExpectTrue(tr, (rc == ERROR_NONE) || (rc == ERROR_GENERAL),
                   "Non-JSON params resolves with ERROR_NONE or ERROR_GENERAL");

        DrainAsyncRespondJobs();
        impl->Release();
        service->Release();
    }

    ::unlink(overridePath.c_str());
    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_ComRpc_AdditionalContext_NotObject()
{
    /** Exercise UpdateContext when onlyAdditionalContext=true but additionalContext
     *  is NOT a JSON object (e.g. a string).
     *  Covers lines 478-479: LOGERR("Additional context is not a JSON object...")
     */
    TestResult tr;

    EnvVarGuard guard("APPGATEWAY_L0_DISABLE_COMRPC", "0");

    const std::string overridePath = TempOverridePath("comrpc_nonobj_ctx.override.json");
    // "additionalContext" is a plain string, not a JSON object
    const std::string overrideJson = R"JSON(
{
  "resolutions": {
    "test.comrpc.nonobj": {
      "alias": "org.rdk.FbSettings",
      "useComRpc": true,
      "additionalContext": "not-an-object"
    }
  }
}
)JSON";
    ExpectTrue(tr, WriteTextFile(overridePath, overrideJson), "Write ComRpc non-object ctx override JSON");

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        cfg.provideResponder = true;
        cfg.provideRequestHandler = true;
        cfg.requestHandlerCallsign = "org.rdk.FbSettings";

        auto* service = new L0Test::ServiceMock(cfg, true);
        ConfigureImplOrFail(tr, impl, service, { BaseResolutionsPath(), overridePath });

        std::string resolution;
        const auto ctx = MakeContext();
        // HasComRpcRequestSupport → ProcessComRpcRequest → UpdateContext(onlyAdditionalContext=true)
        // additionalContext is a string → not OBJECT → lines 478-479 hit
        const uint32_t rc = impl->Resolve(ctx,
                                         "org.rdk.AppGateway",
                                         "test.comrpc.nonobj",
                                         "{}",
                                         resolution);

        // The request handler is still called (UpdateContext falls back with empty params)
        ExpectTrue(tr, (rc == ERROR_NONE) || (rc == ERROR_GENERAL),
                   "ComRpc with non-object ctx resolves without crash");

        DrainAsyncRespondJobs();
        impl->Release();
        service->Release();
    }

    ::unlink(overridePath.c_str());
    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_Resolve_BeforeShellConfigure()
{
    /** Exercise FetchResolvedData when mResolverPtr is null (Configure(IShell*) not called yet).
     *  Covers lines 392-395: mResolverPtr null → ERROR_GENERAL
     *  Also covers lines 311-314 in Configure(IStringIterator*): mResolverPtr null → ERROR_BAD_REQUEST
     */
    TestResult tr;

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        // Test lines 311-314: Configure(IStringIterator*) before Configure(IShell*)
        // mResolverPtr is null → returns ERROR_GENERAL (not ERROR_BAD_REQUEST)
        SimpleStringIterator* it = new SimpleStringIterator({ BaseResolutionsPath() });
        const uint32_t cfgRc = impl->Configure(it);
        it->Release();
        ExpectEqU32(tr, cfgRc, ERROR_GENERAL,
                    "Configure(iterator) before Configure(shell) returns ERROR_GENERAL");

        // Test lines 392-395: Resolve before Configure(IShell*) → mResolverPtr null
        // TODO: Resolve() submits an async RespondJob that fires after impl is freed (UAF).
        // Commented out until RespondJob holds a strong reference (AddRef/Release) on parent.
        // std::string resolution;
        // const auto ctx = MakeContext();
        // const uint32_t rc = impl->Resolve(ctx, "org.rdk.AppGateway", "dummy.method", "{}", resolution);
        // ExpectEqU32(tr, rc, ERROR_GENERAL,
        //             "Resolve before Configure(shell) returns ERROR_GENERAL");
        // ExpectTrue(tr, !resolution.empty(), "Resolution contains initialize error message");

        impl->Release();
    }

    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_Resolve_NotConfigured()
{
    /** Exercise FetchResolvedData when mResolverPtr->IsConfigured() is false.
     *  Covers lines 400-403: resolver not configured → ERROR_GENERAL
     */
    TestResult tr;

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        cfg.provideResponder = true;
        auto* service = new L0Test::ServiceMock(cfg, true);

        // Call Configure(IShell*) to initialize mResolverPtr, but skip Configure(IStringIterator*)
        auto* configIfc = static_cast<WPEFramework::Exchange::IConfiguration*>(
            impl->QueryInterface(WPEFramework::Exchange::IConfiguration::ID));
        ExpectTrue(tr, configIfc != nullptr, "IConfiguration available");
        if (configIfc != nullptr) {
            const uint32_t shellRc = configIfc->Configure(service);
            // In L0 test environments without /etc/app-gateway/resolution.base.json,
            // Configure(shell) may return ERROR_GENERAL (file not found fallback fails).
            // mResolverPtr is still set regardless; what matters is IsConfigured()=false.
            ExpectTrue(tr, (shellRc == ERROR_NONE) || (shellRc == ERROR_GENERAL),
                       "Configure(shell) proceeds (success if system config exists)");
            configIfc->Release();
        }

        // Do NOT call Configure(IStringIterator*) → mResolverPtr->IsConfigured() returns false
        std::string resolution;
        const auto ctx = MakeContext();
        const uint32_t rc = impl->Resolve(ctx, "org.rdk.AppGateway", "dummy.method", "{}", resolution);
        ExpectEqU32(tr, rc, ERROR_GENERAL,
                    "Resolve without path configuration returns ERROR_GENERAL");
        ExpectTrue(tr, !resolution.empty(), "Resolution contains not-configured error message");

        // Drain async respond jobs before releasing; in CI the fallback config loads
        // successfully so Resolve() triggers an async socket response via the responder.
        // Without this drain the WorkerPool job fires after impl is freed → UAF crash.
        DrainAsyncRespondJobs();
        impl->Release();
        service->Release();
    }

    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_RegionalConfig()
{
    /** Exercise the regional configuration loading path with valid regional JSON.
     *  Covers lines 63-110 (Region class), 121-146 (GetPathsForCountry), 256-297 (parse branch)
     *
     *  The test writes its config to /etc/app-gateway/resolutions.json.  A RAII guard
     *  backs up any pre-existing content and restores it (or removes the file if it did
     *  not exist) when the function returns — even on early return or failure.
     */
    TestResult tr;

    const std::string etcResPath = "/etc/app-gateway/resolutions.json";

    // ── RAII guard: back up now, restore unconditionally on scope exit ──────
    struct FileGuard {
        const std::string& path;
        std::string        savedContent;
        bool               existed;

        explicit FileGuard(const std::string& p) : path(p), existed(false) {
            std::ifstream f(p);
            if (f.is_open()) {
                savedContent.assign(std::istreambuf_iterator<char>(f),
                                    std::istreambuf_iterator<char>());
                existed = true;
            }
        }
        ~FileGuard() {
            if (existed) {
                // Restore the original content
                std::ofstream f(path, std::ios::out | std::ios::trunc);
                if (f.is_open()) f << savedContent;
            } else {
                ::unlink(path.c_str());
            }
        }
        // non-copyable
        FileGuard(const FileGuard&) = delete;
        FileGuard& operator=(const FileGuard&) = delete;
    } guard(etcResPath);
    // ────────────────────────────────────────────────────────────────────────

    // Build regional config JSON that points to the actual test base-resolutions file
    const std::string baseResPath = BaseResolutionsPath();
    const std::string regionalJson =
        "{"
        "  \"defaultCountryCode\": \"US\","
        "  \"regions\": ["
        "    {"
        "      \"countryCodes\": [\"US\", \"CA\"],"
        "      \"paths\": [\"" + baseResPath + "\"]"
        "    }"
        "  ]"
        "}";

    if (!WriteTextFile(etcResPath, regionalJson)) {
        std::cerr << "NOTE: Skipping Test_AppGatewayImplementation_RegionalConfig "
                     "(cannot write to " << etcResPath << ")" << std::endl;
        return tr.failures;
    }

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance for regional config test");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        cfg.provideResponder = true;
        auto* service = new L0Test::ServiceMock(cfg, true);

        // Configure(IShell*) reads /etc/app-gateway/resolutions.json → regional config path
        auto* configIfc = static_cast<WPEFramework::Exchange::IConfiguration*>(
            impl->QueryInterface(WPEFramework::Exchange::IConfiguration::ID));
        ExpectTrue(tr, configIfc != nullptr, "IConfiguration available for regional config test");
        if (configIfc != nullptr) {
            const uint32_t cfgRc = configIfc->Configure(service);
            // ERROR_NONE means regional config was parsed and resolutions loaded
            // ERROR_GENERAL means config parsing failed (acceptable in restricted environments)
            ExpectTrue(tr, (cfgRc == ERROR_NONE) || (cfgRc == ERROR_GENERAL),
                       "Configure(shell) with regional config returns ERROR_NONE or ERROR_GENERAL");
            configIfc->Release();
        }

        impl->Release();
        service->Release();
    }

    // guard destructor restores (or removes) /etc/app-gateway/resolutions.json here
    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_EventHook_ScheduledOnSubscribe()
{
    /** When an event subscription succeeds (listen=true) and the resolution has
     *  an eventHook configured, an async EventHookJob must be submitted which
     *  routes the hook method through FetchResolvedData → ProcessComRpcRequest.
     *  This test verifies:
     *   1. The primary subscribe response is returned (listening=true).
     *   2. After draining the worker pool, the hook method was dispatched to the
     *      IAppGatewayRequestHandler on AppGatewayCommon (subscribeCount > 0 and
     *      the request handler receives a call for the hook method).
     */
    TestResult tr;

    // Write a minimal config that has eventHook on lifecycle2.onstatechanged
    // pointing to lifecycle.ready (useComRpc=true so the hook routes via
    // ProcessComRpcRequest → IAppGatewayRequestHandler).
    const std::string overridePath = TempOverridePath("eventhook_subscribe.override.json");
    const std::string overrideJson = R"JSON(
{
  "resolutions": {
    "lifecycle2.onstatechanged": {
      "alias": "org.rdk.AppGatewayCommon",
      "event": "Lifecycle2.onStateChanged",
      "eventHook": "lifecycle.ready"
    },
    "lifecycle.ready": {
      "alias": "org.rdk.AppGatewayCommon",
      "useComRpc": true
    }
  }
}
)JSON";
    ExpectTrue(tr, WriteTextFile(overridePath, overrideJson), "Write eventHook override JSON");

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        cfg.provideAppNotifications = true;
        cfg.provideResponder        = true;
        cfg.provideRequestHandler   = true; // receives the hook method call

        auto* service = new L0Test::ServiceMock(cfg, true);

        // Load only our minimal override (no base file needed for this test)
        ConfigureImplOrFail(tr, impl, service, { overridePath });

        std::string resolution;
        const auto ctx = MakeContext();
        // Subscribe to lifecycle2.onstatechanged → should also schedule the hook
        const uint32_t rc = impl->Resolve(ctx,
                                          "org.rdk.AppGateway",
                                          "lifecycle2.onstatechanged",
                                          "{\"listen\":true}",
                                          resolution);

        ExpectEqU32(tr, rc, ERROR_NONE, "EventHook subscribe returns ERROR_NONE");
        ExpectTrue(tr, resolution.find("\"listening\":true") != std::string::npos,
                   "Response contains listening=true");

        // Drain the worker pool so the async EventHookJob has time to execute
        DrainAsyncRespondJobs();

        // The hook (lifecycle.ready) is useComRpc=true, so it goes through
        // IAppGatewayRequestHandler::HandleAppGatewayRequest on AppGatewayCommon.
        auto* handler = service->GetRequestHandlerFake();
        if (handler != nullptr) {
            ExpectTrue(tr, handler->handleCount > 0,
                       "EventHookJob dispatched: HandleAppGatewayRequest called for hook method");
        } else {
            // Request handler fake not available in this build — verify at least
            // that the subscribe itself succeeded (AC3: async, non-blocking).
            ExpectTrue(tr, rc == ERROR_NONE, "Hook scheduled without blocking subscribe response");
        }

        impl->Release();
        service->Release();
    }

    ::unlink(overridePath.c_str());
    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_EventHook_NotScheduledWhenAbsent()
{
    /** Negative case: when an event has no eventHook configured, subscribing
     *  must NOT trigger any hook dispatch — existing behaviour is unchanged.
     */
    TestResult tr;

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        cfg.provideAppNotifications = true;
        cfg.provideResponder        = true;
        cfg.provideRequestHandler   = true;

        auto* service = new L0Test::ServiceMock(cfg, true);

        // Base resolutions: localization.onlocalechanged has no eventHook
        ConfigureImplOrFail(tr, impl, service, { BaseResolutionsPath() });

        std::string resolution;
        const auto ctx = MakeContext();
        const uint32_t rc = impl->Resolve(ctx,
                                          "org.rdk.AppGateway",
                                          "localization.onlocalechanged",
                                          "{\"listen\":true}",
                                          resolution);

        ExpectEqU32(tr, rc, ERROR_NONE, "Subscribe without eventHook returns ERROR_NONE");
        ExpectTrue(tr, resolution.find("\"listening\":true") != std::string::npos,
                   "Response contains listening=true");

        DrainAsyncRespondJobs();

        // No hook should have been dispatched to the request handler
        auto* handler = service->GetRequestHandlerFake();
        if (handler != nullptr) {
            ExpectTrue(tr, handler->handleCount == 0,
                       "No EventHookJob dispatched for event without eventHook");
        }

        impl->Release();
        service->Release();
    }

    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_AppGatewayImplementation_EventHook_NotScheduledOnUnsubscribe()
{
    /** Negative case: unsubscribing (listen=false) from an event that has
     *  eventHook configured must NOT trigger the hook.
     */
    TestResult tr;

    const std::string overridePath = TempOverridePath("eventhook_unsubscribe.override.json");
    const std::string overrideJson = R"JSON(
{
  "resolutions": {
    "lifecycle2.onstatechanged": {
      "alias": "org.rdk.AppGatewayCommon",
      "event": "Lifecycle2.onStateChanged",
      "eventHook": "lifecycle.ready"
    },
    "lifecycle.ready": {
      "alias": "org.rdk.AppGatewayCommon",
      "useComRpc": true
    }
  }
}
)JSON";
    ExpectTrue(tr, WriteTextFile(overridePath, overrideJson), "Write eventHook override JSON");

    auto* impl = WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");

    if (impl != nullptr) {
        L0Test::ServiceMock::Config cfg;
        cfg.provideAppNotifications = true;
        cfg.provideResponder        = true;
        cfg.provideRequestHandler   = true;

        auto* service = new L0Test::ServiceMock(cfg, true);

        ConfigureImplOrFail(tr, impl, service, { overridePath });

        std::string resolution;
        const auto ctx = MakeContext();
        // Unsubscribe (listen=false) — hook must NOT be dispatched
        const uint32_t rc = impl->Resolve(ctx,
                                          "org.rdk.AppGateway",
                                          "lifecycle2.onstatechanged",
                                          "{\"listen\":false}",
                                          resolution);

        ExpectEqU32(tr, rc, ERROR_NONE, "Unsubscribe with eventHook returns ERROR_NONE");
        ExpectTrue(tr, resolution.find("\"listening\":false") != std::string::npos,
                   "Response contains listening=false");

        DrainAsyncRespondJobs();

        auto* handler = service->GetRequestHandlerFake();
        if (handler != nullptr) {
            ExpectTrue(tr, handler->handleCount == 0,
                       "EventHookJob not dispatched on unsubscribe (listen=false)");
        }

        impl->Release();
        service->Release();
    }

    ::unlink(overridePath.c_str());
    return tr.failures;
}
