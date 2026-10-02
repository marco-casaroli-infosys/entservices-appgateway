#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <atomic>
#include <cstdlib>
#include <cerrno>
#include <thread>
#include <chrono>
#include <sys/stat.h>
#include <sys/types.h>

#include <core/core.h>
#include <com/IIteratorType.h>

#include <AppGatewayImplementation.h>
#include <Resolver.h>
#include <AppGateway.h>
#include <plugins/IDispatcher.h>
#include "L0Expect.hpp"
#include "L0TestTypes.hpp"
#include "ServiceMock.h"

using AppGatewayPlugin = WPEFramework::Plugin::AppGateway;
using WPEFramework::PluginHost::IPlugin;
using WPEFramework::Core::ERROR_BAD_REQUEST;
using WPEFramework::Core::ERROR_NONE;
using L0Test::ExpectEqU32;
using L0Test::ExpectTrue;
using L0Test::TestResult;

namespace {

static void ExpectNotEmpty(TestResult& tr, const std::string& s, const std::string& what) {
    if (s.empty()) {
        tr.failures++;
        std::cerr << "FAIL: " << what << " should be non-empty" << std::endl;
    }
}

// Lightweight IStringIterator implementation for Configure(paths)
class SimpleStringIterator : public WPEFramework::Exchange::IAppGatewayResolver::IStringIterator {
public:
    explicit SimpleStringIterator(const std::vector<std::string>& items)
        : _items(items), _index(0), _refCount(1) {}
    ~SimpleStringIterator() override = default;

    uint32_t AddRef() const override { return ++_refCount; }
    uint32_t Release() const override {
        const uint32_t n = --_refCount;
        if (n == 0) {
            delete this;
            return WPEFramework::Core::ERROR_DESTRUCTION_SUCCEEDED;
        }
        return WPEFramework::Core::ERROR_NONE;
    }
    void* QueryInterface(const uint32_t id) override {
        if (id == WPEFramework::Exchange::IAppGatewayResolver::IStringIterator::ID) {
            const_cast<SimpleStringIterator*>(this)->AddRef();
            return static_cast<WPEFramework::Exchange::IAppGatewayResolver::IStringIterator*>(this);
        }
        return nullptr;
    }

    bool Next(std::string& out) override {
        if (_index < _items.size()) {
            out = _items[_index++];
            return true;
        }
        return false;
    }
    bool Previous(std::string& out) override {
        if (_index == 0) {
            return false;
        }
        --_index;
        out = _items[_index];
        return true;
    }
    void Reset(const uint32_t position) override {
        if (position == 0) {
            _index = 0;
        } else if (position > _items.size()) {
            _index = static_cast<uint32_t>(_items.size());
        } else {
            _index = position;
        }
    }
    bool IsValid() const override {
        return (_index > 0) && (_index <= _items.size());
    }
    uint32_t Count() const override {
        return static_cast<uint32_t>(_items.size());
    }
    std::string Current() const override {
        if (!IsValid()) {
            return std::string();
        }
        return _items[_index - 1];
    }

private:
    std::vector<std::string> _items;
    uint32_t _index;
    mutable std::atomic<uint32_t> _refCount;
};

static bool EnsureDir(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    // Try to create
    if (mkdir(path.c_str(), 0777) == 0) {
        return true;
    }
    // If parent missing, create parent then retry (simple "mkdir -p" behavior for one level)
    // Find last slash
    const auto pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        const std::string parent = path.substr(0, pos);
        if (!parent.empty() && EnsureDir(parent)) {
            return (mkdir(path.c_str(), 0777) == 0) || (errno == EEXIST);
        }
    }
    return (errno == EEXIST);
}

static bool WriteTextFile(const std::string& path, const std::string& content) {
    const auto slashPos = path.find_last_of('/');
    if (slashPos != std::string::npos) {
        const std::string dir = path.substr(0, slashPos);
        if (!EnsureDir(dir)) {
            std::cerr << "ERROR: Unable to create directory for " << path << std::endl;
            return false;
        }
    }
    std::ofstream ofs(path, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
        std::cerr << "ERROR: Unable to open file for write: " << path << std::endl;
        return false;
    }
    ofs << content;
    return true;
}

// Sanitize an environment variable intended to be used as a file/directory
// path. Rejects values that are null/empty, non-absolute, or contain "..".
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

static std::string ComputeBaseResolutionsPathFromThisFile() {
    // Prefer env var if provided. It may point to either file or directory.
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

    // This repository’s authoritative base file lives under:
    //   <repo-root>/AppGateway/resolutions/resolution.base.json
    //
    // Compute <repo-root> from this test file path:
    //   <repo-root>/Tests/L0Tests/AppGateway/Resolver_Configure_And_ResolveTests.cpp
    const std::string f = __FILE__;
    const std::string marker = "/Tests/L0Tests/AppGateway/";
    const auto pos = f.rfind(marker);
    if (pos != std::string::npos) {
        const std::string repoRoot = f.substr(0, pos);
        return repoRoot + "/AppGateway/resolutions/resolution.base.json";
    }

    // Last resort: relative path (works when executing from repo root).
    return "AppGateway/resolutions/resolution.base.json";
}

// Build a minimal context for direct Resolve() calls
static WPEFramework::Exchange::GatewayContext MakeContext() {
    WPEFramework::Exchange::GatewayContext ctx;
    ctx.requestId = 1234;
    ctx.connectionId = 42;
    ctx.appId = "com.example.test";
    return ctx;
}

struct PluginAndService {
    L0Test::ServiceMock* service { nullptr };
    WPEFramework::PluginHost::IPlugin* plugin { nullptr };

    explicit PluginAndService(const L0Test::ServiceMock::Config& cfg = L0Test::ServiceMock::Config())
        : service(new L0Test::ServiceMock(cfg, true))
        , plugin(WPEFramework::Core::Service<AppGatewayPlugin>::Create<IPlugin>()) {
    }
    ~PluginAndService() {
        if (plugin != nullptr) { plugin->Release(); plugin = nullptr; }
        if (service != nullptr) { service->Release(); service = nullptr; }
    }
};

static void DrainAsyncRespondJobs() {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

} // namespace

// PUBLIC_INTERFACE
uint32_t Test_Resolver_Configure_WithBaseOnly_LoadsOK() {
    /** Configure resolver (mock) with base-only path and resolve a known method. Expect success. */
    TestResult tr;

    PluginAndService ps;

    // Initialize plugin (registers JSON-RPC resolve and instantiates mock resolver)
    const std::string initResult = ps.plugin->Initialize(ps.service);
    ExpectTrue(tr, initResult.empty(), "Initialize() returns empty string on success");

    // Obtain the resolver interface directly and call Configure(paths)
    auto* resolver = static_cast<WPEFramework::Exchange::IAppGatewayResolver*>(
        ps.plugin->QueryInterface(WPEFramework::Exchange::IAppGatewayResolver::ID));

    ExpectTrue(tr, resolver != nullptr, "IAppGatewayResolver available via QueryInterface(ID)");
    if (resolver != nullptr) {
        std::vector<std::string> paths;
        paths.emplace_back(ComputeBaseResolutionsPathFromThisFile());
        SimpleStringIterator* it = new SimpleStringIterator(paths);
        const uint32_t cfgRc = resolver->Configure(it);
        it->Release();

        ExpectEqU32(tr, cfgRc, ERROR_NONE, "Configure(base-only) returns ERROR_NONE");

        // Resolve a known method from the base file; mock resolver returns success for general methods
        std::string result;
        const auto ctx = MakeContext();
        const uint32_t rc = resolver->Resolve(ctx, "org.rdk.AppGateway", "device.name", "{}", result);
        ExpectTrue(tr, (rc == ERROR_NONE) || (rc == WPEFramework::Core::ERROR_GENERAL),
                   "Resolve known method returns ERROR_NONE/ERROR_GENERAL depending on request-handler availability");
        resolver->Release();
    }

    DrainAsyncRespondJobs();
    ps.plugin->Deinitialize(ps.service);
    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_Resolver_Configure_WithOverride_TakesPrecedence() {
    /** Load base resolutions, then override with a test overlay; the override alias should win (last-wins). */
    TestResult tr;

    // Use the internal Resolver directly to validate overlay precedence.
    WPEFramework::Plugin::Resolver resolver(nullptr);

    const std::string basePath = ComputeBaseResolutionsPathFromThisFile();
    bool ok = resolver.LoadConfig(basePath);
    ExpectTrue(tr, ok, "LoadConfig(base) succeeds");

    // Create a small override for device.name to point to a different alias
    const std::string overrideDir = "l0test/config";
    const std::string overridePath = overrideDir + std::string("/test.resolutions.override.json");
    const std::string overrideContent = R"JSON(
{
  "resolutions": {
    "device.name": { "alias": "org.rdk.OverridePlugin.getName", "useComRpc": false }
  }
}
)JSON";
    ExpectTrue(tr, EnsureDir(overrideDir), "Ensure l0test/config path exists");
    ExpectTrue(tr, WriteTextFile(overridePath, overrideContent), "Write override JSON");

    ok = resolver.LoadConfig(overridePath);
    ExpectTrue(tr, ok, "LoadConfig(override) succeeds");

    const std::string resolvedAlias = resolver.ResolveAlias("device.name");
    ExpectTrue(tr, resolvedAlias == "org.rdk.OverridePlugin.getName",
               "Override precedence: alias for device.name is org.rdk.OverridePlugin.getName");

    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_Resolver_Resolve_UnknownMethod_ReturnsNotFound() {
    /** Use real AppGatewayImplementation to configure with base, then resolve an unknown method.
     * Implementation returns a non-OK error (mapped as NotSupported/General) and payload may be empty or an error object.
     */
    TestResult tr;

    // Per-test isolation: new AppGatewayImplementation instance each time.
    WPEFramework::Exchange::IAppGatewayResolver* impl =
        WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");
    if (impl == nullptr) {
        return tr.failures;
    }

    // Heap ServiceMock: AppGatewayImplementation can queue async jobs.
    auto* service = new L0Test::ServiceMock(L0Test::ServiceMock::Config(), true);

    // Guard: IConfiguration::Configure(&shell) must succeed before continuing.
    bool preconditionsOk = true;
    auto configIfc = static_cast<WPEFramework::Exchange::IConfiguration*>(impl->QueryInterface(WPEFramework::Exchange::IConfiguration::ID));
    if (configIfc != nullptr) {
        const uint32_t shellRc = configIfc->Configure(service);
        configIfc->Release();
        ExpectEqU32(tr, shellRc, ERROR_NONE, "IConfiguration::Configure(&shell) returns ERROR_NONE");
        preconditionsOk = (shellRc == ERROR_NONE);
    } else {
        tr.failures++;
        std::cerr << "FAIL: IConfiguration interface not available on AppGatewayImplementation" << std::endl;
        preconditionsOk = false;
    }

    if (preconditionsOk) {
        // Guard: Configure(paths) must succeed before Resolve().
        std::vector<std::string> paths{ ComputeBaseResolutionsPathFromThisFile() };
        SimpleStringIterator* it = new SimpleStringIterator(paths);
        const uint32_t cfgRc = impl->Configure(it);
        it->Release();
        ExpectEqU32(tr, cfgRc, ERROR_NONE, "Explicit Configure(base) returns ERROR_NONE");
        preconditionsOk = (cfgRc == ERROR_NONE);
    }

    if (preconditionsOk) {
        // Resolve a method that is not present to trigger NotSupported/General error
        std::string result;
        const auto ctx = MakeContext();
        const uint32_t rc = impl->Resolve(ctx, "org.rdk.AppGateway", "no.such.method", "{}", result);

        // Accept any non-OK (implementation-specific mapping).
        if (rc == ERROR_NONE) {
            tr.failures++;
            std::cerr << "FAIL: Unknown method returned ERROR_NONE unexpectedly" << std::endl;
        }
    } else {
        std::cerr << "NOTE: Skipping Resolve() because preconditions (Configure(&shell) / Configure(paths)) failed." << std::endl;
    }

    DrainAsyncRespondJobs();
    impl->Release();
    service->Release();
    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_Resolver_Resolve_MalformedParams_ReturnsBadRequest() {
    /** Call an event method with malformed params; expect ERROR_BAD_REQUEST from AppGatewayImplementation. */
    TestResult tr;

    WPEFramework::Exchange::IAppGatewayResolver* impl =
        WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");
    if (impl == nullptr) {
        return tr.failures;
    }

    auto* service = new L0Test::ServiceMock(L0Test::ServiceMock::Config(), true);

    bool preconditionsOk = true;
    auto configIfc = static_cast<WPEFramework::Exchange::IConfiguration*>(impl->QueryInterface(WPEFramework::Exchange::IConfiguration::ID));
    if (configIfc != nullptr) {
        const uint32_t shellRc = configIfc->Configure(service);
        configIfc->Release();
        ExpectEqU32(tr, shellRc, ERROR_NONE, "IConfiguration::Configure(&shell) returns ERROR_NONE");
        preconditionsOk = (shellRc == ERROR_NONE);
    } else {
        tr.failures++;
        std::cerr << "FAIL: IConfiguration interface not available on AppGatewayImplementation" << std::endl;
        preconditionsOk = false;
    }

    if (preconditionsOk) {
        // Configure with base so HasEvent() mapping for localization.onLanguageChanged is present
        std::vector<std::string> paths{ ComputeBaseResolutionsPathFromThisFile() };
        SimpleStringIterator* it = new SimpleStringIterator(paths);
        const uint32_t cfgRc = impl->Configure(it);
        it->Release();
        ExpectEqU32(tr, cfgRc, ERROR_NONE, "Explicit Configure(base) returns ERROR_NONE");
        preconditionsOk = (cfgRc == ERROR_NONE);
    }

    if (preconditionsOk) {
        // Use an event method (from base config) and pass malformed params string
        std::string result;
        const auto ctx = MakeContext();
        const uint32_t rc = impl->Resolve(ctx, "org.rdk.AppGateway", "localization.onLanguageChanged", "{ this is not valid json }", result);

        ExpectEqU32(tr, rc, ERROR_BAD_REQUEST, "Malformed params => ERROR_BAD_REQUEST");
    } else {
        std::cerr << "NOTE: Skipping Resolve() because preconditions (Configure(&shell) / Configure(paths)) failed." << std::endl;
    }

    DrainAsyncRespondJobs();
    impl->Release();
    service->Release();
    return tr.failures;
}

// PUBLIC_INTERFACE
uint32_t Test_Resolver_Configure_InvalidJson_ReturnsError() {
    /** Provide an invalid JSON path to Configure(paths) and assert it fails (non-OK). */
    TestResult tr;

    WPEFramework::Exchange::IAppGatewayResolver* impl =
        WPEFramework::Core::Service<WPEFramework::Plugin::AppGatewayImplementation>::Create<WPEFramework::Exchange::IAppGatewayResolver>();
    ExpectTrue(tr, impl != nullptr, "Create AppGatewayImplementation instance");
    if (impl == nullptr) {
        return tr.failures;
    }

    // Heap shell for lifetime safety.
    auto* service = new L0Test::ServiceMock(L0Test::ServiceMock::Config(), true);

    bool preconditionsOk = true;
    auto configIfc = static_cast<WPEFramework::Exchange::IConfiguration*>(impl->QueryInterface(WPEFramework::Exchange::IConfiguration::ID));
    if (configIfc != nullptr) {
        const uint32_t shellRc = configIfc->Configure(service);
        configIfc->Release();
        ExpectEqU32(tr, shellRc, ERROR_NONE, "IConfiguration::Configure(&shell) returns ERROR_NONE");
        preconditionsOk = (shellRc == ERROR_NONE);
    } else {
        tr.failures++;
        std::cerr << "FAIL: IConfiguration interface not available on AppGatewayImplementation" << std::endl;
        preconditionsOk = false;
    }

    const std::string dir = "l0test/config";
    const std::string invalidPath = dir + std::string("/invalid.resolutions.json");
    ExpectTrue(tr, EnsureDir(dir), "Ensure l0test/config path exists");
    // Write invalid JSON
    ExpectTrue(tr, WriteTextFile(invalidPath, "{ invalid json"), "Write invalid JSON config");

    if (preconditionsOk) {
        std::vector<std::string> paths{ invalidPath };
        SimpleStringIterator* it = new SimpleStringIterator(paths);
        const uint32_t cfgRc = impl->Configure(it);
        it->Release();

        if (cfgRc == ERROR_NONE) {
            tr.failures++;
            std::cerr << "FAIL: Configure(invalid json) unexpectedly returned ERROR_NONE" << std::endl;
        }
    } else {
        std::cerr << "NOTE: Skipping Configure(invalid json) because Configure(&shell) failed." << std::endl;
    }

    DrainAsyncRespondJobs();
    impl->Release();
    service->Release();
    return tr.failures;
}

// ---------------------------------------------------------------------------
// Resolver gap tests — cover 26 uncovered lines in Resolver.cpp
// These create Resolver directly (not via AppGatewayImplementation) to exercise
// internal methods that are not reachable through the existing Configure/Resolve
// L0 paths.
// ---------------------------------------------------------------------------

// Covers: Resolver::ClearResolutions() body (lines 174-175)
uint32_t Test_Resolver_ClearResolutions()
{
    using WPEFramework::Plugin::Resolver;
    TestResult tr;

    // ClearResolutions does not use mService; pass nullptr.
    Resolver r(nullptr);

    const std::string base = ComputeBaseResolutionsPathFromThisFile();
    const bool loaded = r.LoadConfig(base);
    if (!loaded) {
        std::cerr << "NOTE: Skipping Test_Resolver_ClearResolutions (base resolutions not found)" << std::endl;
        return tr.failures;
    }

    ExpectTrue(tr, r.IsConfigured(), "IsConfigured() == true after LoadConfig");
    r.ClearResolutions();  // covers lines 174-175
    ExpectTrue(tr, !r.IsConfigured(), "IsConfigured() == false after ClearResolutions");
    return tr.failures;
}

// Covers: LoadConfig — file not found early return (lines 55-56)
uint32_t Test_Resolver_LoadConfig_FileNotFound()
{
    using WPEFramework::Plugin::Resolver;
    TestResult tr;

    Resolver r(nullptr);
    const bool ok = r.LoadConfig("/tmp/this_file_does_not_exist_l0test_agw.json");
    ExpectTrue(tr, !ok, "LoadConfig returns false when file does not exist");
    return tr.failures;
}

// Covers: LoadConfig — valid JSON but missing "resolutions" key (lines 136-141)
uint32_t Test_Resolver_LoadConfig_NoResolutionsKey()
{
    using WPEFramework::Plugin::Resolver;
    TestResult tr;

    const std::string path = "/tmp/agw_l0test_noresolutions.json";
    ExpectTrue(tr, WriteTextFile(path, "{\"other\": {}}"), "Write JSON without resolutions key");

    Resolver r(nullptr);
    const bool ok = r.LoadConfig(path);
    ExpectTrue(tr, !ok, "LoadConfig returns false when JSON has no 'resolutions' key");

    ::unlink(path.c_str());
    return tr.failures;
}

// Covers: CallThunderPlugin — null service (lines 211-212) and empty alias (lines 217-218)
uint32_t Test_Resolver_CallThunderPlugin_NullService_EmptyAlias()
{
    using WPEFramework::Plugin::Resolver;
    TestResult tr;

    // null service → mService == nullptr → early return ERROR_GENERAL (lines 211-212)
    {
        Resolver r(nullptr);
        std::string response;
        const auto rc = r.CallThunderPlugin("org.rdk.UserSettings.getAudioDescription", "{}", response);
        ExpectEqU32(tr, rc, WPEFramework::Core::ERROR_GENERAL,
                    "CallThunderPlugin with null service returns ERROR_GENERAL");
    }

    // non-null service but empty alias → early return ERROR_GENERAL (lines 217-218)
    // Note: Resolver::~Resolver() calls mService->Release(), so we must not
    // call svc->Release() ourselves — doing so would double-free the object.
    {
        L0Test::ServiceMock::Config cfg;
        auto* svc = new L0Test::ServiceMock(cfg, true);
        {
            Resolver r(svc);
            std::string response;
            const auto rc = r.CallThunderPlugin("", "{}", response);
            ExpectEqU32(tr, rc, WPEFramework::Core::ERROR_GENERAL,
                        "CallThunderPlugin with empty alias returns ERROR_GENERAL");
        } // ~Resolver calls svc->Release() → refcount 0 → deleted
    }
    return tr.failures;
}

// Covers: key-not-found return paths in HasEvent / HasIncludeContext /
//         HasComRpcRequestSupport / IsVersionedEvent / HasPermissionGroup
//         (lines 264, 279, 290, 301, 314)
uint32_t Test_Resolver_LookupMissingKey()
{
    using WPEFramework::Plugin::Resolver;
    TestResult tr;

    Resolver r(nullptr);
    const std::string base = ComputeBaseResolutionsPathFromThisFile();
    if (!r.LoadConfig(base)) {
        std::cerr << "NOTE: Skipping Test_Resolver_LookupMissingKey (base resolutions not found)" << std::endl;
        return tr.failures;
    }

    const std::string missing = "this.key.definitely.does.not.exist";

    // HasEvent — key not found → return false (line 264)
    ExpectTrue(tr, !r.HasEvent(missing), "HasEvent returns false for unknown key");

    // HasIncludeContext — key not found → return false (line 279)
    WPEFramework::Core::JSON::Variant dummy;
    ExpectTrue(tr, !r.HasIncludeContext(missing, dummy), "HasIncludeContext returns false for unknown key");

    // HasComRpcRequestSupport — key not found → return false (line 290)
    ExpectTrue(tr, !r.HasComRpcRequestSupport(missing), "HasComRpcRequestSupport returns false for unknown key");

    // IsVersionedEvent — key not found → return false (line 301)
    ExpectTrue(tr, !r.IsVersionedEvent(missing), "IsVersionedEvent returns false for unknown key");

    // HasPermissionGroup — key not found → return false (line 314)
    std::string pgroup;
    ExpectTrue(tr, !r.HasPermissionGroup(missing, pgroup), "HasPermissionGroup returns false for unknown key");

    return tr.failures;
}

// Covers: Resolver::HasEventHook() — both present and absent cases
uint32_t Test_Resolver_HasEventHook_PresentAndAbsent()
{
    using WPEFramework::Plugin::Resolver;
    TestResult tr;

    const std::string configPath = "/tmp/agw_l0test_eventhook.json";
    const std::string configJson = R"JSON(
{
  "resolutions": {
    "lifecycle2.onstatechanged": {
      "alias": "org.rdk.AppGatewayCommon",
      "event": "Lifecycle2.onStateChanged",
      "eventHook": "lifecycle.ready"
    },
    "localization.onlanguagechanged": {
      "alias": "org.rdk.AppGatewayCommon",
      "event": "Localization.onLanguageChanged"
    }
  }
}
)JSON";

    ExpectTrue(tr, WriteTextFile(configPath, configJson), "Write eventHook config JSON");

    Resolver r(nullptr);
    const bool loaded = r.LoadConfig(configPath);
    ExpectTrue(tr, loaded, "LoadConfig with eventHook field succeeds");

    // Positive: key has eventHook configured
    std::string hookMethod;
    const bool hasHook = r.HasEventHook("lifecycle2.onstatechanged", hookMethod);
    ExpectTrue(tr, hasHook, "HasEventHook returns true for key with eventHook");
    ExpectTrue(tr, hookMethod == "lifecycle.ready",
               "HasEventHook outputs expected hook method 'lifecycle.ready'");

    // Negative: key exists but has no eventHook field
    std::string emptyHook;
    const bool noHook = r.HasEventHook("localization.onlanguagechanged", emptyHook);
    ExpectTrue(tr, !noHook, "HasEventHook returns false for key without eventHook");
    ExpectTrue(tr, emptyHook.empty(), "hookMethod is empty when HasEventHook returns false");

    // Negative: key does not exist at all
    std::string missingHook;
    ExpectTrue(tr, !r.HasEventHook("no.such.method", missingHook),
               "HasEventHook returns false for unknown key");

    ::unlink(configPath.c_str());
    return tr.failures;
}
